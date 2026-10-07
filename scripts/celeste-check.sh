#!/usr/bin/env bash
# Priority regression check on locally supplied Celeste carts.
#
# Runs every assets/cards/celeste*.p8.png (never committed) through the
# compatibility scanner with scripted input and reports the final frame hash,
# host time per frame and, when valgrind is installed, host instructions per
# frame (deterministic, unlike wall time).  Results are compared with the
# baseline from the previous `--update` run in build/celeste-baseline/.
#
#   scripts/celeste-check.sh            compare with the baseline
#   scripts/celeste-check.sh --update   record a new baseline
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCAN="$PROJECT_ROOT/build/tools/pico8pocket_compat_scan"
BASELINE_DIR="$PROJECT_ROOT/build/celeste-baseline"
FRAMES="${CELESTE_FRAMES:-1800}"
SEED=1
update=0
[[ "${1:-}" == "--update" ]] && update=1

shopt -s nullglob
carts=("$PROJECT_ROOT"/assets/cards/celeste*.p8.png "$PROJECT_ROOT"/assets/cards/celeste*.p8)
if (( ${#carts[@]} == 0 )); then
    echo "No Celeste cart in assets/cards/ (e.g. celeste.p8.png, celeste_classic_2.p8.png)."
    exit 0
fi
[[ -x "$SCAN" ]] || { echo "Build the scanner first: make compat-scan" >&2; exit 1; }
mkdir -p "$BASELINE_DIR"

status=0
for cart in "${carts[@]}"; do
    name="$(basename "$cart")"
    # A failing scan must reach the FAILED report, not stop the script.
    result="$("$SCAN" "$cart" "$FRAMES" "$SEED" 2>&1 | tail -1 || true)"
    if [[ "$result" != ok* ]]; then
        echo "$name: FAILED: $result"
        status=1
        continue
    fi
    hash="$(sed -n 's/.*hash=\([0-9a-f]*\).*/\1/p' <<<"$result")"
    avg="$(sed -n 's/.* avg=\([0-9.]*ms\).*/\1/p' <<<"$result")"
    per_frame=""
    if command -v valgrind >/dev/null; then
        total="$(valgrind --tool=callgrind --callgrind-out-file=/dev/null \
            "$SCAN" "$cart" "$FRAMES" "$SEED" 2>&1 |
            sed -n 's/.*Collected : \([0-9]*\).*/\1/p')"
        base_total="$(valgrind --tool=callgrind --callgrind-out-file=/dev/null \
            "$SCAN" "$cart" 0 "$SEED" 2>&1 |
            sed -n 's/.*Collected : \([0-9]*\).*/\1/p')"
        if [[ -n "$total" && -n "$base_total" ]] && (( FRAMES > 0 )); then
            per_frame=$(( (total - base_total) / FRAMES ))
        fi
    fi

    line="hash=$hash avg=$avg"
    [[ -n "$per_frame" ]] && line+=" instructions/frame=$per_frame"
    baseline_file="$BASELINE_DIR/$name.txt"
    if (( update )); then
        printf '%s %s\n' "$hash" "${per_frame:-0}" > "$baseline_file"
        echo "$name: $line (baseline recorded)"
    elif [[ -f "$baseline_file" ]]; then
        read -r base_hash base_per_frame < "$baseline_file"
        compare="frames $([[ "$hash" == "$base_hash" ]] && echo identical || echo CHANGED)"
        if [[ -n "$per_frame" && "$base_per_frame" != 0 ]]; then
            compare+=", cost $(awk -v a="$per_frame" -v b="$base_per_frame" \
                'BEGIN { printf "%+.1f%%", (a - b) * 100 / b }')"
        fi
        echo "$name: $line ($compare vs baseline)"
    else
        echo "$name: $line (no baseline; run with --update)"
    fi
done
exit $status
