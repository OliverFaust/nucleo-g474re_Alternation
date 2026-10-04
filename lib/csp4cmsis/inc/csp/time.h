// --- time.h (CMSIS-RTOS2 migration) ---
#ifndef CSP4CMSIS_TIME_H
#define CSP4CMSIS_TIME_H

// cmsis_os2.h for osKernelGetTickFreq(); RTOS2 tick counts are plain
// uint32_t (no RTOS-specific tick type), unlike FreeRTOS's TickType_t.
#include "cmsis_os2.h"
#include <stdint.h>

#ifdef __cplusplus
namespace csp {

/**
 * @brief Represents a duration or absolute time point in a type-safe manner.
 * Encapsulates a raw RTOS2 tick count and provides conversion helpers.
 */
struct Time {
    // The internal representation is the raw tick count
    uint32_t ticks;

    // Default constructor for Time()
    Time() : ticks(0) {}

    // Constructor required for the Time unit helpers (e.g., Seconds())
    explicit Time(uint32_t t) : ticks(t) {}

    /**
    * @brief Converts the Time object into the raw tick count for RTOS2 API calls.
    */
    uint32_t to_ticks() const {
        return ticks;
    }
};

// ----------------------------------------------------
// C++CSP Style Time Unit Helpers
// ----------------------------------------------------

/**
 * @brief Creates a csp::Time duration representing a number of seconds.
 *
 * osKernelGetTickFreq() (a runtime call) replaces FreeRTOS's
 * configTICK_RATE_HZ (a compile-time macro) -- CMSIS-RTOS2 doesn't
 * expose the kernel tick frequency as a constant, since a portable
 * RTOS2 caller can't assume any particular backend defines one the
 * same way.
 */
inline Time Seconds(uint32_t s) {
    return Time(s * osKernelGetTickFreq());
}

/**
 * @brief Creates a csp::Time duration representing a number of milliseconds.
 */
inline Time Milliseconds(uint32_t ms) {
    return Time((ms * osKernelGetTickFreq()) / 1000U);
}

} // namespace csp
#endif // __cplusplus

#endif // CSP4CMSIS_TIME_H
