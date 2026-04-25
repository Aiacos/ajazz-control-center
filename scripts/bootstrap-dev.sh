#!/usr/bin/env bash
# ============================================================================
# AJAZZ Control Center — developer bootstrap
# ============================================================================
#
# One command to go from `git clone` to a running development build:
#
#   ./scripts/bootstrap-dev.sh
#
# What it does:
#   1. Detects your distro / OS.
#   2. Installs all build-time dependencies via the native package manager
#      (prompts once for sudo, only if needed). macOS uses Homebrew.
#   3. Installs the udev rule (Linux only).
#   4. Configures and builds the project with the `dev` preset.
#   5. Prints the exact command to launch the freshly-built binary.
#
# You can re-run this script any time; dependency install is idempotent.
# ============================================================================
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

# ---------- pretty output (same style as install.sh) ----------------------
# shellcheck disable=SC2034 # DIM kept for symmetry with install.sh even if unused here
if [[ -t 1 ]]; then
    BOLD=$(tput bold)
    DIM=$(tput dim)
    RED=$(tput setaf 1)
    GRN=$(tput setaf 2)
    YLW=$(tput setaf 3)
    BLU=$(tput setaf 4)
    RST=$(tput sgr0)
else
    BOLD=
    DIM=
    RED=
    GRN=
    YLW=
    BLU=
    RST=
fi
# shellcheck disable=SC2034 # DIM referenced via export for callers
export DIM
step() { printf '%s==>%s %s%s%s\n' "$BLU" "$RST" "$BOLD" "$*" "$RST"; }
info() { printf '    %s\n' "$*"; }
ok() { printf '%s ok%s %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '%swarn%s %s\n' "$YLW" "$RST" "$*"; }
die() {
    printf '%sfail%s %s\n' "$RED" "$RST" "$*" >&2
    exit 1
}
need() { command -v "$1" >/dev/null 2>&1; }

sudo_cmd() {
    if [[ $EUID -eq 0 ]]; then
        "$@"
    elif need sudo; then
        sudo "$@"
    else
        die "need root for: $*"
    fi
}

# ---------- dependency install --------------------------------------------
install_deps_fedora() {
    step "Installing build dependencies (Fedora/RHEL/openSUSE)"
    sudo_cmd dnf install -y \
        cmake ninja-build gcc-c++ git pkgconf-pkg-config \
        qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
        qt6-qttools-devel qt6-qtsvg-devel \
        python3-devel python3-pip \
        systemd-devel libudev-devel libusb1-devel \
        clang-tools-extra
}

install_deps_debian() {
    step "Installing build dependencies (Debian/Ubuntu)"
    sudo_cmd apt-get update -qq
    sudo_cmd apt-get install -y \
        cmake ninja-build g++ git pkg-config \
        qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev \
        qt6-tools-dev libqt6svg6-dev \
        python3-dev python3-pip \
        libudev-dev libsystemd-dev libusb-1.0-0-dev \
        clang-format clang-tidy
}

install_deps_arch() {
    step "Installing build dependencies (Arch)"
    sudo_cmd pacman -Syu --needed --noconfirm \
        cmake ninja gcc git pkgconf \
        qt6-base qt6-declarative qt6-tools qt6-svg \
        python python-pip systemd-libs libusb \
        clang
}

install_deps_macos() {
    step "Installing build dependencies (macOS / Homebrew)"
    if ! need brew; then
        die "Homebrew not found. Install it first: https://brew.sh"
    fi
    brew install cmake ninja qt@6 python@3.11 pkg-config
    local qt_prefix
    qt_prefix=$(brew --prefix qt@6)
    export CMAKE_PREFIX_PATH="$qt_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    info "CMAKE_PREFIX_PATH set to include $qt_prefix"
}

# ---------- udev rule (Linux only) ----------------------------------------
install_udev() {
    local rule="$ROOT/resources/linux/99-ajazz.rules"
    [[ -f $rule ]] || {
        warn "udev rule not found at $rule, skipping"
        return
    }
    step "Installing udev rule (user-level device access, no logout required)"
    sudo_cmd install -m 644 "$rule" /etc/udev/rules.d/99-ajazz.rules
    sudo_cmd udevadm control --reload-rules
    sudo_cmd udevadm trigger
    ok "udev configured — replug the device is NOT required"
}

# ---------- distro detection ----------------------------------------------
detect_and_install() {
    case "$(uname -s)" in
        Darwin)
            install_deps_macos
            return
            ;;
        Linux) ;;
        *) die "unsupported OS: $(uname -s)" ;;
    esac

    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        case "${ID}${ID_LIKE:+ $ID_LIKE}" in
            *fedora* | *rhel* | *centos* | *opensuse* | *suse*) install_deps_fedora ;;
            *debian* | *ubuntu*) install_deps_debian ;;
            *arch* | *manjaro*) install_deps_arch ;;
            *) die "unsupported distro: $ID (open an issue or install deps manually)" ;;
        esac
    else
        die "cannot detect Linux distro (no /etc/os-release)"
    fi
    install_udev
}

# ---------- pre-commit hooks ----------------------------------------------
install_precommit() {
    step "Installing pre-commit hooks"
    if ! need pre-commit; then
        if need pipx; then
            pipx install pre-commit
        else
            pip install --user pre-commit || {
                warn "pip install failed; skipping pre-commit setup. Run 'pip install pre-commit' manually later."
                return 0
            }
        fi
    fi
    (cd "$ROOT" && pre-commit install && pre-commit install --hook-type commit-msg) ||
        warn "pre-commit install skipped (not a git repo yet?)"
    ok "pre-commit installed — hooks run automatically on every commit"
}

# ---------- build ----------------------------------------------------------
build() {
    step "Configuring with preset 'dev'"
    cmake --preset dev -S "$ROOT"
    step "Building"
    cmake --build --preset dev
    ok "build complete"
}

# ---------- main -----------------------------------------------------------
main() {
    cd "$ROOT"
    detect_and_install
    install_precommit
    build
    cat <<EOF

${GRN}Setup complete.${RST}

  Run the app:      ${BOLD}./build/dev/src/app/ajazz-control-center${RST}
  Run tests:        ${BOLD}ctest --preset dev${RST}
  Package (all):    ${BOLD}cmake --build --preset dev --target package${RST}

Tip: from now on you can use the ${BOLD}Makefile${RST} shortcuts:
  make build       # incremental rebuild
  make run         # build and run
  make test        # build and run tests
  make package     # build deb/rpm/dmg/msi locally

EOF
}

main "$@"
