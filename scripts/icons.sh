#!/bin/sh
# Make the icons of the app from the pixel heart of assets/brand/logo-source.png.
# The PNG files are in git, so that a build does not need this script.
# Usage: scripts/icons.sh
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/build"
clang -fobjc-arc -Wall "$ROOT/tools/logo.m" -framework Foundation -framework CoreGraphics -framework ImageIO \
    -o "$ROOT/build/logo"
"$ROOT/build/logo" "$ROOT/assets/brand/logo-source.png" "$ROOT/assets/brand/logo.png" 32 >/dev/null
# The icon: the heart on black, in a square. Its gloss is in the art, so
# iOS adds no gloss (UIPrerenderedIcon).
"$ROOT/build/logo" "$ROOT/assets/brand/logo-source.png" "$ROOT/build/icon-square.png" 32 000000 >/dev/null
sips -z 57 57 "$ROOT/build/icon-square.png" --out "$ROOT/resources/Icon.png" >/dev/null
sips -z 114 114 "$ROOT/build/icon-square.png" --out "$ROOT/resources/Icon@2x.png" >/dev/null
