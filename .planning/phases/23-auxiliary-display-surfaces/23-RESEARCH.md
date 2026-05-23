# Phase 23: Auxiliary Display Surfaces - Research

**Researched:** 2026-05-23
**Domain:** AKP05E Stream-Dock-Plus aux-surface image wiring (Qt6 C++20, app-layer) over an already-shipping device backend
**Confidence:** HIGH (backend code + tests read directly in-tree; wire bytes pinned by existing MockTransport tests). Per-surface device *framing* is PROVISIONAL (hardware confirm = Phase 25).

\<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Reuse the backend wire methods (do NOT add new builders):**

- Drive the existing `Akp05Device::setMainImage` / `setEncoderImage` / `setTouchStripImage` + the DRA
  builder (`buildSecondaryScreenHeader` / `CmdSecondaryScreen`) through the **Phase-14 control
  service** (held-open handle) + the **Phase-16 editor** (assignment targets beyond keys). Encode via
  `image_pipeline` (encoder 100×100, main 800×100). The wire format is RE-sourced — do NOT alter it.

**Provisional-framing reconciliation (the load-bearing honesty deliverable):**

- The code models per-encoder **`ENC` LCDs (100×100)**; akp05.md states the AKP05E has **no separate
  encoder LCDs** — the per-encoder graphics render as **4 zones of the 800×480 touch strip**. Phase 23
  ships the wiring for BOTH the `ENC` path (as-coded) AND the touch-strip-zone path (via `DRA`), and
  **flags this divergence as PROVISIONAL** (§5). Do NOT delete the `ENC` path blind — the RE is the
  source of truth and the **hardware wins** in Phase 25, where the doc is reconciled. Treat the DRA
  rect layout (location/width/height/x/y) as a hypothesis to verify.

**Hardware gating:**

- HARDWARE-GATED: requires the physical AKP05E (`0300:3004`, fw `V3.AKP05E.01.007`) with a working
  `uaccess` ACL for the LIVE confirmation (Phase 25; replug/`setfacl` if root-only — systemd ≥258).
  Phase 23's automated gate is `MockTransport` byte-level assertions (ENC index byte, MAI whole-strip,
  DRA rect header) — no hardware needed to land the wiring + tests.

### Claude's Discretion (planner/executor)

- Editor UX for assigning to encoder/strip/touch surfaces (extend the Phase-16 editor with per-surface
  targets).
- Whether to expose `DRA` partial-zone upload now or wire it behind the per-encoder-overlay path.
- How `setTouchStripImage` (declared in `IDisplayCapable`) maps to the wire (confirm it's implemented
  in the backend vs needs wiring). — **RESOLVED below: `setTouchStripImage` lives on
  `ITouchStripDisplayCapable`, not `IDisplayCapable`, and is fully implemented + byte-tested.**

### Deferred Ideas (OUT OF SCOPE)

- Family coverage (AKP03/153/815 aux surfaces) → Phase 24.
- LIVE hardware confirmation + provisional §5 reconciliation (ENC-vs-zone, DRA rect) → Phase 25 (VERIFY-05).
- Boot logo (`LOG`) / `M_V` secondary-screen logo → backlog (vendor §10 P3). **Note: `LOG`/`setBootLogo`
  is already implemented in the backend (`IBootLogoCapable`); it is simply not in scope to wire here.**
  \</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                                                                                                                                                                                                                                   | Research Support                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| DISPLAY-10             | Aux surfaces accept assigned images — per-encoder overlays (rendered as the 4 touch-strip zones; reconcile in-code `ENC` model vs akp05.md "no separate encoder LCD"), main LCD strip, touch strip, incl. `DRA` rect-addressable partial-zone upload. Per-surface framing provisional until device-confirmed. | Backend fully implements all four paths (`setEncoderImage` ENC, `setMainImage` MAI, `setTouchStripImage`/`clearTouchStrip` DRA) and they are byte-tested (`test_akp05_touch_strip.cpp`). Phase 23 = **app-side wiring** of these into the control service + editor, plus a MockTransport gating test asserting the ENC index byte, MAI whole-strip, and DRA rect header. The ENC-vs-zone divergence is flagged PROVISIONAL; both paths are wired, neither deleted. |
| \</phase_requirements> |                                                                                                                                                                                                                                                                                                               |                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |

## Summary

The framing in the brief ("the device-backend wire methods already exist") **understates how done the
backend is**. Reading the code: **every** auxiliary-surface wire path the phase needs is already
*implemented, exposed through a capability mix-in, and byte-tested with MockTransport*. There are no
missing builders and no half-finished backend methods. Specifically:

- `setMainImage` (MAI, 800×100) — implemented (`akp05.cpp:604`), full encode→header→chunk→ULEND.
- `setEncoderImage` (ENC, 100×100, 0-based index at byte 12) — implemented (`akp05.cpp:635`), full path.
- `setTouchStripImage` / `clearTouchStrip` (DRA rect-addressable) — implemented
  (`akp05.cpp:715` / `:745`) on the `ITouchStripDisplayCapable` mix-in, delegating to the shared
  `setSecondaryScreenImage` helper (`:670`), byte-tested in `test_akp05_touch_strip.cpp`.
- `setBootLogo` (LOG) — implemented (`akp05.cpp:776`) on `IBootLogoCapable` (out of scope to wire).
- `buildUploadFinished` (ULEND) is emitted by *every* image burst inside `sendImage` (`:856`).

So Phase 23 is **not a backend phase**. It is an **app-layer wiring phase** identical in shape to
Phase 14: today **zero app code calls any aux-surface method** (`grep -rn setMainImage|setEncoderImage| setTouchStripImage src/app/` → 0 hits). The deliverable is to extend the **Phase-14
`StreamDockControlService`** (the held-open paint path) with `assignEncoderImage` / `assignMainImage` /
`assignTouchStripZone` reuse methods, repaint encoder/strip surfaces from the active `Profile`
(`Profile::encoders` already carries an `EncoderBinding::state` KeyState), and surface those targets to
the Phase-16 editor. The single gating proof is a MockTransport byte test (ENC index byte, MAI
whole-strip envelope, DRA rect header bytes).

**The load-bearing risk is a dependency order problem, not a technical one.** `StreamDockControlService`
**does not exist yet** — Phase 14 is *planned-not-executed* (`14-02-PLAN.md` exists, no SUMMARY, no
`src/app/src/stream_dock_control_service.{hpp,cpp}`). Phase 16 *also* extends the same not-yet-existent
file (`16-03-PLAN.md` `files_modified` lists it). Phase 23 has `depends_on: [14]`. **STOP gate:** if
Phase 14 (and ideally 16) have not executed, Phase 23 has no `StreamDockControlService` to extend and
must not start. See "STOP Gate / Dependency Order" below.

**Primary recommendation:** Plan Phase 23 as a thin extension of `StreamDockControlService` that adds
encoder/main/touch-strip-zone assign + repaint methods reusing the already-byte-tested backend
capabilities (NO new wire builders, NO edits to `akp05.cpp`/`akp05_protocol.hpp`), proves it with a
MockTransport wire test asserting the ENC/MAI/DRA bytes, ships BOTH the ENC and the DRA zone path while
flagging the divergence PROVISIONAL, and defers the live confirmation to Phase 25. Gate execution on
Phase 14 (and 16) having executed first.

## STOP Gate / Dependency Order (read before planning)

| Dependency                            | State (verified 2026-05-23)                                                                                                                                                                | Implication for Phase 23                                                                                                                                                                                                                   |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Phase 14 — `StreamDockControlService` | **Planned, NOT executed.** `14-02-PLAN.md` present; **no `14-02-SUMMARY.md`**; `src/app/src/stream_dock_control_service.{hpp,cpp}` **do not exist**; `grep setKeyImage src/app/` = 0 hits. | Phase 23 extends this service. If it does not exist, **STOP** — there is nothing to wire into.                                                                                                                                             |
| Phase 16 — editor / page repaint      | **Planned, NOT executed.** `16-03-PLAN.md` `files_modified` *also* lists `stream_dock_control_service.{hpp,cpp}` (adds `repaintPage`).                                                     | The editor assignment UX (Claude's-discretion target for aux surfaces) lands in Phase 16. If 16 is unexecuted, Phase 23's editor-side targets have no editor to attach to — wire the *service* surface and defer/coordinate the editor UX. |
| Backend (`Akp05Device`)               | **Executed and shipping.** All aux methods implemented + byte-tested.                                                                                                                      | No backend work. Reuse only.                                                                                                                                                                                                               |

**Action for the planner:** the first wave (or a `checkpoint:human-verify` task) must confirm
`stream_dock_control_service.{hpp,cpp}` exist and the Phase-14 MockTransport service test is green
before any aux-surface wiring task runs. Read `14-02-SUMMARY.md` for the chosen profile-accessor seam,
the 1-based keyIndex mapping, and the default-brightness constant — Phase 23 must reuse the *same* seam
and mapping, not invent a parallel one. (If 14/16 are executed in the same milestone run before 23, this
is satisfied automatically; the gate exists to catch an out-of-order standalone run.)

## Architectural Responsibility Map

| Capability                                        | Primary Tier                                        | Secondary Tier                       | Rationale                                                                                                                                   |
| ------------------------------------------------- | --------------------------------------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Encode RGBA→JPEG at surface dims                  | Device backend (`image_pipeline`, PRIVATE-linked)   | —                                    | ARCH-04: single host-side encode path; app never JPEG-encodes. App passes RGBA8 + dims; backend resizes + encodes.                          |
| ENC / MAI / DRA wire framing + chunk + ULEND      | Device backend (`akp05.cpp` / `akp05_protocol.hpp`) | —                                    | RE-sourced wire format; LOCKED do-not-alter. Already byte-tested.                                                                           |
| Hold the device open for the session              | App service (`StreamDockControlService`, Phase 14)  | —                                    | Single held `shared_ptr<IDevice>` (Pitfall 2). The aux paint path piggybacks on this handle.                                                |
| Coalesce/queue aux-surface writes                 | App service (`StreamDockControlService`)            | —                                    | Mirror the Phase-14 single-shot `QTimer` drain for key images; reuse for encoder/strip bursts to avoid firmware desync (DOCK-02 class bug). |
| Map `Profile::encoders[i].state` → device surface | App service (repaint path)                          | App editor (assignment UX, Phase 16) | Profile already carries `EncoderBinding::state` (a `KeyState` with imagePath/background). Repaint iterates it; editor authors it.           |
| Render KeyState → RGBA (load image / solid fill)  | App service                                         | —                                    | Same QImage-load/solid-fill logic the Phase-14 key repaint uses; reuse, do not duplicate.                                                   |
| Per-encoder-overlay → touch-strip zone mapping    | App service (zone geometry)                         | RE doc (PROVISIONAL framing)         | The encoder-zone→rect math (x = zone·200, w = 200) is a host-side decision; the *device acceptance* of that rect is provisional → Phase 25. |
| Editor target rows for encoder/strip surfaces     | App editor (QML, Phase 16)                          | App service                          | Discretionary UX; attaches to the service's new public assign methods.                                                                      |

## Standard Stack

No external packages. This is in-tree C++20 / Qt6 against the existing device backend and app service
layer. The "stack" is the project's own modules:

| Module                                                                           | Role                                                                                                                                                                                                    | Status                         |
| -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------ |
| `ajazz_core` (`capabilities.hpp`)                                                | `IDisplayCapable` (setMainImage), `IEncoderCapable` (setEncoderImage), `ITouchStripDisplayCapable` (setTouchStripImage/clearTouchStrip), `IBootLogoCapable` (setBootLogo) — all Qt-free, COD-031-clean. | shipping                       |
| `ajazz_devices_streamdeck` (`akp05.cpp`, `akp05_protocol.hpp`, `image_pipeline`) | All aux-surface wire builders + chunked upload + ULEND + the RGBA→JPEG encode.                                                                                                                          | shipping, byte-tested          |
| `StreamDockControlService` (`src/app/src/`)                                      | The held-open app paint path to extend.                                                                                                                                                                 | **Phase 14 — NOT YET CREATED** |
| Qt6 `QImage`/`QImageWriter` (via `image_pipeline.cpp`, PRIVATE)                  | Host-side resize + JPEG encode.                                                                                                                                                                         | shipping                       |
| Catch2 + `MockTransport` (`tests/unit/fixtures/mock_transport.hpp`)              | Hardware-free wire-byte assertions (the Phase-23 gating proof).                                                                                                                                         | shipping                       |

**Installation:** none — `cmake --preset linux-release && cmake --build --preset linux-release`.

## Package Legitimacy Audit

**Not applicable.** Phase 23 installs no external packages (pure in-tree C++/Qt). No npm/PyPI/cargo
dependency is added. The Package Legitimacy Gate is N/A for this phase.

## Backend Capability Inventory (DONE vs MISSING) — answers the brief's investigation targets

### Target 1 — Backend aux methods: DONE or MISSING?

| Surface                 | Capability mix-in                               | Backend impl                                                      | Wire shape                                                                                              | Status                                                                                                                  |
| ----------------------- | ----------------------------------------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Main LCD strip (MAI)    | `IDisplayCapable::setMainImage`                 | `akp05.cpp:604`                                                   | `buildMainImageHeader` → BE16 size at bytes 10..11, no index; chunks; ULEND                             | **DONE** (encode 800×100 via `akp05MainTransform`) [VERIFIED: read akp05.cpp:604-613]                                   |
| Per-encoder LCD (ENC)   | `IEncoderCapable::setEncoderImage`              | `akp05.cpp:635`                                                   | `buildEncoderImageHeader` → BE16 size at 10..11, **0-based encoder index at byte 12**; chunks; ULEND    | **DONE** (encode 100×100 via `akp05EncoderTransform`; range-checks `< EncoderCount`) [VERIFIED: read akp05.cpp:635-651] |
| Touch strip (DRA, rect) | `ITouchStripDisplayCapable::setTouchStripImage` | `akp05.cpp:715` → delegates to `setSecondaryScreenImage` (`:670`) | `buildSecondaryScreenHeader` → BE32 size 8..11, location byte 12, BE16 w/h/x/y at 13..20; chunks; ULEND | **DONE + byte-tested** (`test_akp05_touch_strip.cpp:130`) [VERIFIED: read akp05.cpp:670-743]                            |
| Touch strip clear       | `ITouchStripDisplayCapable::clearTouchStrip`    | `akp05.cpp:745`                                                   | DRA full-panel black (800×480 @ 0,0)                                                                    | **DONE + byte-tested** (`test_akp05_touch_strip.cpp:227`)                                                               |
| Boot logo (LOG)         | `IBootLogoCapable::setBootLogo`                 | `akp05.cpp:776`                                                   | `buildLogoSizeHeader` → BE16 size at 10..11; chunks; ULEND                                              | **DONE** (out of scope to wire — backlog) [VERIFIED: read akp05.cpp:776-790]                                            |

**`Akp05Device` inherits all of:** `IDevice, IDisplayCapable, IEncoderCapable, IClockCapable, IBootLogoCapable, ITouchStripDisplayCapable` (`akp05.cpp:409-414`). **Resolved discretion question:**
`setTouchStripImage` is NOT on `IDisplayCapable` — it is on **`ITouchStripDisplayCapable`**
(`capabilities.hpp:1531`), and it is fully implemented and byte-tested. The brief's "is it implemented
or only declared?" is answered: **implemented**. There is **no missing backend method** to write.

### Target 2 — DRA rect header layout (PROVISIONAL §5), reported verbatim

From `akp05_vendor.md §2` (DRA row) and pinned in `akp05_protocol.hpp:213-226` and `akp05.cpp:106-129`.
The on-wire 1024-byte header (offsets are *buffer* offsets, report-ID stripped upstream — POSIX prepends
report-ID 0x00 at write time per `prependReportIdPosix=true`):

```
byte  0..2  : "CRT"  (0x43 0x52 0x54)        prefix
byte  3..4  : 0x00 0x00                       reserved
byte  5..7  : "DRA"  (0x44 0x52 0x41)        CmdSecondaryScreen
byte  8..11 : BE32 JPEG payload size
byte  12    : location  (zone discriminator; 0x12 is RESERVED for the M_V boot-logo variant — refused)
byte  13..14: BE16 rect width
byte  15..16: BE16 rect height
byte  17..18: BE16 rect x origin
byte  19..20: BE16 rect y origin
byte  21..511 (..1023): 0x00 padding
```

\[CITED: docs/protocols/streamdeck/akp05_vendor.md §2 "DRA" row — derived from Ghidra
`SDDevice::getSecondaryScreenPicInfo` @ 0x18001e310, 2026-05-17\]
**PROVISIONAL** — every byte is a Ghidra-derived hypothesis; no live AKP05E DRA capture exists yet.
**Hardware wins in Phase 25.** The header *bytes our code emits* are pinned by `test_akp05_touch_strip.cpp`
(width 200=0x00C8 at 13..14, height 100=0x0064 at 15..16, x=0x0064 at 17..18, y=0x00C8 at 19..20), so a
Phase-25 capture that contradicts the layout breaks those CHECKs and forces the doc+code update — by
design.

**Encoder-zone → rect mapping (host-side, also provisional):** the strip is 800×480 split into 4 zones
aligned to the 4 encoders. The natural mapping for encoder `i` (0..3) is `x = i*200, width = 200, height = 480` (or a ~100px band like the legacy `MainDisplayHeightPx=100` if only a label band is drawn).
`akp05_protocol.hpp:86-92` documents "Stream Deck Plus uses 200×100 per encoder; the Mirabox N4 strip is
4 × (200×480)". **The exact per-zone resolution and y-origin are PROVISIONAL** (the header comment says
"100×100 for backwards-compat … until the v3 capture makes the real per-zone resolution explicit"). The
planner should pick `x = zoneIndex*200, w = 200` and flag the height/y as a Phase-25 confirm item; do not
hard-code a "correct" value as if verified.

### Target 3 — ENC-vs-zone divergence: what each path emits, which to wire

| Path                       | Opcode                   | What the wire carries                                   | Where it targets (per code)                                                   | Where it targets (per akp05.md hardware claim)                                                         |
| -------------------------- | ------------------------ | ------------------------------------------------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `setEncoderImage`          | **ENC** (0x45 0x4E 0x43) | 100×100 JPEG, 0-based encoder index at byte 12          | "a dedicated 100×100 LCD above each encoder" (`akp05.cpp:7-8` header comment) | **There is no separate encoder LCD** — overlays are 4 zones of the 800×480 touch strip (`akp05.md:24`) |
| `setTouchStripImage` (DRA) | **DRA**                  | rect-addressable JPEG to (x,y,w,h) on the 800×480 strip | one of the 4 touch-strip zones                                                | the *correct* mechanism per akp05.md + vendor §10 P0 ("DRA enables 4-zone-aligned partial updates")    |

**Divergence (the honesty deliverable):** the in-code model says ENC writes to a physical per-encoder
LCD; akp05.md says that LCD doesn't exist and per-encoder graphics are touch-strip zones drawn via DRA.
**Both paths are real opcodes in the vendor SDK** (`akp05_vendor.md §2` lists ENC *and* DRA as separate
host→dev commands). We cannot know from RE alone whether the AKP05E firmware (a) honors ENC by drawing
into a strip zone internally, (b) ignores ENC entirely, or (c) has a real encoder LCD after all.
**LOCKED instruction: ship BOTH, delete NEITHER, flag PROVISIONAL.** Phase 25's live witness decides
which is the real per-encoder mechanism and updates the RE doc + code then. For Phase 23 wiring:
**wire the per-encoder-overlay user intent through DRA zones** (the vendor-preferred P0 path) *and* keep
`setEncoderImage`/ENC reachable (it's already wired into `IEncoderCapable`); the editor/service can offer
both or default to DRA-zone with ENC as the documented fallback. Document the choice; do not remove ENC.

### Target 4 — App-side wiring gap

`grep -rn "setMainImage\|setEncoderImage\|setTouchStripImage\|setSecondaryScreenImage" src/app/` →
**ZERO hits.** Exactly the Phase-14 pattern: capture-verified wire layer, no app caller. The wiring path:

- **Phase-14 control service** (`StreamDockControlService`, once it exists): add public reuse methods
  e.g. `assignEncoderImage(idx, QImage)`, `assignMainImage(QImage)`, `assignTouchStripZone(zone, QImage)`
  that route through the held `shared_ptr<IDevice>` via `dynamic_cast<IEncoderCapable*>` /
  `dynamic_cast<IDisplayCapable*>` / `dynamic_cast<ITouchStripDisplayCapable*>` (null-check within 3
  lines, Pitfall 1), reusing the existing coalesced `QTimer` drain.
- **Repaint from profile:** extend `repaintFromProfile`/`repaintPage` to also iterate
  `Profile::encoders` (each `EncoderBinding::state` is a `KeyState` with `imagePath`/`background`) and
  paint each encoder's overlay (via DRA zone and/or ENC). `Profile` has `keys` and `encoders` maps; there
  is **no dedicated touch-strip binding field** today — a whole-strip image is not a per-control binding,
  so the strip-image assignment is more an editor action than a profile repaint (note for the planner).
- **Phase-16 editor** (`16-03`, also unexecuted): adds the assignment-target rows. Discretionary UX.

### Target 5 — Tests (hardware-free, MockTransport)

The pattern is established and *already covers DRA*. `test_akp05_touch_strip.cpp` (read in full) asserts:
DRA header CRT/opcode bytes, BE32 size > 0, location byte, BE16 w/h/x/y at exact offsets, all chunks ==
`PacketSize` (1024), trailing ULEND `U L E N D` at bytes 5..9, out-of-range location → false + zero
writes, device-yank → false (not throw). `test_akp05_protocol.cpp` covers ENC/MAI/key header bytes.
**For Phase 23 the new test surface is the *app service* layer** (mirror `14-02-PLAN.md`'s
`test_stream_dock_control_service.cpp`): drive the new assign methods through `makeAkp05WithTransport` +
`MockTransport`, assert the ENC index byte (byte 12 = 0-based encoder index), the MAI whole-strip header
(bytes 5..7 = `M A I`, no index byte), and the DRA rect header for an encoder-zone (x = zone·200). Use
`tests::qtGuiApp()` because the encode path drives `QImage`. ASCII-only TEST_CASE/SECTION titles.

## Architecture Patterns

### System Architecture Diagram

```
  Phase-16 editor (QML)                 Profile (core)
  per-surface assign UX                 keys{}  encoders{ EncoderBinding.state: KeyState }
        |                                     |
        | user assigns image to               | profileChanged / repaintPage
        | encoder i / main strip / strip zone |
        v                                     v
  +-------------------------------------------------------------+
  |  StreamDockControlService  (Phase 14, app layer)            |
  |  - holds shared_ptr<IDevice> open for the session           |
  |  - coalesced single-shot QTimer drain (last-write-wins)     |
  |  - NEW: assignEncoderImage / assignMainImage /              |
  |         assignTouchStripZone  (+ repaint encoders)          |
  +-------------------------------------------------------------+
        | dynamic_cast<IEncoderCapable*> / <IDisplayCapable*> / <ITouchStripDisplayCapable*>
        | (null-check within 3 lines)   pass RGBA8 + dims; NO app-side JPEG
        v
  +-------------------------------------------------------------+
  |  Akp05Device  (device backend — SHIPPING, do not edit)      |
  |  setEncoderImage -> ENC header (idx@12) ┐                   |
  |  setMainImage    -> MAI header          ├-> image_pipeline  |
  |  setTouchStripImage -> DRA rect header  ┘   (RGBA->JPEG)    |
  |        |  each: header -> 1024B chunks -> ULEND sentinel     |
  +-------------------------------------------------------------+
        | ITransport::write()   (real HID  OR  MockTransport in tests)
        v
  AKP05E  0300:3004  (LIVE confirm = Phase 25)   |  MockTransport.writes() (Phase-23 gate)
```

### Recommended Project Structure (files touched — NO new modules)

```
src/app/src/
├── stream_dock_control_service.hpp/.cpp   # EXTEND (Phase-14 file): add aux assign + encoder repaint
├── application.cpp                          # wire editor signal -> service aux methods (if 16 lands here)
src/devices/streamdeck/src/
├── akp05.cpp / akp05_protocol.hpp           # READ ONLY — do NOT edit (LOCKED)
├── image_pipeline.*                          # READ ONLY — reuse encode path
tests/unit/
├── test_stream_dock_control_service.cpp     # EXTEND: ENC/MAI/DRA-zone wire assertions for aux surfaces
├── CMakeLists.txt                            # register if a new test file is added
```

### Pattern 1: Reuse a capability via dynamic_cast on the held handle

**What:** route an aux assign through the held `shared_ptr<IDevice>` by `dynamic_cast` to the right
mix-in; null-check immediately; no-op cleanly for non-capable devices.
**When to use:** every aux-surface assign in the control service.

```cpp
// Source: pattern from .planning/phases/14-stream-dock-control-service/14-02-PLAN.md key_links
// + capabilities.hpp mix-in surface (in-tree). [VERIFIED: read 14-02-PLAN.md + capabilities.hpp]
auto* enc = dynamic_cast<core::IEncoderCapable*>(m_activeDevice.get());
if (enc == nullptr) { return; }                 // null-check within 3 lines (Pitfall 1)
enc->setEncoderImage(encoderIndex /*0-based*/, rgba, w, h);   // backend encodes 100x100 + ENC + ULEND
```

### Pattern 2: Encoder-zone via DRA (the vendor-preferred per-overlay path)

**What:** draw a per-encoder overlay as a touch-strip rect instead of (or in addition to) ENC.

```cpp
// Source: ITouchStripDisplayCapable contract (capabilities.hpp:1531) + test_akp05_touch_strip.cpp.
// location MUST be < zoneCount (4); x = zoneIndex*200 (PROVISIONAL geometry — confirm Phase 25).
auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(m_activeDevice.get());
if (strip == nullptr) { return; }
strip->setTouchStripImage(rgba, srcW, srcH,
                          /*location=*/zoneIndex,
                          /*x=*/static_cast<std::uint16_t>(zoneIndex * 200),
                          /*y=*/0, /*rectWidth=*/200, /*rectHeight=*/100);
```

### Anti-Patterns to Avoid

- **Adding a new wire builder.** All builders exist. LOCKED: do not alter `akp05_protocol.hpp`.
- **JPEG-encoding in the app.** ARCH-04: the backend owns the single encode path. Pass RGBA8 + dims.
- **Deleting the ENC path** because akp05.md says "no encoder LCD." LOCKED: hardware wins in Phase 25.
- **Hard-coding per-zone resolution as if verified.** It's provisional; tag it.
- **Inventing a parallel held-handle / drain.** Reuse the Phase-14 `StreamDockControlService` plumbing.
- **`nlohmann::json` in core** (COD-031). Not needed here; the aux capabilities are Qt-free already.

## Don't Hand-Roll

| Problem                     | Don't Build                    | Use Instead                                           | Why                                                                         |
| --------------------------- | ------------------------------ | ----------------------------------------------------- | --------------------------------------------------------------------------- |
| RGBA→JPEG at surface dims   | A QImageWriter call in the app | Backend `encodeForDevice` via the capability method   | ARCH-04 single encode path; surface dims live in the backend transforms     |
| ENC/MAI/DRA framing         | A packet builder               | `setEncoderImage`/`setMainImage`/`setTouchStripImage` | RE-sourced, byte-tested, LOCKED do-not-alter                                |
| Chunking + ULEND commit     | A chunk loop in the service    | `Akp05Device::sendImage` (private, auto-ULEND)        | Already handles 1024B chunks, oversize refusal (SEC-008), device-yank→false |
| Holding the HID handle open | A new device opener            | The Phase-14 held `shared_ptr<IDevice>`               | Pitfall 2 (drop the ptr → handle closes mid-session)                        |
| Coalescing rapid writes     | A new debounce timer           | The Phase-14 single-shot `QTimer` drain               | DOCK-02 firmware-desync mitigation already designed                         |

**Key insight:** Phase 23 writes *no device-facing code at all*. Every byte that hits the wire is already
implemented and tested. The only new code is app-service glue + a service-level MockTransport test.

## Runtime State Inventory

Not a rename/refactor/migration phase. **N/A — no stored data, live-service config, OS-registered
state, secrets, or build artifacts are mutated.** This is additive app-layer wiring over existing code.
The only "state" is the in-memory `Profile::encoders` map, already persisted by the Phase-16 profile
JSON path (out of scope to change here).

## Common Pitfalls

### Pitfall 1: Starting Phase 23 before `StreamDockControlService` exists

**What goes wrong:** the phase has nothing to extend; tasks reference a non-existent file.
**Why it happens:** Phase 14 (and 16) are *planned-not-executed*; Phase 23 `depends_on:[14]`.
**How to avoid:** gate the first wave on the file existing + the Phase-14 service test green
(`checkpoint:human-verify` or a Wave-0 existence check). Read `14-02-SUMMARY.md` for the seam/mapping.
**Warning signs:** `ls src/app/src/stream_dock_control_service.hpp` → not found; no `14-02-SUMMARY.md`.

### Pitfall 2: Treating ENC-vs-zone as a bug to fix now

**What goes wrong:** deleting/"correcting" the ENC path based on akp05.md, breaking the LOCKED
ship-both-flag-provisional instruction and pre-empting the Phase-25 hardware decision.
**Why it happens:** akp05.md confidently says "no separate encoder LCD"; the code confidently models one.
**How to avoid:** wire both, delete neither, tag PROVISIONAL. The RE has gaps; hardware decides (CLAUDE.md).
**Warning signs:** a task description that says "remove setEncoderImage" or "the ENC path is wrong."

### Pitfall 3: keyIndex/encoderIndex base confusion

**What goes wrong:** keys are **1-based** in the backend (`keyIndexInRange` rejects 0); encoders are
**0-based** (`< EncoderCount`). Touch-strip `location` must be `< zoneCount` (4). Mixing these silently
no-ops (range-check refuses) or pokes the wrong surface.
**Why it happens:** `IDisplayCapable` doxygen says "zero-based" for keys but the shipping backend is
1-based (the backend wins — Phase-14 interfaces block documents this).
**How to avoid:** pass 1-based key indices, 0-based encoder indices, zone-id `< 4` for DRA. Mirror the
Phase-14 profile→device mapping recorded in `14-02-SUMMARY.md`.
**Warning signs:** a test that expects byte 12 == 1 for encoder 0, or zero writes when an image was set.

### Pitfall 4: Provisional DRA rect treated as verified

**What goes wrong:** the per-zone width/height/y is asserted as correct, so a Phase-25 capture that
differs looks like a regression instead of the expected reconciliation.
**Why it happens:** the header bytes are pinned in tests, which *looks* authoritative.
**How to avoid:** the test pins what *our code emits* (a contract), not what *the device wants* (unknown).
Comment the provisional geometry; route the reconciliation to Phase 25.
**Warning signs:** a doc/test claiming the DRA layout is "confirmed" without a 2026-05-2x+ live capture.

### Pitfall 5: App-side JPEG encode / new builder creep

**What goes wrong:** ARCH-04 single-encode-path violated; or `akp05_protocol.hpp` edited (LOCKED).
**How to avoid:** the service passes RGBA8 + dims only; `git diff --stat` must exclude
`akp05.cpp`/`akp05_protocol.hpp`.
**Warning signs:** `QImageWriter` or `buildSecondaryScreenHeader` appearing under `src/app/`.

## Code Examples

### Repaint encoder overlays from the active profile (service extension)

```cpp
// Source: composed from in-tree Profile (profile.hpp:142) + capabilities (capabilities.hpp)
// + Phase-14 repaint pattern (14-02-PLAN.md). Encoders are 0-based; KeyState carries imagePath/background.
void StreamDockControlService::repaintEncodersFromProfile(core::Profile const& p) {
    for (auto const& [idx, binding] : p.encoders) {        // EncoderBinding.state : KeyState
        QImage img = renderKeyState(binding.state);        // reuse the SAME helper as key repaint
        // Default to the vendor-preferred DRA zone path; ENC remains available (flag PROVISIONAL).
        assignTouchStripZone(static_cast<std::uint8_t>(idx), img);   // -> DRA, x = idx*200
        // assignEncoderImage(static_cast<std::uint8_t>(idx), img);  // -> ENC (kept, not deleted)
    }
}
```

### Service-level MockTransport assertion for the ENC index byte

```cpp
// Source: pattern from test_akp05_touch_strip.cpp + 14-02-PLAN.md interfaces block. ASCII title.
TEST_CASE("control service assignEncoderImage emits ENC header with 0-based index", "[stream-dock-control]") {
    tests::qtGuiApp();
    // ... makeAkp05WithTransport + MockTransport, inject as the service's active device ...
    service.assignEncoderImage(/*encoderIndex=*/2, img2);   // drain QTimer, then:
    auto const& header = transport->writes().front();
    CHECK(header[5] == 0x45); CHECK(header[6] == 0x4e); CHECK(header[7] == 0x43); // "ENC"
    CHECK(header[12] == 0x02);                                                    // 0-based index
    // trailing ULEND U L E N D at bytes 5..9 of the last write
}
```

## State of the Art

| Old Approach                              | Current Approach                                        | When Changed                                  | Impact                                                                                   |
| ----------------------------------------- | ------------------------------------------------------- | --------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Whole-strip `MAI` re-upload per redraw    | `DRA` rect-addressable partial upload                   | vendor §10 P0; backend landed pre-Phase-23    | 4× bandwidth win (one 200-wide zone vs the 800×480 panel) on per-encoder overlay redraws |
| `STP` flush only after image bursts       | `ULEND` commit sentinel per burst (auto in `sendImage`) | backend `sendImage`                           | fixes firmware-desync class after rapid image bursts                                     |
| Backend methods existed but no app caller | App service (`StreamDockControlService`) drives them    | Phase 14 (pending) → Phase 23 extends for aux | closes the Phase-10 UAT gap (panel dark / images never reach device)                     |

**Deprecated/outdated:**

- The 512-byte packet assumption in old AKP05 notes — corrected to **1024-byte OUT / 512-byte IN**
  (firmware-confirmed `V3.AKP05E.01.007`, `akp05_protocol.hpp:18-30`). `PacketSize == 1024`.
- "no separate encoder LCD" (akp05.md) vs in-code 100×100 ENC LCD — **unresolved divergence**, ship both,
  reconcile at Phase 25 (not deprecated, *provisional*).

## Assumptions Log

| #   | Claim                                                                                                                     | Section                    | Risk if Wrong                                                                                                                                          |
| --- | ------------------------------------------------------------------------------------------------------------------------- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| A1  | Encoder-zone→rect geometry is `x = zoneIndex*200, w = 200` (height/y provisional)                                         | Target 2 / Pattern 2       | Overlay lands in the wrong strip region on real hardware → Phase-25 corrects geometry; low risk (test pins our contract, not device truth)             |
| A2  | The vendor-preferred per-encoder-overlay mechanism is DRA zones, not ENC                                                  | Target 3                   | If firmware honors ENC and ignores DRA zones, the default path shows nothing on encoders → Phase 25 flips the default; mitigated by keeping BOTH wired |
| A3  | Phase 14 (and 16) will be executed before Phase 23 (or gated in-run)                                                      | STOP Gate                  | If not, Phase 23 has no service to extend → blocked; mitigated by the explicit Wave-0 existence gate                                                   |
| A4  | Whole-strip image assignment is an editor action, not a `Profile` per-control binding (no touch-strip field in `Profile`) | Target 4                   | If a strip-image binding is later wanted in the profile schema, that's a schema change deferred to Phase 16/24                                         |
| A5  | The Phase-14 coalesced `QTimer` drain + profile-accessor seam are reusable as-is for encoder/strip repaint                | Don't Hand-Roll / Patterns | If Phase-14 shipped a different seam, reuse it verbatim from `14-02-SUMMARY.md` rather than this assumption                                            |

**All package/version claims:** N/A (no external packages). All wire-byte and capability claims are
[VERIFIED] by reading the in-tree backend and tests this session.

## Open Questions

1. **Is the per-encoder overlay drawn via ENC or via a DRA strip zone on the real AKP05E?**

   - What we know: both opcodes exist in the vendor SDK; akp05.md says there's no separate encoder LCD.
   - What's unclear: which one the firmware actually renders for per-encoder graphics.
   - Recommendation: wire both, default to DRA zones, flag PROVISIONAL, decide at Phase 25 (VERIFY-05).

1. **Exact DRA per-zone resolution and y-origin.**

   - What we know: 800×480 strip, 4 zones, `x = i*200`. Code comments cite 200×100 and 4×(200×480).
   - What's unclear: the real per-zone pixel band the firmware expects.
   - Recommendation: pick 200×100 (or 200×480), comment provisional, confirm in Phase 25.

1. **Does the editor (Phase 16) own strip/encoder assignment UX, and is it executed?**

   - What we know: `16-03-PLAN.md` extends the same service file; both 14 and 16 are unexecuted.
   - Recommendation: wire the *service* surface in Phase 23; coordinate editor rows with Phase 16's
     actual execution state (the editor UX is Claude's discretion and can land with/after 16).

## Environment Availability

| Dependency                                    | Required By                | Available                    | Version                 | Fallback                                                                            |
| --------------------------------------------- | -------------------------- | ---------------------------- | ----------------------- | ----------------------------------------------------------------------------------- |
| CMake + Ninja + Qt6 toolchain                 | build/test                 | ✓ (project baseline)         | per CLAUDE.md (Qt 6.7+) | —                                                                                   |
| `ctest --preset linux-release`                | gating proof               | ✓                            | —                       | —                                                                                   |
| Physical AKP05E (`0300:3004`) + `uaccess` ACL | **LIVE confirmation only** | ✗ for Phase 23 (intentional) | fw `V3.AKP05E.01.007`   | **MockTransport byte tests** (the Phase-23 gate); live witness deferred to Phase 25 |

**Missing dependencies with no fallback:** none for Phase 23 (hardware is intentionally Phase-25 scope).
**Missing dependencies with fallback:** physical AKP05E → MockTransport wire assertions (sufficient to land
the wiring + tests).

## Validation Architecture

### Test Framework

| Property           | Value                                                              |
| ------------------ | ------------------------------------------------------------------ |
| Framework          | Catch2 (v3) + `tests::MockTransport` fixture + `tests::qtGuiApp()` |
| Config file        | `tests/unit/CMakeLists.txt` (sources listed explicitly)            |
| Quick run command  | `ctest --preset linux-release -R "stream-dock-control\|akp05"`     |
| Full suite command | `ctest --preset linux-release`                                     |

### Phase Requirements → Test Map

| Req ID                   | Behavior                                                                                 | Test Type   | Automated Command                                                     | File Exists?                                                                                                           |
| ------------------------ | ---------------------------------------------------------------------------------------- | ----------- | --------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| DISPLAY-10 (ENC)         | `assignEncoderImage(i)` → ENC header, 0-based index at byte 12, +ULEND                   | unit/wire   | `ctest --preset linux-release -R stream-dock-control`                 | ❌ Wave 0 (extend `test_stream_dock_control_service.cpp`)                                                              |
| DISPLAY-10 (MAI)         | `assignMainImage` → `M A I` at bytes 5..7, no index byte, +ULEND                         | unit/wire   | same                                                                  | ❌ Wave 0                                                                                                              |
| DISPLAY-10 (DRA zone)    | `assignTouchStripZone(z)` → DRA header, BE16 w/h/x/y, x = z·200, +ULEND                  | unit/wire   | same                                                                  | ⚠️ DRA *backend* bytes already covered by `test_akp05_touch_strip.cpp`; add the *service-level* zone-mapping assertion |
| DISPLAY-10 (repaint)     | `repaintEncodersFromProfile` paints each bound encoder from `Profile::encoders[i].state` | unit        | same                                                                  | ❌ Wave 0                                                                                                              |
| DISPLAY-10 (provisional) | ENC path still reachable + commented PROVISIONAL (no deletion)                           | static/grep | `grep -n setEncoderImage src/app/src/stream_dock_control_service.cpp` | ❌ Wave 0                                                                                                              |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R stream-dock-control`
- **Per wave merge:** `ctest --preset linux-release` (full suite, ~408 cases)
- **Phase gate:** full suite green + `git diff --stat` excludes `akp05.cpp`/`akp05_protocol.hpp` before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] Confirm `src/app/src/stream_dock_control_service.{hpp,cpp}` exist (Phase-14 executed) — **STOP gate**.
- [ ] Extend `tests/unit/test_stream_dock_control_service.cpp` — ENC/MAI/DRA-zone wire assertions + encoder repaint (covers DISPLAY-10).
- [ ] No new framework install needed (Catch2 + MockTransport + qtGuiApp already in tree).

*(Existing `test_akp05_touch_strip.cpp` and `test_akp05_protocol.cpp` already cover the backend bytes;
the gap is the app-service layer.)*

## Security Domain

`security_enforcement` not present in `.planning/config.json` → treated as **enabled**.

### Applicable ASVS Categories

| ASVS Category         | Applies | Standard Control                                                                                                                                                                                                                               |
| --------------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no      | No auth surface in this phase                                                                                                                                                                                                                  |
| V3 Session Management | no      | —                                                                                                                                                                                                                                              |
| V4 Access Control     | no      | Local device I/O only                                                                                                                                                                                                                          |
| V5 Input Validation   | **yes** | Backend already range-checks: encoderIndex `< 4`, keyIndex 1..10, DRA location `< zoneCount`, JPEG payload ≤ 65535 (SEC-008/CWE-190 in `sendImage`), touch-X clamp (SEC-009). The service must pass *through* these guards, never bypass them. |
| V6 Cryptography       | no      | —                                                                                                                                                                                                                                              |

### Known Threat Patterns for AKP05E aux-surface wiring

| Pattern                                             | STRIDE    | Standard Mitigation                                                                                                                                                                                                  |
| --------------------------------------------------- | --------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Oversized/malformed image desyncs firmware          | DoS       | Backend caps payload at 65535 + emits ULEND; service coalesces bursts via QTimer drain (DOCK-02). [VERIFIED: akp05.cpp:861]                                                                                          |
| Out-of-range encoder/zone index pokes wrong surface | Tampering | Backend range-checks + WARN+no-op/false-return; service must not pre-compute an unchecked index. [VERIFIED: akp05.cpp:640,726]                                                                                       |
| Device yank mid-burst                               | DoS       | `sendImage` catches the transport throw → returns false (never escapes the override); held-handle re-resolve on hot-plug, not on a stale ptr (Pitfall 2). [VERIFIED: akp05.cpp:887 + test_akp05_touch_strip.cpp:325] |
| Untrusted profile imagePath → image load            | Tampering | Load via Qt safe image decoders only (carried from Phase 14 T-14b-03; deep path validation is Phase 16/19). [ASSUMED — confirm with Phase-16 posture]                                                                |
| 0x12 location → unintended M_V boot-logo path       | Tampering | Backend refuses location 0x12 and rejects `location >= zoneCount`; service passes only `< 4`. [VERIFIED: akp05.cpp:678,726]                                                                                          |

## Sources

### Primary (HIGH confidence)

- `src/devices/streamdeck/src/akp05.cpp` (read full) — all aux-surface impls, ranges, ULEND, encode transforms.
- `src/devices/streamdeck/src/akp05_protocol.hpp` (read full) — builders, DRA header layout, geometry constants, PacketSize=1024.
- `src/devices/streamdeck/src/image_pipeline.hpp` (read full) — ARCH-04 RGBA→JPEG encode contract.
- `src/core/include/ajazz/core/capabilities.hpp` (relevant sections) — `IDisplayCapable`, `IEncoderCapable`, `IBootLogoCapable`, `ITouchStripDisplayCapable`, `TouchStripInfo`, `Capability` enum.
- `src/core/include/ajazz/core/profile.hpp` — `Profile`, `Binding`, `EncoderBinding`, `KeyState`, `ProfilePage`.
- `tests/unit/test_akp05_touch_strip.cpp` (read full) — DRA/ULEND/range/yank byte assertions (the established test pattern).
- `.planning/phases/14-stream-dock-control-service/14-02-PLAN.md` (read full) — the service shape, DeviceLookup, coalesced drain, interfaces, threat model.
- `.planning/phases/16-device-controls-binding-persistence-pages/16-03-PLAN.md` — confirms 16 also extends the service (`repaintPage`), both 14/16 unexecuted.
- `.planning/REQUIREMENTS.md` — DISPLAY-10 text + status (Pending, Phase 23).

### Secondary (MEDIUM confidence)

- `docs/protocols/streamdeck/akp05.md` — hardware/layout ("no separate encoder LCD; overlays are touch-strip zones"); marked unverified-from-capture.
- `docs/protocols/streamdeck/akp05_vendor.md §1–§3` — Ghidra-derived opcode table (ENC/MAI/DRA/ULEND/LOG), `getSecondaryScreenPicInfo`, AKP05E SKU correction (`0x0300:0x3004`, fw `V3.AKP05E.01.007`).

### Tertiary (LOW confidence)

- The exact DRA per-zone resolution / y-origin and the ENC-vs-zone firmware behavior — **PROVISIONAL**, no live capture; Phase-25 hardware witness required.

## Metadata

**Confidence breakdown:**

- Backend capability inventory (DONE/MISSING): HIGH — read the impl + tests directly.
- App-wiring approach: HIGH — mirrors the executed-pattern Phase-14 plan; only the target file's existence is pending.
- DRA rect framing / ENC-vs-zone: LOW (intentionally) — provisional RE, hardware-gated to Phase 25.
- Dependency/STOP gate: HIGH — verified `stream_dock_control_service.*` absent and no `14-02-SUMMARY.md`.

**Research date:** 2026-05-23
**Valid until:** ~2026-06-22 (stable in-tree; re-check the moment Phase 14/16 execute and produce SUMMARY files — read those for the canonical seam/mapping before planning Phase 23 tasks).
