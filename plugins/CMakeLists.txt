# Bundled first-party plugins (Phase 6 of docs/architecture/NATIVE-HARDWARE-MONITOR.md).
#
# Builds the ajazz-sysmon monitor plugin (Rust, cargo) + its btop-metrics-helper
# (C++, own CMake tree) via the existing build-bundle.sh and installs the
# assembled `com.ajazz.sysmon2.sdPlugin` into
#   ${CMAKE_INSTALL_DATADIR}/ajazz-control-center/bundled-plugins/
# so .deb/.rpm/.flatpak payloads carry it. The app seeds bundled plugins into
# the user plugins dir on first run (seeding lives in the app's plugin catalog
# launch-sweep; contract: this exact install path).
#
# Linux host-target only for now — the cross-target matrix (macOS/Windows
# helper backends) is a later phase; on other hosts this is a silent no-op.

find_program(AJAZZ_CARGO cargo)

if(NOT (UNIX AND NOT APPLE))
    message(STATUS "bundled-plugins: non-Linux host — skipping ajazz-sysmon bundle")
    return()
endif()
if(NOT AJAZZ_CARGO)
    message(STATUS "bundled-plugins: cargo not found — skipping ajazz-sysmon bundle")
    return()
endif()
# CI checks out without submodules; the metrics-helper hard-errors when the
# btop tree is absent, which failed the whole ubuntu leg (develop 2026-07-02).
# Degrade to a skip like the cargo/host guards above — packaging jobs that must
# ship the bundle check out with `submodules: recursive`.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ajazz-sysmon/metrics-helper/third_party/btop/src/btop_shared.cpp")
    message(
        WARNING
            "bundled-plugins: btop submodule missing — skipping ajazz-sysmon bundle. "
            "Run: git submodule update --init --recursive"
    )
    return()
endif()

set(_sysmon_src "${CMAKE_CURRENT_SOURCE_DIR}/ajazz-sysmon")
set(_sysmon_out "${CMAKE_BINARY_DIR}/bundled-plugins")
set(_sysmon_bundle "${_sysmon_out}/com.ajazz.sysmon2.sdPlugin")

# build-bundle.sh is incremental underneath (cargo + ninja); the stamp only gates re-running it when
# the plugin sources change.
file(
    GLOB_RECURSE
    _sysmon_deps
    CONFIGURE_DEPENDS
    "${_sysmon_src}/plugin/src/*.rs"
    "${_sysmon_src}/plugin/Cargo.toml"
    "${_sysmon_src}/plugin/manifest.json"
    "${_sysmon_src}/plugin/property-inspector.html"
    "${_sysmon_src}/metrics-helper/src/*"
    "${_sysmon_src}/metrics-helper/CMakeLists.txt"
)

add_custom_command(
    OUTPUT "${_sysmon_bundle}/manifest.json"
    COMMAND "${_sysmon_src}/../ajazz-sysmon/build-bundle.sh" "${_sysmon_out}"
    DEPENDS ${_sysmon_deps} "${_sysmon_src}/build-bundle.sh"
    COMMENT "Assembling bundled plugin com.ajazz.sysmon2.sdPlugin"
    VERBATIM
)

add_custom_target(
    ajazz_sysmon_bundle ALL DEPENDS "${_sysmon_bundle}/manifest.json"
    COMMENT "Bundled plugin com.ajazz.sysmon2.sdPlugin is up to date"
)

include(GNUInstallDirs)
install(DIRECTORY "${_sysmon_bundle}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ajazz-control-center/bundled-plugins"
        USE_SOURCE_PERMISSIONS
)
