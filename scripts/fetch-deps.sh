#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$PROJECT_ROOT/.deps"
SDK_DIR="$DEPS_DIR/openfgpaSDK"
DIABLO_DIR="$DEPS_DIR/diablo"
FAKE08_DIR="$DEPS_DIR/fake-08"

SDK_URL="https://github.com/openfpgaOS/openfgpaSDK.git"
SDK_COMMIT="628a12b551ac8137373c477e97466b84d153d2af"
DIABLO_COMMIT="2687e3d0c22d659674dbc6717959a98aedd92957"
DIABLO_URL="https://github.com/openfpgaOS/Diablo/archive/${DIABLO_COMMIT}.tar.gz"
DIABLO_ARCHIVE_SHA256="a951e12c9370740d11145bd2d19e3d4fa348b354a9f8dd8e4f9e5507660e1cb5"
DIABLO_OS20_SHA256="a92fd8908c48e26730c2ec7e69be56d905cbeea6f320d636c77741f8da31b50d"
DIABLO_OS_SHA256="588257b23ac96a8ae3cc10fee3d24c550f80befe7bc06156beae961875131f3b"
DIABLO_LOADER_SHA256="fc1f8d37eb0fb322006b4428f0b59d2801c9b74978bd3f24103f5f9bbfca3c99"
FAKE08_URL="https://github.com/jtothebell/fake-08.git"
FAKE08_COMMIT="814991a2571ad3970e386cef48f3b148aa1c27b9"
Z8LUA_COMMIT="e6928578d46b61fd5ea30cfcf547e855a30a0553"
Z8LUA_PATCHES=(
    "$PROJECT_ROOT/patches/z8lua-rv32-performance.patch"
    "$PROJECT_ROOT/patches/z8lua-env-fallback.patch"
)

fetch_one() {
    local name="$1" url="$2" commit="$3" destination="$4"

    if [[ -d "$destination/.git" ]]; then
        local current
        current="$(git -C "$destination" rev-parse HEAD)"
        if [[ "$current" != "$commit" ]]; then
            echo "$name is at $current, expected $commit" >&2
            echo "Move $destination aside and run make deps again." >&2
            return 1
        fi
        echo "$name: pinned revision already present"
        return 0
    fi

    if [[ -e "$destination" ]]; then
        echo "$destination exists but is not a Git checkout" >&2
        return 1
    fi

    git clone --no-checkout "$url" "$destination"
    git -C "$destination" fetch --depth 1 origin "$commit"
    git -C "$destination" checkout --detach "$commit"
    echo "$name: checked out $commit"
}

fetch_archive() {
    local name="$1" url="$2" commit="$3" archive_sha256="$4" destination="$5"
    local source_stamp="$destination/.p8p-source-commit"

    if [[ -f "$source_stamp" ]]; then
        local current
        current="$(tr -d '\r\n' < "$source_stamp")"
        if [[ "$current" != "$commit" ]]; then
            echo "$name is at $current, expected $commit" >&2
            echo "Move $destination aside and run make deps again." >&2
            return 1
        fi
        echo "$name: pinned archive already present"
        return 0
    fi

    if [[ -e "$destination" ]]; then
        echo "$destination exists but has no source stamp" >&2
        return 1
    fi

    local work archive unpack actual_sha256
    work="$(mktemp -d "$DEPS_DIR/.diablo-fetch.XXXXXX")"
    archive="$work/source.tar.gz"
    unpack="$work/unpack"
    curl -L --fail --silent --show-error -o "$archive" "$url"
    actual_sha256="$(shasum -a 256 "$archive" | awk '{print $1}')"
    if [[ "$actual_sha256" != "$archive_sha256" ]]; then
        echo "$name archive checksum mismatch: $actual_sha256" >&2
        return 1
    fi

    mkdir -p "$unpack"
    tar -xzf "$archive" -C "$unpack" --strip-components=1
    printf '%s\n' "$commit" > "$unpack/.p8p-source-commit"
    mv "$unpack" "$destination"
    rm -rf "$work"
    echo "$name: extracted $commit"
}

verify_sha256() {
    local file="$1" expected="$2" label="$3" actual
    [[ -f "$file" ]] || { echo "Missing $label: $file" >&2; return 1; }
    actual="$(shasum -a 256 "$file" | awk '{print $1}')"
    [[ "$actual" == "$expected" ]] || {
        echo "$label checksum mismatch: $actual" >&2
        return 1
    }
}

mkdir -p "$DEPS_DIR"
fetch_one "openfpgaOS SDK" "$SDK_URL" "$SDK_COMMIT" "$SDK_DIR"
fetch_archive "Diablo SDK/runtime" "$DIABLO_URL" "$DIABLO_COMMIT" \
    "$DIABLO_ARCHIVE_SHA256" "$DIABLO_DIR"
verify_sha256 "$DIABLO_DIR/runtime/pocket/os20.rbf_r" "$DIABLO_OS20_SHA256" \
    "Diablo os20 bitstream"
verify_sha256 "$DIABLO_DIR/runtime/pocket/os.bin" "$DIABLO_OS_SHA256" \
    "Diablo OS image"
verify_sha256 "$DIABLO_DIR/runtime/pocket/loader.bin" "$DIABLO_LOADER_SHA256" \
    "Diablo loader"
grep -q '^#define OF_API_VERSION_MINOR  *8$' \
    "$DIABLO_DIR/src/sdk/include/of_version.h" || {
    echo "Diablo SDK is not API 0.8.x" >&2
    exit 1
}
fetch_one "Fake-08" "$FAKE08_URL" "$FAKE08_COMMIT" "$FAKE08_DIR"

git -C "$FAKE08_DIR" submodule update --init --depth 1 libs/z8lua
actual_z8lua_commit="$(git -C "$FAKE08_DIR/libs/z8lua" rev-parse HEAD)"
if [[ "$actual_z8lua_commit" != "$Z8LUA_COMMIT" ]]; then
    echo "z8lua is at $actual_z8lua_commit, expected $Z8LUA_COMMIT" >&2
    exit 1
fi
echo "z8lua: checked out $actual_z8lua_commit"

for z8lua_patch in "${Z8LUA_PATCHES[@]}"; do
    patch_name="$(basename "$z8lua_patch")"
    if git -C "$FAKE08_DIR/libs/z8lua" apply --reverse --check \
            "$z8lua_patch" >/dev/null 2>&1; then
        echo "z8lua: $patch_name already applied"
    elif git -C "$FAKE08_DIR/libs/z8lua" apply --check "$z8lua_patch"; then
        git -C "$FAKE08_DIR/libs/z8lua" apply "$z8lua_patch"
        echo "z8lua: applied $patch_name"
    else
        echo "z8lua: cannot apply $z8lua_patch" >&2
        exit 1
    fi
done
