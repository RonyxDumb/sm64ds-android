# CLAUDE.md — Super Mario 64 DS native Android port

Repository: https://github.com/RonyxDumb/sm64ds-android
Primary branch: `main`

## Mission: port the ENTIRE original game

Continue implementing **all of Super Mario 64 DS natively for Android ARM64** using the recovered original game code. This is a native port, NOT an emulator, a visual mock-up, or a minigame-only project.

The end goal is a stable, performant, fully playable game from boot to completion, with the original systems and content operating correctly on Android. **The minigame room is one part of this mission, not the overall priority or end goal.**

Preserve original game behavior wherever practical. Never claim functionality simply because a source unit compiles, a scene loads, or an APK is generated.

## Full-game coverage

Track, implement, connect, and validate ALL relevant subsystems:

- **Boot and front end:** title, menus, file select, character selection, transitions, settings and original game flow.
- **Progress and saves:** four save slots where applicable, save/load and persistence, stars, keys, unlockables, records, checkpoints, progression flags and ending.
- **Castle and hub:** playable exterior and interior, doors, warps, interactive objects, NPCs, scene transitions and correct return paths.
- **Playable characters:** Mario, Luigi, Wario and Yoshi; original movement, jump and attack states, abilities/power-ups, transformations, animation and physics.
- **Courses and missions:** all original courses, secret areas, star missions, level scripts, objectives, collectibles and course completion.
- **Actors and gameplay:** enemies, bosses, platforms, moving meshes, triggers, spawners, interactions, camera, collision, hazards, particles and gameplay events.
- **Graphics:** original 3D rendering, geometry, textures, materials, animations, lighting, shadows, 2D sprites, HUD, font, menus, effects and Nintendo DS screen layers.
- **Audio:** original music, sequencer behavior, sound effects, voice where applicable, transitions and timing.
- **Input / Android UI:** original buttons and touch-screen semantics; reliable touchscreen controls and hardware-controller mapping; suitable dual-screen/fullscreen layout; persistent user-facing control visibility setting.
- **Minigames:** the original minigame room, all selectable minigame scenes, full input/gameplay, menus, round transitions, retry/quit and saved records.
- **Other original modes:** audit and eventually integrate remaining supported original game modes, including multiplayer if technically feasible in a native Android architecture; do not silently omit features.
- **Android platform:** ARM64 lifecycle, memory, asset/ROM loading, OpenGL ES compatibility, performance, stability, save storage, audio backend, screen resizing and APK packaging.

A complete game requires **verified, continuous gameplay**, not merely a high percentage of compiled source files.

## Start of EVERY session

1. Check `git status`, `git branch --show-current`, local/remote HEAD and pending edits. Never delete or overwrite uncommitted work.
2. Read this file, `README.md`, `android/README.md`, `android/CHECKPOINT.md`, relevant test/build documentation and recent commits on `main`.
3. Inspect relevant GitHub Actions failures and verify the latest actual source and runtime frontier. Treat old summaries as historical, not as current truth.
4. Identify the **highest-impact missing integration** blocking playable end-to-end game progress, or the user's latest explicit priority. Start implementing it.
5. Work autonomously on real code. Do not stop after producing only a plan, inventory or speculative explanation when implementation is possible.
6. Consult existing recovered classes, symbol maps, overlays, tests and proven patterns before creating shims or new abstractions.

## Development order (dynamic, not an excuse to ignore working areas)

1. **Foundation:** correct original-code linkage, scene construction, overlay ownership, original ABI/layouts, DS-memory translation, event loop, scheduler, resource loading and platform interfaces.
2. **First end-to-end gameplay loop:** title -> file select -> playable castle -> enter course -> move/interact -> earn a real star -> return to castle -> save -> reload.
3. **Breadth across the whole game:** complete player actions, course-specific actors, camera/collision, menus, audio, effects, enemies, bosses, unlockables and every star mission.
4. **Additional modes and minigames:** preserve already implemented minigame progress; finish the original room, Wanted, Amida and remaining games without fabricated behavior.
5. **Verification and polish:** remaining feature coverage, compatibility, touch controls, performance, regression fixes and actual Android-device validation.

This order is a planning default. If a shared engine dependency unlocks many systems or the user requests another area, adjust based on evidence. **Do not spend every session exclusively on minigames.** Prefer changes that unlock multiple actual game systems.

## Original-code fidelity / correctness

- Prefer recovered original source from `src/` and declarations in `include/`. Use an audited native adaptation/linkage layer when required; avoid changing original source only to hide porting errors.
- Respect Nintendo DS ARM9 and overlay address ownership, original data layouts, calling conventions, constructors/destructors, vtables, pointer-to-member functions, global initialization, scheduler phases and scene transitions.
- Preserve original mechanics, timing and save semantics. Do not hard-code fake victories, star awards, scores, actors, backgrounds or placeholders presented as complete game content.
- Do **not** introduce artificial `supports_scene` allowlists or forced feature blocks. Let original game paths execute as far as implemented; maintain memory-safety/bounds checks, trustworthy diagnostics and explicit failure for unresolved native calls. Do not disguise missing code as success.
- When a missing dependency is discovered, implement or correctly connect its original behavior, then add a targeted regression test. Avoid unbounded generic fallbacks and broad stubs.
- Keep proprietary Nintendo ROMs, extracted artwork/audio, private game dumps, personal save files, signing keys and secrets **out of Git**. Users provide their own lawfully obtained game data.
- Do not invent test results or claim that host execution proves Android hardware playability.

## Key paths (verify against current tree)

- `src/`, `include/` — recovered game systems and original declarations.
- `src/minigames/` — one subsystem, not the project's entire scope.
- `android/app/src/main/cpp/runtime/game_program.cpp` — Android game program/scene integration.
- `android/app/src/main/cpp/` — native runtime, renderer, input/audio and platform integration.
- `android/` — Android application, build, tests and APK packaging.
- `port/ntr/` — Nintendo DS compatibility, memory, graphics/resource handling.
- `port/tools/hostgen.py` — audited source transformations/adaptation.
- `config/arm9/` — original symbol maps.
- `android/CHECKPOINT.md` — persistent, factual development handoff and progress log.
- `.github/workflows/` — CI, Android builds and diagnostics.

## Speed + validation

- Favor **parallel independent integration work only when safe**, and avoid unnecessary full rebuilds.
- For each change: inspect affected dependency graph -> implement original behavior -> run focused compilation/link/test -> fix regressions -> commit.
- Run targeted host tests, ASan/UBSan checks, ABI/layout tests, scene regressions and ROM-backed tests when compatible private assets are locally available.
- Run full Android builds, lint and CI at integration milestones and when needed to diagnose actual Android failures. Don't repeatedly rebuild unchanged components without purpose.
- Before calling anything playable, separate these evidence levels:
  1. Recovered source compiles.
  2. Native code links into runtime.
  3. Host regression passes.
  4. Private ROM-backed host execution reproduces genuine game behavior.
  5. APK builds successfully.
  6. The behavior is tested and works on an Android device.
- A feature is **not complete** merely because levels 1-5 pass. Clearly state what is and is not verified.
- Prefer improving measured functionality and removing actual blockers over inflating source-file counts.

## GitHub workflow — mandatory

- Work **only on `main`**. Do not create other branches, worktrees with new branches, or Pull Requests.
- Make small, logically coherent checkpoint commits directly on `main`; push when permitted.
- Check upstream state before pushing. Handle non-fast-forward updates safely; never force-push or rewrite history without explicit authorization.
- Do not overwrite someone else's concurrent modifications. Stop and report if a genuine conflict cannot be safely resolved.
- Include code, regression tests and checkpoint notes together when useful.

## Persistent memory and handoff

Update `android/CHECKPOINT.md` at every substantial milestone or session end:
- Date and commit SHA (or "pending" before commit).
- **Area of the full game** changed (castle, player, courses, engine, graphics, audio, minigames, controls, etc.).
- Actual original code linked/implemented; file names; first remaining missing symbol or runtime dependency.
- Tests performed and precise results; distinguish host/private-ROM/APK/device evidence.
- Regressions, failures, unverified behavior and actual blockers.
- Next concrete integration step prioritized for **overall game completion**.

Maintain a factual feature-coverage checklist if useful; do not mark a feature complete until the specified behavior is demonstrated. Re-read the updated checkpoint at the start of the next session.

## Historical snapshot — not a live completion claim

As of a 2026-11-06 checkpoint, the project reported 2,407/2,407 adapted complete source views compiling, while original raw-source units and their native integration still had important gaps. The private-EU-ROM host runtime had exercised genuine Wanted gameplay up to a correct stylus hit, score 1 and next board, but background rendering and broader game flow still needed verification. Castle actor/camera integration and actual on-device gameplay remained unverified in the documented status. **Recheck the current repository and CI; do not treat these statements as current or as proof of full playability.**

## Communication

- Speak to the user concisely in Italian.
- Report: files/features implemented, tests (with evidence), actual GitHub commit SHA, outstanding blocker and next step.
- Clearly distinguish verified facts, probable causes and hypotheses.
- Optimize for fastest **real progress toward the entire playable game**, not for demo output, code volume, or vague completion claims.
