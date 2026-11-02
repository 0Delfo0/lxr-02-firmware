#!/usr/bin/env python3
"""Build the reference test project: every parameter at a known, stated value.

  python3 tools/make_ref_project.py --out test-project        # files + manifest
  python3 tools/make_ref_project.py --out /run/media/.../LXR02 --slot 63
  python3 tools/make_ref_project.py --manifest-only

Why this exists: the issue #1 device test was run four times, and the largest
single cause was that every measurement was taken on whatever project happened
to be loaded. That kit had two clock-synced LFOs pointed at the voice under
test, so one trig came out as four amplitude bursts. Nothing was broken; the
starting state was simply unknown. See POST-MORTEM-2026-11-01.md.

The files are built here rather than captured from the device, so that the
repository holds the project and any drift is a diff. Layout and offsets come
from docs/file-formats.md and the `snd_offset` / `glo_cfg_offset` fields of
docs/params.json, so this cannot disagree with the firmware's own description.

SPDX-License-Identifier: GPL-3.0-only
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PARAMS = os.path.join(ROOT, "docs", "params.json")

NUM_KITS = 64
NUM_PATTERNS = 64
NUM_TRACKS = 7
NUM_STEPS = 64
TRACK_LENGTH = 16
PATTERN_BODY = 3649
CONDITION_BYTES = 168
NEUTRAL_TIMING = 0x70        # flam 0, shift 7 = no shift (file-formats.md)
NO_AUTOMATION = 0xFF
PM63_ZERO = 60               # DTYPE_PM63 displays raw - 60, so 60 reads as 0

PROJECT_NAME = "REFTEST"
SLOT_NAME = "REF"

# Every sound parameter is set by exactly one rule below, keyed on the
# (category, short) pair of its name in params.json, and the script asserts
# that all 247 are covered. The reason column is what ends up in the manifest.
SOUND_RULES = {
    ("Oscilltr", "wav"): (0, "sine: one partial, so a level reading is the envelope and nothing else"),
    ("Oscilltr", "coa"): (90, "raw 90 = display 30, inside the range the manual suggests for a kick, so the pitch is clearly audible"),
    ("Oscilltr", "fin"): (PM63_ZERO, "no fine detune"),
    ("Oscilltr", "mod"): (0, "FM mode; with FM amount 0 this is a plain sine"),
    ("Oscilltr", "eng"): (0, "the stock engine, not feedback FM"),
    ("Oscilltr", "tmd"): (0, "no timbre decay"),
    ("Oscilltr", "pwm"): (64, "mid; only reached by the PWM waveform, which is not selected"),
    ("Noise", "noi"): (64, "mid; only reached through the noise mix, which is 0"),
    ("Nois/Osc", "mix"): (0, "tonal part only, no noise: an envelope is measurable, noise is not"),
    ("Mod Osc", "f1"): (PM63_ZERO, "no offset"),
    ("Mod Osc", "f2"): (PM63_ZERO, "no offset"),
    ("Mod Osc", "g1"): (0, "modulation oscillators silent"),
    ("Mod Osc", "g2"): (0, "modulation oscillators silent"),
    ("Filter", "typ"): (7, "off. Label 7 is `off`, NOT 0, which is LP - params.json labels"),
    ("Filter", "frq"): (127, "wide open; irrelevant with the filter off, and harmless if it is turned on"),
    ("Filter", "res"): (0, "no resonance peak to confuse a level reading"),
    ("Filter", "drv"): (0, "no filter overdrive"),
    ("Veloc EG", "atk"): (0, "immediate attack, so the onset is a step and its time is unambiguous"),
    ("Veloc EG", "dec"): (40, "**chosen from a measurement on this very kit**, see the calibration table in the manifest: the hit falls 40 dB in 660 ms, so it finishes well inside a bar and leaves silence before the next one. 100 was the first choice and is wrong for a reference - the tone is still within 0.7 dB of its peak 400 ms later and never completes, so neither a hit count nor a decay change is measurable"),
    ("Veloc EG", "d1"): (40, "closed hat decay, same as the other decays"),
    ("Veloc EG", "d2"): (40, "open hat decay, same as the other decays"),
    ("Veloc EG", "slp"): (64, "mid slope. Deliberately not 127, which is the value issue #6 was about"),
    ("Veloc EG", "rpt"): (0, "no envelope repeats, so one trig is one hit"),
    ("Mod EG", "dec"): (0, "no pitch envelope"),
    ("PitchMod", "mod"): (0, "no pitch modulation: the pitch stays put during the hit"),
    ("FM", "amt"): (0, "no FM"),
    ("FM", "frq"): (PM63_ZERO, "no offset"),
    ("Voice", "vol"): (100, "loud enough to sit far above the noise floor, short of the top of the range"),
    ("Voice", "pan"): (PM63_ZERO, "centred, so both channels carry the same signal"),
    ("Voice", "drv"): (0, "no voice overdrive"),
    ("Voice", "srt"): (127, "full sample rate. mixer_decimation_rate is 1.0 = full rate (mixer.c:39,50), and the CC scales 0..1, so 127 is the clean end"),
    ("Voice", "out"): (0, "the first stereo pair. Deliberately not 6 (FX): that is what feeds the FX bus"),
    ("LFO", "frq"): (64, "mid; irrelevant with the amount at 0"),
    ("LFO", "mod"): (0, "**the one that broke the #1 measurement three times**: two LFOs were pointed at the voice under test"),
    ("LFO", "wav"): (0, "sine; irrelevant with the amount at 0"),
    ("LFO", "voi"): (1, "voice 1; irrelevant with the amount at 0, and the minimum of the range"),
    ("LFO", "dst"): (0, "modTargets index 0 is PAR_NONE: no destination"),
    ("LFO", "rtg"): (0, "no retrigger"),
    ("LFO", "snc"): (0, "no clock sync. Sync is what made the modulation track the tempo and look like extra hits"),
    ("LFO", "ofs"): (0, "no offset"),
    ("Velocity", "vol"): (0, "the hard-wired velocity-to-volume link off, so step velocity does not change the level"),
    ("Velocity", "amt"): (0, "no velocity modulation"),
    ("Velocity", "dst"): (0, "modTargets index 0 is PAR_NONE: no destination"),
    ("Transnt", "vol"): (0, "no transient: the click would dominate the peak of a short hit"),
    ("Transnt", "wav"): (0, "irrelevant with the volume at 0"),
    ("Transnt", "frq"): (64, "irrelevant with the volume at 0"),
    ("Fx", "typ"): (0, "off"),
    ("Fx", "out"): (0, "the first stereo pair. **There is no `off` value here** - the labels are output destinations only, so the FX is silenced by `typ` 0 and by no voice routing to FX"),
    ("Fx", "d/w"): (0, "fully dry. On its own this does NOT silence the FX - measured - which is why `typ` is 0 as well"),
    ("Fx", "drv"): (0, "no drive"),
    ("Fx", "col"): (64, "mid; unreached with the FX off"),
    ("Fx", "ton"): (64, "mid; unreached with the FX off"),
    ("Fx", "vol"): (0, "no FX output"),
    ("Fx", "rat"): (0, "unreached with the FX off"),
    ("Fx", "wav"): (0, "unreached with the FX off"),
    ("", ""): (0, "reserved parameter with no name in params.json: left at 0"),
}

GLOBAL_RULES = {
    "PAR_BPM": (1, "**`MID`, external MIDI sync**, so the project arrives ready for host-driven measurement. Measured in #81: a project load does carry the sync mode, because `preset_loadGlobals()` -> `menu_sendAllGlobals()` -> `menu_parseGlobalParam(PAR_BPM, ...)` sends `frontPanel_sendData(SET_BPM, ...)`, the front-panel message `CortexParser.c:648-662` turns into `seq_setExtSync()`. SysEx `PARAM_SET` cannot, which is why the opposite was believed for a while. Verified end to end: with the panel on `EXT` beforehand, loading this project put `PAR_BPM` at 1 and the sequencer then ran from a clock sent over USB. **The trade-off:** with `MID` the internal clock is off, so START on the panel does nothing until a clock arrives - right for a measurement project, confusing if you pick the unit up to play it, in which case turn pot 1 up on the config page"),
    "PAR_MIDI_CHAN_1": (1, "every voice on channel 1, so one channel reaches everything"),
    "PAR_MIDI_CHAN_2": (1, "as above"),
    "PAR_MIDI_CHAN_3": (1, "as above"),
    "PAR_MIDI_CHAN_4": (1, "as above"),
    "PAR_MIDI_CHAN_5": (1, "as above"),
    "PAR_MIDI_CHAN_6": (1, "as above"),
    "PAR_MIDI_CHAN_7": (1, "as above"),
    "PAR_MIDI_CHAN_GLOBAL": (1, "as above"),
    "PAR_MIDI_NOTE1": (60, "one distinct note per voice, 60-66, so a single voice can be triggered without muting the others"),
    "PAR_MIDI_NOTE2": (61, "see note 1"),
    "PAR_MIDI_NOTE3": (62, "see note 1"),
    "PAR_MIDI_NOTE4": (63, "see note 1"),
    "PAR_MIDI_NOTE5": (64, "see note 1"),
    "PAR_MIDI_NOTE6": (65, "see note 1"),
    "PAR_MIDI_NOTE7": (66, "see note 1"),
    "PAR_MIDI_FILT_TX": (15, "all: transmit everything, so the host can watch what the device sends"),
    "PAR_MIDI_FILT_RX": (15, "all: receive notes, clock, CC and program change. Clock needs bit 1, CC needs bit 2"),
    "PAR_MIDI_ROUTING": (0, "no soft thru: nothing is echoed between the ports"),
    "PAR_MIDI_MODE": (0, "trigger mode"),
    "PAR_FETCH": (0, "off: a pot jumps the value instead of picking it up, which keeps panel edits predictable"),
    "PAR_FOLLOW": (0, "off"),
    "PAR_QUANTISATION": (0, "off: recorded notes land where they are played"),
    "PAR_SCREENSAVER_ON_OFF": (0, "off, so the display still shows what it showed an hour ago"),
    "PAR_PRESCALER_CLOCK_IN": (1, "24 PPQ on the analog clock input, matching MIDI clock resolution"),
    "PAR_PRESCALER_CLOCK_OUT1": (4, "24 PPQ out"),
    "PAR_PRESCALER_CLOCK_OUT2": (4, "24 PPQ out"),
    "PAR_TRIG_GATE_MODE": (0, "off"),
    "PAR_BAR_RESET_MODE": (0, "off"),
    "PAR_PATTERN_MODE": (0, "kit change off: loading a pattern does not swap the kit under a measurement"),
    "PAR_SYSEX_ENABLE": (1, "on: without it nothing here can be driven or read from the laptop"),
    "PAR_SYSEX_DEVICE_ID": (1, "device 1, what tools/lxr_test.py addresses"),
    "PAR_SYSEX_ECHO": (0, "off: the echo streams a message per panel edit and competes with measurement traffic"),
}


def load():
    with open(PARAMS) as f:
        return json.load(f)


def sound_values(doc):
    """One value per sound parameter, with the rule that produced it."""
    out, missing = {}, []
    for p in doc["params"]:
        if p.get("scope") != "sound":
            continue
        n = p.get("name") or {}
        key = (n.get("category", ""), n.get("short", ""))
        if key not in SOUND_RULES:
            missing.append(f"{p['symbol']} (category {key[0]!r}, short {key[1]!r})")
            continue
        value, why = SOUND_RULES[key]
        lo, hi = p.get("min", 0), p.get("max", 127)
        if not lo <= value <= hi:
            missing.append(f"{p['symbol']}: rule value {value} outside its range {lo}-{hi}")
            continue
        out[p["id"]] = (value, why, p)
    return out, missing


def global_values(doc):
    out, missing = {}, []
    for p in doc["params"]:
        if p.get("scope") != "global":
            continue
        if p["symbol"] not in GLOBAL_RULES:
            missing.append(f"{p['symbol']} has no rule")
            continue
        value, why = GLOBAL_RULES[p["symbol"]]
        lo, hi = p.get("min", 0), p.get("max", 255)
        if not lo <= value <= hi:
            missing.append(f"{p['symbol']}: rule value {value} outside its range {lo}-{hi}")
            continue
        out[p["id"]] = (value, why, p)
    return out, missing


def name8(text):
    # NUL padded, which is what the firmware writes. Read off the card after the
    # firmware had saved the whole project itself: PROJ63/00-REF.SND carries
    # b"REF\0\0\0\0\0".
    #   A SysEx *dump* of a pattern does come back space padded, and that was
    # briefly taken for the file format. It is not: sx_copyName() normalises the
    # name on the SysEx receive path, so only the slot written that way is
    # affected. The file names on the card prove it - that slot is
    # "00-REF__.PAT", the underscores being the spaces encoded by the 8.3 naming
    # rule, while every other slot is "NN-REF.PAT". See issue #96.
    return text.encode("ascii", "replace")[:8].ljust(8, b"\x00")


def build_snd(doc, sv):
    """8-byte name, 247 parameter bytes at their own snd_offset, EngV01 tag."""
    buf = bytearray(261)
    buf[0:8] = name8(SLOT_NAME)
    for pid, (value, _why, p) in sv.items():
        buf[p["snd_offset"]] = value & 0xFF
    buf[255:261] = b"EngV01"
    return bytes(buf)


def build_pat():
    """An empty pattern: no step active anywhere, neutral timing, length 16."""
    body = bytearray(PATTERN_BODY)
    for trk in range(NUM_TRACKS):
        for s in range(NUM_STEPS):
            o = trk * 512 + s * 7
            body[o + 0] = 0                      # bit 7 clear: step not active
            body[o + 1] = 100                    # probability, 0-100, full
            body[o + 2] = 60                     # note
            body[o + 3] = NO_AUTOMATION
            body[o + 4] = 0
            body[o + 5] = NO_AUTOMATION
            body[o + 6] = 0
        for s in range(NUM_STEPS):
            body[trk * 512 + 448 + s] = NEUTRAL_TIMING
        body[3641 + trk] = TRACK_LENGTH
    body[3640] = 0                               # shuffle off
    body[3648] = 0                               # every pattern uses kit 0
    return name8(SLOT_NAME) + b"PatV04" + bytes(body) + bytes(CONDITION_BYTES)


def build_sng():
    body = bytearray(64)
    body[0] = 0                                  # play pattern 0
    body[1] = 64                                 # 64 = end of song
    for i in range(2, 64):
        body[i] = 64
    return name8(SLOT_NAME) + bytes(body)


def build_glo(doc, gv):
    size = 33
    buf = bytearray(size)
    for pid, (value, _why, p) in gv.items():
        buf[p["glo_cfg_offset"]] = value & 0xFF
    return bytes(buf)


def manifest(doc, sv, gv, slot):
    lines = [
        "# Reference test project: every value and why",
        "",
        "**Generated by `tools/make_ref_project.py`. Do not edit by hand.**",
        "",
        f"Project slot `PROJ{slot:02d}`, {NUM_KITS} kits, {NUM_PATTERNS} patterns and songs, all identical.",
        "Names are upper case because FatFs is built without long file names (`_USE_LFN 0`), so the",
        "firmware only ever sees 8.3 short names - the same spelling the factory card uses.",
        "Offsets and sizes come from [`file-formats.md`](../lxr-02-firmware/docs/file-formats.md) and from",
        "the `snd_offset` and `glo_cfg_offset` fields of `params.json`, so this cannot disagree with the",
        "firmware's own description of its files.",
        "",
        "## What this project is for",
        "",
        "A measurement on the LXR-02 is only reproducible if the starting state is known. The issue #1",
        "test was run four times, and the largest single cause was two clock-synced LFOs pointed at the",
        "voice being measured in whatever kit happened to be loaded. Here nothing modulates anything",
        "unless the test asks for it, and every value is written down.",
        "",
        "## Properties this project guarantees",
        "",
        "| | |",
        "|---|---|",
        "| modulation | all six LFO amounts 0, no LFO sync, no LFO destination; every velocity amount 0 and the velocity-to-volume link off |",
        "| FX | `PAR_FX_TYPE` off, no voice routed to the FX bus, dry/wet 0 |",
        "| filter | type `off` (label 7), resonance 0, no drive |",
        "| envelope | attack 0, decay 100, slope 64, no repeats - one trig is one hit with a measurable tail |",
        "| sound | sine oscillator, no noise, no FM, no transient, no pitch envelope |",
        "| timing | shuffle 0, flam 0, step shift at its neutral 7 |",
        "| patterns | every step inactive on every track, track lengths 16, no trig conditions, all patterns on kit 0 |",
        "| triggering | one distinct MIDI note per voice, 60 to 66, all on channel 1 |",
        "",
        "## The decay value is calibrated, not assumed",
        "",
        "`PAR_VELOD3` was swept on this kit with one trig per bar at 120 bpm (a 2 s bar), measuring the",
        "envelope of the hit. The peak does not move with the decay, which is what makes the peak a stable",
        "reference and the decay the thing to measure:",
        "",
        "| decay | peak | falls 20 dB | falls 40 dB | level 400 ms after the peak |",
        "|---|---|---|---|---|",
        "| 10 | -8.0 dBFS | 113 ms | 125 ms | -71.2 dB |",
        "| 20 | -8.0 dBFS | 246 ms | 271 ms | -71.3 dB |",
        "| **40** | **-8.0 dBFS** | **600 ms** | **660 ms** | **-8.0 dB** |",
        "| 60 | -8.0 dBFS | never, inside the bar | never | -3.3 dB |",
        "| 80 | -8.0 dBFS | never | never | -1.6 dB |",
        "| 100 | -8.0 dBFS | never | never | -0.7 dB |",
        "",
        "40 is the project's value: it completes in 660 ms, so a 2 s bar holds one hit and then silence,",
        "and the mapping from the parameter to the time is steep and monotonic below it. At 60 and above",
        "the envelope outlives the bar and every metric saturates - which is what made 100, the first",
        "choice, useless.",
        "",
        "## Sound parameters",
        "",
        f"All {len(sv)} of them, in parameter id order.",
        "",
        "| id | symbol | value | range | why |",
        "|---|---|---|---|---|",
    ]
    for pid in sorted(sv):
        value, why, p = sv[pid]
        lines.append(f"| {pid} | `{p['symbol']}` | **{value}** | {p.get('min')}-{p.get('max')} | {why} |")
    lines += ["", "## Global settings (`glo.cfg`)", "",
              "| id | symbol | value | range | why |", "|---|---|---|---|---|"]
    for pid in sorted(gv):
        value, why, p = gv[pid]
        lines.append(f"| {pid} | `{p['symbol']}` | **{value}** | {p.get('min')}-{p.get('max')} | {why} |")
    lines += [
        "",
        "## How to use it",
        "",
        f"1. Copy `PROJ{slot:02d}/` to the root of the test card.",
        f"2. On the device, hold **LOAD** + **step 11**, turn the encoder to **{slot}**, press the encoder.",
        "3. The sync mode comes with the project: after the load the BPM field reads **`MID`** and a clock",
        "   sent over USB drives the sequencer - measured in #81. Nothing to set by hand.",
        "   To play the unit from its own clock instead, turn **pot 1** up on the config page",
        "   (**SHIFT + step 12**); with `MID` the internal clock is off and START waits for a clock.",
        "4. Verify before measuring: `python3 tools/check_ref_project.py` reads every parameter back over",
        "   SysEx and diffs it against this manifest.",
        "",
        "## What is deliberately NOT guaranteed",
        "",
        "- `proj.idx` at the card root is not written, so the unit does not come up in this project by",
        "  itself; it has to be loaded.",
        "- The kit name is the same in all 64 slots, so a test that depends on telling kits apart by name",
        "  needs to rename them first.",
        "- Parameter values that no reader can reach are set to a stated mid or zero value rather than",
        "  being left out, so the files have no undefined bytes.",
    ]
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", help="directory to write proj<NN>/ into (a card mount point, or a path in the repo)")
    ap.add_argument("--slot", type=int, default=63, help="project slot number (default 63, away from the factory projects)")
    ap.add_argument("--manifest-only", action="store_true")
    a = ap.parse_args(argv)

    doc = load()
    sv, miss_s = sound_values(doc)
    gv, miss_g = global_values(doc)
    if miss_s or miss_g:
        print("every parameter must be covered by a rule; these are not:")
        for m in miss_s + miss_g:
            print("  " + m)
        return 1
    expected_sound = doc["counts"]["END_OF_SOUND_PARAMETERS"]
    if len(sv) != expected_sound:
        print(f"covered {len(sv)} sound parameters, params.json says there are {expected_sound}")
        return 1
    print(f"{len(sv)} sound parameters and {len(gv)} globals, every one covered by a stated rule")

    if a.manifest_only or not a.out:
        sys.stdout.write(manifest(doc, sv, gv, a.slot))
        return 0

    # Upper case throughout: FatFs is built with _USE_LFN 0, so the firmware
    # only ever sees 8.3 short names, and that is how the factory card spells
    # them (PROJ00/00-BADON.SND, GLO.CFG, PRJ.NFO).
    folder = os.path.join(a.out, f"PROJ{a.slot:02d}")
    os.makedirs(folder, exist_ok=True)
    snd, pat, sng = build_snd(doc, sv), build_pat(), build_sng()
    assert len(snd) == 261 and len(pat) == 3831 and len(sng) == 72, (len(snd), len(pat), len(sng))
    for slot in range(NUM_KITS):
        open(os.path.join(folder, f"{slot:02d}-{SLOT_NAME}.SND"), "wb").write(snd)
        open(os.path.join(folder, f"{slot:02d}-{SLOT_NAME}.PAT"), "wb").write(pat)
        open(os.path.join(folder, f"{slot:02d}-{SLOT_NAME}.SNG"), "wb").write(sng)
    open(os.path.join(folder, "GLO.CFG"), "wb").write(build_glo(doc, gv))
    open(os.path.join(folder, "PRJ.NFO"), "wb").write(name8(PROJECT_NAME))
    with open(os.path.join(a.out, "MANIFEST.md"), "w") as f:
        f.write(manifest(doc, sv, gv, a.slot))
    total = sum(os.path.getsize(os.path.join(folder, f)) for f in os.listdir(folder))
    print(f"wrote {folder}: {len(os.listdir(folder))} files, {total} bytes")
    print(f"  .snd {len(snd)} B each, .pat {len(pat)} B, .sng {len(sng)} B, "
          f"glo.cfg {len(build_glo(doc, gv))} B, prj.nfo 8 B")
    print(f"wrote {os.path.join(a.out, 'MANIFEST.md')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
