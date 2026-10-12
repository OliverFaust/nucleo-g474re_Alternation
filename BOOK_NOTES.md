# Book notes

The source files deliberately contain no comments: the explanations are in the book, which refers to the code by line. This file keeps every comment removed from this chapter's code on 2026-10-12 (from commit `e6030bd`), so that nothing is lost.

For each file, in file order: where the comment was (line numbers before the removal), the comment itself, and the code it explains, numbered as in the source file now, i.e. as in the book listing.

## `Core/Src/application.cpp`

1. **Line 4**, at the end of the code line; explains listing line 4:

   ```cpp
   // StaticTask_t
   ```

   ```cpp
   4  #include "FreeRTOS.h"
   ```

2. **Line 33**, at the end of the code line; explains listing line 33:

   ```cpp
   // done
   ```

   ```cpp
   33  SleepFor(Forever);
   ```

3. **Line 45**, above the code; explains listing lines 45–47:

   ```cpp
   // The only process that prints: two threads printing at once would lose characters.
   ```

   ```cpp
   45    void run() override {
   46      SleepFor(Milliseconds(10));
   47      printf("[Receiver] Task running. Using Resident-Guard ALT.\r\n");
   ```

4. **Lines 56–57**, above the code; explains listing line 55:

   ```cpp
   // fairSelect() starts after the guard selected last time, so neither sender can starve
   // the other. When it returns, the selected guard's message has been transferred.
   ```

   ```cpp
   55      Alternative alt(inA | msgA, inB | msgB);
   ```

5. **Line 98**, at the end of the code line; explains listing line 95:

   ```cpp
   // done
   ```

   ```cpp
   95  SleepFor(Forever);
   ```

6. **Lines 103–104**, above the code; explains listing lines 100–101:

   ```cpp
   // MainApp runs above the network, so the processes first run after MainApp has printed
   // its messages and exited.
   ```

   ```cpp
   100  static constexpr osPriority_t MAIN_APP_PRIORITY = osPriorityBelowNormal;
   101  static constexpr osPriority_t NETWORK_PRIORITY  = osPriorityLow;
   ```

7. **Line 108**, above the code; explains listing lines 103–104:

   ```cpp
   // Static stack (384 words = 1.5 KB) and control block: no heap.
   ```

   ```cpp
   103  alignas(8) static uint32_t mainAppStack[384];
   104  static StaticTask_t mainAppControlBlock;
   ```
