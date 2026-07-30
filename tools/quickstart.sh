#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build-quickstart"}
rust_target=${CARGO_TARGET_DIR:-"$root/target/quickstart"}
export CARGO_TARGET_DIR="$rust_target"
hermas="$rust_target/debug/hermas"
work=$(mktemp -d "${TMPDIR:-/tmp}/hermas-quickstart-XXXXXX")
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

image="$work/grade-pipeline.hgi"
"$hermas" workflow image \
    apps/grade-pipeline/grade-pipeline.hscript "$image" \
    apps/grade-pipeline/grade-list.hschema \
    apps/grade-pipeline/mean-calculator.hschema \
    apps/grade-pipeline/printer.hschema
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

"$build/hermas_grade_list" --workspace "$workspace" &
grade_pid=$!
"$build/hermas_mean_calculator" --workspace "$workspace" &
mean_pid=$!
"$build/hermas_printer" --workspace "$workspace" &
printer_pid=$!

for _ in $(seq 1 100); do
    [[ -S "$control_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$control_socket" ]]
"$build/hermas_run" \
    --workspace "$workspace" 1
wait "$grade_pid" "$mean_pid" "$printer_pid"

kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
"$build/hermas_history" --workspace "$workspace"

echo "Restarting the daemon against completed durable state..."
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
"$build/hermas_history" --workspace "$workspace"

echo "Hermas quickstart passed."
