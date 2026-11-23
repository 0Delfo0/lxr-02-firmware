#!/usr/bin/env python3
"""The LXR-02's MIDI control surface, generated from docs/params.json.

  python3 tools/midi_map.py generate          # write docs/midi-map.md
  python3 tools/midi_map.py check             # assert the mapping rules only
  python3 tools/midi_map.py verify            # ask the device, write the json
  python3 tools/midi_map.py id PAR_VELOD3     # one parameter, by symbol

`generate` is deterministic and offline. `verify` needs the unit on USB with
SysEx enabled and answers the question the tables cannot: whether PARAM_GET
sees the change a CC or NRPN makes. That is not uniform - midiParser_ccHandler()
has one `case` per parameter and only some of them write activeParameters - and
not knowing it is why the issue #1 measurement had to be taken from audio.

The owner's manual (sections 9.7 and 9.8) has its own tables. It is Erica's and
lives in gitignored download/, so nothing is copied from it here; where it
disagrees with the firmware, the disagreement is recorded and the manual is
cited by section number.

SPDX-License-Identifier: GPL-3.0-only
"""

import argparse
import importlib.util
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PARAMS = os.path.join(ROOT, "docs", "params.json")
OUT_MD = os.path.join(ROOT, "docs", "midi-map.md")
OUT_JSON = os.path.join(ROOT, "docs", "midi-map-verify.json")

# CC2 numbers that are not parameters and so are absent from params.json. They
# are reached as NRPN numbers, and the CC2 switch acts on them directly.
CC2_EXTRAS = [
    (200, 206, "CC2_MUTE_1..7", "mute voice 1-7, value 0/1",
     "seq_setMute(); these are the ones that silence a voice. docs/params.json "
     "documents 1000-1006 instead, and those only write the bitmask the LEDs "
     "are drawn from - see issue #76. Measured: 200 takes a voice down "
     "62.5 dB, 1000 moves it 0.0 dB."),
    (210, 210, "NRPN_FX_FREEZE", "engage the freeze FX",
     "0 = release, 1..8 = a fixed rate, 9..127 = the rate from PAR_FX_P1. Not "
     "a parameter: handled before the CC2 path in midiParser_nrpnHandler()."),
]

SPECIAL_CC = [
    (0, "Bank Select MSB", "Selects a kit. The value **wraps onto the 64 "
        "kits**: 0-63 are the kits, 64 is kit 0 again and 127 is kit 63 "
        "(frontPanelParser.c, `% NUM_KIT_SLOTS`). Same behaviour as the "
        "official firmware from erica-synths/lxr-desktop-app dca0a44 on; "
        "before #74 a value above 63 was loaded anyway and read past "
        "mainParameters[64] into the edit buffer."),
    (1, "Modulation wheel", "Accepted and does nothing (`case CC_MOD_WHEEL` is empty)."),
    (6, "NRPN data entry", "The value byte of an NRPN message."),
    (98, "NRPN number LSB", "Low 7 bits of the NRPN number."),
    (99, "NRPN number MSB", "High 7 bits of the NRPN number."),
    (120, "All sound off", "Standard channel mode message."),
    (123, "All notes off", "Standard channel mode message."),
]


def load():
    with open(PARAMS) as f:
        return json.load(f)


def by_symbol(doc):
    return {p["symbol"]: p for p in doc["params"]}


def entries(doc, kind):
    out = [p for p in doc["params"] if (p.get("midi") or {}).get("type") == kind]
    out.sort(key=lambda p: p["midi"]["number"])
    return out


def check(doc):
    """The two arithmetic rules params.json states, asserted on every entry.

    A params.json that stops matching them must fail here rather than produce a
    document that is quietly wrong.
    """
    bad = []
    for p in entries(doc, "cc"):
        if p["midi"]["number"] != p["id"] + 1:
            bad.append(f"{p['symbol']}: CC {p['midi']['number']} but id {p['id']} (expected CC {p['id'] + 1})")
    for p in entries(doc, "nrpn"):
        if p["midi"]["number"] != p["id"] - 128:
            bad.append(f"{p['symbol']}: NRPN {p['midi']['number']} but id {p['id']} (expected NRPN {p['id'] - 128})")
    for kind in ("cc", "nrpn"):
        seen = {}
        for p in entries(doc, kind):
            n = p["midi"]["number"]
            if n in seen:
                bad.append(f"{kind.upper()} {n} claimed by both {seen[n]} and {p['symbol']}")
            seen[n] = p["symbol"]
    for p in entries(doc, "cc") + entries(doc, "nrpn"):
        if p.get("scope") != "sound":
            bad.append(f"{p['symbol']} is {p.get('scope')} scope but has a MIDI number")
    return bad


def voice_of(p):
    v = p.get("voice")
    return str(v) if v else "-"


def label(p):
    n = p.get("name") or {}
    cat, long = n.get("category", ""), n.get("long", "")
    text = f"{cat} {long}".strip() or n.get("short", "")
    return text or p["symbol"]


def table(rows, header):
    w = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    out = ["| " + " | ".join(str(h).ljust(w[i]) for i, h in enumerate(header)) + " |",
           "|" + "|".join("-" * (w[i] + 2) for i in range(len(header))) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c).ljust(w[i]) for i, c in enumerate(r)) + " |")
    return "\n".join(out)


def generate(doc, verify=None):
    sym = by_symbol(doc)
    v = verify or {}
    readable = v.get("readable", {})

    def read_col(p):
        k = str(p["id"])
        if k not in readable:
            return "?"
        return "yes" if readable[k] else "**no**"

    def rows(kind):
        return [[p["midi"]["number"], p["id"], p["symbol"], (p.get("name") or {}).get("short", ""),
                 label(p), voice_of(p), f"{p.get('min')}-{p.get('max')}", read_col(p)]
                for p in entries(doc, kind)]

    head = ["#", "param id", "symbol", "short", "name", "voice", "range", "PARAM_GET sees it"]
    cc_rows, nrpn_rows = rows("cc"), rows("nrpn")
    when = v.get("when", "not yet run")
    fw = v.get("firmware", "-")
    seen = sum(1 for k in readable if readable[k]) if readable else "?"
    total = len(readable) if readable else "?"

    doc_md = f"""# MIDI control surface of the LXR-02

**Generated by `tools/midi_map.py` from `docs/params.json`. Do not edit by hand** - regenerate with
`python3 tools/midi_map.py generate`. `params.json` is the mapping hosts are built against, so a change
there shows up here and nowhere else has to be kept in step.

Counts in this build: {len(cc_rows)} parameters on CC, {len(nrpn_rows)} on NRPN,
{doc['counts']['NUM_PARAMS']} parameters in total, of which {doc['counts']['END_OF_SOUND_PARAMETERS']}
are sound parameters.

## How a parameter is addressed

{chr(10).join('- **' + k + '**: ' + val for k, val in doc['midi_rules'].items() if k in ('cc', 'nrpn'))}

An NRPN message is three control changes in order: **CC 99** = number MSB, **CC 98** = number LSB,
**CC 6** = value (7 bit).

Panel edits are **not** transmitted as CC or NRPN by stock firmware
({doc['midi_rules'].get('panel_edits', '')!r}); to follow what the panel is doing, use the SysEx
parameter echo in [`sysex.md`](sysex.md).

## What `PARAM_GET` can and cannot see

**Every CC and NRPN in the tables below is visible to `PARAM_GET`: {seen} of {total}, measured.** Not for
the reason one would guess from `midiParser_ccHandler()`, which mostly calls the engine setter and leaves
`activeParameters` alone - `case VELOD3` calls `slopeEg2_setDecay()` and nothing else. An incoming CC is
**also forwarded to the front-panel parser** (`uart_sendFrontpanelByte`, `MidiParser.c:1925-1930`), and
`frontPanel_ccHandler()` is what writes `activeParameters`. The right-hand column is kept in the tables
because it is measured rather than assumed.

**What `PARAM_GET` cannot see is a parameter moved by the sequencer's automation lanes.**
`autoNode_setDestination()` and `autoNode_updateValue()` call `midiParser_ccHandler()` directly
(`automationNode.c:37,57`), with no front-panel forwarding, so the engine changes and `activeParameters`
does not. Measured on `PAR_VELOD3`: `CC 55 = 100` over MIDI reads back as 100; the automation lane then
writing 20 leaves `PARAM_GET` still reporting 100, and so does removing the automation again. **Anything
the automation lanes do has to be measured from the audio** - which is why the issue #1 measurement was
taken through an audio interface and not over SysEx.

Last verified: {when}, firmware `{fw}`.

## Special control change numbers

{table([[n, name, note] for n, name, note in SPECIAL_CC], ["CC", "meaning", "note"])}

## Numbers that are not parameters

Reached as NRPN numbers and acted on directly by the CC2 path, so they are absent from `params.json`:

{table([[f"{a}" if a == b else f"{a}-{b}", s, d, note] for a, b, s, d, note in CC2_EXTRAS],
       ["NRPN", "symbol", "does", "note"])}

## Traps that have cost time

- **Sync mode lives in `PAR_BPM`** (id 272), not in a setting of its own: **`0` = `EXT`** (the analog
  clock and reset jacks), **`1` = `MID`** (MIDI clock), anything else = that tempo internally
  (`CortexParser.c:648-662`). The owner's manual section 9.4 says `0` is MIDI clock sync, which is **wrong
  for this firmware**. MIDI start and stop are accepted in both external modes, but only `MID` advances
  steps from MIDI clock (`sequencer.c:465`), so a MIDI start under `EXT` leaves the sequencer on step 1
  and looks like a fault.
- **Writing `PAR_BPM` over SysEx does not change the sync mode.** `seq_setExtSync()` is called only from
  the front-panel path; the SysEx write stores the value and repaints the display. Measured three times.
  The mode has to be set on the panel: SHIFT + step 12, then pot 1.
- **The mute NRPNs that silence a voice are 200-206**, not the 1000-1006 `params.json` documents
  (issue #76).
- **`PAR_FX_DRY_WET = 0` does not turn the FX off.** Use `PAR_FX_TYPE` (id 234) and `PAR_FX_ROUTING`
  (id 235), both to 0. Measured: with dry/wet alone at 0 the output is unchanged.
- **Mod-target parameters hold an index, not a parameter number.** {doc['midi_rules'].get('mod_target_values', '')}
- **Per-voice parameter ids are not a fixed stride apart in a way worth guessing.** Look the symbol up -
  `python3 tools/midi_map.py id PAR_AMOUNT_LFO1` - rather than computing `base + n`. Doing the arithmetic
  from one voice's ids is how two LFOs went unnoticed through three measurements in the issue #1 session.

## Control change

{table(cc_rows, head)}

## NRPN

{table(nrpn_rows, head)}
"""
    return doc_md


# ---- device verification ---------------------------------------------------
def _lxr():
    spec = importlib.util.spec_from_file_location("lxr_test", os.path.join(HERE, "lxr_test.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def verify(doc, port=None, settle=0.06):
    """Send every CC and NRPN, and record whether PARAM_GET sees the change.

    Everything is restored from a snapshot taken first. PARAM_SET goes through
    midiParser_ccHandler() as well (ParameterArray.c:62), so restoring the
    parameter also restores the engine, not just the number.
    """
    lxr = _lxr()
    dev = lxr.Device(port)
    ident = lxr.identity(dev)
    print(f"firmware {ident['version_string']}, {ident['num_params']} params")

    items = [(p, "cc") for p in entries(doc, "cc")] + [(p, "nrpn") for p in entries(doc, "nrpn")]
    ids = [p["id"] for p, _ in items]
    before = lxr.get_params(dev, ids)
    print(f"snapshot of {len(before)} parameters taken")

    readable, mismatch = {}, []
    try:
        for n, (p, kind) in enumerate(items, 1):
            pid, num = p["id"], p["midi"]["number"]
            old = before[pid]
            lo, hi = p.get("min", 0), p.get("max", 127)
            # A value inside the parameter's own range and different from the
            # current one, so "unchanged" cannot be mistaken for "arrived".
            target = hi if old <= (lo + hi) // 2 else lo
            if target == old:
                target = old + 1 if old < hi else old - 1
            if kind == "cc":
                dev.raw([0xB0, num, target & 0x7F])
            else:
                dev.raw([0xB0, 99, (num >> 7) & 0x7F])
                dev.raw([0xB0, 98, num & 0x7F])
                dev.raw([0xB0, 6, target & 0x7F])
            time.sleep(settle)
            now = lxr.get_params(dev, [pid])[pid]
            seen = now != old
            readable[str(pid)] = seen
            if seen and now != target:
                mismatch.append((p["symbol"], kind, num, target, now))
            if n % 20 == 0 or n == len(items):
                print(f"  {n}/{len(items)}")
    finally:
        # Restore in batches; PARAM_SET takes several pairs per frame.
        pairs = [(pid, before[pid]) for pid in ids]
        for i in range(0, len(pairs), 8):
            lxr.set_params(dev, pairs[i:i + 8])
        after = lxr.get_params(dev, ids)
        wrong = [pid for pid in ids if after[pid] != before[pid]]
        print(f"restored: {'all parameters back to their snapshot values' if not wrong else f'{len(wrong)} NOT restored: {wrong[:10]}'}")

    yes = sum(1 for k in readable if readable[k])
    print(f"PARAM_GET sees the change for {yes} of {len(readable)} parameters")
    return {"when": time.strftime("%Y-%m-%d"), "firmware": ident["version_string"],
            "readable": readable,
            "scaled": [{"symbol": s, "kind": k, "number": n, "sent": t, "read": r}
                       for s, k, n, t, r in mismatch]}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("command", choices=["generate", "check", "verify", "id"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--port", help="LXR MIDI port, e.g. hw:2,0,0")
    a = ap.parse_args(argv)
    doc = load()

    bad = check(doc)
    if bad:
        print("docs/params.json does not match its own mapping rules:")
        for b in bad:
            print("  " + b)
        return 1
    if a.command == "check":
        print(f"mapping rules hold for all {len(entries(doc, 'cc'))} CC and "
              f"{len(entries(doc, 'nrpn'))} NRPN entries")
        return 0

    if a.command == "id":
        sym = by_symbol(doc)
        for name in a.args:
            p = sym.get(name)
            if not p:
                hits = [s for s in sym if name.upper() in s]
                print(f"{name}: no such symbol" + (f"; did you mean {hits[:6]}?" if hits else ""))
                continue
            m = p.get("midi") or {}
            how = (f"CC {m['number']}" if m.get("type") == "cc"
                   else f"NRPN {m['number']}" if m.get("type") == "nrpn" else "no CC/NRPN")
            print(f"{name}: id {p['id']}, {p.get('scope')} scope, range {p.get('min')}-{p.get('max')}, "
                  f"voice {voice_of(p)}, {how}")
        return 0

    if a.command == "verify":
        result = verify(doc, a.port)
        with open(OUT_JSON, "w") as f:
            json.dump(result, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"wrote {OUT_JSON}")

    v = None
    if os.path.exists(OUT_JSON):
        with open(OUT_JSON) as f:
            v = json.load(f)
    with open(OUT_MD, "w") as f:
        f.write(generate(doc, v))
    print(f"wrote {OUT_MD}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
