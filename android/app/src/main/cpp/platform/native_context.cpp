#include "native_context.h"
#include <map>
namespace sm64ds::platform {
namespace {
std::mutex contextsMutex;std::map<void*,std::unique_ptr<NativeContext>> contexts;
thread_local NativeContext *starting=nullptr;
void math_save(void *guest){auto *p=static_cast<uint8_t*>(guest)+0x48;const uint64_t numbers[]={ntr::io_read(0x04000290,8),ntr::io_read(0x04000298,8),ntr::io_read(0x040002b8,8)};std::memcpy(p,numbers,24);const uint16_t modes[]={uint16_t(ntr::io_read(0x04000280,2)&3),uint16_t(ntr::io_read(0x040002b0,2)&1)};std::memcpy(p+24,modes,4);}
void math_restore(const void *guest){auto *p=static_cast<const uint8_t*>(guest)+0x48;uint64_t n[3];uint16_t mode[2];std::memcpy(n,p,24);std::memcpy(mode,p+24,4);ntr::io_write(0x04000290,n[0],8);ntr::io_write(0x04000298,n[1],8);ntr::io_write(0x04000280,mode[0],2);ntr::io_write(0x040002b8,n[2],8);ntr::io_write(0x040002b0,mode[1],2);}
[[noreturn]]void begin_context(){auto *current=starting;if(!current)std::terminate();uint32_t argument;std::memcpy(&argument,static_cast<uint8_t*>(current->guest)+4,4);code<void(*)(uint32_t)>(current->entry)(argument);std::terminate();}
}
NativeContext &context(void *guest,bool create){std::lock_guard<std::mutex> guard(contextsMutex);auto it=contexts.find(guest);if(it!=contexts.end())return *it->second;if(!create)throw std::runtime_error("Restoring an uninitialized native context");auto result=std::make_unique<NativeContext>();result->guest=guest;auto *pointer=result.get();contexts.emplace(guest,std::move(result));return *pointer;}
void init_context(void *guest,uint32_t entry,uint32_t stack){
 auto &native=context(guest,true);native.state.fill(0);native.entry=entry;native.saved=false;native.owner={};
 native.stack.resize(1024*1024);uintptr_t top=reinterpret_cast<uintptr_t>(native.stack.data()+native.stack.size())&~uintptr_t(15);
#if defined(__aarch64__)
 native.state[11]=reinterpret_cast<uintptr_t>(&begin_context);native.state[12]=top;
#elif defined(__x86_64__)
 top-=8;*reinterpret_cast<uintptr_t*>(top)=0;native.state[6]=top;native.state[7]=reinterpret_cast<uintptr_t>(&begin_context);native.state[8]=0x037f00001f80ull;
#else
#error Native context register layout is required for this architecture
#endif
 uint32_t image[18]{};image[0]=(entry&1)?0x3f:0x1f;image[14]=stack-0x40;image[16]=entry+4;image[17]=stack;std::memcpy(guest,image,sizeof image);math_save(guest);
}
[[noreturn]]void restore_context(void *guest){auto &native=context(guest,false);if(native.owner!=std::thread::id{}&&native.owner!=std::this_thread::get_id())throw std::runtime_error("Native context belongs to another OS thread");math_restore(guest);mask_update(0xc0,native.irq);starting=&native;sm64ds_context_load_registers(native.state.data());}
void capture_fault(void *destination,int reason){struct Snapshot{uint64_t caller,frame,reason,status;};Snapshot value{reinterpret_cast<uintptr_t>(__builtin_return_address(0)),reinterpret_cast<uintptr_t>(__builtin_frame_address(0)),uint64_t(reason),processor_status()};std::memcpy(destination,&value,sizeof value);}
}
extern "C" sm64ds::platform::NativeContext *sm64ds_context_save_storage(void *guest){
 using namespace sm64ds::platform;auto &native=context(guest,true);native.owner=std::this_thread::get_id();native.saved=true;native.irq=processor_status()&0xc0;
 uint32_t status=processor_status(),one=1;std::memcpy(guest,&status,4);std::memcpy(static_cast<uint8_t*>(guest)+4,&one,4);
 const uint64_t values[]={ntr::io_read(0x04000290,8),ntr::io_read(0x04000298,8),ntr::io_read(0x040002b8,8)};std::memcpy(static_cast<uint8_t*>(guest)+0x48,values,24);
 const uint16_t modes[]={uint16_t(ntr::io_read(0x04000280,2)&3),uint16_t(ntr::io_read(0x040002b0,2)&1)};std::memcpy(static_cast<uint8_t*>(guest)+0x60,modes,4);return &native;
}
static_assert(offsetof(sm64ds::platform::NativeContext,state)==0);
