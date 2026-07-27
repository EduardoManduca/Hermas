#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build-quickstart"}
rust_target=${CARGO_TARGET_DIR:-"$root/target/quickstart"}
export CARGO_TARGET_DIR="$rust_target"
herma2="$rust_target/debug/herma2"
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
cargo build -p herma2
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build" -j2

image="$work/grade-pipeline.h2gi"
"$herma2" workflow image \
    apps/grade-pipeline/grade-pipeline.hscript2 "$image" \
    apps/grade-pipeline/grade-list.hschema2 \
    apps/grade-pipeline/mean-calculator.hschema2 \
    apps/grade-pipeline/printer.hschema2
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

grade_fp=$(fingerprint 1 1)
mean_fp=$(fingerprint 2 2)
printer_fp=$(fingerprint 3 3)
if [[ -z "$grade_fp" || -z "$mean_fp" || -z "$printer_fp" ]]; then
    echo "quickstart: could not discover Action fingerprints" >&2
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

"$build/hermas2_grade_list" "$app_socket" "$grade_fp" &
grade_pid=$!
"$build/hermas2_mean_calculator" "$app_socket" "$mean_fp" &
mean_pid=$!
"$build/hermas2_printer" "$app_socket" "$printer_fp" &
printer_pid=$!

for _ in $(seq 1 100); do
    [[ -S "$control_socket" ]] && break
    kill -0 "$daemon_pid"
    sleep 0.05
done
[[ -S "$control_socket" ]]
"$build/hermas2_run" \
    "$control_socket" 1 --image "$image"
wait "$grade_pid" "$mean_pid" "$printer_pid"

kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
"$build/hermas2_history" "$state/journal.h2j"

echo "Restarting the daemon against completed durable state..."
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
"$build/hermas2_history" "$state/journal.h2j"

echo "Hermas quickstart passed."
