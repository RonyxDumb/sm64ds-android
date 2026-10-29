"""Exact source repairs for native Player compilation; DS inputs stay intact."""

from tree_sitter import Language, Parser
import tree_sitter_cpp

PARSER = Parser(Language(tree_sitter_cpp.language()))


def source_fixes(path, text):
    if path == "src/game/player/Player.cpp":
        return player_fixes(text)
    if path == "src/game/actors/d_a_nknk.cpp":
        # Vec3 is this TU's anonymous-struct typedef, never a struct tag.
        return text.replace("struct Vec3", "Vec3")
    return remaining_cpp_fixes(path, text)


def player_fixes(raw):
    def one(text, old, new):
        assert text.count(old) == 1, (old, text.count(old))
        return text.replace(old, new)

    b = raw.encode()
    edits = []

    def visit(n):
        if n.type == "function_definition":
            decl = n.child_by_field_name("declarator")
            body = n.child_by_field_name("body")
            name = b[decl.start_byte : decl.end_byte].decode()
            t = b[body.start_byte : body.end_byte].decode()
            if name.startswith("Player::St_HoldLight_Main("):
                t = one(t, "int var_r4 = 0;", "var_r4 = 0;")
                t = t[:1] + "\n    int var_r4;" + t[1:]
            if name.startswith("func_ov002_020e2ea0("):
                for old, new in [
                    ("int val =", "val ="),
                    ("dBgPi *cr =", "cr ="),
                    ("int diff =", "diff ="),
                ]:
                    t = one(t, old, new)
                t = t[:1] + "\n    int val, diff;\n    dBgPi *cr;" + t[1:]
            edits.append((body.start_byte, body.end_byte, t))
            return
        for c in n.named_children:
            visit(c)

    visit(PARSER.parse(b).root_node)
    for a, z, t in sorted(edits, reverse=True):
        b = b[:a] + t.encode() + b[z:]
    raw = b.decode()
    raw = one(
        raw,
        "__builtin__clear((void*)mHeldObjQueue, 0x14);",
        "std::memset((void*)mHeldObjQueue, 0, 0x14);",
    )
    raw = one(
        raw,
        "func_ov002_020e6ad4(&data_ov002_02110804);",
        "func_ov002_020e6ad4((unsigned short *)&data_ov002_02110804);",
    )
    return (
        'extern "C" short data_02082214[];\nextern "C" unsigned short *func_ov002_020e6ad4(unsigned short *);\n'
        + raw
    )


import re


def exact(text, before, after, count=1):
    assert text.count(before) == count, (before, text.count(before), count)
    return text.replace(before, after)


def remaining_cpp_fixes(path, s):
    # These callbacks have no return value in their bodies or their callers.
    s = re.sub(r'\bint (\w+::OnAttacked[12]\s*\()', r'void \1', s)
    if path.endswith('SoundEngine.cpp'):
        s, count = re.subn(r'\(extern (void|int)(?= ?\*?\(\*)', r'(\1', s)
        assert count == 23, count
    if path.endswith('d_a_bombking.cpp'):
        s = exact(s, '    Player *other = this->mTalkingPlayer;', '    other = this->mTalkingPlayer;')
        s = exact(s, '    short msg = 0;', '    msg = 0;')
        b = s.encode()
        edits = []
        def visit(node):
            if node.type == 'function_definition':
                body = node.child_by_field_name('body')
                value = b[body.start_byte:body.end_byte].decode()
                if 'other = this->mTalkingPlayer;' in value and 'msg = 0;' in value:
                    edits.append(body.start_byte+1)
                return
            for child in node.named_children: visit(child)
        visit(PARSER.parse(b).root_node)
        assert len(edits) == 1
        at = edits[0]
        s = (b[:at]+b'\n    Player *other;\n    short msg;'+b[at:]).decode()
    if path.endswith('d_a_krb.cpp'):
        s = exact(s, '    unsigned int id = goomba->mdCcAc_c.otherOwner;', '    id = goomba->mdCcAc_c.otherOwner;')
        s = exact(s, 'int daKrb_c::func_ov084_021298d0() {', 'int daKrb_c::func_ov084_021298d0() {\n    unsigned int id;')
    if path.endswith('d_a_obj_ewb_ice.cpp'):
        s = exact(s, 'typedef int (daObjEwbIce_c::*EwbIceStateFunc)();', 'typedef sm64ds::native_abi::StoredMember<daObjEwbIce_c, int()> EwbIceStateFunc;')
    if path.endswith('d_a_onms.cpp'):
        s = exact(s, 's16 data_02082214[];', 'extern s16 data_02082214[];')
    if path.endswith('d_mg_trmpln2_mario.cpp'):
        s = exact(s, 'System *System::New(unsigned, unsigned, Fix12, Fix12, Fix12, const Vector3_16f *, Callback *);', '', 2)
    if path.endswith('d_s_mg_snowball.cpp'):
        s = exact(s, '#define C1 ((char*)(void*)(int)(c))', '#define C1 ((char *)(c))')
        s = exact(s, '#define C2 ((char*)(void*)(int)(C1))', '#define C2 C1')
        s = exact(s, '#define SNOW_OFF(m) ((int)&((dScMgSnowball_c *)0)->m)', '#define SNOW_OFF(m) offsetof(dScMgSnowball_c, m)')
    if path.endswith('d_s_mg_panel.cpp'):
        s = 'extern "C" int data_0209d4b8;\n'+s
    namespace_views = ('d_a_wanwan.cpp', 'd_a_obj_wc_obj02.cpp', 'd_a_obj_wc_obj05.cpp', 'd_mg_trmpln2_mario.cpp', 'd_a_propeller_heyho.cpp', 'd_mg3d_heyho_obj_adapter.cpp', 'd_s_mg_base.cpp', 'unit020bfec0.cpp', 'unit020cd744.cpp')
    if any(path.endswith(name) for name in namespace_views):
        from arm64_namespace_views import namespace_imports
        s = namespace_imports(s)
    if path.endswith('d_a_btn.cpp'):
        s = exact(s, 'namespace vecret { extern "C" Vector3 func_ov079_02123d4c(void *thiz); }', '')
        s = exact(s, '    return vecret::func_ov079_02123d4c(this);', '    Vector3 result;\n    func_ov079_02123d4c((int *)&result, this);\n    return result;')
    if path.endswith('d_a_obj_ct_mecha03.cpp'):
        s = exact(s, 'func_020393d4(&mMeshCollider, &dBgW::UpdatePosWithTransform);', 'func_020393d4(&mMeshCollider, (void *)&dBgW::UpdatePosWithTransform);')
    if path.endswith('d_a_obj_seesaw.cpp'):
        s = exact(s, 'func_020393c4(&mMeshCollider, func_ov095_02135e90);', 'func_020393c4(&mMeshCollider, (void *)func_ov095_02135e90);')
    if path.endswith('d_a_star.cpp'):
        s = exact(s, 'int daStar_c::func_ov002_020e8dd8()', 'void daStar_c::func_ov002_020e8dd8()')
        s = exact(s, 'volatile unsigned int daStar_c::func_ov002_020e8c34()', 'void daStar_c::func_ov002_020e8c34()')
    if path.endswith('d_a_propeller_heyho.cpp'):
        s = re.sub(r'(?m)^extern daPropeller_Heyho_c::State (data_ov070_021235(?:9c|cc));', r'extern "C" daPropeller_Heyho_c::State \1;', s)
    if path.endswith('d_s_mg_smartball.cpp'):
        s = s.replace('struct V2 ', 'V2 ')
        s = re.sub(r'(?<=,)\s*(?:(func_0203d738)|(NullDestructor_0203d47c))(?=,|\))', lambda m:' (void *)'+m[0].strip(), s)
    if path.endswith('unit020b2c84.cpp'):
        s = exact(s, '(e->*((ElemFn *)sBannerSetupTable)[st])();', '((ElemFn *)sBannerSetupTable)[st].invoke(e);')
        s = exact(s, 'typedef void (MgBanner::*ElemFn)();', 'typedef sm64ds::native_abi::StoredMember<MgBanner, void()> ElemFn;')
        s = re.sub(r'(?m)^extern (PairW|int) (sBanner(?:Update|Setup|Draw)Table|sMgBanner(?:Sprites|Files))\[\];', r'extern "C" \1 \2[];', s)
        s = '#include "runtime/stored_member.h"\n'+s
    if path.endswith('cstd4.cpp'):
        s = exact(s, 'asm void func_0206dab4(void) { swp r0, r0, [r1]; bx lr }', 'unsigned func_0206dab4(unsigned value,unsigned *word) { return __atomic_exchange_n(word,value,__ATOMIC_SEQ_CST); }')
        s = exact(s, 'asm { dcd 0xe7ffffff }', 'std::terminate();')
        s = '#include <exception>\n'+s
    return s
