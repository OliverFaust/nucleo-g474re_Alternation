#ifndef CSP4CMSIS_PUBLIC_CHANNEL_H
#define CSP4CMSIS_PUBLIC_CHANNEL_H

#include "rendezvous_channel.h"
#include "buffered_channel.h"

// Largest element type that IsrChanout<T>::putFromISR() accepts (bytes). The element is
// copied with BASEPRI raised (see buffered_channel.h, "Masked copy"), so
// this bounds the extra interrupt latency an ISR write can cause. Raise it
// per project with -DCSP4CMSIS_ISR_MAX_ELEMENT_SIZE=<bytes> if the latency
// is acceptable; for large payloads send an index into a static pool.
#ifndef CSP4CMSIS_ISR_MAX_ELEMENT_SIZE
#define CSP4CMSIS_ISR_MAX_ELEMENT_SIZE 64
#endif

/// ISR writes only through IsrChanout<T> from buffered channels (2.0); rendezvous
/// and signal channels are Block-only. Undefined in 1.x.
#define CSP4CMSIS_ISR_WRITER_API 1

namespace csp {

// Forward declarations
template <typename T> class Chanin;
template <typename T> class Chanout;

/**
 * @brief Pipe Operators for Alternative Syntax.
 */
template <typename T>
ChannelBinding<T, Chanin<T>> operator|(Chanin<T>& chan, T& dest) {
    return ChannelBinding<T, Chanin<T>>(chan, dest);
}

template <typename T>
ChannelBinding<const T, Chanout<T>> operator|(Chanout<T>& chan, const T& source) {
    return ChannelBinding<const T, Chanout<T>>(chan, source);
}

// =============================================================
// Channel End Wrappers (Chanout / Chanin)
// =============================================================

template <typename T>
class Chanout {
private:
    internal::BaseAltChan<T>* internal_ptr;
    internal::GuardSlot guard_slot;   // this end's ALT guard (see GuardSlot)
public:
    Chanout(internal::BaseAltChan<T>* ptr) : internal_ptr(ptr) {}
    
    void operator<<(const T& data) { internal_ptr->output(&data); }
    void write(const T& data) { internal_ptr->output(&data); }
    
    internal::Guard* getGuard(const T& source) {
        return internal_ptr->getOutputGuard(guard_slot, source);
    }
};

/**
 * @brief ISR writer end of a buffered channel (the only ISR write path).
 * Obtain it with SamplingBufferedChannel::isrWriter(); rendezvous and signal
 * channels have none (an interrupt cannot wait for a partner).
 * The element is copied with BASEPRI raised, so its size is bounded at
 * compile time by CSP4CMSIS_ISR_MAX_ELEMENT_SIZE.
 */
template <typename T>
class IsrChanout {
    static_assert(sizeof(T) <= CSP4CMSIS_ISR_MAX_ELEMENT_SIZE,
                  "IsrChanout: sizeof(T) exceeds CSP4CMSIS_ISR_MAX_ELEMENT_SIZE "
                  "(send an index into a static pool, or raise the limit)");
private:
    internal::IsrSink<T>* sink;
public:
    explicit IsrChanout(internal::IsrSink<T>* s) : sink(s) {}
    /// Never blocks. Block policy: false if the buffer is full.
    /// KeepNewest/KeepOldest: always true (the policy decides what is kept).
    bool putFromISR(const T& data) { return sink->putFromISR(data); }
};

template <typename T>
class Chanin {
private:
    internal::BaseAltChan<T>* internal_ptr;
    internal::GuardSlot guard_slot;   // this end's ALT guard (see GuardSlot)
public:
    Chanin(internal::BaseAltChan<T>* ptr) : internal_ptr(ptr) {}
    
    void operator>>(T& dest) { internal_ptr->input(&dest); }
    void read(T& dest) { internal_ptr->input(&dest); }
    
    internal::Guard* getGuard(T& dest) {
        return internal_ptr->getInputGuard(guard_slot, dest);
    }
};

// =============================================================
// Static Channel Containers (v1.1 Sampling API)
// =============================================================

/**
 * @brief Zero-capacity synchronisation primitive (rendezvous).
 * Block policy only: KeepNewest/KeepOldest are rejected at compile time
 * (use SamplingBufferedChannel<T, 1, P> for a "latest value" channel).
 * No ISR writer: interrupts write to buffered channels (isrWriter()).
 */
template <typename T, BufferPolicy P = BufferPolicy::Block>
class SamplingChannel {
private:
    internal::RendezvousChannel<T, P> internal_chan;
public:
    SamplingChannel() = default;
    
    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
};

/**
 * @brief Buffered Asynchronous Primitive.
 * Decouples timing. Supports Lossy policies (KeepNewest/Oldest).
 */
template <typename T, size_t SIZE, BufferPolicy P = BufferPolicy::Block>
class SamplingBufferedChannel {
private:
    internal::BufferedChannel<T, SIZE, P> internal_chan;   // static storage for SIZE elements
public:
    SamplingBufferedChannel() = default;
    SamplingBufferedChannel(const SamplingBufferedChannel&) = delete;
    SamplingBufferedChannel& operator=(const SamplingBufferedChannel&) = delete;

    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
    /// ISR writer end (the only way to write from an interrupt).
    IsrChanout<T> isrWriter() { return IsrChanout<T>(&internal_chan); }
};

/// Payload of a signal channel (no data).
struct Signal {};

/**
 * @brief Signal channel: a rendezvous that carries no data (csp::Signal).
 * Same protocol as SamplingChannel (OWRV); use reader()/writer() like any
 * channel, e.g. `out << csp::Signal{}`, `in >> s`, `in | s` in an ALT.
 */
template <BufferPolicy P = BufferPolicy::Block>
class SignalChannel {
    static_assert(P == BufferPolicy::Block,
                  "SignalChannel: KeepNewest/KeepOldest need a buffer: use SamplingBufferedChannel<csp::Signal, 1, P>");
private:
    internal::RendezvousChannel<Signal, P> internal_chan;
public:
    SignalChannel() = default;
    Chanout<Signal> writer() { return Chanout<Signal>(&internal_chan); }
    Chanin<Signal> reader() { return Chanin<Signal>(&internal_chan); }
};

// =============================================================
// Public Aliases & Legacy Support
// =============================================================

/**
 * @brief Standard CSP rendezvous channel (Blocking).
 */
template <typename T>
using Channel = SamplingChannel<T, BufferPolicy::Block>;

/**
 * @brief Standard CSP buffered channel (Blocking).
 */
template <typename T, size_t SIZE>
using BufferedChannel = SamplingBufferedChannel<T, SIZE, BufferPolicy::Block>;

/**
 * @brief Semantic alias for shared input ports.
 */
template <typename T, BufferPolicy P = BufferPolicy::Block> 
using Any2OneChannel = SamplingChannel<T, P>;

template <typename T, size_t S, BufferPolicy P = BufferPolicy::Block> 
using BufferedAny2OneChannel = SamplingBufferedChannel<T, S, P>;

/**
 * @brief Legacy API 1.0 Compatibility Aliases.
 */
template <typename T, BufferPolicy P = BufferPolicy::Block>
using One2OneChannel = SamplingChannel<T, P>;

template <typename T, size_t S, BufferPolicy P = BufferPolicy::Block>
using BufferedOne2OneChannel = SamplingBufferedChannel<T, S, P>;

} // namespace csp

#endif // CSP4CMSIS_PUBLIC_CHANNEL_H