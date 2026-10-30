#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
VIDEO_BUILD=$(mktemp -d)
trap 'rm -rf "$VIDEO_BUILD"' EXIT
python3 android/tools/packed_sources.py --out "$VIDEO_BUILD/packed"
flags=(-std=c++17 -O1 -pthread -Wall -Wextra -Werror -Wno-unknown-pragmas
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare
    -fwrapv -fsigned-char -fno-strict-aliasing -ffunction-sections -fdata-sections
    -fsanitize=address,undefined -g -DSM64DS_PLATFORM_PC
    -Iandroid/app/src/main/cpp -Iport/ntr/include -I"$VIDEO_BUILD/packed/include" -Iinclude)
# Compile the exact Android relocation branch on the host; the other units
# keep their normal host platform. No production test switch or fake mapper.
g++ "${flags[@]}" -D__ANDROID__ -c port/ntr/io.cpp -o "$VIDEO_BUILD/android_io.o"
g++ "${flags[@]}" -Wl,--gc-sections android/tests/video_registers_test.cpp \
    "$VIDEO_BUILD/packed/GX_Init.cpp" \
    "$VIDEO_BUILD/packed/func_ov007_020c1404.cpp" \
    android/app/src/main/cpp/runtime/{packed_memory,game_heap}.cpp \
    port/ntr/{gx,texture,ppu}.cpp "$VIDEO_BUILD/android_io.o" -ldl \
    -o "$VIDEO_BUILD/video_registers_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
    "$VIDEO_BUILD/video_registers_test"
for unit in G3i cstd; do
    g++ "${flags[@]}" -c "$VIDEO_BUILD/packed/$unit.cpp" -o "$VIDEO_BUILD/$unit.o"
done
g++ -Iinclude "${flags[@]}" -Wl,--gc-sections android/tests/math_units_test.cpp \
    android/app/src/main/cpp/runtime/{native_math,packed_memory,game_heap}.cpp \
    port/ntr/{gx,texture}.cpp "$VIDEO_BUILD"/{android_io,G3i,cstd}.o -ldl -o "$VIDEO_BUILD/math_units_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
    "$VIDEO_BUILD/math_units_test"
