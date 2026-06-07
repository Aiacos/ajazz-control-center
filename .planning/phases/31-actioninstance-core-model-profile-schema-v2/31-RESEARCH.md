# Phase 31: ActionInstance Core Model + Profile Schema v2 - Research

**Researched:** 2026-06-08
**Domain:** Hand-rolled JSON (de)serialization in `ajazz_core` (C++20, no nlohmann/no Qt); OpenDeck/StreamDeck action-instance data model; lazy v1->v2 profile migration
**Confidence:** HIGH (codebase-verified; the entire phase is internal serialization work with no new external dependencies)

## Summary

This phase is **pure data-model + hand-rolled-serialization work inside `ajazz_core`**. There is no new library, no external dependency, and no device or wire-protocol contact. The whole job is: (1) add an `ActionInstance` / `ActionState` struct pair in a new `src/core/include/ajazz/core/action_instance.hpp`, faithful in shape to the StreamDeck/OpenDeck `states[]` / `currentState` / `settings` / `children` model; (2) extend the existing hand-rolled writer/reader in `src/core/src/profile.cpp` to (de)serialize `std::optional<ActionInstance> instance` on `Binding` and `EncoderBinding`; (3) make `profileFromJson()` fold a legacy singular `state:` key into a `states:[<one>]` array (lazy migration on read); (4) register a new `action_instance` Catch2 target; (5) update `docs/protocols/PROFILE_SCHEMA.md` + its embedded JSON Schema.

The codebase already contains a complete, idiomatic template for every piece of this. The `writeKeyState`/`readKeyState` pair (profile.cpp:148-188, 664-695) is the exact model for the new `writeActionState`/`readActionState`. The `writeBinding`/`readBinding` and `writeTouchZoneBinding`/`readTouchZoneBinding` pairs show how to add an optional nested object to a binding. The `Action::settingsJson` \<-> `"settings"` escaped-JSON-string convention (profile.cpp:113-114, 615-616) is exactly what `ActionInstance.settings` must mirror. The existing `test_profile_serialization.cpp` "v1 profile migrates touchZones" test (lines 202-222) is the precise template for the v1->v2 migration test.

**Primary recommendation:** Add `action_instance.hpp` (structs only, nlohmann-free), implement `writeActionInstance`/`readActionInstance` + `writeActionState`/`readActionState` inside the anonymous namespace of `profile.cpp` mirroring the KeyState helpers, wire `instance` into `writeBinding`/`readBinding`/`writeEncoderBinding`/`readEncoderBinding`, fold legacy `state:` into `states:[]` in `readActionInstance`, and add `tests/unit/test_action_instance.cpp` with a `[action_instance]` tag registered in `tests/unit/CMakeLists.txt`. Do NOT touch `_schemaVersion` semantics — presence of `states`/`instance` is the discriminator (CONTEXT decision; matches the existing forward-compatible reader).

## Architectural Responsibility Map

| Capability                                    | Primary Tier                           | Secondary Tier | Rationale                                                                                              |
| --------------------------------------------- | -------------------------------------- | -------------- | ------------------------------------------------------------------------------------------------------ |
| ActionInstance/ActionState struct definitions | `ajazz_core` (installed public header) | —              | Profile schema lives in core; must be nlohmann-free (COD-031) and Qt-free (event-loop-free unit tests) |
| Hand-rolled JSON (de)serialization            | `ajazz_core` (`profile.cpp` impl TU)   | —              | Writer/reader already live here; nlohmann is forbidden in core, so this stays hand-rolled              |
| v1->v2 lazy migration                         | `ajazz_core` (`profileFromJson`)       | —              | Migration is a read-time parser concern, not an app/device concern                                     |
| Round-trip tests                              | `tests/unit` (Catch2)                  | —              | Core serialization unit-tests with no Qt/event loop, mirroring `test_profile_serialization.cpp`        |
| Schema doc + embedded JSON Schema             | `docs/protocols/PROFILE_SCHEMA.md`     | —              | Source-of-truth for wire keys (CLAUDE.md hard rule)                                                    |

## Standard Stack

No new packages. This phase uses **only** what `ajazz_core` already links:

| Library                                                                     | Version          | Purpose                             | Why Standard                                                           |
| --------------------------------------------------------------------------- | ---------------- | ----------------------------------- | ---------------------------------------------------------------------- |
| C++ stdlib (`<optional>`, `<vector>`, `<string>`, `<sstream>`, `<cstdint>`) | C++20            | structs + hand-rolled writer/reader | Core is deliberately dependency-free; COD-031 forbids nlohmann in core |
| Catch2 (`Catch2::Catch2WithMain`)                                           | already vendored | round-trip unit tests               | The whole `tests/unit/` suite is Catch2; mirror existing files         |

**No `npm install` / `pip install` / `cargo add` step exists for this phase.** No Package Legitimacy Audit is required (no external packages installed). slopcheck/registry verification: N/A.

### Alternatives Considered

| Instead of                            | Could Use                                     | Tradeoff                                                                                                            |
| ------------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| hand-rolled writer/reader             | nlohmann::json in core                        | **FORBIDDEN** — COD-031 release-blocker; nlohmann is PRIVATE to `ajazz_plugins` only                                |
| `settings` as nested JSON object      | escaped JSON string                           | CONTEXT-LOCKED to escaped string — keeps wire linear, round-trips unknown sub-keys, matches `Action::settingsJson`  |
| explicit `schemaVersion` field gating | presence-of-`states`/`instance` discriminator | CONTEXT-LOCKED — avoids the brittle version gate that already bit touchZones (CR-01/WR-06, see profile.cpp:882-892) |

## Architecture Patterns

### Data flow (serialization)

```
Profile (C++ struct)
   |  profileToJson(profile)            [profile.cpp:233]
   v
  writeBinding(out, b)                  [profile.cpp:190]   <-- ADD: if b.instance, emit "instance":{...}
   |    writeChain(onPress/onRelease/onLongPress)
   |    writeKeyState(b.state)          [profile.cpp:156]   (legacy singular "state" — KEEP, unchanged)
   |    writeActionInstance(*b.instance)                    <-- NEW helper, mirrors writeKeyState shape
   |        "states":[ writeActionState(...), ... ]         <-- array of KeyState-payload objects
   |        "currentState": <int>
   |        "settings": <escaped JSON string>
   |        "children":[ writeActionInstance(...), ... ]    <-- RECURSIVE
   v
 compact JSON string

JSON string
   |  profileFromJson(json)             [profile.cpp:851]
   v
  readBinding(r)                        [profile.cpp:697]   <-- ADD: key=="instance" -> b.instance = readActionInstance(r)
   |    readActionInstance(r)                               <-- NEW
   |        key=="states"  -> states.push_back(readActionState(r)) per element
   |        key=="state"   -> states = { readActionState(r) }      <-- LAZY v1->v2 FOLD
   |        key=="currentState" -> currentState = readUInt() (clamp to 0 if >= states.size())
   |        key=="settings"     -> settings = readString()
   |        key=="children" -> children.push_back(readActionInstance(r)) per element  (RECURSIVE)
   v
 Profile (C++ struct)
```

### Recommended placement

```
src/core/include/ajazz/core/
  action_instance.hpp     # NEW: ActionInstance + ActionState structs (nlohmann-free, Qt-free)
  profile.hpp             # EDIT: #include "action_instance.hpp"; add std::optional<ActionInstance> instance
                          #       to Binding (after line 104) and EncoderBinding (after line 117)
src/core/src/
  profile.cpp             # EDIT: add writeActionState/readActionState + writeActionInstance/readActionInstance
                          #       in the anon namespace; wire into writeBinding/readBinding + encoder variants
tests/unit/
  test_action_instance.cpp  # NEW: [action_instance]-tagged round-trip + migration tests
  CMakeLists.txt            # EDIT: add test_action_instance.cpp to the ajazz_unit_tests sources list
docs/protocols/PROFILE_SCHEMA.md  # EDIT: add $defs.ActionInstance + $defs.ActionState; add instance/states keys
```

### Pattern 1: Optional nested object on a binding (mirror KeyState)

**What:** `Binding::state` is written only when non-default and read by matching the `"state"` key. `ActionInstance` follows the same shape but is wrapped in `std::optional` (CONTEXT: "No `instance` key -> `instance = std::nullopt`").
**When to use:** the `instance` field on Binding/EncoderBinding.
**Example (writer side — extend profile.cpp:190-202):**

```cpp
// Source: codebase profile.cpp writeBinding (verified)
void writeBinding(std::ostringstream& out, Binding const& b) {
    out << "{";
    writeChain(out, "onPress", b.onPress);
    out << ",";
    writeChain(out, "onRelease", b.onRelease);
    out << ",";
    writeChain(out, "onLongPress", b.onLongPress);
    if (!keyStateIsDefault(b.state)) {
        out << ",\"state\":";
        writeKeyState(out, b.state);
    }
    if (b.instance) {                      // NEW: emit only when present (optional)
        out << ",\"instance\":";
        writeActionInstance(out, *b.instance);
    }
    out << "}";
}
```

**Example (reader side — extend profile.cpp:697-723):**

```cpp
// in readBinding's key-dispatch chain:
} else if (key == "instance") {
    b.instance = readActionInstance(r);   // NEW; absent key leaves std::nullopt
} else {
    r.skipValue();
}
```

### Pattern 2: writeActionState / readActionState (reuse KeyState payload)

**What:** CONTEXT-LOCKED: "Full `KeyState` parity per state ... reuse `KeyState` as the per-state visual payload." So `ActionState` IS a `KeyState` (either `struct ActionState { KeyState visual; }` or a typedef). Simplest faithful model: each state's wire object is exactly the existing `$defs.KeyState` shape. **Recommendation:** define `ActionState` as a thin wrapper containing a `KeyState` (leaves room for a future per-state non-visual field without a wire break), and serialize it by delegating to the existing `writeKeyState`/`readKeyState`.
**Example:**

```cpp
// writeActionState delegates to the existing KeyState serializer
void writeActionState(std::ostringstream& out, ActionState const& s) {
    writeKeyState(out, s.visual);   // emits {"imagePath":..,"text":..,...,"fontSize":n}
}
ActionState readActionState(JsonReader& r) {
    ActionState s{};
    s.visual = readKeyState(r);     // reuses the existing reader at profile.cpp:667
    return s;
}
```

NOTE: `writeKeyState` always emits `fontSize` even on a default state (profile.cpp:185-186), so an ActionState always produces a non-empty object — unlike `Binding::state` which is omitted entirely when default. That is correct here: a `states[]` element must be present in the array even if visually blank.

### Pattern 3: states[] array + lazy v1->v2 fold inside readActionInstance

**What:** The reader accepts BOTH the new `"states":[...]` array form AND a legacy singular `"state":{...}` object, folding the latter into a one-element vector. The writer ALWAYS emits `"states":[...]`.
**Example (reader):**

```cpp
ActionInstance readActionInstance(JsonReader& r) {
    ActionInstance inst{};
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const key = r.readString();
            r.expect(':');
            if (key == "states") {
                r.expect('[');
                if (!r.tryConsume(']')) {
                    while (true) {
                        inst.states.push_back(readActionState(r));
                        if (r.tryConsume(',')) continue;
                        r.expect(']');
                        break;
                    }
                }
            } else if (key == "state") {          // LAZY v1->v2 FOLD: singular -> array-of-one
                inst.states.clear();
                inst.states.push_back(readActionState(r));
            } else if (key == "currentState") {
                inst.currentState = r.readUInt();
            } else if (key == "settings") {
                inst.settings = r.readString();   // escaped JSON string, mirrors Action::settingsJson
            } else if (key == "children") {
                r.expect('[');
                if (!r.tryConsume(']')) {
                    while (true) {
                        inst.children.push_back(readActionInstance(r));  // RECURSIVE
                        if (r.tryConsume(',')) continue;
                        r.expect(']');
                        break;
                    }
                }
            } else if (key == "id" || key == "uuid") {   // optional: action UUID if modeled
                inst.actionUuid = r.readString();
            } else {
                r.skipValue();                    // forward-compat: skip unknown keys
            }
            if (r.tryConsume(',')) continue;
            r.expect('}');
            break;
        }
    }
    // currentState defensive clamp (CONTEXT: clamp out-of-range to 0 on read)
    if (inst.states.empty() || inst.currentState >= inst.states.size()) {
        inst.currentState = 0;
    }
    return inst;
}
```

### Anti-Patterns to Avoid

- **Gating the `instance`/`states` branches on `_schemaVersion`.** This is exactly the CR-01/WR-06 bug the touchZones reader had (profile.cpp:882-892 documents the fix). RFC 8259 does not guarantee key order; gate on key *presence*, never on a version number read earlier in the stream.
- **Storing `settings` as a nested JSON object.** CONTEXT-LOCKED and COD-031-relevant: it must be an escaped JSON *string* like `Action::settingsJson`. A nested object would require a general JSON value model in core (which the narrow reader does not have) and would not round-trip unknown sub-keys.
- **Emitting singular `"state"` from the writer.** The writer ALWAYS emits `"states":[...]`; only the *reader* understands the legacy singular form. Otherwise round-trip tests on a folded v1 profile would re-emit v1.
- **Inventing a narrower per-state struct.** CONTEXT says reuse the full `KeyState` payload for state parity.
- **Adding `nlohmann` or any Qt type to `action_instance.hpp`.** It is an installed public core header — COD-031 + the Qt-free-core invariant both apply. Use only `<optional>`, `<string>`, `<vector>`, `<cstdint>` and the existing `KeyState` (which is itself in `profile.hpp`/`capabilities.hpp`).

## ActionInstance / ActionState field set (faithful to StreamDeck/OpenDeck)

The StreamDeck SDK model [CITED: docs.elgato.com/streamdeck/sdk/guides/actions, guides/settings] is:

- an action **instance** carries per-instance **settings** (persisted JSON), a **states** array (each with its own image/title), and a **currentState** (0-based index, only meaningful when multiple states exist).
- Stream Deck natively supports up to 2 states (Toggle Action); >2 is allowed in the manifest. OpenDeck's **Multi Action** nests **child** action instances run sequentially.

Mapped to this project's hand-rolled, escaped-settings convention:

| C++ field                      | Type                          | JSON wire key                                                                                      | Notes / provenance                                                                                                                                                                                                                                                                                       |
| ------------------------------ | ----------------------------- | -------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ActionInstance::actionUuid`   | `std::string`                 | `"id"` (or `"uuid"`)                                                                               | Dotted plugin action id, e.g. `com.elgato.counter.increment`. Mirrors `Action::id` wire key `"id"`. [ASSUMED] — confirm whether the model needs the action UUID at all in this phase (Phase 32 dispatch needs it; Phase 31 is model-only). Reader should accept both `id` and `uuid` for forward-compat. |
| `ActionInstance::states`       | `std::vector<ActionState>`    | `"states"` (array)                                                                                 | Per-state visuals. Writer always emits the array. Reader also accepts legacy singular `"state"` and folds to one element. [VERIFIED: codebase + CITED: StreamDeck SDK]                                                                                                                                   |
| `ActionInstance::currentState` | `std::uint32_t` (default 0)   | `"currentState"`                                                                                   | 0-based active index; clamp to 0 if `>= states.size()` on read. [CITED: StreamDeck SDK websocket plugin ref]                                                                                                                                                                                             |
| `ActionInstance::settings`     | `std::string`                 | `"settings"`                                                                                       | **Escaped JSON string**, NOT nested object. Mirrors `Action::settingsJson` \<-> `"settings"` (profile.cpp:113, 615; PROFILE_SCHEMA.md:14). [VERIFIED: codebase]                                                                                                                                          |
| `ActionInstance::children`     | `std::vector<ActionInstance>` | `"children"` (array)                                                                               | Recursive — an instance may contain child instances (Multi Action). [CITED: OpenDeck Multi Action]                                                                                                                                                                                                       |
| `ActionState::visual`          | `KeyState`                    | flattened — the state object IS a `$defs.KeyState` (imagePath/text/background/foreground/fontSize) | Reuse `KeyState`; serialize via existing `writeKeyState`/`readKeyState`. [VERIFIED: codebase profile.cpp:156,667]                                                                                                                                                                                        |

**Discretion (CONTEXT):** exact field ordering, whether `ActionState` is a struct-wrapping-`KeyState` vs a typedef, and helper names are the executor's call. Recommendation above (wrapper struct) leaves a forward-compatible seam at zero cost.

**Wire example (new `instance` on a key Binding):**

```jsonc
"1": {
  "onPress": [], "onRelease": [], "onLongPress": [],
  "instance": {
    "id": "com.elgato.counter.increment",
    "states": [
      {"imagePath":"/icons/off.png","text":"Off","fontSize":14},
      {"imagePath":"/icons/on.png","text":"On","fontSize":14}
    ],
    "currentState": 1,
    "settings": "{\"count\":3}",
    "children": []
  }
}
```

## Don't Hand-Roll

| Problem                                       | Don't Build                      | Use Instead                                                       | Why                                                                                              |
| --------------------------------------------- | -------------------------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Per-state visual (de)serialization            | a new state-visual writer/reader | the existing `writeKeyState`/`readKeyState` (profile.cpp:156,667) | exact parity is a CONTEXT requirement; reuse guarantees it and avoids drift                      |
| JSON string escaping for `settings`           | a second escape routine          | the existing `escape()` / `readString()` (profile.cpp:51,407)     | already RFC-8259-minimal and round-trip-tested                                                   |
| RGB array (de)serialization for state colors  | new `[r,g,b]` codec              | `writeRgb`/`readRgb` (profile.cpp:37,651)                         | reached transitively via `writeKeyState`; nothing to do                                          |
| Out-of-range integer rejection (currentState) | bespoke parsing                  | `readUInt()` (profile.cpp:489)                                    | already range-checks against UINT32_MAX and rejects signs; then add the `>= states.size()` clamp |

**Key insight:** Nearly every primitive this phase needs already exists in `profile.cpp`'s anonymous namespace. The new code is almost entirely *composition* of `writeKeyState`/`readKeyState`, `escape`/`readString`, and the array-loop idiom — not new parsing machinery.

## Runtime State Inventory

This phase is **greenfield serialization + lazy read migration**, not a rename/refactor of stored state. There is no rename of any persisted key, no datastore key change, no OS-registered state, no secret/env-var rename, and no build-artifact name change.

| Category            | Items Found                                                                                                                                                                                                                                                                                                                                                              | Action Required                                                                                                                 |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| Stored data         | Existing v1.3 profile JSON files on disk carry the singular `state:` key on bindings. They are NOT mutated by this phase — migration is **lazy on read** (fold `state` -> `states[]` in `readActionInstance` only when an `instance`/legacy object is parsed). The top-level `Binding::state` legacy key is untouched and still read by `readBinding` (profile.cpp:710). | None on disk. Read-path fold only. A v1 profile re-saved after load is rewritten in v2 form by the writer (expected, lossless). |
| Live service config | None — profiles are local files, no external service stores the schema.                                                                                                                                                                                                                                                                                                  | None — verified: profile I/O is `readProfileFromDisk`/`writeProfileToDisk` (`profile_io.hpp`), no network/service.              |
| OS-registered state | None — verified: no Task Scheduler / launchd / systemd unit references the profile schema.                                                                                                                                                                                                                                                                               | None.                                                                                                                           |
| Secrets/env vars    | None — verified: no secret key references `state`/`states`/`instance`.                                                                                                                                                                                                                                                                                                   | None.                                                                                                                           |
| Build artifacts     | New TU `test_action_instance.cpp` added to `ajazz_unit_tests`; a `cmake` reconfigure is required so the new source compiles (standard, automatic on next build).                                                                                                                                                                                                         | Reconfigure/build (normal).                                                                                                     |

**The canonical question — "after the code changes, what runtime systems still have the old string cached?"**: Nothing. Existing v1 profile files keep the singular `state:` key on disk and load correctly via the lazy fold; they are only rewritten to `states[]` if/when the user re-saves. This is the success-criterion-3 contract, by design.

## Common Pitfalls

### Pitfall 1: Version-gating the new keys (the touchZones CR-01 trap, again)

**What goes wrong:** Reading `_schemaVersion` first and only parsing `instance`/`states` when version >= N silently discards data when keys arrive out of order.
**Why it happens:** RFC 8259 does not guarantee object key order; a hand-edit or third-party tool can emit `instance` before `_schemaVersion`.
**How to avoid:** Gate purely on key *presence*, exactly like the post-fix touchZones branch (profile.cpp:882-892). The CONTEXT decision "No explicit schema-version field; presence of states/instance is the discriminator" enforces this.
**Warning signs:** Any `if (schemaVersion >= ...)` near the new branches. There must be none.

### Pitfall 2: Writer emitting singular `state` breaks round-trip on folded v1 profiles

**What goes wrong:** If the writer ever emits `"state"` for an ActionInstance, a v1->load->save->reload cycle stays v1 and success-criterion-3 ("round-trips correctly as a states array of one") fails.
**How to avoid:** Writer emits ONLY `"states":[...]`. The singular `"state"` key is a *reader-only* legacy alias inside `readActionInstance`.
**Warning signs:** grep the new writer for `"state"` (without the `s`) — it should appear only in the unchanged `writeBinding`/`writeKeyState` legacy `Binding::state` path, never in `writeActionInstance`.

### Pitfall 3: Recursive `std::vector<ActionInstance>` member-of-incomplete-type

**What goes wrong:** `struct ActionInstance { std::vector<ActionInstance> children; };` — a class containing a `std::vector` of itself. This is **well-defined and legal** in C++ for `std::vector` (the standard library allows incomplete types in `std::vector` since C++17), unlike a raw `ActionInstance children[]` or `std::array`. No `std::unique_ptr` indirection is needed.
**How to avoid:** Use `std::vector<ActionInstance>` directly (CONTEXT-LOCKED). Do NOT add `unique_ptr`/`shared_ptr` — unnecessary and would complicate value semantics in tests.
**Warning signs:** A "field has incomplete type" compile error means you used `std::array`/by-value-array or forgot that `std::vector` is the allowed container. Recursion depth is not validated by the narrow reader — if a hostile-input depth guard is wanted, that is a deferred hardening item, not a phase requirement.

### Pitfall 4: COD-031 — nlohmann leaking into the new public header

**What goes wrong:** `action_instance.hpp` is an installed public core header. Any `#include <nlohmann/...>` there is a release-blocker (CLAUDE.md COD-031 boundary).
**How to avoid:** Header has only stdlib + `KeyState` (from `profile.hpp`/`capabilities.hpp`). All JSON lives in `profile.cpp`'s anon namespace (hand-rolled). Verify with the phase success-criterion grep: `grep -rn nlohmann src/core/include/` must return 0.
**Warning signs:** Any include of a JSON or Qt type in the new header.

### Pitfall 5: ASCII-only test names (cross-platform ctest filter)

**What goes wrong:** Em-dash/arrow in a `TEST_CASE` title mangles under the Win32 CMD codepage and Catch2's filter stops matching (CLAUDE.md cross-platform rule).
**How to avoid:** Use `-` and `->` only in test titles. Tag everything `[action_instance]` so `ctest -R action_instance` (success-criterion-2) matches by the executable/test-name regex.

## Code Examples

### CMake registration (mirror test_profile_serialization)

The unit-test target is monolithic: every `tests/unit/*.cpp` is added to the single `ajazz_unit_tests` executable via the `add_executable(...)` source list (CMakeLists.txt:1-308) and discovered by `catch_discover_tests(ajazz_unit_tests)` (CMakeLists.txt:679). To add the new test, append one line to that source list next to the sibling profile tests (after line 184, `test_profile_multiaction.cpp`, or after line 177-178 `test_profile_serialization.cpp` / `test_profile_io.cpp`):

```cmake
# Phase 31 (BIND-01/BIND-02): ActionInstance / ActionState core model + Profile schema v2.
# Pure-core round-trip + v1->v2 migration tests (no Qt, no event loop) — mirrors
# test_profile_serialization.cpp. Runnable via `ctest --preset linux-release -R action_instance`.
test_action_instance.cpp
```

No new `target_link_libraries`/`target_sources` block is needed: `ajazz::core` (which carries `profile.cpp`/`action_instance.hpp`) is already linked (CMakeLists.txt:421), and `Catch2::Catch2WithMain` provides `main()`. `ctest -R action_instance` matches because `catch_discover_tests` registers each `TEST_CASE` by name; tag-or-name containing `action_instance` will be selected.

### Round-trip test skeleton (mirror test_profile_serialization.cpp:43)

```cpp
// Source: pattern from tests/unit/test_profile_serialization.cpp (verified)
#include "ajazz/core/action_instance.hpp"
#include "ajazz/core/profile.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("ActionInstance round-trips 3-state instance via Binding", "[action_instance][roundtrip]") {
    using namespace ajazz::core;
    Profile p{};
    p.id = "ai-3state"; p.name = "AI"; p.deviceCodename = "akp05e";

    Binding b{};
    ActionInstance inst{};
    inst.actionUuid = "com.test.toggle";
    inst.settings = R"({"x":1})";
    for (int i = 0; i < 3; ++i) {
        ActionState s{}; s.visual.text = "S" + std::to_string(i); inst.states.push_back(s);
    }
    inst.currentState = 2;
    b.instance = inst;
    p.keys[0] = b;

    auto const restored = profileFromJson(profileToJson(p));
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.states.size() == 3);
    REQUIRE(ri.currentState == 2);
    REQUIRE(ri.settings == R"({"x":1})");
    REQUIRE(ri.states[1].visual.text.value() == "S1");
}
```

### Required round-trip variants (success-criterion-2) + migration (success-criterion-3,4)

- **0-state** instance: `states` empty -> `currentState` must clamp to 0; emits `"states":[]`.
- **1-state** instance.
- **3-state** instance with `currentState == 2`.
- **2-children** instance: `children.size() == 2`, recursion verified, each child itself round-trips.
- **v1->v2 migration:** parse a literal legacy JSON with a singular `"state":{...}` inside an `instance` object; assert `restored.states.size() == 1` and that re-serializing emits `"states":[`. (Mirror `test_profile_serialization.cpp:202-222` "v1 profile migrates touchZones".)
- **No `instance` key -> `std::nullopt`** (success-criterion-4): parse a binding JSON with no `instance` key; assert `restored.keys.at(k).instance == std::nullopt`. Also assert `EncoderBinding::instance == std::nullopt` for an encoder with no instance.
- **currentState clamp:** parse an instance with `"currentState":99` and 2 states; assert restored `currentState == 0` (lossless load, CONTEXT defensive-read rule).

## State of the Art

| Old Approach                                              | Current Approach                                             | When Changed                                                 | Impact                                             |
| --------------------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------------------------- |
| Single `state:` KeyState per binding                      | per-state visuals via `states[]` on an ActionInstance        | this phase                                                   | Toggle/Multi Action support (dispatch in Phase 32) |
| `_schemaVersion:2` field present + (formerly) gated reads | additive presence-based discrimination (`states`/`instance`) | CR-01/WR-06 (touchZones) -> this phase continues the pattern | No brittle version gate; out-of-order keys safe    |

**Deprecated/outdated:** Nothing is deprecated. The legacy singular `state:` remains readable forever (lazy fold); `Binding::state` (the binding's own KeyState, profile.cpp:104,710) is a *separate* field from any `ActionInstance.states` and is NOT removed.

## Assumptions Log

| #   | Claim                                                                                                    | Section             | Risk if Wrong                                                                                                                                                                                                                                                                                      |
| --- | -------------------------------------------------------------------------------------------------------- | ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | `ActionInstance` carries an action UUID field (`"id"`/`"uuid"`)                                          | field-set table     | LOW — Phase 32 dispatch needs the action id; if the planner decides the model is "visual + settings + children only" for Phase 31, drop the field. Reader should still skip an unknown `id` key gracefully. Confirm during planning whether the UUID belongs in this phase's model or is deferred. |
| A2  | StreamDeck "up to 2 states natively, >2 allowed" maps cleanly to an unbounded `std::vector<ActionState>` | field-set, variants | NONE — the C++ model is intentionally unbounded; the 0/1/3-state test variants are CONTEXT-mandated and exceed the StreamDeck-native 2, which is fine for the data model.                                                                                                                          |

**Everything else is VERIFIED against the codebase or CITED to the StreamDeck SDK docs.**

## Open Questions

1. **Does `ActionInstance` need the action UUID (`id`) in Phase 31, or is it model-visuals-only?**

   - What we know: CONTEXT lists the model as `states[]` / `currentState` / `settings` / `children` — it does NOT explicitly list an action id. StreamDeck/OpenDeck instances always carry the action UUID; Phase 32 dispatch (Multi/Toggle handlers) will need it.
   - What's unclear: whether to add `actionUuid` now (forward-looking) or defer to Phase 32.
   - Recommendation: add an optional `"id"` field now (cheap, forward-compatible, reader skips it if absent). Flag as A1 for planner confirmation; either choice is non-breaking because the reader skips unknown keys.

1. **`ActionState` as wrapper-struct vs typedef of `KeyState`.**

   - What we know: CONTEXT mandates full KeyState parity and explicitly says "reuse `KeyState`".
   - Recommendation: a thin `struct ActionState { KeyState visual; }` wrapper (forward-compatible seam at zero wire cost) — but a bare `using ActionState = KeyState;` is equally CONTEXT-compliant. Executor's discretion (CONTEXT grants this).

## Environment Availability

No external dependencies. This phase is code-only inside `ajazz_core` + a Catch2 test, both already part of the build.

| Dependency                                    | Required By       | Available | Version                      | Fallback |
| --------------------------------------------- | ----------------- | --------- | ---------------------------- | -------- |
| C++20 toolchain (GCC/Clang)                   | core compilation  | ✓         | per CMake presets            | —        |
| Catch2 (vendored)                             | unit tests        | ✓         | linked in `ajazz_unit_tests` | —        |
| CMake + Ninja, `ctest --preset linux-release` | build + test gate | ✓         | working preset (CLAUDE.md)   | —        |

**Missing dependencies with no fallback:** none.

## Validation Architecture

### Test Framework

| Property           | Value                                                                                      |
| ------------------ | ------------------------------------------------------------------------------------------ |
| Framework          | Catch2 (`Catch2::Catch2WithMain`), monolithic `ajazz_unit_tests` target                    |
| Config file        | `tests/unit/CMakeLists.txt` (single `add_executable` source list + `catch_discover_tests`) |
| Quick run command  | `ctest --preset linux-release -R action_instance`                                          |
| Full suite command | `ctest --preset linux-release` (~408 cases per CLAUDE.md; trust live count)                |

### Phase Requirements -> Test Map

| Req ID  | Behavior                                                                                                               | Test Type        | Automated Command                                                       | File Exists?                           |
| ------- | ---------------------------------------------------------------------------------------------------------------------- | ---------------- | ----------------------------------------------------------------------- | -------------------------------------- |
| BIND-01 | ActionInstance model + hand-rolled (de)serialization; `Binding`/`EncoderBinding` carry `std::optional<ActionInstance>` | unit             | `ctest --preset linux-release -R action_instance`                       | ❌ Wave 0 (`test_action_instance.cpp`) |
| BIND-01 | COD-031 preserved — no nlohmann in new public header                                                                   | grep gate        | `grep -rn nlohmann src/core/include/` (expect 0)                        | ✅ existing invariant                  |
| BIND-02 | v1 singular `state` folds to `states[]` array-of-one, round-trips                                                      | unit (migration) | `ctest --preset linux-release -R action_instance`                       | ❌ Wave 0                              |
| BIND-02 | 0/1/3-state + 2-children round-trip variants                                                                           | unit             | `ctest --preset linux-release -R action_instance`                       | ❌ Wave 0                              |
| BIND-02 | no `instance` key -> `std::nullopt` (Binding + EncoderBinding)                                                         | unit             | `ctest --preset linux-release -R action_instance`                       | ❌ Wave 0                              |
| BIND-02 | existing v1.3 profile still loads unchanged                                                                            | unit             | existing `test_profile_serialization.cpp` v1-migration cases stay green | ✅ regression-guard                    |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R action_instance` (+ `grep -rn nlohmann src/core/include/` == 0).
- **Per wave merge:** `ctest --preset linux-release -R "profile|action_instance"` (catch profile-serialization regressions).
- **Phase gate:** full `ctest --preset linux-release` green before `/gsd:verify-work`.

### Wave 0 Gaps

- [ ] `tests/unit/test_action_instance.cpp` — covers BIND-01, BIND-02 (round-trip + migration + nullopt + clamp variants)
- [ ] `tests/unit/CMakeLists.txt` — add `test_action_instance.cpp` to the `ajazz_unit_tests` source list (after line 184)
- [ ] `src/core/include/ajazz/core/action_instance.hpp` — new nlohmann-free header
- [ ] Framework install: none — Catch2 already vendored and linked

## Security Domain

> `security_enforcement` not set to `false` for this codebase; the section is included but the attack surface is narrow.

### Applicable ASVS Categories

| ASVS Category         | Applies | Standard Control                                                                                                                                                                                                                                                                                                                                                                                               |
| --------------------- | ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no      | no auth in this phase                                                                                                                                                                                                                                                                                                                                                                                          |
| V3 Session Management | no      | n/a                                                                                                                                                                                                                                                                                                                                                                                                            |
| V4 Access Control     | no      | n/a                                                                                                                                                                                                                                                                                                                                                                                                            |
| V5 Input Validation   | yes     | the hand-rolled reader is the trust boundary for profile JSON (incl. third-party/hand-edited files). Reuse existing hardening: `readUInt()` range-checks + sign-rejection (profile.cpp:489-519), map-key bounds checks (profile.cpp:790-801), `skipValue()` for unknown keys. Add: `currentState` clamp; do NOT recurse `children` without bound if hostile-depth is a concern (deferred hardening — note it). |
| V6 Cryptography       | no      | no crypto introduced                                                                                                                                                                                                                                                                                                                                                                                           |

### Known Threat Patterns for hand-rolled JSON parser

| Pattern                                               | STRIDE            | Standard Mitigation                                                                                                                                                                                             |
| ----------------------------------------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Integer overflow/wrap in `currentState` / state count | Tampering         | `readUInt()` already rejects out-of-range/signed; add `>= states.size()` clamp to 0                                                                                                                             |
| Deeply nested `children` recursion (stack exhaustion) | Denial of Service | LOW risk for local profile files; if hardening is wanted later, add a depth cap. **Deferred** (CONTEXT defers fuzz/property-based tests); flag as a known non-blocking item, do not implement blind this phase. |
| Malformed/truncated JSON                              | Tampering         | existing reader throws `std::runtime_error` with byte offset (profile.cpp:571); new branches inherit this via `expect()`/`fail()`                                                                               |

## Sources

### Primary (HIGH confidence)

- `src/core/include/ajazz/core/profile.hpp` — current schema (Action, KeyState, Binding, EncoderBinding, TouchZoneBinding, ProfilePage, Profile; profileToJson/profileFromJson decls)
- `src/core/src/profile.cpp` — hand-rolled writer (writeKeyState:156, writeBinding:190, writeEncoderBinding:205, writeTouchZoneBinding:221) + reader (readKeyState:667, readBinding:697, readEncoderBinding:725, readActionArray:634, JsonReader:366; touchZones CR-01 fix:882-892; readUInt hardening:489)
- `docs/protocols/PROFILE_SCHEMA.md` — wire-key source of truth (settings = escaped string:14,189; $defs.KeyState:96; additive-only policy:176)
- `tests/unit/test_profile_serialization.cpp` — round-trip + v1-migration test idioms (touchZones migration:202; KeyState round-trip:132; ordering regression:278)
- `tests/unit/CMakeLists.txt` — monolithic ajazz_unit_tests source list (1-308) + catch_discover_tests (679); ajazz::core/Catch2 linked (421-426)
- `.planning/REQUIREMENTS.md` — BIND-01 (24), BIND-02 (25)
- `./CLAUDE.md` — COD-031 boundary, schema-doc-is-truth, ASCII-only test names, ctest preset

### Secondary (MEDIUM confidence)

- Stream Deck SDK — Actions, Settings, Keys, websocket/plugin references [CITED] — states[]/currentState/settings instance semantics

## Project Constraints (from CLAUDE.md)

- **COD-031 (release-blocker):** no `nlohmann::json` in `ajazz_core` or any installed public header. `action_instance.hpp` MUST be nlohmann-free and Qt-free. Verify: `grep -rn nlohmann src/core/include/` == 0 (a phase success criterion).
- **Schema doc is source of truth for JSON wire keys.** Update `docs/protocols/PROFILE_SCHEMA.md` + embedded JSON Schema this phase; align field names to the schema, never to the C++ field name.
- **Conventional Commits + atomic commits + pre-commit hooks** (never `--no-verify` unless the hook itself is broken). Likely scopes: `feat(profile):` / `feat(core):` / `test(profile):` / `docs(profile):`.
- **GitFlow-lite:** work on this `experiment/mirajazz` branch (the active feature branch — always test here, per the "test on feature branch" memory), not main/develop.
- **ASCII-only ctest test names** (`-` and `->`, no em-dash/arrow).
- **Working preset:** `ctest --preset linux-release`; filter flag is `-R` / `--tests-regex`.
- **Hand-rolled-JSON-in-core invariant:** no Qt in core so tests run without an event loop — mirror the dependency-free style of `profile.cpp`.

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — no new packages; uses only existing core + Catch2.
- Architecture / serialization extension points: HIGH — every helper has an in-codebase template with exact line anchors.
- Field set faithfulness: HIGH for states/currentState/settings/children (codebase + StreamDeck SDK); MEDIUM for whether the action UUID belongs in Phase 31 (A1 — planner to confirm).
- Migration mechanic: HIGH — directly mirrors the verified touchZones lazy-migration pattern.
- Pitfalls: HIGH — drawn from documented in-codebase fixes (CR-01/WR-06, readUInt hardening, ASCII test names).

**Research date:** 2026-06-08
**Valid until:** 2026-07-08 (stable — internal serialization; no fast-moving external dependency)
