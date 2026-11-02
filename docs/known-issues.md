# Known issues

Defects that are understood, verified in the source, and **deliberately not fixed**, with the reason. A
defect that is merely open belongs in the issue tracker, not here; this file is for the ones where the
decision was to live with the behaviour and say so.

## Pulling the SD card during an access hangs the unit

**Status:** known limitation. Do not remove the card while the unit is reading or writing it.

**What happens.** Removing the card while a load, a save or the autosave is in flight leaves the firmware
in a loop with interrupts still running: the screen stops updating and the only way out is a power cycle.
Nothing is reported, because nothing in the path can report.

**Where it is, verified in the source.** Two parts, and the second is the one that hangs:

1. The timeout passed to the transfers is effectively infinite. `SD_TIMEOUT`
   (`FATFS/Target/sd_diskio.c:36`) resolves to `SDMMC_DATATIMEOUT`, which is `0xFFFFFFFFU`
   (`Drivers/STM32F7xx_HAL_Driver/Inc/stm32f7xx_ll_sdmmc.h:304`). It reaches `BSP_SD_ReadBlocks()` and
   `BSP_SD_WriteBlocks()` (`sd_diskio.c:154,185`) as a millisecond value, so about **49.7 days**. The
   inactive `#else` branch of the same `#define` would use `30 * 1000` (`:40`).
2. After a transfer returns, both paths spin with no bound at all:

   ```c
   /* wait until the read operation is finished */
   while(BSP_SD_GetCardState()!= MSD_OK)
   {
   }
   ```

   `sd_diskio.c:157` and `:188`. `BSP_SD_GetCardState()` is
   `HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER ? SD_TRANSFER_OK : SD_TRANSFER_BUSY`
   (`FATFS/Target/bsp_driver_sd.c:205-208`), so with no card it never returns `MSD_OK`. The file contains
   **no `HAL_GetTick()` at all**, so there is nothing to bound either loop.

**Why it is not fixed.** Bounding the two loops is a small change; recovering afterwards is not. A timeout
in the middle of a write leaves a half-written file, FatFs state mid-operation and a volume whose
allocation table may disagree with its directory, and there is no obviously safe thing to do next -
remounting, retrying and giving up each lose something different. That is a piece of work with its own
design and its own test rig, not a line in a loop, and doing only the easy half would turn a hang into
silent corruption, which is worse.

`tunefish777/openelixir-02` reached the same conclusion and lists it in their `docs/KNOWN_ISSUES.md` as
P7. The defect exists in Erica's firmware, in every fork, and in this one.

**If you need to swap cards**, stop the sequencer, wait for the autosave (two seconds after the last edit),
and then remove the card.

**Tracked as** issue #4 in this fork's tracker, closed with this entry, and issue #100, which is the
bounded-loops-plus-recovery work if it is ever taken on.
