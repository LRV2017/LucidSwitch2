# Guest Kernel Runtime Foundation

## Scope

This is an original, deterministic host-side substrate for future guest-process and Horizon service work. It is not a reimplementation of Horizon OS, a binary-compatible SVC implementation, or a title launcher.

## Implemented

- Bounded process table with process identifiers, names, and active/terminated lifecycle states.
- Bounded thread table with AArch64 CPU context, entry PC, 16-byte-aligned stack pointer, initial X0 argument, priority, and lifecycle state.
- Cooperative scheduler: lower numeric priorities run first; threads of equal priority are selected FIFO by ready-set insertion order.
- Explicit blocking and waking by a nonzero wait key.
- Process-owned typed kernel handles, handle validation, close, and automatic owner cleanup on process termination.
- Deterministic tests for priority selection, yield, block/wake, process isolation, and cleanup.

## Not implemented

- No memory/address-space ownership, page-table isolation, process image loading, MOD0 relocation, NSO loading, or launch-argument ABI.
- No preemptive timer scheduler, CPU core migration, TLS ABI, kernel stacks, mutex wait queues, or event/semaphore semantics.
- No SVC dispatch table, HIPC/CMIF sessions, service manager, filesystem, audio, input, display, or Horizon object semantics.
- CPU contexts are held by this scheduler but are not automatically run by the emulator execution loop.
- This runtime is not enough to boot Horizon or run an ordinary Switch application.

## Design constraints

The runtime is single-host-thread and deterministic. It uses bounded tables and no background threads. Wait keys are opaque scheduler identifiers, not guest-visible Horizon handles. Kernel handles are process-local and identify typed host-side objects only; object implementations and service protocols remain future work.


## Synchronization object substrate

The host-side Runtime now implements manual/auto-reset events, bounded counting semaphores, and ownership-checked recursive mutexes behind process-owned typed handles. Event and semaphore probes are nonblocking; callers can resolve the object's ID, block a cooperative thread on that wait key, then signal/release the object to wake waiters. Mutex unlock wakes threads on its wait key. This provides testable synchronization semantics but is not the Horizon SVC ABI, a real-time scheduler, priority inheritance, or a full kernel object/handle-transfer implementation.
