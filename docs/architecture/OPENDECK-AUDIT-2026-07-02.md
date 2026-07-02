# OpenDeck subsystem audit — 2026-07-02

Four parallel read-only scans (bridge↔SPA contract, Elgato WS protocol path,
catalog/lifecycle, recent-diff semantics) triggered by user reports of "many
small plugin bugs". Every finding below was verified on both sides (code +
contract) before being recorded. Status letters: **F** fixed, **O** open,
**D** deferred (feature-sized).

## Cluster 1 — SPA profile management (opendeck_bridge)

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 1.1 | P1 | `rename_profile` ignores `oldId`/`retain` → renames the ACTIVE profile; SPA "Duplicate" destructively renames instead of copying | F |
| 1.2 | P1 | `set_selected_profile` never CREATES a missing profile (SPA "Create profile" is a silent no-op, vanishes on restart) | F |
| 1.3 | P2 | Single-global-profile vs upstream per-device selected profile; second device shows first device's profile | O |

## Cluster 2 — settings/PI routing (plugin_device_bridge + sd_plugin_server)

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 2.1 | P1 | `didReceiveSettings` always echoed to the plugin, never to a WS PI; `setSettings` self-echo can loop plugins that setSettings inside didReceiveSettings | F |
| 2.2 | P1 | `set/getGlobalSettings` from a WS PI key the store on the PI's context string, not the owning plugin — PI-saved globals unreadable by the plugin | F |
| 2.3 | P1 | `setGlobalSettings` never emits `didReceiveGlobalSettings` to the other party (spec: notify plugin + all its PIs) | F |
| 2.4 | P2 | Two context namespaces: PI registers/talks SPA dot-form, plugin sees wire `#` form; `sendToPlugin` forwarded verbatim → plugin can't match instance; `sendToPropertyInspector` exact-uuid match never hits the PI | F |
| 2.5 | P2 | `get*Settings` optional `id` correlator never echoed on `didReceive*` (modern Elgato SDK promise helper breaks) | F |

## Cluster 3 — plugin lifecycle (plugin_manager + catalog)

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 3.1 | P1 | `remove_plugin` never stops the running plugin (process/HTML page keeps running & painting); `m_live` keeps the key so reinstall never respawns until app restart | F |
| 3.2 | P2 | Store update/reinstall of a running plugin never respawns it | F |
| 3.3 | P2 | CDN installs named by numeric product id, file installs by manifest UUID → same plugin can exist twice with identity collision | O |
| 3.4 | P2 | HTML-plugin disable leaks the page when PUUID ≠ dir name (`m_htmlPages` keyed by puuid, erased by dir name) | F |
| 3.5 | P2 | Bundled-plugin seed "already present" branch never re-asserts consent → sweep can quarantine a bundled plugin | O |
| 3.6 | P2 | Seed partial-copy failure leaves broken install + marker blocks reseed forever | O |
| 3.7 | P2 | Cross-filesystem promote fallback copies only two dir levels (deep bundles silently truncated) | O |
| 3.8 | P3 | Per-plugin `allowPlugin()` unreachable (no caller) — quarantine recovery is global-toggle only | O |
| 3.9 | P3 | `.SDPlugin` case-variant dirs spawn but are invisible to list/verify/quarantine | O |
| 3.10 | P3 | Extractor: no ZIP64 EOCD | O |
| 3.11 | P3 | Nested single-folder wrapper → manifest-less dir promoted as success, never discovered | O |
| 3.12 | P3 | Bridge file-install ignores installFromFile's return (failures look like success) | F |
| 3.13 | P3 | `remove_plugin` can't remove quarantined `.disabled` dirs; consent key outlives removal | O |
| 3.14 | P3 | `exitApp` addressed to dir-name key; PUUID plugins never get it on disable/shutdown | F |

## Cluster 4 — instance editor / actions (bridge + shaping)

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 4.1 | P1 | `set_state` contract inverted: SPA sends the full edited ActionState to persist; we discard it and switch `current_state` instead (editor edits vanish; browsing states flips live state) | F |
| 4.2 | P1 | Multi Action / Toggle Action advertised but `children: null` → ParentActionView throws; drop into parent overwrites the parent | F (unadvertised until implemented) |
| 4.3 | P2 | 4-segment contexts vs upstream 5-segment → every PI gets `isInMultiAction: true`; paste never opens the PI | O |
| 4.4 | P2 | `update_image` null-clear ignores Encoder/touch; paints regardless of selected profile/device | O |
| 4.5 | P2 | `create_instance` missing controllers guard (Keypad-only action bindable to a dial) | F |
| 4.6 | P3 | `trigger_virtual_press` on a touch slot injects phantom key `keyCount+zone+1` instead of a touch tap | F |
| 4.7 | P3 | `plugin_reloaded` payload `{}` instead of plugin id → open PI iframes never refresh | F |
| 4.8 | P3 | `list_plugins` lacks `builtin`/`has_settings_interface` | O |
| 4.9 | P3 | `show_alert`/`show_ok`/`key_moved`/`device_brightness`/`applications` events never emitted to SPA | O |

## Cluster 5 — settings surface (bridge)

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 5.1 | P2 | `backup/restore/open_config/open_log` unhandled → all four Settings buttons dead | F |
| 5.2 | P2 | `set_settings` store-only: brightness/sleep sliders in OpenDeck Settings are inert | O |
| 5.3 | P2 | `make_info` returns `{}` — stock Elgato PI libs reading `info.application.*` throw before registering | O |
| 5.4 | P3 | `restart` / `reload_plugin` / `show_settings_interface` unhandled | O |
| 5.5 | P3 | `get_build_info` plain string vs upstream HTML (OS-conditional UI never matches) | O |

## Cluster 6 — protocol details (plugin_device_bridge)

| # | Sev | Finding | Status |
|---|-----|---------|--------|
| 6.1 | P2 | Stale `m_titleByKey`/`m_encoderFeedback`/`m_encoderLayoutOverride` survive context retirement → old action's title composited onto the NEW action's image | F |
| 6.2 | P2 | Encoder i and touch-zone i collide on the same context id (registry tuple identical) → wrong action uuid in dial events when both bound | O (needs convention change on the locked TouchUp lookup) |
| 6.3 | P2 | Touch-zone `setImage`/`setTitle` mirror resolves only `profile.encoders` → touch visuals never reach the SPA canvas / can overwrite the encoder slot | O |
| 6.4 | P2 | PI settings vs willAppear settings precedence diverge (PI/SPA read binding instance, plugin runs on stored) — reopening PI shows defaults | O |
| 6.5 | P3 | PI context resolution misses touch-zone instances (Keypad math on an Encoder-registered ctx) | O |
| 6.6 | P3 | `dialRotate.pressed` hardcoded false | O |
| 6.7 | P3 | `touchTap.tapPos` raw device X (0-255), y=0, `hold` never true | O (needs strip geometry) |
| 6.8 | P3 | `titleParametersDidChange.title` always "" (user label never delivered) | O |
| 6.9 | P3 | No `deviceDidConnect` replay for late-registering plugins; phantom "akp05e" fallback id | O |
| 6.10 | P3 | `{}` reserved sentinel: plugin can never clear its settings back to empty | O |
| 6.11 | P3 | `mirrorSafeDataUri` xmlns probe matches `xmlns:xlink` (false positive, no default-ns injection) | F |
| 6.12 | P3 | `clearBindingsForPlugin` skips touchZones (uninstall leaves orphan touch bindings) | F |
| 6.13 | P3 | Encoder/touch in-place rebind sends no willDisappear to the displaced plugin | O |
| 6.14 | P3 | Narrowing casts: keyIdx0+1 → uint8_t (wraps ≥255); encoder position unbounded into uint8_t | O |

Icons (fixed pre-audit, same sweep): .jpg icon probe missing; sysmon bundle
shipped no icon (`--render-test` arg order).
