#!/usr/bin/env python3
"""Project the game class ABI while retaining native C++ method bodies.

Unlike a host vptr, the explicit address point and Member records preserve the
cartridge layout. Generated bridges are the only callable code in those tables.
Matching inputs and all their sizeof checks remain intact and hash guarded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from lift_frontend import Lift, PARSER, ROOT, normalize
from frontend_sdk_views import flatten
import hostgen


def local_cpp(raw):
    # Select the original C++ spelling without expanding included headers into
    # every consumer. Keep compiler and platform conditionals out of the view.
    raw = re.sub(r'^\s*#include\s+([^\n]+)',r'#pragma packed_include \1',raw,flags=re.M)
    value = subprocess.run(['g++','-E','-P','-x','c++','-'],input=raw,text=True,
                           stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True).stdout
    return re.sub(r'^#pragma packed_include\s+', '#include ',value,flags=re.M)


def walk(node):
    yield node
    for child in node.named_children:
        yield from walk(child)


def text(raw,node):
    return raw[node.start_byte:node.end_byte]


def function_decl(node):
    decl=node.child_by_field_name('declarator')
    while decl and decl.type not in ('function_declarator','abstract_function_declarator'):
        decl=decl.child_by_field_name('declarator') or next((c for c in decl.named_children if c.type.endswith('declarator')),None)
    return decl


class GameLift(Lift):
    symbols={}

    def address(self,name):
        return self.symbols[name] if name in self.symbols else super().address(name)

    def result(self):
        # Runtime entries use typed Ptr<signature> calls. The frontend's legacy
        # word-only convenience wrappers cannot express namespace-local types.
        value=self.rewrite(self.root)
        includes=list(re.finditer(r'^\s*#include[^\n]*\n',value,re.M))
        at=includes[-1].end() if includes else 0
        value=value[:at]+'\n'+'\n'.join(dict.fromkeys(self.hoisted))+'\n'+value[at:]
        if '' in self.functions: raise ValueError('unnamed native function declaration')
        if self.functions:
            names=re.compile(r'\b(?:'+'|'.join(re.escape(n) for n in sorted(self.functions,key=len,reverse=True))+r')\b')
            value=names.sub(lambda m:'pk_'+m.group(),value)
        return value

    def decode(self,node,base):
        if node and node.type in ('function_declarator','abstract_function_declarator'):
            params=[]
            for p in node.child_by_field_name('parameters').named_children:
                if p.type in ('parameter_declaration','optional_parameter_declaration'):
                    value=self.declaration(p,parameter=True)
                    default=p.child_by_field_name('default_value')
                    if default: value+=' = '+self.rewrite(default)
                    params.append(value)
                elif p.type=='variadic_parameter': params.append('...')
                elif p.type!='comment': raise ValueError('unsupported native parameter: '+self.text(p))
            return self.decode(node.child_by_field_name('declarator'),('function',base,params))
        if node and node.type in ('reference_declarator','abstract_reference_declarator'):
            children=[c for c in node.named_children if c.type!='type_qualifier']
            return self.decode(children[-1] if children else None,('ref',base,''))
        if node and node.type in ('destructor_name','operator_name'):
            return self.text(node),base
        return super().decode(node,base)

    def typename(self,value):
        if isinstance(value,tuple) and value[0]=='ref':
            return self.typename(value[1])+'&'
        return super().typename(value)

    def native_member(self,node):
        parent=node.parent
        while parent:
            if parent.type=='field_declaration_list': return True
            if parent.type in ('translation_unit','namespace_definition','compound_statement'): break
            parent=parent.parent
        decl=function_decl(node)
        return bool(decl and '::' in self.text(decl.child_by_field_name('declarator')))

    def rewrite(self,node):
        kind=node.type
        if kind=='type_descriptor':
            return self.declaration(node)
        if kind=='declaration' and node.parent.type=='compound_statement' and function_decl(node) and not any(
                c.type=='storage_class_specifier' and self.text(c)=='extern' for c in node.named_children):
            # A local class-valued direct initializer is not an extern function.
            # Parsing alone cannot distinguish `Fix12 a(value)` from a prototype.
            return self.text(node)
        if kind in ('declaration','field_declaration','type_definition'):
            # tree-sitter-cpp represents ARM member declarators with an ERROR
            # node. Parse only this bounded declarator, not arbitrary source.
            value=self.text(node)
            member=re.fullmatch(r'(typedef\s+)?(.+?)\s*\(\s*([\w:]+)::\*(\w+)\s*\)\s*\((.*?)\)\s*;',value,re.S)
            if member:
                return ('typedef ' if member[1] else '')+'Member<'+member[3]+', '+member[2].strip()+'('+member[5]+')> '+member[4]+';'
        if kind=='call_expression':
            fn=node.child_by_field_name('function')
            args=node.child_by_field_name('arguments')
            member=re.fullmatch(r'\((.*?)\s*(->\*|\.\*)\s*(.*?)\)',self.text(fn),re.S)
            if member:
                obj=member[1] if member[2]=='->*' else '&('+member[1]+')'
                return 'member_call('+obj+', '+member[3]+''.join(', '+self.rewrite(a) for a in args.named_children)+')'
        if kind=='function_definition' and self.native_member(node):
            decl=node.child_by_field_name('declarator')
            body=node.child_by_field_name('body')
            ret=self.base(node) if node.child_by_field_name('type') else ''
            name,value=self.decode(decl,ret)
            storage=[self.text(c) for c in node.named_children if c.type in ('storage_class_specifier','type_qualifier')]
            middle=self.source[decl.end_byte:body.start_byte].decode()
            # Initializer lists also contain pointer casts and constructor args.
            for c in node.named_children:
                if c.type=='field_initializer_list': middle=self.rewrite(c)+' '
            return ' '.join(storage)+(' ' if storage else '')+self.named(name,value)+middle+self.rewrite(body)
        if kind in ('declaration','field_declaration') and function_decl(node) and self.native_member(node):
            ret=self.base(node) if node.child_by_field_name('type') else ''
            name,value=self.decode(node.child_by_field_name('declarator'),ret)
            storage=[self.text(c) for c in node.named_children if c.type in ('storage_class_specifier','type_qualifier')]
            decl=function_decl(node)
            qualifiers=' '.join(self.text(c) for c in decl.named_children if c.type=='type_qualifier')
            return ' '.join(storage)+(' ' if storage else '')+self.named(name,value)+(' '+qualifiers if qualifiers else '')+';'
        return super().rewrite(node)


class Classes:
    def __init__(self,headers,symbols):
        self.records={}
        self.headers=headers
        self.symbols=symbols
        for path,raw in headers.items():
            def collect(node,scope=''):
                if node.type=='namespace_definition':
                    scope+=text(raw,node.child_by_field_name('name'))+'::'
                if node.type in ('struct_specifier','class_specifier') and node.child_by_field_name('body') and node.child_by_field_name('name'):
                    name=text(raw,node.child_by_field_name('name'))
                    qualified=scope+name
                    bases=next((c for c in node.named_children if c.type=='base_class_clause'),None)
                    base=re.sub(r'^:\s*|\b(public|protected|private)\b','',text(raw,bases)).strip() if bases else None
                    self.records[qualified]=dict(name=name,base=base,node=node,path=path,scope=scope)
                    scope=qualified+'::'
                for c in node.named_children: collect(c,scope)
            collect(PARSER.parse(raw.encode()).root_node)
        for name in list(self.records): self.slots(name)

    def slots(self,name):
        record=self.records[name]
        if 'slots' in record: return record['slots']
        base=record['base'].split(',')[0].strip() if record['base'] else None
        if base and base not in self.records and record['scope']+base in self.records: base=record['scope']+base
        inherited=dict(self.slots(base)) if base in self.records else {}
        slots=inherited.copy()
        raw=self.headers[record['path']]
        body=record['node'].child_by_field_name('body')
        own=[]
        for node in body.named_children:
            decl=function_decl(node)
            if not decl or not re.search(r'\bvirtual\b',text(raw,node)): continue
            method=text(raw,decl.child_by_field_name('declarator'))
            probe=GameLift('struct Probe { '+text(raw,node)+' };')
            member=next(n for n in walk(probe.root) if n.type in ('declaration','field_declaration','function_definition') and function_decl(n))
            params=function_decl(member).child_by_field_name('parameters')
            types=[probe.typename(probe.decode(p.child_by_field_name('declarator'),probe.base(p))[1])
                   for p in params.named_children if p.type=='parameter_declaration']
            signature='~' if method.startswith('~') else method+'('+','.join(types)+')'
            if signature not in slots: slots[signature]=max((s[0]+s[1] for s in slots.values()),default=0),2 if method.startswith('~') else 1
            own.append((node,method,slots[signature][0]))
        record.update(slots=slots,virtuals=own,root=bool(own) and not inherited)
        mangled=''.join(str(len(part))+part for part in name.split('::'))
        record['vtable']=self.symbols.get('_ZTV'+('N'+mangled+'E' if '::' in name else mangled),0)
        return slots

    def header(self,path):
        raw=self.headers[path]
        edits=[]
        for qualified,r in self.records.items():
            if r['path']!=path: continue
            body=r['node'].child_by_field_name('body')
            if r['root']:
                base=next((n for n in r['node'].named_children if n.type=='base_class_clause'),None)
                if base: edits.append((base.start_byte+1,base.start_byte+1,' public Vptr, '))
                else: edits.append((body.start_byte,body.start_byte,' : public Vptr '))
            if r['base'] and ',' in r['base'] and r['slots']:
                primary=r['base'].split(',')[0].strip()
                edits.append((body.start_byte+1,body.start_byte+1,'\npublic: void packed_set_vptr(uint32_t address) { '+primary+'::packed_set_vptr(address); }\n'))
            constructors=[]
            for node in body.named_children:
                decl=function_decl(node)
                if decl and text(raw,decl.child_by_field_name('declarator'))==r['name']:
                    constructors.append(node)
                if node.type=='function_definition' and decl and r['slots']:
                    method=text(raw,decl.child_by_field_name('declarator'))
                    if method in (r['name'],'~'+r['name']) and not re.search(r'\bvirtual\b',text(raw,node)):
                        start=node.child_by_field_name('body').start_byte+1
                        edits.append((start,start,' packed_set_vptr(0x%08xu); '%r['vtable']))
            if r['slots'] and not constructors:
                edits.append((body.start_byte+1,body.start_byte+1,'\npublic: '+r['name']+'() { packed_set_vptr(0x%08xu); }\n'%r['vtable']))
            for node,method,slot in r['virtuals']:
                value=text(raw,node)
                if method.startswith('~'):
                    value=re.sub(r'\bvirtual\s+','',value)
                    if node.type=='function_definition': value=value.replace('{','{ packed_set_vptr(0x%08xu); '%r['vtable'],1)
                    edits.append((node.start_byte,node.end_byte,value))
                    continue
                decl=function_decl(node)
                lift=GameLift('struct Probe { '+value+' };')
                member=next(n for n in walk(lift.root) if n.type in ('declaration','field_declaration','function_definition') and function_decl(n))
                local=function_decl(member)
                ret=lift.base(member)
                name,signature=lift.decode(local,ret)
                params=local.child_by_field_name('parameters')
                typed=[]; arguments=[]; stored=[]
                for i,p in enumerate(params.named_children):
                    if p.type!='parameter_declaration': continue
                    pn,pt=lift.decode(p.child_by_field_name('declarator'),lift.base(p))
                    if pt=='void': continue
                    pn=pn or 'arg'+str(i)
                    typed.append(lift.named(pn,pt)); arguments.append('&'+pn if isinstance(pt,tuple) and pt[0]=='ref' else pn)
                    stored.append(lift.typename(('ptr',pt[1],'')) if isinstance(pt,tuple) and pt[0]=='ref' else lift.typename(pt))
                suffix=' const' if re.search(r'\)\s*const',value) else ''
                sig=ret+'(Ptr<'+qualified+'>'+''.join(', '+p for p in stored)+')'
                wrapper=ret+' '+method+'('+', '.join(typed)+')'+suffix+' { return virtual_call<'+sig+'>(this, '+str(slot)+''.join(', '+a for a in arguments)+'); }'
                impl=re.sub(r'\bvirtual\s+','',value)
                impl=re.sub(r'\b'+re.escape(method)+r'(?=\s*\()', 'packed_impl_'+method,impl, count=1)
                impl=re.sub(r'\s*=\s*0\s*;', ';',impl)
                edits.append((node.start_byte,node.end_byte,wrapper+'\n'+impl))
            # Allocation is supplied by make<T>; no host operator can return a
            # four-byte logical handle. Deleting destructors are explicit bridges.
            for node in body.named_children:
                if re.search(r'\boperator\s+(new|delete)\b',text(raw,node)):
                    edits.append((node.start_byte,node.end_byte,''))
        for begin,end,value in sorted(edits,reverse=True): raw=raw[:begin]+value+raw[end:]
        return raw

    def source(self,raw):
        edits=[]; bridges=[]
        for node in walk(PARSER.parse(raw.encode()).root_node):
            if node.type!='function_definition': continue
            decl=function_decl(node)
            if not decl: continue
            qualified=text(raw,decl.child_by_field_name('declarator'))
            if '::' not in qualified: continue
            cls,method=qualified.rsplit('::',1)
            if cls not in self.records:
                candidates=[name for name in self.records if name.endswith('::'+cls)]
                if len(candidates)==1: cls=candidates[0]
                else: continue
            record=self.records[cls]
            body=node.child_by_field_name('body')
            constructor=method==record['name']; destructor=method=='~'+record['name']
            if (constructor or destructor) and record['slots']:
                edits.append((body.start_byte+1,body.start_byte+1,' packed_set_vptr(0x%08xu); '%record['vtable']))
            virtual=any(name==method for _,name,_ in record['virtuals'])
            actual='packed_impl_'+method if virtual and not destructor else method
            if actual!=method:
                name=decl.child_by_field_name('declarator')
                edits.append((name.end_byte-len(method),name.end_byte,actual))
            tags=re.findall(r'// @symbol ([^\n]+)',raw[:node.start_byte])
            if not tags: raise ValueError('native method without symbol: '+qualified)
            symbols=re.findall(r'_Z\w+',tags[-1])
            if not symbols: raise ValueError('native method has no mangled symbol: '+qualified)
            snippet=text(raw,node)
            probe=GameLift(snippet)
            local=probe.root.named_children[0]
            fd=function_decl(local)
            ret=probe.base(local) if local.child_by_field_name('type') else 'void'
            if local.child_by_field_name('type'):
                _,signature=probe.decode(local.child_by_field_name('declarator'),ret)
                ret=probe.typename(signature[1])
            params=[]; args=[]
            for i,p in enumerate(fd.child_by_field_name('parameters').named_children):
                if p.type!='parameter_declaration': continue
                pn,pt=probe.decode(p.child_by_field_name('declarator'),probe.base(p))
                if pt=='void': continue
                pn='argument'+str(i)
                ref=isinstance(pt,tuple) and pt[0]=='ref'
                params.append(probe.named(pn,('ptr',pt[1],'') if ref else pt))
                args.append('*'+pn if ref else pn)
            # Static methods have no self. The header, not the mangled name,
            # establishes that contract.
            hraw=self.headers[record['path']]
            header_methods=[n for n in record['node'].child_by_field_name('body').named_children
                            if function_decl(n) and text(hraw,function_decl(n).child_by_field_name('declarator'))==method]
            static=any(re.search(r'^\s*static\b',text(hraw,n)) for n in header_methods)
            nested=set()
            for n in record['node'].child_by_field_name('body').named_children:
                if n.type in ('struct_specifier','class_specifier','enum_specifier') and n.child_by_field_name('name'):
                    nested.add(text(hraw,n.child_by_field_name('name')))
                if n.type=='type_definition':
                    value=text(hraw,n)
                    match=re.search(r'::\*(\w+)\s*\)',value) or re.search(r'\b(\w+)\s*;',value)
                    if match: nested.add(match[1])
            def qualify(value):
                for name in nested: value=re.sub(r'(?<![:\w])'+re.escape(name)+r'\b',cls+'::'+name,value)
                return value
            params=[qualify(value) for value in params]; ret=qualify(ret)
            for symbol in symbols:
                signature=[] if static else ['Ptr<'+cls+'> self']
                signature+=params
                if constructor:
                    ret='Ptr<'+cls+'>'
                    action='::new (self.get()) '+cls+'('+', '.join(args)+'); return self;'
                elif destructor:
                    action='self.get()->~'+record['name']+'();'
                    if re.search(r'D0E',symbol): action+=' Space::current().release(self.address());'
                else:
                    action='return '+(cls+'::' if static else 'self->')+actual+'('+', '.join(args)+');'
                bridges.append((symbol,'extern "C" '+ret+' pk_'+symbol+'('+', '.join(signature)+') { '+action+' }'))
        for begin,end,value in sorted(edits,reverse=True): raw=raw[:begin]+value+raw[end:]
        for cls,r in self.records.items():
            for _,method,_ in r['virtuals']:
                if not method.startswith('~'):
                    raw=re.sub(r'\b'+re.escape(cls)+'::'+re.escape(method)+r'(?=\s*\()',cls+'::packed_impl_'+method,raw)
        return raw,bridges


def local_externs(raw):
    """Give per-function ABI declarations unique C++ names, shared ELF names.

    Matching TUs intentionally carry different register views of one callee.
    An asm alias preserves those local call shapes without conflating their
    declarations when native C++ needs them hoisted out of a member body.
    """
    edits=[]; aliases={}
    for index,fn in enumerate(n for n in walk(PARSER.parse(raw.encode()).root_node) if n.type=='function_definition'):
        body=fn.child_by_field_name('body')
        names=set()
        for node in walk(body):
            if node.type!='declaration' or not function_decl(node): continue
            if not any(c.type=='storage_class_specifier' and text(raw,c)=='extern' for c in node.named_children): continue
            name=text(raw,function_decl(node).child_by_field_name('declarator'))
            if re.fullmatch(r'\w+',name): names.add(name)
        value=text(raw,body)
        for name in sorted(names,key=len,reverse=True):
            alias='local_call_'+str(index)+'_'+name
            aliases['pk_'+alias]='pk_'+name
            value=re.sub(r'\b'+re.escape(name)+r'\b',alias,value)
        if names: edits.append((body.start_byte,body.end_byte,value))
    for begin,end,value in reversed(edits): raw=raw[:begin]+value+raw[end:]
    return raw,aliases


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args()
    profile=json.loads(args.profile.read_text())
    symbols={}
    maps=['config/arm9/symbols.txt','config/arm9/itcm/symbols.txt']+profile.get('symbol_maps',[])
    for path in maps:
        for m in re.finditer(r'^(\S+) kind:\S+.*?addr:0x([0-9a-f]+)',(ROOT/path).read_text(),re.M): symbols[m[1]]=int(m[2],16)
    GameLift.symbols=symbols
    headers={path:local_cpp((ROOT/path).read_text()) for path in profile['headers'] if path!='include/types.h'}
    classes=Classes(headers,symbols)
    entries=[]; bindings=[]; aliases={}
    for path in profile['headers']+profile['sources']:
        raw=(ROOT/path).read_text()
        if path=='include/types.h': view=local_cpp(raw)
        elif path in headers: view=classes.header(path)
        else:
            view=raw
            if path=='src/runtime/actor/ActorBase.cpp':
                # h != 0 implies one of the two branches that assigns avail.
                # Keep a sentinel for native optimizers across handle calls.
                view=view.replace('    u32 avail;', '    u32 avail = 0;')
            if '#include "decl_common.h"' in view:
                common=(ROOT/'include/decl_common.h').read_text()
                code=re.sub(r'/\*.*?\*/|//[^\n]*','',raw,flags=re.S)
                used=set(re.findall(r'\b(?:func_\w+|data_\w+|_Z\w+)\b',code))
                declarations=[m.group() for m in re.finditer(r'^extern [^;]+;',common,re.M)
                              if set(re.findall(r'\b(?:func_\w+|data_\w+|_Z\w+)\b',m.group()))&used]
                view=view.replace('#include "decl_common.h"','#include "common.h"\n'+'\n'.join(declarations))
            view,bridges=classes.source(view)
            view,local_aliases=local_externs(view)
        view=hostgen.transform(view)[0] if path.startswith('src/') else view
        view=re.sub(r'\bNTR_MMIO\(((?:(?:unsigned|signed)\s+)?(?:char|short|int|long(?: long)?|[su](?:8|16|32|64))),\s*([^()]+)\)',
                    r'(::ntr::Reg<\1>((\2)))',view)
        lift=GameLift(normalize(flatten(view,PARSER,aliases)),path.endswith('.c'))
        source=lift.result()
        if path.startswith('src/'):
            lines=[]
            for line in source.splitlines():
                match=re.search(r'\b(pk_local_call_\w+)\(',line) if line.startswith('extern "C" ') and line.endswith(';') else None
                if match and match[1] in local_aliases:
                    line=line[:-1]+' __asm__("'+local_aliases[match[1]]+'");'
                lines.append(line)
            source='\n'.join(lines)+'\n'
        source=re.sub(r'^#define (\w+) ',r'#undef \1\n#define \1 ',source,flags=re.M)
        definitions=[n for n,_,_ in lift.exports]
        if path.startswith('src/'):
            source+='\n'+'\n'.join(value for _,value in bridges)+'\n'
            definitions += [name for name,_ in bridges]
            for name in definitions:
                if name in symbols: bindings.append(dict(address=symbols[name],symbol='pk_'+name))
        entries.append(dict(path=path,sha256=hashlib.sha256(raw.encode()).hexdigest(),source=source,
                            globals=lift.globals,functions=sorted(lift.functions),definitions=definitions,
                            header=path in headers or path=='include/types.h',namespace='sm64ds::game::source'))
    functions={name for row in entries for name in row['functions']}
    matcher=re.compile(r'\b(?:'+'|'.join(re.escape(name) for name in sorted(functions,key=len,reverse=True))+r')\b')
    for row in entries: row['source']=matcher.sub(lambda m:'pk_'+m.group(),row['source'])
    data=dict(version=1,entries=entries,bindings=bindings,namespace='sm64ds::game',dependencies={path:hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in maps})
    args.out.write_text(json.dumps(data,indent=2)+'\n')

if __name__=='__main__': main()
