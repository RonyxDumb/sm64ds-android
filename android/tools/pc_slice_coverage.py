#!/usr/bin/env python3
"""Coverage of the upstream PC port's gate slices by the Android runtime.

The PC port (external/sm64ds-pc/port) grows its host build gate by gate;
each port/slice_gate*.txt lists the src/ and port/unmatched/ translation
units that gate needs on the host. Gates 10-28 are exactly the castle-grounds
gameplay stack (Player standing and walking, jump and camera, level boot,
castle objects, HUD, minimap). This tool lists, per gate, which functions of
those units the Android stage runtime already binds and which it does not,
and can emit the unbound single-function units as frontier_batch.py input.

Usage: pc_slice_coverage.py [--gates 10,13,14,...] [--missing] [--json OUT]
"""
import argparse
import json
import re
from pathlib import Path

import frontier_batch

ROOT = Path(__file__).resolve().parents[2]
UP = ROOT/'external/sm64ds-pc'
CASTLE_GATES = [10, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 25, 26, 27, 28]


def slice_units(gate):
    path = UP/'port'/('slice_gate%d.txt' % gate)
    units = []
    for line in path.read_text().splitlines():
        line = line.split('#', 1)[0].strip()
        if line.startswith(('src/', 'port/')):
            units.append(line)
    return list(dict.fromkeys(units))


def unit_symbols(rel, by_name):
    """Function symbols a unit defines: its @symbol markers, else its stem."""
    path = UP/rel
    if not path.exists():
        return []
    text = path.read_text(errors='ignore')
    marked = re.findall(r'^// @symbol (\S+)', text, re.M)
    if marked:
        return [m for m in marked if m in by_name]
    return [path.stem] if path.stem in by_name else []


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gates', default=','.join(map(str, CASTLE_GATES)))
    ap.add_argument('--missing', action='store_true', help='print every unbound symbol')
    ap.add_argument('--json', type=Path)
    args = ap.parse_args()
    by_name, _ = frontier_batch.symbol_tables()
    linked, native = frontier_batch.linked_addresses()
    bound = linked | native
    report, missing_all = {}, {}
    for gate in [int(g) for g in args.gates.split(',')]:
        total = done = 0
        missing = []
        for rel in slice_units(gate):
            for sym in unit_symbols(rel, by_name):
                total += 1
                if by_name[sym][0] in bound:
                    done += 1
                else:
                    missing.append((sym, rel))
                    missing_all.setdefault(sym, rel)
        report[gate] = (done, total, missing)
        print('gate %3d: %4d / %4d functions bound' % (gate, done, total))
        if args.missing:
            for sym, rel in missing:
                print('    %-60s %s' % (sym, rel))
    print('castle stack: %d distinct unbound functions' % len(missing_all))
    if args.json:
        args.json.write_text(json.dumps(missing_all, indent=1, sort_keys=True))


if __name__ == '__main__':
    main()
