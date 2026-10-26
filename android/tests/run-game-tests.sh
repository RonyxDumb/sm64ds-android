#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
GAME_BUILD=$(mktemp -d)
trap 'rm -rf "$GAME_BUILD"' EXIT
python3 android/tools/native_sources.py --out "$GAME_BUILD/generated"
for source in src/GetOwnerLanguage.c src/SetBg0Offset.c src/SetSubBg{0,1,2}Offset.c \
    src/unnamed/arm9/0202/func_020233f4.c src/ui/messages/func_0201fe08.c \
    src/unnamed/arm9/0203/func_0203b{b60,c7c}.c; do
    gcc -std=c11 -Wno-unknown-pragmas -fsanitize=address,undefined -g -Iinclude -c "$source" -o "$GAME_BUILD/$(basename "$source").o"
done
g++ -std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-int-to-pointer-cast \
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -fno-strict-aliasing \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined -g \
    -DSM64DS_PLATFORM_PC -Iinclude -Iport/ntr/include -Iandroid/app/src/main/cpp -I"$GAME_BUILD/generated" \
    android/tests/game_program_test.cpp android/app/src/main/cpp/runtime/{game_program,cartridge,game_input,resources,game_heap,save_store}.cpp \
    "$GAME_BUILD/generated"/runtime/*.cpp "$GAME_BUILD/generated/src/SharedFilePtr.cpp" \
    "$GAME_BUILD/generated/src/engine/save"/*.cpp \
    port/ntr/{io,gx,texture,ppu,fs,lz77}.cpp "$GAME_BUILD"/*.o -o "$GAME_BUILD/game_program_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$GAME_BUILD/game_program_test" "$@"
