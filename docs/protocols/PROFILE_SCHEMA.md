# Profile JSON Schema

A profile describes the mapping between physical controls on a device and
logical actions that the profile engine fires when those controls are
activated.

This document is the **source of truth for the wire format**. The C++
struct `ajazz::core::Profile` (see `src/core/include/ajazz/core/profile.hpp`)
is internal naming; the field names declared below are the on-disk JSON
keys produced by `profileToJson()` and consumed by `profileFromJson()`.

| C++ field                 | JSON wire key | Why they differ                 |
| ------------------------- | ------------- | ------------------------------- |
| `Profile::deviceCodename` | `"device"`    | shorter, schema-natural         |
| `Action::settingsJson`    | `"settings"`  | underlying type is escaped JSON |

## Schema (current)

```jsonc
{
  "$schema":   "https://json-schema.org/draft/2020-12/schema",
  "type":      "object",
  "required":  ["id", "name", "device"],
  "properties": {
    "id":     { "type": "string", "format": "uuid" },
    "name":   { "type": "string", "minLength": 1 },
    "device": { "type": "string", "description": "device codename, e.g. akp153" },

    "keys": {
      "type": "object",
      "patternProperties": {
        "^[0-9]+$": { "$ref": "#/$defs/Binding" }
      }
    },
    "encoders": {
      "type": "object",
      "patternProperties": {
        "^[0-9]+$": { "$ref": "#/$defs/EncoderBinding" }
      }
    },
    "mouseButtons": {
      "type": "object",
      "additionalProperties": { "$ref": "#/$defs/Binding" }
    },
    "pages": {
      "type": "object",
      "description": "Sub-folder pages reachable via Action kind=openFolder. Keyed by stable page id (UUIDv4 recommended); the root page lives in the top-level `keys` map.",
      "additionalProperties": { "$ref": "#/$defs/ProfilePage" }
    },
    "applicationHints": {
      "type": "array",
      "items": { "type": "string" }
    }
  },

  "$defs": {
    "Binding": {
      "type": "object",
      "description": "Key-style binding: three action chains keyed by event.",
      "properties": {
        "onPress":     { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "onRelease":   { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "onLongPress": { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "state":       { "$ref": "#/$defs/KeyState" }
      }
    },
    "EncoderBinding": {
      "type": "object",
      "description": "Rotary-encoder binding: clockwise, counter-clockwise, and press chains.",
      "properties": {
        "onCw":    { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "onCcw":   { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "onPress": { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "state":   { "$ref": "#/$defs/KeyState" }
      }
    },
    "Action": {
      "type": "object",
      "required": ["id"],
      "properties": {
        "kind": {
          "type": "string",
          "enum": ["plugin", "sleep", "key", "command", "url", "openFolder", "back"],
          "default": "plugin",
          "description": "Built-in step kind interpreted natively by ActionEngine. `plugin` forwards to the plugin host."
        },
        "id":       { "type": "string", "description": "<plugin-id>.<action-id> when kind=plugin; otherwise unused." },
        "label":    { "type": "string", "description": "User-visible label shown in the editor." },
        "settings": {
          "type": "string",
          "description": "Opaque JSON document carried verbatim to the action interpreter. Stored as an escaped JSON string (NOT a nested object) so the wire format stays linear and unknown sub-keys round-trip untouched."
        },
        "delayMs":  { "type": "integer", "minimum": 0, "description": "Inter-step delay (used by kind=sleep; also added as a post-step pause for other kinds)." }
      }
    },
    "KeyState": {
      "type": "object",
      "description": "Optional visual appearance for the slot on the device LCD and in the editor preview.",
      "properties": {
        "imagePath":  { "type": "string" },
        "text":       { "type": "string" },
        "background": { "type": "array", "items": { "type": "integer", "minimum": 0, "maximum": 255 }, "minItems": 3, "maxItems": 3 },
        "foreground": { "type": "array", "items": { "type": "integer", "minimum": 0, "maximum": 255 }, "minItems": 3, "maxItems": 3 },
        "fontSize":   { "type": "integer", "minimum": 6, "maximum": 64 }
      }
    },
    "ProfilePage": {
      "type": "object",
      "description": "Sub-folder page. Reachable through an Action of kind=openFolder whose settings carry `{\"target\":\"<page-id>\"}`.",
      "required": ["name"],
      "properties": {
        "name":     { "type": "string", "minLength": 1 },
        "keys": {
          "type": "object",
          "patternProperties": {
            "^[0-9]+$": { "$ref": "#/$defs/Binding" }
          }
        },
        "children": {
          "type": "array",
          "items": { "type": "string", "description": "child page id" }
        }
      }
    }
  }
}
```

## Example

```jsonc
{
  "id":     "4c3a5b84-feed-4c0c-a1b9-deadbeef0001",
  "name":   "OBS scene switcher",
  "device": "akp153",
  "applicationHints": [ "obs", "obs-studio" ],
  "keys": {
    "1": {
      "onPress": [
        {
          "kind":     "plugin",
          "id":       "com.obsproject.obs.switch-scene",
          "label":    "Cam 1",
          "settings": "{\"scene\":\"Cam 1\"}",
          "delayMs":  0
        }
      ],
      "onRelease":   [],
      "onLongPress": []
    },
    "15": {
      "onPress": [
        {
          "kind":     "plugin",
          "id":       "com.obsproject.obs.toggle-mute",
          "label":    "Mute",
          "settings": "{\"source\":\"Mic\"}",
          "delayMs":  0
        }
      ],
      "onRelease":   [],
      "onLongPress": []
    }
  },
  "encoders": {
    "0": {
      "onCw":    [{"kind":"plugin","id":"com.obsproject.obs.volume-up","label":"Vol+","settings":"{\"source\":\"Mic\"}","delayMs":0}],
      "onCcw":   [{"kind":"plugin","id":"com.obsproject.obs.volume-down","label":"Vol-","settings":"{\"source\":\"Mic\"}","delayMs":0}],
      "onPress": [{"kind":"plugin","id":"com.obsproject.obs.toggle-mute","label":"Mute","settings":"{\"source\":\"Mic\"}","delayMs":0}]
    }
  }
}
```

## Compatibility policy

- **Additive only.** New fields may be added in minor versions; existing
  fields never change meaning.
- **Forward-compatible readers.** Unknown top-level keys, unknown Binding
  keys, and unknown Action keys are skipped silently by
  `profileFromJson()` so third-party tooling can layer extensions.
- **Unknown `kind` values** fall back to `kind=plugin` for forward
  compatibility with future built-in step kinds.
- **Per-family subtypes.** `keys` maps to physical key indices on devices
  that have keys; `encoders` on devices with encoders; `mouseButtons` on
  mice; `pages` provides nested folder pages on devices that support them
  (currently AKP devices via `Action.kind=openFolder`). A profile may
  populate multiple families when authored against a multi-device preset.
- **`settings` is a string, not an object.** The wire format escapes the
  inner JSON so unknown sub-keys round-trip exactly. UIs that want to
  edit settings as a nested object should `JSON.parse(action.settings)`
  on read and `JSON.stringify(obj)` on write.
