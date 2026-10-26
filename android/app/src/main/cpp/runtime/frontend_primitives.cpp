#include "frontend_core.h"
#include "packed_ptr.h"
#include "ntr/lz77.h"
#include "ntr/mmio.h"
#include <atomic>
#include <climits>
#include <stdexcept>

using sm64ds::packed::Ptr;
using sm64ds::packed::Space;
namespace cstd { int fdiv(int,int); }
namespace {
bool port(uint32_t address) { return address>=ntr::IO_BASE && address<ntr::IO_BASE+ntr::IO_SIZE; }
void copy(uint32_t src,uint32_t dst,int bytes,int width) {
    if (bytes<0 || bytes%width) throw std::runtime_error("Invalid native copy extent.");
    if (!bytes) return;
    if (!port(src) && !port(dst)) {
        std::memmove(Space::current().data(dst,bytes),Space::current().data(src,bytes),bytes);
        return;
    }
    for (int i=0;i<bytes;i+=width) {
        uint64_t value=0;
        if (port(src+i)) value=ntr::io_read(src+i,width);
        else std::memcpy(&value,Space::current().data(src+i,width),width);
        if (port(dst+i)) ntr::io_write(dst+i,value,width);
        else std::memcpy(Space::current().data(dst+i,width),&value,width);
    }
}
void count(int bytes, int alignment) {
    if (bytes<0 || bytes%alignment) throw std::runtime_error("Invalid native bulk operation extent.");
}
void fill32(unsigned value, Ptr<int> dst, int bytes) {
    count(bytes,4); if (!bytes) return;
    if (port(dst.address())) {
        for (int i=0;i<bytes;i+=4) ntr::io_write(dst.address()+i,value,4);
        return;
    }
    auto out=static_cast<uint32_t *>(Space::current().data(dst.address(),bytes));
    for (int i=0; i<bytes/4; ++i) out[i]=value;
}
}
extern "C" {
uint8_t pk_frontend_command(int value) { return sm64ds::FrontendCore::current().command(value); }
int pk_func_02051f1c(Ptr<void> handle,unsigned archive,int entry) {
    return sm64ds::FrontendCore::current().start_sound(handle.address(),archive,entry);
}
int pk_func_0204fa2c(Ptr<void> handle,int frames) {
    return sm64ds::FrontendCore::current().control_sound(handle.address(),sm64ds::SoundOp::Stop,frames);
}
void pk_func_0204f8cc(Ptr<void> handle,int level,int frames) {
    sm64ds::FrontendCore::current().control_sound(handle.address(),sm64ds::SoundOp::Fade,level,frames);
}
void pk_func_0204f73c(Ptr<void> handle,int value) {
    sm64ds::FrontendCore::current().control_sound(handle.address(),sm64ds::SoundOp::PlayerParam,0x1a,value);
}
void pk_func_0204f76c(Ptr<void> handle,int tracks,int value) {
    sm64ds::FrontendCore::current().control_sound(handle.address(),sm64ds::SoundOp::TrackParam,tracks,0x19,value);
}
void pk_func_0204f79c(Ptr<void> handle,int tracks,int value) {
    sm64ds::FrontendCore::current().control_sound(handle.address(),sm64ds::SoundOp::TrackParam,tracks,0x1a,value);
}
void pk_func_0204f7fc(Ptr<void> handle,int tracks,int value) {
    sm64ds::FrontendCore::current().control_sound(handle.address(),sm64ds::SoundOp::TrackParam,tracks,0x0c,value);
}
void pk_func_0204f924(Ptr<void> handle,uint8_t priority) {
    sm64ds::FrontendCore::current().sound_priority(handle.address(),priority);
}
void pk_func_0204f934(Ptr<void> handle) { sm64ds::FrontendCore::current().detach_sound(handle.address()); }
int pk_frontend_allocate(Ptr<void> bytes) { return Space::current().allocate(bytes.address()); }
void pk_frontend_deallocate(Ptr<void> address) { Space::current().release(address.address()); }
int pk_frontend_largest() { return static_cast<int>(Space::current().largest_free_span()); }
Ptr<void> pk_frontend_load(uint16_t handle) { return sm64ds::FrontendCore::current().load_file(handle); }
void pk_DecompressLZ16(Ptr<void> src, Ptr<void> dst) {
    auto &s=Space::current();
    int n=static_cast<int>(s.allocation_size(src.address()));
    int capacity=static_cast<int>(s.allocation_size(dst.address()));
    if (ntr::lz77_unpack(static_cast<const uint8_t *>(src.get(n)),n,
                        static_cast<uint8_t *>(dst.get(capacity)),capacity)<0)
        throw std::runtime_error("Invalid frontend compressed stream.");
}
void pk__ZN4CP1514FlushDataCacheEjj(uint32_t address, uint32_t bytes) {
    if (bytes) (void)Space::current().data(address,bytes);
    // CPU memory is coherent on the native target. GPU uploads use explicit
    // backend copies; no cartridge CP15 instruction may execute here.
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
void pk_MultiStore32Bytes(unsigned v, Ptr<int> p, int n) { fill32(v,p,n); }
void pk_MultiStore_Int(int v, Ptr<int> p, int n) { fill32(v,p,n); }
void pk_MultiStore16(unsigned short v, Ptr<char> p, int n) {
    count(n,2); if (!n) return;
    if (port(p.address())) {
        for (int i=0;i<n;i+=2) ntr::io_write(p.address()+i,v,2);
        return;
    }
    auto out=static_cast<uint16_t *>(Space::current().data(p.address(),n));
    for (int i=0; i<n/2; ++i) out[i]=v;
}
void pk_MultiCopyHalf(int src, Ptr<char> dst, int n) {
    copy(uint32_t(src),dst.address(),n,2);
}
void pk_MultiCopy_Int(Ptr<int> src,Ptr<int> dst,int n) { copy(src.address(),dst.address(),n,4); }
void pk_MultiCopy32Bytes(Ptr<int> src,Ptr<int> dst,int n) { copy(src.address(),dst.address(),n,4); }
void pk_Copy48Bytes(Ptr<int> src,Ptr<int> dst) { copy(src.address(),dst.address(),48,4); }
void pk_Copy36Bytes(Ptr<int> src,Ptr<int> dst) { copy(src.address(),dst.address(),36,4); }
void pk_Copy32Bytes(Ptr<int> src,Ptr<int> dst) { copy(src.address(),dst.address(),32,4); }
void pk_Copy48BytesFixed(Ptr<int> src,Ptr<int> dst) {
    for (int i=0;i<4;++i) copy(src.address()+i*12,dst.address(),12,4);
}
void pk_DMAStartTransfer(unsigned channel,Ptr<void> src,Ptr<void> dst,unsigned control) {
    if (channel>3) throw std::runtime_error("Invalid native DMA channel");
    const unsigned width=(control&(1u<<26))?4:2;
    const unsigned units=control&0x1fffff;
    const unsigned sourceMode=(control>>23)&3, targetMode=(control>>21)&3;
    if (sourceMode==3) throw std::runtime_error("Invalid DMA source mode");
    uint32_t source=src.address(),target=dst.address();
    for (unsigned i=0;i<units;++i) {
        copy(source,target,static_cast<int>(width),static_cast<int>(width));
        if (sourceMode!=2) source+=sourceMode==1?-width:width;
        if (targetMode!=2) target+=targetMode==1?-width:width;
    }
    ntr::io_write(0x040000b8+channel*12,control&~(1u<<31),4);
}
void pk_func_0205a358(unsigned channel,Ptr<void> src,unsigned bytes,Ptr<void(int)> callback,int argument) {
    // The ARM version uses a FIFO IRQ to submit chunks. Native FIFO consumption
    // is synchronous; call the same source chunker, then its completion hook.
    extern void pk_func_01ffde98(unsigned,Ptr<void>,unsigned);
    pk_func_01ffde98(channel,src,bytes);
    if (callback) callback(argument);
}
void pk_func_02052514(Ptr<int> src,Ptr<int> dst) {
    for (int r=0;r<3;++r) {
        for (int c=0;c<3;++c) dst[r*4+c]=src[r*3+c];
        dst[r*4+3]=0;
    }
    dst[12]=dst[13]=dst[14]=0; dst[15]=4096;
}
void pk_func_020527e8(Ptr<int> m,int x,int y,int z) {
    fill32(0,m,48); m[0]=x; m[4]=y; m[8]=z;
}
void pk_DMASyncWordTransfer(unsigned channel,Ptr<void> src,Ptr<void> dst,unsigned bytes) {
    if (channel>3 || bytes>INT_MAX) throw std::runtime_error("Invalid native DMA transfer.");
    copy(src.address(),dst.address(),static_cast<int>(bytes),4);
}
void pk_DMASyncHalfTransfer(unsigned channel,Ptr<void> src,Ptr<void> dst,unsigned bytes) {
    if (channel>3 || bytes>INT_MAX) throw std::runtime_error("Invalid native DMA transfer.");
    copy(src.address(),dst.address(),static_cast<int>(bytes),2);
}
void pk_DMASyncFillTransfer(unsigned channel,Ptr<void> dst,unsigned value,unsigned bytes) {
    if (channel>3 || bytes>INT_MAX) throw std::runtime_error("Invalid native DMA fill.");
    fill32(value,dst,static_cast<int>(bytes));
}
void pk_func_02059fd0(int channel,int src,int dst,unsigned bytes,Ptr<void(int)> callback,int argument) {
    pk_DMASyncWordTransfer(static_cast<unsigned>(channel),uint32_t(src),uint32_t(dst),bytes);
    // Native copies complete synchronously. Preserve the original completion
    // callback after all bytes have reached the backend.
    if (callback) callback(argument);
}
void pk_func_02059fa8(int channel) {
    if (channel<0 || channel>3) throw std::runtime_error("Invalid native DMA channel.");
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
void pk_func_0205a064(unsigned channel,unsigned dst,unsigned value,unsigned bytes,Ptr<void(unsigned)> callback,unsigned argument) {
    pk_DMASyncFillTransfer(channel,dst,value,bytes);
    if (callback) callback(argument);
}
void pk_func_0205256c(Ptr<int> m,int s,int c) {
    // Native form of the SDK's Thumb Y-rotation primitive, 0205256c.
    const int v[9]={c,0,-s,0,4096,0,s,0,c};
    std::memcpy(m.get(sizeof v),v,sizeof v);
}
void pk_Matrix3x3_SetRotationZ(Ptr<int> m,int s,int c) {
    const int v[9]={c,s,0,-s,c,0,0,0,4096};
    std::memcpy(m.get(sizeof v),v,sizeof v);
}
int pk__ZN4cstd4fdivEii(int a,int b) { return cstd::fdiv(a,b); }
[[noreturn]] void pk_Crash() { throw std::runtime_error("Original game graphics invariant failed."); }
void pk_CP15_EnableDTCM() { std::atomic_thread_fence(std::memory_order_seq_cst); }
void pk_CP15_FlushAndInvalidateDataCache() { std::atomic_thread_fence(std::memory_order_seq_cst); }
void pk_CP15_FlushDataCache() { std::atomic_thread_fence(std::memory_order_seq_cst); }
void pk__ZN4CP1527FlushAndInvalidateDataCacheEjj(uint32_t address,uint32_t bytes) {
    pk__ZN4CP1514FlushDataCacheEjj(address,bytes);
}
int pk__ZN4cstd6strcmpEPKcS1_(Ptr<const char> a,Ptr<const char> b) { return std::strcmp(a.get(),b.get()); }
void pk_func_0205a588(int a, int v, int n) {
    count(n,1); if (n) std::memset(Space::current().data(uint32_t(a),n),v,n);
}
void pk_Matrix4x3_LoadIdentity(Ptr<void> p) {
    auto m=Ptr<int>(p); fill32(0,m,48); m[0]=m[4]=m[8]=4096;
}
void pk_func_02052ec8(Ptr<void> p) {
    auto m=Ptr<int>(p); fill32(0,m,64); m[0]=m[5]=m[10]=m[15]=4096;
}
int pk__ZN4cstd3divEii(int a,int b) {
    if (!b) throw std::runtime_error("Native integer division by zero.");
    return a==INT_MIN && b==-1 ? INT_MIN : a/b;
}
int pk__ZN4cstd3modEii(int a,int b) {
    if (!b) throw std::runtime_error("Native integer modulus by zero");
    return a==INT_MIN && b==-1?0:a%b;
}
int pk_cstd_div(int a,int b) { return pk__ZN4cstd3divEii(a,b); }
int pk_cstd_mod(int a,int b) { return pk__ZN4cstd3modEii(a,b); }
int pk_cstd_fdiv(int a,int b) { return cstd::fdiv(a,b); }
int pk_cstd_abs(int a) { return a<0?int32_t(0u-uint32_t(a)):a; }
int pk__ZN4cstd3absEi(int a) { return pk_cstd_abs(a); }
int pk___aeabi_idiv(int a,int b) { return pk__ZN4cstd3divEii(a,b); }
long long pk_ll_sdiv(long long a,long long b) {
    if (!b) throw std::runtime_error("Native long division by zero");
    return a==LLONG_MIN && b==-1?LLONG_MIN:a/b;
}
int pk_func_01ffaa34(long long a,long long b) { return static_cast<int>(pk_ll_sdiv(a,b)); }
void pk_Matrix3x3_LoadIdentity(Ptr<int> m) { fill32(0,m,36); m[0]=m[4]=m[8]=4096; }
void pk_func_02052800(Ptr<int> m,int s,int c) {
    const int v[12]={4096,0,0,0,c,s,0,-s,c,0,0,0}; std::memcpy(m.get(sizeof v),v,sizeof v);
}
void pk_func_02052820(Ptr<int> m,int s,int c) {
    const int v[12]={c,0,-s,0,4096,0,s,0,c,0,0,0}; std::memcpy(m.get(sizeof v),v,sizeof v);
}
void pk_func_0205283c(Ptr<int> m,int s,int c) {
    const int v[12]={c,s,0,-s,c,0,0,0,4096,0,0,0}; std::memcpy(m.get(sizeof v),v,sizeof v);
}
void pk_func_020590fc(int arena,int heap,Ptr<void> address) {
    if (arena || heap!=-1) throw std::runtime_error("Unsupported native SDK heap selection");
    Space::current().release(address.address());
}
Ptr<void> pk_func_02059170(int arena,int heap,int bytes) {
    if (arena || heap!=-1 || bytes<0) throw std::runtime_error("Unsupported native SDK allocation");
    return Space::current().allocate(static_cast<unsigned>(bytes));
}
long long pk__ZN4cstd4ldivEii(int a,int b) {
    if (!b) throw std::runtime_error("Native fractional division by zero.");
    int64_t scaled=int64_t(a)*0x100000000LL;
    return scaled==LLONG_MIN && b==-1 ? LLONG_MIN : scaled/b;
}
void pk_SubVec3(Ptr<int> a,Ptr<int> b,Ptr<int> out) {
    for (int i=0; i<3; ++i) out[i]=int32_t(uint32_t(a[i])-uint32_t(b[i]));
}
void pk_func_02059eb0(Ptr<unsigned char> dst) {
    std::memset(dst.get(0x54),0,0x54);
    dst[0]=static_cast<unsigned char>(sm64ds::FrontendCore::current().language());
}
void pk_func_0205f040() { sm64ds::FrontendCore::current().latch_touch(); }
void pk_func_0205efc0(Ptr<void> dst) {
    const auto &s=sm64ds::FrontendCore::current().latched_touch();
    std::memcpy(dst.get(sizeof s),s.data(),sizeof s);
}
}
