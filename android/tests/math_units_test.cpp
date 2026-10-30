#include "ntr/mmio.h"
#include "runtime/packed_memory.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace cstd { int fdiv(int,int); int sqrt(unsigned long long); int div(int,int); int mod(int,int); long long ldiv(int,int); }
// The production frontend adapter forwards this SDK boundary to native fdiv.
extern "C" int pk__ZN4cstd4fdivEii(int a,int b) { return cstd::fdiv(a,b); }
extern "C" void abi__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(
    unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
namespace {
constexpr uint32_t Control=0x04000280, Numer=0x04000290, Denom=0x04000298;
constexpr uint32_t Result=0x040002a0, Remainder=0x040002a8;
constexpr uint32_t SqrtControl=0x040002b0, SqrtResult=0x040002b4, SqrtParam=0x040002b8;
void check(bool value,const char *why) { if (!value) throw std::runtime_error(why); }
}
int main() {
    try {
        check(ntr::io_init(),"Cannot initialize arithmetic registers");
        sm64ds::GameHeap heap;
        sm64ds::packed::Space space(heap);
        space.map(ntr::IO_BASE,ntr::host_ptr(ntr::IO_BASE),ntr::IO_SIZE);
        sm64ds::packed::Space::Scope scope(space);
        const auto matrix=space.allocate(64);
        // Actual recovered projection, entered after NormalizeVec3's 64/64
        // mode. Negative near-far must be interpreted in 64/32 mode, otherwise
        // its Z lanes collapse to zero even while the projected XY looks valid.
        ntr::io_write(Control,2,2);
        abi__ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(
            4096,4096,4096,4096,8192,4096,0,matrix);
        const auto *m=static_cast<const int32_t *>(space.data(matrix,64));
        check(m[0]==4096 && m[5]==4096 && m[10]==-12288 && m[11]==-4096 && m[14]==-16384,
            "Recovered perspective matrix inherited the wrong division mode");
        space.release(matrix);
        // SDK routines also submit 64-bit operands as two word stores. Either
        // word must restart the unit, including a high word written last.
        ntr::io_write(Control,2,2);
        ntr::io_write(Numer,0,8); ntr::io_write(Denom,3,8);
        ntr::io_write(Numer+4,1,4);
        check(ntr::io_read(Result,8)==1431655765,"Divider ignored numerator high-word write");
        check(ntr::io_read(Remainder,8)==1,"Split numerator remainder is stale");
        ntr::io_write(Denom+4,1,4);
        check(ntr::io_read(Result,8)==0,"Divider ignored denominator high-word write");
        ntr::io_write(Control,3,2); // same 64/32 operands as mode 1
        check(ntr::io_read(Result,8)==1431655765,"Division mode 3 used a 64-bit denominator");

        ntr::io_write(SqrtControl,1,2);
        ntr::io_write(SqrtParam,0,8);
        ntr::io_write(SqrtParam+4,1,4);
        check(ntr::io_read(SqrtResult,4)==65536,"Square root ignored operand high-word write");

        // Native synchronous helpers share state with recovered asynchronous
        // users. In particular G3i::PerspectiveW_ relies on fdiv selecting
        // 64/32 mode before its next numerator/denominator stores.
        ntr::io_write(Control,0,2);
        check(cstd::fdiv(1,6)==683 && cstd::fdiv(-1,6)==-683,"Q12 rounding changed");
        check((ntr::io_read(Control,2)&3)==1,"Native fdiv did not select the hardware mode");
        check(ntr::io_read(Numer,8)==0xffffffff00000000ull,"Native fdiv did not submit its numerator");
        check(ntr::io_read(Denom,8)==6,"Native fdiv did not submit its denominator");
        ntr::io_write(Numer, uint64_t(4096)<<32,8);
        ntr::io_write(Denom,4096,8);
        check(ntr::io_read(Result,8)==0x100000000ull,"Async projection divide inherited the wrong mode");

        // Zero division is a defined DS unit result, not a native exception.
        check(cstd::fdiv(4096,0)==0,"Q12 zero-divisor result changed");
        check(ntr::io_read(Result,8)==UINT64_MAX,"64-bit zero-divisor quotient changed");
        check(ntr::io_read(Remainder,8)==uint64_t(4096)<<32,"Zero-divisor remainder changed");
        check(ntr::io_read(Control,2)&0x4000,"Zero-divisor status was not set");
        check(cstd::fdiv(-4096,0)==0 && ntr::io_read(Result,8)==1,"Negative zero-divisor result changed");
        check(cstd::fdiv(0,0)==0,"Zero over zero must not throw");
        check(cstd::fdiv(1,6)==683 && !(ntr::io_read(Control,2)&0x4000),"Valid divide did not clear zero status");
        check(cstd::div(-7,3)==-2 && cstd::mod(-7,3)==-1,"SDK integer division rounding changed");
        check(!(ntr::io_read(Control,2)&3),"SDK integer divide did not select 32/32 mode");
        check(cstd::div(7,0)==-1 && cstd::mod(-7,0)==-7,"SDK integer zero-divisor result changed");
        check(cstd::ldiv(1,2)==0x80000000ll && (ntr::io_read(Control,2)&3)==1,"SDK long divide omitted hardware state");
        check(cstd::ldiv(-1,0)==1,"SDK long zero-divisor result changed");

        ntr::io_write(Control,2,2);
        ntr::io_write(Numer,0x8000000000000000ull,8); ntr::io_write(Denom,UINT64_MAX,8);
        check(ntr::io_read(Result,8)==0x8000000000000000ull && !ntr::io_read(Remainder,8),"64-bit overflow result changed");
        ntr::io_write(Control,0,2);
        ntr::io_write(Numer,0x80000000u,8);
        check(ntr::io_read(Result,8)==0x80000000u,"32-bit overflow result changed");
        ntr::io_write(Numer,5,8); ntr::io_write(Denom,0,8);
        check(ntr::io_read(Result,8)==0xffffffffu,"32-bit zero-divisor upper word changed");

        check(cstd::sqrt(2304)==48,"Native square root rounding changed");
        check((ntr::io_read(SqrtControl,2)&1)==1 && ntr::io_read(SqrtParam,8)==9216 &&
            ntr::io_read(SqrtResult,4)==96,"Native square root omitted hardware state");
        std::puts("Recovered projection, arithmetic word stores, native/async state, zero division and overflow passed.");
    } catch (const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
