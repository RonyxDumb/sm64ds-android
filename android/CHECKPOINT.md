# Current checkpoint — 2026-10-30, stage audio and 2D interface execute

Initializer checkpoint bd65668f1bda862705d1caaafd67a40c999eace4 passed both
GitHub Actions jobs (run 41027966348). The target remains a genuinely playable
castle exterior; scene initialization alone is not acceptance.

The complete original GetSoundGroupID, Sound::LoadGroupAndSetBank and
LoadGraphics2D bodies now run, including all three controller-text strings.
Native SDAT cache ownership supplies SDK group load/save/restore services;
groups are checked against real resident SSEQ/SBNK/SWAR/SSAR resources used by
the existing playback engine. No debug flag forces the loader's early-return
branch. Native font/video upload providers perform actual validated uploads.
The original text/BMG cursor lanes reference boot's live language bank without
narrowing native pointers. Existing frontend G2/G2S getters are reused.

ASan/UBSan found a real ARM semantic dependency in Message2's glyph blitter:
logical pixel shifts can have count 32, and pixel words can be negative when
read as signed int. The scoped projection now preserves 32-bit ARM logical
shift results, including zero at count 32. A no-ROM regression executes the
original blitter with synthetic glyph words and checks both destination tiles.
No-ROM tests also execute the original sound-group loader over a synthetic
SDAT and validate native cache ownership, snapshots and rejection. Music tests
cover synthetic group metadata/invalid types and genuine ROM PCM/streaming.

All four private-ROM character/save paths pass strict checks through the new
stop: dExtSimpleModel_c::LoadFile(SharedFilePtr&) at 0x02017a3c. UI/BMG state,
group/bank selection, all 58 initializers and eleven module records are checked.
Local tests use ASan/UBSan with LeakSanitizer disabled for process restrictions.
Original matching sources stay unchanged; ROM, images and decoded assets are
not committed. Android CI is pending for this checkpoint.

Next: real shared-file/model loads and common-model pool, then faders, level
model, LoadClsnAndObjects, actual Player/actor initialization and frame loops.
Known integration work after scene entry: GameProgram currently updates input
only while scene 3 is requested, renders GX geometry/OBJ only for title scene 1,
and StageFlow ticks only the scene actor rather than the full original process
lists. These must be fixed and exercised before calling the castle playable.
Overlay unload/cleanup also remains incomplete. Version 0.4.6-stage-2d,
versionCode 10. Do not claim on-phone rendering verification from host tests.

---

# Current checkpoint — 2026-10-30, all castle overlay initializers execute

Loader checkpoint d56e39d6172b3d3ae100c44c2cf06ccae2c9b6eb passed both
GitHub Actions jobs (run 41025768588), APK/lint and native ARM64 included.
The user's explicit next acceptance target is playable castle grounds, with
all dependencies after LoadLevelOverlays resolved. Do not stop at scene spawn,
resource residency or an actor with missing behavior/render/physics methods.

This implementation executes the original common-overlay 1/2 loading before
scene construction and all 58 initializers in the eleven castle modules:
1,2,9,62,69,85,89,94,100,98,102. Bindings are installed by module only when its
real image has loaded; resident data alone does not publish code. Matching C
initializer shards come from the pinned PC dependency, guarded by file and
symbol-map hashes. The single already-folded star-gate initializer is retained
in android/compat/ with its exact historical source commit/path provenance.
Original SharedFilePtr construction and destructor registration bodies execute
on the checked eight-byte records. The real overlay-0 handle catalog supplies
FAT IDs, while archive handles >=0x8000 remain unchanged as original code says.
Player's genuine palette-initialization helper and rabbit-save predicate run.

No-ROM overlay SDK tests and all four private-ROM character/save launches
pass under ASan/UBSan (local LeakSanitizer disabled for process restrictions).
Private checks verify eleven residency records, unchanged code/decompression,
real model/animation/collision FAT IDs, initialized PMF state tables (including
a writable .data table, not just BSS), and the linked destructor list holding
original 32-bit code handles. These are real constructors, not prefilled BSS.

Actual next dependency: GetSoundGroupID at 0x0202de24, after LoadLevelOverlays
and every requested initializer has returned. Continue original scene sound
setup, LoadGraphics2D, model/file/fader loaders, LoadClsnAndObjects and real
Player/actor behavior/render/collision until castle grounds is playable.
Overlay unload/replacement and scene cleanup are still incomplete. Gameplay
has not been reached. The 0.4.3 relocated-MMIO fix still needs on-phone visual
verification. Android CI is pending for this initializer checkpoint.

Version 0.4.5-native-initializers, versionCode 9. All ROM and extracted asset
bytes remain private. Regenerate with lift_stage.py (tree-sitter dependencies
installed), packed_sources.py, and compile_frontend.py for the stage manifest.
Local cached build and ROM paths are scratch only; GitHub main is authoritative.

---

# Current checkpoint — 2026-10-30, original level-overlay loader executes

Video repair 19716ad79baba6ae82ed8ac03fe1df8928a47b93 passed both GitHub
Actions jobs, including native ARM64 frontend execution, host regressions,
GLES and Android APK/lint (run 41022886778). Its private-ROM game tests also
passed for four save/character selections. An on-phone visual result is still
needed; do not claim the stripes are visually resolved from host tests alone.

This checkpoint links the complete LoadLevelOverlays, object-overlay selection
and preload/reuse bodies from d_s_stage_methods.cpp, plus the original
LoadOverlay, FS_LoadOverlay, metadata lookup, image load, BSS clear, BLZ/start
and SDK tracking bodies. Native providers supply checked card reads, cache
coherence, storage ownership and BLZ decompression; they do not execute ARM
instructions or skip missing initializers. The ARM argument-word pair used by
FS_OpenFileFast is reconstructed explicitly rather than assuming adjacent host
ABI locals. Absolute overlay-ID/RAM-bound symbols retain their numeric meaning.
The actual ROM archive and boot header feed both SDK metadata lookup paths.

No-ROM ASan/UBSan coverage now loads a synthetic overlay through the original
SDK: direct/cached metadata, out-of-table rejection, file/image bytes, zeroed
BSS, a real bound initializer, duplicate-region failure, seven-column object
overlay selection and the special/multiplayer paths. The normal game test
runs this even without a cartridge. The private-ROM continuation checks the
level image against Cartridge::overlay_data after original read/decompression.

The actual next scene-3 stop is 0x02112458, the first static initializer in
castle-grounds overlay 9 (formerly unlinked LoadLevelOverlays at 0x0202ded4).
The scene is still not playable and must never be published as initialized
when that call fails. Overlay 1/2 were pre-mounted as data; their full original
static initialization is also outstanding and must run before actor loading.
Castle grounds requires overlay IDs 1,2,9,62,69,85,89,94,100,98,102, verified
from the cartridge's genuine level/object tables. Next: module-scoped native
initializer bindings (the pinned PC dependency preserves the matching C
initializer shards folded into newer class TUs), then continue InitResources.

Version 0.4.4-overlay-loader, versionCode 8. Android CI is pending for this
loader checkpoint. Local private ROM and all extracted asset bytes stay out
of Git. Changes are limited to the Android port/projection and its tests;
original matching src/ and include/ sources remain untouched.

---

# Current checkpoint — 2026-10-30, reproduced and repaired relocated MMIO

The shared `include/nitro/hw/registers.h` macros were not part of the packed
header projection. On Android, an occupied fixed I/O address causes `io_init`
to allocate translated storage. The original GX display-mode routines then
wrote through raw header macros into the old mapping while the PPU read the
translated registers. This explains the device log's missing 3D-select bit.

A no-ROM regression now occupies the low I/O window and compiles the actual
Android relocation branch of io.cpp. Before the repair, the original
GX_SetGraphicsMode call fails the translated-window check. After projecting
the shared header, MAIN DISPCNT is 08011b08 (instead of 08011b00); all 49,152
pixels use the supplied 3D layer. Display off/on, sub-engine mode, power, BG
scroll, affine pointers, DMA pointers, timer, memory and IRQ macros also pass,
and the foreign low mapping remains byte-for-byte unchanged.

The original matching src/ and include/ files are unchanged. Both committed
manifests and their regeneration paths include the checked register header.
The normal CI host suite runs the relocation regression. Local ASan/UBSan
checks pass with LeakSanitizer disabled because this environment denies its
process inspection; CI retains its normal leak checks.

Version: 0.4.3-relocated-mmio, versionCode 7. Full APK/frontend CI and the
private-ROM stage continuation are pending for this implementation checkpoint.
This is a reproduced register-routing repair, not yet an on-phone visual result.
The remaining actual scene-3 blocker is LoadLevelOverlays at 0x0202ded4;
continue through its original source and real overlay dependencies.

---

# Current checkpoint — 2026-10-30, resumed device-log investigation

Last implementation checkpoint: da5c8c8c9e6d244347c847f9277e8316f0137375
(version 0.4.2-video-capture). The user tested that APK: the vertical stripes
remain on the physical lower/right LCD. The previous capture-timing fix did not
resolve this device rendering fault.

Verified phone log (Samsung SM-A346B, Android 16, ARM64):
- At title tick 240: POWCNT1=020e, DISPCAPCNT=00000000,
  MAIN DISPCNT=08011b00, BG0..3=1712,0005,0210,010d.
- SUB DISPCNT=00011c00, BG0..3=0284,0314,0087,0187.
- VRAM A..I=83,8b,84,81,00,83,82,00,82.
- MAIN BG0 is enabled, but its 3D-select bit (bit 3) is clear. POWCNT1 bit 15
  is clear, so MAIN drives the lower/right LCD.
- Launching a save reaches "Unlinked native code handle: 33742548", exactly
  0x0202ded4 / LoadLevelOverlays, rather than an artificial scene-3 stop.

Strong source-based lead, not yet a reproduced fix:
GX_SetGraphicsMode requests bg0As3d=1, but GX_Init.cpp accesses REG_DISPCNT
through include/nitro/hw/registers.h. That header still contains raw fixed DS
register pointers. Android can relocate the I/O window; these header accesses
may then bypass ntr::host_ptr/io_write while literal accesses in projected
sources use the translated window. Existing host checks normally map the DS
addresses directly and therefore cannot detect this discrepancy.

Resume implementation:
1. Force an occupied low I/O window in a regression, using the Android
   relocation path. Exercise the actual projected GX display-mode routines;
   check that the foreign low mapping is unchanged and the PPU sees 3D BG0.
2. Route the shared hardware-register header through the audited packed
   projection, preserving original matching src/ and include/ files.
3. Save that verified repair on main, build an APK, and retain the exact checks.
4. Link the complete original LoadLevelOverlays body and its real dependencies
   through the existing packed stage path. Continue to the next actual missing
   binding; never substitute success or suppress the original spawn failure.

The previous local environment reset before this work was committed.
No repair for the raw-header lead or LoadLevelOverlays is claimed in this
diagnostic checkpoint. Keep publishing incremental code checkpoints on main.

---

# Current checkpoint — 2026-10-29, title capture timing and unobstructed menu input

Version `0.4.2-video-capture` (versionCode 6) fixes a concrete timing bug:
`GameProgram::frame` could run up to four native ticks before rasterizing once.
The original title (`ov007_020b0358` / `ov007_020b6eb4`) requests a one-shot
capture on one tick and changes the BG/VRAM use on the next. A pending capture
now forces scanout at the end of its native tick, including inside catch-up
batches. Ordinary frames retain one final presentation per Android callback.
This prevents skipped captures from leaving tiled data in a captured bitmap.

The boot and title/save menus now show only a small START control. Stick,
A/B/X/Y, shoulders and camera overlays are enabled from the actual active
scene-3 state, not a timer. Hidden controls do not intercept menu stylus input.
Physical gamepads remain connected. Touch conversion now matches the renderer's
integer full-surface viewports even when display cutouts/navigation insets move
the virtual controls.

The PPU regression covers the title's actual capture control value 0x80370010,
the destination at bank D+0x8000, one-shot completion and all 49,152 pixels after
D is remapped to a mode-3 bitmap. Java coverage includes menu controls, physical
gamepads and odd-size landscape surfaces with asymmetric insets. These checks
are part of the existing Android workflow; see this commit's workflow result
for execution evidence. Both engines' BG registers, capture state and all nine
VRAM bank controls are now included in the exported VIDEO log.

Work in this checkpoint was prepared through GitHub because the attached local
execution environment was unavailable. No on-phone visual verification or
private-ROM execution was possible in this session. The screenshot's complete
rendering issue is therefore not declared resolved solely from compilation.
The two title LCDs remain visible; full-screen gameplay/minimap work and the
existing stage LoadLevelOverlays integration blocker remain separate work.

---

# Current checkpoint — 2026-10-29, correct ARM9 startup data and module bindings

The ARM9 data image now follows the original CRT0 module parameters and
autoload/Entry operations. The static data ends at 0x0209b000; bytes after it in
the expanded file contain ITCM/DTCM payloads and are **not initialized main RAM**.
The main BSS through 0x020aa420 is now cleared, ITCM data is relocated, and the
native DTCM data window is cleared with the original IRQ return word installed.
Previously opcode bytes occupied save, stage and OS globals. No cartridge
instructions execute; callable entries still require registered native code.

Stages now import only ARM9/ITCM shared routines from the frontend. Overlay 7's
title methods are excluded because overlay 2 reuses their addresses for player
and actor code. An unported gameplay method therefore reports its real missing
binding instead of resolving an unrelated title method. The native ROM archive
also occupies the SDK's actual inline data_020a8074 record, as overlay file IDs
require.

The private-ROM ASan/UBSan launch checks pass for all four saves, including BSS,
autoload, IRQ data, actual archive mounts and card I/O. The ROM-free frontend
regression also passes and verifies that stage-shared registration excludes
the title factory. The next stage dependency remains LoadLevelOverlays at
0x0202ded4. The prior resource checkpoint 6ffd05e58aebe109fdd67d5071e7ff551c789faa
passed both the [2407/2407 source census](https://github.com/RonyxDumb/sm64ds-android/actions/runs/40871349860)
and [APK/lint workflow](https://github.com/RonyxDumb/sm64ds-android/actions/runs/40871349845).

---

# Stage resources and native card I/O

The complete original dScStage_c::InitResources body now executes as checked
native C++ against its 0x9c8 cartridge record. The committed stage manifest
contains 45 bindings from 38 source units, with source/header hashes and native
code lookups for all imports. No branch or missing callee is replaced by a
success value. The regular build applies this manifest without parser packages.

Private-ROM execution now passes the scene fader/audio reset, full scene/3D
register setup, input reset, level-state initialization, archive loading and
cap-icon setup. The next genuine missing binding is LoadLevelOverlays at
0x0202ded4, still within the original initialization call and spawn phase 5.
The actor has not completed initialization; gameplay is not yet running.

Overlay 1 is now loaded alongside overlay 2 and the level overlay. Previously
its entire interval was incorrectly exposed as zero-filled ARM9 BSS. The native
card provider maps the immutable cartridge, registers its actual FNT/FAT through
the recovered SDK, and performs real file open/read/close operations. The original
LoadArchive, archive allocation, NARC parser, registry and mounting functions run
unchanged through their checked views. Common, text/language and castle archives
are genuine owned allocations, with real NARC bytes and FAT/FIMG pointers.
The allocator preserves signed alignment; current default-heap ownership is
explicit and checked. Native-owned boot/input/save lanes are marshalled into
the stage image without aliasing widened C++ records onto DS layouts.

ASan/UBSan private-ROM checks pass for all four file/character launches and
verify the constructed stage plus those mounted archives. Card checks exercise
FNT lookup, content reads, EOF clamping, immediate async completion, negative
length, close/reclose, and relocated FAT/FNT tables. Platform arithmetic/context
and native address/member migration regressions also pass. The preceding
constructor checkpoint 5245da5af6b8a9c181d0c4c046dc96f46b5cd516 passed the full
[Android APK/lint workflow](https://github.com/RonyxDumb/sm64ds-android/actions/runs/40866286900).
The APK workflow runs again for this runtime checkpoint.

---

# Original packed stage construction

The recovered source checkpoint 5d349ff28241be67f9500e8089f2625e5bb3347b
passes both the full 2407/2407 ARM64 census and the complete Android APK/lint
workflow, including native ARM64 frontend execution:
[verified APK build](https://github.com/RonyxDumb/sm64ds-android/actions/runs/40865004091).

The stage factory at 0x0202e088 is now bound to native code. Its genuine
0x9c8-byte cartridge record is allocated and cleared, then fBase's scene
tree/owner/priority bookkeeping, all particle callbacks, the simple model's
identity transform, and the static mesh collider are constructed at their
original offsets. The original post-construction scheduler runs the actual
InitResources PMF chain. It now reaches the next unlinked scene method,
ResetFadersAndSound at 0x0202e66c, with spawn phase 5. No scene success is
published. Source/header/symbol-map hashes guard these ABI transcriptions.

The private-ROM ASan/UBSan regression passes for all four file/character
launches and asserts the real constructed records, owner links, vtables,
particle water/scale state, model transform, collider and allocation extent.
The scheduler also avoids initializing an already-live actor twice, and its
list traversal has cycle detection without an artificial actor-count limit.
Gameplay initialization and subsequent actor integration remain in progress.

---

# Restored source and native services

**All 2407 original source units pass Android ARM64 compilation.** This was
revalidated after recovery on commit 6293ceb97acbaac1ec99fab00297f4b24e9b61db:
[all four census shards](https://github.com/RonyxDumb/sm64ds-android/actions/runs/40863346300)
finished with zero failures, timeouts or tool errors. Exact counts are saved in
`android/checks/arm64-census.json`. Original `src/` and `include/` stay intact.

The current changes additionally restore the arithmetic/platform/context tests,
require every source to compile in CI, preserve the declaring class of native
member callbacks, and fix Android's noreturn diagnostic in its unwind adapter.
Local ASan/UBSan tests pass for 20,000 arithmetic pairs, conversions/remainders,
status writes, memory edges, three codecs and callbacks, CRC, lock IDs, IRQ
waits, stored native/packed member calls and native stack switches (UBSan).

The six existing runtime ownership wrappers remain the sole linked definitions
of their symbols. The APK build after the unwind fix passed on 5d349ff28.
Compiler success does not mean subsequent gameplay is connected.

---

# Recovery checkpoint — 2026-10-29

The prior local session recorded a complete 2407/2407 Android ARM64 census,
then was reset before its final commit could be pushed. The published parent
332fefcd1 preserved the earlier 2286/2407 checkpoint. Recovery now restores
source changes from the recorded work in incremental GitHub checkpoints.

The remaining C++ repairs, eight-byte member cells, all 73 ARM9 service
units and all 18 statically lowered arithmetic units have now been restored.
The Android runtime duplicate definitions found before the reset are addressed
by retaining the existing ownership wrappers for six symbols. The old full-census
result remains historical evidence until this recovered tree is rerun.
The game still requires the packed stage factory at 0x0202e088 and subsequent
stage/Player/camera/collision integration. No gameplay success is claimed.

---

# Android checkpoint — 2026-10-29, ARM64 source migration and scene factory

**Not a finished or verified playable port.** All 2,407 original `src/` C/C++
units were compiled with Android NDK 27.2.12479018 (`aarch64-linux-android26`).
With the checked complete-source views, **2,286 compile and 121 fail**, down
from 1,884 compiled / 523 failed at the start. This is 402 additional successful
units. All 407 selected projections pass; no source was skipped or replaced by
a successful stub. `src/` and `include/` remain unchanged.

- `Player.cpp` now compiles in its entirety as an AArch64 object. Its block-local
  ABI declarations retain their original linker labels, with typed references
  to byte-labelled data storage where local struct types cannot have external
  linkage. Exact repairs preserve the original goto paths, replace the MWCC
  clear builtin with `memset`, and declare the actual missing palette helper.
- Native pointer/integer round trips use checked 32-bit data/code handles.
  `SharedFilePtr` and reviewed resource descriptor views retain their asserted
  DS extents using stored pointer cells. C record views retain their layout
  assertions and every source function body. Parser disambiguation preserves
  cast/arithmetic precedence; unparsed expressions are retained verbatim.
- The source migration profile pins all selected sources and input headers.
  These **compile-only objects are not automatically linked into the APK**.
  Native polymorphic layouts, raw byte offsets, member-function pointer tables,
  linker-defined integer constants and cross-unit call signatures still need
  runtime work. A successful Player object is not a running Player actor.
- Removed the unconditional scene-3 stop in `GameProgram::tick`. The recovered
  `dScene_c::SpawnIfNecessary` now calls the scene-spawn path through the original
  `ACTOR_SPAWN_TABLE`. The checked factory chain preserves pre-spawn hooks,
  parameters, phases, null returns and the post-construction hook. Missing code
  fails at its actual unbound native function address. There is no fallback
  character, synthetic scene success or jump into cartridge ARM instructions.
- The full Android native shared library links with `--no-undefined` after the
  scene-spawn change. Host ASan/UBSan checks cover high-address data/callback
  round trips, pointer adjustment, macro arithmetic and member-call retention.
  Local LeakSanitizer is disabled because this environment denies its process
  inspection; CI retains its normal enabled default.
- With the private EU ROM, the recovered boot/frontend/file-launch regression
  passes for all four save/character setups. Each scene-3 request resolves the
  real `g_profile_STAGE` and reaches `dScStage_c_classInit` at 0x0202e088,
  stopping on that specific missing native binding with spawn phase 4. The
  failed creation leaves the scene request pending and never publishes success.
  This is host execution evidence; no Android device is connected.

Reproduce the complete census (no ROM required):

```sh
python3 -m pip install -r android/tools/requirements-migration.txt
python3 android/tools/compile_all_src.py \
  --ndk "$ANDROID_NDK_HOME" --out build/compile-audit \
  --apply-projections --require-projections-pass --keep-objects
```

`summary.json`, `failures.txt`, one log per original source, and the successful
AArch64 objects remain in that output directory. Add `--require-all-pass` to
make the still-incomplete entire folder a hard failure. A green census workflow
only establishes that its checked projections compile; it does not hide the
remaining failures.

The next scene dependency is the native `dScStage_c_classInit` factory at
**0x0202e088**, referenced by `g_profile_STAGE` at **0x0209213c**. It must construct
the real packed stage/actor layout, then connect original initialization,
Player, camera, collision and rendering. Do not bind the newly compiled native
C++ factory to a packed factory signature without porting its object ABI.
Of the 121 remaining compile failures, 92 contain ARM9 assembly/platform code;
the other 29 include member-pointer/state record widths and inconsistent
recovered declarations. Resume from the per-file census rather than the old CSV.

---

# Android checkpoint — 2026-10-26, native title execution

Production connects original `b7138` initialization, `b7090`/`aed98` input and
menu dispatch, `b7040` model/particle drawing and `b6eb4` graph callbacks.
617 recovered source units and 789 ARM/Thumb identifiers bind to compiled native
functions. The original source and layout assertions are unchanged.

- Native private-ROM execution survives 1,200 frames, the blank-save intro star,
  Mario title and stylus/button-driven transitions under ASan/UBSan, including
  use-after-return checks. The ROM-free source fixture and teardown pass.
- Full ModelComponents and particle update/draw helpers execute. ARM register
  liveness in pooled BX veneers, localized character loading, material indices
  and cursor mode forwarding is explicit in reviewed source views.
- Title save/backlight/sound commands reach native owners. Native SSAR players
  retain handle identity, priority, stop/fade and sequencer playback status.
  Tempo and some modulation controls remain unsupported in the pinned engine.
- LCDC/BG/OBJ bank aliases share physical storage on Linux/Android. Texture and
  palette slot routing, FIFO aliases, texture transforms, perspective UVs,
  alpha/culling, OBJ 2D mapping, affine/bitmap BGs and display capture are wired.
  Rendering fidelity still needs work; no phone performance is established.
- Actual stylus selection of Adventure and button confirmation in the source
  save menu emitted a launch result, ran `StartFile` and requested scene 3.
  Stage 3 still needs
  a packed actor/vtable/member-call ABI, then original Player/collision/camera,
  levels, HUD/minimap and full gameplay. No device is connected.

Regeneration (pinned tree-sitter 0.25.2 and tree-sitter-cpp 0.23.4):

```sh
python3 android/tools/lift_frontend.py --profile android/tools/frontend_execution_profile.json --out android/tools/frontend_manifest.json
```

Android/ARM64 builds must be checked for the final published revision. Continue
through actual user input into the save selection and stage; do not synthesize
menu results or replace missing game code with successful stubs.


---

# Android checkpoint — 2026-10-26, recovered file launch and stage controls

**Not playable.** The normal Android session completes boot and the original
title-file preparation, then reports the missing interactive frontend. No device
is attached. Supplying a frontend result in the native execution check now runs
the genuine file selection/`StartFile` path through to the scene-3 request; it
does not execute a stage, actor manager or Player.

## Implemented

- Command 20's actual read/activate/summary loop, slot lookup/buffer installer and
  summary function are generated from `d_s_dsmt.cpp`. Native ownership replaces
  only its 0xcc-byte allocation. Each slot retains read-failure status and the
  original save defaults, character count, coin records and star counts.
- The title's actual result-3/4/5 launch branch calls the recovered `StartFile`,
  `LoadLevel{,NoReturn}`, `SetNextStar`, `SetPlayerGlobals`, controller-mode and
  player-count functions. Distinct files select the correct active slot,
  character and mode; the request is castle grounds / entrance 0 / star 1 / scene 3.
- Fixed save/star/coin/minigame and settings byte lanes alias their true native
  storage. The title message-bank pointer lives in a native-width member beyond
  the 0x9c scalar settings, not inside a DS integer slot. `CpuCopy8` follows the
  actual source-first primitive, not its misleading parameter names.
- Stage `CheckCameraInput`, `CheckInput` and `ResetInput` execute on a generated,
  checked pointer-free 0x18 controller record. The camera leaf takes a flags
  value; no fixed-layout camera or Player is instantiated. ROM button tables use
  native pointer cells. Original touch delay/clamp, button maps and camera hit
  regions are preserved; invalid native counts/modes are rejected.
- Android stick axes reach that same Q12 record with partial magnitude and radial
  clamping. Menu stick-to-button conversion is disabled for stage input to avoid
  accidental camera buttons in alternate controller modes. The original input
  gate and active stylus gesture retain priority. Raw touch samples include the
  fourth validity halfword required by the frontend's input entry point.
- The integer arithmetic seam now follows original divider/sqrt rounding. The
  earlier truncated divider made 30-frame fades take 31 ticks; corrected source
  fades bring the boot boundary to 120 ticks rather than 122. Source atan2's ROM
  table/quadrants are retained; its integer-to-soft-float sign comparison is
  represented as the equivalent integer sign test on AArch64.
- A second `GameProgram` is rejected before its members can reset an active
  session's game globals. File/metadata/font state is released on teardown.

## Verification

- Private ROM, ASan/UBSan: all three distinct saved slots and a blank Yoshi file;
  summary stars/coins/unlocks; high native pointers; preserved minigame bytes;
  original scene-3 request; controller/button/touch/camera/quadrant checks; four
  complete boot/session lifetimes. No sanitizer errors.
- ARM64 debug/release APKs and Lint pass. The linked game code uses
  `--no-undefined`. Host/save/input/audio regressions and source-reference checks
  remain part of validation. No matching source/header changes or ROM data.
- The execution check **supplies** the title result and camera flags. It is not
  evidence that the title dispatcher, camera, stage or Player runs interactively.
- APK symbol inspection confirms live `TitleFiles::load`, `CheckCameraInput`,
  `CheckInput` and atan2. The frontend's missing caller means `StartFile`, its
  launch chain and `ResetInput` are still discarded from the APK by section GC;
  the host execution check retains and executes those same implementations.

## Resume here

1. The 124 consumers of `data_ov007_0210342c` still need one coherent native
   frontend work layout. Start with `func_ov007_020b7138.c`,
   `func_ov007_020b7090.c`, `func_ov007_020aed98.cpp`, root allocation
   `func_ov007_020b68e8.c` and its five builders. Keep source assertions intact.
2. Port frontend rendering, OAM/model callbacks and its update dispatcher, then
   deliver its real return value to `GameProgram::accept_title_result`. The
   existing preparation/launch path is reusable; do not replace the menu with
   guessed button regions or select a file automatically.
3. Scene 3's lifecycle, native actor ownership/dispatch, Player/collision/camera,
   level actors, source-driven 3D/animation, HUD/minimap, game-wide sounds and
   Android device/lifecycle/performance verification remain unfinished.
4. ARM64 remains the required target. The Windows x86 upstream depends on its
   32-bit layouts and MSVC bridges. No ARM32 fallback, emulation or unsafe
   pointer-narrowing workaround was introduced.

---

Historical checkpoints below are superseded by the current status.

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
