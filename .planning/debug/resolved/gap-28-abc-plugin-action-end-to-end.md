---
status: resolved
trigger: 'Phase 28 gap-closure: GAP A (no OS/version diagnostic), GAP B (plugin action bound to dial does not fire end-to-end), GAP C (wrong -pluginUUID passed to node plugins)'
created: 2026-05-31T00:00:00Z
updated: 2026-05-31T00:00:00Z
---

## Current Focus

hypothesis: |
GAP B root cause is CONFIRMED (multi-layer):
(1) manifest.puuid reads PUUID not UUID, so the standard Elgato UUID field is not passed as -pluginUUID
(2) When a plugin registers with the manifest UUID (via hardcoded registerPlugin), ownerForActionUuid works,
but the profileChanged -> populateContextsForActivePage lambda has a guard issue that may prevent
firing if device is not yet active; OR more precisely: the profile object IS consistent,
the guard at line 546 uses m_pluginBridge->activeDeviceId() which requires onDeviceConnected to
have been called. When the device physically connects, onDeviceConnected fires, but at that point
the profile has no binding yet. When commitEncoderBinding fires profileChanged later, the lambda
calls populateContextsForActivePage. This should work IF activeDeviceId is set.
(3) The stale stub at application.cpp:406-409 catches plugin actions in the ActionEngine path
and logs "ignored (plugin host arrives Phase 19)" -- this runs BEFORE deviceEvent is emitted.
The action engine runs it->second.onPress for EncoderPressed (line 227-228 of input service),
which calls the Phase-19 stub (not the bridge). The bridge gets the deviceEvent signal AFTER.
The issue is that m_engine->run(onPress) calls the plugin-stub, which logs the stale message.
But the bridge ALSO receives deviceEvent and does its own lookup -- this is not mutually exclusive.
PRIME SUSPECT for no-willAppear: when commitEncoderBinding fires profileChanged, the lambda
at application.cpp:542-550 calls populateContextsForActivePage(activeDeviceId). But at this
point m_registeredPlugins in the bridge may be empty (plugin hasn't registered yet), OR the plugin
has registered but ownerForActionUuid fails because the plugin registered under the .sdPlugin dir
name (if it echoes -pluginUUID).

CONFIRMED: manifest.puuid maps to PUUID key, not UUID key. Standard Elgato "UUID": "com.acc.test.dialdemo"
in manifest.json does NOT set puuid. So pluginUuid falls back to pluginId = dir-name =
"com.acc.test.dialdemo.sdPlugin". The node receives -pluginUUID "com.acc.test.dialdemo.sdPlugin".
If the node plugin sends registerPlugin with this literal string, m_registeredPlugins contains
"com.acc.test.dialdemo.sdPlugin", and ownerForActionUuid("com.acc.test.dialdemo.dial", {...})
checks if "com.acc.test.dialdemo.sdPlugin" is a prefix of "com.acc.test.dialdemo.dial" --
it is NOT (because .sdPlugin is not a dotted-prefix boundary match).
HOWEVER the live-verification notes say the test plugin HARDCODES the manifest uuid in registerPlugin,
so it registers as "com.acc.test.dialdemo". With that UUID, ownerForActionUuid would match.

So GAP C is real but the live test worked around it by hardcoding. The remaining issue for
no-willAppear is timing: if plugin registers first (before commitEncoderBinding),
onPluginRegistered fires and calls populateContextsForActivePage -- but the profile has no
binding yet, so no contexts are registered. Then commitEncoderBinding fires, profileChanged
fires, the lambda calls populateContextsForActivePage with the binding present.
ownerForActionUuid finds "com.acc.test.dialdemo" in m_registeredPlugins. This should work.

NEXT: instrument the actual paths to confirm which step fails.

test: Build with additional TRACE logging, launch, and observe which path is taken
expecting: Either populateContextsForActivePage finds the binding but returns without registering,
or the deviceEvent signal doesn't reach onDeviceEvent with the correct device ID
next_action: |

1. Read the full onDeviceEvent to confirm it uses the deviceId from the signal correctly
1. Check that m_activeDeviceId in the bridge is non-empty when profileChanged fires
1. Check if application.cpp:406-409 stale stub causes an early return before deviceEvent
1. Fix GAP C (manifest UUID parsing) and GAP A (diagnostic counter) and GAP B (root cause)

## Symptoms

expected: Plugin receives willAppear on bind + dialDown/keyDown on synthetic input
actual: Plugin only receives passHello; app logs "plugin action ... ignored (plugin host arrives Phase 19)"
errors: "[input] plugin action com.acc.test.dialdemo.dial ignored (plugin host arrives Phase 19)"
reproduction: commitEncoderBinding + input.encoder/input.encoderPress
started: Phase 28 live verification

## Eliminated

- hypothesis: Profile object mismatch (commitEncoderBinding writes different object than accessor returns)
  evidence: Both m_profileController->activeProfile() and commitEncoderBinding use the same m_profile member
  timestamp: 2026-05-31T00:10:00Z

- hypothesis: injectSyntheticEvent does not emit deviceEvent
  evidence: dispatch() at line 294 unconditionally emits deviceEvent(m_activeDeviceId, ev);
  injectSyntheticEvent calls dispatch() directly
  timestamp: 2026-05-31T00:10:00Z

## Evidence

- timestamp: 2026-05-31T00:15:00Z
  checked: plugin_manager.cpp lines 373, plugin_manifest.cpp line 207
  found: manifest.puuid maps to "PUUID" key only; top-level "UUID" key in Elgato manifests is NOT parsed
  implication: GAP C is real - node plugins receive -pluginUUID = dir-name.sdPlugin, not manifest UUID

- timestamp: 2026-05-31T00:20:00Z
  checked: plugin_catalog_model.cpp lines 384-386
  found: manifestRunnableHere rejection has no counter; skips silently with no diagnostic variable incremented
  implication: GAP A confirmed - need skippedOsVersion counter

- timestamp: 2026-05-31T00:25:00Z
  checked: application.cpp lines 404-409 (ActionEngine plugin stub)
  found: m_engine run()s onPress for EncoderPressed at line 227-228 of input service; the engine
  calls the lambda at 406-409 for plugin kind actions which logs "ignored (plugin host arrives Phase 19)"
  This is a separate path from the bridge; the bridge still gets deviceEvent signal.
  The log message is confusing but not the cause of no-willAppear.
  implication: The stale stub is a misleading log, not a blocker. Should be silenced or removed.

- timestamp: 2026-05-31T00:30:00Z
  checked: application.cpp lines 542-550 (profileChanged -> populateContextsForActivePage lambda)
  found: Guard is m_pluginBridge->activeDeviceId().isEmpty(). This calls onDeviceConnected which
  must have fired. In live scenario: AKP05E physically connects at startup -> onDeviceConnected("akp05e")
  fires -> m_activeDeviceId = "akp05e". Later commitEncoderBinding fires profileChanged ->
  lambda calls populateContextsForActivePage("akp05e"). The plugin should be in m_registeredPlugins
  by then (it registered at startup before the binding was committed).
  implication: The path SHOULD work if plugin is registered and activeDeviceId is set.
  The actual failure may be in ownerForActionUuid matching, which depends on what UUID
  the plugin registered with.

## Resolution

root_cause: |
THREE bugs:

GAP C (blocks B): plugin_manifest.cpp reads PUUID not UUID for manifest.puuid. The top-level
"UUID" field (standard Elgato format) is not read into manifest.puuid. So pluginManager passes
dir-name.sdPlugin as -pluginUUID to node processes. If a plugin follows the spec and echoes
-pluginUUID in registerPlugin, it registers under the wrong UUID, ownerForActionUuid fails to
match it, no contexts are registered, no willAppear fires. The live test worked around it by
hardcoding the manifest UUID in registerPlugin, but this is a latent routing bug.

GAP B (stale log misleads, actual issue may be timing/UUID): The "ignored (plugin host arrives
Phase 19)" log fires from the ActionEngine fallback for all plugin actions. This is harmless to
the bridge path but confusing. More critically, if GAP C were not worked around, no willAppear
would fire. With GAP C worked around, the bridge path SHOULD work.

GAP A: installedActions() at line 384 silently continues when manifestRunnableHere returns false,
with no diagnostic counter. Users see 0 tools with no explanation.

fix: to be applied
verification: pending
files_changed: []
