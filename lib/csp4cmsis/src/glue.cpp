// --- glue.cpp ---
//
// Previously overrode global operator new/delete to route through
// FreeRTOS's pvPortMalloc/vPortFree. Removed: investigation found zero
// live allocation need anywhere in csp4cmsis itself or in the reference
// ALT/select test used during its own RTOS2/RTX5 validation -- the only
// real dependency found was in a downstream project's own inference
// pipeline (a std::vector<EValue> allocated once per cycle), which is
// that project's own implementation detail, not something shared
// library code should be dictating heap behavior for. A project with a
// genuine allocation need should supply its own local operator
// new/delete (or equivalent) rather than relying on this file.
//
// Also home of the weak default for csp4cmsis_fatal_error() (csp_fatal.h).

#include "csp/csp_fatal.h"
#include "csp/csp_rtos_static.h"

// Static allocation (the default since 3.0) with FreeRTOS needs
// xTaskCreateStatic() and friends. Checked here, in a library source file,
// rather than in a header: FreeRTOS declares StaticTask_t etc. whatever the
// setting, so applications compile either way and this file reports it.
#if defined(CSP4CMSIS_STATIC_ALLOCATION) && defined(CSP4CMSIS_RTOS2_BACKEND_FREERTOS)
  #if !defined(configSUPPORT_STATIC_ALLOCATION) || (configSUPPORT_STATIC_ALLOCATION == 0)
    #error "CSP4CMSIS: static allocation (the default) needs configSUPPORT_STATIC_ALLOCATION 1 in FreeRTOSConfig.h (STM32CubeMX: FREERTOS > Config parameters > Memory Allocation: Dynamic / Static). Or define CSP4CMSIS_DYNAMIC_ALLOCATION for dynamic allocation."
  #endif
#endif

extern "C" {
    // Last fatal message, for inspection with a debugger.
    const char* volatile csp4cmsis_last_fatal_error = nullptr;

    __attribute__((weak)) void csp4cmsis_fatal_error(const char* message) {
        csp4cmsis_last_fatal_error = message;
        for (;;) { }
    }
}
