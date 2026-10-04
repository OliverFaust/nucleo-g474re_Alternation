#ifndef CSP4CMSIS_BUFFERED_CHANNEL_H
#define CSP4CMSIS_BUFFERED_CHANNEL_H

// =============================================================================
// BufferedChannel<T, SIZE, P> -- bounded buffer, statically allocated (2.0)
//
// Storage : `SIZE` elements of T inside the channel object (no RTOS queue,
//           no heap). T must be trivially copyable (moved with memcpy).
// Policies: Block      -- writers block while full, readers while empty
//           KeepNewest -- a write to a full channel overwrites the oldest
//                         element, atomically (one critical section)
//           KeepOldest -- a write to a full channel is dropped
//
// Concurrency design (see BUFFERED_CHANNEL_ANALYSIS.md, design B):
//  * The ring state (storage_, head_, count_) and the ALT registrations are
//    only touched inside csp_enter_critical()/csp_exit_critical().
//  * NO CMSIS-RTOS2 call is ever made inside such a section. Every operation
//    snapshots whom to wake inside the section and acts after leaving it.
//  * Blocking uses two counting semaphores with static control blocks
//    (CSP4CMSIS_STATIC_ALLOCATION):
//      items  : tokens <= count_          (released after a push,
//                                          acquired before a pop)
//      spaces : tokens <= SIZE - count_   (Block only; acquired before a
//                                          push, released after a pop)
//    KeepNewest overwrite / KeepOldest drop leave count_ unchanged and
//    move no token. Readers always pop the current oldest element.
//  * ALT readiness is the SEMAPHORE count (items / spaces), not the ring
//    state: "ready" can only be observed once the token exists, so a partner
//    preempted between its ring update and its token release makes the ALT
//    block, never spin. Protocol (R/C = ALT side, P/T/S/G = partner side):
//      enable(): R register {thread, flag} (critical section), then
//                C readiness = osSemaphoreGetCount(...) > 0
//      partner : P ring update (critical section), T osSemaphoreRelease(),
//                S snapshot of the registration (critical section), G signal
//    A lost wakeup would need C before T and S before R; with R before C and
//    T before S that is a cycle, i.e. impossible.
//    At most ONE ALTing reader and ONE ALTing writer (Block) per channel -- a
//    second, different thread is a fatal error. Any number of blocking
//    readers/writers is allowed. activate() takes the token with timeout 0;
//    it can only fail if another reader/writer took that token in between
//    (i.e. made progress); select() then starts a new round, which reads the
//    updated count.
//  * putFromISR(): never blocks; Block policy fails if full. ISR priority
//    must be numerically >= CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY
//    (masked by the critical section).
//
// Masked copy -- interrupt latency:
//    Every element copy (output(), input(), putFromISR(), ALT activate())
//    runs INSIDE the CSP critical section, i.e. with BASEPRI raised: all
//    interrupts at or below CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY are
//    held off for the duration of one memcpy of sizeof(T) bytes, plus a
//    constant overhead. Interrupts above that priority are not affected.
//    For small elements (<= 64 bytes) this is comparable to an RTOS queue
//    operation (FreeRTOS also copies inside its own critical section).
//    csp::IsrChanout<T> (the only way to call putFromISR(), obtained from
//    SamplingBufferedChannel::isrWriter()) enforces this at compile time:
//    sizeof(T) <= CSP4CMSIS_ISR_MAX_ELEMENT_SIZE (default 64, public_channel.h;
//    raise it with -D if the latency is acceptable). Task-side operations
//    are not limited.
//    For large elements, do NOT buffer the payload: keep payloads in a
//    statically allocated pool and send an index or pointer instead, e.g.
//      static Frame pool[N];                              // payloads
//      SamplingBufferedChannel<uint8_t, N> filled;        // ISR -> task: index
//      SamplingBufferedChannel<uint8_t, N> free_slots;    // task -> ISR: index
//    so the masked copy is one byte; ownership of pool[i] moves with its
//    index. (An ISR cannot block on input(), so it must only use indices it
//    already owns -- e.g. a pre-assigned ping-pong pair -- until a
//    non-blocking ISR read is added.)
// =============================================================================

#include "cmsis_os2.h"
#include "csp_critical.h"
#include "csp_fatal.h"
#include "csp_rtos_static.h"
#include "csp_semaphore.h"
#include "channel_base.h"
#include "alt.h"
#include <cstring>
#include <cstddef>
#include <type_traits>

/// API generation of BufferedChannel (2 = static ring buffer, SIZE template
/// parameter). Undefined in 1.x.
#define CSP4CMSIS_BUFFERED_CHANNEL_API 2

namespace csp::internal {

    /// Whom to wake after leaving a critical section.
    struct AltWake {
        osThreadId_t thread = nullptr;
        uint32_t     flag   = 0;
        void signal() const { if (thread != nullptr) (void)osThreadFlagsSet(thread, flag); }
    };

    // CspSemaphore: csp_semaphore.h (static control block under CSP4CMSIS_STATIC_ALLOCATION).

    struct NoSemaphore {
        void create(uint32_t, uint32_t, const char*) {}
        void release() {}
        bool available() const { return true; }
    };

    template <typename T, size_t SIZE, csp::BufferPolicy P> class BufferedInputGuard;
    template <typename T, size_t SIZE, csp::BufferPolicy P> class BufferedOutputGuard;

    template <typename T, size_t SIZE, csp::BufferPolicy P = csp::BufferPolicy::Block>
    class BufferedChannel : public internal::BaseAltChan<T>, public internal::IsrSink<T>
    {
        static_assert(SIZE > 0, "BufferedChannel: SIZE must be > 0");
        static_assert(std::is_trivially_copyable_v<T>,
                      "BufferedChannel: T must be trivially copyable (elements are copied with memcpy)");

        static constexpr bool kBlock = (P == csp::BufferPolicy::Block);

        friend class BufferedInputGuard<T, SIZE, P>;
        friend class BufferedOutputGuard<T, SIZE, P>;

    private:
        // --- ring buffer (critical-section protected) ---
        alignas(T) unsigned char storage_[SIZE * sizeof(T)];
        size_t          head_  = 0;        // index of the oldest element
        volatile size_t count_ = 0;        // elements stored

        // --- ALT registrations (critical-section protected) ---
        AltWake alt_reader_;
        AltWake alt_writer_;               // Block only

        CspSemaphore items_;
        std::conditional_t<kBlock, CspSemaphore, NoSemaphore> spaces_;

        unsigned char* at(size_t i) { return storage_ + (i % SIZE) * sizeof(T); }

        // Inside a critical section: append / remove one element.
        void pushLocked(const T* src) {
            std::memcpy(at(head_ + count_), src, sizeof(T));
            count_ = count_ + 1;
        }
        void popLocked(T* dst) {
            std::memcpy(dst, at(head_), sizeof(T));
            head_  = (head_ + 1) % SIZE;
            count_ = count_ - 1;
        }
        // S: snapshot a registration (after the token was released).
        AltWake snapshot(const AltWake& slot) {
            uint32_t s = csp_enter_critical();
            AltWake w = slot;
            csp_exit_critical(s);
            return w;
        }

        // Block: push after a `spaces` token has been taken.  P, T, S, G.
        void pushWithToken(const T* src) {
            { uint32_t s = csp_enter_critical(); pushLocked(src); csp_exit_critical(s); }
            items_.release();
            snapshot(alt_reader_).signal();
        }
        // Pop after an `items` token has been taken.  P, T, S, G.
        void popWithToken(T* dst) {
            { uint32_t s = csp_enter_critical(); popLocked(dst); csp_exit_critical(s); }
            if constexpr (kBlock) {
                spaces_.release();
                snapshot(alt_writer_).signal();
            }
        }

        // KeepNewest / KeepOldest write, task or ISR: never blocks.
        void offer(const T* src) {
            bool pushed = false;
            {
                uint32_t s = csp_enter_critical();
                if (count_ < SIZE) {
                    pushLocked(src);
                    pushed = true;
                } else if constexpr (P == csp::BufferPolicy::KeepNewest) {
                    // Full: the oldest slot becomes the newest, in place
                    // (count_ and the tokens are unchanged).
                    std::memcpy(at(head_), src, sizeof(T));
                    head_ = (head_ + 1) % SIZE;
                }
                // KeepOldest and full: drop.
                csp_exit_critical(s);
            }
            if (pushed) {
                items_.release();
                snapshot(alt_reader_).signal();
            }
        }

        // --- ALT registration: R (critical section), then C (token count) ---
        template <typename Sem>
        bool altRegister(AltWake& slot, osThreadId_t t, uint32_t flag, const Sem& tokens, bool reader) {
            bool conflict = false;
            {
                uint32_t s = csp_enter_critical();
                if (slot.thread != nullptr && slot.thread != t) conflict = true;
                else { slot.thread = t; slot.flag = flag; }
                csp_exit_critical(s);
            }
            if (conflict)
                fatal(reader ? "CSP4CMSIS: BufferedChannel: second ALTing reader (at most one per channel)"
                             : "CSP4CMSIS: BufferedChannel: second ALTing writer (at most one per channel)");
            return tokens.available();
        }
        template <typename Sem>
        bool altUnregister(AltWake& slot, osThreadId_t t, const Sem& tokens) {
            {
                uint32_t s = csp_enter_critical();
                if (slot.thread == t) slot = AltWake{};
                csp_exit_critical(s);
            }
            return tokens.available();
        }

    public:
        BufferedChannel() {
            items_.create(SIZE, 0, "CspBufItems");
            spaces_.create(SIZE, SIZE, "CspBufSpaces");
        }
        ~BufferedChannel() override = default;

        // Guards and registrations point at this object.
        BufferedChannel(const BufferedChannel&) = delete;
        BufferedChannel& operator=(const BufferedChannel&) = delete;
        BufferedChannel(BufferedChannel&&) = delete;
        BufferedChannel& operator=(BufferedChannel&&) = delete;

        static constexpr size_t capacity() { return SIZE; }

        // --- BaseAltChan ---
        bool pending() override { return count_ > 0; }

        /// Block: a write would not block now. KeepNewest/KeepOldest: always true.
        bool space_available() override {
            if constexpr (kBlock) return count_ < SIZE;
            else return true;
        }

        /// Never blocks. Block: false if full. KeepNewest/KeepOldest: true.
        bool putFromISR(const T& data) override {
            if constexpr (kBlock) {
                if (!spaces_.acquire(0)) return false;
                pushWithToken(&data);
                return true;
            } else {
                offer(&data);
                return true;
            }
        }

        void input(T* const dest) override {
            if (!items_.acquire(osWaitForever)) fatal("CSP4CMSIS: BufferedChannel: input() wait failed");
            popWithToken(dest);
        }

        void output(const T* const source) override {
            if constexpr (kBlock) {
                if (!spaces_.acquire(osWaitForever)) fatal("CSP4CMSIS: BufferedChannel: output() wait failed");
                pushWithToken(source);
            } else {
                offer(source);
            }
        }

        void beginExtInput(T* const dest) override { this->input(dest); }
        void endExtInput() override { }

        // --- Guard factories (guard lives in the caller's handle slot) ---
        internal::Guard* getInputGuard(GuardSlot& slot, T& dest) override {
            return slot.emplace<BufferedInputGuard<T, SIZE, P>>(this, &dest);
        }
        internal::Guard* getOutputGuard(GuardSlot& slot, const T& source) override {
            return slot.emplace<BufferedOutputGuard<T, SIZE, P>>(this, &source);
        }
    };

    // =============================================================
    // Guards
    // =============================================================

    template <typename T, size_t SIZE, csp::BufferPolicy P>
    class BufferedInputGuard : public Guard {
    private:
        BufferedChannel<T, SIZE, P>* channel;
        T* dest;
        osThreadId_t owner = nullptr;
    public:
        BufferedInputGuard(BufferedChannel<T, SIZE, P>* c, T* d) : channel(c), dest(d) {}

        bool enable(AltScheduler* alt, uint32_t flag) override {
            owner = alt->ownerThread();
            return channel->altRegister(channel->alt_reader_, owner, flag, channel->items_, true);
        }
        bool disable() override { return channel->altUnregister(channel->alt_reader_, owner, channel->items_); }
        bool activate() override {
            if (!channel->items_.acquire(0)) return false;   // another reader took the token
            channel->popWithToken(dest);
            return true;
        }
    };

    template <typename T, size_t SIZE, csp::BufferPolicy P>
    class BufferedOutputGuard : public Guard {
    private:
        BufferedChannel<T, SIZE, P>* channel;
        const T* source;
        osThreadId_t owner = nullptr;
        static constexpr bool kBlock = (P == csp::BufferPolicy::Block);
    public:
        BufferedOutputGuard(BufferedChannel<T, SIZE, P>* c, const T* s) : channel(c), source(s) {}

        bool enable(AltScheduler* alt, uint32_t flag) override {
            if constexpr (!kBlock) { (void)alt; (void)flag; return true; }   // never blocks
            else {
                owner = alt->ownerThread();
                return channel->altRegister(channel->alt_writer_, owner, flag, channel->spaces_, false);
            }
        }
        bool disable() override {
            if constexpr (!kBlock) return true;
            else return channel->altUnregister(channel->alt_writer_, owner, channel->spaces_);
        }
        bool activate() override {
            if constexpr (!kBlock) { channel->offer(source); return true; }
            else {
                if (!channel->spaces_.acquire(0)) return false;   // another writer took the token
                channel->pushWithToken(source);
                return true;
            }
        }
    };

} // namespace csp::internal

#endif // CSP4CMSIS_BUFFERED_CHANNEL_H
