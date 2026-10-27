#include "crash_diagnostics.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    char path[] = "/tmp/sm64ds-native-crash-XXXXXX";
    const int temporary = mkstemp(path);
    assert(temporary >= 0);
    close(temporary);

    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        // A deliberate SIGSEGV is scoped to this forked child. We must
        // preserve the platform crash handler rather than swallow the fault.
        struct rlimit noCore{};
        setrlimit(RLIMIT_CORE, &noCore);
        if (!sm64ds::diagnostics::install(path)) _exit(95);
        sm64ds::diagnostics::note("TEST", "last native renderer action");
        raise(SIGSEGV);
        _exit(96);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    std::ifstream report(path);
    const std::string contents{std::istreambuf_iterator<char>(report),
                               std::istreambuf_iterator<char>()};
    assert(contents.find("last native renderer action") != std::string::npos);
    assert(contents.find("[FATAL-NATIVE] signal=11") != std::string::npos);
    assert(contents.find("fault=") != std::string::npos);
    std::remove(path);
    std::puts("Native fatal-signal file persistence and re-raise passed.");
}
