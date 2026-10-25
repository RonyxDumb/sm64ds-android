# port/ntr -- the host DS platform layer

Imported 2026-08-02 from the port research repo (MIT, project-internal;
twelve commits, each landing one seam with a visual harness). Register and
hardware semantics are from GBATEK only: no SDK source, no emulator code.

What each piece is:

| File | Layer |
|---|---|
| `io.cpp` / `mmio.h` | real memory mapped at the DS addresses (main RAM, I/O, palettes, VRAM, OAM) in the 32-bit process, plus side-effect dispatch for write-triggered registers (CP divider/sqrt, GXFIFO) |
| `gx.cpp` | the geometry engine: command decode from FIFO/port writes, matrix stacks, vertex assembly, lighting at NORMAL time, software raster |
| `texture.cpp` | all seven DS texture formats, decoded from mapped VRAM |
| `ppu.cpp` | 2D engine text-mode background scan-out, BMP writer |
| `rt.cpp` / `runtime.cpp` | fiber scheduler (the game blocks in VBlankIntrWait; the host frame loop lives on the other fiber), IRQ state, CpuSet/DMA/CP15 shims |
| `lz77.cpp` | LZ77 type-0x10 decode (third implementation in the project; cross-checks welcome) |
| `fs.cpp` | FNT/FAT parsing, ROM-image and extracted-directory backends (the port's live seam is port/hal/fs.cpp over the asset catalog; this one reads ROM structures directly and serves as a harness backend) |
| `bmd.cpp` | host-side BMD parsing for harnesses. The GAME parses BMDs itself in the real path; this exists to render a model without the game and to cross-check |

ARM64/native backend fixes (2026-10-25): the harness now composes bone transforms
by bone ID independently of file record order, remaps each mesh part's matrix
slots through its transform-ID table, and resolves material/group bindings from
all bones. Specular/emission is read from material +0x2c; +0x24 is POLYGON_ATTR.
Polygon alpha and texture repeat/mirror flags are retained. The title-face asset
now renders coherently in the headless OpenGL ES regression test.

`rt.cpp` also has a cooperative condition-variable/thread backend for POSIX hosts.
Game and presentation execution alternate at VBlank; the Windows fiber backend
is preserved. This is a scheduler seam, not proof of a linked game startup.

The MMIO source transform lives at `port/tools/hostgen.py`: it rewrites
`*(volatile T *)0x04xxxxxx` derefs in copies of `src/` files into `NTR_MMIO()`
proxied accesses, emitted under the build tree. `src/` itself is never edited.
