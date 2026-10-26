#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
FRONTEND_BUILD=$(mktemp -d)
trap 'rm -rf "$FRONTEND_BUILD"' EXIT
python3 android/tools/packed_sources.py --out "$FRONTEND_BUILD/generated"
python3 android/tools/compile_frontend.py --generated "$FRONTEND_BUILD/generated" --out "$FRONTEND_BUILD/objects"
g++ -std=c++17 -O2 -pthread -Wall -Wextra -Werror -Wno-unknown-pragmas \
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare \
    -fwrapv -fsigned-char -fno-strict-aliasing -fsanitize=address,undefined -g \
    -Iandroid/app/src/main/cpp -Iport/ntr/include -Iinclude \
    android/tests/frontend_core_test.cpp \
    android/app/src/main/cpp/runtime/{frontend_core,frontend_primitives,packed_memory,game_heap,native_math}.cpp \
    port/ntr/{io,gx,texture,lz77}.cpp "$FRONTEND_BUILD/objects"/*.o -ldl -o "$FRONTEND_BUILD/frontend_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" "$FRONTEND_BUILD/frontend_test"
