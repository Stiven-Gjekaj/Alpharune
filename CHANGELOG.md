# Changelog

## 1.0.0 (2026-10-19)

Chapter 1 of DELTARUNE plays from the start to the end on the iPhone 4S.
A debug build of the app replayed all of `tests/play/chapter1.keys` on the
phone: it went through the same 146 rooms at the same frames as the Mac,
to the screen after the credits.

- The runtime uses fdlibm for sin, cos, tan, the arc functions, exp, log,
  and pow. The math library of iOS 6 and the one of macOS give different
  bits, and the phone went another way in the maze of the Field. Now the
  Mac and the phone give the same bits.
- A debug build plays `/var/mobile/Media/Alpharune/replay.keys` as fast as
  the phone can, with no sound, and writes each change of the room to
  `log.txt`. `alpharune-mac --rooms` writes the same lines.
- The Mac host and the app share the player of the key scripts (`ar_keys`).
- Checked on the Mac: the shop of Seam, the menu, the ITEM, ACT, and SPARE
  commands, and the fight with Jevil. For the fight, debug commands gave
  the Door Key and set the HP and the attack.

The replays set the HP and the attack of the party with debug commands, so
that they do not lose a battle. Chapters 2 to 5 come in later versions.

## 0.8.0 (2026-10-18)

This release measures the speed and the memory on the iPhone 4S, and
makes the later starts short.

- Each room of Chapter 1 that a warp reaches runs in less than 16 ms for
  each frame on the A5. The slowest is the middle of the town, at 15.3 ms.
  A frame can take 33 ms.
- Chapter 1 uses 114 MB of textures and at most 155 MB of resident memory.
  The textures stay RGBA8888, so that the pixel art stays exact.
- The app keeps the decoded texture pages in
  `/var/mobile/Media/Alpharune/cache/` (113 MB). The first start decodes
  them; a later start loads them about four times faster. On the 4S, the
  first frame comes after 0.5 s instead of 1.7 s, and the stops when a
  room loads its art are shorter.
- Six music files play on the phone: the game names them in upper case,
  and the runtime now finds a file in any case. One of them is the music of
  the legend.
- A debug build logs the time of the first frame and each frame that takes
  longer than 100 ms.

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
