# CSP4CMSIS 3.0.0 update: results

**Date:** 2026-10-06. **Board, tools:** as for 2.0.1 (`../migration_2.0.1/RESULTS.md`). **Library:**
CSP4CMSIS v3.0.0 (commit `647a1cb`), unmodified. Procedure: `CHECKLIST.md` section 9 of
nucleo-g474re_The_Process. Scripts: `../migration_2.0.1/run.sh` (170 s run, SWD readings), `fair_patch.py`,
`fair_read.sh` (unchanged; the process object layout is the same in 3.0). Reference: the 2.0.1 logs
`../migration_2.0.1/results/final_*`, `fair_new_result.txt`, `console_loss_evidence.txt`.

## Commits (branch csp4cmsis-3.0.0, from main `1d98d11`)

| Commit | Change |
|---|---|
| lib/csp4cmsis | unmodified v3.0.0 sources, `LICENSE`, `VERSION` |
| Debug and Release defines | only `CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY=5` and `CSP4CMSIS_DEVICE_HEADER="stm32g4xx.h"` |
| `SleepFor(Forever)`, `SleepFor(Milliseconds(10))` | were `SleepFor(osWaitForever)` (twice; a plain number no longer compiles) and `SleepFor(Milliseconds(10).to_ticks())` |
| README | 3.0.0, the two defines |

The ALT (`Alternative alt(inA | msgA, inB | msgB)`, `fairSelect()`) is unchanged in 3.0.

## Board results

| | 2.0.1 Debug | 3.0.0 Debug | 2.0.1 Release | 3.0.0 Release |
|---|---|---|---|---|
| Build | 0 errors, 0 warnings | 0 errors, 0 warnings | 0 errors, 0 warnings | 0 errors, 0 warnings |
| text / data / bss (B) | 47 452 / 132 / 16 320 | 47 504 / 132 / 16 320 | 28 720 / 112 / 16 232 | 28 632 / 112 / 16 232 |
| UART (170 s) | banner, Starting/Finished lines, 200 progress lines, `SUCCESS` | identical up to the last lines (below) | | identical up to the last lines |
| Stacks used: SenderA / SenderB / Receiver / MainApp / defaultTask | 532 / 480 / 724 / 620 / 128 B | 532 / 480 / **716** / 620 / 128 B | 500 / … / 660 / … | **512** / … / **676** / … (others identical) |
| Priorities, FreeRTOS heap (0 allocations), `_sbrk` | | identical | | identical |

- **Last console lines:** the 2.0.1 logs end with `Verified 2000000 messages...` and `SUCCESS: 2000000
  messages verified heap-free.` but lack `[Sender 1] Finished.`; the 3.0.0 logs (Debug, Release and the
  fresh-clone run, all alike) contain `[Sender 1] Finished.` but end at `Verified 1990000 messages...`. Both
  lack `[Sender 2] Starting sequence.` This is the console loss documented for 2.0.1
  (`console_loss_evidence.txt`): three processes print, and the BSP's `__io_putchar()` drops the characters
  of one while the UART is busy with the other; which lines collide depends on timing, and 3.0.0's timing
  differs slightly. Not a regression (classification (b) of the checklist: no fault in the example or the
  library):
  - over SWD after the Release run, all three threads are in their final `SleepFor(Forever)` (TCB
    `xStateListItem.pvContainer` = delayed list, `ucNotifyState` = 0);
  - the instrumented fairness test (below) shows all 2 000 000 messages selected and `error_found` = 0.
- **Stacks:** Receiver −8 B (Debug), +16 B (Release); SenderA +12 B (Release), all well inside their stacks;
  cause not examined (library code changed in 3.0).
- **Static allocation without the define:** 0 FreeRTOS heap allocations (FreeRTOS detected from `FreeRTOS.h`).

## Fairness test (instrumented scratch build, not committed)

`fair_patch.py` plus, for this run, the Receiver's `error_found` exported as `t_err` (scratch only):

| | Selections | A / B | Switches | Longest run | A per 10 000 | Time | `error_found` |
|---|---|---|---|---|---|---|---|
| 2.0.1 Debug | 2 000 000 | 1 000 000 / 1 000 000 | 1 999 999 | 1 | 5000..5000 | 103 304 ms | (not recorded) |
| 3.0.0 Debug | 2 000 000 | 1 000 000 / 1 000 000 | 1 999 999 | 1 | 5000..5000 | 103 306 ms | 0 |
| 2.0.1 Release | 2 000 000 | 1 000 000 / 1 000 000 | 1 999 999 | 1 | 5000..5000 | 52 593 ms | (not recorded) |
| 3.0.0 Release | 2 000 000 | 1 000 000 / 1 000 000 | 1 999 999 | 1 | 5000..5000 | 52 989 ms | 0 |

`fairSelect()` with both guards always ready: strict rotation, as in 2.0.1. `results/fair_v300_result.txt`.

## Regeneration and fresh clone

- **GENERATE CODE** on the committed `.ioc`: no change in git; rebuilt ELFs byte-identical (Debug
  `f62209e8…`, Release `7f12f0a7…`, as on the board).
- **Fresh clone** to another path, empty workspace, import, build (0 errors, 0 warnings both): Release ELF
  byte-identical, Debug flash image identical; flashed: output identical to the checkout's 3.0.0 run
  (`results/fresh_debug_uart.txt`).
