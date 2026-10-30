#include "runtime/packed_memory.h"
#include "runtime/packed_ptr.h"
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <vector>

// Packed source headers declare the cartridge's size_t inside each unit's
// namespace. Keep that ABI spelling out of the native test's global scope.
namespace test_types {
#include "nitro/hw/registers.h"
}
using test_types::u8;
using test_types::u16;
using test_types::u32;
using test_types::s32;

extern "C" void abi_GX_SetGraphicsMode(unsigned, unsigned, unsigned);
extern "C" void abi_GXS_SetGraphicsMode(unsigned);
extern "C" void abi_GX_DispOn();
extern "C" void abi_func_02053b98();
extern "C" void abi_func_ov007_020c1404(unsigned);

static void check(bool value, const char *why) {
    if (!value) throw std::runtime_error(why);
}

int main() {
    try {
        // Occupy the real DS register address before the production Android
        // I/O initializer runs. Never replace a mapping owned by the host.
        void *foreign = mmap(reinterpret_cast<void *>(ntr::IO_BASE), ntr::IO_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        check(foreign != MAP_FAILED, "Cannot reserve the foreign low I/O window");
        std::memset(foreign, 0xa5, ntr::IO_SIZE);
        check(ntr::io_init(), "I/O initialization failed");
        check(ntr::host_ptr(ntr::IO_BASE) != foreign, "Android I/O relocation was not exercised");

        sm64ds::GameHeap heap;
        sm64ds::packed::Space space(heap);
        std::vector<uint8_t> arm9(0x100000);
        space.map(0x02000000u, arm9.data(), arm9.size());
        space.map(ntr::IO_BASE, ntr::host_ptr(ntr::IO_BASE), ntr::IO_SIZE);
        sm64ds::packed::Space::Scope scope(space);
        *sm64ds::packed::Ptr<uint16_t>(0x02099fccu) = 1;

        // Exact phone trace before the repair: engine A on the lower LCD,
        // BG0 enabled but mistakenly decoded as text instead of geometry.
        ntr::io_write(0x04000000u, 0x08011b00u, 4);
        ntr::io_write(0x04000008u, 0x1712u, 2);
        abi_GX_SetGraphicsMode(1, 0, 1);
        const auto main = ntr::io_read(0x04000000u, 4);
        std::printf("Relocated MAIN DISPCNT: %08x (expected 08011b08)\n", unsigned(main));
        check(main == 0x08011b08u, "GX_SetGraphicsMode bypassed the translated I/O window");
        ntr::Framebuffer geometry{}, frame{};
        for (auto &row : geometry.px)
            for (auto &pixel : row) pixel = 0xff00ff00u;
        ntr::ppu_scanout(ntr::ENGINE_A, frame, &geometry);
        for (const auto &row : frame.px)
            for (auto pixel : row)
                check(pixel == 0xff00ff00u, "BG0 sampled tiled VRAM instead of the 3D layer");

        abi_func_02053b98();
        check((ntr::io_read(0x04000000u, 4) & 0x30000u) == 0, "GX_DispOff used raw MMIO");
        abi_GX_DispOn();
        check(ntr::io_read(0x04000000u, 4) == main, "GX_DispOn lost the 3D selection");
        ntr::io_write(0x04001000u, 0x00011c00u, 4);
        abi_GXS_SetGraphicsMode(3);
        check(ntr::io_read(0x04001000u, 4) == 0x00011c03u, "GXS mode used raw MMIO");

        REG_POWCNT1 = 0x820fu;
        REG_BG0OFS = 0x00120034u;
        *REG_DB_BG3PA = 0x100;
        *REG_DMA_CNT_PTR(2) = 0x12345678u;
        REG_EXMEMCNT = 0x4310;
        REG_WRAMCNT = 3;
        REG_TM0CNT_L = 0x55aa;
        REG_IE = 0x00100001u;
        check(ntr::io_read(0x04000304u, 2) == 0x820fu, "POWCNT1 used raw MMIO");
        check(ntr::io_read(0x04000010u, 4) == 0x00120034u, "BG offset used raw MMIO");
        check(ntr::io_read(0x04001030u, 2) == 0x100u, "Affine register pointer used raw MMIO");
        check(ntr::io_read(0x040000d0u, 4) == 0x12345678u, "DMA register pointer used raw MMIO");
        check(ntr::io_read(0x04000204u, 2) == 0x4310u, "EXMEMCNT used raw MMIO");
        check(ntr::io_read(0x04000247u, 1) == 3, "WRAMCNT used raw MMIO");
        check(ntr::io_read(0x04000100u, 2) == 0x55aau, "Timer used raw MMIO");
        check(ntr::io_read(0x04000210u, 4) == 0x00100001u, "IRQ mask used raw MMIO");

        // The original menu palette binder uses a nonvolatile local pointer.
        // Reading PLTT_BASE back cannot detect a missed command side effect:
        // even a plain memory write changes that latch. Check rendered texels.
        ntr::io_write(0x04000240u, 0x83, 1); // A: texture slot 0
        ntr::io_write(0x04000245u, 0x83, 1); // F: palette slot 0
        std::memset(ntr::host_ptr(0x06800000u), 1, 64);
        auto *palette = static_cast<uint16_t *>(ntr::host_ptr(0x06890000u));
        palette[1] = 0x001f;
        palette[9] = 0x03e0;
        auto texture = space.allocate(40);
        auto *record = static_cast<uint32_t *>(space.data(texture, 40));
        record[1] = 4; record[9] = 16;
        auto draw = [&](unsigned format, unsigned address, uint32_t expected) {
            ntr::gx_reset();
            ntr::gx_teximage_param(format << 26); // 8x8 indexed texture
            record[1] = format; record[9] = address;
            abi_func_ov007_020c1404(address ? texture : 0);
            auto command = [](unsigned id, uint32_t value) {
                ntr::gx_write_port(0x04000400u + id * 4, value);
            };
            command(0x40, 1);
            for (auto point : {0x1000f000u, 0xf000f000u, 0xf0001000u, 0x10001000u}) {
                command(0x23, point); command(0x23, 0);
            }
            frame = {};
            ntr::gx_render(frame);
            for (const auto &row : frame.px)
                for (auto pixel : row)
                    check(pixel == expected, "Menu palette command did not select the uploaded colours");
        };
        draw(4, 16, 0xff00ff00u); // 16-byte palette units
        std::memset(ntr::host_ptr(0x06800000u), 0x55, 16);
        draw(2, 16, 0xff00ff00u); // 2-bit format uses 8-byte units
        std::memset(ntr::host_ptr(0x06800000u), 1, 64);
        draw(4, 0, 0xffff0000u); // null material resets the palette
        space.release(texture);
        for (unsigned i = 0; i < ntr::IO_SIZE; ++i)
            check(static_cast<const uint8_t *>(foreign)[i] == 0xa5,
                  "A projected hardware macro modified foreign host memory");
        munmap(foreign, ntr::IO_SIZE);
        std::puts("Relocated hardware macros, original GX mode/palette calls and rendered colours passed.");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
