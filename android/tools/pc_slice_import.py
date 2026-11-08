#!/usr/bin/env python3
"""Bulk-import the castle gameplay stack of the upstream PC port.

The PC host build (external/sm64ds-pc/port) compiles the recovered src/
units listed in its gate slices with 32-bit MSVC; that build is not usable
on ARM64 Android (4-byte pointers, MSVC pointer-to-member flags). What does
carry over is the *selection*: the slices name exactly the original units
the castle-grounds stack needs. This tool takes every function of those
units that the Android stage runtime does not bind yet (pc_slice_coverage),
projects it through the same reviewed views as the hand-listed units
(stage_isolated_views.py), and keeps only units that

  1. project (the ARM64 packed-pointer view can express them),
  2. compile with the host gcc ASan build flags, and
  3. compile with clang and the APK's own -Werror flags.

Everything else is reported with its reason in pc_slice_rejected.json for
targeted work. Linkage is still by cartridge address; runtime evidence
(ROM-backed tests) decides correctness, exactly as for frontier units.

Usage: pc_slice_import.py --build DIR [--gates ...] [--rounds N]
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import frontier_batch
import pc_slice_coverage

ROOT = Path(__file__).resolve().parents[2]
UP = ROOT/'external/sm64ds-pc'
UNITS = ROOT/'android/tools/pc_slice_units.json'
REJECTED = ROOT/'android/tools/pc_slice_rejected.json'
MEMBER_DEF = re.compile(r'^(?:[\w][\w\s\*&:<>,]*?\s)?(\w+)::(~?\w+)\s*\([^;{]*\)\s*(?:const\s*)?\{', re.M)


def mangled_class(symbol):
    """The outermost class of a mangled member name (_ZN<len><Class>...)."""
    match = re.match(r'_ZNK?(\d+)', symbol)
    if not match:
        return None
    start = len(match[0])
    return symbol[start:start+int(match[1])]


def is_static_member(cls, method):
    for rel in ('include/'+cls+'.h', 'external/sm64ds-pc/include/'+cls+'.h'):
        path = ROOT/rel
        if path.exists() and re.search(r'\bstatic\b[^;\n]*\b'+re.escape(method)+r'\s*\(', path.read_text()):
            return True
    return False


def classify(symbol, rel, overlay):
    """An importable unit entry, or (None, reason)."""
    path = UP/rel
    text = path.read_text(errors='ignore')
    if rel.startswith('port/'):
        return None, 'port-side source (seat by hand)'
    if path.stem == symbol:
        member = MEMBER_DEF.search(text)
        if member:
            cls, method = member[1], member[2]
            if cls != mangled_class(symbol):
                return None, 'nested or namespaced member %s::%s' % (cls, method)
            if method.startswith('~') or method == cls:
                return None, 'structor (read from cartridge code by hand)'
            if is_static_member(cls, method):
                return None, 'static member (needs a free view)'
            return {'kind': 'member', 'symbol': symbol, 'overlay': overlay, 'cls': cls}, None
        return {'kind': 'free', 'symbol': symbol, 'overlay': overlay}, None
    # Multi-function source: the @symbol block.
    start = text.find('// @symbol '+symbol+'\n')
    if start < 0:
        return None, 'no @symbol block'
    end = text.find('\n// @symbol ', start+1)
    block = text[start:] if end < 0 else text[start:end]
    member = MEMBER_DEF.search(block)
    if member:
        cls, method = member[1], member[2]
        if method.startswith('~') or method == cls or is_static_member(cls, method):
            return None, 'structor or static member in a class source'
        line = block[member.start():block.index('{', member.start())].strip()
        return {'kind': 'source_member', 'symbol': symbol, 'overlay': overlay, 'cls': cls,
                'rel': 'external/sm64ds-pc/'+rel, 'name': line}, None
    return {'kind': 'block', 'symbol': symbol, 'overlay': overlay, 'rel': 'external/sm64ds-pc/'+rel}, None


def pc_rel(entry):
    for ext in ('.c', '.cpp'):
        if (UP/'src'/(entry['symbol']+ext)).exists():
            return 'src/'+entry['symbol']+ext
    return entry.get('rel', '').replace('external/sm64ds-pc/', '')


def project_ok(entry):
    sys.path.insert(0, str(ROOT/'android/tools'))
    import lift_stage
    from stage_isolated_views import pc_slice_unit
    try:
        row, bindings = pc_slice_unit(lift_stage.projected, entry)
        for binding in bindings:
            if 'extern "C" unsigned int '+binding['symbol']+'(' not in row['source'] and \
               not re.search(r'extern "C"[^\n]*\b'+re.escape(binding['symbol'])+r'\s*\(', row['source']):
                return 'binding without a generated entry point: '+binding['symbol']
        return None
    except Exception as error:  # the projection reports why it cannot express a unit
        return '%s: %s' % (type(error).__name__, str(error)[:160])


def run(cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)


def regenerate(build):
    for cmd in (['python3', 'android/tools/lift_stage.py'],
                ['python3', 'android/tools/native_sources.py', '--out', str(build/'generated')],
                ['python3', 'android/tools/verify_stage_sources.py', '--ranges-header',
                 str(build/'generated/stage_code_ranges.h')],
                ['python3', 'android/tools/packed_sources.py', '--out', str(build/'stage'),
                 '--manifest', 'android/tools/stage_manifest.json']):
        result = run(cmd)
        if result.returncode:
            raise SystemExit(' '.join(cmd)+' failed:\n'+result.stdout[-2000:]+result.stderr[-4000:])


def defined_elsewhere():
    """Names the runtime already defines as pk_/abi_ entry points: native
    C++ (often an SDK boundary), the frontend and the minigame manifests.
    Importing them again would duplicate the definition."""
    names = set()
    for path in (ROOT/'android/app/src/main/cpp').rglob('*.cpp'):
        names |= set(re.findall(r'\b(?:pk|abi)_(\w+)\s*\(', path.read_text(errors='ignore')))
    for manifest in ('frontend_manifest.json', 'minigame_manifest.json'):
        data = json.loads((ROOT/'android/tools'/manifest).read_text())
        for row in data['entries']:
            names |= set(re.findall(r'extern "C"[^\n(]*\b(?:pk|abi)_(\w+)\s*\(', row.get('source', '')))
    return names


def failing_units(output):
    return set(re.findall(r'stage_pcslice_(\w+?)\.cpp', output))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, required=True, help='host build directory (run-game-tests layout)')
    ap.add_argument('--gates', default=','.join(map(str, pc_slice_coverage.CASTLE_GATES)))
    ap.add_argument('--rounds', type=int, default=6)
    args = ap.parse_args()
    by_name, _ = frontier_batch.symbol_tables()
    linked, native = frontier_batch.linked_addresses()
    existing = json.loads(UNITS.read_text())['units'] if UNITS.exists() else []
    rejected = json.loads(REJECTED.read_text()) if REJECTED.exists() else {}
    elsewhere = defined_elsewhere()
    for entry in existing:
        if entry['symbol'] in elsewhere:
            rejected[entry['symbol']] = {'gate': entry.get('gate'), 'source': entry.get('rel', ''),
                                         'reason': 'already defined natively/frontend/minigame'}
    existing = [e for e in existing if e['symbol'] not in elsewhere]
    kept = []
    for entry in existing:
        reason = None
        if entry['kind'] in ('free', 'member'):
            fresh, reason = classify(entry['symbol'], pc_rel(entry), entry['overlay'])
            if fresh and fresh['kind'] != entry['kind']:
                reason = 'reclassified as '+fresh['kind']
        reason = reason or project_ok(entry)
        if reason:
            rejected[entry['symbol']] = {'gate': entry.get('gate'), 'source': entry.get('rel', ''), 'reason': reason}
        else:
            kept.append(entry)
    existing = kept
    have = {e['symbol'] for e in existing}
    candidates = {}
    for gate in [int(g) for g in args.gates.split(',')]:
        for rel in pc_slice_coverage.slice_units(gate):
            for sym in pc_slice_coverage.unit_symbols(rel, by_name):
                addr, overlay = by_name[sym]
                if addr in linked or addr in native or addr in frontier_batch.SDK_BOUNDARY:
                    continue
                if sym in have or sym in candidates or sym in elsewhere:
                    continue
                candidates[sym] = (rel, overlay, gate)
    views = (ROOT/'android/tools/stage_isolated_views.py').read_text()
    entries = []
    for sym, (rel, overlay, gate) in sorted(candidates.items()):
        if "'"+sym+"'" in views:
            continue  # hand-listed (and possibly deliberately excluded) already
        entry, reason = classify(sym, rel, overlay)
        if entry:
            reason = project_ok(entry)
        if reason:
            rejected[sym] = {'gate': gate, 'source': rel, 'reason': reason}
            continue
        entry['gate'] = gate
        entries.append(entry)
    print('candidates %d, projected %d, rejected so far %d' % (len(candidates), len(entries), len(rejected)))
    units = existing+entries
    for round_ in range(args.rounds):
        UNITS.write_text(json.dumps({'units': units}, indent=1)+'\n')
        regenerate(args.build)
        gcc = run(['python3', 'android/tools/compile_frontend.py', '--generated', str(args.build/'stage'),
                   '--out', str(args.build/'stage-objects'), '--incremental'])
        clang = run(['python3', 'android/tools/ndk_warning_check.py', '--generated', str(args.build/'stage'),
                     '--include', str(args.build/'packed/include'), '--include', str(args.build/'minigame/include')])
        bad = failing_units(gcc.stdout+gcc.stderr) | failing_units(clang.stdout+clang.stderr)
        print('round %d: %d units, %d failing to compile' % (round_, len(units), len(bad)))
        if not bad:
            break
        for entry in units:
            if entry['symbol'] in bad:
                rejected[entry['symbol']] = {'gate': entry.get('gate'), 'source': entry.get('rel', ''),
                                             'reason': 'compile (gcc host or clang NDK flags)'}
        units = [e for e in units if e['symbol'] not in bad]
    UNITS.write_text(json.dumps({'units': units}, indent=1)+'\n')
    REJECTED.write_text(json.dumps(rejected, indent=1, sort_keys=True)+'\n')
    regenerate(args.build)
    print('imported %d units; %d rejected (see %s)' % (len(units), len(rejected), REJECTED.name))


if __name__ == '__main__':
    main()
