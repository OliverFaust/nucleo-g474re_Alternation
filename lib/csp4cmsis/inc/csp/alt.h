#ifndef CSP4CMSIS_ALT_H
#define CSP4CMSIS_ALT_H

#include "cmsis_os2.h"
#include "csp_rtos_static.h"
#include <stddef.h>
#include <initializer_list>
#include <type_traits>
#include "time.h"
#include "csp_fatal.h"

// =============================================================================
// Thread-flag allocation (CMSIS-RTOS2 thread flags, per thread)
//
//   bit  0       RENDEZVOUS_FLAG (alt_channel_sync.h): plain blocking rendezvous
//   bits 8..23   ALT wakeups: guard i of the Alternative a thread is currently
//                selecting on is signalled with bit (8 + i); MAX_GUARDS = 16
//   bits 1..7,
//   bits 24..30  free for the application
//   bit  31      invalid in CMSIS-RTOS2 (error-code range)
//
// select() clears its bits at the start of every round and re-verifies
// every wakeup (disable() reports readiness again), so a late or stale
// signal is harmless: it only starts a new round.
//
// FreeRTOS backend: the CMSIS-RTOS2 adapter implements thread flags with the
// task notification at index 0 (xTaskNotify/xTaskNotifyWait). Native FreeRTOS
// code that uses index-0 notifications on the same task (xTaskNotifyGive,
// ulTaskNotifyTake, stream/message buffers, ...) collides with CSP4CMSIS
// processes. Keep native notifications off CSP process threads, or move them
// to another index (configTASK_NOTIFICATION_ARRAY_ENTRIES > 1).
// =============================================================================
#define CSP4CMSIS_ALT_FLAG_SHIFT   8U
#define CSP4CMSIS_ALT_MAX_GUARDS   16U

namespace csp {
    // Forward declarations
    template <typename T> class Chanin;
    template <typename T> class Chanout;

    namespace internal {
        class AltScheduler;

        constexpr uint32_t ALT_FLAG_SHIFT = CSP4CMSIS_ALT_FLAG_SHIFT;
        constexpr uint32_t ALT_MAX_GUARDS = CSP4CMSIS_ALT_MAX_GUARDS;
        constexpr uint32_t ALT_FLAG_MASK  = ((1UL << ALT_MAX_GUARDS) - 1UL) << ALT_FLAG_SHIFT;
        /// Thread-flag bit used to wake the selecting thread for guard index i.
        constexpr uint32_t altFlag(size_t i) { return 1UL << (ALT_FLAG_SHIFT + i); }

        /**
         * @brief Base Guard Interface.
         *
         * select() protocol (one-winner with re-verification, "OWRV"; see
         * docs/formal/alt_owrv_extended.csp), per round:
         *   1. The ALT's state word becomes ENABLING. enable(alt, flag) in
         *      fairness order until one returns true (ready now). A guard
         *      that returns false registers, so that its partner signals
         *      `flag` to alt->ownerThread() when it may be ready. enable()
         *      must check and register atomically.
         *   2. If none was ready and the ALT was not claimed meanwhile:
         *      state WAITING, wait for any enabled guard's flag.
         *   3. disable() every guard that was enabled this round (and only
         *      those); it returns whether the guard can complete NOW
         *      (re-verification: a stale flag just starts a new round).
         *   4. If a rendezvous partner CLAIMED this ALT for one of its
         *      guards (state word), that guard wins; otherwise the first
         *      ready guard in fairness order; none -> new round.
         *   5. activate(): commit. false = the partner went away or a
         *      competitor took the item/space; start a new round.
         *   Readiness must only be reported while the partner's offer stands
         *   and cannot be withdrawn except by the partner making progress
         *   (otherwise select() could retry without anybody making progress).
         */
        class Guard {
        public:
            virtual bool enable(AltScheduler* alt, uint32_t flag) = 0;
            virtual bool disable() = 0;
            virtual bool activate() = 0;
            virtual ~Guard() = default;
        };

        class AltScheduler {
        public:
            /// One-winner state word. Read and written ONLY inside CSP
            /// critical sections (csp_critical.h), by this ALT and by
            /// rendezvous partners that claim it.
            enum State : uint8_t { IDLE, ENABLING, WAITING, CLAIMED };
        private:
            // Thread currently running select() on this ALT; wakeups are
            // thread flags sent to it (no RTOS object per Alternative).
            osThreadId_t owner = nullptr;
            volatile State   state_        = IDLE;
            volatile uint32_t claimed_flag_ = 0;   // guard flag a partner claimed (state_ == CLAIMED)
            // Timeouts (no RTOS timer): the tick count when select() started,
            // and the shortest delay of the timeout guards enabled in the
            // current round (NO_LIMIT: none). Owner thread only.
            static constexpr uint32_t NO_LIMIT = 0xFFFFFFFFUL;
            uint32_t start_tick_ = 0;
            uint32_t wait_limit_ = NO_LIMIT;
        public:
            // --- state word, inside a CSP critical section only ---
            bool claimableLocked() const { return state_ == ENABLING || state_ == WAITING; }
            bool enablingLocked() const  { return state_ == ENABLING; }
            bool claimedForLocked(uint32_t flag) const { return state_ == CLAIMED && claimed_flag_ == flag; }
            /// Commits this ALT to the guard with `flag` (it must select it).
            void claimLocked(uint32_t flag) { state_ = CLAIMED; claimed_flag_ = flag; }

            AltScheduler() = default;
            AltScheduler(const AltScheduler&) = delete;
            AltScheduler& operator=(const AltScheduler&) = delete;

            /**
             * @brief The core ALT selection logic (see Guard).
             * @param offset Used for Fair Alts to prevent starvation.
             * @return The index of the selected guard.
             */
            unsigned int select(Guard** guardArray, size_t amount, size_t offset = 0);

            /// Thread flag for `flag` to the selecting thread (ISR-safe).
            /// Callers must not hold a CSP critical section.
            void wakeUp(uint32_t flag);
            osThreadId_t ownerThread() const { return owner; }

            /// Ticks since this select() started. Unsigned difference: correct
            /// across the wrap of the 32-bit tick count. Owner thread only.
            uint32_t elapsed() const { return osKernelGetTickCount() - start_tick_; }
            /// A timeout guard that is not ready yet: wait at most until
            /// `delay` ticks after the start of select(). Owner thread only.
            void limitWait(uint32_t delay) { if (delay < wait_limit_) wait_limit_ = delay; }
        };

        /**
         * @brief Timeout guard: ready once `delay` ticks have passed since
         * the start of the select() it takes part in. No RTOS timer: the
         * deadline is fixed when select() starts, and select() waits for its
         * thread flags at most until the earliest deadline of its enabled
         * timeout guards (docs/formal/alt_timeout_deadline.csp). A delay of
         * 0 is ready at once and never waits.
         */
        class TimeoutGuard : public Guard {
        private:
            uint32_t      delay_ticks;
            AltScheduler* alt = nullptr;
        public:
            explicit TimeoutGuard(csp::Time delay)
                // osWaitForever (0xFFFFFFFF) is not a duration
                : delay_ticks(delay.to_ticks() < 0xFFFFFFFFUL ? delay.to_ticks() : 0xFFFFFFFEUL) {}
            bool enable(AltScheduler* a, uint32_t) override {
                alt = a;
                if (a->elapsed() >= delay_ticks) return true;
                a->limitWait(delay_ticks);
                return false;
            }
            bool disable() override { return alt->elapsed() >= delay_ticks; }   // expired?
            bool activate() override { return true; }
        };

    } // namespace internal

    /**
     * @brief Glue logic for Pipe Syntax (chan | msg).
     */
    template <typename T, typename ChanType>
    struct ChannelBinding {
        ChanType& channel;
        T& data_ref;

        ChannelBinding(ChanType& c, T& d) : channel(c), data_ref(d) {}

        internal::Guard* getInternalGuard() const {
            return channel.getGuard(data_ref);
        }
    };

    /**
     * @brief Public Wrapper for Guards.
     */
    class Guard {
    public:
        internal::Guard* internal_guard_ptr = nullptr;
        virtual ~Guard() = default;
    protected:
        Guard(internal::Guard* internal_ptr) : internal_guard_ptr(internal_ptr) {}
    };

    /**
     * @brief Relative timeout for an Alternative: selected when `delay`
     * ticks have passed since select() started and no guard listed before
     * it is ready. Plain data, no RTOS object.
     */
    class RelTimeoutGuard : public Guard {
    private:
        internal::TimeoutGuard timer_storage;
    public:
        RelTimeoutGuard(csp::Time delay)
            : Guard(&timer_storage), timer_storage(delay) {}
        ~RelTimeoutGuard() override = default;
        // internal_guard_ptr points into this object: not copyable/movable.
        RelTimeoutGuard(const RelTimeoutGuard&) = delete;
        RelTimeoutGuard& operator=(const RelTimeoutGuard&) = delete;
    };

    /**
     * @brief The Alternative (ALT) construct.
     * Manages multiple guards and selects the first one available.
     */
    class Alternative {
    private:
        static const size_t MAX_GUARDS = internal::ALT_MAX_GUARDS;
        internal::Guard* internal_guards[MAX_GUARDS];
        size_t num_guards = 0;
        internal::AltScheduler internal_alt;
        size_t fair_select_start_index = 0;

    public:
        Alternative() : num_guards(0) {}

        // Bindings are taken by reference (never copied): a copied
        // RelTimeoutGuard would leave the Alternative pointing into the
        // copy. The constraint keeps this template from hijacking copy
        // construction.
        template <typename... Bindings,
                  typename = std::enable_if_t<(sizeof...(Bindings) > 0) &&
                      !(std::is_same_v<std::decay_t<Bindings>, Alternative> || ...)>>
        Alternative(Bindings&&... bindings) : num_guards(0) {
            (addBinding(bindings), ...);
        }

        Alternative(std::initializer_list<internal::Guard*> guard_list);
        Alternative(std::initializer_list<csp::Guard*> guard_list);

        /**
         * @brief Priority Select: Always checks guards in the order they were added.
         */
        int priSelect();

        /**
         * @brief Fair Select: Rotates the starting index to ensure all guards get a turn.
         */
        int fairSelect();

        // --- Binding Helpers ---

        template <typename T>
        void addBinding(const ChannelBinding<T, Chanin<T>>& b) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = b.getInternalGuard();
            }
        }

        template <typename T>
        void addBinding(const ChannelBinding<const T, Chanout<T>>& b) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = b.getInternalGuard();
            }
        }

        void addBinding(RelTimeoutGuard& tg) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = tg.internal_guard_ptr;
            }
        }

        void addBinding(internal::Guard* g) {
            if (num_guards < MAX_GUARDS) {
                internal_guards[num_guards++] = g;
            }
        }
    };
}

#endif // CSP4CMSIS_ALT_H
