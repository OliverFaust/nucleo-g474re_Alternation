# What changes in the book chapter text (Alternation), 2.0.1 -> 3.0.0

1. **`application.cpp` listing:**
   - `SleepFor(Forever);` replaces `SleepFor(osWaitForever);` (both senders and the receiver, "done: sleep
     for ever"). In 3.0 `SleepFor()` takes a duration (`Time`); `Forever` is the duration "for ever".
   - `SleepFor(Milliseconds(10));` replaces `SleepFor(Milliseconds(10).to_ticks());` (the receiver's start).
   - The ALT is unchanged: `Alternative alt(inA | msgA, inB | msgB);` and `alt.fairSelect()`.
2. **Project setup:** two defines in both configurations (`CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY=5`,
   `CSP4CMSIS_DEVICE_HEADER="stm32g4xx.h"`); static allocation is the default, FreeRTOS is detected.
3. **Library version:** CSP4CMSIS 3.0.0 (stable 3.x API), unmodified in `lib/csp4cmsis/`.
4. **Console output shown in the chapter:** if the text shows the run ending with `SUCCESS: 2000000 messages
   verified heap-free.`, say that on the board some lines can be missing (three processes print, the BSP's
   console driver drops characters while the UART is busy): with 2.0.1 the senders' `Finished` lines were
   lost, with 3.0.0 the last progress line and `SUCCESS`. The fairness and completion results are identical.
5. **Unchanged:** the ALT's behaviour (strict rotation with `fairSelect()`), priorities, memory (0 FreeRTOS
   heap allocations), the formal model.
