// --- public_task.h (Refactored for SPN, CMSIS-RTOS2 migration, static allocation) ---
#ifndef CSP4CMSIS_PUBLIC_TASK_H
#define CSP4CMSIS_PUBLIC_TASK_H

#include "csp4cmsis.h" // Includes CSProcess, ThreadFuncWrapper, TaskCtx, etc.
#include "cmsis_os2.h"
#include <cstdio>

extern "C" void ThreadFuncWrapper(void* pvParameters);

namespace csp {

// Default priority for a single-process Run(). Was derived from
// FreeRTOS's own configMAX_PRIORITIES ("the top of whatever range is
// configured"); replaced with a portable osPriority_t constant instead --
// see the investigation that picked this specific value (not
// osPriorityISR=56, which is technically accepted by both the FreeRTOS
// adapter and RTX5's osThreadNew() but is the one tier both specs
// document as conventionally reserved for ISR-deferred processing, a
// different role than "the general highest-priority CSP process").
// osPriorityRealtime7 (55) is the highest tier without that documented
// special meaning.
constexpr osPriority_t CSP_DEFAULT_TASK_PRIORITY = osPriorityRealtime7;

/**
 * @brief Launches a single CSProcess as an RTOS2 task, enforcing the Static Process Network (SPN) model.
 * * CRITICAL SPN REQUIREMENT: The CSProcess object MUST be allocated STATICALLY
 * by the application (e.g., as a global or static local variable). Its stack
 * buffer, TCB, and TaskCtx (all owned by CSProcess/CSProcessStatic<N>, see
 * process.h) inherit that same static storage duration -- the static
 * osThreadNew() call below performs no heap allocation of its own.
 * * @param process Reference to the STATICALLY allocated CSProcess object.
 * @param priority The RTOS2 priority for this task.
 */
inline void Run(CSProcess& process, osPriority_t priority = CSP_DEFAULT_TASK_PRIORITY) {

    osPriority_t effective_priority = resolveTaskPriority(process, priority);

    // API 1.3: TaskCtx now lives inside `process` itself (static storage,
    // same lifetime as its stack/TCB) -- prepareTaskCtx() just fills it in
    // and hands back a pointer. No allocation, no caller-owned storage.
    TaskCtx* ctx = process.prepareTaskCtx(/*completion_sem=*/nullptr);

    // stack_mem/stack_size are backend-agnostic (see csp_rtos_static.h),
    // set unconditionally. cb_mem/cb_size (the thread control block) are
    // opt-in and backend-specific -- only set when CSP4CMSIS_STATIC_
    // ALLOCATION is defined; otherwise they stay at their zero default,
    // which every CMSIS-RTOS2 backend treats as "allocate dynamically".
    // stack_size is BYTES (RTOS2 convention), unlike stackWords()'s own
    // word-based unit.
    osThreadAttr_t attr = {};
    attr.name       = "CSP_PROC";
    attr.stack_mem  = process.stackBuffer();
    attr.stack_size = process.stackWords() * sizeof(internal::csp_stack_word_t);
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
    attr.cb_mem     = process.taskBuffer();
    attr.cb_size    = sizeof(internal::csp_static_thread_storage_t);
#endif
    attr.priority   = effective_priority;

    osThreadId_t handle = osThreadNew(ThreadFuncWrapper, ctx, &attr);

    process.setTaskHandle(handle); // NULL on failure -- fine, stackHighWaterMarkWords() treats that as "unavailable"

    if (handle == NULL) {
        printf("FATAL ERROR: Failed to create RTOS2 task for CSProcess "
               "(osThreadNew returned NULL -- check stack/TCB buffers).\r\n");
    }
}

/**
 * @brief Pauses the current process for a specified number of ticks.
 * Maps directly to CMSIS-RTOS2's osDelay.
 * @param ticks_to_sleep The number of RTOS ticks to pause.
 */
inline void SleepFor(uint32_t ticks_to_sleep) {
    osDelay(ticks_to_sleep);
}

// NOTE: For full C++CSP compatibility, you would also define helper time functions here,
// but they are omitted for simplicity as per the plan.

} // namespace csp

#endif // CSP4CMSIS_PUBLIC_TASK_H
