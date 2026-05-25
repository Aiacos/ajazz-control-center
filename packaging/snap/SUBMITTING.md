<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Building and submitting the AJAZZ Control Center snap

This document covers the full lifecycle for the **Snap** package: building
locally, testing the unsigned snap on your own machine, and publishing to the
Canonical **Snap Store**, including the `raw-usb` auto-connect store request.

The recipe is `packaging/snap/snapcraft.yaml`. It targets **`core24`** with the
**`kde-neon-6`** extension (Qt 6 + KDE Frameworks 6 runtime), `confinement: strict`, `grade: stable`.

> All commands below are run from `packaging/snap/` unless noted. `snapcraft`
> auto-discovers `snapcraft.yaml` in the current directory.

______________________________________________________________________

## 0. Prerequisites (one-time)

```bash
# snapd + snapcraft + an isolated build backend
sudo snap install snapcraft --classic
sudo snap install lxd            # recommended build provider on Linux
sudo lxd init --auto
# (Multipass is the alternative provider; LXD is lighter for CI.)
```

`snapcraft` builds inside an LXD/Multipass VM by default, so the host toolchain
and the project's local Qt 6.11 kit are irrelevant to the snap build — the
extension pulls the Qt 6 SDK content snap into the build environment.

______________________________________________________________________

## 1. Build the snap

```bash
cd packaging/snap
snapcraft            # build + pack -> ajazz-control-center_<version>_amd64.snap
```

Useful variants:

```bash
snapcraft pack                 # pack an already-built tree (skip rebuild)
snapcraft --verbosity=debug    # full build log when a part fails
snapcraft clean                # nuke the build VM / cached parts and start fresh
snapcraft --destructive-mode   # build directly on host (CI only; pollutes host)
```

The version is derived in `override-pull` from `git describe --tags` (e.g.
`v0.1.0` → snap version `0.1.0`), so build from a checkout that has the release
tag for a clean version string. The output filename is
`ajazz-control-center_<version>_amd64.snap`.

______________________________________________________________________

## 2. Local install + interface test (unsigned)

A locally-built snap is **unsigned** and not in the store, so it must be
installed with `--dangerous` (skips the store assertion check) and
`--devmode`-or-strict. Use strict so confinement actually matches production:

```bash
sudo snap install --dangerous ./ajazz-control-center_*.snap
```

> **Unsigned / confinement caveat.** `--dangerous` is required for any snap not
> downloaded from the store; it tells snapd to trust a snap with no signed
> assertion. Such a snap also CANNOT auto-connect any interface (auto-connect
> only applies to store-published, reviewer-approved snaps), so you must connect
> every manual interface by hand for local testing.

Connect the device + helper interfaces (none of these auto-connect, even from
the store, until a store request is granted — see §4):

```bash
sudo snap connect ajazz-control-center:raw-usb
sudo snap connect ajazz-control-center:hardware-observe
sudo snap connect ajazz-control-center:removable-media   # optional

# Verify what's wired:
snap connections ajazz-control-center
```

`raw-usb` extends snapd confinement but does **not** override Unix file
permissions on `/dev/hidraw*` / `/dev/bus/usb/*`. The host must still have the
project's udev rule installed so the node is user-readable:

```bash
sudo cp ../../resources/linux/70-ajazz.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
# then physically replug the device (uaccess ACL only applies on real replug)
```

Run it:

```bash
ajazz-control-center
# or, to see denials surface:
snap run ajazz-control-center
journalctl -f | grep DENIED      # AppArmor confinement denials, if any
```

Remove when done:

```bash
sudo snap remove ajazz-control-center
```

______________________________________________________________________

## 3. Snap Store: register and publish

### 3a. Account + login

Create a developer account at <https://snapcraft.io/store>, then:

```bash
snapcraft login
# Headless / CI: export a credentials file instead of interactive login
snapcraft export-login --snaps ajazz-control-center \
  --acls package_access,package_push,package_update,package_release - \
  > snapcraft-store-credentials
# then in CI:  export SNAPCRAFT_STORE_CREDENTIALS="$(cat snapcraft-store-credentials)"
```

### 3b. Register the name (one-time)

```bash
snapcraft register ajazz-control-center
```

Name registration is first-come; if `ajazz-control-center` is taken by an
unrelated party, file a dispute via the store. Confirm with:

```bash
snapcraft names
```

### 3c. Upload + release

Push to **edge** first, smoke-test, then promote:

```bash
# Upload and release to edge in one step:
snapcraft upload --release=edge ./ajazz-control-center_*.snap

# Install from edge on a test box and re-run the §2 interface checks:
sudo snap install --edge ajazz-control-center
sudo snap connect ajazz-control-center:raw-usb
sudo snap connect ajazz-control-center:hardware-observe

# Promote a known-good revision to stable (no rebuild):
snapcraft list-revisions ajazz-control-center        # find the revision number
snapcraft release ajazz-control-center <revision> stable

# ...or upload straight to stable once you trust the build:
snapcraft upload --release=stable ./ajazz-control-center_*.snap
```

Check status / per-channel map:

```bash
snapcraft status ajazz-control-center
```

Optional progressive (percentage) rollout of a stable revision:

```bash
snapcraft release ajazz-control-center <revision> stable --progressive 25
```

______________________________________________________________________

## 4. raw-usb auto-connect (store request)

By default the user must run `snap connect ajazz-control-center:raw-usb` once
after install — `raw-usb`, `hardware-observe` and `hidraw` are all
**manual-connect** interfaces (`auto-connect: no`) because they grant
privileged hardware access.

To make `raw-usb` connect automatically on install, file a request in the Snap
Store forum. This is reviewed by Canonical's store team:

1. Publish at least one revision (edge is fine) so the snap exists in the store.
1. Open a thread in the **store-requests** / **privileged-interfaces**
   category: <https://forum.snapcraft.io/c/store-requests> (sign in with your
   Ubuntu One / snapcraft account).
1. Title it e.g. *"Auto-connection request for `ajazz-control-center`:
   raw-usb + hardware-observe"* and justify it: this is a hardware control
   panel for a whole family of AJAZZ HID devices (VID prefixes 0300, 3151,
   0c45, 248a, 249a, 3554) that cannot use the per-device `hidraw` interface
   because `hidraw` slots match exactly one `/dev/hidrawN` node or one
   `VID:PID` pair, not a multi-VID device family.
1. Reference precedents (`arduino`, `simple-scan`, `zwave-js-ui`, `chromium`
   WebUSB) which were granted `raw-usb` auto-connect for the same reason.

Until the request is granted, the snap still works — the user just connects the
interface manually. Track the request and re-confirm with `snap connections`
after it lands.

> **Why not `hidraw`?** The snapd `hidraw` interface is intentionally
> single-device: each slot is one `/dev/hidrawN` node or one `usb-vendor` /
> `usb-product` pair. A control center for many AJAZZ models across several
> USB vendor IDs cannot enumerate the family through it, so `raw-usb` (raw
> access to all USB devices) plus `hardware-observe` (read sysfs USB topology
> for hot-plug detection) is the correct combination. The commented `hidraw`
> slot in `snapcraft.yaml` is left as an opt-in for privacy-conscious users
> who want to grant a single named device instead.

______________________________________________________________________

## 5. Per-release checklist

For every new upstream release tag:

1. [ ] Tag the release in git (`vX.Y.Z`) and check it out — `override-pull`
   derives the snap version from `git describe`.
1. [ ] Bump nothing in `snapcraft.yaml` for the version (it's auto-adopted);
   only edit it for genuine recipe changes (new dep, new interface).
1. [ ] Verify `cmake-parameters` still match `release.yml`
   (`AJAZZ_BUILD_TESTS=OFF`, `AJAZZ_ENABLE_WERROR=OFF`,
   `AJAZZ_INSTALL_UDEV_RULES=OFF`).
1. [ ] `snapcraft clean && snapcraft` — clean build to catch stale-cache drift.
1. [ ] `sudo snap install --dangerous ./*.snap` and run the §2 interface
   checks against real hardware (raw-usb + hardware-observe connected).
1. [ ] `snapcraft upload --release=edge ./*.snap`; install from edge; smoke-test.
1. [ ] `snapcraft release ajazz-control-center <revision> candidate` (optional
   gate) → then `stable`.
1. [ ] `snapcraft status ajazz-control-center` — confirm the stable channel now
   points at the new revision.
1. [ ] If this is the first store revision: complete §4 (raw-usb auto-connect
   request). For later revisions, confirm the grant still holds with
   `snap connections`.
1. [ ] Confirm the listing metadata (icon, screenshots, summary) on
   <https://snapcraft.io/ajazz-control-center> — these are managed via the
   web dashboard or `snapcraft.yaml`/store metadata, not baked into the
   revision.

______________________________________________________________________

## 6. Caveats summary

- **Unsigned local snaps** need `snap install --dangerous` and connect every
  interface by hand; they never auto-connect.
- **Strict confinement** means the device list is empty until `raw-usb` is
  connected. This is expected, not a bug.
- **raw-usb does not bypass Unix permissions** — the host still needs the
  `70-ajazz.rules` udev rule (or equivalent ACL) for `/dev/hidraw*` to be
  user-accessible. On systemd ≥ 258 the `uaccess` ACL only applies on a real
  physical replug (see project memory / `CLAUDE.md`).
- **kde-neon-6 auto-adds** desktop/wayland/x11/opengl/audio/unity7/network
  plugs; do not duplicate them in the `apps` stanza.

## References

- KDE neon extensions —
  <https://documentation.ubuntu.com/snapcraft/stable/reference/extensions/kde-neon-extensions/>
- raw-usb interface —
  <https://snapcraft.io/docs/reference/interfaces/raw-usb-interface/>
- hidraw interface —
  <https://snapcraft.io/docs/reference/interfaces/hidraw-interface/>
- Manage revisions and releases —
  <https://documentation.ubuntu.com/snapcraft/stable/how-to/publishing/manage-revisions-and-releases/>
- Build and publishing example — <https://snapcraft.io/docs/snapcraft-build-example>
- Store-requests forum category — <https://forum.snapcraft.io/c/store-requests>
