#ifndef CSP_RTOS_STATIC_H
#define CSP_RTOS_STATIC_H

#include <stdint.h>

// Allocation of the library's RTOS objects (3.0 on):
//
// - Static (the default): every thread, event-flags and semaphore control
//   block CSP4CMSIS creates is a member of a CSP4CMSIS object (no RTOS heap).
//   The control-block types are backend-specific, so the backend must be
//   known: CSP4CMSIS_RTOS2_BACKEND_FREERTOS or _RTX5, if the project does not
//   define one, is detected from RTE_Components.h (pack builds:
//   RTE_CMSIS_RTOS2_FreeRTOS / RTE_CMSIS_RTOS2_RTX5), else from which RTOS
//   header is on the include path (FreeRTOS.h or rtx_os.h).
// - Dynamic (opt-out, define CSP4CMSIS_DYNAMIC_ALLOCATION): RTOS objects come
//   from the RTOS's own allocator (NULL cb_mem); no backend needed.
//
// Before 3.0 dynamic was the default and CSP4CMSIS_STATIC_ALLOCATION opted
// in; that define is still accepted (it now only restates the default).
#if defined(CSP4CMSIS_DYNAMIC_ALLOCATION) && defined(CSP4CMSIS_STATIC_ALLOCATION)
  #error "CSP4CMSIS: define CSP4CMSIS_DYNAMIC_ALLOCATION or CSP4CMSIS_STATIC_ALLOCATION, not both (static allocation is the default since 3.0)"
#endif
#if !defined(CSP4CMSIS_DYNAMIC_ALLOCATION) && !defined(CSP4CMSIS_STATIC_ALLOCATION)
  #define CSP4CMSIS_STATIC_ALLOCATION 1
#endif

#if defined(CSP4CMSIS_STATIC_ALLOCATION)

  #if defined(CSP4CMSIS_RTOS2_BACKEND_FREERTOS) && defined(CSP4CMSIS_RTOS2_BACKEND_RTX5)
    #error "CSP4CMSIS: define only one of CSP4CMSIS_RTOS2_BACKEND_FREERTOS and CSP4CMSIS_RTOS2_BACKEND_RTX5"
  #endif
  #if !defined(CSP4CMSIS_RTOS2_BACKEND_FREERTOS) && !defined(CSP4CMSIS_RTOS2_BACKEND_RTX5)
    #if __has_include("RTE_Components.h")
      #include "RTE_Components.h"
    #endif
    #if defined(RTE_CMSIS_RTOS2_FreeRTOS)
      #define CSP4CMSIS_RTOS2_BACKEND_FREERTOS 1
    #elif defined(RTE_CMSIS_RTOS2_RTX5)
      #define CSP4CMSIS_RTOS2_BACKEND_RTX5 1
    #elif __has_include("FreeRTOS.h") && !__has_include("rtx_os.h")
      #define CSP4CMSIS_RTOS2_BACKEND_FREERTOS 1
    #elif __has_include("rtx_os.h") && !__has_include("FreeRTOS.h")
      #define CSP4CMSIS_RTOS2_BACKEND_RTX5 1
    #else
      #error "CSP4CMSIS: static allocation (the default) needs the CMSIS-RTOS2 backend, and it could not be detected (neither or both of FreeRTOS.h and rtx_os.h on the include path). Define CSP4CMSIS_RTOS2_BACKEND_FREERTOS or CSP4CMSIS_RTOS2_BACKEND_RTX5, or CSP4CMSIS_DYNAMIC_ALLOCATION for dynamic allocation."
    #endif
  #endif

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
    // configSUPPORT_STATIC_ALLOCATION is checked in glue.cpp (FreeRTOS defines
    // StaticTask_t etc. regardless of it).
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
