# Baseline before the CSP4CMSIS 2.0.1 update

**Date:** 2026-10-04. **Commit:** `ac037bc` (`main`; until today the repository's only branch was
`develop`, renamed `main` on GitHub). **Library:** `lib/csp4cmsis/`, the pre-1.0 FreeRTOS-native
snapshot, tree `63c61d6` (= The_Process's `17c36e6` with a changed `alternative.cpp`, the "O(1)
ALT"). **Build:** STM32CubeIDE 2.1.0 headless, Debug (`-O0`, GNU++17); Release does not build (no
`lib/csp4cmsis/inc` include path and no GNU++17 in Release: 15 errors). **Board:** NUCLEO-G474RE,
ST-LINK-V3 VCP (LPUART1) 115200.

## State

| Item | Finding |
|---|---|
| CubeMX | CMSIS_V2; FW_G4 V1.6.1, CubeMX 6.16.1; heap 30 720 B hand-edited in `FreeRTOSConfig.h`; newlib reentrancy off; timer task priority 2; `defaultTask` in the `.ioc` but **deleted by hand** from `main.c` (a USER CODE note says so; regeneration brings it back) |
| `main.c` | bootstrap `printf` and `csp_app_main_init()` outside USER CODE (as in The_Process) |
| `.cproject` | `refreshScope` entries for a workspace project `/nucleo-g474re_v10` (a different name) |
| Tools in the repository | `csp4cmsis_map_report.py` (map-file analysis, stdlib Python) |
| LICENSE | "Copyright (c) 2024 Your Name" |
| README | formal-model link to another repository; "USART1" (console is LPUART1); "zero heap usage" (MainApp and defaultTask came from the heap); `lib/CSP4CMSIS/`; example output with an old "BOli2" banner; no description of the ALT; no tool versions |
| Formal model | `Formal model/Alternation.csp`: SenderA, SenderB, Receiver with external choice `[]` -- this example's network (sequence modulo 3); its readme describes the 1.x implementation (event groups, `__CLZ`) |

## Network and ALT

| | |
|---|---|
| Processes | Sender 1 and Sender 2 (`CSProcessStatic<256>`): send 1 000 000 `Message{int source_id, sequence_num}` (8 B) each with `out << msg` (not ALT), print "Starting" and "Finished"; Receiver (`CSProcessStatic<512>`) |
| Channels | `chan_A`, `chan_B`: `Channel<Message>` = rendezvous, Block; one writer, one reader each |
| ALT | one `Alternative alt(inA \| msgA, inB \| msgB)` in the Receiver, `fairSelect()`, input guards only; one ALTing reader per channel, no ALTing writer, no symmetric ALT, no timeout, no ISR |
| Receiver | checks source id and sequence number per channel; prints `Verified n messages` every 10 000 and `SUCCESS: 2000000 messages verified heap-free.` |
| Priorities | Senders and Receiver 2 (native), MainApp 3, no defaultTask |

## Board results (Debug)

| | |
|---|---|
| ELF SHA-256 | `174876fbccabe558cd63bd0604224249baf8042a2f9de2c2cef08160c6765cd3` |
| text / data / bss | 44 440 / 132 / 41 316 B |
| UART (170 s) | banner; `*** MainApp_Task: Run() returned, network finished. Terminating. ***` printed **before** the processes start (StaticNetwork returns at once: the message is wrong); `[Sender 1] Starting sequence.`; **`[Sender 2] Starting sequence.` missing**; 200 `Verified` lines, `SUCCESS: 2000000 messages verified heap-free.`; no DATA ERROR; **garbled lines** where two processes printed at once (`[Receir 2] Finished.`, `00000 messages...`): the BSP's `__io_putchar()` drops characters while the UART is busy (as in _Interrupts) |
| Throughput | 2 000 000 messages in 127.5–128.8 s (about 15 700 per second) |
| Stacks | Sender 1 528 B, Sender 2 428 B (of 1 KB), Receiver 696 B (of 2 KB) |
| FreeRTOS heap (30 720 B) | 5 allocations, 2 frees |
| newlib `_sbrk` | 1032 B |

## Fairness (instrumented scratch build, not committed: `fair_patch.py`)

Counts per guard, switches between guards, longest run of the same guard (and where it ended),
per-10 000-selection blocks.

| Image | A / B | Switches (of 1 999 999) | Longest run | A per 10 000 block | Time |
|---|---|---|---|---|---|
| first instrumented image | 1 000 000 / 1 000 000 | 1 986 706 | **3484** | 4975 .. **6726** | 128.8 s |
| with position counters, run 1 and 2 (identical) | 1 000 000 / 1 000 000 | 1 985 838 | 40 (at the end, guard A: the tail after Sender 2 finished) | 4999 .. 5019 | 127.5 s |

`fairSelect()` alternates in 99.3 % of the selections; the remaining repetitions and the long run of
3484 in one image depend on timing (equal priorities, time slicing): the same image repeats exactly,
a slightly different image does not.
