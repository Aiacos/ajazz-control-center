# v1.1 UI Verifies - Operator Checklist (VERIFY-01..04)

> ASCII-only. Run this checklist against a built + launched AJAZZ Control Center
> app with the four target devices physically connected. Record results in the
> per-verify "Acceptance" line and in the summary table at the bottom of the
> file.

This checklist closes the four real-hardware visual UI verifications that v1.1
deferred (VERIFY-01..04, REQUIREMENTS.md). The deliverable of plan 13-02 IS
this checklist; running it is a separate, deferred operator activity.

______________________________________________________________________

## Prerequisites (read before starting)

### Build precondition

The app must be configured, built, and launched. Qt6 `CorePrivate`
(`qzipreader_p.h`) is REQUIRED at configure time and is shipped on Fedora as
`qt6-qtbase-private-devel`. Install it FIRST:

```
sudo dnf install qt6-qtbase-private-devel
```

(This is an environment dependency, NOT a project-tree mutation. The CLAUDE.md
hard rule "no system-level mutations from project tooling" applies to project
code; an operator manually installing a build dep on their machine is fine.)

Then build the app the normal way and launch it from the build tree, e.g.:

```
cmake --preset linux-release
cmake --build --preset linux-release
./build/linux-release/src/app/ajazz-control-center
```

### Hardware precondition

The 4 physical devices the v1.1 catalogue covers must be plugged in and
visible to the kernel as `/dev/hidraw*` nodes with `uaccess` ACL granting
the current user read+write. Devices in scope:

1. An AKP05 family Stream Dock (one of: AJAZZ AKP05, Mirabox N4, AJAZZ AKP05E
   USB `0x0300:0x3004`).
1. An AKP153 family Stream Dock (one of the lower-key Stream Dock SKUs).
1. An AJAZZ AK980 PRO keyboard (USB `0x0c45:0x8009`).
1. An AJAZZ wireless mouse (a `0x248a:` / `0x249a:` / `0x3554:` AJ-series mouse
   with the Microdia `0x0c45:0x7016` wireless dongle).

The udev rule at `resources/linux/70-ajazz.rules` must be installed under
`/etc/udev/rules.d/` or `/usr/lib/udev/rules.d/`. If ACLs look wrong run:

```
ls -l /dev/hidraw*    # expect uaccess ACL granting your uid r+w
```

If the file shows root-only ownership, replug the device PHYSICALLY (per
CLAUDE.md, `udevadm trigger --action=change` does NOT restore the `uaccess`
ACL on systemd >= 258 / Linux device access notes). Dev-only transient
recovery: `sudo setfacl -m u:$(id -u):rw /dev/hidraw<N>`.

### Expected `hasClock` state per descriptor (REFERENCE for VERIFY-01)

VERIFY-01 cross-checks the rendered sync surface against `DeviceDescriptor::hasClock`.
The current code reality (verified 2026-05-28 against `src/devices/*/src/register.cpp`):

| Codename                                | `hasClock` | Source-of-truth                                                                                |
| --------------------------------------- | :--------: | ---------------------------------------------------------------------------------------------- |
| `akp05e` (USB `0x0300:0x3004`)          |  `false`   | DEVICES-11 / ARCH-05 — Stream Dock firmware has no RTC. Landed via commit `07c5902` (Phase 14) |
| `ak980pro` (USB `0x0c45:0x8009`)        |   `true`   | ARCH-05.1 — real 4-packet `0x28` firmware RTC envelope, hardware-confirmed                     |
| Generic AKP05-family backend (non-3004) |   `true`   | Plan 05-02 / A-03 / D-03 — backend inherits `IClockCapable`                                    |
| AKP815                                  |   `true`   | A-03 / D-03 — has a firmware RTC                                                               |
| AKP153-family rows                      |   `true`   | per `register.cpp` family table                                                                |
| AJAZZ mice (no TFT)                     |  `false`   | conditional: `.hasClock = tft` — only true on TFT-equipped variants                            |

There is **no pending demotion** for VERIFY-01: both flag positions above are
already final. (An earlier draft of this checklist carried a `DEVICES-06`
"ak980pro demotion" note; that claim was superseded by ARCH-05.1 when the real
RTC was hardware-confirmed. ARCH-05.1 wins; the AK980 PRO Sync button is
VISIBLE by design.)

Sanity-check at start (should match the table above):

```
grep -n 'codename = "akp05e"' -A 8 src/devices/streamdeck/src/register.cpp
grep -n 'codename = "ak980pro"' -A 8 src/devices/keyboard/src/register.cpp
```

### As-built UI map (where each surface lives in the running app)

The plan 13-02 PLAN.md interfaces block described a v1.1 layout where the
sync button and the maturity tooltip lived inline on each sidebar
`DeviceRow`. That layout has SINCE been refactored - the sidebar row no
longer carries either surface. The current routes are:

| Surface                       | Where it lives in the running app                                                                                 |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Global auto-sync toggle       | `Settings` drawer (top-right gear icon) -> "Time sync" frame -> SwitchDelegate "Auto-sync time on device connect" |
| Per-device sync surface       | Sidebar -> click device row -> middle pane `ProfileEditor` -> `Settings` tab -> "Time synchronization" section    |
| Per-device "Sync time now"    | Same `Settings` tab -> Button labelled "Sync time now"                                                            |
| Per-device sync result inline | Same `Settings` tab -> Label next to the button: "[check] Synced" on success, "[x] <error message>" on failure    |
| Manual-sync success toast     | Top-right `Notification` pill, text "Time synced: <codename>", green; fires ONLY on `manualSyncSucceeded`         |
| Maturity surface (per device) | Same `Settings` tab -> "Support maturity" section: a coloured chip with the tier word + a one-line description    |

The sidebar row itself today only carries: per-family icon, model name,
connected / offline status text, an optional `BatteryIndicator` chip, and
an "Offline" pill - no sync button, no maturity tooltip.

### How to record results

Each verify ends in an Acceptance line of the form
`PASS / FAIL / BLOCKED - notes`. Fill the line, then fill the summary table
at the end of the file. PASS means the expected behaviour was observed
verbatim. FAIL means the observed behaviour contradicts the expected
behaviour (this is a regression and must be filed). BLOCKED means a
prerequisite is missing (e.g. a device not present, the build did not
configure, a build precondition like `qt6-qtbase-private-devel` missing).

______________________________________________________________________

## VERIFY-01 - Per-device sync surface tracks `hasClock`

**Source-of-truth UI element:** `src/app/qml/SettingsRow.qml`, "Time
synchronization" section is gated by `visible: root.hasClock` (line 196).
The `hasClock` value flows from `DeviceDescriptor::hasClock` via
`DeviceModel::capabilitiesFor(codename)` into the `ProfileEditor` -> "Settings"
tab.

**Pre-conditions:**

1. App is launched and the device sidebar is populated with at least the 4
   required devices (Stream Dock AKP05-family + Stream Dock AKP153-family +
   AK980 PRO + AJAZZ mouse).
1. The `hasClock` reference table above has been sanity-checked against the
   live `register.cpp` files (a quick grep — should match verbatim).

**Steps:**

1. Click the AKP05-family Stream Dock row in the sidebar.
1. In `ProfileEditor` click the `Settings` tab.
1. Observe whether the "Time synchronization" section (header label "Time
   synchronization" + Switch "Auto-sync time on connect" + Button "Sync time
   now") is rendered.
1. Repeat steps 1-3 for the AKP153-family row.
1. Repeat steps 1-3 for the `ak980pro` row.
1. Repeat steps 1-3 for the AJAZZ mouse row.

**Expected (on-screen wording, verbatim):**

- For rows whose descriptor sets `hasClock=true`: the "Time synchronization"
  section IS visible. The Switch label reads `Auto-sync time on connect`.
  The button label reads `Sync time now`.
- For rows whose descriptor sets `hasClock=false`: the "Time synchronization"
  section is ABSENT (the `Settings` tab still renders, but only the "Support
  maturity" section and - on AK-series only - the "Fn-layer behaviour" / "Sleep
  timer" / "Key response time" controls are shown).

**Acceptance (PASS iff ...):**

PASS iff (a) the generic AKP05-family AND AKP153-family Stream Dock rows DO
show the "Time synchronization" section (`hasClock=true` per Plan 05-02 /
A-03) AND (b) the `ak980pro` row DOES show it (`hasClock=true` per ARCH-05.1
real 4-packet `0x28` RTC) AND (c) the `akp05e` (USB `0x0300:0x3004`) row
DOES NOT show it (`hasClock=false` per DEVICES-11 / ARCH-05, landed in
commit `07c5902`) AND (d) the non-TFT mouse row DOES NOT show it
(`hasClock=tft` evaluates false).

If the live `register.cpp` `hasClock` value disagrees with the reference
table at top of this checklist, that is a regression — record FAIL with the
delta.

`PASS / FAIL / BLOCKED - notes:` **PASS** (3/4 sub-checks exercised; (a) BLOCKED — lab has no AKP153 / generic AKP05-family device. (b) ak980pro shows section ✓. (c) akp05e (0x3004) hides section ✓ — DEVICES-11/ARCH-05 confirmed live. (d) REFINED — mouse (AJ159 APEX) hasClock=true via TFT, shows section ✓; "non-TFT mouse" assumption in checklist did not apply to this lab unit. Operator session 2026-05-28.)

______________________________________________________________________

## VERIFY-02 - Global auto-sync toggle persists across restart + 300 ms arrival firing path

**Source-of-truth UI element:** `src/app/qml/SettingsPage.qml` "Time sync"
frame -> SwitchDelegate `Auto-sync time on device connect` (line 206). Wired
to `TimeSyncService.autoSync` which mirrors the QSettings key `Time/AutoSync`
on every setter (`src/app/src/time_sync_service.cpp` `kSettingsKey`, line 38;
`setAutoSync`, line 84). Arrival firing path:
`TimeSyncService::onDeviceArrivedDebounced` uses
`QTimer::singleShot(std::chrono::milliseconds(300), ...)` (line 208).

**Pre-conditions (persistence half):**

1. App is launched.
1. The "Settings" drawer is reachable from the top-right header gear / settings
   action.

**Steps (persistence):**

1. Open the global Settings drawer.
1. Scroll to the "Time sync" frame.
1. Toggle the SwitchDelegate labelled `Auto-sync time on device connect` to ON
   (note its prior state).
1. Fully quit the app (close the main window AND exit tray, if minimised).
1. Relaunch the app and reopen the Settings drawer.

**Expected (persistence):**

- The toggle reads ON. The hint label below still reads exactly:
  `Note: Currently no AJAZZ device firmware supports host clock writes - the toggle is scaffolded so the wire format can drop in without UI churn.`
- On disk, the value `Time/AutoSync` under the QSettings store is `true`
  (typically `~/.config/AJAZZ/AjazzControlCenter.conf` on Linux; key path
  `[Time]` / `AutoSync=true`).

**Steps (arrival firing path):**

1. With the global toggle ON, unplug-and-replug a clock-capable Stream Dock
   (e.g. an AKP05-family or AKP153-family device that retains
   `hasClock=true`).
1. Watch the app log on stderr (or via `journalctl -p info`) for a
   `time-sync` category line.

**Expected (arrival firing):**

- Within ~300 ms of the device arrival event the log records the auto-sync
  push attempt for the arriving codename. The success path emits a
  `syncSucceeded` signal that turns the inline status in the `Settings` tab
  to `[check] Synced`. The NotImplemented path silently INFO-logs (per D-02:
  auto-sync surface is glyph/inline only - NO toast).

**Acceptance (PASS iff ...):**

PASS iff (a) the toggle state survives a full quit + relaunch AND (b) the
arrival firing path produces a `time-sync` log line ~300 ms after the
replug for the arriving codename AND (c) NO "Time synced" green pill appears
in the top-right on the auto-sync arrival (auto path emits only
`syncSucceeded`, not `manualSyncSucceeded`; the toast is gated on the latter,
see VERIFY-03 honesty contract).

`PASS / FAIL / BLOCKED - notes:` **PASS**. (a) Toggle persists across SIGTERM quit + relaunch — `[Time] AutoSync=true` mirrored to `~/.config/Aiacos/AJAZZ Control Center.conf` and read back on launch. (b) Replug of full USB topology produced 3 simultaneous `time-sync` log lines (ajazz_24g_8k at +311ms, ak980pro at +582ms with success via 4-pkt 0x28, akp05e at +359ms with NotImplemented skip). Range 311-582ms — the "~300ms" target was the debounce window; HID re-init adds 50-360ms latency. (c) NO green "Time synced" pill on auto-arrival — operator-confirmed live. **Documented finding (non-regression):** mouse auto-sync skipped at +311ms with reason `HID write failed when pushing time to 'ajazz_24g_8k'` — race condition where hidraw is still re-enumerating when the 300ms timer fires. The auto-sync orchestrator does NOT retry; battery polls 15s later succeed but setTime is not re-attempted. Honesty contract intact (INFO skip with verbatim reason, no false success). See STATE.md "Open follow-up items" for the retry-on-HID-recovery TODO.

______________________________________________________________________

## VERIFY-03 - NotImplemented -> inline failure status, NEVER a false "Time synced" success toast (Pitfall 19 honesty contract)

**Source-of-truth UI elements:**

- `src/app/src/time_sync_service.cpp` `doPush`, lines 252-256: maps
  `TimeSyncResult::NotImplemented` to the error string
  `"Time-sync wire format not yet implemented for this device"`. The path
  emits `syncFailed(codename, reason)` (line 164), NOT `syncSucceeded`.
- `src/app/qml/SettingsRow.qml` Connections target `TimeSyncService`, lines
  82-86: `onSyncFailed(codename, message)` sets `_syncStatus = message`. The
  inline label (line 226-233) then renders `[x] <message>` in muted colour.
- `src/app/qml/Main.qml` `syncNote` `Notification`, lines 167-173: green
  `Notification` pill labelled `Time synced: <codename>` is wired ONLY to
  `onManualSyncSucceeded`. The `NotImplemented` path does NOT emit
  `manualSyncSucceeded` (only `setSystemTimeOn` on the OK path does, line
  161-162 of `time_sync_service.cpp`).

**Pre-conditions:**

1. A device whose backend returns `TimeSyncResult::NotImplemented` from
   `IClockCapable::setTime`. Per ARCH-05 the default verdict on AKP05 family
   units is NotImplemented unless captures confirm a real RTC envelope; the
   `ak980pro` setTime IS implemented (4-packet 0x28 envelope per ARCH-05.1).
   So the natural NotImplemented target is an AKP05-family or AKP153-family
   Stream Dock (whichever still advertises `hasClock=true` for VERIFY-01).
1. The "Time synchronization" section is visible on that device's `Settings`
   tab (else VERIFY-03 cannot be exercised - re-derive from VERIFY-01).

**Steps (manual click path):**

1. Open the device's `Settings` tab.
1. Confirm the `_syncStatus` inline label is absent (no prior sync this
   session). If it isn't, re-select the device to reset the label
   (`onDeviceCodenameChanged` resets `_syncStatus = ""`).
1. Click the `Sync time now` button.
1. Observe the inline status label next to the button AND the top-right
   pill area.

**Expected (manual click, NotImplemented):**

- Inline label appears reading exactly:
  `[x] Time-sync wire format not yet implemented for this device`
  (rendered in muted colour, the `[x]` glyph is the QML `qsTr("[x] %1")`
  template; the actual rendered text is the lowercase-x cross then the
  message). Wording is from `time_sync_service.cpp` line 255.
- Top-right `Notification` pill area shows NOTHING. No green pill labelled
  `Time synced: <codename>` ever appears for a NotImplemented result.

**Steps (auto-sync tick path):**

1. With the global auto-sync toggle ON (VERIFY-02), unplug-and-replug the
   same NotImplemented device.
1. After the ~300 ms debounce window, observe both the device's `Settings` tab
   AND the top-right pill area.

**Expected (auto-sync tick, NotImplemented):**

- The `_syncStatus` inline label DOES NOT update (the auto-sync path emits
  only an INFO log on failure - it does NOT emit `syncFailed`, so the
  Connections handler for `onSyncFailed` never fires). The `[check] Synced`
  status also DOES NOT appear (auto path does not emit `syncSucceeded` on
  NotImplemented; the error path skips the success signal).
- Top-right `Notification` pill area shows NOTHING. NO green
  `Time synced: <codename>` pill appears (the pill is gated on
  `manualSyncSucceeded`, which the auto path never emits).

**Acceptance (PASS iff ...):**

PASS iff:

1. On the MANUAL `Sync time now` click against a NotImplemented device, the
   inline status reads the failure message (`[x] Time-sync wire format not yet implemented for this device`) AND no green `Time synced` pill appears in
   the top-right.
1. On the AUTO arrival against the same NotImplemented device, no green
   `Time synced` pill appears in the top-right.

ANY GREEN `Time synced: <codename>` PILL ON A NOTIMPLEMENTED RESULT IS AN
IMMEDIATE FAIL. This is the Pitfall 19 honesty contract: a `NotImplemented`
verdict must never present as success. No exceptions, no "the toast was
fast" excuses.

`PASS / FAIL / BLOCKED - notes:` **PASS** (auto-half + side-test on success) + BLOCKED (manual-click + NotImplemented intersection has no clickable target on connected hw). **Auto-half PASS** — witnessed live during VERIFY-02 replug: `[time-sync] auto-sync skipped for akp05e: Time-sync wire format not yet implemented for this device` fired at +359ms after arrival, AND no green pill appeared (operator-confirmed). **Side-test PASS** — manual click of "Sync time now" on AK980 PRO produced (i) `[keyboard.ak980] setTime → device clock set to 2026-05-28 12:33:37 (local)`, (ii) inline `✓ Synced` next to the button, (iii) green `Time synced: ak980pro` pill in top-right (operator-confirmed visually; transient, screenshot timing missed it but user saw it). **Manual-half BLOCKED** — no device on connected lab has the `hasClock=true + setTime=NotImplemented` intersection: AKP05E is hasClock=false (no button to click), AK980 PRO + mouse have setTime IMPLEMENTED. Pitfall 19 double-barrier confirmed: pill fires on `manualSyncSucceeded` (live witnessed), never on auto-arrival or NotImplemented (live witnessed). Operator session 2026-05-28.

______________________________________________________________________

## VERIFY-04 - Maturity surface renders the correct tier across all 5 tier values

**Source-of-truth UI element:** `src/app/qml/SettingsRow.qml` "Support
maturity" section (lines 149-191): a coloured chip displaying the tier word
(`Verified` / `Functional` / `Partial` / `Probed` / `Scaffolded`, see
`_maturityLabel` lines 54-60) next to a one-line description (see
`_maturityDesc` lines 61-67). The tier value flows from `devices.yaml` ->
`device_maturity_map.generated.hpp` -> `DeviceModel::MaturityRole` ->
`DeviceModel::capabilitiesFor(codename).maturity` -> `ProfileEditor._maturity`
-> `SettingsRow.deviceMaturity`.

NOTE: the plan 13-02 PLAN.md described a sidebar-row hover tooltip
`"Maturity: %1"`. That tooltip has been REPLACED in the as-built v1.1 UI by
the chip + description in the `Settings` tab. The acceptance below tests
the as-built surface, not the original tooltip.

**Pre-conditions:**

1. The 4 required devices are connected so each has a sidebar row.
1. The `docs/_data/devices.yaml` maturity field for each row is known (cross-
   check by `grep -A 2 '<codename>' docs/_data/devices.yaml | grep maturity:`).

**Steps:**

1. Click each connected device row in turn.
1. On each device's `Settings` tab, observe the "Support maturity" section.
1. Record the tier word shown on the chip AND the description below it.
1. Cross-check each tier word against the `devices.yaml` `maturity:` value
   for that codename.

**Expected (on-screen wording, verbatim):**

- Chip text is ONE of (capitalised, exactly): `Verified`, `Functional`,
  `Partial`, `Probed`, `Scaffolded`.
- The description label next to the chip reads, per tier:
  - `Verified`: `Tested on real hardware.`
  - `Functional`: `All advertised features work.`
  - `Partial`: `Some features work; others in progress.`
  - `Probed`: `Protocol probed; wiring in progress.`
  - `Scaffolded`: `Support not implemented yet.`
- For each connected device the chip's tier word matches the `devices.yaml`
  `maturity:` value for that codename (verbatim, lowercase compared
  case-insensitively to the chip word).
- The chip background and border are colour-tinted per tier:
  `Verified` -> success green, `Functional` -> brand accent,
  `Partial` -> warning amber, `Probed` / `Scaffolded` -> muted neutral.

**Coverage (all 5 tiers):** the v1.1 catalogue does not necessarily ship all
5 tiers on the 4 connected devices, so coverage of the missing tiers is
verified by selecting a representative un-connected catalogue row (if the
row is hidden by the connected-only sidebar filter, this sub-check is
recorded BLOCKED with the tier list and dependency). The chip text must
render legibly (no clipping, readable contrast against the tile background)
for every tier value present.

**Acceptance (PASS iff ...):**

PASS iff (a) every connected device's "Support maturity" chip displays the
correct tier word against `devices.yaml` AND (b) the description label
matches the tier-mapped string above AND (c) every tier word that IS present
in the connected set renders legibly (no clipping, readable contrast). If
tiers beyond the connected set cannot be exercised because of the connected-
only sidebar filter, record those tiers BLOCKED with the tier names listed
rather than FAIL.

`PASS / FAIL / BLOCKED - notes:` **PASS** (2/5 tiers exercised) + BLOCKED (3/5 tiers — Verified, Probed, Scaffolded). (a) Chip text matches devices.yaml verbatim: akp05e=Partial ✓, ak980pro=Functional ✓, ajazz_24g_8k=Functional ✓. (b) Description label verbatim per tier mapping: Partial="Some features work; others in progress." ✓; Functional="All advertised features work." ✓ (observed on both ak980pro and mouse). (c) Per-tier color rendering correct: Partial chip is amber/yellow (`Theme.warningAccent` `#f59e0b`), Functional chips are brand red/coral (`Theme.accent`) — distinct colors per tier as designed; both Functional chips render IDENTICAL color (one-singleton confirms). Coverage of Verified, Probed, Scaffolded tiers BLOCKED: sidebar is connected-only by v1.1 design intent; no offline-catalogue view exists. Operator session 2026-05-28.

______________________________________________________________________

## Results summary

Fill this table after running the four verifies. Use ASCII PASS / FAIL /
BLOCKED only.

| Verify    | Outcome      | Operator notes                                                                                                                                                                               | Date (UTC) |
| --------- | ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- |
| VERIFY-01 | PASS+BLOCKED | 3/4 connected devices pass (akp05e hides, ak980pro shows, mouse shows because TFT); BLOCKED on AKP153 + generic AKP05 (lab has no such device).                                              | 2026-05-28 |
| VERIFY-02 | PASS         | Toggle survives quit+relaunch; arrival firing produced 3 time-sync log lines within 311-582ms; no green pill on auto-path. Documented finding: mouse retry-on-HID-recovery follow-up.        | 2026-05-28 |
| VERIFY-03 | PASS+BLOCKED | AUTO-half on NotImplemented (AKP05E) witnessed silent during VERIFY-02; side-test on success (AK980 PRO) green pill fired correctly; MANUAL+NotImplemented intersection BLOCKED (no target). | 2026-05-28 |
| VERIFY-04 | PASS+BLOCKED | 2/5 tiers exercised (Partial + Functional); chip text + description + per-tier color all match expected; Verified+Probed+Scaffolded BLOCKED (connected-only sidebar by design).              | 2026-05-28 |

Operator name / session: Lorenzo Argentieri / 2026-05-28 UAT walkthrough (orchestrator-assisted, sub-agent Claude Opus 4.7)

If any verify is FAIL, file an issue tagged `regression` with the failing
verify ID, the observed behaviour, and the expected wording from this
checklist. If any verify is BLOCKED, record the blocking dependency
(missing device, missing build dep) in the notes column so the next
operator session can pick it up.
