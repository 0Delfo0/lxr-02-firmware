# Host-side unit tests for the HAL-free codec, meter, FX and feedback FM. Uses the
# system C compiler.
#   make -C tests/host        build and run
#   make -C tests/host clean
ROOT    := ../..
CC      ?= cc

# __fp16 is an ARM type: Reverb.c and Freeze.c use it, so on a non-ARM host the
# tests did not compile at all and had to be skipped. _Float16 is the C23 type
# with the same IEEE binary16 storage format, and like __fp16 on Cortex-M7 it is
# computed by converting to float, so the results match. Defined only where the
# compiler does not already know __fp16.
FP16 := $(shell echo '__fp16 x;' | $(CC) -xc -c -o /dev/null - 2>/dev/null || echo -D__fp16=_Float16)

# The target has unsigned plain char (arm-none-eabi defines __CHAR_UNSIGNED__),
# x86 has signed. The firmware uses plain char for file names and FatFs paths,
# so the host build matches the target instead of the host.
CFLAGS  ?= -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined
CFLAGS  += -I$(ROOT)/Core/Inc -funsigned-char $(FP16)
BUILD   := build

.PHONY: test clean
test: $(BUILD)/test_sysex_codec $(BUILD)/test_cpu_meter $(BUILD)/test_reverb $(BUILD)/test_freeze $(BUILD)/test_feedback_fm $(BUILD)/test_seq_condition
	./$(BUILD)/test_sysex_codec
	./$(BUILD)/test_cpu_meter
	./$(BUILD)/test_reverb
	./$(BUILD)/test_freeze
	./$(BUILD)/test_feedback_fm
	./$(BUILD)/test_seq_condition

$(BUILD)/test_feedback_fm: test_feedback_fm.c $(ROOT)/Core/Src/Soundengine/DSPAudio/FeedbackFm.c $(ROOT)/Core/Inc/Soundengine/DSPAudio/FeedbackFm.h $(ROOT)/Core/Inc/Preset/KitEngineFormat.h $(ROOT)/Core/Src/Sysex/SysexCodec.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_feedback_fm.c $(ROOT)/Core/Src/Soundengine/DSPAudio/FeedbackFm.c $(ROOT)/Core/Src/Sysex/SysexCodec.c -lm

$(BUILD)/test_seq_condition: test_seq_condition.c $(ROOT)/Core/Src/Soundengine/Sequencer/SeqCondition.c $(ROOT)/Core/Inc/Soundengine/Sequencer/SeqCondition.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_seq_condition.c $(ROOT)/Core/Src/Soundengine/Sequencer/SeqCondition.c

$(BUILD)/test_sysex_codec: test_sysex_codec.c $(ROOT)/Core/Src/Sysex/SysexCodec.c $(ROOT)/Core/Inc/Sysex/SysexCodec.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_sysex_codec.c $(ROOT)/Core/Src/Sysex/SysexCodec.c

$(BUILD)/test_cpu_meter: test_cpu_meter.c $(ROOT)/Core/Src/Soundengine/CpuMeter.c $(ROOT)/Core/Inc/Soundengine/CpuMeter.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DCPU_METER_NO_HW -o $@ test_cpu_meter.c $(ROOT)/Core/Src/Soundengine/CpuMeter.c

$(BUILD)/test_reverb: test_reverb.c $(ROOT)/Core/Src/Soundengine/DSPAudio/FX/Reverb.c $(ROOT)/Core/Inc/Soundengine/DSPAudio/FX/Reverb.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_reverb.c $(ROOT)/Core/Src/Soundengine/DSPAudio/FX/Reverb.c -lm

$(BUILD)/test_freeze: test_freeze.c $(ROOT)/Core/Src/Soundengine/DSPAudio/FX/Freeze.c $(ROOT)/Core/Inc/Soundengine/DSPAudio/FX/Freeze.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_freeze.c $(ROOT)/Core/Src/Soundengine/DSPAudio/FX/Freeze.c -lm

clean:
	rm -rf $(BUILD)
