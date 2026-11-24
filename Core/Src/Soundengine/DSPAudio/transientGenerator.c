/*
 * transientGenerator.c
 *
 *  Created on: 29.06.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */



#include "Soundengine/DSPAudio/transientGenerator.h"
#include "Soundengine/config.h"
#include <string.h>
//---------------------------------------------------------------
void transient_init(TransientGenerator* transient)
{
	transient->pitch 	= 1.f;
	transient->output 	= 0;
	transient->phase	= 0;
	transient->waveform	= 0;
	transient->volume	= 1.f;
};
//---------------------------------------------------------------
IN_ITCM_RAM void transient_setWaveform(TransientGenerator* transient, const uint8_t waveform)
{
	if(waveform < NUM_TRANSIENTS + 2)
		transient->waveform = waveform;
	else
		transient->waveform = 0;
}
//---------------------------------------------------------------
void transient_trigger(TransientGenerator* transient)
{
	//reset the phase to the beginning when the generator is triggered
	transient->phase	= 0;
}
//---------------------------------------------------------------
/*IN_ITCM_RAM*/ void transient_calcBlock(TransientGenerator* transient, int16_t* buf, const uint8_t size)
{
	if((transient->waveform<=1) || (transient->phase>=2311061504u)) //no sample for snap and offset, no sample data if phase >= sample length
	{
		memset((int32_t*)buf,0,size/2*sizeof(int32_t));
		return; //snapEg and offset
	}

	uint8_t i;
	for(i=0;i<size;i++)
	{
		//if phase is < then table size, we increment it
		//transient->phase += (transient->phase<2311061504u) * (transient->pitch*(1<<20)); //2311061504 => 2204<<20
		if(transient->phase<2311061504u)
		{
			uint32_t phase = transient->phase;
			phase = phase >> 20;
			/* * 256 rather than << 8: transientData is int8_t, and shifting a
			   negative value left is undefined. Every compiler so far produces
			   the arithmetic result, which is why this sounds right, but it is
			   not something to leave to the next optimiser.
			     The (phase < TRANSIENT_SAMPLE_LENGTH) factor is kept as it was:
			   it reads like a bound but cannot act as one, since the index is
			   already bounded by the enclosing if - phase < 2311061504 gives
			   phase>>20 <= 2203, inside the 2205 rows. Removing it would be a
			   behaviour-free change to code we may yet send upstream. */
			buf[i] = transient->volume*(transientData[transient->waveform-2][phase]*256) * (phase < TRANSIENT_SAMPLE_LENGTH);//* transientVolumeTable[phase>>5];
			transient->phase +=  (transient->pitch*(1<<20)); //2311061504 => 2204<<20
		}
		else
		{
			buf[i]=0;
		}

	}
}
//---------------------------------------------------------------
/* Not called from anywhere: the per-sample variant superseded by
   transient_calcBlock(), which every voice uses. Left in place rather than
   deleted to keep the diff against upstream small, but it must not be revived
   as it stands - it holds two defects the block variant does not:
     - transientData[transient->waveform-1] indexes 1..12 of a [12] array, so
       waveform 13 reads past the table. PAR_TRANS<n>_WAVE has max 13, so that
       value is selectable. transient_calcBlock uses waveform-2 and is right.
     - phase>>5 indexes transientVolumeTable[69] with no bound of its own; the
       block variant is bounded by its enclosing if to 2203>>5 = 68, exactly
       the last element. */
void transient_calc(TransientGenerator* transient)
{
	uint32_t phase = transient->phase;
	//if phase is < than table size we increment it
	phase += (phase<2311061504u) * (transient->pitch*(1<<20)); //2311061504 => 2204<<20
	transient->phase = phase;
	phase = phase >> 20;
	if(transient->waveform==0)
	{
		//click
		transient->output = transient->volume * 32512 * transientVolumeTable[phase>>5];

	}
	else
	{
		transient->output = transient->volume*(transientData[transient->waveform-1][phase]<<8) * transientVolumeTable[phase>>5];
	}

};
//---------------------------------------------------------------
