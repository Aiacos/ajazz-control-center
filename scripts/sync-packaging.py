#!/usr/bin/env python3
"""Synchronise every package-manager manifest under ``packaging/`` to a release.

Single source of truth: the GitHub release assets + their ``SHA256SUMS`` (the
file the release workflow publishes), plus the source tag tarball. Given a tag
(``vX.Y.Z``) this rewrites the version / download URL / checksum (and the MSI
ProductCode where relevant) across winget, Chocolatey, Homebrew, AUR, Flathub,
Fedora and Ubuntu. Snap needs no edit (it derives the version via ``git
describe`` at build time).

Run by ``.github/workflows/sync-packages.yml`` on ``release: published``; also
runnable locally:  ``python scripts/sync-packaging.py --tag v0.1.0``

It only edits files in-place; it never commits or pushes (the workflow does).
Missing optional inputs (e.g. ``msiinfo`` for the ProductCode) warn and leave
the existing value rather than failing the whole sync.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO = "Aiacos/ajazz-control-center"
ROOT = Path(__file__).resolve().parent.parent
PKG = ROOT / "packaging"


def log(msg: str) -> None:
    print(f"[sync] {msg}")


def warn(msg: str) -> None:
    print(f"[sync] WARNING: {msg}", file=sys.stderr)


def fetch(url: str) -> bytes:
    log(f"GET {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "ajazz-sync"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def sha256_of(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def replace_once(path: Path, pattern: str, repl: str, *, count: int = 0) -> None:
    """Regex-replace in a file; error if the pattern matched nothing."""
    text = path.read_text(encoding="utf-8")
    new, n = re.subn(pattern, repl, text, count=count, flags=re.MULTILINE)
    if n == 0:
        warn(f"{path.relative_to(ROOT)}: pattern not found: {pattern!r}")
        return
    if new != text:
        path.write_text(new, encoding="utf-8")
        log(f"updated {path.relative_to(ROOT)} ({n}x): {pattern!r}")


# --------------------------------------------------------------------------- #
# Inputs gathered once and shared across channels.
# --------------------------------------------------------------------------- #
class Release:
    def __init__(self, tag: str) -> None:
        self.tag = tag
        self.version = tag.lstrip("v")
        base = f"https://github.com/{REPO}/releases/download/{tag}"
        self.msi_url = f"{base}/ajazz-control-center-{self.version}-win64.msi"
        self.dmg_name = f"ajazz-control-center-{self.version}-Darwin.dmg"
        self.tarball_url = f"https://github.com/{REPO}/archive/refs/tags/{tag}.tar.gz"
        self._sums: dict[str, str] | None = None
        self._tarball_sha: str | None = None
        self._commit: str | None = None
        self._product_code: str | None = None

    # checksums of the published binary assets, keyed by filename
    @property
    def sums(self) -> dict[str, str]:
        if self._sums is None:
            text = fetch(
                f"https://github.com/{REPO}/releases/download/{self.tag}/SHA256SUMS"
            ).decode()
            d: dict[str, str] = {}
            for line in text.splitlines():
                parts = line.split()
                if len(parts) >= 2:
                    d[Path(parts[-1]).name] = parts[0].lower()
            self._sums = d
        return self._sums

    def asset_sha(self, suffix: str) -> str:
        for name, sha in self.sums.items():
            if name.endswith(suffix):
                return sha
        raise SystemExit(f"no asset ending in {suffix!r} in SHA256SUMS")

    @property
    def msi_sha(self) -> str:
        return self.asset_sha("win64.msi")

    @property
    def dmg_sha(self) -> str:
        return self.asset_sha("Darwin.dmg")

    @property
    def tarball_sha(self) -> str:
        if self._tarball_sha is None:
            self._tarball_sha = sha256_of(fetch(self.tarball_url))
        return self._tarball_sha

    @property
    def commit(self) -> str:
        if self._commit is None:
            out = subprocess.run(
                [
                    "git",
                    "ls-remote",
                    f"https://github.com/{REPO}.git",
                    f"refs/tags/{self.tag}^{{}}",
                ],
                capture_output=True,
                text=True,
                check=False,
            ).stdout.strip()
            if not out:  # un-annotated tag: no peeled ref
                out = subprocess.run(
                    ["git", "ls-remote", f"https://github.com/{REPO}.git", f"refs/tags/{self.tag}"],
                    capture_output=True,
                    text=True,
                    check=False,
                ).stdout.strip()
            self._commit = out.split()[0] if out else ""
            if not self._commit:
                warn(f"could not resolve commit for {self.tag}")
        return self._commit

    @property
    def product_code(self) -> str | None:
        """ProductCode from the MSI Property table (needs `msiinfo` from msitools)."""
        if self._product_code is None and shutil.which("msiinfo"):
            tmp = ROOT / ".sync-tmp.msi"
            try:
                tmp.write_bytes(fetch(self.msi_url))
                props = subprocess.run(
                    ["msiinfo", "export", str(tmp), "Property"],
                    capture_output=True,
                    text=True,
                    check=False,
                ).stdout
                m = re.search(r"^ProductCode\t(\{[0-9A-Fa-f-]+\})", props, re.MULTILINE)
                self._product_code = m.group(1).upper() if m else None
            finally:
                tmp.unlink(missing_ok=True)
        if not shutil.which("msiinfo"):
            warn("msiinfo (msitools) not available — leaving winget/choco ProductCode unchanged")
        return self._product_code


# --------------------------------------------------------------------------- #
# Per-channel updaters.
# --------------------------------------------------------------------------- #
def sync_winget(rel: Release) -> None:
    base = PKG / "winget"
    # newest existing version dir is the template
    versions = sorted((p for p in base.iterdir() if p.is_dir()), key=lambda p: p.name)
    if not versions:
        warn("no winget version dir to template from")
        return
    src = versions[-1]
    dst = base / rel.version
    if dst != src and not dst.exists():
        shutil.copytree(src, dst)
        log(f"winget: created {dst.relative_to(ROOT)} from {src.name}")
    inst = dst / "Aiacos.AjazzControlCenter.installer.yaml"
    for f in dst.glob("*.yaml"):
        replace_once(f, r"^PackageVersion: .*$", f"PackageVersion: {rel.version}")
    replace_once(inst, r"InstallerUrl: .*$", f"InstallerUrl: {rel.msi_url}")
    replace_once(inst, r"InstallerSha256: .*$", f"InstallerSha256: {rel.msi_sha.upper()}")
    if rel.product_code:
        replace_once(inst, r"ProductCode: '\{[^']*\}'", f"ProductCode: '{rel.product_code}'")
    replace_once(
        dst / "Aiacos.AjazzControlCenter.locale.en-US.yaml",
        r"ReleaseNotesUrl: .*$",
        f"ReleaseNotesUrl: https://github.com/{REPO}/releases/tag/{rel.tag}",
    )


def sync_chocolatey(rel: Release) -> None:
    nuspec = PKG / "chocolatey" / "ajazz-control-center.nuspec"
    replace_once(nuspec, r"<version>[^<]*</version>", f"<version>{rel.version}</version>")
    replace_once(
        nuspec,
        r"<releaseNotes>[^<]*</releaseNotes>",
        f"<releaseNotes>https://github.com/{REPO}/releases/tag/{rel.tag}</releaseNotes>",
    )
    replace_once(nuspec, r"/v[0-9][^/]*/resources", f"/{rel.tag}/resources")  # iconUrl tag
    inst = PKG / "chocolatey" / "tools" / "chocolateyinstall.ps1"
    replace_once(inst, r"url64bit\s*=\s*'[^']*'", f"url64bit       = '{rel.msi_url}'")
    replace_once(inst, r"checksum64\s*=\s*'[^']*'", f"checksum64     = '{rel.msi_sha.upper()}'")
    if rel.product_code:
        replace_once(
            PKG / "chocolatey" / "tools" / "chocolateyuninstall.ps1",
            r"\$productCode\s*=\s*'\{[^']*\}'",
            f"$productCode = '{rel.product_code}'",
        )


def sync_homebrew(rel: Release) -> None:
    cask = PKG / "homebrew" / "Casks" / "ajazz-control-center.rb"
    replace_once(cask, r'version "[^"]*"', f'version "{rel.version}"')
    replace_once(cask, r'sha256 "[0-9a-fA-F]{64}"', f'sha256 "{rel.dmg_sha}"')


def sync_aur(rel: Release) -> None:
    pkgbuild = PKG / "aur" / "PKGBUILD"
    replace_once(pkgbuild, r"^pkgver=.*$", f"pkgver={rel.version}")
    replace_once(pkgbuild, r"^pkgrel=.*$", "pkgrel=1")
    replace_once(pkgbuild, r"sha256sums=\('[0-9a-fA-F]+'\)", f"sha256sums=('{rel.tarball_sha}')")
    srcinfo = PKG / "aur" / ".SRCINFO"
    if srcinfo.exists():
        replace_once(srcinfo, r"pkgver = .*$", f"pkgver = {rel.version}")
        replace_once(srcinfo, r"pkgrel = .*$", "pkgrel = 1")
        # Preserve the `name::url` rename the PKGBUILD uses.
        replace_once(
            srcinfo,
            r"source = .*$",
            f"source = ajazz-control-center-{rel.version}.tar.gz::{rel.tarball_url}",
        )
        replace_once(srcinfo, r"sha256sums = .*$", f"sha256sums = {rel.tarball_sha}")


def sync_flathub(rel: Release) -> None:
    # Only the APP module is version-bumped; the hidapi/pybind11/nlohmann
    # dependency modules keep their own pinned tag+commit. Scope the edit to the
    # text AFTER the app repo URL so a blanket replace can't clobber the deps.
    man = PKG / "flathub" / "io.github.Aiacos.AjazzControlCenter.yml"
    if not man.exists():
        return
    text = man.read_text(encoding="utf-8")
    idx = text.find("Aiacos/ajazz-control-center.git")
    if idx < 0:
        warn("flathub: app module url not found")
        return
    head, tail = text[:idx], text[idx:]
    tail = re.sub(r"(\n\s*tag:\s*)v[0-9]\S*", rf"\g<1>{rel.tag}", tail, count=1)
    if rel.commit:
        tail = re.sub(r"(\n\s*commit:\s*)[0-9a-f]{40}", rf"\g<1>{rel.commit}", tail, count=1)
    new = head + tail
    if new != text:
        man.write_text(new, encoding="utf-8")
        log(f"updated {man.relative_to(ROOT)} (app module tag/commit)")


def sync_fedora(rel: Release) -> None:
    spec = PKG / "fedora" / "ajazz-control-center.spec"
    replace_once(spec, r"^Version:\s*.*$", f"Version:        {rel.version}")
    replace_once(spec, r"^Release:\s*.*$", "Release:        1%{?dist}")


def sync_ubuntu(rel: Release) -> None:
    ch = PKG / "ubuntu" / "debian" / "changelog"
    if not ch.exists():
        return
    text = ch.read_text(encoding="utf-8")
    if f"({rel.version}-1" in text.split("\n", 1)[0]:
        log("ubuntu: changelog already at this version")
        return
    entry = (
        f"ajazz-control-center ({rel.version}-1~noble1) noble; urgency=medium\n\n"
        f"  * Release {rel.version}. See https://github.com/{REPO}/releases/tag/{rel.tag}\n\n"
        " -- AJAZZ Control Center contributors <noreply@github.com>  "
        f"{subprocess.run(['date', '-R'], capture_output=True, text=True, check=False).stdout.strip()}\n\n"  # noqa: E501
    )
    ch.write_text(entry + text, encoding="utf-8")
    log("ubuntu: prepended new debian/changelog entry")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tag", required=True, help="release tag, e.g. v0.1.0")
    args = ap.parse_args()
    tag = args.tag if args.tag.startswith("v") else f"v{args.tag}"
    rel = Release(tag)
    log(f"syncing packaging to {tag} (version {rel.version})")
    log(f"MSI sha256   = {rel.msi_sha}")
    log(f"DMG sha256   = {rel.dmg_sha}")
    log(f"tarball sha  = {rel.tarball_sha}")
    log(f"tag commit   = {rel.commit or '(unknown)'}")
    log(f"ProductCode  = {rel.product_code or '(unchanged)'}")
    for name, fn in [
        ("winget", sync_winget),
        ("chocolatey", sync_chocolatey),
        ("homebrew", sync_homebrew),
        ("aur", sync_aur),
        ("flathub", sync_flathub),
        ("fedora", sync_fedora),
        ("ubuntu", sync_ubuntu),
    ]:
        log(f"--- {name} ---")
        try:
            fn(rel)
        except Exception as exc:
            warn(f"{name}: {exc}")
    log("done — review `git diff packaging/` and commit.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
