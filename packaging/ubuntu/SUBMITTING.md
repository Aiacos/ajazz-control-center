# Distributing AJAZZ Control Center on Ubuntu (Launchpad PPA)

This directory holds the **Debian source packaging** for `ajazz-control-center`
targeting Ubuntu via a [Launchpad **Personal Package Archive
(PPA)**](https://documentation.ubuntu.com/launchpad/user/reference/packaging/ppas/ppa/).

A PPA is the realistic self-service route: you upload a *signed source package*,
Launchpad's build farm compiles it **from source** for each Ubuntu series you
target, and users install it with `add-apt-repository` + `apt`. Launchpad does
**not** accept pre-built `.deb` files — it only builds sources it compiles
itself.

The `debian/` directory here is a **build-from-source** recipe (analogous to the
AUR `PKGBUILD` and the Flatpak/Homebrew manifests): `debhelper` drives CMake +
Ninja against the distro's own Qt 6 / hidapi / nlohmann-json `-dev` packages.

> All commands below run on an **Ubuntu** host (or a `noble` container/chroot).
> They are NOT run from this repo's CI and the resulting artifacts MUST NOT be
> committed here — the PPA is a separate Launchpad-side service.

References (the sources of truth):

- [Launchpad PPA reference](https://documentation.ubuntu.com/launchpad/user/reference/packaging/ppas/ppa/)
- [Building a source package (Launchpad)](https://documentation.ubuntu.com/launchpad/user/reference/packaging/ppas/building-a-source-package/)
- [Upload a package to a PPA (Launchpad)](https://documentation.ubuntu.com/launchpad/user/how-to/packaging/ppa-package-upload/)
- [Debian New Maintainers' Guide - debian/ layout](https://www.debian.org/doc/manuals/maint-guide/dreq.en.html)
- [debhelper(7)](https://manpages.debian.org/testing/debhelper/debhelper.7.en.html)
- [Debian source format "3.0 (quilt)"](https://www.debian.org/doc/manuals/debmake-doc/ch05.en.html)

______________________________________________________________________

## What's in `debian/`

| File                      | Purpose                                                                                                                                                                                           |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `control`                 | Source + Binary stanzas; `Build-Depends` (debhelper-compat 13, cmake, ninja-build, Qt 6 `-dev`, libhidapi-dev, etc.) and runtime `Depends` (`${shlibs:Depends}`, `${misc:Depends}`, QML modules). |
| `rules`                   | `dh` sequencer with `--buildsystem=cmake+ninja`; passes the project flags (tests OFF, WERROR OFF, `AJAZZ_USE_SYSTEM_DEPS=ON`).                                                                    |
| `changelog`               | `0.1.0-1~noble1`, targeting the `noble` series.                                                                                                                                                   |
| `copyright`               | Machine-readable DEP-5, GPL-3.0-or-later.                                                                                                                                                         |
| `source/format`           | `3.0 (quilt)` - the standard non-native format.                                                                                                                                                   |
| `*.install`               | Selects the binary, udev rule, desktop entry, AppStream metainfo and hicolor icons into the package.                                                                                              |
| `*.postinst` / `*.postrm` | Reload + retrigger udev so the `uaccess` rule applies without a reboot.                                                                                                                           |

> **No `debian/compat` file.** Modern debhelper expresses the compat level via
> `Build-Depends: debhelper-compat (= 13)` in `debian/control`. Shipping *both*
> `debian/compat` and `debhelper-compat` is an error - we use the in-control
> form only.

______________________________________________________________________

## One-time account + key setup

1. **Create a Launchpad account** at <https://launchpad.net/+login> and sign the
   [Ubuntu Code of Conduct](https://launchpad.net/codeofconduct) (required before
   you can upload).

1. **Generate (or reuse) an OpenPGP key** and publish it so Launchpad can verify
   your signatures:

   ```sh
   gpg --full-generate-key
   gpg --list-secret-keys --keyid-format long   # note the key id after the slash
   gpg --send-keys --keyserver keyserver.ubuntu.com <KEYID>
   ```

   Add the key fingerprint to your account at
   <https://launchpad.net/~/+editpgpkeys> and confirm the encrypted email
   Launchpad sends back.

1. **Register the PPA.** On your Launchpad profile choose *"Create a new PPA"*,
   name it `ajazz-control-center`. Its dput target is then
   `ppa:aiacos/ajazz-control-center`.
   (Replace `aiacos` with your actual Launchpad username if different.)

1. **Install the packaging toolchain** on your Ubuntu build host:

   ```sh
   sudo apt update
   sudo apt install devscripts debhelper dput-ng lintian build-essential
   ```

______________________________________________________________________

## First upload

The PPA builds from an upstream tarball plus this `debian/` directory. The
release already publishes a source tarball:
`https://github.com/Aiacos/ajazz-control-center/archive/refs/tags/v0.1.0.tar.gz`.

1. **Fetch and lay out the upstream source** under the Debian `_` naming
   convention (the orig tarball must be named `<pkg>_<upstream>.orig.tar.gz`):

   ```sh
   wget -O ajazz-control-center_0.1.0.orig.tar.gz \
     https://github.com/Aiacos/ajazz-control-center/archive/refs/tags/v0.1.0.tar.gz
   tar xf ajazz-control-center_0.1.0.orig.tar.gz
   cd ajazz-control-center-0.1.0
   ```

1. **Drop in this packaging directory** (copy `debian/` from this repo's
   `packaging/ubuntu/`):

   ```sh
   cp -r /path/to/ajazz-control-center/packaging/ubuntu/debian .
   ```

1. **Build the SIGNED source package.** `-S` = source-only build (Launchpad
   refuses binary uploads); `-sa` = include the `.orig.tar.gz` (mandatory for a
   brand-new package's first upload to a series):

   ```sh
   debuild -S -sa
   ```

   `debuild` signs both the `.dsc` and the `.changes` with your GPG key. If
   `clearsign failed`, pass the key explicitly:
   `debuild -S -sa -k<KEYID>`. The same email/key MUST match your Launchpad
   account.

1. **Lint before upload** (Launchpad runs its own checks, but catch issues
   early):

   ```sh
   lintian ../ajazz-control-center_0.1.0-1~noble1_source.changes
   ```

1. **Upload to the PPA:**

   ```sh
   dput ppa:aiacos/ajazz-control-center \
     ../ajazz-control-center_0.1.0-1~noble1_source.changes
   ```

   Launchpad emails you when the source is accepted, then again when the build
   succeeds or fails (build logs are linked from the PPA's *"View package
   details"* page). Published packages appear under
   <https://launchpad.net/~aiacos/+archive/ubuntu/ajazz-control-center>.

______________________________________________________________________

## What users run

Once the PPA build is published:

```sh
sudo add-apt-repository ppa:aiacos/ajazz-control-center
sudo apt update
sudo apt install ajazz-control-center
```

The udev rule ships in the package, so `/dev/hidraw*` access works after a
device replug (the `postinst` retriggers udev for already-connected devices).

______________________________________________________________________

## Targeting multiple Ubuntu series

A given source upload builds for **one** series (set by the `changelog`
distribution field). To support, e.g., both `noble` (24.04) and `oracular`
(24.10), upload one source package per series with a series-suffixed version so
each is distinct and upgradable:

- `0.1.0-1~noble1` -> `noble`
- `0.1.0-1~oracular1` -> `oracular`

The `~noble1` / `~oracular1` suffix sorts **older** than the plain `0.1.0-1`
(`~` is the lowest-sorting character in `dpkg --compare-versions`), and keeps the
per-series builds from colliding. Bump only the changelog stanza (distribution +
version suffix) for each series; the rest of `debian/` is identical.

Tip: `backportpackage` (from `ubuntu-dev-tools`) automates rebuilding one source
for several series.

______________________________________________________________________

## Per-release update checklist

When a new upstream tag (e.g. `v0.2.0`) ships:

1. **Refresh the orig tarball** to the new tag and rename it
   `ajazz-control-center_0.2.0.orig.tar.gz`.

1. **Add a new `debian/changelog` stanza** (use `dch`):

   ```sh
   dch -v 0.2.0-1~noble1 --distribution noble "Update to upstream 0.2.0."
   ```

   For each additional series, add a stanza with the matching version suffix
   (`~oracular1`, ...).

1. **Re-check dependencies.** If upstream adds a Qt module or other lib, update
   `Build-Depends` / `Depends` in `debian/control` in the same change.

1. **Rebuild + relint + reupload:**

   ```sh
   debuild -S -sa        # -sa on the first upload of a new upstream version
   lintian ../ajazz-control-center_0.2.0-1~noble1_source.changes
   dput ppa:aiacos/ajazz-control-center \
     ../ajazz-control-center_0.2.0-1~noble1_source.changes
   ```

   For a packaging-only re-spin of the *same* upstream version, bump the Debian
   revision (`0.2.0-2~noble1`) and use `-sd` (omit the orig tarball, since it is
   already on Launchpad) instead of `-sa`.

______________________________________________________________________

## The official Ubuntu archive (much harder - separate route)

Getting `ajazz-control-center` into the **official Ubuntu archive** (so it
installs from `universe` with no PPA) is a substantially larger effort and is
**not** the PPA path above:

- Ubuntu pulls the vast majority of packages **from Debian**. The expected route
  is to first get the package into [Debian](https://www.debian.org/doc/manuals/maint-guide/),
  typically via the [mentors.debian.net](https://mentors.debian.net/) sponsorship
  process and a Debian Developer upload; it then auto-syncs into Ubuntu.
- Promotion from `universe` toward `main`, or any new binary package in `main`,
  requires a [Main Inclusion Review (MIR)](https://wiki.ubuntu.com/MainInclusionProcess)
  covering security, maintenance commitment, build reproducibility and quality.
- This means ongoing maintainer obligations (security updates, transitions, RC
  bug response) that a PPA does not.

For a single-maintainer hobby project, the **PPA is the right distribution
mechanism**; pursue the Debian-first / MIR route only if there is demand and a
willing long-term maintainer.
