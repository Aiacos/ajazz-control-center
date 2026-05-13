# Milestones

## v1.0 milestone (Shipped: 2026-05-13)

**Phases completed:** 2 phases, 0 plans, 6 tasks

**Key accomplishments:**

- Wires the out-of-process Python plugin host into the Application lifecycle, gates plugin loading on manifest signature verification, and closes two pre-existing robustness gaps surfaced during self-review.
- Fixes the silent dual-instance bug in `BrandingService` (light theme rendered dark) and applies the same fix prophylactically across five other `QML_SINGLETON` services.

______________________________________________________________________
