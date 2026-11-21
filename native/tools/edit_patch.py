#!/usr/bin/env python3
"""Edit a decomp file by exact text replacements and record the result as the port's patch for it.

Usage: edit_patch.py <decomp-relative path> <old> <new> [<old> <new> ...]
Replacements apply on top of the file's existing patch, if any. Each <old> must occur exactly once.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

N = Path(__file__).resolve().parent.parent
rel = sys.argv[1]
pairs = sys.argv[2:]
src = N / "decomp" / rel
patch = N / "port" / "patches" / (("include/" + rel[len("include/"):]) if rel.startswith("include/") else rel)
patch = patch.with_name(patch.name + ".patch")
text = src.read_text(encoding="utf-8", errors="surrogateescape")
with tempfile.TemporaryDirectory() as tmp:
    f = Path(tmp) / "f"
    f.write_text(text, encoding="utf-8", errors="surrogateescape")
    if patch.exists():
        subprocess.run(["patch", "-s", str(f), str(patch)], check=True)
        text = f.read_text(encoding="utf-8", errors="surrogateescape")
    for old, new in zip(pairs[0::2], pairs[1::2]):
        n = text.count(old)
        if n != 1:
            sys.exit(f"edit_patch: {rel}: {old!r} occurs {n} times")
        text = text.replace(old, new)
    f.write_text(text, encoding="utf-8", errors="surrogateescape")
    subprocess.run([str(N / "tools" / "mkpatch.sh"), rel, str(f)], check=True)
