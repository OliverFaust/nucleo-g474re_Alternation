// --- barrier.cpp ---

#include "csp/barrier.h"
#include "csp/csp_critical.h"
#include "csp/csp_fatal.h"

namespace csp::internal {

// =============================================================
//  Barrier Implementation (see barrier.h)
// =============================================================

Barrier::Barrier(size_t N) : max_processes(N) {
    if (N == 0) fatal("CSP4CMSIS: Barrier: N must be >= 1");
    uint32_t max_tokens = (N > 1) ? (uint32_t)(N - 1) : 1U;
    release_[0].create(max_tokens, 0, "CspBarrier0");
    release_[1].create(max_tokens, 0, "CspBarrier1");
}

void Barrier::sync() {
    uint32_t p;
    bool last;
    {
        uint32_t s = csp_enter_critical();
        p = phase & 1U;
        count = count + 1;
        last = (count == max_processes);
        if (last) { count = 0; phase = phase + 1; }   // the next phase starts now
        csp_exit_critical(s);
    }
    if (last) {
        // Release exactly the N - 1 processes waiting in this phase.
        for (size_t i = 1; i < max_processes; ++i) release_[p].release();
    } else {
        if (!release_[p].acquire(osWaitForever)) fatal("CSP4CMSIS: Barrier: wait failed");
    }
}

} // namespace csp::internal
