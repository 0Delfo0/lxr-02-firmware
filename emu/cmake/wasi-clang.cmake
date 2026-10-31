# LXR-02 Emu (unofficial) - toolchain for a wasm32-wasi build with plain clang.
#
#   cmake -S emu -B build/emu-wasm -DCMAKE_TOOLCHAIN_FILE=emu/cmake/wasi-clang.cmake
#
# Needs clang, wasm-ld, the wasm32 compiler-rt builtins and wasi-libc
#   Ubuntu:   apt install clang lld wasi-libc libclang-rt-dev-wasm32
#   openSUSE: zypper in clang lld wasi-libc
#
# Distributions disagree on two things, so both are probed rather than assumed:
# where the sysroot lives (Ubuntu /usr, openSUSE /usr/share/wasi-sysroot), and
# how the target is spelled - wasm32-wasi is the historical name of what is now
# wasm32-wasip1, and the sysroot, the crt objects and the compiler-rt builtins
# all have to agree with the spelling clang is given. -DWASI_SYSROOT=<path>
# overrides the search.
# SPDX-License-Identifier: GPL-3.0-only
set(CMAKE_SYSTEM_NAME Generic)   # CMake has no WASI platform module
set(CMAKE_SYSTEM_PROCESSOR wasm32)

if(NOT CMAKE_C_COMPILER)
    find_program(CMAKE_C_COMPILER NAMES clang-18 clang REQUIRED)
endif()

# ---- find the sysroot ------------------------------------------------------
# A self-contained sysroot has include/stdio.h of its own; a /usr-style one
# keeps it under include/<triple>. Plain /usr/include/stdio.h is the HOST glibc
# header and must never be accepted: it configures cleanly and then compiles
# the firmware against the wrong libc.
if(NOT WASI_SYSROOT)
    foreach(candidate /usr/share/wasi-sysroot /usr/wasm32-wasi /opt/wasi-sdk/share/wasi-sysroot)
        if(EXISTS ${candidate}/include/wasm32-wasi/stdio.h
           OR EXISTS ${candidate}/include/wasm32-wasip1/stdio.h
           OR EXISTS ${candidate}/include/stdio.h)
            set(WASI_SYSROOT ${candidate})
            break()
        endif()
    endforeach()
    if(NOT WASI_SYSROOT AND EXISTS /usr/include/wasm32-wasi/stdio.h)
        set(WASI_SYSROOT /usr)
    endif()
endif()
set(WASI_SYSROOT "${WASI_SYSROOT}" CACHE PATH "wasi-libc sysroot")
if(NOT WASI_SYSROOT)
    message(FATAL_ERROR "No wasi-libc sysroot found. Install wasi-libc (openSUSE: zypper in wasi-libc, "
                        "Ubuntu: apt install wasi-libc libclang-rt-dev-wasm32) or pass -DWASI_SYSROOT=<path>.")
endif()

# ---- the spelling the sysroot uses is the one clang must be given ----------
foreach(candidate wasm32-wasip1 wasm32-wasi)
    if(EXISTS ${WASI_SYSROOT}/lib/${candidate}/libc.a)
        set(triple ${candidate})
        set(wasi_lib ${WASI_SYSROOT}/lib/${candidate})
        break()
    endif()
endforeach()
if(NOT triple AND EXISTS ${WASI_SYSROOT}/lib/libc.a)
    set(triple wasm32-wasi)
    set(wasi_lib ${WASI_SYSROOT}/lib)
endif()
if(NOT triple)
    message(FATAL_ERROR "No libc.a under ${WASI_SYSROOT}/lib: the sysroot is incomplete.")
endif()

foreach(dir ${WASI_SYSROOT}/include/${triple} ${WASI_SYSROOT}/include)
    if(EXISTS ${dir}/stdio.h)
        set(wasi_include ${dir})
        break()
    endif()
endforeach()
if(NOT wasi_include)
    message(FATAL_ERROR "No stdio.h under ${WASI_SYSROOT}/include: the sysroot is incomplete.")
endif()

message(STATUS "wasi target ${triple}, sysroot ${WASI_SYSROOT} (include ${wasi_include}, lib ${wasi_lib})")

set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_SYSROOT ${WASI_SYSROOT})
set(CMAKE_C_FLAGS_INIT "-isystem ${wasi_include}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -L${wasi_lib}")
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
