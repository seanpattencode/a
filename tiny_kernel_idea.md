# Tiny Unix Kernel with Linux Fallback

## Core Idea

A minimal, TCC-compilable Unix kernel that implements the Linux syscall ABI. Handles common operations natively for speed and simplicity. Forwards unimplemented syscalls to a real Linux kernel running underneath.

## Design

### What it is

- A separate, standalone kernel — not Linux patches
- Speaks the Linux ABI: same syscall numbers, same ELF loader, same /proc and /sys interfaces
- Can run unmodified Linux binaries
- Written in clean, portable C — no GCC extensions, no macro hell
- Compiles with TCC in seconds, Clang -O3 for release

### What it implements natively

- Process management (fork, exec, wait, exit)
- Memory management (mmap, brk, munmap)
- File I/O (open, read, write, close, stat, lseek)
- Signals
- Pipes
- A simple scheduler
- A minimal VFS layer

### What it forwards to Linux

- Exotic syscalls (io_uring, perf_event, bpf, etc.)
- Hardware drivers (GPU, USB, network, storage controllers)
- Complex filesystems (ext4, btrfs, NFS)
- Networking stack (initially — implement later if desired)

### Forwarding mechanism

The kernel runs on top of Linux via KVM or syscall forwarding. When it encounters a syscall it hasn't implemented:

1. Trap the syscall
2. Forward it to the host Linux kernel
3. Return the result to the process

The process never knows the difference. It sees a Linux kernel. The implementation details are hidden behind the ABI boundary.

## Why separate kernel, not patches

| | Separate kernel | Linux patches |
|---|---|---|
| Codebase | Small, yours, clean | 30M lines, not yours |
| Updates | At your pace | Rebase hell every release |
| TCC support | Built-in from day one | Fight the mailing list forever |
| GCC extensions | None needed | Thousands of dependencies |
| Build time | Seconds | Minutes |
| Politics | Zero | Infinite |

## Why this isn't as hard as people think

The OS/kernel problem is misunderstood. People conflate three different problems:

1. **Writing a kernel** — actually easy. Process scheduling, memory management, syscall dispatch. This is a finite, well-understood problem. Unix proved it can be done in a few thousand lines.

2. **Hardware compatibility** — hard. Every USB device, every GPU, every network card, every storage controller. This is where Linux's 30 million lines come from. The fallback to Linux solves this entirely.

3. **Ecosystem and trust** — the real hard part. Getting people and businesses to use it. Linux binary compatibility via the ABI solves the software side. The business trust side takes time and track record.

By implementing (1) natively and delegating (2) to Linux, you skip the hard part and keep the clean part.

## Build system

```
# Dev build — instant iteration
tcc -o kernel kernel.c

# Release build — max performance
clang -O3 -o kernel kernel.c
```

That's it. If your kernel can't be built with a one-liner, it's too complicated.

## Precedent

- **WSL1** — Windows forwarding Linux syscalls. Shipped to millions. Ugly concept, worked in practice.
- **MINIX** — Tiny Unix. Proved a microkernel can be simple.
- **xv6** — Teaching kernel. Shows the core of Unix fits in ~10K lines.
- **SerenityOS** — "I'll do it myself." One person, from scratch, became a real project.

## The bet

A tiny kernel handling the fast path natively + Linux for everything else gives you:

- Sub-second cold builds
- A codebase you can hold in your head
- Linux binary compatibility for free
- A clean foundation to grow from

The ugly part — two kernels running — is a feature. It's a clean boundary. You implement syscalls at your own pace, and every one you implement is one less forwarded to Linux. Eventually the fallback path stops getting hit for your workload. You never have to implement the full kernel — just the parts you use.

## Multi-kernel backend

The forwarding layer doesn't have to target one Linux kernel. The tiny kernel becomes a router that picks which Linux backend handles the fallback based on context:

- **Bleeding-edge 6.x** — dev machine, new features, latest drivers
- **Hardened 5.15 LTS** — production workloads, stability guarantees
- **Legacy 4.x** — old hardware compatibility, ancient driver support
- **Experimental** — your own kernel branch with new scheduler, new memory manager, whatever you're testing

Same process, same binary, same ABI. The backing kernel changes based on config, workload, or even per-process policy.

This is something you literally can't do with Linux alone — you're locked to one kernel version per boot. A thin routing layer in front changes that. You get version isolation without containers, without VMs, without rebooting.

It also makes upgrades safe. Roll a new kernel version into the pool, route 10% of fallback traffic to it, watch for issues. If it breaks, route back. Zero-downtime kernel upgrades with canary testing — at the kernel level.

## Hot swap via fast compile

Hot swapping a running kernel is traditionally one of the hardest problems in systems programming. You have to serialize all kernel state — page tables, file descriptors, sockets, scheduler queues, device state — load a new kernel, deserialize everything, and resume. Linux has `kexec` for fast reboot and `kpatch`/`ksplice` for live function patching, but neither can swap a whole kernel without process death.

**A 20ms compile time makes this problem irrelevant.**

If the kernel compiles in 20ms and boots in milliseconds, you don't need true hot swap. You need a fast restart:

1. Suspend all processes
2. Compile new kernel (20ms with TCC)
3. kexec into it (milliseconds)
4. Restore process state from the stable layer
5. Resume

Total downtime: under a second. Processes see a brief pause, not a reboot. At that point the distinction between "hot swap" and "really fast restart" is academic — nobody can tell the difference.

The entire hot swap problem exists because Linux takes minutes to build and seconds to boot. A TCC-compilable kernel eliminates both. You don't solve hard problems — you make them irrelevant.

### State preservation

The thin kernel layer is key. If the tiny kernel owns all persistent state (process table, fd table, memory mappings) and only forwards computation to the Linux backend, then swapping the backend loses nothing. The stable layer is the checkpoint. The backend is disposable.

Design the forwarding interface to be stateless — every forwarded syscall is self-contained, no backend-side state persists between calls — and swapping backends becomes "stop forwarding here, start forwarding there." No serialization needed.

## The Tanenbaum-Torvalds debate, resolved

In 1992, Tanenbaum argued monolithic kernels are unmaintainable and crash-prone. The solution: microkernels — isolate components into separate processes, communicate via message passing. Linus argued the IPC overhead is too high and monolithic is the only way to get real performance.

They were both right about different things and both wrong about the root cause.

**The problem was never monolithic vs microkernel. It was code size.**

Tanenbaum's real goals:
- Readable code
- Fewer bugs
- Easier to audit and reason about

Linus's real goals:
- No IPC overhead
- No context switches between kernel servers
- Direct function calls, maximum performance

A tiny monolithic kernel achieves both. It's small enough to be correct and auditable (Tanenbaum's goal) with zero message-passing overhead (Linus's goal). The microkernel was the wrong tool for the right problem.

### Why this architecture avoids the microkernel trap

A microkernel splits the kernel into servers that must constantly talk to each other. File system server talks to disk driver server talks to memory manager. Every interaction is an IPC — context switch, message copy, context switch back. This overhead is permanent and structural. It never goes away no matter how good your implementation is.

This tiny kernel architecture has exactly **one** communication boundary: the forwarding path to the Linux backend. And that boundary only gets hit for unimplemented syscalls. Every syscall you implement natively is one that never crosses the boundary. Over time, for your workload, the fallback path stops getting hit entirely.

| | Microkernel | Tiny monolithic + Linux fallback |
|---|---|---|
| IPC boundaries | Many, permanent, on every operation | One, shrinks over time |
| Communication overhead | Structural, unavoidable | Only on unimplemented syscalls |
| Code simplicity | Split across many servers | One small codebase |
| Safety/auditability | Good (isolation) | Good (small enough to read) |
| Performance | Hurt by IPC | Native speed for implemented paths |

Tanenbaum was solving the right problem with the wrong tool. A tiny monolithic kernel solves it without the overhead.
