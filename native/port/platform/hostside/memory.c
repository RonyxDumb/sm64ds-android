// The DS address space, reserved at the DS addresses in this 32-bit process.
//
// The game reads and writes main RAM, the shared area at 0x027ff000, the I/O registers, palettes, VRAM and OAM by
// absolute address, exactly as on the console. Every region is ordinary memory here; VRAM is the only one with a
// mapping that changes: banks A..I live in one shared-memory object and are mapped into the CPU's view
// (0x06000000..0x068a3fff) as the VRAMCNT registers say, the same way the console's memory controller does.
#include "port.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/sharedmem.h>
#else
#include <sys/syscall.h>
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

uint8_t *g_vram_banks;
const uint32_t g_vram_bank_offset[VRAM_BANK_COUNT] = {0x00000, 0x20000, 0x40000, 0x60000, 0x80000,
                                                      0x90000, 0x94000, 0x98000, 0xa0000};
const uint32_t g_vram_bank_size[VRAM_BANK_COUNT] = {0x20000, 0x20000, 0x20000, 0x20000, 0x10000,
                                                    0x4000,  0x4000,  0x8000,  0x4000};
// LCDC addresses of the banks (VRAMCNT MST 0).
static const uint32_t s_lcdc_addr[VRAM_BANK_COUNT] = {0x06800000, 0x06820000, 0x06840000, 0x06860000, 0x06880000,
                                                      0x06890000, 0x06894000, 0x06898000, 0x068a0000};
static int s_vram_fd = -1;

// The CPU-visible VRAM is managed in 16 KiB pages (the smallest bank and the largest Android page size).
#define VPAGE 0x4000u
#define VPAGE_COUNT (DS_VRAM_SIZE / VPAGE)
static int8_t s_page_bank[VPAGE_COUNT];     // bank mapped at each page, -1 = none
static uint32_t s_page_offset[VPAGE_COUNT]; // offset of that page in the bank

static bool reserve(uint32_t addr, uint32_t size) {
    void *p = mmap((void *) (uintptr_t) addr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                   -1, 0);
    if (p == MAP_FAILED || p != (void *) (uintptr_t) addr) {
        port_log("memory: cannot reserve %08x+%x (%s, got %p)", addr, size, strerror(errno), p);
        if (p != MAP_FAILED) {
            munmap(p, size);
        }
        return false;
    }
    return true;
}

int port_shared_create(const char *name, size_t size) {
#ifdef __ANDROID__
    return ASharedMemory_create(name, size);
#else
    int fd = (int) syscall(SYS_memfd_create, name, 0);
    if (fd >= 0 && ftruncate(fd, (off_t) size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
#endif
}

bool port_mem_init(void) {
    int fd = port_shared_create("ds-vram", VRAM_TOTAL_SIZE);
    if (fd < 0) {
        port_log("memory: cannot create the VRAM object");
        return false;
    }
    return port_mem_init_fd(fd);
}

bool port_mem_init_fd(int vram_fd) {
    if (!reserve(DS_ITCM_BASE, DS_ITCM_SIZE) || !reserve(DS_MAIN_BASE, DS_MAIN_SIZE) ||
        !reserve(DS_IO_BASE, DS_IO_SIZE) || !reserve(DS_IO2_BASE, 0x4000) || !reserve(DS_PLTT_BASE, 0x4000) ||
        !reserve(DS_VRAM_BASE, DS_VRAM_SIZE) || !reserve(DS_OAM_BASE, 0x4000) || !reserve(DS_GBA_BASE, 0x4000)) {
        return false;
    }
    s_vram_fd = vram_fd;
    // A second view of the banks for the renderer and the capture, also below 4 GiB (the arm64 guest reads it).
    g_vram_banks = mmap((void *) (uintptr_t) 0x0c000000, VRAM_TOTAL_SIZE, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_FIXED_NOREPLACE, s_vram_fd, 0);
    if (g_vram_banks == MAP_FAILED) {
        port_log("memory: cannot map the VRAM object");
        return false;
    }
    memset(s_page_bank, -1, sizeof(s_page_bank));
    // Power-on state of the registers the game reads before writing.
    IO16(0x130) = 0x03ff; // KEYINPUT: nothing pressed
    for (int bank = 0; bank < VRAM_BANK_COUNT; bank++) {
        port_vram_remap(bank);
    }
    return true;
}

// Where the bank is visible to the CPU for a VRAMCNT value: base address, or 0 when it is not CPU-visible
// (texture image / palette slots, extended palettes, disabled).
static uint32_t cpu_address(int bank, uint8_t cnt) {
    if (!(cnt & 0x80)) {
        return 0;
    }
    uint32_t mst = cnt & 7, ofs = (cnt >> 3) & 3;
    switch (bank) {
    case VRAM_A:
    case VRAM_B:
    case VRAM_C:
    case VRAM_D:
        if (bank >= VRAM_C && mst == 4) {
            return bank == VRAM_C ? 0x06200000 : 0x06600000; // C: BG engine B, D: OBJ engine B
        }
        switch (mst & 3) { // A/B: MST is 2 bits
        case 0:
            return s_lcdc_addr[bank];
        case 1:
            return 0x06000000 + ofs * 0x20000;
        case 2:
            // A/B: OBJ engine A; C/D: ARM7 work RAM, not visible here
            return bank <= VRAM_B ? 0x06400000 + (ofs & 1) * 0x20000 : 0;
        default:
            return 0; // texture image slot
        }
    case VRAM_E:
        switch (mst) {
        case 0: return s_lcdc_addr[bank];
        case 1: return 0x06000000;
        case 2: return 0x06400000;
        default: return 0; // texture palette, BG / OBJ extended palette
        }
    case VRAM_F:
    case VRAM_G:
        switch (mst) {
        case 0: return s_lcdc_addr[bank];
        case 1: return 0x06000000 + (ofs & 1) * 0x4000 + (ofs >> 1) * 0x10000;
        case 2: return 0x06400000 + (ofs & 1) * 0x4000 + (ofs >> 1) * 0x10000;
        default: return 0;
        }
    case VRAM_H:
        switch (mst & 3) {
        case 0: return s_lcdc_addr[bank];
        case 1: return 0x06200000;
        default: return 0; // BG extended palette of engine B
        }
    case VRAM_I:
        switch (mst & 3) {
        case 0: return s_lcdc_addr[bank];
        case 1: return 0x06208000;
        case 2: return 0x06600000;
        default: return 0; // OBJ extended palette of engine B
        }
    }
    return 0;
}

static void map_page(uint32_t page) {
    void *addr = (void *) (uintptr_t) (DS_VRAM_BASE + page * VPAGE);
    void *p;
    if (s_page_bank[page] < 0) {
        p = mmap(addr, VPAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    } else {
        int bank = s_page_bank[page];
        p = mmap(addr, VPAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, s_vram_fd,
                 (off_t) (g_vram_bank_offset[bank] + s_page_offset[page]));
    }
    if (p != addr) {
        port_fatal("memory: VRAM page %08x remap failed", (unsigned) (uintptr_t) addr);
    }
}

void port_vram_remap(int bank) {
    static const uint32_t cnt_offset[VRAM_BANK_COUNT] = {0x240, 0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x248, 0x249};
    // Drop the bank's previous pages.
    for (uint32_t page = 0; page < VPAGE_COUNT; page++) {
        if (s_page_bank[page] == bank) {
            s_page_bank[page] = -1;
            map_page(page);
        }
    }
    uint32_t base = cpu_address(bank, IO8(cnt_offset[bank]));
    if (base == 0) {
        return;
    }
    for (uint32_t off = 0; off < g_vram_bank_size[bank]; off += VPAGE) {
        uint32_t page = (base - DS_VRAM_BASE + off) / VPAGE;
        if (page >= VPAGE_COUNT) {
            break;
        }
        s_page_bank[page] = (int8_t) bank;
        s_page_offset[page] = off;
        map_page(page);
    }
}
