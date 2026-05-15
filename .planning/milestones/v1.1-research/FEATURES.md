# Feature Research — v1.1 Device Lifecycle Hardening + Scaffolding-to-Functional

**Domain:** Stream-deck / macropad / control-surface device management (Qt 6 desktop app)
**Milestone:** v1.1 (subsequent to v1.0 retro-fit catalogue)
**Researched:** 2026-05-13
**Confidence:** MEDIUM-HIGH (competitive analysis HIGH; absolute "what AJAZZ vendor app does" remains LOW — vendor binary is Mirabox-rebrand Vue-in-Electron, inventoried in `d5616ef` but not feature-mapped end-to-end)

______________________________________________________________________

## Scope clarification

This milestone is **not greenfield**. The app already exists with:

- 13 catalogued devices (6 functional, 7 scaffolded).
- A `DeviceModel` exposing a sidebar list via `QML_SINGLETON` services.
- A `HotplugMonitor` reworked on 2026-05-12/13 that newly recognises 3 devices.
- A `Capability` bit-flag pattern (`hasRgb`, `hasTouchStrip`) on `DeviceDescriptor` — already extensible.
- An approved time-sync design (`docs/superpowers/specs/2026-05-13-time-sync-design.md`) targeting five-layer scaffolding with `Result::NotImplemented` stubs.

The three feature categories below are therefore framed as **deltas on top of an existing app**, not from-scratch propositions. "Table stakes" here means "what users of comparable apps already expect and would notice missing from v1.1," not "what an MVP control-center would need."

______________________________________________________________________

## Category 1 — Hot-plug Hardening UX

### Table Stakes (Users Expect These)

| Feature                                                     | Why Expected                                                                                                                                                                                                                                                                                                                                                 | Complexity | Notes / dependencies on existing code                                                                                                                                                                                                 |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Silent auto-reconnect on replug                             | Every competitor does this (`streamdeck-linux-gui`, OpenDeck, Elgato, Loupedeck, Companion). The 2026-05-12/13 fix is the foundation; UX layer needs to reflect it.                                                                                                                                                                                          | LOW        | Already half-built — `HotplugMonitor::deviceArrived` fires; need to ensure `DeviceRegistry` re-opens the same `IDevice*` and `DeviceModel` row keeps stable identity by `codename`. No new code if signal routing is already correct. |
| Sidebar row persists across disconnect with "Offline" badge | Elgato shows keys/encoders in the app "while the hardware is disconnected" (per [Stream Deck SDK docs](https://docs.elgato.com/streamdeck/sdk/guides/devices/)). Users expect "my profile didn't vanish when the cable jiggled."                                                                                                                             | LOW-MEDIUM | Requires a `DeviceState` enum on `DeviceModel` (`Connected`/`Disconnected`/`Errored`) — extends the existing role pattern. Sort key must stay stable (`codename`) so the row doesn't jump on reconnect.                               |
| Disconnect surfaced as non-blocking toast (NOT modal)       | LogRocket / Carbon / Microsoft notification guidance all converge: "low-priority, system-generated, do not interrupt." No competitor uses a modal for cable yank.                                                                                                                                                                                            | LOW        | New `ToastService` (or reuse existing TrayController for a balloon). Quiet by default. Anti-pattern: blocking dialog "Device X disconnected — OK?"                                                                                    |
| Reconnect = rebind to last profile silently                 | streamdeck-linux-gui "automatically and gracefully reconnects"; Elgato's Smart Profiles auto-pick the right profile per device. Prompting on reconnect is universally regarded as friction.                                                                                                                                                                  | LOW        | Profile is keyed by `Profile::deviceCodename` (per `project_wire_format_convention.md` memory). Replug → look up last profile for that codename → bind. Wire format is already canonical.                                             |
| Multi-device sidebar: sort stability under churn            | If the user has 3 devices and yanks the middle one, the remaining two MUST NOT reorder. Companion users complain explicitly when this breaks ([bitfocus/companion #1564](https://github.com/bitfocus/companion/issues/1564), [#2795](https://github.com/bitfocus/companion/issues/2795)).                                                                    | LOW        | Sort by `(deviceClass, codename)` lexicographically — not by `connectionTime` or `vid:pid` discovery order. Verify `DeviceModel` doesn't currently sort by arrival order.                                                             |
| Focus retention when selected device disconnects            | If row 2 is selected and disconnects, selection should remain on row 2 (showing offline state), NOT jump to row 1 or 3. Equivalent to "open file disappeared from disk" UX in editors.                                                                                                                                                                       | MEDIUM     | Requires QML view to bind to `codename`, not to row-index. Easy bug to introduce; needs an explicit test.                                                                                                                             |
| Debounce rapid disconnect/reconnect storms                  | Loupedeck and Companion both have documented bugs where flapping USB causes a notification storm ([Loupedeck users report this](https://support.loupedeck.com/), [Companion #2735](https://github.com/bitfocus/companion/issues/2735)). Den Delimarsky's reverse-eng notes also call this out. Bad cables / USB-C adapters routinely cause sub-second flaps. | MEDIUM     | 300-500 ms debounce on `HotplugMonitor` aggregated to UI layer. Same primitive the time-sync design already uses (300 ms `QTimer::singleShot` after Arrived). Worth extracting as a reusable `DebouncedSignal` helper.                |

### Differentiators (Competitive Advantage)

| Feature                                                 | Value Proposition                                                                                                                                                                                                                                                 | Complexity | Notes                                                                                                     |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | --------------------------------------------------------------------------------------------------------- |
| "Last seen" timestamp on offline rows                   | None of the OSS comparators show this. Useful for "did this device actually reconnect after I rebooted, or is it still dead?" Especially valuable for users running 3+ devices.                                                                                   | LOW        | One `QDateTime` per `DeviceModel` row, updated on each `deviceArrived`. Surfaced in tooltip.              |
| Reconnect-counter / health indicator                    | Visible badge on devices that have disconnected ≥N times in last minute. Diagnostic gold — competitors leave the user guessing whether it's their cable or their software.                                                                                        | MEDIUM     | Per-row ring-buffer of recent transitions. Threshold + badge styling.                                     |
| Multi-device test harness baked into the codebase       | The v1.1 goal includes "multi-device baseline tests" — making this a reusable test fixture (mock `IDevice` + `HotplugMonitor`) is something OSS competitors lack. Already aligned with the existing test conventions described in the time-sync design § Testing. | MEDIUM     | Extends `tests/unit/` and `tests/integration/` patterns. Adds value to every future device-related phase. |
| Per-device "auto-reload profile" toggle (vs. always-on) | Power users sometimes want the *opposite*: hold the offline state, don't auto-rebind. Elgato doesn't expose this; we can.                                                                                                                                         | LOW        | New `Profile::autoRebind = true` default + Settings checkbox.                                             |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature                                          | Why Requested                    | Why Problematic                                                                                                                                                                                    | Alternative                                                                                                                                                                                               |
| ------------------------------------------------ | -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Modal dialog "Device disconnected, retry?"       | Feels "thorough" / "responsible" | Forced focus-steal, blocks workflow, useless on a cable wiggle. Universally panned in notification UX literature ([NN/g](https://www.nngroup.com/articles/indicators-validations-notifications/)). | Non-blocking toast that auto-dismisses; persistent badge on the sidebar row.                                                                                                                              |
| Auto-retry loop hammering libusb open()          | "Recover faster"                 | Floods kernel log; on a half-broken cable, creates the very flapping storm users complain about.                                                                                                   | Debounced backoff (e.g. 300 ms → 1 s → 5 s, then wait for next `LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED`). libusb docs explicitly warn that only `libusb_get_device_descriptor` is safe in the LEFT callback. |
| Prompt user "Which profile to use?" on reconnect | "Give the user control"          | The user already chose; they unplugged for 10 seconds. Asking again is friction, not control.                                                                                                      | Auto-rebind to last profile keyed by `codename`; expose the per-device opt-out in Settings (see differentiators).                                                                                         |
| Sidebar reorder by recency / connection time     | "Show newly-plugged at top"      | Breaks muscle memory; selected row jumps. Companion users complain about this exact behaviour.                                                                                                     | Stable lexicographic sort by `(deviceClass, codename)`. Optional manual drag-reorder is a future-work item, not v1.1.                                                                                     |
| Cross-platform "device removal sound"            | Mimics OS behaviour              | Annoying, accessibility regression, conflicts with OS toast.                                                                                                                                       | None — leave to OS.                                                                                                                                                                                       |
| Reload entire QML scene on disconnect            | "Refresh state cleanly"          | Loses Settings page state, scrolls position, focus. Heavy GPU/JS cost on Qt WebEngine paths.                                                                                                       | Reactive update via `DeviceModel` role change only.                                                                                                                                                       |

### Complexity tier summary (Hot-plug)

- **LOW** (≤ 1-2 days each, mostly already-existing primitives): silent auto-reconnect, sidebar persistence with badge, toast, stable sort, "last seen" tooltip, per-device opt-out.
- **MEDIUM** (3-5 days each, new primitives or test infra): focus retention test + fix, debounce primitive, reconnect-counter, multi-device test harness.
- **HIGH**: none in this category.

______________________________________________________________________

## Category 2 — Time-Sync UX

### Table Stakes (Users Expect These)

| Feature                                                   | Why Expected                                                                                                                                                                     | Complexity | Notes / dependencies                                                                                                                                                                         |
| --------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-device "Sync now" button (manual one-shot)            | Already specified in the approved design § 4. Universally what users expect for any "send X to device" operation (Loupedeck Brightness/Hue, Companion's "Send command").         | LOW        | Already specified — `DeviceRow.qml` button gated on `hasClock` role. Returns `Result::NotImplemented` toast today. Depends on `Capability::Clock` bit (new) and `hasClock` model role (new). |
| Capability-gated UI (button hidden if no `IClockCapable`) | Showing a button that doesn't apply is amateur-hour. Elgato's plugin SDK exposes capability flags exactly for this; Stream Deck SDK hides keys/encoders on incompatible devices. | LOW        | Already specified in design § 4 — `hasClock` role drives visibility. Depends on `Capability` bit-flag pattern (already in place — `hasRgb`, `hasTouchStrip` precedent).                      |
| Honest error reporting on `NotImplemented`                | The whole milestone is shipping scaffolding. If the toast says "Synced!" when nothing happened, users lose trust permanently.                                                    | LOW        | Already specified in design § Error handling — exclamation glyph + tooltip explaining "wire format not yet implemented." Critical: do NOT report success.                                    |

### Differentiators (Competitive Advantage)

| Feature                              | Value Proposition                                                                                                                                                                                                                      | Complexity | Notes                                                                                                                                                           |
| ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Global "auto-sync on connect" toggle | None of the comparators offer this because none of their target devices have RTCs. We're scaffolding for a possible future where they do — and a single global toggle ("set every device's clock when it appears") is the right shape. | LOW        | Already specified in design § 3 — `TimeSyncService::setAutoSync(bool)`, persisted via `QSettings`. Hooks into `HotplugMonitor::deviceArrived`. 300 ms debounce. |
| UTC at the interface boundary        | Avoids the "two encodings crossing the API" trap (decision #3 in the design doc). Backends translate to BCD / vendor format / Unix epoch as needed.                                                                                    | LOW        | Already specified. `std::chrono::system_clock::time_point` at the `IClockCapable::setTime` boundary.                                                            |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature                                           | Why Requested                                     | Why Problematic                                                                                                                                                                                                                                                                | Alternative                                                                                                |
| ------------------------------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------- |
| Interval-based re-sync (every N minutes)          | "Keep clock accurate"                             | Adds queue management + power concerns; can't validate the need without a real backend that actually accepts `setTime`. Already explicitly deferred (design decision #4).                                                                                                      | Per-arrival re-push is the right unit of work. Revisit if/when a real backend lands and drift is measured. |
| Device → host time read-back                      | Symmetry / "feels complete"                       | Out of scope per design § Non-goals. Adds bi-directional state, conflict resolution ("device says 14:02, host says 14:01"). No vendor today exposes a get-time event anyway (vendor app inventory in `d5616ef` confirmed 12 sendable / 19 received events, none time-related). | One-way push only. Period.                                                                                 |
| Per-device time-zone offset setting               | "Different devices in different rooms"            | The host is the authoritative wall-clock; the device just displays. TZ logic belongs in the firmware's render path, not the sync path.                                                                                                                                         | UTC at the boundary; firmware/render-layer concern only.                                                   |
| World-clock / NTP client built into app           | Mission creep                                     | We're an HID control center, not a chronograph.                                                                                                                                                                                                                                | Use the host OS clock. Done.                                                                               |
| Per-device sync-history log surfaced in UI        | "Diagnose drift"                                  | Drift can't drift if `setTime` returns `NotImplemented` — premature for v1.1. Adds storage + display surface.                                                                                                                                                                  | Log to existing log facility; surface in a future "Device Diagnostics" pane if/when a real backend exists. |
| Render-time-on-keyface clock widget               | Conflates "sync RTC" with "show time on a button" | Different feature entirely. Uses the existing image-upload path (`IDisplayCapable::setKeyImage`), not `IClockCapable`. The design doc explicitly carves this out as future-work § Future-work alternative.                                                                     | Out of scope for this slice. Tracked separately in TODO.md.                                                |
| Toast that says "Time synced" on `NotImplemented` | Looks "successful"                                | Lies to the user; destroys trust.                                                                                                                                                                                                                                              | Exclamation glyph + tooltip explaining the actual state. Already specified.                                |

### Complexity tier summary (Time-sync)

- **LOW** (all five layers): every feature in this category is already designed, sized, and stub-only. The whole slice is intentionally small.
- **MEDIUM**: none.
- **HIGH**: none in v1.1. (Real wire-format implementations would be HIGH, but they're deferred to a future "Clock Widget" / per-family reverse-eng slice.)

### Per-device vs global toggle — recommendation

**Global toggle for auto-sync; per-device "Sync now" button.** The design doc already lands here, and it's the right call:

- Per-device auto-sync toggles would require a UI for 13 devices, most of which can't sync anyway. Cognitive overhead with no payoff.
- The global toggle is a single `QSettings` row; the per-device button is the per-device control. Mirrors how Loupedeck Control Center handles brightness (per-device control, global preferences page).

______________________________________________________________________

## Category 3 — Scaffolded-Device Wiring (Maturation Path)

### Table Stakes (Users Expect These)

| Feature                                                            | Why Expected                                                                                                                                                                              | Complexity | Notes / dependencies                                                                                                                   |
| ------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| Catalogued, recognised, but possibly "feature-stub" status         | OpenDeck and StreamController explicitly enumerate supported devices and degrade gracefully on unsupported ones. Users expect to see their device acknowledged even if not fully working. | LOW        | Already done — `DeviceDescriptor` registry catalogues all 13 devices. v1.0 retro-fit shipped this.                                     |
| Honest capability advertisement per device                         | If a backend says `hasRgb` but RGB doesn't work, users justifiably feel deceived. The existing capability flags pattern is the right discipline; extend it.                               | LOW        | Per-backend `capabilities()` method already exists. Discipline = "only set the bit when the feature actually round-trips on hardware." |
| Backend stubs return `Result::NotImplemented` (not crash, not lie) | Stream-deck reverse-engineering literature (Den Delimarsky's blogs, cliffrowley's HID gist) and the AJAZZ context all converge: stub gracefully, log clearly, never silently no-op.       | LOW        | Already the established pattern — see `aj_series.cpp` header `@warning`, and the time-sync design's stub recipe.                       |

### Differentiators (Competitive Advantage)

| Feature                                                                                         | Value Proposition                                                                                                                                                                                                                                                                 | Complexity     | Notes                                                                                                                                                                                                                     |
| ----------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Documented maturation tiers per device family                                                   | "Scaffolded → Probed → Partial → Functional → Verified" is the implicit OSS path; making it explicit (in `docs/_data/devices.yaml`) lets users set expectations and lets contributors find pickup work. None of the OSS comparators do this — they just list "supported" devices. | LOW-MEDIUM     | New YAML field `maturity: scaffolded\|probed\|partial\|functional\|verified`. Renders into the README and/or a Settings page. Already aligned with the `time_sync: scaffolded` capability flag pattern in the design doc. |
| Per-family "what works / what doesn't" inline in the UI                                         | Avoid the support-channel question "does X work with your software?" by answering it inside the app. Competitors push this to README; we can render it next to the device row.                                                                                                    | MEDIUM         | New `MaturityTooltipService` driven by the YAML.                                                                                                                                                                          |
| Reverse-eng artefacts (HID captures, decoded report fragments) committed alongside backend code | The OSS Stream Deck community (`cliffrowley/d18a9c4569537b195f2b1eb6c68469e0`, Den Delimarsky's blogs) demonstrate this is gold for future maintainers. We've already started in `~/MEGAsync/Ajazz/` and `docs/protocols/streamdeck/`.                                            | LOW per device | Each scaffolded → functional promotion gets a `docs/protocols/<family>/<device>.md` with capture excerpts, report-ID table, ack patterns. Already the precedent.                                                          |
| Capability discovery probe at runtime                                                           | When backend opens device, send a benign read and confirm vendor/product/firmware string matches descriptor. Surface mismatches. Competitors silently accept anything matching VID:PID.                                                                                           | MEDIUM         | New `IDevice::probe()` hook returning a probe record. Doesn't gate `open()`; just emits a `deviceProbed` signal the model can show.                                                                                       |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature                                                                     | Why Requested                                        | Why Problematic                                                                                                                                                                                                                                                                                                                      | Alternative                                                                                                                                                                                 |
| --------------------------------------------------------------------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Big-bang "promote all 7 scaffolded devices to functional in this milestone" | "Symmetry" / "looks complete"                        | 7 backends × full feature parity is months of clean-room reverse-eng. Will either ship buggy or slip the milestone. Cliff Rowley's notes + Den Delimarsky's 2-blog Plus reverse-eng both took weeks per *single* device.                                                                                                             | Pick 1-2 promising candidates (look for stream-deck-family devices similar to already-functional AKP153 — wire format likely shares ancestry). Promote those. Document the path for others. |
| Generic "vendor app HID replay" as the maturation tactic                    | "Just record what the vendor app does and replay it" | Legally murky outside clean-room methodology ([Sedona Conference commentary](https://www.thesedonaconference.org/sites/default/files/publications/Commentary_on_Use_of_Clean_Rooms_0.pdf)). Also brittle — vendor-app changes break us. Den Delimarsky's reverse-eng explicitly worked from observed behaviour, not decompiled code. | Two-team clean-room: one captures + writes specs, separate team implements from specs only. Document the discipline in `docs/protocols/<family>/<device>.md`.                               |
| Marketing the device list with "supported" without qualifiers               | Sales pressure                                       | Users discover the gap, write angry issues, trust erodes (see Loupedeck Control Center user complaints in [DPReview thread](https://www.dpreview.com/forums/thread/4706392)).                                                                                                                                                        | "Catalogued: 13. Functional: 6. Scaffolded: 7. Per-device matrix in README." Already roughly the AJAZZ ACC public posture; tighten the language.                                            |
| Speculative descriptor entries for unreleased AJAZZ devices                 | "Future-proofing"                                    | Pollutes the catalogue with un-tested code paths, increases the maintenance surface for zero current user benefit.                                                                                                                                                                                                                   | Add devices when at least one developer has hardware. Catalogue ≠ wishlist.                                                                                                                 |
| Telemetry "phone home which devices users actually connect"                 | "Prioritise the right devices"                       | Privacy regression, conflicts with the project's "no system-level mutations" posture (user-memory `feedback_no_system_mutations.md`).                                                                                                                                                                                                | GitHub issue template asking users to share VID:PID + device name. Opt-in, transparent.                                                                                                     |
| Vendor-driver bundling / wine'd installer in the app                        | "Make AKB980 PRO work today"                         | Vendor driver (V1.0.0.6 Delphi installer in `~/MEGAsync/Ajazz/`) is a Windows-only Innosetup payload. Bundling it is a licensing minefield + creates a wine dependency.                                                                                                                                                              | Stub backend now (per time-sync design decision #1); replace incrementally when clean-room reverse-eng produces a spec.                                                                     |

### Maturation path (the typical pattern in this domain)

Synthesised from the AJAZZ codebase precedent + Stream Deck reverse-eng literature ([Den Delimarsky](https://den.dev/blog/reverse-engineering-stream-deck/), [Den Delimarsky on Plus](https://den.dev/blog/reverse-engineer-stream-deck-plus/), [cliffrowley HID gist](https://gist.github.com/cliffrowley/d18a9c4569537b195f2b1eb6c68469e0), [Stream Deck HID API](https://docs.elgato.com/streamdeck/hid/intro/)):

```
Tier 0 — Catalogued
  • VID:PID registered, descriptor entry, capability bits = none/all-false.
  • Hot-plug recognises it; backend is empty shell.
  • Cost: ~30 minutes per device.

Tier 1 — Probed
  • USB descriptor dump captured (lsusb -v / Windows USBView).
  • Vendor app traffic captured (Wireshark+usbmon / Wireshark with USBPcap).
  • Initial report-ID table sketched in docs/protocols/.
  • Cost: ~half a day per device.

Tier 2 — Partial (minimal feature subset)
  • One or two primary features working — for stream-decks: setKeyImage + setBrightness.
  • For keyboards: layer-switch / macro recall.
  • Backend implements IDisplayCapable or IInputCapable; advertises one Capability bit honestly.
  • Cost: 2-5 days per device, dominated by capture-decode loop.

Tier 3 — Functional (full primary feature parity)
  • All buttons / encoders / displays usable.
  • Sleep / wake / brightness / firmware-version readback all work.
  • Cost: another 3-7 days; this is where edge cases bite (long-press, encoder rotation packing, etc.).

Tier 4 — Verified (parity-with-vendor, regression-tested)
  • Integration tests pass against real hardware.
  • Stress-tested under hot-plug churn.
  • Edge cases documented.
  • Cost: variable; aim for "we'd ship this to a streamer."
```

**For v1.1 specifically, the recommendation is to promote 1-2 scaffolded devices Tier 0 → Tier 2** (Partial), not Tier 0 → Tier 4. Phase planning picks which.

**Best candidates for promotion** (from PROJECT.md context: 7 scaffolded, of which the stream-dock family already has 3 functional sisters — AKP153 / AKP03 / AKP05):

- Any stream-dock-family scaffolded device — wire format is highly likely to share ancestry with the working AKP siblings. Probe + diff is the fast path.
- AKB980 PRO is **not** the best candidate for v1.1 — different family, vendor driver is a Delphi installer, dev env lacks `wine`/`innoextract`. Stub stays for v1.1.

### Complexity tier summary (Scaffolded-device wiring)

- **LOW** (≤ 1 day each): maturation tier YAML field, devices.yaml updates, documentation skeleton per family, capability-honesty audit on existing functional backends.
- **MEDIUM** (3-7 days per device): Tier 0 → Tier 2 promotion of a stream-dock sibling; new `probe()` hook; per-family `docs/protocols/<family>/<device>.md` filled to "minimal feature subset" detail.
- **HIGH** (multiple weeks per device): Tier 2 → Tier 4 promotion (full parity) — explicitly NOT v1.1 scope.

______________________________________________________________________

## Feature Dependencies

```
Stable sidebar sort                Capability::Clock bit         Maturity tier YAML
        │                                  │                              │
        │ enables                          │ enables                      │ enables
        ▼                                  ▼                              ▼
Focus retention on disconnect     hasClock model role            Per-device "what works" tooltip
        │                                  │                              │
        │ enables                          │ enables                      │
        ▼                                  ▼                              ▼
Reconnect-counter / health      "Sync now" button visibility     Documented maturation path
        │                                  │                              │
        └──────── all gate on ──────► DeviceModel role-extension pattern
                                            │
                                            │ which builds on
                                            ▼
                   Existing capability flags (hasRgb, hasTouchStrip)
                                            │
                                            │ surfaced via
                                            ▼
                                  HotplugMonitor (2026-05-12/13 rework)

Auto-sync on connect ──requires──► HotplugMonitor::deviceArrived  +  TimeSyncService
                                            │
                                            └──enhances──► Silent auto-reconnect (Cat 1)
                                                           [auto-sync is a natural rider on rebind]

Probe hook ──enhances──► Maturity tier accuracy
            ──enhances──► Capability honesty (Cat 3 anti-feature mitigation)
            ──conflicts with──► Aggressive auto-retry loop (Cat 1 anti-feature)
                                [both touch the open() path; sequencing matters]
```

### Dependency notes

- **All three categories depend on the `DeviceModel` role-extension pattern.** v1.1 adds at minimum: `HasClockRole` (Cat 2), `ConnectionStateRole` + `LastSeenRole` (Cat 1), `MaturityRole` (Cat 3). One PR can introduce all three roles consistently; doing them piecemeal risks the QML_SINGLETON dual-instance bug class re-surfacing.
- **Auto-sync (Cat 2) is a natural rider on silent auto-reconnect (Cat 1).** Both fire on `HotplugMonitor::deviceArrived`. If reconnect is debounced (300-500 ms), auto-sync can debounce on the same primitive. Sequence reconnect first.
- **The new probe hook (Cat 3 differentiator) and the auto-retry-on-disconnect anti-feature (Cat 1) both touch `IDevice::open()`.** If `probe()` is added, ensure it's called once per arrival, not in a retry loop. Otherwise probe churn re-creates the flapping-storm anti-pattern.
- **Hot-plug hardening (Cat 1) must land before scaffolded promotion (Cat 3) integration tests.** Promoted backends will be exercised by the multi-device test harness, which itself depends on the debounced hotplug primitive.
- **CR-01 (Win32 OOP host env pollution) and WR-01 (loadTrustRoots parser) are listed in PROJECT.md as v1.1 carry-overs.** Neither is in this research's three categories, but WR-01 has an unresolved architectural decision (`nlohmann::json` vs. custom scanner vs. accept COD-031). That decision should be made before phase planning, not deferred into a phase.

______________________________________________________________________

## MVP Definition (for v1.1, scoped relative to existing v1.0)

### Launch With (v1.1 — must-have)

- [ ] **Silent auto-reconnect with stable sort + focus retention** — the headline hot-plug fix users will notice. Without this, the 2026-05-12/13 rework feels half-done.
- [ ] **Disconnect/reconnect toast (non-blocking) + offline badge on sidebar row** — minimum visible feedback so users know what just happened.
- [ ] **Multi-device baseline test harness** — explicitly named in PROJECT.md milestone goal; gates the rest of the milestone.
- [ ] **Time-sync five-layer scaffolding (per approved design)** — `Capability::Clock` + `IClockCapable` + `TimeSyncService` + `hasClock` model role + QML toggle + per-row button + docs. All stubs return `NotImplemented`.
- [ ] **Maturity-tier YAML field on devices.yaml + README regeneration** — frames the rest of the device-wiring effort honestly.
- [ ] **CR-01 fix (Win32 env pollution)** — per PROJECT.md, requires Windows validation in this milestone.
- [ ] **WR-01 architectural decision + implementation** — per PROJECT.md.

### Add After Validation (v1.1.x patch series)

- [ ] **Promote 1-2 scaffolded stream-dock devices Tier 0 → Tier 2** — depends on which families show clean probe results. Phase planning picks.
- [ ] **Debounced reconnect with reconnect-counter badge** — addresses real-world flapping; nice-to-have only if user reports show it matters.
- [ ] **Per-device "auto-rebind profile" toggle** — power-user opt-out; only add if requested.

### Future Consideration (v1.2+)

- [ ] **Real wire-format implementations behind `IClockCapable::setTime()`** — waits for either a firmware that actually exposes RTC, OR a separate "Clock Widget" host-side feature (image-upload path).
- [ ] **Capability discovery probe (`IDevice::probe()`)** — useful but additive; not blocking for v1.1.
- [ ] **Per-family `docs/protocols/<family>/<device>.md` full reverse-eng writeups** — incrementally; not a v1.1 deliverable.
- [ ] **Tier 2 → Tier 4 promotion of any scaffolded device** — months of work per device.
- [ ] **Render-time-on-keyface "Clock Widget"** — separate slice per time-sync design § Future-work alternative.
- [ ] **Device → host time read-back** — only if a real backend ever exposes it.

______________________________________________________________________

## Feature Prioritization Matrix

| Feature                                               | User Value                       | Implementation Cost           | Priority                            |
| ----------------------------------------------------- | -------------------------------- | ----------------------------- | ----------------------------------- |
| Silent auto-reconnect                                 | HIGH                             | LOW                           | **P1**                              |
| Sidebar offline-badge + stable sort + focus retention | HIGH                             | LOW-MEDIUM                    | **P1**                              |
| Disconnect toast (non-blocking)                       | MEDIUM                           | LOW                           | **P1**                              |
| Time-sync five-layer scaffolding                      | MEDIUM (current) / HIGH (future) | LOW                           | **P1** (low cost + unblocks future) |
| Maturity-tier YAML + README                           | MEDIUM                           | LOW                           | **P1**                              |
| Multi-device baseline test harness                    | MEDIUM (users) / HIGH (project)  | MEDIUM                        | **P1**                              |
| CR-01 Win32 env fix                                   | HIGH (Windows users)             | MEDIUM                        | **P1** (per PROJECT.md)             |
| WR-01 parser hardening                                | MEDIUM                           | MEDIUM (needs decision first) | **P1** (per PROJECT.md)             |
| Promote 1-2 scaffolded → Tier 2                       | HIGH (those users)               | MEDIUM                        | **P2**                              |
| Debounced reconnect + counter badge                   | LOW-MEDIUM                       | MEDIUM                        | **P2**                              |
| Per-device auto-rebind toggle                         | LOW                              | LOW                           | **P2**                              |
| Reconnect-history / "last seen" tooltip               | LOW                              | LOW                           | **P2**                              |
| Capability `probe()` hook                             | MEDIUM                           | MEDIUM                        | **P3**                              |
| Per-family full reverse-eng writeups                  | MEDIUM                           | HIGH (per device)             | **P3**                              |
| Tier 2 → Tier 4 promotion of any device               | HIGH                             | HIGH                          | **P3**                              |
| Real `IClockCapable::setTime` wire format             | LOW (no firmware exposes it)     | HIGH                          | **P3** (blocked on firmware)        |
| Clock Widget (image-upload render)                    | MEDIUM                           | HIGH                          | **P3** (separate slice)             |

**Priority key:**

- **P1**: v1.1 must-have. PROJECT.md milestone goal directly references these or they're prerequisites.
- **P2**: v1.1 nice-to-have / v1.1.x patch series. Schedule if time permits.
- **P3**: Future milestones. Not v1.1.

______________________________________________________________________

## Competitor Feature Analysis

| Feature                                  | Elgato Stream Deck app                                                                                                                                | streamdeck-linux-gui                     | OpenDeck                  | StreamController          | Loupedeck Control Center                    | AJAZZ vendor app (Mirabox rebrand)                 | Our v1.1 plan                                                                                     |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- | ------------------------- | ------------------------- | ------------------------------------------- | -------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Auto-reconnect on replug                 | Yes (silent)                                                                                                                                          | Yes ("automatically and gracefully")     | Implied (no explicit doc) | Yes                       | Yes (with `reconnectInterval`, default 3 s) | Unknown (Vue-in-Electron, not deeply mapped)       | **Match** — debounced, silent, badge                                                              |
| Multi-device sidebar                     | Yes ([help.elgato.com](https://help.elgato.com/hc/en-us/articles/4424235832717-Elgato-Stream-Deck-Use-multiple-Stream-Deck-devices-at-the-same-time)) | Yes                                      | Yes                       | Yes                       | Yes                                         | Yes (per AJAZZ docs)                               | **Match** — stable sort key already exists                                                        |
| Disconnect notification UX               | Silent + ghost row                                                                                                                                    | Silent                                   | Silent                    | Silent                    | Toast + reconnect attempt                   | Unknown                                            | **Match** — non-blocking toast + offline badge                                                    |
| Per-device profile auto-binding          | Yes (Smart Profiles, per-app + per-device)                                                                                                            | Yes (configuration persisted per-device) | Yes (per-device dropdown) | Yes                       | Yes                                         | Yes (multi-profile per device)                     | **Match** — keyed by `codename`, already canonical wire format                                    |
| Time-sync to device                      | No (Stream Deck clocks are rendered host-side via image upload, per [d5616ef](.) inventory)                                                           | No                                       | No                        | No                        | No                                          | No (vendor "clock widget" is host-side Vue render) | **Exceed** — scaffolding ready for future firmware support; `Result::NotImplemented` honest today |
| Capability flags per device              | Yes (SDK exposes device type)                                                                                                                         | Implicit                                 | Implicit                  | Yes                       | Yes                                         | Yes                                                | **Match** (existing pattern); **exceed** by adding maturity tier                                  |
| Maturity tier per device family (public) | No                                                                                                                                                    | No (lists supported only)                | No                        | No (lists supported only) | No                                          | No                                                 | **Differentiator** — honest scaffolded/partial/functional/verified                                |
| Reconnect-counter / health badge         | No                                                                                                                                                    | No                                       | No                        | No                        | No (users complain about flapping)          | No                                                 | **Differentiator** — P2                                                                           |
| Probe-on-open verification               | No (silent VID:PID accept)                                                                                                                            | No                                       | No                        | No                        | No                                          | No                                                 | **Differentiator** — P3                                                                           |
| Modal dialog on disconnect               | No (rightly so)                                                                                                                                       | No                                       | No                        | No                        | No                                          | No                                                 | **Anti-feature** — explicitly NOT building                                                        |
| Bundled vendor-driver installer          | N/A (first-party)                                                                                                                                     | No                                       | No                        | No                        | N/A (first-party)                           | Self-extracting installer (V1.0.0.6 Delphi)        | **Anti-feature** — explicitly NOT building; stub backends instead                                 |

______________________________________________________________________

## Quality-gate self-check

- [x] **Categories clear** — three sections, one per question (Hot-plug UX, Time-sync UX, Scaffolded-device wiring).
- [x] **Complexity noted per feature** — every Table-Stakes / Differentiator / Anti-Feature table has a Complexity column; per-category summary at the end of each section.
- [x] **Dependencies on existing features identified** — every feature table's Notes column references the existing primitive (`DeviceModel` role pattern, `Capability` bit-flags, `HotplugMonitor`, `Profile::deviceCodename` wire key, `QML_SINGLETON` discipline, 300 ms debounce primitive). Cross-category dependency diagram in § Feature Dependencies.
- [x] **Table stakes vs differentiators vs anti-features explicit** — Requirements stage can lift each table directly.
- [x] **Tied to PROJECT.md milestone goals** — every P1 in the prioritisation matrix maps to a PROJECT.md v1.1 target feature or carry-over (CR-01, WR-01).
- [x] **Honest about uncertainty** — confidence MEDIUM-HIGH overall; AJAZZ vendor app feature-mapping flagged LOW; Loupedeck disconnect UX flagged "documented bugs exist."

______________________________________________________________________

## Sources

**Competitor / ecosystem analysis:**

- [Elgato Stream Deck — Use multiple Stream Deck devices at the same time](https://help.elgato.com/hc/en-us/articles/4424235832717-Elgato-Stream-Deck-Use-multiple-Stream-Deck-devices-at-the-same-time) — multi-device sidebar baseline
- [Devices | Stream Deck SDK](https://docs.elgato.com/streamdeck/sdk/guides/devices/) — disconnect events, ghost-row behaviour
- [Elgato Stream Deck — Smart Profiles](https://help.elgato.com/hc/en-us/articles/360053419071-Elgato-Stream-Deck-Smart-Profiles) — per-device + per-app profile auto-binding
- [streamdeck-linux-gui (GitHub)](https://github.com/streamdeck-linux-gui/streamdeck-linux-gui) — "auto reconnect: automatically and gracefully reconnects" quote
- [StreamController (GitHub)](https://github.com/StreamController/StreamController) — multi-device + plugin GTK4 reference
- [Boatswain — Apps for GNOME](https://apps.gnome.org/Boatswain/) — GNOME-native Stream Deck baseline
- [OpenDeck (GitHub)](https://github.com/nekename/OpenDeck) — plugin compatibility breadth
- [Loupedeck npm package](https://www.npmjs.com/package/loupedeck) — `reconnectInterval` default 3 s, disconnect event semantics
- [Loupedeck support — device not connecting](https://support.loupedeck.com/device-not-connecting) — real-world flapping UX issues
- [bitfocus/companion #1564](https://github.com/bitfocus/companion/issues/1564), [#2735](https://github.com/bitfocus/companion/issues/2735), [#2795](https://github.com/bitfocus/companion/issues/2795) — disconnect flapping bug reports
- [AJAZZ AKP153E product page](https://ajazzbrand.com/products/ajazz-akp153-desk-controller) — first-party feature claims
- [Ajazz Software Guide](https://financesofttech.com/ajazz-software-guide/) — multi-mode connectivity, macro engine

**Reverse-engineering / maturation path:**

- [Reverse Engineering The Stream Deck — Den Delimarsky](https://den.dev/blog/reverse-engineering-stream-deck/)
- [Reverse Engineering The Stream Deck Plus — Den Delimarsky](https://den.dev/blog/reverse-engineer-stream-deck-plus/)
- [Notes on the Stream Deck HID protocol — cliffrowley gist](https://gist.github.com/cliffrowley/d18a9c4569537b195f2b1eb6c68469e0)
- [Stream Deck HID API — Introduction](https://docs.elgato.com/streamdeck/hid/intro/)
- [Clean-room design — Wikipedia](https://en.wikipedia.org/wiki/Clean_room_design)
- [Sedona Conference — Commentary on Use of Clean Rooms (PDF)](https://www.thesedonaconference.org/sites/default/files/publications/Commentary_on_Use_of_Clean_Rooms_0.pdf)
- [Retro Reversing — Legality of Reverse Engineering & Clean Room Reversing](https://www.retroreversing.com/clean-room-reversing)

**UX / notification design references:**

- [NN/g — Indicators, Validations, and Notifications](https://www.nngroup.com/articles/indicators-validations-notifications/)
- [LogRocket — Toast notifications UX best practices](https://blog.logrocket.com/ux-design/toast-notifications/)
- [Carbon Design System — Notification pattern](https://carbondesignsystem.com/patterns/notification-pattern/)
- [Microsoft Learn — Toast UX guidance](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/toast-ux-guidance)

**Internal sources (canonical for this project):**

- `.planning/PROJECT.md` — v1.1 milestone goal, target features, key constraints
- `docs/superpowers/specs/2026-05-13-time-sync-design.md` — approved time-sync design (Design A, pre-emptive scaffolding)
- Commit `d5616ef` — vendor-app inventory, no firmware time-sync exists today
- User-memory `feedback_no_system_mutations.md` — no telemetry, no system-config writes
- User-memory `project_wire_format_convention.md` — `Profile::deviceCodename` ⇄ `"device"` wire key

______________________________________________________________________

*Feature research for: AJAZZ Control Center v1.1 (subsequent milestone)*
*Researched: 2026-05-13*
