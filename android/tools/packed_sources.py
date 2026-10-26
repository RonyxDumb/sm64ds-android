#!/usr/bin/env python3
"""Apply the audited frontend projection; matching sources stay unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]


def generate(out, manifest=None):
    data = json.loads((manifest or ROOT/'android/tools/frontend_manifest.json').read_text())
    if data['version'] != 1:
        raise ValueError('unsupported frontend manifest')
    sources = []
    inputs = set()
    for row in data['entries']:
        source = ROOT/row['path']
        inputs.add(source)
        if hashlib.sha256(source.read_text().encode()).hexdigest() != row['sha256']:
            raise ValueError('frontend source changed; regenerate/review projection: ' + row['path'])
        for path, digest in row.get('dependencies', {}).items():
            inputs.add(ROOT/path)
            if hashlib.sha256((ROOT/path).read_text().encode()).hexdigest() != digest:
                raise ValueError('frontend dependency changed; regenerate/review projection: ' + path)
        if row['header']:
            dest = out/'include'/source.relative_to(ROOT/'include')
            text = '#pragma once\n' + row['source']
        else:
            dest = out/(source.stem + '.cpp')
            text = ('#include "runtime/packed_ptr.h"\n#include "ntr/mmio.h"\n#include <cstddef>\n'
                    'namespace sm64ds::frontend::unit_' + source.stem + ' {\n'
                    'using namespace sm64ds::packed;\n#pragma pack(push, 4)\n' +
                    ('#define bool int\n' if source.suffix == '.c' else '') + row['source'] +
                    '\n#undef bool\n#pragma pack(pop)\n}\n')
            sources.append(dest)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text('// Generated from ' + row['path'] + '; see frontend_manifest.json.\n' + text)
    (out/'sources.cmake').write_text('set(PACKED_FRONTEND_SOURCES\n' +
                                    ''.join('    "' + str(p) + '"\n' for p in sources) + ')\n')
    (out/'inputs.cmake').write_text('set(PACKED_FRONTEND_INPUTS\n' +
                                   ''.join('    "' + str(p) + '"\n' for p in sorted(inputs)) + ')\n')
    return sources

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--manifest', type=Path)
    args = p.parse_args()
    generate(args.out, args.manifest)
