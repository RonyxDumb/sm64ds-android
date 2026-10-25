# Super Mario 64 DS Android

Native Android port in development. **This APK is an importer and native asset
verification/backend-test build, not a playable game.** No emulator or ARM guest execution is
included. Original decompilation sources and their build configuration are preserved.

## Current build

- Java launcher, landscape immersive layout, Android Storage Access Framework.
- One `.nds` file or one ROM inside a ZIP. Archive paths are never written to disk.
- Bounded background import (128 MiB including ignored ZIP entries), progress,
  ZIP CRC checks, staged writes and atomic replacement of the previous ROM.
- Full SHA-256 validation against the tested European ASMP revision 0 dump:
  `ce5829aaa79f06c67140ed1f45cbca738b0a9d0180538c8c1498472d859db032`.
  Modified, trimmed and other revision dumps are currently rejected.
- Private ROM copy survives process restarts without the original document URI;
  persistent URI permissions are released after copying, so they do not accumulate.
- JNI ARM64 library using the existing `port/ntr/fs.cpp` and `lz77.cpp`, plus
  the unchanged `src/DotVec3.c` and `src/CrossVec3.c` game implementations.
- Native filesystem mount, fixed-point math self-test, Mario model read/decompression.
  These operations run automatically for the saved ROM on subsequent launches.
- Explicit backend test: ES3 renders the authentic title-face model; virtual
  multitouch controls feed native input; A plays a real SDAT sample and B selects
  the next. These diagnostic bindings do not implement recovered player gameplay.
- AAudio output with audio-focus and lifecycle handling. The pinned PC
  SSEQ sequencer/mixer and a pointer-safe SBNK/SWAR adapter are integrated.
  X plays sequence 58; Y stops. Game-driven sound commands are still pending.
- The POSIX VBlank scheduler is compiled and host-tested; original game entry
  point integration is still pending.

This stores the cartridge privately and indexes it on verification; it does not yet
build a GPU asset cache. It does not initialize the actor/game scene runtime.

## Build

First run `git submodule update --init --recursive` (or clone with `--recurse-submodules`).
See [PC_PORT.md](PC_PORT.md) for the pinned playable PC release and integration status.
Use Python 3, JDK 17, Android SDK platform/build-tools 35, NDK 27.2.12479018 and CMake 3.22.1.
Set `ANDROID_HOME` (or create ignored `android/local.properties` with `sdk.dir=...`).

```sh
cd android
./gradlew assembleDebug lintDebug
./gradlew assembleRelease
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

On Windows, use `gradlew.bat` inside `android`, or run `docker_build.bat` from the
repository root with Docker Desktop in Linux container mode. Docker installs the
pinned SDK/NDK/CMake packages, runs host tests and builds/lints the debug APK.
The Gradle wrapper pins and verifies the Gradle distribution SHA-256.

Debug output: `android/app/build/outputs/apk/debug/app-debug.apk`.
Release output: `android/app/build/outputs/apk/release/app-release-unsigned.apk`;
release signing is intentionally left to the owner. The debug APK is signed with
a development key. No ROM is needed for either build or public CI.

GitHub Actions: [Android workflow](../.github/workflows/android.yml), artifact
`sm64ds-android-debug`. A workflow build does not establish device compatibility.

## Tests

```sh
bash android/tests/run-host-tests.sh
# Optionally exercise native reads on your own supported ROM:
bash android/tests/run-host-tests.sh /absolute/path/to/game.nds
python tools/port_refcheck.py
```

Host tests cover recovered game math, LZ77 bounds, filesystem offset overflow,
FAT extent validation, truncated name tables, unsafe names, cyclic directory
graphs, failed-mount state, raw/ZIP imports, duplicate ROM entries, decompression
bomb limits and interrupted provider reads. ASan/UBSan are enabled. To test the
provided archive locally, pass it to `RomImportTest` after compiling the Java test;
this also checks the full digest and detects an altered payload.

## Next integration work

1. Recover pointer-independent layouts for the game runtime. `port/host_abi.h`
   currently rejects 64-bit pointers; bypassing it corrupts layout assumptions.
   Port files use 32-bit guest fields, fixed offsets and host constructor/vtable
   bridges. Start with heap/root memory and verify each bridge on ARM64.
2. Connect the new POSIX `rt_run` scheduler to the recovered boot/frame loop and
   Android lifecycle. The backend viewer does not execute that entry point.
   Keep simulation timing independent from display refresh.
3. Close scene/startup dependencies: the PC CMake tree builds smoke executables,
   not an application linking the full boot, save selection and castle path.
4. The GPU backend now consumes the geometry pipeline for the title-face asset.
   Connect scene submissions, animation, 2D scanout/HUD/minimap and touch mapping.
5. Connect the tested multitouch state to recovered input globals; add physical
   controllers. Complete SSEQ/SSAR/SBNK playback and connect the new sample mixer
   and AAudio backend to game sound commands, then saves/device profiling.

The upstream `PORTED` percentage tracks its source-port frontier; it is not an
Android playability percentage. See [CHECKPOINT.md](CHECKPOINT.md) for evidence.
