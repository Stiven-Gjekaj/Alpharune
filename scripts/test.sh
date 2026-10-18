#!/bin/sh
# Build and run the tests.
# Usage: scripts/test.sh [--phone] [NAME]
# With no option, the tests of the core operate on this Mac.
# With --phone, the same tests operate on the iPhone at "ssh iphone4s".
# NAME runs only the tests with NAME in their name.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/tests"
mkdir -p "$OUT"
CORE=$(ls "$ROOT"/src/core/*.c 2>/dev/null || true)
FDLIBM=$(ls "$ROOT"/src/third_party/fdlibm/*.c)
TESTS=$(ls "$ROOT"/tests/*.m "$ROOT"/tests/core/*.m 2>/dev/null || true)

if [ "$1" = "--phone" ]; then
    shift
    # shellcheck disable=SC2086
    "$ROOT/build.sh" "$OUT/artests-armv7" -I"$ROOT/src/core" -I"$ROOT/tests" \
        $CORE $FDLIBM $TESTS -lz -lbz2 -framework Foundation >"$OUT/build.log" 2>&1 ||
        { grep -v 'liblaunch.dylib' "$OUT/build.log" >&2; exit 1; }
    ssh iphone4s 'mkdir -p /tmp/artests'
    scp -q "$OUT/artests-armv7" iphone4s:/tmp/artests/artests
    ssh iphone4s "cd /tmp/artests && ldid -S artests && ./artests $*"
else
    # fdlibm keeps its own warnings, so it compiles alone.
    mkdir -p "$OUT/fdlibm"
    for c in $FDLIBM; do
        clang -ffp-contract=off -w -g -mmacosx-version-min=10.9 -c "$c" -o "$OUT/fdlibm/$(basename "$c" .c).o"
    done
    # shellcheck disable=SC2086
    clang -fobjc-arc -ffp-contract=off -Wall -Werror -g -mmacosx-version-min=10.9 -I"$ROOT/src/core" -I"$ROOT/tests" \
        $CORE "$OUT"/fdlibm/*.o $TESTS -lz -lbz2 -framework Foundation -o "$OUT/artests"
    "$OUT/artests" "$@"
fi
