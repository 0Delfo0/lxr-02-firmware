#!/usr/bin/env bash
# Build and run every test in tests/emu, with ASan and UBSan on.
#
# These used to be compiled one gcc line at a time, per session, which is how a
# test ends up built against the wrong include order and proving something else
# (#128). The source list, the include paths and the float-cast flags live in
# emu/CMakeLists.txt and are not restated here: this script only configures a
# sanitizer build of that same project and runs ctest.
#
#   ./tests/emu/run.sh                  all of them
#   ./tests/emu/run.sh test_song_end     the ones whose name matches
#   CC=clang ./tests/emu/run.sh          device-accurate float casts
#
# Under clang, add -fno-sanitize=float-cast-overflow: the FM and PWM
# oscillators cast out-of-range phase to uint32_t by design and rely on VCVT
# saturation, so the reports are noise. That is already in SAN below.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CC="${CC:-gcc}"
BUILD="$ROOT/build/emu-san-$(basename "$CC")"

SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
if [ "$(basename "$CC")" = clang ]; then
    SAN="$SAN -fno-sanitize=float-cast-overflow"
fi

cmake -S "$ROOT/emu" -B "$BUILD" -DCMAKE_C_COMPILER="$CC" \
      -DCMAKE_C_FLAGS="$SAN" -DCMAKE_EXE_LINKER_FLAGS="$SAN" >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null

# A test file that silently stops being built is the failure this guards
# against, so the count is checked rather than trusted.
on_disk=$(ls "$HERE"/test_*.c | wc -l)
registered=$(cd "$BUILD" && ctest -N | grep -c "^  Test  *#" || true)
registered=$((registered - 1))          # the selftest target is not a test file
echo "tests/emu: $on_disk files, $registered registered"
if [ "$on_disk" -ne "$registered" ]; then
    echo "MISMATCH: $on_disk test files but $registered registered - a file is not being built" >&2
    exit 1
fi

cd "$BUILD"
if [ $# -gt 0 ]; then
    exec ctest -j"$(nproc)" --output-on-failure -R "$1"
fi
exec ctest -j"$(nproc)" --output-on-failure
