"""Statically lower complete hash-pinned MSL arithmetic assembly to C++.

Every input instruction is accounted for; unsupported syntax fails generation.
Direct source branches become C++ gotos. Scalar entry adapters preserve bits,
carry, status writes, exception paths, and all internal arithmetic entry points.
"""
import re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
NAMES=['_dadd','_dmul','_ll_sdiv','_ll_udiv','_s32_div_f','_u32_div_f','_ull_mod','func_01ff859c','func_01ff8e10','func_01ff9378','func_01ff97d8','func_01ffa344','func_01ffa440','func_01ffa4bc','func_01ffa594','func_01ffafd4','func_01ffb008','func_01ffb030']
PATHS={n:('src/'+n+'.c' if not n.startswith('func_') else 'src/unnamed/arm9/01ff/'+n+'.c') for n in NAMES}
CORE={'_dadd_core':('_dadd',0x10),'_dsub_core':('func_01ff8e10',0x10),'_fadd_core':('func_01ff9378',0x14),'_fsub_core':('func_01ffa594',0x14),'_ull_mod_core':('_ull_mod',8),'_ll_sdiv_core':('_ll_sdiv',0x10),'_ll_sdiv_L074':('_ll_sdiv',0x74),'_u32_div_f_core':('_u32_div_f',8),'func_01ffa538':('func_01ffa4bc',0x7c),'func_01ffa558':('func_01ffa4bc',0x9c)}
CONDITION={'eq':'w.z','ne':'!w.z','hs':'w.c','lo':'!w.c','mi':'w.n','pl':'!w.n','vs':'w.v','vc':'!w.v','hi':'w.c&&!w.z','ls':'!w.c||w.z','ge':'w.n==w.v','lt':'w.n!=w.v','gt':'!w.z&&w.n==w.v','le':'w.z||w.n!=w.v','al':'true'}
OPS=['umull','stmdb','ldmia','clz','mov','mvn','and','orr','eor','bic','add','adc','sub','sbc','rsb','rsc','cmp','cmn','tst','teq','ldr','str','bl','bx','b']

def parse(name):
 raw=(ROOT/PATHS[name]).read_text();raw=re.sub(r'/\*.*?\*/|//[^\n]*','',raw,flags=re.S)
 m=re.search(r'asm\s+[^{}]+\{(.*)\}',raw,re.S);assert m,name
 lines=[];labels={}
 for line in m[1].splitlines():
  line=line.strip().rstrip(';')
  if not line:continue
  if line.endswith(':'):labels[line[:-1]]=len(lines);continue
  lines.append(line)
 return lines,labels

def register(s):
 s=s.strip();aliases={'ip':12,'sp':13,'lr':14,'pc':15}
 if s in aliases:return aliases[s]
 m=re.fullmatch(r'r(\d+)',s)
 if not m:raise ValueError('not a register '+s)
 return int(m[1])
def reg(s):return 'w.r[%d]'%register(s)
def scalar(s):
 s=s.strip()
 if s.startswith(('#','=')):return 'uint32_t('+s[1:]+')'
 return reg(s)
def split(s):return [x.strip() for x in re.split(r',\s*(?![^\[{}]*[\]}])',s)]
def operand(parts,flags=False):
 if len(parts)==1:return scalar(parts[0])
 assert len(parts)==2,parts
 if parts[1]=='rrx':return 'w.rrx(%s,%s)'%(scalar(parts[0]),str(flags).lower())
 m=re.fullmatch(r'(lsl|lsr|asr)\s+(.+)',parts[1]);assert m,parts
 return 'w.shift(%s,%s,%s,%s,%s)'%(scalar(parts[0]),scalar(m[2]),{'lsl':0,'lsr':1,'asr':2}[m[1]],str(flags).lower(),str(m[2].startswith('#')).lower())
def decode(mn):
 m=re.fullmatch(r'ldm(eq|ne|lo|mi)ia',mn)
 if m:return 'ldmia',m[1],False
 for op in OPS:
  if not mn.startswith(op):continue
  suffix=mn[len(op):];cond='al';flags=False
  if suffix not in CONDITION and suffix.endswith('s'):suffix=suffix[:-1];flags=True
  if suffix in CONDITION:cond=suffix
  elif suffix:continue
  return op,cond,flags
 raise ValueError('unsupported opcode '+mn)
def lower(name):
 lines,labels=parse(name);out=[]
 def branch(target,tail=True):
  if target in labels:return 'goto L%d;'%labels[target]
  other,offset=CORE.get(target,(target,0));assert other in NAMES,target
  return ('return ' if tail else '')+'run_'+other+'(w,%d);'%(offset//4)
 def instruction(line,index):
  mn,_,args=line.partition(' ');op,cond,flags=decode(mn);p=split(args);s=''
  if op in ('b','bl'):
   if op=='bl' and p[0] in ('func_0207322c','func_02073238'):s='w.r[0]=0x020aa3f4u;' if p[0].endswith('2c') else 'w.r[0]=0x020aa3f8u;'
   elif op=='bl' and re.fullmatch('func_0206[0-9a-f]+',p[0]):s='exception(w,0x%su);'%p[0][-8:]
   else:s=branch(p[0],op=='b')
  elif op=='bx':assert p==['lr'],line;s='return;'
  elif op in ('stmdb','ldmia'):
   assert p[0]=='sp!',line
   rs=sorted(register(x.strip()) for x in p[1].strip('{}').split(','))
   s='{const uint32_t at=w.r[13]%s; '%('-'+str(len(rs)*4) if op=='stmdb' else '')
   for j,r in enumerate(rs):s+=('w.write(at+%d,w.r[%d]); '%(j*4,r) if op=='stmdb' else 'w.r[%d]=w.read(at+%d); '%(r,j*4))
   s+='w.r[13]=at%s;}'%('' if op=='stmdb' else '+'+str(len(rs)*4))
  elif op in ('ldr','str'):
   if p[1].startswith('='):s=reg(p[0])+'='+scalar(p[1])+';'
   else:
    m=re.fullmatch(r'\[([^,]+)(?:,\s*(#[^\]]+))?\]',p[1]);assert m,line
    address=reg(m[1])+('+'+scalar(m[2]) if m[2] else '')
    s=reg(p[0])+'=w.read('+address+');' if op=='ldr' else 'w.write('+address+','+reg(p[0])+');'
  elif op=='clz':s=reg(p[0])+'='+reg(p[1])+'?__builtin_clz('+reg(p[1])+'):32;'
  elif op=='umull':s='{const uint64_t result=uint64_t('+reg(p[2])+')*'+reg(p[3])+';'+reg(p[0])+'=uint32_t(result);'+reg(p[1])+'=uint32_t(result>>32);}'
  elif op in ('add','adc','sub','sbc','rsb','rsc','cmp','cmn'):
   compare=op in ('cmp','cmn');a=scalar(p[0] if compare else p[1]);b=operand(p[1:] if compare else p[2:]);flag=flags or compare
   if op in ('rsb','rsc'):a,b=b,a
   if op in ('sub','sbc','rsb','rsc','cmp'):b='~('+b+')'
   carry='w.c' if op in ('adc','sbc','rsc') else ('true' if op in ('sub','rsb','cmp') else 'false')
   expr='w.sum(%s,%s,%s,%s)'%(a,b,carry,str(flag).lower())
   if not compare and p[0]=='pc':
    assert op=='add' and p[1]=='pc',line
    expr='uint32_t(%d)+(%s)/4'%(index+2,operand(p[2:]));s='switch('+expr+'){'
    for i in range(len(lines)):s+='case %d:goto L%d;'%(i,i)
    s+='default:throw std::runtime_error("Invalid unrolled arithmetic branch");}'
   else:s=('(void)' if compare else reg(p[0])+'=')+expr+';'
  else:
   unary=op in ('mov','mvn');compare=op in ('tst','teq')
   value=operand(p[1:] if unary or compare else p[2:],flags or compare)
   if op=='mvn':value='~('+value+')'
   if not unary:
    a=scalar(p[0] if compare else p[1]);operator={'and':'&','tst':'&','orr':'|','eor':'^','teq':'^','bic':'&~'}[op];value='('+a+')'+operator+'('+value+')'
   s='{const uint32_t value='+value+';'+('' if compare else reg(p[0])+'=value;')+('w.nz(value);' if flags or compare else '')+'}'
  return ('if('+CONDITION[cond]+'){'+s+'}' if cond!='al' else s)
 out.append('void run_'+name+'(Work &w,unsigned entry){switch(entry){')
 for i in range(len(lines)):out.append('case %d:goto L%d;'%(i,i))
 out.append('default:throw std::runtime_error("Invalid arithmetic entry");}')
 for i,line in enumerate(lines):out.append('L%d: %s // %s'%(i,instruction(line,i),line))
 out.append('throw std::runtime_error("Arithmetic source fell through its end");}')
 return '\n'.join(out)
def wrapper(name):
 if name in ('_dadd','_dmul','func_01ff8e10'):return 'double %s(double a,double b){Work w;uint64_t x,y;std::memcpy(&x,&a,8);std::memcpy(&y,&b,8);w.pair(x);w.pair(y,2);run_%s(w,0);x=w.pair();std::memcpy(&a,&x,8);return a;}'%(name,name)
 if name=='func_01ff859c':return 'uint64_t %s(double a){Work w;uint64_t x;std::memcpy(&x,&a,8);w.pair(x);run_%s(w,0);return w.pair();}'%(name,name)
 if name in ('_ll_sdiv','_ll_udiv','_ull_mod'):return 'uint64_t %s(uint64_t a,uint64_t b){Work w;w.pair(a);w.pair(b,2);run_%s(w,0);return w.pair();}'%(name,name)
 if name=='func_01ffafd4':return 'void %s(){Work w;run_%s(w,0);}'%(name,name)
 if name in ('func_01ffa344','func_01ffa440','func_01ffa4bc'):return 'uint32_t %s(uint32_t a){Work w;w.r[0]=a;run_%s(w,0);return w.r[0];}'%(name,name)
 return 'uint32_t %s(uint32_t a,uint32_t b){Work w;w.r[0]=a;w.r[1]=b;run_%s(w,0);return w.r[0];}'%(name,name)
def generate(path):
 name=Path(path).stem;assert PATHS.get(name)==path
 declarations='\n'.join('void run_'+n+'(Work &,unsigned);' for n in NAMES)
 return '#include "platform/soft_math.h"\nnamespace sm64ds::platform::soft_math{\n'+declarations+'\n'+lower(name)+'\n}\nusing namespace sm64ds::platform::soft_math;\nextern "C"{\n'+wrapper(name)+'\n}\n'
