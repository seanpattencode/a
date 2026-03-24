# Goto vs Function Call in C: A Benchmark and Assembly Analysis

## Overview

Benchmarking `goto` loops against function calls in C, compiled with TCC (no optimization), Clang `-O0`, Clang `-O2`, Clang `-O3`, and Clang `-O3 -march=native`. Includes assembly-level analysis explaining the performance differences.

## The Code

Two equivalent operations — incrementing a counter 100,000,000 times:

```c
// Function call version
int func_add(int a, int b) {
    return a + b;
}
for (int i = 0; i < ITERS; i++) {
    result = func_add(result, 1);
}

// Goto version
loop:
    result = result + 1;
    i++;
    if (i < ITERS) goto loop;
```

## Benchmark Results

30 runs, 100,000,000 iterations each, alternating execution order to eliminate ordering bias.

| Compiler Flags         | func call avg (ms) | goto loop avg (ms) | Ratio     |
|------------------------|--------------------:|--------------------:|-----------|
| TCC (no opt)           | ~301                | ~164                | **1.83x** |
| Clang `-O0`            | ~301                | ~164                | **1.83x** |
| Clang `-O2`            | ~162                | ~159                | **1.01x** |
| Clang `-O3`            | ~163                | ~160                | **1.02x** |
| Clang `-O3 -march=native` | ~162             | ~160                | **1.01x** |

### Key Observations

- **Without optimization**: goto is consistently ~1.83x faster.
- **With optimization (-O2+)**: the gap disappears entirely. The ~1-2% difference is measurement noise — confirmed by alternating the execution order across runs, which spread outlier spikes evenly across both methods.
- **-O3 and -march=native** provide no additional benefit over -O2 here. The function is trivial enough that inlining at -O2 already eliminates all overhead.

## Why Goto Is Faster Without Optimization

### TCC Assembly (never inlines)

TCC is a single-pass compiler. It **never** inlines functions. Every function call emits a real `bl`/`ret` pair.

**Goto loop** — direct jump:
```asm
ldur  w0, [x29, #-0x4]     ; load result
add   w0, w0, #0x1          ; result + 1
stur  w0, [x29, #-0x4]     ; store result
ldur  w0, [x29, #-0x8]     ; load i
add   w0, w0, #0x1          ; i++
stur  w0, [x29, #-0x8]     ; store i
cmp   w0, #100              ; i < 100?
cbnz  → top                 ; branch back
```

**Function call loop** — real call overhead each iteration:
```asm
mov   w1, #0x1              ; arg2 = 1
ldur  w0, [x29, #-0x4]     ; arg1 = result
bl    simple_add            ; CALL: save lr, jump to function
                            ;   function body: add w0, w1, w0; ret
stur  w0, [x29, #-0x4]     ; store return value
b     → loop check          ; back to loop
```

The `bl` (branch-and-link) instruction saves the return address to the link register, jumps to the function, the function executes `add` + `ret`, then returns. This call/return overhead happens **every single iteration** — 100 million times.

### Clang -O0 Assembly

Same story as TCC. `-O0` means no inlining, so every `func_add` call is a real function call with full stack frame overhead.

## Why They're Equal With Optimization

### Clang -O2 Assembly

At `-O2`, Clang **inlines** `func_add`, producing identical assembly for both loops:

```asm
; func call loop (inlined)        ; goto loop
.LBB1_2:                          .LBB1_4:
    ldur  w9, [x29, #-44]            ldur  w9, [x29, #-44]
    subs  w8, w8, #1                  subs  w8, w8, #1
    add   w9, w9, #1                  add   w9, w9, #1
    stur  w9, [x29, #-44]            stur  w9, [x29, #-44]
    b.ne  .LBB1_2                     b.ne  .LBB1_4
```

**Identical instructions.** The function call has been completely eliminated — the compiler replaced it with the function body directly in the loop. Both are just: load, decrement counter, add, store, branch.

The `ldur`/`stur` (load/store) remain because the result variable is `volatile`, forcing memory access each iteration (otherwise -O2 would compute the entire result at compile time and emit `mov w0, #100000000`).

## When Does Inlining NOT Happen?

Even with `-O2`, the compiler won't always inline. Tested cases:

```c
// 1. Always inlined — matches goto
int simple_add(int a, int b) { return a + b; }

// 2. Never inlined — real bl call every time
__attribute__((noinline)) int noinline_add(int a, int b) { return a + b; }

// 3. Complex function — compiler decides based on cost model
int heavy(int x) {
    int r = x;
    for (int i = 0; i < 10; i++) r = r * 31 + i;
    return r;
}
```

Clang -O2 assembly confirms:
```asm
.LBB3_1:  ; goto           → add + branch (direct jump)
.LBB3_3:  ; simple_add     → add + branch (inlined, identical to goto)
.LBB3_5:  ; noinline_add   → bl noinline_add (real function call!)
.LBB3_7:  ; heavy          → madd + branch (inlined, different body)
```

### Cases where function calls will NOT be optimized to match goto:

| Case | Why |
|------|-----|
| `__attribute__((noinline))` | Explicitly prevents inlining |
| Function in separate `.c` file | Compiler can't see the body (unless LTO is enabled) |
| `-O0` | No optimization at all |
| Recursive functions | Can't inline infinite depth |
| Function pointers | Compiler can't resolve target at compile time |
| Very large function bodies | Inliner's cost model decides it's not worth it |

## TCC vs Clang: Goto Loop Assembly Comparison

Same goto loop, three compilers. 30 runs, 100M iterations each.

| Compiler | Instructions per iteration | Avg time (ms) | Min (ms) |
|----------|---------------------------:|---------------:|---------:|
| **TCC**      | ~14 | 167.4 | 163.6 |
| **Clang -O0** | ~12 | 164.7 | 161.0 |
| **Clang -O2** | **4** | 161.0 | 158.9 |

### TCC goto loop — 14 instructions

```asm
ldur  w0, [x29, #-0x24]       ; load result (volatile)
add   w0, w0, #0x1             ; result + 1
stur  w0, [x29, #-0x24]       ; store result
mov   x30, #-0x120             ; compute address of i (via x30!)
ldr   w0, [x29, x30]           ; load i
mov   x1, x0                   ; dead copy
add   w0, w0, #0x1             ; i++
mov   x30, #-0x120             ; compute address of i AGAIN
str   w0, [x29, x30]           ; store i
mov   x30, #-0x120             ; compute address of i A THIRD TIME
ldr   w0, [x29, x30]           ; reload i for compare
mov+movk x1, #100000000        ; load constant (2 insns)
cmp   w0, w1                   ; i < ITERS?
cset  w0, lt / cbnz            ; materialize bool, then branch
```

TCC wastes instructions because:
- **Reloads `i`'s stack address 3 times** via `mov x30, #-0x120` — no register allocation, recalculates every access
- **Uses `x30` (the link register!) as scratch** — the only register TCC knows how to use freely
- **Dead `mov x1, x0`** — leftover from expression evaluation, never used
- **`cset` + `cbnz` instead of branch-on-flags** — materializes the comparison result into a register, then tests the register, instead of branching directly on the flags

### Clang -O0 goto loop — 12 instructions

```asm
ldur  w8, [x29, #-0x2c]       ; load result (volatile)
add   w8, w8, #0x1             ; result + 1
stur  w8, [x29, #-0x2c]       ; store result
ldr   w8, [sp, #0x28]          ; load i
add   w8, w8, #0x1             ; i++
str   w8, [sp, #0x28]          ; store i
ldr   w8, [sp, #0x28]          ; reload i (redundant — just stored it)
mov+movk w9, #100000000        ; load constant (2 insns)
subs  w8, w8, w9               ; compare
b.ge  exit                     ; branch if done
b     loop                     ; branch back
```

Better than TCC but still wasteful:
- **Redundant reload of `i`** right after storing it — O0 doesn't track that `w8` already holds the value
- **Two separate branches** at the end (`b.ge exit` + `b loop`) instead of a single conditional branch
- At least uses proper registers (`w8`, `w9`) instead of abusing `x30`

### Clang -O2 goto loop — 4 instructions

```asm
ldr   w9, [x29, #0x1c]        ; load result (volatile)
subs  w8, w8, #0x1             ; decrement counter (sets flags)
add   w9, w9, #0x1             ; result + 1
str   w9, [x29, #0x1c]        ; store result (volatile)
b.ne  loop                     ; branch on flags directly
```

Everything is optimized away:
- **`i` lives in a register (`w8`)** — counts down from 100M, never touches memory
- **`subs` sets the zero flag directly** — no separate `cmp`, the branch reads flags from the decrement
- **No loads/stores for the counter** — only `result` hits memory because it's `volatile`
- **Single conditional branch** — `b.ne` reads the flags from `subs`, no `cset` indirection

### What Makes the Difference

| Optimization | TCC | Clang -O0 | Clang -O2 |
|---|---|---|---|
| Register allocation for `i` | No (stack + x30 scratch) | No (stack) | Yes (w8) |
| Eliminate redundant loads | No | No | Yes |
| Branch on flags directly | No (cset+cbnz) | Partially (subs+b.ge+b) | Yes (subs+b.ne) |
| Count direction optimization | No (counts up) | No (counts up) | Yes (counts down) |
| Dead code elimination | No (dead mov) | No (redundant reload) | Yes |

Counting down instead of up is a subtle but important optimization — decrementing to zero lets `subs` set the zero flag naturally, eliminating the need to load the 100000000 constant for comparison on every iteration.

## O2 vs O3 vs O3 -march=native Assembly Diff

### The hot loop: identical

All three produce the exact same goto inner loop:

```asm
ldr   w9, [x29, #...]         ; load result (volatile)
subs  w8, w8, #0x1             ; decrement counter
add   w9, w9, #0x1             ; result + 1
str   w9, [x29, #...]         ; store result
b.ne  loop                     ; branch back
```

**O3 and O3 -march=native do not improve the hot loop over O2.** There's nothing left to optimize — it's already the minimum possible instruction sequence for a volatile increment loop.

### O3 -march=native vs O3: zero difference

```
$ diff goto_O3.s goto_O3_native.s
(no output — files are identical)
```

`-march=native` has no effect here. The loop uses only basic integer and memory instructions (`ldr`, `str`, `add`, `subs`, `b.ne`) that are available on every AArch64 CPU. There are no SIMD opportunities, no advanced instructions, nothing for `-march=native` to unlock.

### O2 vs O3: different stats computation, same hot loop

The only differences are in the **post-benchmark statistics code** (computing min/max/avg over the 30 timing samples):

**O2** — uses **NEON SIMD** to vectorize the min/max reduction:
```asm
cmgt  v1.2d, v22.2d, v0.2d    ; compare 2 values at once
bsl   v1.16b, v22.16b, v0.16b ; bitwise select (vectorized min/max)
addp  d4, v4.2d               ; pairwise add for sum
```

**O3** — uses **scalar** compare-and-select chains:
```asm
cmp   x11, x10                ; compare one pair
csel  x8, x11, x10, lt        ; select min
csel  x9, x11, x10, gt        ; select max
add   x10, x11, x10           ; accumulate sum
; ... repeated 30 times
```

O2 processes the 30 timing results 2-at-a-time using 128-bit NEON registers (`v0`–`v24`), while O3 unrolls it into a long chain of scalar comparisons. O3's approach uses more instructions but avoids the overhead of moving data between scalar and NEON register files.

Neither approach affects the benchmark result — this code runs once after all timing is complete. The actual measured loop is byte-for-byte identical across O2, O3, and O3 -march=native.

## Conclusion

- **Goto is fundamentally a direct jump.** It always compiles to a branch instruction (`b` on ARM64). No overhead, no indirection.
- **Function calls have inherent overhead**: save return address, potentially push/pop registers, jump, execute, return. This is eliminated only when the compiler inlines the function.
- **With a modern optimizing compiler (-O2+)**, trivial functions get inlined and produce identical machine code to goto. There is zero performance difference.
- **Without optimization (TCC, -O0)**, goto is ~1.83x faster because every function call pays the full call/return cost on each iteration.
- **Use functions for readability.** The performance difference only exists in unoptimized builds. In production (`-O2`+), the compiler makes them equivalent. Goto has legitimate uses (error cleanup, state machines, generated code) but "performance" is not one of them in optimized builds.
