#!/bin/sh
# Build Alpharune.app into build/.
# Usage: scripts/app.sh [--debug]
# With --debug, the app writes its log to /var/mobile/Media/Alpharune/log.txt
# and reads commands from /tmp/Alpharune.cmd (see scripts/tap.sh).
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$ROOT/build/Alpharune.app"
DEBUG=""
if [ "$1" = "--debug" ]; then DEBUG="-DAR_DEBUG"; fi
rm -rf "$APP"
mkdir -p "$APP"

# shellcheck disable=SC2046
"$ROOT/build.sh" "$APP/Alpharune" $DEBUG -I"$ROOT/src/core" -I"$ROOT/src/gfx" -I"$ROOT/src/ui" \
    $(ls "$ROOT"/src/core/*.c "$ROOT"/src/third_party/fdlibm/*.c "$ROOT"/src/gfx/*.c "$ROOT"/src/ui/*.m) \
    -lz -lbz2 -framework Foundation -framework UIKit -framework CoreGraphics -framework QuartzCore \
    -framework OpenGLES -framework AudioToolbox \
    2>&1 | grep -v 'liblaunch.dylib' >&2 || true
[ -f "$APP/Alpharune" ]

cp "$ROOT/resources/Info.plist" "$APP/Info.plist"
cp "$ROOT"/resources/*.png "$APP/"
printf 'APPL????' > "$APP/PkgInfo"
echo "$APP"
