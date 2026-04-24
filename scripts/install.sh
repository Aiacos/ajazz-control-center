#!/usr/bin/env bash
# ============================================================================
# AJAZZ Control Center — one-shot installer
# ============================================================================
#
# Usage (copy-paste, zero friction):
#
#   curl -fsSL https://raw.githubusercontent.com/Aiacos/ajazz-control-center/main/scripts/install.sh | bash
#
# What this script does, in order:
#   1. Detects your OS and package manager.
#   2. Downloads the latest stable release artifact from GitHub.
#   3. Installs it via the native package manager (dnf / apt / flatpak /
#      brew cask / winget — falls back to AppImage-style portable bundle
#      if none is available).
#   4. Installs the udev rule on Linux (via the package post-install).
#   5. Prints a one-line success message with the binary path.
#
# Opinionated defaults:
#   * Linux: prefers Flatpak when available (fully sandboxed, no sudo at
#     runtime). Falls back to .rpm / .deb for native integration.
#   * macOS: Homebrew cask if Homebrew is installed, otherwise direct DMG.
#   * Windows: winget (users can re-run this script from PowerShell via
#     `irm https://.../install.sh | bash` on WSL, but the canonical
#     Windows path is `winget install Aiacos.AjazzControlCenter`).
#
# No action requires adding yourself to a group or logging out: the udev
# rules use `TAG+="uaccess"` so systemd-logind grants access to the
# currently-logged-in user automatically.
# ============================================================================
set -euo pipefail

REPO="Aiacos/ajazz-control-center"
APP_ID="io.github.Aiacos.AjazzControlCenter"

# ---------- pretty output --------------------------------------------------
if [[ -t 1 ]]; then
    BOLD=$(tput bold); DIM=$(tput dim); RED=$(tput setaf 1)
    GRN=$(tput setaf 2); YLW=$(tput setaf 3); BLU=$(tput setaf 4)
    RST=$(tput sgr0)
else
    BOLD=; DIM=; RED=; GRN=; YLW=; BLU=; RST=
fi
step()  { printf '%s==>%s %s%s%s\n' "$BLU" "$RST" "$BOLD" "$*" "$RST"; }
info()  { printf '    %s\n' "$*"; }
ok()    { printf '%s ok%s %s\n' "$GRN" "$RST" "$*"; }
warn()  { printf '%swarn%s %s\n' "$YLW" "$RST" "$*"; }
die()   { printf '%sfail%s %s\n' "$RED" "$RST" "$*" >&2; exit 1; }

# ---------- helpers --------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1; }

latest_tag() {
    # Return the latest release tag (e.g. `v0.1.0`). Empty string if there
    # is no release yet — the script then tries `main` as a fallback when
    # the distro supports building from source.
    curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" 2>/dev/null \
        | awk -F'"' '/"tag_name":/ {print $4; exit}' || true
}

asset_url() {
    # $1 = tag, $2 = filename regex
    curl -fsSL "https://api.github.com/repos/${REPO}/releases/tags/$1" 2>/dev/null \
        | awk -F'"' -v want="$2" '$2=="browser_download_url" && $4 ~ want {print $4; exit}'
}

sudo_cmd() {
    if [[ $EUID -eq 0 ]]; then
        "$@"
    elif need sudo; then
        sudo "$@"
    else
        die "need root to run: $*  (install sudo or run as root)"
    fi
}

# ---------- detection ------------------------------------------------------
detect_os() {
    local u; u=$(uname -s)
    case "$u" in
        Linux)  OS=linux ;;
        Darwin) OS=macos ;;
        MINGW*|MSYS*|CYGWIN*) OS=windows ;;
        *) die "unsupported OS: $u" ;;
    esac
}

detect_linux_installer() {
    if need flatpak; then
        INSTALLER=flatpak
    elif need dnf; then
        INSTALLER=dnf
    elif need apt-get; then
        INSTALLER=apt
    elif need pacman; then
        INSTALLER=manual-rpm   # Arch users: grab .rpm via rpm-ostree? Offer manual AppImage route.
        warn "Arch Linux is not a first-class target; will offer the .flatpak instead."
        INSTALLER=flatpak-bundle
    else
        INSTALLER=flatpak-bundle
    fi
}

# ---------- install paths --------------------------------------------------
install_linux_flatpak() {
    step "Installing AJAZZ Control Center via Flatpak"
    if ! flatpak remotes --columns=name | grep -q '^flathub$'; then
        info "Adding Flathub remote (per-user)…"
        flatpak --user remote-add --if-not-exists flathub \
            https://dl.flathub.org/repo/flathub.flatpakrepo
    fi
    # Until published on Flathub, fall back to sideload bundle
    local tag url tmp
    tag=$(latest_tag)
    [[ -n "$tag" ]] || die "no release published yet"
    url=$(asset_url "$tag" "\\.flatpak$")
    [[ -n "$url" ]] || die "no .flatpak asset in release $tag"
    tmp=$(mktemp -t acc-XXXXXX.flatpak)
    info "Downloading $tag bundle…"
    curl -fsSL -o "$tmp" "$url"
    flatpak --user install --noninteractive "$tmp"
    rm -f "$tmp"
    ok "installed — launch with: flatpak run ${APP_ID}"
}

install_linux_dnf() {
    step "Installing AJAZZ Control Center via dnf"
    local tag url tmp
    tag=$(latest_tag); [[ -n "$tag" ]] || die "no release published yet"
    url=$(asset_url "$tag" "\\.rpm$")
    [[ -n "$url" ]] || die "no .rpm asset in release $tag"
    tmp=$(mktemp -t acc-XXXXXX.rpm)
    info "Downloading $tag RPM…"
    curl -fsSL -o "$tmp" "$url"
    sudo_cmd dnf install -y "$tmp"
    rm -f "$tmp"
    ok "installed — launch with: ajazz-control-center"
}

install_linux_apt() {
    step "Installing AJAZZ Control Center via apt"
    local tag url tmp
    tag=$(latest_tag); [[ -n "$tag" ]] || die "no release published yet"
    url=$(asset_url "$tag" "\\.deb$")
    [[ -n "$url" ]] || die "no .deb asset in release $tag"
    tmp=$(mktemp -t acc-XXXXXX.deb)
    info "Downloading $tag DEB…"
    curl -fsSL -o "$tmp" "$url"
    sudo_cmd apt-get install -y "$tmp"
    rm -f "$tmp"
    ok "installed — launch with: ajazz-control-center"
}

install_macos() {
    step "Installing AJAZZ Control Center on macOS"
    if need brew; then
        info "Homebrew detected, using cask…"
        brew install --cask aiacos/tap/ajazz-control-center 2>/dev/null || {
            warn "cask not published yet; falling back to DMG download."
            install_macos_dmg
        }
        ok "installed — open from Launchpad or: open -a 'AJAZZ Control Center'"
    else
        install_macos_dmg
    fi
}

install_macos_dmg() {
    local tag url tmp
    tag=$(latest_tag); [[ -n "$tag" ]] || die "no release published yet"
    url=$(asset_url "$tag" "\\.dmg$")
    [[ -n "$url" ]] || die "no .dmg asset in release $tag"
    tmp=$(mktemp -t acc-XXXXXX.dmg)
    info "Downloading $tag DMG…"
    curl -fsSL -o "$tmp" "$url"
    hdiutil attach "$tmp" -nobrowse -quiet
    local vol; vol=$(ls /Volumes | grep -i ajazz | head -1)
    cp -R "/Volumes/$vol/AJAZZ Control Center.app" /Applications/
    hdiutil detach "/Volumes/$vol" -quiet || true
    rm -f "$tmp"
    ok "installed to /Applications — first launch: right-click → Open to bypass Gatekeeper"
}

install_windows_hint() {
    cat <<EOF
${BOLD}Windows installation${RST}

This script is designed for POSIX shells. On Windows, open PowerShell
and run one of:

  ${BOLD}winget install Aiacos.AjazzControlCenter${RST}         (recommended)
  ${BOLD}scoop install ajazz-control-center${RST}               (if you use Scoop)

Or download the .msi from:
  https://github.com/${REPO}/releases/latest
EOF
}

# ---------- main -----------------------------------------------------------
main() {
    detect_os
    step "Detected OS: ${BOLD}$OS${RST}"
    case "$OS" in
        linux)
            detect_linux_installer
            info "Installer: $INSTALLER"
            case "$INSTALLER" in
                flatpak|flatpak-bundle) install_linux_flatpak ;;
                dnf)                    install_linux_dnf ;;
                apt)                    install_linux_apt ;;
                *) die "no supported installer found" ;;
            esac
            info
            info "${DIM}Device access is granted via udev uaccess ACLs."
            info "${DIM}No group membership, no logout, no replug required.${RST}"
            ;;
        macos)   install_macos ;;
        windows) install_windows_hint ;;
    esac
    step "Done."
}

main "$@"
