# Stage-1 → Stage-2 hand-off contract

This is the ABI the first stage guarantees when it jumps to the second-stage
loader. Lock it early; everything on both sides depends on it.

## Entry state

| Thing            | Guarantee at stage-2 entry                                        |
|------------------|-------------------------------------------------------------------|
| Exception level  | EL2 (so stage 2 can choose to run the OS at EL2 or drop to EL1)    |
| `x0`             | physical pointer to `struct scion_boot_info` (see `include/boot.h`)|
| `x1`–`x3`        | 0 (reserved)                                                      |
| MMU              | **off**                                                           |
| Caches           | D-cache off and clean/invalidated; I-cache may be on              |
| Interrupts       | masked (DAIF=0xF)                                                 |
| Stack            | valid SP in reserved SRAM/DRAM, 16-byte aligned                   |
| DRAM             | fully initialised, trained, and described in `boot_info.mem`      |
| Console UART     | initialised; `boot_info.uart_base` names it                      |
| Secondary cores  | parked in the spin-table at `boot_info.cpu_release_addr`          |

## `struct scion_boot_info`

Versioned (`abi_version`) so the two stages can evolve independently. The first
stage fills it; the second stage reads it and must reject an `abi_version` it
does not understand. Field layout lives in [`include/boot.h`](../include/boot.h)
— that file is the single source of truth; this doc is the prose.

## Why EL2, MMU off

Matches what both targets want handed to them:

- **Linux/mainline (arm64):** kernel expects to be entered with the MMU off,
  D-cache off, at EL2 if available, `x0` = physical DTB. Stage 2 builds the DTB
  (or reuses the vendor one) and re-enters per `Documentation/arm64/booting.rst`.
- **XNU (Scion):** expects `x0` = physical boot-args pointer and an identity or
  MMU-off environment; stage 2 translates `scion_boot_info` into the boot-args
  struct for the target XNU version and the carveout map.

Keeping stage 1 OS-agnostic (it only guarantees machine state + `boot_info`)
is what lets the same first stage feed either OS. All OS-specific knowledge
lives in stage 2.

## Checklist for "stage 1 is done"

1. Console prints over the real UART on hardware.
2. DRAM passes a walking-1s/0s test across the whole bank.
3. Secondary cores come up via PSCI `CPU_ON` and park in the spin-table.
4. `scion_boot_info` validates (magic, version, non-zero mem map).
5. Control reaches a stage-2 stub that prints `x0->magic` back out the UART.

## The second-stage loader (`loader/`)

The loader that S-Boot starts from BOOT (docs/STATUS.md) has two contracts of its own; both are
defined in [`include/boot.h`](../include/boot.h).

### Starting an OS

| Thing           | XNU entry (`kind xnu`)                              | Image entry (`kind image`)            |
|-----------------|-----------------------------------------------------|---------------------------------------|
| Exception level | EL1 (S-Boot's)                                      | EL1                                   |
| `x0`            | `boot_args` (Revision 2, Version 2), as sboot does  | S-Boot's FDT (cold) or `fdt_pa` (re-entry), else 0 |
| `x1`-`x3`       | 0                                                   | 0                                     |
| MMU, caches     | off; whole D-cache cleaned + invalidated by set/way; I-cache and TLB invalidated | same |
| Interrupts      | masked; the loader's own watchdog stopped           | same                                  |
| Display         | S-Boot's framebuffer, DECON trigger free-running    | same                                  |
| Integrity       | CRC32 + SHA-256 of the record checked (and pinned digests, if built with them) | same |

The command line is the payload's own, as edited in the menu, plus `scion_ldr=<base>,<size>` and
`scion_guard=<pa>` when there is room.

### Re-entering the loader

Base + 0x40 is the re-entry point; +0x44 holds `"SCNR"` and +0x48 the record version (1), which
the OS checks before it jumps. The OS side is scion-kernel's warm restart
(`docs/ios27/WARM-RESTART.md` there); the two sides share `struct scion_reentry` (728 bytes).

| Thing     | Guarantee at re-entry |
|-----------|-----------------------|
| EL, MMU   | EL1, MMU and D-cache off, everything the loader reads cleaned to PoC, I-cache invalidated |
| CPUs, IRQ | the boot CPU only (others PSCI `CPU_OFF`), DAIF masked, its GIC CPU interface off |
| `x0`      | the record (8-byte aligned, outside the kernel slot and the OS RAM the loader will fill); `x1`-`x3` 0 |
| RAM       | BOOT's kernel slot put back where S-Boot had it (`SCION_RE_RESTAGED`): XNU reuses that RAM, so the OS reads it again from the BOOT partition |
| Watchdog  | if `SCION_RE_WDT_ARMED`: the loader pets the cluster-1 WDT while it runs and stops it before the next OS, unless `SCION_RE_WDT_KEEP` |
| FDT       | `fdt_pa`, normally the copy a cold boot kept at `SCION_FDT_SAVE_*` + 0x1000 (lines with `sweetwarm=`) |

The loader trusts nothing it finds in RAM: it validates the record (magic, version, size, CRC32,
terminated line) and checks the SHA-256 of whatever entry it starts. An invalid record means a cold
start without an FDT. A re-entry for a panic or a watchdog counts as a boot-loop strike and is not
started directly once the guard has tripped.
