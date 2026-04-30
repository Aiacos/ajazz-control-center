# SPDX-License-Identifier: GPL-3.0-or-later
"""End-to-end tests for ``scripts/sign-plugin-manifest.py``.

Drives the CLI as a subprocess so the canonicalisation rules are
exercised exactly as a publisher would invoke them. The host-side
verifier (C++) must produce the same canonical bytes for a manifest
to roundtrip cleanly — these tests pin the canonical-form contract.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "scripts" / "sign-plugin-manifest.py"


def _minimal_manifest() -> dict:
    return {
        "UUID": "com.example.unit-test",
        "Name": "Unit-test plugin",
        "Version": "1.0.0",
        "Author": "Tests",
        "Description": "Fixture used by manifest signing tests.",
        "Icon": "icon",
        "CodePath": "main.py",
        "Actions": [
            {
                "UUID": "com.example.unit-test.act",
                "Name": "Action",
                "Icon": "icon",
                "States": [{"Image": "img"}],
            }
        ],
        "OS": [{"Platform": "linux", "MinimumVersion": "22.04"}],
        "SDKVersion": 2,
        "Software": {"MinimumVersion": "1.0"},
    }


def _run(*args: str, cwd: Path | None = None) -> subprocess.CompletedProcess:
    # S603: invocation is a fixed-path script we ship in the repo with
    # caller-controlled arguments — no untrusted input on the path.
    return subprocess.run(  # noqa: S603
        [sys.executable, str(SCRIPT), *args],
        check=False,
        capture_output=True,
        text=True,
        cwd=cwd,
    )


def test_keygen_produces_valid_keypair(tmp_path: Path) -> None:
    """``keygen`` writes priv.pem (mode 0600) + pub.pem under --out-dir."""
    out = tmp_path / "keys"
    res = _run("keygen", "--out-dir", str(out))
    assert res.returncode == 0, res.stderr
    assert (out / "priv.pem").exists()
    assert (out / "pub.pem").exists()
    # priv.pem must be mode 0600 (rw owner only) so a leaked checkout
    # doesn't leave the publisher key world-readable.
    assert (out / "priv.pem").stat().st_mode & 0o077 == 0


def test_sign_then_verify_roundtrips(tmp_path: Path) -> None:
    """A freshly-signed manifest must verify against its embedded key."""
    keys = tmp_path / "keys"
    _run("keygen", "--out-dir", str(keys))
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps(_minimal_manifest()), encoding="utf-8")

    sign = _run("sign", "--manifest", str(manifest), "--priv-key", str(keys / "priv.pem"))
    assert sign.returncode == 0, sign.stderr

    # The signer must have populated both fields with the right
    # base64 lengths (Ed25519 pubkey = 32 bytes → 44 b64 chars,
    # signature = 64 bytes → 88 b64 chars).
    signed = json.loads(manifest.read_text(encoding="utf-8"))
    signing = signed["Ajazz"]["Signing"]
    assert len(signing["Ed25519PublicKey"]) == 44
    assert len(signing["Ed25519Signature"]) == 88

    verify = _run("verify", "--manifest", str(manifest))
    assert verify.returncode == 0, verify.stderr


def test_tampered_manifest_fails_verification(tmp_path: Path) -> None:
    """Mutating any field after signing breaks verification."""
    keys = tmp_path / "keys"
    _run("keygen", "--out-dir", str(keys))
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps(_minimal_manifest()), encoding="utf-8")
    _run("sign", "--manifest", str(manifest), "--priv-key", str(keys / "priv.pem"))

    # Mutate a non-signing field: the canonical bytes change → sig fails.
    signed = json.loads(manifest.read_text(encoding="utf-8"))
    signed["Description"] = "tampered description"
    manifest.write_text(json.dumps(signed, indent=2), encoding="utf-8")

    verify = _run("verify", "--manifest", str(manifest))
    assert verify.returncode == 1
    assert "Signature is invalid" in verify.stderr


def test_unsigned_manifest_is_rejected(tmp_path: Path) -> None:
    """``verify`` on a manifest with no Signing block exits non-zero."""
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps(_minimal_manifest()), encoding="utf-8")
    verify = _run("verify", "--manifest", str(manifest))
    assert verify.returncode == 1
    assert "unsigned" in verify.stderr.lower()


def test_signature_does_not_cover_itself(tmp_path: Path) -> None:
    """Re-signing must succeed regardless of a previous signature value.

    Pins the canonicalisation rule that the Ed25519Signature field is
    excluded from the bytes-under-signature. If we accidentally
    included it, signing twice with the same key would emit different
    signatures for the same manifest content.
    """
    keys = tmp_path / "keys"
    _run("keygen", "--out-dir", str(keys))
    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps(_minimal_manifest()), encoding="utf-8")

    _run("sign", "--manifest", str(manifest), "--priv-key", str(keys / "priv.pem"))
    sig1 = json.loads(manifest.read_text())["Ajazz"]["Signing"]["Ed25519Signature"]

    _run("sign", "--manifest", str(manifest), "--priv-key", str(keys / "priv.pem"))
    sig2 = json.loads(manifest.read_text())["Ajazz"]["Signing"]["Ed25519Signature"]

    assert sig1 == sig2, "canonical-form must exclude the signature field itself"


def _public_key_of(keys_dir: Path, tmp_path: Path) -> str:
    """Sign a throwaway manifest with the keypair under ``keys_dir`` and
    return the embedded ``Ed25519PublicKey`` (base64).

    Lets a test inject a "different publisher's key" without re-rolling
    the canonicalisation logic in test code.
    """
    throwaway = tmp_path / f"throwaway-{keys_dir.name}.json"
    throwaway.write_text(json.dumps(_minimal_manifest()), encoding="utf-8")
    _run("sign", "--manifest", str(throwaway), "--priv-key", str(keys_dir / "priv.pem"))
    return json.loads(throwaway.read_text())["Ajazz"]["Signing"]["Ed25519PublicKey"]


def test_swapping_publisher_key_breaks_verification(tmp_path: Path) -> None:
    """Mutating Ed25519PublicKey alone must invalidate the signature.

    Without this property, a malicious actor who learns one valid
    signature could attach a *different* public key (their own) to
    the same manifest and have it verify under their key. The
    canonicalisation deliberately keeps the public key inside the
    signed bytes so the binding is one-to-one.
    """
    keys = tmp_path / "keys"
    _run("keygen", "--out-dir", str(keys))
    other_keys = tmp_path / "other-keys"
    _run("keygen", "--out-dir", str(other_keys))
    other_pub_b64 = _public_key_of(other_keys, tmp_path)

    manifest = tmp_path / "manifest.json"
    manifest.write_text(json.dumps(_minimal_manifest()), encoding="utf-8")
    _run("sign", "--manifest", str(manifest), "--priv-key", str(keys / "priv.pem"))

    # Replace the publisher key with a different one but keep the
    # original signature: canonical bytes change → verification fails.
    signed = json.loads(manifest.read_text(encoding="utf-8"))
    signed["Ajazz"]["Signing"]["Ed25519PublicKey"] = other_pub_b64
    manifest.write_text(json.dumps(signed), encoding="utf-8")

    verify = _run("verify", "--manifest", str(manifest))
    assert verify.returncode == 1


@pytest.mark.parametrize(
    ("subcommand", "extra_args", "expected_exit"),
    [
        ("keygen", [], 0),  # uses default --out-dir=keys (we override below)
        ("verify", ["--manifest", "/nonexistent/manifest.json"], 1),
    ],
)
def test_cli_argparse_contract(
    subcommand: str, extra_args: list[str], expected_exit: int, tmp_path: Path
) -> None:
    """Smoke-test that the CLI exits cleanly on the documented commands."""
    if subcommand == "keygen":
        extra_args = ["--out-dir", str(tmp_path / "k")]
    res = _run(subcommand, *extra_args, cwd=tmp_path)
    assert res.returncode == expected_exit, res.stderr
