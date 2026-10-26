"""Source-only views of stateless SDK APIs; never flatten game/actor classes."""
SDK = {'GX', 'GXS', 'G2x', 'G3X', 'G3i', 'CP15'}


def flatten(text, parser):
    """Give static SDK functions distinct C names across recovered namespaces.

    The source uses both struct GX static declarations and namespace GX
    declarations for the same API. Neither owns instance state. In particular,
    GX::SetGraphicsMode and GXS::SetGraphicsMode must remain different symbols.
    """
    raw = text.encode()
    root = parser.parse(raw).root_node
    edits, local = [], {}

    def spelling(n):
        return raw[n.start_byte:n.end_byte].decode()

    def name_of(decl):
        if decl is None:
            return None
        while decl.type not in ('identifier', 'field_identifier', 'qualified_identifier'):
            child = decl.child_by_field_name('declarator')
            if child is None:
                children = [c for c in decl.named_children if c.type != 'comment']
                if len(children) != 1:
                    return None
                child = children[0]
            decl = child
        return decl

    def visit(node, scope=None):
        name, body = node.child_by_field_name('name'), node.child_by_field_name('body')
        if node.type in ('namespace_definition', 'struct_specifier', 'class_specifier') and name and body:
            key = spelling(name)
            if key in SDK:
                # These specific SDK containers have no instance data. A new
                # field or method body needs a reviewed projection instead.
                if node.type != 'namespace_definition':
                    def check(child):
                        if child.type in ('field_declaration','function_definition'):
                            storage = [spelling(c) for c in child.named_children if c.type=='storage_class_specifier']
                            if 'static' not in storage:
                                raise ValueError('SDK API acquired instance state: ' + key)
                            if child.type=='field_declaration' and not any(c.type=='function_declarator' for c in child.named_children):
                                raise ValueError('SDK API acquired a data member: ' + key)
                        for nested in child.named_children:
                            check(nested)
                    for child in body.named_children:
                        check(child)
                edits.extend([(node.start_byte, body.start_byte + 1, ''),
                              (body.end_byte - 1, node.end_byte, '')])
                scope = key
        if scope and node.type == 'function_declarator':
            name = name_of(node.child_by_field_name('declarator'))
            if name:
                value = spelling(name)
                if '::' not in value:
                    local[(scope, value)] = scope + '_' + value
                    edits.append((name.start_byte, name.end_byte, scope + '_' + value))
        for child in node.named_children:
            visit(child, scope)

    visit(root)

    # Unqualified calls inside SDK namespaces name that namespace's own API.
    def calls(node, scope=None):
        name = node.child_by_field_name('name')
        if node.type in ('namespace_definition', 'struct_specifier', 'class_specifier') and name:
            key = spelling(name)
            if key in SDK:
                scope = key
        if scope and node.type == 'call_expression':
            fn = node.child_by_field_name('function')
            if fn and fn.type == 'identifier' and (scope, spelling(fn)) in local:
                edits.append((fn.start_byte, fn.end_byte, local[(scope, spelling(fn))]))
        for child in node.named_children:
            calls(child, scope)

    calls(root)
    edits.sort(reverse=True)
    end = len(raw)
    for first, last, replacement in edits:
        if last > end:
            raise ValueError('overlapping SDK projection edits')
        raw = raw[:first] + replacement.encode() + raw[last:]
        end = first
    # Use syntax nodes so provenance comments and string contents stay intact.
    qualified = []
    def qualify(node):
        if node.type == 'qualified_identifier':
            value = raw[node.start_byte:node.end_byte].decode()
            parts = value.split('::')
            if len(parts)==2 and parts[0] in SDK and parts[1].isidentifier():
                qualified.append((node.start_byte,node.end_byte,'_'.join(parts)))
                return
        for child in node.named_children:
            qualify(child)
    qualify(parser.parse(raw).root_node)
    for first,last,value in sorted(qualified,reverse=True):
        raw=raw[:first]+value.encode()+raw[last:]
    return raw.decode()
