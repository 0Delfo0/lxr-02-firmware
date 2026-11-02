#!/usr/bin/env python3
"""Which MIDI channel does a per-voice CC actually answer on? (issue #14)

midiParser_parseMidiMessage() filters a per-voice CC with
    chanonly == midi_MidiChannels[voiceNr]
and midiParser_cc2voiceNr() returns 1-based voice numbers, 0 for "no voice" and
7 for "all voices" (CC 115). midi_MidiChannels is 0-based with element 7 the
global channel, so the index is one voice too high.

This sweeps all 16 channels and reports which ones a voice-1 CC and a voice-2 CC
are accepted on. No assumption about how the 1-16 parameter maps onto the 0-15
wire channel: the sweep shows it.

Measured on firmware without the fix, with voice 1 on channel 3, voice 2 on
channel 5 and the global channel on 16:

    CC 50 -> VELOA1 (voice 1) accepted on MIDI channels [5, 16]
    CC 52 -> VELOA2 (voice 2) accepted on MIDI channels [1, 16]

5 is voice 2's channel and 1 is voice 3's, which is the off-by-one. 16 is the
global channel and is correct in both. With the fix the first list must read
[3, 16] and the second [5, 16].

Every parameter it touches is read first and written back at the end.

SPDX-License-Identifier: GPL-3.0-only
"""
import importlib.util, os, sys, time
spec = importlib.util.spec_from_file_location("lxr_test", "tools/lxr_test.py")
lxr = importlib.util.module_from_spec(spec); spec.loader.exec_module(lxr)
dev = lxr.Device()

CHAN1, CHAN2, CHANG = 273, 274, 293
PAR_A1, PAR_A2 = 49, 51            # PAR_VELOA1 (CC 50), PAR_VELOA2 (CC 52)
WANT1, WANT2, WANTG = 3, 5, 16     # distinct, and a global that is not either

g0 = lxr.get_params(dev, [CHAN1, CHAN2, CHANG, PAR_A1, PAR_A2])
print(f"as found: voice1 ch {g0[CHAN1]}, voice2 ch {g0[CHAN2]}, global ch {g0[CHANG]}, "
      f"VELOA1 {g0[PAR_A1]}, VELOA2 {g0[PAR_A2]}")
try:
    lxr.set_params(dev, [(CHAN1, WANT1), (CHAN2, WANT2), (CHANG, WANTG)])
    time.sleep(0.3)
    g = lxr.get_params(dev, [CHAN1, CHAN2, CHANG])
    print(f"set to:   voice1 ch {g[CHAN1]}, voice2 ch {g[CHAN2]}, global ch {g[CHANG]}")
    print()
    for cc, pid, name, owner in ((50, PAR_A1, "CC 50 -> VELOA1", "voice 1"),
                                 (52, PAR_A2, "CC 52 -> VELOA2", "voice 2")):
        accepted = []
        for ch in range(16):
            lxr.set_params(dev, [(pid, 10)])
            time.sleep(0.05)
            dev.raw([0xB0 | ch, cc, 99])
            time.sleep(0.07)
            if lxr.get_params(dev, [pid])[pid] == 99:
                accepted.append(ch)
        print(f"  {name} ({owner}) accepted on wire channels {accepted}  "
              f"= MIDI channels {[c+1 for c in accepted]}")
finally:
    lxr.set_params(dev, [(CHAN1, g0[CHAN1]), (CHAN2, g0[CHAN2]), (CHANG, g0[CHANG]),
                         (PAR_A1, g0[PAR_A1]), (PAR_A2, g0[PAR_A2])])
    time.sleep(0.2)
    h = lxr.get_params(dev, [CHAN1, CHAN2, CHANG, PAR_A1, PAR_A2])
    print()
    print("restored" if h == g0 else f"RESTORE MISMATCH {h} against {g0}")
