#!/usr/bin/env python3
"""Measure what the LXR-02 actually plays: trigger over USB MIDI, record over
USB audio, report levels.

  python3 tools/lxr_audio.py floor                 # the noise floor, nothing playing
  python3 tools/lxr_audio.py trigger 59            # one voice, peak level
  python3 tools/lxr_audio.py slope-sweep           # issue #6, as a measurement

Needs an audio interface with the LXR-02's output patched into it, `arecord`
from alsa-utils and numpy. The MIDI side is tools/lxr_test.py, which needs
neither.

Each voice answers to one note, from PAR_MIDI_NOTE1..7 in the global settings -
this reads them off the device rather than assuming, and that is what isolates
a single voice without muting the others.

SPDX-License-Identifier: GPL-3.0-only
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import time
import wave

import numpy as np

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("lxr_test", os.path.join(_here, "lxr_test.py"))
lxr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lxr)

PAR_MIDI_NOTE1 = 294            # global settings, docs/params.json
PAR_VOL1 = 88                   # drum 1 voice volume, the positive control
PAR_PITCH_SLOPE1 = 78           # the `slp` of drum 1's MOD page


def find_capture():
    """A USB capture card that is not the LXR-02's own MIDI-only interface."""
    with open("/proc/asound/cards") as f:
        cards = f.read()
    # Match each card on its own line: the LXR-02 is a USB-Audio card too, with
    # MIDI and no capture stream, so looking at the rest of the file would find
    # its name and rule every card out.
    for line in cards.splitlines():
        m = re.match(r"\s*(\d+)\s+\[", line)
        if not m or "USB-Audio" not in line or "Sonic Potions" in line:
            continue
        num = m.group(1)
        if os.path.exists(f"/proc/asound/card{num}/pcm0c"):
            return f"hw:{num},0"
    sys.exit("no USB capture device found (arecord -l)")


RATE = 48000


def record(seconds, capture, during=None):
    """Record and return (samples, rate); `during` runs while recording.

    --samples rather than -d: -d takes whole seconds, and the measurements here
    are shorter than that.
    """
    fd, path = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    proc = subprocess.Popen(
        ["arecord", "-D", capture, "-f", "S32_LE", "-c", "2", "-r", str(RATE),
         f"--samples={int(seconds * RATE)}", path],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        if during:
            time.sleep(0.35)        # let the capture settle before the trigger
            during()
        _, err = proc.communicate(timeout=seconds + 10)
        if proc.returncode != 0:
            sys.exit(f"arecord failed: {err.decode(errors='replace').strip()}")
        w = wave.open(path)
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i4")
        return data.astype(np.float64).reshape(-1, w.getnchannels()) / float(1 << 31), \
            w.getframerate()
    finally:
        if proc.poll() is None:
            proc.kill()
        if os.path.exists(path):
            os.unlink(path)


def db(x):
    return 20 * np.log10(max(float(x), 1e-12))


def centroid(v, rate):
    """Spectral centroid in Hz, so a parameter that changes the tone rather than
    the level can still be seen to have arrived."""
    x = v[:, 0]
    X = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / rate)
    total = X.sum()
    return float((f * X).sum() / total) if total else 0.0


def levels(v):
    return (max(db(np.abs(v[:, c]).max()) for c in range(v.shape[1])),
            max(db(np.sqrt((v[:, c] ** 2).mean())) for c in range(v.shape[1])))


def voice_notes(dev):
    vals = lxr.get_params(dev, list(range(PAR_MIDI_NOTE1, PAR_MIDI_NOTE1 + 7)))
    return [vals[PAR_MIDI_NOTE1 + i] for i in range(7)]


def hit(dev, note, times=2, gap=0.45, vel=127):
    def go():
        for n in range(times):
            dev.raw([0x90, note, vel])
            if n + 1 < times:
                time.sleep(gap)
    return go


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--capture", help="ALSA capture device, e.g. hw:1,0")
    ap.add_argument("--port", help="LXR MIDI port, e.g. hw:2,0,0")
    ap.add_argument("command", choices=["floor", "trigger", "slope-sweep", "notes"])
    ap.add_argument("args", nargs="*", type=int)
    args = ap.parse_args(argv)

    capture = args.capture or find_capture()
    dev = lxr.Device(args.port)
    print(f"capture {capture}   midi {dev.port}")

    if args.command == "notes":
        for i, n in enumerate(voice_notes(dev), 1):
            print(f"  voice {i}: note {n}" + ("  (0 = any note)" if n == 0 else ""))
        return 0

    if args.command == "floor":
        v, _ = record(2, capture)
        pk, rms = levels(v)
        print(f"  floor: peak {pk:.1f} dBFS   rms {rms:.1f} dBFS")
        return 0

    if args.command == "trigger":
        note = args.args[0] if args.args else voice_notes(dev)[0]
        v, _ = record(2, capture, hit(dev, note))
        pk, rms = levels(v)
        print(f"  note {note}: peak {pk:.1f} dBFS   rms {rms:.1f} dBFS")
        return 0

    # ---- issue #6: a pitch EG slope of 127 must not silence the voice -------
    # PAR_PITCH_SLOPE1 is the `slp` of DRUM 1's MOD page, the parameter whose
    # setter divided by zero at 127 (DecayEg_setSlope, Decay.c:74-79).
    par_slope, voice_note = PAR_PITCH_SLOPE1, voice_notes(dev)[0]
    original = lxr.get_params(dev, [par_slope])[par_slope]
    print(f"  drum 1 answers to note {voice_note}, slope parameter {par_slope} "
          f"is at {original}")
    v, _ = record(1.5, capture)
    floor = levels(v)[0]
    print(f"  noise floor: {floor:.1f} dBFS peak")

    # A measurement that cannot fail proves nothing: turn the voice down and
    # check the level follows, so an unchanging level later means the slope
    # really has no effect on loudness rather than that nothing arrives.
    vol = lxr.get_params(dev, [PAR_VOL1])[PAR_VOL1]
    try:
        lxr.set_params(dev, [(PAR_VOL1, 0)])
        quiet = levels(record(1.5, capture, hit(dev, voice_note))[0])[0]
    finally:
        lxr.set_params(dev, [(PAR_VOL1, vol)])
    loud = levels(record(1.5, capture, hit(dev, voice_note))[0])[0]
    print(f"  control: volume 0 -> {quiet:.1f} dBFS, volume {vol} -> {loud:.1f} dBFS "
          f"({loud - quiet:+.1f} dB)")
    if loud - quiet < 10:
        print("  FAIL  the control did not move: the parameter path or the audio "
              "path is not working, so nothing below can be trusted")
        return 1

    results, tones = {}, {}
    try:
        for slope in (0, 32, 64, 96, 110, 120, 126, 127):
            lxr.set_params(dev, [(par_slope, slope)])
            v, rate = record(1.5, capture, hit(dev, voice_note))
            pk, rms = levels(v)
            results[slope], tones[slope] = pk, centroid(v, rate)
            print(f"  slope {slope:>3}: peak {pk:7.1f} dBFS   rms {rms:7.1f} dBFS"
                  f"   centroid {tones[slope]:6.0f} Hz")
    finally:
        lxr.set_params(dev, [(par_slope, original)])
        print(f"  slope restored to {original}")

    spread = max(tones.values()) - min(tones.values())
    print(f"  the slope moved the spectral centroid by {spread:.0f} Hz across its range")

    ref, top = results.get(64), results.get(127)
    print()
    if ref is not None and top is not None:
        print(f"  slope 127 against slope 64: {top - ref:+.1f} dB")
        print(f"  127 above the noise floor:  {top - floor:+.1f} dB")
        if top - floor < 10:
            print("  FAIL  the voice is silent at slope 127 - issue #6 is present")
            return 1
        if abs(top - ref) > 6:
            print("  FAIL  slope 127 is more than 6 dB from slope 64")
            return 1
        print("  PASS  slope 127 plays at the same level as slope 64")
    return 0


if __name__ == "__main__":
    sys.exit(main())
