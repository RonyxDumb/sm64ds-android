#include "crash_diagnostics.h"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

namespace sm64ds::diagnostics {
namespace {
constexpr int watched[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE};
struct sigaction previous[sizeof(watched) / sizeof(watched[0])]{};
volatile sig_atomic_t logFd = -1;
volatile sig_atomic_t handling = 0;
bool installed = false;

size_t put_text(char *out, size_t at, const char *text) {
    if (!text) return at;
    while (*text && at < 240) out[at++] = *text++;
    return at;
}
size_t put_dec(char *out, size_t at, uint64_t number) {
    char digits[24];
    unsigned count = 0;
    do {
        digits[count++] = static_cast<char>('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof digits);
    while (count && at < 240) out[at++] = digits[--count];
    return at;
}
size_t put_hex(char *out, size_t at, uintptr_t number) {
    constexpr char digits[] = "0123456789abcdef";
    at = put_text(out, at, "0x");
    bool found = false;
    for (int shift = int(sizeof number * 8) - 4; shift >= 0; shift -= 4) {
        const unsigned nibble = (number >> shift) & 15u;
        if (nibble || found || shift == 0) {
            found = true;
            if (at < 240) out[at++] = digits[nibble];
        }
    }
    return at;
}
void signal_handler(int signal, siginfo_t *info, void *opaque) {
    if (handling) _exit(128 + signal);
    handling = 1;
    const int fd = logFd;
    if (fd >= 0) {
        char line[256];
        size_t n = put_text(line, 0, "[FATAL-NATIVE] signal=");
        n = put_dec(line, n, static_cast<unsigned>(signal));
        n = put_text(line, n, " tid=");
        n = put_dec(line, n, static_cast<unsigned>(syscall(SYS_gettid)));
        if (info) {
            n = put_text(line, n, " fault=");
            n = put_hex(line, n, reinterpret_cast<uintptr_t>(info->si_addr));
        }
#if defined(__aarch64__)
        if (opaque) {
            const auto *state = static_cast<const ucontext_t *>(opaque);
            n = put_text(line, n, " pc=");
            n = put_hex(line, n, static_cast<uintptr_t>(state->uc_mcontext.pc));
        }
#else
        (void)opaque;
#endif
        line[n++] = '\n';
        (void)write(fd, line, n);
        // fsync() is async-signal-safe and helps preserve the report when
        // Android terminates the crashing process immediately afterwards.
        (void)fsync(fd);
    }
    // Restore the system's previous handler so Android can still produce
    // its normal native tombstone. Never continue after a fatal signal.
    for (unsigned i = 0; i < sizeof watched / sizeof watched[0]; ++i)
        if (watched[i] == signal) {
            (void)sigaction(signal, &previous[i], nullptr);
            break;
        }
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, signal);
    (void)sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    (void)syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), signal);
    _exit(128 + signal);
}
} // namespace

bool install(const char *absolutePath) {
    if (installed) return true;
    if (!absolutePath || absolutePath[0] != '/') return false;
    const int fd = open(absolutePath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    logFd = fd;
    for (unsigned i = 0; i < sizeof watched / sizeof watched[0]; ++i) {
        struct sigaction action{};
        action.sa_sigaction = signal_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        if (sigaction(watched[i], &action, &previous[i]) != 0) {
            // Never claim success with a partially installed signal handler.
            for (unsigned j = 0; j < i; ++j)
                (void)sigaction(watched[j], &previous[j], nullptr);
            logFd = -1;
            close(fd);
            return false;
        }
    }
    installed = true;
    note("NATIVE", "signal handlers installed (SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE)");
    return true;
}

void note(const char *category, const char *detail) {
    const int fd = logFd;
    if (fd < 0) return;
    char line[256];
    size_t n = put_text(line, 0, "[");
    n = put_text(line, n, category ? category : "NATIVE");
    n = put_text(line, n, "] ");
    n = put_text(line, n, detail ? detail : "(no detail)");
    line[n++] = '\n';
    (void)write(fd, line, n);
}
} // namespace sm64ds::diagnostics
