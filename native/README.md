# Native port (mkds-android architecture)

The game's own code, from the matched decompilation ([tangosdev/sm64ds-decomp](https://github.com/tangosdev/sm64ds-decomp),
pinned as the `native/decomp` submodule, unmodified), compiled for the host CPU, with the Nintendo DS hardware
replaced by a native platform layer. The layout and the platform layer follow
[mkds-android](https://github.com/RonyxDumb/mkds-android), whose DS platform layer (`port/platform`) this port
adapts: both games run on the same NitroSDK and the same hardware.

| Path | Contents |
| --- | --- |
| `decomp/` | the decompilation (submodule) |
| `port/patches/` | unified diffs the port applies to decomp files (C/C++ that mwcc accepts and GCC/clang do not) |
| `port/compat/` | force-included prefix and shadow headers |
| `port/exclude.txt` | decomp units the platform layer replaces (startup, hand-written assembly, OS/IRQ, DMA, ARM7 buses) |
| `port/platform/` | the DS platform layer (from mkds-android, being adapted) |
| `tools/portify.py` | prepares the decomp's sources for the native compilers (patches, register writes, block-scope externs) |
| `tools/gapgen.py` | the parts of the DS image without source: their symbols stay at their DS addresses, filled from the ROM |
| `tools/compile_census.py` | compiles every prepared unit and lists failures |

## Data without source

The decomp matches every function, but about 380 KB of data is still kept as extracted bytes. In the native port
that data stays at its DS address inside the reserved DS address space (as in mkds, the process keeps the DS memory
map), and is copied from the user's ROM when its module loads. `tools/gapgen.py` turns the decomp's configuration
(no ROM content) into absolute symbols for that data and a table of the pointers inside it that must be redirected
to native code or data.

## Status

See `../android/CHECKPOINT.md`.

```sh
python3 tools/portify.py decomp /tmp/gen port/exclude.txt
python3 tools/compile_census.py /tmp/gen /tmp/census "gcc -m32"   # 2721/2721
python3 tools/gapgen.py decomp /tmp/gen/gap
```
