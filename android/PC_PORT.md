# PC runtime integration

The playable PC port is on upstream `port/release-0.4.0`, pinned here as the
`external/sm64ds-pc` submodule at `6230b2996636e3fd0e03cb178161c94cb4b73cc9`.
The upstream main branch's `port/` smoke suite is not that playable build.
The previous Android checkpoints used the main-branch suite as their reference;
that was an incomplete reference for reproducing PC startup.

Initialize the dependency before building:

```sh
git submodule update --init --recursive
```

The pinned source and MIT attribution remain intact in the submodule. No ROM,
extracted resources or prebuilt PC executables are included or packaged.

## Integrated in Android

- The release's actual `port/hal/sdat/sseq.cpp` and `mixer.cpp` compile unchanged
  in the Android library: sequence events, multiple tracks/players, instrument
  envelopes, priorities, looping and the 192 Hz sequencer clock.
- `pc_audio/archive.cpp` supplies a checked, pointer-width-independent SDAT,
  INFO/FAT, SBNK and SWAR adapter using the privately imported ROM. Instrument
  wave links are held in host side tables; no 64-bit pointers are stored in
  cartridge records. All waves are decoded before playback.
- `pc_audio/stream.cpp` drives the PC engine on a producer thread. A bounded
  stereo PCM ring connects its 32768 Hz output to AAudio's actual device rate.
  The audio callback only reads/resamples PCM; it performs no sequencer work,
  file reads, allocation, mutex locking or condition-variable notification.
- Existing raw sample diagnostics remain available. X starts BGM sequence 58;
  Y stops sequences. These bindings are explicit backend tests, not game events.

The adapter is separate from the PC ARM9 command consumer. The recovered game's
Sound front doors and ARM9/ARM7 command queue are not yet connected to Android.
The engine also inherits the PC mixer's approximated volume curve and any
upstream unimplemented sequence opcodes; this is not a claim of hardware-exact
sound for every event.

## Original boot compilation

The original `d_s_boot_methods.cpp` now passes the ARM64 compile gate generated
through `port/tools/hostgen.py`. The only additional pointer transformation
removes the exact `LADR(p)` intermediate cast to `unsigned int`; actual ROM
32-bit offsets are left intact. The matching source itself is unchanged.

The gate is an OBJECT target dependent on the normal Android build. It is NOT
linked into the APK and is NOT called. The compiled object has 58 external
symbol references (functions and data, including some already present in the
Android backend). That number is an object-file inventory, not a count of
unimplemented functions or a linked-game completion percentage.

Compiling is insufficient because layout assumptions differ. The ARM64 NDK
measures `sizeof(dScBoot_c)==0x98`, versus the recovered DS `0x58`; a native
pointer is 8 bytes and a pointer-to-member function is 16 bytes. The PC build's
explicit MSVC x86 layout is different again. Bypassing an ABI assertion or
silencing narrowing warnings does not make these objects interchangeable.

Next: port the recovered scene/heap ownership and bridge actual boot link
dependencies with typed host state. Then connect level mounting, actor registry,
input, GPU submission/2D scanout, saves and the sound command consumer from the
pinned playable source. Preserve the source/host separation and do not replace
missing dependencies with success-returning stubs.
