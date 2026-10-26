# Android checkpoint — 2026-10-26, recovered boot execution

**Not playable.** The production Android session now calls recovered boot logic
instead of the model/sample test. It reaches the request for scene 1; title,
save selection, castle and gameplay are not linked. No Android device is attached.

## Implemented

- `GameProgram` owns a native projection of the boot leaf's scalar fields. Three
  recovered `dScBoot_c` method bodies run through audited generation; no fixed
  DS actor allocation, manual vtable or native member-pointer copy is used.
- Recovered color/brightness faders and `dScene_c` request functions are linked.
  Input updates run at game-tick frequency, independently of display refresh.
- Runtime ARM9 data access supplies the original boot constants. The ROM's own
  13-row NARC mount table resolves archive-interior handles with checked bounds.
- The actual recovered `LoadFont(0)` and language message loader/parser execute,
  with resident pointer types adapted to native width. The native boot worker
  queue runs this real preparation before reporting completion. It does not run
  the original DS worker's thread/card/overlay body; resources and the sequencer
  are already initialized by Session.
- Source boot sound request 4:0 reaches the pinned PC sequencer and AAudio path.
  Manual sample/music diagnostic controls and the rotating-face session are gone.
- Two-screen ES3 scanout presents the actual game-written framebuffers. Fixed
  four-bit BG character bases and target/master brightness allow the original
  Nintendo logo and copyright to render and fade correctly.
- The missing title transition becomes a caught Java failure with session/audio
  cleanup, rather than an uncaught C++ exception or fake successful scene.

## Verified

- ARM64 native library link with `--no-undefined`; debug and release APKs plus
  Lint pass (0 errors, 22 warnings). Debug is signed; release is unsigned.
- ASan/UBSan with the private European ROM: recovered boot reaches scene 1 after
  122 ticks; actual font, message pointers, file ownership and repeat session
  teardown pass. Boot logo/copyright frames were inspected locally.
- Native/importer/input/save/heap/BLZ/resource/audio regressions pass. Boot SSAR
  archive 4 entry 0 produces nonzero PCM (peak 8151) in the pinned engine.
- Mesa GLES3 checks texture orientation, RGBA lanes, both screens and resource
  recreation. This is host rendering evidence, not an Android-driver test.
- `port_refcheck.py`: all 233 references resolve. Matching `src/` and `include/`
  remain unchanged. No ROM, extracted data or APK is committed.

## Resume here

1. Port the actual scene-1 frontend (`dScDSMT_c`, overlay 7). Entry points:
   `src/game/stages/d_s_dsmt.cpp`, `src/unnamed/ov007/func_ov007_020b7138.c`,
   `func_ov007_020b7090.c`, `func_ov007_020aed98.cpp`.
2. Unify the pointer-bearing frontend work records across all their consumers.
   `Ov007Work` still has DS-relative padding, pointer arrays and an asserted
   `chosenItemId` at 0x180; widening pointers changes that layout. Its callers
   also retain raw offsets and `int` pointer arguments. Do not disable that
   assertion and label the resulting native compile safe.
3. Connect the real title state machine, callback/OAM/model rendering and file
   pick through recovered `StartFile`, then the scene-3 stage/actor/Player chain.
4. Fullscreen 3D stage rendering, small minimap, game-wide sounds and Android
   runtime/lifecycle/performance testing remain. The boot's native leaf state
   does not solve the general actor ABI or the original main loop.

---

Historical checkpoints below are superseded by the current execution status.

# Android checkpoint — 2026-10-25, native resource heap and recovered input

**Not playable yet.** The APK still displays the native backend scene. Original
boot, save-selection scene, castle, Player/actor runtime, HUD/minimap and game-driven
sound commands are not connected. No Android device/emulator is attached.

## Landed and exercised

- Native game heap: a bounded 32 MiB mmap arena, external host-width metadata,
  signed forward/backward alignment, coalescing, in-place resize, checked frees
  and checked 32-bit arena offsets. This replaces the resource allocation seam;
  it does NOT make the remaining fixed-layout actor/heap classes ARM64-safe.
- Cartridge resource catalog: the supported European ROM's own overlay 0 is
  read and BLZ-decompressed as data. All 2058 handle-to-path entries are resolved
  against its filesystem. No ARM guest instructions are executed and no constant
  handle/FAT bias is used by this Android resource path.
- Six actual SharedFilePtr ownership/constructor functions are selected verbatim
  from `src/SharedFilePtr.cpp`. The card-I/O seam follows the pinned PC port's
  approach, using the existing filesystem/LZ77 decoder and native heap. SharedFilePtr
  is allocated at native size; no 64-bit pointers are written into DS records.
- The backend scene now loads its original model/animation through those recovered
  resource functions. BMD parsing still uses immutable 32-bit file offsets. ROM
  mounting and heap ownership last for the native session.
- The unchanged original `func_0203bc7c.c` and `func_0203bb60.c` are linked and run
  on each current render frame, producing the original key/stylus state. Native
  aliases preserve the overlapping key words and touch byte lanes. Gameplay tick
  scheduling and the Player's analog-control record still need integration.
- Physical controller keys, sticks, hats and triggers merge with multitouch;
  disconnect/focus-loss handling clears held input. Right-stick camera bits are
  backend inputs, not proof that the recovered camera uses them.
- The recovered cartridge-save implementation below remains linked. Original
  record defaults/checksums/mirror recovery run, but no recovered menu uses them yet.

## Verification

- Fresh ARM64 debug and release APK builds plus lint passed (0 errors, 22 warnings).
  A stale restored lint/dex cache initially reported obsolete resources/duplicate
  synthetic classes; a fresh project cache and generated output resolved it.
- ASan/UBSan: heap allocation/coalescing/resize/invalid-free and 1000 mixed
  allocations; valid and malformed BLZ streams; synthetic catalog and actual ROM
  catalog; SharedFilePtr's 255-reference limit, release and session recreation.
- Actual ROM: 2058 catalog handles, face model 78736 decoded bytes and pose 2268
  bytes; no remaining resource allocations after release. The 64-bit host test
  used addresses above 4 GiB. This is host execution evidence, not a phone test.
- Original input routines: opposite-direction filtering, press/release edges,
  stylus edge/coordinates, alias addresses and reset passed. Java controller/touch
  merge, deadzone, disconnect and reset tests passed.
- Existing ROM/importer, scheduler, audio, BMD and PC music regression tests passed.
  Mesa headless GLES3 rendered the original face: 1798 triangles, 27508 covered
  pixels; no GL errors. `port_refcheck.py`: all 233 references resolve.
- Original `src/` and `include/` are unchanged. No ROM/resources/APKs are committed.

## Resume here

1. The boot compile gate is still an OBJECT target only. Do not label it a game
   boot. Its native class is 0x98 rather than DS 0x58 and native member pointers
   are 16 bytes; those layouts still need typed scene/actor ownership and call
   bridges before linking the whole boot path.
2. Use `runtime/resources.*`, `game_heap.*`, `game_input.*` and `save_store.*` as
   real dependencies, not new diagnostic replacements. Keep the matching sources
   unchanged and extend `android/tools/native_sources.py` with audited transforms.
3. Close the actual boot-worker, fader, scene transition and 2D scanout dependencies;
   then link the recovered title/save-selection/castle chain. Do not add dummy
   success-returning dependencies or fake character movement.
4. Connect source-driven geometry/animation, HUD/minimap, camera/player controls
   and Sound events to the existing ES3/AAudio backends, then test on Android.

---

# Previous checkpoint — recovered cartridge saves

The APK remains a backend viewer, not a playable port. Boot, file selection and
castle/player code are still not linked. This increment closes the save-medium
dependency using actual recovered game code rather than a replacement save format.

- `android/tools/native_sources.py` generates audited host variants of
  `src/engine/save/SaveData{,2}.cpp`. Only the card-write source parameter and
  its six pointer casts are adapted to native pointers. Cartridge addresses and
  lengths stay 32 bit. The read fallback's uninitialized `ok` is initialized to
  false. The generator rejects changed upstream patterns. Matching sources are
  unchanged.
- `runtime/save_store.*` provides a private 8192-byte raw cartridge image, real
  record locking, checked reads and writes, fsync and atomic file replacement.
  Failed persistence leaves the previous in-memory image intact. Invalid-size
  existing images are rejected and preserved.
- Session creation opens `game.sav` beside the private ROM and runs the original
  minigame-record read/default logic. No fresh save is written during startup.
- Original `ReadFileData`, `SaveFile`, minigame I/O, default values, record tag,
  rolling checksum and primary/mirror selection are linked. These operations are
  not yet called by a recovered menu or gameplay scene.
- Verified: ARM64 debug/release APK builds and lint; ASan/UBSan save tests covering
  record bytes/checksum, both copies, mirror recovery, corrupted records, reopen,
  malformed images, write failure and bounds. No device is connected.

Next dependency: native heap/resource ownership and the original scene boot;
do not describe cartridge persistence as a working save-selection menu.

---

# Previous checkpoint — PC audio integration 0.3

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
