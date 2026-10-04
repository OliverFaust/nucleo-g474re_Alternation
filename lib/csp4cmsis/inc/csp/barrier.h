// --- barrier.h ---
#ifndef CSP4CMSIS_BARRIER_H
#define CSP4CMSIS_BARRIER_H

#include "cmsis_os2.h"
#include "csp_semaphore.h"
#include <stddef.h> // For size_t
#include <stdint.h>

namespace csp {

    namespace internal {

        /**
         * @brief A reusable synchronization point where a fixed number of processes
         * must arrive before any are allowed to proceed.
         *
         * The arrival count and the phase are updated in a CSP critical
         * section. Waiters of phase p block on release_[p % 2]; the last
         * arrival of phase p starts phase p + 1 and releases exactly N - 1
         * tokens of release_[p % 2]. A process that runs ahead into phase
         * p + 1 waits on the other semaphore, so it can never take a token
         * meant for a slow waiter of phase p. Both semaphores have static
         * control blocks under CSP4CMSIS_STATIC_ALLOCATION (no RTOS heap).
         */
        class Barrier {
        private:
            const size_t max_processes;
            size_t   count = 0;      // CSP critical section
            uint32_t phase = 0;      // CSP critical section (parity selects release_)
            CspSemaphore release_[2];

        public:
            /**
             * @brief Constructs a barrier that requires N processes (N >= 1) to synchronize.
             */
            explicit Barrier(size_t N);
            ~Barrier() = default;
            Barrier(const Barrier&) = delete;
            Barrier& operator=(const Barrier&) = delete;

            /**
             * @brief Blocks the calling task until all N processes have arrived.
             */
            void sync();
        };

    } // namespace csp::internal

    // Alias in the main csp namespace for user-friendliness
    using Barrier = internal::Barrier;

} // namespace csp

#endif // CSP4CMSIS_BARRIER_H
