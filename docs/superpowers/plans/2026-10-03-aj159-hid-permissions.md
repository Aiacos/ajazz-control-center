# AJ159 HID permissions recovery

## Goal

Resolve the `hid_open failed` error for the AJAZZ AJ159 APEX
(`0x3151:0x5007`) recorded in `Video_2026-10-03_11-39-26.mp4` and the
application log.

## Confirmed state

- The device is present in USB and HID enumeration.
- The application selects the vendor collection with `usage page 0xFFFF` and
  `usage 0x02`.
- `/dev/hidraw0..2` are owned by `root:root` with mode `0600` and have no ACL
  entry for user `sanya`.
- There is no AJAZZ rule under `/etc/udev/rules.d`; the udev file installed in
  `~/.local/lib/udev` is not loaded by the system.
- As a result, the UI detects the device through enumeration, but opening it
  and reading battery/time data or writing settings fail with access denied.

## Resolution

1. Keep udev installation owned by the top-level CMake configuration and
   remove the duplicate app-level install rule. A user-level prefix must not
   imply that a system-level setting is active.
1. Add an `EACCES` hint to HID transport errors that points to the system udev
   rule.
1. Add a regression test that checks the rule for VID `3151`.
1. Install the rule on the current system, reload udev, and verify the ACL.

## Verification

- Catch2 regression test for the udev rule.
- Release build with `make release`.
- Run the focused regression test with `ctest --preset release -R ...`.
- Smoke test: launch the installed binary with a bounded timeout and verify
  that it completes startup without an immediate crash.

## Risks and rollback

The changes are limited to installation configuration, HID diagnostics, and
tests. Revert the affected files to roll back the code. Remove the system udev
rule separately from `/etc/udev/rules.d`, then reload the rules.

## Execution results

- Installed the system rule at `/etc/udev/rules.d/70-ajazz.rules` and reloaded
  udev. The `/dev/hidraw0..2` nodes received the `user:sanya:rw-` ACL.
- `make release` completed successfully.
- The regression test and full CTest suite passed: `874/874`.
- The installed `/usr/local/bin/ajazz-control-center` passed startup on
  Wayland and opened `VID=3151 PID=5007` using `usage+page filtered`. The
  process was stopped after a bounded `12s` timeout.
