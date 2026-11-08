# Checkpoint — 2026-11-08 (session 4b), reloaded level runs to frame 2539

Branch: `main`. Area: **castle-grounds level after the opening's reload (HUD,
faders, castle actors' Behavior), tooling**. Evidence: private EU ROM host runs
(fast probe + ASan/UBSan). No APK built locally; no Android device.

Comparison with other NDS ports (studied, read-only clones):
- dguillot-gh/phantom-hourglass-recomp (fork of the FFCCEOT-Recomp engine):
  *static recompilation* of every ARM9 function (24,621, boundaries from the
  zeldaret/ph decomp symbols) to C, run with a CPU-register context on host
  fibers; the ARM7, timers, DMA, IRQs, 2D/3D GPU and sound come from a
  pinned, configure-time-patched **melonDS core**. Hooks by address
  (`replace|before|instead`). Built locally from the user's ROM; nothing
  generated is committed.
- aabrole/HuntersRecomp: Android wrapper of mstan/ndsrecomp +
  MetroidPrimeHuntersRecomp: ARM9/ARM7 recompiled to native "banks" plus an
  interpreter fallback promoted by measured coverage; melonDS-derived
  rendering (compute renderer ported to GLES 3.2).
- What applies here: function boundaries from the symbol maps (we have
  config/arm9), address-keyed hook/bind tables (our space.bind), a
  coverage/frontier-driven loop (run-frontier-loop.sh), and "generate from the
  user's ROM, commit only non-proprietary data" (structor_frames.json holds
  addresses/offsets only). Not adopted: melonDS hardware emulation and
  whole-program machine-code translation -- this project is a native port of
  the recovered source (CLAUDE.md), and recompiled ROM code could not be
  committed or built in CI.

What is new since dd537b5:
- Reloaded stage: `HUD::InitResources` and `HUD::Behavior` (unit-local shells:
  record fields by natural alignment, sibling member this-calls),
  `FaderColor::AdvanceFade`, `Fader::AdvanceInterp`, `FaderBrightness`
  members, castle actors' Behavior: BigBrickBlock, QuestionBlock, SignPost,
  `dBgActor_c::IsClsnInRange(OnScreen)`, `dActor_c::DistToCPlayer`,
  `DropShadowScaleXYZ`, camera `func_0200ca50` (behavior PMF), Player
  `func_ov002_020c607c`, `func_ov002_020ef070`, ov102 ground probe,
  `dBgActor_c::UpdateKillByMegaChar` (native from source + cartridge frame).
- Real bug fixed: a `(Animation *)&mModelAnim` upcast must add the 0x50 base
  offset (cartridge `add r0, r4, #0x370`); the view dropped it and
  Animation::Advance divided by an unset frame count (SIGFPE).
- PMF state tables: `(c->*TABLE[c->idx].pmf[K])()` helpers are detected by
  frontier_batch.py and become rows of stage_pmf_state_calls.inc (native
  template over the checked member-call resolver).
- Views: inline-cast virtual calls `((Shell *)(x))->m()`, dBgW casts with
  arguments (`Enable(actor)`), `struct CLS *p` fields for every actor class,
  opaque pointer-only records, upcasts of record members.

Measured (private EU ROM, Yoshi slot, no input, 4000 frames requested):
opening -> LoadLevel (level 1) at ~2500 -> teardown 2505 -> reload 2506 ->
the castle-grounds stage runs its HUD and actors to **frame 2539**.
First remaining blocker: `daMip_c::Behavior` (ov085 0x0212c230,
src/game/actors/d_a_mip.cpp, 0x5c0 bytes): ~25 C++ member/static calls
(ClosestPlayer, Particle::System::New, Message::PrepareTalk, Sound::PlaySub,
SaveData::NumGlowingRabbitsFound, Player::ShowMessage/GetTalkState/DropActor,
UpdateYoshiEat, SetState, UpdatePos, UpdateWMClsn, UpdateGrab, ...) that the
packed view cannot spell yet. Yoshi is still not under input control.

Tests: ASan/UBSan no-ROM suite rc=0; private-ROM full castle test rc=0;
4000-frame Yoshi opening under ASan/UBSan: no sanitizer report, stops at the
daMip_c::Behavior frontier (0x0212c230, same as the fast probe);
ndk_warning_check 2378/2378 clean.

Next step (most promising): a mangled-name resolver in the views that maps
`A::B(...)`, `obj->Method(...)` and bare member calls to their unique symbol
in config/arm9 + overlay symbol maps (with the parameter types from the
recovered headers), so large actor members like daMip_c::Behavior project
without hand replacements; then continue the frontier until the opening
script ends and Player input is enabled; verify walk/jump/camera with
SM64DS_STAGE_INPUT under ASan.

---

# Checkpoint — 2026-11-08 (session 4), opening level change: teardown + reload run

Branch: `main`. Area: **engine (scene lifecycle, address space) / castle actors /
fader / tooling / CI**. Evidence: private EU ROM (SHA-256 ce5829aa…) host runs;
no APK build in this session beyond the NDK clang syntax check, no device.

Architecture decision (PC port reuse): the upstream PC port builds the same
recovered `src/` with 32-bit MSVC, so its binaries cannot run on ARM64. What is
reused: its gate slices (unit selection, `pc_slice_import.py`, 678 units),
its recovered class sources (`external/sm64ds-pc/src/actors|game|engine/**`,
now searched by `frontier_batch.py` through `// @symbol` markers) and its
port/unmatched C bodies. Android-specific: the packed 32-bit DS address space,
projections, native SDK/platform boundaries and the structor frames below.

What is new:
- **Root cause of the intro stall** (fixed): the stage path never advanced the
  scene fader, so `Stage::Behavior` returned early forever. GameProgram now
  advances it every stage frame, and keeps running the current stage while a
  stage-requested scene change (the opening's `LoadLevel`) fades out, until the
  original `dScene_c::BeforeBehavior`/`AfterCleanupResources` destroy it.
- **Stage teardown chain linked** (frame 2505): `Stage::CleanupResources`
  (archives, sound voice group, `EndKuppaScript`, skybox/texture-animation
  destructors, colour fader, `CleanCommonModelDataArr`, mesh colliders, KCL
  free, `UnloadLevelOverlays` -> `UnloadOverlay` -> `FS_UnloadOverlay` ->
  `FS_EndOverlay` static-destructor chain, `Message::ResetAllGlobals`),
  `Player::CleanupResources`, CleanupResources of Fish, Bird, Butterfly, Door,
  Cannon, SignPost, BigBrickBlock, QuestionBlock, da1up_c, daMcFlag_c,
  daObjMc_Metalnet_c, and every structor of those classes.
- **Structor frames from the cartridge code**: `android/tools/structor_frames.py`
  reads a destructor from the user's ROM, accepts it only if it is straight-line
  frame code (vtable stores, member/base structors, `__cxa_vec_cleanup`,
  operator delete; no compare, no condition, no load through `this`) and
  records addresses/offsets/vtable words in `structor_frames.json` (no code
  bytes). `stage_structor_frames.inc` is generated from the JSON (no ROM
  needed in CI). 40+ frames; it caught a hand-written error (Player D1 had the
  two array element destructors swapped). Conditional frames stay hand-written
  (ShadowModel, fLiNdBa(Pr)_c, Particle::SysTracker, Model D1/D0).
- **One DS RAM across a level change**: `Space::carry_from` moves the heap
  allocations, dynamic native code handles and native data segments into the
  next stage space; `prepare_scene` carries ARM9 data/BSS, ov001 and ov002,
  skips crt0 `.ctors`, the ROM archive registration and the sound-cache init
  (already in RAM), and links the resident overlays 1/2 code again.
  Before this, the FS archive list was re-registered and cycled.
- **DS faders in the native fader slot**: `dScene_c::SetFaders` on a DS fader
  object (the KuppaScript's `data_0209b294`) wraps it in `StageDsFader`, which
  drives the DS object through its cartridge vtable and mirrors
  `currInterp/speed` for native readers.
- Views: `SharedFilePtr::Release` spellings, record-member collision calls
  (`mMeshCollider.IsEnabled/Disable`), virtual deletes of models, class-source
  file-scope declarations, virtual self calls by header slot, static members
  as free views, dBgActor_c-derived actors, fBase_c `actorID`.
- **Real ABI bug fixed**: natively bound functions returning `bool`/`int16_t`/
  `uint8_t` (e.g. `Player::IsState` 0x020e308c) were called by projected code
  as `int`-returning; the upper register bits are undefined on x86-64 and
  AArch64. The sanitizer-free probe build read garbage (IsState "true"), so
  its castle run diverged from ASan at frame 2. All 19 bound narrow-return
  natives now return 32-bit values (sign/zero-extended as the ARM callee
  does). ASan and the fast probe now agree frame by frame (RAM+heap diff).
- `LoadExitObjects` (ov002 0x020fe420) leaves `rot.z` unset on the cartridge
  (stack slot sp+0xc); the projection defines it as 0 so exit actors are
  deterministic. Player `func_ov002_020c607c` (vertical line probe) and
  `func_ov002_020ef070` (forward line hit) written from their sources with
  the cartridge frames (dBgCh_Lin/dBgPi temporaries).
- **CI fix**: run 42438572159 failed in `stage_runtime.cpp:2956` with clang
  `-Wvexing-parse` (APK only). Fixed; `ndk_warning_check.py --native` now also
  checks the hand-written runtime sources with the target's flags.

Measured (private EU ROM, host, Yoshi slot, no input, `SM64DS_STAGE_FRAMES=4000`):
the opening reaches `LoadLevel` at frame ~2473, fades out, the castle stage is
destroyed at frame 2505 (all actors' CleanupResources/destructors, overlays
unloaded), the next stage is constructed in the carried RAM at frame 2506:
`dScStage_c::InitResources` loads its archives and level, and
`ContinueKuppaScriptIfNecessary` resumes the opening script (white fade on
the script fader). Frontier: `HUD::InitResources` (ov002 0x020fda04,
external/sm64ds-pc/src/_ZN3HUD13InitResourcesEv.cpp: VRAM/palette loads, HUD
record fields `unk60..unk68` that the HUD view does not type yet).

Not verified / open:
- Yoshi is not yet controllable: the reloaded stage stops at the HUD frontier.
  Walking/jumping/camera/collision under control are not measured yet.
- No APK built or run in this session; Android device behaviour unverified.
- The carried space keeps the old space's native segment mappings; ASan fake
  stack segments could be stale in ASan builds (not observed).

Tests:
- ASan/UBSan no-ROM suite (`run-stage-probe.sh`, fast=0): rc=0.
- ASan/UBSan private-ROM full castle test (all save/character paths): rc=0.
- ASan/UBSan private-ROM opening, Yoshi slot, 4000 frames requested: no
  sanitizer report; LoadLevel to level 1 at frame ~2500, reload at 2506,
  stops at the HUD::InitResources frontier (expected, unlinked).
- `ndk_warning_check.py` (stage+packed+minigame generated units and native
  runtime sources, clang 18, APK flags): 2361/2361 clean.

Next step: HUD::InitResources (add `unk60..unk68` and `GX/GXS::LoadOBJPltt`
to the HUD view), then follow the frontier until the opening hands control to
Yoshi; verify walk/run/jump/land/camera with `SM64DS_STAGE_INPUT` traces for
3600+ frames under ASan.

---

# Checkpoint — 2026-11-07 (session 3), Yoshi castle opening runs; Player states linked

Branch: `main` (pushed). Area: **player / castle / camera / collision / audio / particles / tooling**.
Evidence: private EU ROM (SHA-256 ce5829aa…) host runs of the production stage
path with ASan/UBSan (`run-game-tests.sh` mirror, leak detection off). No APK
build, no Android device in this session.

What is new (original code; native views only where the projection cannot
express the ABI, each citing its recovered source or the cartridge code):
- **Player state machine**: 80 recovered `Player::St_*` units (Wait/Walk/Run/
  Jump/Fall/Land/Swim/Slide/...) linked in one batch; 20 more (cannon, grab,
  kicks, balloon, Yoshi mouth) still need struct-member projections.
  Level entry: `St_LevelEnter_Main` (8-byte PMF step table), entrance steps
  0/1/2/5, `FinishedAnim`, floor-pitch probe, walk-physics core
  `func_ov002_020d3b9c` (upstream port/unmatched C body), many Player.cpp
  blocks (020c44c4, 020c0434, 020c18b0, 020c04e0, 020caf68, 020bf224, ...).
- **Real bug fixed**: `dActor_c::FindWithID` is a static member; its member
  view added a receiver, so every lookup by unique ID returned null. With
  the fix the opening KuppaScript reaches Mario/Luigi/Wario (warp pipe, door).
- **Audio boundary**: `NNS_SndArcPlayerStartSeqArc(Ex)`, `SetVolume`,
  `SetTrackPan` over a cartridge-visible NNSSndSeqPlayer shadow record per
  native effect player (handle words point at it, as in the SDK); SND3D
  handle update `func_020485f0/func_02048d80`; `Sound::PlayBank0/3`.
- **Models/structors** read from cartridge code where the sources only
  declare them: Model/ModelAnim::Virtual10, TextureSequence D0/D1,
  dExtFrameCtrl_c/dExtAnmModel_c/Model/dExtModel_c D2, dActor_c D2,
  CutsceneObject D1, daDemo_c::anmModel_c D0, `__cxa_vec_cleanup`.
- **Collision**: dBgPi::CopyTo/operator=, dBgCh_SphCrr::SetFloorResult,
  dBgW_KcMbg::GetNormal. **Particles**: SysTracker::Contents registry
  (FindData/Create/Link/Unlink, Entry Initialise/Reset), simpleCallback_c
  SpawnParticles/OnUpdate, running dust. `__aeabi_idiv`, `cstd::abs`.
- HUD factory `dMeter_c_classInit` (only reached on a save that already saw
  the opening; HUD::InitResources is the next unit there).

Tooling (speeds every next session):
- `android/tools/frontier_batch.py <addr|symbol> [--apply]`: plans the batch
  for a missing handle -- free units, member units, Player.cpp members and
  free blocks, ldr-ip tail-call veneers -- and appends them.
- Member views: reference parameters, Player self calls (IsState/ChangeState/
  FinishedAnim/...), Sound statics on mCamSpacePos, scalar dActor_c base
  members of actor classes, opaque State records, C units naming `this`.
- Castle probe env: `SM64DS_STAGE_CHARACTER=3` (Yoshi), `SM64DS_STAGE_SKIP_INTRO=1`
  (SaveData+8 bit 7, the ROM's own "opening seen" flag), `SM64DS_STAGE_INPUT=
  "first-last:keys:x:y,..."`, `SM64DS_STAGE_TRACE_PLAYER=N`, `SM64DS_STAGE_LIST_ACTORS=1`.

Measured (private EU ROM, host, Yoshi slot, no input): the original opening
now runs to **frame 1994**. Frames 0-1300: Lakitu flyover; ~1300-1450: the
warp pipe; the script spawns Mario/Luigi/Wario (IDs 84-86) through
func_0200e5fc and drives them (level-entry jump out of the pipe, landing,
St_Wait, then walking/landing/dive/slide physics from ~1880 on), while Yoshi
(param1 3, data_0209f394[0]) waits in St_Wait on the castle roof
(y = 13303872). The KuppaScript (data_0209fc48 = 0x02089af8) is still
running at the frontier, so input is not yet given to Yoshi.
Full castle ROM test (all four save/character paths) and the no-ROM suite:
rc=0 on the final commit of this session (host ASan/UBSan, leaks off).

Frontier: frame 1995, `Player::Hurt(const Vector3&, u32, Fix12i, u32, u32, u32)`
(external/sm64ds-pc/src/_ZN6Player4HurtERK7Vector3j5Fix12IiEjjj.cpp, a member
with a reference parameter -- add it as a MEMBER_UNITS entry; the views now
translate reference parameters) plus func_ov002_020bd250 and
func_ov002_020d5cec. Verify visually (SM64DS_STAGE_DUMP) that a character
really gets hurt in the original opening at this point; if not, the cause is
upstream (physics or script input) and must be found first.

Iteration loop used this session (about 4-5 min per frontier step):
`frontier_batch.py <addr> --apply`, regenerate (lift_stage.py,
native_sources.py, packed_sources.py), incremental compile, then
`SM64DS_STAGE_CHARACTER=3 SM64DS_STAGE_FRAMES=4000 game_program_test <rom>`.
Most time is the 1300-frame opening replay under ASan; a stage snapshot
or a non-ASan probe build would cut it substantially.

Known approximations (documented in code): the stage voice table plays
handle-based effects on native players 2-15 without SND3D pan/volume
beyond SetVolume/SetTrackPan; func_02048c30 initialises a register the ROM
leaves unset on its stopped-voice path; func_ov002_020d3b9c is upstream's
port/unmatched near-miss body.

NOT verified: Yoshi under player control, walking/jumping from input,
camera following the player, door entry, HUD (only on saves that saw the
opening: HUD::InitResources next), APK/NDK build of the new units, device.

---

# Checkpoint — 2026-11-07 (later), castle intro renders; Player::Behavior reached

Branch `ccr-f1be4bbe-fzzi8q`. Area: **graphics / castle / player / audio**.
Evidence: private EU ROM host runs (ASan/UBSan), PPM frame dumps inspected.

- **The castle grounds render from original code.** Two blockers fixed: the
  stage never opened a new GX frame (`ntr::gx_begin_frame` per stage tick,
  the SwapBuffers boundary title/minigames already had), and the projection
  was all zeros because `data_0209ee90+0x44` (perspective W scale) and the
  default light/material registers set by the tail of `func_0201a054` were
  missing from the fresh stage image. Frames 0..1100 show the Lakitu intro
  flyover (castle, tower, sky/clouds, grounds).
- The intro runs ~1340 frames without input; at frame ~1138 the player is
  rendered (native Player::Render, TextureSequence::Update, head-model
  selector and two predicates translated from cartridge ARM code where no
  source was recovered), and at **frame 1343 `Player::Behavior`** starts
  (native, from the recovered body; state machine via the State mMain PMF).
- Audio: Sound::Play2D / Play / PlayCharVoice on the native effect player.
  KNOWN GAP: SND3D positional attenuation is not ported.
- Test: the castle ROM test now accepts the completed first tick (spawn
  state 0, dying trees, intro sound group, ARM9 .ctors destructor records).
  The full ROM test was not re-run after the last batch.
- Probe aids in game_program_test: `SM64DS_STAGE_FRAMES`, `SM64DS_STAGE_DUMP`,
  `SM64DS_STAGE_DUMP_EVERY`, `SM64DS_DUMP_CODE=<addr>` (raw bytes for ARM
  translation), `SM64DS_GX_DEBUG` (gx.cpp), `SM64DS_TRACE_FAULTS`.
- Tooling: `SOURCE_MEMBERS` projects class members straight from `src/`.

Frontier: frame 1343, inside Player::Behavior — `func_ov002_020bdef0`
(Player.cpp member; its projection still fails to compile and needs field
replacements). Then the remaining Behavior callees and the first player
states (wait/walk) — after that, feed stick input through
`GameProgram::tick` in the probe to verify free movement.
Not verified: APK/NDK build of the new units, device behavior, player
movement (no input driven yet).

---

# Checkpoint — 2026-11-07, castle grounds run whole original frames (intro cutscene live)

Branch `ccr-f1be4bbe-fzzi8q` (session branch; ~50 commits on top of main
76260a4). Area: **engine / castle / cutscene / camera / HUD / actors**.
Evidence level: private EU ROM (SHA-256 ce5829aa…) host execution with
ASan/UBSan; no-ROM suite (`run-game-tests.sh`) passed after the first batch.
No APK build or device test in this session.

What changed (all from recovered sources; native veneers only where the
lift cannot express the ABI, each citing its source):
- **crt0 static initializers**: `StageFlow::prepare_scene` now runs the
  cartridge's ARM9 `.ctors` table (0x02086b60, 23 entries). Before this the
  intro KuppaScript path pointers (patched by `__sinit_02073e6c`) were null.
  All 23 `src/unnamed/arm9/0207/__sinit_*` bodies are projected
  (`lift_stage.py: arm9_initializers`), destructors as code handles.
- `data_0209fc48` is the running KuppaScript pointer (RunKuppaScript), not a
  byte flag; the bridge now carries the whole word.
- DS-side **fader proxy**: original code calling `data_0209f5bc->vt->…`
  reaches GameProgram's native fader through a proxy vtable of native handles.
- Native from recovered source: Stage::LoadFog/LoadTextureTransformers/
  LoadSkybox/Render/RenderModel/RenderModelTransparent/Behavior,
  Particle::SysTracker::Initialise/Update/Contents::Update, texture/palette
  VRAM allocators, ShadowModel::CleanAll/RenderAll, dScene_c and dActor_c
  Before/After Behavior/Render/Cleanup, fBase_c cleanup + destructor chain
  (MIPS, daTree_c), Clipper ctor/TransformAndClip, cstd::sqrt/atan2 stage
  bindings, LakituBro::Render, Door::Behavior, daTree_c::Render/Cleanup,
  daSetSE_c::Behavior, Player HasNoCap/IsCollectingCap, OAM::RenderSub.
- Linked isolated units (auto-frontier loop): cutscene handlers
  (func_ov002_020f7410/7bb8/…), camera path (func_02008b4c), Camera::
  Behavior, Minimap Behavior/Render/position transforms, OAM::Render,
  overlay-1 coin/cap HUD (func_ov001_020aaf40/aaa54/aa960), Clipper frustum
  test, message/BG leaves, Vec3 helpers, Timer, etc.
- Tooling: `stage_isolated_views.py` handles nested classes, Vector3 use,
  local record hoisting, array members, `this->`; overlay-1 symbols resolve.
  `SM64DS_TRACE_FAULTS=1` prints the native stack for rejected handles.
  `SM64DS_STAGE_FRAMES=N` (game_program_test) runs N castle frames and
  reports the first missing dependency, then exits.

Measured frontier (private ROM, no input): castle spawn + InitResources +
intro cutscene start + **frames 0 and 1 complete**; frame 2 runs Stage::
Behavior, camera, minimap, doors, trees, Lakitu and stops at
`_ZN14CutsceneObject6RenderEv` (0x020f6cac area, ov002) — the next unit.
MIPS is correctly destroyed on frame 1 (fresh save fails its progress check,
as in the source), then batched trees.

Adaptations to note (not fabricated behavior): Stage::Behavior skips its
own CheckCameraInput/CheckInput calls because StageFlow::update_input runs
those same original routines once per frame before the scene (Android
input bridge). Actor memory is the native packed heap, so fBase_c's
operator delete releases through it.

Not verified / open: the castle test's post-tick actor assertions after the
first tick now completes (run in progress at time of writing); APK/NDK
compile of the new units; rendering output of the castle frames (no
screenshot inspected); no player control yet — the intro cutscene
(Lakitu/camera/message) must finish before free movement.
Next: keep the frame loop going (CutsceneObject::Render, message system
func_0201f138 / Message::Update when the letter appears), then drive input
with SM64DS_STAGE_FRAMES once the cutscene ends to verify Player movement.

---

# Checkpoint — 2026-11-06, castle grounds past MIPS into the Lakitu intro

Branch `claude/sm64ds-android-port-54w4q4` (local commits; push blocked by
missing GitHub write access, HTTP 403). Area: **castle / actors / camera**.

Private EU ROM (SHA-256 ce5829aa…) host runs of the production stage path
(ASan/UBSan, `run-game-tests.sh <rom>`, `SM64DS_TRACE_STAGE=1`) moved the
first missing castle dependency through, in order:
- `0x0212cc2c` daMip_c_classInit → native factory from its relocations
  (0x474, dEnemyBase_c, dCcAc_c, dBgCh_Actr, ModelAnim, 2× ShadowModel).
- `0x0212c7fc` original daMip_c::InitResources (reviewed view of
  src/game/actors/d_a_mip.cpp). On the fresh save it returns 0 at the
  original 0x40000000 progress-flag check, as the source does.
- `0x0212c070`/`0x0212c004` original OnPendingDestroy / CleanupResources.
- `0x0212ed54` daC_Jugem_c_classInit (0x2e8, two ModelAnim, TextureSequence,
  two ShadowModel), original LakituBro InitResources/Cleanup/OnPendingDestroy,
  native TextureSequence ctor (0x02015a50) and SetState PMF dispatch (0x0212e728).
- `0x0212df84` StateArriveInit (castle intro) from the recovered isolated unit,
  with Camera::SetPos/SetLookAt/SetFlag_3, dActor_c::ClosestPlayer and
  func_ov002_020d228c (Player intro state + animation).
- `0x020c6f34` Player::St_LevelEnter_Cleanup.

Test fix: the overlay-6 image check now compares the loaded file only;
its BSS is legitimately populated by the original static initializers
(first difference was 0x021421ec, BSS). This failure predated this session.

Not yet done: the castle-test frontier assertion (still pinned to
0x0212cc2c) must be updated to the new frontier and assertions added for
the MIPS/Lakitu instances; Android NDK/APK compile of the new generated
units is unverified here (no SDK in this container); no device test.
Next: continue the Lakitu intro (state run PMFs, FUN_02029a68, Sound::PlaySub,
messages) until the castle grounds reach a steady playable frame.

---

# Checkpoint — 2026-11-06, Wanted earns its first original point

Work continues on main. Added 43 original function units, including all 23
Amida gameplay methods, five missing shared sprite/banner wrappers, the line
rasterizer, vector helpers, BG offsets and the actual ARM9 actor-list initializer.
The scene now schedules original behavior/render callbacks, waits on original
child actor readiness and retains its genuine overlay-6 vtable and PMF records.

Verified against a private EU cartridge with ASan/UBSan: actual title touch ->
room -> Wanted, original four-picture board, 3,413 OBJ pixels, a correct stylus
hit, original reward animation, an earned score of 1 and the next board.
The no-ROM actor/controller/constructor/record/wipe/math regressions pass too.
This is host execution of the Android runtime, not an Android phone test.

Corrected multidimensional array order, unaligned stepped scene views, Amida
Point value-copy ABI and null-based offsetof expressions. Original src/include
remain unchanged. Path-based /MG assets now use the actual cartridge filesystem.
Retail LoadFont(2) reads 0x20 bytes at +2 in a 0x20-byte palette; the native view
copies its fifteen owned 4bpp colours without reading the unused extra halfword.

Still being checked: background rendering (the first gameplay capture showed
sprites/timer but missing BGs), Amida startup/pen input, full Wanted game-over,
retry/quit/record persistence and final Android compilation. The full original
scene graphics reset is now connected and is under the private-cartridge test.
No complete minigame-room catalogue, final rendering or phone play is claimed.
No ROM, extracted file, screenshot or save is committed.

---

# Checkpoint — 2026-11-06, original Wanted and common minigame execution

Work continues on main. The native link now includes original Wanted gameplay,
base lifecycle, pause/options, 29 banner resources and animations, shared FX,
2D sprite wrappers and the original BSS callback initializers. The projection
preserves four-byte records and resolves source imports at cartridge addresses.

Local no-ROM game suite passed with AddressSanitizer/UndefinedBehaviorSanitizer.
The private EU title -> room -> Wanted integration is being exercised; no
successful launch, complete round or Android phone test is claimed here yet.
No ROM, extracted resource or rendered cartridge artwork is committed.

---

# Checkpoint — 2026-11-05, Hanachan Wiggler array and original state constructor

A third original 2D scene factory is now bound: Hanachan (0x182),
allocating its 0x4f68-byte record, including fifteen 0x98-byte Wigglers
whose five Point constructors are empty in source. dMgState_c initialization
now calls the actual recovered 0x020b8a70 constructor for every ported
2D scene instead of writing only its state field. The no-ROM scene
regression exercises the third factory and zeroed Wiggler components.
No playable round or complete minigame UI is claimed.

---

# Checkpoint — 2026-11-05, first original 2D minigame constructors

Native constructor bindings for Luigi's Wanted (scene 0x16e) and Amida
(scene 0x173): allocate exact retail object sizes, run the fBase scene
manager constructor, initialize recovered dScMgBase_c fields and its shared
state defaults, preserve record slot/top score, construct the original empty
Amida component arrays, and install cartridge RTTI-resolved original vtables.
The factory lives at the real ROM profile address; the original actor
post-construction hook executes afterward. There is no fake victory/score.

A no-ROM host test validates independent RTTI/vtable resolution, constructor
sizes, scene parameters, records and timers. Full pause-options icon setup,
scene-kind table and both minigames' gameplay methods remain unlinked. Any
missing constructor or callback fails at the original native address; these
are **not** two playable minigames.

---

# Checkpoint — 2026-11-05, preserve selected original row across overlay switch

Carry all 0x34 bytes of gMgMenuRow (ARM9 0x0209b308) from the live
overlay-5 menu address space to the overlay-6 gameplay owner before
the menu storage is destroyed. This preserves variant, scene parameters,
records, music and level fields selected by the genuine source.
The private EU cartridge regression checks the overlay-6 owner received
the complete row unchanged. This does not claim a playable round.

---

# Checkpoint — 2026-11-05, original minigame base leaves

Nine original ov004 entry points are now bound to native typed leaf bodies:
base variant test, display mode, rendering/pending-destruction defaults,
level lookup and credit arithmetic. Host regression covers saturation,
missing-scene handling and variant parameter bits. This is not a complete
scene constructor or a playable game.

---

# Checkpoint — 2026-11-05, ungated original minigame gameplay bootstrap

Branch **main**. Removed `supports_scene`, `mg_CanStartGame`, the
generated unsupported-icon rejection and the generic unsupported-scene stop.
The original menu selection now calls `StartSceneFade` for every unlocked icon.

Added a separate overlay-6 gameplay owner: it preserves shared live save/input
bytes, maps original overlay 4 and 6, binds recovered core/controller/scheduler
routines and follows the ROM scene-profile factory instead of a native
allow-list. This is an incomplete original-code boot path, **not** a playable
minigame. The missing actor/scene constructors must be ported next. A missing
native method will raise an unlinked-code diagnostic rather than showing a
fake game or successful score. Memory and cartridge bounds remain checked.

Game overlay residency is covered by a private-EU-ROM test; compilation and
actual Android playability require CI/device verification.

---

# Current checkpoint — 2026-11-05, original shared minigame controller and scanline wipe

Version **0.4.23-minigame-controller**, versionCode 27, branch **main**.
**The original room works; no complete gameplay minigame is playable yet.**

New original executable dependencies:
- All 52 dMgState_c methods/helpers, all 20 states and the original 49
  two-word member-pointer records. The controller retains its 0x28-byte
  cartridge layout. Native scene callbacks use checked slots 18/19/20.
  Invalid state indices and unsupported PMF tables fail before dispatch.
- Overlay 4 is mapped beside the original overlay-5 room, with actual PMF
  validation. The private controller test additionally maps overlay 6:
  the original text pointers at 0x020bca44 reference that overlay, which
  replaces overlay 5 at the same 0x020bfec0 load address during gameplay.
- 23 original common score/language helpers: score caps, high/low score
  comparisons, session best, five-rank insertion, record pairs and the
  original byte-1 save slot. The rank comparison and last submitted score
  remain distinct exactly as in the source. Actual EEPROM persistence is
  exercised; no gameplay-earned score is claimed.
- Native-owned dWipe_c uses the original typed methods, circle/ramp
  builders and HBlank/VBlank handlers. Adapted IRQ/MMIO/cache/palette
  boundaries feed per-line WIN0/WIN1 snapshots shared by both native
  BG and OBJ passes. Original timing and pending type changes remain.
- The PPU now supports WIN0/WIN1 priority, wrapped horizontal/vertical
  bounds, layer masks and color-effect gating; OBJ color fade endpoints
  are handled too. OBJ-window sprites and mosaic remain unsupported.
- Native color/brightness fader writes now go through logical MMIO,
  avoiding raw writes to unmapped DS register addresses on Android.

Verified locally with AddressSanitizer/UndefinedBehaviorSanitizer:
- All 20 controller states, timers, scene-readiness/fade gates, render
  PMFs, result-touch flow and malformed table rejection. Controller UI,
  audio and score interfaces use explicit test spies in this test only;
  production unresolved graphics/audio callbacks still fail at their call.
- The same controller test against actual private EU overlay-4/6 data.
- Original scoring cap/ranking/slot extraction/session semantics and
  actual original EEPROM save/reopen, including lower-is-better mode.
- Both original wipe builders, original scanline handlers, both engines,
  window priority/wrap/layer/effect masks, pending type and IRQ teardown.
- Existing original menu/title touchscreen round trip, unsupported-game
  tap rejection and four-session castle/180-frame Player regression.
- Existing PPU display/bank/capture tests, relocated video register and
  original math tests; source generator/hash tests and diff checks.

The previous bf72d8e menu checkpoint's public CI completed successfully.
This checkpoint's Android ARM64 APK/lint CI must be checked after publication;
no device is attached, and host tests do not establish phone playability.
Local LeakSanitizer remains disabled because the executor cannot inspect
thread proc entries; address/undefined checks stay enabled.

Remaining: full dScMgBase_c scene/resource lifecycle, banners/options/fonts,
overlay-6 scene/component constructors and game loops, first genuine complete
round/retry/return/earned record, then the remaining games and variants.
Gameplay taps remain gated before menu/wipe mutations until those bindings
exist. No original src/ or include/ file, ROM, resource or save is published.

---

# Source-data correction — 2026-11-05, original initial character

A further private-ROM check confirms gMgGroupBase at ARM9 0x0208a170 starts
at 3 (Yoshi), and gMgLastSelected starts at -1. The first native menu entry
now reads that original initializer instead of forcing group 0. Later entries
retain the runtime's last selected tab/page. The private-ROM regression checks
Yoshi first and then switches Mario -> Yoshi -> Mario before the page tests.
This follow-up retains the targeted Clang build fix; its tests/build are rerunning.
Gameplay scenes remain unlinked and no Android device claim is made.

---

# Verification update — 2026-11-05, original title touch and ARM64 compile follow-up

The private-ROM regression now drives the actual title with the stylus:
intro tap (128,96), then STANZA DEI GIOCHI (220,170). The original frontend
returns 7 and the room opens, without injecting a result code. Full menu,
return/title/castle/Player and no-ROM regressions pass afterward.

Code 5fc39c4 passed public CI native filesystem/save/game tests, original
frontend tests, GLES tests and the native ARM64 frontend runner. APK compilation
then stopped on Clang -Wreturn-type-c-linkage for Ptr<char> in the new service.
The follow-up uses the same targeted diagnostic setting as the existing
projected frontend; both sides are C++ and retain the identical checked
four-byte Ptr<char> signature. An explicit ABI size assertion is added.
The APK/lint rerun is pending; this update does not claim an Android build pass.

No game scene or complete round has been enabled. The remaining gameplay
limitations below are unchanged; no Android device test has been performed.

---

# Current checkpoint — 2026-11-05, executable original minigame room

Version **0.4.22-minigame-menu**, versionCode 26, branch **main**.
**The original room/menu now runs; no minigame gameplay scene is playable yet.**

Implemented and exercised against the private EU cartridge:
- Title result 7 calls the unchanged original StartMinigameMenu(0). Native
  tick executes scene 5; exit calls original ExitMinigameMenu and recreates
  the original title with its own VRAM resources. The final private-ROM test
  dismisses the intro and touches STANZA DEI GIOCHI through the original
  frontend. It does not inject result 7; the real dispatcher produces it.
- Full recovered dScMiniGm_c InitResources, Behavior, Render and CleanupResources
  run via a hash-guarded four-byte-pointer projection, with the exact 0xb0-byte
  scene record. Original src/ and include/ are unchanged.
- Actual BG files, dynamic group icons, locked tiles, original OAM sprite tables,
  Italian captions, records, NEW badges, tab hit tests, arrow animations,
  page timers, 8-pixel scrolling and exit countdown execute from source/data.
  Main/sub rendered scanouts were visually inspected. Android's two-screen
  side-by-side composition is unchanged.
- Live save/input spans share bytes with recovered ARM9 helpers. Original
  EEPROM persistence is used; dirty flags are not cleared after a failed write.
  Cartridge archive preloads and LZ writes are checked, including VRAM targets.
- Original OAM reset requires overlapping forward copies; this is retained.
  Original width/height helpers now perform the equivalent byte reads without
  constructing misaligned native int references. Unused sprites stay hidden.
- Unsupported game taps are rejected BEFORE selected-row, palette, wipe or
  pending-scene changes. They play the original error effect and leave the
  real menu responsive; there is no fake successful launch or generated score.

Local verification (AddressSanitizer + UndefinedBehaviorSanitizer):
- Complete no-ROM game dependency tests, including original OAM size tables,
  reset, rendering/upload, single-buffer routing and 128-entry limit: PASS.
- Full private-ROM boot/title -> menu -> tabs/pages -> exit -> original title,
  visible sprites on BOTH screens, unsupported-game rejection, and existing
  four-session file/castle/180-frame Player regression: PASS.
- Original EEPROM defaults/records/cap repair/checksums/mirror/reopen/I/O-failure
  tests: PASS. Actual EU overlay-5 catalog/slot permutation: PASS.
- Relocated video-register, GX/palette, projection/fixed-math tests: PASS.
- Native generator tests, source manifest regeneration and diff checks: PASS.
- Full local host suite stops at missing javac; public CI runs that full suite,
  ARM64 frontend, GLES, APK build and lint. Latest menu build status is pending
  at this code checkpoint; do not confuse the prior checkpoint's CI with it.
- LeakSanitizer cannot inspect this executor's proc thread entries, so local
  runs disable leak detection only; address/undefined checks stay enabled.

Remaining blocking work, not completed or verified:
- Overlay-4 dScMgBase_c/dMgState_c lifecycle, banners/options/fonts and native
  callback tables; overlay-6 game/component actor dispatch for 30 distinct
  scenes and six variants; dWipe capture/scanline lifecycle.
- First original game launch, complete round, real scoring/win/loss/retry,
  return after gameplay, and persistence of a gameplay-earned record.
- Android device/emulator touch, audio, pause/resume and playability. No device
  is attached. Host tests/scanouts do not establish those claims.

See android/MINIGAMES.md for exact scope and next executable dependencies.
No ROM, extracted resource, screenshot or save image is published.

---

# Current checkpoint — 2026-11-05, verified EU minigame catalog/save correction

A private-ROM test exposed two errors in the earlier foundation:
- The record slot is sceneParam bits 8..15, not bits 8..23. The old parser
  rejected the actual EU overlay 5 table. It now accepts all 36 real rows
  at 0x020c24d8, including row 2 -> save slot 6 and row 6 -> save slot 10.
- Menu rows are not in record-slot order. Default initialization and cap repair
  now use the original MiniGm_GetSaveTable mapping, validating the complete
  permutation before mutating saves.

Verified locally with AddressSanitizer and UndefinedBehaviorSanitizer:
actual private EU overlay catalog acceptance; original EEPROM defaults,
record repair, checksums, mirror recovery, reopen and I/O-failure regression.
LeakSanitizer is disabled locally because this executor cannot inspect its
thread proc entries; address and undefined-behavior checks remain enabled.

Menu/OAM integration is still under development and is not included in this
checkpoint. No minigame gameplay or Android device interaction is claimed.

---

# Current checkpoint — 2026-11-05, original minigame records, menu timing and fixed artwork

Last tested code checkpoint: 3bb6d1cdec0944b65584723cbc213ab7758f8297.
Branch: `main`; no branch, pull request, ROM, or extracted cartridge asset was created.
Port version remains 0.4.21-player-animation. **Menu scene 5 and original
minigame scenes 0x169..0x186 are NOT yet linked or playable.**

New source-derived runtime work:
- `MinigameProgress::initialize_records` implements the original
  `dScMiniGm_c::InitResources` once-only seed of all 36 x 5 saved records,
  the 0x80000000 initialized flag, unsigned score-cap repair, and
  the `ownRecords == 1` first-rank-only exception. It returns dirty status;
  no fabricated persistence or automatic score was introduced.
- `GameProgram::prepare_minigame_menu` explicitly preflights the real overlay-5
  catalog, loads original menu art, initializes original save records, and
  creates a menu state session. It is deliberately **not called** from the
  title frontend while scene 5 still lacks its renderer/lifecycle.
- `MinigameMenuSession` owns original 0x12-frame page arrow delays,
  eight-pixel per-tick scroll over 0xb0 pixels, 0x1c-frame exit countdown,
  four character tabs, 0x1e group debounce and 0..0x3f animation phases.
  It only emits selection/exit requests, not successful game launches.
- `MinigameMenuArtwork::load` reads and bounds-checks actual EU ROM files
  for original main/sub BG2 characters, main BG2 map, palettes and OBJ
  characters, including the source's 0xd5..0xda language selection. LZ77
  decoding is checked; missing or malformed resources throw, not replaced
  by synthetic artwork. Dynamic group icons and OAM tables are NOT linked.
- `android/tests/run-game-tests.sh` includes all new native menu units and
  the five exact recovered ARM9 helpers; no-ROM tests verify record defaults,
  cap repair, score persistence, page events, scrolling, exit countdown,
  compressed-asset validation and rejected invalid input.

Verification and remaining blockers:
- At publication the code commit's GitHub Actions no-ROM native/host tests,
  GLES tests and frontend ARM64 runner had passed; final ARM64 APK build/lint
  was still pending. Run: https://github.com/RonyxDumb/sm64ds-android/actions/runs/42054328151
- GitHub CI has **no private European game ROM**: the 36-row overlay-5
  discovery and artwork loading must also be tested against a user-provided
  cartridge and on an Android device before any playability claim.
- The scene-5 dScMiniGm_c resource lifecycle, original OAM sprites and
  dynamically chosen group-icon BG maps, and bridge from frontend return
  code 7 remain unimplemented. GameProgram::tick still handles scenes 1
  and 3 only. Overlay-6 gameplay actor dispatch, scores and 30 distinct
  minigames with six variants remain the subsequent major integration.
- Existing File Select, stage path, native screens and saves have not been
  deliberately bypassed or replaced by fake minigame UI.

---

# Current checkpoint — 2026-11-05, execute original ARM9 minigame save leaf bodies

Continues published main 91484c03a09581644190a669f729ff09bda4a1a9.
Current port remains **not playable** in the minigame room; the earlier
0.4.21-player-animation version is unchanged.

Five recovered, unchanged ARM9 C source files now compile directly into the
Android target and the host SaveStore regression:
func_02013568 (set record), func_02013580 (read record),
func_020135d0 (unseen badge), func_0201361c (unlock),
func_02013638 (unlock test). The checked native MinigameProgress interface
calls these original bodies, never a success-returning stub. The original
data_0209caf4[36][5] alias is mapped exactly onto the live save block
at data_0209caa0 + 0x54. The recovered func_02013598 still has an
unconditional negative shift for indices below 28, so clear_new uses a
bounded native byte/word implementation instead of invoking undefined
behavior. The original SaveData::SaveMinigames handles actual persistence,
mirror and checksums. Existing minigame catalog, overlay discovery and
touch hitbox decoder remain unchanged.

CI: latest push awaits the native filesystem/save/game tests, ARM64 APK and
lint results. This is not evidence of a running menu or any playable
original minigame. ROM-based overlay-5 table acceptance, full scene-5
lifecycle, overlay-6 gameplay, UI rendering, touch and device persistence
still require separately verified runtime work.

---

# Current checkpoint — 2026-11-05, original minigame catalog, saves and touchscreen hitboxes

Continues published main 1163315db6494dadf97a5361ba0f30056808ecc4.
Version remains 0.4.21-player-animation; **neither the menu nor any minigame is playable**.

New native-owned runtime components:
- minigame_catalog.{h,cpp} reads overlay 5 from the supplied European
  cartridge, validates the 36 recovered 0x34-byte MgEntry rows, unique
  0..35 parameter tags, 30 unique original scene IDs (0x169..0x186), and
  six alternate entries. It also verifies that original overlay 6 exists.
  The data is not guessed or synthesized if the cartridge table is absent.
- minigame_progress.{h,cpp} accesses the live, original minigame save block
  at data_0209caa0+0x44. The source-derived flags/bit allocations implement
  unlocked/new badges, seen state, and all 36x5 original record slots.
  persist() calls recovered SaveData::SaveMinigames, including real EEPROM
  checksums and mirrored writes, returning failure when a write fails.
- minigame_controls.{h,cpp} identifies original touchscreen hitboxes from
  d_s_mini_gm.cpp (4 character tabs, two icon pages, arrows and exit),
  filters locked entries, and honors caller-side input gating.
  It is a pure touch decoder, **not** an independent or synthetic visual menu.

Verification:
- No-ROM catalog tests construct only a parser fixture and reject duplicate
  tags, invalid scene IDs and unsupported overlay routes.
- No-ROM save tests use a separate 8 KiB EEPROM image for real recovered
  minigame save, reopening it to assert unlock/new/record persistence.
  Touch decoder tests verify page changes, selection and locked-icon rejection.
- Private-ROM game test now preflights the actual overlay-5/6 catalog.
  No private ROM is on GitHub Actions; that validation requires one supplied
  separately. A synthetic parser fixture is not evidence of correct ROM art.
- Android CI status of the latest code checkpoint was pending at publication;
  Android device/emulator interaction, original menu rendering, game startup
  and completion have **not** been verified.

Still missing and blocking a playable minigame room:
- Link the original dScMiniGm_c InitResources/Behavior/Render/Cleanup,
  OAM draw and animation callbacks, game audio/fonts and original overlay-5
  scene scheduler into ARM64 packed native bindings.
- Connect frontend result 7 -> original StartMinigameMenu(0), scene 5 lifecycle,
  exit/back transitions, fader/scroll timers, and saved progress loading.
  Do not enable the transition before original scene callbacks are executable:
  the current GameProgram::tick rejects all scenes except 1 and 3.
- Link 0x169..0x186 to original overlay-6 minigame actors including dScMgBase_c,
  dMgState_c (ov004), per-game scenes, gameplay, scoring and error-free return.
  Existing raw-source compilation does **not** establish runtime playability.
- Test all menu/round-trip/persistence assertions on device with a user-owned
  cartridge; preserve existing side-by-side screens, title and stage routes.

These changes are foundational and must not be reported as completion of any
individual minigame or the full minigame suite. No ROM or extracted assets
are committed. All commits are on main, without PRs or new branches.

---

# Current checkpoint — 2026-11-05, minigame font work-buffer seam

Continues published main ef931b13c5d1b2fdf5dee101f91ce71b23803884.
**Partial infrastructure only; minigame menu and gameplay NOT working or verified.**
The existing 0.4.21 player-animation version remains unchanged.

The original GetSceneOverlayID routes scene 5 (dScMiniGm_c) to overlay 5
and minigame actor IDs 0x169–0x186 to overlay 6. The frontend's original
dScDSMT_c::Behavior requests StartMinigameMenu(0) for result 7, but the
native GameProgram::accept_title_result currently only launches save-file
results 3–5. GameProgram::tick still runs scenes 1 and 3 only. Neither the
overlay-5 menu actor nor overlay-6 game actors are bound to a native
packed-ABI scheduler. Their original src/minigames sources exist but are not
in the Android execution manifest. In particular, no original menu render,
page navigation, save-record lifecycle, or completed minigame can be claimed.

One concrete dependency is closed: Mg_GetWorkBuffer now returns native-owned
storage sized to the real cartridge's file 0x980e. LoadFileAt's checked copy
can write to this storage without hijacking live BG/OBJ VRAM, and rejects
writes beyond it. The private-ROM game integration test checks buffer
stability, exact resource bytes, and out-of-bounds rejection. The test is
compiled in CI but its ROM-backed branch only runs when explicitly supplied
a private cartridge. No protected ROM or extracted game assets are committed.

Verification status at publication: GitHub Actions build for ef931b1 was
in progress; not yet a reported pass. Android device/emulator rendering,
touchscreen, navigation, original game launch, play-to-completion and record
persistence have NOT been tested. The new seam must not be equated with
functional minigame support. Previous two-screen display, stage and title
routes are intentionally unchanged.

Next: implement an original overlay-5 scene-owner with the dScene_c/fBase_c
lifecycle in a checked packed::Space, mount its immutable cartridge tables
and original OAM/font/audio callback code, connect result 7 + saved-minigame
read/write without modifying original decomp files, then verify selection,
return, unlock/records and overlay-6 scene dispatch. Bind and test at least
one complete original overlay-6 game before extending the registry. Do not
replace missing native callbacks with successful stubs.

---

# Current checkpoint — 2026-11-05, original Player body animation and rendering

Continues published main ede8cbeca70166789142e461d6f1dfba2c49148d.
Version 0.4.21-player-animation, versionCode 25. **Not playable.**

Complete original Player_AdvanceAnims and Player::IsAnim bodies now execute
against the actual body-model and animation-file tables. Character/metal model
selection, the eight animation-specific texture-cursor resets and body/texture
frame advancement are preserved. Complete dExtAnmModel::UpdateVerts and Render
connect BCA bone sampling, vertex-matrix rebuilding and the original model draw.
No synthetic motion, substituted pose or unknown-callback success is used.

ASan/UBSan passes all four private-ROM save/character launches. Each original
body model (0, 1, 2, 3) executes 180 consecutive animation/bone/render updates:
the cursor matches original wrap/clamp behavior, skeletal transform storage
changes, and every draw emits geometry. The existing 400-update water/material/
geometry tests and all item/mushroom, Player initialization, camera, KCL,
moving-mesh and process-list assertions still pass. No-ROM integration, packed
source rename/removal, hostgen/native-source and source-hash checks pass.
LeakSanitizer is disabled locally.

These tests draw the selected body independently of the complete Player actor.
They do not verify full Player::Behavior/Render, head/accessory placement,
walking, jumping, camera following, world audio or complete scene-3 frames.
Initialization still reaches the actual rabbit factory dependency at 0x0212cc2c
(overlay 85). It must be closed before the normal world-frame path can advance.

The latest published water-render Android workflow was running when this
checkpoint was prepared; its frontend-arm64 job passed. This increment's full
ARM64 APK/lint workflow is pending publication. No local Android SDK/javac or
attached device/emulator is available. The broad host runner's native ROM probe
passes but its Java step requires CI. Side-by-side screens and previous graphics
fixes remain. No ROM, extracted assets, private captures or APK is committed.

Next: finish original scene/actor/particle/HUD initialization and the remaining
per-frame Player state, collision, camera and world/audio callbacks. The body
animation milestone is not playable-gameplay evidence.

---

# Previous checkpoint — 2026-11-05, consecutive original water rendering

Continues published main 0ba35886ea031f2842734eb53c889c823008260c.
Version 0.4.20-water-render, versionCode 24. **Not playable.**

The complete original castle-water Render and CleanupResources bodies and
dExtSimpleModel Render are now connected. On each of four private-ROM
save/character launches, 400 consecutive original water Behavior/Render calls
advance/wrap the BTA, change actual material records and emit nonempty geometry.
The regression also verifies that all seven waterfall positions come from the
original spawn table. These are isolated actor updates, not full stage frames;
all four merged integration paths retain the item/mushroom checks and reach
the actual rabbit factory dependency, 0x0212cc2c.

ASan/UBSan passes all four launches and existing Player/camera/KCL/mesh/ordered
process tests. Video/math, source-hash, native-source/hostgen checks pass.
LeakSanitizer remains disabled locally. An incremental generation bug found
while integrating the concurrent checkpoint is fixed: renamed/removed generated
source views no longer leave duplicate native callback definitions behind. Its
rename/removal regression passes and runs in the host CI suite.

The preceding 7e7fdb9 Android workflow succeeded, including ARM64 debug/release
builds and lint. This increment awaits its own CI. Local javac and Android SDK
are unavailable; no Android device/emulator is attached. The broad host runner
passes the private-ROM probe then stops at javac. Side-by-side screens and
graphics fixes are preserved. No ROM, extracted assets or captures are committed.

Next: rabbit initialization, remaining scene/HUD/particles and the actual
Player/camera/render/audio world-frame chain.


---

# Current checkpoint — 2026-11-05, original item blocks and mushroom resources

Continues published main 7e7fdb95454b2eb7f9243038eceb3e0869c4464c.
Version 0.4.19-native-items, versionCode 23. Not playable.

All six original item-block factories now construct their 0x3f8-byte records,
background collider, animated model and cuboid shadow. Complete InitResources,
model/collider matrix helpers and Idle enter execute. SetState invokes the
original overlay's real member-pointer table, including checked adjustment and
unknown-callback failure. Original content/model selection remains intact.
The 1UP/mega-mushroom factories preserve their 0x398-byte enemy records,
cylinder, sphere/line query, model and cylindrical shadow. Complete original
InitResources and empty OnPendingDestroy are projected, including hidden-type,
collision, display, save and course conditions. The unlock counter is a full
32-bit field as its source header requires. Movement and prize behavior are
not yet verified for these actors.

ASan/UBSan passes all four private-ROM save/character launches beyond both
former factories to daMip_c_classInit at 0x0212cc2c (overlay 85). Live actor
checks cover models/BCA/shadow ownership, original Idle scale/gravity, copied
shadow matrix, moving KCL/CLPS, mushroom owner links, home positions, type and
unlock fields. No-ROM checks execute the real block Idle PMF and reject an
unbound state callback. Earlier Player, camera, scheduler, KCL, signpost and
400-step water/material checks still pass. Source hashes and diff checks pass;
LeakSanitizer is disabled locally. Native filesystem, video/math, input, saves,
heap/resources, scheduler and audio/PCM regressions also passed this session.

The 7e7fdb9 Android workflow completed successfully: build, ARM64 debug/release,
lint and frontend-arm64. This increment awaits its own CI. No Android device
or emulator is attached; local javac/SDK are absent. There are still no complete
original world frames, Player movement/jump/render verification or playable-APK
claim. Next: original rabbit initialization, then remaining scene/particle/HUD
and actual per-frame Player/camera/render/audio dependencies. Matching sources,
side-by-side screens and previous graphics fixes are preserved; no game data
is committed.

---

# Current checkpoint — 2026-11-05, original castle water and texture animation

Continues published main 39e13b3bb779b22a89a5768903cd2d263428ee79.
Version 0.4.18-castle-water, versionCode 22. Not playable.

The original castle-water factory, complete InitResources, waterfall-spawning
helper and Behavior now execute. The real water BMD, KCL and CLPS are owned and
the collider is registered. Its original seven waterfall actors initialize
with the correct course-dependent particle ID. Texture SRT Prepare/SetFile/Update
and the complete original frame-controller Advance body are connected. This
initializes emitters; their per-frame particle production is not yet verified.

ASan/UBSan passes all four private-ROM save/character routes through water to
QuestionBlock's factory at 0x0214a8ec (overlay 102). Each route executes 400
original water Behavior/BTA Update calls, checks cursor wraps and actual changes
to material records, and inspects the live colliders and seven emitter actors.
No-ROM tests additionally check file changes, cursor retention, reverse wrapping,
fractional speed and non-looping clamping. Existing Player, camera, signpost,
mesh, input, source-hash and process-list checks remain active. LeakSanitizer
is disabled locally. This is repeated execution of one actor, not complete
world frames or playable character movement.

The integration now compares the pinned delinks' actual .text/.init sections
with the cartridge. The former comparison ended at the initializer pointer and
incorrectly included writable BTA descriptors; the original texture Prepare
legitimately resolves those descriptors in .data. Executable bytes still must
remain identical. The empty overlay 69 has an explicit audited no-code marker.

The preceding 39e13b3 Android workflow succeeded, including ARM64 build/lint.
This increment awaits its own Android CI. The local host has no javac or Android
SDK/device; Java/Android validation runs in CI. Reference-port findings and
playable acceptance criteria are in NATIVE_RUNTIME.md. Next: the original item
block resource/state chain, remaining scene/HUD/particles, then complete
Player/camera/render/audio world frames. Side-by-side screens are preserved;
no ROM, extracted assets or private captures are committed.

---

# Current checkpoint — 2026-11-05, original movable signpost

Continues published main 4479e2330da49db6f12c504f6cc5b555a0376dd6.
Version 0.4.17-signpost, versionCode 21. Not playable.

The complete original movable signpost InitResources and UpdateShadowMatrix
bodies now run. The factory preserves the 0x5a4-byte actor, background model,
moving KCL, active cylinder, cuboid shadow and sphere/line query constructors.
Its original ground ray snaps the home position to the real castle collision;
model/KCL ownership, 0x199 collision scale, shadow matrix, gravity and the two
water-query flags are checked on live actors. No unknown callback succeeds.

Verified: all four private-ROM save/character launches pass ASan/UBSan through
the signpost and stop at daObjMcWater_c_classInit, 0x02111d8c (overlay 9).
The integration keeps the prior Player, camera, scheduler and moving-mesh
assertions. Stage source hashes, hostgen/native-source tests (3 each), packed
compilation and diff checks pass. Local LeakSanitizer is disabled. The broad
host runner's first native ROM check passes, but stops because javac is absent;
Android CI performs the Java and ARM64 checks. Initial main's last four Android
workflows were successful, including 4479e23; this new checkpoint awaits CI.

Next: castle water and its seven waterfall emitters, then remaining original
scene/particle/HUD and per-frame dependencies. There is still no completed
original gameplay frame and no Android device/emulator test. Side-by-side
screens and prior graphics fixes are unchanged. No ROM or extracted data is
committed.

---

# Current checkpoint — 2026-11-05, moving meshes and castle metal net

Continues published main 3953c8d95174f154995f076f45a0b59e5b4303b3.
Version 0.4.16-moving-mesh, versionCode 20. This checkpoint is not playable.

The complete original moving-mesh SetFile/Transform, coordinate conversion,
previous-frame carrying, velocity/yaw and movement-direction filter bodies now
execute over checked 0x1c8-byte records. The Thumb scale-matrix primitive is
adapted natively. dBgActor's original model/collider transform bodies and the
original first-holder KCL loader are connected. Metalnet's complete resource
initializer loads its real BMD, KCL and CLPS and preserves the original save/star
conditions. The original NumStars routine is linked, not a replacement count.
A reviewed wall-sign initializer/factory is compiled; the distinct movable
signpost actor remains the next runtime dependency.

ASan/UBSan checks cover scale/inverse round trips, quarter-turn transforms,
previous-frame carrying, velocity, signed yaw wrap and stationary/moving depth
filtering. All four private-ROM save/character paths initialize actual metal-net
models and meshes and advance to daObjTatefuda_c_classInit at 0x020bc3c8.
The regression inspects live actor records and real KCL/CLPS ownership; preceding
Player, camera, castle and scheduler assertions remain active. Source/hostgen
checks pass. LeakSanitizer is disabled locally. Android ARM64 CI and device
verification are pending for this checkpoint.

Next: finish the movable signpost, then castle water/waterfall initialization
and the remaining scene/particle/HUD and per-frame dependencies. A complete
original gameplay frame and visible movement/collision have not been verified.
ROM, extracted resources and private test output remain untracked.

---

# Current checkpoint — 2026-11-05, castle actor initialization

Continues published main ceed5171a39d45d21ef3bac79793fb6e8a6ee5cd.
Version 0.4.15-castle-actors, versionCode 19. This checkpoint is not playable.

Original complete InitResources bodies now run for the Door, exit trigger,
castle flag, tree, bird, butterfly, ambient sound and cannon actors. The fish
factory and InitResources are connected as well. Factories preserve cartridge
allocation sizes, subobject construction, vtables and the original frame/state
records. Door decoration/key resources and the actual key-model loader retain
their original selection and ownership. Position-only and actor-owned collision
cylinders use their real records. Rotation/translation helpers and frame counts
are available to these complete game bodies; no unknown callback succeeds.

The four private-ROM save/character paths advance through the former overlay-100
Door frontier to daObjMc_Metalnet_c_classInit at 0x02112048 in overlay 9.
The regression inspects live actor states, real model/BCA/shadow resources and
cannon/butterfly cylinder owners, in addition to the preceding castle, Player,
camera, KCL and scheduler checks. The build uses all frontend and stage units;
ASan/UBSan are enabled and local LeakSanitizer remains disabled. This does not
establish a complete gameplay frame or on-phone playability.

Host compilation now supports opt-in content-checked object reuse. An explicit
SM64DS_GAME_BUILD keeps the cache; source/header/compiler-option changes
invalidate it and removed projections delete their stale objects. A real compiler
regression verifies reuse and header/source invalidation. Clean default builds
retain their existing full-compilation path.

Next: original moving-mesh collider transforms and Metalnet/Water actors, then
remaining object, stage fog/skybox/particle/HUD initialization and actual
per-frame Player/camera/rendering dependencies. Playable acceptance requires
visible castle geometry, original movement/jumping/collision over multiple
complete frames, followed by an ARM64 Android build and device verification.
ROM, extracted data and local captures remain untracked.

Reference ports were checked directly: sm64-port's produce_one_frame brackets
one original game-loop iteration with graphics backend calls; its Android base
adapts the platform backend. The upstream DS port's port/README still specifies
a 32-bit-first native ABI, so its host classes cannot simply replace the checked
four-byte-handle Android records. Its recovered actor/collider source is reused
where the original algorithms apply, preserving the DS game and resources.

---

# Current checkpoint — 2026-11-04, entrance, KCL queries and camera initialization

Continues published main ea15b84250029d047f951c61693e20d56b3306a5.
Version 0.4.14-castle-entrance, versionCode 18. This checkpoint is not playable.

The full original Player::St_LevelEnter_Init and SetAnim bodies now execute,
including animation selection, shared-file ownership and the entrance ground
probe. The Player finishes InitResources and the dActor/dBase/fBase post-init
chain, joining the real ordered behavior/render lists. Native dCamera_c factory,
InitResources, state installation, frustum rebuilding, initial positioning and
Render finish; the live view and inverse matrices are updated through the
original CopyToViewMat body. Queries use the original KCL octree traversal,
prism/plane tests and surface filters for both ground rays and line segments.

StageFlow now calls the complete original scheduler order: cleanup, pending
initialization, behavior, rendering and scene-tree reconciliation. Native
adapters preserve priorities, deferred insertion, inherited pause flags and
destruction routing. GameProgram keeps gameplay input active after scene 3
becomes current, retains analog input instead of menu key emulation, and feeds
stage GX/OBJ/capture output to scanout. The complete frame cannot yet execute:
remaining class methods still fail at their actual unresolved calls.

All four private-ROM save/character paths reach daDoor_c_classInit at
0x0214589c (overlay 100), after Player and camera initialization. Assertions
inspect alive states, entrance lock/state, original models, colliders, camera
projection and live view matrix. No-ROM ASan/UBSan checks exercise real ground
and segment traversal against a synthetic KCL prism, surface/owner filtering,
one-sided surfaces and five-phase scheduler order/tree traversal. The segment
distance convention was cross-checked against the private cartridge's ITCM
instruction at 0x01ffb6a4; guest instructions are never executed.

A clean host rebuild of all 618 frontend and 162 stage translation units plus
the native runtime passes these private-ROM checks under ASan/UBSan. Relocated
video/math regressions and source/host-generation audits pass. Regeneration is
identical with PYTHONHASHSEED 9 and 900. LeakSanitizer is disabled because this
environment cannot inspect processes. The preceding ea15b84 Android build/lint
workflow passed; this checkpoint still needs its own ARM64 CI/device run.

Next: connect the complete overlay-100 Door actor, then continue actual object,
HUD, stage fog/skybox/particles and per-frame Player/camera dependencies. No
dummy factory, skipped object category or successful unknown callback is used.
Playable acceptance still requires visible castle geometry and Player movement,
jumping/collision and multiple complete frames, followed by Android/device
verification. ROM, decoded assets and private captures remain untracked.

The N64 reference was inspected for architecture:
[sm64-port pc_main.c](https://github.com/sm64-port/sm64-port/blob/master/src/pc/pc_main.c)
runs original game logic once per frame between graphics backend calls, with
separate audio/backend ownership;
[Android SDL/GLES base](https://github.com/VDavid003/sm64-port-android-base)
adapts the platform layer. The DS implementation follows that frame ownership
principle while retaining its own recovered actor scheduler, KCL format and ABI.

---

# Previous checkpoint — 2026-11-04, Player resources and entrance-state handoff

Published castle-assets checkpoint: bedfa240db2d63d1a29c83399f400a42810cbf63.
Published Player-resources checkpoint: ea15b84250029d047f951c61693e20d56b3306a5.
The goal remains playable castle grounds; this checkpoint is not playable.

The complete original entrance/object dispatch now reaches dActor_c::Spawn,
the cartridge's Player factory and the actual 0x768-byte Player constructor.
Native constructors retain the original actor/list records, five body-model
slots, eight head-model slots, animation arrays, shadow and collision members.
Player factories publish native bindings only after overlay 2 loads.

The complete original Player::InitResources body is projected with checked
field lanes. It loads real body/head BMDs, BCA/BTP animations, shadow components
and the original 50-element trail array. The empty Vector3 constructor's
under-declared prototype now forwards its real object argument through the
native array callback. Actor cylinders use the original Q12 yaw/translation
routine; mesh query configuration retains its original member offsets.
The recovered Player::ChangeState semantics execute cleanup-veto and init
member pointers using the existing checked ARM member-pointer dispatcher,
including the control lock and held-object redirect. No unknown callback is
replaced by a successful return.

All four private-ROM save/character launches pass ASan/UBSan to the exact next
missing dependency, Player::St_LevelEnter_Init at 0x020c7838. Checks inspect the
Player's factory/vtable, entrance position/character, all five body and eight
head models, shadow, trail cookies, held-object queue and both collider records.
No-ROM checks also cover cleanup veto, state reset, control lock, carried-object
redirect and array-construction rollback. The relocated video/math regressions
pass, including the original projection and shared arithmetic state. Host source
and generation audits pass. Local LeakSanitizer remains disabled because process
inspection is unavailable. Android APK/ARM64 CI and phone verification have not
been run for this checkpoint.

Continue with the complete St_LevelEnter_Init body and its actual callees, then
finish the remaining InitResources/entrance camera/HUD/object dependencies.
Before declaring gameplay ready, update the original full actor process lists
and verify GameProgram's gameplay input and 3D scanout: those title-only/requested-
scene guards and StageFlow's scene-only frame loop are still unresolved.
Version 0.4.13-player-resources, versionCode 17. Matching src/include and the
pinned PC dependency are unchanged. ROM, decoded assets and private test outputs
are not tracked.

---

# Current checkpoint — 2026-11-04, castle model and collision mesh

Continues main dc15df22496e65c278157be0b10f8a2eb3cc1bb1. The acceptance
condition remains playable castle grounds. This checkpoint does not meet it.

The original array-new dependency at 0x02073470 is connected with genuine
cookies, forward constructors and reverse partial cleanup. All seven wipe
faders construct their original 0x60-byte records and load actual BMD models.
Complete original simple-model LoadAndSetFile/DoSetFile/func_02016ff4 and
stage LoadModel bodies execute through the existing native model primitives.
The common-model pool now contains 20 real files (12 shared, 7 wipes, castle).

All seventeen original ov002 object/category dispatch bodies are projected,
including LoadClsnAndObjects. They bind only after overlay 2 loads. The KCL
file is read, its four pointer lanes rebased, and the real collider initialized
and registered in the 24-slot mesh table. Path and camera/minimap table setup
use their original storage. Multiple projections of one source TU have explicit
unique unit names; duplicate or malformed generated names fail closed.

ASan/UBSan private-ROM checks pass for Mario, Luigi, Wario and blank-file Yoshi
through this frontier. Checks validate all 7 fader cookies/embedded models,
the castle model, and the entire collision payload against the independently
loaded StageAssets data. No-ROM checks cover real constructor dispatch and
partial-array rollback in reverse order, with no leaked owned allocation.
Local LeakSanitizer is disabled because process inspection is unavailable.

Next actual missing dependency: dActor_c::Spawn at 0x02010e2c, reached from
LoadEntranceObjects. Player/actor factories, initialization, frame scheduling,
camera, input and stage rendering must still execute before claiming gameplay.
Android CI and phone verification are pending for this checkpoint. Version
0.4.12-stage-assets, versionCode 16. Matching src/include and the pinned PC
dependency are unchanged. No ROM, extracted data or private captures are tracked.

---

# Current checkpoint — 2026-10-30, shared arithmetic-unit state

The phone report sm64ds-crash-report(2).txt ends in a native fixed-point
zero-divisor exception. Native cstd helpers now submit the original SDK
operands/modes to the shared arithmetic units, preserving state for recovered
asynchronous callers such as G3i::PerspectiveW_. Division by zero returns the
DS arithmetic result; split high-word operand writes restart the units and
64-bit signed overflow is handled without host undefined behavior.

The relocated-register test and the new recovered-projection arithmetic test
pass under ASan/UBSan (LeakSanitizer disabled for this executor). The latter
executes the real projected perspective routine after a different division
mode, and checks synchronous/async state, zero division, split writes, integer
rounding and overflow. This verifies the reported arithmetic failure, not a
claim of playable gameplay. Android CI and phone verification are pending.
The accepted side-by-side layout and prior texture fixes remain intact.
Version 0.4.11-native-math, versionCode 15. Stage integration continues from
the original array-new dependency at 0x02073470; ROM/assets remain private.

---

# Current checkpoint — 2026-10-30, menu palette command routing

Published code: 64c2987cbcf1b51a84974771fae94d102d00e3bc (main), identical
source tree to the locally tested f9c4c070ab5aea68931660d6430e2e5999ce9a63.
Android run: https://github.com/RonyxDumb/sm64ds-android/actions/runs/41101493606
Debug APK artifact sm64ds-android-debug, ID 12999826613, 672493-byte ZIP:
https://github.com/RonyxDumb/sm64ds-android/actions/runs/41101493606/artifacts/12999826613
The artifact is present for this exact code commit. At this handoff the overall
run and both job conclusions are still pending/in_progress; no complete Android
CI pass is claimed. A fresh run-status check is needed before recording success.

Parent main: 4610f2cf5f705e84c872fb01292379fe5ae70f5e. The phone now shows
smooth star reflections but a brief oversized star and corrupted title buttons.
The button defect was reproduced with the private EUR cartridge. The footer
atlas indices and its palette in physical VRAM F exactly matched their source
records, while GX decoded the atlas using F+0 instead of its palette at F+0x390.

The recovered func_ov007_020c1404 keeps PLTT_BASE in a nonvolatile local pointer.
Its packed Ptr updated the I/O latch without executing the geometry command,
so later textures inherited the previous palette. A checked Android execution
view marks this one hardware pointer volatile, using the existing VolatileRef
command dispatch. The matching src/include trees and submodule remain unchanged.
The reviewed projection and committed frontend manifest are updated together.

The relocated-register regression now executes the real projected palette
binder and checks every rendered pixel, rather than just reading the register
latch. It fails before the repair and passes after it, covering 16-byte palette
units, format 2's 8-byte units and the null-material reset. BMD, PPU capture,
W/Z depth, all texture-coordinate modes, hostgen/source audits and all four
private-ROM save/character launch paths pass with ASan/UBSan (detect_leaks=0).
An independent decode of the original menu atlas matches all 16,384 renderer
pixels exactly after this change. The full local host script stops at javac,
which is absent from this executor; Android CI must provide the Java/APK checks.

The transient oversized star has NOT been reproduced or declared fixed.
Three 660-callback host probes used single ticks, 30 Hz catch-up and 60 Hz with
periodic 250 ms stalls. The largest observed star width was 46.174 DS pixels.
Android now records GX-REFLECTION in the existing exportable diagnostic log:
three normal reference frames, then oversized/invalid reflected-model geometry,
up to sixteen records per session. Each includes tick, catch-up/capture context,
projected bounds, W/Z ranges and display/capture registers. No clamp or hidden
frame suppresses the reported symptom. A phone retest and that log are needed
if the enlargement persists.

Version 0.4.10-menu-palettes, versionCode 14. The APK is available as above;
complete CI status and phone verification remain pending. The stage loader
still stops at the previously documented
func_02073470 dependency; playable gameplay is not established. ROM, decoded
assets, uploaded screenshots and private captures are not committed.

---

# Current checkpoint — 2026-10-30, normal/vertex texture-coordinate units

Both graphics repairs are published on main after explicit user approval:
1fe8b86070618bc201a77cb9b61858c30a8477a1 (W depth) and
5a650e068bb7a2874dd52a618d703d5d7fbc3fca (generated texture-coordinate units).
The GitHub connection published the changes because local Git had no write
credentials. Both source trees were checked against the locally tested trees
and are identical; the original local IDs were 4667bca and 1c97e5e.
Android Actions run: https://github.com/RonyxDumb/sm64ds-android/actions/runs/41085791071
Both jobs passed: native ARM64 frontend execution and the complete build job,
including host regressions, game dependency checks, frontend execution,
headless GLES3, debug/release APK compilation and Android lint.
Download the debug APK from artifact sm64ds-android-debug (ID 12992532010):
https://github.com/RonyxDumb/sm64ds-android/actions/runs/41085791071/artifacts/12992532010

After fixing depth, the opening star retained a repeated gold texture. Its
decoded renderer texture was compared byte-for-byte with an independent BMD
load of the private cartridge's face_demo_mariostar.bmd: it was identical.
Tracing the real normal-source texture matrix found a scale of 256 for the
32px reflection. NORMAL/VTX matrix products use 1/16-texel units, while the
native backend stores coordinates in texels. That conversion was missing,
making the reflection repeat 16 times across the star. The backend now
converts the matrix contribution before adding the raw TEXCOORD offset.
TEXCOORD-source matrix and constant-lane units retain their original behavior.

The new no-ROM test covers all four coordinate sources, positive/negative
normal and vertex products, the two TEXCOORD constant lanes, negative
sub-texel rounding and NORMAL latching. It fails against the parent renderer
and passes with this repair under ASan/UBSan. The W/Z regression passes too.
The four existing private-ROM save/character launch paths pass after both
repairs. Private host captures show the now smooth gold star and removal of
the reproduced depth noise; the File Select/menu paths were also exercised.

Remaining: compare the complete title transitions and file-menu backgrounds
on the phone, and continue the previously recorded stage loader dependency
at func_02073470. These changes do not establish playable gameplay. Android CI
passed as recorded above; on-phone visual verification is still pending.
Version 0.4.9-texture-units, versionCode 13. Original matching sources and the
pinned submodule are unchanged. ROM, decoded assets and test captures remain
private; local sanitizer checks disable LeakSanitizer as before.

---

# Current checkpoint — 2026-10-30, title W-buffer depth and menu ordering

The depth speckles in the new phone screenshots were reproduced using the
supplied private EUR cartridge. Isolating BG0 and geometry located the background
speckles in GX, before 2D composition. The original title initialization
func_ov007_020b6c54 writes SWAP_BUFFERS=2, requesting W depth for subsequent
geometry. The renderer previously ignored that command and always tested Z.
Recorded backdrop and wallpaper vertices share Z=0.499799787998 while their
W values are 58.605594635 and 58.5327148438. Comparing the collapsed Z values
caused unstable visibility across flat panels and incorrect menu overlap.

The depth selection now travels with each assembled triangle, persists across
host frames, and uses perspective-correct W for W mode. Explicit Z selection
still uses Z. The no-ROM regression checks every pixel of overlapping panels
in both submission orders, switching back to Z, frame persistence and both
FIFO/direct-port swap latching. It fails against the previous renderer and
passes with the repair under ASan/UBSan. Relocated-register and PPU capture
regressions pass. All four existing private-ROM save/character paths pass.

Host captures confirm removal of the reproduced depth speckles. Further
texture distortion is still visible on the star and some menu backgrounds;
this checkpoint does not claim those issues or gameplay are complete. Phone
verification and Android CI are pending. The previous stage integration stop
at func_02073470 remains. Version 0.4.8-w-depth, versionCode 12. Local checks
disable LeakSanitizer as in preceding checkpoints. No private ROM, decoded
assets, screenshots or temporary captures are committed.

---

# Current checkpoint — 2026-10-30, twelve common models load natively

Published parent: 0bef66b916527aa969d655a5da0e1207c38ae701, stage audio/2D UI.
The acceptance target is still playable castle grounds, not merely a loaded
scene. No gameplay success is claimed at this checkpoint.

The original SharedFilePtr LoadFile/Release/refcount-limit, model LoadFile,
AddToCommonModelDataArr, ReallocateModelFile and func_02017060 bodies now run.
Native card providers read actual FAT files or registered NARC archives,
perform genuine LZ77 decoding, cache owned allocations and free them on the
last release. Checked native heap size/reallocate services preserve DS handles
while shrinking the real allocation. No prefilled model records or dummy
success functions replace these game bodies.

No-ROM tests exercise actual shared-file reads, cache reuse, reference counts,
255-holder refusal, last-reference cleanup and allocation ownership. All four
private-ROM save/character paths pass strict ASan/UBSan checks through loading
and rebasing all twelve common BMD models. The common-model pool and each
SharedFilePtr are checked, and a fresh packed NARC resource read is compared
byte-for-byte against the independently decoded Cartridge::file result.
The previous overlay, sound-group, controller-text and glyph-shift tests pass.
Local LeakSanitizer remains disabled for process-inspection restrictions.

Actual next dependency: func_02073470 at 0x02073470, the original array-new
helper for seven 0x60-byte wipe faders (8-byte cookie). Continue its genuine
construct/cleanup path, dFdWipe_c/model file setup, stage LoadModel, collision
and object spawning, then real actors/Player and full frame processing.
Known input/process-list/GX presentation integration work is recorded in the
previous checkpoint and must be exercised before declaring gameplay ready.
Android CI is pending for this checkpoint. Version 0.4.7-shared-models,
versionCode 11. Matching sources and the pinned dependency are unchanged.
ROM, extracted assets, build caches and local test logs remain private.

---

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
