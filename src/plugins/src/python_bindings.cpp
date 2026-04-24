// SPDX-License-Identifier: GPL-3.0-or-later
//
// `ajazz` Python runtime module exposed inside the embedded interpreter.
// Plugin code imports it with `from ajazz import Plugin, action`.
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
