#ifndef CSP4CMSIS_CRITICAL_H
#define CSP4CMSIS_CRITICAL_H

// Portable replacement for FreeRTOS's taskENTER_CRITICAL()/taskEXIT_CRITICAL()
// (and the _FROM_ISR variant) -- CMSIS-RTOS2 doesn't standardize a raw
// critical-section API, so csp4cmsis provides its own, built directly on
// CMSIS-Core BASEPRI intrinsics rather than FreeRTOS.
//
// BASEPRI, not PRIMASK: investigation (see docs/debugging-case-studies.md
// if this ships with one) confirmed multiple interrupts in real csp4cmsis
// deployments run above configMAX_SYSCALL_INTERRUPT_PRIORITY by design or
// vendor default (e.g. the console UART and an I2C/I3C sensor path both at
// the highest hardware priority on one target, an SE/MHU doorbell IRQ
// hardcoded just below it on another) -- a PRIMASK-based section would
// newly mask those during every csp4cmsis critical section. BASEPRI masks
// only priorities at or below the configured threshold, matching what
// FreeRTOS itself does on mainline Cortex-M, and leaves that above-
// threshold tier running exactly as it does today.
//
// __get_BASEPRI()/__set_BASEPRI_MAX()/__set_BASEPRI() are inherently
// interrupt-context-safe (unlike FreeRTOS's task/ISR-split API), so there
// is deliberately no separate _FromISR() pair here -- call the same two
// functions from ISR context too.

// The device header: cmsis_os2.h alone does NOT pull in CMSIS-Core
// (confirmed by reading it -- it includes only <stdint.h>/<stddef.h>), so
// __NVIC_PRIO_BITS/__get_BASEPRI/__set_BASEPRI/__set_BASEPRI_MAX aren't
// visible through the includes the rest of csp4cmsis already uses.
//
// Pack builds (CMSIS-Toolbox, uVision): RTE_Components.h is generated per
// project and defines CMSIS_device_header to the device header of the
// project's MCU. Builds without packs (STM32CubeIDE, vendor SDK makefiles)
// have no RTE_Components.h; they name the device header themselves with
// CSP4CMSIS_DEVICE_HEADER, e.g. -DCSP4CMSIS_DEVICE_HEADER='"stm32g4xx.h"'.
#if defined(__has_include)
  #if __has_include("RTE_Components.h")
    #include "RTE_Components.h"
  #endif
#endif
#if defined(CMSIS_device_header)
  #include CMSIS_device_header
#elif defined(CSP4CMSIS_DEVICE_HEADER)
  #include CSP4CMSIS_DEVICE_HEADER
#else
  #error "CSP4CMSIS: no device header. Pack builds get it from RTE_Components.h (CMSIS_device_header); without packs, define CSP4CMSIS_DEVICE_HEADER to the device header, e.g. \"stm32g4xx.h\" (Documentation/CSP4CMSIS_Configuration.md)"
#endif

// csp4cmsis's own threshold, independent of any specific RTOS's macro name
// -- keeps the library RTOS-agnostic. Projects define this to match
// whatever their underlying RTOS treats as configMAX_SYSCALL_INTERRUPT_
// PRIORITY (or equivalent), via a compiler -D flag -- not hardcoded here.
#ifndef CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY
#error "Define CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY to match your RTOS's critical-section threshold"
#endif

namespace csp::internal {

    // DSB + ISB after raising BASEPRI: the section's first instruction then
    // runs with the new priority in force, without relying on how soon an
    // MSR that raises the execution priority takes effect. Same sequence as
    // the FreeRTOS ARM_CM3/CM4F/CM33/CM55 ports (msr basepri; dsb; isb).
    // Lowering BASEPRI on exit needs no barrier.
    //
    // NOT sufficient for Cortex-M7 r0p1 (erratum 837070: a BASEPRI write
    // can be delayed so that an interrupt at the new masked level is still
    // taken). FreeRTOS's ARM_CM7/r0p1 port brackets the MSR with
    // CPSID i / CPSIE i; do the same here before using CSP4CMSIS on that core
    // revision. No known CSP4CMSIS target uses a Cortex-M7.
    inline uint32_t csp_enter_critical() {
        uint32_t saved = __get_BASEPRI();
        __set_BASEPRI_MAX(CSP4CMSIS_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - __NVIC_PRIO_BITS));
        __DSB();
        __ISB();
        return saved;
    }

    inline void csp_exit_critical(uint32_t saved) {
        __set_BASEPRI(saved);
    }

} // namespace csp::internal

#endif // CSP4CMSIS_CRITICAL_H
