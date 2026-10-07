# What changes in the book chapter text (Alternation), 2.0.1 -> 3.0.0

1. **`application.cpp` listing:**
   - `SleepFor(Forever);` replaces `SleepFor(osWaitForever);` (both senders and the receiver, "done: sleep
     for ever"). In 3.0 `SleepFor()` takes a duration (`Time`); `Forever` is the duration "for ever".
   - `SleepFor(Milliseconds(10));` replaces `SleepFor(Milliseconds(10).to_ticks());` (the receiver's start).
   - The ALT is unchanged: `Alternative alt(inA | msgA, inB | msgB);` and `alt.fairSelect()`.
2. **Project setup:** two defines in both configurations (`CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY=5`,
   `CSP4CMSIS_DEVICE_HEADER="stm32g4xx.h"`); static allocation is the default, FreeRTOS is detected.
3. **Library version:** CSP4CMSIS 3.0.0 (stable 3.x API), unmodified in `lib/csp4cmsis/`.
4. **The console has one owner (branch `console-owner`):** the Senders no longer print (`[Sender n] Starting
   sequence.` and `[Sender n] Finished.` are gone from the listing and the output). The Receiver reports
   `[Receiver] Sender 1: all 1000000 messages received` (and the same for Sender 2) when a channel's last
   message has arrived, and ends with `SUCCESS: ...` or, new, `[Receiver] DATA ERROR: verification stopped
   after n messages.` Text: the console is a shared resource; the BSP's console driver silently drops the
   characters of a second thread that prints while the UART is busy (measured: whole lines lost in every run
   while three processes printed), so one process owns it while the network runs. MainApp's lines stay: it
   prints and ends before the processes run (higher priority). The output listing in the chapter: banner,
   `Task running`, 200 progress lines with the two `all 1000000 messages received` lines before the last one,
   `SUCCESS`; on the board every line now appears, in this order.
5. **Unchanged:** the ALT's behaviour (strict rotation with `fairSelect()`), priorities, memory (0 FreeRTOS
   heap allocations, C-library heap 0: the "Zero-Heap" banner stays), the formal model.
