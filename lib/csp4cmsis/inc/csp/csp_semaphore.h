#ifndef CSP4CMSIS_SEMAPHORE_H
#define CSP4CMSIS_SEMAPHORE_H

// Counting semaphore used by CSP4CMSIS channels. Under
// CSP4CMSIS_STATIC_ALLOCATION the control block is a member of the object
// (no RTOS heap); otherwise the backend allocates it. Creation failure is
// fatal (csp4cmsis_fatal_error()).

#include "cmsis_os2.h"
#include "csp_fatal.h"
#include "csp_rtos_static.h"

namespace csp::internal {

    class CspSemaphore {
    private:
        osSemaphoreId_t id_ = nullptr;
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
        csp_static_semaphore_storage_t cb_;
#endif
    public:
        CspSemaphore() = default;
        CspSemaphore(const CspSemaphore&) = delete;
        CspSemaphore& operator=(const CspSemaphore&) = delete;
        ~CspSemaphore() { if (id_ != nullptr) (void)osSemaphoreDelete(id_); }

        void create(uint32_t max_count, uint32_t initial, const char* name) {
            osSemaphoreAttr_t attr = {};
            attr.name = name;
#if defined(CSP4CMSIS_STATIC_ALLOCATION)
            attr.cb_mem  = &cb_;
            attr.cb_size = sizeof(cb_);
#endif
            id_ = osSemaphoreNew(max_count, initial, &attr);
            if (id_ == nullptr) fatal("CSP4CMSIS: osSemaphoreNew() failed");
        }
        bool acquire(uint32_t timeout) { return osSemaphoreAcquire(id_, timeout) == osOK; }
        void release() { (void)osSemaphoreRelease(id_); }
        bool available() const { return osSemaphoreGetCount(id_) > 0U; }   // not in a critical section
    };

} // namespace csp::internal

#endif // CSP4CMSIS_SEMAPHORE_H
