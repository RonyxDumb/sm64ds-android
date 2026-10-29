#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
MIGRATION_BUILD=$(mktemp -d)
trap 'rm -rf "$MIGRATION_BUILD"' EXIT
python3 - "$MIGRATION_BUILD/expressions.cpp" <<'PY'
from pathlib import Path
import sys
sys.path.insert(0, 'android/tools')
from arm64_migration import transform
Path(sys.argv[1]).write_text(transform(
    Path('android/tests/migration_expression_fixture.cpp').read_text()))
PY
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iandroid/app/src/main/cpp -Iport/ntr/include -Iinclude \
    android/tests/native_abi_test.cpp \
    "$MIGRATION_BUILD/expressions.cpp" \
    android/app/src/main/cpp/runtime/{packed_memory,game_heap,native_math}.cpp \
    port/ntr/{io,gx,texture,lz77}.cpp -ldl -o "$MIGRATION_BUILD/native_abi_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
    "$MIGRATION_BUILD/native_abi_test"
