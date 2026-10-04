#include "csp/alt_channel_sync.h"
#include "csp/csp_critical.h"
#include "csp/csp_fatal.h"
#include <cstring>

// Rendezvous core, one-winner ALT with re-verification (see the header and
// docs/formal/alt_owrv_extended.csp). Every "Crit c;" scope below is one CSP
// critical section; RTOS calls and element copies happen outside them.

namespace csp::internal {

namespace {
    struct Crit {
        uint32_t saved;
        Crit() : saved(csp_enter_critical()) {}
        ~Crit() { csp_exit_critical(saved); }
        Crit(const Crit&) = delete;
        Crit& operator=(const Crit&) = delete;
    };
    struct Wake {
        osThreadId_t thread = nullptr;
        uint32_t     flag   = 0;
        void signal() const { if (thread != nullptr) (void)osThreadFlagsSet(thread, flag); }
    };
}

RendezvousCore::RendezvousCore(size_t element_size) : size_(element_size) {
    in_.serial.create(1, 1, "CspRvIn");
    out_.serial.create(1, 1, "CspRvOut");
}

// Blocks until `done` is set by the partner (which then sets RENDEZVOUS_FLAG).
// Stale RENDEZVOUS_FLAGs only cause another check.
void RendezvousCore::waitDone(volatile bool& done) {
    for (;;) {
        bool d;
        { Crit c; d = done; }
        if (d) return;
        uint32_t r = osThreadFlagsWait(RENDEZVOUS_FLAG, osFlagsWaitAny, osWaitForever);
        if ((r & osFlagsError) != 0U) fatal("CSP4CMSIS: rendezvous: osThreadFlagsWait() failed");
    }
}

// ---------------------------------------------------------------------------
// Plain operations
// ---------------------------------------------------------------------------
void RendezvousCore::output(const void* src) {
    if (!out_.serial.acquire(osWaitForever)) fatal("CSP4CMSIS: rendezvous: output() wait failed");
    osThreadId_t reader = nullptr; void* dst = nullptr; Wake wake;
    {
        Crit c;
        if (in_.pend_thread != nullptr) {                  // a plain reader waits: copy to it
            reader = in_.pend_thread; dst = in_.pend_data; in_.pend_thread = nullptr;
        } else {                                           // offer; an ALT reader re-verifies
            out_.pend_thread = osThreadGetId(); out_.pend_data = const_cast<void*>(src); out_.pend_done = false;
            if (in_.alt != nullptr) { wake.thread = in_.alt->ownerThread(); wake.flag = in_.alt_flag; }
        }
    }
    if (reader != nullptr) {
        std::memcpy(dst, src, size_);
        { Crit c; in_.pend_done = true; }
        (void)osThreadFlagsSet(reader, RENDEZVOUS_FLAG);
    } else {
        wake.signal();
        waitDone(out_.pend_done);
    }
    out_.serial.release();
}

void RendezvousCore::input(void* dst) {
    if (!in_.serial.acquire(osWaitForever)) fatal("CSP4CMSIS: rendezvous: input() wait failed");
    osThreadId_t writer = nullptr; const void* src = nullptr; Wake wake;
    {
        Crit c;
        if (out_.pend_thread != nullptr) {                 // a plain writer waits: copy from it
            writer = out_.pend_thread; src = out_.pend_data; out_.pend_thread = nullptr;
        } else {                                           // offer; an ALT writer re-verifies
            in_.pend_thread = osThreadGetId(); in_.pend_data = dst; in_.pend_done = false;
            if (out_.alt != nullptr) { wake.thread = out_.alt->ownerThread(); wake.flag = out_.alt_flag; }
        }
    }
    if (writer != nullptr) {
        std::memcpy(dst, src, size_);
        { Crit c; out_.pend_done = true; }
        (void)osThreadFlagsSet(writer, RENDEZVOUS_FLAG);
    } else {
        wake.signal();
        waitDone(in_.pend_done);
    }
    in_.serial.release();
}

// ---------------------------------------------------------------------------
// ALT reader (input guard)
// ---------------------------------------------------------------------------
bool RendezvousCore::inEnable(AltScheduler* alt, uint32_t flag, void* dst) {
    bool ready = false, conflict = false; Wake wake;
    {
        Crit c;
        if (out_.pend_thread != nullptr) {
            ready = true;                                  // plain writer pending
        } else if (out_.alt != nullptr && alt->enablingLocked() && out_.alt->claimableLocked()) {
            alt->claimLocked(flag);                        // ALT-vs-ALT: claim BOTH words
            out_.alt->claimLocked(out_.alt_flag);
            pair_src_ = out_.alt_data; pair_writer_ = out_.alt->ownerThread(); pair_done_ = false;
            wake.thread = pair_writer_; wake.flag = out_.alt_flag;
            ready = true;
        } else if (in_.alt != nullptr && in_.alt != alt) {
            conflict = true;
        } else {
            in_.alt = alt; in_.alt_flag = flag; in_.alt_data = dst;
        }
    }
    if (conflict) fatal("CSP4CMSIS: rendezvous channel: second ALTing reader (at most one per channel)");
    wake.signal();
    return ready;
}

bool RendezvousCore::inDisable(AltScheduler* alt) {
    Crit c;
    if (in_.alt == alt) { in_.alt = nullptr; in_.alt_data = nullptr; }
    return out_.pend_thread != nullptr || (out_.alt != nullptr && out_.alt->claimableLocked());
}

bool RendezvousCore::inActivate(AltScheduler* alt, uint32_t flag, void* dst) {
    enum { NONE, PAIR, PLAIN } kind = NONE;
    const void* src = nullptr; osThreadId_t writer = nullptr; Wake wake;
    {
        Crit c;
        if (alt->claimedForLocked(flag)) {                 // claimed pair (by either side)
            kind = PAIR; src = pair_src_; writer = pair_writer_;
        } else if (out_.pend_thread != nullptr) {          // take the pending plain writer
            kind = PLAIN; src = out_.pend_data; writer = out_.pend_thread; out_.pend_thread = nullptr;
        } else if (out_.alt != nullptr && out_.alt->claimableLocked()) {
            out_.alt->claimLocked(out_.alt_flag);          // claim the ALT writer now
            kind = PAIR; src = out_.alt_data; writer = out_.alt->ownerThread();
            pair_src_ = src; pair_writer_ = writer; pair_done_ = false;
            wake.thread = writer; wake.flag = out_.alt_flag;
        }
    }
    if (kind == NONE) return false;                        // partner went away: new round
    wake.signal();
    std::memcpy(dst, src, size_);                          // writer is blocked: buffers stable
    { Crit c; if (kind == PAIR) pair_done_ = true; else out_.pend_done = true; }
    (void)osThreadFlagsSet(writer, RENDEZVOUS_FLAG);
    return true;
}

// ---------------------------------------------------------------------------
// ALT writer (output guard)
// ---------------------------------------------------------------------------
bool RendezvousCore::outEnable(AltScheduler* alt, uint32_t flag, const void* src) {
    bool ready = false, conflict = false; Wake wake;
    {
        Crit c;
        if (in_.pend_thread != nullptr) {
            ready = true;                                  // plain reader pending
        } else if (in_.alt != nullptr && alt->enablingLocked() && in_.alt->claimableLocked()) {
            alt->claimLocked(flag);                        // ALT-vs-ALT: claim BOTH words
            in_.alt->claimLocked(in_.alt_flag);
            pair_src_ = src; pair_writer_ = alt->ownerThread(); pair_done_ = false;
            wake.thread = in_.alt->ownerThread(); wake.flag = in_.alt_flag;
            ready = true;
        } else if (out_.alt != nullptr && out_.alt != alt) {
            conflict = true;
        } else {
            out_.alt = alt; out_.alt_flag = flag; out_.alt_data = const_cast<void*>(src);
        }
    }
    if (conflict) fatal("CSP4CMSIS: rendezvous channel: second ALTing writer (at most one per channel)");
    wake.signal();
    return ready;
}

bool RendezvousCore::outDisable(AltScheduler* alt) {
    Crit c;
    if (out_.alt == alt) { out_.alt = nullptr; out_.alt_data = nullptr; }
    return in_.pend_thread != nullptr || (in_.alt != nullptr && in_.alt->claimableLocked());
}

bool RendezvousCore::outActivate(AltScheduler* alt, uint32_t flag, const void* src) {
    enum { NONE, PAIR, PLAIN } kind = NONE;
    osThreadId_t reader = nullptr; void* dst = nullptr; Wake wake;
    {
        Crit c;
        if (alt->claimedForLocked(flag)) {                 // claimed pair: the reader copies
            kind = PAIR;
        } else if (in_.pend_thread != nullptr) {           // take the pending plain reader
            kind = PLAIN; reader = in_.pend_thread; dst = in_.pend_data; in_.pend_thread = nullptr;
        } else if (in_.alt != nullptr && in_.alt->claimableLocked()) {
            in_.alt->claimLocked(in_.alt_flag);            // claim the ALT reader now
            pair_src_ = src; pair_writer_ = alt->ownerThread(); pair_done_ = false;
            wake.thread = in_.alt->ownerThread(); wake.flag = in_.alt_flag;
            kind = PAIR;
        }
    }
    if (kind == NONE) return false;                        // partner went away: new round
    if (kind == PLAIN) {                                   // the ALT performs the transfer
        std::memcpy(dst, src, size_);
        { Crit c; in_.pend_done = true; }
        (void)osThreadFlagsSet(reader, RENDEZVOUS_FLAG);
        return true;
    }
    wake.signal();
    waitDone(pair_done_);                                  // the reader copies from `src`
    return true;
}

bool RendezvousCore::pending() {
    Crit c;
    return out_.pend_thread != nullptr || out_.alt != nullptr;
}

bool RendezvousCore::space_available() {
    Crit c;
    return in_.pend_thread != nullptr || in_.alt != nullptr;
}

} // namespace csp::internal
