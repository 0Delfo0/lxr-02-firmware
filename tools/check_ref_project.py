#!/usr/bin/env python3
"""Read the device back and diff it against the reference project's manifest.

  python3 tools/check_ref_project.py            # the kit in the edit buffer and the globals
  python3 tools/check_ref_project.py --kit 5    # load kit slot 5 first, then check

Run it after loading the reference project. The expected values come from the
same rule tables tools/make_ref_project.py writes the files from, so this
compares the device against the repository and not against a second opinion.

SPDX-License-Identifier: GPL-3.0-only
"""

import argparse
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# PAR_BPM carries the sync mode, and the mode can only be set on the panel:
# writing it anywhere else does not call seq_setExtSync(). A test that wants
# the sequencer clocked from the laptop therefore has 1 (`MID`) here on
# purpose, so a difference on this id is reported and not counted as a failure.
BY_HAND = {272}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port")
    ap.add_argument("--kit", type=int, help="load this kit slot before checking")
    ap.add_argument("--quiet", action="store_true", help="only print what differs")
    a = ap.parse_args(argv)

    ref = _load("make_ref_project")
    lxr = _load("lxr_test")
    doc = ref.load()
    sv, miss_s = ref.sound_values(doc)
    gv, miss_g = ref.global_values(doc)
    if miss_s or miss_g:
        print("the rule tables do not cover every parameter; fix make_ref_project.py first")
        return 2

    dev = lxr.Device(a.port)
    ident = lxr.identity(dev)
    st = lxr.status(dev)
    print(f"firmware {ident['version_string']}, kit slot {st['kit_slot']}, pattern {st['pattern']}, "
          f"running {st['running']}")
    if a.kit is not None:
        dev.request(lxr.CMD["KIT_LOAD"], [a.kit], expect=lxr.CMD["ACK"])
        time.sleep(0.4)
        print(f"loaded kit slot {a.kit}")

    bad = []
    for title, want in (("sound parameters", sv), ("global settings", gv)):
        ids = sorted(want)
        got = lxr.get_params(dev, ids)
        diffs = [(pid, want[pid][0], got[pid], want[pid][2]["symbol"], want[pid][1])
                 for pid in ids if got[pid] != want[pid][0]]
        print(f"{title}: {len(ids) - len(diffs)}/{len(ids)} match the manifest")
        for pid, exp, act, symbol, why in diffs:
            note = "  <- set by hand, expected to differ" if pid in BY_HAND else ""
            print(f"    id {pid:>4} {symbol:<26} manifest {exp:>4}  device {act:>4}{note}")
            if not note:
                print(f"         ({why[:88]})")
        bad += [d for d in diffs if d[0] not in BY_HAND]

    # The things the measurement actually depends on, called out separately so a
    # pass is not buried in a long list.
    critical = {}
    for p in doc["params"]:
        n = p.get("name") or {}
        if (n.get("category"), n.get("short")) in (("LFO", "mod"), ("LFO", "snc"), ("LFO", "dst"),
                                                   ("Velocity", "amt"), ("Velocity", "vol"),
                                                   ("Fx", "typ"), ("Voice", "out"),
                                                   ("Veloc EG", "rpt"), ("Transnt", "vol")):
            critical[p["id"]] = p
    got = lxr.get_params(dev, sorted(critical))
    wrong = [(pid, sv[pid][0], got[pid], critical[pid]["symbol"]) for pid in sorted(critical)
             if got[pid] != sv[pid][0]]
    print()
    print(f"nothing-modulates-anything check: {len(critical) - len(wrong)}/{len(critical)} "
          f"({'clean' if not wrong else 'NOT CLEAN'})")
    for pid, exp, act, symbol in wrong:
        print(f"    {symbol:<26} expected {exp}, device {act}")

    print()
    print("PASS: the device matches the manifest" if not bad else
          f"FAIL: {len(bad)} parameters differ from the manifest")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
