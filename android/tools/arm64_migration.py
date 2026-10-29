#!/usr/bin/env python3
"""Generate checked ARM64 compatibility views of complete recovered source files.

This closes compiler errors, not the game link. Polymorphic instance layouts,
member pointers and raw offsets still need runtime integration. No failed input
is replaced by a stub, no layout assertion is disabled, and all original source
bodies are retained. The profile pins every selected source and header input.

Regeneration dependency: tree-sitter==0.25.2, tree-sitter-cpp==0.23.4.
"""

from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys
from tree_sitter import Language, Parser
import tree_sitter_cpp
from arm64_migration_aliases import local_aliases
from arm64_migration_fixes import source_fixes

ROOT = Path(__file__).resolve().parents[2]
PARSER = Parser(Language(tree_sitter_cpp.language()))
sys.path.insert(0, str(ROOT / "port/tools"))
import hostgen

words = {
    "int": "word",
    "s32": "word",
    "signed int": "word",
    "unsigned": "uword",
    "u32": "uword",
    "unsigned int": "uword",
}
_header_types = None


def type_names(text):
    data = text.encode()
    result = set()

    def declared(node):
        if node.type in ("identifier", "type_identifier"):
            return data[node.start_byte : node.end_byte].decode()
        child = node.child_by_field_name("declarator")
        if child:
            return declared(child)
        for child in node.named_children:
            if child.type not in ("parameter_list", "template_argument_list"):
                name = declared(child)
                if name:
                    return name

    def visit(node):
        if node.type in (
            "class_specifier",
            "struct_specifier",
            "enum_specifier",
            "alias_declaration",
        ):
            name = node.child_by_field_name("name")
            if name:
                result.add(data[name.start_byte : name.end_byte].decode())
        if node.type == "type_definition":
            for decl in node.children_by_field_name("declarator"):
                name = declared(decl)
                if name:
                    result.add(name)
        for child in node.named_children:
            visit(child)

    visit(PARSER.parse(data).root_node)
    return result


def disambiguate(raw):
    # A syntax-only C++ parser cannot tell `(base) + i` from a cast to a
    # hypothetical type named base. Extra expression parentheses remove that
    # ambiguity without changing evaluation or pointer arithmetic precedence.
    global _header_types
    if _header_types is None:
        collected = set()
        for header in (ROOT / "include").rglob("*.h"):
            collected.update(type_names(header.read_text()))
        _header_types = collected
    known = _header_types | type_names(raw)
    data = raw.encode()
    edits = []

    def visit(node):
        if node.type == "cast_expression":
            typ = node.child_by_field_name("type")
            if (
                typ
                and len(typ.named_children) == 1
                and typ.named_children[0].type == "type_identifier"
            ):
                name = data[typ.start_byte : typ.end_byte].decode()
                if name not in known:
                    # Preserve whitespace after the close parenthesis.
                    close = typ.end_byte
                    while close < len(data) and data[close : close + 1] in b" \t":
                        close += 1
                    if data[close : close + 1] == b")":
                        edits.extend(
                            (
                                (node.start_byte, node.start_byte, "("),
                                (close + 1, close + 1, ")"),
                            )
                        )
        for child in node.named_children:
            visit(child)

    visit(PARSER.parse(data).root_node)
    for first, last, text in sorted(set(edits), reverse=True):
        data = data[:first] + text.encode() + data[last:]
    return data.decode()


def transform(raw):
    # tree-sitter parses `(u32)(p)->field` as a call followed by a field
    # access. Spell the known typedef's primitive cast before walking the tree.
    # Leave comments and literals byte-for-byte unchanged.
    tokens = re.compile(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\(\s*(u32|s32)\s*\)',
        re.S,
    )
    raw = tokens.sub(
        lambda m: ("(unsigned int)" if m[1] == "u32" else "(int)") if m[1] else m[0],
        raw,
    )
    raw = disambiguate(raw)
    raw, member_fields = re.subn(
        r'int \((daOwl_c|daSnowman_c|daObjEwbIce_c)::\*(mEnter|mMain|enter|main|execute|update)\)\(\);',
        r'sm64ds::native_abi::StoredMember<\1, int()> \2;', raw)

    def descriptors(m):
        return re.sub(
            r"([A-Za-z_][\w:]*) \*(\w+)(\s*\[[^\]]+\])?;",
            r"sm64ds::native_abi::StoredPointer<\1> \2\3;",
            m.group(),
        )

    records = (
        "ResourceDescriptor",
        "KaitendaiResources",
        "AxleResources",
        "FlyingCarpetResources",
        "FileRow",
        "ClpsRow",
        "EwbIceFileSlot",
        "EwbIceClpsSlot",
        "BombkingAnimFile",
        "BombkingModelFile",
        "BTPNameRecord",
        "BTPSeqRecord",
        "RouletteStrip",
    )
    raw = re.sub(
        r"struct (?:" + "|".join(records) + r") \{.*?\n\};",
        descriptors,
        raw,
        flags=re.S,
    )
    b = raw.encode()
    root = PARSER.parse(b).root_node

    def text(n):
        return b[n.start_byte : n.end_byte].decode()

    def rewrite(n):
        # Retain an unparsed expression verbatim. Guessing through an ERROR node
        # can accidentally remove an actor's pointer-to-member call operator.
        if n.type == "ERROR":
            return text(n)
        if n.type == "storage_class_specifier" and text(n) == "register":
            return ""
        if n.type == "cast_expression":
            typ = n.child_by_field_name("type")
            val = n.child_by_field_name("value")
            spelling = text(typ)
            v = rewrite(val)
            if spelling in words:
                return "sm64ds::native_abi::" + words[spelling] + "(" + v + ")"
            if "*" in spelling and "&" not in spelling:
                return "sm64ds::native_abi::pointer<" + spelling + ">(" + v + ")"
        if n.type == "call_expression":
            fn = n.child_by_field_name("function")
            args = n.child_by_field_name("arguments")
            if (
                fn
                and fn.type == "parenthesized_expression"
                and text(fn)[1:-1].strip() in words
            ):
                spelling = text(fn)[1:-1].strip()
                vals = [c for c in args.named_children if c.type != "comment"]
                if len(vals) == 1:
                    return (
                        "sm64ds::native_abi::"
                        + words[spelling]
                        + "("
                        + rewrite(vals[0])
                        + ")"
                    )
            if (
                fn
                and fn.type == "template_function"
                and text(fn).startswith(("reinterpret_cast<", "static_cast<"))
            ):
                a = next(
                    (
                        c
                        for c in fn.named_children
                        if c.type == "template_argument_list"
                    ),
                    None,
                )
                if a and len(a.named_children) == 1:
                    spelling = text(a.named_children[0])
                    vals = [c for c in args.named_children if c.type != "comment"]
                    if len(vals) == 1:
                        if spelling in words:
                            return (
                                "sm64ds::native_abi::"
                                + words[spelling]
                                + "("
                                + rewrite(vals[0])
                                + ")"
                            )
                        if "*" in spelling and "&" not in spelling:
                            return (
                                "sm64ds::native_abi::pointer<"
                                + spelling
                                + ">("
                                + rewrite(vals[0])
                                + ")"
                            )
        if n.type in ("preproc_def", "preproc_function_def"):
            # Macro bodies are token trees, not expressions in this parse. Identity
            # LADR is the same reviewed allocation-only macro as hostgen's version.
            t = text(n)
            t = re.sub(r"\(\(void\s*\*\)\(unsigned int\)\(p\)\)", r"((void *)(p))", t)
            if n.type == "preproc_function_def":
                value = n.child_by_field_name("value")
                if value:
                    body = text(value).rstrip().replace("\\\n", "\n")
                    if body and body != raw:
                        transformed = transform("void macro_body(){" + body + ";}")
                        a = transformed.index("{") + 1
                        end = transformed.rfind("}")
                        t = (
                            t[: value.start_byte - n.start_byte]
                            + transformed[a:end].rstrip()[:-1].replace("\n", "\\\n")
                            + "\n"
                        )
            return t
        result = []
        at = n.start_byte
        for c in n.named_children:
            result += [b[at : c.start_byte].decode(), rewrite(c)]
            at = c.end_byte
        return "".join(result) + b[at : n.end_byte].decode()

    return '#include "runtime/native_abi.h"\n' + ('#include "runtime/stored_member.h"\n' if member_fields else '') + rewrite(root)


def checked_inputs(profile):
    if profile["version"] != 1:
        raise ValueError("unsupported ARM64 migration profile")
    inputs = dict(profile["headers"])
    inputs.update({path: row["sha256"] for path, row in profile["sources"].items()})
    for path, digest in inputs.items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
            raise ValueError("ARM64 migration input changed; review the view: " + path)


def packed_view(path, clang):
    from lift_frontend import Lift, normalize

    # Use the C record branches even for a free-function C++ TU. Every selected
    # input is checked below to retain its complete set of function bodies.
    view = subprocess.check_output(
        [
            str(clang),
            "--target=aarch64-linux-android26",
            "-x",
            "c",
            "-E",
            "-P",
            "-DSM64DS_PLATFORM_PC",
            "-I" + str(ROOT / "include"),
            str(ROOT / path),
        ],
        cwd=ROOT,
        text=True,
    )
    view = view.replace("typedef int bool;", "")
    view = normalize(hostgen.transform(view)[0])
    view = re.sub(
        r"\bNTR_MMIO\(((?:(?:unsigned|signed)\s+)?(?:char|short|int|long(?: long)?|[su](?:8|16|32|64))),\s*([^()]+)\)",
        r"(::ntr::Reg<\1>((\2)))",
        view,
    )
    view = re.sub(r"\(\s*u32\s*\)", "(unsigned int)", view)
    generated = Lift(view, path.endswith(".c")).result()

    # A header's C branch must not silently remove a selected source's game
    # functions. In particular, frontend manifests also have partial slices;
    # this migration census deliberately compiles complete translation units.
    def definitions(text):
        data = text.encode()
        result = []

        def walk(node):
            if node.type == "function_definition":
                decl = node.child_by_field_name("declarator")
                result.append(data[decl.start_byte : decl.end_byte])
                return
            for child in node.named_children:
                walk(child)

        walk(PARSER.parse(data).root_node)
        return result

    # The preprocessor can add header inline bodies, but cannot remove a body
    # from these deliberately unconditional, hash-pinned source files.
    if len(definitions(view)) < len(definitions((ROOT / path).read_text())):
        raise ValueError("preprocessing removed source bodies: " + path)
    unit = Path(path).stem
    return (
        '#include "runtime/packed_ptr.h"\n#include "ntr/mmio.h"\n#include <cstddef>\n'
        "namespace sm64ds::migration::unit_" + unit + " {\n"
        "using namespace sm64ds::packed;\n#pragma pack(push,4)\n"
        + ("#define bool int\n" if path.endswith(".c") else "")
        + generated
        + "\n#undef bool\n#pragma pack(pop)\n}\n"
    )


def generate(out, clang):
    profile = json.loads(
        (ROOT / "android/tools/arm64_migration_profile.json").read_text()
    )
    checked_inputs(profile)
    # Copy the include tree so quoted includes in another game header see the
    # same SharedFilePtr ABI. Merely prepending -I would mix the two layouts.
    shutil.copytree(ROOT / "include", out / "include", dirs_exist_ok=True)
    header = (ROOT / "include/SharedFilePtr.h").read_text()
    if header.count("void *filePtr;") != 1:
        raise ValueError("SharedFilePtr storage declaration changed")
    header = header.replace(
        "void *filePtr;", "sm64ds::native_abi::StoredPointer<void> filePtr;"
    )
    header = header.replace(
        "#ifndef SM64DS_PLATFORM_PC",
        "// The explicit stored-pointer view retains its DS extent assertion.\n#if 1",
    )
    (out / "include/SharedFilePtr.h").write_text(
        '#include "runtime/native_abi.h"\n' + header
    )
    for name in ("daBombking_c.h", "daSanbo_c.h", "daYurei_Mucho_c.h"):
        file = out / "include" / name
        text = file.read_text()
        text = re.sub(
            r"^extern int (_ZTV\w+)\[\];$", r'extern "C" int \1[];', text, flags=re.M
        )
        file.write_text(text)
    views = json.loads((ROOT / "android/tools/arm64_header_views.json").read_text())
    for name, edits in views.items():
        file = out / "include" / name
        text = file.read_text()
        for edit in edits:
            if text.count(edit["before"]) < 1:
                raise ValueError("native header view changed: " + name)
            text = text.replace(edit["before"], edit["after"])
        file.write_text(text)
    result = {}
    frontend = None
    for path, row in profile["sources"].items():
        if row["mode"] == "native_cpp":
            text = source_fixes(path, (ROOT / path).read_text())
            text = local_aliases(text)
            text = re.sub(
                r"\(\(int\)\&\(\((\w+) \*\)0\)->(\w+)\)", r"offsetof(\1, \2)", text
            )
            text = transform(text)
            includes = [out / "include"]
        elif row["mode"] in ("packed_c", "packed_cpp"):
            text = packed_view(path, clang)
            includes = []
        elif row["mode"] == "frontend_complete":
            if frontend is None:
                from packed_sources import generate as frontend_generate

                frontend_generate(out / "frontend")
                frontend = json.loads(
                    (ROOT / "android/tools/frontend_manifest.json").read_text()
                )
            entry = next(item for item in frontend["entries"] if item["path"] == path)
            raw = (ROOT / path).read_bytes()

            def count_functions(node):
                if node.type == "function_definition":
                    return 1
                return sum(count_functions(child) for child in node.named_children)

            if count_functions(PARSER.parse(raw).root_node) != len(
                entry["definitions"]
            ):
                raise ValueError(
                    "frontend view is only a partial source slice: " + path
                )
            result[path] = {
                "path": (out / "frontend" / (Path(path).stem + ".cpp")).resolve(),
                "include_dirs": [out / "frontend/include"],
            }
            continue
        else:
            raise ValueError("unknown migration mode: " + row["mode"])
        dest = out / Path(path).with_suffix(".cpp")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(
            "// ARM64 compile migration; complete input: " + path + "\n" + text
        )
        result[path] = {"path": dest.resolve(), "include_dirs": includes}
    return result
