/* Setting up an effect in the emulator, and the control that says the setup
   worked.

   Learned the hard way on 2026-11-24: PAR_FX_TYPE, PAR_FX_ROUTING and
   PAR_FX_DRY_WET configure the effect but nothing feeds it. What feeds it is
   PAR_AUDIO_OUT<n> = 6, the per-voice output assignment (MENU_AUDIO_OUT is
   "St1","St2","L1","R1","L2","R2","FX"). Without that the bus has no input,
   every type sounds exactly like off, and a test reports digital silence that
   reads as "no artefact". A whole session went into that before the positive
   control existed. See issue #115.

   Order matters too: the type has to be set before the per-effect parameters,
   because FxBus_updateParameter() switches on the current type. */
#ifndef FX_SETUP_H
#define FX_SETUP_H

#include <math.h>
#include "lxr_api.h"
#include "Parameters.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"

/* p1..p4 are the effect's own four parameters; for the delay they are
   time, range, filter and feedback in that order. */
static inline void fx_setup(int voice, int fxType, int dryWet,
                            int p1, int p2, int p3, int p4)
{
	lxr_set_param(PAR_AUDIO_OUT1 + voice, 6);   /* 6 = FX */
	lxr_set_param(PAR_FX_TYPE, fxType);         /* the type first */
	lxr_set_param(PAR_FX_ROUTING, 0);           /* 0 = St1, the main pair */
	lxr_set_param(PAR_FX_DRY_WET, dryWet);
	lxr_set_param(PAR_FX_P2, p2);
	lxr_set_param(PAR_FX_P1, p1);
	lxr_set_param(PAR_FX_P3, p3);
	lxr_set_param(PAR_FX_P4, p4);
	lxr_idle_blocks(200);
}

/* Peak over `ms` of rendering, in dBFS. */
static inline double fx_peak_db(int ms, int sr, int block)
{
	float buf[256 * 4];
	const int nb = (sr * ms / 1000) / block;
	double pk = 0;
	for (int b = 0; b < nb; b++) {
		lxr_render(buf, block, 4);
		for (int i = 0; i < block * 4; i++)
			if (fabs(buf[i]) > pk) pk = fabs(buf[i]);
	}
	return pk > 0 ? 20.0 * log10(pk) : -200.0;
}

/* The control every FX test runs before it measures anything: one hit with the
   effect off, one with it on, and the two must differ. Returns the difference
   in dB over the first `ms` after the hit; a test should refuse to report
   numbers unless it is clearly positive. */
static inline double fx_is_audible(int voice, int fxType, int ms,
                                   int sr, int block,
                                   int p1, int p2, int p3, int p4)
{
	double d[2];
	for (int on = 0; on < 2; on++) {
		fx_setup(voice, on ? fxType : 0, 100, p1, p2, p3, p4);
		voiceControl_noteOn((uint8_t)voice, 60, 127);
		d[on] = fx_peak_db(ms, sr, block);
		voiceControl_noteOff((uint8_t)voice);
		(void)fx_peak_db(1000, sr, block);
	}
	return d[1] - d[0];
}

#endif /* FX_SETUP_H */
