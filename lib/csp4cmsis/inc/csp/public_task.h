// --- public_task.h: SleepFor ---
#ifndef CSP4CMSIS_PUBLIC_TASK_H
#define CSP4CMSIS_PUBLIC_TASK_H

#include "cmsis_os2.h"
#include "csp_time.h"

namespace csp {

/**
 * @brief Suspends the calling process for `duration`: SleepFor(Milliseconds(250)),
 * SleepFor(Ticks(10)), SleepFor(Forever). A plain number does not compile
 * (3.0): its unit, ticks or milliseconds, would be a guess.
 */
inline void SleepFor(Time duration) {
    osDelay(duration.to_ticks());
}

} // namespace csp

#endif // CSP4CMSIS_PUBLIC_TASK_H
