// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file python_bindings.cpp
 * @brief Defines the @c ajazz Python runtime module for the embedded interpreter.
 *
 * This translation unit is linked into the host binary and registers the
 * @c ajazz module via @c PYBIND11_EMBEDDED_MODULE so that plugin code can
 * write @c from @c ajazz @c import @c Plugin, @c action without any
 * separate shared library.
 *
 * Exported symbols:
 * - @c ajazz.Rgb          — three-component RGB colour struct.
 * - @c ajazz.DeviceFamily — enum mirroring ajazz::core::DeviceFamily.
 */
//
#include "ajazz/core/capabilities.hpp"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(ajazz, m) {
    m.doc() = "AJAZZ Control Center plugin runtime.";

    py::class_<ajazz::core::Rgb>(m, "Rgb")
        .def(py::init<std::uint8_t, std::uint8_t, std::uint8_t>(),
             py::arg("r") = 0,
             py::arg("g") = 0,
             py::arg("b") = 0)
        .def_readwrite("r", &ajazz::core::Rgb::r)
        .def_readwrite("g", &ajazz::core::Rgb::g)
        .def_readwrite("b", &ajazz::core::Rgb::b);

    py::enum_<ajazz::core::DeviceFamily>(m, "DeviceFamily")
        .value("Unknown", ajazz::core::DeviceFamily::Unknown)
        .value("StreamDeck", ajazz::core::DeviceFamily::StreamDeck)
        .value("Keyboard", ajazz::core::DeviceFamily::Keyboard)
        .value("Mouse", ajazz::core::DeviceFamily::Mouse);

    // Plugins write pure Python subclasses that inherit from this base.
    m.def("_noop", []() {});
}
