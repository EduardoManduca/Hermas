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
    apps/order-total/receipt.hschema \
    apps/order-total/discount.hschema \
    apps/order-total/tax.hschema
chmod 600 "$image"

workspace="$work/runtime"
app_socket="$workspace/apps.sock"
control_socket="$workspace/control.sock"
"$build/hermasd" --workspace "$workspace" "$image" 1 &
daemon_pid=$!
for _ in $(seq 1 100); do
    [[ -S "$app_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$app_socket" ]]

# Semantic identity resolves graph-local assignments. A binary whose
# contract is absent must be rejected without claiming numeric IDs.
if "$build/hermas_grade_list" --workspace "$workspace" \
    >"$work/wrong-app.out" 2>"$work/wrong-app.err"; then
    echo "order-total: wrong Action binary was accepted" >&2
    exit 1
fi
grep -F \
    "workspace Action identity failed: action-not-found" \
    "$work/wrong-app.err"

"$build/hermas_discount" --workspace "$workspace" &
discount_pid=$!
"$build/hermas_tax" --workspace "$workspace" &
tax_pid=$!
"$build/hermas_receipt" \
    --workspace "$workspace" >"$work/receipt.out" &
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
    --workspace "$workspace" 40 --value not-an-integer \
    >"$work/invalid.out" 2>"$work/invalid.err"; then
    echo "order-total: invalid scalar input was accepted" >&2
    exit 1
fi
grep -F "invalid argument or value" "$work/invalid.err"
run_output=$("$build/hermas_run" \
    --workspace "$workspace" 41 --value 10000)
grep -F "outcome=success" <<<"$run_output"
grep -F "display=true" <<<"$run_output"
wait "$discount_pid" "$tax_pid" "$receipt_pid"
grep -Fx "Order total: 9900 cents" "$work/receipt.out"

kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
before=$(sha256sum "$workspace/state/journal.hj")

"$build/hermasd" --workspace "$workspace" &
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
after=$(sha256sum "$workspace/state/journal.hj")
[[ "$before" == "$after" ]]

"$build/hermas_history" --workspace "$workspace"
"$build/hermas_history" --json --workspace "$workspace" \
    >"$work/history.jsonl"
python3 tests/test_history_json.py completed "$work/history.jsonl" \
    --workflow 1 --execution 41 --outcome success
echo "Independent Order Total pipeline passed."
