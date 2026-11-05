"""Exercise real object reuse and invalidation in the host compiler driver."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as temp:
    generated=Path(temp)/'generated'
    include=generated/'include'
    include.mkdir(parents=True)
    objects=Path(temp)/'objects'
    source=generated/'fixture.cpp'
    source.write_text('#include "fixture.h"\nint fixture_value() { return FIXTURE_VALUE; }\n')
    header=include/'fixture.h'
    header.write_text('#define FIXTURE_VALUE 7\n')
    command=[sys.executable,str(ROOT/'android/tools/compile_frontend.py'),
             '--generated',str(generated),'--out',str(objects),'--incremental']

    def build():
        subprocess.run(command,cwd=ROOT,check=True,capture_output=True,text=True)

    build()
    obj=objects/'fixture.cpp.o'
    first=obj.read_bytes()
    timestamp=obj.stat().st_mtime_ns
    build()
    assert obj.stat().st_mtime_ns==timestamp
    header.write_text('#define FIXTURE_VALUE 23\n')
    build()
    assert obj.read_bytes()!=first
    second=obj.read_bytes()
    source.write_text(source.read_text().replace('return FIXTURE_VALUE','return FIXTURE_VALUE + 1'))
    build()
    assert obj.read_bytes()!=second
    source.unlink()
    build()
    assert not obj.exists()
    assert not (objects/'fixture.cpp.sha256').exists()
print('Host object cache: reuse, header/source invalidation and removed-unit cleanup passed.')
