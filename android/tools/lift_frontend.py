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
from frontend_sdk_views import flatten as flatten_sdk
ROOT = Path(__file__).resolve().parents[2]
PARSER = Parser(Language(tree_sitter_cpp.language()))
BEHAVIORS = {name: '_ZN8Particle' + str(len(name)) + name + '4FuncERNS_10EffectDataEPcR7Vector3'
             for name in ('Acceleration','Jitter','Converge','Turn','LimitPlane','RadiusConverge')}


def definitions(raw, symbols):
    """Select actual tagged bodies from a TU, retaining its hash as input."""
    found = {}
    def visit(node):
        if node.type == 'function_definition':
            tags = re.findall(r'// @symbol (\S+)',raw[:node.start_byte])
            if tags and tags[-1] in symbols:
                found[tags[-1]] = raw[node.start_byte:node.end_byte]
        for child in node.named_children:
            visit(child)
    visit(PARSER.parse(raw.encode()).root_node)
    if set(found) != set(symbols):
        raise ValueError('selected native definitions changed: ' + str(set(symbols)-set(found)))
    return '\n'.join(found[name] for name in symbols) + '\n'


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
                        bitfields = [c for c in node.named_children if c.type == 'bitfield_clause']
                        if bitfields and len(decls)!=1:
                            raise ValueError('multiple bitfield declarators need an explicit view')
                        bits = ' ' + self.text(bitfields[0]) if bitfields else ''
                        rendered.append(prefix + self.named(name, value) + bits + (' = ' + self.rewrite(init) if init else '') +
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
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--full-model', action='store_true')
    parser.add_argument('sources', nargs='*')
    args = parser.parse_args()
    if args.profile:
        if args.sources or args.headers:
            parser.error('profile cannot be combined with explicit sources/headers')
        profile = json.loads(args.profile.read_text())
        args.sources, args.headers = profile['sources'], profile['headers']
        args.full_model = args.full_model or profile.get('full_model',False)
    if not args.sources:
        parser.error('sources or --profile are required')
    entries = []
    sdk_aliases = {}
    for rel in args.sources + args.headers:
        raw = (ROOT/rel).read_text()
        view = raw
        dependencies = {}
        if rel == 'src/cstd.cpp':
            selected = {'_ZN4cstd16reciprocal_asyncEi':'reciprocal_async',
                        '_ZN4cstd11fdiv_resultEv':'fdiv_result',
                        '_ZN4cstd11ldiv_resultEv':'ldiv_result', 'func_02053200':'func_02053200'}
            view = definitions(raw,selected)
            for symbol,name in selected.items():
                view = view.replace('cstd::'+name,symbol)
                view = re.sub(r'\b'+name+r'\b',symbol,view)
            view = ('#include "types.h"\nextern void _ZN4cstd16reciprocal_asyncEi(int);\n'
                    'extern int _ZN4cstd11fdiv_resultEv(void);\n' + view)
        if rel == 'src/engine/sound/SoundEngine.cpp':
            view = definitions(raw,['func_0204f94c'])
        if rel == 'src/engine/particle/Particle_Manager.cpp':
            # The constructor/parser own POD records and stored callbacks. Keep
            # their genuine bodies; the instance update/draw methods are separate.
            prefix = raw[:raw.index('namespace Particle {\nstruct EffectData;')]
            prefix = prefix.replace('#include "Particle__System.h"','')
            records = raw[raw.index('typedef void (*BehaviorFunc)'):raw.index('extern "C" {',raw.index('struct Flags2'))]
            records = records.replace('Particle::EffectData &','Particle::EffectData *').replace('Vector3 &','Vector3 *')
            callbacks = 'namespace Particle { union EffectData;\n' + ''.join(
                'extern void '+symbol+'(EffectData *, char *, Vector3 *);\n' for symbol in BEHAVIORS.values())+'}\n'
            view = prefix + callbacks + records + definitions(raw,['func_0204a4c8','func_0204a17c'])
            for name,symbol in BEHAVIORS.items():
                view = view.replace('Particle::'+name+'::Func','Particle::'+symbol)
        if rel == 'include/Particle__Behavior.h':
            for name,symbol in BEHAVIORS.items():
                expression = r'struct '+name+r' \{\s*static void Func\(EffectData& effect, char\* particle, Vector3& velocity\);\s*\};'
                view,n = re.subn(expression,'extern void '+symbol+'(EffectData *effect, char *particle, Vector3 *velocity);',view)
                if n!=1: raise ValueError('particle callback declaration changed: '+name)
        if rel == 'src/engine/particle/Particle_Behavior.cpp':
            view = raw.replace('#include "decl_common.h"','#include "types.h"\nextern unsigned int data_020a4d30;')
            for name,symbol in sorted(BEHAVIORS.items(),key=lambda item:len(item[0]),reverse=True):
                view = re.sub(r'\b'+name+r'::Func\b',symbol,view)
            view = view.replace('EffectData& effect','EffectData *effect').replace('Vector3&','Vector3 *')
            view = view.replace('Element& state = *(Element*)particle;','Element *state = (Element*)particle;')
            for name in ('effect','velocity','acceleration','state'):
                view = re.sub(r'(?<![.>])\b'+name+r'\.',name+'->',view)
            view = view.replace('namespace Particle {','using Particle::EffectData;\nusing Particle::Element;')
            view = view.replace('} // namespace Particle','')
            view = view.replace('include/math/*.h','the math headers')
        if rel == 'src/engine/gx/G3X.cpp':
            # The matching compiler widened this bool's stack slot to a word.
            # Read the native bool value without a four-byte out-of-bounds load.
            if view.count('if (*(int *)&e)')!=1: raise ValueError('SDK bool-slot idiom changed')
            view = view.replace('if (*(int *)&e)','if (e)')
        if rel == 'src/unnamed/ov007/func_ov007_020c3550.c':
            # Original ARM 020c3570..020c3594 keeps the allocated p in r0
            # through bx lr. The recovered void spelling loses that live result
            # on native ABIs; expose it explicitly, without changing its stores.
            signature = 'void func_ov007_020c3550(int a, int b, int c, int d)'
            if view.count(signature) != 1 or view.count('char* p =') != 1:
                raise ValueError('character record return projection changed')
            view = view.replace(signature, signature.replace('void ', 'int ', 1))
            end = view.rfind('}')
            view = view[:end] + '    return (int)p;\n' + view[end:]
        if rel == 'src/unnamed/ov007/func_ov007_020c99d8.c':
            # 020c9a1c calls c3550, then returns with r0 untouched.
            declaration = 'extern void func_ov007_020c3550('
            call = '    func_ov007_020c3550('
            if view.count(declaration) != 1 or view.count(call) != 1:
                raise ValueError('character file return projection changed')
            view = view.replace(declaration, 'extern int func_ov007_020c3550(')
            view = view.replace(call, '    return func_ov007_020c3550(')
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
        if rel in ('include/dExtModel_c.h', 'include/dExtSimpleModel_c.h'):
            # These headers already provide an explicit C record view. Use it
            # for frontend data; do not instantiate a native polymorphic class.
            marker = '/* The same object' if 'Simple' in rel else '/* The same objects'
            start = raw.index(marker)
            end = raw.index('#endif /* __cplusplus */', start)
            assertions = re.findall(r'typedef char (?:ModelComponents|dExtModel_c|dExtSimpleModel_c)_size.*?;',raw,re.S)
            view = '#include "types.h"\n#include "BMD_File.h"\n#include "math/Matrix.h"\n'
            if 'Simple' in rel:
                view += '#include "dExtModel_c.h"\n'
            view += raw[start:end] + '\n' + '\n'.join(assertions) + '\n'
            if 'Simple' in rel:
                view += 'extern void _ZN17dExtSimpleModel_c13LoadTexAndPalER8BMD_File(BMD_File *file);\n'
        if rel == 'include/Particle__Manager.h':
            declaration = '    System *AddSystem(int definitionID, Vector3 &position);'
            if raw.count(declaration) != 1:
                raise ValueError('particle manager data view changed')
            view = raw.replace(declaration, '')
        if rel == 'src/unnamed/ov007/func_ov007_020bc47c.cpp':
            call = 'dExtSimpleModel_c::LoadTexAndPal(*m->modelFile)'
            if raw.count(call) != 1:
                raise ValueError('frontend static model loader call changed')
            view = raw.replace(call, '_ZN17dExtSimpleModel_c13LoadTexAndPalER8BMD_File(m->modelFile)')
        if rel == 'src/engine/gx/GX_VramCnt.cpp':
            declaration = 'extern "C" GXVRamCnt data_020a6088;'
            if raw.count(declaration) != 1:
                raise ValueError('SDK VRAM state declaration changed')
            view = raw.replace(declaration, 'extern GXVRamCnt data_020a6088;')
        if rel == 'src/engine/model/Model4.cpp':
            if args.full_model:
                # Static methods have no actor/vptr ABI. Retain the complete
                # original model construction and VRAM allocation/upload code.
                names={}
                def collect(node):
                    if node.type=='function_definition':
                        decl=node.child_by_field_name('declarator')
                        text=raw[decl.start_byte:decl.end_byte]
                        match=re.search(r'dExtSimpleModel_c::(\w+)\(',text)
                        if match:
                            tags=re.findall(r'// @symbol (\S+)',raw[:node.start_byte])
                            if not tags: raise ValueError('model static symbol missing')
                            names[match.group(1)]=tags[-1]
                    for child in node.named_children: collect(child)
                collect(PARSER.parse(raw.encode()).root_node)
                if len(names)!=5: raise ValueError('model static method set changed')
                view=raw.replace('#include "dExtSimpleModel_c.h"','#include "BMD_File.h"')
                # The recovered palette-only path reads an unset texture format.
                # Reject that invalid material rather than inventing a format.
                guard='        if (idx2 != -1) {\n            u32 v2;'
                if view.count(guard)!=1: raise ValueError('material format dependency changed')
                view=view.replace(guard,guard+'\n            if (c1 == -1) { Crash(); return; }')
                for name,symbol in names.items():
                    view=view.replace('dExtSimpleModel_c::'+name,symbol)
                    view=re.sub(r'\b'+name+r'\b',symbol,view)
                if view.count('BMD_File &file')!=2: raise ValueError('model file reference projection changed')
                view=view.replace('BMD_File &file','BMD_File *file')
                view=re.sub(r'\bfile\.', 'file->',view)
                view=re.sub(r'&\s*file\b','file',view)
                # These expressions compute sizes, not memory references.
                for expression,replacement in [('&((struct A *)0)[a + b]','((char *)0 + sizeof(struct A) * (a + b))'),
                                               ('&((struct E *)0)[p[1]]','((char *)0 + sizeof(struct E) * p[1])')]:
                    if view.count(expression)!=1: raise ValueError('model offset idiom changed')
                    view=view.replace(expression,replacement)
            else:
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
            # Type arguments in a macro assignment can parse as declarations.
            # Spell the exact mmio.h macro expansion before the syntax walk.
            view = re.sub(r'\bNTR_MMIO\(((?:(?:unsigned|signed)\s+)?(?:char|short|int|long(?: long)?|[su](?:8|16|32|64))),\s*([^()]+)\)',
                          r'(::ntr::Reg<\1>((\2)))',view)
        if rel == 'src/NormalizeVec3.c':
            if view.count('    *ime;') != 1:
                raise ValueError('normalizer volatile read changed')
            view = view.replace('    *ime;', '    (void)static_cast<u16>(*ime);')
        lift = Lift(normalize(flatten_sdk(view, PARSER, sdk_aliases)))
        try:
            source = lift.result()
        except ValueError as error:
            raise ValueError(rel + ': ' + str(error)) from None
        entries.append(dict(path=rel, sha256=hashlib.sha256(raw.encode()).hexdigest(), source=source,
                            dependencies=dependencies, globals=lift.globals,
                            functions=sorted(lift.functions), header=rel in args.headers))
    # A call may import its declaration from a projected header. Give it the
    # same native spelling even when the TU itself has no local extern.
    functions = {name for row in entries for name in row['functions']}
    for row in entries:
        for name in sorted(functions, key=len, reverse=True):
            row['source'] = re.sub(r'\b' + re.escape(name) + r'\b', 'pk_' + name, row['source'])
        for original,alias in sdk_aliases.items():
            row['source']=re.sub(r'\bpk_'+re.escape(original)+r'\b','pk_'+alias,row['source'])
    args.out.write_text(json.dumps(dict(version=1,entries=entries), indent=2) + '\n')

if __name__ == '__main__':
    main()
