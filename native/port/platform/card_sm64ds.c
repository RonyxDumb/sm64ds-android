// The game card on the host for Super Mario 64 DS: ROM reads from the user's dump, and the backup memory (an 8 KB
// EEPROM, kept in the save file in the cartridge's own layout, as sm64ds-pc-port does). The decomp's backup tasks
// (src/unnamed/arm9/0206, CARD_IdentifyBackup, CARDi_EndTask) run unchanged; this file answers the requests they
// send to the ARM7 (CARDi_Request) and runs their tasks (CARDi_SetTask) on the calling thread.
#include "port.h"

#include <stdlib.h>
#include <string.h>

#include "hostapi.h"

#include "types.h"
#include "nitro/card/card_common.h"

void port_os_poll(void);
void port_os_defer(void (*fn)(void *), void *arg);

static const uint8_t *g_rom;
static uint32_t g_rom_size;

extern CARDiCommandArg data_020a8160;

void CARD_Init(void) {
    g_rom = hapi_rom(&g_rom_size);
    memcpy((void *) 0x027ffe00, g_rom, 0x160); // the ROM header, where the firmware leaves it
    CARDiCommon *p = &cardi_common;
    p->cmd = &data_020a8160;
    p->lock_owner = -3; // OS_LOCK_ID_ERROR: free
    p->lock_ref = 0;
    p->lock_queue = 0;
    p->busy_q = 0;
    p->dma = ~0u;
    p->flag = CARD_STAT_INIT | CARD_STAT_INIT_CMD;
}

void CARD_LockRom(u16 lockId) { (void) lockId; }
void CARD_UnlockRom(u16 lockId) { (void) lockId; }
void CARD_LockBackup(u16 lockId) { (void) lockId; }
void CARD_UnlockBackup(u16 lockId) { (void) lockId; }
void CARD_WaitRomAsync(void) {}
void CARDi_InitCommon(void) {}

typedef struct DeferredCall {
    MIDmaCallback fn;
    void *arg;
} DeferredCall;

static void run_callback(void *p) {
    DeferredCall *call = p;
    MIDmaCallback fn = call->fn;
    void *arg = call->arg;
    free(call);
    fn(arg);
}

void CARDi_ReadRom(u32 dma, const void *src, void *dst, u32 len, MIDmaCallback callback, void *arg, int isAsync) {
    (void) dma;
    u32 s = (u32) (uintptr_t) src;
    if ((uint64_t) s + len > g_rom_size) {
        port_log("card: ROM read %08x+%x past the end", s, len);
        len = s < g_rom_size ? g_rom_size - s : 0;
    }
    memcpy(dst, g_rom + s, len);
    if (callback) {
        if (isAsync) {
            DeferredCall *call = malloc(sizeof(*call));
            call->fn = callback;
            call->arg = arg;
            port_os_defer(run_callback, call);
        } else {
            callback(arg);
        }
    }
    port_os_poll(); // file loading is where the DS takes its VBlank interrupts
}

// ------------------------------------------------------------------------------------------------ backup memory
#define SAVE_SIZE 0x2000u
static uint8_t s_save[SAVE_SIZE];
static bool s_save_loaded, s_save_dirty;

static void save_load(void) {
    if (s_save_loaded) return;
    memset(s_save, 0xff, SAVE_SIZE); // an erased chip
    hapi_save_read(s_save, SAVE_SIZE);
    s_save_loaded = true;
}

void port_save_flush(void) {
    if (!s_save_dirty) return;
    if (hapi_save_write(s_save, SAVE_SIZE)) s_save_dirty = false;
}

// A request to the ARM7: its parameters are in p->cmd. Returns nonzero on success, as the SDK.
int CARDi_Request(CARDiCommon *p, s32 req, s32 retry) {
    (void) retry;
    save_load();
    CARDiCommandArg *cmd = p->cmd;
    p->command = req;
    cmd->result = CARD_RESULT_SUCCESS;
    switch (req) {
    case CARD_REQ_INIT:
    case CARD_REQ_IDENTIFY:
        break;
    case CARD_REQ_READ_BACKUP:
        if ((uint64_t) cmd->src + cmd->len > SAVE_SIZE) {
            cmd->result = CARD_RESULT_TIMEOUT;
            break;
        }
        memcpy((void *) (uintptr_t) cmd->dest, s_save + cmd->src, cmd->len);
        break;
    case CARD_REQ_PROGRAM_BACKUP:
        if ((uint64_t) cmd->dest + cmd->len > SAVE_SIZE) {
            cmd->result = CARD_RESULT_TIMEOUT;
            break;
        }
        memcpy(s_save + cmd->dest, (const void *) (uintptr_t) cmd->src, cmd->len);
        s_save_dirty = true;
        break;
    case CARD_REQ_VERIFY_BACKUP:
        if ((uint64_t) cmd->dest + cmd->len > SAVE_SIZE ||
            memcmp(s_save + cmd->dest, (const void *) (uintptr_t) cmd->src, cmd->len) != 0) {
            cmd->result = CARD_RESULT_TIMEOUT;
        }
        break;
    default:
        port_log("card: backup request %d not supported", (int) req);
        cmd->result = CARD_RESULT_TIMEOUT;
        break;
    }
    if (req == CARD_REQ_PROGRAM_BACKUP) {
        port_save_flush();
    }
    return cmd->result == CARD_RESULT_SUCCESS;
}

// The card thread's task: run on the calling thread, as the synchronous path does.
void CARDi_SetTask(CARDiTask task) {
    CARDiCommon *p = &cardi_common;
    p->cur_th = OSi_ThreadInfo.current;
    task(p);
}

// cardi_arg (data_020a8160): the request block shared with the ARM7 (its unit is replaced by this file).
CARDiCommandArg data_020a8160;
