/*
 * Decay.c
 *
 *  Created on: 16.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */



#include "Soundengine/DSPAudio/Decay.h"
#include "Soundengine/config.h"
#include <math.h>

//-------------------------------------------------
float DecayEg_calcSlopeValue(float val, float slope)
{
	/* EG Slope 0 gives slope == -1 (DecayEg_setSlope: amount is -1 and
	   2*-1/(1-(-1)) is -1), so the numerator is 0 for every val - that is the
	   intended "no pitch sweep". Pitch Decay 127 gives a decay step of 0
	   (DecayEg_calcTime), so the envelope stays at val == 1 and the denominator
	   is 1 + (-1)*1 = 0 as well. 0/0 is NaN and it goes into the oscillator's
	   pitch: 64 of 64 samples, measured.

	   The guard goes here and not in DecayEg_setSlope(), where the fix for the
	   other extreme lives. Clamping the amount away from -1 too would give
	   slope 0 a small sweep at every decay value and move every kit that uses
	   it, which is what #93 was about. This changes exactly one output: with
	   slope == -1 the expression below is 0*val/(1-|val|), which is 0 wherever
	   it is defined, so returning 0 is the same number everywhere except at
	   |val| == 1, where it was NaN.

	   Reported upstream as erica-synths/lxr-desktop-app#18 by Justin Worrall
	   on 2026-11-27, whose one-line guard this is. */
	if(slope <= -1.f)
		return 0.f;
	return (1+slope)*val/(1+slope*fabsf(val));
}
//-------------------------------------------------
void DecayEg_init(DecayEg* eg)
{
	eg->decay 	= 0.01f;
	eg->value 	= 0;
};
//-------------------------------------------------
#define TIME_K (2*0.99f/(1.f-0.99f))
float DecayEg_calcTime(uint8_t data2)
{
	const float val = (data2)/127.f;
#if USE_BIG_SAMPLE_BUFFER
	//routine is called only half as often -> *2
	const float ret = (1.f-((1.f+TIME_K)*val/(1.f+TIME_K*fabsf(val)))) * 2.f;
	return ret > 1.f ? 1.f : ret;


#else
	return 1.f-((1.f+TIME_K)*val/(1.f+TIME_K*fabsf(val)));
#endif
}
//-------------------------------------------------
void DecayEg_trigger(DecayEg* eg)
{
	eg->value = 1.f;
};
//-------------------------------------------------
IN_ITCM_RAM float DecayEg_calc(DecayEg* eg)
{
	float val = eg->value;

	if(val>0)
	{
		val -= eg->decay;

	} else
	{
		val = 0;
	}
	eg->value = val;
	return DecayEg_calcSlopeValue(val,eg->slope);

};
//-------------------------------------------------
void DecayEg_setDecay(DecayEg* eg, uint8_t data2)
{
	eg->decay = DecayEg_calcTime(data2);
}
//--------------------------------------------------
IN_ITCM_RAM void DecayEg_setSlope(DecayEg* eg, uint8_t data2)
{
	/* Only 127 is broken, so only 127 is changed. With the factor 2 the amount at
	   data2 == 127 is exactly 1.0f, the division below is by zero, slope becomes
	   +inf and the engine state never recovers from it - see below. Every other
	   value keeps the stock arithmetic bit for bit, so existing kits sound as
	   they always did.
	     0.5f*1.999f is what the stock expression yields at 127 once the division
	   by zero is taken out: (127/127.f)-0.5f is exactly 0.5f, and 1.999f is the
	   cap slopeEg2_setSlope() has always used.
	     The earlier fix here scaled the whole range by 1.999f instead. That also
	   removed the division by zero, but it moved all 128 values: measured with
	   test/engine/compare.sh, all 32 rendered kits changed. */
	const float amount 	= data2 < 127 ? ((data2/127.f)-0.5f)*2 : 0.5f*1.999f;
	eg->slope 			= 2*amount/(1-amount);
}
//--------------------------------------------------
