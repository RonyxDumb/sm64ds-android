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
target_link_options(sm64ds_android PRIVATE "-Wl,-z,max-page-size=16384")

target_link_libraries(sm64ds_android PRIVATE GLESv3 aaudio)
