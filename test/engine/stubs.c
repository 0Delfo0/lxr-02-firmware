/* Generated no-op stubs for symbols outside the audio engine (sequencer, UI, UART, USB, codec).
 *
 * Ported from tunefish777/openelixir-02, test/engine/stubs.c, by Moritz Waser
 * (GPL-3.0-only, the same licence as this tree). The block marked "ported:" at
 * the end covers symbols this fork has and theirs does not. See issue #95. */
#include "stm32f7xx_hal.h"
#include "IO/adcPots.h"
#include "Preset/PresetManager.h"
#include "Soundengine/AudioCodecManager.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/Sequencer/EuklidGenerator.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/TriggerOut.h"
#include "Soundengine/USB/usb_manager.h"
#include "Soundengine/cs4344_cs5343.h"
#include "Soundengine/globals.h"
#include "ledHandler.h"
#include "main.h"
#include "menu/menu.h"
int CodecInit(void) { return 0; }
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim) { return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim) { return HAL_OK; }
void preset_touchTimer(PresetTimer type) { }
void menu_updateVoicePages(void) { }
uint8_t euklid_getLength(uint8_t trackNr) { return 0; }
uint8_t euklid_getRotation(uint8_t trackNr) { return 0; }
uint8_t euklid_getSteps(uint8_t trackNr) { return 0; }
void euklid_init(void) { }
void euklid_setLength(uint8_t trackNr, uint8_t value) { }
void euklid_setRotation(uint8_t trackNr, uint8_t value, uint8_t patternNr) { }
void euklid_setSteps(uint8_t trackNr, uint8_t value, uint8_t patternNr) { }
void led_updateSeqLeds(void) { }
uint8_t menu_getActiveVoice(void) { return 0; }
uint8_t menu_getSubPage(void) { return 0; }
uint8_t menu_getViewedPattern(void) { return 0; }
void menu_repaint(void) { }
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { }
void preset_morph_tick(void) { }
void seq_addNote(uint8_t trackNr,uint8_t vel, uint8_t note) { }
void seq_armAutomationStep(uint8_t stepNr, uint8_t track,uint8_t isArmed) { }
void seq_clearAutomation(uint8_t trackNr, uint8_t pattern, uint8_t automTrack) { }
void seq_clearTrack(uint8_t trackNr, uint8_t pattern) { }
void seq_copyPattern(uint8_t src, uint8_t dst) { }
void seq_copyTrack(uint8_t srcNr, uint8_t dstNr, uint8_t pattern) { }
uint16_t seq_getBpm(void) { return 0; }
uint8_t seq_getExtSync(void) { return 0; }
void seq_init(void) { }
uint8_t seq_isAutomationStepArmed(void) { return 0; }
uint8_t seq_isRunning(void) { return 0; }
uint8_t seq_isTrackMuted(uint8_t trackNr) { return 0; }
void seq_midiNoteOff(uint8_t chan) { }
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value) { }
void seq_resetPosition(void) { }
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc) { }
void seq_sendStepInfoToFront(uint16_t stepNr) { }
void seq_setActiveAutomationTrack(uint8_t trackNr) { }
void seq_setBpm(uint16_t bpm) { }
void seq_setErasingMode(uint8_t active) { }
void seq_setExtSync(uint8_t isExt) { }
void seq_setMute(uint8_t trackNr, uint8_t isMuted) { }
void seq_setNextPattern(const uint8_t patNr) { }
void seq_setQuantisation(uint8_t value) { }
void seq_setRecordingMode(uint8_t active) { }
void seq_setRoll(uint8_t voice, uint8_t onOff) { }
void seq_setRollRate(uint8_t rate) { }
void seq_setRunning(uint8_t isRunning) { }
void seq_setShuffle(float shuffle) { }
void seq_setTrackLength(uint8_t trackNr, uint8_t length) { }
void seq_sync(void) { }
void seq_tick(void) { }
void sync_midiResetPosition(void) { }
void sync_midiStartStop(uint8_t isStart) { }
void uart_clearFrontFifo(void) { }
void uart_processMidi(void) { }
void uart_sendFrontpanelByte(uint8_t data) { }
void uart_sendFrontpanelSysExByte(uint8_t data) { }
void uart_sendMidi(MidiMsg msg) { }
void usb_sendMidi(MidiMsg msg) { }
void usb_tick(void) { }
uint8_t activeParameters[NUM_PARAMS];
volatile  uint8_t bCurrentSampleValid;
int16_t dma_buffer[OUTPUT_DMA_SIZE*4];
int16_t dma_buffer2[OUTPUT_DMA_SIZE*4];
RNG_HandleTypeDef hrng;
TIM_HandleTypeDef htim2;
uint8_t menu_activePage;
PatternSet seq_patternSet;
uint8_t seq_resetBarOnPatternChange;
uint8_t seq_selectedStep;
volatile uint32_t systick_ticks;
uint8_t trigger_dividerClockOut1;
uint8_t trigger_prescalerClockInput;

/* ported: this fork's additions, which the engine sources reference and
   openelixir-02 does not have. The trig conditions and beat repeat live in
   sequencer.c and the DIN SysEx hook in SysexHandler.c, all outside the audio
   engine, so they are stubbed like the rest. CpuMeter.c is NOT stubbed: it is
   HAL-free under CPU_METER_NO_HW and is linked for real, so mixer.c's timing
   calls stay as they are on the device. */
uint8_t seq_getStepCondition(uint8_t pattern, uint8_t track, uint8_t step) { return 0; }
void seq_setStepCondition(uint8_t pattern, uint8_t track, uint8_t step, uint8_t condition) { }
void seq_setBeatRepeat(uint8_t rate, uint8_t active) { }
void sysex_rxDinMessage(uint8_t status, uint8_t isSysexByte) { }
/* 216 MHz, the HCLK CMakeLists.txt's clock tree produces, so cpuMeter_init()
   computes the same cycles-per-block figure as on the device. */
uint32_t SystemCoreClock = 216000000;
