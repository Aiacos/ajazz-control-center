#!/usr/bin/env bash
# ============================================================================
# AJAZZ Control Center — environment doctor
# ============================================================================
# Quick health check for developers: verifies the toolchain, Qt, Python, and
# actually enumerates attached AJAZZ devices.
# ============================================================================
set -u

if [[ -t 1 ]]; then
    BOLD=$(tput bold)
    GRN=$(tput setaf 2)
    RED=$(tput setaf 1)
    YLW=$(tput setaf 3)
    RST=$(tput sgr0)
else
    BOLD=
    GRN=
    RED=
    YLW=
    RST=
fi

ok() { printf '  %s✔%s %s\n' "$GRN" "$RST" "$*"; }
bad() { printf '  %s✘%s %s\n' "$RED" "$RST" "$*"; }
warn() { printf '  %s!%s %s\n' "$YLW" "$RST" "$*"; }
head() { printf '\n%s== %s ==%s\n' "$BOLD" "$*" "$RST"; }

need_ver() {
    # $1 = command, $2 = minimum version (dotted, e.g. 3.28)
    local cmd=$1 min=$2 out ver
    if ! command -v "$cmd" >/dev/null 2>&1; then
        bad "$cmd not installed (need >= $min)"
        return 1
    fi
    out=$("$cmd" --version 2>&1 | head -1)
    ver=$(echo "$out" | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)
    if [[ -z $ver ]]; then
        warn "$cmd: could not parse version — got '$out'"
        return 0
    fi
    if [[ "$(printf '%s\n%s\n' "$min" "$ver" | sort -V | head -1)" == "$min" ]]; then
        ok "$cmd $ver"
    else
        bad "$cmd $ver (need >= $min)"
    fi
}

head "Toolchain"
need_ver cmake 3.28
need_ver ninja 1.11
need_ver git 2.30
if command -v g++ >/dev/null 2>&1; then need_ver g++ 13; else warn "g++ not found (clang++ can be used instead)"; fi
if command -v clang++ >/dev/null 2>&1; then need_ver clang++ 17; fi

head "Qt 6"
if command -v qmake6 >/dev/null 2>&1; then
    ok "qmake6 — $(qmake6 -query QT_VERSION)"
elif command -v qmake-qt6 >/dev/null 2>&1; then
    ok "qmake-qt6 — $(qmake-qt6 -query QT_VERSION)"
elif [[ -n ${CMAKE_PREFIX_PATH:-} ]]; then
    ok "CMAKE_PREFIX_PATH set to: $CMAKE_PREFIX_PATH"
else
    bad "Qt 6 not detected. On Fedora: sudo dnf install qt6-qtbase-devel"
fi

head "Python"
need_ver python3 3.11
python3 -c "import pybind11" 2>/dev/null && ok "pybind11 (host)" || warn "pybind11 not in host Python (ok: bundled via FetchContent)"

head "Runtime environment"
case "$(uname -s)" in
    Linux)
        [[ -f /etc/udev/rules.d/99-ajazz.rules ]] && ok "udev rule installed" || bad "udev rule missing — run: make udev"
        if command -v systemd-detect-virt >/dev/null 2>&1; then
            ok "logind present (uaccess ACLs will work)"
        else
            warn "systemd-logind not detected — uaccess may not work; fall back to the plugdev group"
        fi
        ;;
    Darwin) ok "macOS — Input Monitoring permission required on first launch" ;;
    *) ok "$(uname -s)" ;;
esac

head "Attached AJAZZ devices"
if command -v lsusb >/dev/null 2>&1; then
    found=0
    while IFS= read -r line; do
        ok "$line"
        found=1
    done < <(lsusb 2>/dev/null | grep -Ei 'ID (0300|3151|3554):')
    [[ $found -eq 0 ]] && warn "no AJAZZ devices currently attached (this is fine if you're just building)"
elif [[ "$(uname -s)" == "Darwin" ]]; then
    system_profiler SPUSBDataType 2>/dev/null | grep -iE 'AJAZZ|Mirabox|Stream Deck' | head -5 ||
        warn "no AJAZZ devices listed in SPUSBDataType"
else
    warn "no lsusb — cannot enumerate devices"
fi

printf '\n%sdoctor complete.%s\n' "$BOLD" "$RST"
