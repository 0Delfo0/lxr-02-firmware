# What OpenElixir-02 has already fixed, and what that means for us

A comparison of `tunefish777/openelixir-02` against this fork, item by item, against the firmware review
by jhw in [erica-synths/lxr-desktop-app#3](https://github.com/erica-synths/lxr-desktop-app/issues/3).
Written for issue #32. Every "present in our tree" line below that carries a `file:line` was checked in
this pass; the ones that say "per the issue" were not re-checked and are only as good as the issue.

## The two forks are siblings

```
git merge-base main openelixir/main   ->  a43cd4f  ==  upstream/main
upstream/main..openelixir/main        ->  33 commits
upstream/main..main                   ->  47 commits
```

So any file compares directly: `git diff upstream/main..openelixir/main -- <path>`.

Their fixes live in four commits, and how big each one is decides whether it can be reused:

| commit | what | size | reusable? |
|---|---|---|---|
| `8ad5946` | sound bugs: pitch EG slope 127, snare and compressor wrap | **19 insertions, 4 files** | yes, almost verbatim |
| `14a127f` | MIDI and sequencer bugs | 82 insertions, 8 files | mostly; one hunk touches their own `KitEngines.c` |
| `f600975` | memory corruption and out-of-bounds | 93 insertions, 8 files | mostly; `menu.c` is the biggest part |
| `ff93732` | SD card: write errors, safe project save, robust loading | **371 insertions, 398 deletions in `presetManager.c`** | **no**: it is a rewrite of the save path, with every write going through new `preset_fwrite`/`preset_fcloseWrite` helpers |

That last row is the practical headline. Their SD work is not a patch to lift, it is a restructure to adopt
whole or to reimplement — which is the choice we already made for #3 with `preset_writeExact()`.

## The blocking list, BL1 to BL14

| ours | jhw | in our tree | openelixir |
|---|---|---|---|
| **#1** BL1 NRPN/CC index | M1 | fixed by us, in `main` | **fixed** in `f600975` - *this corrects the table in issue #32, which said "fixed by nobody"* |
| **#2** BL2 `empty_directory` | — | fixed by us, in `main` | fixed |
| **#3** BL3 save ignores write results | P1, P3, P5, P6 | fixed by us, in `main` | **fixed** in `ff93732`, by the rewrite |
| **#4** BL4 SD has no timeout | P7 | **present, and narrower than the issue says**: `SD_TIMEOUT` *is* passed to the transfers (`FATFS/Target/sd_diskio.c:154,185`), but the waits that follow them are `while(BSP_SD_GetCardState() != MSD_OK)` with no bound (`:157,188`), and the file contains no `HAL_GetTick()` at all. Those two loops are the hang | **deliberately not fixed** - their `docs/KNOWN_ISSUES.md` lists P7 as a known limitation: "don't remove the card while it is being read or written" |
| **#5** BL5 global menu rewrites track lengths | U-B1 | fixed by us, in `main` | **fixed** in `14a127f`: `selectButtonMode = SELECT_MODE_MENU` instead of `SEQ_CONFIG`, whose numeric value collided with `SELECT_MODE_LAST_STEP_ALL` |
| **#6** BL6 pitch EG slope 127 | D1 | fixed by us, in `main` | **fixed** in `8ad5946`, with a host engine test for it |
| **#7** BL7 FatFs lock never released | — | **fixed by us**, in `main` at `caf2781`. One leak, in `preset_saveProject()`; the audit is on the issue. The "28 `f_open` against 43 `f_close`" that used to be here was `grep` over the whole file - the preprocessor says **11** live `f_open` in `presetManager.c` and **none** in `main.c`, and a count is not a verdict anyway, since both hand-over helpers open a file for their caller to close | **fixed** in `ff93732`: "one error path left a file open and blocked the card until a reboot". Same remedy as ours, and theirs is three weeks older |
| **#8** BL8 save targets the cursor slot | — | **present**, `buttonHandler.c:925` passes `menu_selectedPresetNr[SAVE_TYPE_KIT]` | **fixed** in `ff93732`: `menu_loadedPresetNr[CURRENT_KIT]`. A one-line change |
| **#9** BL9 two Note Ons per hit | M6 | **present**, `MidiVoiceControl.c` `voiceControl_noteOn()` still sends the note itself | **fixed** in `14a127f` by deleting that send, with the reason in a comment: the callers already send it after a note off, so every note went out twice and only the second got a note off |
| **#10** BL10 parser runs from the TIM2 ISR and the main loop | M4/M5 in part | per the issue | **not fixed as such.** They fixed the *symptom* - realtime bytes arriving mid-message corrupting it and MIDI thru - but their `midiParser_parseUartData()` has no reentrancy guard either. The two-context problem is still open in both trees |
| **#11** BL11 song starting with LOOP picks pattern 65 | S-A1, S-B7 | **present**, `sequencer.c:1348-1358` | **fixed** in `14a127f`: the test becomes `seq_pendingPattern >= MAX_PATTERN`, and the song end now also sends MIDI stop and tells the front panel, which used to keep showing "running" |
| **#12** BL12 USB MIDI TX writes past the buffer | M2, M12 | per the issue | **fixed** in `f600975`: the length was `(uint8_t)(Wr - Rd)`, which after a wrap sent up to 255 bytes from past the end; now it stops at the buffer end and retries when `CDC_Transmit_FS` is busy instead of dropping |
| **#13** BL13 parameter-lock slots never freed | U-B2, U-B3 | per the issue | **fixed** in `14a127f`, but the fix leans on their own `seq_automationTarget()` and `menu_getActiveVoice()`, so it is a model rather than a patch |
| **#14** BL14 per-voice MIDI channel off by one | M7 | **present**, `MidiParser.c:1938`: `midi_MidiChannels[voiceNr]` where `voiceNr` is 1-based | **fixed** in `14a127f`: `midi_MidiChannels[voiceNr-1]`, with the comment "(voiceNr is 1-based here, midi_MidiChannels 0-based)". One character |

## Items of ours they also fixed, outside the BL list

| ours | jhw | openelixir |
|---|---|---|
| **#15** RAT diode table read past its end | D5 | fixed in `f600975`, and a NaN there no longer survives until reboot |
| **#17** snare wraps instead of saturating | D2 | fixed in `8ad5946` |
| **#18** mono compressor has no saturation | D4 | fixed in `8ad5946` |
| **#21** BPM integer division | S-B5 | fixed in `14a127f` - **but incompletely.** They changed two sites to `(1000*60.f)`; `sequencer.c:1611` in their tree still reads `(1000*60)/seq_tempo`. Our tree has all three (`:385`, `:415`, `:1668`), so our fix should cover the third |
| **#24** new project copies the init kit off by 8 | P9 | fixed in `ff93732` |
| **#30** ping-pong delay indexes past `FX_BUFFER` | — | fixed in `796e9e2`, found by their host render test, and they state it is present in stock - so it applies to us |
| **#31** CortexParser uses 7-bit values as indices | — | fixed in `f600975`: "front panel messages used unchecked step, track and pattern numbers" |
| **#42** limiter and 32-bit summing | — | implemented in `7501ff3`, with a config switch for the stock mix |
| **#65** project save empties the destination first | P2 | fixed in `ff93732`: writes `/projNNn` and replaces `/projNN` only once everything is written |
| **#74** CC 0 above the last kit | K-1 | fixed in `14a127f` - and their fix also covers a second half we had not noticed: bank select did not update the *current* kit |

## Items they keep on purpose, which we should decide about too

Both are ours already, and their reasoning is worth having:

- **#66** wavetable reads one element past the row (D13). They keep it: wrapping to `(i+1) & 1023` would
  change the sound of existing kits. It only reads, nothing is overwritten.
- **#22** internal shuffle also shuffles the clock outputs (S-B6). Kept as stock, decided 2026-10-26.

## What they fixed and we have no issue for

Each of these is in their commit messages with a jhw ID, and none of them maps to an issue in our tracker:

| jhw | what | their commit |
|---|---|---|
| S-A2 | recording, rolls and flams used step index −1 while the sequencer was stopped | `f600975` |
| U-A1 | SHIFT + a held select button armed steps far past the 64 | `f600975` |
| U-A2, U-D | a menu value outside a name table's range read past the table; the waveform sample number wrote past its buffer | `f600975` |
| M13 | an FX type above the last one, reachable by modulation or NRPN, made the FX page read past its pages | `f600975` |
| K-1d | bank select and the three NRPN control CCs were recorded as step automation | `14a127f` |
| S-B7 | a song end did not send MIDI stop and left the front panel showing "running", so the next START press stopped instead of starting | `14a127f` |
| P4 | saving under a new name left the slot's old file behind, and loading could find it first | `ff93732` |
| P10 | pattern files were not read completely before replacing the pattern, and patterns from older firmware inherited the previous pattern's flams and shifts | `ff93732` |
| P11 | a damaged `glo.cfg` left the settings half overwritten; at boot the defaults were written to the card without being applied | `ff93732` |
| K-2, P8 | track lengths, kit and project numbers read from files were not range checked | `ff93732` |

## Pull requests: one fork reaches both repositories

`tunefish777/openelixir-02` is itself a fork of `erica-synths/lxr-desktop-app`:

```
gh repo view tunefish777/openelixir-02 --json isFork,parent
  {"isFork":true,"parent":{"name":"lxr-desktop-app","owner":{"login":"erica-synths"}}}
```

So it sits in the **same fork network** as our `0Delfo0/lxr-02-firmware`. Two consequences:

- We **cannot** fork `openelixir-02` as well: GitHub allows one fork per network per account, and ours is
  already spent on `TWildberger/lxr-02-firmware`.
- We do not need to. A pull request can be opened between any two repositories in one network, so the
  existing `github` remote is the vehicle for a PR to **either** `erica-synths/lxr-desktop-app` or
  `tunefish777/openelixir-02`, from a branch pushed to our fork.

The branch rule in `CLAUDE.md` is unchanged and matters more here: an upstream branch starts from
**`upstream/main`**, not from our `main`, or the PR drags 47 commits with it. For a PR aimed at
openelixir instead, branch from `openelixir/main`.

The clearest PR candidate against openelixir is the incomplete `S-B5` fix: one line,
`sequencer.c:1611`, with their own commit as the reference for why.

## Where we both fixed it: whose fix is better

Five defects were fixed by both forks. Read side by side rather than assumed (issue #92). Two of the five
have a verdict that costs us something.

### BL6 / D1, pitch EG slope 127 - **theirs is better, and ours is a sound regression**

| | |
|---|---|
| ours | `amount = ((data2/127.f)-0.5f)*1.999f` |
| theirs (`8ad5946`) | `amount = data2 < 127 ? ((data2/127.f)-0.5f)*2 : 0.5f*1.999f` |

Both stop the division by zero. The difference is what happens to the other 127 values: we changed the
scale factor for **every** value, so every kit's pitch EG slope is now slightly different from what it was
on stock firmware, while theirs is **bit-identical for 0 to 126** and special-cases only the value that
was broken. The error we introduced is small - at `data2 = 64` the amount moves by about 0.05 % - but it
is on every value and it is unnecessary.

**Action:** adopt their shape. Issue #93.

This is also the clearest argument for porting their `test/engine` harness: a bit-exact A/B render of the
engine would have failed the moment our version changed a value it did not have to.

### BL1 / M1, the CC and NRPN index - **ours is better, and theirs still has the worse half**

Both bound the `MIDI_CC2` write. Ours keeps a 256-entry array and bounds the write; theirs keeps the
255-entry array and bounds the write with `paramNr < sizeof(midiParser_originalCcValues)`. Equivalent.

But the defect has two halves, and **their `MIDI_CC` path is untouched**
(`openelixir/main:Core/Src/Soundengine/MIDI/MidiParser.c:166-169`):

```c
const uint16_t paramNr = msg.data1-1;
if(updateOriginalValue && !isFloatModulator) {
    midiParser_originalCcValues[paramNr+1] = value.itg;
}
```

For `msg.data1 == 0` - CC 0, bank select, which DAWs and controllers send routinely - `paramNr`
underflows to `0xFFFF` and `paramNr+1` promotes to **65536**: a write 64 KB past a 255-byte array. That
statement is character for character the one our tree carried, and UndefinedBehaviorSanitizer reported it
on our tree at that line as *"index 65536 out of bounds for type 'uint8_t [255]'"*. We have not run a
sanitizer on their tree, so this is a reading plus our own sanitizer output for identical code, not a
direct observation of theirs.

**Action:** report it to them. Issue #94.

### BL2, `empty_directory` - equivalent, ours is slightly more robust

Theirs defines `EMPTY_DIR_PATH_LEN 64` and checks `i + sizeof(fno.fname) > EMPTY_DIR_PATH_LEN`; ours
passes the buffer size as a parameter and derives it from the buffer with `sizeof`. Both close the
overflow. Theirs is shorter; ours removes the hidden contract that the caller's buffer must match a
constant declared next to the function. No action.

### BL5 / U-B1, the global menu rewriting track lengths - identical

Both set `selectButtonMode = SELECT_MODE_MENU` in place of `SEQ_CONFIG`, whose numeric value collided
with `SELECT_MODE_LAST_STEP_ALL`. Same line, same value, arrived at independently. No action.

### BL3, the save path - **different scope, not better or worse**

Ours checks the result of every write and adds `preset_writeExact()` for the FatFs behaviour of
reporting a full card as a short write with `FR_OK`. Theirs rewrites the save path so that every write
goes through `preset_fwrite`/`preset_fcloseWrite`, and in the same commit also fixes P2, P4, P9, P10, P11
and K-2/P8.

So their commit covers far more ground, but as one 740-line change. Ours covers the defect this issue was
about and leaves the rest tracked as #65, #69, #73 and #88 to #91. Comparing them as "better" or "worse"
is the wrong question; the real one is whether our targeted fix covers the cases theirs does, and the
answer is that it covers the write-result case and not the other six, which is why those six now have
issues. No action beyond those.

## What this changes about our plan

1. **#1's "fixed by nobody" was wrong**, and that mattered: it was one of the two items picked as best
   upstream-PR candidates on the grounds that nobody had solved it. M1 is solved in openelixir.
2. **#4 is the only BL item nobody fixes**, and openelixir has written down a decision not to. That is an
   argument for treating it as a documented limitation rather than work.
3. **#9, #11 and #14 are small and have a readable GPL-3 solution each** - one deletion, one bounds
   change, one off-by-one. They are the cheapest correctness wins left on the blocking list.
4. **#7, #8, #13, #24, #65 and the whole SD group** have solutions entangled with a 740-line rewrite of
   `presetManager.c`. Reimplementing against their design, as we did for #3, is the realistic route.
5. **Ten defects they fixed have no issue here yet** (the table above), which is the real yield of this
   comparison.
