// Crash reports: a fatal signal writes the registers, a backtrace and the library's load address to crash.txt in the
// app's files; the Java side shows it at the next start so that it can be sent to the developer.
#include "port.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

static char s_path[512];
static int s_log_fd = -1;

static void put(int fd, const char *s) {
    ssize_t r = write(fd, s, strlen(s));
    (void) r;
}

static void put_hex(int fd, const char *label, uintptr_t v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%08lx\n", label, (unsigned long) v);
    put(fd, buf);
}

typedef struct Trace {
    int fd, depth;
} Trace;

static _Unwind_Reason_Code trace_one(struct _Unwind_Context *ctx, void *arg) {
    Trace *t = arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc) {
        Dl_info info;
        char buf[256];
        if (dladdr((void *) pc, &info) && info.dli_fname) {
            snprintf(buf, sizeof(buf), "  #%02d pc %08lx  %s+%lx (%s)\n", t->depth, (unsigned long) pc,
                     strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname,
                     (unsigned long) (pc - (uintptr_t) info.dli_fbase), info.dli_sname ? info.dli_sname : "?");
        } else {
            snprintf(buf, sizeof(buf), "  #%02d pc %08lx\n", t->depth, (unsigned long) pc);
        }
        put(t->fd, buf);
    }
    return ++t->depth < 40 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void handler(int sig, siginfo_t *info, void *uc_) {
    int fd = open(s_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "signal %d code %d fault address %08lx\n", sig, info->si_code,
                 (unsigned long) (uintptr_t) info->si_addr);
        put(fd, buf);
        Dl_info self;
        if (dladdr((void *) handler, &self)) {
            put_hex(fd, "libmkds.so loaded at ", (uintptr_t) self.dli_fbase);
        }
#if defined(__arm__)
        ucontext_t *uc = uc_;
        put_hex(fd, "pc ", uc->uc_mcontext.arm_pc);
        put_hex(fd, "lr ", uc->uc_mcontext.arm_lr);
        put_hex(fd, "sp ", uc->uc_mcontext.arm_sp);
        Dl_info at;
        if (dladdr((void *) uc->uc_mcontext.arm_pc, &at) && at.dli_sname) {
            put(fd, "in ");
            put(fd, at.dli_sname);
            put(fd, "\n");
        }
        if (dladdr((void *) uc->uc_mcontext.arm_lr, &at) && at.dli_sname) {
            put(fd, "called from ");
            put(fd, at.dli_sname);
            put(fd, "\n");
        }
#elif defined(__aarch64__)
        // The game code (the guest image, 32-bit pointers at 0x10000000) has no unwind tables: the backtrace stops
        // at the signal frame, so the registers say where it happened (symbols: nm guest.elf).
        ucontext_t *uc = uc_;
        put_hex(fd, "pc ", uc->uc_mcontext.pc);
        put_hex(fd, "lr ", uc->uc_mcontext.regs[30]);
        put_hex(fd, "sp ", uc->uc_mcontext.sp);
        for (int i = 0; i < 4; i++) {
            char label[8] = {'x', (char) ('0' + i), ' ', 0};
            put_hex(fd, label, uc->uc_mcontext.regs[i]);
        }
        if (uc->uc_mcontext.pc >= 0x10000000 && uc->uc_mcontext.pc < 0x20000000) put(fd, "in the game image\n");
        // A few words of the guest stack: return addresses of the callers.
        uint64_t sp = uc->uc_mcontext.sp;
        if (sp >= 0x50000000 && sp < 0x58000000) {
            put(fd, "stack:");
            for (int i = 0; i < 24; i++) {
                char w[16];
                snprintf(w, sizeof(w), " %08x", ((const uint32_t *) (uintptr_t) sp)[i]);
                put(fd, w);
            }
            put(fd, "\n");
        }
#else
        (void) uc_;
#endif
        put(fd, "backtrace:\n");
        Trace t = {fd, 0};
        _Unwind_Backtrace(trace_one, &t);
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

// append: the game process (arm64) shares the app's log.
void android_crash_init(const char *dir, bool append) {
    snprintf(s_path, sizeof(s_path), "%s/crash.txt", dir);
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/log.txt", dir);
    s_log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | (append ? 0 : O_TRUNC), 0644);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

// Also keeps the log in log.txt (port_log).
void android_log_line(const char *line) {
    if (s_log_fd >= 0) {
        put(s_log_fd, line);
        put(s_log_fd, "\n");
    }
}
