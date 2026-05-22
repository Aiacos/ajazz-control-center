## AJAZZ Control Center

Open, cross-platform control center for AJAZZ & Mirabox HID devices — stream-deck-style key panels, VIA-compatible keyboards, and AJ-line mice. Pick your platform below, download the matching asset from the **Assets** section at the bottom of this release, and follow the install steps.

> All assets are accompanied by a `SHA256SUMS` file and SLSA build-provenance attestations. Verify a download with `sha256sum -c SHA256SUMS` (Linux/macOS) or `gh attestation verify <file> --owner Aiacos`.

### Downloads

| Platform | File (in Assets below) | Install |
| --- | --- | --- |
| **Linux** (Debian / Ubuntu) | `ajazz-control-center-*_amd64.deb` | `sudo apt install ./ajazz-control-center-*_amd64.deb` |
| **Linux** (Fedora / RHEL / openSUSE) | `ajazz-control-center-*.x86_64.rpm` | `sudo dnf install ./ajazz-control-center-*.x86_64.rpm` |
| **Linux** (any distro) | `ajazz-control-center.flatpak` | `flatpak install --user ./ajazz-control-center.flatpak` |
| **Windows 10/11** | `ajazz-control-center-*-win64.msi` | Double-click the `.msi` and follow the wizard |
| **Windows** (portable) | `ajazz-control-center-*-win64.zip` | Unzip anywhere and run `AjazzControlCenter.exe` |
| **macOS** (Universal: Intel + Apple Silicon) | `ajazz-control-center-*-Darwin.dmg` | Open the `.dmg`, drag the app to **Applications** |

*(Exact version/architecture suffixes vary per release — match the prefix and extension.)*

### Install instructions

**Linux — .deb (Debian, Ubuntu, Mint, Pop!\_OS)**
```bash
sudo apt install ./ajazz-control-center-*_amd64.deb
# or, without apt:
sudo dpkg -i ./ajazz-control-center-*_amd64.deb && sudo apt-get -f install
```

**Linux — .rpm (Fedora, RHEL, openSUSE)**
```bash
sudo dnf install ./ajazz-control-center-*.x86_64.rpm
# openSUSE:  sudo zypper install ./ajazz-control-center-*.x86_64.rpm
```

**Linux — Flatpak (any distro)**
```bash
flatpak install --user ./ajazz-control-center.flatpak
flatpak run io.github.Aiacos.AjazzControlCenter
```

> **Linux device access:** the `.deb`/`.rpm` packages install a udev rule
> (`70-ajazz.rules`) that grants your user access to AJAZZ devices via
> `uaccess` — no group membership needed. **Replug the device** (or reboot)
> once after installing so the ACL is applied. On Flatpak the same rule must
> be present on the host; if a device isn't detected, replug it. If access
> still fails on a dev machine, a transient fix is
> `sudo setfacl -m u:$(id -u):rw /dev/hidraw*`.

**Windows — .msi installer**
1. Download the `*-win64.msi` asset.
2. Double-click it and follow the installer wizard.
3. If the unsigned build triggers SmartScreen, click **More info → Run anyway**. (Releases built with the project's Authenticode certificate are signed and won't prompt.)

**Windows — portable .zip**
Unzip anywhere and run `AjazzControlCenter.exe`. No installation required.

**macOS — .dmg (Universal)**
1. Open the `*-Darwin.dmg` asset.
2. Drag **AJAZZ Control Center** into your **Applications** folder.
3. If the build is unsigned/un-notarized, macOS Gatekeeper may block first launch: **right-click (or Control-click) the app → Open → Open**. You only need to do this once. (Notarized releases open normally.)

### Documentation & changelog

- 📖 **Quick Start & full docs:** https://github.com/Aiacos/ajazz-control-center/wiki
- 🧩 **Supported devices:** https://github.com/Aiacos/ajazz-control-center/wiki/Supported-Devices
- 🛠️ **Troubleshooting / FAQ:** https://github.com/Aiacos/ajazz-control-center/wiki/Troubleshooting
- 📝 **Full changelog:** https://github.com/Aiacos/ajazz-control-center/blob/main/CHANGELOG.md

---
