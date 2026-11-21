cmake_minimum_required(VERSION 3.22)
project(mkds_port C)

# The game assumes 32-bit pointers (structures and files loaded in place).
# - 32-bit targets (armeabi-v7a, the i386 Linux test build): one image, the game linked with the platform layer.
# - 64-bit ARM (arm64-v8a, aarch64 Linux): the game and the DS platform layer form a separate "guest" image built
#   with GCC for AArch64 with 32-bit pointers (tools/build_guest.py), loaded below 4 GiB by the host library.
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" AND NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "64-bit builds are for AArch64 only")
    endif()
    set(GUEST_SPLIT ON)
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(DECOMP ${REPO}/decomp)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(DECOMP_VERSION eur)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ------------------------------------------------------------------ decomp sources, prepared for clang
file(GLOB_RECURSE DECOMP_INPUTS CONFIGURE_DEPENDS ${DECOMP}/src/*.c ${DECOMP}/libs/*.c ${DECOMP}/include/types.h
     ${DECOMP}/config/${DECOMP_VERSION}/arm9/overlays/*/delinks.txt)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPO}/tools/portify.py ${CMAKE_CURRENT_SOURCE_DIR}/exclude.txt)
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${REPO}/tools/portify.py ${DECOMP} ${GEN} ${CMAKE_CURRENT_SOURCE_DIR}/exclude.txt ${DECOMP_VERSION}
    RESULT_VARIABLE PORTIFY_RESULT)
if (NOT PORTIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "tools/portify.py failed")
endif()
file(STRINGS ${GEN}/sources.txt GAME_SOURCES)

file(GLOB_RECURSE DECOMP_INCLUDE_DIRS LIST_DIRECTORIES true ${DECOMP}/libs/*)
list(FILTER DECOMP_INCLUDE_DIRS INCLUDE REGEX "/include$")
list(SORT DECOMP_INCLUDE_DIRS)

if (NOT GUEST_SPLIT)
add_library(game STATIC ${GAME_SOURCES})
target_include_directories(game PRIVATE
    ${GEN}/include
    ${GEN}/msl_include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/include
    )
target_compile_definitions(game PRIVATE version_EUR)
target_compile_options(game PRIVATE
    -std=gnu11
    -include ${CMAKE_CURRENT_SOURCE_DIR}/compat/port_prefix.h
    -O2
    -fno-strict-aliasing   # the game type-puns freely
    -fwrapv                # and relies on wrapping arithmetic
    -fcommon               # tentative definitions shared between files, as CodeWarrior allows
    -fshort-wchar          # wchar_t is 16 bits on the DS
    -Dconst=               # constant data is RAM on the DS and the game patches some of it: keep it writable
    -fno-zero-initialized-in-bss # keep data in definition order: some tables run on into the next definition
    -ffunction-sections
    -w)
# Plain char: unsigned in game code, signed in the libraries (tools/project.py).
foreach(SRC ${GAME_SOURCES})
    if (SRC MATCHES "/src/libs/")
        set_source_files_properties(${SRC} PROPERTIES COMPILE_OPTIONS -fsigned-char)
    else()
        set_source_files_properties(${SRC} PROPERTIES COMPILE_OPTIONS -funsigned-char)
    endif()
endforeach()
endif()

# ------------------------------------------------------------------ platform layer
set(P ${CMAKE_CURRENT_SOURCE_DIR}/platform)
# Renderers: they work from frame snapshots, in the app process.
set(RENDER_SOURCES ${P}/gpu2d.c ${P}/tex.c ${P}/soft3d.c ${P}/hostside/memory.c)
option(HOST_GL "Host test build: render with OpenGL ES (EGL, Mesa)" OFF)
option(HOST_IPC "Host test build (32-bit): also build the two-process variant (mkds_server, mkds_ipc)" OFF)
file(GLOB GL_SOURCES CONFIGURE_DEPENDS ${P}/gl/*.c)
file(GLOB ANDROID_SOURCES CONFIGURE_DEPENDS ${P}/android/*.c)
file(GLOB HOST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/host/*.c)

if (GUEST_SPLIT)
    # The game side: the guest image, its loader and the host side of the system services.
    set(GUEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/guest)
    set(GUEST_CROSS "aarch64-linux-gnu-" CACHE STRING "Prefix of the GCC cross toolchain for the guest image")
    add_custom_target(guest_image ALL
        COMMAND ${Python3_EXECUTABLE} ${REPO}/tools/build_guest.py ${GEN} ${GUEST_DIR} ${GUEST_CROSS}
        BYPRODUCTS ${GUEST_DIR}/guest.elf
        COMMENT "Building the guest image (game, 32-bit pointers)")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/guest_image.S
        ".section .rodata\n.balign 64\n.globl guest_image_start\nguest_image_start:\n"
        ".incbin \"${GUEST_DIR}/guest.elf\"\n.globl guest_image_end\nguest_image_end:\n"
        ".section .note.GNU-stack,\"\",%progbits\n")
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/guest_image.S PROPERTIES
        OBJECT_DEPENDS ${GUEST_DIR}/guest.elf)
    enable_language(ASM)
    set(GAME_SIDE_SOURCES ${P}/hostside/memory.c ${P}/hostside/hostapi_impl.c ${P}/hostside/online.c ${P}/hostside/bridge.c
        ${CMAKE_CURRENT_BINARY_DIR}/guest_image.S)
else()
    # One image: the game and the whole platform layer.
    file(GLOB GAME_SIDE_SOURCES CONFIGURE_DEPENDS ${P}/*.c ${P}/hostside/*.c)
    list(REMOVE_ITEM GAME_SIDE_SOURCES ${P}/hostside/bridge.c)
endif()

function(port_target T)
    target_include_directories(${T} PRIVATE ${GEN}/include ${P}/include ${P} ${P}/gl ${CMAKE_CURRENT_SOURCE_DIR}/guest)
    target_compile_definitions(${T} PRIVATE version_EUR)
    target_compile_options(${T} PRIVATE -O2 -fno-strict-aliasing -fwrapv -Wall -Wno-unused-function
        $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)
    # The game writes to some of its constant data (pointers patched into tables): no read-only relocations.
    target_link_options(${T} PRIVATE -Wl,--no-undefined -Wl,-z,norelro)
    if (ANDROID)
        target_link_options(${T} PRIVATE -Wl,-z,max-page-size=16384)
    endif()
endfunction()

function(port_link_game T)
    if (GUEST_SPLIT)
        add_dependencies(${T} guest_image)
    else()
        target_link_libraries(${T} PRIVATE -Wl,--whole-archive game -Wl,--no-whole-archive)
    endif()
endfunction()

option(QUEST "Android: the Meta Quest build (the game on a screen in the headset, through OpenXR)" OFF)

if (ANDROID AND GUEST_SPLIT)
    # arm64: the app library (Android side, renderers) and the game process (ipc/ipc.h).
    add_library(mkds SHARED ${ANDROID_SOURCES} ${GL_SOURCES} ${RENDER_SOURCES} ${P}/ipc/client.c)
    target_compile_definitions(mkds PRIVATE PORT_IPC)
    target_link_libraries(mkds PRIVATE android log EGL GLESv3 aaudio m dl)
    if (QUEST)
        # The OpenXR loader comes with the app (the Khronos loader for Android, a Gradle dependency).
        find_package(OpenXR REQUIRED CONFIG)
        target_compile_definitions(mkds PRIVATE PORT_QUEST)
        target_link_libraries(mkds PRIVATE OpenXR::openxr_loader)
    endif()
    port_target(mkds)
    # An executable, named like a library so that it is installed with them (and may be run from there).
    add_executable(mkdsgame ${GAME_SIDE_SOURCES} ${P}/ipc/server.c ${P}/android/crash.c)
    set_target_properties(mkdsgame PROPERTIES OUTPUT_NAME "libmkdsgame.so" SUFFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    target_link_libraries(mkdsgame PRIVATE android log m dl)
    port_target(mkdsgame)
    port_link_game(mkdsgame)
    add_dependencies(mkds mkdsgame)
elseif (ANDROID)
    set(SRC ${GAME_SIDE_SOURCES} ${ANDROID_SOURCES} ${GL_SOURCES} ${RENDER_SOURCES})
    list(REMOVE_DUPLICATES SRC)
    add_library(mkds SHARED ${SRC})
    target_link_libraries(mkds PRIVATE android log EGL GLESv3 aaudio m dl)
    port_target(mkds)
    port_link_game(mkds)
else()
    # Headless test build for Linux (i386, or AArch64 with the guest image in process).
    set(SRC ${GAME_SIDE_SOURCES} ${RENDER_SOURCES} ${HOST_SOURCES})
    if (HOST_GL)
        list(APPEND SRC ${GL_SOURCES})
    endif()
    list(REMOVE_DUPLICATES SRC)
    add_executable(mkds ${SRC})
    target_link_libraries(mkds PRIVATE pthread m)
    target_link_options(mkds PRIVATE -Wl,-z,now)
    if (HOST_GL)
        target_compile_definitions(mkds PRIVATE HOST_GL)
        target_link_libraries(mkds PRIVATE EGL GLESv2)
    endif()
    port_target(mkds)
    port_link_game(mkds)
    if (HOST_IPC)
        set(SRC ${GAME_SIDE_SOURCES} ${P}/ipc/server.c)
        list(REMOVE_DUPLICATES SRC)
        add_executable(mkds_server ${SRC})
        target_link_libraries(mkds_server PRIVATE pthread m)
        target_link_options(mkds_server PRIVATE -Wl,-z,now)
        port_target(mkds_server)
        port_link_game(mkds_server)
        add_executable(mkds_ipc ${RENDER_SOURCES} ${HOST_SOURCES} ${P}/ipc/client.c)
        target_compile_definitions(mkds_ipc PRIVATE PORT_IPC)
        target_link_libraries(mkds_ipc PRIVATE pthread m)
        port_target(mkds_ipc)
    endif()
endif()
