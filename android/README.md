# Super Mario 64 DS Android

Native port in development. **Version 0.4 runs recovered boot logic and stops at
the unlinked title scene. It is not playable.** The old face/sample test is no
longer the Android session. No emulator or ARM guest execution is used.

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

At the completed fade to scene 1, the runtime reports that `dScDSMT_c` is not yet
linked and releases the session. The title, file-selection UI, castle, Player,
level actors, 3D gameplay, HUD/minimap and game-wide sound integration remain
unfinished. Boot uses a two-screen interface; it is not the future fullscreen
stage renderer/minimap layout. No on-device compatibility or performance claim
is made. See [CHECKPOINT.md](CHECKPOINT.md) for the exact frontier.

## Build

Initialize the pinned playable-PC dependency:

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
# Private-ROM execution of the production native boot:
bash android/tests/run-game-tests.sh /absolute/path/to/game.nds
python tools/port_refcheck.py
```

The native game test runs the actual production boot, font/message dependencies,
fader and transition, checks native file ownership and repeats teardown/recreation
under ASan/UBSan. It requires the user's ROM to establish boot execution; without
a ROM public CI checks malformed NARC bounds only. Existing importer, heap, save,
input and audio tests remain. Mesa tests check screen orientation, color lanes,
sub-screen presentation and GL recreation. Host checks do not establish phone
startup, audio output or Android-driver rendering.
