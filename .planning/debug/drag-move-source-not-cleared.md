---
slug: drag-move-source-not-cleared
status: resolved
trigger: |
  DATA_START
  When dragging an already-bound key onto another key in the device canvas
  (a "move binding" operation), the destination key receives the binding but
  the SOURCE key is NOT cleared — the old button/icon stays visible at the
  key the user dragged FROM.
  (User's words, Italian: "nella UI rimane visible il vecchio pulsante da
  dove trascino verso un nuovo pulsante.")
  DATA_END
created: 2026-06-04
updated: 2026-06-04
---

# Debug: drag-move source key not cleared

## Symptoms

- **Expected:** Dragging a bound key A onto key B *moves* the binding — B shows
  the action, A becomes empty.
- **Actual:** B receives the binding, but A still shows the old button/icon.
  The source key is not cleared on move.
- **Error messages:** None reported (visual/UI symptom only).
- **Timeline:** "Never worked" per user — BUT this directly contradicts shipped
  commit `67ee92a` *"fix(29): clear the source key on move + make icon-less
  bound keys draggable"*. So either that fix is incomplete/ineffective, or the
  current uncommitted working-tree changes have altered the path.
- **Reproduction:** GUI app (`build/linux-release`), real physical mouse,
  device canvas (DeviceView.qml), drag from one bound key to another.

## Environment / context

- Branch: `experiment/mirajazz`
- Claimed-fix commit `67ee92a` touched: `DeviceView.qml`, `KeyCell.qml`,
  `live_key_image_provider.hpp`, `stream_dock_control_service.{cpp,hpp}`.
- **Uncommitted working-tree changes (may be load-bearing):**
  `src/app/CMakeLists.txt`, `src/app/qml/DeviceView.qml`, `src/app/qml/Main.qml`,
  `src/app/qml/ProfileBar.qml`, `src/app/qml/ProfileEditor.qml`,
  `src/app/src/profile_controller.cpp`,
  `src/app/src/stream_dock_control_service.cpp`,
  `?? src/app/qml/DeviceSelector.qml`
- Drag mechanics (per project memory): real-mouse drag uses `Drag.Internal` +
  cursor-follow ghost (Wayland breaks `Drag.Automatic`); internal-drag payload
  is read from the `DragRelay` singleton (NOT mimeData) — see commits
  `17c280f`, `d07a2af`, `1285083`. A "move" is a key→key drag; a "bind" is a
  library-tile→key drag.
- Debug channel available: launch `AJAZZ_DEBUG_CONTROL=1`, drive via
  `scripts/ajazz-debug`. NOTE harness gap: synthetic drag may not reproduce a
  real-mouse `Drag.Internal` move — prefer reading the canvas state
  (`profile.*`, `qml.get`) after a move to confirm source/dest binding state.

## Current Focus

- **hypothesis:** CONFIRMED — `onKeySwapRequested` in DeviceView.qml did not
  mirror the swap into the QML `bindings` ListModel immediately. The visual
  update depended entirely on `repaintPage()` → `clearKeyImage()` →
  `keyImageCleared()` → `bindings.setProperty()`, but `repaintPage()` returns
  early when no active device is connected (`!m_activeDevice`). In that path
  `keyImageCleared` is never emitted and the source cell stays stale.
  Additionally, the `profileChanged → _syncFromProfile()` path uses
  `bindings.set()` (whole-element replacement) rather than `setProperty()`
  (in-place mutation), and `bindings.set()` has less reliable binding
  re-evaluation than `setProperty()` for an integer-model Repeater whose
  delegates access the ListModel via explicit `bindings.get(index).iconSource`.
- **next_action:** DONE — fix applied.

## Evidence

- `67ee92a` commit description documents the previous fix: cleared device LCD
  - emitted `keyImageCleared`. The QML `onKeyImageCleared` handler uses
    `bindings.setProperty()` — same in-place mechanism the fix now uses.
- Trash-drop handler in DeviceView.qml already used an explicit `bindings.set()`
  call after `commitKeyBinding` — indicating the `_syncFromProfile()` path alone
  was not considered sufficient for visual updates.
- `onKeyActionDropped` (library→key drop) places an explicit `bindings.set()`
  BEFORE the `commitKeyBinding` call for immediate visual feedback — the same
  "don't wait for profileChanged" pattern we now apply to key→key moves.
- `repaintPage` guard at lines 463-469: returns early when `!m_activeDevice`.
  If no device is active, `clearKeyImage` is never called, `keyImageCleared`
  is never emitted, and the `onKeyImageCleared` QML handler never fires.
  `_syncFromProfile()` is the only remaining path, and it uses `bindings.set()`
  (whole-row replacement) rather than `setProperty()` (in-place).

## Eliminated

- C++ `swapKeyBindings`: confirmed correct by unit tests; source key is empty
  after swap; `activeKeyBindings()` returns `iconSource = ""` for source key.
- Device LCD clear: `clearKeyImage()` correctly blanks the physical device key
  and emits `keyImageCleared`. This path is fine when a device is active.
- `_syncFromProfile()` logic: correctly reads the swapped profile state and
  sets model entries. The issue is the reliability of the visual re-evaluation.

## Resolution

- **root_cause:** `onKeySwapRequested` in DeviceView.qml relied on
  `repaintPage() → clearKeyImage() → keyImageCleared` to clear the source key
  in the QML canvas. This path is skipped when `m_activeDevice == nullptr`
  (no device connected). The `_syncFromProfile()` fallback uses
  `bindings.set()` (whole-element replacement), which is less reliable than
  `bindings.setProperty()` (in-place mutation) for triggering binding
  re-evaluation in an integer-model Repeater.
- **fix:** In `onKeySwapRequested` (DeviceView.qml), immediately mirror the
  swap into the `bindings` ListModel using `bindings.setProperty()` calls
  before calling `ProfileController.swapKeyBindings(src, dst)`. The
  `setProperty` in-place mutation is the same mechanism used by the working
  `onKeyImageCleared` handler. This guarantees a reliable visual update
  regardless of whether a device is connected. The
  `profileChanged → _syncFromProfile` path continues to run as a consistency
  guarantee (ensures label/actionId are also consistent after the swap).
  Also removed three DIAG instrumentation log lines from working tree
  (profile_controller.cpp, stream_dock_control_service.cpp, DeviceView.qml)
  that were added during investigation.
- **files_changed:**
  - `src/app/qml/DeviceView.qml` — core fix in `onKeySwapRequested` handler
