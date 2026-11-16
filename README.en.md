# Ifirm

A statically-composed capability microkernel for RISC-V 64, with **policy-as-image** and
**digest-verified composition**. It boots on QEMU `virt` under OpenSBI, composes 13 kernel
components and 5 user domains from declared manifests, then loads real ELF programs from a
writable on-disk filesystem.

Status: **v4.2, research prototype**. It runs, it is tested, and it is not a production or
security kernel. See [SECURITY.md](SECURITY.md) before drawing conclusions about isolation.

Chinese documentation: [README.md](README.md) · Architecture: [docs/architecture.md](docs/architecture.md)

## What actually works

| Claim | How you can falsify it |
|---|---|
| Policy is an image, not code: the CA (central authority) loads its rules from `charter.bin`, a 32-byte header plus 12-byte rules, covered by a digest | Flip one byte of the charter → `IFIRM-COMP FAIL name=ca`, zero `IFIRM-DOMAIN` lines (test ac004) |
| Composition is a kernel operation: 13 components declare name/provides/requires/level; the kernel checks monotonic levels and satisfied dependencies, hashes each component, and prints an aggregate digest | Remove a component or break a level → boot stops before `stage=2` |
| Templates are verified before they run: each user template carries a v2 manifest (magic, ABI, name, service bits, stack pages, digest) | Flip one byte of a template → `IFIRM-COMP FAIL name=tpl`, no READY (test ac002) |
| Userland is real, not compiled into the kernel: shell, coreutils, writable FS, ELF loader, spawn/wait | ac003 asserts `loaded from disk (elf)` appears 0 times in the kernel image and ≥1 time on disk |
| Authorization is auditable: one entry point `ca_authorize()`, a 256-entry audit ring, derivation tree, tree revoke, an 8-slot queue drained in the timer tick | `make run`, then type `ca` in the shell to dump grants/denials/enqueues |

## Quick start

Ubuntu 24.04 (or similar), x86_64 host, no KVM required (TCG is fine):

```sh
sudo apt install -y clang lld binutils-riscv64-linux-gnu gcc-riscv64-linux-gnu \
                    qemu-system-misc make
git clone https://github.com/<you>/ifirm && cd ifirm
make          # templates -> blobs -> kernel ELF -> charter -> disk image
make test     # AC-001..AC-006
make run      # QEMU serial console, exits after 20 s
```

`make test` prints one line per assertion and a final `TEST ac-00N PASS|FAIL`. On a 4-vCPU
TCG VM the full suite takes about 80 seconds.

## Boot log

```text
IFIRM-BOOT stage=0
IFIRM-BOARD model=riscv-virtio,qemu ram=256MiB timebase=10000000 uart=0x10000000 plic=0xc000000
IFIRM-BOOT stage=1
IFIRM-MEM total=65536 free=.. used_kernel=.. frag=..
IFIRM-VM on
IFIRM-CAPS mmu=1 timer=1 plic=1 uart=1 hw_watchdog=0 kernel_hang_recovery=false controller_restart=true cache=C1
IFIRM-COMP n=1 name=board lvl=1 digest=0x..            (13 lines)
IFIRM-CA charter=0x9a0b264 rules=9 digest=0x1270c770
IFIRM-CA queue depth=8 full=-11 drained=8 ok=5 deny=3 steal=1 test=ok
IFIRM-TPLSET n=3 ok digest=0xffffffffe9685ef4
IFIRM-DOMAIN id=1 state=READY name=producer ...
IFIRM-READY domains=5 pages_used=649 t_ms=133
IFIRM-FS on files=8 bytes=203868 blk=v1 rw=1 free=7769
IFIRM-INIT name=sh dom=6 entry=0x10000 pages=2
IFIRM-COMPOSE n=13 ok digest=0x4fc88915
IFIRM-BOOT stage=2
```

Then the shell runs: `ls`, `cat`, `hello`, `ping`, `crash`, `mkdir`, `write`, `ps`,
and `ca` (dump the audit ring).

## Repo layout

```
boot/start.S              entry, clear .bss, jump to C
arch/rv64/                UART (rx+tx), SBI, trap entry and dispatch
kernel/component.c        the 13 atomic components, level/dependency checks, digest
kernel/ca.c               charter parsing, rules, audit ring, derivation, revoke, 8-slot queue
kernel/cap.c              capability table, generations, derivation, revoke into ICT
kernel/template.c         template v2 manifest validation; tplreg.c registry
kernel/domain.c           static composition of 5 domains
kernel/spawn.c            spawn from disk, argv, domain reaping
kernel/fs.c               IFIRMFS2 (writable, 64 inodes, 67.5 KiB max file)
kernel/virtio.c           virtio-mmio block device (legacy and modern)
kernel/elf.c              ELF64 loader
kernel/syscall.c          27 syscalls + user pointer validation
user/                     ulib.h, shell, 7 programs, 3 templates
tools/                    mkimg, mkfs, mkcharter (host tools)
tests/                    ac001..ac006
```

Source: 6,117 lines. Kernel image: text 35,354 / data 10,656 / bss 270,460 bytes.

## What is borrowed, what might be new

Borrowed, and not claimed otherwise: capability-based isolation (Hydra → EROS → seL4), static
partitioning (ARINC 653), build-time composition (Unikraft), handle semantics (Fuchsia),
restart-in-place supervision (QNX).

Possibly new, stated as falsifiable properties rather than as theory:

1. **Policy as an image.** Authority rules live in a compiled, hashed artifact outside the
   kernel text; the kernel refuses to compose if the digest does not match. Changing a policy
   is a build, not a code change, and the change is visible in the boot log.
2. **Composition as a kernel operation.** Components and templates are checked for declared
   dependencies and level monotonicity, and each is hashed into an aggregate digest, so the
   exact composition is a single value.
3. **A measurable authorization path.** Every grant/deny/revoke is counted and auditable, and
   the data path is forbidden from asking the CA for permission (the design invariant
   `data_path_ca_requests_total == 0`), so the cost of centralization is bounded and visible.

If you think these are re-packaging of existing ideas, that is a fair reading — the point is
that they are measurable here, not that they are unprecedented.

## Limitations (short list)

Single hart, no SMP; no demand paging or COW; no device interrupts (polling virtio, no PLIC
use beyond reporting); drivers in the kernel; no user-space drivers; no IPC capability
transfer; no notifications; round-robin only, no priorities; no persistence guarantees; the
console syscall does not check a capability. Details and the full threat model in
[SECURITY.md](SECURITY.md).

## License

Apache-2.0. See [LICENSE](LICENSE).
