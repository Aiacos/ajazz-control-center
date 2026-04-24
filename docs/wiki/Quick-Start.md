# Quick Start

Get AJAZZ Control Center running and configure your first key in under
five minutes.

## 1. Install

Pick the package for your platform from the
[Releases page](https://github.com/Aiacos/ajazz-control-center/releases).

### Linux

- **Fedora / RHEL / openSUSE:** download the `.rpm` and
  `sudo dnf install ./ajazz-control-center-*.rpm`.
- **Debian / Ubuntu:** download the `.deb` and
  `sudo apt install ./ajazz-control-center-*.deb`.
- **Flatpak (any distro):**
  `flatpak install flathub io.github.Aiacos.AjazzControlCenter` *(pending submission — today use the `.flatpak` bundle from Releases)*.

The Linux packages install a udev rule at
`/etc/udev/rules.d/99-ajazz.rules` granting the `plugdev` group access to
AJAZZ HID interfaces. Make sure your user is in that group:

```bash
sudo usermod -aG plugdev "$USER"
# then log out and back in
```

### Windows

Run the `.msi` installer. No drivers need to be installed — AJAZZ devices
present as standard HID and are claimed via `hid.dll`.

### macOS

Open the universal `.dmg` and drag the app to **Applications**. On first
launch you may need to grant **Input Monitoring** permission in
**System Settings → Privacy & Security** so macros can be sent.

## 2. Connect a device

Plug your AJAZZ device in. Launch **AJAZZ Control Center**. The device
should appear in the left-hand device list within one second. If it does
not:

- Linux: run `lsusb | grep -Ei '0300|3151|3554'` to confirm the device
  is seen by the kernel.
- Run `acc --log-level=debug` from a terminal; the logger will print
  enumeration errors.
- See [Troubleshooting](Troubleshooting).

## 3. Create your first profile

1. Select the device in the left pane.
2. Click **New Profile** and give it a name (e.g. `Blender`).
3. For a stream-deck key: drag **Hotkey → Ctrl+S** from the action
   palette onto key 1. Set an icon from the **Image** tab.
4. For a keyboard: switch to the **Layer** tab, click a key on the
   virtual layout, then choose **Macro → Play Macro** from the panel.
5. For a mouse: open the **DPI** tab and edit the DPI stages and their
   LED colours.
6. Click **Apply**. The profile is written to the device immediately and
   saved to `~/.config/ajazz-control-center/profiles/` (Linux),
   `%APPDATA%\ajazz-control-center\profiles\` (Windows) or
   `~/Library/Application Support/ajazz-control-center/profiles/`
   (macOS).

## 4. Bind to an app (optional)

In the profile header click **Per-app** and pick the process name
(`blender`, `kdenlive.exe`, etc.). The profile will auto-activate when
that window is focused.

## 5. Next steps

- [Plugin Development](Plugin-Development) — automate anything with Python.
- [Supported Devices](Supported-Devices) — see what your device supports.
