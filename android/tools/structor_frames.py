#!/usr/bin/env python3
"""Native structor frames read from the cartridge code.

The recovered destructor/constructor units hold only the (usually empty)
body: mwccarm emits the frame itself -- vtable stores, member and base
structors in reverse declaration order, __cxa_vec_cleanup for array members
and the class operator delete. Those frames are straight-line ARM code with
`this` in r4. This tool reads a structor from the user's own ROM, accepts it
only when every instruction is one of those frame operations (no compares,
no conditional execution, no loads through `this`), and records the
resulting operation list in structor_frames.json: addresses, offsets and
vtable words only, never code bytes. A frame that does anything else is
rejected and must be written by hand.

stage_structor_frames.inc (C++, generated from the JSON alone, no ROM needed)
defines one native function per frame and bind_structor_frames(space, module)
which binds the frames of the main ARM9 module (-1) or one overlay.

Usage:
  structor_frames.py --rom ROM --add SYMBOL|0xADDR ... [--recurse]
  structor_frames.py --generate
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'android/tools'))
import frontier_batch  # noqa: E402

FRAMES = ROOT/'android/tools/structor_frames.json'
OUTPUT = ROOT/'android/app/src/main/cpp/runtime/stage_structor_frames.inc'


def blz_decompress(data):
    """Nintendo's backwards LZ (overlay/ARM9 compression)."""
    data = bytearray(data)
    inc_len = struct.unpack_from('<I', data, len(data)-4)[0]
    if not inc_len:
        return bytes(data)
    hdr_len = data[len(data)-5]
    enc_len = struct.unpack_from('<I', data, len(data)-8)[0] & 0xffffff
    dec_len = len(data)-enc_len
    pak = data[dec_len:dec_len+enc_len-hdr_len][::-1]
    raw_len = enc_len+inc_len
    raw = bytearray()
    i = 0
    flags = mask = 0
    while len(raw) < raw_len:
        mask >>= 1
        if not mask:
            if i >= len(pak):
                break
            flags, mask = pak[i], 0x80
            i += 1
        if not flags & mask:
            if i >= len(pak):
                break
            raw.append(pak[i])
            i += 1
        else:
            if i+1 >= len(pak):
                break
            pos = pak[i] << 8 | pak[i+1]
            i += 2
            length = min((pos >> 12)+3, raw_len-len(raw))
            pos = (pos & 0xfff)+3
            for _ in range(length):
                raw.append(raw[-pos])
    return bytes(data[:dec_len])+bytes(raw[::-1])


class Rom:
    def __init__(self, path):
        self.rom = Path(path).read_bytes()
        rom = self.rom
        self.fat = struct.unpack_from('<I', rom, 0x48)[0]
        arm9_off, _, arm9_ram, arm9_size = struct.unpack_from('<IIII', rom, 0x20)
        arm9 = rom[arm9_off:arm9_off+arm9_size]
        # ARM9 module parameters (nitrocode magic) carry the compressed end.
        magic = arm9.find(struct.pack('<II', 0xdec00621, 0x2106c0de))
        if magic >= 0:
            end = struct.unpack_from('<I', arm9, magic-0x1c+0x14)[0]
            if end:
                cut = end-arm9_ram
                arm9 = blz_decompress(arm9[:cut])+arm9[cut:]
        self.modules = {0: (arm9_ram, arm9)}
        ovt, ovt_size = struct.unpack_from('<II', rom, 0x50)
        for k in range(ovt_size//32):
            ov_id, ram, size, _, _, _, file_id, flags = struct.unpack_from('<8I', rom, ovt+k*32)
            start, end = struct.unpack_from('<II', rom, self.fat+file_id*8)
            image = rom[start:end]
            if flags & 0x01000000:
                image = blz_decompress(image[:flags & 0xffffff])
            self.modules[ov_id+1000] = (ram, image)

    def read(self, address, size, module):
        ram, image = self.modules[0 if module < 0 else module+1000]
        if not ram <= address < ram+len(image):
            raise ValueError('0x%08x is outside module %d' % (address, module))
        return image[address-ram:address-ram+size]


class Reject(Exception):
    pass


def translate(code, address):
    """The frame's operations, or Reject. r4 (or r0 before it is moved) is
    `this`; expressions are ('self', off), ('const', v), ('load', v), ('ret',)."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    md.detail = True
    regs = {'r0': ('self', 0)}
    written = set()
    ops = []
    for insn in md.disasm(code, address):
        m, o = insn.mnemonic, insn.op_str
        if insn.cc not in (capstone.arm.ARM_CC_AL, capstone.arm.ARM_CC_INVALID):
            raise Reject('conditional %s %s at 0x%x' % (m, o, insn.address))
        if m == 'push':
            continue
        if m == 'bx' and o == 'lr' or m == 'pop' and 'pc' in o or m == 'ldm' and 'pc' in o:
            ret = regs.get('r0')
            return ops, ret
        if m == 'pop':
            if o.strip('{}').split(', ')[-1] == 'lr':
                continue
            raise Reject('pop %s' % o)
        match = re.fullmatch(r'(r\d+|ip), (r\d+)', o)
        if m == 'mov' and match:
            if match[2] not in regs:
                raise Reject('mov from undefined %s' % match[2])
            regs[match[1]] = regs[match[2]]
            written.add(match[1])
            continue
        match = re.fullmatch(r'(r\d+), #(-?(?:0x)?[0-9a-f]+)', o)
        if m == 'mov' and match:
            regs[match[1]] = ('const', int(match[2], 0) & 0xffffffff)
            written.add(match[1])
            continue
        match = re.fullmatch(r'(r\d+), \[pc, #(-?(?:0x)?[0-9a-f]+)\]', o)
        if m == 'ldr' and match:
            literal = insn.address+8+int(match[2], 0)
            off = literal-address
            if not 0 <= off <= len(code)-4:
                raise Reject('literal outside the read window')
            regs[match[1]] = ('const', struct.unpack_from('<I', code, off)[0])
            written.add(match[1])
            continue
        match = re.fullmatch(r'(r\d+), \[(r\d+)\]', o)
        if m == 'ldr' and match and regs.get(match[2], ('',))[0] == 'const':
            regs[match[1]] = ('load', regs[match[2]][1])
            written.discard(match[2])
            written.add(match[1])
            continue
        match = re.fullmatch(r'(r\d+), (r\d+), #((?:0x)?[0-9a-f]+)', o)
        if m == 'add' and match and regs.get(match[2], ('',))[0] == 'self':
            regs[match[1]] = ('self', regs[match[2]][1]+int(match[3], 0))
            written.add(match[1])
            continue
        match = re.fullmatch(r'(r\d+), (r\d+), (r\d+)', o)
        if m == 'add' and match and regs.get(match[2], ('',))[0] == 'self' and regs.get(match[3], ('',))[0] == 'const':
            # A member beyond the 8-bit immediate range: this + a literal.
            regs[match[1]] = ('self', regs[match[2]][1]+regs[match[3]][1])
            written.discard(match[3])
            written.add(match[1])
            continue
        match = re.fullmatch(r'(r\d+), \[(r\d+)(?:, #((?:0x)?[0-9a-f]+))?\]', o)
        if m == 'str' and match and regs.get(match[2], ('',))[0] == 'self':
            value = regs.get(match[1])
            if not value or value[0] not in ('const', 'self'):
                raise Reject('store of %s' % (value,))
            ops.append(('store', regs[match[2]][1]+int(match[3] or '0', 0), value))
            written.discard(match[1])  # consumed by the store, not a call argument
            continue
        if m == 'bl' and o.startswith('#'):
            target = int(o[1:], 0)
            count = 0
            for k in range(4):
                if 'r%d' % k in written or (k == 0 and 'r0' in regs):
                    count = k+1
            args = []
            for k in range(count):
                if 'r%d' % k not in regs:
                    raise Reject('argument r%d undefined at 0x%x' % (k, insn.address))
                args.append(regs['r%d' % k])
            ops.append(('call', target, args))
            for k in ('r0', 'r1', 'r2', 'r3', 'ip'):
                regs.pop(k, None)
            regs['r0'] = ('ret',)
            written = set()
            continue
        raise Reject('%s %s at 0x%x' % (m, o, insn.address))
    raise Reject('no return within the read window')


def expr(e):
    kind = e[0]
    if kind == 'self':
        return 'self' if not e[1] else 'self+0x%xu' % e[1]
    if kind == 'const':
        return '0x%08xu' % e[1]
    if kind == 'load':
        return 'at<uint32_t>(0x%08xu)' % e[1]
    if kind == 'ret':
        return 'result'
    raise ValueError(e)


def generate(frames):
    lines = ['// Generated by android/tools/structor_frames.py from structor_frames.json:',
             '// structor frames read from the cartridge code (vtable stores, member and',
             '// base structors, __cxa_vec_cleanup, operator delete). Do not edit.',
             'namespace structor_frames {', '']
    for frame in frames:
        lines.append('// %s (module %d, 0x%08x)' % (frame['symbol'], frame['module'], frame['address']))
        lines.append('inline uint32_t f_%08x(uint32_t self) {' % frame['address'])
        uses_result = any('ret' in json.dumps(op) for op in frame['ops']) or frame['ret'] == ['ret']
        if uses_result:
            lines.append('    uint32_t result=0;')
        for op in frame['ops']:
            if op[0] == 'store':
                lines.append('    at<uint32_t>(%s)=%s;' % (expr(('self', op[1])), expr(op[2])))
            else:
                _, target, args = op
                sig = ','.join(['uint32_t']*len(args))
                call = 'Ptr<uint32_t(%s)>(0x%08xu)(%s)' % (sig, target, ','.join(expr(a) for a in args))
                lines.append('    %s%s;' % ('result=' if uses_result else '(void)', call))
        lines.append('    return %s;' % (expr(frame['ret']) if frame['ret'] else 'self'))
        lines.append('}')
    lines += ['', '// module -1: the resident main ARM9 module; otherwise the overlay id.',
              'inline void bind_structor_frames(packed::Space &space,int module) {']
    for frame in frames:
        lines.append('    if (module==%d) space.bind(0x%08xu,&f_%08x);' % (frame['module'], frame['address'], frame['address']))
    lines += ['}', '', '} // namespace structor_frames', '']
    OUTPUT.write_text('\n'.join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rom')
    ap.add_argument('--add', nargs='*', default=[])
    ap.add_argument('--recurse', action='store_true', help='also add unlinked structor callees')
    ap.add_argument('--generate', action='store_true')
    args = ap.parse_args()
    data = json.loads(FRAMES.read_text()) if FRAMES.exists() else {'frames': []}
    frames = {f['address']: f for f in data['frames']}
    if args.add:
        rom = Rom(args.rom)
        by_name, by_addr = frontier_batch.symbol_tables()
        linked, native = frontier_batch.linked_addresses()
        todo = list(args.add)
        failed = 0
        while todo:
            item = todo.pop(0)
            if re.fullmatch(r'0x[0-9a-fA-F]+|\d+', item):
                address = int(item, 0)
                symbol = by_addr.get(address, '0x%08x' % address)
            else:
                symbol = item
                address = by_name[symbol][0]
            # Overlays load at 0x020aa420 and above (FS_LoadOverlay bounds);
            # below is the resident main ARM9 module (-1; overlay 0 is ov000).
            module = -1 if address < 0x020aa420 else by_name[symbol][1]
            if address in frames:
                continue
            try:
                code = rom.read(address, 0x100, module)
                ops, ret = translate(code, address)
            except Reject as error:
                print('REJECT %-44s 0x%08x %s' % (symbol, address, error))
                failed += 1
                continue
            frames[address] = {'symbol': symbol, 'address': address, 'module': module, 'ops': ops, 'ret': ret}
            print('FRAME  %-44s 0x%08x %d ops' % (symbol, address, len(ops)))
            if args.recurse:
                for op in ops:
                    if op[0] != 'call':
                        continue
                    callee = by_addr.get(op[1])
                    if callee and frontier_batch.is_structor(callee) and op[1] not in linked \
                            and op[1] not in native and op[1] not in frames:
                        todo.append(callee)
                    # Array element destructors passed to __cxa_vec_cleanup.
                    for arg in op[2]:
                        name = by_addr.get(arg[1]) if arg[0] == 'const' else None
                        if name and frontier_batch.is_structor(name) and arg[1] not in linked \
                                and arg[1] not in native and arg[1] not in frames:
                            todo.append(name)
        data['frames'] = sorted(frames.values(), key=lambda f: f['address'])
        FRAMES.write_text(json.dumps(data, indent=1)+'\n')
        if failed:
            print('%d frame(s) need hand work' % failed)
    if args.add or args.generate:
        generate(sorted(frames.values(), key=lambda f: f['address']))


if __name__ == '__main__':
    main()
