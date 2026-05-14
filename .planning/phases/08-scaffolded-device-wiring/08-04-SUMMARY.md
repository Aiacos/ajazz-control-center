---
phase: 08-scaffolded-device-wiring
plan: 04
type: summary
status: complete
commits:
  - 62da68c (AKP815 promotion #1 — landed before milestone start)
  - 7d114fd (Mirabox N3 promotion #2 — cross-reference doc)
requirements_closed: [DEVICES-04]
---

# Plan 08-04 — promote 1-2 Stream-Dock siblings

## Commits + diffstat

**Promotion #1** (pre-milestone, in scope): `62da68c feat(streamdeck): add AKP815 backend + canonical PIDs + fix N4 catalog mapping` — AKP815 descriptor + factory + protocol doc; maturity tier set to `probed` (no real-device capture yet).

**Promotion #2**: `7d114fd docs(streamdeck): add mirabox_n3 cross-reference doc (Phase 8 DEVICES-04 promotion #2)`

```
 docs/protocols/streamdeck/mirabox_n3.md | 98 +++++++++++++++++++++++++++++++++
 1 file changed, 98 insertions(+)
```

Mirabox N3 (rev. 1) maturity set to `partial` — inherits the `Akp03Device` functional behavior; new doc cross-references the shared protocol surface. Promotes to `functional` when first-hand Mirabox capture lands.

## Requirements closed

- **DEVICES-04** — two scaffolded Stream-Dock-family siblings promoted off Tier 0 (AKP815 → `probed`, Mirabox N3 → `partial`), with documented per-device protocol artefacts. AKB980 PRO explicitly excluded per scope (wine-only vendor installer).

## Tech debt noted

- AKP815 maturity promotes to `partial` when first capture lands.
- Mirabox N3 (rev. 1) maturity promotes to `functional` when first-hand capture lands.
