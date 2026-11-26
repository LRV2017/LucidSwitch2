# A64 exclusive-access subset

## Implemented forms

The interpreter supports aligned 32-bit and 64-bit general-purpose exclusive accesses:

- LDXR Wt, [Xn|SP] and LDXR Xt, [Xn|SP]
- LDAXR Wt, [Xn|SP] and LDAXR Xt, [Xn|SP]
- STXR Ws, Wt, [Xn|SP] and STXR Ws, Xt, [Xn|SP]
- STLXR Ws, Wt, [Xn|SP] and STLXR Ws, Xt, [Xn|SP]
- CLREX

The memory bus associates each mapped 16-byte granule with a version. An exclusive load returns the data and captures the current version in the executing CPU context. Any ordinary guest write to that granule advances its version. An exclusive store succeeds only when the current version, physical address, and access size still match the context's reservation. Success writes the data and returns status zero; a lost or mismatched reservation performs no write and returns status one. Every completed store-exclusive attempt consumes the local reservation.

The 16-byte reservation granule is a conservative implementation choice for this host model, not a claim about the Switch or Switch 2 hardware's exact monitor granularity. Byte and halfword forms, pair exclusives, LSE atomic instructions, and Load-Acquire/Store-Release (LDAR/STLR) remain unsupported.

## Concurrency boundary

The Switch 2 CPU cluster schedules one guest instruction at a time on the host thread. The shared reservation-version model lets tests interleave two architectural contexts and verify that one core's store invalidates another core's reservation. MemoryBus is not currently protected for concurrent host threads. This implementation does not claim true parallel SMP, hardware cache coherency, cache-line ownership, or a complete Arm memory model.

LDAXR and STLXR decode as their acquire/release forms, but because guest execution is currently sequential on one host thread, this layer does not implement independent host-thread ordering fences. DMB/DSB/ISB semantics and broader barrier behavior are still future work.

## Tests

The A64 tests cover successful W-register exclusive load/store, failure after an interleaved peer write, successful X-register LDAXR/STLXR, CLREX invalidation, and a misaligned exclusive-load fault. The compatibility report labels the feature partial.
