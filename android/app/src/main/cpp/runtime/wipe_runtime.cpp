#include "wipe_runtime.h"
#include "ntr/mmio.h"
#include "ntr/ppu.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
#include <stdexcept>

dWipe_c data_0209f61c;
u8 data_0209f5f8=0;
volatile u8 data_0209f5fc=0;
u8 data_0209f600[2]{},data_0209f604[2]{},data_020926c8[2]{},data_020926cc[2]{};
volatile u32 data_0209f608=0,data_0209f60c=0;
alignas(4) u8 data_0209f648[0x600]{};
alignas(4) short data_023c0000[0x2000]{};
namespace {
IRQ::Handler hblank=nullptr;
bool interruptEnabled=false,displayEnabled=false;
}
namespace sm64ds::wipe_detail {
void set_handler(u32 mask,IRQ::Handler handler) {
    if (mask!=2 || (handler && handler!=sm64ds_wipe_hblank))
        throw std::runtime_error("Unsupported original wipe IRQ callback.");
    hblank=handler;
}
u32 enable(u32 mask) {
    if (mask!=2) throw std::runtime_error("Unsupported original wipe IRQ mask.");
    const u32 previous=interruptEnabled?2:0;
    interruptEnabled=true; return previous;
}
u32 disable(u32 mask) {
    if (mask!=2) throw std::runtime_error("Unsupported original wipe IRQ mask.");
    const u32 previous=interruptEnabled?2:0;
    interruptEnabled=false; return previous;
}
int set_hblank(int enabled) {
    const auto previous=ntr::io_read(0x04000004,2);
    displayEnabled=enabled!=0;
    ntr::io_write(0x04000004,(previous&~0x10u)|(displayEnabled?0x10u:0),2);
    return previous&0x10;
}
void flush(const void *source,size_t size) {
    if (!source && size) throw std::runtime_error("Invalid native wipe cache range.");
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
void fill32(int value,int *target,int bytes) {
    if (!target || bytes<0 || bytes%4)
        throw std::runtime_error("Invalid native wipe fill extent.");
    for (int i=0;i<bytes/4;++i) std::memcpy(target+i,&value,4);
}
void copy32(const int *source,int *target,int bytes) {
    if (!source || !target || bytes!=4)
        throw std::runtime_error("Invalid native wipe scanline copy.");
    const auto address=reinterpret_cast<uintptr_t>(target);
    if (address!=0x04000040u && address!=0x04001040u)
        throw std::runtime_error("Invalid native wipe window target.");
    u32 value; std::memcpy(&value,source,4);
    ntr::io_write(static_cast<uint32_t>(address),value,4);
}
void blend(volatile u16 *target,u16 mask,int value) {
    const auto address=reinterpret_cast<uintptr_t>(target);
    if (address!=0x04000050u && address!=0x04001050u)
        throw std::runtime_error("Invalid native wipe blend target.");
    value=std::clamp(value,-16,16);
    ntr::io_write(static_cast<uint32_t>(address),mask|(value<0?0xc0:0x80),2);
    ntr::io_write(static_cast<uint32_t>(address)+4,std::abs(value),2);
}
void palette_main(const void *source,u32 offset,u32 bytes) {
    if (!source || offset || bytes!=2)
        throw std::runtime_error("Invalid original wipe main palette upload.");
    std::memcpy(ntr::host_ptr(0x05000000),source,2);
}
void palette_sub(const void *source,u32 offset,u32 bytes) {
    if (!source || offset || bytes!=2)
        throw std::runtime_error("Invalid original wipe sub palette upload.");
    std::memcpy(ntr::host_ptr(0x05000400),source,2);
}
}
namespace sm64ds {
bool wipe_hblank_active() { return hblank && interruptEnabled && displayEnabled; }
void reset_wipe() {
    sm64ds_wipe_reset_original();
    hblank=nullptr; interruptEnabled=displayEnabled=false;
    data_0209f5f8=0; data_0209f5fc=0; data_0209f608=data_0209f60c=0;
    ntr::ppu_clear_window_lines();
}
void initialize_wipe(Cartridge &cartridge) {
    reset_wipe();
    const auto colors=cartridge.arm9(0x020926c8,8);
    std::memcpy(data_020926c8,colors.data(),2);
    std::memcpy(data_020926cc,colors.data()+4,2);
    std::memset(data_0209f600,0,2); std::memset(data_0209f604,0,2);
}
void prepare_wipe_scanlines() {
    ntr::ppu_clear_window_lines();
    if (!wipe_hblank_active()) return;
    const auto vcount=ntr::io_read(0x04000006,2);
    const auto status=ntr::io_read(0x04000004,2);
    std::array<u32,ntr::SCREEN_H> main{},sub{};
    // Execute the original VBlank table flip and original HBlank routine.
    // The host rasterizer consumes the resulting per-line window registers
    // once for BG and OBJ on both engines, without advancing game time.
    sm64ds_wipe_vblank();
    for (unsigned line=0;line<ntr::SCREEN_H;++line) {
        main[line]=ntr::io_read(0x04000040,4);
        sub[line]=ntr::io_read(0x04001040,4);
        ntr::io_write(0x04000006,line,2);
        ntr::io_write(0x04000004,status|2u,2);
        hblank();
    }
    ntr::io_write(0x04000006,vcount,2);
    ntr::io_write(0x04000004,status,2);
    ntr::ppu_set_window_lines(ntr::ENGINE_A,main.data());
    ntr::ppu_set_window_lines(ntr::ENGINE_B,sub.data());
}
}
