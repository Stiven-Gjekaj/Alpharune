# Roadmap

Each release adds one big part. The risky work comes first: the bytecode
VM, the textures, and the speed on the A5. The reason for each choice is in
[docs/decisions.md](docs/decisions.md). Each release is a milestone on
GitHub, and each item below is an issue in that milestone.

The counts come from the files of the reference copy, DELTARUNE.app 1.0.0.
`chapter1_mac/game.ios` has 147 rooms, 353 objects, 1093 sprites, 692
scripts, 1680 code entries, 151 sounds, 12 fonts, and 8 texture pages. It
calls 354 functions that are not its own scripts. The chapter select,
`game.ios`, has 3 rooms and 17 objects, and calls 14 more functions.

## 0.1: the foundation (done, 2026-10-18)

- Add the app, install, test, and package scripts, and the Cydia
  packaging, from Balatro4S.
- Read the chunks of `game.ios`: GEN8, STRG, SPRT, TPAG, TXTR, FONT, ROOM,
  OBJT, SCPT, CODE, VARI, FUNC, SOND, and AUDO.
- Decode the texture pages. They are QOI images in a bzip2 stream
  (`2zoq`).
- Draw a sprite from a texture page with OpenGL ES 2.0, in landscape, at
  853 x 640 in the middle of the screen.
- Draw the D-pad on the left bar and the Z, X, and C buttons on the right
  bar, and read the touches.

## 0.2: the bytecode VM (done, 2026-10-18)

- Run each instruction of bytecode version 17: the stack, the arithmetic,
  the compare, the branch, the call, and the return.
- Add the values: real, string, array, struct, method, and undefined.
- Add the variable scopes: self, other, global, local, argument, and the
  builtin variables of an instance.
- Add the structs and the methods of GML 2.3: `@@NewGMLObject@@`,
  `@@NewGMLArray@@`, `method`, and `@@This@@`.
- Add `with` over an object, an instance, `all`, and `other`.
- Run the code of `game.ios` on the Mac in a headless test.
- Replay a play of Chapter 1 on the Mac, with no window, from a script of
  keys (`tests/play/chapter1.keys`).

## 0.3: objects and rooms (done, 2026-10-18)

- Create and destroy instances, with the parent objects.
- Run the events in the order of GameMaker: create, begin step, alarm,
  step, end step, draw, draw GUI, room start, room end, user events, and
  destroy.
- Load a room: its instances, its creation code, its layers, its tiles,
  and its backgrounds.
- Add the layer functions (47 in Chapter 1), the views, and the cameras.
- Add the collisions: the bounding boxes, the precise masks,
  `place_meeting`, and `collision_*`.
- Sort the draw by depth.

## 0.4: the drawing (done, 2026-10-18)

- Port the `draw_*` functions that the chapters call (49 in Chapter 1).
- Draw the 12 fonts of Chapter 1 and the 10 fonts of the chapter select.
- Add the color, the alpha, the blend modes, and `merge_color`.
- Add the surfaces, if the chapters use them.
- Play the chapter select. Only Chapter 1 opens. The other chapters show
  that they come in a later version.

## 0.5: input and sound (done, 2026-10-18)

- Connect the pads to `keyboard_check`, `keyboard_check_pressed`, and
  `keyboard_check_released`.
- Decode the Ogg sounds with stb_vorbis, and play them.
- Stream the music from the 339 files in `mus/`.
- Port the audio functions (18 in Chapter 1).

## 0.6: files and saves (done, 2026-10-18)

- Port the `file_text_*`, `ini_*`, `buffer_*`, and `json_*` functions.
- Read and write the saves in the format of the PC game.
- Open Chapter 1 from the chapter select.

## 0.7: Chapter 1 plays to the end (done, 2026-10-18)

- Play the light world: the school, the town, and the house.
- Play the dark world: Castle Town, the Field, the Great Board, and the
  Forest.
- Play Card Castle and the end of the chapter.
- Play the battles: the FIGHT, ACT, MAGIC, and DEFEND commands, the TP, and
  the bullet box.
- Check the replay to the end of Chapter 1 before each release
  (`scripts/replay.sh`).

## 0.8: the speed and the memory

- Measure the frame rate of each room on the A5, and fix each slow room.
- Measure the memory. The 7 texture pages of 2048 x 2048 take 112 MiB
  as RGBA8888. Find a format that keeps the pixel art sharp and fits the
  512 MB of the 4S.
- Measure the start time, and keep the first start and the later starts
  short. Keep the decoded textures in a cache in
  `/var/mobile/Media/Alpharune/`.

## 1.0.0

- Play the shops, the menus, the ITEM and SPARE commands, and the secret
  boss.
- Play Chapter 1 from the start to the end on the phone, and fix each
  difference with the Mac copy. Compare the VM with the GameMaker runtime
  of the Mac copy where a difference is not clear.
- Publish the release on GitHub with the .deb, and add it to the Cydia
  repository.

## After 1.0

- One minor release for each of Chapters 2, 3, 4, and 5.
- Japanese text, with its font.
