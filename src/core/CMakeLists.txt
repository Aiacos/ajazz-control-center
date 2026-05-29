add_library(
    ajazz_core STATIC
    src/device_registry.cpp
    src/hid_transport.cpp
    src/event_bus.cpp
    src/profile.cpp
    src/profile_io.cpp
    src/profile_bundle.cpp
    src/action_engine.cpp
    src/executor.cpp
    src/macro_recorder.cpp
    src/notification_service.cpp
    src/logger.cpp
    # Debug epic Phase 1: reusable LogSink building blocks (fan-out tee,
    # bounded queryable ring buffer, append-to-file). No Qt / no JSON, so
    # they stay inside the COD-031 boundary and back the out-of-process
    # debug log stream wired up in the app tier.
    src/log_sinks.cpp
    src/hotplug_monitor.cpp
    # Phase 21-01: IInputSynthesizer — always-compiled stub (AJAZZ_FEATURE_INPUT_SYNTH OFF by
    # default keeps the gating test hardware-free). The OS TUs are added unconditionally below
    # so they participate in every build; each TU is self-empty unless both
    # AJAZZ_FEATURE_INPUT_SYNTH and the platform #if are satisfied.
    src/input_synthesizer_stub.cpp
    src/input_synthesizer_linux.cpp
    src/input_synthesizer_win.cpp
    src/input_synthesizer_mac.cpp
    # Phase 21-03: BuiltinActionRegistry — pure-core UUID->handler dispatch table (no Qt, no
    # nlohmann; COD-031). The app tier (BuiltinActionsService) populates it with handlers for the
    # ~24 com.hotspot.streamdock.* built-in UUIDs; the plugin executor short-circuits to it BEFORE
    # the Phase-19 bridge path. settingsJson is passed verbatim (parsed app-tier).
    src/builtin_action_registry.cpp
)

add_library(ajazz::core ALIAS ajazz_core)

target_include_directories(
    ajazz_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Public headers in include/ajazz/core/ do not include any Qt header, so Qt6::Core is a PRIVATE
# implementation dependency only (COD-031). Consumers that need Qt (e.g. the GUI app, the streamdeck
# library) link Qt6::Core themselves.
target_link_libraries(ajazz_core PRIVATE Qt6::Core ajazz::warnings ajazz::sanitizers hidapi::hidapi)

# ----------------------------------------------------------------------------
# HotplugMonitor needs OS-specific libraries for USB-arrival notifications.
# ----------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
    target_link_libraries(ajazz_core PRIVATE PkgConfig::UDEV)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(ajazz_core PRIVATE "-framework CoreFoundation" "-framework IOKit")
elseif(WIN32)
    # RegisterDeviceNotificationW lives in user32.lib, which MSVC links by default; explicit
    # just-in-case for Clang-cl / MinGW.
    target_link_libraries(ajazz_core PRIVATE user32)
endif()

set_target_properties(ajazz_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ----------------------------------------------------------------------------
# Phase 21-01: AJAZZ_FEATURE_INPUT_SYNTH — native OS input synthesis backends. When ON, the per-OS
# TUs (Linux uinput / Windows SendInput / macOS CGEvent) are active and
# makePlatformInputSynthesizer() is defined. Default OFF so the gating test stays hardware-free (no
# /dev/uinput, no OS permissions needed). COD-031: no Qt added to the core target for this feature.
# ----------------------------------------------------------------------------
if(AJAZZ_FEATURE_INPUT_SYNTH)
    target_compile_definitions(ajazz_core PRIVATE AJAZZ_FEATURE_INPUT_SYNTH=1)
    message(STATUS "AJAZZ_FEATURE_INPUT_SYNTH ON — native OS input synthesis backends enabled")
endif()
