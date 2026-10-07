// --- csp_time.h ---  (csp/time.h before 3.0: renamed, so that it can never
// hide the C library's <time.h>)
#ifndef CSP4CMSIS_TIME_H
#define CSP4CMSIS_TIME_H

// cmsis_os2.h for osKernelGetTickFreq(); RTOS2 tick counts are plain
// uint32_t (no RTOS-specific tick type), unlike FreeRTOS's TickType_t.
#include "cmsis_os2.h"
#include <stdint.h>

#ifdef __cplusplus
namespace csp {

/**
 * @brief A duration in RTOS ticks. Make one with Ticks(), Milliseconds(),
 * Seconds() or Forever; use it with SleepFor() and RelTimeoutGuard.
 */
class Time {
public:
    constexpr Time() : ticks_(0) {}
    /// A tick count. Explicit, so a plain number is never taken as a Time.
    constexpr explicit Time(uint32_t ticks) : ticks_(ticks) {}
    /// The raw tick count, for CMSIS-RTOS2 calls.
    constexpr uint32_t to_ticks() const { return ticks_; }
private:
    uint32_t ticks_;
};

/// A duration of `n` RTOS ticks (the kernel tick, osKernelGetTickFreq()).
constexpr Time Ticks(uint32_t n) { return Time(n); }

/// No end: SleepFor(Forever) is osDelay(osWaitForever). As a RelTimeoutGuard
/// delay it is the longest timeout, 0xFFFFFFFE ticks.
inline constexpr Time Forever{osWaitForever};

// ----------------------------------------------------
// C++CSP Style Time Unit Helpers
// ----------------------------------------------------

namespace internal {
    /// ticks = ceil(amount * tick_frequency / per_second), computed in 64 bits;
    /// 0 stays 0, any other duration is at least 1 tick; saturates at
    /// 0xFFFFFFFE (0xFFFFFFFF is osWaitForever, not a duration).
    inline uint32_t to_ticks_round_up(uint32_t amount, uint32_t per_second) {
        const uint64_t f = osKernelGetTickFreq();
        const uint64_t t = ((uint64_t)amount * f + (per_second - 1U)) / per_second;
        return t > 0xFFFFFFFEULL ? 0xFFFFFFFEUL : (uint32_t)t;
    }
}

/**
 * @brief A duration of `s` seconds, in ticks of the kernel tick frequency
 * (osKernelGetTickFreq(), a run-time call: CMSIS-RTOS2 has no compile-time
 * tick rate).
 */
inline Time Seconds(uint32_t s) {
    return Time(internal::to_ticks_round_up(s, 1U));
}

/**
 * @brief A duration of `ms` milliseconds, in ticks, rounded UP (2.1.0): a
 * non-zero duration is never shorter than requested, and never 0 ticks.
 * Milliseconds(0) is 0 ticks.
 */
inline Time Milliseconds(uint32_t ms) {
    return Time(internal::to_ticks_round_up(ms, 1000U));
}

} // namespace csp
#endif // __cplusplus

#endif // CSP4CMSIS_TIME_H
