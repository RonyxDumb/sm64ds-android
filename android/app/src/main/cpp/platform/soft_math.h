#pragma once
#include "runtime/packed_memory.h"
#include "runtime/native_abi.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
namespace sm64ds::platform::soft_math {
// Local scratch values for statically lowered MSL arithmetic. Direct C++
// branches implement the source; no cartridge instruction is fetched/executed.
struct Work {
 std::array<uint32_t,16> r{};std::array<uint32_t,1024> scratch{};
 bool n=false,z=false,c=false,v=false;
 Work(){r[13]=0xff001000u;}
 uint32_t read(uint32_t address)const{
  if(address>=0xff000000u&&address<0xff001000u){if(address&3)throw std::runtime_error("Unaligned arithmetic scratch read");return scratch.at((address-0xff000000u)/4);}
  uint32_t value;std::memcpy(&value,packed::Space::current().data(address,4),4);return value;
 }
 void write(uint32_t address,uint32_t value){
  if(address>=0xff000000u&&address<0xff001000u){if(address&3)throw std::runtime_error("Unaligned arithmetic scratch write");scratch.at((address-0xff000000u)/4)=value;return;}
  std::memcpy(packed::Space::current().data(address,4),&value,4);
 }
 void nz(uint32_t value){n=(value>>31)!=0;z=value==0;}
 uint32_t sum(uint32_t a,uint32_t b,bool carry,bool flags){
  const uint64_t wide=uint64_t(a)+b+uint32_t(carry);const uint32_t result=uint32_t(wide);
  if(flags){nz(result);c=wide>>32;v=((~(a^b)&(a^result))>>31)!=0;}return result;
 }
 uint32_t rrx(uint32_t value,bool flags){const uint32_t result=(uint32_t(c)<<31)|(value>>1);if(flags)c=value&1;return result;}
 uint32_t shift(uint32_t value,unsigned amount,unsigned kind,bool flags,bool immediate){
  amount=immediate?amount:(amount&255u);if(immediate&&amount==0&&kind!=0)amount=32;if(!amount)return value;
  bool carry=false;uint32_t result=0;
  if(kind==0){if(amount<=32)carry=(value>>(32-amount))&1;if(amount<32)result=value<<amount;}
  if(kind==1){if(amount<=32)carry=(value>>(amount-1))&1;if(amount<32)result=value>>amount;}
  if(kind==2){carry=amount>=32?(value>>31):((value>>(amount-1))&1);result=amount>=32?(uint32_t(0)-(value>>31)):uint32_t(int32_t(value)>>amount);}
  if(flags)c=carry;return result;
 }
 uint64_t pair()const{return r[0]|(uint64_t(r[1])<<32);}
 void pair(uint64_t value,unsigned at=0){r[at]=uint32_t(value);r[at+1]=uint32_t(value>>32);}
};
inline void exception(Work &w,uint32_t address){native_abi::pointer<void(*)(Work &)>(address)(w);}
}
