#ifndef CSP4CMSIS_CHANNEL_BASE_H
#define CSP4CMSIS_CHANNEL_BASE_H

#include <stddef.h>
#include <new>

namespace csp {

    enum class BufferPolicy {
        Block,      // Standard CSP: block until partner arrives
        KeepNewest, // Sampling: overwrite oldest if full/no partner
        KeepOldest  // Sampling: discard new if full/no partner
    };
    
    template <typename T> class Chanin;
    template <typename T> class Chanout;
    class Alternative; 
}

namespace csp::internal {

    class Guard;

    /**
     * @brief Storage for one ALT guard, owned by a channel-end handle
     * (Chanin/Chanout). Guards are constructed into the caller's slot by
     * getInputGuard()/getOutputGuard(), so their state (target buffer,
     * registration) belongs to the process's own channel end, not to the
     * channel: two writers ALTing on one channel no longer share -- and
     * re-target -- a single guard object. A process must not share one
     * handle object with another process (each process owns its ends).
     */
    struct GuardSlot {
        static constexpr size_t SIZE = 24;
        alignas(void*) unsigned char bytes[SIZE];

        template <typename G, typename... Args>
        G* emplace(Args&&... args) {
            static_assert(sizeof(G) <= SIZE, "GuardSlot too small for this guard type");
            static_assert(alignof(G) <= alignof(void*), "guard over-aligned for GuardSlot");
            // Guards own no resources; the previous occupant is simply
            // overwritten (no destructor side effects to preserve).
            return ::new (static_cast<void*>(bytes)) G(static_cast<Args&&>(args)...);
        }
    };

    /**
     * @brief ISR write interface. Only buffered channels implement it: an
     * interrupt can never wait, so it can only hand data to a buffer.
     * Obtained by applications through csp::IsrChanout<T>.
     */
    template <typename DATA_TYPE>
    class IsrSink {
    public:
        /// Never blocks. Block policy: false if full; KeepNewest/KeepOldest: true.
        virtual bool putFromISR(const DATA_TYPE& data) = 0;
    protected:
        ~IsrSink() = default;
    };

    /**
     * @brief The core contract for CSP communication.
     * Updated to support both Synchronous (Rendezvous) and Asynchronous (Buffered) logic.
     */
    template <typename DATA_TYPE>
    class BaseChan 
    {
    public:
        template <typename U>
        friend class ::csp::Chanin; 

        template <typename U>
        friend class ::csp::Chanout;
        
    protected:
        inline virtual ~BaseChan() = default;

        virtual void input(DATA_TYPE* const dest) = 0;
        virtual void output(const DATA_TYPE* const source) = 0;
        
        virtual void beginExtInput(DATA_TYPE* const dest) = 0;
        virtual void endExtInput() = 0;
    }; 
    
    /**
     * @brief Extends BaseChan with methods required for ALT, Polling, and ISRs.
     */
    template <typename DATA_TYPE>
    class BaseAltChan : public BaseChan<DATA_TYPE>
    {
    public:
        /** @brief Returns true if data is waiting to be read. */
        virtual bool pending() = 0;

        /** @brief Returns true if a write operation will not block. 
         * For KeepNewest/KeepOldest, this is effectively always true. 
         */
        virtual bool space_available() = 0;

        /// Constructs this channel's input/output guard in `slot` (see GuardSlot).
        virtual internal::Guard* getInputGuard(GuardSlot& slot, DATA_TYPE& dest) = 0;
        virtual internal::Guard* getOutputGuard(GuardSlot& slot, const DATA_TYPE& source) = 0;
        
    public:
        inline virtual ~BaseAltChan() = default;

        friend class ::csp::Alternative; 
    };

} // namespace csp::internal

#endif // CSP4CMSIS_CHANNEL_BASE_H