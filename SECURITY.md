# Security policy and threat model

## Status

Ifirm is a **research prototype**. It is not a security kernel, it has not been formally
verified, and it has not been audited. Do not deploy it anywhere that matters, and do not
cite it as evidence that a design is secure.

## Attacker model

What the design does try to contain:

- A **malicious or buggy user domain**: it may pass arbitrary pointers, malformed ELF files,
  bad syscall arguments, oversized IPC messages, or try to use handles it does not own.
  User pointers are validated page by page (V|U) and copied through PTE-derived physical
  addresses; ELF bounds checks use subtraction to avoid overflow; handles carry a generation
  and are checked against the caller's table; the authorization queue validates ticket
  ownership.

What is explicitly **out of scope**:

- A compromised kernel component, the CA itself, or the boot chain (OpenSBI).
- Hardware attacks, DMA attacks, side channels, fault injection, rowhammer.
- A malicious template at build time: templates are trusted by digest, and whoever controls
  the build controls the digest.
- Multi-core concurrency: there is exactly one hart, so there are no cross-hart races to
  reason about — and no SMP isolation either.

## Known weaknesses (not bugs, design gaps)

1. `SYS_CONSOLE_WRITE` does not check that the caller holds a console capability. Every domain
   can print. This is an ABI-level gap; fixing it requires a capability check plus updating
   every user program.
2. Drivers (virtio-blk, UART) live in the kernel. That contradicts a minimal TCB and is the
   largest architectural gap.
3. The kernel polls the virtio queue with a bounded loop (20,000,000 iterations). A wedged
   device stalls a non-preemptible kernel until the bound is hit.
4. No demand paging: memory is committed up front, and a domain that exhausts its pages fails
   rather than swapping or being killed cleanly.
5. Single CA, single hart: the CA is a single point of failure and of authority.
6. The audit ring is 256 entries and lives in RAM; it does not survive reboot, and it can be
   overwritten by a busy system.
7. The authorization queue is 8 slots, FIFO only, with no cancellation and no per-domain
   quota. A domain can occupy slots by flooding requests (back-pressure is by `-EAGAIN`, not
   fairness).
8. Preemption happens only on trap return; a domain that spins without syscalls keeps the CPU
   until the next timer interrupt is delivered, and kernel code is not preemptible.
9. No ASID/PCID: every address-space switch flushes the TLB.
10. The test suite runs under TCG; timing assertions are relative, not absolute.

## Reporting

Open a GitHub issue for anything that is not exploitable. For something that is, open a
private security advisory on the repository. There is no bug bounty and no response-time
commitment; this is a one-person research project.
