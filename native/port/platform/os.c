// NitroSDK OS on the host: thread switching, interrupts, alarms, ticks, arenas.
//
// Threads. The decomp's own scheduler (libs/NitroSDK/os/src/thread_scheduler.c) decides which OSThread runs; this file
// only replaces what it does in assembly: the context switch. Each OSThread runs on its own host thread, and exactly
// one host thread runs game code at any time (the one whose OSThread is OSi_ThreadInfo.mCurrentThread), so the game
// sees the single CPU of the DS. A switch posts the next thread's semaphore and waits on its own.
//
// Interrupts. The DS interrupts game code at any instruction; here the interrupt sources (VBlank, alarms, deferred
// completions) are delivered at well-defined points: when no thread is runnable (the DS would be halted in its idle
// thread) and at "safe points" in running code (interrupts re-enabled, card reads, decompression), which is where
// long-running threads such as the file loader spend their time. Handlers run with the SDK's interrupt-mode rules: they
// may wake threads, the switch happens when they return.
#include "port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "hostapi.h"

#include "types.h"
#include "registers.h"
#include "arena_cache_init.h"
#include "interrupt_control.h"
#include "interrupt_table_init.h"
#include "irq_wait_handler.h"
#include "lock_word_and_bus_ownership.h"
#include "reset_interrupts_halt.h"
#include "system_state_globals.h"
#include "thread_scheduler.h"
#include "Main/Net/block_transfer_and_thread_globals.h"
#include "thread_sync_and_console_info.h"
#include "tick_counter.h"
#include "alarm_timer.h"
#include "memory_interface_init.h"
#include "card_rom_backup_access.h"
#include "power_management_commands.h"

volatile bool g_quit;
volatile bool g_paused; // the app is in the background: the game stops at its next wait
uint32_t g_vblank_rate_mhz = 59826;

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
volatile u64 gTickOverflowCount;
u16 gTickInitialized;

// The OS clock: real time, or with g_vblank_rate_mhz == 0 (tests) a virtual clock that only moves forward when every
// thread waits, straight to the next event, so that frames, alarms and threads keep the DS's proportions.
static uint64_t s_virtual_ns;
static uint64_t os_time_ns(void) { return g_vblank_rate_mhz ? port_now_ns() - s_tick_origin_ns : s_virtual_ns; }

static uint64_t ns_to_ticks(uint64_t ns) { return (uint64_t) ((double) ns * (TICK_HZ / 1e9)); }
static uint64_t ticks_to_ns(uint64_t ticks) { return (uint64_t) ceil((double) ticks * (1e9 / TICK_HZ)) + 1; }

void OS_InitTick(void) {
    gTickInitialized = 1;
}
u32 OS_IsTickAvailable(void) { return gTickInitialized; }
u64 OS_GetTick(void) {
    uint64_t t = ns_to_ticks(os_time_ns());
    gTickOverflowCount = t >> 16;
    return t;
}
u32 OS_GetTickLo(void) { return (u32) OS_GetTick() & 0xffff; }

static volatile u32 s_irq_disabled; // CPSR I bit of the running game code

// ------------------------------------------------------------------------------------------------ host threads
typedef struct HostThread {
    OSThread *thread;
    void (*func)(void *);
    void *arg;
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

static void host_thread_main(void *p) {
    HostThread *self = p;
    hapi_tls_set(self);
    hapi_sem_wait(self->run);
    s_irq_disabled = 0; // a new thread starts with interrupts enabled
    self->func(self->arg);
    OS_ExitThread();
}

// ------------------------------------------------------------------------------------------------ interrupts
static bool s_in_irq;

bool port_os_in_irq(void) { return s_in_irq; }

OSIntrMode OS_DisableInterrupts(void) {
    OSIntrMode prev = s_irq_disabled;
    s_irq_disabled = 0x80;
    return prev;
}

static void os_poll(void);

OSIntrMode OS_EnableInterrupts(void) {
    OSIntrMode prev = s_irq_disabled;
    s_irq_disabled = 0;
    os_poll();
    return prev;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode state) {
    OSIntrMode prev = s_irq_disabled;
    s_irq_disabled = state & 0x80;
    if (!s_irq_disabled) {
        os_poll();
    }
    return prev;
}

OSIntrMode OS_DisableInterrupts_IrqAndFiq(void) { return OS_DisableInterrupts(); }
OSIntrMode OS_RestoreInterrupts_IrqAndFiq(OSIntrMode state) { return OS_RestoreInterrupts(state); }

// 0x12 is IRQ mode, 0x1f system mode.
u32 OS_GetProcMode(void) { return s_in_irq ? 0x12 : 0x1f; }

// What OS_IrqHandler and OS_IrqHandler_ThreadSwitch do on the DS, for the flags in mask.
void port_os_raise_irq(uint32_t mask) {
    IO32(0x214) |= mask; // IF
    if (!(IO16(0x208) & 1)) {
        return; // IME off: stays pending
    }
    bool was_in_irq = s_in_irq;
    s_in_irq = true;
    OSi_ThreadInfo.mIrqDepth++;
    u32 pending;
    while ((pending = IO32(0x214) & IO32(0x210)) != 0) {
        int bit = 31 - __builtin_clz(pending);
        IO32(0x214) &= ~(1u << bit);
        if (bit < 22 && OS_IRQTable[bit]) {
            OS_IRQTable[bit]();
        }
        // OS_IrqHandler_ThreadSwitch: every thread waiting in OS_WaitIrq becomes runnable.
        for (OSThread *t = OSi_IrqThreadQueue.mHead; t; ) {
            OSThread *next = t->mLinkNext;
            t->mState = THREAD_STATE_RUNNABLE;
            t->mQueue = NULL;
            t->mLinkPrev = NULL;
            t->mLinkNext = NULL;
            t = next;
        }
        if (OSi_IrqThreadQueue.mHead) {
            OSi_IrqThreadQueue.mHead = OSi_IrqThreadQueue.mTail = NULL;
            OSi_ThreadInfo.mIsNeedRescheduling = 1;
        }
    }
    OSi_ThreadInfo.mIrqDepth--;
    s_in_irq = was_in_irq;
}

void OS_IrqHandler(void) { port_os_raise_irq(0); }

// ------------------------------------------------------------------------------------------------ alarms
static OSAlarm *s_alarms; // ordered by mFire
u16 gAlarmInitialized;

void OS_InitAlarm(void) {
    if (!gAlarmInitialized) {
        gAlarmInitialized = 1;
        s_alarms = NULL;
    }
}
u32 OS_IsAlarmAvailable(void) { return gAlarmInitialized; }
void OS_CreateAlarm(OSAlarm *alarm) {
    alarm->mHandler = NULL;
    alarm->mTag = 0;
}

static void alarm_insert(OSAlarm *alarm) {
    OSAlarm **link = &s_alarms, *prev = NULL;
    while (*link && (*link)->mFire <= alarm->mFire) {
        prev = *link;
        link = &(*link)->mNext;
    }
    alarm->mNext = *link;
    alarm->mPrev = prev;
    if (*link) {
        (*link)->mPrev = alarm;
    }
    *link = alarm;
}

static void alarm_unlink(OSAlarm *alarm) {
    if (alarm->mPrev) {
        alarm->mPrev->mNext = alarm->mNext;
    } else if (s_alarms == alarm) {
        s_alarms = alarm->mNext;
    }
    if (alarm->mNext) {
        alarm->mNext->mPrev = alarm->mPrev;
    }
    alarm->mPrev = alarm->mNext = NULL;
}

void OS_SetAlarm(OSAlarm *alarm, u64 tick, OSAlarmHandler handler, void *arg) {
    if (!alarm || alarm->mHandler) {
        port_fatal("OS_SetAlarm: alarm %p already armed", (void *) alarm);
    }
    alarm->mPeriod = 0;
    alarm->mHandler = handler;
    alarm->mArg = arg;
    alarm->mFire = OS_GetTick() + tick;
    alarm_insert(alarm);
}

void OS_SetPeriodicAlarm(OSAlarm *alarm, u64 start, u64 period, OSAlarmHandler handler, void *arg) {
    alarm->mHandler = handler;
    alarm->mArg = arg;
    alarm->mPeriod = period;
    alarm->mStart = start;
    u64 now = OS_GetTick(), fire = start;
    if (fire < now && period) {
        fire += ((now - fire) / period + 1) * period;
    }
    alarm->mFire = fire;
    alarm_insert(alarm);
}

void OS_CancelAlarm(OSAlarm *alarm) {
    if (!alarm->mHandler) {
        return;
    }
    alarm_unlink(alarm);
    alarm->mHandler = NULL;
    alarm->mPeriod = 0;
}

static void run_due_alarms(u64 now) {
    while (s_alarms && s_alarms->mFire <= now) {
        OSAlarm *alarm = s_alarms;
        alarm_unlink(alarm);
        OSAlarmHandler handler = alarm->mHandler;
        void *arg = alarm->mArg;
        if (alarm->mPeriod) {
            alarm->mFire += alarm->mPeriod;
            alarm_insert(alarm);
        } else {
            alarm->mHandler = NULL;
        }
        // Alarms are timer 1 interrupts on the DS.
        bool was_in_irq = s_in_irq;
        s_in_irq = true;
        OSi_ThreadInfo.mIrqDepth++;
        handler(arg);
        OSi_ThreadInfo.mIrqDepth--;
        s_in_irq = was_in_irq;
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
    s_in_irq = true;
    OSi_ThreadInfo.mIrqDepth++;
    for (int i = 0; i < n; i++) {
        list[i].fn(list[i].arg);
    }
    OSi_ThreadInfo.mIrqDepth--;
    s_in_irq = was_in_irq;
    return true;
}

// ------------------------------------------------------------------------------------------------ VBlank pacing
uint64_t g_frame_count;
static uint64_t s_next_vblank_ns;

static uint64_t vblank_period_ns(void) { return 1000000000000ull / (g_vblank_rate_mhz ? g_vblank_rate_mhz : 59826); } // ns per VBlank

static uint64_t s_last_vblank_ns;

// For the wireless (wm7.c): where this console is in its frame, and a nudge of its VBlank timing. On the DS, MP
// children keep their VBlank in step with the parent's; here a child steers its own from when the parent's data
// arrives.
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
    SYSTEM_WORK->mVblankCount++;
    if (IO16(0x004) & 0x0008) { // DISPSTAT: VBlank IRQ enabled
        port_os_raise_irq(1);
    }
}

// Runs every event that is due. Returns true when something ran.
bool port_wm7_poll(void);   // wm7.c
bool port_wm7_active(void);
static bool run_events(bool idle) {
    bool ran = run_deferred();
    ran |= port_wm7_poll();
    if (s_alarms) {
        u64 now = OS_GetTick();
        if (s_alarms->mFire <= now) {
            run_due_alarms(now);
            ran = true;
        }
    }
    (void) idle;
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

// Sleeps until the next event when nothing is runnable.
static void wait_for_event(void) {
    uint64_t wake = s_next_vblank_ns;
    if (s_alarms) {
        uint64_t alarm_ns = ticks_to_ns(s_alarms->mFire);
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
    if (port_wm7_active() && wake > now + 1000000) {
        wake = now + 1000000; // the wireless delivers its messages at interrupt points: look again soon
    }
    if (wake > now) {
        hapi_sleep_ns(wake - now);
    }
}

// Safe point: deliver due interrupts and switch if one of them woke a higher-priority thread.
static void os_poll(void) {
    if (s_irq_disabled || s_in_irq || !OSi_IsThreadInitialized) {
        return;
    }
    if (run_events(false) && OSi_ThreadInfo.mIsNeedRescheduling) {
        OSi_ThreadInfo.mIsNeedRescheduling = 0;
        OSi_RescheduleThread();
    }
}

void port_os_poll(void) { os_poll(); }

// ------------------------------------------------------------------------------------------------ scheduler

extern u32 OSi_RescheduleCount;
extern OSThread **OSi_CurrentThreadPtr;
extern OSThread OSi_IdleThread;
extern void (*OSi_SystemCallbackInSwitchThread)(OSThread *, OSThread *);

void OS_InitThread(void) {
    if (OSi_IsThreadInitialized) {
        return;
    }
    OSi_IsThreadInitialized = 1;
    OSi_CurrentThreadPtr = &OSi_ThreadInfo.mCurrentThread;
    OSi_LauncherThread.mPriority = THREAD_PRIORITY_LAUNCHER;
    OSi_LauncherThread.mId = 0;
    OSi_LauncherThread.mState = THREAD_STATE_RUNNABLE;
    OSi_LauncherThread.mNext = NULL;
    OSi_LauncherThread.mProfiler = 0;
    OSi_LauncherThread.mJoinQueue.mHead = OSi_LauncherThread.mJoinQueue.mTail = NULL;
    OSi_ThreadInfo.mListHead = &OSi_LauncherThread;
    OSi_ThreadInfo.mCurrentThread = &OSi_LauncherThread;
    OSi_ThreadInfo.mIsNeedRescheduling = 0;
    OSi_ThreadInfo.mIrqDepth = 0;
    *(ThreadSystem **) HW_THREADINFO_MAIN = &OSi_ThreadInfo;
    OS_SetSwitchThreadCallback(NULL);
    // The launcher thread is the host thread that called port_os_run.
    hapi_tls_set(host_of(&OSi_LauncherThread));
    t_self->started = true;
    // No idle thread: when nothing is runnable OSi_RescheduleThread waits for an interrupt itself.
    OSi_IdleThread.mPriority = THREAD_PRIORITY_IDLE;
    OSi_IdleThread.mState = THREAD_STATE_FINISHED;
}

void OS_CreateThread(OSThread *thread, void (*func)(void *), void *arg, u8 *stack, u32 stackSize, u32 prio) {
    OSIntrMode enabled = OS_DisableInterrupts();
    thread->mPriority = prio;
    thread->mId = OSi_GetUnusedThreadId();
    thread->mState = THREAD_STATE_PARKED;
    thread->mProfiler = 0;
    OSi_InsertThreadToList(thread);
    thread->mStackBottom = stack;
    thread->mStackTop = stack - stackSize;
    thread->mStackWarningOffset = 0;
    thread->mJoinQueue.mHead = thread->mJoinQueue.mTail = NULL;
    thread->mR0 = arg;
    thread->mLr = OS_ExitThread;
    OS_SetThreadDestructor(thread, NULL);
    thread->mQueue = NULL;
    thread->mLinkNext = thread->mLinkPrev = NULL;
    thread->mAlarmForSleep = NULL;
    thread->mMutex = NULL;
    thread->mMutexQueue.mHead = thread->mMutexQueue.mTail = NULL;

    HostThread *host = host_of(thread);
    host->func = func;
    host->arg = arg;
    host->started = false;
    OS_RestoreInterrupts(enabled);
}

static void start_host(HostThread *host) {
    host->started = true;
    hapi_thread_start(host_thread_main, host);
}

// Hands the CPU from the calling host thread to `next`. Returns when the calling thread is current again, unless it
// is finished (then the host thread ends).
static void switch_to(OSThread *current, OSThread *next) {
    if (OSi_SystemCallbackInSwitchThread) {
        OSi_SystemCallbackInSwitchThread(current, next);
    }
    if (OSi_ThreadInfo.mSwitchCallback) {
        OSi_ThreadInfo.mSwitchCallback(current, next);
    }
    OSi_ThreadInfo.mCurrentThread = next;
    OSIntrMode saved_irq = s_irq_disabled; // the CPSR I bit is part of each thread's context
    HostThread *nh = host_of(next);
    HostThread *self = t_self;
    if (!nh->started) {
        start_host(nh);
    }
    hapi_sem_post(nh->run);
    if (current->mState == THREAD_STATE_FINISHED) {
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

void OSi_RescheduleThread(void) {
    if (OSi_RescheduleCount != 0) {
        return;
    }
    if (OSi_ThreadInfo.mIrqDepth != 0 || s_in_irq) {
        OSi_ThreadInfo.mIsNeedRescheduling = 1;
        return;
    }
    OSThread *current = *OSi_CurrentThreadPtr;
    OSThread *next = OS_SelectThread();
    // Nothing runnable: the DS idles until an interrupt wakes a thread. Deliver events until one does.
    while (next == NULL) {
        if (g_quit) {
            hapi_thread_exit();
        }
        while (g_paused && !g_quit) {
            hapi_sleep_ns(20000000);
        }
        OSIntrMode saved = s_irq_disabled;
        s_irq_disabled = 0; // the idle thread runs with interrupts enabled
        if (!run_events(true)) {
            wait_for_event();
        }
        s_irq_disabled = saved;
        OSi_ThreadInfo.mIsNeedRescheduling = 0;
        next = OS_SelectThread();
    }
    if (current == next) {
        return;
    }
    switch_to(current, next);
}

void OSi_ExitThread_ArgSpecified(OSThread *thread, void *arg) {
    (void) thread;
    OSi_ExitThread(arg);
}

void OSi_ExitThread_Destroy(void) {
    OSThread *current = *OSi_CurrentThreadPtr;
    OS_DisableScheduler();
    OSi_UnlockAllMutex(current);
    OSi_RemoveThreadFromList(current);
    current->mState = THREAD_STATE_FINISHED;
    OS_WakeupThread(&current->mJoinQueue);
    OS_EnableScheduler();
    OSi_RescheduleThread();
    port_fatal("os: finished thread resumed");
}

void port_os_run(void (*entry)(void)) {
    if (!s_tick_origin_ns) {
        s_tick_origin_ns = port_now_ns();
    }
    s_next_vblank_ns = os_time_ns() + vblank_period_ns();
    entry();
}

// ------------------------------------------------------------------------------------------------ arenas, system
static void set_arena(OSArenaId id, u32 lo, u32 hi) {
    SYSTEM_WORK->mArenaInfo.lo[id] = (void *) (uintptr_t) lo;
    SYSTEM_WORK->mArenaInfo.hi[id] = (void *) (uintptr_t) hi;
}

void *OS_GetInitArenaLo(OSArenaId id) {
    switch (id) {
    case OS_ARENA_MAIN: return (void *) (uintptr_t) PORT_ARENA_MAIN_LO;
    case OS_ARENA_ITCM: return (void *) (uintptr_t) DS_ITCM_BASE;
    case OS_ARENA_DTCM: return (void *) (uintptr_t) DS_DTCM_BASE;
    case OS_ARENA_SHARED: return (void *) 0x027ff000;
    default: return NULL;
    }
}

void *OS_GetInitArenaHi(OSArenaId id) {
    switch (id) {
    case OS_ARENA_MAIN: return (void *) (uintptr_t) PORT_ARENA_MAIN_HI;
    case OS_ARENA_ITCM: return (void *) (uintptr_t) (DS_ITCM_BASE + DS_ITCM_SIZE);
    case OS_ARENA_DTCM: return (void *) (uintptr_t) (DS_DTCM_BASE + 0x3f80 - 0x800);
    case OS_ARENA_SHARED: return (void *) 0x027ff000;
    default: return NULL;
    }
}

void OS_InitArena(void) {
    for (int id = 0; id < OS_ARENA_MAX; id++) {
        set_arena(id, (u32) (uintptr_t) OS_GetInitArenaLo(id), (u32) (uintptr_t) OS_GetInitArenaHi(id));
    }
}
void OS_InitArenaEx(void) {}
void OS_DisableMainExArena(void) {}
void *OS_GetArenaLo(OSArenaId id) { return SYSTEM_WORK->mArenaInfo.lo[id]; }
void *OS_GetArenaHi(OSArenaId id) { return SYSTEM_WORK->mArenaInfo.hi[id]; }
void OS_SetArenaLo(OSArenaId id, void *lo) { SYSTEM_WORK->mArenaInfo.lo[id] = lo; }
void OS_SetArenaHi(OSArenaId id, void *hi) { SYSTEM_WORK->mArenaInfo.hi[id] = hi; }

u32 OSi_Initialized;

extern int g_port_language;
void OS_Init(void) {
    if (OSi_Initialized) {
        return;
    }
    OSi_Initialized = 1;
    // The user settings the firmware leaves at 0x027ffc80: the language in bits 0-2 of the halfword at +0x64.
    *(volatile u16 *) (uintptr_t) 0x027ffce4 = (u16) ((*(volatile u16 *) (uintptr_t) 0x027ffce4 & ~7) | (g_port_language & 7));
    // Wireless channels the firmware allows: 1 to 13.
    if (!SYSTEM_WORK->mNvramUserInfo.mAllowedChannel) SYSTEM_WORK->mNvramUserInfo.mAllowedChannel = 0x1fff;
    OS_InitArena();
    OS_InitLock();
    OS_InitIrqTable();
    MI_Init();
    OS_InitThread();
    CARD_Init();
    PM_Init();
}

// Caches and the write buffer do not exist here.
void DC_StoreAll(void) {}
void DC_FlushAll(void) {}
void DC_InvalidateRange(void *address, u32 size) { (void) address, (void) size; }
void DC_StoreRange(const void *address, u32 size) { (void) address, (void) size; }
void DC_FlushRange(const void *address, int size) { (void) address, (void) size; }
void DC_WaitWriteBufferEmpty(void) {}
void IC_InvalidateRange(void *address, u32 size) { (void) address, (void) size; }

void OS_InitException(void) {}
void OS_InitVAlarm(void) {}
void OS_InitReset(void) {}
void OS_EnableProtectionUnit(void) {}
void OS_DisableProtectionUnit(void) {}
void OS_SetProtectionRegion1(u32 param) { (void) param; }
void OS_SetProtectionRegion2(u32 param) { (void) param; }
u32 OS_GetDTCMAddress(void) { return DS_DTCM_BASE; }

void OS_Terminate(void) {
    port_log("OS_Terminate");
    g_quit = true;
    for (;;) {
        hapi_thread_exit();
    }
}

void OS_Halt(void) { OSi_RescheduleThread(); }

void OS_SpinWait(u32 cycle) {
    (void) cycle;
    os_poll();
}

void WaitByLoop(s32 loopCount) {
    (void) loopCount;
    os_poll();
}

void VBlankIntrWait(void) { OS_WaitIrq(1, 1); }

// The console's language (owner information): 1 English, 2 French, 3 German, 4 Italian, 5 Spanish. The host sets
// it from the phone's language before the game starts.
int g_port_language = 1;

// The player's name for multiplayer and races, when the app's network settings give one (10 characters, UTF-16, as
// the save file's nickname; the host writes it, at any time). The game takes its name through port_player_name
// where it tells it to other players and where it sets up a race (tools/portify.py).
u16 g_port_player_name[12];

void *port_player_name(void *saved) {
    static u16 name[10];
    if (!g_port_player_name[0]) return saved;
    memcpy(name, g_port_player_name, sizeof(name));
    return name;
}

// The owner information the DS firmware keeps: a fresh console in the chosen language.
void OS_GetOwnerInfo(OwnerInfo *info) {
    static const char name[] = "Player";
    memset(info, 0, sizeof(*info));
    info->mLanguage = (u32) g_port_language;
    info->mFavoriteColor = 0;
    info->mBirthdayMonth = 1;
    info->mBirthdayDay = 1;
    for (int i = 0; name[i]; i++) {
        ((u16 *) info->mNickName)[i] = (u8) name[i];
        info->mNickNameLength++;
    }
}

// Each device has its own address (kept next to the save file): stations tell each other apart by it.
void OS_GetMacAddress(void *macAddress) {
    u8 mac[6];
    hapi_net_mac(mac);
    memcpy(macAddress, mac, 6);
}

// ------------------------------------------------------------------------------------------------ lock words
// One CPU and no ARM7: a lock is taken as soon as it is asked for.
static u32 s_lock_ids = 0;
void OS_InitLock(void) {}
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
s32 OS_LockByWord(u16 lockID, OSLockWord *lockp, void (*ctrlFuncp)(void)) {
    (void) lockID, (void) lockp;
    if (ctrlFuncp) {
        ctrlFuncp();
    }
    return 0;
}
s32 OS_UnlockByWord(u16 lockID, OSLockWord *lockp, void (*ctrlFuncp)(void)) {
    (void) lockID, (void) lockp;
    if (ctrlFuncp) {
        ctrlFuncp();
    }
    return 0;
}
s32 OS_LockCard(u16 lockID) { (void) lockID; return 0; }
s32 OS_UnlockCard(u16 lockID) { (void) lockID; return 0; }
s32 OS_LockCartridge(u16 lockID) { (void) lockID; return 0; }
s32 OS_UnlockCartridge(u16 lockID) { (void) lockID; return 0; }
s32 OS_UnLockCartridge(u16 lockID) { (void) lockID; return 0; }
s32 OS_TryLockCartridge(u16 lockID) { (void) lockID; return 0; }
u16 OS_ReadOwnerOfLockWord(OSLockWord *lockp) {
    (void) lockp;
    return 0;
}
