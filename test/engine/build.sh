#!/bin/bash
# Usage: build.sh <repo root> <output dir>
# Compiles the audio engine sources of <repo root> for the host (strict IEEE
# float, no fast-math, so refactorings that keep the arithmetic give identical
# bits) and links them with the harness into <output dir>/render.
#
# Ported from tunefish777/openelixir-02, test/engine/build.sh, by Moritz Waser
# (GPL-3.0-only, the same licence as this tree). Changes made for this fork are
# marked "ported:" below. See docs/openelixir-comparison.md and issue #95.
set -e
ROOT=$(cd "$1" && pwd); OUT=$2; HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT/obj"
# ported: -std=gnu11 to match the firmware, which CMakeLists.txt builds with
# CMAKE_C_STANDARD 11. Without it the host compiler's default applies: gcc 15 and
# later default to C23, where "void f();" is a prototype with no parameters, and
# the engine's unprototyped declarations (SVF_init, Distortion_init and others)
# stop compiling. Matching the firmware's standard is also what makes a bit-exact
# comparison mean anything.
CFLAGS="-std=gnu11 -O1 -g -fno-fast-math -ffp-contract=off -fno-strict-aliasing -fcommon -w -DSTM32F765xx -DUSE_HAL_DRIVER -DCPU_METER_NO_HW -DFIRMWARE_NAME=\"OE\" -D__fp16=_Float16 -include $HERE/shim/host_cmsis.h"
INC="-I$HERE/shim -I$ROOT/Core/Inc -I$ROOT/Drivers/CMSIS/Include -I$ROOT/Drivers/CMSIS/Device/ST/STM32F7xx/Include -I$ROOT/Drivers/STM32F7xx_HAL_Driver/Inc -I$ROOT/Middlewares/JS/STM32_USB_Device_Library/Core/Inc -I$ROOT/Middlewares/JS/STM32_USB_Device_Library/Class/CDC/Inc -I$ROOT/USB_DEVICE_MIDI/Target -I$ROOT/USB_DEVICE_MIDI/App -I$ROOT/FATFS/App -I$ROOT/FATFS/Target -I$ROOT/Middlewares/Third_Party/FatFs/src"
# ported: glob what the tree actually has instead of a fixed list. The two forks
# diverge here - this one has FeedbackFm.c and FX/Freeze.c, theirs has
# DjFilter.c, VoiceSlot.c and FX/VerbTools/ - so a missing directory must not be
# an error.
SRCS=$(ls "$ROOT"/Core/Src/Soundengine/DSPAudio/*.c "$ROOT"/Core/Src/Soundengine/DSPAudio/FX/*.c 2>/dev/null)
if compgen -G "$ROOT/Core/Src/Soundengine/DSPAudio/FX/VerbTools/*.c" > /dev/null; then
	SRCS="$SRCS $(ls "$ROOT"/Core/Src/Soundengine/DSPAudio/FX/VerbTools/*.c)"
fi
# ported: CpuMeter.c is HAL-free under CPU_METER_NO_HW - tests/host already
# builds it that way - so the meter is linked for real rather than stubbed, and
# mixer.c keeps the timing calls it has on the device.
# ported: only when the tree has it, so the harness can also build an older base
# commit - compare.sh renders one - from before the CPU meter existed.
if [ -f "$ROOT/Core/Src/Soundengine/CpuMeter.c" ]; then
	SRCS="$SRCS $ROOT/Core/Src/Soundengine/CpuMeter.c"
fi
SRCS="$SRCS $ROOT/Core/Src/Soundengine/FxBus.c $ROOT/Core/Src/Soundengine/MIDI/MidiParser.c $ROOT/Core/Src/Soundengine/MIDI/MidiVoiceControl.c $ROOT/Core/Src/Soundengine/MIDI/ParameterArray.c $ROOT/Core/Src/Soundengine/MIDI/MidiMessages.c $ROOT/Core/Src/Preset/initKit.c $ROOT/Core/Src/Soundengine/MIDI/CortexParser.c $ROOT/Core/Src/Soundengine/Soundengine.c"
# UI side of engine switching (when the tree has it)
if [ -f "$ROOT/Core/Src/Preset/KitEngines.c" ]; then
	SRCS="$SRCS $ROOT/Core/Src/Preset/KitEngines.c $ROOT/Core/Src/menu/Cc2Text.c"
fi
fail=0
for f in $SRCS; do
	o="$OUT/obj/$(basename "$f" .c).o"
	gcc -c $CFLAGS $INC "$f" -o "$o" 2>"$o.err" || { echo "FAIL $f"; head -8 "$o.err"; fail=1; }
done
[ $fail = 0 ] || exit 1
python3 "$HERE/gen_ranges.py" "$ROOT" "$OUT/ranges_gen.c"
for f in "$HERE"/*.c "$OUT/ranges_gen.c"; do
	gcc -c $CFLAGS $INC "$f" -o "$OUT/obj/harness_$(basename "$f" .c).o" 2>"$OUT/obj/harness.err" || { cat "$OUT/obj/harness.err"; exit 1; }
done
gcc -o "$OUT/render" "$OUT"/obj/*.o -lm
