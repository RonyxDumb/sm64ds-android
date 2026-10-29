"""Keep C symbol labels while isolating recovered namespace-local ABI views."""
from tree_sitter import Language, Parser
import tree_sitter_cpp
import re
PARSER = Parser(Language(tree_sitter_cpp.language()))

def namespace_imports(raw):
    data = raw.encode()
    root = PARSER.parse(data).root_node
    edits = []
    def text(n): return data[n.start_byte:n.end_byte].decode()
    def name_of(n):
        if n.type in ('identifier', 'type_identifier'): return n
        c = n.child_by_field_name('declarator')
        if c: return name_of(c)
        for c in n.named_children:
            if c.type not in ('parameter_list', 'type_descriptor'):
                r = name_of(c)
                if r: return r
    def visit(n, ns=False, lang='C++'):
        if n.type == 'namespace_definition': ns = True
        if n.type == 'linkage_specification':
            lang = 'C' if re.match(r'extern\s+"C"', text(n)) else 'C++'
        if ns and n.type == 'declaration':
            ds = n.children_by_field_name('declarator')
            if ds and not any(d.type == 'init_declarator' for d in ds):
                t = text(n)
                sub = []
                offset = 0
                if not re.match(r'(extern|typedef|using)\b', t):
                    t = 'extern ' + t
                    offset = 7
                for d in ds:
                    name = name_of(d)
                    if name: sub.append((d.end_byte-n.start_byte+offset, ' __asm__("'+text(name)+'")'))
                if sub:
                    for at, value in sorted(sub, reverse=True): t = t[:at]+value+t[at:]
                    edits.append((n.start_byte, n.end_byte, 'extern "C++" { '+t+' }'))
            return
        if n.type == 'function_definition':
            d = n.child_by_field_name('declarator')
            if ns and lang == 'C' and '::' not in text(d).split('(')[0]:
                body = n.child_by_field_name('body')
                name = name_of(d)
                sig = data[n.start_byte:body.start_byte].decode().strip()
                edits.append((n.start_byte, n.end_byte, 'extern "C++" { '+sig+' __asm__("'+text(name)+'");\n'+text(n)+'\n}'))
            return
        for c in n.named_children: visit(c, ns, lang)
    visit(root)
    for a, z, value in sorted(edits, reverse=True): data = data[:a]+value.encode()+data[z:]
    return data.decode()
