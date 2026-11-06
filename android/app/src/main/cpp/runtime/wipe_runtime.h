#pragma once
#include "dWipe_c.h"
#include "IRQ.h"
#include "cartridge.h"
#include <cstddef>

// These are native-owned state and tables, never a host vptr in a DS record.
extern dWipe_c data_0209f61c;
extern u8 data_0209f5f8;
extern volatile u8 data_0209f5fc;
extern u8 data_0209f600[2],data_0209f604[2],data_020926c8[2],data_020926cc[2];
extern volatile u32 data_0209f608,data_0209f60c;
extern u8 data_0209f648[0x600];
extern short data_023c0000[0x2000];
namespace GX { void LoadBGPltt(const void *,u32,u32); }
namespace GXS { void LoadBGPltt(const void *,u32,u32); }
namespace cstd { int sqrt(unsigned long long); }
namespace sm64ds::wipe_detail {
void set_handler(u32,IRQ::Handler);
void set_external_handlers(IRQ::Handler,IRQ::Handler);
u32 enable(u32); u32 disable(u32);
int set_hblank(int);
void flush(const void *,size_t);
void fill32(int,int *,int);
void copy32(const int *,int *,int);
void blend(volatile u16 *,u16,int);
void palette_main(const void *,u32,u32); void palette_sub(const void *,u32,u32);
}
namespace sm64ds {
void initialize_wipe(Cartridge &);
void reset_wipe();
void prepare_wipe_scanlines();
bool wipe_hblank_active();
}
extern "C" void sm64ds_wipe_hblank();
extern "C" void sm64ds_wipe_vblank();
extern "C" void sm64ds_wipe_reset_original();
