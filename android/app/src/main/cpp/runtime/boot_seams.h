#pragma once
#include "ntr/mmio.h"
#include "runtime/boot_state.h"
#include "dScene_c.h"
#include "dFdColor_c.h"
#include "SaveData.h"
#include "Sound.h"
#include "PlayerInput.h"
#include "AsyncJob.h"
#include "MessageBank.h"
namespace GX {
void DisableAllBanks();
void SetBankForBG(u16);
void SetBankForOBJ(u16);
void SetBankForSubBG(u16);
void SetBankForSubOBJ(u16);
void LoadBGPltt(const void *, u32, u32);
}
namespace GXS {
void LoadBGPltt(const void *, u32, u32);
}
namespace G2S {
u32 GetBG1CharPtr();
u32 GetBG1ScrPtr();
u32 GetBG2CharPtr();
u32 GetBG2ScrPtr();
}
namespace Message {
void DisplaySaveStatusText(u32);
}
extern "C" {
extern dFdBrightness_c *data_0209f5bc, *data_0209d4ac;
extern dFdColor_c data_0209f5e8;
extern u8 data_0209f1e8, data_0209d454, data_0209d45c;
extern int data_0208ee44;
extern u16 data_020a0e58[], data_020a0e5a[], data_02092664;
extern u32 data_0209f5b8;
extern u8 *data_0209d6e8, *data_0209d6ec, *data_0209d6fc, *data_0209d708, *data_0209d70c;
extern u8 data_020914e0[0x48], data_02091570[0x168];
extern u8 data_020916d8[0x1ec], data_020918c4[0x84c], data_0209d3ac[2];
void _ZN2GX15DisableAllBanksEv();
void _ZN2GX15SetGraphicsModeEiii(int, int, int);
void _ZN3GXS15SetGraphicsModeEi(int);
void _ZN2GX6DispOnEv();
void *_ZN2G212GetBG0ScrPtrEv();
void *func_02054efc();
void DecompressLZ16(const void *, void *);
void SetBg0Offset(int, int);
void SetSubBg0Offset(int, int);
void SetSubBg1Offset(int, int);
void SetSubBg2Offset(int, int);
void func_020233f4();
int func_0201a1bc();
void LoadMessageBankForLanguage();
int GetOwnerLanguage();
int func_0203da3c();
void *LoadFile(int);
void Deallocate(void *);
u32 LoadCompressedFileAt(u16, void *);
void func_0201cd08(int);
u32 func_02012790(u32);
}
