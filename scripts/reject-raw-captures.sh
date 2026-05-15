#!/usr/bin/env bash
# Reject raw USB capture files at commit time (CAPTURE-01 / Pitfall 17).
#
# Pre-commit invokes this with the staged paths as positional args (we set
# pass_filenames: true). For each staged path we check:
#
#   1. Extension blocklist: basename matches *.pcap or *.pcapng
#      (case-insensitive) -> REJECT outright.
#   2. Captures-sink size guardrail: path is under
#      .planning/research/captures/, is NOT the README or .gitignore, IS
#      binary, AND exceeds 10240 bytes -> REJECT.
#
# Rejection message points at docs/policies/capture-data-hygiene.md and
# scripts/hex-to-cpparray.py per CAPTURE-01 spec.
#
# Output is ASCII-only (no em-dash, no arrows) so that Windows CI shells
# that ever invoke pre-commit locally render the rejection cleanly through
# the CMD codepage (CLAUDE.md hard rule on test/string ASCII-only).
set -euo pipefail

SINK_DIR=".planning/research/captures"
SINK_README="${SINK_DIR}/README.md"
SINK_GITIGNORE="${SINK_DIR}/.gitignore"
SIZE_LIMIT_BYTES=10240

POLICY_DOC="docs/policies/capture-data-hygiene.md"
SANITISER_SCRIPT="scripts/hex-to-cpparray.py"

bad=0

emit_reject() {
    # $1 = path, $2 = reason
    {
        printf 'REJECTED: %s\n' "$1"
        printf '  Reason: %s\n' "$2"
        printf '  Remediation: See %s and use %s to produce\n' \
            "$POLICY_DOC" "$SANITISER_SCRIPT"
        printf '  a sanitised fixture under tests/integration/fixtures/<codename>/.\n'
    } >&2
}

is_binary() {
    # $1 = path. Returns 0 if binary, 1 otherwise. Best-effort: missing
    # `file` command or unreadable files fall through as "not binary" so
    # we never false-reject on weird FS state.
    [[ -f $1 ]] || return 1
    if ! command -v file >/dev/null 2>&1; then
        return 1
    fi
    file --mime-encoding "$1" 2>/dev/null | grep -q 'binary'
}

for p in "$@"; do
    # Skip non-files (deletions show up as paths that no longer exist on disk).
    [[ -e $p ]] || continue

    # Lowercase basename for case-insensitive extension match.
    base_lower="${p,,}"

    # Rule 1: extension blocklist (case-insensitive).
    case "$base_lower" in
        *.pcap | *.pcapng)
            emit_reject "$p" "raw USB capture file (extension blocklist: *.pcap / *.pcapng, case-insensitive)"
            bad=1
            continue
            ;;
    esac

    # Rule 2: captures-sink size guardrail.
    case "$p" in
        "${SINK_DIR}"/* | "./${SINK_DIR}"/*)
            # Normalise leading ./ so the README/gitignore exceptions match.
            normalised="${p#./}"
            if [[ $normalised == "$SINK_README" || $normalised == "$SINK_GITIGNORE" ]]; then
                continue
            fi
            if [[ ! -f $p ]]; then
                continue
            fi
            size_bytes=$(wc -c <"$p" | tr -d '[:space:]')
            if [[ -z $size_bytes ]]; then
                continue
            fi
            if ((size_bytes > SIZE_LIMIT_BYTES)) && is_binary "$p"; then
                emit_reject "$p" "binary file >${SIZE_LIMIT_BYTES} bytes under ${SINK_DIR}/ (captures-sink size guardrail)"
                bad=1
            fi
            ;;
    esac
done

exit "$bad"
