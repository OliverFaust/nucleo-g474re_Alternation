#ifndef CSP_WRAPPER_H
#define CSP_WRAPPER_H

#include "cmsis_os2.h"
#include <tuple>
#include <utility>
#include <cstdio>
#include "csp4cmsis.h"

// --- 1. START CSP NAMESPACE (For Definitions) ---
namespace csp {
    class CSProcess; // Defined in process.h
    // TaskCtx is defined in process.h (needed there so public_task.h can
    // see it too -- csp4cmsis.h includes public_task.h before run.h).

    enum class ExecutionMode {
        TerminatingNetwork, // Blocking: spawns all processes, waits for all to finish.
        StaticNetwork        // Non-blocking: spawns all processes, returns immediately.
    };
} // end namespace csp definition block

// --- 2. The Globally Friended Task Wrapper (DECLARATION ONLY) ---
extern "C" {
    void ThreadFuncWrapper(void* pvParameters);
}


// --- 3. Continue CSP Namespace (For Template Logic) ---
namespace csp {

// Composition-wide default priority for the ParallelHelper Run()
// overloads. Was tskIDLE_PRIORITY + 2 -- a literal native FreeRTOS
// priority of 2 (tskIDLE_PRIORITY is always 0), deliberately offset above
// Idle rather than landing on it, per that formula's own intent: "low/
// background, but not literally competing with the OS idle task."
// osPriorityLow (8) is the closest portable match to that intent --
// osPriorityIdle (1) would misrepresent it (it's exactly the tier the
// original formula was written to avoid), and there's no named
// osPriority_t constant at the unnamed numeric slots in between.
//
// FLAGGED, NOT YET HARDWARE-VERIFIED: this changes a real relative-
// scheduling relationship. Both existing call sites that rely on this
// default (application.cpp's MainApp_Task, neuropathway's
// csp4cmsis_spn.cpp) launch their own task via xTaskCreate(...,
// tskIDLE_PRIORITY + 3, ...) -- native priority 3 -- then spawn their CSP
// network via this default. Under the old formula the network ran at
// native priority 2, BELOW its launching task; osPriorityLow maps
// (FreeRTOS adapter: priority - 1) to native priority 7, ABOVE it -- a
// real ordering flip, not just a renumbering. Reasoned to be
// unobservable in both existing call sites (their launching task spends
// nearly all its post-spawn time blocked in vTaskDelay() for periodic
// reporting, not competing for CPU), but that's reasoning, not something
// confirmed on hardware -- check this deliberately in the next
// neuropathway hardware pass rather than assume it's fine.
constexpr osPriority_t CSP_LEGACY_PARALLEL_PRIORITY = osPriorityLow;

// --- Parallel Helper ---
template <typename... Processes>
class ParallelHelper {
private:
    std::tuple<Processes&...> procs;
    static constexpr size_t num_procs = sizeof...(Processes);

    // API 1.3: spawns process I with ITS OWN declared stack/priority.
    // Stack/TCB/TaskCtx are all owned by the process itself
    // (CSProcessStatic<N>, see process.h) -- the static osThreadNew()
    // call makes no heap allocation, and neither does preparing its
    // TaskCtx.
    //
    // Note: TaskCtx deliberately does NOT live as a ParallelHelper member.
    // ParallelHelper instances (and the temporaries InParallel(...)
    // produces) do not have a lifetime guarantee matching the tasks they
    // spawn -- Run() below takes one by value, for instance. Only the
    // CSProcess objects themselves are contractually static, so that's
    // where TaskCtx storage has to live.
    template <std::size_t I>
    void spawn_task(osSemaphoreId_t sem, osPriority_t composition_priority) {
        CSProcess& proc = std::get<I>(procs);

        TaskCtx* ctx = proc.prepareTaskCtx(sem);
        osPriority_t priority = resolveTaskPriority(proc, composition_priority);

        // osThreadAttr_t -- same pattern as public_task.h's Run(); see
        // that file's comment and csp_rtos_static.h for why stack_mem/
        // stack_size are backend-agnostic but cb_mem/cb_size (the thread
        // control block) are opt-in/backend-specific.
        osThreadAttr_t attr = {};
        attr.name       = proc.name();
        attr.stack_mem  = proc.stackBuffer();
        attr.stack_size = proc.stackWords() * sizeof(internal::csp_stack_word_t);
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
        attr.cb_mem     = proc.taskBuffer();
        attr.cb_size    = sizeof(internal::csp_static_thread_storage_t);
#endif
        attr.priority   = priority;

        osThreadId_t handle = osThreadNew(ThreadFuncWrapper, ctx, &attr);

        proc.setTaskHandle(handle);

        if (handle == NULL) {
            printf("FATAL ERROR: Failed to create RTOS2 task for CSProcess '%s' "
                   "(osThreadNew returned NULL -- check stack/TCB buffers).\r\n",
                   proc.name());
        }
    }

    // Spawns ALL processes, indices 0..N-1.
    template <std::size_t I>
    void spawn_all(osSemaphoreId_t sem, osPriority_t composition_priority) {
        if constexpr (I < sizeof...(Processes)) {
            spawn_task<I>(sem, composition_priority);
            spawn_all<I + 1>(sem, composition_priority);
        }
    }

    template <std::size_t I, typename Func>
    void forEachProcessImpl(Func&& f) {
        if constexpr (I < sizeof...(Processes)) {
            f(static_cast<CSProcess&>(std::get<I>(procs)));
            forEachProcessImpl<I + 1>(std::forward<Func>(f));
        }
    }

public:
    explicit ParallelHelper(Processes&... p) : procs(p...) {}

    // 1. Blocking Run (ExecutionMode::TerminatingNetwork).
    // API 1.2: spawns ALL N processes (including index 0) as their own
    // tasks and blocks the CALLING task until all N have completed.
    // Previously, index 0 ran inline on the caller's stack; the caller
    // now does no CSP work of its own and can safely self-delete once
    // this returns, if it has nothing further to do.
    void execute_terminating(osPriority_t composition_priority) {
        osSemaphoreAttr_t done_sem_attr = {};
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
        // Static semaphore buffer instead of osSemaphoreNew()'s dynamic
        // path -- see csp_rtos_static.h for why the correct backing type
        // is backend-specific (confirmed per-backend: each one's own
        // osSemaphoreNew() uses the same control-block type for both
        // binary and counting semaphores, cast from attr->cb_mem). Local
        // `static` storage is fine: this function blocks until every
        // process has signaled done_sem, so the buffer only needs to
        // outlive that wait, not the ParallelHelper itself.
        static internal::csp_static_semaphore_storage_t done_sem_storage;
        done_sem_attr.cb_mem  = &done_sem_storage;
        done_sem_attr.cb_size = sizeof(done_sem_storage);
#endif
        osSemaphoreId_t done_sem = osSemaphoreNew(num_procs, 0, &done_sem_attr);

        spawn_all<0>(done_sem, composition_priority);

        for (size_t i = 0; i < num_procs; ++i) {
            osSemaphoreAcquire(done_sem, osWaitForever);
        }
        osSemaphoreDelete(done_sem); // releases the handle, not done_sem_storage's memory
    }

    // 2. Non-Blocking Run (ExecutionMode::StaticNetwork).
    // API 1.2: spawns ALL N processes (including index 0) and returns
    // immediately. No process runs on the calling task's stack.
    void execute_static(osPriority_t composition_priority) {
        spawn_all<0>(NULL, composition_priority);
    }

    /**
     * @brief Invokes f(CSProcess&) for every process in this composition,
     * in declaration order. Useful for post-spawn bookkeeping such as a
     * periodic stack-usage report -- see csp4cmsis_spn.cpp.
     */
    template <typename Func>
    void forEachProcess(Func&& f) {
        forEachProcessImpl<0>(std::forward<Func>(f));
    }
};

// --- Public API Syntax ---

template <typename... Processes>
ParallelHelper<Processes...> InParallel(Processes&... procs) {
    return ParallelHelper<Processes...>(procs...);
}

// 1. Terminating-network Run(). 'priority' is the COMPOSITION-WIDE
// default: it applies to any process that hasn't overridden
// taskPriority(). The default value matches pre-1.2 behavior exactly.
template <typename... Processes>
void Run(ParallelHelper<Processes...> helper,
         osPriority_t priority = CSP_LEGACY_PARALLEL_PRIORITY) {
    helper.execute_terminating(priority);
}

// 2. Explicit ExecutionMode selection. Same priority semantics as (1).
template <typename... Processes>
void Run(ParallelHelper<Processes...> helper, ExecutionMode mode,
         osPriority_t priority = CSP_LEGACY_PARALLEL_PRIORITY) {
    if (mode == ExecutionMode::StaticNetwork) {
        helper.execute_static(priority);
    } else {
        helper.execute_terminating(priority);
    }
}

} // namespace csp

#endif // CSP_WRAPPER_H
