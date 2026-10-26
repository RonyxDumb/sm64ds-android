"""Reviewed ABI views used by the original title update and draw paths.

The matching source is an input, never edited. Register-liveness repairs make
arguments/results explicit at the native ABI boundary; records keep DS offsets.
"""
import re


def view_for(rel, raw, view, parser, behaviors, full_particles):
    if rel == 'include/dExtModel_c.h':
        view += '\nstruct BCA_File;\n'
    if rel == 'src/unnamed/ov007/func_ov007_020bc894.cpp':
        view = view.replace('#include "dExtModel_c.h"','#include "dExtModel_c.h"\nextern void _ZN15ModelComponents11UpdateBonesEP8BCA_Filei(ModelComponents *, BCA_File *, int);')
        view = view.replace('((ModelComponents *)model)->UpdateBones(m->boneAnims[idx], p->frame)',
                            '_ZN15ModelComponents11UpdateBonesEP8BCA_Filei((ModelComponents *)model, m->boneAnims[idx], p->frame)')
    if rel == 'src/unnamed/ov007/func_ov007_020b7708.cpp':
        view = re.sub(r'static_cast<Particle::Manager \*>\(data_ov007_0210344c\)\s*->AddSystem\(0, \*static_cast<Vector3 \*>\(pos\)\)',
                      '_ZN8Particle7Manager9AddSystemEiR7Vector3((Particle::Manager *)data_ov007_0210344c, 0, (Vector3 *)pos)',view)
        view = view.replace('#include "Particle__System.h"','#include "Particle__System.h"\nextern Particle::System *_ZN8Particle7Manager9AddSystemEiR7Vector3(Particle::Manager *, int, Vector3 *);')
    veneers = {'020c8440':'020c3d1c', '020c92a0':'020c3d1c', '020c8098':'020c3d1c',
               '020c9080':'020c3d1c', '020c3544':'020c3d1c', '020c8970':'020c897c',
               '020bdeb0':'020be098'}
    for name in ('020c05f8','020c0a9c','020c105c','020c1620','020c2018','020c22f8',
                 '020c2410','020c338c','020c8b04','020c8f58','020c9460'):
        veneers[name]='020c3d1c'
    suffix = rel.rsplit('/',1)[-1].split('.')[0].removeprefix('func_ov007_')
    if rel.startswith('src/unnamed/ov007/') and suffix in veneers:
        target = 'func_ov007_'+veneers[suffix]
        param = 'int' if suffix == '020bdeb0' else 'void *'
        view = ('extern void '+target+'('+param+');\nvoid func_ov007_'+suffix+
                '('+param+' value) { '+target+'(value); }\n')
    if rel in ('src/unnamed/ov007/func_ov007_020b2cf0.c','src/unnamed/ov007/func_ov007_020b413c.c'):
        view = re.sub(r'\bG2\b','G2_record',view)
    if rel == 'src/unnamed/ov007/func_ov007_020c9110.c':
        view = re.sub(r'\bthis\b','self',view)
    if rel == 'src/unnamed/ov007/func_ov007_020cb4b0.cpp':
        view = view.replace('s16(*t)[3]','short (*t)[3]').replace('s16(*tc)[3]','short (*tc)[3]')
    if rel == 'src/unnamed/ov007/func_ov007_020bcdb8.cpp':
        # 020bce28 BL b3f20; 020bce2c BL c99d8: r0 is still the loaded file.
        view = view.replace('void func_ov007_020b3f20(void* p);','void *func_ov007_020b3f20(void* p);')
        view = view.replace('void* func_ov007_020c99d8(void);','int func_ov007_020c99d8(int);')
        view = view.replace('    func_ov007_020b3f20(obj);','    void *file = func_ov007_020b3f20(obj);')
        view = view.replace('void* m = func_ov007_020c99d8();','void* m = (void *)func_ov007_020c99d8((int)file);')
    if rel == 'src/unnamed/ov007/func_ov007_020c798c.c':
        view = view.replace('extern void func_ov007_020c78b0(void);','extern void func_ov007_020c78b0(void *);')
        view = view.replace('    func_ov007_020c78b0();','    func_ov007_020c78b0(t);')
    if rel == 'src/unnamed/ov007/func_ov007_020b3360.c':
        # 020b3380..339c forwards previousState in r1, except ff -> 1.
        view = view.replace('extern void func_ov007_020b3c54(void);','extern void func_ov007_020b3c54(int, int);')
        view = view.replace('        func_ov007_020b3c54();',
                            '        func_ov007_020b3c54(0, p0->previousState == 0xff ? 1 : p0->previousState);')
    if rel == 'src/unnamed/ov007/func_ov007_020b3c54.c':
        # 020b3d14 stores mode; its BX forwards the existing r1.
        view = view.replace('extern void func_ov007_020b3d30(int a);','extern void func_ov007_020b3d30(int a, int mode);')
        view = view.replace('func_ov007_020b3d30(*(int*)(data_ov007_02103360 + 4));',
                            'func_ov007_020b3d30(*(int*)(data_ov007_02103360 + 4), mode);')
    if rel == 'src/unnamed/ov007/func_ov007_020b7090.c':
        view = view.replace('extern void *func_ov007_020bdebc(void);','extern void func_ov007_020bdebc(void);')
        view = view.replace('extern void func_ov007_020aed98(void *thiz);','extern void func_ov007_020aed98(void);')
        view = view.replace('func_ov007_020aed98(func_ov007_020bdebc());',
                            'func_ov007_020bdebc();\n    func_ov007_020aed98();')
    # Undefined paths in the recovered source must not become invented native
    # success values. Callers supply these finite state/index domains.
    if rel == 'src/unnamed/ov007/func_ov007_020beeb0.c':
        view = view.replace('    default:\n        break;',
                            '    default:\n        throw std::runtime_error("Invalid title mesh state");')
    if rel == 'src/unnamed/ov007/func_ov007_020b1718.c':
        view = view.replace('    }\n\n    if (flag != 0)',
                            '    default: throw std::runtime_error("Invalid title transition mode");\n    }\n\n    if (flag != 0)')
    if rel == 'src/unnamed/ov007/func_ov007_020b2454.c':
        view = view.replace('    }\n    AddVec3',
                            '    default: throw std::runtime_error("Invalid title figure mode");\n    }\n    AddVec3')
    if rel == 'src/unnamed/ov007/func_ov007_020ca308.c':
        view = view.replace('    }\n\n    if (cur >= 0',
                            '    } else { throw std::runtime_error("Invalid title wave index"); }\n\n    if (cur >= 0')
    if rel == 'src/unnamed/ov007/func_ov007_020c30ac.cpp':
        view = view.replace('    int bestIdx;','    int bestIdx = -1;')
        view = view.replace('        return bestIdx;',
                            '        if (bestIdx < 0) throw std::runtime_error("Empty title nearest-point search");\n        return bestIdx;')
    if rel == 'src/engine/model/ModelComponents.cpp':
        edits = []
        methods = {'UpdateBones':'_ZN15ModelComponents11UpdateBonesEP8BCA_Filei',
                   'UpdateVertsUsingBones':'_ZN15ModelComponents21UpdateVertsUsingBonesEv',
                   'Render':'_ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3'}
        def visit(node):
            if node.type == 'function_definition':
                decl = node.child_by_field_name('declarator')
                match = re.match(r'ModelComponents::(\w+)\((.*)\)',raw[decl.start_byte:decl.end_byte],re.S)
                if match and match[1] in methods:
                    body = node.child_by_field_name('body')
                    content = re.sub(r'\bthis\b','self',raw[body.start_byte:body.end_byte])
                    for field in ('modelFile','materials','transforms'):
                        content = re.sub(r'(?<![.>])\b'+field+r'\b','self->'+field,content)
                    if match[1] == 'Render':
                        old = '((void (*)(void *))func_02044b30)(self)'
                        if content.count(old)!=2: raise ValueError('material index forwarding changed')
                        content = content.replace(old,'func_02044b30((char *)self, idx)')
                    params = 'ModelComponents *self'+(', '+match[2] if match[2] else '')
                    edits.append((decl.start_byte,body.end_byte,methods[match[1]]+'('+params+') '+content))
            for child in node.named_children: visit(child)
        visit(parser.parse(raw.encode()).root_node)
        if len(edits)!=3: raise ValueError('model component method set changed')
        for first,last,content in sorted(edits,reverse=True): view = view[:first]+content+view[last:]
        view = view.replace('((ModelComponents *)self)->UpdateVertsUsingBones()',
                            methods['UpdateVertsUsingBones']+'((ModelComponents *)self)')
        view = view.replace('#include "dExtModel_c.h"','#include "dExtModel_c.h"\nextern void '+
                            methods['UpdateVertsUsingBones']+'(ModelComponents *);')
    if full_particles and rel == 'include/Particle__System.h':
        view = raw.replace('#include "Particle__SysTracker.h"','namespace Particle { struct SysTracker; }')
        view = re.sub(r'^    static [^;]+;','',view,flags=re.M)
        view = re.sub(r'#ifndef SM64DS_PLATFORM_PC\s*','',view)
        # Keep the real System extent assertion active in its explicit POD view.
        view = view.replace('#endif\n\n}\n','\n}\n')
    if full_particles and rel == 'src/engine/particle/Particle_Manager.cpp':
        prefix = raw[:raw.index('namespace Particle {\nstruct EffectData;')]
        records = raw[raw.index('typedef void (*BehaviorFunc)'):raw.index('extern "C" {',raw.index('struct Flags2'))]
        records = records.replace('Particle::EffectData &','Particle::EffectData *').replace('Vector3 &','Vector3 *')
        callbacks = 'namespace Particle { union EffectData;\n'+''.join(
            'extern void '+symbol+'(EffectData *, char *, Vector3 *);\n' for symbol in behaviors.values())+'}\n'
        tail = raw[raw.index('// @symbol func_0204a4c8'):]
        tail = 'extern "C" {\n'+tail
        tail = tail.replace('System *Manager::AddSystem(int definitionID, Vector3 &position)',
                            'System *_ZN8Particle7Manager9AddSystemEiR7Vector3(Manager *self, int definitionID, Vector3 *position)')
        a = tail.index('System *_ZN8Particle7Manager9AddSystemEiR7Vector3')
        b = tail.index('\n}\n',a)+3
        method = tail[a:b].replace('&position','position')
        for field in ('mFreeSystems','mDefinitions','unk_02c','unk_02e','mActiveSystems'):
            method = re.sub(r'(?<![.>])\b'+field+r'\b','self->'+field,method)
        tail = tail[:a]+method+tail[b:]
        for name,symbol in behaviors.items(): tail = tail.replace('Particle::'+name+'::Func','Particle::'+symbol)
        view = prefix+'using namespace Particle;\n'+callbacks+records+tail
    return view
