#pragma once

namespace sm64ds::diagnostics {
// Opened once before the render thread starts. The fatal signal handler
// writes only with async-signal-safe syscalls; it does not unwind C++.
bool install(const char *absolutePath);
void note(const char *category, const char *detail);
}
