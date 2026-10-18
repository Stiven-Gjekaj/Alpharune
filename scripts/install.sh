#!/bin/sh
# Build Alpharune.app, copy it to the phone at "ssh iphone4s", and open it.
# It also copies the files of DELTARUNE to the phone when the phone does
# not have them: the chapter select, Chapter 1, and the music.
# Usage: scripts/install.sh [--debug]
# DELTARUNE_APP sets the path of DELTARUNE.app on this Mac.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
GAME=${DELTARUNE_APP:-/Applications/DELTARUNE.app}/Contents/Resources
DEST=/var/mobile/Media/Alpharune
"$ROOT/scripts/app.sh" "$@" >/dev/null

ssh iphone4s "mkdir -p $DEST/save"
for part in game.ios options.ini chapter1_mac mus; do
    if ! ssh iphone4s "test -e $DEST/$part"; then
        [ -e "$GAME/$part" ] || { echo "install: no $part in $GAME" >&2; exit 1; }
        echo "install: copy $part to the phone" >&2
        COPYFILE_DISABLE=1 tar --no-xattrs -C "$GAME" -cf - "$part" | ssh iphone4s "tar -C $DEST -xf -"
    fi
done
ssh iphone4s "chown -R mobile:mobile $DEST"

ssh iphone4s 'killall Alpharune >/dev/null 2>&1; rm -rf /Applications/Alpharune.app'
# The sftp server of the phone cannot make a new directory, so copy with tar.
# COPYFILE_DISABLE stops macOS tar from adding "._" files.
COPYFILE_DISABLE=1 tar --no-xattrs -C "$ROOT/build" -cf - Alpharune.app | ssh iphone4s 'tar -C /Applications -xf -'
# uicache must run as the mobile user, so that SpringBoard sees the new icon.
ssh iphone4s 'cd /Applications/Alpharune.app && ldid -S Alpharune &&
    chown -R root:admin /Applications/Alpharune.app &&
    chmod 755 Alpharune &&
    su mobile -c uicache &&
    open io.github.stivengjekaj.alpharune'
