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

### Clock-demotion ordering dependency (READ before VERIFY-01)

VERIFY-01 expects the per-row sync surface to be HIDDEN on devices whose
descriptor sets `hasClock=false`. Two known descriptors should arrive at
`hasClock=false` per ROADMAP:

- `akp05e` (USB `0x0300:0x3004` - the firmware-confirmed retail unit;
  previously mis-filed as `akp03_variant_3004`). DEVICES-05 demotion is in
  Phase 10.
- `ak980pro` (USB `0x0c45:0x8009`). DEVICES-06 demotion is in Phase 12.

If those demotions have NOT yet landed at the moment the operator runs this
checklist, the descriptors still advertise `hasClock=true` - and VERIFY-01
records BLOCKED + the dependency, NOT a hard FAIL. Verify before starting:

```
grep -n 'codename = "akp05e"' -A 5 src/devices/streamdeck/src/register.cpp
grep -n 'codename = "ak980pro"' -A 5 src/devices/keyboard/src/register.cpp
```

If either descriptor shows `.hasClock = true,` the demotion has NOT landed -
flag VERIFY-01 BLOCKED until that phase has shipped.

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
prerequisite is missing (e.g. clock-demotion ordering, a device not
present, the build did not configure).

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
1. The clock-demotion ordering check above has been performed and recorded
   (whether the demotions are landed or not).

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

PASS iff (a) the AKP05-family AND AKP153-family Stream Dock rows DO show the
"Time synchronization" section AND (b) the `ak980pro` row DOES NOT show it,
post-DEVICES-06 demotion AND (c) the `akp05e` (USB `0x0300:0x3004`) row DOES
NOT show it, post-DEVICES-05 demotion AND (d) the mouse row DOES NOT show it.

If DEVICES-05 or DEVICES-06 has NOT landed at the moment of testing, mark the
corresponding sub-check BLOCKED (NOT FAIL) and record the dependency. The
remaining sub-checks still apply.

`PASS / FAIL / BLOCKED - notes: ____________________________________________`

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

`PASS / FAIL / BLOCKED - notes: ____________________________________________`

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

`PASS / FAIL / BLOCKED - notes: ____________________________________________`

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

`PASS / FAIL / BLOCKED - notes: ____________________________________________`

______________________________________________________________________

## Results summary

Fill this table after running the four verifies. Use ASCII PASS / FAIL /
BLOCKED only.

| Verify    | Outcome | Operator notes | Date (UTC) |
| --------- | ------- | -------------- | ---------- |
| VERIFY-01 |         |                |            |
| VERIFY-02 |         |                |            |
| VERIFY-03 |         |                |            |
| VERIFY-04 |         |                |            |

Operator name / session: \_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_\_

If any verify is FAIL, file an issue tagged `regression` with the failing
verify ID, the observed behaviour, and the expected wording from this
checklist. If any verify is BLOCKED, record the blocking dependency
(missing demotion phase, missing device, missing build dep) in the notes
column so the next operator session can pick it up.
