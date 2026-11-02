/*
 * MidiParser.h
 *
 *  Created on: 13.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */


#ifndef MIDIPARSER_H_
#define MIDIPARSER_H_

#include "stm32f7xx_hal.h"
#include "Soundengine/globals.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"


void midiParser_parseUartData(unsigned char data);
void midiParser_ccHandler(MidiMsg msg, uint8_t updateOriginalValue, uint8_t isFloatModulator, float v);
void midiParser_parseMidiMessage(volatile MidiMsg msg);
// 1 when incoming USB MIDI is forwarded to the DIN output
uint8_t midiParser_isUsb2MidiRouted(void);
float midiParser_calcDetune(uint8_t value);
// check mtc status, might stop the sequencer
void midiParser_checkMtc();

#if 0
void midiDebugSend(uint8_t b1, uint8_t b2);
#endif

// 0 - Off - nothing to nothing
// 1 - U2M - usb in to midi out
// 2 - M2M - midi in to midi out
// 3 - A2M - usb in and midi in to midi out
// 4 - M2U - midi in to usb out
// 5 - M2A - midi in to usb out and midi out

void midiParser_setRouting(uint8_t value);

// it's either tx or rx. value is a bitmap value from 0 to 15 where (lsb first):
// bit 1 - Note on/off
// bit 2 - Realtime on/off
// bit 3 - CC on/off (right now only applies to Rx)
// bit 4 - Prog chg on/off
void midiParser_setFilter(uint8_t is_tx, uint8_t value);

// a place to store all the incoming CC values
//needed to know to which value the automation node should return
/* 256 covers every index that is ever READ back:
     - autoNode_setDestination() reads [node->destination], and destination is
       assigned from Step.param1Nr (SeqStep.h), a uint8_t, so it cannot exceed
       255 even from a corrupt .PAT file. It is the only reader.
     - MIDI_CC writes [msg.data1], 0-127.
     - MIDI_CC2 writes [msg.data1 + 128]. data1 is 8 bit there, because
       midiParser_nrpnHandler() truncates a uint16_t NRPN number into it, so
       that index runs to 383 - past this array on purpose. Those entries have
       no automation destination that could ask for them (a destination d > 127
       arrives as data1 = d - 128 and so writes [d], which is <= 255), so the
       write is bounded in midiParser_ccHandler() rather than carrying 128
       bytes nothing reads.
   The old 255 was one short of the readable range, and CC 0 used to land at
   65536 because paramNr underflowed. */
#define MIDI_ORIGINAL_CC_VALUE_COUNT 256
extern uint8_t midiParser_originalCcValues[MIDI_ORIGINAL_CC_VALUE_COUNT];


extern uint8_t midi_MidiChannels[8]; // last element is global channel
//extern uint8_t midi_NoteOverride[7];
//extern uint8_t midi_mode; not used anymore

//enum MIDI_modeEnum
//{
//	MIDI_MODE_TRIGGER,
//	MIDI_MODE_NOTE,
//} MidiModes;


enum TxRxFilterValues
{
	MIDI_FILTER_NOTES = 0x01,
	MIDI_FILTER_SYSTEM = 0x02,
	MIDI_FILTER_CC = 0x04,
	MIDI_FILTER_PROG_C = 0x08,
};

// high nibble is TX low nibble is RX. see above (midiParser_setFilter) for bitmap
extern uint8_t midiParser_txRxFilter;

#endif /* MIDIPARSER_H_ */
