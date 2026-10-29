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
    return text


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
