---
status: partial
phase: 34-per-app-profiles-event-parity-audit
source: [34-VERIFICATION.md]
started: 2026-06-08T18:15:00Z
updated: 2026-06-08T18:15:00Z
---

## Current Test

[awaiting human testing — all items are live/hardware-gated and cannot run headless on this machine]

## Tests

### 1. Wayland live focus-walk on niri

expected: Focus a mapped app A, then app B; the watcher emits the app_id change and the device repaints within 500 ms; `log.tail` shows willDisappear (outgoing) then willAppear (incoming) in order.
result: [pending]

### 2. X11/XWayland live switch

expected: Force the X11 backend, focus app A then app B; profile auto-switches on the mapped app gaining focus and the bridge lifecycle fires willDisappear->willAppear in order.
result: [pending]

### 3. Graceful degradation on a capability-absent compositor

expected: On GNOME/KDE-without-wlr (or forced isActive()==false): no crash/silent break; `waylandCapabilityWarningChip` shows non-empty amber warning text; manual profile switching still works.
result: [pending]

### 4. EVENT dispatch end-to-end via a registered plugin (live WS)

expected: Trigger switchToProfile, systemDidWakeUp, applicationDidLaunch/Terminate; each appears in `plugin.protocolLog` at the subscribed plugin only.
result: [pending]

### 5. Retail-device repaint latency (\<500 ms)

expected: A real device repaints within 500 ms of the foreground change. (Hardware-gated: only the 0x3004 demo unit exists; needs a retail AKP05E.)
result: [pending]

### 6. systemDidWakeUp from a real OS source (logind PrepareForSleep D-Bus)

expected: A real OS suspend/resume triggers systemDidWakeUp to subscribed plugins. (OS-source wiring deferred this phase; dispatch is unit-proven.)
result: [pending]

### 7. WR-04 Win32 recycled-HWND dedup on a real Windows host

expected: A foreground change to a process that reuses a previously-seen HWND is NOT suppressed (dedup on resolved appId). (Compile-only on Linux; flag for the Phase 35 Windows walk.)
result: [pending]

## Summary

total: 7
passed: 0
issues: 0
pending: 7
skipped: 0
blocked: 0

## Gaps
