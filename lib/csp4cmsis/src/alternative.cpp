#include "csp/alt.h"
#include "csp/csp_critical.h"
#include <cstdio>
// CMSIS compiler intrinsics (__CLZ, etc.) -- typically pulled in via the
// device header, but included explicitly here since this file depends on
// __CLZ directly.
extern "C" {
    #include <cmsis_compiler.h>
}

namespace csp::internal {

// =============================================================
// AltScheduler Implementation
// =============================================================

unsigned int AltScheduler::select(Guard** guardArray, size_t amount, size_t offset) {
    if (amount == 0) return 0;
    if (amount > ALT_MAX_GUARDS) fatal("CSP4CMSIS: Alternative: more than 16 guards");

    owner = osThreadGetId();
    start_tick_ = osKernelGetTickCount();     // timeout guards count from here

    uint32_t wait_mask = 0;
    for (size_t i = 0; i < amount; ++i) wait_mask |= altFlag(i);

    size_t order[ALT_MAX_GUARDS];
    bool   ready[ALT_MAX_GUARDS];

    for (;;) {
        // New round: drop wakeups left over from earlier rounds; the state
        // word says "enabling" (partners may claim this ALT from now on).
        (void)osThreadFlagsClear(wait_mask);
        { uint32_t s = csp_enter_critical(); state_ = ENABLING; claimed_flag_ = 0; csp_exit_critical(s); }
        wait_limit_ = NO_LIMIT;                   // set by the timeout guards enabled below

        // Phase 1: Enable, starting at 'offset' (fair ALT). Stops at the
        // first guard that is ready now (or that claimed its partner).
        size_t enabled = 0;
        bool any_ready = false;
        for (size_t i = 0; i < amount; ++i) {
            size_t idx = (i + offset) % amount;
            order[enabled++] = idx;
            if (guardArray[idx]->enable(this, altFlag(idx))) { any_ready = true; break; }
        }

        // Phase 2: Wait, unless something was ready or a partner claimed
        // this ALT while it was enabling. With timeout guards, wait at most
        // until the earliest deadline (fixed at the start of select(), so a
        // new round never postpones it); none left: no wait.
        if (!any_ready) {
            uint32_t timeout = osWaitForever;
            bool expired = false;
            if (wait_limit_ != NO_LIMIT) {
                uint32_t e = elapsed();
                if (e >= wait_limit_) expired = true; else timeout = wait_limit_ - e;
            }
            bool wait;
            { uint32_t s = csp_enter_critical(); wait = (state_ == ENABLING); if (wait) state_ = WAITING; csp_exit_critical(s); }
            if (wait && !expired) {
                uint32_t r = osThreadFlagsWait(wait_mask, osFlagsWaitAny, timeout);
                if ((r & osFlagsError) != 0U && r != (uint32_t)osFlagsErrorTimeout) {
                    for (size_t k = 0; k < enabled; ++k) (void)guardArray[order[k]]->disable();
                    fatal("CSP4CMSIS: Alternative: osThreadFlagsWait() failed");
                }
                // No trust in the flag itself: every guard is re-verified below.
            }
        }

        // Phase 3: Disable exactly the guards enabled in this round; each
        // reports whether it can complete now (re-verification).
        for (size_t k = 0; k < enabled; ++k) ready[k] = guardArray[order[k]]->disable();

        // Phase 4: A claim wins (the partner is committed to this guard);
        // otherwise the first ready guard in fairness order; else stale.
        bool claimed; uint32_t cflag;
        { uint32_t s = csp_enter_critical(); claimed = (state_ == CLAIMED); cflag = claimed_flag_; csp_exit_critical(s); }
        int selected = -1;
        if (claimed) {
            for (size_t k = 0; k < enabled; ++k)
                if (altFlag(order[k]) == cflag) { selected = (int)order[k]; break; }
            if (selected < 0) fatal("CSP4CMSIS: Alternative: claimed for a guard that was not enabled");
        } else {
            for (size_t k = 0; k < enabled; ++k)
                if (ready[k]) { selected = (int)order[k]; break; }
        }
        if (selected < 0) continue;                                   // stale wakeup: new round

        // Phase 5: Commit. false = the partner went away / a competitor
        // took the item or space (it made progress): new round.
        if (!guardArray[selected]->activate()) continue;
        { uint32_t s = csp_enter_critical(); state_ = IDLE; csp_exit_critical(s); }
        return (unsigned int)selected;
    }
}

void AltScheduler::wakeUp(uint32_t flag) {
    osThreadId_t t = owner;
    if (t != nullptr) (void)osThreadFlagsSet(t, flag);
}

} // namespace csp::internal

namespace csp {

// =============================================================
// Alternative Implementation
// =============================================================

int Alternative::priSelect() {
    return (int)internal_alt.select(internal_guards, num_guards, 0);
}

int Alternative::fairSelect() {
    if (num_guards <= 1) return priSelect();

    size_t actual_index = internal_alt.select(internal_guards, num_guards, fair_select_start_index);

    // Update fairness index to ensure next guard has priority next time
    fair_select_start_index = (actual_index + 1) % num_guards;

    return (int)actual_index;
}

} // namespace csp
