#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$PROJECT_ROOT/.deps"
SDK_DIR="$DEPS_DIR/openfgpaSDK"
DIABLO_DIR="$DEPS_DIR/diablo"
FAKE08_DIR="$DEPS_DIR/fake-08"

SDK_URL="https://github.com/openfpgaOS/openfgpaSDK.git"
SDK_COMMIT="628a12b551ac8137373c477e97466b84d153d2af"
DIABLO_URL="https://github.com/openfpgaOS/Diablo.git"
DIABLO_COMMIT="8cb3198d50618696e07f5b49ad086a369dc57706"
# Only the SDK, the Pocket runtime and the container tooling are needed; the
# game sources are left out of the sparse checkout.
DIABLO_PATHS=(runtime/pocket src/sdk tools)
DIABLO_OS20_SHA256="f8e21c5f75cbbd2f442e028118a855e5b4490eac6bdf21ae322fecf798590719"
DIABLO_OS_SHA256="054abd4b3d2c57d55b4ecec23f2fcaa3703206db3df14ebe7793c22941d34ef7"
DIABLO_LOADER_SHA256="fc1f8d37eb0fb322006b4428f0b59d2801c9b74978bd3f24103f5f9bbfca3c99"
FAKE08_URL="https://github.com/jtothebell/fake-08.git"
FAKE08_COMMIT="814991a2571ad3970e386cef48f3b148aa1c27b9"
Z8LUA_COMMIT="e6928578d46b61fd5ea30cfcf547e855a30a0553"
Z8LUA_PATCHES=(
    "$PROJECT_ROOT/patches/z8lua-rv32-performance.patch"
    "$PROJECT_ROOT/patches/z8lua-env-fallback.patch"
    "$PROJECT_ROOT/patches/z8lua-upstream-backports.patch"
    "$PROJECT_ROOT/patches/z8lua-vm-fastpath.patch"
    "$PROJECT_ROOT/patches/z8lua-number-parse.patch"
    "$PROJECT_ROOT/patches/z8lua-local-compound.patch"
    "$PROJECT_ROOT/patches/z8lua-number-format.patch"
    "$PROJECT_ROOT/patches/z8lua-split-nonstring.patch"
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

# Shallow, blobless, sparse checkout of a pinned commit.  The commit ID pins
# the content; unlike GitHub's generated tarballs it cannot change upstream.
fetch_sparse() {
    local name="$1" url="$2" commit="$3" destination="$4"
    shift 4

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
        echo "Move $destination aside and run make deps again." >&2
        return 1
    fi

    # Build in a staging directory so an interrupted fetch never leaves a
    # partial checkout at the destination.
    local staging="$destination.partial"
    rm -rf "$staging"
    git init -q "$staging"
    git -C "$staging" remote add origin "$url"
    git -C "$staging" sparse-checkout set --no-cone "$@"
    git -C "$staging" fetch --depth 1 --filter=blob:none origin "$commit"
    git -C "$staging" checkout -q --detach "$commit"
    mv "$staging" "$destination"
    echo "$name: checked out $commit"
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
fetch_sparse "Diablo SDK/runtime" "$DIABLO_URL" "$DIABLO_COMMIT" \
    "$DIABLO_DIR" "${DIABLO_PATHS[@]/#//}"
verify_sha256 "$DIABLO_DIR/runtime/pocket/os20.rbf_r" "$DIABLO_OS20_SHA256" \
    "Diablo os20 bitstream"
verify_sha256 "$DIABLO_DIR/runtime/pocket/os.bin" "$DIABLO_OS_SHA256" \
    "Diablo OS image"
verify_sha256 "$DIABLO_DIR/runtime/pocket/loader.bin" "$DIABLO_LOADER_SHA256" \
    "Diablo loader"
grep -q '^#define OF_API_VERSION_MINOR  *9$' \
    "$DIABLO_DIR/src/sdk/include/of_version.h" || {
    echo "Diablo SDK is not API 0.9.x" >&2
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

# Later patches may touch context used by earlier ones, so individual
# reverse-apply checks are unreliable.  Record the applied patch set and
# rebuild the pristine pinned tree whenever it changes.
Z8LUA_DIR="$FAKE08_DIR/libs/z8lua"
patch_stamp="$(git -C "$Z8LUA_DIR" rev-parse --absolute-git-dir)/p8p-patches"
patch_digest="$(cat "${Z8LUA_PATCHES[@]}" | shasum -a 256 | awk '{print $1}')"
tree_digest() {
    git -C "$Z8LUA_DIR" diff HEAD | shasum -a 256 | awk '{print $1}'
}
if [[ -f "$patch_stamp" &&
      "$(cat "$patch_stamp")" == "$patch_digest $(tree_digest)" ]]; then
    echo "z8lua: patches already applied"
else
    git -C "$Z8LUA_DIR" checkout -q -- .
    git -C "$Z8LUA_DIR" clean -q -fd
    for z8lua_patch in "${Z8LUA_PATCHES[@]}"; do
        if ! git -C "$Z8LUA_DIR" apply "$z8lua_patch"; then
            echo "z8lua: cannot apply $z8lua_patch" >&2
            exit 1
        fi
        echo "z8lua: applied $(basename "$z8lua_patch")"
    done
    printf '%s %s\n' "$patch_digest" "$(tree_digest)" > "$patch_stamp"
fi
