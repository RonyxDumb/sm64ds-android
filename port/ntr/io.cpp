// The host I/O bus: a real memory window at the DS I/O addresses, plus side-effect
// dispatch for the registers whose writes do something.
//
// Register semantics here are from GBATEK. No SDK source is reproduced.

#include "ntr/mmio.h"

#include "ntr/gx.h"

#include <cstring>
#include <cerrno>
#include <string>
#include <array>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ntr {
namespace {

uint8_t *g_io = nullptr;
#if !defined(_WIN32)
int g_vram_fd=-1;
struct VramAlias { uintptr_t base; size_t size; };
std::array<VramAlias,9> g_vram_aliases{};
size_t g_vram_alias_count=0;

bool init_vram_banks() {
    const int fd=static_cast<int>(syscall(SYS_memfd_create,"sm64ds-vram",1u));
    if (fd<0) return false;
    if (ftruncate(fd,0xa4000)!=0 || mmap(reinterpret_cast<void *>(0x06800000u),0xa4000,
        PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0)==MAP_FAILED) { close(fd); return false; }
    g_vram_fd=fd; return true;
}
void update_vram_banks() {
    // LCDC, BG and OBJ windows alias the same physical bank. SDK clears and
    // uploads through one window before selecting another; no copies are due.
    for (size_t i=0;i<g_vram_alias_count;++i) {
        const auto &old=g_vram_aliases[i];
        if (mmap(reinterpret_cast<void *>(old.base),old.size,PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED)
            throw std::runtime_error("Cannot remove native VRAM alias");
    }
    g_vram_alias_count=0;
    constexpr size_t offsets[]={0,0x20000,0x40000,0x60000,0x80000,0x90000,0x94000,0x98000,0xa0000};
    constexpr size_t sizes[]={0x20000,0x20000,0x20000,0x20000,0x10000,0x4000,0x4000,0x8000,0x4000};
    for (unsigned bank=0;bank<9;++bank) {
        const unsigned control=g_io[0x240+bank+(bank>=7?1:0)];
        if (!(control&0x80)) continue;
        const unsigned mst=control&7,ofs=(control>>3)&3;
        uintptr_t base=0;
        if (bank<4) {
            if (mst==1) base=0x06000000u+ofs*0x20000u;
            else if (bank<2 && mst==2) base=0x06400000u+(ofs&1)*0x20000u;
            else if (bank==2 && mst==4) base=0x06200000u;
            else if (bank==3 && mst==4) base=0x06600000u;
        } else if (bank==4) {
            if (mst==1) base=0x06000000u;
            else if (mst==2) base=0x06400000u;
        } else if (bank<7) {
            const unsigned offset=(ofs&1)*0x4000u+(ofs>>1)*0x10000u;
            if (mst==1) base=0x06000000u+offset;
            else if (mst==2) base=0x06400000u+offset;
        } else if (bank==7) {
            if (mst==1) base=0x06200000u;
        } else {
            if (mst==1) base=0x06208000u;
            else if (mst==2) base=0x06600000u;
        }
        if (!base) continue;
        if (mmap(reinterpret_cast<void *>(base),sizes[bank],PROT_READ|PROT_WRITE,
            MAP_SHARED|MAP_FIXED,g_vram_fd,offsets[bank])==MAP_FAILED)
            throw std::runtime_error("Cannot route native VRAM bank");
        g_vram_aliases[g_vram_alias_count++]={base,sizes[bank]};
    }
}
#endif

inline uint8_t *slot(uint32_t addr) { return g_io + (addr - IO_BASE); }

inline uint64_t raw_read(uint32_t addr, unsigned width) {
    uint64_t v = 0;
    std::memcpy(&v, slot(addr), width);
    return v;
}

inline void raw_write(uint32_t addr, uint64_t value, unsigned width) {
    std::memcpy(slot(addr), &value, width);
}

// --- CP: divider and square root -----------------------------------------------
// GBATEK: 0x4000280 DIVCNT, 0x4000290 NUMER(64), 0x4000298 DENOM(64),
//         0x40002A0 RESULT(64), 0x40002A8 REMAINDER(64),
//         0x40002B0 SQRTCNT, 0x40002B4 SQRT_RESULT(32), 0x40002B8 SQRT_PARAM(64).
// The busy bit (0x8000) is never set: the host computes synchronously, so any
// `while (DIVCNT & 0x8000);` spin-wait falls straight through.

constexpr uint32_t DIVCNT = 0x4000280, DIV_NUMER = 0x4000290, DIV_DENOM = 0x4000298;
constexpr uint32_t DIV_RESULT = 0x40002A0, DIV_REM = 0x40002A8;
constexpr uint32_t SQRTCNT = 0x40002B0, SQRT_RESULT = 0x40002B4, SQRT_PARAM = 0x40002B8;

void run_divide() {
    const uint16_t mode = static_cast<uint16_t>(raw_read(DIVCNT, 2)) & 3;
    const int64_t numer = static_cast<int64_t>(raw_read(DIV_NUMER, 8));
    const int64_t denom = static_cast<int64_t>(raw_read(DIV_DENOM, 8));

    int64_t n = numer, d = denom;
    if (mode == 0) {                       // 32 / 32
        n = static_cast<int32_t>(numer);
        d = static_cast<int32_t>(denom);
    } else if (mode == 1) {                // 64 / 32
        d = static_cast<int32_t>(denom);
    }

    int64_t quot, rem;
    if (d == 0) {
        // GBATEK: division by zero leaves remainder = numerator and a sign-based
        // quotient. Mirror that rather than trapping.
        rem = n;
        quot = (n < 0) ? 1 : -1;
        if (mode == 0) quot = (n < 0) ? 1 : -1;
    } else {
        quot = n / d;
        rem = n % d;
    }
    raw_write(DIV_RESULT, static_cast<uint64_t>(quot), 8);
    raw_write(DIV_REM, static_cast<uint64_t>(rem), 8);
}

uint32_t isqrt64(uint64_t v) {
    // Integer floor(sqrt(v)) by restoring bit-by-bit; matches the DS unit's result.
    uint64_t rem = 0, root = 0;
    for (int i = 0; i < 32; ++i) {
        root <<= 1;
        rem = (rem << 2) | (v >> 62);
        v <<= 2;
        if (rem > root) {
            rem -= root | 1;
            root += 2;
        }
    }
    return static_cast<uint32_t>(root >> 1);
}

void run_sqrt() {
    const uint16_t mode = static_cast<uint16_t>(raw_read(SQRTCNT, 2)) & 1;
    uint64_t param = raw_read(SQRT_PARAM, 8);
    if (mode == 0) param &= 0xFFFFFFFFull;      // 32-bit mode
    raw_write(SQRT_RESULT, isqrt64(param), 4);
}

}  // namespace

namespace {

// Reserve one DS region at its real address. Fixed low bases keep (u32)&x exact.
void *map_fixed(uintptr_t base, size_t size) {
#if defined(_WIN32)
    void *p = VirtualAlloc(reinterpret_cast<void *>(base), size,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *p = mmap(reinterpret_cast<void *>(base), size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) p = nullptr;
#endif
    if (p) std::memset(p, 0, size);
    return p;
}

}  // namespace

bool io_init() {
    if (g_io) return true;
    struct Region { uintptr_t base; size_t size; void *mapping = nullptr; };
    constexpr Region regions[] = {
        {MAIN_BASE, MAIN_SIZE}, {IO_BASE, IO_SIZE}, {PLTT_BASE, PLTT_SIZE},
        {VRAM_BASE, VRAM_SIZE}, {OAM_BASE, OAM_SIZE},
    };
    std::array<Region, sizeof(regions) / sizeof(regions[0])> mapped{};
    size_t mapped_count = 0;
    void *io = nullptr;
    for (const auto &r : regions) {
        void *p = map_fixed(r.base, r.size);
#if defined(__ANDROID__)
        // Android may reserve low virtual-address windows for its own runtime.
        // The I/O proxy and packed frontend can use a relocated host backing.
        // Do not replace the existing mapping with MAP_FIXED.
        if (!p && r.base == IO_BASE) {
            p = mmap(nullptr, r.size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) p = nullptr;
        }
#endif
        if (!p) {
            // MAIN is optional. Game code still directly addresses DS video
            // windows, so never overwrite an occupied mapping with MAP_FIXED.
            if (r.base == MAIN_BASE) continue;
#if defined(_WIN32)
            const unsigned long error = GetLastError();
#else
            const int error = errno;
#endif
            for (size_t i = mapped_count; i > 0; --i) {
#if defined(_WIN32)
                VirtualFree(mapped[i - 1].mapping, 0, MEM_RELEASE);
#else
                munmap(mapped[i - 1].mapping, mapped[i - 1].size);
#endif
            }
            throw std::runtime_error("Cannot map DS video region at address " +
                std::to_string(static_cast<unsigned long long>(r.base)) +
                " (size " + std::to_string(r.size) + ", error " +
                std::to_string(error) + ")");
        }
        mapped[mapped_count++] = {r.base, r.size, p};
        if (r.base == IO_BASE) io = p;
    }
#if !defined(_WIN32)
    if (!init_vram_banks()) {
        const int error = errno;
        for (size_t i = mapped_count; i > 0; --i)
            munmap(mapped[i - 1].mapping, mapped[i - 1].size);
        throw std::runtime_error("Cannot map DS VRAM bank backing (errno " +
            std::to_string(error) + ")");
    }
#endif
    g_io = static_cast<uint8_t *>(io);
    // The geometry FIFO starts empty and less than half full.
    raw_write(0x04000600u, 0x06000000u, 4);
    return true;
}

void *host_ptr(uint32_t addr) {
    // Only the Android I/O window may be relocated. Physical DS addresses
    // remain the keys for register side effects and packed frontend handles.
    if (addr >= IO_BASE && addr - IO_BASE < IO_SIZE && g_io)
        return g_io + (addr - IO_BASE);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(addr));
}

uint64_t io_read(uint32_t addr, unsigned width) {
    if (!g_io && !io_init()) return 0;
    if (addr==0x04000600u && width==4) return gx_status()|(raw_read(addr,4)&0xc0000000u);
    return raw_read(addr, width);
}

void io_write(uint32_t addr, uint64_t value, unsigned width) {
    if (!g_io && !io_init()) return;
    raw_write(addr, value, width);
#if !defined(_WIN32)
    if (addr<0x0400024au && addr+width>0x04000240u) update_vram_banks();
#endif

    // Writing the low half of the operand is what starts the unit on hardware.
    if (addr == DIV_DENOM || addr == DIV_NUMER || addr == DIVCNT) run_divide();
    else if (addr == SQRT_PARAM || addr == SQRTCNT) run_sqrt();
    // Geometry: 0x4000400 is the packed FIFO, 0x4000440.. are the command ports.
    // These execute on write and leave nothing readable behind, so unlike every
    // other block they cannot be served by the memory window alone.
    else if (addr>=0x04000400u && addr<0x04000440u) gx_write_fifo(static_cast<uint32_t>(value));
    else if (addr >= 0x04000440u && addr <= 0x040005FFu)
        gx_write_port(addr, static_cast<uint32_t>(value));
}

}  // namespace ntr
