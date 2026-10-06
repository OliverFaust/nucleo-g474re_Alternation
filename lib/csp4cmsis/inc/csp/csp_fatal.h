#ifndef CSP4CMSIS_FATAL_H
#define CSP4CMSIS_FATAL_H

// Fatal configuration/usage errors (a required RTOS object could not be
// created, or a documented usage rule was violated, e.g. a second process
// ALTing on the same end of a BufferedChannel). CSP4CMSIS never continues
// after one: silently running with a broken channel is worse than stopping.
//
// csp4cmsis_fatal_error() is a weak symbol (default in glue.cpp: records the
// message in csp4cmsis_last_fatal_error for a debugger and spins forever).
// Applications may override it, e.g. to record the message and reset, and may
// call it themselves for their own unrecoverable errors. It must not return;
// if an override does return, CSP4CMSIS spins in place.
//
// Calling context: CSP4CMSIS calls it from threads, never with a CSP critical
// section active. But it may be called from an interrupt handler (an
// application's ISR may call it, e.g. when an isrWriter() write fails), so a
// replacement must be ISR-safe: no stdio, no RTOS calls that are not allowed
// in an ISR, no blocking. Record the message and halt or reset.

#ifdef __cplusplus
extern "C" {
#endif

void csp4cmsis_fatal_error(const char* message);

#ifdef __cplusplus
}

namespace csp::internal {
    [[noreturn]] inline void fatal(const char* message) {
        csp4cmsis_fatal_error(message);
        for (;;) { }
    }
}
#endif

#endif // CSP4CMSIS_FATAL_H
