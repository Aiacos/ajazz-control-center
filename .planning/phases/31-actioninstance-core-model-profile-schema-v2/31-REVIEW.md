---
phase: 31-actioninstance-core-model-profile-schema-v2
reviewed: 2026-06-08T00:00:00Z
depth: standard
files_reviewed: 6
files_reviewed_list:
  - src/core/include/ajazz/core/action_instance.hpp
  - src/core/include/ajazz/core/profile.hpp
  - src/core/src/profile.cpp
  - docs/protocols/PROFILE_SCHEMA.md
  - tests/unit/test_action_instance.cpp
  - tests/unit/CMakeLists.txt
findings:
  critical: 1
  warning: 5
  info: 3
  total: 9
status: issues_found
---

# Phase 31: Code Review Report

**Reviewed:** 2026-06-08T00:00:00Z
**Depth:** standard
**Files Reviewed:** 6
**Status:** issues_found

## Summary

Phase 31 adds an OpenDeck/StreamDeck-shaped `ActionInstance`/`ActionState` core
model with hand-rolled JSON (de)serialisation, additively threaded onto
`Binding`/`EncoderBinding`. The structural design is sound: the COD-031 boundary
holds (no nlohmann / no Qt in any `src/core/include/` header — only doc-comment
mentions), `KeyState` was correctly relocated into `action_instance.hpp` with a
clean one-directional include from `profile.hpp` (no ODR risk — single
definition, `#pragma once`), the lazy v1->v2 migration gates on key PRESENCE not
a `_schemaVersion` field as required, wire keys match `PROFILE_SCHEMA.md`, and
the test titles are ASCII-only.

The dominant defect is a parser-robustness gap that the phase brief explicitly
called out: **the recursive reader has no depth limit**, so a deeply-nested
`children` (or unknown-key) payload overflows the stack and crashes the process.
Because `profileFromJson` is reachable from on-disk profile files (loaded via
`readProfileFromDisk`, including imported/plugin-bundled profiles), this is a
denial-of-service / crash vector that directly contradicts the code's own stated
"lossless load -- never reject, never crash" contract. Secondary findings cover
an incomplete JSON-escape writer, a key-order-dependent state/states fold, a
silent clamp that drops data, and round-trip tests that under-assert byte
stability for the recursive/multi-state paths.

## Critical Issues

### CR-01: Unbounded recursion in the JSON reader (stack-overflow DoS on hostile/corrupt profiles)

**File:** `src/core/src/profile.cpp:785-841` (`readActionInstance`, the
`children` branch at 812-823) and `src/core/src/profile.cpp:597-644`
(`skipValue`, nested object/array branches at 601-626)

**Issue:** Neither `readActionInstance` (recursing on `"children"`) nor
`skipValue` (recursing on nested unknown objects/arrays) carries a recursion
depth limit. A profile of the form
`{"id":"x","name":"y","device":"z","keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],"instance":{"children":[{"children":[{"children":[ ... ]}]}]}}}}`
nested a few thousand levels deep — or the same nesting hidden under any unknown
key, which routes through `skipValue` — will exhaust the C++ call stack and
**crash the entire application with a SIGSEGV**. `profileFromJson` is fed
untrusted-ish input: `readProfileFromDisk` loads profile files from disk, and
profiles can arrive via plugin bundles / import / hand-edit. The reader's own
documented contract (line 836: "never reject, never index OOB") and the schema's
"forward-compatible readers ... skipped silently" promise are both violated — a
stack overflow is neither a graceful skip nor a `std::runtime_error`. Every other
malformed-input path in this parser fails loudly via `fail()`; recursion depth is
the one hole that crashes instead.

**Fix:** Thread a depth counter through the recursive paths and `fail()` past a
sane cap (children nesting and arbitrary value nesting are both bounded in any
real profile). Example:

```cpp
// Add to JsonReader:
static constexpr int kMaxDepth = 64;
int depth_{0};
struct DepthGuard {
    JsonReader& r;
    explicit DepthGuard(JsonReader& rr) : r(rr) {
        if (++r.depth_ > kMaxDepth) {
            r.fail("maximum nesting depth exceeded");
        }
    }
    ~DepthGuard() { --r.depth_; }
};

// At the top of readActionInstance(JsonReader& r):
DepthGuard guard{r};

// At the top of skipValue():
DepthGuard guard{r};
```

(Depth is the right axis to guard, not total node count — a wide-but-shallow
profile is fine; only nesting blows the stack.)

## Warnings

### WR-01: `escape()` emits raw control characters (0x00-0x1F), producing invalid JSON

**File:** `src/core/src/profile.cpp:51-76` (`escape`)

**Issue:** The writer escapes only `"`, `\`, `\n`, `\r`, `\t`. Any other control
character — NUL (`0x00`), `\b` (`0x08`), `\f` (`0x0C`), `0x01`..`0x1F` — is
emitted verbatim in the `default:` branch. RFC 8259 requires all U+0000..U+001F
to be escaped. The `settings` field is documented as carrying an "opaque escaped
JSON string" that may hold arbitrary plugin-supplied bytes, and `text`/`label`
are free-form user strings. The narrow in-house reader happens to read these raw
bytes back (so the project's own round-trip survives), but the emitted file is
**not valid JSON** and will be rejected or mangled by any external tool, the
app-layer `QJsonDocument` override mentioned in `profile.hpp:199`, or a future
strict reader — and a raw NUL byte specifically can truncate the value in any
C-string consumer.

**Fix:** Escape `\b` and `\f` explicitly and `\u00XX`-escape the remaining
control range:

```cpp
case '\b': out << "\\b"; break;
case '\f': out << "\\f"; break;
default:
    if (static_cast<unsigned char>(ch) < 0x20) {
        char buf[7];
        std::snprintf(buf, sizeof buf, "\\u%04x",
                      static_cast<unsigned>(static_cast<unsigned char>(ch)));
        out << buf;
    } else {
        out << ch;
    }
    break;
```

### WR-02: Legacy `state` / new `states` fold is order-dependent and can silently drop data

**File:** `src/core/src/profile.cpp:792-807` (`readActionInstance`)

**Issue:** The `"state"` branch (804-807) does `inst.states.clear()` then pushes
one element; the `"states"` branch (792-803) appends. If a (hand-edited or
third-party) instance object carries BOTH keys, the result depends on key order:
`states` then `state` -> the entire `states` array is silently wiped down to the
single folded legacy state; `state` then `states` -> the legacy state is kept and
the real states append after it. RFC 8259 does not guarantee key order, so this
is a non-deterministic data-loss path. The writer never emits both, but the
reader is documented as accepting external input.

**Fix:** Make the fold deterministic and non-destructive — only fold the legacy
singular `state` when no `states` array was seen, and ignore (or `skipValue`) a
legacy `state` once `states` is populated:

```cpp
} else if (key == "state") {
    // Only honour the legacy singular form if no states[] has been parsed.
    if (inst.states.empty()) {
        inst.states.push_back(readActionState(r));
    } else {
        (void)readActionState(r); // states[] wins; discard the legacy form.
    }
}
```

### WR-03: Out-of-range `currentState` is silently clamped to 0, losing user intent without diagnostic

**File:** `src/core/src/profile.cpp:836-839` (`readActionInstance`)

**Issue:** When `currentState >= states.size()`, the value is reset to 0 with no
log/warning. This is defensible as a safety clamp (it does prevent an OOB index
downstream), but it silently mutates persisted user state on a benign edit — e.g.
a user deletes a state from a 3-state toggle by hand and the active index quietly
snaps back to the first state instead of the nearest valid one. Clamping to
`states.size() - 1` would preserve "user wanted the last state" intent; clamping
to 0 discards it. The phase brief lists this clamp as intended behavior, so this
is a WARNING (robustness/UX), not a blocker — but the choice of 0 vs last is worth
a deliberate decision.

**Fix:** Prefer clamping to the nearest valid index rather than always 0:

```cpp
if (inst.states.empty()) {
    inst.currentState = 0;
} else if (inst.currentState >= inst.states.size()) {
    inst.currentState = static_cast<std::uint32_t>(inst.states.size() - 1);
}
```

If clamp-to-0 is the deliberate product choice, leave it but document why in the
struct comment (action_instance.hpp:87-88) so it is not "fixed" later by mistake.

### WR-04: Round-trip tests assert structure but not byte-stable serialisation; the recursive/clamp paths under-test the writer

**File:** `tests/unit/test_action_instance.cpp:33-226`

**Issue:** The "round-trips" cases assert the *restored struct's* fields, which
proves read+write agree with each other but does **not** prove the serialisation
is stable or that the writer emits the schema-correct shape. Concretely:
(1) the 2-children case (120-161) never re-serialises and compares
`profileToJson(p1) == profileToJson(profileFromJson(profileToJson(p1)))`, so a
writer regression in the recursive `children` emit (e.g. a missing separator on
nested children) that the lenient reader happens to tolerate would pass; (2) the
0-state case (33-54) asserts `currentState == 0` but that is also the default, so
it cannot distinguish "clamped" from "never set"; (3) no test exercises the
CR-01 deep-nesting failure mode at all. The brief explicitly asks whether the
tests assert round-trip *stability* — currently they do not.

**Fix:** Add a double-serialise idempotence assertion to at least the children
and 3-state cases, and add a depth-limit test once CR-01 is fixed:

```cpp
auto const j1 = profileToJson(p);
auto const j2 = profileToJson(profileFromJson(j1));
REQUIRE(j1 == j2);                       // byte-stable round-trip
// after CR-01 fix:
REQUIRE_THROWS_AS(profileFromJson(deeplyNested), std::runtime_error);
```

### WR-05: `readUintKeyedMap` uses `std::stoul` directly, accepting non-canonical / signed map keys the writer never emits

**File:** `src/core/src/profile.cpp:938-959` (`readUintKeyedMap`) and the
inline touchZones key loop at `1046-1062`

**Issue:** Map keys are parsed with `std::stoul(idxStr)`, which accepts leading
whitespace, a leading `+`/`-` (e.g. `"-1"` wraps to a huge unsigned that then
fails the range check, but `"-0"` -> 0 succeeds), and a trailing-garbage prefix
(`std::stoul("12abc")` returns 12 and does not report the unconsumed tail). The
keys/encoders/touchZones maps are therefore looser than the schema's
`"^[0-9]+$"` patternProperties contract — `"+5"`, `" 5"`, or `"5x"` all silently
map to index 5. This is inconsistent with the strict `readUInt()` used elsewhere
(which explicitly rejects a leading sign, profile.cpp:569). Not a crash, but a
silent-acceptance correctness gap for hand-edited or third-party profiles.

**Fix:** Validate the key matches `^[0-9]+$` before/instead of `stoul`, mirroring
`readUInt`'s discipline (reject sign, require all-digit, range-check):

```cpp
if (idxStr.empty() ||
    idxStr.find_first_not_of("0123456789") != std::string::npos) {
    throw std::runtime_error("profileFromJson: non-numeric map key \"" + idxStr + "\"");
}
```

## Info

### IN-01: `_schemaVersion` is parsed into a `[[maybe_unused]]` local that is never consulted

**File:** `src/core/src/profile.cpp:1014, 1018-1019`

**Issue:** `schemaVersion` is read from the wire and stored but never used for any
dispatch (the touchZones branch was intentionally decoupled from it per the
CR-01/WR-06 fix referenced in the comments). The dead local is honestly
documented as a forward seam, so this is informational only. Confirm the
intent — if no near-term consumer is planned, consuming via `r.readUInt()`
without binding the result would be cleaner; if the seam is wanted, keep it.

### IN-02: Recursive-children memory growth is unbounded even after a depth cap

**File:** `src/core/src/profile.cpp:812-823`

**Issue:** Once CR-01 caps nesting depth, a wide payload (millions of sibling
children at shallow depth) can still allocate large vectors. This is a
resource-exhaustion edge, explicitly out of scope for v1 (performance/DoS-by-size
is not correctness), noted only so it is on the record alongside the depth fix.
No action required for this phase.

### IN-03: `ActionState` is a one-field wrapper around `KeyState`; verify the seam earns its cost

**File:** `src/core/include/ajazz/core/action_instance.hpp:63-65`

**Issue:** `ActionState` currently holds only `KeyState visual`, and on the wire
it serialises to exactly a bare `KeyState` object (writeActionState delegates
straight to writeKeyState). The header documents this as a deliberate
forward-compatibility seam for a future per-state non-visual field at zero wire
cost, which is reasonable. Informational: this is intentional and the doc is
accurate; left here only so a future reader does not "simplify" the wrapper away
and break the forward seam.

______________________________________________________________________

_Reviewed: 2026-06-08T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
