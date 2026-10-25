# Android checkpoint — 2026-10-25, PC audio integration 0.3

The Android APK is still not a playable game. The original boot/scene/level
runtime is not linked. This increment replaces the incomplete PC reference with
a pinned dependency on the actual playable PC release. See [PC_PORT.md](PC_PORT.md).

## New work

- Pinned upstream `port/release-0.4.0` at
  `6230b2996636e3fd0e03cb178161c94cb4b73cc9` in `external/sm64ds-pc`.
- Reused that release's original SSEQ sequencer and 16-channel mixer on ARM64.
- Added checked SDAT/INFO/FAT and SBNK/SWAR adapter with host pointer side tables,
  eager sample decoding, a producer thread and bounded callback-safe PCM ring.
- Connected sequenced music to AAudio alongside existing sample playback:
  X starts BGM 58; Y stops it. The existing A/B sample test is retained.
- Extended hostgen only for the exact pointer-truncating LADR macro. The original
  boot methods now compile as an ARM64 object gate in the APK build. That object
  is not linked/called; its 58 external references still need real link closure.
- Android boot-class layout measured at 0x98 bytes vs DS 0x58; member-function
  pointer size 16 vs the PC's explicitly selected 8. This is an ABI porting task,
  not just a missing startup function call.

## Verified locally

- Debug/release ARM64 APK builds and Android Lint pass, including the recovered
  boot object compile gate. The release APK is unsigned; the debug APK is signed.
- ASan/UBSan host tests: BGM 58 rendered for five seconds with peak magnitude
  32768 and nonzero energy; SSAR archive 0 entry 3 produced peak 29261.
- Resampled PCM ring produced nonzero output; pause/resume and instance teardown
  followed by a second instance passed. An initial underrun callback was counted
  in the host startup test; this is not a measured Android latency result.
- Public CI also exercises a synthetic SDAT archive generated from a square wave
  and a single note; it uses no game data and produces nonzero PCM.
- Three hostgen tests passed: LADR preservation, non-widening of guest offsets,
  existing MMIO routing. Earlier host regression suites remain available.
- Original game/decomp `src/` and `include/` are unchanged.

## Runtime limits

No connected Android device or emulator runtime has been available. On-device
AAudio output, rendering, startup, controls and lifecycle remain unverified.
The test view still shows the original face asset in its frame-zero pose; it is
not the original menu. Full boot, file selection, levels, player logic, saves,
minimap/HUD, general animation and sound-command integration remain unfinished.
Do not present compilation of the recovered boot TU as a successful game boot.

## Changed files

`.gitmodules`, `external/sm64ds-pc` (pinned gitlink),
`android/app/src/main/cpp/pc_audio/{archive.h,archive.cpp,stream.h,stream.cpp}`,
`android/app/src/main/cpp/{CMakeLists.txt,android_audio.h,android_audio.cpp,session.cpp,rom_probe.cpp}`,
`android/app/src/main/java/io/github/ronyxdumb/sm64ds/{ControlsView.java,MainActivity.java}`,
`android/{app/build.gradle,Dockerfile,README.md,PC_PORT.md,CHECKPOINT.md}`,
`android/tests/{hostgen_test.py,music_test.cpp,run-host-tests.sh}`,
`.github/workflows/android.yml`, `port/tools/hostgen.py`, root `README.md`,
`android/app/src/main/assets/THIRD_PARTY_NOTICES.txt`.

Find the published increment in git log under
`android: reuse pinned PC sequencer and compile recovered boot on ARM64`.

---

Previous checkpoint (historical evidence, superseded where noted above):

# Android checkpoint — 2026-10-25, native backends 0.2

The APK now has an explicit native backend test. It is **not a playable port** and
it does not run the original boot, title-menu state machine or actor runtime.

## Implemented

- Native ES 3.0 rendering of the ROM's `data/DSMT/face_demo_mario.bmd` with
  `face_demo_wait_mario.bca` frame-zero pose. This is the authentic startup-face
  asset, displayed in a diagnostic scene; it is not the original startup sequence.
- Geometry/display-list decode and texture decode reuse `port/ntr`. GPU buffers
  and textures are uploaded once per session/context, not regenerated per frame.
  Perspective aspect adaptation, depth test, vertex lighting/colors, texture
  alpha, polygon alpha, clamp/repeat/mirror texture addressing.
- Fixed inherited BMD material/group and per-part matrix-slot bindings; hierarchy
  composition no longer depends on bone record order. Material specular/emission
  is read from +0x2c, not the polygon flags at +0x24.
- Pointer-ID multitouch controls: circular deadzone joystick, A/B/X/Y/L/R,
  START/SELECT, camera directions, and a separate original-touch coordinate state.
  Cancel, pointer reordering, simultaneous joystick/buttons and focus-loss reset.
- Native SDAT FAT/SWAR parsing and PCM8/PCM16/Nintendo IMA-ADPCM decoding.
  The backend test loads 32 short non-looping samples, with a 16-voice stereo
  mixer, linear resampling, hardware-timer-derived sample pitch, bounded SPSC
  event queue and no allocation/locking/file I/O in the AAudio callback.
- AAudio output, audio-focus gating, callback disconnect notification with reopen
  on the render thread, pause/teardown and context-loss resource handling.
- POSIX VBlank scheduler in `port/ntr/rt.cpp`: cooperative game/presentation
  handoff using a condition variable and a thread; Windows fibers are preserved.
  The Android library compiles this seam, but the backend scene does not yet
  invoke a recovered game entry point through it.

## What the test APK does

Import the supported ROM, then press **Test renderer, audio e controlli**.
The original Mario face is shown. The stick/camera arrows rotate it; vertical
stick movement changes viewing distance. A plays an original ROM sample; B
selects the next sample. The overlay displays the current input state. Use the
Android back gesture/button to return to the launcher.

These are diagnostic bindings. Controls do not yet move a recovered Player actor,
and the samples are not yet driven by the game's Sound command queue.

## Validation

- `assembleDebug assembleRelease lintDebug`: passed locally (ARM64 NDK 27.2,
  Gradle 8.9, AGP 8.7.3). Debug APK signed; release APK unsigned.
- ASan/UBSan: ROM/FS/LZ77, BMD nonsequential hierarchy, scheduler completion,
  bounded stop, exceptions and cleanup; PCM8/16/ADPCM known values, sample bounds,
  stereo pan, saturation, invalid notes, queue capacity and threaded queue traffic.
- Java: import safety/digest checks and simultaneous multitouch/release/cancel tests.
- Headless EGL/GLES3 on Mesa llvmpipe: shader compile/link, synthetic textured
  scene, actual ROM face, GL resource teardown/recreate; zero GL errors.
  Actual asset: 1,798 triangles, 27,508 covered pixels at 960x540 in the fixed
  test view. The frame was visually inspected and showed a coherent textured face.
- SDAT local read: 32 one-shots, 601,928 decoded PCM samples; nonzero mixed output.
- `python tools/port_refcheck.py`: 233 references resolve.
- No Nintendo assets, rendered frames or sound samples are committed or packaged.

LeakSanitizer remains disabled locally due restricted process inspection. CI uses
its normal enabled default. GitHub CI execution is separate from these local checks.

## Not verified / not complete

No connected Android device or emulator runtime test is available. Android Activity
launch, GLES on an Android driver, AAudio device output, pause/resume and on-device
multitouch are compiled implementations and still require device testing. A host
PCM buffer test does not establish audible output on a phone.

Original boot, file selection, castle gameplay, character logic, minimap/HUD,
saves, animated texture matrices, full transparency/culling fidelity, SSEQ/SSAR
sequencing, SBNK instruments/envelopes and game-driven music/voices remain absent.
The BCA is sampled at frame zero; general animation playback is not implemented.
There is no Android playability percentage or performance claim.

## Concrete boot blocker and next step

Compiling `src/game/stages/d_s_boot_methods.cpp` with the ARM64 NDK reports pointer
truncation errors in its `LADR` macro, including lines 189, 200 and 206. It also
retains direct MMIO and ROM-global dependencies. Preserve `src/` and extend the
existing hostgen/seam path, then close the boot scene's actual link dependencies.
Do not remove the recovered ABI guard or substitute a fake title/menu.

The POSIX scheduler is cooperative: game code that never reaches VBlank cannot
be force-stopped safely. It is not yet a general Nitro thread implementation.

## Changed files this increment

`port/ntr/{README.md,bmd.cpp,gx.cpp,rt.cpp,include/ntr/bmd.h,include/ntr/gx.h}`;
`android/app/src/main/cpp/{scene.*,gles_renderer.*,audio_core.*,android_audio.*,
session.*,bridge.cpp,rom_probe.cpp,CMakeLists.txt}`;
`android/app/src/main/java/io/github/ronyxdumb/sm64ds/{ControllerState.java,
ControlsView.java,GameSurface.java,RuntimeActivity.java,MainActivity.java,NativeBridge.java}`;
`android/app/{build.gradle,src/main/AndroidManifest.xml}`;
`android/tests/{audio_test.cpp,bmd_test.cpp,runtime_test.cpp,gles_test.cpp,
ControllerStateTest.java,run-host-tests.sh}`;
`.github/workflows/android.yml`, `README.md`, `android/{README.md,CHECKPOINT.md,.clang-format}`.

Original `src/` and `include/` are unchanged. Published commits are discoverable in
`git log` as `port: repair BMD mesh bindings and add portable VBlank scheduling`
and `android: integrate GLES rendering, multitouch and ROM sample audio`.
