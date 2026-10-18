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

## The assets of Deltarune

- **The repository is public and holds no file of Deltarune.** The
  licence of the new code is MIT. The art, the sounds, the music, the text,
  and the code of Deltarune belong to Toby Fox.
- **The app reads the files from the copy of the owner of the phone.**
  The owner copies the game files from a bought copy to
  `/var/mobile/Media/Alpharune/`. Reason: the .deb then holds no file of
  Deltarune, as in Balatro4S.
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

## Releases

- Each big part of the roadmap is one minor release. A release gets a git
  tag `vX.Y.Z`, a changelog entry, a GitHub release with the .deb, and an
  entry in the Cydia repository.
- **An agent may make a full release with no review** (2026-10-18). When
  a milestone is complete and its tests pass on the phone, the agent
  pushes the commits, adds the tag, publishes the GitHub release with the
  .deb, adds the .deb to the Cydia repository, and closes the issues.
