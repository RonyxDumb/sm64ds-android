# Android checkpoint — 2026-10-25

Status: phase 1 foundation; phase 2 filesystem/math slice only. Game is not playable.

## Validation completed

- Gradle 8.9 / AGP 8.7.3: `assembleDebug assembleRelease lintDebug` succeeded.
  Debug APK signed, release APK unsigned. Lint has warnings, zero errors.
- NDK 27.2.12479018 compiled and linked the library for `arm64-v8a`.
- Host C++ tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
  Leak detection disabled locally because this execution environment prevents
  LeakSanitizer process inspection; CI keeps its normal enabled default.
- Host Java import tests passed, including the locally supplied ZIP, exact ROM
  SHA-256 verification and a deliberately altered payload rejection.
- Local ROM probe: ASMP revision 0, 2,175 FAT entries, Mario model decompressed
  to 19,460 bytes. Recovered DotVec3/CrossVec3 known-value checks passed.
- `python tools/port_refcheck.py`: all 233 references resolve.
- `apksigner verify --verbose`: APK v2 signature verified, one signer.
- `aapt dump badging`: package io.github.ronyxdumb.sm64ds, min API 26, target 35.
- APK contains only app code/resources and the ARM64 library; no cartridge assets.

## Verification limits

`adb devices` lists no connected devices. No Android installation, Activity launch,
JNI execution on Android, original game menu, rendered game frame, gameplay,
save/load, audio, touchscreen controls or A34 performance has been verified.
The Java importer and C++ probe were executed on the build host. No emulator
runtime test has been performed. Docker/Windows script execution has not been
performed in this environment. GitHub Actions results are separate from local checks.

## Changed files

- `android/app/src/main/java/io/github/ronyxdumb/sm64ds/`: MainActivity.java,
  NativeBridge.java, RomImport.java.
- `android/app/src/main/cpp/`: CMakeLists.txt, bridge.cpp, rom_probe.cpp,
  rom_probe.h, game_math.cpp.
- `android/app/src/main/`: AndroidManifest.xml, res/values/styles.xml,
  res/drawable/ic_app.xml.
- `android/`: settings.gradle, build.gradle, gradle.properties, app/build.gradle,
  gradlew, gradlew.bat, gradle/wrapper/gradle-wrapper.jar,
  gradle/wrapper/gradle-wrapper.properties, Dockerfile, README.md, CHECKPOINT.md.
- `android/tests/`: rom_probe_test.cpp, RomImportTest.java, run-host-tests.sh.
- `port/ntr/fs.cpp`: defensive ROM filesystem validation and negative-read guard.
- `.github/workflows/android.yml`, `.gitignore`, `.dockerignore`, README.md,
  docker_build.bat.

Original `src/` and `include/` files are untouched. No Nintendo bytes/assets or ROM
are added to the repository or APK. Find the increment's exact SHA in `git log`
under `android: add ARM64 importer and native asset verification`.

## Resume

Follow the integration blockers in android/README.md. Preserve the 32-bit ABI
checks until each consumed game layout has an explicit ARM64-safe representation.
Do not report asset inspection or math self-tests as an original game boot.
