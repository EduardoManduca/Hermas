#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build-workspace-compatibility"}
rust_target=${CARGO_TARGET_DIR:-"$root/target/workspace-compatibility"}
export CARGO_TARGET_DIR="$rust_target"
hermas="$rust_target/debug/hermas"
work=$(mktemp -d "${TMPDIR:-/tmp}/hermas-workspace-compat-XXXXXX")
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

grade="$work/grade.hgi"
order="$work/order.hgi"
"$hermas" workflow image \
    apps/grade-pipeline/grade-pipeline.hscript "$grade" \
    apps/grade-pipeline/grade-list.hschema \
    apps/grade-pipeline/mean-calculator.hschema \
    apps/grade-pipeline/printer.hschema
"$hermas" workflow image \
    apps/order-total/order-total.hscript "$order" \
    apps/order-total/discount.hschema \
    apps/order-total/tax.hschema \
    apps/order-total/receipt.hschema

workspace="$work/runtime"
"$build/hermasd" --workspace "$workspace" "$grade" 7 &
daemon_pid=$!
for _ in $(seq 1 100); do
    [[ -S "$workspace/control.sock" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$workspace/control.sock" ]]
kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=

[[ $(stat -c '%a' "$workspace") == 700 ]]
[[ $(stat -c '%a' "$workspace/state") == 700 ]]
[[ $(stat -c '%a' "$workspace/manifest.hwm") == 600 ]]
[[ $(stat -c '%a' "$workspace/workflow.hgi") == 600 ]]

if "$build/hermasd" --workspace "$workspace" "$grade" 8 \
    >"$work/rebind-id.out" 2>"$work/rebind-id.err"; then
    echo "workspace compatibility: workflow ID rebind succeeded" >&2
    exit 1
fi
grep -F "workspace binding failed: incompatible" \
    "$work/rebind-id.err"

if "$build/hermasd" --workspace "$workspace" "$order" 7 \
    >"$work/rebind-image.out" 2>"$work/rebind-image.err"; then
    echo "workspace compatibility: image rebind succeeded" >&2
    exit 1
fi
grep -F "workspace binding failed: incompatible" \
    "$work/rebind-image.err"

printf '\002' |
    dd of="$workspace/manifest.hwm" bs=1 seek=4 count=1 \
        conv=notrunc status=none
if "$build/hermasd" --workspace "$workspace" \
    >"$work/version.out" 2>"$work/version.err"; then
    echo "workspace compatibility: unsupported manifest started" >&2
    exit 1
fi
grep -F "workspace binding failed: incompatible" "$work/version.err"
if "$build/hermas_history" --workspace "$workspace" \
    >"$work/history.out" 2>"$work/history.err"; then
    echo "workspace compatibility: incompatible state was inspected" >&2
    exit 1
fi
grep -F "workspace binding failed: incompatible" "$work/history.err"
if "$build/hermas_run" --workspace "$workspace" 1 \
    >"$work/run.out" 2>"$work/run.err"; then
    echo "workspace compatibility: incompatible workspace was called" >&2
    exit 1
fi
grep -F "workspace binding failed: incompatible" "$work/run.err"

printf '\001' |
    dd of="$workspace/manifest.hwm" bs=1 seek=4 count=1 \
        conv=notrunc status=none
"$build/hermasd" --workspace "$workspace" &
daemon_pid=$!
for _ in $(seq 1 100); do
    [[ -S "$workspace/control.sock" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$workspace/control.sock" ]]
kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=

echo "Managed workspace compatibility integration passed."
