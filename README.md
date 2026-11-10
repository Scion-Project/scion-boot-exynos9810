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

## Scope / boundary & brick-proof recovery

This repo is bootloader engineering only. See [`docs/boundary.md`](docs/boundary.md) and [`docs/recovery-and-safety.md`](docs/recovery-and-safety.md).
It contains **no exploit, no secure-boot bypass, no signature-check patch, and no
key/fuse/keybox manipulation**.

To ensure the bootloader **can never damage or brick the phone**, Stage 1 enforces:
- **Zero non-volatile writes** (OTP/eFuses `0x10000000`, UFS controller `0x11120000`, IROM, and the BootROM IRAM table `0x02020000..0x02021400` are strictly write-protected).
- **Hardware button override** (`Volume Down + Bixby` on `GPA0` immediately enters Exynos USB Download Mode `04e8:1234`; `Volume Up` forces the recovery shell).
- **Automatic 3-strike boot-loop guard** in `PMU_INFORM1` (`0x14060804`) paired with a fail-safe watchdog timer.
- **Automatic `usbdl` (`04e8:1234` DNW) fallback** on any fatal exception, DRAM init error, or Stage-2 verification failure, compatible with [halal-beef/hubble](https://github.com/halal-beef/hubble) and [halal-beef/houston-pub](https://github.com/halal-beef/houston-pub).

## Layout

```
src/start.S          EL3 reset entry, 2048B-aligned exception vectors, EL3 -> EL2 drop
src/exceptions.c     VBAR setup, ESR_ELx EC decoding & full register crash dump
src/main.c           stage-1 flow: console → WDT/timer → clocks → DRAM → display → handoff
src/loader.c         stage-2 header ('SCN2') validation, CRC32 & SHA-256 verification
src/xmodem.c         UART XMODEM-CRC receiver for loading payloads into RAM without flashing
src/shell.c          interactive UART debug console (md/mw, fbtest, muic, loadx, usbdl, etc.)
src/platform/        exynos9810.h + uart, clock, dram, display (DECON0/font), i2c/MUIC, timer, wdt, safety
src/lib/             freestanding string/printf (klib.c) + IEEE CRC32 & FIPS SHA-256 (crypto.c)
src/psci/psci.c      PSCI/CPU_ON & Exynos 9810 PMU core power sequencing + spin-table
include/boot.h       the stage-1 → stage-2 hand-off struct (ABI) & stage-2 image header
linker/stage1.ld     bare-metal layout with stage-1 footprint symbols
tools/mkstage2.py    host tool to wrap a stage-2 binary with a 'SCN2' CRC32+SHA-256 header
```

## Build

```sh
# QEMU smoke test (EL3 -> EL2 transition, DRAM test, SHA-256 verification)
make PLATFORM=qemu
make PLATFORM=qemu qemu      # needs qemu-system-aarch64

# Force entry into the interactive UART debug console on boot
make clean && make PLATFORM=qemu BOOT_SHELL=1 qemu

# Real hardware target (Exynos 9810 / starlte)
make PLATFORM=starlte
```

Toolchain: auto-detects `aarch64-none-elf-gcc`, `aarch64-linux-gnu-gcc`, or LLVM `clang --target=aarch64-none-elf` + `lld`.

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

## Credits & Acknowledgements

- **Umer Uddin ([@halal-beef](https://github.com/halal-beef))** — Original creator of [Hubble](https://github.com/halal-beef/hubble) and [Houston](https://github.com/halal-beef/houston-pub) (`<umer.uddin@mentallysanemainliners.org>`), whose Exynos BootROM USB Download Mode (`usbdl` / `04e8:1234` DNW) recovery architecture makes unbricking Exynos devices possible.
- **[@Robotix22](https://github.com/Robotix22)** — Creator of [open-mini-bl1](https://github.com/Robotix22/open-mini-bl1) and contributor of Exynos 9810 (`starlte`) BootROM/IRAM recovery support in Hubble and Houston.
- **[@VDavid003](https://github.com/VDavid003)** — Creator of [exynos-usbdl](https://github.com/VDavid003/exynos-usbdl) and contributor to Exynos BootROM recovery research.
- **Markuss Broks & Ivaylo Ivanov ([@ivoszbg](https://github.com/ivoszbg))** — [uniLoader](https://github.com/ivoszbg/uniLoader) Exynos 9810 (`starlte`) DECON0 hardware/software trigger (`0x16030070`) and `0xCC000000` framebuffer bring-up.
