#ifndef ALT_CHANNEL_SYNC_H
#define ALT_CHANNEL_SYNC_H

// =============================================================================
// Rendezvous core: one-winner ALT with re-verification ("OWRV", 2.0).
// Model: docs/formal/alt_one_winner.csp, alt_owrv_extended.csp (ProB-checked).
//
//  * All channel state is protected by CSP critical sections (csp_critical.h),
//    never by an RTOS mutex; no RTOS call is made inside a section.
//  * A partner NEVER completes a communication on behalf of an ALT:
//      - a plain (blocking) writer/reader that finds no partner registers as
//        PENDING (its data stays in its own buffer) and blocks until the
//        other side has copied and released it;
//      - ALT-vs-ALT: whoever finds the other ALT registered and claimable
//        claims BOTH state words in one critical section; the READER side
//        then copies from the writer's buffer and releases the writer.
//  * The element copy happens outside the critical section: the partner is
//    blocked (pending or claimed), so both buffers are stable.
//  * At most one ALTing process per channel end (fatal otherwise); any number
//    of plain writers/readers (serialised per end by a counting semaphore).
//  * Only tasks use rendezvous channels (no ISR path; ISR -> process goes
//    through buffered channels).
//
// Thread flags: RENDEZVOUS_FLAG (bit 0) releases a blocked plain partner or
// an ALT writer waiting for the reader's copy; ALT wakeups use bits 8..23.
// =============================================================================

#include "cmsis_os2.h"
#include "alt.h"
#include "channel_base.h"
#include "csp_semaphore.h"
#include <cstddef>

/// Rendezvous/signal channels use the one-winner protocol with
/// re-verification (2.0). Undefined in 1.x.
#define CSP4CMSIS_ALT_PROTOCOL_OWRV 1

namespace csp::internal {

    static constexpr uint32_t RENDEZVOUS_FLAG = 0x00000001U;

    /// One end of a rendezvous channel (in = reader side, out = writer side).
    struct RvEnd {
        // plain (blocking) process waiting on this end
        osThreadId_t   pend_thread = nullptr;
        void*          pend_data   = nullptr;
        volatile bool  pend_done   = false;     // set by the side that copied
        // ALT registration on this end
        AltScheduler*  alt      = nullptr;
        uint32_t       alt_flag = 0;
        void*          alt_data = nullptr;
        // serialises plain operations on this end (one pending at a time)
        CspSemaphore   serial;
    };

    class RendezvousCore {
    private:
        RvEnd in_;
        RvEnd out_;
        // ALT-vs-ALT pair in progress (at most one: one ALT per end)
        const void*   pair_src_    = nullptr;
        osThreadId_t  pair_writer_ = nullptr;
        volatile bool pair_done_   = false;
        size_t        size_;

        static void waitDone(volatile bool& done);
    public:
        explicit RendezvousCore(size_t element_size);
        RendezvousCore(const RendezvousCore&) = delete;
        RendezvousCore& operator=(const RendezvousCore&) = delete;

        // plain (blocking) operations
        void output(const void* src);
        void input(void* dst);

        // ALT guard operations (flag = the guard's thread flag in `alt`)
        bool inEnable(AltScheduler* alt, uint32_t flag, void* dst);
        bool inDisable(AltScheduler* alt);
        bool inActivate(AltScheduler* alt, uint32_t flag, void* dst);
        bool outEnable(AltScheduler* alt, uint32_t flag, const void* src);
        bool outDisable(AltScheduler* alt);
        bool outActivate(AltScheduler* alt, uint32_t flag, const void* src);

        // informational (racy outside the channel's own use)
        bool pending();
        bool space_available();
    };

    class ChanInGuard : public Guard {
    private:
        RendezvousCore* core;
        void*           dest;
        AltScheduler*   alt  = nullptr;
        uint32_t        flag = 0;
    public:
        ChanInGuard(RendezvousCore* c, void* d) : core(c), dest(d) {}
        bool enable(AltScheduler* a, uint32_t f) override { alt = a; flag = f; return core->inEnable(a, f, dest); }
        bool disable() override { return core->inDisable(alt); }
        bool activate() override { return core->inActivate(alt, flag, dest); }
    };

    class ChanOutGuard : public Guard {
    private:
        RendezvousCore* core;
        const void*     source;
        AltScheduler*   alt  = nullptr;
        uint32_t        flag = 0;
    public:
        ChanOutGuard(RendezvousCore* c, const void* s) : core(c), source(s) {}
        bool enable(AltScheduler* a, uint32_t f) override { alt = a; flag = f; return core->outEnable(a, f, source); }
        bool disable() override { return core->outDisable(alt); }
        bool activate() override { return core->outActivate(alt, flag, source); }
    };

} // namespace csp::internal
#endif
