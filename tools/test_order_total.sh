#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build-order-total"}
rust_target=${CARGO_TARGET_DIR:-"$root/target/order-total"}
export CARGO_TARGET_DIR="$rust_target"
hermas="$rust_target/debug/hermas"
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
cargo build -p hermas
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build" -j2

image="$work/order-total.hgi"
"$hermas" workflow image \
    apps/order-total/order-total.hscript "$image" \
    apps/order-total/discount.hschema \
    apps/order-total/tax.hschema \
    apps/order-total/receipt.hschema
chmod 600 "$image"

description=$("$build/hermas_image_check" --describe "$image")
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
"$build/hermasd" \
    "$image" 1 "$state" "$app_socket" "$control_socket" &
daemon_pid=$!
for _ in $(seq 1 100); do
    [[ -S "$app_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$app_socket" ]]

"$build/hermas_discount" "$app_socket" "$discount_fp" &
discount_pid=$!
"$build/hermas_tax" "$app_socket" "$tax_fp" &
tax_pid=$!
"$build/hermas_receipt" \
    "$app_socket" "$receipt_fp" >"$work/receipt.out" &
receipt_pid=$!

for _ in $(seq 1 100); do
    [[ -S "$control_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$control_socket" ]]

# The graph-aware runner encodes the decimal HSchema Integer. Canonical
# hexadecimal remains available through --hex for exact-byte automation.
if "$build/hermas_run" \
    "$control_socket" 40 --image "$image" --value not-an-integer \
    >"$work/invalid.out" 2>"$work/invalid.err"; then
    echo "order-total: invalid scalar input was accepted" >&2
    exit 1
fi
grep -F "invalid argument or value" "$work/invalid.err"
run_output=$("$build/hermas_run" \
    "$control_socket" 41 --image "$image" --value 10000)
grep -F "outcome=success" <<<"$run_output"
grep -F "display=true" <<<"$run_output"
wait "$discount_pid" "$tax_pid" "$receipt_pid"
grep -Fx "Order total: 9900 cents" "$work/receipt.out"

kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
before=$(sha256sum "$state/journal.hj")

"$build/hermasd" \
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
after=$(sha256sum "$state/journal.hj")
[[ "$before" == "$after" ]]

"$build/hermas_history" "$state/journal.hj"
echo "Independent Order Total pipeline passed."
