# Adding a Device

This guide walks through the process of adding support for a new AJAZZ or Mirabox device. It assumes you've read [`../protocols/REVERSE_ENGINEERING.md`](../protocols/REVERSE_ENGINEERING.md).

## 1. Identify the device

```bash
lsusb -v                        # Linux
system_profiler SPUSBDataType   # macOS
# Windows: Device Manager → Hardware Ids
```

Note the USB **Vendor ID** and **Product ID**. Cross-reference with existing entries in `src/devices/*/src/register.cpp` to avoid duplicates.

## 2. Pick the right family

| If the device has…                 | Put it under            |
|------------------------------------|-------------------------|
| LCD keys and/or dials              | `src/devices/streamdeck`|
| Alphanumeric keys and RGB matrix   | `src/devices/keyboard`  |
| DPI stages, scroll wheel, buttons  | `src/devices/mouse`     |

## 3. Capture traffic

Follow the procedure in `REVERSE_ENGINEERING.md`. Save the annotated decode as `docs/protocols/<family>/<codename>.md`.

## 4. Write the protocol module

Create `src/devices/<family>/src/<codename>_protocol.hpp` and `_protocol.cpp`. Keep these files free of I/O — they only build/parse byte buffers. Example structure:

```cpp
namespace ajazz::streamdeck::mymodel {
    inline constexpr std::uint16_t VendorId  = 0x0300;
    inline constexpr std::uint16_t ProductId = 0x1234;
    std::array<std::uint8_t, 512> buildSetBrightness(std::uint8_t pct);
    std::optional<KeyEvent>       parseInputReport(std::span<std::uint8_t const>);
}
```

## 5. Write unit tests

Mirror every public helper in `tests/unit/test_<codename>_protocol.cpp`:

- Check the exact byte offsets against the spec.
- Test edge cases: clamping, malformed input, unknown command.

## 6. Implement the backend

Create `src/devices/<family>/src/<codename>.cpp` and a factory `makeMyModel` exposed from `<family>.hpp`. Implement `IDevice` plus any applicable capability mix-ins.

## 7. Register it

Extend `src/devices/<family>/src/register.cpp`:

```cpp
reg.registerDevice(
    core::DeviceDescriptor{
        .vendorId  = mymodel::VendorId,
        .productId = mymodel::ProductId,
        .family    = core::DeviceFamily::StreamDeck,
        .model     = "AJAZZ MyModel",
        .codename  = "mymodel",
    },
    &makeMyModel);
```

## 8. Integration tests

Drop a sanitized fragment of your capture into `tests/integration/fixtures/<codename>/` and add a replay test in `tests/integration/`.

## 9. Update documentation

- Add a row to the support matrix in `README.md`.
- Add an entry to `CHANGELOG.md` under "Unreleased".
- If the device needs special udev rules, extend `resources/linux/99-ajazz.rules`.

## 10. Open the PR

Follow the checklist in `.github/pull_request_template.md` and request a review.
