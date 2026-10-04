#ifndef CSP_RTOS_STATIC_H
#define CSP_RTOS_STATIC_H

#include <stdint.h>

// Static (no-heap) allocation for csp4cmsis's RTOS2 objects (thread
// control blocks, event-flags control blocks, semaphore control blocks)
// is opt-in and backend-specific -- every CMSIS-RTOS2 backend supports
// dynamic allocation identically (osThreadNew(NULL) etc.), but each owns
// a completely different internal control-block layout for its static
// path, so a caller-supplied cb_mem/cb_size buffer must be sized/typed to
// match whichever backend is actually running underneath. Backing one
// backend's static allocation with another's control-block type would
// compile (both are just structs) but silently corrupt memory at
// runtime, since the RTOS writes its own real internal state into that
// buffer at a size/layout the wrong type doesn't actually match.
//
// Default (CSP4CMSIS_STATIC_ALLOCATION undefined): nothing in this header
// is defined, and process.h/alt.h/run.h fall back to dynamic allocation
// (NULL cb_mem/zero cb_size) -- genuinely portable across any CMSIS-RTOS2
// backend with zero per-backend configuration.
//
// Projects that need zero-heap csp4cmsis objects opt in explicitly:
// define CSP4CMSIS_STATIC_ALLOCATION, plus exactly one
// CSP4CMSIS_RTOS2_BACKEND_* macro naming which backend's control-block
// layout to use.
#if defined(CSP4CMSIS_STATIC_ALLOCATION)

  #if defined(CSP4CMSIS_RTOS2_BACKEND_FREERTOS)
    // Verified directly against the installed ARM::CMSIS-FreeRTOS@11.3.0
    // pack's CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c: osThreadNew()/
    // osEventFlagsNew()/osSemaphoreNew()'s static paths all check
    // `attr->cb_size >= sizeof(StaticTask_t | StaticEventGroup_t |
    // StaticSemaphore_t)` and cast attr->cb_mem to that exact FreeRTOS
    // type before calling xTaskCreateStatic()/xEventGroupCreateStatic()/
    // xSemaphoreCreateBinaryStatic() internally.
    // task.h/event_groups.h/semphr.h are not self-contained -- FreeRTOS's
    // own convention requires FreeRTOS.h included first (it defines the
    // base types/config these headers assume).
    #include "FreeRTOS.h"
    #include "task.h"
    #include "event_groups.h"
    #include "semphr.h"
    namespace csp::internal {
        using csp_static_thread_storage_t     = StaticTask_t;
        using csp_static_eventflags_storage_t = StaticEventGroup_t;
        using csp_static_semaphore_storage_t  = StaticSemaphore_t;
    }

  #elif defined(CSP4CMSIS_RTOS2_BACKEND_RTX5)
    // Verified directly against the installed ARM::CMSIS-RTX@5.9.1 pack's
    // Include/rtx_os.h: osRtxThread_t/osRtxEventFlags_t/osRtxSemaphore_t
    // are RTX5's own internal control-block structs -- unrelated to, and
    // a different size from, FreeRTOS's StaticTask_t/StaticEventGroup_t/
    // StaticSemaphore_t. rtx_os.h itself defines osRtxThreadCbSize/
    // osRtxEventFlagsCbSize/osRtxSemaphoreCbSize as literally
    // sizeof(osRtxThread_t) etc., confirming these are the exact types
    // RTX5's own static-allocation path expects.
    #include "rtx_os.h"
    namespace csp::internal {
        using csp_static_thread_storage_t     = osRtxThread_t;
        using csp_static_eventflags_storage_t = osRtxEventFlags_t;
        using csp_static_semaphore_storage_t  = osRtxSemaphore_t;
    }

  #else
    #error "CSP4CMSIS_STATIC_ALLOCATION requires a CSP4CMSIS_RTOS2_BACKEND_* define to select the matching backend (CSP4CMSIS_RTOS2_BACKEND_FREERTOS or CSP4CMSIS_RTOS2_BACKEND_RTX5)"
  #endif

#endif // CSP4CMSIS_STATIC_ALLOCATION

namespace csp::internal {
    // Portable stack-word type for CSProcess's caller-supplied stack
    // buffer (osThreadAttr_t.stack_mem/.stack_size). Unlike the control
    // block types above, stack memory is NOT backend-specific: every
    // CMSIS-RTOS2 backend just treats stack_mem as a raw, word-aligned
    // byte range the CPU's SP walks through, not a struct any backend
    // owns or casts through. Confirmed against both backends' headers:
    // FreeRTOS's own StackType_t is `uint32_t` on this port
    // (ARMv8M/non_secure/portmacrocommon.h: portSTACK_TYPE == uint32_t),
    // and RTX5's osRtxThread_t.stack_mem is a plain `void*` (rtx_os.h)
    // with no backend-owned layout -- so a single portable word type
    // works unconditionally here, used regardless of whether
    // CSP4CMSIS_STATIC_ALLOCATION is defined (the stack buffer itself was
    // never actually backend-specific; only the control block was).
    using csp_stack_word_t = uint32_t;
}

#endif // CSP_RTOS_STATIC_H
