#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build-order-total"}
rust_target=${CARGO_TARGET_DIR:-"$root/target/order-total"}
export CARGO_TARGET_DIR="$rust_target"
herma2="$rust_target/debug/herma2"
work=$(mktemp -d "${TMPDIR:-/tmp}/hermas-order-total-XXXXXX")
daemon_pid=

cleanup() {
    if [[ -n "$daemon_pid" ]] && kill -0 "$daemon_pid" 2>/dev/null; then
        kill "$daemon_pid" 2>/dev/null || true
        wait "$daemon_pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

cd "$root"
cargo build -p herma2
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build" -j2

image="$work/order-total.h2gi"
"$herma2" workflow image \
    apps/order-total/order-total.hscript2 "$image" \
    apps/order-total/discount.hschema2 \
    apps/order-total/tax.hschema2 \
    apps/order-total/receipt.hschema2
chmod 600 "$image"

description=$("$build/hermas2_image_check" --describe "$image")
fingerprint() {
    local app=$1
    local action=$2
    awk -v app="app=$app" -v action="id=$action" \
        '$1 == "action" && $2 == app && $3 == action {
            sub(/^fingerprint=/, "", $4)
            print $4
        }' <<<"$description"
}

discount_fp=$(fingerprint 1 1)
tax_fp=$(fingerprint 2 2)
receipt_fp=$(fingerprint 3 3)
if [[ -z "$discount_fp" || -z "$tax_fp" || -z "$receipt_fp" ]]; then
    echo "order-total: could not discover Action fingerprints" >&2
    exit 1
fi

state="$work/state"
app_socket="$work/apps.sock"
control_socket="$work/control.sock"
"$build/hermas2d" \
    "$image" 1 "$state" "$app_socket" "$control_socket" &
daemon_pid=$!
for _ in $(seq 1 100); do
    [[ -S "$app_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$app_socket" ]]

"$build/hermas2_discount" "$app_socket" "$discount_fp" &
discount_pid=$!
"$build/hermas2_tax" "$app_socket" "$tax_fp" &
tax_pid=$!
"$build/hermas2_receipt" \
    "$app_socket" "$receipt_fp" >"$work/receipt.out" &
receipt_pid=$!

for _ in $(seq 1 100); do
    [[ -S "$control_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$control_socket" ]]

# 10,000 cents as a canonical little-endian HSchema2 Integer.
"$build/hermas2_run" \
    "$control_socket" 41 --image "$image" 1027000000000000
wait "$discount_pid" "$tax_pid" "$receipt_pid"
grep -Fx "Order total: 9900 cents" "$work/receipt.out"

kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
before=$(sha256sum "$state/journal.h2j")

"$build/hermas2d" \
    "$image" 1 "$state" "$app_socket" "$control_socket" &
daemon_pid=$!
for _ in $(seq 1 100); do
    [[ -S "$control_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$control_socket" ]]
kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
after=$(sha256sum "$state/journal.h2j")
[[ "$before" == "$after" ]]

"$build/hermas2_history" "$state/journal.h2j"
echo "Independent Order Total pipeline passed."
