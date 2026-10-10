# CSP4CMSIS Alternation Demo for NUCLEO-G474RE

A demonstration of **alternation** (CSP external choice) with the CSP (Communicating Sequential Processes) library CSP4CMSIS on CMSIS-RTOS v2, on an STM32G474RE microcontroller. Two sender processes each send a numbered stream of messages over their own channel; one receiver waits on both channels at once and takes whichever message is ready, checking that every message arrives exactly once and in order. The formal CSP model is in [`Formal model/`](Formal%20model/).

## Features
- FreeRTOS with the CMSIS-RTOS v2 API (STM32CubeMX `CMSIS_V2` interface)
- CSP4CMSIS 3.0.0, in `lib/csp4cmsis/` (unmodified; see `lib/csp4cmsis/VERSION`)
- Alternation: `Alternative` with two input guards and `fairSelect()`
- Rendezvous channels, 2 000 000 messages, each checked for sender id and sequence number
- Zero heap: no FreeRTOS heap and no C library heap allocation (see [Memory](#memory))
- Serial console output via LPUART1, the ST-LINK virtual COM port

## Hardware Requirements
- STM32 Nucleo-G474RE board
- USB cable for programming and serial communication

## Software Requirements

Tested with:

| Tool | Version |
|---|---|
| STM32CubeIDE | 2.1.0 (GNU Tools for STM32 14.3.rel1) |
| STM32CubeMX (only to regenerate code) | 6.17.0 |
| STM32Cube FW_G4 | V1.6.3 (FreeRTOS 10.3.1) |
| CSP4CMSIS | 3.0.0 |

## Serial Configuration
- Baud Rate: 115200
- Data Bits: 8
- Stop Bits: 1
- Parity: None

## Building with STM32CubeIDE
1. Clone this repository. Any directory will do but not the STM32CubeIDE workspace.
2. Open STM32CubeIDE
3. File → Import → Existing Projects into Workspace
4. Select this directory
5. Build (configuration `Debug` or `Release`) and flash to your Nucleo board

The CSP4CMSIS settings are already in the project (G++ compiler, Debug and Release): include path `../lib/csp4cmsis/inc`, and the two defines `CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY=5` and `CSP4CMSIS_DEVICE_HEADER="stm32g4xx.h"`; CSP4CMSIS allocates its RTOS objects statically by default and finds FreeRTOS from `FreeRTOS.h` (explained in the [CSP4CMSIS STM32CubeIDE guide](https://github.com/OliverFaust/CSP4CMSIS/blob/main/Documentation/CSP4CMSIS_STM32CubeIDE.md)).

`nucleo-g474re_v10.ioc` can be opened and regenerated (GENERATE CODE): the application's code in `main.c` and `FreeRTOSConfig.h` sits between `USER CODE BEGIN`/`END` markers, and the FreeRTOS settings it needs (heap size, newlib reentrancy, static default task) are stored in the `.ioc`. defaultTask was removed from main.c; if you regenerate the project with CubeMX, delete it again.

## Project Structure
- `Core/` - `main.c` (CubeMX), `application.cpp` (the example)
- `Drivers/` - STM32 HAL, CMSIS and BSP drivers
- `Formal model/` - CSP-M model of the network
- `lib/csp4cmsis/` - CSP4CMSIS 3.0.0
- `Middlewares/` - FreeRTOS middleware
- `csp4cmsis_map_report.py` - shows how much FLASH and RAM CSP4CMSIS takes (from the linker map file)

## How It Works

1. **Channels**: `chan_A` and `chan_B` are `Channel<Message>`: rendezvous channels (capacity 0). A send blocks until the receiver takes the message, and vice versa.
2. **Senders**: Sender 1 sends `{1, 0}`, `{1, 1}`, … on `chan_A`, Sender 2 sends `{2, 0}`, `{2, 1}`, … on `chan_B`, 1 000 000 messages each.
3. **Receiver – alternation**: `Alternative alt(inA | msgA, inB | msgB);` is an external choice over two *input guards*. Each call of `alt.fairSelect()`:
   - waits until at least one guard is ready (a sender is offering a message);
   - selects **exactly one** ready guard and transfers **its** message into `msgA` or `msgB` before returning; the other sender keeps waiting, its message untouched;
   - returns the index of the selected guard (0 = `chan_A`, 1 = `chan_B`);
   - is **fair**: the guards are checked starting after the one selected last time, so when both senders are ready they are served alternately and neither can starve the other (`priSelect()` would always check in the order the guards were given).
4. The Receiver checks every message (sender id, next sequence number), prints progress every 10 000 messages, reports each sender once all its 1 000 000 messages have arrived, and finally prints `SUCCESS: 2000000 messages verified heap-free.` (or a `DATA ERROR` line). The Senders print nothing.
5. **Start-up**: `main.c` calls `csp_app_main_init()`, which creates the `MainApp` thread (static stack). `MainApp` starts the three processes with `Run(InParallel(sA, sB, r1), ExecutionMode::StaticNetwork, osPriorityLow)`; `Run()` returns at once, and `MainApp` ends. `MainApp` runs at a higher priority (`osPriorityBelowNormal`), so the processes first run after it has ended.

CSP4CMSIS 2.0 implements the alternation with a one-winner protocol: every wakeup is re-checked, so a guard is only selected together with its own data, and no message is lost or delivered twice. Rules: at most one process may wait in an `Alternative` on each end of a channel (here: only the Receiver, on the reading ends); channel element types must be trivially copyable (`Message` is two `int`s).

## Example Console Output

```text
Welcome to STM32 world !

=== STM32 FreeRTOS + CSP4CMSIS bootstrap ===

--- Launching CSP Static Network (Zero-Heap) ---
*** MainApp_Task: network started. Terminating. ***
[Receiver] Task running. Using Resident-Guard ALT.
[Receiver] Verified 10000 messages...
[Receiver] Verified 20000 messages...
...
[Receiver] Verified 1990000 messages...
[Receiver] Sender 1: all 1000000 messages received
[Receiver] Sender 2: all 1000000 messages received
[Receiver] Verified 2000000 messages...
[Receiver] SUCCESS: 2000000 messages verified heap-free.
```

**The console has one owner:** it is a shared resource, and the BSP's console driver (`__io_putchar()`) silently drops the characters of a second thread that prints while the UART is busy, so only the Receiver prints while the network runs (MainApp prints before the processes start, at a higher priority).

## Memory

Measured on the board (Debug and Release):

- **FreeRTOS heap: not used.** `pvPortMalloc()` is never called (0 allocations). The three processes, `MainApp`, and FreeRTOS's idle and timer tasks all have static stacks and control blocks; the rendezvous channels need no RTOS objects. The FreeRTOS heap (`configTOTAL_HEAP_SIZE`) is therefore set to only 1 KB.
- **C library heap: not used.** `main.c` (USER CODE 2) makes `stdout` unbuffered with `setvbuf(stdout, NULL, _IONBF, 0)`; otherwise newlib's `printf()` would `malloc()` a 1 KB `stdout` buffer on first use (measured: 1032 B). With it, `_sbrk()` is never called.
- So the program allocates no heap memory at all: the "(Zero-Heap)" in the start-up banner is literal.

## License and Declaration

MIT License – see the `LICENSE` file. CSP4CMSIS: MIT License, `lib/csp4cmsis/LICENSE`.

Development of this project utilizes AI coding assistants for boilerplate generation, unit test creation, and architectural drafting. All core logic is manually reviewed and verified.

## Acknowledgments
- STMicroelectronics for the HAL library
- FreeRTOS team
