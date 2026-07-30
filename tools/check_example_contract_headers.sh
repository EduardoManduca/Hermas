#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
hermas=${1:-"$root/target/debug/hermas"}
work=$(mktemp -d "${TMPDIR:-/tmp}/hermas-contract-headers-XXXXXX")

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

check_header() {
    local schema=$1
    local committed=$2
    local prefix=$3
    local generated="$work/$(basename "$committed")"

    "$hermas" schema c-header "$root/$schema" "$generated" "$prefix" \
        >/dev/null
    if ! cmp -s "$root/$committed" "$generated"; then
        echo "$committed is stale; regenerate it with hermas schema c-header" >&2
        diff -u "$root/$committed" "$generated" || true
        return 1
    fi
}

check_header \
    apps/grade-pipeline/grade-list.hschema \
    apps/grade-pipeline/grade-list.contract.h \
    HERMAS_GRADE_LIST
check_header \
    apps/grade-pipeline/mean-calculator.hschema \
    apps/grade-pipeline/mean-calculator.contract.h \
    HERMAS_MEAN_CALCULATOR
check_header \
    apps/grade-pipeline/printer.hschema \
    apps/grade-pipeline/printer.contract.h \
    HERMAS_PRINTER
check_header \
    apps/order-total/discount.hschema \
    apps/order-total/discount.contract.h \
    HERMAS_DISCOUNT
check_header \
    apps/order-total/tax.hschema \
    apps/order-total/tax.contract.h \
    HERMAS_TAX
check_header \
    apps/order-total/receipt.hschema \
    apps/order-total/receipt.contract.h \
    HERMAS_RECEIPT

echo "Generated example contract headers are current."
