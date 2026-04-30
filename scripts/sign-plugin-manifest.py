#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sign or verify an AJAZZ Control Center plugin manifest with Ed25519.

Used by plugin publishers to attach an Ed25519 signature to a
``manifest.json``, and by CI / the host to verify a signed manifest.
The same script does both — the host's verification logic in C++
mirrors this exactly (see ``src/plugins/src/manifest_signer.cpp``).

Canonicalisation rules (must match host-side verification):

1. Parse the manifest as UTF-8 JSON.
2. Remove ``Ajazz.Signing.Ed25519Signature`` from the dict (the
   signature is computed over everything *else*, including the
   embedded public key, so a publisher cannot post-hoc swap keys).
3. Re-emit the JSON with: ``ensure_ascii=False``,
   ``sort_keys=True``, ``separators=(",", ":")`` — UTF-8 bytes,
   compact and deterministic.
4. Sign the resulting byte string with Ed25519.

Usage::

    # generate a fresh keypair (writes priv.pem + pub.pem)
    sign-plugin-manifest.py keygen --out-dir ./keys

    # sign in-place: rewrites manifest.json with the
    # Ed25519Signature + Ed25519PublicKey embedded
    sign-plugin-manifest.py sign \\
        --manifest path/to/manifest.json \\
        --priv-key keys/priv.pem

    # verify (exits 0 on success, 1 on bad signature)
    sign-plugin-manifest.py verify --manifest path/to/manifest.json
"""

from __future__ import annotations

import argparse
import base64
import json
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)


def _canonical_bytes(manifest: dict) -> bytes:
    """Return the deterministic byte string the signature covers.

    Removes ``Ajazz.Signing.Ed25519Signature`` (we cannot include the
    signature in its own message) and emits sorted-key compact JSON.
    """
    stripped = json.loads(json.dumps(manifest))  # deep copy
    ajazz = stripped.get("Ajazz", {})
    signing = ajazz.get("Signing", {})
    signing.pop("Ed25519Signature", None)
    return json.dumps(
        stripped,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _b64(b: bytes) -> str:
    return base64.standard_b64encode(b).decode("ascii")


def cmd_keygen(args: argparse.Namespace) -> int:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    priv = Ed25519PrivateKey.generate()
    pub = priv.public_key()

    priv_pem = priv.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    pub_pem = pub.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo,
    )

    priv_path = out_dir / "priv.pem"
    pub_path = out_dir / "pub.pem"
    priv_path.write_bytes(priv_pem)
    priv_path.chmod(0o600)
    pub_path.write_bytes(pub_pem)

    pub_b64 = _b64(
        pub.public_bytes(
            encoding=serialization.Encoding.Raw,
            format=serialization.PublicFormat.Raw,
        )
    )
    print(f"Generated Ed25519 keypair in {out_dir}/")
    print("  priv.pem (mode 0600)")
    print("  pub.pem")
    print("Manifest field value:")
    print(f'  "Ed25519PublicKey": "{pub_b64}"')
    return 0


def cmd_sign(args: argparse.Namespace) -> int:
    manifest_path = Path(args.manifest)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    priv = serialization.load_pem_private_key(Path(args.priv_key).read_bytes(), password=None)
    if not isinstance(priv, Ed25519PrivateKey):
        print(f"::error::{args.priv_key} is not an Ed25519 private key", file=sys.stderr)
        return 2

    pub_raw = priv.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    pub_b64 = _b64(pub_raw)

    manifest.setdefault("Ajazz", {}).setdefault("Signing", {})["Ed25519PublicKey"] = pub_b64
    manifest["Ajazz"]["Signing"].pop("Ed25519Signature", None)

    sig = priv.sign(_canonical_bytes(manifest))
    manifest["Ajazz"]["Signing"]["Ed25519Signature"] = _b64(sig)

    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"Signed {manifest_path}")
    print(f"  publisher key: {pub_b64}")
    print(f"  signature    : {_b64(sig)[:32]}...")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    manifest_path = Path(args.manifest)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    signing = manifest.get("Ajazz", {}).get("Signing", {})
    sig_b64 = signing.get("Ed25519Signature")
    pub_b64 = signing.get("Ed25519PublicKey")
    if not sig_b64 or not pub_b64:
        print("::error::Manifest is unsigned (no Ed25519Signature/PublicKey)", file=sys.stderr)
        return 1
    try:
        sig = base64.standard_b64decode(sig_b64)
        pub_raw = base64.standard_b64decode(pub_b64)
    except (ValueError, base64.binascii.Error) as exc:
        print(f"::error::Bad base64: {exc}", file=sys.stderr)
        return 1

    pub = Ed25519PublicKey.from_public_bytes(pub_raw)
    try:
        pub.verify(sig, _canonical_bytes(manifest))
    except InvalidSignature:
        print(f"::error::Signature is invalid for {manifest_path}", file=sys.stderr)
        return 1

    print(f"OK {manifest_path}")
    print(f"  publisher key: {pub_b64}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_kg = sub.add_parser("keygen", help="generate a publisher keypair")
    p_kg.add_argument("--out-dir", default="keys")

    p_sg = sub.add_parser("sign", help="sign a manifest in-place")
    p_sg.add_argument("--manifest", required=True)
    p_sg.add_argument("--priv-key", required=True)

    p_vr = sub.add_parser("verify", help="verify a signed manifest")
    p_vr.add_argument("--manifest", required=True)

    args = parser.parse_args(argv)
    return {"keygen": cmd_keygen, "sign": cmd_sign, "verify": cmd_verify}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
