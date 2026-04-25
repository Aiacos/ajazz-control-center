# Branding / white-labeling

AJAZZ Control Center is built so that the product can be re-skinned for OEM partners, custom community forks, or simply for users who want to change the accent color and window title. This page describes the branding system: which knobs exist, how they're wired, and how to ship a custom build.

## Layers of branding

| Layer            | Configurable at | Mechanism                                                    |
| ---------------- | --------------- | ------------------------------------------------------------ |
| **Product name** | Build           | CMake cache var `AJAZZ_PRODUCT_NAME`                         |
| **Vendor name**  | Build           | CMake cache var `AJAZZ_VENDOR_NAME`                          |
| **App ID**       | Build           | `AJAZZ_APP_ID` (e.g. `io.github.Aiacos.AjazzControlCenter`)  |
| **Icons**        | Build           | `AJAZZ_BRAND_DIR` — path to a directory with required assets |
| **Theme colors** | Build + runtime | JSON theme file embedded in resources or loaded from disk    |
| **Strings**      | Build           | Optional Qt `.ts` translation file with branded copy         |

Build-layer settings are baked into the binary at configure time. Runtime settings (currently: theme accent + dark/light) live in `QSettings` and can be changed from the UI without rebuilding.

## CMake variables

```bash
cmake -S . -B build \
    -DAJAZZ_PRODUCT_NAME="Acme Deck Studio" \
    -DAJAZZ_VENDOR_NAME="Acme Corp" \
    -DAJAZZ_APP_ID="com.acme.deckstudio" \
    -DAJAZZ_BRAND_DIR="/abs/path/to/branding/acme"
```

When unset, defaults reproduce the upstream "AJAZZ Control Center" identity.

## Brand directory layout

`AJAZZ_BRAND_DIR` must point at a directory with this structure:

```
acme/
├── icons/
│   ├── app.ico                  # Windows
│   ├── app.icns                 # macOS bundle icon
│   ├── app-16.png … app-512.png # Linux hicolor sizes
│   └── tray.png                 # tray-icon source (will be themed at runtime)
├── theme.json                   # accent + neutral palette
├── splash.png                   # optional, shown during PluginHost init
├── about.md                     # optional, replaces the default "About" copy
└── strings.ts                   # optional Qt translation override
```

`theme.json` schema:

```json
{
  "accent":    "#41CD52",
  "accent2":   "#0A82FA",
  "bgBase":    "#14141a",
  "bgSidebar": "#1e1e23",
  "bgRowHover":"#2c2c34",
  "fgPrimary": "#f0f0f0",
  "fgMuted":   "#888888"
}
```

The build system copies all of the above into `qrc:/branding/` and exposes them through the C++ class `BrandingService`:

```cpp
class BrandingService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString productName READ productName CONSTANT)
    Q_PROPERTY(QString vendorName  READ vendorName  CONSTANT)
    Q_PROPERTY(QColor  accent      READ accent      NOTIFY themeChanged)
    Q_PROPERTY(QUrl    appIcon     READ appIcon     CONSTANT)
    ...
};
```

QML reads from `branding.accent`, `branding.bgSidebar`, etc. — no hard-coded hex anywhere in the QML files.

## Runtime overrides

Even on a vanilla build, the user can override colors at runtime via:

- **Settings → Appearance → Theme**: pick from bundled themes or load a `theme.json` from disk.
- `QSettings` key `Branding/ThemeOverride` — a path to a JSON file. If valid, it shadows the embedded theme.

Runtime overrides emit `BrandingService::themeChanged()`, which triggers QML re-evaluation of all `branding.*` bindings.

## What is **not** brandable

- The license — output binaries remain GPL-3.0-or-later. Forks must comply.
- Telemetry — there is no telemetry; nothing to brand.
- The Python `ajazz` runtime module name — plugins import `ajazz` regardless of the wrapper product. Renaming would break the entire plugin ecosystem.

## Verifying a custom brand

```bash
# build with custom brand
cmake --preset linux-release \
    -DAJAZZ_PRODUCT_NAME="Acme Deck" -DAJAZZ_BRAND_DIR=/path/to/acme
cmake --build --preset linux-release

# launch
./build/linux-release/src/app/ajazz-control-center
```

The window title, tray tooltip, taskbar icon and About dialog should all reflect the Acme brand. Theme accent colors should be the ones from `theme.json`.

If anything still says "AJAZZ", file a bug — that's a leak in the branding pipeline and we treat them as P1.
