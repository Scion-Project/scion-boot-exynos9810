# scion-boot-exynos9810

A minimal **first-stage AArch64 bootloader skeleton** for Exynos 9810 phones, starting with the Samsung Galaxy S9
(`starlte`, Exynos 9810). This is the stage that runs with your own code already
in control at EL3: it brings up the lowest-level machine state, then hands off to
a second-stage loader (splash / boot menu / XNU or Linux handoff).

It is deliberately a **scaffold**. The CPU-agnostic plumbing (EL3 entry, C
runtime, UART console, PSCI skeleton, the hand-off contract) is written and
builds today, and the smoke test runs under QEMU. The parts that are specific to
Exynos 9810 silicon — clock tree, PMIC sequencing, DRAM controller training — are
stubbed with documented `TODO(starlte)` markers for you to fill from the SoC
datasheet and the vendor DT. No register addresses are invented; where a value is
unknown it is a named constant with a comment pointing at where to get it.

## Scope / boundary

This repo is bootloader engineering only. See [`docs/boundary.md`](docs/boundary.md).
It contains **no exploit, no secure-boot bypass, no signature-check patch, and no
key/fuse/keybox manipulation**. How this stage comes to be trusted and executed on
a given device is out of scope and not part of this tree — that is the operator's
own affair on their own hardware. Everything here runs unmodified in QEMU with no
device at all.

## Layout

```
src/start.S          EL3 reset entry: stack, BSS clear, drop into C
src/main.c           stage-1 flow: console → platform init → load/verify → handoff
src/platform/        exynos9810.h + uart/clock/dram/pmu (clock+dram are stubs)
src/lib/             tiny freestanding string + printf (no libc)
src/psci/psci.c      PSCI/CPU_ON skeleton (spin-table secondary release)
include/boot.h       the stage-1 → stage-2 hand-off struct (ABI)
linker/stage1.ld     bare-metal layout
qemu/                QEMU virt smoke test
```

## Build

```sh
# QEMU smoke test (PL011 UART, prints the handoff contract and halts)
make PLATFORM=qemu
make PLATFORM=qemu qemu      # needs qemu-system-aarch64

# Real hardware target (requires the TODO(starlte) stubs to be filled)
make PLATFORM=starlte
```

Toolchain: `aarch64-none-elf-gcc` (bare metal) or `aarch64-linux-gnu-gcc`.

## What to fill in (in order)

1. `src/platform/exynos9810.h` — set `UART_BASE` and the PMU/CMU bases from the
   vendor DT (`system-controller@14060000` is already noted).
2. `src/platform/uart.c` — the Exynos console path is sketched; confirm the
   divisor/line-control against a known-good baud on your board.
3. `src/platform/clock.c` — CMU bring-up for the console + DRAM path.
4. `src/platform/dram.c` — LPDDR4 controller init + training. The big one.
5. `src/psci/psci.c` — wire the real PMU core-on sequence (M3 + A55 clusters).
6. `include/boot.h` — lock the hand-off ABI with your stage 2.

Each file has a header comment describing exactly what "done" looks like.
