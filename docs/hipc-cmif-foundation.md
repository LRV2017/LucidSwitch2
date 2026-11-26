# HIPC/CMIF Framing Foundation

## Research references

- SwitchBrew HIPC and IPC marshalling notes: https://switchbrew.org/wiki/HIPC and https://switchbrew.org/wiki/IPC_Marshalling
- libnx CMIF header definitions: https://switchbrew.github.io/libnx/cmif_8h_source.html

HIPC is the transport framing used by Horizon IPC. CMIF is the command protocol commonly carried in HIPC raw data. The project implements only a constrained, descriptor-free subset.

## Implemented

- Parses the 8-byte HIPC header and validates message type, raw word count, reserved bits, and descriptor/special-header counts.
- Accepts only Request and RequestWithContext command types.
- Aligns the CMIF message header to 16 bytes within the raw-data region.
- Validates the SFCI signature, supported CMIF versions 0/1, method ID, token, and bounded payload.
- Encodes the 16-byte SFCO output header and bounded response payload.
- Provides a host-side service/method registry with explicit method handlers.

## Deliberately unsupported

- HIPC pointer descriptors, send/receive/exchange buffers, receive-list descriptors, PID transfer, copy/move handles, and special headers.
- CMIF domain headers, domain object IDs, object creation/release semantics, and multi-object session routing.
- TIPC, service-manager protocols, SVC send-sync-request behavior, thread-local IPC buffers, and actual guest memory mapping.
- No Horizon filesystem, audio, input, display, application, or other named service implementations are registered by default.
- The command registry is a clean-room host-side dispatch facility; it is not a claim of binary or behavioral compatibility with Nintendo's services.

This is a testable message-framing foundation only. It cannot launch a retail Switch title or substitute for the full HIPC/CMIF stack.
