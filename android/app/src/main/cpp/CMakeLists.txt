cmake_minimum_required(VERSION 3.22.1)
project(sm64ds_android LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_library(sm64ds_android SHARED bridge.cpp crash_diagnostics.cpp rom_probe.cpp game_math.cpp screen_renderer.cpp audio_core.cpp android_audio.cpp session.cpp
    "${REPO_ROOT}/src/DotVec3.c" "${REPO_ROOT}/src/CrossVec3.c"
    # Portable original game maths: compile recovered src/ bodies directly for ARM64.
    # No DS register accesses, asm primitives or fixed-width host pointers here.
    "${REPO_ROOT}/src/Vec3_Add.c" "${REPO_ROOT}/src/Vec3_Sub.c"
    "${REPO_ROOT}/src/Vec3_MulScalar.c" "${REPO_ROOT}/src/Vec3_MulScalarInPlace.c"
    "${REPO_ROOT}/src/Vec3_Equal.c" "${REPO_ROOT}/src/Vec3_Asr.c"
    "${REPO_ROOT}/src/Vec3_AsrInPlace.c" "${REPO_ROOT}/src/Vec3_Lsl.c"
    "${REPO_ROOT}/src/Vec3_LslInPlace.c" "${REPO_ROOT}/src/Vec2_Sub.c"
    "${REPO_ROOT}/src/SubVec3.c" "${REPO_ROOT}/src/AngleDiff.c"
    "${REPO_ROOT}/src/MulVec3Mat3x3.c"
    "${REPO_ROOT}/src/Matrix4x3_FromTranslation.c"
    "${REPO_ROOT}/src/Vec3_RotateYAndTranslate.c"
    "${REPO_ROOT}/port/ntr/rt.cpp" "${REPO_ROOT}/port/ntr/io.cpp"
    "${REPO_ROOT}/port/ntr/bmd.cpp" "${REPO_ROOT}/port/ntr/gx.cpp" "${REPO_ROOT}/port/ntr/texture.cpp"
    "${REPO_ROOT}/port/ntr/fs.cpp" "${REPO_ROOT}/port/ntr/lz77.cpp")
target_include_directories(sm64ds_android PRIVATE "${REPO_ROOT}/port/ntr/include" "${REPO_ROOT}/include")
# Matching sources keep their cartridge ABI. Native-owned seams and the checked
# handle projection below are the only ways they enter the ARM64 process.
target_link_options(sm64ds_android PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--gc-sections")

target_link_libraries(sm64ds_android PRIVATE GLESv3 aaudio log)

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

# Link recovered boot methods on native-owned leaf state; never instantiate
# the unported DS actor layout on ARM64 or fill missing dependencies with stubs.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PACKED_ROOT "${CMAKE_CURRENT_BINARY_DIR}/packed-src")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/android/tools/packed_sources.py"
    --out "${PACKED_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
include("${PACKED_ROOT}/sources.cmake")
include("${PACKED_ROOT}/inputs.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${REPO_ROOT}/android/tools/packed_sources.py"
    "${REPO_ROOT}/android/tools/frontend_manifest.json" ${PACKED_FRONTEND_INPUTS})
add_library(sm64ds_frontend STATIC ${PACKED_FRONTEND_SOURCES})
# Projected POD headers must never enter native runtime includes.
target_include_directories(sm64ds_frontend PRIVATE "${PACKED_ROOT}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}" "${REPO_ROOT}/port/ntr/include" "${REPO_ROOT}/include")
# The downloadable debug APK is for on-device playtesting. Keep symbols,
# but do not compile the hundreds of native scene/frontend routines and the
# CPU rasterizer at Clang's unoptimized Debug defaults.
target_compile_options(sm64ds_frontend PRIVATE -O2)
target_compile_options(sm64ds_android PRIVATE -O2)
target_compile_options(sm64ds_frontend PRIVATE -fwrapv -fsigned-char -fno-strict-aliasing
    -fvisibility=hidden -ffunction-sections -fdata-sections
    -Wno-unknown-pragmas -Wno-return-type-c-linkage -Wno-unused-parameter
    -Wno-unused-variable -Wno-sign-compare)
target_compile_options(sm64ds_frontend PRIVATE -Wno-comment -Wno-implicit-fallthrough
    -Wno-misleading-indentation -Wno-unused-but-set-variable -Wno-unused-value)
set_property(TARGET sm64ds_frontend PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
target_sources(sm64ds_android PRIVATE runtime/frontend_core.cpp runtime/frontend_primitives.cpp
    runtime/packed_memory.cpp runtime/actor_dispatch.cpp)
target_link_libraries(sm64ds_android PRIVATE sm64ds_frontend dl)
set(NATIVE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/native-src")
set(SAVE_SOURCES "${NATIVE_ROOT}/src/engine/save/SaveData.cpp"
    "${NATIVE_ROOT}/src/engine/save/SaveData2.cpp" "${NATIVE_ROOT}/src/SharedFilePtr.cpp")
set(GAME_SOURCES "${NATIVE_ROOT}/runtime/boot.cpp" "${NATIVE_ROOT}/runtime/faders.cpp"
    "${NATIVE_ROOT}/runtime/transitions.cpp" "${NATIVE_ROOT}/runtime/font.cpp"
    "${NATIVE_ROOT}/runtime/bg.cpp" "${NATIVE_ROOT}/runtime/message_parser.cpp" "${NATIVE_ROOT}/runtime/approach.cpp"
    "${NATIVE_ROOT}/runtime/stage_flow.cpp" "${NATIVE_ROOT}/runtime/title_selection.cpp"
    "${NATIVE_ROOT}/runtime/stage_input.cpp" "${NATIVE_ROOT}/runtime/input_atan2.cpp"
    "${NATIVE_ROOT}/runtime/title_files.cpp")
add_custom_command(OUTPUT ${SAVE_SOURCES} ${GAME_SOURCES} "${NATIVE_ROOT}/runtime/boot_state.h"
    "${NATIVE_ROOT}/runtime/title_records.h"
    "${NATIVE_ROOT}/runtime/controller_input.h"
    COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/android/tools/native_sources.py" --out "${NATIVE_ROOT}"
    DEPENDS "${REPO_ROOT}/android/tools/native_sources.py" "${REPO_ROOT}/port/tools/hostgen.py"
        "${REPO_ROOT}/src/engine/save/SaveData.cpp" "${REPO_ROOT}/src/engine/save/SaveData2.cpp"
        "${REPO_ROOT}/src/SharedFilePtr.cpp" "${REPO_ROOT}/include/dScBoot_c.h"
        "${REPO_ROOT}/src/game/stages/d_s_boot_methods.cpp" "${REPO_ROOT}/src/game/stages/d_scene.cpp"
        "${REPO_ROOT}/src/runtime/graphics/fader/d_fader.cpp" "${REPO_ROOT}/src/LoadFont.cpp"
        "${REPO_ROOT}/src/engine/gx/G2.cpp" "${REPO_ROOT}/src/ApproachAngle.cpp"
        "${REPO_ROOT}/src/ui/messages/func_0201cb2c.c"
        "${REPO_ROOT}/src/game/stages/d_s_stage_methods.cpp" "${REPO_ROOT}/src/game/stages/d_s_dsmt.cpp"
        "${REPO_ROOT}/src/cstd_atan2.cpp")
add_library(sm64ds_save STATIC ${SAVE_SOURCES})
target_include_directories(sm64ds_save PRIVATE "${REPO_ROOT}/include")
target_compile_definitions(sm64ds_save PRIVATE SM64DS_PLATFORM_PC)
target_compile_options(sm64ds_save PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections
    -fno-strict-aliasing -Wno-unknown-pragmas)
target_sources(sm64ds_android PRIVATE runtime/save_store.cpp runtime/game_heap.cpp runtime/resources.cpp)
target_sources(sm64ds_android PRIVATE runtime/game_input.cpp
    "${REPO_ROOT}/src/unnamed/arm9/0203/func_0203bb60.c"
    "${REPO_ROOT}/src/unnamed/arm9/0203/func_0203bc7c.c")
set_source_files_properties("${REPO_ROOT}/src/unnamed/arm9/0203/func_0203bc7c.c"
    PROPERTIES COMPILE_OPTIONS -Wno-unknown-pragmas)
set_source_files_properties(runtime/resources.cpp PROPERTIES COMPILE_DEFINITIONS SM64DS_PLATFORM_PC)
target_link_libraries(sm64ds_android PRIVATE sm64ds_save)

target_sources(sm64ds_android PRIVATE ${GAME_SOURCES} runtime/game_program.cpp runtime/cartridge.cpp
    runtime/stage_flow.cpp runtime/stage_assets.cpp runtime/title_files.cpp runtime/native_math.cpp
    "${REPO_ROOT}/src/SetNumPlayers.c" "${REPO_ROOT}/src/SublevelToLevel.c"
    "${REPO_ROOT}/src/unnamed/arm9/0203/func_020308d0.c"
    "${REPO_ROOT}/src/DecIfAbove0_Byte.c" "${REPO_ROOT}/src/GetControllerMode.c"
    "${REPO_ROOT}/src/GetPlayerFlagByte.c"
    "${REPO_ROOT}/src/GetSoundMode.c"
    "${REPO_ROOT}/port/ntr/ppu.cpp"
    "${REPO_ROOT}/src/ui/messages/func_0201fe08.c" "${REPO_ROOT}/src/GetOwnerLanguage.c"
    "${REPO_ROOT}/src/SetBg0Offset.c" "${REPO_ROOT}/src/SetSubBg0Offset.c"
    "${REPO_ROOT}/src/SetSubBg1Offset.c" "${REPO_ROOT}/src/SetSubBg2Offset.c"
    "${REPO_ROOT}/src/unnamed/arm9/0202/func_020233f4.c")
target_include_directories(sm64ds_android PRIVATE "${NATIVE_ROOT}")
target_compile_definitions(sm64ds_android PRIVATE SM64DS_PLATFORM_PC)
target_compile_options(sm64ds_android PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections -fno-strict-aliasing)
target_link_options(sm64ds_android PRIVATE -Wl,--no-undefined)
set_source_files_properties(${GAME_SOURCES} runtime/game_program.cpp "${REPO_ROOT}/port/ntr/ppu.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-int-to-pointer-cast;-Wno-int-to-void-pointer-cast;-Wno-unknown-pragmas")
