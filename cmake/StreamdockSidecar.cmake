# SPDX-License-Identifier: GPL-3.0-or-later
#
# StreamdockSidecar.cmake — build + bundle the out-of-process mirajazz Rust
# sidecar (`streamdock-host/`) alongside the app.
#
# The AKP03 / AKP05-N4 / AKP153 Stream Dock families are driven by this sidecar
# (experiment/mirajazz). At runtime `SidecarStreamDockDevice` resolves the
# binary via QCoreApplication::applicationDirPath() + "/streamdock-host" (then
# PATH), so we (a) stage the cargo-built binary next to the app binary in the
# build tree and (b) install it beside the app for every package generator.
#
# Cargo fetches the `mirajazz` crate from git, so a network connection is needed
# at configure/build time on a clean checkout (CI has it; Flatpak builds offline
# from a vendored cargo-sources manifest — see packaging/flatpak/README).
#
# Gated by AJAZZ_BUILD_SIDECAR (default ON). If cargo is absent the build still
# succeeds (the app runs for keyboard/mouse/AKP815) but logs a loud warning —
# Stream Dock devices will fail to open until the sidecar is on PATH.

option(AJAZZ_BUILD_SIDECAR "Build + bundle the streamdock-host Rust sidecar via cargo" ON)

# ajazz_add_streamdock_sidecar(<app_target>) Wires the cargo build of streamdock-host into the given
# app target: the app gains a build dependency on the sidecar, the binary is staged beside the app
# binary post-build, and an install() rule places it beside the installed app.
function(ajazz_add_streamdock_sidecar app_target)
    if(NOT AJAZZ_BUILD_SIDECAR)
        message(STATUS "streamdock-host sidecar: build disabled (AJAZZ_BUILD_SIDECAR=OFF)")
        return()
    endif()

    find_program(CARGO_EXECUTABLE cargo)
    if(NOT CARGO_EXECUTABLE)
        message(
            WARNING
                "cargo not found: the mirajazz Stream Dock sidecar (streamdock-host) will NOT be "
                "built or bundled. Stream Dock (AKP03/AKP05/AKP153) devices need it at runtime. "
                "Install a Rust toolchain (https://rustup.rs) or pass -DAJAZZ_BUILD_SIDECAR=OFF to "
                "silence this warning."
        )
        return()
    endif()

    set(sidecar_dir "${CMAKE_SOURCE_DIR}/streamdock-host")
    set(sidecar_manifest "${sidecar_dir}/Cargo.toml")
    set(sidecar_lockfile "${sidecar_dir}/Cargo.lock")
    set(sidecar_name "streamdock-host${CMAKE_EXECUTABLE_SUFFIX}")

    # Build out-of-source so the in-tree streamdock-host/target stays a dev-only scratch dir and
    # never collides with the CMake build tree.
    set(sidecar_target_dir "${CMAKE_BINARY_DIR}/streamdock-host-target")
    set(sidecar_binary "${sidecar_target_dir}/release/${sidecar_name}")

    # Release, locked (reproducible: fails if Cargo.lock is stale). Sources are listed as DEPENDS so
    # an edit to the Rust code triggers a rebuild.
    file(GLOB_RECURSE sidecar_sources CONFIGURE_DEPENDS "${sidecar_dir}/src/*.rs")

    # macOS universal: when the app is built for both arches the single-arch host cargo binary would
    # be unusable on the other arch, so build each Apple target explicitly and lipo them into a fat
    # binary at the canonical release path. (CI-only path — not exercised by the Linux dev build.)
    # Requires the rustup targets aarch64-apple-darwin + x86_64-apple-darwin to be installed.
    list(LENGTH CMAKE_OSX_ARCHITECTURES sidecar_n_osx_arch)
    if(APPLE AND sidecar_n_osx_arch GREATER 1)
        # One cargo invocation per arch (instead of one multi---target build): it is robust across
        # cargo versions and unambiguous about which per-triple artifact each lipo input refers to.
        # The per-arch binaries land under <target-dir>/<triple>/release/.
        set(sidecar_arch_binaries "")
        set(sidecar_build_commands "")
        foreach(sidecar_arch IN LISTS CMAKE_OSX_ARCHITECTURES)
            if(sidecar_arch STREQUAL "arm64")
                set(sidecar_triple "aarch64-apple-darwin")
            elseif(sidecar_arch STREQUAL "x86_64")
                set(sidecar_triple "x86_64-apple-darwin")
            else()
                message(FATAL_ERROR "streamdock-host: unsupported macOS arch '${sidecar_arch}'")
            endif()
            list(
                APPEND
                sidecar_build_commands
                COMMAND
                "${CARGO_EXECUTABLE}"
                build
                --release
                --locked
                --manifest-path
                "${sidecar_manifest}"
                --target-dir
                "${sidecar_target_dir}"
                --target
                "${sidecar_triple}"
            )
            list(APPEND sidecar_arch_binaries
                 "${sidecar_target_dir}/${sidecar_triple}/release/${sidecar_name}"
            )
        endforeach()
        add_custom_command(
            OUTPUT "${sidecar_binary}" ${sidecar_build_commands}
            COMMAND lipo -create ${sidecar_arch_binaries} -output "${sidecar_binary}"
            WORKING_DIRECTORY "${sidecar_dir}"
            DEPENDS "${sidecar_manifest}" "${sidecar_lockfile}" ${sidecar_sources}
            COMMENT "Building universal streamdock-host Rust sidecar (cargo per-arch + lipo)"
            VERBATIM USES_TERMINAL
        )
    else()
        add_custom_command(
            OUTPUT "${sidecar_binary}"
            COMMAND "${CARGO_EXECUTABLE}" build --release --locked --manifest-path
                    "${sidecar_manifest}" --target-dir "${sidecar_target_dir}"
            WORKING_DIRECTORY "${sidecar_dir}"
            DEPENDS "${sidecar_manifest}" "${sidecar_lockfile}" ${sidecar_sources}
            COMMENT "Building streamdock-host Rust sidecar (cargo build --release --locked)"
            VERBATIM USES_TERMINAL
        )
    endif()

    add_custom_target(
        streamdock_host_sidecar ALL DEPENDS "${sidecar_binary}"
        COMMENT "streamdock-host Rust sidecar"
    )

    # Stage beside the app binary so the dev/run layout resolves the sidecar via
    # applicationDirPath() without an install step.
    add_custom_command(
        TARGET ${app_target}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sidecar_binary}"
                "$<TARGET_FILE_DIR:${app_target}>/${sidecar_name}"
        COMMENT "Staging streamdock-host beside ${app_target}"
        VERBATIM
    )
    add_dependencies(${app_target} streamdock_host_sidecar)

    # Install beside the app. On macOS the app is a .app bundle, so the sidecar goes into
    # Contents/MacOS next to the executable; elsewhere it goes to bin/.
    if(APPLE)
        install(PROGRAMS "${sidecar_binary}"
                DESTINATION "$<TARGET_FILE_NAME:${app_target}>.app/Contents/MacOS"
        )
    else()
        install(PROGRAMS "${sidecar_binary}" DESTINATION bin)
    endif()

    message(STATUS "streamdock-host sidecar: cargo build wired (${CARGO_EXECUTABLE})")
endfunction()
