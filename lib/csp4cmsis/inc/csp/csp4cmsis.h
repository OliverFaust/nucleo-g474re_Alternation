// --- csp4cmsis.h: the one header applications include ---
#ifndef CSP4CMSIS_H
#define CSP4CMSIS_H

// ======================================================================
// 1. Core Definitions (Must be available to both C and C++ sections)
// ======================================================================
// No RTOS-specific header: the library uses CMSIS-RTOS2 only (cmsis_os2.h).
#include "csp_version.h"  // CSP4CMSIS_VERSION_MAJOR/MINOR/PATCH
#include "csp_time.h"     // csp::Time, Ticks(), Milliseconds(), Seconds(), Forever
#include "process.h"      // csp::CSProcess, CSProcessStatic<N>


// ======================================================================
// 2. C++ API Block (Only for C++ Compiler)
// ======================================================================
#ifdef __cplusplus
namespace csp { /* Forward declare namespace content here if needed */ }
// Include all C++-specific headers that define classes/templates.
#include "alt.h"             // Required for ALT functionality
#include "channel_base.h"    // Base classes for internal channel implementations
#include "rendezvous_channel.h" // Rendezvous (and signal) channels, OWRV ALT protocol
#include "buffered_channel.h"// For future implementation
#include "barrier.h"         // Standard CSP primitive
#include "public_channel.h"  // Channel, BufferedChannel, SignalChannel
#include "public_task.h"     // SleepFor()
#include "run.h"             // InParallel(), Run(), ExecutionMode

// Run() is the one way to start processes: Run(InParallel(...), mode).

#endif // __cplusplus


// ======================================================================
// 3. C/C++ Linkage Control Block (Wraps only C-callable symbols)
// ======================================================================
#ifdef __cplusplus
// Start extern "C" block for C functions
extern "C" {
#endif

// 4. External Hooks (Declared for both C and C++ linkers)
void csp_app_main_init(void);
void ThreadFuncWrapper(void *pvParameters); // FreeRTOS Task Entry Point

// 5. End of Linkage Control
#ifdef __cplusplus
} // extern "C"
#endif

#endif // CSP4CMSIS_H