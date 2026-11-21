"""Compiler flags for the decomp's code, shared by the host build, the guest build and the census."""
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent / "port"


def game_flags(gen, rel, gcc=False):
    gen = Path(gen)
    common = [
        "-O2", "-g", "-fno-omit-frame-pointer", "-ffreestanding", "-fno-builtin-printf",
        "-fno-strict-aliasing",          # the game type-puns freely
        "-fwrapv",                       # and relies on wrapping arithmetic
        "-fshort-wchar",                 # wchar_t is 16 bits on the DS
        "-fsigned-char",                 # the decomp builds with mwcc -char signed
        "-ffunction-sections", "-fdata-sections", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-I", str(gen / "include"), "-I", str(gen / "include" / "nitro"), "-I", str(PORT / "platform" / "include"),
        "-include", str(PORT / "compat" / "port_prefix.h"),
        "-w", "-Wno-error", "-Wno-narrowing", "-fno-pic", "-fno-pie",
    ]
    if is_cpp(gen, rel):
        extra = ["-fpermissive"] if gcc else []
        return common + extra + ["-x", "c++", "-std=gnu++11", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                                 "-fno-use-cxa-atexit"]
    extra = ["-fpermissive", "-Wno-error=incompatible-pointer-types", "-Wno-error=int-conversion",
             "-Wno-error=implicit-function-declaration"] if gcc else []
    return common + extra + ["-x", "c", "-std=gnu99", "-fcommon"]


def is_cpp(gen, rel):
    """C++: a .cpp file, or (the decomp's rule in tools/rombuild.py) a file whose first line is //cpp."""
    if rel.endswith(".cpp"):
        return True
    with open(Path(gen) / "src" / rel, encoding="utf-8", errors="surrogateescape") as f:
        f.readline()  # the #line directive tools/portify.py puts first
        return f.readline().startswith("//cpp")
