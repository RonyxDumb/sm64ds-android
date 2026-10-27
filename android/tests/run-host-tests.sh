#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
TEST_BUILD=$(mktemp -d)
trap 'rm -rf "$TEST_BUILD"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -c src/DotVec3.c -o "$TEST_BUILD/dot.o"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -c src/CrossVec3.c -o "$TEST_BUILD/cross.o"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Iport/ntr/include -Iandroid/app/src/main/cpp -Iinclude \
    android/tests/rom_probe_test.cpp android/app/src/main/cpp/rom_probe.cpp \
    port/ntr/fs.cpp port/ntr/lz77.cpp android/app/src/main/cpp/game_math.cpp \
    "$TEST_BUILD/dot.o" "$TEST_BUILD/cross.o" -o "$TEST_BUILD/rom_probe_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/rom_probe_test" "$@"
javac -d "$TEST_BUILD/java" android/app/src/main/java/io/github/ronyxdumb/sm64ds/RomImport.java android/tests/RomImportTest.java
java -cp "$TEST_BUILD/java" RomImportTest
javac -d "$TEST_BUILD/java" android/app/src/main/java/io/github/ronyxdumb/sm64ds/CrashLogStore.java android/tests/CrashLogStoreTest.java
java -cp "$TEST_BUILD/java" CrashLogStoreTest

g++ -std=c++17 -pthread -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Iport/ntr/include android/tests/runtime_test.cpp port/ntr/rt.cpp -o "$TEST_BUILD/runtime_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/runtime_test"
g++ -std=c++17 -pthread -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Iport/ntr/include -Iandroid/app/src/main/cpp android/tests/audio_test.cpp \
    android/app/src/main/cpp/audio_core.cpp port/ntr/fs.cpp -o "$TEST_BUILD/audio_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/audio_test" "$@"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Iport/ntr/include android/tests/bmd_test.cpp port/ntr/{bmd,fs,gx,io,lz77,texture}.cpp -o "$TEST_BUILD/bmd_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/bmd_test"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Iport/ntr/include android/tests/ppu_display_test.cpp \
    port/ntr/{ppu,io,gx,texture}.cpp -o "$TEST_BUILD/ppu_display_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" "$TEST_BUILD/ppu_display_test"
javac -d "$TEST_BUILD/java" android/app/src/main/java/io/github/ronyxdumb/sm64ds/ControllerState.java android/tests/ControllerStateTest.java
java -cp "$TEST_BUILD/java" ControllerStateTest
javac -d "$TEST_BUILD/java" android/app/src/main/java/io/github/ronyxdumb/sm64ds/{ControllerState,GamepadState}.java android/tests/GamepadStateTest.java
java -cp "$TEST_BUILD/java" GamepadStateTest
gcc -std=c11 -Wall -Wextra -Werror -Wno-unknown-pragmas -fsanitize=address,undefined \
    -Iinclude -c src/unnamed/arm9/0203/func_0203bb60.c -o "$TEST_BUILD/touch.o"
gcc -std=c11 -Wall -Wextra -Werror -Wno-unknown-pragmas -fsanitize=address,undefined \
    -Iinclude -c src/unnamed/arm9/0203/func_0203bc7c.c -o "$TEST_BUILD/keys.o"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    -Iandroid/app/src/main/cpp android/tests/input_test.cpp \
    android/app/src/main/cpp/runtime/game_input.cpp "$TEST_BUILD/touch.o" "$TEST_BUILD/keys.o" -o "$TEST_BUILD/input_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/input_test"

python3 android/tests/hostgen_test.py
python3 android/tests/native_sources_test.py
python3 android/tools/native_sources.py --out "$TEST_BUILD/native-src"
g++ -std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-unused-variable \
    -Wno-unused-parameter -Wno-sign-compare -fno-strict-aliasing -ffunction-sections \
    -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined -g \
    -DSM64DS_PLATFORM_PC -Iinclude -Iandroid/app/src/main/cpp android/tests/save_test.cpp \
    android/app/src/main/cpp/runtime/save_store.cpp \
    "$TEST_BUILD/native-src/src/engine/save/SaveData.cpp" \
    "$TEST_BUILD/native-src/src/engine/save/SaveData2.cpp" -o "$TEST_BUILD/save_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/save_test"
g++ -std=c++17 -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -g \
    -DSM64DS_PLATFORM_PC -Iinclude -Iport/ntr/include -Iandroid/app/src/main/cpp \
    android/tests/resources_test.cpp android/app/src/main/cpp/runtime/resources.cpp \
    android/app/src/main/cpp/runtime/game_heap.cpp port/ntr/fs.cpp port/ntr/lz77.cpp \
    "$TEST_BUILD/native-src/src/SharedFilePtr.cpp" -o "$TEST_BUILD/resources_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/resources_test" "$@"
g++ -std=c++17 -pthread -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -O1 -g \
    -fsanitize=address,undefined -Iport/ntr/include -Iandroid/app/src/main/cpp \
    -Iexternal/sm64ds-pc/port/hal/sdat android/tests/music_test.cpp \
    android/app/src/main/cpp/pc_audio/{archive,stream}.cpp android/app/src/main/cpp/audio_core.cpp \
    port/ntr/fs.cpp external/sm64ds-pc/port/hal/sdat/{sseq,mixer}.cpp -o "$TEST_BUILD/music_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/music_test" "$@"
