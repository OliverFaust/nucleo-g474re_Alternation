# csp4cmsis

A high-performance, **heap-free** (no dynamic allocation in the library; see `Documentation/CSP4CMSIS_Configuration.md` §6) C++ implementation of **Communicating Sequential Processes (CSP)** tailored for ARM CMSIS-compliant microcontrollers. It is specifically optimized for the **Cortex-M55** and the **Himax WE2** platform.

This library enables embedded developers to move away from complex mutex/semaphore management toward a formal model of "Processes" that communicate via synchronized "Channels," significantly reducing race conditions and concurrency bugs.



---

## 🚀 Core Philosophy

* **No Dynamic Memory:** The library never allocates. Channels, processes and synchronization primitives live in static storage or on a stack, and with `CSP4CMSIS_STATIC_ALLOCATION` their RTOS control blocks are static too, so there is no heap fragmentation in safety-critical loops. (A fully heap-free *system* also needs RTOS and C-library configuration: see the configuration document, §6.)
* **Rendezvous-Based:** Fundamental synchronization occurs when a sender and receiver meet, ensuring data integrity without the need for intermediate buffering.
* **CMSIS-RTOS2 Integration:** Built on the portable CMSIS-RTOS2 API (thread flags, semaphores, timers), verified on FreeRTOS (CMSIS-FreeRTOS adapter) and Keil RTX5, behind a high-level, type-safe C++ API.
* **Compile-Time Safety:** Uses C++ templates to ensure that channel data types are checked at compile-time.

---

## 📂 Library Structure

### `inc/csp/` (Headers)
* **`csp4cmsis.h`**: The primary entry point. Include this in your application.
* **`process.h`**: Defines the `CSProcess` base class for creating concurrent actors.
* **`channel_base.h` / `rendezvous_channel.h`**: Synchronous communication pipes.
* **`alt.h`**: Implements the `Alternative` mechanism for non-deterministic input multiplexing (similar to `select` in Go).
* **`buffered_channel.h`**: Buffered channels for asynchronous or lossy data flow (`BufferPolicy::Block`, `KeepNewest`, `KeepOldest`).
* **`barrier.h`**: Multi-process synchronization points.

### `src/` (Implementation)
* **`csp_wrapper.cpp`**: The thread entry function of every process (`ThreadFuncWrapper`).
* **`alternative.cpp`**: `select()`: fair selection, one-winner state word, re-verification of every wakeup.
* **`alt_channel_sync.cpp`**: Rendezvous (and signal) channel core: one-winner ALT protocol with re-verification (OWRV).
* **`glue.cpp`**: Internal adapters for CMSIS-compliant RTOS calls.

---

## 🛠 Basic Usage

### 1. Define your Processes
Inherit from `CSProcess` and implement the `run()` method.

```cpp
class Producer : public CSProcess {
    Chanout<int> out;
public:
    Producer(Chanout<int> o) : out(o) {}
    void run() override {
        for(int i = 0; i < 10; ++i) {
            out << i; // Blocks until receiver is ready
        }
    }
};

class Consumer : public CSProcess {
    Chanin<int> in;
public:
    Consumer(Chanin<int> i) : in(i) {}
    void run() override {
        int val;
        while(true) {
            in >> val; // Blocks until sender is ready
            printf("Received: %d\n", val);
        }
    }
};
