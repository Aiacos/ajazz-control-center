# AJAZZ AK980 PRO — `mui.dll` first-pass RE

First-pass reverse-engineering of the custom UI toolkit shipped with the
AK980 PRO Driver. Compiled from **clean-room static Ghidra 12.1 analysis
only** (auto-analyse on the imported DLL); the vendor binary was never
executed.

> Citations point at functions in the Ghidra project at
> `C:/temp/ghidra_proj/ak980_proj/mui.dll`. Raw export dump is at
> `C:/Users/unilo/reverse-eng-workdir/ak980pro/mui_dll_inventory.json`.

______________________________________________________________________

## 1. Binary identity

| Property            | Value                                                |
| ------------------- | ---------------------------------------------------- |
| File                | `app/mui.dll` (1 187 840 bytes)                      |
| Type                | PE32 x86 DLL (GUI subsystem), Visual Studio 2022     |
| Sections            | `Headers`, `.text` (689 KB), `.rdata` (228 KB), `.data` (4.7 KB), `.gfids`, `.tls`, `.rsrc` (218 KB), `.reloc` (34.5 KB) |
| Image base          | `0x10000000`                                         |
| Export count        | **6 604 entry-point symbols** (the "2 201 exports"  earlier sighting was an undercount — the .edata table has 2 201 ordinals but Ghidra resolves both ordinal and mangled C++ names) |
| Naming              | MSVC C++ name mangling (`?Name@Class@MUI@@…`) — full namespace `MUI::*` |
| Resource section    | 218 KB `.rsrc`: bitmap/cursor/icon resources for the toolkit |
| Imports             | Standard Win32 (`user32`, `gdi32`, **GDI+**, `comctl32`, `comdlg32`, `shell32`, `ole32`, `oleaut32`, **`msimg32`** for AlphaBlend, `winmm` for `timeBeginPeriod`) |
| External deps       | `MFC140U.DLL`, `MSVCP140.DLL`, `VCRUNTIME140.DLL`    |

**Not** Microsoft's Windows MUI subsystem. The "MUI" namespace is
internal to AJAZZ / the toolkit vendor (possibly a derived/forked version
of an open-source MFC-like framework — the class-name patterns
(`MObject` / `MControl` / `PaintManager` / `MWindow`) resemble
**[DuiLib](https://github.com/duilib/duilib)** or **[Soui](https://github.com/setoutsoftware/SOUI)**
but I haven't matched signatures verbatim. The build is unlikely to be
pure DuiLib — DuiLib uses `CDuiBase` / `CControlUI`, not `M*` — but the
architecture (a `PaintManager` per window managing child `MControl`s,
mouse-message dispatch through virtual functions) is the same conceptual
model.).

______________________________________________________________________

## 2. Class catalogue (from 6 604 exports, 66 distinct classes)

| Class                  | Method count | Role                                              |
| ---------------------- | ------------ | ------------------------------------------------- |
| `MRichEdit`            | 120          | Rich-text edit widget (windowed)                  |
| `MControl`             | 94           | **Base widget**: paint, mouse, focus, message     |
| `MDialog`              | 75           | Top-level dialog with title-bar / close box       |
| `CustomLightMode`      | 67           | **App-specific**: hosts the per-key RGB grid and 9 host-side lighting effects (see §3) |
| `MenuWnd`              | 59           | Context-menu / popup-menu host                    |
| `MWindow`              | 50           | Top-level window (parent of MDialog)              |
| `MPopupWnd`            | 48           | Floating popup (combo dropdown, tooltip)          |
| `MAirBladder`          | 45           | "Air bladder" (tooltip + arrow callout)           |
| `MTabCtrl`             | 45           | Tab control (My Config / Macro / Light / Screen tabs) |
| `MEdit`                | 43           | Single-line edit box                              |
| `CKeyboardTips`        | 41           | Keyboard mini-help overlay                        |
| `MListBox`             | 38           | Vertical list box (profile list, macro list)      |
| `MPanel`               | 36           | Container panel                                   |
| `MRender`              | 29           | **Rendering pipeline** — GDI+ raster sink         |
| `MComboBox`            | 26           | Combo box                                         |
| `CRecordList`          | 25           | Macro-record event list                           |
| `MControlFont`         | 25           | Font-aware text label                             |
| `Manager`              | 24           | App manager (singleton context)                   |
| `MenuItem`             | 23           | Item in `MenuWnd`                                 |
| `MShadow`              | 21           | Drop-shadow renderer                              |
| `CIconButton`          | 20           | Icon button (close, minimize, store)              |
| `MSlider`              | 17           | Slider (brightness, speed)                        |
| `CDeviceList`          | 17           | Device picker                                     |
| `CColorPalette`        | 16           | Color palette grid                                |
| `MPalette`             | 16           | Color picker (generic)                            |
| `CRadioButton`         | 16           | Radio-button group                                |
| `MScroll`              | 16           | Scrollbar widget                                  |
| `CColorButton`         | 16           | Color-swatch button                               |
| `CSelectButton`        | 16           | Multi-state select button                         |
| `MSpinButton`          | 15           | Up/down spinner                                   |
| `MText`                | 14           | Static text                                       |
| `CColorSelect`         | 13           | Bigger color picker (HSV wheel)                   |
| `MTimer`               | 13           | Timer wrapper (used by Sleep callbacks)           |
| `CIDataObject`         | 13           | OLE drag-and-drop data object                     |
| `MImageList`           | 12           | Image-list (icon strip)                           |
| `MProgress`            | 12           | Progress bar (TFT upload, macro upload)           |
| `CComboBoxList`        | 12           | Combo-list popup                                  |
| `CScrollVertical`      | 11           | Vertical scrollbar                                |
| `CSpinButton`          | 11           | Alternate spin button                             |
| `CCheckButton`         | 11           | Checkbox                                          |
| `CScrollHorizontal`    | 10           | Horizontal scrollbar                              |
| `CGifPicture`          | 10           | **Animated GIF playback** (LCD preview)           |
| `CirclePalette`        | 10           | Circular color palette                            |
| `CTimerButton`         | 10           | Auto-repeat button                                |
| `CPhotoFrame`          | 10           | Image frame (LCD image preview)                   |
| `CIDropTarget`         | 10           | OLE drop target                                   |
| `MButton`              | 9            | Generic button                                    |
| `CColorCombo`          | 9            | Color combo                                       |
| `CSwitchButton`        | 9            | iOS-style on/off switch                           |
| `CDeviceCtrl`          | 8            | Device-status control                             |
| `CSliderPicture`       | 8            | Slider with image background                      |
| `CEnumFormatEtc`       | 7            | OLE drag-and-drop format enumerator               |
| `ColorView`            | 7            | Color preview                                     |
| `CTextButton`          | 6            | Text button                                       |
| `MObject`              | 6            | **Root of the object hierarchy** (refcount, RTTI) |
| `MPicture`             | 6            | Image control                                     |
| `CIDropSource`         | 5            | OLE drop source                                   |
| `MLevelCtrl`           | 5            | Level meter                                       |
| `CImageButton`         | 4            | Image-only button                                 |
| `MTabLayout`           | 3            | Tab layout helper                                 |
| `CLogonEdit`           | 3            | Login-style edit (masked)                         |
| `CArea`                | 3            | Rectangular region helper                         |
| `CHelpButton`          | 3            | Help button                                       |
| `CTextArea`            | 3            | Multi-line text area                              |
| `CDragSourceHelper`    | 2            | OLE drag-source helper                            |
| `MEditEx`              | 2            | Extended edit box                                 |
| `BatteryCtrl`          | (small)      | **Battery status indicator** — `SetBatteryInfo(percent, charging)` called from DeviceDriver.exe FUN_004358c0 |

Also referenced but probably with private exports only:
`PaintManager` (the rendering coordinator), `LCDViewList`, `KeyboardCtrl`,
`DPIControl`, `ImageView`.

### 2.1 Inheritance pattern (inferred from vftables)

```
MObject  (vtbl: OnBaseMessage, OnControlUpdate, GetObjectClass, BaseObjectClassName)
  ├── MControl  (adds: InitControl, InitUI, IsDraw, OnWindowMessage,
  │              OnMousePointChange, OnCheckMouseResponse,
  │              SetControlRect, SetControlVisible, SetControlWndVisible,
  │              SetControlEnabled, GetClassNameW)
  │     ├── MWindow       (windowed widgets)
  │     │     ├── MDialog (closable dialog)
  │     │     ├── MPopupWnd
  │     │     └── MenuWnd
  │     ├── MButton       (clickable widgets)
  │     │     ├── CIconButton, CColorButton, CTextButton, CImageButton,
  │     │     │   CCheckButton, CRadioButton, CSelectButton,
  │     │     │   CSwitchButton, CHelpButton, CTimerButton
  │     ├── MText, MControlFont (static text)
  │     ├── MEdit          (text input)
  │     │     ├── MRichEdit, MEditEx, CLogonEdit
  │     ├── MListBox, MComboBox, CComboBoxList
  │     ├── MSlider, CSliderPicture, MSpinButton, CSpinButton
  │     ├── MScroll, CScrollVertical, CScrollHorizontal
  │     ├── MProgress, MLevelCtrl, MPicture, CGifPicture, CPhotoFrame
  │     ├── MTabCtrl, MTabLayout
  │     ├── MPanel, MShadow, ColorView, MPalette, CColorPalette,
  │     │   CColorSelect, CColorCombo, CirclePalette, MImageList
  │     ├── DPIControl    (DPI-aware container — wraps two children at +0x150 / +0x154 vftables)
  │     ├── BatteryCtrl, CDeviceCtrl, CKeyboardTips
  │     └── CustomLightMode (very large — see §3)
```

**Message-dispatch model**: each `MControl` has virtual methods
`OnBaseMessage(uint msg, uint wp, int lp, long extra)`,
`OnWindowMessage(uint msg, uint wp, long lp)`,
`OnMousePointChange(CPoint&)`, `OnCheckMouseResponse(uint, CPoint)`,
`OnControlUpdate(CRect, int, MControl*)`. The framework owns the
WindowProc; it routes each native message to the appropriate descendant
control via hit-test and bubble.

**Rendering model**: `MControl::DrawControl(HDC)` is virtual; the
`PaintManager` owns the back-buffer (GDI+ `Graphics`) and walks the
control tree to composite. `MShadow` and `MAirBladder` use **GDI+**
`AlphaBlend` (imported from `msimg32.dll`) for soft shadows.

______________________________________________________________________

## 3. `CustomLightMode` — host-side lighting effects (NEW finding)

`CustomLightMode` is the **largest app class** (67 methods). It implements
**9 host-side lighting effects** ("Real-time Lighting" picker — strings
369-386 in `1033.lan`) entirely in C++ on the host, then pushes the cooked
per-key RGB buffer via opcode `0x20 0x04` to the keyboard. The keyboard
itself **does not run** these effects — it only renders static buffers.

| Method                          | Effect name (per `1033.lan` 369-386)         |
| ------------------------------- | -------------------------------------------- |
| `InitLightStar`                 | Starlight                                    |
| `InitLightRain`                 | Fluttering (rain-like)                       |
| `InitFlowerEffect`              | Colorful Fountain (no-op stub seen; possibly subclassed) |
| `InitBreathEffect`              | Dynamic Breathing                            |
| `InitSpringEffect`              | Rainbow Wave                                 |
| `InitVerticalEffect`            | Following Currents                           |
| `InitRollEffect`                | Peak Revolving                               |
| `InitLightRotate`               | Static On variants                           |
| `SetKeyRGBValue(idx, rgb24, ttl)` | per-key RGB compositor                      |

Each effect runs from a `MTimer` callback every N ms, consults a lookup
table at `DAT_100af***` (hue offsets, key indices), computes new RGB
values, and accumulates into a per-key buffer at `this + 0x1564` (one
`uint32` = ARGB per key, up to ~144 keys). When the buffer changes, the
control calls into DeviceDriver.exe's transport path
(`FUN_00427db0` → `0x20 0x04` upload).

**Implication for our reimplementation**: we re-create the 9 effects in
Qt 6 (FFT-driven music rhythm + effect compositors), then push cooked
RGB. The vendor's lookup tables for `InitLightStar` (key index sequences)
are at `DAT_100af849`, `InitLightRain` at `DAT_100af6f0`,
`InitSpringEffect` at `DAT_100af40c..100af418`, `InitVerticalEffect`
(hard-coded grid offsets), `InitRollEffect` at `DAT_100af348`,
`InitLightRotate` at `DAT_100af278..100af300`. These can be re-derived
from first principles for our impl — no need to copy the data.

The **20 firmware-rendered modes** (`0x13` opcode) are a separate
animation system inside the keyboard's MCU. Don't conflate.

______________________________________________________________________

## 4. Embedded resources (`.rsrc`, 218 KB)

The `.rsrc` section likely holds standard PE resources: bitmaps, icons,
cursors, version info. Without descending into the resource directory
parser there's no clean enumeration, but the size suggests **embedded
window backgrounds, drag-handles, and icons** rather than dialog
templates (those would be smaller). All UI textures appear to be loaded
from `skins/theme1/*.png` instead of from `.rsrc` — confirmed by the
strings in DeviceDriver.exe that reference `skins/theme1/img_*.png`.

The 218 KB `.rsrc` probably contains:

1. **Mui DLL version info** (small)
2. **The toolkit's stock cursor / icon set** — close button, sysmenu
   icons, resize cursors
3. **A few embedded fonts / glyph tables** (Symbol Font for the spinner,
   the LCD preview text-overlay)

**Recommendation**: do NOT depend on these resources. Qt 6 gives us all of
them for free (`QStyleFactory`, `QQuickItem`, system cursors).

______________________________________________________________________

## 5. What this means for our reimplementation

| `mui.dll` element                        | Qt 6 / QML equivalent                                                |
| ---------------------------------------- | -------------------------------------------------------------------- |
| `MObject` / `MControl` virtual dispatch  | Qt `QObject` / `QQuickItem` virtual hooks — free                     |
| `PaintManager` + GDI+ composition        | Qt scene graph (`QSGNode`) — GPU-accelerated, free                   |
| `MShadow` + AlphaBlend                   | QML `DropShadow` / `Glow` from QtQuick.Effects                       |
| `MAirBladder` (tooltip with arrow)       | `ToolTip` (QtQuick.Controls 2.x) with a custom shape via `Canvas`    |
| `CSwitchButton` (iOS toggle)             | `Switch` from QtQuick.Controls (Material style)                      |
| `CColorPalette` / `CirclePalette` / `CColorSelect` | `ColorDialog` (QtQuick.Dialogs) + `Canvas`-drawn HSV wheel  |
| `CGifPicture` (animated GIF playback)    | `AnimatedImage` (QtQuick) — supports GIFs directly                   |
| `CPhotoFrame` (LCD image preview)        | `Image` + a `Rectangle` border                                        |
| `MProgress`                              | `ProgressBar` (QtQuick.Controls)                                     |
| `MRichEdit` (rich-text editor)           | `TextArea` with HTML/Markdown support, or `QQuickWebEngineView`      |
| `MListBox` / `MComboBox` / `MTabCtrl`    | `ListView` / `ComboBox` / `TabBar` (QtQuick.Controls)                |
| `CDeviceList`, `CDeviceCtrl`             | Our existing `DeviceListModel` + QML `DeviceCard` component          |
| `CustomLightMode` (9 host-side effects)  | New `LightingEffectsService` C++ class + QML preview pane            |
| `BatteryCtrl::SetBatteryInfo`            | Our existing `Battery` Q_PROPERTY on the device facade               |
| Asynchronous progress (TFT upload)       | Qt signals (`progress(int)`) on a worker `QThread`                   |
| `MTimer` callbacks                       | `QTimer` / QML `Timer` items                                         |

The conclusion of the v1.0 milestone — "do not adopt the MUI toolkit;
use Qt 6 / QML directly" — is **fully corroborated** by this deeper
analysis. mui.dll is a 1.2 MB DLL that re-implements a subset of what Qt
6 gives us in 30 lines of QML per widget.

______________________________________________________________________

## 6. Code corrections required

None for our `mui.dll` reimplementation — there is no `mui.dll`-derived
code in our tree to correct. **Action items**:

- **Add** a new `LightingEffectsService` in
  `src/devices/keyboard/lighting/` (TBD path; nothing exists yet) that
  hosts the 9 effect compositors documented in §3. Implement
  `InitLightStar` / `InitLightRain` / `InitBreathEffect` etc. as pure
  C++ classes that consume our `LightingMatrix` model and emit cooked
  RGB frames at 60 fps. **No CTest coverage needed for the first
  version** — Gherkin-style snapshot tests against per-effect golden
  frames are sufficient.

- **Add** the 9 effect names to the localization catalog (the QML
  picker must expose them in en/zh/ru). Source strings: 369-386 in
  `language/1033.lan` (already mined).

- **Do NOT** depend on `mui.dll`'s class API. Our backend is HID + Qt
  — we reach the device through `IHidTransport`, never through the
  vendor toolkit.
