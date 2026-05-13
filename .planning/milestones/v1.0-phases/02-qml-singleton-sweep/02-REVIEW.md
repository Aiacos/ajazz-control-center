---
phase: 02-qml-singleton-sweep
reviewed: 2026-05-12T00:00:00Z
depth: standard
files_reviewed: 8
files_reviewed_list:
  - src/app/src/branding_service.hpp
  - src/app/src/autostart_service.hpp
  - src/app/src/loaded_plugins_model.hpp
  - src/app/src/plugin_catalog_model.hpp
  - src/app/src/profile_controller.hpp
  - src/app/src/property_inspector_controller.hpp
  - tests/unit/test_branding_service.cpp
  - tests/unit/test_theme_service.cpp
findings:
  critical: 0
  warning: 3
  info: 4
  total: 7
status: issues_found
---

# Phase 2: QML Singleton Sweep — Code Review Report

**Reviewed:** 2026-05-12
**Depth:** standard (per-file analysis + cross-site consistency check + sweep
completeness audit against `grep -rn "QML_SINGLETON" src/`)
**Files Reviewed:** 8 (six headers + two unit-test files)
**Status:** issues_found

## Summary

The fix pattern itself is sound and uniformly applied across the six in-scope
services (`BrandingService`, `AutostartService`, `LoadedPluginsModel`,
`PluginCatalogModel`, `ProfileController`, `PropertyInspectorController`):
each ctor's defaulted `parent` argument is removed, each header carries a
short comment pointing back to `BrandingService` for rationale, and each
class already had a working `create()`/`registerInstance()` pair from the
earlier `7df853c` migration. `Application::exposeToQml` correctly calls
`registerInstance` for every service before `engine.loadFromModule(...)` in
`main.cpp`, and the SEC-003 wiring in `LoadedPluginsModel` (the `m_host`
member, `setPluginHost`, `refresh` exception-swallowing) is preserved
intact.

The most material concerns this review surfaces are about *what the work
deliberately did not do*, all of which are warnings rather than blockers:

1. The two updated unit tests (`test_branding_service.cpp`,
   `test_theme_service.cpp`) only adapt call sites from `BrandingService svc;`
   to `BrandingService svc(nullptr);`. They never construct two instances and
   read from one while writing to the other — i.e. they do not exercise the
   dual-instance code path the fix targets, and they would not regress if
   somebody re-introduced `= nullptr` on the ctor parent. The class-of-bug
   stays untested.
1. The defense (drop the default arg) is brittle: it survives only as long
   as nobody adds a defaulted second ctor parameter, a defaulted-default
   data-member-initialiser, or a second public `T()` ctor overload. There
   is no `static_assert(!std::is_default_constructible_v<T>)` on any of the
   six classes, and no compile-time guard that `create()`/`registerInstance()`
   stay paired with `QML_SINGLETON`. A future cleanup pass that "tidies up"
   the ctor will silently re-introduce the bug — the same trap the original
   commit message warned about.
1. The sweep skipped three further `QML_SINGLETON` classes
   (`ThemeService`, `TrayController`, `DeviceModel`) on the documented
   theory that "their ctors require a non-default reference / pointer".
   That is true *today*, but only one of the three (`ThemeService`) carries
   any code comment to that effect; the other two will be silently broken
   the day someone adds a default to e.g.
   `TrayController(BrandingService*, ProfileController* = nullptr, QObject* = nullptr)`'s
   first parameter, or default-initialises `DeviceRegistry&` away in a
   refactor. The protective rationale is load-bearing but not localised.

The remaining items are doc / cosmetic.

No build-breakage, no security issue, no regression of SEC-003 wiring.

## Warnings

### WR-01: Updated unit tests do not exercise the dual-instance code path

**Files:** `tests/unit/test_branding_service.cpp`, `tests/unit/test_theme_service.cpp`
**Issue:** Both tests were touched by `d7f932f` in a strictly mechanical way:
every `BrandingService svc;` and `BrandingService branding;` was rewritten to
`BrandingService svc(nullptr);` / `BrandingService branding(nullptr);`. The
edits exist purely to keep the test binary compiling after the ctor lost its
default argument. None of the seven branding tests, nor any of the seven
theme tests, instantiates *two* `BrandingService` objects and verifies that
mutations on one are visible to the other (or, more strongly, that
`std::is_default_constructible_v<BrandingService> == false`). Concretely:
the bug class was "QML factory route silently picks Constructor over
Factory whenever `is_default_constructible<T>::value` is true"; the test
suite never asserts on either side of that constraint.

Practically, this means: revert the `branding_service.hpp` change
(reinstating `QObject* parent = nullptr`), rebuild, and 14/14 of the
touched tests still pass. The user-visible bug returns; the regression net
catches nothing.

**Fix:** Add one ctor-shape pin per affected service. A single-line
compile-time check is enough and removes the tests' dependency on a runtime
QML engine:

```cpp
// tests/unit/test_branding_service.cpp — top of file
#include <type_traits>
static_assert(!std::is_default_constructible_v<ajazz::app::BrandingService>,
              "BrandingService must NOT be default-constructible — Qt 6's "
              "QML_SINGLETON SFINAE picks Constructor over Factory and "
              "spawns a duplicate QML-side instance. See d7f932f.");
```

Replicate for `AutostartService`, `LoadedPluginsModel`, `PluginCatalogModel`,
`ProfileController`, `PropertyInspectorController` (and ideally
`ThemeService`, `TrayController`, `DeviceModel` to lock in the
"already-safe" claim from the `e221b21` commit body — see WR-03).

### WR-02: No localised guard against future re-introduction of default-constructibility

**Files:** all six modified headers
**Issue:** The defence the sweep installs is "the *parent* argument has no
default value." That is a load-bearing invariant — Qt 6's
`qqmlprivate.h::singletonConstructionMode<T>()` checks
`std::is_default_constructible_v<T>` directly, without asking why — but the
invariant lives only in a `// No default on \`parent\`: see BrandingService …\`
comment. A future commit can break it in any of the following ways without
tripping a test, a lint, or a compile error:

- re-introducing `QObject* parent = nullptr` (the original bug);
- adding a second ctor with all-defaulted args
  (e.g. an `explicit ProfileController(QString configPath = {}, QObject* parent = nullptr)`
  convenience overload);
- moving any required-by-reference ctor parameter behind a default
  (e.g. switching `core::DeviceRegistry&` to a pointer with `= nullptr`
  in `DeviceModel`).

In each case, the `Q_ASSERT_X` in `create()` keeps firing in debug builds
*only if QML imports the singleton before `registerInstance` runs* — which
the production code path explicitly orders to avoid. Release builds drop
the assert entirely. The dual-instance silently returns.

**Fix:** Co-locate a `static_assert` next to each `QML_SINGLETON` macro so
the invariant is checked at the same point the macro picks the construction
mode. Example for `branding_service.hpp` (insert directly after the class
body, or at namespace scope at the bottom of the header):

```cpp
static_assert(
    !std::is_default_constructible_v<BrandingService>,
    "BrandingService is QML_SINGLETON; making it default-constructible "
    "causes Qt 6 to pick SingletonConstructionMode::Constructor over "
    "Factory, bypassing registerInstance() and spawning a duplicate QML-"
    "side instance. See d7f932f.");
```

This converts the comment-only contract into a build break, which is what
"do not reintroduce this default" means in practice.

### WR-03: Three other QML_SINGLETON sites stay safe by accident, not by construction

**Files:** `src/app/src/theme_service.hpp`, `src/app/src/tray_controller.hpp`,
`src/app/src/device_model.hpp` (all NOT in the modified set)
**Issue:** `grep -rn "QML_SINGLETON" src/` returns nine hits; the sweep
covered six. The commit body for `e221b21` claims the remaining three
(`DeviceModel`, `ThemeService`, `TrayController`) are "already safe — their
ctors require a non-default reference / pointer." That claim is correct as
of HEAD, but:

- `TrayController(BrandingService* branding, ProfileController* profiles = nullptr, QObject* parent = nullptr)`
  has *one* required positional argument (`branding`). Lose that — by
  making it optional with a "fall back to a default null branding"
  convenience — and the class is default-constructible, the dual-instance
  bug returns silently, and the sweep's audit pattern (look for
  `= nullptr` on `parent`) misses it because the trigger is on a
  *different* parameter.
- `DeviceModel(core::DeviceRegistry& registry, QObject* parent = nullptr)`
  has the same shape, with the additional twist that the ref-parameter
  pattern is unusual in the codebase — a future "switch to pointer for
  consistency" refactor would silently re-arm the trap.
- `ThemeService(BrandingService* branding, QObject* parent = nullptr)`
  is the *only one of the three* that carries a comment hinting why the
  `branding` parameter has no default. (And even that comment doesn't
  name the dual-instance trap explicitly.)

Net effect: the sweep is "complete" only by the convention it chose to
audit (`= nullptr` on `parent`). Three sibling singletons survive on a
load-bearing accident with no in-source enforcement.

**Fix:** Apply the WR-02 `static_assert` at every `QML_SINGLETON` site —
including `ThemeService`, `TrayController`, `DeviceModel` — so the
"already safe" claim is checked by the compiler instead of by a future
auditor's grep pass. (Optionally also add a one-line "see BrandingService"
comment to `tray_controller.hpp` and `device_model.hpp` so a casual reader
knows why the existing constructors look the way they do.)

## Info

### IN-01: SUMMARY claims tests for `ThemeService` were updated for "corrected lifecycle"; they were not

**File:** `.planning/phases/02-qml-singleton-sweep/SUMMARY.md` (line 48,
"Tests for `BrandingService` and `ThemeService` updated to assert the
corrected lifecycle.")
**Issue:** `git show d7f932f -- tests/unit/test_theme_service.cpp` shows
the file was touched, but the diff is exclusively
`BrandingService branding;` → `BrandingService branding(nullptr);` — a
mechanical adaptation forced by the `BrandingService` ctor change, not an
addition of any "lifecycle" assertion. The summary overstates what the
test edits cover, which dovetails with WR-01 (the tests don't actually
exercise the dual-instance class). Worth correcting in the retro write-up
so the next reader doesn't conclude that the bug class is now under test.
**Fix:** Reword the SUMMARY bullet to "Tests adapted to the new ctor
signature (no behavioural assertion added)" — or, better, land WR-01 and
keep the original wording.

### IN-02: Five "see BrandingService" comments are byte-identical; one is paraphrased

**Files:** `autostart_service.hpp:43-45`, `loaded_plugins_model.hpp:93-95`,
`plugin_catalog_model.hpp:145-147`, `profile_controller.hpp:48-50`,
`property_inspector_controller.hpp:124-126` (all identical), vs
`branding_service.hpp:58-68` (the long-form rationale being cross-referenced).
**Issue:** Cosmetic. Five of the six headers carry a copy-pasted three-line
comment that ends "spawning a duplicate QML-side instance." The one
authoritative source it points at — the `branding_service.hpp` `@note` — uses
slightly different wording ("read its (stale, dark-themed) palette while
ThemeService updates the registered instance"). The two diverge on detail
level, which is fine, but the cross-reference target is a member-function
docstring rather than a class-level explainer, so a casual `grep` for the
rationale lands in a different prose register. Low priority.
**Fix:** None required. If touched, consider hoisting the long-form
explanation into a single `docs/architecture/QML-SINGLETON.md` and pointing
all six (or nine — see WR-03) headers at it.

### IN-03: `loaded_plugins_model.hpp` comment placement diverges from the other four sweep sites

**File:** `src/app/src/loaded_plugins_model.hpp:93-96`
**Issue:** Cosmetic. In `autostart_service.hpp`, `plugin_catalog_model.hpp`,
`profile_controller.hpp`, `property_inspector_controller.hpp` the "No
default on `parent`" comment sits immediately above the `explicit T(QObject* parent);` line in the public block, with one blank line of separation from
the preceding declaration. In `loaded_plugins_model.hpp` the same comment
sits between the `Roles` enum's closing `};` and `explicit LoadedPluginsModel(QObject* parent);`, with no separating blank line. Doesn't
affect compilation or readability materially, but it breaks the
"each header has the comment in the same visual position" pattern that
makes the sweep easy to audit at a glance.
**Fix:** None required. If touched, add one blank line above the comment
block in `loaded_plugins_model.hpp:93` for parity with the other four.

### IN-04: `branding_service.hpp` doc text refers to itself as "ThemeService" inadvertently

**File:** `src/app/src/branding_service.hpp:64-66`
**Issue:** Cosmetic. The `@note` reads "QML would silently spawn a *second*
BrandingService and read its (stale, dark-themed) palette while
ThemeService updates the registered instance." That is correct prose for
the originally-observed symptom (the user clicks Light → dark stays
because ThemeService called `loadThemeFile` on the C++ side instance and
QML rendered from the QML-side duplicate). But the same trap applies to
any other writer of the C++ side, so the comment over-couples the rationale
to the specific Light/Dark UX. A future reader who's looking at e.g.
`AutostartService` and follows the cross-reference here may wonder how
`ThemeService` is relevant.
**Fix:** None required. If touched, drop "while ThemeService updates the
registered instance" or generalise to "while C++ code paths update the
registered instance."

______________________________________________________________________

_Reviewed: 2026-05-12_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
