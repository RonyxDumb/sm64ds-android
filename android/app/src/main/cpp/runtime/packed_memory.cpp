#include "packed_memory.h"
#include <link.h>
#include <pthread.h>
#include <stdexcept>
#if defined(__SANITIZE_ADDRESS__)
#define SM64DS_PACKED_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SM64DS_PACKED_ASAN 1
#endif
#endif
#ifdef SM64DS_PACKED_ASAN
#include <sanitizer/asan_interface.h>
#endif

namespace sm64ds::packed {
namespace { thread_local Space *threadSpace = nullptr; }
Space &Space::current() {
    if (!threadSpace) throw std::runtime_error("No packed frontend address context.");
    return *threadSpace;
}
void *resolve(uint32_t a, size_t n) { return Space::current().data(a,n); }
uint32_t encode(const void *p) { return Space::current().address(p); }
uintptr_t resolve_code(uint32_t a) { return Space::current().code(a); }
uint32_t encode_code(uintptr_t p) { return Space::current().code_address(p); }
Space::Scope::Scope(Space &next) : previous_(threadSpace) { threadSpace = &next; }
Space::Scope::~Scope() { threadSpace = previous_; }
Space::Space(GameHeap &heap) : heap_(heap) {
    map(0x10000000, heap.at(0,heap.capacity()), heap.capacity());
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr))
        throw std::runtime_error("Cannot resolve native game thread stack.");
    void *stack = nullptr;
    size_t size = 0;
    int result = pthread_attr_getstack(&attr, &stack, &size);
    pthread_attr_destroy(&attr);
    if (result || size > 0x08000000) throw std::runtime_error("Invalid native stack extent.");
    map(0x70000000, stack, size);
}
Space::~Space() { for (const auto &a : allocations_) heap_.deallocate(a.second); }
size_t Space::largest_free_span() const { return heap_.largest_free_span(); }
void Space::map(uint32_t a, void *p, size_t n) {
    if (!a || !p || !n || uint64_t(a)+n > UINT32_MAX)
        throw std::runtime_error("Invalid packed data span.");
    for (const auto &r : regions_)
        if (uint64_t(a) < uint64_t(r.address)+r.size && uint64_t(r.address) < uint64_t(a)+n)
            throw std::runtime_error("Overlapping packed data spans.");
    regions_.push_back({a,n,static_cast<uint8_t *>(p)});
}
void *Space::data(uint32_t a, size_t n) const {
    if (!a) throw std::runtime_error("Null packed data handle.");
    for (const auto &r : regions_)
        if (a >= r.address && uint64_t(a)-r.address <= r.size && n <= r.size-(uint64_t(a)-r.address))
            return r.memory + (a-r.address);
    throw std::runtime_error("Packed data handle outside registered span: " + std::to_string(a));
}
bool Space::map_native_segment(const void *pointer) {
#ifdef SM64DS_PACKED_ASAN
    void *begin = nullptr, *end = nullptr;
    if (__asan_addr_is_in_fake_stack(__asan_get_current_fake_stack(), const_cast<void *>(pointer), &begin, &end)) {
        size_t n = static_cast<uint8_t *>(end)-static_cast<uint8_t *>(begin);
        if (!n || n > 0x60000000u-external_) return false;
        map(external_,begin,n);
        external_ += static_cast<uint32_t>((n+4095)&~size_t(4095));
        return true;
    }
#endif
    struct Find { uintptr_t pointer, start=0; size_t size=0; } find{reinterpret_cast<uintptr_t>(pointer)};
    dl_iterate_phdr([](dl_phdr_info *info, size_t, void *opaque) {
        auto &f = *static_cast<Find *>(opaque);
        for (unsigned i=0; i<info->dlpi_phnum; ++i) {
            const auto &p=info->dlpi_phdr[i];
            uintptr_t start=info->dlpi_addr+p.p_vaddr;
            if (p.p_type==PT_LOAD && f.pointer>=start && f.pointer-start<p.p_memsz) {
                f.start=start; f.size=p.p_memsz; return 1;
            }
        }
        return 0;
    }, &find);
    if (!find.size || find.size>0x60000000u-external_) return false;
    map(external_,reinterpret_cast<void *>(find.start),find.size);
    external_ += static_cast<uint32_t>((find.size+4095)&~size_t(4095));
    return true;
}
uint32_t Space::address(const void *p) {
    if (!p) return 0;
    for (int attempt=0; attempt<2; ++attempt) {
        uintptr_t a=reinterpret_cast<uintptr_t>(p);
        for (const auto &r : regions_) {
            uintptr_t b=reinterpret_cast<uintptr_t>(r.memory);
            if (a>=b && a-b<r.size) return r.address+static_cast<uint32_t>(a-b);
        }
        if (attempt || !map_native_segment(p)) break;
    }
    throw std::runtime_error("Native pointer has no packed data mapping.");
}
uint32_t Space::allocate(uint32_t n) {
    void *p=heap_.allocate(n,4);
    if (!p) throw std::runtime_error("Native frontend heap is full.");
    std::memset(p,0,heap_.size_of(p));
    uint32_t a=0x10000000+heap_.offset_of(p);
    try { allocations_.emplace(a,p); } catch (...) { heap_.deallocate(p); throw; }
    return a;
}
void Space::release(uint32_t a) {
    if (!a) return;
    auto at=allocations_.find(a);
    if (at==allocations_.end()) throw std::runtime_error("Unowned frontend allocation release.");
    heap_.deallocate(at->second); allocations_.erase(at);
}
void Space::bind_code(uint32_t a, uintptr_t p) {
    if (!a || !p || (code_.count(a) && code_.at(a)!=p))
        throw std::runtime_error("Invalid/conflicting native code binding.");
    code_[a]=p;
}
uintptr_t Space::code(uint32_t a) const {
    auto at=code_.find(a);
    if (at==code_.end()) throw std::runtime_error("Unlinked native code handle: " + std::to_string(a));
    return at->second;
}
uint32_t Space::code_address(uintptr_t p) {
    if (!p) return 0;
    for (const auto &entry : code_) if (entry.second==p) return entry.first;
    uint32_t a=nextCode_++; bind_code(a,p); return a;
}
}
