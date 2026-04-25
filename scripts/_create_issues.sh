#!/usr/bin/env bash
# Bulk-create GitHub labels and issues for follow-up work.
# Requires: api_credentials=["github"] (gh wrapper authenticates automatically).
set -u

REPO="Aiacos/ajazz-control-center"
JSON="$(dirname "$0")/_issues.json"

# --- 1. Ensure all labels exist (idempotent) -------------------------------------
declare -A LABEL_COLOR=(
    [P1]="b60205"
    [P2]="d93f0b"
    [P3]="fbca04"
    [security]="b60205"
    [quality]="0e8a16"
    [ui]="5319e7"
    [feature]="0052cc"
    [docs]="0075ca"
    [testing]="bfdadc"
    [plugins]="1d76db"
    [akp]="c5def5"
    [mouse]="c5def5"
    [keyboard]="c5def5"
    [components]="d4c5f9"
    [accessibility]="5319e7"
    [branding]="d4c5f9"
    [navigation]="d4c5f9"
    [i18n]="d4c5f9"
    [polish]="ededed"
    [profiles]="0052cc"
    [concurrency]="e99695"
    [build]="bfd4f2"
    [ci]="bfd4f2"
    [supply - chain]="b60205"
    [dependencies]="0366d6"
    [packaging]="bfd4f2"
    [release]="0e8a16"
    [ecosystem]="0052cc"
    [rgb]="ff7f50"
    [actions]="0052cc"
    [macros]="0052cc"
    [platform]="bfd4f2"
    [trust]="b60205"
    [tray]="d4c5f9"
    [qml]="5319e7"
    [device - capabilities]="0052cc"
    [core]="0052cc"
)

echo "==> Creating labels (idempotent)..."
for name in "${!LABEL_COLOR[@]}"; do
    color="${LABEL_COLOR[$name]}"
    gh label create "$name" --repo "$REPO" --color "$color" --force >/dev/null 2>&1 || true
    printf "  - %-20s %s\n" "$name" "$color"
done

# --- 2. Create issues ------------------------------------------------------------
echo "==> Creating issues from $JSON..."
COUNT=$(python3 -c "import json,sys; print(len(json.load(open('$JSON'))))")
echo "  Total entries: $COUNT"

for i in $(seq 0 $((COUNT - 1))); do
    TITLE=$(python3 -c "import json; print(json.load(open('$JSON'))[$i]['title'])")
    BODY=$(python3 -c "import json; print(json.load(open('$JSON'))[$i]['body'])")
    LABELS=$(python3 -c "import json; print(','.join(json.load(open('$JSON'))[$i]['labels']))")

    printf "  [%2d/%d] %s\n" "$((i + 1))" "$COUNT" "$TITLE"
    if URL=$(gh issue create --repo "$REPO" --title "$TITLE" --body "$BODY" --label "$LABELS" 2>&1); then
        echo "         -> $URL"
    else
        echo "         !! FAILED: $URL"
    fi
done

echo "==> Done."
