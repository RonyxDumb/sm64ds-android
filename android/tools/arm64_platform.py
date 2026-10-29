"""Complete native implementations of the hash-pinned ARM9/BIOS source units."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BODIES={}
def entry(path,body):BODIES[path]=body
for name,size in [('Copy32Bytes',32),('Copy36Bytes',36),('Copy48Bytes',48)]:
 entry('src/'+name+'.c','extern "C" void '+name+'(const void *src,void *dst){copy_words(src,dst,'+str(size)+');}')
entry('src/Copy48BytesFixed.c','extern "C" void Copy48BytesFixed(const void *src,void *dst){copy_fixed(src,dst,48,12);}')
entry('src/CpuCopy8.c','extern "C" void CpuCopy8(const void *src,void *dst,uint32_t size){std::memmove(dst,src,size);}')
for name in ['MultiCopy_Int','MultiCopy32Bytes']:
 entry('src/'+name+'.c','extern "C" void '+name+'(const void *src,void *dst,uint32_t bytes){copy_words(src,dst,bytes);}')
for name in ['MultiStore_Int','MultiStore32Bytes']:
 entry('src/'+name+'.c','extern "C" void '+name+'(uint32_t value,void *dst,uint32_t bytes){fill_words(value,dst,bytes);}')
for name,fast in [('CpuSet',False),('CpuFastSet',True)]:
 entry('src/'+name+'.c','extern "C" void '+name+'(const void *src,void *dst,uint32_t control){cpu_set(src,dst,control,'+str(fast).lower()+');}')
entry('src/BitUnPack.c','extern "C" void BitUnPack(const void *src,void *dst,const BitUnpackInfo *info){unpack_bits(src,dst,*info);}')
entry('src/GetCRC16.c','extern "C" uint16_t GetCRC16(uint16_t crc,const void *data,uint32_t size){return crc16(crc,data,size);}')
entry('src/Sqrt.c','extern "C" uint32_t Sqrt(uint32_t n){return integer_sqrt(n);}')
entry('src/Div.c','extern "C" int32_t Div(int32_t a,int32_t b){return bios_div(a,b);}')
entry('src/Mod.c','extern "C" int32_t Mod(int32_t a,int32_t b){return bios_mod(a,b);}')
entry('src/WaitByLoop.c','extern "C" void WaitByLoop(uint32_t cycles){delay(cycles);}')
entry('src/IntrWait.c','extern "C" void IntrWait(bool discard,uint32_t mask){wait_interrupt(discard,mask);}')
entry('src/VBlankIntrWait.c','extern "C" void VBlankIntrWait(){wait_interrupt(true,ntr::IRQ_VBLANK);}')
entry('src/Halt.c','extern "C" void Halt(){wait_interrupt(false,uint32_t(ntr::io_read(0x04000210,4)));}')
entry('src/IsDebugger.c','extern "C" int IsDebugger(){return ntr::MAIN_SIZE==0x00800000u?0:1;}')
def matrix(path,name,params,values):
 entry(path,'extern "C" void '+name+'(int32_t *m'+params+'){const int32_t values[]={'+values+'};std::memcpy(m,values,sizeof values);}')
matrix('src/Matrix3x3_LoadIdentity.c','Matrix3x3_LoadIdentity','','4096,0,0,0,4096,0,0,0,4096')
matrix('src/Matrix4x3_LoadIdentity.c','Matrix4x3_LoadIdentity','','4096,0,0,0,4096,0,0,0,4096,0,0,0')
matrix('src/Matrix3x3_SetRotationZ.c','Matrix3x3_SetRotationZ',',int32_t s,int32_t c','c,s,0,int32_t(0u-uint32_t(s)),c,0,0,0,4096')
prefix='src/unnamed/arm9/0205/func_'
entry(prefix+'02052514.c','extern "C" void func_02052514(const int32_t *src,int32_t *dst){int32_t values[16]={};for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)values[r*4+c]=src[r*3+c];values[15]=4096;std::memcpy(dst,values,sizeof values);}')
matrix(prefix+'0205256c.c','func_0205256c',',int32_t s,int32_t c','c,0,int32_t(0u-uint32_t(s)),0,4096,0,s,0,c')
matrix(prefix+'020527e8.c','func_020527e8',',int32_t x,int32_t y,int32_t z','x,0,0,0,y,0,0,0,z,0,0,0')
matrix(prefix+'02052800.c','func_02052800',',int32_t s,int32_t c','4096,0,0,0,c,s,0,int32_t(0u-uint32_t(s)),c,0,0,0')
matrix(prefix+'02052820.c','func_02052820',',int32_t s,int32_t c','c,0,int32_t(0u-uint32_t(s)),0,4096,0,s,0,c,0,0,0')
matrix(prefix+'0205283c.c','func_0205283c',',int32_t s,int32_t c','c,s,0,int32_t(0u-uint32_t(s)),c,0,0,0,4096,0,0,0')
matrix(prefix+'02052ec8.c','func_02052ec8','','4096,0,0,0,0,4096,0,0,0,0,4096,0,0,0,0,4096')
entry(prefix+'020553c0.c','extern "C" void func_020553c0(void *port){const uint32_t zeros[128]={};copy_fixed(zeros,port,512,16);}')
entry(prefix+'02059d98.c','extern "C" void func_02059d98(void *p,uint32_t size){std::memset(p,0,size&~31u);}')
entry(prefix+'0205a588.c','extern "C" void func_0205a588(void *dst,uint32_t byte,uint32_t size){std::memset(dst,int(byte&255),size);}')
entry(prefix+'0205a74c.c','extern "C" uint32_t func_0205a74c(uint32_t value,uint32_t *word){return __atomic_exchange_n(word,value,__ATOMIC_SEQ_CST);}')
entry(prefix+'02059d8c.c','extern "C" uint32_t func_02059d8c(uint32_t count){while(count>=4){count-=4;std::atomic_signal_fence(std::memory_order_seq_cst);}return count-4;}')
entry(prefix+'02057014.c','extern "C" int func_02057014(uint16_t id){return code<int(*)(uint16_t)>(0x02057178)(id);}')
entry(prefix+'02057020.c','''extern "C" int func_02057020(){std::lock_guard<std::mutex> guard(Services::current().lock);for(unsigned bank=0;bank<2;++bank){uint32_t address=0x027fffb0+bank*4;auto word=load<uint32_t>(address);if(word){unsigned bit=__builtin_clz(word);store(address,word&~(0x80000000u>>bit));return int(0x40+bank*0x20+bit);}}return -3;}''')
entry(prefix+'02057078.c','''extern "C" void func_02057078(unsigned id){if(id<0x40||id>=0x80)throw std::out_of_range("Invalid lock ID");std::lock_guard<std::mutex> guard(Services::current().lock);unsigned bank=id>=0x60;uint32_t address=0x027fffb0+bank*4;store(address,load<uint32_t>(address)|(0x80000000u>>(id-(bank?0x60:0x40))));}''')
entry('src/__rethrow.c','extern "C" [[noreturn]]void __rethrow(){throw;}')
entry('src/unnamed/arm9/0207/func_02071974.c','extern "C" void func_02071974(const char *character){std::fputc(static_cast<unsigned char>(*character),stderr);std::fflush(stderr);}')
entry('src/unnamed/arm9/0207/func_02071988.c','extern "C" int func_02071988(){return std::fgetc(stdin);}')
entry('src/unnamed/arm9/0207/func_0207199c.c','extern "C" [[noreturn]]void func_0207199c(){std::terminate();}')
entry('src/IRQ3.cpp','''namespace IRQ{unsigned Enable(){return mask_update(0x80,0);}unsigned Disable(){return mask_update(0x80,0x80);}unsigned Restore(unsigned s){return mask_update(0x80,s);}unsigned DisableAll(){return mask_update(0xc0,0xc0);}unsigned RestoreAll(unsigned s){return mask_update(0xc0,s);}}
extern "C" int ARMProcessorMode(){return processor_status()&31;}
namespace CP15{void WaitForInterrupt(){wait_interrupt(false,uint32_t(ntr::io_read(0x04000210,4)));}}''')
for name,bit in [('0206d9cc',0x40),('0206da18',0x80)]:
 kind='FIQ' if bit==0x40 else 'IRQ'
 entry('src/unnamed/arm9/0206/func_'+name+'.cpp','namespace IRQ{bool Set'+kind+'State(bool enabled){return !(mask_update('+str(bit)+',enabled?0:'+str(bit)+')&'+str(bit)+');}}extern "C" int func_'+name+'(){return IRQ::Set'+kind+'State(false);}')
entry('src/engine/cp15/CP15.cpp','''namespace CP15{void FlushDataCache(){cache_fence();}void FlushAndInvalidateDataCache(){cache_fence();}void InvalidateDataCache(unsigned,unsigned){cache_fence();}void FlushDataCache(unsigned,unsigned){cache_fence();}void FlushAndInvalidateDataCache(unsigned,unsigned){cache_fence();}void DrainWriteBuffer(){cache_fence();}void InvalidateInstructionCache(unsigned address,unsigned size){invalidate_instructions(address,size);}}''')
entry('src/engine/cp15/CP154.cpp','''extern "C" int func_0206da64(){return Services::current().control.fetch_or(1)&1;}extern "C" void func_0206da94(){cache_fence();}extern "C" void func_0206da9c(){cache_fence();}namespace CP15{unsigned MPUGetDataRegion7(){return Services::current().regions[7].load();}void MPUDataRegion7(unsigned v){Services::current().regions[7]=v;cache_fence();}}''')
entry('src/engine/cp15/CP15_MPUDataRegion1.cpp','namespace CP15{void MPUDataRegion1(unsigned v){Services::current().regions[1]=v;cache_fence();}}')
entry('src/engine/cp15/CP15_ProtectionUnit.cpp','namespace CP15{void EnableMPU(){Services::current().control.fetch_or(1);cache_fence();}void DisableMPU(){Services::current().control.fetch_and(~1u);cache_fence();}}')
entry('src/engine/cp15/CP15_TCM.cpp','namespace CP15{void EnableDTCM(){Services::current().control.fetch_or(0x10000);cache_fence();}unsigned GetDTCMBaseAddress(){return Services::current().dtcm.load()&0xfffff000u;}}')
entry('src/engine/cp15/CP15_SystemSetup.cpp','''namespace CP15{void SystemSetup(){auto &s=Services::current();s.control.fetch_and(~0xf9005u);cache_fence();const uint32_t regions[]={0x04000033,0x0200002b,0x027c0023,0x08000035,0x023c001b,0x0100002f,0xffff001d,0x027ff017};for(unsigned i=0;i<8;++i)s.regions[i]=regions[i];s.itcm=0x20;s.dtcm=0x023c000a;const uint32_t attributes[]={0x42,0x42,2,0x05100011,0x15111011};for(unsigned i=0;i<5;++i)s.attributes[i]=attributes[i];s.control.fetch_or(0x5707d);cache_fence();}}''')
for name,kind,width in [('DecompressLZ16','Lz77',2),('LZ77UnCompReadNormalWrite8bit','Lz77',1),('RLUnCompReadNormalWrite8bit','Rle',1)]:
 entry('src/'+name+'.c','#include "platform/decompression.h"\nextern "C" void '+name+'(const void *src,void *dst){decompress(src,dst,Compression::'+kind+','+str(width)+');}')
for name,kind,width in [('LZ77UnCompReadByCallbackWrite16bit','Lz77',2),('RLUnCompReadByCallbackWrite16bit','Rle',2),('HuffUnCompReadByCallback','Huffman',4)]:
 entry('src/'+name+'.c','#include "platform/decompression.h"\nextern "C" int '+name+'(const void *src,void *dst,uint32_t argument,const DecompressionStream *callbacks){return decompress(src,dst,Compression::'+kind+','+str(width)+',argument,callbacks);}')
entry('src/unnamed/arm9/0200/func_020048d8.c','#include "platform/decompression.h"\nextern "C" void func_020048d8(void *end){decompress_backwards(static_cast<uint8_t*>(end));}')
entry('src/unnamed/arm9/0200/func_0200497c.c','''extern "C" void func_0200497c(){uint32_t at=load<uint32_t>(0x02004ad8),end=load<uint32_t>(0x02004adc),source=load<uint32_t>(0x02004ae0);while(at!=end){uint32_t destination=load<uint32_t>(at),copy=load<uint32_t>(at+4),zero=load<uint32_t>(at+8);at+=12;for(uint64_t i=0;i<copy;i+=4){store(destination+uint32_t(i),load<uint32_t>(source));source+=4;}destination+=copy;for(uint64_t i=0;i<zero;i+=4)store<uint32_t>(destination+uint32_t(i),0);}for(uint32_t at=load<uint32_t>(0x02004ae4),end=load<uint32_t>(0x02004ae8);at<end;at+=4)store<uint32_t>(at,0);call(0x020049ec);}''')
entry('src/ARMSaveContext.c','#include "platform/context_save.inc"')
entry('src/ARMRestoreContext.c','#include "platform/context_restore.inc"')
entry(prefix+'02058568.c','#include "platform/native_context.h"\nextern "C" void func_02058568(void *ctx,uint32_t pc,uint32_t sp){init_context(ctx,pc,sp);}')
entry(prefix+'02059824.c','extern "C" void func_02059824(){call(0x02059834);}')
entry('src/unnamed/arm9/0207/func_02071790.c','#include <unwind.h>\nextern "C" [[noreturn]]void func_02071790(_Unwind_Exception *exception){_Unwind_Resume(exception);}')
entry('src/IRQHandler.cpp','''#include "platform/native_context.h"
extern "C" void func_01ffd9d4();
namespace IRQ{void UserInterruptHandler(){if(!ntr::io_read(0x04000208,4))return;uint32_t pending=uint32_t(ntr::io_read(0x04000210,4)&ntr::io_read(0x04000214,4));if(!pending)return;unsigned bit=__builtin_ctz(pending);ntr::io_write(0x04000214,1u<<bit,4);call(load<uint32_t>(0x02099fe4+4*bit));signal_interrupt(1u<<bit);func_01ffd9d4();}}
extern "C" void func_01ffd9d4(){uint16_t wake=load<uint16_t>(0x023c0000);if(wake){store<uint16_t>(0x023c0000,0);store<uint16_t>(0x020a6134,1);for(uint32_t at=load<uint32_t>(0x020a6140);at;at=load<uint32_t>(at+0x68)){unsigned bit=load<uint32_t>(at+0x6c);if(bit<32&&(wake&(1u<<bit)))store<uint32_t>(at+0x64,1);}}if(!load<uint16_t>(0x020a6134))return;store<uint16_t>(0x020a6134,0);uint32_t next=load<uint32_t>(0x020a6140);while(next&&load<uint16_t>(next+0x64)!=1)next=load<uint32_t>(next+0x68);uint32_t current=load<uint32_t>(0x020a613c);if(!next||next==current)return;auto hook=load<uint32_t>(0x020a6144);if(hook)code<void(*)(uint32_t,uint32_t)>(hook)(current,next);void *oldContext=packed::Space::current().data(current,0x64),*newContext=packed::Space::current().data(next,0x64);if(!ARMSaveContext(oldContext)){store<uint32_t>(0x020a613c,next);ARMRestoreContext(newContext);}}''')
entry(prefix+'0205947c.c','''#include "platform/native_context.h"
extern "C" void func_0205947c(int reason){auto *p=packed::Space::current().data(0x020a63ac,0x80);capture_fault(p,reason);store<uint32_t>(0x020a63ac+0x6c,uint32_t(reason));store<uint32_t>(0x020a63ac+0x74,processor_status());}''')
entry(prefix+'0205950c.c','''extern "C" void func_0205950c(){auto handler=load<uint32_t>(0x020a63a0);if(!handler)return;mask_update(0x80,0x80);Services::current().control.fetch_or(1);code<void(*)(void*,uint32_t)>(handler)(packed::Space::current().data(0x020a63ac,0x80),load<uint32_t>(0x020a63a4));Services::current().control.fetch_and(~1u);cache_fence();}''')
entry(prefix+'02059468.c','''extern "C" void func_0205947c(int);extern "C" void func_0205950c();extern "C" void func_02059468(){func_0205947c(int(processor_status()&31));func_0205950c();}''')
entry(prefix+'020593f4.c','''extern "C" void func_02059468();extern "C" [[noreturn]]void func_020593f4(){auto handler=load<uint32_t>(0x020a63a8);if(handler)call(handler);func_02059468();std::terminate();}''')
entry('src/Entry.c','''extern "C" void func_020048d8(void *);extern "C" void func_0200497c();extern "C" void func_01ffafd4();namespace CP15{void SystemSetup();}
extern "C" void Entry(){ntr::io_write(0x04000208,0x04000000,4);CP15::SystemSetup();auto packedEnd=load<uint32_t>(0x02004aec);func_020048d8(packedEnd?packed::Space::current().data(packedEnd,0):nullptr);func_0200497c();fill_words(0,packed::Space::current().data(0x023c0000,0x4000),0x4000);fill_words(0,ntr::host_ptr(0x05000000),0x400);fill_words(0x200,ntr::host_ptr(0x07000000),0x400);store<uint32_t>(0x023c3ffc,0x01ffd97c);func_01ffafd4();call(0x02019780);call(0x02072f94);call(0x02007000);}''')
entry('src/unnamed/arm9/01ff/func_01ffdb28.c','''extern "C" void func_01ffdb28(){ntr::io_write(0x04000208,0x04000000,4);store<uint32_t>(0x023c3ffc,0);while((ntr::io_read(0x04000180,2)&15)!=1)wait_interrupt(false,0xffffffffu);ntr::io_write(0x04000180,0x100,2);auto preserved=load<uint32_t>(0x027ffd9c);std::memset(packed::Space::current().data(0x027ffd80,0x80),0,0x80);store(0x027ffd9c,preserved);std::memset(packed::Space::current().data(0x027fff80,0x80),0,0x80);while((ntr::io_read(0x04000180,2)&15)==1)wait_interrupt(false,0xffffffffu);ntr::io_write(0x04000180,0,2);call(load<uint32_t>(0x027ffe24));}''')
entry('src/SoftReset.c','extern "C" void func_01ffdb28();extern "C" void SoftReset(){func_01ffdb28();}')
def generate(path):
 if path not in BODIES:raise ValueError('unported ARM9 unit: '+path)
 return '#include "platform/arm9_services.h"\nusing namespace sm64ds::platform;\nnamespace packed=sm64ds::packed;\n'+BODIES[path]+'\n'

def runtime_sources(out):
 import hashlib,json
 from arm64_soft_math import PATHS,generate as soft_generate
 profile=json.loads((ROOT/'android/tools/arm64_migration_profile.json').read_text())
 # Existing validated native ownership wrappers provide these six symbols.
 # All six still participate in the complete compiler census.
 owned={'src/CpuCopy8.c','src/MultiCopy_Int.c','src/DecompressLZ16.c','src/unnamed/arm9/0205/func_0205a588.c','src/unnamed/arm9/0205/func_02057020.c','src/unnamed/arm9/0205/func_02057078.c'}
 selected={path:generate for path in BODIES if path not in owned}
 selected.update({path:soft_generate for path in PATHS.values()});paths=[]
 for path,generator in selected.items():
  if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=profile['sources'][path]['sha256']:raise ValueError('native platform source changed: '+path)
  file=out/(Path(path).stem+'.cpp');file.parent.mkdir(parents=True,exist_ok=True);file.write_text(generator(path));paths.append(file.resolve())
 (out/'sources.cmake').write_text('set(NATIVE_PLATFORM_SOURCES\n'+''.join('    "'+str(p)+'"\n' for p in paths)+')\n')
 (out/'inputs.cmake').write_text('set(NATIVE_PLATFORM_INPUTS\n'+''.join('    "'+str(ROOT/p)+'"\n' for p in selected)+')\n')
if __name__=='__main__':
 import argparse
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--runtime-out',type=Path,required=True)
 runtime_sources(parser.parse_args().runtime_out.resolve())
