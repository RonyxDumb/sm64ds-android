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

g++ -std=c++17 -pthread -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Iport/ntr/include android/tests/runtime_test.cpp port/ntr/rt.cpp -o "$TEST_BUILD/runtime_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/runtime_test"
g++ -std=c++17 -pthread -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Iport/ntr/include -Iandroid/app/src/main/cpp android/tests/audio_test.cpp \
    android/app/src/main/cpp/audio_core.cpp port/ntr/fs.cpp -o "$TEST_BUILD/audio_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/audio_test" "$@"
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Iport/ntr/include android/tests/bmd_test.cpp port/ntr/{bmd,fs,gx,lz77,texture}.cpp -o "$TEST_BUILD/bmd_test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$TEST_BUILD/bmd_test"
javac -d "$TEST_BUILD/java" android/app/src/main/java/io/github/ronyxdumb/sm64ds/ControllerState.java android/tests/ControllerStateTest.java
java -cp "$TEST_BUILD/java" ControllerStateTest
