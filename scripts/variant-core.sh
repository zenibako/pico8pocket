#!/usr/bin/env bash
# Repackage a pico8pocket release ZIP as a separately installable core, so
# two builds can sit side by side on the Pocket for A/B testing.
#
#   scripts/variant-core.sh IN.zip SUFFIX "Description" OUT.zip
#
# The variant core is Cores/<author>.<shortname><SUFFIX> (zenibako.pico8b
# for SUFFIX b).  It stays on the same platform, so both cores browse the
# same Assets/<platform>/common/cards, but its game entry, application, OS config, settings and save
# files get their own names: no file shares a name with the original core's,
# so copying one core's files can never replace the other's.  The OS binary and
# compatibility pads are shared.
set -euo pipefail

if (( $# != 4 )); then
    echo "usage: $0 IN.zip SUFFIX \"Description\" OUT.zip" >&2
    exit 2
fi
in_zip="$(realpath "$1")"
suffix="$2"
description="$3"
out_zip="$(realpath -m "$4")"
[[ "$suffix" =~ ^[a-z0-9]+$ ]] || { echo "SUFFIX must be [a-z0-9]+" >&2; exit 2; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
unzip -q "$in_zip" -d "$work/in"
src="$work/in"
dst="$work/out"

core_jsons=("$src"/Cores/*/core.json)
(( ${#core_jsons[@]} == 1 )) || { echo "expected one core in $1" >&2; exit 1; }
src_core="$(dirname "${core_jsons[0]}")"
core_id="$(basename "$src_core")"
platform="$(jq -er '.core.metadata.platform_ids[0]' "${core_jsons[0]}")"
author="${core_id%%.*}"
variant_id="$core_id$suffix"
name="pico8pocket$suffix"  # file names inside the platform folders

mkdir -p "$dst/Cores" "$dst/Assets/$platform/common" "$dst/Platforms"
cp -r "$src_core" "$dst/Cores/$variant_id"
cp "$src/Platforms/$platform.json" "$dst/Platforms/"
common="$dst/Assets/$platform/common"
cp "$src/Assets/$platform/common/os.bin" \
   "$src/Assets/$platform/common/p8p_pad5.bin" \
   "$src/Assets/$platform/common/p8p_pad6.bin" \
   "$src/Assets/$platform/common/p8p_pad7.ofsf" "$common/"
cp "$src/Assets/$platform/common/pico8pocket.elf" "$common/$name.elf"
sed "s/^ELF=pico8pocket\.elf$/ELF=$name.elf/" \
    "$src/Assets/$platform/common/pico8pocket.ini" > "$common/$name.ini"
grep -q "^ELF=$name.elf$" "$common/$name.ini"

core="$dst/Cores/$variant_id"
python3 -I - "$core" "$name" "${variant_id#"$author".}" "$description" <<'EOF'
import json, sys
core, name, shortname, description = sys.argv[1:]
path = f"{core}/core.json"
data = json.load(open(path))
meta = data["core"]["metadata"]
meta["shortname"] = shortname
meta["description"] = description
json.dump(data, open(path, "w"), indent=4)
path = f"{core}/data.json"
data = json.load(open(path))
for slot in data["data"]["data_slots"]:
    if slot.get("filename") == "pico8pocket.json":
        slot["filename"] = f"{name}.json"
    elif slot.get("filename") == "pico8pocket.elf":
        slot["filename"] = f"{name}.elf"
    elif slot.get("filename") == "pico8pocket.ini":
        slot["filename"] = f"{name}.ini"
json.dump(data, open(path, "w"), indent=4)
EOF

instance_dir="$dst/Assets/$platform/$variant_id"
mkdir -p "$instance_dir"
sed -e "s/\"pico8pocket\.elf\"/\"$name.elf\"/" \
    -e "s/\"pico8pocket\.ini\"/\"$name.ini\"/" \
    -e "s/\"pico8pocket\.cfg\"/\"$name.cfg\"/" \
    -e "s/\"pico8pocket_\([0-9]\)\.sav\"/\"${name}_\1.sav\"/" \
    "$src/Assets/$platform/$core_id/pico8pocket.json" \
    > "$instance_dir/$name.json"
if grep -q '"pico8pocket[._]' "$instance_dir/$name.json"; then
    echo "instance JSON still names original files" >&2
    exit 1
fi

rm -f "$out_zip"
(cd "$dst" && zip -qr "$out_zip" Cores Assets Platforms)
echo "Created $out_zip (core $variant_id)"
