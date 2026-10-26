#include "frontend_core.h"
#include "packed_ptr.h"
#include "ntr/lz77.h"
#include <atomic>
#include <climits>
#include <stdexcept>

using sm64ds::packed::Ptr;
using sm64ds::packed::Space;
namespace {
void count(int bytes, int alignment) {
    if (bytes<0 || bytes%alignment) throw std::runtime_error("Invalid native bulk operation extent.");
}
void fill32(unsigned value, Ptr<int> dst, int bytes) {
    count(bytes,4); if (!bytes) return;
    auto out=static_cast<uint32_t *>(Space::current().data(dst.address(),bytes));
    for (int i=0; i<bytes/4; ++i) out[i]=value;
}
}
extern "C" {
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
    auto out=static_cast<uint16_t *>(Space::current().data(p.address(),n));
    for (int i=0; i<n/2; ++i) out[i]=v;
}
void pk_MultiCopyHalf(int src, Ptr<char> dst, int n) {
    count(n,2);
    if (n) std::memmove(dst.get(n),Space::current().data(uint32_t(src),n),n);
}
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
