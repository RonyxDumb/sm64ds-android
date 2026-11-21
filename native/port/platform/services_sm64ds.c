// The ARM7's services for Super Mario 64 DS, on the host: the PXI FIFO to the sound driver, the touch panel, power
// management, and the local wireless (not available yet: the game's own error paths run). Also the C++ runtime
// helpers and the few runtime functions the decomp keeps in assembly. From mkds-android's port/platform/services.c.
#include "port.h"

#include <stdlib.h>
#include <string.h>

#include "hostapi.h"

#include "types.h"
#include "nitro/pxi/fifo.h"
#include "nitro/spi/tp.h"

extern volatile bool g_quit;

// ------------------------------------------------------------------------------------------------ PXI
typedef void (*PxiCallback)(PXIFifoTag tag, u32 data, int err);
static PxiCallback s_pxi_callbacks[32];

void PXI_Init(void) {}
void PXI_InitFifo(void) {}
void PXI_SetFifoRecvCallback(int tag, PxiCallback callback) { s_pxi_callbacks[tag & 31] = callback; }
int PXI_IsCallbackReady(int tag, PXIProc proc) { (void) tag, (void) proc; return 1; }

// Replies of the ARM7 (sound alarms): delivered as the FIFO interrupt would.
void port_pxi_reply(int tag, u32 data) {
    if (s_pxi_callbacks[tag & 31]) {
        s_pxi_callbacks[tag & 31]((PXIFifoTag) tag, data, 0);
    }
}

void port_snd_pxi_word(u32 data) __attribute__((weak));

// Without the sound driver: every command list is done as soon as it is sent.
static u32 *s_snd_shared;
static void snd_fallback(u32 data) {
    typedef struct Cmd {
        struct Cmd *next;
        s32 id;
        u32 arg[4];
    } Cmd;
    for (Cmd *c = (Cmd *) (uintptr_t) data; c; c = c->next) {
        if (c->id == 0x19) s_snd_shared = (u32 *) (uintptr_t) c->arg[0]; // SET_SHARED_WORK
    }
    if (s_snd_shared && data) s_snd_shared[0]++; // finishCommandTag
}

int PXI_SendWordByFifo(int tag, u32 data, int err) {
    (void) err;
    switch (tag) {
    case PXI_FIFO_TAG_SOUND:
        if (port_snd_pxi_word) port_snd_pxi_word(data);
        else snd_fallback(data);
        break;
    default:
        port_log("pxi: tag %d word %08x not handled", tag, data);
        break;
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------ touch panel
static struct {
    int initialized, auto_sampling;
    TPData *buf;
    u16 buf_count, index;
    TPData raw;
} s_tp;

static void tp_sample(TPData *out) {
    PortInput in;
    port_input_get(&in);
    out->touch = in.touching ? 1 : 0;
    out->validity = 0;
    out->x = in.touching ? in.touch_x : 0;
    out->y = in.touching ? in.touch_y : 0;
}

void TP_Init(void) { s_tp.initialized = 1; }
// The firmware's calibration: identity (the host already gives screen coordinates).
int TP_GetUserInfo(TPCalibrateParam *calibrate) {
    memset(calibrate, 0, sizeof(*calibrate));
    return 1;
}
void TP_SetCalibrateParam(const TPCalibrateParam *param) { (void) param; }
void TP_RequestSamplingAsync(void) { tp_sample(&s_tp.raw); }
u32 TP_WaitCalibratedResult(TPData *result) {
    *result = s_tp.raw;
    return 0;
}
u32 TP_GetCalibratedResult(TPData *result) { return TP_WaitCalibratedResult(result); }
void TP_GetCalibratedPoint(TPData *disp, const TPData *raw) {
    *disp = *raw;
    if (!raw->touch) disp->x = disp->y = 0;
    if (disp->x > 255) disp->x = 255;
    if (disp->y > 191) disp->y = 191;
}
void TP_RequestAutoSamplingStartAsync(u16 vcount, u16 frequence, TPData samplingBufs[], u16 bufSize) {
    (void) vcount, (void) frequence;
    s_tp.buf = samplingBufs;
    s_tp.buf_count = bufSize;
    s_tp.index = 0;
    s_tp.auto_sampling = 1;
}
void TP_RequestAutoSamplingStopAsync(void) { s_tp.auto_sampling = 0; }
void TP_RequestSetStabilityAsync(u8 retry, u16 range) { (void) retry, (void) range; }
u16 TP_GetLatestIndexInAuto(void) { return s_tp.index; }
u32 TP_CheckError(u32 command) { (void) command; return 0; }
void TP_WaitBusy(u32 command) { (void) command; }

// Every frame: automatic sampling fills the game's ring once per frame.
void port_tp_vblank(void) {
    if (s_tp.auto_sampling && s_tp.buf && s_tp.buf_count) {
        s_tp.index = (u16) ((s_tp.index + 1) % s_tp.buf_count);
        tp_sample(&s_tp.buf[s_tp.index]);
    }
}

// ------------------------------------------------------------------------------------------------ power management
static u32 s_lcd_power = 1;
void PM_Init(void) {}
int PM_SetLCDPower(u32 sw) {
    s_lcd_power = sw;
    return 1;
}
int PM_GetLCDPower(void) { return (int) s_lcd_power; }
u32 PM_SetBackLight(u32 target, u32 sw) { (void) target, (void) sw; return 0; }
u32 PM_GetBackLight(u32 *top, u32 *bottom) {
    if (top) *top = 1;
    if (bottom) *bottom = 1;
    return 0;
}
u32 PM_ForceToPowerOff(void) {
    port_log("PM_ForceToPowerOff");
    g_quit = true;
    return 0;
}
u32 PMi_SendLEDPatternCommand(u32 pattern) { (void) pattern; return 0; }
u32 PMi_SetLED(u32 led) { (void) led; return 0; }
// Closing the lid: the game sleeps; nothing to do on a phone (the activity pauses instead).
void PM_GoSleepMode(u32 trigger, u32 logic, u32 keyPattern) { (void) trigger, (void) logic, (void) keyPattern; }

// ------------------------------------------------------------------------------------------------ cartridge slot
void CTRDG_Init(void) {}

// ------------------------------------------------------------------------------------------------ wireless
// Not available yet: every request fails as with the wireless switched off, and the game's own error paths run.
#define WM_ERRCODE_FAILED 1
#define WM_ERRCODE_WM_DISABLE 12
int WM_Init(void *work, u16 dmaNo) { (void) work, (void) dmaNo; return WM_ERRCODE_WM_DISABLE; }
int WM_Initialize(void *work, void *callback, u16 dmaNo) { (void) work, (void) callback, (void) dmaNo; return WM_ERRCODE_WM_DISABLE; }
int WM_Enable(void *callback) { (void) callback; return WM_ERRCODE_WM_DISABLE; }
int WM_Disable(void *callback) { (void) callback; return WM_ERRCODE_WM_DISABLE; }
int WM_PowerOn(void *callback) { (void) callback; return WM_ERRCODE_WM_DISABLE; }
int WM_PowerOff(void *callback) { (void) callback; return WM_ERRCODE_WM_DISABLE; }
int WM_Reset(void *callback) { (void) callback; return WM_ERRCODE_WM_DISABLE; }
int WM_End(void *callback) { (void) callback; return WM_ERRCODE_WM_DISABLE; }
int WM_Disconnect(void *callback, u16 aid) { (void) callback, (void) aid; return WM_ERRCODE_WM_DISABLE; }
int WM_SetIndCallback(void *callback) { (void) callback; return 0; }
int WM_SetPortCallback(u16 port, void *callback, void *arg) { (void) port, (void) callback, (void) arg; return 0; }
int WM_ReadStatus(void *status) { (void) status; return WM_ERRCODE_WM_DISABLE; }
u16 WM_GetAllowedChannel(void) { return 0; }
int WM_GetLinkLevel(void) { return 0; }
void *WM_GetSharedDataAddress(void *info, void *buf, u16 aid) { (void) info, (void) buf, (void) aid; return NULL; }
int WM_MeasureChannel(void *callback, u16 a, u16 b, u16 ch, u16 t) { (void) callback, (void) a, (void) b, (void) ch, (void) t; return WM_ERRCODE_WM_DISABLE; }
int WM_SetMPDataToPort(void *callback, const void *data, u16 size, u16 bitmap, u16 port, u16 prio) {
    (void) callback, (void) data, (void) size, (void) bitmap, (void) port, (void) prio;
    return WM_ERRCODE_WM_DISABLE;
}
int WM_SetParentParameter(void *callback, const void *param) { (void) callback, (void) param; return WM_ERRCODE_WM_DISABLE; }
int WM_StartConnect(void *callback, const void *info, const u8 *ssid) { (void) callback, (void) info, (void) ssid; return WM_ERRCODE_WM_DISABLE; }
int WM_StartDataSharing(void *info, u16 port, u16 bitmap, u16 size, int doubleMode) { (void) info, (void) port, (void) bitmap, (void) size, (void) doubleMode; return WM_ERRCODE_WM_DISABLE; }
int WM_StartMPEx(void *callback, void *recv, u16 rs, void *send, u16 ss, u16 freq, u16 retry, int a, int b, int c, int d) {
    (void) callback, (void) recv, (void) rs, (void) send, (void) ss, (void) freq, (void) retry, (void) a, (void) b, (void) c, (void) d;
    return WM_ERRCODE_WM_DISABLE;
}
int WM_StartParent(void *callback) { (void) callback; return WM_ERRCODE_WM_DISABLE; }
int WM_StepDataSharing(void *info, const u16 *send, void *set) { (void) info, (void) send, (void) set; return WM_ERRCODE_WM_DISABLE; }

// DS Download Play (the multi-card minigames' child download): not available.
int MB_Init(void *work, const void *user, u32 ggid, u32 tgid, u32 dma) { (void) work, (void) user, (void) ggid, (void) tgid, (void) dma; return WM_ERRCODE_FAILED; }
void MB_End(void) {}
int MB_StartParent(int channel) { (void) channel; return WM_ERRCODE_FAILED; }
void MB_SetParentCommParam(u16 a, u16 b) { (void) a, (void) b; }
int MB_RegisterFile(const void *desc, const void *buf) { (void) desc, (void) buf; return 0; }
u32 MB_GetSegmentLength(void *file) { (void) file; return 0; }
int MB_ReadSegment(void *file, void *buf, s32 len) { (void) file, (void) buf, (void) len; return 0; }
int MB_CommGetParentState(u16 child) { (void) child; return 0; }
u8 MB_CommGetChildrenNumber(void) { return 0; }
const void *MB_CommGetChildUser(u16 child) { (void) child; return NULL; }
int MB_CommIsBootable(u16 child) { (void) child; return 0; }
int MB_CommResponseRequest(u16 child, int ack) { (void) child, (void) ack; return 0; }
void MB_CommSetParentStateCallback(void *callback) { (void) callback; }
