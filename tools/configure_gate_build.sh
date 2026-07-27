#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 BUILD_DIR C_COMPILER FIXTURE_DIR" >&2
    exit 2
fi

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=$1
compiler=$2
fixtures=$(cd "$3" && pwd)

cmake -S "$root" -B "$build" \
    -DCMAKE_C_COMPILER="$compiler" \
    -DHERMAS2_SANITIZE=ON \
    -DHERMAS2_TEST_IMAGE="$fixtures/grade.h2gi" \
    -DHERMAS2_TEST_CHOICE_IMAGE="$fixtures/choice.h2gi" \
    -DHERMAS2_TEST_DEADLINE_IMAGE="$fixtures/deadline.h2gi" \
    -DHERMAS2_TEST_NESTED_DEADLINE_IMAGE="$fixtures/nested.h2gi" \
    -DHERMAS2_TEST_PARALLEL_IMAGE="$fixtures/parallel.h2gi" \
    -DHERMAS2_TEST_SAME_APP_IMAGE="$fixtures/same-app.h2gi" \
    -DHERMAS2_TEST_EACH_IMAGE="$fixtures/each.h2gi" \
    -DHERMAS2_TEST_SAGA_IMAGE="$fixtures/saga.h2gi"
