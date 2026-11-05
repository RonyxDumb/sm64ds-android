#!/usr/bin/env python3
"""Fail closed if source evidence for the packed stage constructors changes."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def verify(out=None, ranges_header=None):
    profile = json.loads((ROOT/'android/tools/stage_runtime_profile.json').read_text())
    if profile['version'] != 1:
        raise ValueError('unsupported stage runtime source profile')
    for path, digest in profile['inputs'].items():
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest() != digest:
            raise ValueError('stage ABI evidence changed; review the native implementation: '+path)
    if out:
        out.write_text('set(STAGE_RUNTIME_INPUTS\n'+''.join(
            '    "'+str(ROOT/path)+'"\n' for path in profile['inputs'])+')\n')
    if ranges_header:
        # The overlay initializer pointer is not a text boundary: writable
        # BTA descriptors precede it. Audit the actual executable sections.
        rows=[]
        for module in (1,2,9,62,69,85,89,94,100,98,102):
            rel='external/sm64ds-pc/config/arm9/overlays/ov%03d/delinks.txt'%module
            if rel not in profile['inputs']:
                raise ValueError('untracked executable-section evidence: '+rel)
            sections=[]
            for line in (ROOT/rel).read_text().splitlines():
                if line and not line[0].isspace(): break
                match=re.match(r'\s+\.(\S+)\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+) kind:code\b',line)
                if match:
                    start,end=int(match[2],16),int(match[3],16)
                    if end<=start: raise ValueError('invalid executable section: '+rel)
                    sections.append((start,end))
            # Overlay 69 contains only empty ctor/BSS sections. Preserve an
            # explicit audited marker instead of inventing an executable span.
            if not sections: sections=[(0,0)]
            rows.extend((module,start,end) for start,end in sections)
        ranges_header.write_text('#pragma once\nstruct StageCodeRange { unsigned module,start,end; };\n'
            'inline constexpr StageCodeRange stageCodeRanges[] = {\n'+''.join(
                '    {%d,0x%08xu,0x%08xu},\n'%row for row in rows)+'};\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--ranges-header', type=Path)
    args=parser.parse_args()
    verify(args.out,args.ranges_header)
