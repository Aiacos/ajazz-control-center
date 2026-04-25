# Documentation Style Guide

AJAZZ Control Center follows the [Google C++ Style
Guide](https://google.github.io/styleguide/cppguide.html#Comments) for all
in-source documentation. The conventions below apply to C++, Python, and QML.

## C++

Use Doxygen Javadoc-style block comments (`/** ... */` or `///`) with `@`-prefixed
tags. Document **what** and **why**, not **how** — the code already shows the
how.

### File header

Every source and header file begins with the SPDX identifier followed by a
short Doxygen `@file` block:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file capabilities.hpp
 * @brief Capability mix-in interfaces advertised by device backends.
 *
 * Backends implement IDevice plus any subset of the capability interfaces
 * declared here. The UI and Python SDK probe capabilities at runtime via
 * the bitset returned by IDevice::capabilities().
 */
```

### Classes and structs

Briefly describe what the type models, mention thread-safety, and link related
types with `@see`.

```cpp
/**
 * @brief Mix-in for devices that expose per-key or main displays.
 *
 * Implementations are responsible for any resizing, color-space, and
 * encoding conversions required by the underlying USB protocol.
 *
 * @note Thread-affine: must be used from the device's I/O thread.
 * @see IRgbCapable
 */
class IDisplayCapable { ... };
```

### Member functions

Always document parameters, return values, side effects, and failure modes.

```cpp
/**
 * @brief Push an RGBA8 image to a key slot.
 *
 * @param keyIndex Zero-based key index in the device's display grid.
 * @param rgba     Tightly packed RGBA8 pixels, length == width * height * 4.
 * @param width    Source image width in pixels.
 * @param height   Source image height in pixels.
 *
 * @throws std::system_error if the underlying transport fails.
 * @pre rgba.size() == width * height * 4u
 */
virtual void setKeyImage(std::uint8_t keyIndex,
                         std::span<std::uint8_t const> rgba,
                         std::uint16_t width,
                         std::uint16_t height) = 0;
```

### Enums and constants

Brief on the enum, single-line `///<` trailing comment per value.

```cpp
/// Supported predefined RGB animations.
enum class RgbEffect : std::uint8_t {
    Static = 0,      ///< Solid color, no animation.
    Breathing,       ///< Smooth fade between two intensities.
    Wave,            ///< Sweeping color wave across the zone.
};
```

### Inline single-line comments

Use `//` (not `///`) for non-documentation comments inside function bodies. Aim
for one comment per *intent*, not one per line.

## Python

Follow [Google docstring
style](https://google.github.io/styleguide/pyguide.html#38-comments-and-docstrings).
Module docstrings come first, then classes, then functions.

```python
"""Hello-world plugin example.

This module demonstrates the minimum surface a plugin must expose to
register a key action with the host.
"""

def on_key_press(device, key_index: int) -> None:
    """Handle a key-press event.

    Args:
      device: The IDevice that emitted the event.
      key_index: Zero-based key index reported by the protocol.

    Returns:
      None. The host ignores the return value.

    Raises:
      RuntimeError: if the device transport has been closed.
    """
```

## QML

Use `//` line comments above each component or property block. Mirror the C++
file header SPDX line.

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sidebar showing the list of connected and previously-seen devices.
// Emits `deviceSelected(codename)` when the user clicks a row.

Rectangle {
    /// Hex color of the row when hovered.
    readonly property color hoverColor: "#2c2c34"
    ...
}
```

## CMake

Comments above every non-trivial block or function call. Use `# ----` banners
to separate logical sections.

## What NOT to comment

- Self-evident accessors (`/** Returns the value. */ int value() const;`).
- Restating the type name (`/** The Foo. */ Foo foo;`).
- TODO / FIXME without a tracking issue — open one and reference it.
