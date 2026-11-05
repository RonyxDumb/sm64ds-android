# Original minigame room

Version 0.4.22 links **the original menu**, not minigame gameplay.

| Path | Native support | Evidence |
| --- | --- | --- |
| Title result 7 -> scene 5 | Connected | Test supplies result 7 at the title/runtime boundary. The initial title icon press is not tested. |
| Menu resources/lifecycle | Connected | Entire recovered InitResources, Behavior, Render and CleanupResources bodies execute with checked four-byte pointer handles. |
| Backgrounds, sprites, Italian captions | Connected | Private-ROM main/sub scanouts inspected; no recreated art. Original OAM size tables, reset, upload and bounds also tested without ROM. |
| Four tabs, arrow delays, two pages, exit | Connected | Original touch conversion, hit tests, timers and scrolling exercised on the actual EU data. |
| Locked games, NEW badges and records | Connected | Original helpers and live save block; exact record-slot permutation, default repair, EEPROM checksums, mirror recovery and reopen tests. |
| First complete minigame/round | **Missing** | Overlay-4 base/controller and overlay-6 game lifecycle/actor callbacks are not executable yet. |
| Gameplay capture wipe | **Missing** | dWipe capture/scanline callbacks are explicitly rejected; unsupported game taps are gated before any wipe or scene mutation. |
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

## Verification

```sh
bash android/tests/run-game-tests.sh
bash android/tests/run-game-tests.sh /private/path/to/your-European-ROM.nds
bash android/tests/run-host-tests.sh
```

The private-ROM test drives original character tabs, page arrows and exit with
touch coordinates, verifies visible OBJ pixels, rejects unsupported game taps,
returns to the original title, and then runs the existing castle/Player regression.
It does **not** play a round or test real Android touch/audio hardware.
Set `SM64DS_MENU_CAPTURE=/tmp/menu` to inspect the two private BMP scanouts.
Never commit cartridge bytes, extracted resources, scanouts or save images.

## Next executable dependency chain

1. Port dWipe's real capture/scanline lifecycle; preserve the menu's fade gates.
2. Map overlay 4 and its dScMgBase_c, dMgState_c and original callback tables.
3. Link one complete overlay-6 scene, its real component constructors, resources,
   gameplay, HUD/banners and cleanup. Add it to the native scene capability list
   only after it can initialize and execute.
4. Verify a full round, genuine score submission, retry, return to scene 5 and
   reopened EEPROM record; then repeat for the remaining scenes and variants.

At present the capability list contains no minigame gameplay scene. A tap cannot
produce a false successful launch, fabricate a win/score or freeze the live menu.
