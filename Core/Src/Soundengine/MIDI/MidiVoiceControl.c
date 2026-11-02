/*
 * MidiVoiceControl.c
 *
 *  Created on: 03.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */


#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/TriggerOut.h"
#include "Soundengine/MIDI/Uart.h"
#include "Soundengine/MIDI/MidiParser.h"
//#include "LCD_driver.h"
uint8_t voiceStatus[NUM_DRUM_VOICES];
static uint8_t active_voices=0;	// which voices are currently playing a note
//----------------------------------------------------------------
// this fn assumes a valid voice is sent
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
	active_voices |= (1<<voice);

	if(voice < 3)
		Drum_trigger(voice, vel, note);
	else if(voice < 4)
		Snare_trigger(vel, note);
	else if(voice < 5)
		Cymbal_trigger(vel, note);
	else
		HiHat_trigger(vel,voice-5,note);
	
	//Send trigger out signal
	/*
	if(trigger_isGateModeOn())
	{
		if(vel)
			trigger_triggerVoice(voice, TRIGGER_ON);
	} else {
		trigger_triggerVoice(voice, TRIGGER_PULSE);
	}
	*/

	/* No MIDI note is sent from here. The test above was always true - the bit
	   is set at the top of this function - so every hit produced a note on from
	   here AND one from the caller, with only one note off to match, leaving the
	   receiving instrument with a note hanging. Worse, the two carried different
	   note numbers whenever a voice has a note override set
	   (PAR_MIDI_NOTE1 + voice, non-zero): the caller sends the override, this
	   sent the raw note.
	     Both callers now send it themselves - seq_triggerVoice() in sequencer.c
	   and midiParser_noteOn() in MidiParser.c - because only they know which
	   note number belongs on the wire. */

	// Send to front panel so it can pulse the LED
	uart_sendFrontpanelByte(NOTE_ON);
	uart_sendFrontpanelByte(voice);
	uart_sendFrontpanelByte(0);
}
//----------------------------------------------------------------
void voiceControl_noteOff(uint8_t voice)
{
	uint8_t midiChan; // which midi channel to send a note on

	if(voice==0xff)
	{
		active_voices = 0;
		seq_midiNoteOff(0xff);
		return;
	}

	//only set voice inactive and send MIDI off when voice is currently playing
	if(active_voices & (1<<voice))
	{
		active_voices &= (~(1<<voice));

		//send midi note off
		midiChan = midi_MidiChannels[voice];
		seq_midiNoteOff(midiChan);
	}
}
//----------------------------------------------------------------
uint8_t voiceControl_isVoicePlaying(uint8_t voice)
{
	return (active_voices & (1<<voice));
}
