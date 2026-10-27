# Super Mario 64 DS — Android native port

This repository focuses on building a native **ARM64 Android** port of *Super Mario 64 DS* using recovered game source. It is not an emulator. **The port is still in development and is not yet a complete, verified playable game.**

The Android runtime currently executes recovered boot and title/menu logic, including save-file selection and the transition request to scene 3. Full stage execution, actors, Player, collision, camera, HUD/minimap and final graphics fidelity remain integration work.

## Build an APK

Requires Python 3, JDK 17, Android SDK platform 35, NDK `27.2.12479018`, CMake `3.22.1` and Gradle. Initialize the pinned audio dependency:

```sh
git submodule update --init --recursive
cd android
./gradlew --no-daemon assembleDebug assembleRelease lintDebug
```

The debug APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`. Windows users can run `android/gradlew.bat` or the root `docker_build.bat`.

[Android build and runtime guide](android/README.md) · [Current integration checkpoint](android/CHECKPOINT.md) · [PC audio dependency](android/PC_PORT.md) · [GitHub Actions builds](https://github.com/RonyxDumb/sm64ds-android/actions/workflows/android.yml)

## Repository layout

- `android/`: launcher, native runtime, OpenGL ES, audio, diagnostics, build scripts and tests
- `src/` and `include/`: original recovered game code retained intact for continuing the native port; **not** all units are ARM64-compatible yet
- `port/ntr/`: Nintendo DS hardware/resource decoding and rendering compatibility layer
- `port/tools/hostgen.py`: audited source transformations
- `config/arm9/`: the three symbol maps required to regenerate the Android frontend manifest
- `external/sm64ds-pc`: pinned upstream PC audio/sequencer dependency (Git submodule)

A diagnostic Android ARM64 compilation of all raw `src/` units is available through the [compile census workflow](https://github.com/RonyxDumb/sm64ds-android/actions/workflows/android-compile-audit.yml). Compile-only success is not equivalent to correct linkage or runtime behavior.

## Assets and credits

No game ROM, Nintendo assets or extracted game data are included. Users must supply their own compatible game dump locally. See [LICENSE](LICENSE), [CREDITS.md](CREDITS.md) and `android/app/src/main/assets/THIRD_PARTY_NOTICES.txt` for source credits and notices.
