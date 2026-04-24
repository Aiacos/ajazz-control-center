# Quick Start

Get AJAZZ Control Center running and configure your first key in under
five minutes.

## 1. Install

### One-line install (Linux & macOS)

```bash
curl -fsSL https://raw.githubusercontent.com/Aiacos/ajazz-control-center/main/scripts/install.sh | bash
```

That's it. The installer picks the best channel for your platform
(Flatpak on Linux if available, native `.deb` / `.rpm` otherwise,
Homebrew cask on macOS). It installs the udev rule automatically and
**does not** require group membership, logout, or replugging.

### Windows

```powershell
winget install Aiacos.AjazzControlCenter
```

### Manual download

Prefer to pick the file yourself? Every release publishes `.deb`,
`.rpm`, `.flatpak`, `.msi` and a universal `.dmg` on the
[Releases page](https://github.com/Aiacos/ajazz-control-center/releases).

- **Fedora / RHEL / openSUSE:** `sudo dnf install ./ajazz-control-center-*.rpm`
- **Debian / Ubuntu:** `sudo apt install ./ajazz-control-center-*.deb`
- **Flatpak:** `flatpak install ./ajazz-control-center-*.flatpak`
- **macOS:** open the `.dmg` and drag the app to **Applications**. On
  first launch grant **Input Monitoring** in *System Settings → Privacy
  & Security* so macros can be sent.
- **Windows:** run the `.msi`. No drivers to install.

### Why you don't need `plugdev` or a logout

The udev rule shipped with the package uses `TAG+="uaccess"`. That tells
`systemd-logind` to attach an ACL granting the current desktop user
read/write access to the device node — at the moment the device appears.
It works on every modern distro (Fedora, Ubuntu, Debian, openSUSE, Arch,
NixOS) and with any desktop session (GNOME, KDE, Hyprland, Sway, tty
login).

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
1. Click **New Profile** and give it a name (e.g. `Blender`).
1. For a stream-deck key: drag **Hotkey → Ctrl+S** from the action
   palette onto key 1. Set an icon from the **Image** tab.
1. For a keyboard: switch to the **Layer** tab, click a key on the
   virtual layout, then choose **Macro → Play Macro** from the panel.
1. For a mouse: open the **DPI** tab and edit the DPI stages and their
   LED colours.
1. Click **Apply**. The profile is written to the device immediately and
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
