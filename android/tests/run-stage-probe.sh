#!/usr/bin/env bash
# Castle frontier probe: the run-game-tests.sh build, with every object cached
# by content hash, optionally without sanitizers (SM64DS_PROBE_FAST=1) so the
# ~2000-frame opening replays several times faster while hunting the next
# unlinked dependency. Verification runs stay on run-game-tests.sh (ASan/UBSan).
#
# Usage: SM64DS_GAME_BUILD=DIR android/tests/run-stage-probe.sh <rom> [probe env...]
#   e.g. SM64DS_STAGE_CHARACTER=3 SM64DS_STAGE_FRAMES=4000 ... run-stage-probe.sh rom.nds
set -euo pipefail
cd "$(dirname "$0")/../.."
G=${SM64DS_GAME_BUILD:?set SM64DS_GAME_BUILD to a persistent build directory}
FAST=${SM64DS_PROBE_FAST:-0}
SAN=(-fsanitize=address,undefined)
NOSAN=()
if [[ $FAST == 1 ]]; then SAN=(); NOSAN=(--no-sanitize); G=$G/fast; fi
mkdir -p "$G"
python3 android/tools/native_sources.py --out "$G/generated" >/dev/null
python3 android/tools/verify_stage_sources.py --ranges-header "$G/generated/stage_code_ranges.h"
python3 android/tools/packed_sources.py --out "$G/packed" >/dev/null
python3 android/tools/packed_sources.py --out "$G/stage" --manifest android/tools/stage_manifest.json >/dev/null
python3 android/tools/packed_sources.py --out "$G/minigame" --manifest android/tools/minigame_manifest.json >/dev/null
for d in packed stage minigame; do
    o=objects; [[ $d != packed ]] && o=$d-objects
    python3 android/tools/compile_frontend.py --generated "$G/$d" --out "$G/$o" --incremental "${NOSAN[@]}"
done
FLAGS=(-std=c++17 -O1 -Wno-maybe-uninitialized -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-int-to-pointer-cast
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -fno-strict-aliasing
    -ffunction-sections -fdata-sections "${SAN[@]}" -g -pthread
    -DSM64DS_PLATFORM_PC -Iinclude -Iport/ntr/include -Iandroid/app/src/main/cpp -I"$G/generated"
    -Iexternal/sm64ds-pc/port/hal/sdat)
R=android/app/src/main/cpp/runtime
SRCS=(android/tests/game_program_test.cpp android/tests/minigame_gameplay_test.cpp android/tests/minigame_oam_test.cpp
      android/tests/minigame_state_test.cpp android/tests/wipe_test.cpp)
for n in game_program cartridge game_input resources game_heap save_store stage_flow stage_runtime stage_player \
         stage_assets minigame_catalog minigame_progress minigame_controls minigame_menu_session minigame_art \
         minigame_runtime minigame_gameplay_runtime minigame_scene_constructors minigame_shared wipe_runtime \
         title_files native_math frontend_core frontend_primitives packed_memory packed_fs packed_audio \
         actor_dispatch collision_dispatch; do SRCS+=("$R/$n.cpp"); done
SRCS+=("$G/generated"/runtime/*.cpp "$G/generated/src/SharedFilePtr.cpp" "$G/generated/src/engine/save"/*.cpp
       android/app/src/main/cpp/pc_audio/archive.cpp android/app/src/main/cpp/pc_audio/stream.cpp
       android/app/src/main/cpp/audio_core.cpp external/sm64ds-pc/port/hal/sdat/sseq.cpp
       external/sm64ds-pc/port/hal/sdat/mixer.cpp port/ntr/io.cpp port/ntr/gx.cpp port/ntr/texture.cpp
       port/ntr/ppu.cpp port/ntr/fs.cpp port/ntr/lz77.cpp port/ntr/rt.cpp)
C_SRCS=(src/GetOwnerLanguage.c src/SetBg0Offset.c src/SetSubBg0Offset.c src/SetSubBg1Offset.c src/SetSubBg2Offset.c
        src/SetNumPlayers.c src/SublevelToLevel.c src/unnamed/arm9/0203/func_020308d0.c src/DecIfAbove0_Byte.c
        src/GetControllerMode.c src/GetPlayerFlagByte.c src/GetSoundMode.c src/unnamed/arm9/0202/func_020233f4.c
        src/ui/messages/func_0201fe08.c src/Vec3_MulScalarInPlace.c src/unnamed/arm9/0201/func_02013568.c
        src/unnamed/arm9/0201/func_02013580.c src/unnamed/arm9/0201/func_020135d0.c
        src/unnamed/arm9/0201/func_0201361c.c src/unnamed/arm9/0201/func_02013638.c
        src/unnamed/arm9/0203/func_0203bb60.c src/unnamed/arm9/0203/func_0203bc7c.c)
mkdir -p "$G/link-objects"
HDRS=$( (find include port/ntr/include android/app/src/main/cpp "$G/generated" -name '*.h' -o -name '*.inc' |
         sort | xargs cat; echo "${FLAGS[*]}") | sha256sum | cut -c1-16)
OBJS=()
compile() { # source object compiler flags...
    local s=$1 o=$2; shift 2
    [[ -f $o ]] || { "$@" -c "$s" -o "$o.tmp" && mv "$o.tmp" "$o"; }
}
pids=()
for s in "${SRCS[@]}" "${C_SRCS[@]}"; do
    h=$( (cat "$s"; echo "$HDRS") | sha256sum | cut -c1-16)
    o=$G/link-objects/$(echo "$s" | tr / _).$h.o
    OBJS+=("$o")
    if [[ $s == *.c ]]; then
        compile "$s" "$o" gcc -std=c11 -Wno-unknown-pragmas "${SAN[@]}" -g -Iinclude &
    else
        compile "$s" "$o" g++ "${FLAGS[@]}" &
    fi
    pids+=($!)
    if (( ${#pids[@]} >= 3 )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
done
for p in "${pids[@]}"; do wait "$p"; done
g++ "${FLAGS[@]}" -Wl,--gc-sections "${OBJS[@]}" "$G/objects"/*.o "$G/stage-objects"/*.o \
    "$G/minigame-objects"/*.o -ldl -o "$G/game_program_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" "$G/game_program_test" "$@"
