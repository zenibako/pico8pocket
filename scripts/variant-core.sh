#!/usr/bin/env bash
# Repackage a pico8pocket release ZIP as a separately installable core, so
# two builds can sit side by side on the Pocket for A/B testing.
#
#   scripts/variant-core.sh IN.zip SUFFIX "Description" OUT.zip
#
# The variant core is Cores/Askent.pico8pocket<SUFFIX>.  It stays on the
# pico8pocket platform, so both cores browse the same Assets/pico8pocket
# cards, but its game entry, application, OS config, settings and save
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

name="pico8pocket$suffix"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
unzip -q "$in_zip" -d "$work/in"
src="$work/in"
dst="$work/out"
mkdir -p "$dst/Cores" "$dst/Assets/pico8pocket/common" "$dst/Platforms"

cp -r "$src/Cores/Askent.pico8pocket" "$dst/Cores/Askent.$name"
cp "$src/Platforms/pico8pocket.json" "$dst/Platforms/"
common="$dst/Assets/pico8pocket/common"
cp "$src/Assets/pico8pocket/common/os.bin" \
   "$src/Assets/pico8pocket/common/p8p_pad5.bin" \
   "$src/Assets/pico8pocket/common/p8p_pad6.bin" \
   "$src/Assets/pico8pocket/common/p8p_pad7.ofsf" "$common/"
cp "$src/Assets/pico8pocket/common/pico8pocket.elf" "$common/$name.elf"
sed "s/^ELF=pico8pocket\.elf$/ELF=$name.elf/" \
    "$src/Assets/pico8pocket/common/pico8pocket.ini" > "$common/$name.ini"
grep -q "^ELF=$name.elf$" "$common/$name.ini"

core="$dst/Cores/Askent.$name"
python3 -I - "$core" "$name" "$description" <<'EOF'
import json, sys
core, name, description = sys.argv[1:]
path = f"{core}/core.json"
data = json.load(open(path))
meta = data["core"]["metadata"]
meta["shortname"] = name
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

instance_dir="$dst/Assets/pico8pocket/Askent.$name"
mkdir -p "$instance_dir"
sed -e "s/\"pico8pocket\.elf\"/\"$name.elf\"/" \
    -e "s/\"pico8pocket\.ini\"/\"$name.ini\"/" \
    -e "s/\"pico8pocket\.cfg\"/\"$name.cfg\"/" \
    -e "s/\"pico8pocket_\([0-9]\)\.sav\"/\"${name}_\1.sav\"/" \
    "$src/Assets/pico8pocket/Askent.pico8pocket/pico8pocket.json" \
    > "$instance_dir/$name.json"
if grep -q '"pico8pocket[._]' "$instance_dir/$name.json"; then
    echo "instance JSON still names original files" >&2
    exit 1
fi

rm -f "$out_zip"
(cd "$dst" && zip -qr "$out_zip" Cores Assets Platforms)
echo "Created $out_zip (core Askent.$name)"
