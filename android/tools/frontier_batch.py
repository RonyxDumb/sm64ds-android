"""Plan the next isolated-unit batch for a missing native code handle.

Usage: python3 android/tools/frontier_batch.py <address|symbol> [--depth N] [--apply]

Resolves the cartridge address to its ARM9/overlay symbol, finds the
recovered isolated unit in external/sm64ds-pc/src, and walks its callees
(depth-limited). A callee counts as linked when its address is already a
stage-manifest binding or appears as a native handle in the runtime C++.
Unlinked callees with a recovered free-function unit are proposed for
FREE_UNITS; the rest are listed so they can be routed to a reviewed view
(member units, block units) by hand. --apply appends the free-function
proposals to FREE_UNITS in stage_isolated_views.py (no other file changes).
This only plans linkage: every unit still compiles from the recovered
source through lift_stage.py, and runtime evidence decides correctness.
"""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UP = ROOT/'external/sm64ds-pc'
# NNS sound SDK internals: the runtime serves this boundary natively
# (stage_runtime.cpp sound section); never link their bodies.
SDK_BOUNDARY = {0x02050c14, 0x020509b0, 0x020523e0, 0x02051a98, 0x02051f1c, 0x02051e60,
                0x0205acac, 0x0205a8f0, 0x02050cdc}
SYM = re.compile(r'^(\S+) kind:function\([^)]*\) addr:0x([0-9a-fA-F]+)')


def symbol_tables():
    by_name, by_addr = {}, {}
    tables = [(UP/'config/arm9/symbols.txt', 0), (UP/'config/arm9/itcm/symbols.txt', 0)]
    for ov in sorted((UP/'config/arm9/overlays').glob('ov*/symbols.txt')):
        tables.append((ov, int(ov.parent.name[2:])))
    for path, overlay in tables:
        for line in path.read_text().splitlines():
            m = SYM.match(line)
            if m:
                addr = int(m[2], 16)
                by_name.setdefault(m[1], (addr, overlay))
                by_addr.setdefault((addr, overlay), m[1])
                by_addr.setdefault(addr, m[1])
    return by_name, by_addr


def linked_addresses():
    manifest = json.loads((ROOT/'android/tools/stage_manifest.json').read_text())
    linked = {b['address'] for b in manifest['bindings']}
    # The stage also binds the frontend's ARM9 SDK units (bind_arm9_sources):
    # the frontend bindings whose address is an ARM9 symbol of config/arm9.
    arm9 = set()
    for rel in ('config/arm9/symbols.txt', 'config/arm9/itcm/symbols.txt'):
        for line in (ROOT/rel).read_text().splitlines():
            match = re.match(r'\S+ kind:function.*?addr:0x([0-9a-f]+)', line)
            if match:
                arm9.add(int(match[1], 16))
    frontend = json.loads((ROOT/'android/tools/frontend_manifest.json').read_text())
    linked |= {b['address'] for b in frontend['bindings'] if b['address'] in arm9}
    native = set()
    for path in (ROOT/'android/app/src/main/cpp').rglob('*'):
        if path.suffix in ('.cpp', '.h', '.inc'):
            # Only addresses the runtime actually binds (space.bind / VENEER).
            for hexval in re.findall(r'(?:bind\(|VENEER\()\s*0x(0[12][0-9a-fA-F]{6})', path.read_text(errors='ignore')):
                native.add(int(hexval, 16))
    return linked, native


def unit_source(name):
    for ext in ('.c', '.cpp'):
        path = UP/'src'/(name+ext)
        if path.exists():
            return path
    return None


def callees(path, by_name, own):
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(), flags=re.S)
    found = []
    for word in dict.fromkeys(re.findall(r'\b([A-Za-z_]\w*)\s*\(', text)):
        if word != own and word in by_name:
            found.append(word)
    # Calls through cartridge addresses (Ptr<...>(0x0201....u)).
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('target')
    ap.add_argument('--depth', type=int, default=3)
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    by_name, by_addr = symbol_tables()
    target = args.target
    if re.fullmatch(r'(0x)?[0-9a-fA-F]+', target) or target.isdigit():
        addr = int(target, 0) if target.startswith('0x') or target.isdigit() else int(target, 16)
        target = by_addr.get(addr)
        if not target:
            raise SystemExit('no function symbol at 0x%08x' % addr)
    linked, native = linked_addresses()
    views = (ROOT/'android/tools/stage_isolated_views.py').read_text()
    listed = set(re.findall(r"\('([\w]+)',\s*\d+", views))
    propose, manual, veneers, seen = [], [], [], set()

    def visit(name, depth):
        if name in seen:
            return
        seen.add(name)
        addr, overlay = by_name[name]
        if addr in SDK_BOUNDARY:
            return
        is_linked = addr in linked or addr in native or name in listed
        src = unit_source(name)
        veneer = src and re.search(r'tail-call veneer to (\w+) \((0x[0-9a-fA-F]+)\)', src.read_text())
        if veneer and 'ldr ip' in src.read_text() and not is_linked:
            # Register-preserving ldr ip / bx ip veneers forward r0-r3 natively.
            veneers.append((name, addr, int(veneer[2], 16)))
            if veneer[1] in by_name:
                visit(veneer[1], depth)
            return
        if not is_linked:
            if src and not (name.startswith('_ZN') and '::' in src.read_text()) and not re.search(r'^\w[^;\n]*\b\w+::~?\w+\s*\([^;]*$', src.read_text(), re.M):
                propose.append((name, overlay, addr, src))
            else:
                manual.append((name, overlay, addr, src))
        if src and depth > 0 and addr not in native:
            for callee in callees(src, by_name, name):
                visit(callee, depth-1)

    visit(target, args.depth)
    for name, overlay, addr, src in propose:
        print('FREE   %-48s ov%03d 0x%08x %s' % (name, overlay, addr, src.relative_to(ROOT)))
    for name, overlay, addr, src in manual:
        print('MANUAL %-48s ov%03d 0x%08x %s' % (name, overlay, addr, src.relative_to(ROOT) if src else '(no recovered unit)'))
    for name, addr, target in veneers:
        print('VENEER %-48s 0x%08x -> 0x%08x' % (name, addr, target))
    if args.apply and veneers:
        path = ROOT/'android/app/src/main/cpp/runtime/stage_tail_veneers.inc'
        path.write_text(path.read_text()+''.join('VENEER(0x%08xu, 0x%08xu) // %s\n' % (a, t, n) for n, a, t in veneers))
        print('appended %d VENEER entries' % len(veneers))
    members, sources, blocks = [], [], []
    for name, overlay, addr, src in manual:
        if src:
            continue
        # Members recovered inside multi-function class sources (Player.cpp).
        for rel in ('external/sm64ds-pc/src/actors/Player.cpp', 'src/game/player/Player.cpp'):
            text = (ROOT/rel).read_text()
            found = re.search(r'^(\w[\w\s\*&]*?\b(\w+)::'+re.escape(name)+r'\([^)]*\))\s*\{?\s*$', text, re.M)
            if found:
                sources.append((name, overlay, found[2], rel, found[1].strip()))
                break
            if '// @symbol '+name+'\n' in text and not name.startswith('_ZN'):
                blocks.append((name, overlay, rel))
                break
    for entry in blocks:
        print('BLOCK  %-48s ov%03d in %s' % entry)
    if args.apply and blocks:
        anchor = 'BLOCK_UNITS = [\n'
        lines = ''.join("    ('%s', %d, '%s'),\n" % e for e in blocks)
        path = ROOT/'android/tools/stage_isolated_views.py'
        path.write_text(path.read_text().replace(anchor, anchor+lines, 1))
        print('appended %d BLOCK_UNITS entries' % len(blocks))
    for entry in sources:
        print('SOURCE %-48s ov%03d %s in %s' % (entry[0], entry[1], entry[4], entry[3]))
    if args.apply and sources:
        anchor = 'SOURCE_MEMBERS = [\n'
        lines = ''.join("    ('%s', %d, '%s', '%s', '%s'),\n" % e for e in sources)
        path = ROOT/'android/tools/stage_isolated_views.py'
        path.write_text(path.read_text().replace(anchor, anchor+lines, 1))
        print('appended %d SOURCE_MEMBERS entries' % len(sources))
    for name, overlay, addr, src in manual:
        cls = re.match(r'_ZNK?(\d+)', name)
        if src and cls:
            cls = name[len(cls[0]):len(cls[0])+int(cls[1])]
            if cls != 'Player' and (ROOT/'external/sm64ds-pc/include'/(cls+'.h')).exists():
                members.append((name, overlay, cls))
    if args.apply and members:
        anchor = 'MEMBER_UNITS = [\n'
        lines = ''.join("    ('%s', %d, '%s'),\n" % m for m in members)
        path = ROOT/'android/tools/stage_isolated_views.py'
        path.write_text(path.read_text().replace(anchor, anchor+lines, 1))
        print('appended %d MEMBER_UNITS entries' % len(members))
    if args.apply and propose:
        anchor = 'FREE_UNITS = [\n'
        lines = ''.join("    ('%s', %d),\n" % (n, ov) for n, ov, _, _ in propose)
        path = ROOT/'android/tools/stage_isolated_views.py'
        path.write_text(path.read_text().replace(anchor, anchor+lines, 1))
        print('appended %d FREE_UNITS entries' % len(propose))


if __name__ == '__main__':
    main()
