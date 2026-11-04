#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INPUT="$PROJECT_ROOT/build/pocket/pico8pocket"
RELEASES="$PROJECT_ROOT/releases"
[[ -d "$INPUT/Cores" ]] || { echo "Missing Pocket tree; run make pocket" >&2; exit 1; }
# Name the zip after the version the assembled core reports (a test build
# carries its -rc.N version).
VERSION="$(jq -er '.core.metadata.version' "$INPUT"/Cores/*/core.json)"
if [[ "${BUNDLE_LOCAL_CARTS:-0}" == "1" ]]; then
    OUTPUT="$RELEASES/pico8pocket-v${VERSION}-local.zip"
else
    OUTPUT="$RELEASES/pico8pocket-v${VERSION}.zip"
fi

mkdir -p "$RELEASES"
rm -f "$OUTPUT"
(cd "$INPUT" && zip -r "$OUTPUT" Cores Assets Platforms >/dev/null)
echo "Package created: $OUTPUT"
