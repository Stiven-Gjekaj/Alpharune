#!/bin/sh
# Build alpharune-mac, the runtime for the Mac with no window, into build/.
# Usage: scripts/mac.sh [--debug]
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPT="-O2"
if [ "$1" = "--debug" ]; then OPT="-O0 -g -fsanitize=address"; fi
mkdir -p "$ROOT/build/fdlibm-mac"
# fdlibm keeps its own warnings, so it compiles alone.
for c in "$ROOT"/src/third_party/fdlibm/*.c; do
    # shellcheck disable=SC2086
    clang $OPT -ffp-contract=off -w -mmacosx-version-min=10.13 -c "$c" -o "$ROOT/build/fdlibm-mac/$(basename "$c" .c).o"
done
# shellcheck disable=SC2086
clang $OPT -fobjc-arc -ffp-contract=off -Wall -Werror -mmacosx-version-min=10.13 \
    -I"$ROOT/src/core" -I"$ROOT/src/gfx" \
    "$ROOT"/src/core/*.c "$ROOT"/build/fdlibm-mac/*.o "$ROOT"/src/gfx/ar_gl.c "$ROOT"/src/mac/ar_mac.m \
    -lz -lbz2 -framework Foundation -framework OpenGL -framework ImageIO -framework CoreGraphics \
    -o "$ROOT/build/alpharune-mac"
echo "$ROOT/build/alpharune-mac"
