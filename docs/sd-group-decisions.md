# The SD group: the decisions to take before writing any of it

Fourteen open issues touch the SD card save and load paths: **#4, #7, #8, #24, #65, #69, #70, #72, #73,
#88, #89, #90, #91** plus **#3**, which is already in `main`. They are not fourteen independent fixes.
Most of them live in one function, several of them interact, and two of them are made unreachable by a
third. So the order and the shape have to be decided once, not discovered one issue at a time.

Everything below was read in the source or measured; `file:line` is given so each claim can be checked.

## They are nearly all in one function

`preset_saveProject()` (`Core/Src/Preset/presetManager.c:2459`) contains, in the order it runs:

| line, relative | what | issue |
|---|---|---|
| 6 | `preset_saveDrumsetRam(menu_selectedPresetNr[SAVE_TYPE_KIT], 0)` - the **cursor** slot, not the kit being edited | **#8** |
| 61 | `empty_directory(...)` - the destination is emptied **before anything is written** | **#65** |
| 113 | `f_write(... prj.nfo ...)` with only `res != FR_OK` checked, so a short write passes | **#73** |
| 130 | `preset_saveGlobals();` - **result discarded** | **#69** |
| 138, 151 | per-kit and per-pattern saves, checked, but they return **after** the folder was emptied | **#65** again |

So a card that fills up half way through leaves the destination project destroyed and the new one
incomplete, and says `Disk error!` or nothing depending on which write failed.

## One piece of good news about the budget

`presetManager.c` has **one** `IN_ITCM_RAM` function, `preset_sendDrumsetParametersImmediate()`
(`:1370`), and it is not on the save path. **This whole group costs flash, not ITCM.** Flash is at
260,772 of 992,256 bytes; ITCM, which has 464 bytes free and is the constraint that has bitten three
times this week, is not involved. That removes the usual reason to keep a fix minimal, and it means the
right shape can be chosen on its merits.

---

## D1. Take their rewrite, or take their design?

`ff93732` in `tunefish777/openelixir-02` changes **371 lines and deletes 398** in `presetManager.c` alone.
It fixes P1, P2, P3, P4, P5, P6, P9, P10, P11, K-2 and P8 in one commit.

- **(a) Adopt the diff.** It is proven in their tree and would close most of this group at once. But our
  tree has diverged where it matters: `preset_writeExact()` from #3, the `EngV01` engine tag, the 168-byte
  trig condition block, and the `empty_directory(path, size)` signature from #2. The diff would conflict
  in all four places, and the result would be one unreviewable commit whose hardware test is "the whole SD
  path still works".
- **(b) Take the design, write the code.** Each piece lands as its own issue with its own test and its own
  measured cost, and the commit messages cite their commit for the idea - route (a) of the `CLAUDE.md`
  "Sources" rule.

**Recommendation: (b).** Not for licence reasons - both trees are GPL-3 and the code may simply be copied -
but because a 740-line change we cannot test as a whole is worse than six changes we can. The one place to
reconsider is D6, where the solution genuinely *is* a body of code.

## D2. Write errors: a sticky accumulator, or a check at every call?

Theirs (`presetManager.c:128-150`):

```c
static FRESULT preset_fopenWrite(const char* path)
{   preset_writeResult = f_open(...); return preset_writeResult; }

static FRESULT preset_fwrite(FIL* fp, const void* buf, UINT btw, UINT* bw)
{
    if(preset_writeResult != FR_OK) return preset_writeResult;   // already failed: write nothing
    preset_writeResult = f_write(fp, buf, btw, bw);
    if(preset_writeResult == FR_OK && *bw != btw) preset_writeResult = FR_DENIED;  // card full
    return preset_writeResult;
}

static FRESULT preset_fcloseWrite(void)
{   const FRESULT res = f_close(...); return preset_writeResult != FR_OK ? preset_writeResult : res; }
```

A save becomes write, write, write, **check once at the end**. The flag is per-file and reset by
`preset_fopenWrite()`.

Ours is `preset_writeExact()` (`:536`), which returns the error and relies on every call site checking it.
**Measured: 19 call sites use it, and `presetManager.c` contains 22 `f_write` calls.** Three writes still
go unchecked, which is exactly what #73 is.

- **(a) Keep per-call checks** and fix the three stragglers. Minimal, explicit, and the next write added by
  anyone re-opens the hole.
- **(b) Adopt the sticky accumulator.** It turns "every call site must remember" into "one check at the
  end", and a forgotten site cannot do damage because writes stop after the first failure.

**Recommendation: (b).** It closes #73 and stops it recurring, which a per-site fix cannot. The cost is one
module-level variable, so the rule that comes with it is that it is reset on open and that no two saves
can be in flight - which is already true, the UI is blocking during a save.

## D3. What error code is a short write? And it is coupled to #70

A full card shows up as `FR_OK` with fewer bytes written than asked - FatFs reports it that way, which is
why #3 exists at all.

- Ours maps it to **`FR_DISK_ERR`**, and the screen says `Disk error!`. That is the message Davide saw.
- Theirs maps it to **`FR_DENIED`**, which is truthful: the card is full, the disk is fine.

**But `menu_showSdError()` names only four codes, and it is the same four in both trees** - measured:
`FR_NO_FILESYSTEM`, `FR_DISK_ERR`, `FR_NOT_READY`, `FR_NO_FILE`, everything else falls to `unknown!`. So
**`FR_DENIED` without extending that switch would make the message worse**, from a wrong-but-specific
`Disk error!` to `unknown!`. #70 is open in openelixir too, for exactly this reason.

**Recommendation: `FR_DENIED`, and #70 in the same change, not after it.** D3 and #70 are one decision.

## D4. How is the project save made atomic?

- **(a) Leave it.** #65 stays: the destination is destroyed before the replacement exists.
- **(b) Their two-rename swap** (`presetManager.c` `preset_saveProject`): write into `/projNNn`, then
  `/projNN` -> `/projNNo`, then `/projNNn` -> `/projNN`, rolling `/projNNo` back if the second rename
  fails. Stale `/projNNn` and `/projNNo` are removed first, and a `/projNN` missing between the two
  renames is restored on the next access.
- **(c) Write into `/tmp`, then swap.** We already have a `/tmp` working folder.

**Recommendation: (b).** Three things it costs, which should be stated rather than discovered:

1. **Twice the space.** During a save the card holds `/projNN` and `/projNNn`. The reference project is
   266,537 bytes, so about 520 KB - nothing on a 32 GB card, but the failure being handled *is* a full
   card, so an atomic save fails earlier than a destructive one. It fails safely, which is the point.
2. **A window with no `/projNN`**, between the two renames. Power lost there leaves `/projNNo` holding the
   data, so the recovery path is part of the work, not an extra.
3. **Two more 8.3 names per project.** `proj00n` is seven characters, so it fits, and FatFs is built
   without long names (`_USE_LFN 0`).

(c) is tempting because the folder exists, but `/tmp` is the autosave working copy: reusing it couples the
project save to the autosave, and a failure in one would corrupt the other.

## D5. Does "save" mean the slot under the cursor, or the kit you are editing? (#8)

Ours passes `menu_selectedPresetNr[SAVE_TYPE_KIT]`, which is wherever the encoder was last turned in a
load or save menu. Theirs passes `menu_loadedPresetNr[CURRENT_KIT]`, the kit actually in the edit buffer.

This is **not** a bug fix, it is a semantic choice, and it changes what the unit does in front of a user:
scroll the load menu without pressing anything, then save the project, and today the kit you were editing
is written into the slot you were merely looking at.

**Recommendation: the loaded kit.** It is what the rest of the firmware means by "current", and
`menu_loadedPresetNr[CURRENT_KIT]` is already maintained for it. Because it is user-visible, the issue's
test procedure has to state the gesture: scroll the load menu to a different slot, do not press, save the
project, and check which slot changed.

## D6. Load path: validate in place, or stage and commit? (#89, #90, #91)

Three issues share one shape. A `.pat` is read **into the live pattern** (#89), so a truncated file leaves
half the new pattern and half the old; `glo.cfg` is applied byte by byte as it is read (#90), so a damaged
file leaves the settings part old and part new; and three numbers read off the card - track lengths, kit
number, project number - are used as indices without a range check (#91).

- **(a) Range-check in place.** Smallest change, and it does not fix the half-applied cases.
- **(b) Stage, validate, then commit.** Read into a buffer, check the length against the generations we
  know (`.pat` is 3215, 3663 or 3831 bytes) and the per-field ranges, and only then replace live state.

**Recommendation: (b) for `.pat` and `glo.cfg`, (a) for the scalars.** The decision this forces is **where
the staging buffer lives**: a pattern is 3,657 bytes. RAM has about 94 KB free but DTCM is at 82%. There is
already a `sx_patternBuf[PATTERN_DATA_LEN]` in `SysexHandler.c` - reusing it would save the memory and
couple the SD path to the SysEx path, so a dump arriving mid-load would corrupt it. **I would not reuse
it**; a second buffer in the RAM region is the honest cost, and it is measurable before committing.

## D7. #72 first, or last?

#72 is "inject short writes in the emulator's RAM disk". Without it, **every fix in this group is
untestable**: a full card cannot be staged on demand, and #3 is already in `main` with its device test
having been a non-regression check rather than a demonstration.

**Recommendation: #72 first.** It is the same argument that `test/engine` settled before #93 - and that one
immediately found that our own fix had changed all 32 kits. The pattern is worth repeating: build the thing
that can fail the test before writing the fix.

## D8. #4, the SD timeout: fix or document?

openelixir **declines it**, in writing: their `docs/KNOWN_ISSUES.md` lists P7 as a known limitation,
"don't remove the card while it is being read or written".

Our issue is also narrower than it says, measured: `SD_TIMEOUT` **is** passed to the transfers
(`FATFS/Target/sd_diskio.c:154,185`), but the waits after them are `while(BSP_SD_GetCardState() != MSD_OK)`
with no bound (`:157,188`), and the file contains no `HAL_GetTick()` at all. Those two loops are the hang.

**Recommendation: document it, like they did, and keep the sharper description.** Two bounded loops are a
small change, but the recovery after a timeout is not: half a written file, FatFs state mid-operation, and
no obvious safe thing to do. That is a project of its own, not part of this group.

## D9. Does any of this change the files on the card?

Checked, and **no**, with one exception. The write-path and load-path work changes no byte of any format.
The atomic save (D4) adds `/projNNn` and `/projNNo` as transient folders, which stock firmware would see as
two stray directories if it looked - it does not, it opens `/projNN` by name. So no `format-change` label
is needed and the warning the working rules require does not apply.

---

## The order that falls out of the above

1. **#72** - the failure injection, so the rest can be tested at all (D7).
2. **D2 + D3 + #70 + #73** - the sticky write accumulator, `FR_DENIED` for a short write, and the error
   screen extended in the same change, because splitting them regresses the message.
3. **#69** - the discarded `preset_saveGlobals()` result; trivial once D2 is in.
4. **#65 + D4** - the atomic save, with the recovery path. This is the big one, and it **unmasks #88**, so
   #88's test becomes meaningful only after it.
5. **#8 (D5)** - the semantic fix, separately, because it is the only user-visible behaviour change here.
6. **#89, #90, #91 (D6)** - the load path, with the staging buffer cost measured first.
7. **#24** - the init kit off-by-8; independent, small, and openelixir fixed it in the same commit (P9).
8. **#88** - last, once #65 stops hiding it.
9. **#4** - documented, not fixed (D8).

## What I need from you

Nine decisions, and I have a recommendation for each. The three that are genuinely yours rather than
technical:

- **D5**: changing what "save project" captures is visible to anyone using the unit.
- **D4**: an atomic save needs twice the free space and fails earlier on a nearly full card, in exchange
  for never destroying a project it has not replaced.
- **D8**: declining #4 means writing down that pulling the card during an access hangs the unit.

---

## What was done, 2026-11-02

The nine decisions were taken as recommended. The order above was followed. Where measuring changed the
answer, it is recorded here rather than left in the issue.

| step | issues | state | cost against the `main` it was measured on |
|---|---|---|---|
| 1 | #72 | in `main` (emulator lane) | - |
| 2 | #73, #70, D2, D3 | in `main` | see #73 |
| 3 | #69 | in `main` | see #69 |
| 4 | #65, D4 | in `main` | see #65 |
| 5 | #8, D5 | in `main` | 0 in every region |
| 6 | **#91** | in `main` | FLASH +80 B |
| 6 | **#90** | branch, awaiting the device test | FLASH +136 B |
| 6 | **#89** | branch, awaiting the device test | RAM +3,824 B, FLASH **-136 B** |
| 7 | **#24** | branch, awaiting the device test | FLASH +32 B |
| 8 | **#88** | branch, awaiting the device test | FLASH +264 B |
| 9 | #4, D8 | documented in `docs/known-issues.md`, closed; the work is #100 | - |

The four branches are on one image, `test/hw-session-3`, and the session sheet is `TEST-SESSION-2.md` in
the workspace repository. Combined: ITCM and DTCM unchanged, RAM +3,824 B, FLASH +296 B.

**D6's open sub-decision is settled: the staging buffer is a 3,817-byte `static` in `presetManager.c`**, so
the RAM region, measured at +3,824 B with alignment. Not the stack, because both readers run from the menu
and from `preset_loadProject()`; not `sx_patternBuf` or `seq_tmpPattern`, because either would couple a
card read to the SysEx path or to the pattern edit buffer. Flash went *down* by 136 B in the same change:
two inlined readers became one staged reader plus one commit.

Three things measuring changed:

- **#90's per-field range check was dropped.** `docs/params.json` disagrees with the data Erica ships:
  all six factory projects carry `PAR_MIDI_CHAN_GLOBAL = 0` where that file says 1-16, `PROJ05` carries
  `PAR_MIDI_NOTE2 = 0`, and `buttonHandler.c:1178` stores `SKIP_NEXT_KIT_AUTOLOAD` (2) in
  `PAR_PATTERN_MODE`, which it calls a 0-1 menu. A clamp built from that file would have rewritten valid
  cards. No global is used as an array index anywhere in `Core/Src`, so there is no memory-safety reason to
  insist. The staging - apply nothing unless the whole file is there - is the whole fix.
- **#90's second half was already fixed in this fork.** openelixir's "defaults written but not applied"
  does not apply: `main.c` calls `preset_createGlobals()`, which applies them as well as writing them.
- **#4's description was wrong about what hangs.** `SD_TIMEOUT` is effectively infinite, but the lockup is
  the two unbounded `while(BSP_SD_GetCardState() != MSD_OK)` loops at `sd_diskio.c:157,188` in a file with
  no `HAL_GetTick()` at all. "Use a finite timeout" would not have fixed it.

And one new issue came out of the measurements: **#99**, a track length of 0 - which the firmware itself
writes, see #23 - is a modulo by zero at three of the eight sites that use a length. #91 deliberately
leaves 0 alone so that defect stays visible where it belongs, in the sequencer.
