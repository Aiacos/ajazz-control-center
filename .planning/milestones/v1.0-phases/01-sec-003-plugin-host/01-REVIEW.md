---
phase: 01-sec-003-plugin-host
reviewed: 2026-05-12T00:00:00Z
depth: deep
files_reviewed: 11
files_reviewed_list:
  - python/ajazz_plugins/_host_child.py
  - src/app/CMakeLists.txt
  - src/app/qml/LoadedPluginsPage.qml
  - src/app/src/application.cpp
  - src/app/src/application.hpp
  - src/app/src/loaded_plugins_model.cpp
  - src/app/src/loaded_plugins_model.hpp
  - src/plugins/src/manifest_signer.cpp
  - src/plugins/src/manifest_signer_win32.cpp
  - src/plugins/src/out_of_process_plugin_host_win32.cpp
  - tests/unit/test_manifest_signer.cpp
findings:
  critical: 1
  warning: 5
  info: 4
  total: 10
status: issues_found
---

# Phase 1: Code Review Report — SEC-003 Plugin Host

**Reviewed:** 2026-05-12
**Depth:** deep
**Files Reviewed:** 11
**Status:** issues_found

## Summary

The wiring is sound on the POSIX path: the OOP host spawns cleanly, manifest verification fails closed on every error condition I traced (verifier missing, manifest missing, exec failure, malformed JSON), trust-roots fall through to "self-signed" rather than "trusted", and the `TrustLevelOf` collapse in `LoadedPluginsModel` correctly treats the empty-publisher edge case as self-signed. The "two pre-existing robustness gaps" closed in `f044660` actually fix what the commit message claims.

The Win32 OOP host path is more concerning. The biggest single defect is **mutating the parent process's environment via `_putenv_s` to feed PYTHONPATH/PYTHONDONTWRITEBYTECODE/PYTHONUNBUFFERED to the child** (the POSIX side correctly does this in the post-fork child branch). This is genuinely surprising behaviour — the host appears to read like a sandboxed spawn but in fact pollutes its own process env on every construction, which leaks across instances and any other parent-side code that reads PYTHONPATH (e.g. the manifest verifier subprocess started later).

Secondary concerns: the f044660 trust-roots fix bounds the parse window by `}` correctly for the malformed case in the test, but the underlying string-grep approach still mishandles a JSON object where `"name"` appears **before** `"key"` (legal JSON, no key order guarantee) — that publisher silently demotes to "self-signed". The Win32 `childPid()` accessor truncates a HANDLE to `int`, returning a value that is neither a real PID nor a usable HANDLE on 64-bit Windows.

The `_host_child.py` `_crash_for_test` reachability of the `ctypes.string_at(0)` belt-and-braces fallback is a real concern only on the Win32 path (no SIGSEGV semantics): on Windows, `os.kill(getpid(), SIGSEGV)` is documented to raise `ValueError` rather than terminating, so the fallback null-deref must run — which it does, but only because the SIGSEGV call's exception isn't caught and Python may halt before the fallback. Worth a comment.

The `ci.yml` and `TODO.md` touches were not deep-reviewed (out of scope per the prompt); both look unobjectionable on a glance.

## Critical Issues

### CR-01: Win32 OOP host mutates parent environment (PYTHONPATH leak across host instances and into sibling processes)

**File:** `src/plugins/src/out_of_process_plugin_host_win32.cpp:463-467`
**Issue:** The Win32 constructor calls `_putenv_s("PYTHONPATH", ...)`, `_putenv_s("PYTHONDONTWRITEBYTECODE", "1")`, and `_putenv_s("PYTHONUNBUFFERED", "1")` **in the parent process** before `CreateProcessW`. There is no `fork()` on Windows; these calls write to the parent's CRT environment block. `CreateProcessW` is then called with `lpEnvironment = nullptr`, which Win32 documents as "use the parent's environment block." Net effect:

1. **Cross-instance pollution.** Constructing a second `OutOfProcessPluginHost` later (or any future feature that does so) will inherit the previous instance's PYTHONPATH stacked into the parent's env, with no way to scope or revert.
1. **Cross-subprocess pollution.** The manifest verifier (`_wspawnvp(_P_WAIT, "python3", ...)` in `manifest_signer_win32.cpp`) inherits the parent's env too. If the verifier's Python ends up importing from a path under `AJAZZ_PLUGIN_PYTHONPATH` (the SDK package directory), the verifier's behaviour becomes coupled to the host's PYTHONPATH choice — invisible coupling that won't surface until someone moves the SDK or a plugin adds a same-named module.
1. **The POSIX sibling does NOT have this defect.** `out_of_process_plugin_host.cpp` (lines 271-286) makes the equivalent `setenv` calls **inside the child branch after `fork()`**, so the parent's env stays clean.

This is a fail-open in the sense that the defect makes the Win32 host's spawn hygiene worse than it appears in code review, and worse than the POSIX backend it claims to mirror "method-for-method".

**Fix:** Build an environment block locally and pass it as `lpEnvironment` to `CreateProcessW` instead of mutating the parent. The block format is `KEY=VALUE\0KEY2=VALUE2\0\0` (UTF-16, terminated by a double-null). Snapshot the parent's env via `GetEnvironmentStringsW`, append/override the three Python vars, free the snapshot via `FreeEnvironmentStringsW`, and pass `CREATE_UNICODE_ENVIRONMENT` in `creationFlags`. Alternative if a full env-block builder is too much for this slice: snapshot the three current values via `_wgetenv_s` before the `_putenv_s` calls and restore them after `CreateProcessW` returns (errors and exceptions included — RAII the restoration). Either fix removes the parent-env leak; the env-block approach is the one the POSIX `setenv-in-child` model maps to most directly.

## Warnings

### WR-01: `loadTrustRoots` silently demotes a publisher when JSON object key order is `name` before `key`

**File:** `src/plugins/src/manifest_signer.cpp:118-143`, mirrored at `src/plugins/src/manifest_signer_win32.cpp:128-149`
**Issue:** The walk does `findStringField(remaining, "key")`, then locates `"key"` at `keyPos`, then defines a window from `keyPos` to the next `}` (or 512 bytes), then searches **inside that window** for `"name"`. JSON object members have no guaranteed order — `{"name":"Aiacos","key":"…"}` is a perfectly legal entry and the bundled file's schema doesn't constrain it. Such an entry yields `name == ""` (because `"name"` is before `"key"` and thus outside the window) and the publisher silently demotes to self-signed for every plugin signed with that key. No warning, no log.

The new Catch2 case `loadTrustRoots: malformed entry never cross-pairs` only pins the cross-pair fix; it does not exercise reverse field order.

**Fix:** The mini-grep approach has aged out of usefulness. Either widen the window to the whole entry by also searching backwards for the matching `{` (cheap), or — preferable — bite the bullet and write a five-state JSON object scanner that walks `publishers[]`, locates each `{...}` body, then runs `findStringField` for both keys against the full body. Add a Catch2 case with `{"name":"X","key":"K"}` ordering.

### WR-02: Win32 `childPid()` returns a truncated HANDLE, not a valid identifier

**File:** `src/plugins/src/out_of_process_plugin_host_win32.cpp:694-705`
**Issue:** The function returns `static_cast<int>(reinterpret_cast<intptr_t>(reinterpret_cast<HANDLE>(m_impl->processHandle)))` — the HANDLE is `void*` (64-bit on x64), `intptr_t` preserves it, but the final `static_cast<int>` truncates the upper 32 bits. The returned value is not a real OS pid, not a usable HANDLE, and not stable across construction (the high bits change). The doc comment acknowledges "callers should treat the value as opaque" but the `int` return type makes that an unenforceable convention — log statements and tests treating it as a pid will silently print misleading values.

The POSIX sibling uses `pid_t` (which fits in `int`) and the value is the real pid, so the contract is asymmetric in addition to lossy.

**Fix:** Return `intptr_t` instead of `int` (or expose it as `qint64` if the IPluginHost interface change is too invasive — though the interface owns the signature and a one-method change is cheap). Alternatively, on Win32 retrieve the real pid via `GetProcessId(handle)` and return that as `int`; pids on Win32 fit comfortably in 32 bits.

### WR-03: `LoadedPluginsModel::setPlugins` always emits `countChanged`, but `Application::initPluginHost` calls `setPlugins` BEFORE `setPluginHost` — leaving a window where QML's "Reload" button (when added) would no-op against the freshly-populated rows

**File:** `src/app/src/application.cpp:117-119`
**Issue:** Order of operations is:

```
m_loadedPlugins->setPlugins(host->plugins());   // QML signal fires; m_host still null
m_loadedPlugins->setPluginHost(host.get());      // host wired
m_pluginHost = std::move(host);                  // ownership transferred
```

If the QML engine were to bind a "Reload" affordance (the model already exposes `Q_INVOKABLE refresh`) and the user clicked it during a narrow startup window between `setPlugins` and `setPluginHost`, `refresh()` would silently no-op against `m_host == nullptr`. Today this is unreachable (the QML engine isn't loaded until `exposeToQml` runs after `bootstrap`), but the ordering encodes a fragile assumption that future bootstrap reorganisation can violate.

Compounding: `loadAll()` is called for its side effect (the host populates its inventory) but its return value is discarded — the count is recovered indirectly via `host->plugins()`. This means the host hits the IPC channel twice in a row (one for `load_all`, then one for `list_plugins`) at startup. The first call already returned a count that's just thrown away.

**Fix:** Wire `setPluginHost` first, then `setPlugins`, so by the time any QML can possibly observe the model, both fields are populated together. Independently, consider `loadAll` returning the count and storing it locally, then deciding whether `plugins()` is even worth calling — if the app commonly has zero loaded plugins, the second IPC roundtrip can be skipped.

### WR-04: `_host_child.py` `_crash_for_test` may not reach the `ctypes.string_at(0)` fallback on Windows

**File:** `python/ajazz_plugins/_host_child.py:342-355`
**Issue:** The function calls `os.kill(os.getpid(), signal.SIGSEGV)`, which on POSIX delivers SIGSEGV and terminates. On Windows, `signal.SIGSEGV` exists as a constant but `os.kill(pid, SIGSEGV)` is documented to raise `OSError` (or in practice `SystemError` / `ValueError` depending on Python version) because Win32 only honours `SIGTERM` / `CTRL_BREAK_EVENT` / `CTRL_C_EVENT` for cross-process signalling; sending arbitrary signals to your own pid is not implemented. The exception is **uncaught** in this function, so Python's default handler will print a traceback to stderr and exit with code 1 — never reaching the `ctypes.string_at(0)` fallback that's documented as the belt-and-braces deterministic-crash path. Net effect on Windows: the test sees a non-zero exit code (good) but not the SIGSEGV exit-status semantics it expects (bad — the test in `test_out_of_process_plugin_host.cpp` may assert on the wait status flags).

This isn't a production-code issue (the function is `_crash_for_test`-only) but the comment claims "Belt and braces" and the belt is broken on Windows.

**Fix:** Wrap the `os.kill` call in `try: ... except Exception: pass` so the `ctypes.string_at(0)` fallback is reached on platforms where `os.kill(SIGSEGV)` is a no-op or raises. While there, consider documenting which exit code/wait-status the test code on each platform should assert against.

### WR-05: `_open_osfhandle` failure path leaves dead-store flag and an unused-after-close pattern

**File:** `src/plugins/src/out_of_process_plugin_host_win32.cpp:594-613`
**Issue:** `bool writeHandleOwned = (writeFd >= 0);` is mutated to `false` after `_close(writeFd)` (line 601) but never read again. More importantly, the cleanup branch on line 600 calls `_close(writeFd)` even when only `readFd < 0` — that's fine (closing a valid fd is a no-op pattern when we're going to throw), but the flag mechanism is over-engineered for what amounts to "close whichever handles still need closing in the right way." There's no actual leak, but the construction is fragile: a future edit that adds a third handle would have to mirror the same flag-and-branch pattern correctly, and the existing dead-store demonstrates the pattern is already error-prone.

Separately, on the failure path the code calls `TerminateProcess(pi.hProcess, 1)` then `CloseHandle(pi.hThread)` — `pi.hThread` here is the handle to the child's main thread, which holds the thread alive separately from the process. This is correct, but the parallel happy path on line 618 also closes `pi.hThread` immediately — both branches converge on releasing the thread handle, which is right but worth a one-liner comment so the symmetry isn't accidental.

**Fix:** Replace the two flag variables with a small RAII helper:

```cpp
struct ScopedHandle { HANDLE h; ~ScopedHandle(){ if (h) CloseHandle(h); } };
```

or declare `writeHandleOwned` `const` and let the branch fall through naturally. Drop the dead store. Add a one-line comment on line 612 that `CloseHandle(pi.hThread)` here mirrors the happy-path close on line 618 — both release the same handle for the same reason.

## Info

### IN-01: `application.cpp` discards `loadAll()` return value but still emits "{} loaded" log line via `rowCountSimple()`

**File:** `src/app/src/application.cpp:115`, `src/app/src/application.cpp:121-124`
**Issue:** `host->loadAll()` returns the load count; the value is discarded. The log line uses `m_loadedPlugins->rowCountSimple()` which is the post-`setPlugins` row count — equivalent in steady state but indirect. Also see WR-03 for the redundant IPC roundtrip implied by this pattern.

**Fix:** Capture the loadAll return into a local and log it directly; drop the indirect read through the model. (Or accept the redundancy, but document why.)

### IN-02: `manifest_signer.cpp` `runChild` uses inherited stdin/stdout (not stderr-only) for the verifier

**File:** `src/plugins/src/manifest_signer.cpp:59-89`
**Issue:** The doc comment on `runChild` says "Inherits stdin/stdout from the parent so the verifier's `::error::` annotations show up in the host's logs." The verifier's annotations actually go to **stderr** (per `print(..., file=sys.stderr)` in `sign-plugin-manifest.py`), not stdout. The comment is misleading; the inheritance is correct (we want stderr to surface) but the rationale is wrong. Same comment lives in the Win32 sibling.

A more substantive concern: by inheriting the parent's stdout, the verifier's `print(f"OK {manifest_path}")` success line lands on the host's stdout — which on a GUI build is generally invisible, but on the CLI dev path becomes noise on every plugin verified at startup. Consider redirecting the verifier child's stdout to `/dev/null` (or the equivalent `NUL` on Windows) and only inheriting stderr.

**Fix:** Update the comment to say "stderr" and decide whether to mute stdout. The latter is a small UX improvement, not a correctness issue.

### IN-03: `LoadedPluginsModel::trustLevelOf` documented as "fail closed" for empty publisher with `signed_==true`, but the comment can be misread

**File:** `src/app/src/loaded_plugins_model.cpp:131-146`
**Issue:** The comment says "an empty publisher with signed\_==true would mean the wiring is broken — fail closed (treat as self-signed) rather than mislabel." This is correct behaviour, but "fail closed" usually means "deny" in security contexts. Here it's "demote to less-trusted" which IS the safer choice but isn't denial. A reader scanning quickly might think a wiring break would block the plugin, when in reality the plugin still loads — only the chip changes colour. The wire contract from the OOP host (lines 424-426 in `out_of_process_plugin_host.cpp`) actually populates `publisher = "self-signed"` explicitly when the verifier returned `valid==true && publisherName.empty()`, so the empty-string branch in `trustLevelOf` is genuinely defensive (and dead in practice).

**Fix:** Rephrase as "fail safe (treat as self-signed)" rather than "fail closed." Note in the comment that the OOP host already writes `"self-signed"` explicitly so the empty-string branch is belt-and-braces.

### IN-04: QML `LoadedPluginsPage.qml` chip colours hardcoded as hex literals; theme file already exposes a Material accent palette

**File:** `src/app/qml/LoadedPluginsPage.qml:160-176`
**Issue:** The amber-800/red-800/red-500/red-200/etc. Tailwind-style hex literals are inlined per row. The codebase already has a `Theme` singleton (used elsewhere in this file via `Theme.spacingMd`, `Theme.fgPrimary`) — consider extending it with `Theme.warningBg` / `Theme.errorBg` etc. so a future light-theme pass doesn't have to grep through delegates.

The rendered colours look correct for the dark theme. The `LoadedPluginsModel` was one of the singletons swept in `e221b21`, so the QML side correctly resolves to the singleton instance now (no dual-instance risk on this page).

**Fix:** Move the four hex pairs to `Theme.qml` constants. Out of scope for this phase, but worth noting before someone adds a light-theme pass.

______________________________________________________________________

_Reviewed: 2026-05-12_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: deep_
