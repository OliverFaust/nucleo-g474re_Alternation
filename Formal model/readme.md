# Alternation (External Choice): Formal Model (CSP-M)

This directory contains the formal **Communicating Sequential Processes (CSP)** model for the Alternation pattern. The specification mathematically defines how a process network manages **External Choice** (`[]`), allowing a centralized receiver to dynamically service multiple independent senders without polling or busy-waiting. The CSP model was verified with [ProB](https://prob.hhu.de/). 

The repository implements this pattern with the C++ `Alternative` construct of CSP4CMSIS 2.0 (a one-winner protocol on CMSIS-RTOS2 thread flags: exactly one ready guard is selected per `fairSelect()`, together with its own message). This formal abstraction evaluates the logical integrity and concurrency of the multiplexing architecture. CSP's external choice `[]` does not prescribe which of several ready channels is taken, so the model covers `priSelect()` and `fairSelect()` alike; the fairness of `fairSelect()` (serving ready guards in turn) is a property of the implementation, not of this model.

## Network Architecture

The system models a reactive hub where two independent processes push data asynchronously to a single listening process. 

1. **`SenderA(c)` and `SenderB(c)`:** Independent entities maintaining localized sequence counters. They push tuples `(source_id, sequence_num)` over their respective channels (`chan_A` and `chan_B`) to the receiver.
2. **`Receiver(nextA, nextB)`:** A reactive hub tracking the expected sequence number for both senders. Using the CSP External Choice operator (`[]`), the receiver blocks until *either* sender is ready, processes the message, performs sequence validation, and recurses with updated tracking state.
3. **`error_found`:** An observable system termination trap that triggers if a sender transmits an out-of-order sequence or an incorrect ID payload. 

## System Composition

The system is assembled into a top-level `SYSTEM` process demonstrating both parallel synchronization and unconstrained interleaving:

```csp
SYSTEM = 
    (SenderA(0) ||| SenderB(0)) 
    [| {| chan_A, chan_B |} |] 
    Receiver(0, 0)
