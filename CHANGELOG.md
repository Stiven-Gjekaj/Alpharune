# Changelog

## 0.7.0 (2026-10-18)

Chapter 1 plays to the end. A replay on the Mac goes from the chapter
select through the Field, the Great Board, the Forest, Card Castle, the
King, the fountain, and the night, to the screen after the credits.

- The replay `tests/play/chapter1.keys` reaches the end of Chapter 1 at
  frame 397900. `scripts/replay.sh` checks this in about 80 seconds on the
  Mac, and `scripts/release.sh` runs it before each release.
- The VM reads and writes `inst.arr[i]` on the correct instance. Before,
  it used the instance of the code, and the Clover puzzle in the Forest
  did not open.
- The app icon has a transparent background on the home screen.
- A debug build logs the resident memory with the speed of each room.
- The debug command `get NAME INDEX` shows one item of a global array.

The replay sets the HP and the attack of the party with debug commands,
so that it does not lose a battle. It does not use the shops, the menus,
the ITEM and SPARE commands, or the secret boss yet. Nobody has played
all of Chapter 1 on the phone yet. Version 1.0 does these.

## 0.6.0 (2026-10-18)

This is the first release. It holds the work of the milestones 0.1 to 0.6.
Chapter 1 plays from the start to the Field. The rest of Chapter 1 comes
in 0.7.

- A virtual machine runs the GML bytecode of DELTARUNE (bytecode version
  17): the values, the arrays, the structs, the methods, `with`, and the
  variable scopes of GameMaker 2.3.
- The runtime runs the rooms, the layers, the tiles, the views, the
  instances, the events, the alarms, the collisions, and the persistent
  rooms, in the order of GameMaker.
- The texture pages of the game decode on the phone (QOI in bzip2), and
  OpenGL ES 2.0 draws the sprites, the fonts, the surfaces, and the blend
  modes.
- The game shows at 853 x 640 in landscape. A D-pad on the left and the Z,
  X, and C buttons on the right control it.
- The sounds and the music play through a mixer, with stb_vorbis for the
  Ogg files. Nobody has listened to them on the phone yet.
- The saves are text files in the format of the PC game. A save from a
  save point loads again.
- The chapter select opens Chapter 1. A chapter that is not in this
  version shows a notice, and the chapter select opens again.
- Chapter 1 plays from the start through the school, the Dark World,
  Castle Town, the first battles, and the Field to the box puzzle. A
  replay on the Mac (`tests/play/chapter1.keys`) checks this path.
- The first room of the Dark World runs in about 4.5 ms each frame on the
  A5. The first start decodes the textures and takes a few seconds.
