#!/usr/bin/env python3
"""Fail closed if source evidence for the packed stage constructors changes."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def verify(out=None):
    profile = json.loads((ROOT/'android/tools/stage_runtime_profile.json').read_text())
    if profile['version'] != 1:
        raise ValueError('unsupported stage runtime source profile')
    for path, digest in profile['inputs'].items():
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest() != digest:
            raise ValueError('stage ABI evidence changed; review the native implementation: '+path)
    if out:
        out.write_text('set(STAGE_RUNTIME_INPUTS\n'+''.join(
            '    "'+str(ROOT/path)+'"\n' for path in profile['inputs'])+')\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    verify(parser.parse_args().out)
