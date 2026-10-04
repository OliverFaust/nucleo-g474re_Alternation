// --- csp4cmsis.h (Finalized SPN Structure with Process Composition) ---
#ifndef CSP4CMSIS_H
#define CSP4CMSIS_H

// ======================================================================
// 1. Core Definitions (Must be available to both C and C++ sections)
// ======================================================================
// FreeRTOS.h previously had to stay here because run.h's
// CSP_LEGACY_PARALLEL_PRIORITY and public_task.h's CSP_DEFAULT_TASK_
// PRIORITY used FreeRTOS-only macros (tskIDLE_PRIORITY, configMAX_
// PRIORITIES) as C++ default function-parameter values, expanded at
// header-parse time. Both were replaced with portable osPriority_t
// constants, clearing that dependency -- confirmed by this include's
// removal building clean.
#include "time.h"     // Defines csp::Time, etc. (Must be C++-safe)
#include "process.h"  // Defines csp::internal::Process base (Must be C++-safe)


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
#include "public_channel.h"  // Includes One2OneChannel<T>
#include "public_task.h"     // Includes CSProcess, SleepFor(), etc.
#include "run.h"             // <--- NEW: Includes InParallel/InSequence helpers, and Run()

// Note: Run() is defined in run.h, and is the single spawn path for both
// one-process and multi-process compositions -- use Run(InParallel(...)).

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