#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
PLATFORM_TEST_BUILD=$(mktemp -d)
trap 'rm -rf "$PLATFORM_TEST_BUILD"' EXIT
python3 - "$PLATFORM_TEST_BUILD" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0,'android/tools')
import arm64_platform as platform,arm64_soft_math as math
out=Path(sys.argv[1]);(out/'math').mkdir();(out/'services').mkdir()
for name,path in math.PATHS.items():(out/'math'/(name+'.cpp')).write_text(math.generate(path))
for path in platform.BODIES:(out/'services'/(Path(path).stem+'.cpp')).write_text(platform.generate(path))
PY
PLATFORM_TEST_FLAGS=(-std=c++17 -O1 -g -pthread -fno-omit-frame-pointer -Iandroid/app/src/main/cpp -Iport/ntr/include)
PLATFORM_TEST_RUNTIME=(android/app/src/main/cpp/runtime/{packed_memory,game_heap}.cpp port/ntr/{io,gx,texture,lz77}.cpp)
g++ "${PLATFORM_TEST_FLAGS[@]}" -fsanitize=address,undefined android/tests/soft_math_test.cpp "$PLATFORM_TEST_BUILD"/math/*.cpp "${PLATFORM_TEST_RUNTIME[@]}" -ldl -o "$PLATFORM_TEST_BUILD/math_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS=halt_on_error=1 "$PLATFORM_TEST_BUILD/math_test"
PLATFORM_TEST_UNITS=(Copy32Bytes Copy36Bytes Copy48Bytes Copy48BytesFixed CpuCopy8 CpuSet CpuFastSet BitUnPack GetCRC16 Sqrt DecompressLZ16 LZ77UnCompReadNormalWrite8bit RLUnCompReadNormalWrite8bit LZ77UnCompReadByCallbackWrite16bit RLUnCompReadByCallbackWrite16bit HuffUnCompReadByCallback Matrix3x3_LoadIdentity Matrix3x3_SetRotationZ func_02052514 func_020527e8 func_0205a74c func_02057020 func_02057078)
PLATFORM_TEST_SOURCES=()
for unit in "${PLATFORM_TEST_UNITS[@]}"; do PLATFORM_TEST_SOURCES+=("$PLATFORM_TEST_BUILD/services/$unit.cpp"); done
g++ "${PLATFORM_TEST_FLAGS[@]}" -fsanitize=address,undefined android/tests/platform_primitives_test.cpp "${PLATFORM_TEST_SOURCES[@]}" "${PLATFORM_TEST_RUNTIME[@]}" port/ntr/rt.cpp -ldl -o "$PLATFORM_TEST_BUILD/services_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS=halt_on_error=1 "$PLATFORM_TEST_BUILD/services_test"
# Actual machine-stack switches are checked with UBSan. ASan needs explicit
# fiber instrumentation before it can check these alternate stacks.
g++ "${PLATFORM_TEST_FLAGS[@]}" -fsanitize=undefined android/tests/platform_context_test.cpp "$PLATFORM_TEST_BUILD/services/ARMSaveContext.cpp" "$PLATFORM_TEST_BUILD/services/ARMRestoreContext.cpp" android/app/src/main/cpp/platform/native_context.cpp "${PLATFORM_TEST_RUNTIME[@]}" port/ntr/rt.cpp -ldl -o "$PLATFORM_TEST_BUILD/context_test"
UBSAN_OPTIONS=halt_on_error=1 "$PLATFORM_TEST_BUILD/context_test"
