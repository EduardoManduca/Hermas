#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:-"$root/target/gate-fixtures"}
herma2=${2:-"$root/target/debug/herma2"}
mkdir -p "$output"

image() {
    local script=$1
    local destination=$2
    shift 2
    "$herma2" workflow image "$root/$script" "$output/$destination" \
        "$@"
}

image apps/grade-pipeline/grade-pipeline.hscript2 grade.h2gi \
    "$root/apps/grade-pipeline/grade-list.hschema2" \
    "$root/apps/grade-pipeline/mean-calculator.hschema2" \
    "$root/apps/grade-pipeline/printer.hschema2"
image apps/typed-choice/choose.hscript2 choice.h2gi \
    "$root/apps/typed-choice/decision.hschema2"
image apps/deadline/within-grade.hscript2 deadline.h2gi \
    "$root/apps/grade-pipeline/mean-calculator.hschema2"
image apps/deadline/nested-grade.hscript2 nested.h2gi \
    "$root/apps/grade-pipeline/grade-list.hschema2" \
    "$root/apps/grade-pipeline/mean-calculator.hschema2"
image apps/bounded-all/cooperate.hscript2 parallel.h2gi \
    "$root/apps/bounded-all/source.hschema2" \
    "$root/apps/bounded-all/alpha.hschema2" \
    "$root/apps/bounded-all/beta.hschema2" \
    "$root/apps/bounded-all/sink.hschema2"
image apps/bounded-all/same-app.hscript2 same-app.h2gi \
    "$root/apps/bounded-all/source.hschema2" \
    "$root/apps/bounded-all/alpha.hschema2" \
    "$root/apps/bounded-all/sink.hschema2"
image apps/bounded-each/batch.hscript2 each.h2gi \
    "$root/apps/bounded-each/orders.hschema2" \
    "$root/apps/bounded-each/reporting.hschema2" \
    "$root/apps/bounded-each/archive.hschema2"
image apps/saga/fulfill.hscript2 saga.h2gi \
    "$root/apps/saga/payment.hschema2" \
    "$root/apps/saga/shipping.hschema2"

sha256sum "$output"/*.h2gi
