#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
FRONTEND_BUILD=$(mktemp -d)
trap 'rm -rf "$FRONTEND_BUILD"' EXIT
python3 android/tools/packed_sources.py --out "$FRONTEND_BUILD/generated"
for source in "$FRONTEND_BUILD/generated"/*.cpp; do
    g++ -std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas \
        -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare \
        -fwrapv -fsigned-char -fno-strict-aliasing -fsanitize=address,undefined -g \
        -Iandroid/app/src/main/cpp -Iport/ntr/include -I"$FRONTEND_BUILD/generated/include" -Iinclude \
        -c "$source" -o "$FRONTEND_BUILD/$(basename "$source").o"
done
g++ -std=c++17 -pthread -Wall -Wextra -Werror -Wno-unknown-pragmas \
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare \
    -fwrapv -fsigned-char -fno-strict-aliasing -fsanitize=address,undefined -g \
    -Iandroid/app/src/main/cpp -Iport/ntr/include -Iinclude \
    android/tests/frontend_core_test.cpp \
    android/app/src/main/cpp/runtime/{frontend_core,frontend_primitives,packed_memory,game_heap,native_math}.cpp \
    port/ntr/{io,gx,texture,lz77}.cpp "$FRONTEND_BUILD"/*.o -ldl -o "$FRONTEND_BUILD/frontend_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$FRONTEND_BUILD/frontend_test"
