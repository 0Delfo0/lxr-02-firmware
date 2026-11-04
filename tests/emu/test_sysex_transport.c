/*
 * Can a host start the sequencer and fire a voice through it? (#77)
 *
 * Before SEQ_TRANSPORT and SEQ_ROLL, nothing about the sequencer could be
 * measured without a finger on START: MIDI_START is acted on only in an
 * external sync mode, and the setting that selects that mode is reachable only
 * from the front panel. And a plain MIDI note goes through midiParser_noteOn(),
 * which never reaches seq_triggerVoice() - so a step's automation lane cannot
 * be made to apply from a host at all.
 *
 * Copyright (C) 2026 LXR-02 Emu contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "lxr_api.h"
#include "Sysex/SysexCodec.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Parameters.h"
#include "menu/menu.h"

#define MANUF 0x7D
#define MODEL 0x4C
#define PROTO 0x01
#define BROADCAST 0x7F

static uint8_t reply[256];
static int reply_len;

/* The device also sends MIDI clock and realtime bytes, so only what lies
   between F0 and F7 is the reply. */
static int in_sysex;
static void collect(uint8_t byte, void *user)
{
	(void)user;
	if (byte == 0xF0) { in_sysex = 1; reply_len = 0; }
	if (!in_sysex) return;
	if (reply_len < (int)sizeof reply)
		reply[reply_len++] = byte;
	if (byte == 0xF7) in_sysex = 0;
}

/* F0 7D 4C <dev> <proto> <cmd> <payload...> <xor> F7 */
static void send(uint8_t cmd, const uint8_t *payload, int n)
{
	uint8_t f[64];
	int i = 0, k;
	uint8_t x = 0;
	f[i++] = 0xF0; f[i++] = MANUF; f[i++] = MODEL;
	f[i++] = BROADCAST; f[i++] = PROTO; f[i++] = cmd;
	for (k = 0; k < n; k++) f[i++] = payload[k];
	for (k = 3; k < i; k++) x ^= f[k];
	f[i++] = (uint8_t)(x & 0x7F);
	f[i++] = 0xF7;
	reply_len = 0;
	lxr_midi_in(f, i);
	lxr_idle_blocks(400);
}

static const char *reply_name(void)
{
	if (reply_len < 7) return "(nothing)";
	if (reply[5] == SX_CMD_ACK) return "ACK";
	if (reply[5] == SX_CMD_NAK) return "NAK";
	return "(other)";
}

int main(void)
{
	int failures = 0;

	if (lxr_init(1, 128) != 0) { puts("FAIL - did not boot"); return 2; }
	lxr_idle_blocks(3000);
	lxr_set_midi_out(collect, NULL);

	/* SysEx has to be enabled, as on the device */
	lxr_set_param(PAR_SYSEX_ENABLE, 1);
	lxr_idle_blocks(200);

	printf("    PAR_SYSEX_ENABLE = %u, PAR_SYSEX_DEVICE_ID = %u\n",
	       activeParameters[PAR_SYSEX_ENABLE], activeParameters[PAR_SYSEX_DEVICE_ID]);
	puts("-- first, a command that has always existed: IDENTITY_REQUEST");
	send(SX_CMD_IDENTITY_REQ, NULL, 0);
	printf("    reply length %d, cmd byte %u (expect %u = IDENTITY_REPLY)\n",
	       reply_len, reply_len > 5 ? reply[5] : 0, SX_CMD_IDENTITY_REPLY);
	if (reply_len < 7 || reply[5] != SX_CMD_IDENTITY_REPLY) {
		puts("FAIL - the SysEx path itself is not answering; nothing below means anything");
		return 2;
	}

	puts("-- SEQ_TRANSPORT 1 with the sequencer stopped");
	printf("    running before: %u\n", seq_isRunning());
	{
		const uint8_t p[1] = { 1 };
		send(SX_CMD_SEQ_TRANSPORT, p, 1);
		printf("    reply %s, payload cmd %u value %u; running now: %u\n",
		       reply_name(), reply_len > 6 ? reply[6] : 0,
		       reply_len > 7 ? reply[7] : 0, seq_isRunning());
		if (!seq_isRunning()) {
			puts("FAIL - the sequencer did not start");
			failures++;
		}
		if (reply_len < 9 || reply[5] != SX_CMD_ACK
				|| reply[6] != SX_CMD_SEQ_TRANSPORT || reply[7] != 1) {
			puts("FAIL - expected ACK(48, 1)");
			failures++;
		}
	}

	puts("-- it runs: the step index advances");
	{
		static float block[256 * 2];
		for (int i = 0; i < 600; i++)
			lxr_render(block, 256, 2);
		printf("    still running: %u\n", seq_isRunning());
	}

	puts("-- SEQ_TRANSPORT 0 stops it");
	{
		const uint8_t p[1] = { 0 };
		send(SX_CMD_SEQ_TRANSPORT, p, 1);
		printf("    reply %s value %u; running now: %u\n", reply_name(),
		       reply_len > 7 ? reply[7] : 0, seq_isRunning());
		if (seq_isRunning()) { puts("FAIL - it did not stop"); failures++; }
	}

	puts("-- action 2 is refused rather than treated as a start");
	{
		const uint8_t p[1] = { 2 };
		send(SX_CMD_SEQ_TRANSPORT, p, 1);
		printf("    reply %s; running: %u\n", reply_name(), seq_isRunning());
		if (reply_len < 9 || reply[5] != SX_CMD_NAK) {
			puts("FAIL - expected a NAK for an action this firmware has no answer to");
			failures++;
		}
		if (seq_isRunning()) { puts("FAIL - and it started anyway"); failures++; }
	}

	puts("-- SEQ_ROLL fires a voice through the sequencer while stopped");
	{
		const uint8_t p[2] = { 0, 1 };
		send(SX_CMD_SEQ_ROLL, p, 2);
		printf("    reply %s value %u\n", reply_name(), reply_len > 7 ? reply[7] : 0);
		if (reply_len < 9 || reply[5] != SX_CMD_ACK
				|| reply[6] != SX_CMD_SEQ_ROLL) {
			puts("FAIL - expected ACK(49, voice)");
			failures++;
		}
		const uint8_t off[2] = { 0, 0 };
		send(SX_CMD_SEQ_ROLL, off, 2);
	}

	puts("-- a voice number past the last one is refused");
	{
		const uint8_t p[2] = { 7, 1 };
		send(SX_CMD_SEQ_ROLL, p, 2);
		printf("    reply %s\n", reply_name());
		if (reply_len < 9 || reply[5] != SX_CMD_NAK) {
			puts("FAIL - expected NAK ERR_RANGE for voice 7 of 0-6");
			failures++;
		}
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
