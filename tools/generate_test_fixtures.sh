#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:-"$root/target/gate-fixtures"}
hermas=${2:-"$root/target/debug/hermas"}
mkdir -p "$output"

image() {
    local script=$1
    local destination=$2
    shift 2
    "$hermas" workflow image "$root/$script" "$output/$destination" \
        "$@"
}

image apps/grade-pipeline/grade-pipeline.hscript grade.hgi \
    "$root/apps/grade-pipeline/grade-list.hschema" \
    "$root/apps/grade-pipeline/mean-calculator.hschema" \
    "$root/apps/grade-pipeline/printer.hschema"
image apps/typed-choice/choose.hscript choice.hgi \
    "$root/apps/typed-choice/decision.hschema"
image apps/deadline/within-grade.hscript deadline.hgi \
    "$root/apps/grade-pipeline/mean-calculator.hschema"
image apps/deadline/nested-grade.hscript nested.hgi \
    "$root/apps/grade-pipeline/grade-list.hschema" \
    "$root/apps/grade-pipeline/mean-calculator.hschema"
image apps/bounded-all/cooperate.hscript parallel.hgi \
    "$root/apps/bounded-all/source.hschema" \
    "$root/apps/bounded-all/alpha.hschema" \
    "$root/apps/bounded-all/beta.hschema" \
    "$root/apps/bounded-all/sink.hschema"
image apps/bounded-all/same-app.hscript same-app.hgi \
    "$root/apps/bounded-all/source.hschema" \
    "$root/apps/bounded-all/alpha.hschema" \
    "$root/apps/bounded-all/sink.hschema"
image apps/bounded-each/batch.hscript each.hgi \
    "$root/apps/bounded-each/orders.hschema" \
    "$root/apps/bounded-each/reporting.hschema" \
    "$root/apps/bounded-each/archive.hschema"
image apps/saga/fulfill.hscript saga.hgi \
    "$root/apps/saga/payment.hschema" \
    "$root/apps/saga/shipping.hschema"

sha256sum "$output"/*.hgi
