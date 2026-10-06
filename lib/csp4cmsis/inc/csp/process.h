// --- process.h: CSProcess, CSProcessStatic<N> ---
#ifndef CSP4CMSIS_PROCESS_H
#define CSP4CMSIS_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "csp_rtos_static.h"

extern "C" {
    void ThreadFuncWrapper(void* pvParameters);
}

// Suggested stack depth (words) for typical CSP4CMSIS processes: a documented
// starting point for CSProcessStatic<N>, not a default. Overridable with -D.
#ifndef CSP_TYPICAL_STACK_WORDS
#define CSP_TYPICAL_STACK_WORDS 256
#endif

namespace csp {

    /// taskPriority()'s "no own priority": the process runs at the
    /// composition priority given to Run().
    inline constexpr osPriority_t CSP_PRIORITY_UNSPECIFIED = osPriorityError;

    /// stackHighWaterMarkWords() before the process has been started.
    inline constexpr uint32_t CSP_STACK_HWM_UNAVAILABLE = 0xFFFFFFFFUL;

    /// Smallest stack CSProcessStatic<N> accepts, in words (256 bytes): a
    /// smaller N is almost certainly a byte count or a typo.
    inline constexpr size_t CSP_MIN_STACK_WORDS = 64;

    class CSProcess;
    template <typename... Processes> class ParallelHelper;

    namespace internal {
        /// Thread argument of ThreadFuncWrapper: the process to run and the
        /// semaphore to release when it returns (TerminatingNetwork).
        struct TaskCtx {
            CSProcess* process;
            osSemaphoreId_t completion_sem;
        };
        osPriority_t resolveTaskPriority(const CSProcess& p, osPriority_t fallback);
    }

    /**
     * @brief Base class of every process. Derive from CSProcessStatic<N>,
     * which supplies the stack, and implement run().
     */
    class CSProcess {
    public:
        virtual ~CSProcess() = default;

        /// Thread name (debugger, RTOS-aware views). Override it.
        virtual const char* name() const { return "csp_task"; }

        /// Stack size in words (the N of CSProcessStatic<N>).
        virtual size_t stackWords() const = 0;

        /// Own thread priority, overriding the composition priority given to
        /// Run(); CSP_PRIORITY_UNSPECIFIED (the default) to use that one.
        virtual osPriority_t taskPriority() const { return CSP_PRIORITY_UNSPECIFIED; }

        /// The process's thread, or NULL before Run() started it.
        osThreadId_t taskHandle() const { return m_task_handle; }

        /**
         * @brief Smallest free stack the thread has had so far, in words
         * (osThreadGetStackSpace(), which returns bytes, divided by the word
         * size); CSP_STACK_HWM_UNAVAILABLE before the process was started.
         */
        uint32_t stackHighWaterMarkWords() const {
            if (m_task_handle == nullptr) {
                return CSP_STACK_HWM_UNAVAILABLE;
            }
            return osThreadGetStackSpace(m_task_handle) / sizeof(internal::csp_stack_word_t);
        }

    protected:
        /// The process body. Returning from it ends the thread.
        virtual void run() = 0;

    private:
        // Spawn plumbing, for Run() (ParallelHelper) and the thread entry
        // function only. The TaskCtx lives in the process object because
        // that object has static storage duration; a ParallelHelper may be
        // a temporary.
        template <typename... Processes> friend class ParallelHelper;
        friend void ::ThreadFuncWrapper(void* pvParameters);

        /// The stack, at least stackWords() words, owned by the process;
        /// nullptr with dynamic allocation (the RTOS allocates it).
        virtual internal::csp_stack_word_t* stackBuffer() = 0;
        /// The thread control block (static allocation), or nullptr.
        virtual void* taskBuffer() = 0;

        internal::TaskCtx* prepareTaskCtx(osSemaphoreId_t completion_sem) {
            m_ctx = internal::TaskCtx{ this, completion_sem };
            return &m_ctx;
        }
        void setTaskHandle(osThreadId_t handle) { m_task_handle = handle; }

        internal::TaskCtx m_ctx{ nullptr, nullptr };
        osThreadId_t m_task_handle = nullptr;
    };

    /**
     * @brief A process with a stack of StackWords words (4 bytes each on
     * Cortex-M). With static allocation (the default) the stack and the thread
     * control block are members of the object, so instances must have static
     * storage duration (namespace scope or function-local `static`): the object
     * is the thread's storage. With CSP4CMSIS_DYNAMIC_ALLOCATION the RTOS
     * allocates both when Run() starts the thread (CMSIS-RTOS2 implementations
     * such as Arm's FreeRTOS adapter accept a caller-provided stack only
     * together with a caller-provided control block).
     */
    template <size_t StackWords>
    class CSProcessStatic : public CSProcess {
        static_assert(StackWords >= CSP_MIN_STACK_WORDS,
                      "CSProcessStatic<N>: N is the stack size in WORDS (4 bytes each), at least "
                      "CSP_MIN_STACK_WORDS (64); e.g. CSProcessStatic<256> for 1 KB");
    public:
        size_t stackWords() const final { return StackWords; }

    private:
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
        internal::csp_stack_word_t* stackBuffer() final { return m_stack; }
        void* taskBuffer() final { return &m_tcb; }

        // alignas(8): AAPCS requires an 8-byte aligned stack; RTX5's
        // osThreadNew() rejects a 4-byte aligned stack_mem.
        alignas(8) internal::csp_stack_word_t m_stack[StackWords];
        internal::csp_static_thread_storage_t m_tcb;
#else
        internal::csp_stack_word_t* stackBuffer() final { return nullptr; }   // the RTOS allocates it
        void* taskBuffer() final { return nullptr; }
#endif
    };

    namespace internal {
        inline osPriority_t resolveTaskPriority(const CSProcess& p, osPriority_t fallback) {
            osPriority_t pp = p.taskPriority();
            return (pp != CSP_PRIORITY_UNSPECIFIED) ? pp : fallback;
        }
    }

} // namespace csp

// CSP_PRIORITY_UNSPECIFIED and CSP_STACK_HWM_UNAVAILABLE were global macros
// before 3.0: still usable without the csp:: prefix.
using csp::CSP_PRIORITY_UNSPECIFIED;
using csp::CSP_STACK_HWM_UNAVAILABLE;

#endif // CSP4CMSIS_PROCESS_H
