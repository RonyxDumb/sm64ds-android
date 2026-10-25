#include "ntr/rt.h"

#include "ntr/mmio.h"

#include <cstdio>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ntr {
namespace {

constexpr uint32_t REG_IME = 0x04000208;
constexpr uint32_t REG_IE = 0x04000210;
constexpr uint32_t REG_IF = 0x04000214;
constexpr uint32_t REG_VCOUNT = 0x04000006;

inline Reg<uint32_t> reg32(uint32_t a) { return Reg<uint32_t>(a); }
inline Reg<uint16_t> reg16(uint32_t a) { return Reg<uint16_t>(a); }

struct Runtime {
    void *host_fiber = nullptr;
    void *game_fiber = nullptr;
    void (*entry)() = nullptr;
    FrameHook hook = nullptr;
    uint64_t frame = 0;
    uint64_t max_frames = 0;
    bool game_done = false;
    bool stop = false;
    uint32_t cpsr_i = 0;     // 0x80 when interrupts are masked
};

Runtime g;
#if !defined(_WIN32)
struct ThreadRuntime {
    std::mutex mutex;
    std::condition_variable cv;
    bool yielded=false, resume=false;
    std::exception_ptr failure;
};
ThreadRuntime threaded;
std::mutex owner;
thread_local bool on_game_thread=false;
struct RuntimeStopped {};
#endif

#if defined(_WIN32)
void CALLBACK game_trampoline(void *) {
    g.entry();
    g.game_done = true;
    // The game returned. Hand control back for good.
    for (;;) SwitchToFiber(g.host_fiber);
}
#endif

// Advance the scanline counter across a frame. Anything spinning on VCOUNT --
// the decomp does, in func_02013f4c -- needs this to actually move.
void run_scanlines() {
    for (uint16_t line = 0; line < 263; ++line) reg16(REG_VCOUNT) = line;
}

}  // namespace

void rt_vblank_wait() {
#if defined(_WIN32)
    if (!g.game_fiber) return;   // not running under rt_run; nothing to yield to
    reg32(REG_IF) |= IRQ_VBLANK;
    SwitchToFiber(g.host_fiber);
#else
    if(!on_game_thread)return;
    std::unique_lock<std::mutex> lock(threaded.mutex);
    reg32(REG_IF) |= IRQ_VBLANK;
    threaded.yielded=true;threaded.cv.notify_one();
    threaded.cv.wait(lock,[]{return threaded.resume;});
    threaded.resume=false;
    if(g.stop)throw RuntimeStopped{};
#endif
}

uint64_t rt_frame() { return g.frame; }

uint32_t rt_irq_disable() {
    const uint32_t prev = g.cpsr_i;
    g.cpsr_i = 0x80;
    return prev;
}

uint32_t rt_irq_enable() {
    const uint32_t prev = g.cpsr_i;
    g.cpsr_i = 0;
    return prev;
}

uint32_t rt_irq_restore(uint32_t prev) {
    const uint32_t was = g.cpsr_i;
    g.cpsr_i = prev & 0x80;
    return was;
}

bool rt_irq_masked() { return g.cpsr_i != 0; }

uint64_t rt_run(void (*game)(), FrameHook hook, uint64_t max_frames) {
#if !defined(_WIN32)
    // Cooperative handoff: only the host OR the game executes between
    // VBlanks. No ucontext/stack pointer tricks and no guest ARM execution.
    std::unique_lock<std::mutex> ownership(owner,std::try_to_lock);
    if(!ownership || !game || !io_init())return 0;
    g=Runtime{};g.hook=hook;g.max_frames=max_frames;
    threaded.yielded=false;threaded.resume=false;threaded.failure=nullptr;
    reg32(REG_IME)=1;reg32(REG_IE)=IRQ_VBLANK;
    std::thread game_thread([game]{
        on_game_thread=true;
        try{game();}catch(const RuntimeStopped&){}
        catch(...){threaded.failure=std::current_exception();}
        on_game_thread=false;
        std::lock_guard<std::mutex> lock(threaded.mutex);
        g.game_done=true;threaded.cv.notify_one();
    });
    std::exception_ptr host_failure;
    try{
        for(;;){
            std::unique_lock<std::mutex> lock(threaded.mutex);
            threaded.cv.wait(lock,[]{return threaded.yielded||g.game_done;});
            if(g.game_done)break;
            threaded.yielded=false;
            // The game is parked in rt_vblank_wait until this handoff ends.
            lock.unlock();run_scanlines();++g.frame;
            if(g.hook && !g.hook(g.frame))g.stop=true;
            if(g.max_frames && g.frame>=g.max_frames)g.stop=true;
            reg32(REG_IF)&=~IRQ_VBLANK;
            lock.lock();threaded.resume=true;threaded.cv.notify_one();
        }
    }catch(...){
        host_failure=std::current_exception();
        std::lock_guard<std::mutex> lock(threaded.mutex);
        g.stop=true;threaded.resume=true;threaded.cv.notify_one();
    }
    game_thread.join();
    if(host_failure)std::rethrow_exception(host_failure);
    if(threaded.failure)std::rethrow_exception(threaded.failure);
    return g.frame;
#else
    if (!io_init()) {
        std::fprintf(stderr, "rt_run: io_init failed\n");
        return 0;
    }

    g = Runtime{};
    g.entry = game;
    g.hook = hook;
    g.max_frames = max_frames;

    g.host_fiber = ConvertThreadToFiber(nullptr);
    if (!g.host_fiber) {
        std::fprintf(stderr, "rt_run: ConvertThreadToFiber failed\n");
        return 0;
    }
    g.game_fiber = CreateFiber(256 * 1024, game_trampoline, nullptr);
    if (!g.game_fiber) {
        std::fprintf(stderr, "rt_run: CreateFiber failed\n");
        return 0;
    }

    // The DS comes up with interrupts enabled and IME set by the CRT0.
    reg32(REG_IME) = 1;
    reg32(REG_IE) = IRQ_VBLANK;

    while (!g.game_done && !g.stop) {
        SwitchToFiber(g.game_fiber);          // run until the game blocks
        if (g.game_done) break;

        run_scanlines();
        ++g.frame;
        if (g.hook && !g.hook(g.frame)) g.stop = true;
        if (g.max_frames && g.frame >= g.max_frames) g.stop = true;

        // The handler is what clears the pending flag on real hardware.
        reg32(REG_IF) &= ~IRQ_VBLANK;
    }

    DeleteFiber(g.game_fiber);
    g.game_fiber = nullptr;
    ConvertFiberToThread();
    return g.frame;
#endif
}

}  // namespace ntr
