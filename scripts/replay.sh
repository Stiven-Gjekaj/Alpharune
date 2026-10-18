#!/bin/sh
# Replay the play of Chapter 1 on this Mac, and check that it reaches the
# end of the chapter.
# Usage: scripts/replay.sh
# DELTARUNE_APP sets the path of DELTARUNE.app on this Mac.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
GAME=${DELTARUNE_APP:-/Applications/DELTARUNE.app}/Contents/Resources
# The frame and the room at the end of tests/play/chapter1.keys.
FRAMES=397900
ROOM=room_chapter_continue
[ -x "$ROOT/build/alpharune-mac" ] || "$ROOT/scripts/mac.sh" >/dev/null
OUT=$(mktemp -d "${TMPDIR:-/tmp}/alpharune-replay.XXXXXX")
"$ROOT/build/alpharune-mac" "$GAME" --frames $FRAMES --shot 0 --script "$ROOT/tests/play/chapter1.keys" \
    --out "$OUT" >"$OUT/log.txt" 2>&1 || true
rm -rf "$OUT/frames"
if grep -q "^frame $FRAMES: room $ROOM\$" "$OUT/log.txt"; then
    echo "replay: frame $FRAMES is in $ROOM"
    rm -rf "$OUT"
else
    echo "replay: frame $FRAMES is not in $ROOM; see $OUT/log.txt" >&2
    grep "^frame $FRAMES\|end at\|fault" "$OUT/log.txt" >&2 || true
    exit 1
fi
