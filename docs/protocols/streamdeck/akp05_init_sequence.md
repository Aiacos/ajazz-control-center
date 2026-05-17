# AJAZZ Stream Dock — application initialisation sequence

> Deep RE pass against `Stream Dock AJAZZ.exe` (42 MB) and
> `SDLibrary1.dll` (13 MB), Ghidra 12.1 + full PDB symbol load, 2026-05-17.
> Companion document to [`akp05_vendor.md`](./akp05_vendor.md). All
> findings are clean-room (decompiled pseudocode + symbol names only —
> no vendor source ingested).
>
> **Authoritative artefacts** for this document:
>
> - `C:\Users\unilo\reverse-eng-workdir\sd-app\ghidra_SDLibrary1_dll.json`
>   (call_sites, decompiled `SDDevice` ctor/`openHidDevice`/
>   `sddeviceWinusbInit`/`startListen`, …)
> - `C:\temp\streamdock_exe_strings.txt` (774 312 strings from main exe)
> - `C:\temp\sdlibrary1_strings.txt` (173 602 strings from SDK DLL)

## 1. Process tree at runtime

```
Stream Dock AJAZZ.exe         ← main UI process (Qt 5.15 + CEF 109 widgets)
├── StreamDockWatcher.exe     ← spawned at install time, runs in tray;
│                                 detects USB hot-plug and re-launches main
├── SplashScreen.exe          ← short-lived splash (53 KB)
├── node20.exe (per plugin)   ← JS plugins spawned via QProcess; one node20
│                                 child per loaded *.sdPlugin with
│                                 `Nodejs.Version=20` in manifest
├── *.exe        (per plugin) ← native C/C++ plugins (e.g.
│                                 streamdockSwitchAudio.exe, system
│                                 monitor's SystemMonitor.exe)
├── ScreenCaptureTool.exe     ← spawned on demand for live screen mirror
└── FirmwareUpgradeTool.exe   ← launched only from "Firmware update" UI
                                 action, NOT auto. Bundles libusb-1.0
                                 (the rest of the app does not link libusb).
```

## 2. WinMain → main() → bootstrap order

Per PDB symbol `int __cdecl main(int,char *[])` at offset `760550` in
`streamdock_exe_strings.txt`, the exe is a standard Qt CMake build with
`set(CMAKE_WIN32_EXECUTABLE TRUE)`. Bootstrap order, in execution order
inferred from `QMessageLogger` source-line tags and import deps:

1. **CRT startup** → `WinMainCRTStartup` → `wWinMain` → C++ ctors run
   (no per-cpp globals visible from RE).
1. **`SplashScreen.exe`** is spawned via `QProcess::startDetached`
   immediately after the CRT comes up (string `"SplashScreen.exe"` at
   offset `760552` adjacent to `main`). The splash shows the Stream Dock
   logo while the main process initialises.
1. **`QApplication app(argc, argv)`** — standard Qt construction. Qt
   plugins (`qwindows`, `qjpeg`, `qsvg`, `qgif`, `qico`, `qwbmp`,
   `qwebp`, `qtga`, `qtiff`, `qicns`) auto-load from
   `<install>/platforms/`, `<install>/imageformats/`. Audio: WASAPI
   plus DirectSound + WMF (`qtaudio_wasapi.dll`, `dsengine.dll`,
   `wmfengine.dll`).
1. **Single-instance check**: `QLocalServer::listen("HotSpot_SD_Singleton_<userId>")`.
   If listening fails, the binary calls `QLocalSocket::connect()` to the
   existing instance and forwards `argv` via the socket, then exits. The
   exact socket name is built from username + a fixed prefix; not
   resolved in this pass. (`QLocalServer` symbols are imported, see
   `streamdock_exe_strings.txt:769551..769564`.) This is **NOT** a Win32
   named mutex, so a system administrator account can run a parallel
   instance for a non-admin user.
1. **`QSettings` organisation/application name** — set to
   `("HotSpot", "Stream Dock AJAZZ")`. Registry root resolves to
   `HKCU\Software\HotSpot\StreamDock\` (verified by string
   `Software\HotSpot\StreamDock\%1` at offset `758154` and
   `HKEY_CURRENT_USER\Software\HotSpot\StreamDock\%1\` at offset
   `758247`).
1. **`LanguageUtilities::initLanguage(QString)`** — loads locale JSON
   from `<install>/translations/`. Languages shipped: `de fr it es pt ja zh_CN zh_HK ru ar ko pl` (`streamdock_exe_strings.txt:760673..760685`).
   Default `en.json` is the fallback.
1. **`QtKeychain` init** — instantiated lazily on first secret read; not
   part of bootstrap.
1. **`SDLibrary1.dll`** loaded by delay-binding (its imports appear in
   the EXE import table). On load, `SDLibrary1`'s `DllMain` initialises
   no global state — the `SDDevice` ctor is per-device.
1. **`QCefContext::QCefContext(QCoreApplication*, int, char**, QCefConfig*)`**
   constructed (`streamdock_exe_strings.txt:767777`). This is the CEF
   bootstrap — it spawns the CEF browser process (`QCefView.dll` →
   embedded `cef_binary_109.1.18+gf1c41e4+chromium-109.0.5414.120`).
   CEF must be initialised **before** any `QCefView` widget is shown.
1. **Main window construction** (`QMainWindow`). At this point the
   splash is still up; the main window is `show()`n after the device
   enumeration kicks off (step 12).
1. **Plugin manager spawn loop** (`SDPluginManager::loadPlugin` /
   `LoadPrivatePlugin`) — iterates `<install>/defaultData/defaultPlugins/`
   and `%APPDATA%/HotSpot/Stream Dock AJAZZ/installedPlugins/`, parses
   each `manifest.json`, then spawns the plugin process. See
   `akp_plugin_sdk.md` for the per-plugin lifecycle.
1. **`SDPluginServer::startListen(port)`** — binds the
   Elgato-compatible WebSocket server on `QHostAddress::Any` (NOT
   `127.0.0.1` — see §5 below). Port comes from a random pool;
   retries every 500 ms if the chosen port is in use
   (`Utilities::getsARandomServerPort()` retry loop in
   `SDTcpServer::startListen` / `SDWebsocketServer::startListen`
   pseudocode).
1. **Device enumeration**: not auto-started by `SDLibrary1`. The exe
   calls `hid_enumerate(vid, 0)` for each known VID, then for every
   matching device constructs an `SDDevice` (or `SDDeviceWinUSB`)
   subclass, calls `openHidDevice(vid, pid, serial)`, which in turn
   starts the read/write threads.
1. **First device handshake** — see §3.
1. **Splash dismissed**, main window `show()`, `app.exec()` enters the
   Qt event loop.

Total cold-start budget observed in the RE log strings:
`Run a.exec().` (offset `760556`) — the splash is kept alive until this
log line fires.

## 3. Per-device open & handshake sequence

### 3.1 hidapi-backed devices (`SDDevice` and `SDGeneralDevice`)

From `SDDevice::openHidDevice(int vid, int pid, QString &serial)`
decompilation at RVA `0x1f370` (see `ghidra_SDLibrary1_dll.json`,
function `openHidDevice`):

```
QMutex::lock(&this->_deviceHandleMutex);
if (this->_isDeviceOpen) return false;        // re-entrant guard

hid_init();
// Branch on whether caller supplied a serial number
if (serial.isEmpty()) {
    this->_handle = hid_open(vid, pid, NULL);
} else {
    this->_handle = hid_open(vid, pid, serial.toStdWString());
}

if (!this->_handle) {
    hid_exit();
    log("Open device: unable to open device, error = %s",
        hid_error(NULL));
    return false;
}

this->_vid = vid;
this->_pid = pid;
this->_usagePage = -1;       // overload-0 (no interface/collection)
this->_usage     = -1;
// vtable slot 0x110: SDGeneralDevice::onHidOpen (probably)
(this->vptr[0x110/8])(this);

// Log all hidapi metadata for diagnostics:
hid_get_manufacturer_string(this->_handle, buf, 256);
hid_get_product_string     (this->_handle, buf, 256);
hid_get_serial_number_string(this->_handle, buf, 256);
hid_get_indexed_string     (this->_handle, 1, buf);
hid_get_output_report_lenght(this->_handle);   // typo "lenght" in vendor src
hid_get_input_report_lenght (this->_handle);
hid_get_feature_report_lenght(this->_handle);
```

There is also a 6-argument overload at RVA `0x20200` that takes
`(int vid, int pid, QString &serial, int interface, int collection, uchar reportId)` and uses `hid_enumerate` + `hid_open_path` to bind to
a specific HID interface / collection (necessary for the V2-protocol
devices that expose multiple HID interfaces).

### 3.2 First commands sent on device open

After `openHidDevice` returns true, the vendor's open path calls (in
the order seen in disassembly):

1. **`startHidReadThread()`** — spawns `SDGeneralReadThread`
   (`hid_read_timeout` loop, see RVA `0x180021280` in
   `readDataFromHidDevice`).
1. **`startHidWriteThread()`** — spawns `SDGeneralWriteThread` which
   blocks on `_writeDataWaitCondition`.
1. **`sendGetHardwareFirmwareVersion()`** (RVA `0x180023440`) — emits
   a single `CRT VER` packet:
   ```
   [0]  = _deviceReportId   (per-device; commonly 0x02 for AKP05)
   [1]  = 'C'
   [2]  = 'R'
   [3]  = 'T'
   [4]  = 0x00
   [5]  = 0x00
   [6]  = 'V'
   [7]  = 'E'
   [8]  = 'R'
   [9..N] = 0x00
   ```
   Then the device responds with an input report whose payload carries
   a string like `"V1.293.…"` (verified literal at
   `sdlibrary1_strings.txt:25014`). The string is decoded via
   `QString::fromAscii_helper` and cached in `_firmwareVersion`.
1. **`isOld293Version()`** (RVA `0x18001ed80`) — only for AKP153
   family. Sends a `hid_get_input_report` (NOT a packet — a feature
   report at the HID protocol level) with the device report ID and
   inspects whether the device returns a non-empty string. Used to
   gate "old 293 firmware" behaviour throughout `SDDevice`.
1. **No clear-display / no reset packet** is emitted at open time. The
   vendor relies on the device firmware to preserve its last on-screen
   image; the first thing the host pushes after VER is the user's
   profile (via `SDDevice::insertSettings`).

### 3.3 WinUSB-backed devices (`SDDeviceWinUSB`)

For higher-bandwidth devices the vendor uses WinUSB instead of hidapi.
From `SDDeviceWinUSB::openHidDevice(int vid, int pid, QString&)` at
RVA `0x14ae50`:

```
// Build the Windows USB device path:
// "USB\\VID_<hex>&PID_<hex>\\<serial>"
local_50 = QString("USB\\VID_") + QString("%1").arg(vid, 4, 16, '0')
         + "&PID_" + QString("%1").arg(pid, 4, 16, '0') + "\\..."
openDevice(this, &GUID, local_50);
```

`SDDeviceWinUSB::openDevice` at RVA `0x14a530` does:

1. `SetupDiGetClassDevsW(GUID, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE)`
1. `SetupDiEnumDeviceInterfaces` until the matching path is found.
1. `CreateFileW(devicePath, GENERIC_READ|GENERIC_WRITE, …, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, …)`
1. `WinUsb_Initialize(handle, &winusbInterfaceHandle)`
1. `WinUsb_QueryDeviceInformation` for descriptor metadata.
1. `WinUsb_GetDescriptor(USB_DEVICE_DESCRIPTOR_TYPE)` then walks the
   interface/endpoint descriptors and **stores `_pipeControl`,
   `_pipeIn`, `_pipeOut` with their EndpointAddress, MaximumPacketSize,
   Interval, PipeType** — the endpoints are discovered, not hardcoded.
1. `sddeviceWinusbInit()` (RVA `0x14b300`) — spawns two
   `QThread::createThreadImpl` workers:
   - `_writeDataThread` running a lambda that drains `_writeDataList`
     via `WinUsb_WritePipe` to `_pipeOut`.
   - `_readDataThread` running a lambda that polls `WinUsb_ReadPipe`
     from `_pipeIn` with a small timeout.
1. The first command emitted on WinUSB devices is **also** `CRT VER`
   — same opcode, but framed without the report-ID byte (see
   §3.4 framing differences).

### 3.4 Critical framing difference: hidapi vs WinUSB

**This contradicts §2 of `akp05_vendor.md`, which we leave in place
for the hidapi case but is wrong for WinUSB.** Per the decompiled
`SDWinUSB::sendLogoSizeCommand` at RVA `0x180147be0` and
`SDWinUSB::getFinishCommand` at RVA `0x180146d20`:

| Position         | hidapi (`SDDevice`)        | WinUSB (`SDWinUSB`)          |
| ---------------- | -------------------------- | ---------------------------- |
| byte 0           | `_deviceReportId` (1 byte) | first byte of opcode (`'C'`) |
| byte 1..3 / 0..2 | `'C','R','T'`              | `'C','R','T'`                |
| reserved gap     | bytes 4..5 = 0x00          | bytes 3..4 = 0x00            |
| 3-byte opcode    | bytes 6..8                 | bytes 5..7                   |
| payload start    | byte 9                     | byte 8                       |
| total packet len | `_packetSize + 1`          | `_pipeOut.MaximumPacketSize` |

So a WinUSB-class device gets:

```
[0]  'C'
[1]  'R'
[2]  'T'
[3]  0x00
[4]  0x00
[5]  opcode byte 0
[6]  opcode byte 1
[7]  opcode byte 2
[8..N]  payload
```

i.e. **one byte further left** than the hidapi framing, because there
is no report-ID byte. WinUSB transfer size is **dynamic** — it tracks
the endpoint's `wMaxPacketSize` descriptor field (typically `0x0200`
for USB-2 bulk endpoints or `0x0400` for the touch-strip endpoint).

### 3.5 Periodic state

After open, `SDDevice` ctor sets up two QObject timers:

- **`_screenOffTimerID`** — fires when the user-configured screen-off
  time elapses; sends a `QUCMD` to dim the LCD.
- **`_heartbeatTimerID`** — if `_enableHeartbeat == true`, fires every
  ~20 ms (per the `TimerTool::setTimerInterval(20)` line in the
  `SDDevice` ctor decompilation) to keep the write-pump alive.

There is no firmware-level keep-alive ping observed — the heartbeat
timer just nudges the write queue.

## 4. Settings / persistence load order

Per registry strings (`HKCU\Software\HotSpot\StreamDock\...`) and
SQLite imports:

1. **App-level prefs** read from registry at startup:
   - `StreamDockLanguage/language`
   - `StreamDockIconMode/LargeIcon`
   - `RotationAngle/angle`
   - `ScreenOffTime/time`
   - `Record/SoftVersion` — last-seen version string for migration
     detection.
   - `StreamDockSkipVersionInfo/isSkipThisVersion` and
     `/SkipVersion` — version the user clicked "skip update".
   - `StreamDockLoadingState/LoadingState` — last-crash recovery flag.
   - `StreamDockLoadingErrorInfo/ErrorInfo` — last crash payload.
1. **Per-device prefs** read on device-open under
   `HKCU\Software\HotSpot\StreamDock\<deviceCodename>\`:
   - `<codename>/SerialNumber` — last known serial.
   - `StreamDockCurrentDevice/Device` — currently-selected device.
   - `MBoxN4/enableTouchSlide`, `MBoxN4/enableTouchClick` — touch-strip
     prefs for Mirabox N4.
   - `MBoxN4+/SlideSwitchMode`, `N4Pro/lightControlMode`,
     `N4Pro/enableVibration` — N4 Pro extras.
   - `<codename>/<deviceId>/ActionState` — JSON blob of action
     bindings per slot.
1. **SQLite profiles** opened via the embedded SQLite engine
   (`SQLite format 3` header at offset `25326` in
   `sdlibrary1_strings.txt`). Schema includes a `Profile` table per
   device codename (`INSERT INTO "%1" (Profile) VALUES ("%2")` at
   offset `760694` in the exe strings). One DB per user under
   `%APPDATA%\HotSpot\Stream Dock AJAZZ\`.
1. **Factory-default profiles** loaded from
   `<install>\defaultData\defaultProfiles\<codename>\<locale>\*.streamDockProfile`
   if the user's DB does not contain a profile for this device. Profile
   files are JSON-in-binary wrappers (Qt `QByteArray` saved verbatim).
1. **OAuth tokens & passwords** read from QtKeychain on demand
   (`KeyChainClass` wrapper). Backends:
   - Windows: Credential Manager (`wincred.dll`)
   - macOS: Keychain Services
   - Linux: libsecret (kwallet / GNOME Keyring)

## 5. Network listener binding posture

**Important security finding**: both the WebSocket and TCP servers
bind to `QHostAddress::Any` — i.e. **all interfaces**, including the
LAN-facing NIC, NOT `127.0.0.1`. Per
`SDTcpServer::startListen` decompilation at RVA `0x1800360c0` and
`SDWebsocketServer::startListen` at RVA `0x1800379a0`:

```cpp
QHostAddress addr(QHostAddress::Any);   // <-- all-interfaces!
this->sdTcpServer->listen(addr, port);
```

However:

1. The browser-side `connectElgatoStreamDeckSocket` JavaScript shim
   in `defaultData/defaultPlugins/com.hotspot.streamdock.memo.sdPlugin/static/action.js`
   hardcodes `ws://127.0.0.1:<port>` — so the *intended* attack
   surface is local-only.
1. The protocol carries an **`authentication` challenge-response**:
   `Utilities::generateSalt()` produces a random salt at listen time,
   and `Utilities::generateSecret(salt, password)` produces the
   expected secret. The first message a client must send (after the
   raw WebSocket upgrade) is an `authentication` event with `{event: "authentication", challenge: <hash>}`.

The use of `QHostAddress::Any` is a vendor footgun we MUST NOT
replicate — see [`akp_plugin_sdk.md`](./akp_plugin_sdk.md) for the
recommended fix in our own implementation.

## 6. Crash recovery / state restoration

- On startup, the exe reads `StreamDockLoadingState/LoadingState` from
  the registry. If it equals `"loading"` (last run did not reach
  `app.exec()` cleanly), it appends a notification card to the in-app
  notification panel and forces a clean state on the loaded profile.
- `StreamDockLoadingErrorInfo/ErrorInfo` carries the JSON crash
  payload (Qt thread name, last source line tag) for the next-run
  diagnostic notification.
- There is **no Windows WER (Windows Error Reporting) opt-in**, no
  Sentry/Crashlytics. Crashes are purely local. Confirms the
  retrospective finding "no telemetry" in `akp05_vendor.md` §8.

## 7. Migration logic

- Version-record key `Record/SoftVersion` (offset `760590`) — on each
  start, compared against the running binary's version string. If
  different and the major.minor differs, the exe runs a migration
  pass:
  - Re-keys profile rows that used the legacy codename (e.g.
    `MBox-N3 ` with trailing space at offset `25235` had been
    written by an earlier version that didn't trim).
  - Migrates `defaultData/defaultPlugins/defaultPlugins.zip` into the
    user's `installedPlugins/` if `installedPlugins/` is empty.
  - Re-writes the `_libcef` runtime version key
    (`firmware-version-check_libcef.json` per `akp05_vendor.md` §4.3)
    so that the next launch knows whether the CEF runtime needs an
    update.

The `ResetDataState/resetData` registry key (offset `760626`) lets
the user force a one-shot reset on next launch from the UI.

## 8. Single-instance enforcement

As noted in §2 step 4: `QLocalServer::listen()` is the mechanism. If
the server name is already taken:

```cpp
QLocalSocket sock;
sock.connectToServer(serverName);
if (sock.waitForConnected(200)) {
    sock.write(QString(argv[1..]).toUtf8());
    sock.waitForBytesWritten(1000);
    sock.disconnectFromServer();
    return 0;   // hand off to the running instance and exit
}
```

The running instance's `QLocalServer::newConnection()` handler reads
the forwarded argv and (e.g.) opens the requested device window, then
emits a `quit()` no-op so the local socket closes cleanly.

`QLocalServer` symbols proven imported by `streamdock_exe_strings.txt`
lines `769551..769564`.

## 9. Plugin-host process spawn

Source: decompilation of `SDPluginManager::loadPlugin` /
`LoadPrivatePlugin` cross-referenced with strings at offsets
`759001..759032`. For each plugin loaded from
`<install>/defaultData/defaultPlugins/<uuid>.sdPlugin/`:

1. Parse `manifest.json`. Reject if `OS` array does not contain the
   current platform.
1. Resolve `CodePath` / `CodePathWin` / `CodePathMac`. Extension is
   either `.exe` (native), `.mjs` / `.cjs` / `.js` (Node.js), or
   `.html` (CEF in-process).
1. **Native plugins**: spawn directly via `QProcess::start(path, args)`.
   If `RunAsAdministrator: true` in the manifest, the spawn is wrapped
   in a UAC elevation request (Windows only).
1. **Node.js plugins**: spawn `<install>/node/node20.exe` with args
   `<plugin>/plugin/index.js -port <port> -pluginUUID <uuid> -registerEvent registerPlugin -info <json>` — these are the exact
   four CLI flags per strings at `759010..759013`. The
   `connectElgatoStreamDeckSocket('<port>','<uuid>','registerPlugin','<info>')`
   shim in the plugin's bundled JS then opens the WebSocket connection
   back to the host.
1. **`.html` plugins**: loaded directly in a `QCefView` inside the
   main process — no subprocess. The JS bridges to the host via
   `QCefQuery` (see [`akp_plugin_sdk.md`](./akp_plugin_sdk.md) §4.3).
1. `QProcess::ProcessError` handler logs `The plugin '%1' crashed: %2`
   and increments a per-plugin restart counter; after 3 crashes the
   plugin is disabled with a notification.

The vendor also tries an AJAZZ-specific shim
`connectMiraBoxSDSocket('%1','%2','%3','%4')` (string at offset
`759005`) — same signature as the Elgato call, but the SDK fallback
chain prefers the Elgato name if both are exported by the plugin
script. This is purely a renamed alias for compatibility with
Mirabox-developer plugins that wanted to feel like a separate API.

## 10. Code corrections required

| File                                                                                | Lines / symbol                | Change                                                                                                                                                                                 | Breaking? | Tests needed                                                              |
| ----------------------------------------------------------------------------------- | ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ------------------------------------------------------------------------- |
| `src/devices/streamdeck/src/akp05.cpp` (`Akp05Device::open`)                        | search for `firmwareVersion(` | After `transport.open()`, emit `buildVersionRequest()` and read the response report; cache in a new `std::string Akp05Device::_firmwareVersion`. Return that from `firmwareVersion()`. | additive  | New `Akp05ProtocolTest::open_emitsVerRequest_andCachesResponse`           |
| `src/devices/streamdeck/src/akp03.cpp` (`Akp03Device::open`)                        | search for `firmwareVersion(` | Same as above. The opcode is identical across the AKP family.                                                                                                                          | additive  | `Akp03ProtocolTest::open_emitsVerRequest_andCachesResponse`               |
| `src/devices/streamdeck/src/akp153.cpp`                                             | `Akp153Device::open`          | Same as above, but also add an `isOld293Version()` helper that issues `hid_get_input_report` (Linux: `HIDIOCGFEATURE`) and gates the V1-protocol path for legacy AKP153 firmware.      | additive  | New `Akp153OldFirmwareTest::detectsOld293Version_branchesProtocol`        |
| `src/host/main/src/main.cpp` (does not exist yet)                                   | new file                      | Implement Qt 6 single-instance via `QSharedMemory` *plus* `QLocalServer` for argv handoff. Reject `--allow-network-host` by default (force `127.0.0.1` listen).                        | additive  | `SingleInstanceTest::secondInstance_handsArgvAndExits`                    |
| `src/host/persistence/src/registry_paths.hpp` (does not exist yet)                  | new file                      | Encode the registry key map from §4. Read on startup; per-device prefs gated behind a feature flag (we do not ship the same registry path as the vendor).                              | additive  | `RegistryPathTest::resolvesPerDevicePath_caseAkp05`                       |
| `src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp` (`IDeviceCapable`) | end of file                   | Add `virtual std::optional<std::string> firmwareVersion() const = 0;` as a non-default. Today the base trait defaults to `"unknown"`; remove the default so all backends opt in.       | breaking  | Existing backends already overriding `firmwareVersion()` — no test churn. |
| `src/devices/streamdeck/src/akp05_protocol.cpp`                                     | `buildVersionRequest()` body  | Verify it emits exactly the 9-byte prefix `CRT\0\0VER` followed by `0x00` padding to 512 bytes. Already correct per akp05_protocol.hpp §99-101 review; add a byte-exact unit test.     | additive  | `Akp05ProtocolBuilderTest::version_isExactly_CRT_VER_zeropadded`          |

**Test coverage**: targeted Catch2 cases as listed above; estimated
~120 new lines of test code, ~80 new lines of production code (mostly
firmware-version parse logic and registry-path resolution).
