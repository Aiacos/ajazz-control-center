# Security audit — ajazz-control-center

> **Status as of 2026-04**: this is a point-in-time snapshot. Many findings
> below — particularly all SEC-\* items in the *Plugin loading* category and
> the embedded-Python execution boundary — have been mitigated by the A4
> out-of-process plugin host refactor (slices 1–3e, see `TODO.md` →
> *Architecture refactors*) and the S2 / S3 / S4 / S6 hardening pass. The
> in-process pybind11 backend referenced throughout this document
> (`src/plugins/src/plugin_host.cpp`) has been deleted; the active backend
> is `OutOfProcessPluginHost` with per-OS sandboxing (bwrap on Linux,
> sandbox-exec on macOS, AppContainer port pending). Treat the line-number
> citations as accurate-at-audit-date; resolve current status via
> `TODO.md` rather than re-grepping the live tree.

Scope: repository snapshot at `/home/user/workspace/ajazz-control-center`, focusing on host/device trust boundaries, parser hardening, plugin execution, release engineering, and dependency/CI posture.

## 1. Threat model

### Assets

- HID devices and their configuration channels: keymaps, macros, firmware mode transitions, RGB, mouse DPI/button state (`src/core/include/ajazz/core/capabilities.hpp:376`, `src/core/include/ajazz/core/capabilities.hpp:503`).
- Profile data and action settings JSON flowing from disk/UI into device writes and plugin dispatch (`src/core/include/ajazz/core/profile.hpp:114`, `python/ajazz_plugins/__init__.py:120`).
- Embedded Python plugin code executing inside the host process with access to local filesystem, user network stack, and process memory (`docs/architecture/PLUGIN-SYSTEM.md:97`, `src/plugins/src/plugin_host.cpp:81`).
- Release artifacts and CI pipeline outputs that users install directly from nightly/stable releases (`README.md:19`, `.github/workflows/nightly.yml:149`, `.github/workflows/release.yml:128`).
- Linux udev access rules and platform packaging permissions that control who can talk to HID nodes (`resources/linux/99-ajazz.rules:3`, `packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml:7`).

### Threats

- Malicious or malformed USB devices feeding unexpected hotplug identifiers or protocol frames to parsers.
- Malicious profiles or oversized asset payloads inducing excessive I/O, integer truncation, or unsafe parser behavior.
- Plugin escape is effectively “plugin compromise of the host,” because plugins already run unsandboxed in-process.
- Supply-chain compromise through unsigned releases, tag-pinned external sources, or mutable third-party GitHub Actions.
- Local privilege or lateral-access expansion through broad desktop packaging permissions (notably Flatpak `--device=all`) and unsandboxed plugin code.

### Trust boundaries

- USB/udev/IOKit/Windows message sources are untrusted external inputs crossing into `HotplugMonitor` (`src/core/src/hotplug_monitor.cpp:108`, `src/core/src/hotplug_monitor.cpp:153`, `src/core/src/hotplug_monitor.cpp:219`).
- HID report bytes from devices are untrusted and cross into parser functions and feature/outbound builders (`src/devices/streamdeck/src/akp05.cpp:171`, `src/devices/mouse/src/aj_series.cpp:170`).
- Plugin directories are untrusted local content crossing into `sys.path`, module import, and Python object execution (`src/plugins/src/plugin_host.cpp:68`, `src/plugins/src/plugin_host.cpp:81`).
- CI/CD artifacts move from GitHub Actions runners to end-user machines without signing or provenance enforcement (`docs/wiki/Release-Process.md:76`, `.github/workflows/release.yml:138`).

## 2. Input validation

- Linux hotplug parsing is the clearest parser bug: `std::stoul` consumes untrusted `idVendor`/`idProduct` sysattrs without validation or try/catch (`src/core/src/hotplug_monitor.cpp:109`, `src/core/src/hotplug_monitor.cpp:115`).
- Windows hotplug parsing uses `wcstoul` and silently truncates into `uint16_t`; malformed long values are not range-checked (`src/core/src/hotplug_monitor.cpp:148`).
- macOS hotplug casts CoreFoundation values to `CFNumberRef` without verifying type IDs before `CFNumberGetValue()` (`src/core/src/hotplug_monitor.cpp:231`, `src/core/src/hotplug_monitor.cpp:236`).
- Streamdeck parsers correctly reject short frames and many malformed tags with `std::optional` (`src/devices/streamdeck/src/akp03.cpp:132`, `src/devices/streamdeck/src/akp05.cpp:172`, `src/devices/streamdeck/src/akp153.cpp:173`).
- Some parser validation remains incomplete: AKP03 accepts any low-nibble encoder index (`src/devices/streamdeck/src/akp03.cpp:159`), and AKP05 accepts any 16-bit touch X coordinate although docs say 0..639 (`src/devices/streamdeck/src/akp05.cpp:217`).
- Profile JSON deserialization is not implemented in core and app load/save are stubs, so there is currently no hardened canonical loader, schema validation, or path policy (`src/core/src/profile.cpp:161`, `src/app/src/profile_controller.cpp:17`).
- Plugin settings JSON falls back to `{}` on malformed input, which is safe from crashes but hides tampering/serialization bugs instead of surfacing validation failures (`python/ajazz_plugins/__init__.py:139`).
- The plugin guide advertises an optional `manifest.yml`, but the host does not parse or validate manifests at all; only `plugin.py` existence is checked (`docs/guides/PLUGIN_DEVELOPMENT.md:11`, `src/plugins/src/plugin_host.cpp:74`).

## 3. Plugin sandbox

- The repository explicitly states there is no plugin sandbox and plugins run with the host’s permissions (`docs/architecture/PLUGIN-SYSTEM.md:95`, `docs/architecture/PLUGIN-SYSTEM.md:97`).
- `PluginHost` appends the current working directory to `sys.path` and then appends each configured search path, increasing import-surface ambiguity and making path shadowing easier (`src/plugins/src/plugin_host.cpp:45`, `src/plugins/src/plugin_host.cpp:48`, `src/plugins/src/plugin_host.cpp:68`).
- `loadAll()` imports any directory with a `plugin.py` file and executes its module/class constructor with no signature verification, manifest allowlist, dependency sandbox, or network/filesystem restriction (`src/plugins/src/plugin_host.cpp:70`, `src/plugins/src/plugin_host.cpp:81`, `src/plugins/src/plugin_host.cpp:83`).
- The docs mention a future permission manifest and a UI warning chip for unsigned plugins, but current code does not implement signature checks, manifests, or a warning UI path (`docs/architecture/PLUGIN-SYSTEM.md:99`, `docs/architecture/PLUGIN-SYSTEM.md:104`, `src/app/qml/Main.qml:36`).

### Recommended sandbox model

1. Move each plugin into a dedicated subprocess with an IPC boundary (local socket/QLocalSocket/stdio JSON-RPC).
1. Define a signed manifest with explicit capabilities: filesystem roots, network egress, notifications, device access, subprocess execution.
1. On Linux, combine subprocess execution with seccomp-bpf, mount namespaces, and a private plugin data directory; on macOS and Windows, restrict via app-container-style policies as far as platform support allows.
1. Sign plugin bundles with Ed25519 and verify before install/load; keep unsigned sideloading behind an explicit developer toggle.
1. Enforce execution timeouts, memory ceilings, and cancellation for action dispatch.

## 4. Privilege model

- Linux udev rules correctly target root-less operation via `TAG+="uaccess"`, avoiding `plugdev` or root requirements (`resources/linux/99-ajazz.rules:3`, `resources/linux/99-ajazz.rules:26`).
- README likewise documents root-less Linux usage and says Windows needs no drivers (`README.md:118`, `README.md:160`).
- Flatpak packaging is much broader: it requests `--device=all`, giving the app access to all host devices rather than only HID-relevant nodes (`packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml:12`, `packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml:14`).
- Unsandboxed Python plugins inherit the same desktop privileges as the app and therefore also inherit the broad Flatpak device grant when packaged that way (`docs/architecture/PLUGIN-SYSTEM.md:97`).

## 5. Memory safety

- No obvious classic out-of-bounds read appears in streamdeck parser code because frame sizes are checked first and field access stays within fixed report sizes (`src/devices/streamdeck/src/akp03.cpp:132`, `src/devices/streamdeck/src/akp05.cpp:172`).
- There are still memory-safety-adjacent issues: unchecked reinterpret/casts in hotplug code (`src/core/src/hotplug_monitor.cpp:155`, `src/core/src/hotplug_monitor.cpp:214`), unchecked CF type assumptions (`src/core/src/hotplug_monitor.cpp:232`), and narrowing conversions from larger integers into 16-bit protocol fields (`src/core/src/hotplug_monitor.cpp:239`, `src/devices/keyboard/src/proprietary_keyboard.cpp:308`).
- Outbound image and macro sends risk protocol desynchronization because lengths are truncated to 16 bits while the full payload is sent (`src/devices/streamdeck/src/akp03.cpp:375`, `src/devices/streamdeck/src/akp05.cpp:390`, `src/devices/streamdeck/src/akp153.cpp:355`).
- Mouse envelopes set the payload length byte to `payload.size()` even when copying only up to 59 bytes into the report body (`src/devices/mouse/src/aj_series.cpp:67`, `src/devices/mouse/src/aj_series.cpp:68`), which can make firmware trust an incorrect size field.
- `ActionContext.settings` accepts any JSON type; if a profile passes a JSON list or scalar, Python handlers expecting a dict may misroute it because no type validation is enforced after `json.loads()` (`python/ajazz_plugins/__init__.py:139`, `python/ajazz_plugins/__init__.py:144`).

## 6. Cryptography & integrity

- Stable releases for macOS and Windows are explicitly unsigned today (`docs/wiki/Release-Process.md:76`, `docs/wiki/Release-Process.md:78`).
- Nightly releases are also explicitly “not signed for production,” and no checksum/hash manifest is attached (`.github/workflows/nightly.yml:149`, `.github/workflows/nightly.yml:160`).
- There is no release-signing step, checksum generation, provenance/attestation, or SBOM generation in the release workflows (`.github/workflows/release.yml:128`, `.github/workflows/release.yml:138`).
- No plaintext credentials were found in the code search for common token/private-key patterns; only documentation/examples mention tokens generically.
- Plugin/package integrity is not protected by signatures or hashes (`docs/architecture/PLUGIN-SYSTEM.md:104`, `src/plugins/src/plugin_host.cpp:81`).

## 7. CI/CD security

- Core CI uses least-privilege `contents: read`, which is a good default (`.github/workflows/ci.yml:17`).
- Lint workflow elevates to `contents: write` and `pull-requests: write` so it can auto-commit fixes back to same-repo PRs; that increases blast radius if the job environment is compromised (`.github/workflows/lint.yml:30`, `.github/workflows/lint.yml:77`).
- Release and nightly workflows both request `contents: write`, which is necessary for releases/tags, but there are no environment protections, checksum verification, or provenance steps (`.github/workflows/release.yml:12`, `.github/workflows/nightly.yml:22`).
- Nightly forcibly rewrites the `nightly` tag (`git push origin -f nightly`), which is convenient but weakens artifact immutability and complicates downstream hash pinning (`.github/workflows/nightly.yml:146`, `.github/workflows/nightly.yml:147`).
- Third-party GitHub Actions are version-tag pinned (`@v4`, `@v5`, `@v6`, `@v2`) rather than commit-SHA pinned, leaving room for upstream tag retargeting risk (`.github/workflows/release.yml:32`, `.github/workflows/lint.yml:82`, `.github/workflows/wiki.yml:40`).
- Dependabot covers GitHub Actions and pip, but not CMake/FetchContent, Flatpak git sources, or Homebrew formulas (`.github/dependabot.yml:3`, `.github/dependabot.yml:8`).
- No secret scanning, CodeQL, dependency-review, or SARIF upload workflow is present.

## 8. Telemetry

- Repository search found no network client code in `src/` or `python/` for telemetry-style uploads, and the branding docs explicitly state there is no telemetry (`docs/architecture/BRANDING.md:88`).
- The current product therefore appears to collect no telemetry in application code, though plugins may of course perform arbitrary network I/O because they are unsandboxed (`docs/architecture/PLUGIN-SYSTEM.md:97`).

## 9. Dependency audit

- `hidapi` is vendored at `hidapi-0.14.0` via FetchContent and Flatpak git source (`CMakeLists.txt:56`, `packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml:19`). No directly applicable public CVE was confirmed for this exact version from authoritative results gathered here, but tag-only pinning still weakens supply-chain immutability.
- `pybind11` is used as `2.13.6` (`CMakeLists.txt:45`, `packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml:30`). Search results here did not show a direct upstream pybind11 package CVE for 2.13.6; maintain normal upgrade cadence and add dependency scanning.
- Qt 6.7.3 is installed in CI/release workflows (`.github/workflows/ci.yml:70`, `.github/workflows/release.yml:33`). Public Qt advisories show that 6.7.3 is affected by CVE-2025-30348 in Qt XML and by QtSvg issues CVE-2025-10728/CVE-2025-10729, while the repo links QtSvg and uses QML/Quick components (`CMakeLists.txt:41`, [Qt security advisory](https://www.qt.io/blog/security-advisory-dos-qt-xml-qdom), [Qt Wiki vulnerability list](https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products)).
- Qt 6.7.x before 6.7.4 was also affected by CVE-2024-39936 in HTTP/2/QtNetwork, though this application does not currently appear to use QtNetwork directly ([Qt Wiki vulnerability list](https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products)).
- Catch2 is fetched at `v3.7.1` for tests (`tests/CMakeLists.txt:2`), again by mutable tag rather than immutable revision.

## 10. Findings table

`id | category | severity (Critical/High/Medium/Low/Info) | file:line | description | suggested_fix | effort (S/M/L) | cve_or_cwe`

| id      | category                      | severity | file:line                                                    | description                                                                                                                                                                                   | suggested_fix                                                                                                                          | effort | cve_or_cwe                                       |
| ------- | ----------------------------- | -------- | ------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | ------ | ------------------------------------------------ |
| SEC-001 | Input validation              | High     | src/core/src/hotplug_monitor.cpp:115                         | Linux hotplug uses `std::stoul` on untrusted udev sysattrs without error handling, so malformed attributes can terminate the monitor thread and leak the current `udev_device*`.              | Use `std::from_chars` with explicit hex/range checks, catch parse errors inside the loop, and RAII-wrap udev objects.                  | S      | CWE-20                                           |
| SEC-002 | Concurrency / memory safety   | High     | src/core/src/hotplug_monitor.cpp:314                         | `setCallback()` races with worker-thread reads of `p_->cb`, creating undefined behavior in a security-sensitive event ingestion path.                                                         | Protect callback state with a mutex or atomic indirection and copy before invocation.                                                  | M      | CWE-362                                          |
| SEC-003 | Plugin sandbox                | Critical | docs/architecture/PLUGIN-SYSTEM.md:97                        | Plugins run in-process with host permissions and no sandbox, so any plugin can execute arbitrary code, access files, and exfiltrate data.                                                     | Move plugins to sandboxed subprocesses with signed manifests and capability gating before allowing third-party ecosystem growth.       | L      | CWE-94                                           |
| SEC-004 | Plugin loading                | High     | src/plugins/src/plugin_host.cpp:48                           | The host appends `.` to `sys.path`, enabling import shadowing from the process working directory.                                                                                             | Remove `.` from `sys.path`, import the embedded module directly, and use explicit per-plugin import roots.                             | S      | CWE-427                                          |
| SEC-005 | Plugin loading                | High     | src/plugins/src/plugin_host.cpp:81                           | Any directory containing `plugin.py` is imported and instantiated with no manifest validation, signature verification, or provenance check.                                                   | Require signed plugin bundles/manifests, validate metadata before load, and quarantine unknown/untrusted plugins.                      | M      | CWE-829                                          |
| SEC-006 | Privilege model               | Medium   | packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml:14 | Flatpak grants `--device=all`, exposing all host devices to the app and its plugins.                                                                                                          | Narrow device access to the minimum viable permission or move HID access through a broker/portal-style helper.                         | M      | CWE-250                                          |
| SEC-007 | Memory safety                 | High     | src/devices/mouse/src/aj_series.cpp:67                       | Mouse feature reports encode `payload.size()` in the length byte even when only 59 bytes are copied into the packet body.                                                                     | Clamp the advertised length to the copied byte count and reject oversize payloads before envelope construction.                        | S      | CWE-131                                          |
| SEC-008 | Memory safety                 | High     | src/devices/streamdeck/src/akp153.cpp:355                    | Image headers truncate payload length to 16 bits but the send loop writes the full payload, allowing protocol desynchronization on oversized images; the same pattern appears in AKP03/AKP05. | Reject payloads larger than protocol max or segment them with matching explicit chunk metadata.                                        | M      | CWE-190                                          |
| SEC-009 | Input validation              | Medium   | src/devices/streamdeck/src/akp05.cpp:220                     | AKP05 touch-strip parser accepts any 16-bit coordinate even though the documented range is 0..639.                                                                                            | Range-check coordinates and reject impossible values before emitting events.                                                           | S      | CWE-20                                           |
| SEC-010 | Input validation              | Medium   | src/devices/streamdeck/src/akp03.cpp:159                     | AKP03 encoder parser accepts 16 possible encoder IDs on hardware that exposes one encoder.                                                                                                    | Reject `encIndex >= EncoderCount` and add malformed-frame tests.                                                                       | S      | CWE-20                                           |
| SEC-011 | Profile / settings validation | Medium   | python/ajazz_plugins/__init__.py:140                         | Plugin settings JSON accepts any JSON type and malformed JSON silently becomes `{}`, masking tampering and causing handler ambiguity.                                                         | Enforce object-only settings payloads and propagate validation errors back to the UI/profile loader.                                   | S      | CWE-20                                           |
| SEC-012 | Persistence                   | Medium   | src/core/src/profile.cpp:161                                 | There is no hardened profile JSON reader in core, and app load/save are stubs, so eventual persistence code lacks an established validated path.                                              | Introduce a single schema-validated loader with bounded sizes, canonical parsing, and atomic saves before enabling profile IO broadly. | L      | CWE-345                                          |
| SEC-013 | Dependency security           | Medium   | .github/workflows/ci.yml:70                                  | CI/release pin Qt 6.7.3, which public advisories list as affected by Qt XML and QtSvg vulnerabilities.                                                                                        | Upgrade to a fixed Qt line (for example 6.8.5+ or other maintained fixed release) and track Qt advisories in CI.                       | M      | CVE-2025-30348 / CVE-2025-10728 / CVE-2025-10729 |
| SEC-014 | CI/CD integrity               | High     | .github/workflows/release.yml:138                            | Release workflow publishes unsigned artifacts with no checksum, attestation, or provenance.                                                                                                   | Add code signing, checksum manifests, SLSA-style provenance/attestation, and release verification instructions.                        | L      | CWE-494                                          |
| SEC-015 | CI/CD integrity               | High     | .github/workflows/nightly.yml:146                            | Nightly force-moves the `nightly` tag, making binaries mutable and hard to hash-pin safely.                                                                                                   | Publish immutable nightly tags or attach signed checksums keyed to commit SHA without force-updating a shared tag.                     | M      | CWE-353                                          |
| SEC-016 | Supply chain                  | Medium   | CMakeLists.txt:50                                            | FetchContent and packaging sources are pinned to mutable tags instead of immutable commits.                                                                                                   | Pin external sources to commit SHAs and verify checksums where possible.                                                               | S      | CWE-494                                          |
| SEC-017 | Workflow hardening            | Medium   | .github/workflows/release.yml:32                             | Third-party GitHub Actions are version-tag pinned rather than commit-SHA pinned.                                                                                                              | Pin each external action to a full commit SHA and rotate via Dependabot.                                                               | S      | CWE-829                                          |
| SEC-018 | CI/CD security                | Medium   | .github/workflows/lint.yml:30                                | Lint workflow grants write permissions and auto-commits fixes back to PR branches, increasing impact if tooling or dependencies are compromised.                                              | Restrict write-back to trusted branches, gate behind maintainers, or remove auto-commit from default workflow.                         | M      | CWE-732                                          |
| SEC-019 | Observability / policy gap    | Low      | docs/architecture/PLUGIN-SYSTEM.md:104                       | Docs promise unsigned-plugin warning UI, but no current implementation enforces or displays that warning.                                                                                     | Either implement the warning in app/plugin model code or remove the claim until it exists.                                             | S      | CWE-693                                          |
| SEC-020 | Release transparency          | Low      | docs/wiki/Release-Process.md:78                              | Stable release process documents that Windows/macOS builds are unsigned, which is acceptable for alpha but materially reduces user trust.                                                     | Prioritize publisher certificates and notarization before declaring stable releases.                                                   | L      | CWE-347                                          |
| SEC-021 | Dependency monitoring         | Medium   | .github/dependabot.yml:3                                     | Dependabot covers Actions and pip only; vendored CMake/FetchContent and Flatpak git sources are outside automated update visibility.                                                          | Add external dependency inventory/SBOM plus scheduled checks for FetchContent and packaging manifests.                                 | M      | CWE-1104                                         |
| SEC-022 | Workflow coverage             | Medium   | .github/workflows/ci.yml:17                                  | No CodeQL, dependency review, secret scanning, or artifact attestation workflow is present.                                                                                                   | Add GitHub Advanced Security equivalents or OSS alternatives for code scanning, dependency review, and secret detection.               | M      | CWE-693                                          |
