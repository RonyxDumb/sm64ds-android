// NitroSDK OS on the host for Super Mario 64 DS: thread switching, interrupts, alarms, ticks, VBlank, locks.
// Adapted from mkds-android's port/platform/os.c to this game's (older) SDK.
//
// Threads. The decomp's own scheduler (src/engine/os/OS_Thread.cpp) decides which OSThread runs; this file only
// replaces what it does in assembly: the context switch (port_os_switch, called by the patched OS_RescheduleThread).
// Each OSThread runs on its own host thread, and exactly one host thread runs game code at any time (the one whose
// OSThread is OSi_ThreadInfo.current), so the game sees the single CPU of the DS. A switch posts the next thread's
// semaphore and waits on its own.
//
// Interrupts. The DS interrupts game code at any instruction; here the interrupt sources (VBlank, alarms, deferred
// completions) are delivered at well-defined points: in the idle thread (OS_Halt), and at "safe points" in running
// code (interrupts re-enabled, card reads, decompression). Handlers run with the SDK's interrupt-mode rules: they may
// wake threads, the switch happens when they return.
#include "port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "hostapi.h"

#include "types.h"
#include "nitro/os/thread.h"
#include "nitro/os/alarm.h"
#include "nitro/os/interrupt.h"
#include "nitro/os/system.h"
#include "nitro/os/systemWork.h"
#include "nitro/os/spinLock.h"

volatile bool g_quit;
volatile bool g_paused; // the app is in the background: the game stops at its next wait
uint32_t g_vblank_rate_mhz = 59826;

extern OSThreadQueue OSi_IrqThreadQueue; // DTCM: threads waiting for an interrupt (OS_WaitIrq)
extern u32 OSi_IsThreadInitialized;

// ------------------------------------------------------------------------------------------------ logging
void port_log(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    hapi_log(line);
}

void port_fatal(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    port_log("FATAL: %s", buf);
    hapi_abort();
}

// ------------------------------------------------------------------------------------------------ time
uint64_t port_now_ns(void) { return hapi_now_ns(); }

// OS ticks run at the bus clock / 64 (33.513982 MHz / 64).
#define TICK_HZ 523655.96875
static uint64_t s_tick_origin_ns;
static int s_tick_initialized;

// The OS clock: real time, or with g_vblank_rate_mhz == 0 (tests) a virtual clock that only moves forward when every
// thread waits, straight to the next event, so that frames, alarms and threads keep the DS's proportions.
static uint64_t s_virtual_ns;
static uint64_t os_time_ns(void) { return g_vblank_rate_mhz ? port_now_ns() - s_tick_origin_ns : s_virtual_ns; }

static uint64_t ns_to_ticks(uint64_t ns) { return (uint64_t) ((double) ns * (TICK_HZ / 1e9)); }
static uint64_t ticks_to_ns(uint64_t ticks) { return (uint64_t) ceil((double) ticks * (1e9 / TICK_HZ)) + 1; }

void OS_InitTick(void) { s_tick_initialized = 1; }
s64 OS_GetTick(void) { return (s64) ns_to_ticks(os_time_ns()); }
int OS_GetTickLo(void) { return (int) (OS_GetTick() & 0xffff); }

static volatile u32 s_irq_disabled; // CPSR I bit of the running game code

// ------------------------------------------------------------------------------------------------ host threads
typedef struct HostThread {
    OSThread *thread;
    void *run; // semaphore, posted when the thread becomes the current one
    bool started;
} HostThread;

#define MAX_THREADS 64
static HostThread s_threads[MAX_THREADS];
static void *s_threads_lock;
#define t_self ((HostThread *) hapi_tls_get())

static HostThread *host_of(OSThread *thread) {
    if (!s_threads_lock) s_threads_lock = hapi_mutex_new();
    hapi_mutex_lock(s_threads_lock);
    HostThread *free_slot = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_threads[i].thread == thread) {
            hapi_mutex_unlock(s_threads_lock);
            return &s_threads[i];
        }
        if (!free_slot && s_threads[i].thread == NULL) {
            free_slot = &s_threads[i];
        }
    }
    if (!free_slot) {
        port_fatal("os: more than %d threads", MAX_THREADS);
    }
    void *sem = free_slot->run;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->run = sem; // semaphores are reused
    free_slot->thread = thread;
    if (!free_slot->run) free_slot->run = hapi_sem_new();
    hapi_mutex_unlock(s_threads_lock);
    return free_slot;
}

// OS_CreateThread's context: the entry point and its argument (r0). The stack is the host thread's own.
void OS_InitContext(OSContext *context, u32 func, u32 sp) {
    memset(context, 0, sizeof(*context));
    context->pc_plus4 = func;
    context->sp = sp;
}

static void host_thread_main(void *p) {
    HostThread *self = p;
    hapi_tls_set(self);
    hapi_sem_wait(self->run);
    s_irq_disabled = 0; // a new thread starts with interrupts enabled
    OSThread *t = self->thread;
    void (*func)(void *) = (void (*)(void *)) (uintptr_t) t->context.pc_plus4;
    func((void *) (uintptr_t) t->context.r[0]);
    OS_ExitThread();
}

// Hands the CPU from the calling host thread to `next` (OSi_ThreadInfo.current already is `next`). Returns when the
// calling thread is current again, unless it terminated (then its host thread ends).
void port_os_switch(OSThread *current, OSThread *next) {
    OSIntrMode saved_irq = s_irq_disabled; // the CPSR I bit is part of each thread's context
    HostThread *nh = host_of(next);
    HostThread *self = t_self;
    if (!nh->started) {
        nh->started = true;
        hapi_thread_start(host_thread_main, nh);
    }
    hapi_sem_post(nh->run);
    if (current->state == OS_THREAD_STATE_TERMINATED) {
        hapi_mutex_lock(s_threads_lock);
        if (self && self->thread == current) {
            self->thread = NULL; // the slot (and its semaphore) can be reused
        }
        hapi_mutex_unlock(s_threads_lock);
        hapi_thread_exit();
    }
    hapi_sem_wait(self->run);
    s_irq_disabled = saved_irq;
}

// ------------------------------------------------------------------------------------------------ interrupts
static bool s_in_irq;

bool port_os_in_irq(void) { return s_in_irq; }

OSIntrMode OS_DisableInterrupts(void) {
    OSIntrMode prev = (OSIntrMode) s_irq_disabled;
    s_irq_disabled = 0x80;
    return prev;
}

static void os_poll(void);

OSIntrMode OS_EnableInterrupts(void) {
    OSIntrMode prev = (OSIntrMode) s_irq_disabled;
    s_irq_disabled = 0;
    os_poll();
    return prev;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode state) {
    OSIntrMode prev = (OSIntrMode) s_irq_disabled;
    s_irq_disabled = state & 0x80;
    if (!s_irq_disabled) {
        os_poll();
    }
    return prev;
}

OSIntrMode OS_DisableInterrupts_IrqAndFiq(void) { return OS_DisableInterrupts(); }
OSIntrMode OS_RestoreInterrupts_IrqAndFiq(OSIntrMode state) { return OS_RestoreInterrupts(state); }

OSProcMode OS_GetProcMode(void) { return s_in_irq ? OS_PROCMODE_IRQ : OS_PROCMODE_SYS; }

// IRQ::SetIRQState / SetFIQState (src/IRQ3.cpp, assembly): the CPSR I/F bits.
void port_irq_set_irq_state(int enable) __asm__("_ZN3IRQ11SetIRQStateEb");
void port_irq_set_irq_state(int enable) {
    s_irq_disabled = (enable & 0xff) ? 0 : 0x80;
    if (!s_irq_disabled) os_poll();
}
void port_irq_set_fiq_state(int enable) __asm__("_ZN3IRQ11SetFIQStateEb");
void port_irq_set_fiq_state(int enable) { (void) enable; }

static void enter_irq(void) {
    s_in_irq = true;
    OSi_ThreadInfo.irqDepth++;
}
static void leave_irq(bool was_in_irq) {
    OSi_ThreadInfo.irqDepth--;
    s_in_irq = was_in_irq;
}

// What OS_IrqHandler and OS_IrqHandler_ThreadSwitch (src/IRQHandler.cpp) do on the DS, for the flags in mask.
void port_os_raise_irq(uint32_t mask) {
    IO32(0x214) |= mask; // IF
    if (!(IO16(0x208) & 1)) {
        return; // IME off: stays pending
    }
    bool was_in_irq = s_in_irq;
    enter_irq();
    u32 pending;
    while ((pending = IO32(0x214) & IO32(0x210)) != 0) {
        int bit = __builtin_ctz(pending); // the lowest pending interrupt first, as OS_IrqHandler
        IO32(0x214) &= ~(1u << bit);
        if (bit < OS_IRQ_TABLE_MAX && OS_IRQTable[bit]) {
            OS_IRQTable[bit]();
        }
        // OS_IrqHandler_ThreadSwitch: every thread waiting in OS_WaitIrq becomes ready.
        u16 waiting = OSi_IrqThreadQueue;
        if (waiting) {
            OSi_IrqThreadQueue = 0;
            for (OSThread *t = OSi_ThreadInfo.list; t; t = t->next) {
                if (waiting & (1u << t->id)) {
                    t->state = OS_THREAD_STATE_READY;
                }
            }
            OSi_ThreadInfo.isNeedRescheduling = 1;
        }
    }
    leave_irq(was_in_irq);
}

void OS_IrqHandler(void) { port_os_raise_irq(0); }

// ------------------------------------------------------------------------------------------------ alarms
static OSAlarm *s_alarms; // ordered by fire
static int s_alarm_initialized;

void OS_InitAlarm(void) {
    if (!s_alarm_initialized) {
        s_alarm_initialized = 1;
        s_alarms = NULL;
    }
}
int OS_IsAlarmAvailable(void) { return s_alarm_initialized; }
void OS_CreateAlarm(OSAlarm *alarm) {
    alarm->handler = NULL;
    alarm->tag = 0;
}

static void alarm_insert(OSAlarm *alarm) {
    OSAlarm **link = &s_alarms, *prev = NULL;
    while (*link && (*link)->fire <= alarm->fire) {
        prev = *link;
        link = &(*link)->next;
    }
    alarm->next = *link;
    alarm->prev = prev;
    if (*link) {
        (*link)->prev = alarm;
    }
    *link = alarm;
}

static void alarm_unlink(OSAlarm *alarm) {
    if (alarm->prev) {
        alarm->prev->next = alarm->next;
    } else if (s_alarms == alarm) {
        s_alarms = alarm->next;
    }
    if (alarm->next) {
        alarm->next->prev = alarm->prev;
    }
    alarm->prev = alarm->next = NULL;
}

void OS_SetAlarm(OSAlarm *alarm, s64 tick, OSAlarmHandler handler, void *arg) {
    if (!alarm || alarm->handler) {
        port_fatal("OS_SetAlarm: alarm %p already armed", (void *) alarm);
    }
    alarm->period = 0;
    alarm->handler = handler;
    alarm->arg = arg;
    alarm->fire = (u64) (OS_GetTick() + tick);
    alarm_insert(alarm);
}

void OS_SetPeriodicAlarm(OSAlarm *alarm, s64 start, s64 period, OSAlarmHandler handler, void *arg) {
    alarm->handler = handler;
    alarm->arg = arg;
    alarm->period = (u64) period;
    alarm->start = (u64) start;
    u64 now = (u64) OS_GetTick(), fire = (u64) start;
    if (fire < now && period) {
        fire += ((now - fire) / (u64) period + 1) * (u64) period;
    }
    alarm->fire = fire;
    alarm_insert(alarm);
}

void OS_CancelAlarm(OSAlarm *alarm) {
    if (!alarm->handler) {
        return;
    }
    alarm_unlink(alarm);
    alarm->handler = NULL;
    alarm->period = 0;
}

static void run_due_alarms(u64 now) {
    while (s_alarms && s_alarms->fire <= now) {
        OSAlarm *alarm = s_alarms;
        alarm_unlink(alarm);
        OSAlarmHandler handler = alarm->handler;
        void *arg = alarm->arg;
        if (alarm->period) {
            alarm->fire += alarm->period;
            alarm_insert(alarm);
        } else {
            alarm->handler = NULL;
        }
        // Alarms are timer interrupts on the DS.
        bool was_in_irq = s_in_irq;
        enter_irq();
        handler(arg);
        leave_irq(was_in_irq);
    }
}

// ------------------------------------------------------------------------------------------------ deferred calls
typedef struct Deferred {
    void (*fn)(void *);
    void *arg;
} Deferred;
static Deferred s_deferred[64];
static int s_deferred_count;

void port_os_defer(void (*fn)(void *), void *arg) {
    if (s_deferred_count == 64) {
        port_fatal("os: deferred queue full");
    }
    s_deferred[s_deferred_count].fn = fn;
    s_deferred[s_deferred_count].arg = arg;
    s_deferred_count++;
}

static bool run_deferred(void) {
    if (!s_deferred_count) {
        return false;
    }
    Deferred list[64];
    int n = s_deferred_count;
    memcpy(list, s_deferred, sizeof(Deferred) * n);
    s_deferred_count = 0;
    bool was_in_irq = s_in_irq;
    enter_irq();
    for (int i = 0; i < n; i++) {
        list[i].fn(list[i].arg);
    }
    leave_irq(was_in_irq);
    return true;
}

// ------------------------------------------------------------------------------------------------ VBlank pacing
uint64_t g_frame_count;
static uint64_t s_next_vblank_ns;
static uint64_t s_last_vblank_ns;

static uint64_t vblank_period_ns(void) { return 1000000000000ull / (g_vblank_rate_mhz ? g_vblank_rate_mhz : 59826); }

int64_t port_os_vblank_phase_ns(void) { return (int64_t) (os_time_ns() - s_last_vblank_ns); }
int64_t port_os_vblank_period_ns(void) { return (int64_t) vblank_period_ns(); }
void port_os_shift_vblank(int64_t ns) {
    if (g_vblank_rate_mhz) s_next_vblank_ns += (uint64_t) ns;
}

void (*g_port_vblank_hook)(void); // tests (host/main.c)

static void do_vblank(void) {
    s_last_vblank_ns = os_time_ns();
    if (g_port_vblank_hook) g_port_vblank_hook();
    port_frame_vblank();
    g_frame_count++;
    OS_GetSystemWork()->vblankCount++;
    IO16(0x004) |= 1; // DISPSTAT: in VBlank
    if (IO16(0x004) & 0x0008) { // VBlank IRQ enabled
        port_os_raise_irq(OS_IE_V_BLANK);
    }
    IO16(0x004) &= ~1;
}

// Runs every event that is due. Returns true when something ran.
static bool run_events(void) {
    bool ran = run_deferred();
    if (s_alarms) {
        u64 now = (u64) OS_GetTick();
        if (s_alarms->fire <= now) {
            run_due_alarms(now);
            ran = true;
        }
    }
    uint64_t now_ns = os_time_ns();
    if (now_ns >= s_next_vblank_ns) {
        s_next_vblank_ns += vblank_period_ns();
        if (now_ns > s_next_vblank_ns + vblank_period_ns() * 4) {
            s_next_vblank_ns = now_ns + vblank_period_ns(); // fell far behind: resynchronise
        }
        do_vblank();
        ran = true;
    }
    return ran;
}

// Sleeps until the next event.
static void wait_for_event(void) {
    uint64_t wake = s_next_vblank_ns;
    if (s_alarms) {
        uint64_t alarm_ns = ticks_to_ns(s_alarms->fire);
        if (alarm_ns < wake) {
            wake = alarm_ns;
        }
    }
    if (!g_vblank_rate_mhz) {
        if (wake > s_virtual_ns) {
            s_virtual_ns = wake;
        }
        return;
    }
    uint64_t now = os_time_ns();
    if (wake > now) {
        hapi_sleep_ns(wake - now);
    }
}

static void reschedule_if_needed(void) {
    if (OSi_ThreadInfo.isNeedRescheduling) {
        OSi_ThreadInfo.isNeedRescheduling = 0;
        OS_RescheduleThread();
    }
}

// Safe point: deliver due interrupts and switch if one of them woke a higher-priority thread.
static void os_poll(void) {
    if (s_irq_disabled || s_in_irq || !OSi_IsThreadInitialized) {
        return;
    }
    if (run_events()) {
        reschedule_if_needed();
    }
}

void port_os_poll(void) { os_poll(); }

// The idle thread's loop body (OSi_IdleThreadProc): the CPU halts until an interrupt.
void OS_Halt(void) {
    if (g_quit) {
        hapi_thread_exit();
    }
    while (g_paused && !g_quit) {
        hapi_sleep_ns(20000000);
    }
    OSIntrMode saved = (OSIntrMode) s_irq_disabled;
    s_irq_disabled = 0;
    if (!run_events()) {
        wait_for_event();
        run_events();
    }
    s_irq_disabled = saved;
    reschedule_if_needed();
}

void OS_SpinWait(u32 cycle) {
    (void) cycle;
    os_poll();
}

// The launcher thread is the host thread that calls port_os_run.
void port_os_run(void (*entry)(void)) {
    if (!s_tick_origin_ns) {
        s_tick_origin_ns = port_now_ns();
    }
    s_next_vblank_ns = os_time_ns() + vblank_period_ns();
    HostThread *self = host_of(&OSi_LauncherThread);
    self->started = true;
    hapi_tls_set(self);
    entry();
}

// ------------------------------------------------------------------------------------------------ system
// Caches, write buffer, TCM and protection unit do not exist here.
void DC_StoreAll(void) {}
void DC_FlushAll(void) {}
void DC_InvalidateRange(void *address, u32 size) { (void) address, (void) size; }
void DC_StoreRange(const void *address, u32 size) { (void) address, (void) size; }
void DC_FlushRange(const void *address, u32 size) { (void) address, (void) size; }
void DC_WaitWriteBufferEmpty(void) {}
void IC_InvalidateRange(void *address, u32 size) { (void) address, (void) size; }
void IC_InvalidateAll(void) {}
void OS_EnableDTCM(void) {}
void OS_SetProtectionRegion1(u32 param) { (void) param; }
void port_cp15_set_region7(u32 v) __asm__("_ZN4CP1514MPUDataRegion7Ej");
void port_cp15_set_region7(u32 v) { (void) v; }
u32 port_cp15_get_region7(void) __asm__("_ZN4CP1517MPUGetDataRegion7Ev");
u32 port_cp15_get_region7(void) { return 0; }

void OS_InitException(void) {}
void OS_SetUserExceptionHandler(void *handler, void *arg) { (void) handler, (void) arg; }
void OS_InitVAlarm(void) {}
void OS_InitReset(void) {}
void MI_SetWramBank(int cnt) { (void) cnt; }

// A retail DS with 4 MB of main memory.
u32 OS_GetConsoleType(void) { return 0x80000000u | 0x01; }
int OS_IsRunOnEmulator(void) { return 0; }

void OS_Terminate(void) {
    port_log("OS_Terminate");
    g_quit = true;
    for (;;) {
        hapi_thread_exit();
    }
}

// The game asks for a reset (after the ending, or the soft-reset button combination).
void OS_ResetSystem(u32 param) {
    port_log("OS_ResetSystem(%u)", param);
    OS_Terminate();
}

// The console's language (owner information): 0 Japanese, 1 English, 2 French, 3 German, 4 Italian, 5 Spanish. The
// host sets it from the phone's language before the game starts.
int g_port_language = 1;

// The owner information the DS firmware keeps (NitroSDK OSOwnerInfo): a fresh console in the chosen language.
void OS_GetOwnerInfo(void *dst) {
    static const char name[] = "Player";
    u8 *info = dst;
    memset(info, 0, 0x50);
    info[0] = (u8) g_port_language; // language
    info[1] = 0;                    // favourite colour
    info[2] = 1;                    // birthday month
    info[3] = 1;                    // birthday day
    u16 *nick = (u16 *) (info + 0x04);
    int n = 0;
    for (; name[n]; n++) nick[n] = (u8) name[n];
    *(u16 *) (info + 0x18) = (u16) n;
}

// Each device has its own address (kept next to the save file): stations tell each other apart by it.
void OS_GetMacAddress(void *macAddress) {
    u8 mac[6];
    hapi_net_mac(mac);
    memcpy(macAddress, mac, 6);
}

// ------------------------------------------------------------------------------------------------ lock words
// One CPU and no ARM7: a lock is taken as soon as it is asked for.
static u32 s_lock_ids;
s32 OS_GetLockID(void) {
    for (int i = 0; i < 32; i++) {
        if (!(s_lock_ids & (1u << i))) {
            s_lock_ids |= 1u << i;
            return 0x40 + i;
        }
    }
    return -3;
}
void OS_ReleaseLockID(u16 lockID) {
    if (lockID >= 0x40 && lockID < 0x60) {
        s_lock_ids &= ~(1u << (lockID - 0x40));
    }
}
void OS_InitLock(void) {}
u16 OS_ReadOwnerOfLockWord(OSLockWord *lockp) {
    (void) lockp;
    return 0;
}
s32 OS_TryLockCartridge(u16 lockID) { (void) lockID; return 0; }
s32 OS_UnLockCartridge(u16 lockID) { (void) lockID; return 0; }
s32 OS_UnlockCartridge(u16 lockID) { (void) lockID; return 0; }
s32 OS_LockCard(u16 lockID) { (void) lockID; return 0; }
s32 OS_UnlockCard(u16 lockID) { (void) lockID; return 0; }
s32 OS_TryLockCard(u16 lockID) { (void) lockID; return 0; }
s32 OS_LockByWord(u16 lockID, OSLockWord *lockp, void (*ctrl)(void)) {
    (void) lockID, (void) lockp;
    if (ctrl) ctrl();
    return 0;
}
s32 OS_UnlockByWord(u16 lockID, OSLockWord *lockp, void (*ctrl)(void)) {
    (void) lockID, (void) lockp;
    if (ctrl) ctrl();
    return 0;
}
s32 OS_TryLockByWord(u16 lockID, OSLockWord *lockp, void (*ctrl)(void)) { return OS_LockByWord(lockID, lockp, ctrl); }
