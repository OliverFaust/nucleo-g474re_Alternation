# CSP4CMSIS 2.0.1 update: results

**Date:** 2026-10-04. **Board:** NUCLEO-G474RE (ST-LINK-V3, VCP = LPUART1, 115200). **Tools:** STM32CubeIDE
2.1.0 (GNU Tools for STM32 14.3.1, headless build), STM32CubeMX 6.17.0, FW_G4 V1.6.3 (FreeRTOS 10.3.1),
STM32CubeProgrammer (flash; SWD reads in hotplug mode, no reset). **Library:** CSP4CMSIS v2.0.1,
unmodified. Baseline: `BASELINE.md`. Scripts: `run.sh` (board run, SWD readings), `measure.py`,
`fair_patch.py` and `fair_read.sh` (fairness instrumentation, scratch builds only).

**Repository:** until 2026-10-04 the only branch was `develop` (GitHub default). The author renamed it
`main` on GitHub; the local branch was renamed and fast-forwarded to `ac037bc`; `csp4cmsis-2.0.1`
starts there.

## Analysis: nothing whose meaning changes in 2.0

| Rule (2.0) | This example |
|---|---|
| at most one ALTing reader / writer per channel | one ALTing reader (Receiver) on `chan_A` and `chan_B`; the Senders do not ALT |
| symmetric ALT now communicates | none |
| policies only on buffered channels | none (`Channel<Message>`, Block) |
| `putFromISR` only via `isrWriter()` | no ISR |
| timeout guards timer-free | none |
| trivially copyable elements | `Message{int, int}` |

## Commits (branch csp4cmsis-2.0.1)

| Commit | Change |
|---|---|
| Baseline | `BASELINE.md`, logs, scripts |
| Prepare for regeneration | heap (30 720, unchanged) and `USE_NEWLIB_REENTRANT` in the `.ioc`; bootstrap into `USER CODE BSP`; CubeMX restores the hand-deleted `defaultTask` (its USER CODE notes removed); `main.c` then identical to The_Process's at the same step |
| Migrate to FW_G4 V1.6.3 | 34 HAL/CMSIS/BSP files; `Middlewares/` (FreeRTOS 10.3.1) unchanged |
| CubeMX: static defaultTask; configASSERT | as in The_Process |
| CSP4CMSIS 2.0.1 | library + LICENSE + VERSION; include path, defines, GNU++17 in Debug and Release; CMSIS-RTOS2 bootstrap, `SleepFor()`, priorities; MainApp's wrong "network finished" message corrected; unbuffered stdout (zero heap, as the banner says); MainApp stack 1.5 KB and FreeRTOS heap 1 KB from the measurements. The ALT code itself is unchanged |
| Untrack language.settings.xml; LICENSE | as in the other repositories; LICENSE "Copyright (c) 2026 Oliver Faust" (first commit 2026-01-31) |
| README, formal-model readme | 2.0 alternation semantics, versions, memory, console note; model readme no longer describes the 1.x event groups and no longer claims the model checks fairness |

## Board results

| | Baseline (old library, Debug) | 2.0.1 Debug | 2.0.1 Release |
|---|---|---|---|
| Build | Debug only (Release: 15 errors) | 0 errors, 0 warnings | 0 errors, 0 warnings |
| text / data / bss (B) | 44 440 / 132 / 41 316 | 47 452 / 132 / 16 320 | 28 720 / 112 / 16 232 |
| Result | `SUCCESS: 2000000 messages verified heap-free.`, no DATA ERROR | same | same (completion confirmed over SWD, see below) |
| Time for 2 000 000 messages (instrumented) | 127.5–128.8 s | **103.3 s** | **52.6 s** |
| Stacks Sender 1 / Sender 2 (1 KB) / Receiver (2 KB) | 528 / 428 / 696 B | 532 / 480 / 724 B | 500 / 448 / 660 B |
| MainApp | 8 KB from the heap (not measured) | 620 B of 1.5 KB (static) | 308 B of 1.5 KB |
| defaultTask (2 KB) | none (deleted by hand) | 128 B (static) | 100 B |
| Priorities Senders, Receiver / MainApp / defaultTask | 2 / 3 / — | 8 / 16 / 24 | 8 / 16 / 24 |
| FreeRTOS heap | 5 allocations, 2 frees | **0 allocations** | **0 allocations** |
| C library heap (newlib `_sbrk`) | 1032 B | **0 B** (`_sbrk()` never called) | **0 B** |

## Fairness (2 000 000 selections; instrumented scratch builds, `fair_patch.py`)

| | Switches (of 1 999 999) | Longest run of one guard | A per 10 000 block | Runs of >= 100 |
|---|---|---|---|---|
| Baseline, first instrumented image | 1 986 706 (99.34 %) | 3484 | 4975 .. 6726 | — |
| Baseline, image with position counters (2 runs, identical) | 1 985 838 (99.29 %) | 40 (the tail, guard A) | 4999 .. 5019 | 0 |
| 2.0.1 Debug (2 runs, identical) | **1 999 999 (100 %)** | **1** | **5000 .. 5000** | 0 |
| 2.0.1 Release (1 run) | **1 999 999 (100 %)** | **1** | **5000 .. 5000** | 0 |

Both versions serve both channels (1 000 000 each, by construction) and neither starves a sender. With
the 1.x library the selection order depends on timing: about 0.7 % of the selections repeat the previous
guard, one image showed a run of 3484 selections of one guard (10 000-blocks with up to 67 % A), and the
two senders finish up to 40 messages apart. With 2.0.1 the Receiver alternates strictly, A, B, A, B,
over all 2 000 000 selections, in Debug and Release. The distributions do **not** agree within the
variation of the old one: 2.0.1 has none.

## Classification of every difference

| # | Difference | Class | Evidence |
|---|---|---|---|
| 1 | Strict alternation (100 % switches, longest run 1) instead of 99.3 % with runs of up to 40 / 3484 | **(b) intended 2.0 behaviour** | 2.0's `fairSelect()` starts after the last selected guard and selects one ready guard per call (`alternative.cpp`, `fair_select_start_index = (actual_index + 1) % num_guards`); with both senders always ready this gives strict alternation. Whether the 1.x repetitions were caused by one of the 1.x ALT defects (lost wakeups) or only by scheduling cannot be told from these measurements: the 1.x run showed no wrong selection (no DATA ERROR), so no defect is **proven** here |
| 2 | 19 % faster in Debug (103.3 s vs 127.5 s) | (b) implementation change | 2.0's one-winner protocol on thread flags and a BASEPRI critical section instead of 1.x's FreeRTOS mutex/event groups; same 2 000 000 checked messages |
| 3 | Data errors | none in either version | 0 DATA ERROR lines in all runs; the 1.x ALT defects listed in CHANGES_2.0 (guard selected without its data, data delivered while another guard was selected, lost wakeups/signals) **did not show up** in this example's 1.x runs |
| 4 | MainApp's message "network started" instead of "Run() returned, network finished" | (b) intended (correction of a wrong message, not library-related) | in `StaticNetwork` mode `Run()` returns at once: the old message appeared before the processes had run |
| 5 | Which console lines are missing or garbled (baseline: `[Sender 2] Starting`, garbled `Finished`; 2.0.1 Debug: `[Sender 2] Starting`, both `Finished`; 2.0.1 Release, one image: also the last progress line and `SUCCESS`) | **not a regression**: pre-existing console fault, timing-dependent | the BSP's `__io_putchar()` drops characters while another thread transmits (`HAL_BUSY` ignored); test copy: 75 characters dropped = exactly the 3 missing lines; for the image without `SUCCESS` on the console, all three threads were read over SWD in their final `SleepFor()` loop (the Receiver had completed): `results/console_loss_evidence.txt` |
| 6 | Heap: 0 FreeRTOS allocations, 0 B C library | (b) intended | static threads, unbuffered stdout |
| 7 | Priorities 8 / 16 / 24 instead of 2 / 3 | (b) intended | named `osPriority_t`, same order |
| 8 | defaultTask present (static) | (b) intended | CubeMX re-creates it on regeneration |

No regression found.

## Regeneration and fresh clone

- **GENERATE CODE** (CubeMX 6.17.0) on the committed `.ioc`: no change in git; rebuilt Debug and
  Release ELFs byte-identical to those flashed above, so the board output is identical.
- **Fresh clone** to another path, empty workspace, import, build (Debug and Release, 0 errors, 0
  warnings), flash: Release ELF byte-identical; Debug flash image identical (the ELF's debug
  information contains the build path); board output and measurements identical
  (`results/fresh_debug_*`). `language.settings.xml` was recreated by the import (ignored by git).

## Formal model

`Formal model/Alternation.csp` (SenderA, SenderB, Receiver with external choice `[]`, sequence checks
modulo 3) **still matches** the 2.0 semantics; it matches them better than 1.x: CSP's `[]` performs
exactly one event of the chosen channel, which is what 2.0's one-winner protocol guarantees (one guard
selected, its own message transferred), and what the 1.x defects could violate. The model does not
express fairness (`[]` leaves the choice among ready channels open), so it covers `priSelect()` and
`fairSelect()` alike; its readme said it evaluated fairness and described the 1.x implementation, both
corrected. Differences that remain by design: the model's counters wrap at 3 and it does not stop after
1 000 000 messages.

## What changes in the book chapter text (Alternation)

**ALT, guards, fairness, selection** (the subject of the chapter):

1. **The listing of the ALT is unchanged:** `Alternative alt(inA | msgA, inB | msgB);` and
   `alt.fairSelect()`, the selection index, the per-guard handling.
2. **What `fairSelect()` guarantees in 2.0:** exactly one ready guard is selected per call, and its own
   message has been transferred into its variable when the call returns; the other sender keeps
   waiting with its message untouched. Any text describing the 1.x mechanism (event groups, `__CLZ`,
   "resident guards" in the channel) must go: in 2.0 the guards live in the channel ends (`Chanin`),
   and the selection is a one-winner protocol on thread flags that re-checks after every wakeup.
3. **Fairness, precisely:** `fairSelect()` checks the guards starting after the one selected last; with
   both senders always ready this gives strict alternation, measured: 1 999 999 switches in 2 000 000
   selections, every 10 000-block exactly 5000 / 5000 (1.x: 99.3 % switches, runs of up to 3484
   depending on timing). `priSelect()` always checks in the given order (the first guard can starve
   the second). Fairness is a property of the implementation; the CSP model's `[]` does not specify it.
4. **Rules that are new or now enforced:** at most one process may ALT on each end of a channel
   (violations end in the fatal-error hook); an ALT on both ends of a channel (symmetric) communicates;
   channel element types must be trivially copyable; policies (KeepNewest/KeepOldest) only on buffered
   channels.
5. **Timeouts** (if the chapter mentions them): `RelTimeoutGuard` is timer-free since 2.0.1, with a
   fixed deadline per `select()` (not used in this example).
6. **Correctness claims:** the 1.x defects (guard selected without its data, data delivered while
   another guard was selected, lost wakeups/signals) are fixed in 2.0; this example's sequence check
   did not catch them in 1.x either, so the chapter should not present the example as evidence of
   them.
7. **Output listing:** "network started" instead of "network finished"; the console note (lines can be
   lost when several processes print at once).

**As in the other chapters:**

8. `application.cpp`: `osThreadNew` with a static 1.5 KB stack (measured 620 B), `osDelay`,
   `osThreadExit()`, `SleepFor(...)` in the processes (also `SleepFor(osWaitForever)` instead of
   `vTaskDelay(portMAX_DELAY)`), includes `cmsis_os2.h` and `FreeRTOS.h`; named priorities
   `osPriorityLow` (processes, 8) < `osPriorityBelowNormal` (MainApp, 16) < `osPriorityNormal`
   (defaultTask, 24), formerly 2 < 3; `Run(..., priority)`.
9. Stack units: `CSProcessStatic<256>` / `<512>` = 1 KB / 2 KB; `osThreadAttr_t.stack_size` in bytes
   (the old `MAIN_APP_STACK_WORDS 2048` meant 8 KB).
10. CubeMX settings: `USE_NEWLIB_REENTRANT` Enabled; heap 1024 B in the `.ioc`; `defaultTask` static
    (CubeMX re-creates a deleted one); printing `configASSERT`; FW_G4 V1.6.3, CubeMX 6.17.0, CubeIDE 2.1.0.
11. Project setup: `lib/csp4cmsis/` = CSP4CMSIS 2.0.1 unmodified (`VERSION`, `LICENSE`); include path
    `../lib/csp4cmsis/inc`; four defines; GNU++17 in Debug and Release.
12. Memory: zero heap, measured: no FreeRTOS heap allocation (heap 1 KB) and no C library heap
    (`setvbuf(stdout, NULL, _IONBF, 0)` in `main.c` USER CODE 2).
13. Console: LPUART1 via the ST-LINK virtual COM port, not USART1.

Logs: `results/`.
