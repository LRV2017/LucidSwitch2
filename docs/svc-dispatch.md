# Initial EL0 SVC Dispatch

## Implemented calls

The emulator dispatches two EL0 SVC calls after exception entry and returns from the exception:

- SVC 0x10 — GetCurrentProcessorNumber: returns virtual CPU index 0 in X0/W0. The current engine models one virtual CPU.
- SVC 0x1E — GetSystemTick: returns the deterministic virtual timer counter in X0. The timer currently advances one tick per guest instruction.

Unknown SVC numbers are left at the EL1 exception vector; the dispatcher does not pretend to implement them.

## Explicitly not implemented

- WaitSynchronization (0x18), thread/process creation and termination, mutex arbitration, event signaling, handle validation, IPC send/receive SVCs, memory mapping/query calls, and the remainder of the Horizon SVC table.
- Switch hardware counter frequency calibration. The virtual timer counter is a deterministic emulator tick count, not a hardware CNTpct implementation.
- Preemptive scheduling and wiring the cooperative host process/thread substrate into guest execution.
- Full PSTATE, exception syndrome, permission, and return semantics.

The syscall identifiers are cross-checked against public libnx SVC declarations and SwitchBrew SVC notes. This small subset is a testable guest ABI experiment, not Horizon compatibility.
