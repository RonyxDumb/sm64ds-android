#pragma once
#include "sdat.h"
#include <vector>
// ARM64 adapter for the pinned PC sequencer. ROM offsets remain 32-bit;
// resident host pointers never get written into 32-bit cartridge records.
void pc_archive_load(); // ntr filesystem must already be mounted
void pc_archive_close();
void pc_archive_load_image(std::vector<sd_u8> source);
int pc_start_sequence(int player, int sequence);
int pc_start_effect(int player, int archive, int entry, int bankOverride = -1);
bool pc_effect_exists(int archive, int entry);
