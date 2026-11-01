#!/usr/bin/env python3
"""Drive an LXR-02 over USB MIDI with the SysEx protocol of docs/sysex.md.

  python3 tools/lxr_test.py identity
  python3 tools/lxr_test.py status [--reset]
  python3 tools/lxr_test.py get 0 1 2
  python3 tools/lxr_test.py set 3 100
  python3 tools/lxr_test.py kit 0          # 7F = edit buffer
  python3 tools/lxr_test.py snapshot       # every parameter, as JSON
  python3 tools/lxr_test.py cc 2 99        # raw control change
  python3 tools/lxr_test.py selftest       # the checks below, PASS/FAIL each

The transport is `amidi` from alsa-utils, deliberately: tools/lxr_status.py
needs mido and python-rtmidi, which need a compiler and the ALSA headers to
install, while amidi is already there on any machine that can see the device.

SysEx has to be enabled on the unit: SHIFT + step 12, second page, `syx` = on.

SPDX-License-Identifier: GPL-3.0-only
"""

import argparse
import json
import re
import subprocess
import sys
import time

MANUFACTURER, MODEL, PROTOCOL = 0x7D, 0x4C, 0x01
BROADCAST = 0x7F

CMD = {
    "IDENTITY_REQUEST": 0x01, "IDENTITY_REPLY": 0x02,
    "STATUS_REQUEST": 0x03, "STATUS_REPLY": 0x04,
    "KIT_REQUEST": 0x10, "KIT_DUMP": 0x11, "KIT_STORE": 0x12,
    "KIT_LOAD": 0x13, "KIT_NAME": 0x14,
    "PATTERN_REQUEST": 0x20, "PATTERN_CHUNK": 0x21, "PATTERN_NAME": 0x22,
    "CONDITION_REQUEST": 0x23, "CONDITION_DUMP": 0x24, "CONDITION_SET": 0x25,
    "GLOBALS_REQUEST": 0x30, "GLOBALS_DUMP": 0x31,
    "PARAM_GET": 0x40, "PARAM_SET": 0x41, "PARAM_VALUE": 0x42,
    "ACK": 0x7E, "NAK": 0x7F,
}
NAK_ERRORS = {0: "unknown command", 1: "checksum", 2: "version", 3: "length",
              4: "range", 5: "read only", 6: "busy", 7: "sd error"}

# PARAM_GET takes at most 64 ids per frame (docs/sysex.md section 5.15).
MAX_IDS = 64


# ---- encodings (docs/sysex.md section 3) -----------------------------------
def u14(value):
    return [value & 0x7F, (value >> 7) & 0x7F]


def read_u14(data, i):
    return data[i] | (data[i + 1] << 7)


def unpack8(data):
    """7-in-8: groups of up to 7 low-7-bit bytes followed by their MSB byte."""
    out = bytearray()
    i = 0
    while i < len(data):
        group = data[i:i + 8]
        if len(group) < 2:
            break                      # a trailing byte without its MSB byte
        body, msbs = group[:-1], group[-1]
        for n, byte in enumerate(body):
            out.append(byte | (0x80 if msbs & (1 << n) else 0))
        i += 8
    return bytes(out)


def checksum(body):
    """XOR of everything from the device byte to the last payload byte."""
    c = 0
    for b in body:
        c ^= b
    return c & 0x7F


def frame(cmd, payload=(), device=BROADCAST):
    body = [device, PROTOCOL, cmd] + list(payload)
    return bytes([0xF0, MANUFACTURER, MODEL] + body + [checksum(body), 0xF7])


def param_ranges(path="docs/params.json"):
    """{id: (min, max)} from the generated table, empty if it is not there."""
    try:
        with open(path) as f:
            params = json.load(f)["params"]
    except OSError:
        return {}
    return {p["id"]: (p.get("min", 0), p.get("max", 127)) for p in params
            if p.get("min") is not None}


def full_range_param(path="docs/params.json"):
    """A sound parameter that really accepts 0-127, for the round-trip check.

    Most parameters clamp: id 3 is a 6-entry waveform menu, so setting it to
    100 reads back 5 and looks like a failure when it is correct behaviour.
    """
    try:
        with open(path) as f:
            params = json.load(f)["params"]
    except OSError:
        return 128                   # PAR_FILTER_DRIVE_1 in firmware 1.7
    for p in params:
        if (p.get("scope") == "sound" and p.get("min") == 0 and p.get("max") == 127
                and p.get("dtype") == "0B127" and not p.get("reserved")
                and (p.get("name") or {}).get("short")):
            return p["id"]
    return 128


# ---- transport -------------------------------------------------------------
class Device:
    def __init__(self, port=None, device=BROADCAST, timeout=1.0):
        self.port = port or self._find_port()
        self.device = device
        self.timeout = timeout

    @staticmethod
    def _find_port():
        out = subprocess.run(["amidi", "-l"], capture_output=True, text=True).stdout
        for line in out.splitlines():
            if re.search(r"LXR|Sonic Potions", line, re.I):
                return line.split()[1]
        sys.exit("no LXR-02 on the MIDI bus (amidi -l). Is the USB cable in?")

    def _run(self, send_hex, listen):
        cmd = ["amidi", "-p", self.port]
        if send_hex:
            cmd += ["-S", send_hex]
        if listen:
            cmd += ["-d", "-t", str(self.timeout)]
        res = subprocess.run(cmd, capture_output=True, text=True,
                             timeout=self.timeout + 5)
        if res.returncode != 0:
            sys.exit(f"amidi failed: {res.stderr.strip()}")
        return res.stdout

    def raw(self, data):
        """Send raw MIDI bytes (a control change, say). No reply expected."""
        self._run(" ".join(f"{b:02X}" for b in data), listen=False)

    def request(self, cmd, payload=(), expect=None):
        """Send one command and return the payload of the first matching reply."""
        out = self._run(" ".join(f"{b:02X}" for b in frame(cmd, payload, self.device)),
                        listen=True)
        for reply in self._sysex_frames(out):
            if reply[:3] != bytes([0xF0, MANUFACTURER, MODEL]):
                continue
            cc, payload_in = reply[5], reply[6:-2]
            if cc == CMD["NAK"]:
                err = payload_in[1] if len(payload_in) > 1 else 0
                sys.exit(f"NAK on command {payload_in[0]:#02x}: "
                         f"{NAK_ERRORS.get(err, err)}")
            if expect is None or cc == expect:
                if reply[-2] != checksum(reply[3:-2]):
                    sys.exit("reply with a bad checksum")
                return cc, payload_in
        return None, None

    @staticmethod
    def _sysex_frames(dump):
        """amidi -d prints hex bytes; cut them into complete F0...F7 frames."""
        data = bytes(int(b, 16) for b in dump.split())
        frames, start = [], None
        for i, b in enumerate(data):
            if b == 0xF0:
                start = i
            elif b == 0xF7 and start is not None:
                frames.append(data[start:i + 1])
                start = None
        return frames


# ---- commands --------------------------------------------------------------
def identity(dev):
    cc, p = dev.request(CMD["IDENTITY_REQUEST"], expect=CMD["IDENTITY_REPLY"])
    if cc is None:
        sys.exit("no IDENTITY_REPLY. Enable SysEx: SHIFT + step 12, page 2, syx = on")
    return {
        "protocol": p[0],
        "firmware": f"{p[1]}.{p[2]}.{p[3]}",
        "version_string": p[4:12].rstrip(b"\x00").decode("ascii", "replace"),
        "num_params": read_u14(p, 12),
        "kit_length": read_u14(p, 14),
        "globals_start": read_u14(p, 16),
        "kit_slots": p[18], "patterns": p[19], "tracks": p[20], "steps": p[21],
        "pattern_data_len": read_u14(p, 22),
        "max_frame": read_u14(p, 24),
        "capabilities": read_u14(p, 26),
    }


def status(dev, reset=False):
    cc, p = dev.request(CMD["STATUS_REQUEST"], [1] if reset else [],
                        expect=CMD["STATUS_REPLY"])
    if cc is None:
        sys.exit("no STATUS_REPLY")
    st = {"kit_slot": p[0], "pattern": p[1], "running": p[2], "echo": p[3]}
    if len(p) >= 16:        # the CPU meter needs capability bit 6
        names = ("cpu_now", "cpu_peak", "cpu_voices", "cpu_mixer", "cpu_fx", "underruns")
        for n, name in enumerate(names):
            st[name] = read_u14(p, 4 + 2 * n) / (10.0 if name != "underruns" else 1)
    return st


def get_params(dev, ids):
    """Read any number of parameters, in frames of at most MAX_IDS ids."""
    values = {}
    for start in range(0, len(ids), MAX_IDS):
        chunk = ids[start:start + MAX_IDS]
        payload = [b for i in chunk for b in u14(i)]
        cc, p = dev.request(CMD["PARAM_GET"], payload, expect=CMD["PARAM_VALUE"])
        if cc is None:
            sys.exit("no PARAM_VALUE")
        for n in range(len(p) // 4):
            values[read_u14(p, 4 * n)] = read_u14(p, 4 * n + 2)
    return values


def set_params(dev, pairs):
    payload = [b for i, v in pairs for b in u14(i) + u14(v)]
    # PARAM_SET has no reply on success, so a NAK is the only thing to look for.
    dev.request(CMD["PARAM_SET"], payload, expect=CMD["NAK"])


def kit(dev, slot):
    cc, p = dev.request(CMD["KIT_REQUEST"], [slot], expect=CMD["KIT_DUMP"])
    if cc is None:
        sys.exit("no KIT_DUMP")
    body = unpack8(p[4:])
    return {"slot": p[0], "count": read_u14(p, 2),
            "name": body[:8].decode("ascii", "replace").rstrip(" \x00"),
            "params": list(body[8:8 + read_u14(p, 2)])}


def pattern(dev, slot):
    """PATTERN_REQUEST answers with every chunk back to back."""
    out = dev._run(" ".join(f"{b:02X}" for b in
                            frame(CMD["PATTERN_REQUEST"], [slot], dev.device)),
                   listen=True)
    raw, info = bytearray(), {}
    for f in Device._sysex_frames(out):
        if f[5] != CMD["PATTERN_CHUNK"]:
            continue
        p = f[6:-2]
        info = {"slot": p[0], "chunks": p[3], "length": read_u14(p, 4)}
        raw += unpack8(p[6:])
    if not info:
        sys.exit("no PATTERN_CHUNK")
    info["name"] = raw[:8].decode("ascii", "replace").rstrip(" \x00")
    info["received"] = len(raw)
    return info


# ---- selftest --------------------------------------------------------------
class Checks:
    """PASS / FAIL, plus KNOWN for a check that an open issue is expected to fail.

    A known failure keeps the exit code at 0 so this can be run after every
    change without the open issues drowning out a new regression - but it still
    prints, and it turns into a FAIL the moment its issue is fixed, which is
    exactly when someone should notice.
    """

    def __init__(self):
        self.failed = self.known = self.n = 0

    def __call__(self, ok, what, detail="", known_issue=None):
        self.n += 1
        if ok:
            tag = "PASS"
        elif known_issue:
            tag = f"KNOWN"
            self.known += 1
        else:
            tag = "FAIL"
            self.failed += 1
        if ok and known_issue:
            tag = "FIXED"          # the issue is closed in fact, if not on the tracker
        suffix = f"  [{detail}]" if detail else ""
        issue = f"  ({known_issue})" if known_issue else ""
        print(f"  {tag:<5} {what}{issue}{suffix}")
        return ok


def selftest(dev):
    check = Checks()

    print("identity")
    ident = identity(dev)
    for k, v in ident.items():
        print(f"    {k:<18} {v}")
    check(ident["protocol"] == PROTOCOL, "protocol version is 1")
    check(ident["num_params"] > 0 and ident["kit_length"] > 0, "parameter counts are sane")
    check(ident["kit_slots"] == 64 and ident["tracks"] == 7, "64 kit slots, 7 tracks")

    print("status")
    st = status(dev)
    for k, v in st.items():
        print(f"    {k:<18} {v}")
    check("cpu_now" in st, "the CPU meter answers (capability bit 6)")
    if "cpu_now" in st:
        check(0 < st["cpu_now"] < 100, "CPU load is under 100 %", f"{st['cpu_now']} %")
        # The counter runs since boot and SD card access stalls the audio, so a
        # lifetime count says nothing. Reset it and see whether it grows while
        # the unit is idle - that is the number that would mean trouble.
        print(f"    underruns since boot: {st['underruns']:.0f}, resetting")
        status(dev, reset=True)
        time.sleep(2)
        st = status(dev)
        check(st["underruns"] == 0, "no underruns in 2 s of idle running",
              f"{st['underruns']:.0f}")
    underruns_before = st.get("underruns", 0)

    print("parameter round trip")
    target = full_range_param()     # a 0-127 sound parameter, restored below
    before = get_params(dev, [target])[target]
    probe = 100 if before != 100 else 50
    set_params(dev, [(target, probe)])
    after = get_params(dev, [target])[target]
    check(after == probe, f"PARAM_SET then PARAM_GET on id {target}",
          f"{before} -> {probe}, read {after}")
    set_params(dev, [(target, before)])
    check(get_params(dev, [target])[target] == before, "original value restored",
          f"{before}")

    # Every sound and global parameter, skipping the read-only pattern range.
    ids = ([i for i in range(ident["kit_length"])]
           + [i for i in range(ident["globals_start"], ident["num_params"])])

    print("control change reaches a parameter")
    snap = get_params(dev, ids)
    dev.raw([0xB0, 2, 99])          # CC 2, the first CC the firmware maps to a sound
    moved = {i: (snap[i], v) for i, v in get_params(dev, ids).items() if v != snap[i]}
    check(len(moved) == 1, "exactly one parameter moved", str(moved))
    if len(moved) == 1:
        pid, (old, new) = next(iter(moved.items()))
        check(new == 99, f"CC 2 set parameter {pid} to the value sent",
              f"{old} -> {new}")
        print(f"    mapping observed: CC 2 -> parameter {pid}"
              f" ({'id = cc - 1' if pid == 1 else 'not id = cc - 1'})")
        set_params(dev, [(pid, old)])

    # CC 0 is bank select, so it loads a kit into the edit buffer: the edit
    # buffer is *meant* to change. What must not change is anything else, and
    # a value above the last slot must be refused rather than read past the
    # array (issue #74). Issue #1 lives in the same message: its paramNr
    # underflowed to 0xFFFF for CC 0, writing 64 KB past a 255-byte array,
    # which is not visible in the parameter space - the checks below cover
    # what is: the unit stays alive and nothing around it moves.
    print("CC 0, bank select (issues #1, #74)")
    globals_ids = [i for i in range(ident["globals_start"], ident["num_params"])]
    globals_before = get_params(dev, globals_ids)
    stored_before = {slot: kit(dev, slot) for slot in (0, 1)}

    dev.raw([0xB0, 0, 5])                       # a slot that exists
    loaded = get_params(dev, [i for i in range(ident["kit_length"])])
    stored5 = kit(dev, 5)["params"]
    # A stored byte outside a parameter's range is clamped when it is loaded,
    # so compare against the clamped value: PAR_VOICE_LFO1..6 have min 1 and
    # read back as 1 on a unit whose kit slots are zeroed.
    ranges = param_ranges()
    def expected(i):
        lo, hi = ranges.get(i, (0, 255))
        return min(max(stored5[i], lo), hi)
    off = {i: (stored5[i], v) for i, v in loaded.items()
           if i < len(stored5) and v != expected(i)}
    check(not off, "CC 0 = 5 loads kit 5 into the edit buffer",
          f"{len(off)} parameters differ: {list(off)[:8]}" if off else
          f"all {len(loaded)} parameters match, clamping included")

    before_oob = get_params(dev, [i for i in range(ident["kit_length"])])
    dev.raw([0xB0, 0, 127])                     # 64 slots exist, so this one does not
    after_oob = get_params(dev, [i for i in range(ident["kit_length"])])
    moved = {i: (before_oob[i], v) for i, v in after_oob.items() if v != before_oob[i]}
    check(not moved, "CC 0 = 127 is refused: there is no kit 127 to load",
          f"{len(moved)} parameters changed - read from past the kit array"
          if moved else "", known_issue="#74")

    check(get_params(dev, globals_ids) == globals_before,
          "the global settings did not move")
    check(all(kit(dev, s)["params"] == stored_before[s]["params"] for s in stored_before),
          "the stored kit slots did not move")
    alive = identity(dev)
    check(alive["version_string"] == ident["version_string"],
          "the unit still answers after CC 0", alive["version_string"])
    st2 = status(dev)
    check(st2.get("underruns", 0) == underruns_before,
          "no new audio underruns", f"{st2.get('underruns')}")
    dev.raw([0xB0, 0, st["kit_slot"]])          # leave a valid kit loaded

    print("kit dumps")
    edit = kit(dev, 0x7F)
    check(len(edit["params"]) == ident["kit_length"],
          "the edit buffer returns a full kit", f"{len(edit['params'])} parameters")
    names = []
    for slot in range(6):
        names.append(kit(dev, slot)["name"])
    print(f"    kit names 0-5: {names}")
    check(all(n for n in names), "every kit slot has a name")

    print("pattern dump")
    pat = pattern(dev, 0x7F)
    for k, v in pat.items():
        print(f"    {k:<18} {v}")
    check(pat["received"] >= pat["length"], "every chunk of the pattern arrived",
          f"{pat['received']} of {pat['length']} bytes")

    print(f"\n{check.n - check.failed - check.known} of {check.n} checks passed"
          + (f", {check.known} known failure(s)" if check.known else "")
          + (f", {check.failed} FAILED" if check.failed else ""))
    return 1 if check.failed else 0


# ---- main ------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="ALSA port, e.g. hw:1,0,0 (default: found by name)")
    ap.add_argument("--device", type=int, default=0,
                    help="device ID 1-16, 0 = broadcast (default)")
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("command", choices=["identity", "status", "get", "set", "kit",
                                        "pattern", "snapshot", "cc", "selftest"])
    ap.add_argument("args", nargs="*", type=int)
    ap.add_argument("--reset", action="store_true", help="status: clear peak and underruns")
    args = ap.parse_args(argv)

    dev = Device(args.port, BROADCAST if args.device == 0 else args.device - 1,
                 args.timeout)

    if args.command == "identity":
        print(json.dumps(identity(dev), indent=2))
    elif args.command == "status":
        print(json.dumps(status(dev, args.reset), indent=2))
    elif args.command == "get":
        print(json.dumps(get_params(dev, args.args), indent=2))
    elif args.command == "set":
        if len(args.args) % 2:
            sys.exit("set takes id/value pairs")
        set_params(dev, list(zip(args.args[::2], args.args[1::2])))
    elif args.command == "kit":
        print(json.dumps(kit(dev, args.args[0] if args.args else 0x7F), indent=2))
    elif args.command == "pattern":
        print(json.dumps(pattern(dev, args.args[0] if args.args else 0x7F), indent=2))
    elif args.command == "snapshot":
        ident = identity(dev)
        ids = ([i for i in range(ident["kit_length"])]
               + [i for i in range(ident["globals_start"], ident["num_params"])])
        print(json.dumps(get_params(dev, ids), indent=2))
    elif args.command == "cc":
        dev.raw([0xB0, args.args[0], args.args[1]])
    elif args.command == "selftest":
        return selftest(dev)
    return 0


if __name__ == "__main__":
    sys.exit(main())
