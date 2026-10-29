#pragma once
#include "runtime/native_abi.h"
#include "runtime/packed_memory.h"
#include "ntr/mmio.h"
#include "ntr/rt.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>
namespace sm64ds::platform {
template<class T>T load(uint32_t address){T v;std::memcpy(&v,packed::Space::current().data(address,sizeof v),sizeof v);return v;}
template<class T>void store(uint32_t address,T v){std::memcpy(packed::Space::current().data(address,sizeof v),&v,sizeof v);}
template<class Signature>Signature code(uint32_t address){return native_abi::pointer<Signature>(address);}
inline void call(uint32_t address){code<void(*)()>(address)();}
struct Services{
 std::atomic<uint32_t> fiq{0},interrupts{0},control{0},dtcm{0x023c000a},itcm{0x20};
 std::array<std::atomic<uint32_t>,8> regions{};std::array<std::atomic<uint32_t>,5> attributes{};std::mutex lock;
 static Services &current(){static Services s;return s;}
};
inline uint32_t mask_update(uint32_t mask,uint32_t value){
 uint32_t old=ntr::rt_irq_masked()?0x80:0;
 if(mask&0x40)old|=Services::current().fiq.exchange(value&0x40);else old|=Services::current().fiq.load();
 if(mask&0x80) ntr::rt_irq_restore(value&0x80);
 return old&mask;
}
inline uint32_t processor_status(){return 0x1f|(ntr::rt_irq_masked()?0x80:0)|Services::current().fiq.load();}
inline void cache_fence(){std::atomic_thread_fence(std::memory_order_seq_cst);}
inline void invalidate_instructions(uint32_t address,uint32_t size){auto *first=static_cast<char*>(packed::Space::current().data(address,size));__builtin___clear_cache(first,first+size);cache_fence();}
inline void signal_interrupt(uint32_t bits){Services::current().interrupts.fetch_or(bits);}
inline void wait_interrupt(bool discard,uint32_t mask){
 auto &pending=Services::current().interrupts;if(discard)pending.fetch_and(~mask);
 for(;;){uint32_t bits=pending.load()&mask;if(bits){pending.fetch_and(~bits);return;}ntr::rt_vblank_wait();signal_interrupt(ntr::IRQ_VBLANK);}
}
inline void delay(uint32_t count){auto ns=std::chrono::nanoseconds((uint64_t(count)*3*1000000000ull+67027963)/67027964);if(ns.count())std::this_thread::sleep_for(ns);}
inline void write_word(void *destination,uint32_t value){auto address=reinterpret_cast<uintptr_t>(destination);if(address>=ntr::IO_BASE&&address<ntr::IO_BASE+ntr::IO_SIZE)ntr::io_write(uint32_t(address),value,4);else std::memcpy(destination,&value,4);}
inline void copy_words(const void *source,void *destination,uint32_t bytes){auto *src=static_cast<const uint8_t*>(source);auto *dst=static_cast<uint8_t*>(destination);for(uint64_t i=0;i<bytes;i+=4){uint32_t value;std::memcpy(&value,src+i,4);write_word(dst+i,value);}}
inline void fill_words(uint32_t value,void *destination,uint32_t bytes){auto *dst=static_cast<uint8_t*>(destination);for(uint64_t i=0;i<bytes;i+=4)write_word(dst+i,value);}
inline void copy_fixed(const void *source,void *destination,unsigned bytes,unsigned block){auto *src=static_cast<const uint8_t*>(source);for(unsigned offset=0;offset<bytes;offset+=block){std::array<uint32_t,4> words{};std::memcpy(words.data(),src+offset,block);for(unsigned i=0;i<block;i+=4)write_word(static_cast<uint8_t*>(destination)+i,words[i/4]);}}
struct BitUnpackInfo{uint16_t bytes;uint8_t sourceBits,destBits;uint32_t offset;};static_assert(sizeof(BitUnpackInfo)==8);
inline void unpack_bits(const void *source,void *destination,const BitUnpackInfo &info){
 if((info.sourceBits!=1&&info.sourceBits!=2&&info.sourceBits!=4&&info.sourceBits!=8)||(info.destBits!=1&&info.destBits!=2&&info.destBits!=4&&info.destBits!=8&&info.destBits!=16&&info.destBits!=32))throw std::runtime_error("Invalid bit-unpack widths");
 auto *src=static_cast<const uint8_t*>(source);auto *dst=static_cast<uint8_t*>(destination);uint32_t word=0;unsigned used=0;auto mask=(1u<<info.sourceBits)-1;
 for(unsigned i=0;i<info.bytes;++i)for(unsigned shift=0;shift<8;shift+=info.sourceBits){uint32_t value=(src[i]>>shift)&mask;if(value||(info.offset&0x80000000u))value+=info.offset&0x7fffffffu;if(info.destBits<32)value&=(1u<<info.destBits)-1;word|=value<<used;used+=info.destBits;if(used==32){write_word(dst,word);dst+=4;word=0;used=0;}}
 if(used)write_word(dst,word);
}
inline void cpu_set(const void *source,void *destination,uint32_t control,bool fast){
 unsigned width=fast||(control&(1u<<26))?4:2;uint32_t count=control&0x1fffffu;if(fast)count=(count+7u)&~7u;
 auto *src=static_cast<const uint8_t*>(source);auto *dst=static_cast<uint8_t*>(destination);bool fixed=control&(1u<<24);uint32_t value=0;if(fixed&&count)std::memcpy(&value,src,width);
 for(uint32_t i=0;i<count;++i){if(!fixed)std::memcpy(&value,src+size_t(i)*width,width);if(width==4)write_word(dst+size_t(i)*width,value);else std::memcpy(dst+size_t(i)*width,&value,2);}
}
inline uint16_t crc16(uint16_t crc,const void *data,uint32_t size){auto *p=static_cast<const uint8_t*>(data);for(uint32_t i=0;i<size;++i){crc^=p[i];for(unsigned bit=0;bit<8;++bit)crc=uint16_t((crc>>1)^((crc&1)?0xa001:0));}return crc;}
inline uint32_t integer_sqrt(uint32_t value){uint32_t result=0,bit=1u<<30;while(bit>value)bit>>=2;while(bit){if(value>=result+bit){value-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}return result;}
inline int32_t bios_div(int32_t a,int32_t b){if(!b)return a<0?1:-1;return int32_t(int64_t(a)/int64_t(b));}
inline int32_t bios_mod(int32_t a,int32_t b){return b?int32_t(int64_t(a)%int64_t(b)):a;}
}
