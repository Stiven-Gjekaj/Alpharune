#!/bin/sh
# Send debug commands to a debug build on the phone at "ssh iphone4s".
# Usage: scripts/tap.sh "key Z" ["hold RIGHT 30" "instances" ...]
# "key K" taps a key, "hold K N" holds it for N frames, and the other lines
# go to ar_run_debug, for example "room room_town".
set -e

[ $# -gt 0 ] || { echo "usage: scripts/tap.sh \"key Z\" ..." >&2; exit 2; }
COMMANDS=$(printf '%s\n' "$@")
ssh iphone4s "printf '%s\n' \"$COMMANDS\" > /tmp/Alpharune.cmd"
