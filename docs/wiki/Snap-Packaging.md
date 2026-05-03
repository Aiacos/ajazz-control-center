# Snap Packaging

How to build, test, and publish AJAZZ Control Center as a Canonical Snap.
This page is the practical companion to
[`packaging/snap/snapcraft.yaml`](https://github.com/Aiacos/ajazz-control-center/blob/main/packaging/snap/snapcraft.yaml);
it explains *why* every interface and option is the way it is, so the
recipe stays maintainable.

If you are just installing the snap, jump to
[Quick install](#quick-install). If you are a maintainer building a
release, start at [Local build](#local-build) and follow the rest top
to bottom.

> **Sibling pages:**
> [Building from Source](Building.md) ·
> [Release Process](Release-Process.md) ·
> the Flatpak recipe at
> [`packaging/flatpak/`](https://github.com/Aiacos/ajazz-control-center/blob/main/packaging/flatpak/).

## Quick install

```bash
sudo snap install ajazz-control-center
sudo snap connect ajazz-control-center:hidraw
sudo snap connect ajazz-control-center:raw-usb
```

The two `snap connect` calls are mandatory: on strict confinement,
device plugs do not auto-connect. Without them the device list shows
"No devices found" and the keypads silently fail to enumerate.

## Why both Snap and Flatpak?

The repo already ships a Flatpak manifest. Why bother with Snap?

| Distro family | Default app store | Snap reach | Flatpak reach |
|---------------|------------------|------------|---------------|
| Ubuntu 22+    | Snap Store       | first-class | sideload only |
| Fedora / RHEL | Flathub          | community   | first-class   |
| Arch / Manjaro| AUR              | community   | community     |
| Debian        | Software Center  | community   | community     |

Ubuntu Desktop ships the Snap Store as the only built-in app store and
is by a wide margin the largest single install surface for a Linux
desktop app. Shipping a Snap covers that user without forcing them to
add a Flathub remote. Both packaging formats coexist; users pick the
one their distro defaults to.

## Architecture overview

The Snap recipe has three moving parts:

```
┌──────────────────────────────────────────────────────────────┐
│ snapcraft.yaml                                               │
│  ├── apps.ajazz-control-center      ← entry point + plugs    │
│  ├── parts.ajazz-control-center     ← cmake build + deps     │
│  └── parts.plugin-resources         ← SEC-003 scripts + JSON │
└──────────────────────────────────────────────────────────────┘
        │
        ├─► uses base: core24            (Ubuntu 24.04 userland)
        ├─► uses extensions: kde-neon-6  (Qt 6.8 + KDE runtime)
        └─► confinement: strict          (interfaces gate access)
```

The **base** controls the GLIBC + GCC + system library versions the snap
runs against. `core24` is the Ubuntu 24.04 LTS userland; it gives us
GLIBC 2.39 and GCC 13 which match what `install-qt-action` produces in
CI.

The **extension** `kde-neon-6` injects Qt 6.8 + KDE Frameworks runtime
into the snap at build time. It saves us shipping our own Qt — the
extension's content snap is shared across every Qt-using snap on the
system, so the actual download is a few MB instead of a few hundred.

The **confinement** is `strict`: nothing in the snap can touch the
host filesystem, devices, or D-Bus services unless an interface
(plug) is declared and connected.

## Confinement: which interfaces and why

Every plug in `snapcraft.yaml` corresponds to a runtime capability.
Strict confinement means you list exactly what you need; nothing else
gets through.

| Plug | What it grants | Why we need it |
|------|---------------|----------------|
| `desktop` | tray icon, window display | base UI surface |
| `desktop-legacy` | X11 tray fallback | KDE Plasma, GNOME w/o AppIndicator extension |
| `wayland` | display under Wayland | modern compositors |
| `x11` | display under X11 | older compositors / Xwayland fallback |
| `opengl` | GPU acceleration | Qt Quick scene graph |
| `audio-playback` | PulseAudio / PipeWire | Plugin audio actions |
| `home` | `~/` read/write | profile JSON in `~/.config/` |
| `network` | egress | Plugin Store catalogue fetcher |
| `network-bind` | local listen sockets | future Stream-Deck-WebSocket bridge (#33) |
| `raw-usb` | `/dev/bus/usb/*` | hidapi libusb backend |
| `hidraw` | `/dev/hidraw*` | hidapi hidraw backend (Linux primary path) |
| `unity7` | StatusNotifier tray | older GNOME shells |
| `removable-media` | `/media/$USER/*` | future plugin assets on USB sticks |

The two device plugs (`raw-usb` and `hidraw`) are the only ones
**not auto-connected** — Canonical's review policy requires explicit
user consent for raw device access. The Quick install section above
shows the two `snap connect` commands the user runs once.

> **Why both `raw-usb` and `hidraw`?** The hidapi library that
> `ajazz_core` links against picks the hidraw backend on Linux when
> available (kernel-native, no userspace usbfs round-trip), but
> falls back to libusb-via-usbfs when hidraw is unavailable. Shipping
> both interfaces makes the snap robust to either path. See the udev
> rule rationale in `resources/linux/99-ajazz.rules` for the
> equivalent permission story on a non-snap install.

## QtWebEngine and the sandbox-in-snap problem

The Property Inspector pane embeds plugin-authored HTML pages via
QtWebEngine. QtWebEngine itself uses Chromium's sandbox, which on
Linux relies on `unshare(2)` namespace creation. **Snapd's strict
confinement also uses namespace creation**, and the two interact
badly: QtWebEngine sees its sandbox call fail and refuses to start
the renderer process.

There are three known workarounds:

1. **Disable the QtWebEngine sandbox at launch** —
   `QTWEBENGINE_DISABLE_SANDBOX=1` as an env var in the snap launcher.
   This is what the `kde-neon-6` extension does by default for
   QtWebEngine consumers. We rely on it; no manual override needed.

2. **Build with `--no-sandbox` baked in** — adding
   `QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox` to `apps.environment`.
   Equivalent to (1), explicit form. Use only if (1) stops working
   in a future Qt release.

3. **Fall back to the schema-driven native inspector** —
   `-DAJAZZ_BUILD_PROPERTY_INSPECTOR=OFF` at cmake configure time.
   The C++ host stays compatible with both modes; the user just
   doesn't get HTML PIs until they install the deb / flatpak.

The recipe defaults to (1). If a future Chromium update breaks the
sandbox env var, `kde-neon-6` typically gets patched within a week
and we get the fix for free.

## Local build

```bash
# 1. Install snapcraft. The classic path on Ubuntu 22.04+:
sudo snap install snapcraft --classic
sudo snap install lxd
sudo lxd init --auto

# 2. From the repo root, kick the build. snapcraft uses LXD by
#    default to spawn a clean core24 build container — slow first
#    time, fast on repeat builds (the cmake + qt cache survives).
cd /path/to/ajazz-control-center
snapcraft pack --use-lxd packaging/snap

# 3. Install the locally-built snap. `--dangerous` skips signature
#    verification (we haven't published the build yet).
sudo snap install --dangerous ./ajazz-control-center_*.snap

# 4. Connect the two device interfaces and run.
sudo snap connect ajazz-control-center:hidraw
sudo snap connect ajazz-control-center:raw-usb
ajazz-control-center
```

The build takes ~15 minutes on a cold cache (Qt fetched + cmake
configure + full C++ compile). On a warm rebuild only the changed
files recompile, ~30 seconds.

## Testing locally

```bash
# Inspect the snap's filesystem and confinement.
unsquashfs -d /tmp/ajazz-snap ajazz-control-center_*.snap
ls /tmp/ajazz-snap/usr/bin/

# Run the binary inside a confinement shell — gives you a snap-style
# environment without installing.
snap try /tmp/ajazz-snap
snap run ajazz-control-center

# Trace blocked syscalls / interface denials. AppArmor logs go to
# kernel messages on most distros.
sudo journalctl -k -f | grep -i apparmor
```

Common first-run issues and what they mean:

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Empty device list | `hidraw` not connected | `sudo snap connect ajazz-control-center:hidraw` |
| Empty Plugin Store | `network` denied | check `/var/log/syslog` for AppArmor denials |
| Tray icon missing on KDE | `desktop-legacy` not exported | usually auto-connect, sometimes `unity7` plug helps |
| HTML PI not rendering | QtWebEngine sandbox failed | confirm `QTWEBENGINE_DISABLE_SANDBOX=1` is set |
| `Failed to load Qt platform plugin "wayland"` | `wayland` plug missing | rebuild with `wayland` in the plugs list |

## CI integration

The `release.yml` workflow gains a `snap-build` job that mirrors the
Debian and macOS release pipelines:

```yaml
snap-build:
  name: Build Snap
  runs-on: ubuntu-24.04
  needs: build
  steps:
    - uses: actions/checkout@v6
    - uses: snapcore/action-build@v1
      id: build
      with:
        path: packaging/snap
    - uses: actions/upload-artifact@v7
      with:
        name: snap
        path: ${{ steps.build.outputs.snap }}
    # On tagged releases only: push to the edge channel.
    - if: startsWith(github.ref, 'refs/tags/v')
      uses: snapcore/action-publish@v1
      env:
        SNAPCRAFT_STORE_CREDENTIALS: ${{ secrets.SNAP_STORE_TOKEN }}
      with:
        snap: ${{ steps.build.outputs.snap }}
        release: edge
```

The `SNAP_STORE_TOKEN` secret is generated once by the maintainer
with `snapcraft export-login` and stored as a repository secret.

## Publishing flow

We use the standard four-channel snap release ladder:

1. **edge** — every CI build on a release tag lands here
   automatically. Reset on every push.
2. **beta** — promoted from edge after one week of soak time
   (no crash reports from the `crashlogger` snap).
3. **candidate** — promoted from beta after community testing on
   the project's Discord.
4. **stable** — promoted from candidate after a maintainer signoff.

Promote between channels with:

```bash
snapcraft release ajazz-control-center <revision> beta
snapcraft release ajazz-control-center <revision> candidate
snapcraft release ajazz-control-center <revision> stable
```

`<revision>` is shown by `snapcraft list-revisions ajazz-control-center`.

## Common pitfalls

### "snap-confine has elevated permissions"

Means the snap was installed with `--devmode` or `--classic` instead
of strict. Fix:

```bash
sudo snap remove ajazz-control-center
sudo snap install ajazz-control-center  # plain, no flag
```

### "cannot find required default-provider snap"

The `kde-neon-6` extension expects the corresponding content snap to
be installed on the host:

```bash
sudo snap install kf6-core24
```

This usually happens automatically; it's only manual on offline
installs.

### Build fails with "out of disk space"

LXD containers default to 10 GB. The full Qt + Chromium build can
push 8 GB. Bump the storage pool:

```bash
sudo lxc storage list
sudo lxc storage set default size=40GB  # or your pool name
```

### Manifest review rejection

Canonical's automated review catches policy violations before the
snap reaches `edge`. The most common ones:

- **`raw-usb` without justification** — fix by adding a manual
  review request via [snapcraft.io/store/manage](https://snapcraft.io/store/manage)
  with a one-paragraph rationale linking to the device list this app
  manages.
- **`home` plug auto-connect** — strict confinement disallows this.
  Either declare it a manual-connect (`auto-connect: false` is the
  default for `home` outside the desktop interface bundle anyway).

The full review policy lives at
[snapcraft.io/docs/snap-review-tools](https://snapcraft.io/docs/snap-review-tools).

## See also

- [`packaging/snap/snapcraft.yaml`](https://github.com/Aiacos/ajazz-control-center/blob/main/packaging/snap/snapcraft.yaml) — the recipe itself
- [`packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml`](https://github.com/Aiacos/ajazz-control-center/blob/main/packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml) — Flatpak counterpart, useful diff target when interfaces change
- [`Release-Process.md`](Release-Process.md) — overall release lifecycle
- [`Building.md`](Building.md) — building outside any container
- [Snapcraft documentation](https://snapcraft.io/docs)
- [Confinement reference](https://snapcraft.io/docs/snap-confinement)
- [Interface management](https://snapcraft.io/docs/interface-management)
