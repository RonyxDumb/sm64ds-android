#include "platform/soft_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <cstring>
namespace sm64ds::platform::soft_math{void run__s32_div_f(Work&,unsigned);void run__u32_div_f(Work&,unsigned);}
extern "C"{
double _dadd(double,double);double _dmul(double,double);double func_01ff8e10(double,double);
uint32_t func_01ff9378(uint32_t,uint32_t);uint32_t func_01ffa594(uint32_t,uint32_t);
uint32_t func_01ffa4bc(uint32_t);uint32_t func_01ffa440(uint32_t);uint32_t func_01ffa344(uint32_t);
uint64_t _ll_sdiv(uint64_t,uint64_t);uint64_t _ll_udiv(uint64_t,uint64_t);uint64_t _ull_mod(uint64_t,uint64_t);
uint64_t func_01ff859c(double);uint32_t func_01ffb008(uint32_t,uint32_t);uint32_t func_01ffb030(uint32_t,uint32_t);void func_01ffafd4();
}
template<class A,class B>A bits(B b){static_assert(sizeof(A)==sizeof(B));A a;std::memcpy(&a,&b,sizeof a);return a;}
int main(){
 using namespace sm64ds;GameHeap heap;packed::Space space(heap);packed::Space::Scope scope(space);uint32_t status[7]{};space.map(0x020aa3f4,status,sizeof status);std::mt19937_64 rng(76451);
 for(int i=0;i<20000;++i){
  uint64_t a=rng(),b=rng();double x=bits<double>(a),y=bits<double>(b);
  if(std::isfinite(x)&&std::isfinite(y)){double got[]={_dadd(x,y),_dmul(x,y),func_01ff8e10(x,y)},expected[]={x+y,x*y,x-y};for(int j=0;j<3;++j)assert(bits<uint64_t>(got[j])==bits<uint64_t>(expected[j]));}
  uint32_t c=rng(),d=rng();float f=bits<float>(c),g=bits<float>(d);
  if(std::isfinite(f)&&std::isfinite(g)){assert(func_01ff9378(c,d)==bits<uint32_t>(f+g));assert(func_01ffa594(c,d)==bits<uint32_t>(f-g));}
  assert(func_01ffa4bc(c)==bits<uint32_t>(float(int32_t(c))));assert(func_01ffa440(c)==bits<uint32_t>(float(c)));
  if(std::isfinite(f)&&f>=-2147483648.0&&f<2147483648.0)assert(func_01ffa344(c)==uint32_t(int32_t(f)));
  if(b){assert(_ll_udiv(a,b)==a/b);assert(_ull_mod(a,b)==a%b);int64_t sa=int64_t(a),sb=int64_t(b);if(sa!=INT64_MIN||sb!=-1)assert(_ll_sdiv(a,b)==uint64_t(sa/sb));}
  if(d){platform::soft_math::Work w;w.r[0]=c;w.r[1]=d;platform::soft_math::run__u32_div_f(w,0);assert(w.r[0]==c/d&&w.r[1]==c%d);w.r[0]=c;w.r[1]=d;platform::soft_math::run__s32_div_f(w,0);int64_t sc=int32_t(c),sd=int32_t(d);assert(w.r[0]==uint32_t(sc/sd)&&w.r[1]==uint32_t(sc%sd));}
 }
 assert(func_01ff859c(123456789123.75)==123456789123ull);
 status[0]=0x00123456;auto old=func_01ffb008(0xff00,0x1200);assert(old==0x00123456&&status[0]==((old&~0xff00u)^0x1200));assert(func_01ffb030(0x001f001f,0x00030002)&0x40000000);assert((status[0]&0x1f1f)==0x302);
 for(int i=1;i<7;++i)status[i]=99;func_01ffafd4();assert(status[0]==0&&status[1]==99);for(int i=2;i<7;++i)assert(status[i]==0);
 std::puts("Static MSL arithmetic: 20,000 randomized pairs, conversions, remainders and status passed.");
}
