#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=${1:-0.1.0-alpha.1}
output=${2:-"$root/dist"}
build=${3:-"$root/build-release"}
name="hermas-$version-linux-x86_64"
stage=$(mktemp -d "${TMPDIR:-/tmp}/hermas-package-XXXXXX")
source_date_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" show -s --format=%ct HEAD)}

cleanup() {
    rm -rf "$stage"
}
trap cleanup EXIT INT TERM

grep -Fq "#define HERMAS_VERSION \"$version\"" \
    "$root/include/hermas2/version.h"
grep -Fq "version = \"$version\"" "$root/compiler/herma2/Cargo.toml"

mkdir -p "$output"
export CARGO_TARGET_DIR="$build/cargo"
cargo build --manifest-path "$root/Cargo.toml" -p herma2 --release
cmake -S "$root" -B "$build/cmake" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$build/cmake" -j2
cmake --install "$build/cmake" --prefix "$stage/$name"
install -m 0755 "$CARGO_TARGET_DIR/release/herma2" "$stage/$name/bin/herma2"

tar --sort=name \
    --mtime="@$source_date_epoch" \
    --owner=0 --group=0 --numeric-owner \
    -C "$stage" -czf "$output/$name.tar.gz" "$name"
git -C "$root" archive \
    --format=tar.gz \
    --prefix="hermas-$version-source/" \
    -o "$output/hermas-$version-source.tar.gz" HEAD
(
    cd "$output"
    sha256sum "$name.tar.gz" "hermas-$version-source.tar.gz" \
        >"hermas-$version-SHA256SUMS"
)
cat "$output/hermas-$version-SHA256SUMS"
