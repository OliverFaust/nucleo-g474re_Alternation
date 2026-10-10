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
6. **Simplified listing (branch `simplify-chapter-code`):** the code shows the chapter's concept and
   nothing else.
   - `using AltChannel = Channel<Message>;` is gone: `static Channel<Message> chan_A, chan_B;`.
     `struct Message { int source_id; int sequence_num; }` stays: the Receiver checks both fields.
   - `Sender` and `Receiver` lose their `name()` overrides; the listing is reformatted (the earlier
     `Chanout < Message >` spacing came from a formatter).
   - **`main.c`:** CubeMX's `defaultTask` is removed (its attributes, its creation, `StartDefaultTask`).
     The program's threads are now exactly the ones in `application.cpp` (MainApp and the processes),
     plus FreeRTOS's idle and timer tasks. The `.ioc` still contains the task: CubeMX does not allow a
     project without one and re-creates it on regeneration (from the `.ioc`, as the static, heap-free
     task it was); the README says to delete it again.
   - **Thread names:** without the `name()` overrides, the processes appear as `csp_task` in a
     debugger's thread view; MainApp keeps its name (`attr.name`).
   - **Comments** shortened to what is surprising; the explanations are in the chapter text.
   - The console output is unchanged.
