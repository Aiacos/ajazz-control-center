#!/usr/bin/env bash
# ============================================================================
# AJAZZ Control Center — CI watcher
# ============================================================================
# Polls GitHub Actions for the workflow runs triggered by HEAD and prints
# their per-workflow conclusion as states transition. Exits 0 when every run
# is `success`, exits 1 if any run finishes `failure` / `cancelled` /
# `timed_out`. Intended to be run right after `git push` so the developer
# gets a single in-terminal verdict instead of having to open the browser.
#
# Usage:
#   make watch-ci          # Makefile entrypoint (recommended)
#   ./scripts/watch-ci.sh  # direct invocation
#
# Designed for the project's parallel-direct-commit-on-main workflow: catches
# regressions that ship from machine A and would otherwise stay invisible to
# machine B until someone notices the red ✗ on github.com (the failure mode
# that produced 14c0aea's Win32 break before this script existed).
# ============================================================================
set -u

if ! command -v gh >/dev/null 2>&1; then
    echo 'watch-ci: `gh` CLI not found. Install it from cli.github.com.' >&2
    exit 2
fi

if [[ -t 1 ]]; then
    BOLD=$(tput bold)
    GRN=$(tput setaf 2)
    RED=$(tput setaf 1)
    YLW=$(tput setaf 3)
    DIM=$(tput dim)
    RST=$(tput sgr0)
else
    BOLD=
    GRN=
    RED=
    YLW=
    DIM=
    RST=
fi

SHA=$(git rev-parse HEAD)
SHORT=${SHA:0:7}
echo "${BOLD}→ watching CI for ${SHORT}${RST}  (Ctrl+C to detach)"
echo

# Poll loop. Pretty-print only when the state set changes so the terminal
# stays readable for slow runs. Transient `gh` failures (rate-limit, network
# blip) are swallowed; the loop just re-tries on the next tick.
prev_state=""
spinner_idx=0
while true; do
    # `gh` normalises `conclusion` to "" (empty string) for non-terminal runs,
    # not `null`. jq's `// .status` doesn't fall through because empty strings
    # are truthy under the alternative operator — explicit branch required.
    runs=$(gh run list --commit="$SHA" \
        --json name,status,conclusion \
        --jq '.[] | "\(if .conclusion == "" then .status else .conclusion end)\t\(.name)"' \
        2>/dev/null | sort -u || true)

    if [[ -z $runs ]]; then
        # No runs registered yet (CI hasn't picked up the push, or wrong SHA).
        # Tick once a second for the first 30s, then drop to 10s.
        if ((spinner_idx < 30)); then
            sleep 1
        else
            sleep 10
        fi
        spinner_idx=$((spinner_idx + 1))
        continue
    fi

    if [[ $runs != "$prev_state" ]]; then
        echo "${DIM}--- $(date +%T) ---${RST}"
        while IFS=$'\t' read -r state name; do
            case "$state" in
                success)
                    printf '  %s✔%s %s  %s%s%s\n' "$GRN" "$RST" "$name" "$DIM" "$state" "$RST"
                    ;;
                failure | cancelled | timed_out | startup_failure | action_required)
                    printf '  %s✘%s %s  %s%s%s\n' "$RED" "$RST" "$name" "$RED" "$state" "$RST"
                    ;;
                in_progress)
                    printf '  %s◐%s %s  %s%s%s\n' "$YLW" "$RST" "$name" "$YLW" "$state" "$RST"
                    ;;
                "" | queued | waiting | requested | pending)
                    # Empty state appears for runs gh just registered but
                    # hasn't picked up a status field for yet — treat as
                    # pending so we don't exit early.
                    printf '  %s·%s %s  %s%s%s\n' "$YLW" "$RST" "$name" "$YLW" "${state:-pending}" "$RST"
                    ;;
                *)
                    # Unknown state (skipped, neutral, ...) — print as-is
                    # without a verdict marker; conservative but visible.
                    printf '  %s?%s %s  %s%s%s\n' "$DIM" "$RST" "$name" "$DIM" "$state" "$RST"
                    ;;
            esac
        done <<<"$runs"
        echo
        prev_state="$runs"
    fi

    # Pending = anything NOT in the explicit terminal-state set. Empty
    # `state` (transient — gh hasn't propagated status yet) is treated as
    # pending. Without this guard the script previously exited with a
    # false "all green" verdict whenever some runs hadn't progressed past
    # queued.
    if echo "$runs" | grep -qvE "^(success|failure|cancelled|timed_out|skipped|neutral|startup_failure|action_required)\b"; then
        sleep 30
        continue
    fi

    # All runs are conclusive now — decide overall verdict.
    if echo "$runs" | grep -qE "^(failure|cancelled|timed_out|startup_failure|action_required)\b"; then
        echo "${RED}${BOLD}✘ one or more workflows failed for ${SHORT}.${RST}"
        echo "${DIM}  inspect with: gh run list --commit=${SHA}${RST}"
        exit 1
    fi

    echo "${GRN}${BOLD}✔ all workflows green for ${SHORT}.${RST}"
    exit 0
done
