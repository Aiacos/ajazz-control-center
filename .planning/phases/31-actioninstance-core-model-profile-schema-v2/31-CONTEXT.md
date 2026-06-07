# Phase 31: ActionInstance Core Model + Profile Schema v2 - Context

**Gathered:** 2026-06-08
**Status:** Ready for planning

<domain>
## Phase Boundary

Add an OpenDeck-shaped `ActionInstance` / `ActionState` data model under `src/core/`
(`action_instance.hpp` + hand-rolled JSON serialization) and wire it into the existing
`Profile` schema as an **additive, optional** field on `Binding` and `EncoderBinding`.
Existing v1.3 profiles must load unchanged. The schema must support Multi Action children
(recursive instances) and per-state images (`states[]`).

This phase delivers the **data model + serialization + v1→v2 migration + round-trip tests
ONLY**. It does NOT implement Multi/Toggle Action *dispatch*, the device editor, or the
Property Inspector — those are Phases 32 and 33. Satisfies requirements BIND-01 and BIND-02.

</domain>

<decisions>
## Implementation Decisions

### Model shape & phase scope

- **Coexist, do not replace.** `ActionInstance` is added as `std::optional<ActionInstance> instance`
  on both `Binding` and `EncoderBinding`. The existing `onPress` / `onRelease` / `onLongPress`
  (and encoder `onCw` / `onCcw` / `onPress`) action-chains are left untouched. Matches success
  criterion 4 and the schema's additive-only compatibility policy.
- **Scope = model + hand-rolled serialization + v1→v2 migration + tests only.** Multi/Toggle
  dispatch and the device editor are Phase 32; the Property Inspector round-trip is Phase 33.
- **Multi Action children are stored as a recursive `std::vector<ActionInstance>`** on the
  instance (an instance may itself contain child instances).
- **`ActionInstance.settings` is an escaped JSON string** under the `"settings"` wire key,
  mirroring the existing `Action::settings` convention. Keeps the wire format linear, preserves
  COD-031 (no `nlohmann` in `ajazz_core` / installed headers), and round-trips unknown sub-keys
  untouched.

### ActionState (per-state visuals) & v1→v2 migration

- **Full `KeyState` parity per state.** Each `ActionState` carries the existing `KeyState`
  fields (imagePath, text, background, foreground, fontSize) — reuse `KeyState` as the per-state
  visual payload rather than inventing a narrower struct.
- **Lazy migration on read.** `profileFromJson()` accepts a legacy singular `state:` key and
  folds it into a `states:` array of one. The writer always emits the `states[]` array form.
  A v1.3 profile file therefore loads without error and round-trips correctly as array-of-one
  (success criterion 3).
- **No `instance` key → `instance = std::nullopt`.** Profiles authored before v2 parse cleanly
  with no synthesized instance (success criterion 4).
- **`currentState` is defensive on read:** defaults to 0 and clamps any out-of-range index to 0
  (lossless load — never reject a profile for a stale index).

### Schema doc, versioning & tests

- **Update `docs/protocols/PROFILE_SCHEMA.md` + the embedded JSON Schema in this phase.** Add
  `ActionInstance` / `ActionState` `$defs` and the `instance` / `states` keys. The schema doc is
  the source of truth for wire keys (CLAUDE.md hard rule) and must not lag the code.
- **No explicit schema-version field.** Rely on the additive / forward-compatible reader; the
  presence of `states` (vs legacy singular `state`) and `instance` is the discriminator. Avoids a
  brittle version gate.
- **New `action_instance` Catch2 test target** (under `tests/unit/`), runnable via
  `ctest --preset linux-release -R action_instance` (success criterion 2).
- **Round-trip coverage:** 0-state, 1-state, 3-state, 2-children variants, plus a dedicated
  v1→v2 migration test (legacy singular `state` profile → states array-of-one).

### Claude's Discretion

- Exact struct field ordering, helper function names, and internal parser layout are at the
  executor's discretion, consistent with the existing hand-rolled writer/reader style in
  `profile.cpp`.

</decisions>

\<code_context>

## Existing Code Insights

### Reusable Assets

- `src/core/include/ajazz/core/profile.hpp` — current schema: `Action`, `KeyState`, `Binding`
  (onPress/onRelease/onLongPress + singular `KeyState state`), `EncoderBinding`,
  `TouchZoneBinding`, `ProfilePage`, `Profile`; plus `profileToJson()` / `profileFromJson()`
  declarations. Reuse `KeyState` directly as the per-state payload.
- `src/core/src/profile.cpp` (the hand-rolled writer/reader) — extend in the established style;
  the parser already tolerates unknown keys and arbitrary whitespace.
- `docs/protocols/PROFILE_SCHEMA.md` — wire-key source of truth; `settings` is documented as an
  escaped JSON string, additive-only policy already stated. Add the new `$defs` here.

### Established Patterns

- **Hand-rolled JSON in core** (no `nlohmann` / no Qt) so it unit-tests without an event loop —
  COD-031 boundary verified by `grep -rn nlohmann src/core/include/` returning 0.
- **Wire key ≠ C++ field** is normal and documented in a table (e.g. `deviceCodename` ⇄ `device`,
  `settingsJson` ⇄ `settings`).
- Catch2 unit tests live in `tests/unit/`; existing relevant files: `test_profile_io.cpp`,
  `test_profile_serialization.cpp`, `test_profile_multiaction.cpp`, `test_profile_persistence.cpp`,
  `test_profile_pages.cpp`.

### Integration Points

- `Binding` and `EncoderBinding` gain `std::optional<ActionInstance> instance`.
- `profileToJson()` / `profileFromJson()` gain serialization for the new field + states[] form.
- New header `src/core/include/ajazz/core/action_instance.hpp` (must stay nlohmann-free).
- CMake: register the new `action_instance` Catch2 test target.

\</code_context>

<specifics>
## Specific Ideas

- The model is OpenDeck / Stream Deck-shaped: an `ActionInstance` has `states[]` (per-state
  visuals), `currentState` (active index, drives rendering + Toggle Action in Phase 32),
  `settings` (opaque escaped-JSON config), and `children` (recursive instances for Multi Action).
- Mirror the existing `Action::settings` escaped-string convention exactly so the Property
  Inspector (Phase 33) can `JSON.parse` / `JSON.stringify` the settings blob symmetrically.

</specifics>

<deferred>
## Deferred Ideas

- Multi Action / Toggle Action **dispatch** (running children sequentially, cycling currentState,
  rendering per-state images, emitting state-change willAppear) → Phase 32 (BIND-04, BIND-05).
- Device editor scroll + drag-to-bind willAppear fix → Phase 32 (BIND-03).
- Property Inspector settings round-trip over the $SD bridge → Phase 33.
- Fuzz / property-based serialization tests (beyond the enumerated round-trip variants).

</deferred>
