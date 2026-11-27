# A64 exclusive and LSE atomic subset

## Exclusive forms

The interpreter supports aligned 32-bit and 64-bit general-purpose exclusive accesses:

- LDXR Wt/Xt and LDAXR Wt/Xt
- STXR Ws,Wt/Xt and STLXR Ws,Wt/Xt
- CLREX

The MemoryBus associates each mapped 16-byte granule with a version. Exclusive loads capture data and the current version. Ordinary writes and atomic read-modify-write operations advance the version. An exclusive store succeeds only when the address, transfer size and reservation version still match. Success returns status zero; a lost reservation returns status one without storing.

## LSE atomic memory operations

The interpreter supports byte, halfword, word and doubleword operations:

- LDADD / LDADDA / LDADDL / LDADDAL
- LDCLR / LDCLRA / LDCLRL / LDCLRAL
- LDEOR / LDEORA / LDEORL / LDEORAL
- LDSET / LDSETA / LDSETL / LDSETAL
- LDSMAX / LDSMAXA / LDSMAXL / LDSMAXAL
- LDSMIN / LDSMINA / LDSMINL / LDSMINAL
- LDUMAX / LDUMAXA / LDUMAXL / LDUMAXAL
- LDUMIN / LDUMINA / LDUMINL / LDUMINAL
- SWP / SWPA / SWPL / SWPAL

The old memory value is returned in the target register; register 31 discards the returned value. LDADD wraps at the selected width, bitwise operations use that width, signed and unsigned min/max compare at the selected width, and SWP stores the source operand. MemoryBus holds one exclusive lock across each complete operation, updates exclusive-reservation versions and invalidates code-cache generations after the write.

Acquire/release encodings are decoded but share the same serialized host model. This does not implement per-core store buffers, weak-memory reordering, private cache hierarchies, or complete barrier semantics. It must not be interpreted as a hardware-faithful ARM memory model.

## Concurrency boundary

MemoryBus synchronizes region lookup, mapping, permission checks, data reads/writes, exclusive reservations and LSE read-modify-write operations. The Switch 2 CPU cluster can schedule eight contexts on host workers. The model still lacks private L1/L2 caches, cache maintenance instructions, realistic cache-line ownership, TLB shootdowns and full architectural memory ordering. The 16-byte exclusive reservation granule is a conservative host-model choice, not a statement about Switch/Switch 2 hardware.

CAS and CASAL are now supported for byte/halfword/word/doubleword operands. CASP pair operations, pair-exclusive instructions, pointer authentication and the complete A64 instruction set remain unsupported.

## Tests

A64 fixtures cover LDXR/LDAXR/STXR/STLXR/CLREX, all implemented LSE operation families, 8/16/32/64-bit widths, alignment faults, 2,000 concurrent increments without lost updates, and LSE invalidation of a peer's exclusive reservation.
