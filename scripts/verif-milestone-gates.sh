#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# VERIF-02 milestone modularity grep gates (v2.0).
#
# Asserts the four modularity / security boundaries that the v2.0 milestone
# locks. Each gate is comment-aware: a naive textual hit that lives only in a
# comment (// or block-comment continuation `*`) does NOT trip the gate, but a
# live-code occurrence does. This mirrors the comment-aware idiom already used
# by .github/workflows/ci.yml (the Phase-30 QHostAddress::Any gate) so the
# local run and CI agree byte-for-byte.
#
# Run locally:   bash scripts/verif-milestone-gates.sh
# CI:            invoked by .github/workflows/ci.yml (Linux-only step).
#
# Exit 0 iff all four gates PASS; non-zero (1) if any gate trips. Every gate is
# still evaluated and reported so a single run shows the full picture.
# ASCII-only output (CLAUDE.md: survive the Win32 CMD codepage).
set -euo pipefail

# Resolve repo root from this script's location so the gates run correctly no
# matter the caller's cwd.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

fail=0

# strip_comments: read grep -rn output on stdin, drop hits whose match column
# begins with `//` or a block-comment continuation `*`. Same regex as ci.yml.
strip_comments() {
    grep -vP ':[0-9]+:\s*(//|[*])' || true
}

# report <name> <note> <hits>: print PASS/FAIL + count; set `fail` on any hit.
report() {
    local name="$1" note="$2" hits="$3" count=0
    if [ -n "${hits}" ]; then
        count="$(printf '%s\n' "${hits}" | grep -c . || true)"
    fi
    if [ "${count}" -eq 0 ]; then
        printf 'PASS  %-22s  count=0   (%s)\n' "${name}" "${note}"
    else
        printf 'FAIL  %-22s  count=%s   (%s)\n' "${name}" "${count}" "${note}"
        printf '%s\n' "${hits}" | sed 's/^/        /'
        fail=1
    fi
}

echo "VERIF-02 milestone modularity gates"
echo "==================================="

# ---- Gate 1: no mirajazz coupling in core or the plugin bridge -------------
# The Stream Dock wire layer lives only in the out-of-process Rust sidecar;
# neither ajazz_core nor the plugin bridge/manager may reference it.
g1="$({ grep -rn mirajazz \
    src/core/ \
    src/app/src/plugin_manager.cpp \
    src/app/src/plugin_device_bridge.cpp 2>/dev/null || true; } | strip_comments)"
report "mirajazz-coupling" "0 mirajazz refs in core/bridge" "${g1}"

# ---- Gate 2: no nlohmann::json include leak into public core headers -------
# COD-031 boundary: nlohmann is PRIVATE to ajazz_plugins; it must never appear
# in an installed/public core header. The meaningful gate is the #include.
g2="$({ grep -rn '#include.*nlohmann' src/core/include/ 2>/dev/null || true; } | strip_comments)"
report "nlohmann-core-include" "0 nlohmann includes in core/include" "${g2}"

# ---- Gate 3: no resurrected C++ AKP wire factory symbols -------------------
# makeAkp03 / makeAkp05 / makeAkp153 were removed in the sidecar migration
# (experiment/mirajazz Slice D). We assert on the function symbols (not the
# akp05 filename, which survives in comments) and drop comment-only hits.
g3="$({ grep -rnE 'makeAkp05|makeAkp03|makeAkp153' src/ 2>/dev/null || true; } | strip_comments)"
report "akp-wire-symbols" "0 makeAkp03/05/153 in live code" "${g3}"

# ---- Gate 4: WS server binds loopback only --------------------------------
# QHostAddress::Any (0.0.0.0) would expose the plugin WebSocket server to every
# interface. The only sanctioned occurrences are documentation comments.
g4="$({ grep -rn 'QHostAddress::Any' src/ 2>/dev/null || true; } | strip_comments)"
report "qhostaddress-any" "0 QHostAddress::Any in live code" "${g4}"

echo "==================================="
if [ "${fail}" -ne 0 ]; then
    echo "RESULT: FAIL - one or more milestone modularity gates tripped."
    exit 1
fi
echo "RESULT: PASS - all four VERIF-02 milestone modularity gates clean."
exit 0
