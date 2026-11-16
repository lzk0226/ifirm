# Architecture

Ifirm is a capability microkernel for RISC-V 64 that runs on QEMU `virt` under OpenSBI. It
starts from a single statically linked ELF, composes kernel components and user domains from
declared manifests, and then runs ordinary ELF programs loaded from a writable disk image.

This document is the 10-minute version. The full specification (40 chapters, 375 numbered
requirements, 46 acceptance criteria) is kept separately and is not part of this repository;
where the two disagree, the code and `tests/ac00*.sh` are authoritative.

## 1. Boot sequence

```
OpenSBI (QEMU -bios default)
  └─ ifirm.elf @ 0x80200000
       boot/start.S      set up stack, clear .bss, jump to kmain
       stage 0           UART up, board description parsed (RAM, timebase, UART, PLIC base)
       stage 1           PMM bitmap, kernel page table, trap vector, timer
       component chain   13 atomic components, in order:
                         board(1) pmm(2) vmm(3) trap(3) cap(4) ca(5) ict(5) ipc(5)
                         tpl(6) domain(7) fs(7) spawn(8) init(9)
       stage 2           idle: schedule domains
```

Each component returns 0 or a negative status; a failure prints `IFIRM-COMP FAIL name=<c> st=<code>`
and halts. Level numbers must be non-decreasing along the chain, and every component in a
`requires` list must already have been provided.

## 2. Memory

- 256 MiB of RAM at `0x80000000`; the kernel is linked at `0x80200000`.
- Physical memory is managed by a bitmap allocator over 4 KiB frames.
- The kernel keeps an identity map of the first 1 GiB for MMIO (UART at `0x10000000`, PLIC at
  `0xc000000`, virtio-mmio slots from `0x10001000`). Identity mappings are **not** user pages:
  only PTEs with `PTE_U` count as "already mapped" when the ELF loader places segments.
- Per-domain address space:

```text
0x0000000000000000   guard page            (unmapped)
0x0000000000001000   KIP page              (reserved, not populated in v4)
0x0000000000010000   template slot 0       (2 MiB per template slot)
0x0000000000210000   template slot 1
...
0x0000000020000000   ICT                   16 pages
0x0000000020010000   user stack            16 KiB + 4 KiB guard, stride 0x5000
```

## 3. Capabilities

- A capability entry is 16 bytes: `{object_id, gen_owner, rights: u16, object_type: u8,
  flags: u8, derived_from}`.
- A handle is 32 bits: `[31:12]` generation, `[11:0]` index; index 0 is never valid. Reusing a
  slot bumps the generation, so stale handles fail the generation check instead of hitting a
  new object.
- Rights are 15 bits (`0x7FFF`): READ, WRITE, EXEC, SEND, RECV, CALL, REPLY, MAP, UNMAP, IRQ,
  MMIO, DELEGATE, REVOKE, INSTANTIATE, ADMIN.
- `cap_delegate` derives a child entry (rights can only shrink, `derived_from` records the
  parent). `cap_revoke_tree` walks the derivation tree and also clears the matching ICT
  shadow entries, so a revoked capability cannot be used through an installed call slot.

## 4. Central authority (CA) and its queue

Policy is an artifact. `charter.bin` is produced by `tools/mkcharter.c` at build time:

```text
offset 0   magic "IFIRMCHA", u16 version, u16 nrules, u32 FNV-1a digest
offset 32  rule[]: {u8 dom, u8 type, u8 phase, u8 pad, u16 rights, u16 pad2, u32 object_id}
```

The kernel parses it, verifies the digest, and uses it as the only source of authority.
`dom == 0` is a wildcard for spawned domains (`dom >= 6`); runtime requests only match rules
with `phase == RUNTIME`. `ca_authorize(dom, type, rights, object_id, phase, &cap)` is the single
entry point; it increments `ca_requests_total`, then `ca_grants_total` or `ca_denied_total`, and
appends to a 256-entry audit ring.

The queue exists so a domain does not block inside `ecall`:

| Step | Interface | Semantics |
|---|---|---|
| enqueue | `SYS_CAP_REQUEST` with `a3 & 1` | no policy decision; returns a ticket, or `-EAGAIN` when full |
| drain | `ca_drain()` from the timer interrupt | processes pending requests in ticket order |
| poll | `SYS_CAP_POLL(ticket, &handle)` | 0 done, `-11` not yet, `-5` wrong domain |

Tickets are `(generation << 8) | slot`, and the generation increments on slot reuse, so a stale
ticket can never observe a newer request's result. Full queue is back-pressure, not a denial:
it does not increment the deny counter.

The design invariant is that the **data path never asks the CA for permission** — only setup,
delegation and revocation do. That keeps the cost of a central authority measurable.

## 5. Templates and composition

A user template is a flat image with a 64-byte manifest: magic `IFIRMTPL`, version 2, header
size, text length, data length, name hash, digest (FNV-1a64 over text||data), name[16], ABI,
stack pages, and a service bitmap (CONSOLE 0x01, CHANNEL 0x02, REPLY 0x04, FAULT 0x08).

At boot the registry verifies each template, then `domain_compose_all()` builds five domains
(producer, consumer, faulty, producer#2, consumer#2) from an explicit plan: two channels, ICT
slots installed only for services the template declared, capabilities granted only through the
CA. The whole set has its own digest (`IFIRM-TPLSET`).

## 6. IPC

Endpoints are kernel objects with a holder bitmap. `SEND` copies a message into a receiver's
buffer if it is waiting, otherwise queues it (the receiver is woken on the timer tick).
`CALL` marks `call_pending` and blocks the caller; `REPLY` copies the result back, clears the
pending flag on both success and failure, and wakes the caller with the error code if the copy
fails. Messages have a fixed maximum size; overflow wakes the receiver without dequeuing.

## 7. Scheduling and traps

One hart, round-robin over READY threads on trap return, 100 Hz timer tick (timebase 10 MHz).
There is no kernel preemption and no priority. The timer handler also drains the CA queue.

## 8. Filesystem and block device

`IFIRMFS2` on a virtio-mmio block device (512-byte sectors):

```text
LBA 0        superblock: "IFIRMFS2", version 2, n_inodes 64
LBA 1-2      block bitmap (2 sectors)
LBA 5-12     inode table (64 x 64 B)
LBA 13-      data blocks, total 8192 blocks, root inode 1
inode        64 bytes: type, used, nlink, size, parent, blocks[7], indirect, name[16], mtime
```

Seven direct blocks plus one single-indirect block give a 67.5 KiB maximum file, write-through,
no journal. Directories are inodes with `READDIR` by index. The image is built by
`tools/mkfs` and mounted read-write at boot; `IFIRM-FS on files=.. bytes=.. blk=v1 rw=1 free=..`
reports the result.

The virtio driver scans eight MMIO slots from `0x10001000` and takes the last empty one. QEMU
8.2 defaults to the legacy interface; the driver handles both. Three descriptors per request:
header, data (with `VIRTQ_DESC_F_WRITE` — omitting it silently yields zero-filled buffers),
and a one-byte status. Completion is polled with a bounded loop.

## 9. ELF loader and spawn

`spawn(path, argv)` reads the file through the FS, validates the ELF header and program headers
with subtraction-based bounds checks, allocates pages, copies segments, maps them user-accessible,
copies argv onto the user stack, grants console access through the CA, installs the ICT slot,
and marks the domain READY. Failures go through one `spawn_reset(id)` path that frees the
address space, clears the capability table, and resets ICT shadow entries — this is the fix for
the "domain number reuse inherits stale capabilities" bug (v4.2, ac006).

## 10. System calls

27 calls, `a7` = number, `a0` = status on return, `a1` = optional payload:

```text
1 IPC_SEND      2 IPC_RECV      3 IPC_CALL      4 IPC_REPLY     5 YIELD
6 EXIT          7 CLOCK         8 DEBUG_WRITE   9 DOMAIN_INFO  10 CONSOLE_WRITE
11 CAP_REQUEST 12 CAP_REVOKE   13 CAP_DELEGATE 14 CAP_AUDIT    15 SPAWN
16 WAIT        17 CONSOLE_READ 18 FS_OPEN      19 FS_READ      20 FS_WRITE
21 FS_CLOSE    22 FS_UNLINK    23 FS_READDIR   24 FS_STAT      25 PROC_LIST
26 FS_MKDIR    27 CAP_POLL
```

Registers other than `a0`/`a1` are preserved across a syscall. `SYS_ICT = 0` with `a6` = slot is
the "call through an installed ICT slot" form.

## 11. Comparison

|  | Ifirm v4.2 | seL4 | Fuchsia | QNX | Linux 0.01 |
|---|---|---|---|---|---|
| Isolation primitive | capabilities, 15 rights | capabilities | handles + jobs | POSIX processes | none (all root) |
| Policy location | hashed image, checked at boot | user-level servers | component framework | config files | kernel code |
| Composition | declared, digest-verified | static config | dynamic | dynamic | none |
| Drivers | in kernel | user space | user space | user space | in kernel |
| SMP | no | yes | yes | yes | no |
| Demand paging | no | yes | yes | yes | yes (swap) |
| Verification | tests only | formal proofs | none | none | none |
| Lines of code | ~6.1k | ~10k core | millions | millions | 10.2k |

Ifirm is closest to seL4 in isolation semantics, to ARINC 653 in static partitioning, to
Unikraft in build-time composition, and to Windows' SRM in having a central authority that
checks every grant. Its contribution is not a new isolation theory; it is making policy,
composition, and authorization cost visible and testable.

## 12. Known gaps / roadmap

1. Move drivers out of the kernel (virtio-blk first) — the largest TCB reduction available.
2. Demand paging + COW; without it, "spawn a program" is the only memory story.
3. IPC capability transfer and notifications.
4. Per-domain quotas and cancellation for the CA queue; make back-pressure fair.
5. Priorities and priority inheritance.
6. SMP (currently a hard non-goal: single hart is a design decision, DEC-008/009).
7. Persistence: journal, or at least a crash-consistent FS.
8. Real hardware boot (U-Boot, a board with a serial port) instead of QEMU/OpenSBI.
