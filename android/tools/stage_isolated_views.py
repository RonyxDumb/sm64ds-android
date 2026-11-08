"""Reviewed views of recovered isolated member units (external/sm64ds-pc/src).

Each unit is one original function whose calls are already spelled as their
mangled ABI symbols. Only the receiver changes: `this` becomes the cartridge
object address and named fields become their header-recorded offsets. The
body, branches and constants stay the recovered function's own.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCALARS = {'u8': 'u8', 's8': 's8', 'u16': 'u16', 's16': 's16', 'u32': 'u32',
           's32': 's32', 'int': 's32', 'Fix12i': 's32', 'short': 's16',
           'char': 's8', 'bool': 'u8', 'unsigned': 'u32', 's64': 's64', 'u64': 'u64'}
FIELD = re.compile(r'^\s+(?:unsigned\s+)?([A-Za-z_]\w*)\s*(\*?)\s*(\w+)\s*(\[\w+\])?\s*;\s*/\*\s*0x([0-9a-f]+)')


def header_fields(rel, minimum=0):
    fields = {}
    for line in (ROOT/rel).read_text().splitlines():
        match = FIELD.match(line)
        if not match:
            continue
        typ, pointer, name, array, offset = match[1], match[2], match[3], match[4], int(match[5], 16)
        if offset < minimum:
            continue
        if array:
            # Arrays decay to a typed element pointer at the member offset.
            if pointer or typ in SCALARS:
                fields[name] = ('[]'+('u32' if pointer else SCALARS[typ]), offset)
            continue
        if pointer:
            fields[name] = ('u32', offset)
        elif typ in SCALARS:
            fields[name] = (SCALARS[typ], offset)
    return fields


def player_fields():
    from stage_actor_views import ACTOR_FIELDS
    fields = dict(ACTOR_FIELDS)
    # Scalar dActor_c members the reviewed actor table does not spell yet
    # (mCamSpacePosX/Y/Z, mPrevAngleX, ...) come from the recovered header.
    for name, field in header_fields('external/sm64ds-pc/include/dActor_c.h').items():
        if field[1] < 0xd0:
            fields.setdefault(name, field)
    fields.update(header_fields('include/Player.h', 0xd0))
    return fields


def shared_declarations(raw):
    """Imports from the unit's decl_*.h headers, limited to names it uses."""
    text = re.sub(r'^\s*#[^\n]*', '', raw, flags=re.M)
    used = set(re.findall(r'\b\w+\b', text))
    # Names the unit declares itself keep the unit's own spelling.
    own = set(re.findall(r'^\s*extern\b[^;{]*?(\w+)\s*(?:\(|\[|;)', text, re.M))
    found = []
    for header in re.findall(r'#include "(decl_\w+\.h)"', raw):
        for rel in ('external/sm64ds-pc/include/'+header, 'include/'+header):
            if (ROOT/rel).exists():
                for line in re.findall(r'^extern [^;\n{}]+;', (ROOT/rel).read_text(), re.M):
                    name = re.search(r'(\w+)\s*(?:\(|\[|;)', line)
                    if name and name[1] in used and name[1] not in own:
                        found.append(line)
                break
    return '\n'.join(dict.fromkeys(found))


def vector_reference_abi(raw):
    """`const Vector3 &` parameters of extern C declarations are the ARM
    pointer word; `*(Vector3*)(base + off)` arguments of those calls pass
    the address itself."""
    if not re.search(r'extern "C"[^;\n]*Vector3\s*&', raw):
        return raw
    names = set()
    def decl(match):
        names.add(re.search(r'(\w+)\s*\(', match[0])[1])
        return re.sub(r'(?:const\s+)?(?:struct\s+)?Vector3\s*&\s*(\w*)', r'const void *\1', match[0])
    raw = re.sub(r'extern "C"[^;\n]*Vector3\s*&[^;\n]*;', decl, raw)
    for name in names:
        raw = re.sub(r'(\b'+name+r'\([^;]*?)\*\s*\(\s*(?:const\s+)?Vector3\s*\*\s*\)\s*\(([^()]*)\)', r'\1(\2)', raw)
        raw = re.sub(r'(\b'+name+r'\([^;]*?)\*\s*\(\s*(?:const\s+)?Vector3\s*\*\s*\)\s*\(([^()]*)\)', r'\1(\2)', raw)
    return raw


def isolated(projected, symbol, overlay, cls, fields, extra='', replacements=None, rel=None, name=None,
             types=None, prelude=''):
    rel = rel or 'external/sm64ds-pc/src/'+symbol+'.cpp'
    raw = (ROOT/rel).read_text()
    # Player::State records are 0x18 bytes of three PMFs; units that only
    # take their cartridge addresses declare them opaquely.
    raw = re.sub(r'(^|\n)struct State;[ \t]*', r'\1struct State { unsigned char pmf[0x18]; };', raw)
    raw = re.sub(r'extern\s+Player::State\s+(data_\w+)\s*;', r'extern char \1[];', raw)
    raw = vector_reference_abi(raw)
    if '::' in cls:
        # A nested class (Particle::SysTracker): drop the enclosing namespace
        # block so the member body is matched as Inner::method.
        outer, cls = cls.split('::')
        opened = re.search(r'^namespace '+outer+r'\s*\{\s*$', raw, re.M)
        if opened:
            raw = raw[:opened.start()]+raw[opened.end():]
            close = raw.rstrip().rfind('}')
            raw = raw[:close]+raw[close+1:]
    if name:
        from native_sources import function
        raw = function(raw, name)
        # In-body file-scope externs become ordinary declarations.
        hoisted = re.findall(r'^\s*(extern [^;\n(]+;)\s*$', raw, re.M)
        raw = re.sub(r'^\s*extern [^;\n(]+;\s*$\n', '', raw, flags=re.M)
        for old, new in (types or {}).items():
            hoisted = [re.sub(r'\b'+old+r'\b', new, line) for line in hoisted]
        raw = prelude+'extern "C" {\n'+'\n'.join(hoisted)+'\n}\n'+raw
    shared = shared_declarations(raw)
    raw = re.sub(r'^\s*#(?:include|pragma)[^\n]*\n', '', raw, flags=re.M)
    for old, new in (replacements or {}).items():
        raw = raw.replace(old, new)
    declarations = [shared] if shared else []
    for block in re.findall(r'extern "C" \{\n(.*?)\n\}\n', raw, re.S):
        declarations.append(block)
    raw = re.sub(r'extern "C" \{\n.*?\n\}\n', '', raw, flags=re.S)
    signature = re.search(r'^(\w[\w\s\*]*?[\s\*])\s*'+cls+r'::(\w+)\(([^)]*)\)\s*(?:const\s*)?\{', raw, re.M)
    if not signature:
        raise ValueError('no member body in '+rel)
    result = signature[1].strip()
    if '*' in result:
        result = 'void *'  # object pointers are opaque four-byte handles here
    params = signature[3].strip()
    # Reference parameters are the ARM pointer word: `T &x` becomes `T *x`,
    # `&x` the pointer itself and every other use of x its referent.
    references = []
    def pointer_param(param):
        found = re.fullmatch(r'(.*?)\s*&\s*(\w+)', param.strip())
        if not found:
            return param
        references.append(found[2])
        return found[1]+' *'+found[2]
    params = ', '.join(pointer_param(p) for p in params.split(',')) if params else params
    params = '' if params in ('', 'void') else ', '+params
    start = signature.start()
    tail = raw[signature.end():]
    for name in references:
        tail = re.sub(r'&\s*'+name+r'\b', '\x00'+name, tail)
        tail = re.sub(r'(?<![.>\w\x00])'+name+r'\b', '(*'+name+')', tail)
        tail = tail.replace('\x00'+name, name)
    body = raw[:start]+'extern "C" '+result+' '+symbol+'(void *object'+params+') {'+tail
    for old, new in (replacements or {}).items():
        body = body.replace(old, new)
    body = body.replace('((char*)this)', '((char *)object)').replace('(char*)this', '(char *)object')
    # Explicit member access through `this` names the same receiver fields.
    body = re.sub(r'\bthis->', '', body)
    body = re.sub(r'\bthis\b', 'object', body)
    if cls == 'Player':
        # Player.cpp members call their func_ov002_* siblings with the
        # implicit receiver; isolated units already pass it explicitly.
        body = player_self_calls(body, bare_members=rel.endswith('src/game/player/Player.cpp'))
    for name, (typ, offset) in fields.items():
        if typ.startswith('[]'):
            body = re.sub(r'(?<![.>\w])'+name+r'\b',
                          '(('+typ[2:]+' *)((char *)object+0x%x))' % offset, body)
            continue
        body = re.sub(r'(?<![.>\w])'+name+r'\b',
                      '(*('+typ+' *)((char *)object+0x%x))' % offset, body)
    # Older isolated units spell unnamed members unk_<offset>; take the type
    # the reviewed header records at that same offset.
    by_offset = {offset: typ for typ, offset in fields.values() if not typ.startswith('[]')}
    def unknown(match):
        offset = int(match[1], 16)
        if offset not in by_offset:
            raise ValueError('untyped member unk_%x in %s' % (offset, rel))
        return '(*('+by_offset[offset]+' *)((char *)object+0x%x))' % offset
    body = re.sub(r'(?<![.>\w])unk_([0-9a-f]+)\b', unknown, body)
    declarations = '\n'.join(declarations)
    # Local record types the imported declarations already name must precede
    # them: hoist top-level struct definitions of the unit into the prelude.
    local_types = ''
    records = list(re.finditer(r'^struct (\w+)\s*\{\n.*?^\};\n', body, re.M | re.S))
    records += list(re.finditer(r'^typedef struct\s*\w*\s*\{.*?^\}\s*(\w+);\n', body, re.M | re.S))
    for match in records:
        if re.search(r'\b'+match[1]+r'\b', declarations):
            local_types += match[0]
            body = body.replace(match[0], '', 1)
    # Opaque class handles: the bodies only pass these pointers through.
    opaque = sorted(set(re.findall(r'\b(\w+_c|Player)\b', declarations+body)) - {symbol})
    extra = ''.join('struct %s;\n' % name for name in opaque
                    if not re.search(r'struct\s+'+name+r'\s*\{', extra+body)) + extra
    for vector in ('struct Vector3 { s32 x,y,z; };\n', 'struct Vector3_16 { s16 x,y,z; };\n'):
        name = vector.split()[1]
        if re.search(r'\b'+name+r'\b', declarations+body) and not re.search(
                r'struct\s+'+name+r'\s*\{|\}\s*'+name+r'\s*;', extra+body+declarations):
            extra += vector
    row, bindings = projected(rel, body, declarations, [symbol], extra=extra+local_types, auto_globals=False)
    row['unit'] = 'stage_isolated_'+symbol
    for binding in bindings:
        binding['overlay'] = overlay
    return row, bindings


_PLAYER_METHODS = None


def player_methods():
    """Scalar-ABI Player member methods: bare name -> (mangled, ret, params)."""
    global _PLAYER_METHODS
    if _PLAYER_METHODS is None:
        from frontier_batch import symbol_tables
        by_name, _ = symbol_tables()
        scalar = r'(?:unsigned\s+)?(?:int|short|char|bool|u8|s8|u16|s16|u32|s32|Fix12i|void\s*\*|unsigned)'
        _PLAYER_METHODS = {}
        text = (ROOT/'include/Player.h').read_text()
        for match in re.finditer(r'^\s+(int|bool|void|u32|s32|u8|s8|u16|s16)\s+(\w+)\(([^()]*)\)\s*;', text, re.M):
            ret, name, params = match[1], match[2], match[3].strip()
            params = '' if params in ('', 'void') else params
            if params and not all(re.fullmatch(scalar+r'(?:\s+\w+)?', p.strip()) for p in params.split(',')):
                continue
            mangled = [n for n in by_name if n.startswith('_ZN6Player%d%sE' % (len(name), name))]
            if not mangled and name.startswith('func_ov002_') and name in by_name:
                mangled = [name]  # members still named by their cartridge symbol
            if len(mangled) == 1:
                _PLAYER_METHODS[name] = (mangled[0], 'int' if ret == 'bool' else ret, params)
    return _PLAYER_METHODS


def player_self_calls(body, bare_members=False):
    """Bare calls of the receiver's own member methods become their
    mangled cartridge symbols with the receiver as the first argument."""
    decls = []
    # Cartridge Player::State records passed by reference: opaque byte
    # arrays whose address is the ABI pointer (as free units normalise them).
    body = re.sub(r'extern\s+Player::State\s+(data_\w+)\s*;', r'extern char \1[];', body)
    for name, mangled in (('IsState', '_ZN6Player7IsStateERNS_5StateE'),
                          ('ChangeState', '_ZN6Player11ChangeStateERNS_5StateE')):
        pattern = r'(?<![.>\w:~])'+name+r'\s*\(\s*(data_\w+)\s*\)'
        if re.search(pattern, body):
            body = re.sub(pattern, mangled+r'(object, \1)', body)
            decls.append('extern "C" int %s(void *, void *);' % mangled)
    # Sound statics taking the dActor_c mCamSpacePos (+0x74) by reference.
    for name, mangled, scalars in (('PlayBank0', '_ZN5Sound9PlayBank0EjRK7Vector3', 1),
                                   ('PlayBank3', '_ZN5Sound9PlayBank3EjRK7Vector3', 1),
                                   ('PlayCharVoice', '_ZN5Sound13PlayCharVoiceEjjRK7Vector3', 2)):
        pattern = r'Sound::'+name+r'\(([^;]*?),\s*&?mCamSpacePos\s*\)'
        if re.search(pattern, body):
            body = re.sub(pattern, mangled+r'(\1, (void *)((char *)object + 0x74))', body)
            decls.append('extern "C" int %s(%s, void *);' % (mangled, ', '.join(['unsigned int']*scalars)))
    body = re.sub(r'&\s*mCamSpacePos\b', '(void *)((char *)object + 0x74)', body)
    if 'FX_MUL(' in body and '#define FX_MUL' not in body:
        decls.append('#define FX_MUL(v1, v2) ((s32)(((s64)(v1) * (v2) + 0x800LL) >> 12))')
    for name, (mangled, ret, params) in player_methods().items():
        if name in ('IsState', 'ChangeState') or (mangled == name and not bare_members):
            continue
        pattern = r'(?<![.>\w:~])'+name+r'\s*\('
        if not re.search(pattern, body) or re.search(r'\b'+name+r'\s*\([^)]*\)\s*\{', body):
            continue
        if re.search(r'extern[^;{}]*\b'+name+r'\s*\(', body) or re.search(
                r'^[ \t]*(?:extern\s+)?(?:const\s+)?(?:unsigned\s+|signed\s+)?\w+[\s\*]+'+name+r'\s*\([^;{]*\)\s*;', body, re.M):
            continue  # declared by the unit itself as a free call with explicit receiver
        body = re.sub(pattern+r'(\s*\))?', lambda m: mangled+('(object)' if m[1] else '(object, '), body)
        if mangled+'(' not in body.split('extern "C"', 1)[0]:
            decls.append('extern "C" %s %s(void *%s);' % (ret, mangled, ', '+params if params else ''))
    if decls:
        at = body.index('extern "C"')
        body = body[:at]+'\n'.join(decls)+'\n'+body[at:]
    return body


PLAYER_STATES = [
    '_ZN6Player12St_Dive_InitEv',
    '_ZN6Player12St_Wait_InitEv',
    '_ZN6Player11St_Fly_InitEv',
    '_ZN6Player11St_Owl_InitEv',
    '_ZN6Player11St_Owl_MainEv',
    '_ZN6Player12St_Bonk_InitEv',
    '_ZN6Player12St_Bonk_MainEv',
    '_ZN6Player12St_Fall_InitEv',
    '_ZN6Player12St_Fall_MainEv',
    '_ZN6Player12St_Hurt_InitEv',
    '_ZN6Player12St_Hurt_MainEv',
    '_ZN6Player12St_Jump_InitEv',
    '_ZN6Player12St_Jump_MainEv',
    '_ZN6Player12St_Land_InitEv',
    '_ZN6Player12St_Spin_InitEv',
    '_ZN6Player12St_Spin_MainEv',
    '_ZN6Player12St_Walk_InitEv',
    '_ZN6Player12St_Walk_MainEv',
    '_ZN6Player13St_Crawl_InitEv',
    '_ZN6Player13St_Crawl_MainEv',
    '_ZN6Player14St_Cannon_InitEv',
    '_ZN6Player14St_Crouch_InitEv',
    '_ZN6Player14St_Crouch_MainEv',
    '_ZN6Player14St_Owl_CleanupEv',
    '_ZN6Player15St_Grabbed_MainEv',
    '_ZN6Player15St_Hurt_CleanupEv',
    '_ZN6Player15St_Swallow_InitEv',
    '_ZN6Player15St_Wait_CleanupEv',
    '_ZN6Player16St_BackFlip_InitEv',
    '_ZN6Player16St_BurnFire_InitEv',
    '_ZN6Player16St_BurnFire_MainEv',
    '_ZN6Player16St_BurnLava_InitEv',
    '_ZN6Player16St_BurnLava_MainEv',
    '_ZN6Player16St_LongJump_MainEv',
    '_ZN6Player16St_SideFlip_InitEv',
    '_ZN6Player16St_SideFlip_MainEv',
    '_ZN6Player16St_WallJump_InitEv',
    '_ZN6Player16St_WallJump_MainEv',
    '_ZN6Player17St_ButtSlide_InitEv',
    '_ZN6Player17St_ButtSlide_MainEv',
    '_ZN6Player17St_HoldHeavy_InitEv',
    '_ZN6Player17St_HoldHeavy_MainEv',
    '_ZN6Player17St_HoldLight_InitEv',
    '_ZN6Player17St_LedgeGrab_InitEv',
    '_ZN6Player17St_LedgeGrab_MainEv',
    '_ZN6Player17St_LedgeHang_InitEv',
    '_ZN6Player17St_LedgeHang_MainEv',
    '_ZN6Player17St_PunchKick_MainEv',
    '_ZN6Player17St_SlopeJump_InitEv',
    '_ZN6Player17St_SlopeJump_MainEv',
    '_ZN6Player17St_SweepKick_InitEv',
    '_ZN6Player17St_SweepKick_MainEv',
    '_ZN6Player17St_WallSlide_InitEv',
    '_ZN6Player17St_WallSlide_MainEv',
    '_ZN6Player17St_WaterJump_InitEv',
    '_ZN6Player17St_WindCarry_InitEv',
    '_ZN6Player18St_Balloon_CleanupEv',
    '_ZN6Player18St_DizzyStars_InitEv',
    '_ZN6Player18St_DizzyStars_MainEv',
    '_ZN6Player18St_TurnAround_InitEv',
    '_ZN6Player18St_TurnAround_MainEv',
    '_ZN6Player19St_Electrocute_InitEv',
    '_ZN6Player19St_Electrocute_MainEv',
    '_ZN6Player19St_GroundPound_InitEv',
    '_ZN6Player19St_SwingPlayer_InitEv',
    '_ZN6Player19St_TornadoSpin_InitEv',
    '_ZN6Player20St_HoldLight_CleanupEv',
    '_ZN6Player20St_LedgeHang_CleanupEv',
    '_ZN6Player20St_StomachSlide_InitEv',
    '_ZN6Player21St_DizzyStars_CleanupEv',
    '_ZN6Player21St_HeadstandJump_InitEv',
    '_ZN6Player21St_JumpQuicksand_InitEv',
    '_ZN6Player21St_JumpQuicksand_MainEv',
    '_ZN6Player21St_SmallLaunchUp_InitEv',
    '_ZN6Player21St_SmallLaunchUp_MainEv',
    '_ZN6Player21St_WaitQuicksand_InitEv',
    '_ZN6Player21St_YoshiPower_CleanupEv',
    '_ZN6Player22St_GrabBowserTail_InitEv',
    '_ZN6Player22St_GrabBowserTail_MainEv',
    '_ZN6Player22St_SwingPlayer_CleanupEv',
    '_ZN6Player24St_SlideKickRecover_InitEv',
    '_ZN6Player25St_GrabBowserTail_CleanupEv',
]


VECTOR_TYPES = 'struct Vector3 { s32 x,y,z; }; struct Vector3_16 { s16 x,y,z; };\n'


def symbol_block(rel, symbol):
    """One `// @symbol` block of a multi-function recovered source file."""
    text = (ROOT/rel).read_text()
    start = text.index('// @symbol '+symbol+'\n')
    end = text.find('\n// @symbol ', start+1)
    return text[start:] if end < 0 else text[start:end+1]


def free_unit(projected, symbol, overlay, replacements=None, rel=None):
    """A recovered isolated free function: raw offsets, mangled callees."""
    if rel:
        raw = symbol_block(rel, symbol)
        # Blocks re-declare their own symbol inside the body; the definition
        # already declares it.
        raw = re.sub(r'^\s*extern [^;\n]*\b'+symbol+r'\s*\([^;]*;\n', '', raw, flags=re.M)
        # In-body externs are hoisted ahead of the stage typedefs: spell
        # their scalar types with the builtin names.
        def builtin(match):
            line = match[0]
            for old, new in (('u32', 'unsigned int'), ('s32', 'int'), ('Fix12i', 'int'),
                             ('u16', 'unsigned short'), ('s16', 'short'),
                             ('u8', 'unsigned char'), ('s8', 'signed char')):
                line = re.sub(r'\b'+old+r'\b', new, line)
            # Record pointers in hoisted declarations are opaque ARM words.
            builtins = {'void', 'char', 'int', 'short', 'long', 'unsigned', 'signed', 'float', 'double', 'extern', 'const'}
            line = re.sub(r'\b(?:struct\s+)?(\w+)(\s+const)?\s*\*',
                          lambda m: m[0] if m[1] in builtins else 'const void *' if m[2] else 'void *', line)
            return line
        raw = re.sub(r'^[ \t]+extern [^;\n]*;', builtin, raw, flags=re.M)
    else:
        for ext in ('.c', '.cpp'):
            rel = 'external/sm64ds-pc/src/'+symbol+ext
            if (ROOT/rel).exists():
                break
        raw = (ROOT/rel).read_text()
    if rel.endswith('.c'):
        # C units may name a parameter `this`; the projection is C++.
        raw = re.sub(r'\bthis\b', 'this_', raw)
        # C units spelling their own boolean enum: C++ already has them.
        raw = re.sub(r'^enum\s*\{\s*false\s*,\s*true\s*\}\s*;\s*$', '', raw, flags=re.M)
    raw = vector_reference_abi(raw)
    # These two recovered Player blocks declare Pair2i but never use it.
    # Remove only that dead local type; preserve the code and used layouts.
    if symbol in ('func_ov002_020c14b8', 'func_ov002_020c25a8'):
        raw, removed = re.subn(
            r'^[ \t]*typedef struct\s*\{[^{}]*\}\s*Pair2i\s*;\n',
            '', raw, count=1, flags=re.M)
        if removed != 1:
            raise ValueError('missing unused Pair2i typedef in '+symbol)
    # Player members still named func_ovNNN_<addr> carry a C++-mangled
    # spelling in some sources; the cartridge symbol is the bare name.
    raw = re.sub(r'\b_ZN6Player\d+(func_ov\d+_[0-9a-f]{8})E[A-Za-z0-9_]*', r'\1', raw)
    # Player::State is a 0x18-byte record of three PMFs; units name it opaquely
    # but take the address of its cartridge instances.
    raw = re.sub(r'(^|\n)struct State;[ \t]*', r'\1struct State { unsigned char pmf[0x18]; };', raw)
    # Volatile-qualified pointer cells only pin the ROM's reloads.
    raw = raw.replace('* volatile *', '**')
    # Empty inline destructors only pin the ROM's frame; they do nothing.
    raw = re.sub(r'\s*~\w+\(\)\s*\{\s*\}', '', raw)
    if 'Player::State' in raw:
        # Cartridge state records passed by reference: opaque byte arrays
        # whose address is the ABI pointer.
        raw = re.sub(r'Player::State\s*&\s*(\w*)', r'void *\1', raw)
        raw = re.sub(r'extern Player::State (data_\w+);', r'extern char \1[];', raw)
    if rel and '/actors/' in rel:
        # Record types the block only handles through pointers stay opaque.
        used = set(re.findall(r'\b([A-Z]\w*|d[A-Z]\w*)\s*\*', raw))
        known = {'Vector3', 'Vector3_16', 'State', 'SurfaceInfo', 'CLPS', 'Matrix4x3', 'Fix12i'}
        opaque = sorted(n for n in used - known
                        if not re.search(r'struct\s+'+n+r'\s*\{|\}\s*'+n+r'\s*;|typedef[^;]*\b'+n+r'\s*;', raw))
        raw = ''.join('struct %s;\n' % n for n in opaque) + raw
    if re.search(r'(?<!::)\bState\s*&', raw) and 'struct State {};' not in raw:
        # Player::State records by reference in plain declarations: the
        # ABI pointer, and the cartridge records passed by address.
        raw = re.sub(r'(?:struct\s+)?(?<!::)\bState\s*&\s*(\w*)', r'void *\1', raw)
        raw = re.sub(r'(_ZN6Player\d+(?:Change|Is)StateERNS_5StateE\(\s*[^,()]+,\s*)(data_\w+)', r'\1&\2', raw)
    if '->IsOnGround()' in raw:
        raw = re.sub(r'\(\(dBgCh_Actr \*\)(\([^()]*\))\)->IsOnGround\(\)',
                     r'_ZNK10dBgCh_Actr10IsOnGroundEv((void *)\1)', raw)
        raw = 'extern "C" int _ZNK10dBgCh_Actr10IsOnGroundEv(void *);\n'+raw
    if raw.lstrip('/cp\n').startswith('struct State {};') or '\nstruct State {};\n' in raw:
        # The by-reference Player state ABI: references are pointers, the
        # local Player shell's methods are their mangled symbols.
        raw = raw.replace('struct State {};', 'struct State { unsigned char pmf[0x18]; };', 1)
        raw = re.sub(r'struct Player \{[^}]*\};', 'struct Player;', raw, count=1)
        raw = raw.replace('State &', 'State *')
        raw = re.sub(r'(_ZN6Player\d+\w*StateE\w*\(\s*\w+\s*,\s*)(data_\w+)', r'\1&\2', raw)
        methods = {'GetHealth': ('_ZN6Player9GetHealthEv', 'int'),
                   'IsState': ('_ZN6Player7IsStateERNS_5StateE', 'int'),
                   'ChangeState': ('_ZN6Player11ChangeStateERNS_5StateE', 'int')}
        for name, (mangled, ret) in methods.items():
            if re.search(r'->'+name+r'\(', raw):
                raw = re.sub(r'(\w+)->'+name+r'\(\)', mangled+r'(\1)', raw)
                raw = re.sub(r'(\w+)->'+name+r'\((data_\w+)\)', mangled+r'(\1, &\2)', raw)
                if 'int '+mangled+'(' not in raw:
                    decl = 'extern "C" '+ret+' '+mangled+('(void *);' if name=='GetHealth' else '(void *, State *);')
                    raw = raw.replace('struct Player;', 'struct Player;\n'+decl, 1)
    shared = shared_declarations(raw)
    raw = re.sub(r'^\s*#(?:include|pragma)[^\n]*\n', '', raw, flags=re.M)
    for old, new in (replacements or {}).items():
        raw = raw.replace(old, new)
    if shared:
        raw = shared+'\n'+raw
    extra = ''
    if not re.search(r'struct Vector3\s*\{|\}\s*Vector3\s*;', raw):
        extra += 'struct Vector3 { s32 x,y,z; };\n'
    if not re.search(r'struct Vector3_16\s*\{|\}\s*Vector3_16\s*;', raw):
        extra += 'struct Vector3_16 { s16 x,y,z; };\n'
    row, bindings = projected(rel, raw, '', [symbol], extra=extra, auto_globals=False)
    row['unit'] = 'stage_isolated_'+symbol
    for binding in bindings:
        binding['overlay'] = overlay
    return row, bindings


FREE_UNITS = [
    ('func_ov002_020dd2f4', 2),
    ('func_ov002_020dc560', 2),
    ('func_0207322c', 0),
    ('func_ov002_020e0ccc', 2),
    ('func_ov002_020dba0c', 2),
    ('func_ov002_020dc174', 2),
    ('func_0201226c', 0),
    ('func_02048c30', 0, {
        # Out of range the ROM stops the voice and then sets its volume from
        # an unassigned register; the stopped voice makes the value moot,
        # zero keeps the host deterministic.
        '    int t;\n': '    int t = 0;\n'}),
    ('func_020116c4', 0),
    ('func_02011ac4', 0, {
        # K&R declarations of the slot initialiser and the SND3D update.
        'extern int func_02011b38();': 'extern int func_02011b38(void *, int, int, int, short);',
        'extern int func_02048c30();': 'extern int func_02048c30(void *, int, int);'}),
    ('func_ov002_020e04a4', 2),
    ('func_0200fccc', 0),
    ('func_0200fc44', 0),
    ('func_ov002_020e2c84', 2),
    ('func_ov002_020da9d4', 2),
    ('func_ov002_020db54c', 2),
    ('func_0200d8c8', 0, {
        # cstd::fdiv (0x02053258), named in the unit's header comment.
        'extern Fix12i Vec3_Dist(': 'extern int _ZN4cstd4fdivEii(int, int);\nextern Fix12i Vec3_Dist('}),
    ('func_ov002_020e301c', 2),
    ('func_ov002_020e0a64', 2),
    ('func_ov002_020bd45c', 2, {
        # Sound::PlayBank0's reference parameter is the ARM pointer word.
        'namespace Sound{ void PlayBank0(unsigned int, const Vector3 &); }':
            'extern "C" void _ZN5Sound9PlayBank0EjRK7Vector3(unsigned int, const void *);',
        'Sound::PlayBank0(v, *(Vector3*)(c+0x74));': '_ZN5Sound9PlayBank0EjRK7Vector3(v, c+0x74);'}),
    ('func_ov002_020e1e70', 2),
    ('func_02012120', 0),
    ('func_ov002_020e2664', 2),
    ('func_ov002_020d674c', 2),
    ('func_ov002_020dde74', 2),
    ('func_ov002_020e28d4', 2),
    ('func_ov002_020e25f0', 2),
    ('func_ov002_020e2ad0', 2),
    ('func_ov002_020e2b6c', 2),
    ('func_ov002_020e2be4', 2),
    ('func_ov002_020bd3a0', 2),
    ('ReadUnalignedUshort', 0),
    ('func_ov002_020f035c', 2),
    ('func_ov002_020d4748', 2),
    ('func_ov002_020d4c30', 2, {
        # Sound::PlayBank0's reference parameter is the ARM pointer word.
        'struct Sound {\n    static void PlayBank0(unsigned int id, const Vector3 &v);\n};':
            'extern "C" void _ZN5Sound9PlayBank0EjRK7Vector3(unsigned int id, const Vector3 *v);',
        'Sound::PlayBank0(0xb3, self->p74);': '_ZN5Sound9PlayBank0EjRK7Vector3(0xb3, &self->p74);',
        'Sound::PlayBank0(self->p66c + 0x70, self->p74);': '_ZN5Sound9PlayBank0EjRK7Vector3(self->p66c + 0x70, &self->p74);',
        'Sound::PlayBank0(0xa4, self->p74);': '_ZN5Sound9PlayBank0EjRK7Vector3(0xa4, &self->p74);',
        # The namespace-local declarations only pin mwccarm's call shape.
        '    struct System {\n        static void NewSimple(unsigned int id, Fix12i a, Fix12i b, Fix12i c);\n    };\n': '',
        'Particle::_ZN8Particle6System9NewSimple': '_ZN8Particle6System9NewSimple',
        'namespace Particle {\n': '', '\n}\n\nstruct Obj {': '\nstruct Obj {'}),
    ('func_0201251c', 0),
    ('func_02048a18', 0),
    ('func_02048af4', 0),
    ('func_0203da4c', 0),
    ('func_ov085_0212e59c', 85),
    ('func_02011ee4', 0, {
        # K&R declarations: NNS_SndHeapLoadState(heap, level) and the group
        # loader (this, heap), as func_02011f7c declares them.
        'extern int func_020510a4();': 'extern int func_020510a4(int, int);',
        'extern int func_020134d8();': 'extern int func_020134d8(void *, int);'}),
    ('func_02011f7c', 0),
    ('func_ov002_020bd4e0', 2),
    ('func_0203db0c', 0),
    ('func_ov002_020d413c', 2),
    ('func_02010844', 0),
    ('func_ov002_020d4d88', 2),
    ('func_ov002_020d45c0', 2),
    ('func_02022b04', 0),
    ('_ZN8Particle20RunningSlidingDustAtE5Fix12IiES1_S1_', 0, {
        # Particle (include/Particle.h): mRunningSlidingDustSystemID +0x750,
        # mRunningSlidingDustCallback +0x754.
        '#include "Particle.h"\n': '', 'extern struct Particle* data_0209ee74;': 'extern char *data_0209ee74;',
        'data_0209ee74->mRunningSlidingDustSystemID =': '*(u32 *)(data_0209ee74 + 0x750) =',
        '            data_0209ee74->mRunningSlidingDustSystemID,': '            *(u32 *)(data_0209ee74 + 0x750),',
        '(void*)&data_0209ee74->mRunningSlidingDustCallback': '(void*)(data_0209ee74 + 0x754)'}),
    ('func_ov002_020d5c6c', 2),
    ('func_ov002_020d600c', 2),
    ('func_ov002_020dd824', 2),
    ('func_ov002_020bd20c', 2),
    ('func_ov002_020bd06c', 2),
    ('func_ov002_020f63a0', 2),
    ('func_ov002_020f5f60', 2),
    ('func_ov002_020f23d0', 2),
    ('func_ov002_020f6a9c', 2),
    ('func_ov002_020f7384', 2),
    ('func_ov002_020f79c0', 2),
    ('func_ov002_020f6b28', 2),
    ('func_ov002_020f65b8', 2),
    ('func_02048d80', 0),
    ('func_ov002_020f6ae4', 2),
    ('func_ov002_020f26c4', 2),
    ('_ZN8dActor_c10FindWithIDEj', 0, {
        # A static member (include/dActor_c.h): no receiver; the id is r0.
        'dActor_c *dActor_c::FindWithID(u32 id)': 'extern "C" dActor_c *_ZN8dActor_c10FindWithIDEj(u32 id)',
        'extern "C" {\nextern dActor_c *data_0209b468;': 'struct dActor_c;\nextern "C" {\nextern dActor_c *data_0209b468;'}),
    ('func_02008200', 0),
    ('func_02011698', 0),
    ('func_020393bc', 0),
    ('func_ov002_020bd4ac', 2),
    ('func_020485f0', 0),
    ('func_ov002_020d4540', 2),
    ('func_ov002_020d718c', 2),
    ('func_ov002_020d8118', 2),
    ('func_ov002_020d853c', 2),
    ('func_ov002_020d8944', 2),
    ('_ZN8Particle6System9NewSimpleEj5Fix12IiES2_S2_', 0, {
        # Particle::mManager is the word at +0x004 (include/Particle.h).
        '#include "Particle.h"\n': '', 'extern struct Particle* data_0209ee74;': 'extern void *data_0209ee74;',
        'data_0209ee74->mManager': '*(void **)((char *)data_0209ee74 + 4)'}),
    ('func_02011b38', 0, {
        # The K&R declaration stands for Player_PlaySoundEffect(handle, kind, id).
        'extern int Player_PlaySoundEffect();': 'extern int Player_PlaySoundEffect(void *, int, int);'}),
    ('Vec3_LslInPlace', 0),
    ('Vec3_Add', 0),
    ('Vec3_MulScalar', 0),
    ('Math_Function_0203b0fc', 0),
    ('func_ov002_020f7538', 2, {
        # The registered destructor is the original Vector3 D1 code handle.
        '(int)_ZN7Vector3D1Ev': '(int)0x020072c0', 'extern void _ZN7Vector3D1Ev(void);\n': ''}),
    ('func_ov002_020f7410', 2, {
        # `register` is not C++17; the 16-bit angle store keeps its ROM value.
        'register int *p': 'int *p',
        '*(short *)(self + 0x8e) = 0xffffb3c0;': '*(short *)(self + 0x8e) = (short)-0x4c40;'}),
    ('func_0201ef38', 0),
    ('func_ov002_020f6618', 2, {
        # The unit's local SharedFilePtr {int a, file;} is the shared 8-byte
        # record (file pointer at +4); references are pointers in the ABI.
        'struct SharedFilePtr { int a, file; };\n': '',
        '(SharedFilePtr& f)': '(SharedFilePtr* f)',
        'void* _Znwj(unsigned int sz);': 'void* _Znwj(unsigned int sz);\nvoid _ZN15TextureSequence7PrepareER8BMD_FileR8BTP_File(void*, void*);',
        '_ZN5Model8LoadFileER13SharedFilePtr(**(SharedFilePtr**)(self + 0x70))':
            '_ZN5Model8LoadFileER13SharedFilePtr(*(SharedFilePtr**)(self + 0x70))',
        '_ZN9Animation8LoadFileER13SharedFilePtr(*(*(SharedFilePtr***)(self + 0x74))[i])':
            '_ZN9Animation8LoadFileER13SharedFilePtr((*(SharedFilePtr***)(self + 0x74))[i])',
        '(void*)(*(*(SharedFilePtr***)(self + 0x74))[0]).file':
            '(void*)(*(SharedFilePtr***)(self + 0x74))[0]->filePtr',
        '(void*)(*(SharedFilePtr**)(self + 0x70))->file': '(void*)(*(SharedFilePtr**)(self + 0x70))->filePtr',
        '_ZN15TextureSequence8LoadFileER13SharedFilePtr(*(*(SharedFilePtr***)(self + 0x78))[i])':
            '_ZN15TextureSequence8LoadFileER13SharedFilePtr((*(SharedFilePtr***)(self + 0x78))[i])',
        'TextureSequence::Prepare(*(BMD_File*)bmd, *(BTP_File*)btp);':
            '_ZN15TextureSequence7PrepareER8BMD_FileR8BTP_File(bmd, btp);',
        '(void*)(*(*(SharedFilePtr***)(self + 0x78))[0]).file':
            '(void*)(*(SharedFilePtr***)(self + 0x78))[0]->filePtr'}),
    ('func_ov002_020f7020', 2),
    ('func_ov002_020f6960', 2, {
        # A reference parameter is a pointer in the ARM ABI.
        'LoadFileER13SharedFilePtr(SharedFilePtr&)': 'LoadFileER13SharedFilePtr(SharedFilePtr*)',
        '_ZN5Model8LoadFileER13SharedFilePtr(*(*(SharedFilePtr**)(self + 0x5c)))':
            '_ZN5Model8LoadFileER13SharedFilePtr(*(SharedFilePtr**)(self + 0x5c))'}),
    ('func_ov002_020f6a50', 2, {
        # Callbacks are the original code addresses (Vector3 C1/D1), not
        # zero-argument host veneers.
        '(void*)func_0203d384, (void*)_ZN7Vector3D1Ev': '(void*)0x0203d384u, (void*)0x020072c0u',
        'extern void _ZN7Vector3D1Ev(void);\n': '', 'extern void func_0203d384(void);\n': ''}),
    ('Vec3_Sub', 0),
    ('func_020089f8', 0),
    ('func_02009414', 0),
    ('func_020094a8', 0),
    ('func_020082a0', 0),
    ('func_ov002_020bd600', 2),
    ('func_0200e5fc', 0, {
        # Same volatile byte read-modify-write; the packed reference has no +=.
        '*pb += 1;': '*pb = *pb + 1;'}),
    ('func_0200e6d8', 0),
    ('func_0200e5ac', 0),
    ('func_02013524', 0),
    ('func_02013078', 0),
    ('func_02049764', 0),
    ('func_02011dcc', 0),
    ('func_ov002_020f2630', 2),
    ('func_0200e4e4', 0),
    ('func_02043f98', 0),
    ('func_0200e55c', 0),
    ('func_0200e494', 0),
    ('RunKuppaScript', 0),
    ('StartIntroCutscene', 0),
    ('IsStarCollected', 0), ('IsStarCollectedInLevel', 0), ('IsStarCollectedInCurLevel', 0),
    ('SetStarMarker', 0), ('ContinueKuppaScriptIfNecessary', 0),
    ('func_0202a958', 0), ('func_0202a96c', 0), ('func_0202a980', 0),
    ('GetMinimapID', 0), ('GetMinimapScale', 0),
    ('Player_PlaySoundEffect', 0),
    ('func_02049018', 0),
    ('func_ov002_020e640c', 2),
    ('func_020167a4', 0),
    ('func_020162a4', 0),
    ('func_0201628c', 0),
    ('func_ov002_020e4990', 2),
    ('func_ov002_020e3f90', 2),
    ('ApproachAngle', 0),
    ('func_ov002_020e4374', 2),
    ('func_ov002_020e444c', 2),
    ('Color_Interp', 2),
    ('func_ov002_020d71a0', 2),
    ('Player_ReleaseHeldActor', 2),
    ('func_ov002_020e6a78', 2),
    ('func_ov002_020e69c0', 2),
    ('func_ov002_020e6b3c', 2),
    ('func_ov002_020e6b74', 2),
    ('func_ov002_020d869c', 2),
    ('func_020105cc', 0),
    ('func_ov002_020d8d10', 2),
    ('func_ov002_020db5f4', 2),
    ('func_ov002_020db8bc', 2),
    ('func_ov002_020d9298', 2),
    ('func_ov002_020d8360', 2),
    ('func_ov002_020d85fc', 2),
    ('func_ov002_020eea84', 2),
    ('func_ov002_020d06c0', 2),
    ('func_ov002_020d0178', 2),
    ('func_02022d00', 0),
    ('func_02022a4c', 0),
    ('func_ov002_020e3078', 2),
    ('func_02059650', 0),
    ('func_0200d858', 0),
    ('func_ov002_020e0478', 2),
    ('func_ov002_020d5338', 2),
    ('func_ov002_020dec70', 2),
    ('func_0203abcc', 0),
    ('func_0203ac00', 0),
    ('func_0203abd4', 0),
    ('func_020393cc', 0),
    ('func_02039794', 0),
    ('func_02038298', 0),
    ('func_02038234', 0),
    ('func_020381cc', 0),
    ('func_020380c0', 0),
    ('func_02037fc0', 0),
    ('func_02037f94', 0),
    ('func_02037f64', 0),
    ('func_02037f5c', 0),
    ('func_02037f54', 0),
    ('func_02037f44', 0),
    ('func_02037e9c', 0),
    ('func_02037e2c', 0),
    ('func_02037e14', 0),
    ('func_02037de8', 0),
    ('func_02037dc4', 0),
    ('func_02037b5c', 0),
    ('func_02037b1c', 0),
    ('func_02037a6c', 0),
    ('func_02037a38', 0),
    ('func_02037a04', 0),
    ('func_020379f4', 0),
    ('func_020379d0', 0),
    ('func_020379c0', 0),
    ('func_0203799c', 0),
    ('func_0203798c', 0),
    ('func_02037968', 0),
    ('func_0203794c', 0),
    ('func_02037940', 0),
    ('func_02037938', 0),
    ('func_020378dc', 0),
    ('func_02037888', 0),
    ('func_02037880', 0),
    ('func_0203782c', 0),
    ('func_02037608', 0),
    ('func_020374d4', 0),
    ('func_020374b8', 0),
    ('func_020371b0', 0),
    ('func_02036acc', 0),
    ('func_02035860', 0),
    ('func_02035800', 0),
    ('func_02035784', 0),
    ('func_02035764', 0),
    ('func_0203573c', 0),
    ('func_020356d4', 0),
    ('func_020356c8', 0),
    ('func_0203567c', 0),
    ('func_0203564c', 0),
    ('func_02035638', 0),
    ('func_020355fc', 0),
    ('func_020355dc', 0),
    ('func_020355c8', 0),
    ('func_020355b4', 0),
    ('func_020355a0', 0),
    ('func_0203558c', 0),
    ('func_02035550', 0),
    ('func_0203553c', 0),
    ('func_020354b0', 0),
    ('func_0203547c', 0),
    ('func_0203545c', 0),
    ('func_0203543c', 0),
    ('func_02035408', 0),
    ('func_020353d4', 0),
    ('func_020353b0', 0),
    ('func_020355e8', 0),
    ('func_020353f4', 0),
    ('func_020357c0', 0),
    ('func_020353e0', 0),
    ('func_020357a0', 0),
    ('func_02035770', 0),
    ('func_020357e0', 0),
    ('func_02037318', 0),
    ('func_02035798', 0),
    ('func_ov002_020d91b8', 2),
    ('func_0201179c', 0),
    ('func_020123c8', 0),
    ('func_02011934', 0),
    ('func_02048a1c', 0),
    ('func_ov002_020d91e0', 2),
    ('func_ov002_020d82f0', 2),
    ('func_02037e58', 0),
    ('func_02037e84', 0),
    ('func_02037e68', 0),
    ('func_02037e48', 0),
    ('func_02037e38', 0),
    ('func_02037e90', 0),
    ('func_02037e20', 0),
    ('SurfaceInfo_TestFlag0x20', 0),
    ('func_020375ec', 0),
    ('func_02035394', 0),
    ('func_ov002_020d36d8', 2),
    ('_ZN8Particle6System3NewEjj5Fix12IiES2_S2_PK11Vector3_16fPNS_8CallbackE', 0),
    ('func_ov002_020d2f24', 2),
    ('func_ov002_020d2da0', 2),
    ('func_ov002_020d22ec', 2),
    ('func_ov002_020d2fdc', 2),
    ('func_ov002_020bcdf0', 2),
    ('GetPlayerFlagByte', 0),
    ('IsPlayerWarping', 0),
    ('func_ov002_020e4bb8', 2),
    ('func_ov002_020db704', 2),
    ('func_ov002_020d8158', 2),
    ('GetAngleToCamera', 0),
    ('func_ov002_020e032c', 2),
    ('func_ov002_020d80d0', 2),
    ('DecIfAbove0_Short', 0),
    ('func_02035684', 0),
    ('func_0203568c', 0),
    ('func_02012694', 0),
    ('func_ov002_020bd438', 2),
    ('func_ov002_020bd4c8', 2),
    ('func_0201b388', 0),
    ('_ZN7Message7AddCharEc', 0, {'void Message::AddChar(': 'extern "C" void _ZN7Message7AddCharEc(', 'struct Message {\n    static void AddChar(char c);\n};\n': ''}),
    ('func_0201b7cc', 0),
    ('func_0201f138', 0),
    ('func_ov100_021452e4', 100),
    ('func_ov100_02144cf8', 100),
    ('func_ov002_020f65ec', 2),
    ('func_ov002_020f64ac', 2),
    ('func_ov002_020f20f4', 2),
    ('func_ov002_020f23f0', 2),
    ('Vec3_DivScalarInPlace', 0),
    ('Vec3_MulScalarInPlace', 0),
    ('func_0203dafc', 0),
    ('IsAreaShowing', 0),
    ('func_0204302c', 0),
    ('func_0203b27c', 0),
    ('func_0203b3c0', 0),
    ('func_0203db3c', 0),
    ('func_ov001_020aa960', 1),
    ('_ZN7Clipper13Func_020150E8ER7Vector35Fix12IiEPh', 0, {
        # Clipper.h layout: vptr, four plane normals at 0x04, near/far 0x50/0x54.
        '#pragma opt_propagation off\n': '',
        'extern "C" int _ZN7Clipper13Func_020150E8ER7Vector35Fix12IiEPh(':
            'struct ClipperPlane { s32 x; s32 y; s32 z; };\n'
            'struct Clipper { u8 pad_000[4]; ClipperPlane mPlaneNormals[4]; s32 mAspectRatio;\n'
            '    s32 mNearZ; s32 mFarZ; u16 mFovAngle; };\n'
            'extern "C" int _ZN7Clipper13Func_020150E8ER7Vector35Fix12IiEPh('}),
    ('_ZN7Minimap21FixTHIPaintingRoomPosER7Vector3', 2, {
        # A static member: its reference parameter is the ARM pointer word.
        'void Minimap::FixTHIPaintingRoomPos(Vector3 & v_)\n{\n    struct Vector3* v = &v_;':
            'extern "C" void _ZN7Minimap21FixTHIPaintingRoomPosER7Vector3(struct Vector3 *v)\n{'}),
    ('_ZN7Minimap15GetPosOnMinimapER7Vector3S1_5Fix12IiEsS1_', 2),
    ('_ZN7Minimap20GetPosFromMinimapPosER7Vector3S1_5Fix12IiEsS1_', 2, {
        # References are ARM pointer words.
        'struct Vec3& a, struct Vec3& b, int c, short d, struct Vec3& e':
            'struct Vec3* a, struct Vec3* b, int c, short d, struct Vec3* e',
        'Vec3_Sub(&local, &a, &data_ov002_0211116c);': 'Vec3_Sub(&local, a, &data_ov002_0211116c);',
        'Vec3_RotateYAndTranslate(&e, &b, d, &local);': 'Vec3_RotateYAndTranslate(e, b, d, &local);'}),
    ('_ZN3OAM6RenderEbP7OamAttriiii5Fix12IiES3_ii', 0, {
        # The stage prelude already defines Matrix2x2 as an int[4] record.
        '#pragma opt_common_subs off\n': '', '#pragma opt_propagation off\n': '',
        '#pragma opt_loop_invariants off\n': '',
        'struct Matrix2x2 { int a, b, c, d; };': 'struct OamAffine2 { int a, b, c, d; };',
        'struct Matrix2x2 *m);': 'struct OamAffine2 *m);',
        '    struct Matrix2x2 m;': '    struct OamAffine2 m;'}),
    # Static OAM size-table lookups: free functions in the ARM ABI.
    ('_ZN3OAM11GetObjWidthEii', 0, {
        'u8 OAM::GetObjWidth(int shape, int sizeBits)': 'extern "C" u8 _ZN3OAM11GetObjWidthEii(int shape, int sizeBits)',
        # The same byte, read without binding an int reference at a byte offset.
        'return *(unsigned char *)&((int *)((char *)data_020755a0 + sizeBits))[shape];':
            'return *((unsigned char *)data_020755a0 + sizeBits + shape * 4);'}),
    ('_ZN3OAM12GetObjHeightEii', 0, {
        'u8 OAM::GetObjHeight(int shape, int sizeBits)': 'extern "C" u8 _ZN3OAM12GetObjHeightEii(int shape, int sizeBits)',
        'return *(unsigned char *)&((int *)((char *)data_020755ac + sizeBits))[shape];':
            'return *((unsigned char *)data_020755ac + sizeBits + shape * 4);'}),
    ('func_ov002_020f2210', 2, {
        # The same volatile halfword read-modify-write; the packed reference has no -=.
        '*(volatile u16 *)LP(p + 0x12) -= 1;': '*(volatile u16 *)LP(p + 0x12) = *(volatile u16 *)LP(p + 0x12) - 1;'}),
    ('_ZN7Message6UpdateEv', 0, {
        # Static Message::Update: no receiver in the ARM ABI.
        'class Message {\npublic:\n    void Update();\n};\n\nvoid Message::Update()':
            'extern "C" void _ZN7Message6UpdateEv(void)'}),
    ('func_ov002_020e3e00', 2, {
        # The prelude defines the real Model record; the unit only passes it.
        'struct Model { };\n': ''}),
    ('func_ov002_020bd480', 2, {
        # Sound::PlayCharVoice through its ABI symbol; the reference is a pointer.
        'namespace Sound{ void PlayCharVoice(unsigned int, unsigned int, const Vector3 &); }':
            'extern "C" unsigned int _ZN5Sound13PlayCharVoiceEjjRK7Vector3(unsigned int, unsigned int, const Vector3 *);',
        'Sound::PlayCharVoice(c[0x6d9], v, *(Vector3*)(c+0x74));':
            '_ZN5Sound13PlayCharVoiceEjjRK7Vector3(c[0x6d9], v, (Vector3*)(c+0x74));'}),
    ('func_02011af8', 0, {
        # C's unprototyped declarations, spelled with the callees' real ABI.
        'extern int func_02011b38();': 'extern int func_02011b38(void *c, int a1, int a2, int a3, short s1);',
        'extern int func_020123c8();': 'extern int func_020123c8(void *p, int a, int b, int pos);'}),
    ('func_ov002_020f2340', 2, {
        # OAM::RenderSub through its mangled ABI symbol.
        'struct OamAttr;\nstruct OAM { static void RenderSub(OamAttr *, int, int); };\nextern OamAttr data_ov002_0210bddc;':
            'extern "C" void _ZN3OAM9RenderSubEP7OamAttrii(void *, int, int);\nextern char data_ov002_0210bddc[];',
        'OAM::RenderSub(&data_ov002_0210bddc,': '_ZN3OAM9RenderSubEP7OamAttrii(data_ov002_0210bddc,'}),
    ('UpdateMinimap', 0, {
        # The stage prelude already names a different MinimapState record.
        'MinimapState': 'UpdateMinimapState'}),
    ('func_ov001_020aaa54', 1, {
        # CapIcon.h's record layout (vptr 0x00 .. mFlags 0x1b); the unit
        # only reads and writes these fields.
        'extern dCapIcon_c* data_ov001_020ad634[];':
            'struct dActor_c;\nstruct dCapIcon_c { void *vptr; struct dActor_c *mOwner; u32 mOwnerUniqueID;\n'
            '    struct dCapIcon_c *mPrev; struct dCapIcon_c *mNext; s32 mSlot;\n'
            '    u8 mCharacter; u8 unk_19; u8 unk_1a; u8 mFlags; };\n'
            'extern dCapIcon_c* data_ov001_020ad634[];\nvoid _ZN10dCapIcon_c6UnlinkEv(dCapIcon_c *);',
        'sl->Unlink();': '_ZN10dCapIcon_c6UnlinkEv(sl);'}),
    ('func_ov001_020aaf40', 1, {
        # C's local false/true enum; C++ already has the keywords.
        'enum { false, true };\n': ''}),
    ('func_020391f0', 0),
    ('func_0206e28c', 0),
    ('func_02023178', 0),
    ('func_02023408', 0),
    ('LoadDebugFont', 0),
    ('func_ov002_020f6bc0', 2),
    ('Vec3_HorzLen', 0),
    ('Vec3_Equal', 0),
    ('func_ov002_020effb8', 2),
    ('func_0200af0c', 0),
    ('func_0202ed14', 0),
    ('func_0202fc40', 0),
    ('func_02059ba0', 0),
    ('func_0201aad4', 0),
    ('func_0201aa18', 0),
    ('func_0201964c', 0),
    ('func_02011a5c', 0),
    ('func_02008b4c', 0),
    ('func_ov002_020f7bb8', 2),
]


# Free functions taken from one `// @symbol` block of a multi-function
# recovered file (raw-offset C): (symbol, overlay, file, optional replacements).
BLOCK_UNITS = [
    ('func_ov002_020bf90c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c0688', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf56c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c06fc', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf88c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf9d4', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c5dec', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf27c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf2d8', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c04ac', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf224', 2, 'external/sm64ds-pc/src/actors/Player.cpp', {
        # Entry18 (Player.cpp file scope): 0x18-byte controller records whose
        # s16 val leads each record.
        'extern struct Entry18 data_0209f4a0[];': 'extern short data_0209f4a0[];',
        'data_0209f4a0[data_020a0e40].val': 'data_0209f4a0[data_020a0e40 * 12]'}),
    ('func_ov002_020bf5e0', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c18b0', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c04e0', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c0434', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c44c4', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020caf68', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bdc18', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bdb50', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020be3b0', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c29d4', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c2b08', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bd984', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c61ac', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c5d60', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c031c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('Player_ScaleByCharFactor', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020be008', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020ce9c8', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c0cbc', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c0d90', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bfec0', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf40c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c5d0c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c0fb4', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c1234', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c25a8', 2, 'external/sm64ds-pc/src/actors/Player.cpp', {
        # The ARM body falls through with r0 == ret (0) from the last call.
        '    ret = func_ov002_020c2138(c);\n    if (ret != 0)\n        return ret;\n}\n':
            '    ret = func_ov002_020c2138(c);\n    if (ret != 0)\n        return ret;\n    return ret;\n}\n'}),
    ('func_ov002_020c231c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c133c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c14b8', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c16ec', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020ce798', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020cabe0', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c6adc', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c5244', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c0364', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020c2db8', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf13c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020bf36c', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020ce8bc', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
    ('func_ov002_020ca940', 2, 'external/sm64ds-pc/src/actors/Player.cpp'),
]


# Members projected from the recovered class sources in src/ (symbol,
# overlay, class, source file, exact definition head, optional replacements).
SOURCE_MEMBERS = [
    ('_ZN6Player4HealEi', 2, 'Player', 'external/sm64ds-pc/src/actors/Player.cpp', 'int Player::Heal(int amt)'),
    ('func_ov002_020c71e0', 2, 'Player', 'src/game/player/Player.cpp', 'void Player::func_ov002_020c71e0()'),
    ('func_ov002_020c2f64', 2, 'Player', 'src/game/player/Player.cpp', 'void Player::func_ov002_020c2f64()'),
    ('func_ov002_020c70ac', 2, 'Player', 'src/game/player/Player.cpp', 'void Player::func_ov002_020c70ac()'),
    ('func_ov002_020c0108', 2, 'Player', 'src/game/player/Player.cpp', 'int Player::func_ov002_020c0108(int p1)', {
        # The in-body externs are hoisted ahead of the body's local typedefs.
        'void _ZN7PathPtr6FromIDEj(void*, u32 id);': 'void _ZN7PathPtr6FromIDEj(void*, unsigned int id);',
        'void _ZNK7PathPtr7GetNodeER7Vector3j(void*, void*, u32 idx);': 'void _ZNK7PathPtr7GetNodeER7Vector3j(void*, void*, unsigned int idx);',
        'extern u32 _ZNK7PathPtr9GetUnk004Ev(void*);': 'extern unsigned int _ZNK7PathPtr9GetUnk004Ev(void*);',
        'extern s32 data_ov002_020ff200[];': 'extern int data_ov002_020ff200[];',
        'extern s16 data_02082214[];': 'extern short data_02082214[];',
        'extern int Vec3_HorzAngle(const Vector3 *v0, const Vector3 *v1);': 'extern int Vec3_HorzAngle(const void *v0, const void *v1);',
        'extern int Vec3_VertAngle(const Vector3 *v1, const Vector3 *v0);': 'extern int Vec3_VertAngle(const void *v1, const void *v0);'}),
    ('_ZN6Player7IsInAirEv', 2, 'Player', 'src/game/player/Player.cpp', 'int Player::IsInAir()'),
    ('func_ov002_020c4188', 2, 'Player', 'src/game/player/Player.cpp', 'int Player::func_ov002_020c4188()', {
        # dCamera_c::mFlags (0x154) and daSoundObj_c mSoundID/mCounterLimit (0xd4/0xde).
        '((dCamera_c *)p)->mFlags': '(*(u32 *)((char *)p + 0x154))',
        'daSoundObj_c *obj = (daSoundObj_c *)mAttachedActor;': 'char *obj = (char *)mAttachedActor;',
        'obj->mSoundID': '(*(s32 *)(obj + 0xd4))', 'obj->mCounterLimit': '(*(u16 *)(obj + 0xde))'}),
]


PLAYER_SOURCE = [
    ('_ZN6Player9GetHealthEv', 'int Player::GetHealth()'),
]


def mip_fields():
    from stage_actor_views import ACTOR_FIELDS
    fields = dict(ACTOR_FIELDS)
    fields.update(header_fields('include/dEnemyBase_c.h', 0xd0))
    fields.update(header_fields('include/daMip_c.h', 0x110))
    return fields


MIP_SOURCE = [
    ('_ZN7daMip_c13StateIdleInitEv', 'int daMip_c::StateIdleInit()', {
        'mModelAnim.SetAnim((BCA_File *)(*(void **)((char *)&data_ov085_021305c0 + 4)),':
            '_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0x300,data_ov085_021305c0.filePtr,'}),
]
MIP_DECLARATIONS = '''
extern "C" {
void _ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit(void *,void *,int,int,u16);
}
'''


MINIMAP_UNITS = [
    ('_ZN7Minimap13InitResourcesEv', {
        # The recovered unit under-declares this member call; the ROM passes
        # the same receiver in r0.
        'void _ZN7Minimap19UpdateLevelSpecificEv(void);': 'void _ZN7Minimap19UpdateLevelSpecificEv(void *);',
        '_ZN7Minimap19UpdateLevelSpecificEv();': '_ZN7Minimap19UpdateLevelSpecificEv(this);'}),
    ('_ZN7Minimap19UpdateLevelSpecificEv', {
        'namespace G2S {\n    void* GetBG3CharPtr();\n}\n': '',
        'struct Event {\n    static int GetBit(unsigned int bit);\n};\n': '',
        'int IsStarCollected(int level, int star);':
            'int IsStarCollected(int level, int star);\nvoid *_ZN3G2S13GetBG3CharPtrEv(void);\nint _ZN5Event6GetBitEj(unsigned int);',
        'G2S::GetBG3CharPtr()': '_ZN3G2S13GetBG3CharPtrEv()',
        'Event::GetBit(': '_ZN5Event6GetBitEj('}),
    ('_ZN7Minimap16CleanupResourcesEv', {}),
    ('_ZN7Minimap16OnPendingDestroyEv', {}),
]


MEMBER_UNITS = [
    ('_ZN9Animation8GetFlagsEv', 0, 'Animation'),
    ('_ZN6Player12St_Land_MainEv', 2, 'Player', {
        # 0x760 is mSpineRot[2] (s16 mSpineRot[3] at 0x75c, include/Player.h).
        'unk_760 = 0x4000;': 'mSpineRot[2] = 0x4000;'}),
    ('_ZN14CutsceneObject16CleanupResourcesEv', 2, 'CutsceneObject', {
        # The model members' second vtable slot (+4, the D0 destructor) is a
        # cartridge code word: dispatch through the ROM vtable.
        'struct Obj { virtual void v00(); virtual void m04(); };': '',
        'Obj* a = *(Obj**)': 'void* a = *(void**)', 'Obj* b = *(Obj**)': 'void* b = *(void**)',
        'a->m04();': '((void (*)(void *))((*(void ***)a)[1]))(a);',
        'b->m04();': '((void (*)(void *))((*(void ***)b)[1]))(b);'}),
    ('_ZN14CutsceneObject16OnPendingDestroyEv', 2, 'CutsceneObject'),
    ('_ZN10dBgW_KcMbg9GetNormalEsR7Vector3', 0, 'dBgW_KcMbg', {
        # dBgW_Kc::kclFile is +0x20; KCL_File normals +0x04 (6-byte s16
        # vectors) and tris +0x08 (0x10-byte records, normalIdx +6), per
        # include/dBgW_Kc.h.
        'extern "C" void func_02039db8(dBgW_KcMbg *self,': 'extern "C" void func_02039db8(void *self,',
        'KCL_File *p = kclFile;\n    u16 n = p->tris[triID].normalIdx;\n    s16 *v = p->normals[n];':
            'char *p = *(char **)((char *)this + 0x20);\n    u16 n = *(u16 *)(*(char **)(p + 8) + triID * 0x10 + 6);\n'
            '    s16 *v = (s16 *)(*(char **)(p + 4) + n * 6);'}),
    ('_ZN7dCcAc_c10GetOwnerIDEv', 0, 'dCcAc_c'),
    ('_ZN5Timer7GetTimeEv', 0, 'Timer'),
    ('_ZN5Timer9StopTimerEv', 0, 'Timer'),
    ('_ZN5Timer10StartTimerEv', 0, 'Timer'),
    ('_ZN10dBgCh_Actr20UpdateExtraContinousEv', 0, 'dBgCh_Actr', {
        # The extern block's closing line carries a comment.
        '}  /* end extern "C" -- the definition below is a member, not a C symbol */': '}',
        # SurfaceInfo.h: CLPS (8 bytes) then the face normal.
        'struct dBgPiLoc\n{': 'struct SurfaceInfo {\n    struct { u32 w0; u32 w1; } clps;\n    Vector3 normal;\n};\nstruct dBgPiLoc \n{',
        'ClearAllGroundFlags();': '_ZN10dBgCh_Actr19ClearAllGroundFlagsEv(this);',
        'ShouldUpdatePosY()': '_ZNK10dBgCh_Actr16ShouldUpdatePosYEv(this)',
        # IsOnGround() is the recovered const accessor; GetClsnPos returns by
        # hidden result pointer (declared above as (Vector3 *, void *)).
        'IsOnGround()': '_ZNK10dBgCh_Actr10IsOnGroundEv(this)',
        'v7c = ((dBgCh_Lin *)(t + 0x134))->GetClsnPos();': '_ZN9dBgCh_Lin10GetClsnPosEv(&v7c, t + 0x134);',
        'int  func_020355dc(void *);': 'int  func_020355dc(void *);\nint _ZNK10dBgCh_Actr10IsOnGroundEv(const void *);\nvoid _ZN10dBgCh_Actr19ClearAllGroundFlagsEv(void *);\nint _ZNK10dBgCh_Actr16ShouldUpdatePosYEv(const void *);'}),
    ('_ZNK5dBgPi9GetClsnIDEv', 0, 'dBgPi'),
    ('_ZN10dBgCh_Actr19ClearAllGroundFlagsEv', 0, 'dBgCh_Actr'),
    ('_ZN10dBgCh_Actr22ClearJustHitGroundFlagEv', 0, 'dBgCh_Actr'),
    ('_ZNK10dBgCh_Actr13JustHitGroundEv', 0, 'dBgCh_Actr'),
    ('_ZN10dBgCh_Actr13SetGroundFlagEv', 0, 'dBgCh_Actr'),
    ('_ZN10dBgCh_Actr15ClearGroundFlagEv', 0, 'dBgCh_Actr'),
    ('_ZN10dBgCh_Actr15ClearLimMovFlagEv', 0, 'dBgCh_Actr'),
    ('_ZNK10dBgCh_Actr13GetLimMovFlagEv', 0, 'dBgCh_Actr'),
    ('_ZNK10dBgCh_Actr16ShouldUpdatePosYEv', 0, 'dBgCh_Actr'),
    ('_ZNK10dBgCh_Actr15ShouldUpdatePosEv', 0, 'dBgCh_Actr'),
    ('_ZN5dBgCh18StopDetectingWaterEv', 0, 'dBgCh'),
    ('_ZN5dBgCh21StopDetectingOrdinaryEv', 0, 'dBgCh'),
    ('_ZN5dBgCh19StartDetectingToxicEv', 0, 'dBgCh'),
    ('_ZNK10dBgCh_Actr10IsOnGroundEv', 0, 'dBgCh_Actr'),
    ('_ZNK9Animation12WillHitFrameEi', 0, 'Animation'),
    ('_ZN6Player12St_Wait_MainEv', 2, 'Player'),
    ('_ZN9ModelBase12ApplyOpacityEj', 0, 'ModelBase'),
    ('_ZN7Minimap6RenderEv', 2, 'Minimap', {
        # The local Player view's one method is the overlay-2 ABI symbol.
        '    int HasNoCap();\n};': '};\nextern "C" int _ZN6Player8HasNoCapEv(Player *);',
        'pl->HasNoCap()': '_ZN6Player8HasNoCapEv(pl)'}),
    ('_ZN9Animation8FinishedEv', 0, 'Animation'),
    ('_ZN14CutsceneObject8BehaviorEv', 2, 'CutsceneObject', {
        # The local dBgCh_Gnd probe through its ABI: C1, SetObjAndPos (the
        # reference is a pointer), DetectClsn, clsnY at 0x44, then D1.
        # (Replacements run twice; the anchor is consumed on the first pass.)
        'extern Mtx43 data_020a0e68;\n': 'extern "C" {\n'
            'void *_ZN9dBgCh_GndC1Ev(void *);\nvoid *_ZN9dBgCh_GndD1Ev(void *);\n'
            'void _ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c(void *, Vector3 *, void *);\n'
            'int _ZN9dBgCh_Gnd10DetectClsnEv(void *);\n}\nextern Mtx43  data_020a0e68;\n',
        '      dBgCh_Gnd ground;\n      ground.SetObjAndPos(v, 0);\n      if (ground.DetectClsn() != 0)\n      {\n        s32 h = ground.clsnY;':
            '      unsigned char ground[0x50];\n      _ZN9dBgCh_GndC1Ev(ground);\n'
            '      _ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c(ground, &v, 0);\n'
            '      if (_ZN9dBgCh_Gnd10DetectClsnEv(ground) != 0)\n      {\n        s32 h = *(s32 *)(ground + 0x44);',
        '          *((u8 *) ((((int) c) + 0x103))) |= 1;\n        }\n      }\n':
            '          *((u8 *) ((((int) c) + 0x103))) |= 1;\n        }\n      }\n      _ZN9dBgCh_GndD1Ev(ground);\n'}),
    ('_ZN7Minimap8BehaviorEv', 2, 'Minimap', {
        # `self` aliases the receiver; spell its members directly.
        '    Minimap *self = this;\n': '', 'self->': '', '(int)self': '(int)this'}),
    # References are pointers in the ARM ABI.
    ('_ZN6Camera8BehaviorEv', 0, 'Camera', {
        'int &dst': 'int *dst', 'short &dst': 'short *dst',
        # The state-table identity test compares a cartridge address.
        '(u32)&data_0208733c': '0x0208733cu',
        '_Z14ApproachLinearRiii(*(int *)(c + 0x134),': '_Z14ApproachLinearRiii((int *)(c + 0x134),',
        '_Z15ApproachLinear2Rsss(*(short *)(c + 0x18c),': '_Z15ApproachLinear2Rsss((short *)(c + 0x18c),',
        '_Z15ApproachLinear2Rsss(*(short *)(c + 0x18e),': '_Z15ApproachLinear2Rsss((short *)(c + 0x18e),',
        '_Z15ApproachLinear2Rsss(*(short *)(c + 0x188),': '_Z15ApproachLinear2Rsss((short *)(c + 0x188),'}),
    # Static Stage::LC_Render: the generated receiver word is unused.
    ('_ZN5Stage9LC_RenderEv', 0, 'Stage', {
        '        RenderBouncingArrows();': '        _ZN5Stage20RenderBouncingArrowsEv();'}),
    ('_ZNK6Camera12IsUnderwaterEv', 0, 'Camera'),
    ('_ZN5Stage9RenderFogEv', 0, 'Stage'),
    ('_ZN5Stage11GetSkyboxIDEv', 0, 'Stage'),
    ('_ZN5Timer10ResetTimerEv', 0, 'Timer'),
    ('_ZN14CutsceneObject13InitResourcesEv', 2, 'CutsceneObject', {
        # The unit names overlay-9 data at 0x02113c20 as a function.
        '&func_02113c20': '&data_ov009_02113c20',
        'int CutsceneObject::InitResources()':
            'extern "C" {\nextern char data_ov009_02113c20;\n}\nint CutsceneObject::InitResources()'}),
]


def class_fields(cls):
    cls = cls.replace('::', '__')
    from stage_actor_views import ACTOR_FIELDS
    fields = dict(ACTOR_FIELDS) if cls.startswith(('d', 'Player')) else {}
    for rel in ('include/'+cls+'.h', 'external/sm64ds-pc/include/'+cls+'.h'):
        if (ROOT/rel).exists():
            if re.search(r'(?:struct|class)\s+'+cls+r'\s*:\s*(?:public\s+)?dActor_c\b', (ROOT/rel).read_text()):
                fields = dict(ACTOR_FIELDS)  # an actor: the dActor_c base members
            fields.update(header_fields(rel, 0))
            break
    return fields


def slices(projected):
    for symbol, overlay, cls, *rest in MEMBER_UNITS:
        row, bindings = isolated(projected, symbol, overlay, cls, class_fields(cls),
                                 replacements=rest[0] if rest else None)
        if not overlay:
            for binding in bindings: binding.pop('overlay', None)
        yield row, bindings
    minimap = header_fields('external/sm64ds-pc/include/Minimap.h', 0x50)
    for symbol, replacements in MINIMAP_UNITS:
        yield isolated(projected, symbol, 2, 'Minimap', minimap, replacements=replacements)
    mip = mip_fields()
    for symbol, name, replacements in MIP_SOURCE:
        yield isolated(projected, symbol, 85, 'daMip_c', mip, replacements=replacements,
                       rel='src/game/actors/d_a_mip.cpp', name=name,
                       types={'DaMipAnimationFilePtr': 'SharedFilePtr', 'DaMipModelFile': 'SharedFilePtr'},
                       prelude=MIP_DECLARATIONS)
    fields = player_fields()
    for symbol in PLAYER_STATES:
        yield isolated(projected, symbol, 2, 'Player', fields)
    for symbol, overlay, *rest in FREE_UNITS:
        row, bindings = free_unit(projected, symbol, overlay, rest[0] if rest else None)
        if not overlay:
            for binding in bindings: binding.pop('overlay', None)
        yield row, bindings
    for symbol, overlay, rel, *rest in BLOCK_UNITS:
        yield free_unit(projected, symbol, overlay, rest[0] if rest else None, rel=rel)
    for symbol, overlay, cls, rel, name, *rest in SOURCE_MEMBERS:
        row, bindings = isolated(projected, symbol, overlay, cls, player_fields() if cls == 'Player' else class_fields(cls),
                                 rel=rel, name=name, replacements=rest[0] if rest else None)
        if not overlay:
            for binding in bindings: binding.pop('overlay', None)
        yield row, bindings
    for symbol, name in PLAYER_SOURCE:
        yield isolated(projected, symbol, 2, 'Player', fields,
                       rel='src/game/player/Player.cpp', name=name)
