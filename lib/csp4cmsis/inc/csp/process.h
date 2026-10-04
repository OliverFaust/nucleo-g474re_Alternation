// --- process.h (CMSIS-RTOS2 migration, API 1.3 static task/stack allocation) ---
#ifndef CSP4CMSIS_PROCESS_H
#define CSP4CMSIS_PROCESS_H

#include <stddef.h>   // For size_t, NULL definition
#include "cmsis_os2.h"
#include "csp_rtos_static.h"

extern "C" {
    void ThreadFuncWrapper(void* pvParameters);
}

// =============================================================
// API 1.3: CSP4CMSIS makes no dynamic (heap) allocations for task
// creation. Every CSProcess supplies its own stack buffer and TCB as
// static storage (see CSProcessStatic<N> below), so stackWords() is
// a fixed, compile-time property of the process, queried by Run()
// to build the static osThreadNew() call.
//
// taskPriority() keeps its "unspecified" sentinel -- priority isn't
// tied to a static buffer, so a composition-wide fallback resolved
// at spawn time is still fine there.
// =============================================================

// API 1.4 (CMSIS-RTOS2 migration): priority is now represented as
// osPriority_t, not FreeRTOS's raw UBaseType_t. osPriority_t's own
// values (osPriorityIdle=1 .. osPriorityISR=56) are a plain numeric
// scale by design -- the CMSIS-RTOS2 spec deliberately keeps them
// castable to/from small integers so ports can pass through arbitrary
// priority numbers -- and this library's own priority constants
// (tskIDLE_PRIORITY+N-style small integers, see run.h/public_task.h)
// fall well inside that range, so this is a direct value swap, not a
// lookup-table redesign. osPriorityError (-1) is CMSIS-RTOS2's own
// "invalid/unset" sentinel and replaces the old (UBaseType_t)-1.
#define CSP_PRIORITY_UNSPECIFIED osPriorityError

// Sentinel returned by CSProcess::stackHighWaterMarkWords() when the
// process hasn't been spawned yet (no osThreadId_t captured).
#define CSP_STACK_HWM_UNAVAILABLE ((uint32_t)-1)

// Suggested stack depth (words) for typical CSP4CMSIS processes.
// Not a fallback -- CSProcessStatic<N> callers should pick N deliberately
// per process; this exists purely so call sites have a documented
// starting point instead of a bare magic number.
#ifndef CSP_TYPICAL_STACK_WORDS
#define CSP_TYPICAL_STACK_WORDS 256
#endif

namespace csp {
    // Forward declarations of core internal classes
    namespace internal {
        class Kernel;
    }

    class CSProcess; // Defined below

    /**
     * @brief Context handed to ThreadFuncWrapper as pvParameters: which
     * process to run, and (optionally) a semaphore to signal on
     * completion.
     */
    struct TaskCtx {
        CSProcess* process;
        osSemaphoreId_t completion_sem;
    };

    // =============================================================
    // CSP Public API Definition
    // =============================================================

    /**
     * @brief The abstract base class for all user-defined concurrent tasks (Processes).
     */
    class CSProcess {
    public:
        virtual ~CSProcess() = default;

        /**
         * @brief Returns the name of the process for RTOS2 task registration.
         * Users can override this in their derived classes for better debugging.
         */
        virtual const char* name() const { return "csp_task"; }

        /**
         * @brief RTOS2 task stack depth, in words, for this process.
         * Fixed at compile time (see CSProcessStatic<N>) -- Run() uses this
         * together with stackBuffer()/taskBuffer() to build a static
         * osThreadNew() call. There is no dynamic-allocation fallback.
         */
        virtual size_t stackWords() const = 0;

        /**
         * @brief Pointer to this process's statically-allocated stack
         * buffer, at least stackWords() words long. The process object
         * (which must itself have static storage duration -- e.g. a
         * function-local `static` or a global) owns this memory for its
         * entire lifetime; Run() never allocates, frees, or resizes it.
         */
        virtual internal::csp_stack_word_t* stackBuffer() = 0;

        /**
         * @brief Pointer to this process's statically-allocated thread
         * control-block storage, or nullptr when CSP4CMSIS_STATIC_
         * ALLOCATION isn't defined (dynamic allocation -- Run() leaves
         * osThreadAttr_t.cb_mem/.cb_size at their zero default in that
         * case, and this return value is simply never used). Returns
         * void*, not a backend-specific type, since callers only ever
         * assign it straight into osThreadAttr_t.cb_mem (itself void*) --
         * see csp_rtos_static.h for why the real backing type varies by
         * backend. Same ownership/lifetime rules as stackBuffer().
         */
        virtual void* taskBuffer() = 0;

        /**
         * @brief RTOS2 task priority for this process when spawned by
         * Run(). If a composition-wide priority is also supplied to the
         * parallel Run(ParallelHelper<...>, ...) overloads, a priority
         * returned here (i.e. not CSP_PRIORITY_UNSPECIFIED) takes
         * precedence for this process only.
         */
        virtual osPriority_t taskPriority() const { return CSP_PRIORITY_UNSPECIFIED; }

        // =========================================================
        // API 1.3: internal spawn-time plumbing.
        //
        // These are called by Run() (public_task.h) and ParallelHelper
        // (run.h) only -- not intended for application code. They live
        // here, as members of CSProcess, rather than as locals owned by
        // the caller, because CSProcess is the one object in this chain
        // guaranteed to have static storage duration (same reasoning as
        // stackBuffer()/taskBuffer()). Putting TaskCtx storage anywhere
        // with a shorter lifetime -- e.g. as a ParallelHelper member,
        // where a helper can be a short-lived temporary/by-value
        // parameter -- risks a dangling pointer once the spawned task
        // actually runs and dereferences it.
        // =========================================================

        /**
         * @brief Fills in this process's owned TaskCtx (self pointer +
         * completion semaphore) and returns a pointer to it, suitable for
         * passing as osThreadNew()'s argument.
         */
        TaskCtx* prepareTaskCtx(osSemaphoreId_t completion_sem) {
            m_ctx = TaskCtx{ this, completion_sem };
            return &m_ctx;
        }

        /// Records the osThreadId_t returned by osThreadNew(), so
        /// stackHighWaterMarkWords() has something to query later.
        void setTaskHandle(osThreadId_t handle) { m_task_handle = handle; }

        /// The RTOS2 task handle for this process, or NULL if it
        /// hasn't been spawned (or spawning failed).
        osThreadId_t taskHandle() const { return m_task_handle; }

        /**
         * @brief Live stack high-water-mark reading for this process's
         * task, in words -- i.e. the minimum free stack ever observed so
         * far (see freertos-stack-usage-estimation-brief.md for caveats).
         * Returns CSP_STACK_HWM_UNAVAILABLE if the process hasn't been
         * spawned yet.
         *
         * osThreadGetStackSpace() (CMSIS-RTOS2) returns BYTES, unlike
         * FreeRTOS's uxTaskGetStackHighWaterMark() (words) -- divided by
         * sizeof(internal::csp_stack_word_t) here so this function's own
         * name/contract ("...Words") and every existing caller (e.g.
         * csp4cmsis_spn.cpp's stack-usage report, which multiplies this
         * return value back out by the same word size) keep working
         * unchanged.
         */
        uint32_t stackHighWaterMarkWords() const {
            if (m_task_handle == nullptr) {
                return CSP_STACK_HWM_UNAVAILABLE;
            }
            return osThreadGetStackSpace(m_task_handle) / sizeof(internal::csp_stack_word_t);
        }

    protected:
        // C++CSP Standard: The primary process logic.
        virtual void run() = 0;

        // C++CSP4CMSIS Extension: Called by the ThreadFuncWrapper upon completion.
        virtual void endProcess() {}

    private:
        // The RTOS2 wrapper function needs to access the protected run() method.
        friend void ::ThreadFuncWrapper(void* pvParameters);

        TaskCtx m_ctx{ nullptr, nullptr };
        osThreadId_t m_task_handle = nullptr;
    };

    /**
     * @brief Convenience base that gives a CSProcess its statically-allocated
     * stack buffer and TCB, sized at compile time via the template
     * parameter. All CSP4CMSIS process classes should derive from this
     * (instead of CSProcess directly) so that Run() can spawn them with a
     * static osThreadNew() call -- CSP4CMSIS performs no dynamic (heap)
     * allocation anywhere in task creation.
     *
     * As with CSProcess itself, instances must have static storage
     * duration (global or function-local `static`): the stack buffer and
     * TCB are members of the process object, so the object's lifetime
     * *is* the task's storage lifetime.
     *
     * @tparam StackWords Stack depth, in words (internal::csp_stack_word_t units), for
     * this process's task. Pick deliberately per process -- there is no
     * shared fallback. CSP_TYPICAL_STACK_WORDS is provided as a
     * documented starting point, not a default to rely on blindly.
     */
    template <size_t StackWords>
    class CSProcessStatic : public CSProcess {
    public:
        size_t stackWords() const final { return StackWords; }
        internal::csp_stack_word_t* stackBuffer() final { return m_stack; }
        void* taskBuffer() final {
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
            return &m_tcb;
#else
            return nullptr;
#endif
        }

    private:
        // alignas(8): ARM AAPCS requires 8-byte SP alignment at public
        // interfaces -- a plain csp_stack_word_t (uint32_t) array only
        // guarantees natural 4-byte alignment, which RTX5's osThreadNew()
        // validates and rejects outright (confirmed on real hardware:
        // svcRtxThreadNew()'s `(uint32_t)stack_mem & 7` check faulted two
        // of three CSProcess threads whose linker-assigned addresses
        // happened to land 4-byte-misaligned, while the third -- same
        // class, same stack size, just a different linker offset --
        // succeeded). The FreeRTOS adapter never checks this at all, so
        // the same latent misalignment silently passed there -- this is
        // a genuine portability bug in the library, not a backend-
        // specific concern, and belongs on both backends.
        alignas(8) internal::csp_stack_word_t m_stack[StackWords];
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
        internal::csp_static_thread_storage_t m_tcb;
#endif
    };

    // =============================================================
    // API 1.3: Priority resolution helper. Stack sizing has no
    // resolve-with-fallback step -- only priority does.
    // =============================================================
    inline osPriority_t resolveTaskPriority(const CSProcess& p, osPriority_t fallback) {
        osPriority_t pp = p.taskPriority();
        return (pp != CSP_PRIORITY_UNSPECIFIED) ? pp : fallback;
    }

} // namespace csp

namespace csp::internal {

    // Alias 'Process' to the new public name 'CSProcess'
    using Process = csp::CSProcess;

    // Type alias for convenience when passing process pointers
    using ProcessPtr = Process*;

    #define NullProcessPtr (static_cast<csp::internal::ProcessPtr>(NULL))

} // namespace csp::internal

#endif // CSP4CMSIS_PROCESS_H
