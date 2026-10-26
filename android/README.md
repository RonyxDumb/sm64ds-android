# Super Mario 64 DS Android

Native port in development. The Android session runs the recovered boot and
original title update/render loop. No emulator or ARM guest execution is used.
Stage/Player integration is still in progress.

## Current execution

The launcher imports the European ASMP revision 0 ROM (`.nds` or one ROM in a
ZIP), validates its SHA-256 and keeps a private copy for subsequent launches.
The accepted dump is `ce5829aaa79f06c67140ed1f45cbca738b0a9d0180538c8c1498472d859db032`.

`Avvia boot originale` invokes recovered `dScBoot_c` method bodies on native-owned
leaf state. The matching `src/` and `include/` remain unchanged. This projection
does not instantiate the unported DS actor manager or copy its vtable.

The session draws the original Nintendo logo and copyright, loads the real font
and message bank, advances source faders, processes original key/stylus updates
at 59.8261 Hz and forwards boot sound archive 4 / entry 0 to the PC sequencer.
Read-only ARM9 constants and the cartridge's NARC mount table are read at runtime.
No Nintendo data is packaged in the APK or repository.

At the completed fade to scene 1, command 20's recovered loop reads all three
files, activates each through the original copy helper and computes the title's
star/coin/character summaries. The file buffer has native ownership, and the
message-bank pointer is a native-width member outside the fixed scalar settings.
The original `b7138` initialization calls the `b68e8` factory to construct shared records, all
24 item views, texture/palette records and the two-screen OAM/BG display lists.
Its 617 recovered translation units use checked four-byte handles to full-width
native addresses; their original record assertions remain active. It then sets up models,
texture/palette VRAM uploads, particle resources, sprite groups and visit state. No ARM
instructions from the cartridge are callable through these data records.

The scene now pumps the actual `b7090`/`aed98` dispatcher, `b7040` rendering
and `b6eb4` graph callbacks each frame. Original button/stylus state drives the
intro and title transitions. Geometry FIFO aliases, banked textures/palettes,
OBJ tile mapping, affine/bitmap backgrounds and display capture feed the two
screen buffers. Audio requests use real SSAR/SSEQ players, stop/fade operations
and playback status. Some SDK modulation/tempo controls remain unsupported by
the pinned sequencer.

Private-ROM execution with stylus/button input reaches the actual save menu
and emits a launch result. It is passed to the original file-launch branch;
results 3/4/5 map to slots 0/1/2 and call `StartFile`/`LoadLevel`. No launcher
code selects a file. Scene 3, actor/Player execution, collision/camera/HUD and
complete rendering fidelity remain under development. The current title uses
two screens; fullscreen gameplay/minimap composition is pending. No phone
compatibility or performance claim is made.

## Build

Initialize the pinned PC sources:

```sh
git submodule update --init --recursive
```

Use Python 3, JDK 17, SDK platform/build-tools 35, NDK 27.2.12479018 and CMake
3.22.1. Set `ANDROID_HOME` or ignored `android/local.properties`.

```sh
cd android
./gradlew --no-daemon assembleDebug assembleRelease lintDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Windows: `gradlew.bat` in `android`, or `docker_build.bat` from the repository root
with Docker Desktop. Debug is development-signed; release is unsigned. No ROM is
needed for compilation or public CI.

Outputs:
- `android/app/build/outputs/apk/debug/app-debug.apk`
- `android/app/build/outputs/apk/release/app-release-unsigned.apk`

GitHub Actions: [Android workflow](../.github/workflows/android.yml), artifact
`sm64ds-android-debug`.

## Verification

```sh
bash android/tests/run-host-tests.sh
bash android/tests/run-game-tests.sh
bash android/tests/run-frontend-tests.sh
# Private-ROM execution of the production native boot:
bash android/tests/run-game-tests.sh /absolute/path/to/game.nds
python tools/port_refcheck.py
```

The native game check runs production boot, font/message dependencies, faders,
title file preparation and the recovered launch branch. It checks distinct slots,
blank-file Yoshi defaults, native pointers, summary counts, unchanged minigame data,
stage request, controller modes/touch/camera regions and repeated teardown under
ASan/UBSan. The corrected original divider rounding gives 30-frame fades and a
120-tick boot fade followed by an active title. It requires the user's ROM for execution; without
a ROM public CI checks malformed NARC bounds only. Existing importer, heap, save,
input and audio tests remain. Mesa tests check screen orientation, color lanes,
sub-screen presentation and GL recreation. Host checks do not establish phone
startup, audio output or Android-driver rendering.
