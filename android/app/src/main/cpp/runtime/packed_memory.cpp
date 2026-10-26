#include "packed_memory.h"
#include <link.h>
#include <algorithm>
#include <pthread.h>
#include <cstdio>
#include <sstream>
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
uint32_t encode(uintptr_t p) { return Space::current().address(reinterpret_cast<const void *>(p)); }
uintptr_t resolve_code(uint32_t a) { return Space::current().code(a); }
uint32_t encode_code(uintptr_t p) { return Space::current().code_address(p); }
Space::Scope::Scope(Space &next) : previous_(threadSpace) {
    next.map_thread_stack();
    threadSpace = &next;
}
Space::Scope::~Scope() { threadSpace = previous_; }
Space::Space(GameHeap &heap) : heap_(heap) {
    map(0x10000000, heap.at(0,heap.capacity()), heap.capacity());
    map_thread_stack();
}
void Space::map_thread_stack() {
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr))
        throw std::runtime_error("Cannot resolve native game thread stack.");
    void *stack = nullptr;
    size_t size = 0;
    int result = pthread_attr_getstack(&attr, &stack, &size);
    pthread_attr_destroy(&attr);
    if (result || size > 0x08000000) throw std::runtime_error("Invalid native stack extent.");
    for (const auto &region : regions_)
        if (region.memory==stack && region.size==size) return;
    if (uint64_t(nextStack_)+size>0x80000000u)
        throw std::runtime_error("Native frontend stack handle space exhausted.");
    map(nextStack_,stack,size);
    nextStack_+=static_cast<uint32_t>((size+4095)&~size_t(4095));
}
Space::~Space() { for (const auto &a : allocations_) heap_.deallocate(a.second); }
size_t Space::largest_free_span() const { return heap_.largest_free_span(); }
void Space::map(uint32_t a, void *p, size_t n) {
    if (!a || !p || !n || uint64_t(a)+n > UINT32_MAX)
        throw std::runtime_error("Invalid packed data span.");
    // The native frontend performs many thousands of checked DS-pointer
    // reads per frame. Keep its immutable address regions sorted, so every
    // pointer dereference uses O(log N) lookup instead of scanning every
    // register/VRAM/overlay/native stack/heap mapping.
    const auto it = std::lower_bound(regions_.begin(), regions_.end(), a,
        [](const Region &r, uint32_t addr) { return r.address < addr; });
    if ((it != regions_.end() && uint64_t(a) + n > it->address) ||
        (it != regions_.begin() && uint64_t((it-1)->address) + (it-1)->size > a))
        throw std::runtime_error("Overlapping packed data spans.");
    regions_.insert(it, {a,n,static_cast<uint8_t *>(p)});
    // New host ranges can alias a larger allocator VMA registered earlier.
    // Clear the hot cache so the lowest DS-handle mapping keeps precedence.
    recentNativeValid_ = false;
}
void *Space::data(uint32_t a, size_t n) const {
    if (!a) throw std::runtime_error("Null packed data handle.");
    auto it = std::upper_bound(regions_.begin(), regions_.end(), a,
        [](uint32_t addr, const Region &r) { return addr < r.address; });
    if (it != regions_.begin()) {
        --it;
        const uint64_t offset = uint64_t(a) - it->address;
        if (offset <= it->size && n <= it->size - offset)
            return it->memory + offset;
    }
    throw std::runtime_error("Packed data handle outside registered span: " + std::to_string(a));
}
bool Space::map_native_segment(const void *pointer) {
    // A native pointer can live in a shared object's PT_LOAD, an ASan fake
    // stack frame, or a C++ heap/anonymous allocator mapping. Never narrow the
    // host address: register its backing span and return an independent handle.
    auto register_span = [this](const void *begin, size_t bytes) {
        constexpr uint64_t End = 0x60000000u;
        const uint64_t aligned = (uint64_t(bytes) + 4095u) & ~uint64_t(4095u);
        if (!bytes || !begin || external_ >= End || aligned > End - external_)
            return false;
        map(external_, const_cast<void *>(begin), bytes);
        external_ += static_cast<uint32_t>(aligned);
        return true;
    };
#ifdef SM64DS_PACKED_ASAN
    void *begin = nullptr, *end = nullptr;
    if (__asan_addr_is_in_fake_stack(__asan_get_current_fake_stack(), const_cast<void *>(pointer),
                                    &begin, &end)) {
        return register_span(begin, static_cast<uint8_t *>(end) - static_cast<uint8_t *>(begin));
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
    if (find.size)
        return register_span(reinterpret_cast<void *>(find.start), find.size);

    // dl_iterate_phdr does not describe malloc/new buffers. Resolve only a
    // currently mapped, readable/writable anonymous allocator VMA. Do not
    // auto-register file mappings, devices, other threads' stacks or guards.
    // Android bionic commonly labels allocator VMAs [anon:libc_malloc] or
    // [anon:scudo:primary]; glibc also uses [heap] and unnamed VMAs.
    FILE *maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return false;
    char line[2048];
    uintptr_t base = 0;
    size_t length = 0;
    const uint64_t at = reinterpret_cast<uintptr_t>(pointer);
    while (std::fgets(line, sizeof line, maps)) {
        unsigned long long low = 0, high = 0;
        char permissions[5] = {};
        int afterInode = 0;
        if (std::sscanf(line, "%llx-%llx %4s %*s %*s %*s %n",
                        &low, &high, permissions, &afterInode) != 3)
            continue;
        if (permissions[0] != 'r' || permissions[1] != 'w' ||
            at < low || at >= high || high <= low)
            continue;
        const char *label = line + afterInode;
        while (*label == ' ' || *label == '\t') ++label;
        const bool anonymous = *label == '\n' || *label == 0 ||
            !std::strncmp(label, "[heap]", 6) || !std::strncmp(label, "[anon:", 6);
        if (!anonymous || high > UINTPTR_MAX || high - low > SIZE_MAX)
            break;
        base = static_cast<uintptr_t>(low);
        length = static_cast<size_t>(high - low);
        break;
    }
    std::fclose(maps);
    return length && register_span(reinterpret_cast<void *>(base), length);
}
uint32_t Space::address(const void *p) {
    if (!p) return 0;
    const uintptr_t a=reinterpret_cast<uintptr_t>(p);
    if (recentNativeValid_) {
        const uintptr_t b = reinterpret_cast<uintptr_t>(recentNative_.memory);
        if (a >= b && a-b < recentNative_.size)
            return recentNative_.address + static_cast<uint32_t>(a-b);
    }
    for (int attempt=0; attempt<2; ++attempt) {
        for (const auto &r : regions_) {
            const uintptr_t b=reinterpret_cast<uintptr_t>(r.memory);
            if (a>=b && a-b<r.size) {
                recentNative_ = r;
                recentNativeValid_ = true;
                return r.address+static_cast<uint32_t>(a-b);
            }
        }
        if (attempt || !map_native_segment(p)) break;
    }
    std::ostringstream message;
    message << "Native pointer has no packed data mapping: 0x" << std::hex
            << reinterpret_cast<uintptr_t>(p)
            << " (not in a registered span or a readable/writable native allocator mapping)";
    throw std::runtime_error(message.str());
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
uint32_t Space::allocation_size(uint32_t a) const {
    auto at=allocations_.find(a);
    if (at==allocations_.end()) throw std::runtime_error("Unowned frontend allocation size.");
    return heap_.size_of(at->second);
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
MemberTarget Space::resolve_member(uint32_t object, MemberCall method) const {
    // The ROM stores its C++ member pointer as {ptr, adjust}. The low bit
    // of adjust signals virtual dispatch; the remaining signed bits are the
    // byte adjustment to this (arithmetic right-shift by one).
    if (!object || (!method.entry && !method.adjustment))
        throw std::runtime_error("Null native actor/member call.");
    const int64_t adjusted = int64_t(object) + (method.adjustment >> 1);
    if (adjusted <= 0 || adjusted > UINT32_MAX)
        throw std::runtime_error("Native actor this-adjustment is outside packed address space.");
    const auto at = static_cast<uint32_t>(adjusted);
    // Even a direct function pointer must reference a registered actor.
    data(at, 4);
    uint32_t entry = method.entry;
    if (method.adjustment & 1) {
        // DS vtables are arrays of four-byte code handles, not host-sized
        // function pointers. The descriptor contains a byte offset.
        if ((entry & 3) || entry > 0x10000u)
            throw std::runtime_error("Invalid native actor virtual slot.");
        uint32_t vtable = 0;
        std::memcpy(&vtable, data(at, sizeof vtable), sizeof vtable);
        if (uint64_t(vtable) + entry > UINT32_MAX)
            throw std::runtime_error("Native actor vtable slot overflows packed address space.");
        std::memcpy(&entry, data(vtable + entry, sizeof entry), sizeof entry);
    }
    // Space::code() checks that this points to actual linked host machine
    // code. ROM instructions and stale 32-bit addresses are never callable.
    return {at, code(entry)};
}
uint32_t Space::code_address(uintptr_t p) {
    if (!p) return 0;
    for (const auto &entry : code_) if (entry.second==p) return entry.first;
    uint32_t a=nextCode_++; bind_code(a,p); return a;
}
}
