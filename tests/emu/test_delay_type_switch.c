/* Measures the transient the firmware produces when the delay type is switched
   from Del to Pp and back, on the host, so the device finding of 2026-11-24 can
   be attributed: a +32 dB jump was measured on hardware switching to ping-pong
   even with #111's clamp in place, so the clamp is not what removes it - or,
   worse, is what causes it. Running this against the library with and without
   those three masks answers which.

   The observable is named before the test: the peak of the first 40 ms after
   the switch against the peak of the 60 ms before it, plus the largest
   sample-to-sample step in each window. A click is a discontinuity, so the
   second number is the one that cannot be explained by the delay simply
   repeating louder. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "lxr_api.h"
#include "Parameters.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "fx_setup.h"

#define SR        44100
#define BLOCK     64
#define CH        2

static float buf[BLOCK * CH];

static void render(float *dst, int blocks)
{
	for (int b = 0; b < blocks; b++) {
		lxr_render(buf, BLOCK, CH);
		if (dst)
			for (int i = 0; i < BLOCK * CH; i++)
				dst[b * BLOCK * CH + i] = buf[i];
	}
}

static void stats(const float *x, int n, float *peak, float *step)
{
	*peak = 0.f; *step = 0.f;
	for (int i = 0; i < n; i++) {
		const float a = fabsf(x[i]);
		if (a > *peak) *peak = a;
		if (i) {
			const float d = fabsf(x[i] - x[i-1]);
			if (d > *step) *step = d;
		}
	}
}

static float db(float x) { return x > 0.f ? 20.f * log10f(x) : -200.f; }

/* 60 ms before, 40 ms after, in blocks of 64 frames - integer arithmetic so
   these are constant expressions and can size an array */
#define MS_BLOCKS(ms) (((SR) * (ms) / 1000) / (BLOCK))
#define PRE_BLOCKS  MS_BLOCKS(60)
#define POST_BLOCKS MS_BLOCKS(40)

static int one_switch(int to, float *jump_db, float *step_db)
{
	static float pre[PRE_BLOCKS * BLOCK * CH];
	static float post[POST_BLOCKS * BLOCK * CH];

	voiceControl_noteOn(0, 60, 127);
	render(NULL, MS_BLOCKS(120));                 /* let the tail establish */
	render(pre, PRE_BLOCKS);
	lxr_set_param(PAR_FX_DELAY_TYPE, to);
	render(post, POST_BLOCKS);

	float pp, ps, qp, qs;
	stats(pre,  PRE_BLOCKS  * BLOCK * CH, &pp, &ps);
	stats(post, POST_BLOCKS * BLOCK * CH, &qp, &qs);
	*jump_db = db(qp) - db(pp);
	*step_db = db(qs) - db(ps);
	render(NULL, MS_BLOCKS(400));                 /* let it settle */
	return pp > 1e-5f;                            /* was there signal to judge against? */
}

/* The control that has to pass before any number below means anything: with the
   delay engaged the tail after the drum has decayed must stand above the tail
   with the effect off. Without it this test reported "no transient" twelve times
   in a row while rendering digital silence. */
static int delay_is_audible(void)
{
	double tail[2];
	for (int t = 0; t < 2; t++) {
		fx_setup(0, t ? 4 : 0, 100, 40, 64, 110, 110);
		lxr_set_param(PAR_FX_DELAY_TYPE, 0);
		lxr_idle_blocks(200);
		voiceControl_noteOn(0, 60, 127);
		render(NULL, MS_BLOCKS(300));
		double e = 0; long n = 0;
		for (int b = 0; b < MS_BLOCKS(1200); b++) {
			lxr_render(buf, BLOCK, CH);
			for (int i = 0; i < BLOCK * CH; i++) { e += (double)buf[i]*buf[i]; n++; }
		}
		voiceControl_noteOff(0);
		render(NULL, MS_BLOCKS(500));
		tail[t] = n ? sqrt(e / n) : 0.0;
	}
	printf("  controllo: coda con FX off %.1f dBFS, con delay %.1f dBFS (differenza %+.1f dB)\n",
	       db((float)tail[0]), db((float)tail[1]), db((float)tail[1]) - db((float)tail[0]));
	return tail[1] > tail[0] * 4.0;   /* +12 dB: il delay si sente */
}

int main(void)
{
	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);

	if (!delay_is_audible()) {
		puts("  SALTATO - il delay non si sente nell'emulatore, quindi qualunque");
		puts("            numero su una commutazione misurerebbe il silenzio.");
		puts("            Vedi la issue sul bus FX non raggiungibile dall'API host.");
		return 0;
	}

	/* voice 1 into the bus, delay, and all four of its parameters - the filter
	   included, because Delay_process() passes its output through
	   calcOnePole(&d->tone, out) and the boot value of that one-pole is closed:
	   without PAR_FX_P3 the delay is silent and the whole test measures nothing
	   (#115). */
	fx_setup(0, 4, 100, 40, 64, 110, 110);
	lxr_set_param(PAR_FX_DELAY_TYPE, 0);  /* Del */
	lxr_idle_blocks(200);

	/* the delay line has to hold audio before a read trails into it */
	puts("  warm-up: filling the delay line");
	for (int k = 0; k < 40; k++) {
		voiceControl_noteOn(0, 60, 127);
		render(NULL, MS_BLOCKS(120));
		voiceControl_noteOff(0);
	}

	const int N = 12;
	double jmax[2] = {-1e9, -1e9}, smax[2] = {-1e9, -1e9};
	int over[2] = {0, 0}, judged[2] = {0, 0};
	puts("  verso   salto dB   discontinuita' dB");
	for (int i = 0; i < N * 2; i++) {
		const int to = (i & 1) ? 1 : 0;          /* 1 = Pp, 0 = Del */
		float j, s;
		if (!one_switch(to, &j, &s)) {
			printf("  -> %-4s  (nessun segnale prima: finestra sul silenzio)\n", to ? "Pp" : "Del");
			continue;
		}
		judged[to]++;
		if (j > jmax[to]) jmax[to] = j;
		if (s > smax[to]) smax[to] = s;
		if (j > 6.f) over[to]++;
		printf("  -> %-4s  %+8.1f  %+14.1f\n", to ? "Pp" : "Del", j, s);
	}
	for (int to = 0; to < 2; to++)
		printf("\n  -> %-3s: %d commutazioni valutate, salto massimo %+.1f dB, "
		       "discontinuita' massima %+.1f dB, oltre +6 dB in %d\n",
		       to ? "Pp" : "Del", judged[to], jmax[to], smax[to], over[to]);
	return 0;
}
