#!/usr/bin/env python3
"""Acceptance measurement for the reference test project (issue #80).

  python3 tools/accept_ref_project.py                 # 120 bpm, 2 s bars
  python3 tools/accept_ref_project.py --bpm 60 --track 2

Writes one trig into the active pattern, clocks the sequencer from here, and
measures the result. Nothing is muted and no parameter is changed: if the
project is what MANIFEST.md says, one trig gives exactly one hit per bar with a
repeatable peak and a decay that finishes inside the bar.

Needs the panel set to MID (SHIFT + step 12, pot 1) so MIDI clock drives the
sequencer, an audio interface carrying the LXR-02's output, and `arecord`.

Two mistakes this deliberately avoids, both of which produced wrong answers
first:

- **No onset detection.** The clock comes from here and the pattern has one
  trig, so the hit times are known and do not need finding. A generic detector
  reported 43 hits in a 6 s recording during the issue #1 session.
- **The quiet window stops short of the end of the bar.** A window exactly one
  bar long ends on the next hit's onset, so asking "is the rest of the bar
  quiet?" over the whole remainder answers no, every time, on a perfectly clean
  recording.

SPDX-License-Identifier: GPL-3.0-only
"""

import argparse
import importlib.util
import os
import sys
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


NO_AUTOMATION = 255
NAME_LEN = 8


def pack8(d):
    out = bytearray()
    for i in range(0, len(d), 7):
        g, msb = d[i:i + 7], 0
        for n, b in enumerate(g):
            out.append(b & 0x7F)
            if b & 0x80:
                msb |= 1 << n
        out.append(msb)
    return bytes(out)


def read_pattern(lxr, dev):
    out = dev._run(" ".join(f"{b:02X}" for b in
                   lxr.frame(lxr.CMD["PATTERN_REQUEST"], [0x7F], dev.device)), listen=True)
    body, length, slot = bytearray(), 0, None
    for f in lxr.Device._sysex_frames(out):
        if f[5] != lxr.CMD["PATTERN_CHUNK"]:
            continue
        p = f[6:-2]
        slot, length = p[0], p[4] | (p[5] << 7)
        body += lxr.unpack8(p[6:])
    return slot, length, bytearray(body[:length])


def write_pattern(lxr, dev, slot, length, body):
    chunk = 196
    chunks = [body[i:i + chunk] for i in range(0, len(body), chunk)]
    for i, c in enumerate(chunks):
        payload = [slot, 0, i, len(chunks), length & 0x7F, (length >> 7) & 0x7F] + list(pack8(bytes(c)))
        cc, _ = dev.request(lxr.CMD["PATTERN_CHUNK"], payload, expect=lxr.CMD["ACK"])
        if cc is None:
            sys.exit(f"no ACK for pattern chunk {i}")


def one_trig(base, track, step):
    """The reference pattern with a single trig added, and nothing else changed."""
    b = bytearray(base)
    body = memoryview(b)[NAME_LEN:]
    o = track * 512 + step * 7
    body[o + 0] = 0x80 | 100            # bit 7 = step active, volume 100
    bar, bit = step // 16, step % 16
    o = 3584 + track * 8 + bar * 2
    v = (body[o] | (body[o + 1] << 8)) | (1 << bit)
    body[o], body[o + 1] = v & 0xFF, (v >> 8) & 0xFF
    return b


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port")
    ap.add_argument("--capture")
    ap.add_argument("--bpm", type=int, default=120)
    ap.add_argument("--track", type=int, default=2, help="0-6; 2 is Drum 3")
    ap.add_argument("--step", type=int, default=0)
    ap.add_argument("--seconds", type=float, default=9.0)
    a = ap.parse_args(argv)

    audio = _load("lxr_audio")
    lxr = audio.lxr
    dev = lxr.Device(a.port)
    cap = a.capture or audio.find_capture()

    slot, length, original = read_pattern(lxr, dev)
    body = memoryview(bytearray(original))[NAME_LEN:]
    track_len = body[3641 + a.track]
    bar_ms = round(60_000 / a.bpm / 4 * track_len)
    active = sum(1 for t in range(7) for s in range(64) if body[t * 512 + s * 7] & 0x80)
    print(f"pattern slot {slot}: {active} active steps, track {a.track} length {track_len}, "
          f"shuffle {body[3640]}, kit {body[3648]}")
    print(f"{a.bpm} bpm with a {track_len}-step track = a {bar_ms} ms bar")
    if active:
        print("  NOTE: the pattern already has active steps; this is not the reference project as built")

    stop = threading.Event()

    def clock():
        raw = os.open("/dev/snd/midiC%sD0" % dev.port.split(":")[1].split(",")[0], os.O_WRONLY)
        try:
            os.write(raw, bytes([0xFA]))
            period = 60.0 / (a.bpm * 24)
            nxt = time.monotonic() + period
            while not stop.is_set():
                now = time.monotonic()
                if now < nxt:
                    time.sleep(min(period / 4, max(nxt - now, 0)))
                    continue
                os.write(raw, bytes([0xF8]))
                nxt += period
        finally:
            os.write(raw, bytes([0xFC]))
            os.close(raw)

    try:
        write_pattern(lxr, dev, slot, length, one_trig(original, a.track, a.step))
        time.sleep(0.4)
        stop.clear()
        t = threading.Thread(target=clock, daemon=True)
        t.start()
        time.sleep(0.8)
        v, rate = audio.record(a.seconds, cap)
        stop.set()
        t.join(timeout=2)
    finally:
        stop.set()
        time.sleep(0.2)
        write_pattern(lxr, dev, slot, length, original)
        _, _, back = read_pattern(lxr, dev)
        same = bytes(back) == bytes(original)
        print(f"pattern restored: {'identical' if same else 'DIFFERS from what was read at the start'}")

    block = max(1, rate // 1000)
    x = np.abs(v[:, 0])
    n = len(x) // block * block
    e = 20 * np.log10(np.maximum(x[:n].reshape(-1, block).max(axis=1), 1e-12))
    floor = float(np.percentile(e, 5))
    thr = (floor + float(e.max())) / 2
    over = e > thr
    hits = [i for i in range(1, len(e)) if over[i] and not over[i - 1]]
    gaps = [hits[i + 1] - hits[i] for i in range(len(hits) - 1)]
    print()
    print(f"floor {floor:.1f} dBFS, loudest {e.max():.1f} dBFS, threshold {thr:.1f} dBFS")
    print(f"{len(hits)} hits at {hits} ms")
    print(f"  gaps {gaps} ms against a {bar_ms} ms bar "
          f"(worst error {max(abs(g - bar_ms) for g in gaps) if gaps else '-'} ms)")

    ok = True
    if len(hits) < 2:
        print("  FAIL: fewer than two hits, nothing to compare")
        return 1
    rows = []
    for i, h in enumerate(hits[:-1]):
        w = e[h:h + bar_ms]
        peak = float(w.max())
        below = np.where(w < peak - 40)[0]
        t40 = int(below[0]) if len(below) else None
        # Stop 50 ms short of the bar: a window exactly one bar long ends on the
        # next hit's onset, and then the tail is never quiet.
        tail = w[min(len(w) - 1, (t40 or 0) + 100):len(w) - 50]
        rows.append((peak, t40, float(tail.max()) if len(tail) else float("nan")))
    peaks = [r[0] for r in rows]
    t40s = [r[1] for r in rows if r[1] is not None]
    tails = [r[2] for r in rows]
    print()
    print(f"  peak        {np.mean(peaks):7.1f} dBFS   spread {max(peaks) - min(peaks):.1f} dB   {[round(p,1) for p in peaks]}")
    if t40s:
        print(f"  falls 40 dB {np.mean(t40s):7.0f} ms     spread {max(t40s) - min(t40s)} ms     {t40s}")
    else:
        print("  falls 40 dB  never inside the bar - the sound outlives it, so the decay is unmeasurable")
        ok = False
    print(f"  after it,   {np.mean(tails):7.1f} dBFS   {'quiet' if max(tails) <= floor + 10 else 'NOT QUIET'}"
          f"   ({max(tails) - floor:+.1f} dB over the floor at worst)")

    if max(peaks) - min(peaks) > 0.5:
        print("  FAIL: the peak is not repeatable to 0.5 dB")
        ok = False
    if max(tails) > floor + 10:
        print("  FAIL: something sounds between the hits")
        ok = False
    if gaps and max(abs(g - bar_ms) for g in gaps) > 10:
        print("  FAIL: the hits are not one bar apart")
        ok = False
    print()
    print("PASS: one trig gives one repeatable hit per bar and silence after it" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
