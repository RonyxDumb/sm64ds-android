#pragma once
#include <string>
namespace sm64ds {
// Owns the ntr filesystem during this call; callers must serialize access.
// Returns a diagnostic or throws std::runtime_error. Does not execute ARM code.
std::string probe_rom(const char *path);
}
