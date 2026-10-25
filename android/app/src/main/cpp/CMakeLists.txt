cmake_minimum_required(VERSION 3.22.1)
project(sm64ds_android LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_library(sm64ds_android SHARED bridge.cpp rom_probe.cpp game_math.cpp scene.cpp gles_renderer.cpp audio_core.cpp android_audio.cpp session.cpp
    "${REPO_ROOT}/src/DotVec3.c" "${REPO_ROOT}/src/CrossVec3.c"
    "${REPO_ROOT}/port/ntr/rt.cpp" "${REPO_ROOT}/port/ntr/io.cpp"
    "${REPO_ROOT}/port/ntr/bmd.cpp" "${REPO_ROOT}/port/ntr/gx.cpp" "${REPO_ROOT}/port/ntr/texture.cpp"
    "${REPO_ROOT}/port/ntr/fs.cpp" "${REPO_ROOT}/port/ntr/lz77.cpp")
target_include_directories(sm64ds_android PRIVATE "${REPO_ROOT}/port/ntr/include" "${REPO_ROOT}/include")
# Only pointer-independent seams are compiled here. Do not bypass host_abi.h
# to pull the recovered 32-bit game layouts into an ARM64 process.
target_link_options(sm64ds_android PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--gc-sections")

target_link_libraries(sm64ds_android PRIVATE GLESv3 aaudio)

set(PC_SDAT "${REPO_ROOT}/external/sm64ds-pc/port/hal/sdat")
if(NOT EXISTS "${PC_SDAT}/sseq.cpp")
    message(FATAL_ERROR "Initialize pinned PC sources: git submodule update --init --recursive")
endif()
target_sources(sm64ds_android PRIVATE pc_audio/archive.cpp pc_audio/stream.cpp
    "${PC_SDAT}/sseq.cpp" "${PC_SDAT}/mixer.cpp")
target_include_directories(sm64ds_android PRIVATE "${PC_SDAT}" "${CMAKE_CURRENT_SOURCE_DIR}")
# These upstream files also compile as standalone host tools; preserve their
# implementation and silence only the unused diagnostic helpers on Clang.
set_source_files_properties("${PC_SDAT}/sseq.cpp" "${PC_SDAT}/mixer.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-function;-Wno-unused-parameter")

# Compile the recovered boot TU through the existing host-source seam. This is
# deliberately an object gate, NOT an APK link: unresolved game dependencies must
# be implemented before an entry point can be called. Never manufacture stubs.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/native-src")
set(SAVE_SOURCES "${NATIVE_ROOT}/src/engine/save/SaveData.cpp"
    "${NATIVE_ROOT}/src/engine/save/SaveData2.cpp")
add_custom_command(OUTPUT ${SAVE_SOURCES}
    COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/android/tools/native_sources.py" --out "${NATIVE_ROOT}"
    DEPENDS "${REPO_ROOT}/android/tools/native_sources.py" "${REPO_ROOT}/port/tools/hostgen.py"
        "${REPO_ROOT}/src/engine/save/SaveData.cpp" "${REPO_ROOT}/src/engine/save/SaveData2.cpp")
add_library(sm64ds_save STATIC ${SAVE_SOURCES})
target_include_directories(sm64ds_save PRIVATE "${REPO_ROOT}/include")
target_compile_definitions(sm64ds_save PRIVATE SM64DS_PLATFORM_PC)
target_compile_options(sm64ds_save PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections
    -fno-strict-aliasing -Wno-unknown-pragmas)
target_sources(sm64ds_android PRIVATE runtime/save_store.cpp)
target_link_libraries(sm64ds_android PRIVATE sm64ds_save)

set(BOOT_HOST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/boot-host")
set(BOOT_HOST_CPP "${BOOT_HOST_ROOT}/src/game/stages/d_s_boot_methods.cpp")
add_custom_command(OUTPUT "${BOOT_HOST_CPP}"
    COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/port/tools/hostgen.py"
        --out "${BOOT_HOST_ROOT}" _ZN9dScBoot_c13InitResourcesEv
    DEPENDS "${REPO_ROOT}/port/tools/hostgen.py"
        "${REPO_ROOT}/src/game/stages/d_s_boot_methods.cpp"
    WORKING_DIRECTORY "${REPO_ROOT}")
add_library(sm64ds_boot_frontier OBJECT "${BOOT_HOST_CPP}")
target_include_directories(sm64ds_boot_frontier PRIVATE "${REPO_ROOT}/include" "${REPO_ROOT}/port/ntr/include")
target_compile_definitions(sm64ds_boot_frontier PRIVATE SM64DS_PLATFORM_PC)
target_compile_options(sm64ds_boot_frontier PRIVATE -Wno-unknown-pragmas -Wno-int-to-pointer-cast -Wno-int-to-void-pointer-cast)
add_dependencies(sm64ds_android sm64ds_boot_frontier)
