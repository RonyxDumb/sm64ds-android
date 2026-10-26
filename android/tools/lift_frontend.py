#!/usr/bin/env python3
"""Audit frontend pointer projection into a hash-guarded source manifest.

Regeneration only: tree-sitter==0.25.2, tree-sitter-cpp==0.23.4. Builds apply
the checked manifest with packed_sources.py and need only the Python stdlib.
Matching sources and their layout assertions remain unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'port/tools'))
import hostgen
from tree_sitter import Language, Parser
import tree_sitter_cpp
ROOT = Path(__file__).resolve().parents[2]
PARSER = Parser(Language(tree_sitter_cpp.language()))


def method_view(raw, qualified, symbol, record, prefix):
    """Project one static loader, not the model's native polymorphic class."""
    matches = []
    def visit(node):
        if node.type == 'function_definition':
            decl = node.child_by_field_name('declarator')
            if qualified in raw[decl.start_byte:decl.end_byte]:
                matches.append(node)
        for child in node.named_children:
            visit(child)
    visit(PARSER.parse(raw.encode()).root_node)
    if len(matches) != 1:
        raise ValueError('static loader definition changed: ' + qualified)
    node = matches[0].child_by_field_name('body')
    body = raw[node.start_byte:node.end_byte]
    body = re.sub(r'\bfile\.', 'file->', body)
    body = re.sub(r'&\s*file\b', 'file', body)
    return prefix + '\nvoid ' + symbol + '(' + record + ' *file) ' + body + '\n'


def normalize(text):
    return re.sub(r'\((?:int|u32|unsigned int)\)\s*&\s*\(\((\w+(?:\s+\w+)?)\s*\*\)\s*0\)->(\w+)',
                  r'offsetof(\1, \2)', text)


class Lift:
    def __init__(self, text):
        self.source = text.encode()
        self.root = PARSER.parse(self.source).root_node
        self.globals, self.functions, self.exports = {}, set(), []

    def text(self, node):
        return self.source[node.start_byte:node.end_byte].decode()

    def base(self, node):
        base = node.child_by_field_name('type')
        if base is None:
            raise ValueError('missing declaration type: ' + self.text(node))
        q = [self.text(c) for c in node.named_children if c.type == 'type_qualifier']
        value = self.text(base)
        if value in ('long', 'long int', 'signed long', 'signed long int'):
            value = 'int'
        if value in ('unsigned long', 'unsigned long int'):
            value = 'unsigned int'
        return ' '.join(q + [value])

    def decode(self, node, base):
        if node is None:
            return '', base
        kind = node.type
        if kind in ('identifier', 'field_identifier', 'type_identifier', 'qualified_identifier', 'primitive_type'):
            return self.text(node), base
        child = node.child_by_field_name('declarator')
        if kind in ('pointer_declarator', 'abstract_pointer_declarator'):
            q = ' '.join(self.text(c) for c in node.named_children if c.type == 'type_qualifier')
            return self.decode(child, ('ptr', base, q))
        if kind in ('array_declarator', 'abstract_array_declarator'):
            size = node.child_by_field_name('size')
            return self.decode(child, ('array', base, self.text(size) if size else ''))
        if kind in ('function_declarator', 'abstract_function_declarator'):
            values = []
            for p in node.child_by_field_name('parameters').named_children:
                if p.type == 'parameter_declaration':
                    values.append(self.declaration(p, parameter=True))
                elif p.type == 'variadic_parameter':
                    values.append('...')
                elif p.type != 'comment':
                    raise ValueError('unsupported parameter: ' + self.text(p))
            return self.decode(child, ('function', base, values))
        if kind in ('parenthesized_declarator', 'abstract_parenthesized_declarator'):
            children = [c for c in node.named_children if c.type != 'comment']
            if len(children) == 1:
                return self.decode(children[0], base)
        raise ValueError('unsupported declarator ' + kind + ': ' + self.text(node))

    def typename(self, value):
        if isinstance(value, str):
            return value
        kind, inner, extra = value
        if kind == 'ptr':
            return (extra + ' ' if extra else '') + 'Ptr<' + self.typename(inner) + '>'
        if kind == 'array':
            return self.typename(inner) + '[' + extra + ']'
        if kind == 'function':
            return self.typename(inner) + '(' + ', '.join(extra) + ')'
        raise ValueError(value)

    def named(self, name, value):
        if isinstance(value, tuple) and value[0] == 'array':
            return self.named(name, value[1]) + '[' + value[2] + ']'
        if isinstance(value, tuple) and value[0] == 'function':
            return self.typename(value[1]) + ' ' + name + '(' + ', '.join(value[2]) + ')'
        return self.typename(value) + (' ' + name if name else '')

    def declaration(self, node, parameter=False):
        name, value = self.decode(node.child_by_field_name('declarator'), self.base(node))
        if parameter and isinstance(value, tuple) and value[0] == 'array':
            value = ('ptr', value[1], '')
        return self.named(name, value)

    def address(self, name):
        m = re.fullmatch(r'data_(?:ov\d+_)?([0-9a-f]{8})', name)
        if m:
            return int(m.group(1), 16)
        for file in (ROOT/'config/arm9/symbols.txt', ROOT/'config/arm9/overlays/ov007/symbols.txt'):
            for line in file.read_text().splitlines():
                if line.startswith(name + ' '):
                    m = re.search(r'addr:0x([0-9a-f]+)', line)
                    if m:
                        return int(m.group(1), 16)
        raise ValueError('unresolved frontend datum: ' + name)

    def rewrite(self, node):
        kind = node.type
        if kind in ('preproc_def', 'preproc_function_def'):
            value = node.child_by_field_name('value')
            if value and '*' in self.text(value):
                probe = Lift('void expression() { ' + self.text(value).rstrip() + '; }')
                stmt = probe.root.named_children[0].child_by_field_name('body').named_children[0]
                if stmt.type != 'expression_statement' or len(stmt.named_children) != 1:
                    raise ValueError('unsupported frontend macro: ' + self.text(node))
                return self.source[node.start_byte:value.start_byte].decode() + probe.rewrite(stmt.named_children[0]) + '\n'
        if kind == 'linkage_specification':
            body = node.child_by_field_name('body')
            if body is not None:
                text = self.rewrite(body)
                return text[1:-1] if body.type == 'declaration_list' else text
        if kind == 'cast_expression':
            spelling = self.declaration(node.child_by_field_name('type'))
            body = self.rewrite(node.child_by_field_name('value'))
            if spelling in ('int', 's32'):
                return 'word(' + body + ')'
            if spelling in ('unsigned int', 'u32'):
                return 'uword(' + body + ')'
            return '(' + spelling + ')(' + body + ')'
        if kind == 'call_expression':
            fn, args = node.child_by_field_name('function'), node.child_by_field_name('arguments')
            if fn and fn.type == 'template_function' and self.text(fn).startswith(('reinterpret_cast<', 'static_cast<')):
                target = next(c for c in fn.named_children if c.type == 'template_argument_list').named_children[0]
                spelling = self.declaration(target) if target.type == 'type_descriptor' else self.text(target)
                return '(' + spelling + ')(' + ', '.join(self.rewrite(c) for c in args.named_children) + ')'
        if kind in ('declaration', 'field_declaration', 'type_definition', 'parameter_declaration', 'type_descriptor'):
            base = node.child_by_field_name('type')
            decls = node.children_by_field_name('declarator')
            if base and decls and base.child_by_field_name('body') is None:
                rendered = []
                for decl in decls:
                    init, actual = None, decl
                    if decl.type == 'init_declarator':
                        actual, init = decl.child_by_field_name('declarator'), decl.child_by_field_name('value')
                    name, value = self.decode(actual, self.base(node))
                    if kind == 'parameter_declaration' and isinstance(value, tuple) and value[0] == 'array':
                        value = ('ptr', value[1], '')
                    storage = [self.text(c) for c in node.named_children if c.type == 'storage_class_specifier']
                    is_function = isinstance(value, tuple) and value[0] == 'function'
                    if name.startswith(('data_', 'IDENTITY_')) and 'extern' in storage and not is_function:
                        address = self.address(name)
                        self.globals[name] = address
                        if isinstance(value, tuple) and value[0] == 'array':
                            expression = 'ArrayRef<' + self.typename(value[1]) + '>(0x%08xu)' % address
                        else:
                            expression = '(*Ptr<' + self.typename(value) + '>(0x%08xu))' % address
                        rendered.append('\n#define ' + name + ' ' + expression + '\n')
                    else:
                        prefix = 'typedef ' if kind == 'type_definition' else ' '.join(storage) + (' ' if storage else '')
                        if is_function:
                            self.functions.add(name)
                            prefix = 'extern "C" '
                        rendered.append(prefix + self.named(name, value) + (' = ' + self.rewrite(init) if init else '') +
                                        ('' if kind in ('parameter_declaration', 'type_descriptor') else ';'))
                return '\n'.join(rendered)
            if kind == 'type_descriptor' and base:
                return self.declaration(node)
        if kind == 'function_definition':
            decl = node.child_by_field_name('declarator')
            name, value = self.decode(decl, self.base(node))
            if not isinstance(value, tuple) or value[0] != 'function':
                raise ValueError('not a plain frontend function: ' + self.text(decl))
            storage = [self.text(c) for c in node.named_children if c.type == 'storage_class_specifier']
            prefix = 'static ' if 'static' in storage or 'inline' in storage else 'extern "C" '
            if prefix != 'static ':
                self.functions.add(name)
                params = decl
                while params.type != 'function_declarator':
                    params = params.child_by_field_name('declarator')
                args = []
                for p in params.child_by_field_name('parameters').named_children:
                    if p.type == 'parameter_declaration':
                        pn, pt = self.decode(p.child_by_field_name('declarator'), self.base(p))
                        if pt == 'void':
                            continue
                        if isinstance(pt, tuple) and pt[0] == 'array':
                            pt = ('ptr', pt[1], '')
                        args.append(self.typename(pt))
                self.exports.append((name, value[1] == 'void', args))
            return prefix + self.named(name, value) + ' ' + self.rewrite(node.child_by_field_name('body'))
        output, at = [], node.start_byte
        for child in node.named_children:
            output += [self.source[at:child.start_byte].decode(), self.rewrite(child)]
            at = child.end_byte
        output.append(self.source[at:node.end_byte].decode())
        return ''.join(output)

    def result(self):
        text = self.rewrite(self.root)
        for name in sorted(self.functions, key=len, reverse=True):
            text = re.sub(r'\b' + re.escape(name) + r'\b', 'pk_' + name, text)
        for name, void, types in self.exports:
            args = ', '.join('unsigned int a' + str(i) for i in range(len(types)))
            call = 'pk_' + name + '(' + ', '.join('(' + t + ')(a' + str(i) + ')' for i,t in enumerate(types)) + ')'
            body = call + ';' if void else 'return uword(' + call + ');'
            text += '\nextern "C" ' + ('void' if void else 'unsigned int') + ' abi_' + name + '(' + args + ') { ' + body + ' }\n'
        return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--headers', nargs='*', default=[])
    parser.add_argument('sources', nargs='+')
    args = parser.parse_args()
    entries = []
    for rel in args.sources + args.headers:
        raw = (ROOT/rel).read_text()
        view = raw
        dependencies = {}
        if rel == 'include/types.h':
            view = subprocess.check_output(['gcc','-x','c','-E','-P','-Iinclude',rel], cwd=ROOT, text=True)
            view = view.replace('typedef int bool;', '')
        if rel == 'include/decl_common.h':
            # c44e4 only imports this declaration. Preserve the actual shared
            # spelling without importing unrelated game globals or actor ABIs.
            lines = re.findall(r'^extern int func_ov007_020c3df4\(int, int\);$', raw, re.M)
            if len(lines) != 1:
                raise ValueError('frontend allocator declaration changed')
            view = '#include "common.h"\n' + lines[0] + '\n'
        if rel == 'src/engine/model/Model4.cpp':
            view = method_view(raw, 'dExtSimpleModel_c::UpdateFileOffsets',
                               '_ZN17dExtSimpleModel_c17UpdateFileOffsetsER8BMD_File', 'BMD_File',
                               '#include "BMD_File.h"\n#define REBASE(p) ((char *)(p) + base)')
        if rel == 'src/engine/animation/dExtFrameCtrl_c_UpdateFileOffsets.cpp':
            record = re.search(r'struct BCA_File \{.*?\n\};', raw, re.S)
            if not record:
                raise ValueError('BCA loader record changed')
            view = method_view(raw, 'dExtFrameCtrl_c::UpdateFileOffsets',
                               '_ZN15dExtFrameCtrl_c17UpdateFileOffsetsER8BCA_File', 'BCA_File',
                               '#include "types.h"\n' + record.group())
        if rel == 'src/engine/model/dExtAnmTexPat_c_UpdateFileOffsets.cpp':
            header = 'include/dExtAnmTexPat_c.h'
            text = (ROOT/header).read_text()
            dependencies[header] = hashlib.sha256(text.encode()).hexdigest()
            record = re.search(r'struct BTP_File \{.*?\n\};', text, re.S)
            locals_ = re.findall(r'struct BTP(?:Name|Seq)Record \{.*?\n\};', raw, re.S)
            assertions = re.findall(r'^typedef char BTP.*?;', raw, re.M)
            if not record or len(locals_) != 2 or len(assertions) != 2:
                raise ValueError('BTP loader records changed')
            view = method_view(raw, 'dExtAnmTexPat_c::UpdateFileOffsets',
                               '_ZN15dExtAnmTexPat_c17UpdateFileOffsetsER8BTP_File', 'BTP_File',
                               '#include "types.h"\n#define REBASE(p) ((char *)(p) + base)\n' +
                               '\n'.join([record.group(), *locals_, *assertions]))
        if rel.startswith('src/'):
            view = hostgen.transform(view)[0]
        if rel == 'src/NormalizeVec3.c':
            if view.count('    *ime;') != 1:
                raise ValueError('normalizer volatile read changed')
            view = view.replace('    *ime;', '    (void)static_cast<u16>(*ime);')
        lift = Lift(normalize(view))
        source = lift.result()
        entries.append(dict(path=rel, sha256=hashlib.sha256(raw.encode()).hexdigest(), source=source,
                            dependencies=dependencies, globals=lift.globals,
                            functions=sorted(lift.functions), header=rel in args.headers))
    # A call may import its declaration from a projected header. Give it the
    # same native spelling even when the TU itself has no local extern.
    functions = {name for row in entries for name in row['functions']}
    for row in entries:
        for name in sorted(functions, key=len, reverse=True):
            row['source'] = re.sub(r'\b' + re.escape(name) + r'\b', 'pk_' + name, row['source'])
    args.out.write_text(json.dumps(dict(version=1,entries=entries), indent=2) + '\n')

if __name__ == '__main__':
    main()
