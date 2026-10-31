# LXR-02 Emu (unofficial) - toolchain for a wasm32-wasi build with plain clang.
#
#   cmake -S emu -B build/emu-wasm -DCMAKE_TOOLCHAIN_FILE=emu/cmake/wasi-clang.cmake
#
# Needs clang, wasm-ld, the wasm32 compiler-rt builtins and wasi-libc
# (Ubuntu: clang lld wasi-libc libclang-rt-dev-wasm32). WASI_SYSROOT points
# at the sysroot; Ubuntu installs it under /usr.
# SPDX-License-Identifier: GPL-3.0-only
set(CMAKE_SYSTEM_NAME Generic)   # CMake has no WASI platform module
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(triple wasm32-wasi)

if(NOT CMAKE_C_COMPILER)
    find_program(CMAKE_C_COMPILER NAMES clang-18 clang REQUIRED)
endif()
set(CMAKE_C_COMPILER_TARGET ${triple})
# Distributions disagree on where the sysroot goes and on whether its include
# and lib directories carry the triple. Ubuntu puts them in /usr as
# include/wasm32-wasi and lib/wasm32-wasi; openSUSE's wasi-libc installs a
# self-contained sysroot under /usr/share/wasi-sysroot with plain include and
# lib/wasm32-wasi. Probe instead of hardcoding, and keep the cache variable so
# -DWASI_SYSROOT=... still wins.
if(NOT WASI_SYSROOT)
    # A self-contained sysroot has include/stdio.h of its own; a /usr-style one
    # keeps it under include/<triple>. Plain /usr/include/stdio.h is the HOST
    # glibc header, so it must never be accepted: it configures cleanly and then
    # compiles the firmware against the wrong libc.
    foreach(candidate /usr/share/wasi-sysroot /usr/wasm32-wasi /opt/wasi-sdk/share/wasi-sysroot)
        if(EXISTS ${candidate}/include/${triple}/stdio.h OR EXISTS ${candidate}/include/stdio.h)
            set(WASI_SYSROOT ${candidate})
            break()
        endif()
    endforeach()
    if(NOT WASI_SYSROOT AND EXISTS /usr/include/${triple}/stdio.h)
        set(WASI_SYSROOT /usr)
    endif()
endif()
set(WASI_SYSROOT "${WASI_SYSROOT}" CACHE PATH "wasi-libc sysroot")
if(NOT WASI_SYSROOT)
    message(FATAL_ERROR "No wasi-libc sysroot found. Install wasi-libc (openSUSE: zypper in wasi-libc, "
                        "Ubuntu: apt install wasi-libc libclang-rt-dev-wasm32) or pass -DWASI_SYSROOT=<path>.")
endif()
set(CMAKE_SYSROOT ${WASI_SYSROOT})

if(EXISTS ${WASI_SYSROOT}/include/${triple}/stdio.h)
    set(wasi_include ${WASI_SYSROOT}/include/${triple})
else()
    set(wasi_include ${WASI_SYSROOT}/include)
endif()
if(EXISTS ${WASI_SYSROOT}/lib/${triple})
    set(wasi_lib ${WASI_SYSROOT}/lib/${triple})
else()
    set(wasi_lib ${WASI_SYSROOT}/lib)
endif()
message(STATUS "wasi sysroot: ${WASI_SYSROOT} (include ${wasi_include}, lib ${wasi_lib})")

set(CMAKE_C_FLAGS_INIT "-isystem ${wasi_include}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -L${wasi_lib}")
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
