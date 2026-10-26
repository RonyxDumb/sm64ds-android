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
    for path,digest in data.get('dependencies',{}).items():
        inputs.add(ROOT/path)
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=digest:
            raise ValueError('frontend symbol map changed; regenerate/review projection: '+path)
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
    if 'bindings' in data:
        dest = out/'frontend_bindings.cpp'
        symbols = sorted({binding['symbol'] for binding in data['bindings']})
        text = '#include "runtime/packed_memory.h"\nextern "C" {\n'
        text += ''.join('void '+name+'();\n' for name in symbols)+'}\n'
        text += 'namespace sm64ds::frontend {\nvoid bind_sources(packed::Space &space) {\n'
        text += ''.join('    space.bind(0x%08xu, %s);\n'%(b['address'],b['symbol']) for b in data['bindings'])
        text += '}\n}\n'
        dest.write_text(text)
        sources.append(dest)
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
