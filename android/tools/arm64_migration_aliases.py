"""Retain recovered block-local ABI views without contradictory C declarations.

Different methods describe the same cartridge symbol through different local
record types. Give function declarations distinct C++ identifiers with their
original linker labels. Data declarations use byte-labelled storage and a typed
reference, so local record types never acquire impossible external linkage and
address-of/array semantics are retained.
"""

import re
from tree_sitter import Language, Parser
import tree_sitter_cpp

PARSER = Parser(Language(tree_sitter_cpp.language()))


def local_aliases(raw):
    data = raw.encode()
    root = PARSER.parse(data).root_node
    edits = []

    def text(node):
        return data[node.start_byte : node.end_byte].decode()

    def identifier(node):
        if node.type in ("identifier", "type_identifier"):
            return node
        child = node.child_by_field_name("declarator")
        if child:
            return identifier(child)
        for child in node.named_children:
            if child.type not in ("parameter_list", "type_descriptor"):
                found = identifier(child)
                if found:
                    return found
        return None

    def plain_function(decl):
        while decl.type in ("pointer_declarator", "reference_declarator"):
            decl = decl.child_by_field_name("declarator")
            if decl is None:
                return False
        if decl.type != "function_declarator":
            return False
        actual = decl.child_by_field_name("declarator")
        return actual and actual.type in ("identifier", "qualified_identifier")

    def visit(node):
        if node.type != "function_definition":
            for child in node.named_children:
                visit(child)
            return
        body = node.child_by_field_name("body")
        declarations, names = [], {}

        def collect(current):
            if current.type == "declaration":
                external = any(
                    c.type == "storage_class_specifier" and text(c) == "extern"
                    for c in current.named_children
                )
                decls = current.children_by_field_name("declarator")
                selected = []
                for decl in decls:
                    name = identifier(decl)
                    if not name:
                        continue
                    old = text(name)
                    # Constructor-style local initializers can be parsed as
                    # function declarations. Only known game-name shapes can
                    # be implicit block-scope extern function declarations.
                    if not external and not (
                        plain_function(decl) and re.match(r"(func_|_Z|[A-Z])", old)
                    ):
                        continue
                    names[old] = old + "__native_" + str(node.start_byte)
                    selected.append((decl, name, old))
                if selected:
                    if len(selected) != len(decls):
                        raise ValueError(
                            "mixed native extern declaration: " + text(current)
                        )
                    declarations.append((current, selected))
            for child in current.named_children:
                collect(child)

        collect(body)
        skipped = {declaration.id for declaration, _ in declarations}

        def uses(current):
            if current.id in skipped:
                return
            if (
                current.type in ("identifier", "type_identifier")
                and text(current) in names
            ):
                edits.append(
                    (current.start_byte, current.end_byte, names[text(current)])
                )
            for child in current.named_children:
                uses(child)

        uses(body)
        for declaration, selected in declarations:
            prefix = data[declaration.start_byte : selected[0][0].start_byte].decode()
            prefix = re.sub(r"\bextern\b", "", prefix).strip()
            result = []
            for decl, name, old in selected:
                alias = names[old]
                original = text(decl)
                begin = name.start_byte - decl.start_byte
                end = name.end_byte - decl.start_byte
                if plain_function(decl):
                    spelling = original[:begin] + alias + original[end:]
                    result.append(
                        "extern " + prefix + " " + spelling + ' __asm__("' + old + '");'
                    )
                else:
                    typename = alias + "_type"
                    spelling = original[:begin] + typename + original[end:]
                    result.append("typedef " + prefix + " " + spelling + ";")
                    result.append(
                        "extern unsigned char "
                        + alias
                        + '_storage[] __asm__("'
                        + old
                        + '");'
                    )
                    result.append(
                        typename
                        + " &"
                        + alias
                        + " = *reinterpret_cast<"
                        + typename
                        + " *>("
                        + alias
                        + "_storage);"
                    )
            edits.append(
                (declaration.start_byte, declaration.end_byte, "\n".join(result))
            )

    visit(root)
    for first, last, replacement in sorted(set(edits), reverse=True):
        data = data[:first] + replacement.encode() + data[last:]
    return data.decode()
