# PC runtime integration

The playable upstream dependency is pinned at `port/release-0.4.0`, commit
`6230b2996636e3fd0e03cb178161c94cb4b73cc9`, in `external/sm64ds-pc`.
Initialize it with `git submodule update --init --recursive`. The MIT source and
attribution remain intact; ROM data and executables are not packaged.

## Reused on Android

- The pinned `port/hal/sdat/sseq.cpp` and `mixer.cpp` compile unchanged. The native
  archive adapter keeps SBNK/SWAR pointers outside the cartridge's 32-bit records.
- A producer thread feeds a bounded PCM ring. AAudio only reads/resamples PCM;
  the callback performs no sequencer work, file reads or allocation.
- The recovered boot's `Sound::Play2D(4, 0)` request now reaches that sequencer.
  The old manual X/Y music and A/B sample controls have been removed from the
  production session. This does not establish complete game-wide audio.

## Native boot

The previous OBJECT-only boot gate has been replaced with linked source methods.
`android/tools/native_sources.py` selects the recovered boot methods, faders,
scene-request functions and font/message dependencies. Only audited native-pointer
and hardware seams change; the matching source tree is untouched.

`GameProgram` owns the boot leaf's scalar state separately from the fixed-layout
`fBase_c` actor manager. It does not instantiate the original boot class/factory
or run the original top-level game entry. This closes execution of boot logic,
not the complete scene/actor lifecycle. A native tick queue performs real font
initialization before reporting worker completion. The resource catalog and
sequencer are already initialized by the session. It is a native replacement for
the platform-dependent card/font worker, not execution of the DS worker body.

The PC's `hal/scene_link100_boot.cpp` explicitly refuses that DS worker and the
playable PC default enters the title separately. Copying that refusal into Android
would not close the original boot dependency.

## Remaining frontier

The recovered boot requests scene 1 (`dScDSMT_c`, overlay 7). Its frontend's work
records mix native pointers with fixed DS offsets, including `Ov007Work` in
`func_ov007_020aed98.cpp`. All consumers must share an audited native layout;
suppressing its offset assertion or narrowing pointers cannot make it safe.

Start with `d_s_dsmt.cpp`, `func_ov007_020b7138.c` (frontend setup),
`func_ov007_020b7090.c` (input/update), and `func_ov007_020aed98.cpp` (menu dispatch).
Preserve actual initialization, callbacks, OAM/model submissions and the original
save-slot-to-`StartFile` handoff. Then connect scene 3's stage/Player lifecycle.
No missing dependency should be replaced with a successful dummy return.
