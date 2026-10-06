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

/// Writing end of a channel: `out << value` (or out.write(value)) blocks until
/// the value is taken (rendezvous) or stored (buffered channel, Block policy).
template <typename T>
class Chanout {
private:
    internal::BaseAltChan<T>* internal_ptr;
    internal::GuardSlot guard_slot;   // this end's ALT guard (see GuardSlot)

    template <typename, typename> friend class ChannelBinding;
    friend struct internal::Access;
    internal::Guard* getGuard(const T& source) {
        return internal_ptr->getOutputGuard(guard_slot, source);
    }
public:
    Chanout(internal::BaseAltChan<T>* ptr) : internal_ptr(ptr) {}

    void operator<<(const T& data) { internal_ptr->output(&data); }
    void write(const T& data) { internal_ptr->output(&data); }
};

/**
 * @brief ISR writer end of a buffered channel (the only ISR write path).
 * Obtain it with BufferedChannel::isrWriter(); rendezvous and signal
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

/// Reading end of a channel: `in >> var` (or in.read(var)) blocks until a value
/// arrives.
template <typename T>
class Chanin {
private:
    internal::BaseAltChan<T>* internal_ptr;
    internal::GuardSlot guard_slot;   // this end's ALT guard (see GuardSlot)

    template <typename, typename> friend class ChannelBinding;
    friend struct internal::Access;
    internal::Guard* getGuard(T& dest) {
        return internal_ptr->getInputGuard(guard_slot, dest);
    }
public:
    Chanin(internal::BaseAltChan<T>* ptr) : internal_ptr(ptr) {}

    void operator>>(T& dest) { internal_ptr->input(&dest); }
    void read(T& dest) { internal_ptr->input(&dest); }
};

// =============================================================
// Channels. Construct them at namespace scope or as function-local statics.
// Any channel may be shared by several writers and several readers, each
// with its own writer()/reader() end, using plain << and >>. In an ALT, at
// most one reader and one writer of a channel may be ALTing (a second ALTing
// reader or writer is a fatal error).
// =============================================================

/**
 * @brief Rendezvous channel: a write completes when a reader has taken the
 * value. No buffer, no ISR writer (an interrupt cannot wait for a partner):
 * interrupts write to a BufferedChannel through isrWriter().
 */
template <typename T>
class Channel {
private:
    internal::RendezvousChannel<T, BufferPolicy::Block> internal_chan;
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
};

/**
 * @brief Buffered channel with a static ring buffer of SIZE elements.
 * Policy P when the buffer is full: Block (the writer waits; the default),
 * KeepNewest (the oldest element is overwritten), KeepOldest (the new element
 * is dropped). isrWriter() is the only way to write from an interrupt.
 */
template <typename T, size_t SIZE, BufferPolicy P = BufferPolicy::Block>
class BufferedChannel {
private:
    internal::BufferedChannel<T, SIZE, P> internal_chan;   // static storage for SIZE elements
public:
    BufferedChannel() = default;
    BufferedChannel(const BufferedChannel&) = delete;
    BufferedChannel& operator=(const BufferedChannel&) = delete;

    Chanout<T> writer() { return Chanout<T>(&internal_chan); }
    Chanin<T> reader() { return Chanin<T>(&internal_chan); }
    /// ISR writer end (the only way to write from an interrupt).
    IsrChanout<T> isrWriter() { return IsrChanout<T>(&internal_chan); }
};

/// Payload of a signal channel (no data).
struct Signal {};

/**
 * @brief Signal channel: a rendezvous that carries no data (csp::Signal):
 * `out << csp::Signal{}`, `in >> s`, `in | s` in an ALT.
 */
class SignalChannel {
private:
    internal::RendezvousChannel<Signal, BufferPolicy::Block> internal_chan;
public:
    SignalChannel() = default;
    SignalChannel(const SignalChannel&) = delete;
    SignalChannel& operator=(const SignalChannel&) = delete;

    Chanout<Signal> writer() { return Chanout<Signal>(&internal_chan); }
    Chanin<Signal> reader() { return Chanin<Signal>(&internal_chan); }
};

namespace internal {
    /// Access to ALT internals for the regression suite (tests/fvp_sse300).
    /// Not part of the API.
    struct Access {
        template <typename T>
        static Guard* inputGuard(Chanin<T>& in, T& dest) { return in.getGuard(dest); }
        template <typename T>
        static Guard* outputGuard(Chanout<T>& out, const T& source) { return out.getGuard(source); }
        static Guard* guardOf(csp::Guard& g) { return g.internal_guard_ptr; }
        static void addGuard(Alternative& alt, Guard* g) { alt.addBinding(g); }
    };
}

} // namespace csp

#endif // CSP4CMSIS_PUBLIC_CHANNEL_H