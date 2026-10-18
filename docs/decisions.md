# Decisions

This file records each choice of the owner and each technical decision.
Each entry gives the reason. The owner made these choices on 2026-10-18.

## What the app is

- **Alpharune is a demake of Deltarune for the iPhone 4S on iOS 6.1.3**
  (jailbroken). It is a native app, installed as a .deb package.
- **The app runs the GML bytecode of Deltarune in a new C runtime.** The
  runtime reads `data.win` and runs its code in a virtual machine. Native
  code does the drawing, the audio, and the input. Reason: Deltarune has
  thousands of scripts, rooms, and cutscenes. A rewrite by hand, as in
  Balatro4S, takes too long. A runtime plays each chapter with the code of
  the game itself.
- **1.0 plays Chapter 1.** Each later chapter is one more minor release.
- **1.0 opens on the chapter select of Deltarune.** Only Chapter 1 opens.
  The other chapters show that they come in a later version.
- **1.0 is in English.** Japanese comes in a later release. Reason: the
  Japanese font has many glyphs, and the A5 has 512 MB of memory.
- **The saves use the format of the PC game.** A save from the Mac copies
  to the phone and back.

## The assets of Deltarune

- **The repository is public and holds no file of Deltarune.** The
  licence of the new code is MIT. The art, the sounds, the music, the text,
  and the code of Deltarune belong to Toby Fox.
- **The app reads the files from the copy of the owner of the phone.**
  The owner copies the game files from a bought copy to
  `/var/mobile/Media/Alpharune/`. Reason: the .deb then holds no file of
  Deltarune, as in Balatro4S.
- **The reference is the macOS release of Deltarune in
  `/Applications/DELTARUNE.app`** (bundle version 1.0.0). Each chapter is a
  file `chapterN_mac/game.ios` in `DELTARUNE.app/Contents/Resources/`. The
  SHA-1 of the Chapter 1 file is
  `37da228e674ba07cb721eb2814654fd380c2aa15`. The tools read the app from
  `/Applications`, or from the path in `DELTARUNE_APP`. The file of Chapter 1 has bytecode
  version 17 and 1680 code entries, so it runs in a VM and is not compiled
  with YYC. Reason: Deltarune has no release for Android or iOS, so an APK
  is not a copy of the game from Toby Fox.
- **The tests do not read the files of Deltarune.** A test builds the
  state that it needs.

## Identity

- Bundle ID `io.github.stivengjekaj.alpharune`, display name `Alpharune`.
- iPhone only. iOS 6.0 or later. Landscape only.
- The class prefix is `AR`.

## The screen and the controls

- **The game scales from 640 x 480 to 853 x 640 pixels in the middle of
  the screen.** Reason: this is the largest 4:3 area on the 960 x 640
  screen of the 4S.
- **A D-pad is on the left bar. The Z, X, and C buttons are on the right
  bar.** Reason: these are the keys of Deltarune.

## Toolchain

- The same toolchain as Balatro4S, Groovelet, RetroTube, and ZettaLite:
  `build.sh`, the iPhoneOS 9.3 SDK with `fix-sdk.sh`, clang for
  `armv7-apple-ios6.0`, ARM mode only, ARC for Objective-C.

## Textures and memory

- **The texture pages stay RGBA8888 in the memory of the GPU.** Reason:
  the pixel art must stay exact, and the memory is enough. Measured on the
  4S with iOS 6.1.3 (2026-10-18): Chapter 1 uses 114 MB of textures and at
  most 155 MB of resident memory in all of its rooms. The app did not stop
  in a sweep through 133 rooms. RGBA4444 or PVRTC would change the colors.
- **The decoded pages stay in `/var/mobile/Media/Alpharune/cache/`.**
  Reason: the A5 decodes a page of 2048 x 2048 in 220 to 600 ms, and reads
  it from the cache in about 100 ms. Chapter 1 decodes in 2.7 s, and loads
  from the cache in 0.7 s. The cache takes 113 MB of space. Each file name
  has a hash of the page bytes, so a new copy of the game makes new files.
- **The game files are found in any case.** Reason: GameMaker made the game
  on Windows, and the game names six music files in upper case. The file
  system of the phone sees case.

## The speed on the A5

- Measured with a debug build, 150 frames in each room (2026-10-18): each
  room of Chapter 1 that a warp reaches runs in less than 16 ms for each
  frame. The slowest is `room_town_mid`, at 15.3 ms. The game runs at 30
  frames each second, so a frame can take 33 ms.
- 13 small rooms of the town send Kris back to `room_town_mid` when a warp
  puts Kris there at this point of the story, so they have no number of
  their own.

## Releases

- Each big part of the roadmap is one minor release. A release gets a git
  tag `vX.Y.Z`, a changelog entry, a GitHub release with the .deb, and an
  entry in the Cydia repository.
- **An agent may make a full release with no review** (2026-10-18). When
  a milestone is complete and its tests pass on the phone, the agent
  pushes the commits, adds the tag, publishes the GitHub release with the
  .deb, adds the .deb to the Cydia repository, and closes the issues.
