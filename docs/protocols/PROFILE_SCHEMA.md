# Profile JSON Schema

A profile describes the mapping between physical controls on a device and logical actions that the profile engine fires when those controls are activated.

## Schema (draft)

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
        "^[0-9]+$": { "$ref": "#/$defs/Binding" }
      }
    },
    "mouseButtons": {
      "type": "object",
      "additionalProperties": { "$ref": "#/$defs/Binding" }
    },
    "applicationHints": {
      "type": "array",
      "items": { "type": "string" }
    }
  },

  "$defs": {
    "Binding": {
      "type": "object",
      "properties": {
        "onPress":     { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "onRelease":   { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "onLongPress": { "type": "array", "items": { "$ref": "#/$defs/Action" } },
        "state": {
          "type": "object",
          "properties": {
            "imagePath":  { "type": "string" },
            "text":       { "type": "string" },
            "background": { "type": "array", "items": { "type": "integer" }, "minItems": 3, "maxItems": 3 },
            "foreground": { "type": "array", "items": { "type": "integer" }, "minItems": 3, "maxItems": 3 },
            "fontSize":   { "type": "integer", "minimum": 6, "maximum": 64 }
          }
        }
      }
    },
    "Action": {
      "type": "object",
      "required": ["id"],
      "properties": {
        "id":       { "type": "string", "description": "<plugin-id>.<action-id>" },
        "label":    { "type": "string" },
        "settings": { "type": "object", "additionalProperties": true }
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
      "state":   { "text": "Cam 1", "background": [255, 87, 34] },
      "onPress": [
        { "id": "com.obsproject.obs.switch-scene", "settings": { "scene": "Cam 1" } }
      ]
    },
    "2": {
      "state":   { "text": "Cam 2", "background": [38, 198, 218] },
      "onPress": [
        { "id": "com.obsproject.obs.switch-scene", "settings": { "scene": "Cam 2" } }
      ]
    },
    "15": {
      "state":   { "text": "Mute", "background": [239, 83, 80] },
      "onPress": [
        { "id": "com.obsproject.obs.toggle-mute", "settings": { "source": "Mic" } }
      ]
    }
  }
}
```

## Compatibility policy

- **Additive only.** New fields may be added in minor versions; existing fields never change meaning.
- **Forward-compatible readers.** Unknown fields are preserved on round-trip so third-party tools can layer extensions.
- **Per-family subtypes.** `keys` maps to physical key indices on devices that have keys; `encoders` on devices with encoders; `mouseButtons` on mice. A profile may populate multiple families when authored against a multi-device preset.
