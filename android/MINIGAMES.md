# Original minigame room

Version 0.4.23 links the original menu and shared controller. The latest
checkpoint also mounts overlay 6 and attempts its original scene factory, but
does **not** yet implement the constructors needed to finish a game launch.

| Path | Native support | Evidence |
| --- | --- | --- |
| Title result 7 -> scene 5 | Connected | Private-ROM test dismisses the intro and touches STANZA DEI GIOCHI in the actual frontend; result 7 is produced by original code. |
| Menu resources/lifecycle | Connected | Entire recovered InitResources, Behavior, Render and CleanupResources bodies execute with checked four-byte pointer handles. |
| Backgrounds, sprites, Italian captions | Connected | Private-ROM main/sub scanouts inspected; no recreated art. Original OAM size tables, reset, upload and bounds also tested without ROM. |
| Four tabs, arrow delays, two pages, exit | Connected | Original touch conversion, hit tests, timers and scrolling exercised on the actual EU data. |
| Locked games, NEW badges and records | Connected | Original helpers and live save block; exact record-slot permutation, default repair, EEPROM checksums, mirror recovery and reopen tests. |
| Shared controller and original records | Connected as common dependencies | 52 original state/helper bodies, all 20 enter states and 49 original EU member pointers; 23 original score/language/record helpers. UI/audio dependencies are observed by test spies in controller tests, not implemented by success stubs in production. |
| Gameplay overlay 6 | Bootstrap connected | Overlay-5 menu resources are released; a separate address space mounts original overlay 6, preserves input/save aliases and looks up the real scene factory. Missing constructor bindings fail at the original address. |
| Gameplay scanline wipe | Connected | Original dWipe ramp/circle table builders and HBlank/VBlank handlers feed WIN0/WIN1 masks on both native PPUs. Lifecycle, pending type changes, masks, effects and teardown are tested. |
| First complete minigame/round | **Missing** | The full dScMgBase resource lifecycle, shared banners/options, and overlay-6 scene/component actor callbacks remain unlinked. |
| Android phone play test | **Not performed** | Host execution and CI compilation do not establish on-device behavior. |

## Implementation

`lift_minigames.py` generates a hash-guarded manifest from unchanged
`src/minigames/d_s_mini_gm.cpp` and `src/engine/oam/OAM.cpp`. Only the native ABI
seams, explicit `self` argument, safe byte-table reads and unsupported-route
gate differ. There is no independent menu drawing or synthetic icon table.

`MinigameRuntime` owns the scene's exact 0xb0-byte record and private ARM9/overlay
data. Save and touchscreen spans map directly to the live native records.
The existing DS PPU draws both original screens; the Android composition stays
side by side. File Select reinitializes its VRAM when the room exits.

The source's OAM reset relies on forward, overlapping CPU copies. A memmove
replacement does not propagate the hidden-entry pattern, so this path retains
the original sequential word behavior. Width/height queries read bytes directly,
avoiding an unaligned intermediate int reference on native platforms.

The earlier catalog parser incorrectly combined parameter bits 8..23. Real EU
rows use bits 8..15 for the saved-record slot; bits 16..23 hold another value.
The menu order is a permutation of record slots. Both parser and initialization
are corrected and verified against the cartridge.

`lift_minigame_state.py` preserves the 0x28-byte controller, including its
two-word member pointers; checked dispatch resolves native code and scene
virtual slots 18/19/20 without interpreting a native vptr as cartridge data.
The native room maps overlay 4 and checks all 49 PMF records before use.
State IDs outside 0..19 are rejected before indexing the original enter table.

`lift_minigame_records.py` compiles the original score caps, high/low score
comparison, session best, rank insertion, record pair and slot selection bodies.
The original distinction between the score used to choose a rank and the last
submitted score written to that rank is retained. Tests submit scores through
these actual bodies and persist/reopen the original EEPROM; they do not claim
that a gameplay round earned those scores.

`dWipe_c` is native-owned and uses the recovered typed methods. Only IRQ,
cache, MMIO and palette boundaries are adapted. The host executes the original
scanline handlers into a per-frame window snapshot shared by BG and OBJ.
WIN0/WIN1 priority, wrapped bounds, layer masks and effect enables are supported;
OBJ-window sprites and mosaic still are not. Original src/ and include/ files
are unchanged.

## Verification

```sh
bash android/tests/run-game-tests.sh
bash android/tests/run-game-tests.sh /private/path/to/your-European-ROM.nds
bash android/tests/run-host-tests.sh
```

The private-ROM test drives original character tabs, page arrows and exit with
touch coordinates, verifies visible OBJ pixels, verifies overlay-6 residency,
returns to the original title, and then runs the existing castle/Player regression.
It does **not** play a round or test real Android touch/audio hardware.
Set `SM64DS_MENU_CAPTURE=/tmp/menu` to inspect the two private BMP scanouts.
Never commit cartridge bytes, extracted resources, scanouts or save images.

Controller tests also run against actual overlay-4 PMFs and overlay-6 text
tables. Overlay 6 replaces overlay 5 at 0x020bfec0; mapping the menu data in
place of the game data cannot satisfy the controller's original text pointers.
No production game capability is enabled until its entire lifecycle is linked.

## Next executable dependency chain

1. Finish dScMgBase_c resource/scene lifecycle, shared graphics/banner/options
   dependencies and original overlay-6 factory binding. The shared controller,
   records and scanline wipe already execute.
2. Complete original scene construction/scheduling and keep the selected
   menu row, parameters, live records and return tab/page across transitions.
3. Link one complete overlay-6 scene, its real components, gameplay,
   HUD/banners and cleanup without replacing missing callbacks with stubs.
4. Verify a full round, genuine score submission, retry, return to scene 5 and
   reopened EEPROM record; then repeat for the remaining scenes and variants.

The native capability list and forced unsupported-tap guard have been removed.
Unlocked icons now request their original scene IDs. Missing native actor
constructors still fail explicitly; no successful round or earned score is faked.
