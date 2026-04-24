# Troubleshooting

Before filing a bug, collect a debug log:

```bash
acc --log-level=debug --dump-capabilities > acc.log 2>&1
```

and attach it to the issue.

## Linux: device is not detected

1. Confirm the kernel sees it:

   ```bash
   lsusb | grep -Ei '0300|3151|3554'
   ```

   If nothing is shown, it is a cable / kernel issue, not an app issue.

2. Install the udev rule (the `.deb`/`.rpm`/`.flatpak` packages do this
   automatically via post-install; if you built from source, run the
   Makefile shortcut):

   ```bash
   make udev
   ```

   You do **not** need to add yourself to a group, log out, or replug
   the device — the rule uses `TAG+="uaccess"` so the current desktop
   user is granted access via ACLs by systemd-logind.

3. Check the rule was picked up:

   ```bash
   ls -l /dev/hidraw* | grep $USER
   ```

   You should see your username in the ACL (or the `+` sign). If not,
   unplug + replug the device; for stubborn systems (rare):

   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger --subsystem-match=usb --subsystem-match=hidraw
   ```

4. Make sure no other process owns the HID interface:

   ```bash
   sudo fuser /dev/hidraw*
   ```

   Killing the vendor app or a competing tool (OpenDeck, streamdeck-ui)
   will free the device.

5. Run the environment health check:

   ```bash
   make doctor
   ```

## Windows: device is detected but no input / no display

- Exit the AJAZZ vendor app completely (check the system tray).
- Unplug and replug the device.
- Reinstall the app — the MSI registers the HID filter that re-opens
  the interface with shared access.

## macOS: keystrokes from macros are not sent

- Grant **Input Monitoring** and **Accessibility** permission to
  AJAZZ Control Center in **System Settings → Privacy & Security**.
- Quit and reopen the app after granting.

## Flatpak: device access denied

The Flatpak runs sandboxed. Grant USB access once:

```bash
flatpak override --user --device=all io.github.Aiacos.AjazzControlCenter
```

(Per-device filtering can replace `--device=all` in the future.)

## Stream deck images are black or corrupted

- The device expects exactly 85×85 pixels in BGRA. If you are building
  from source, check that `QImage::Format_ARGB32` is being converted
  correctly — see `src/devices/streamdeck/akp153.cpp`.
- Try a lower brightness: `--brightness=50`. A few AKP153E units with
  early firmware miss the top rows at 100 % brightness.

## Plugin not appearing in action palette

1. Confirm the plugin installed into the *bundled* interpreter:

   ```bash
   acc --list-plugins
   ```

2. Check the log for import errors:

   ```bash
   acc --log-level=debug 2>&1 | grep plugin
   ```

3. Make sure your `pyproject.toml` has
   `[project.entry-points."ajazz.plugins"]` set.

## CI build failure after local commit

- Ensure `clang-format --style=file -n src/**/*.{hpp,cpp}` returns clean.
- Run `cmake --build --preset dev --target clang-tidy` locally.
- YAML / JSON / Python are all validated by the lint job; `pre-commit
  run --all-files` catches everything before you push.

## Crash on startup with `QQmlApplicationEngine failed to load component`

Your Qt installation is older than 6.7 or is missing the Quick Controls
2 module. Reinstall Qt 6.7+ with **Qt Quick Controls 2**, or on Linux:

```bash
sudo dnf install qt6-qtquickcontrols2-devel     # Fedora
sudo apt install qt6-quickcontrols2-dev         # Debian/Ubuntu
```

## Anything else

Search [open issues](https://github.com/Aiacos/ajazz-control-center/issues?q=is%3Aissue)
or open a new one with your `acc.log` attached.
