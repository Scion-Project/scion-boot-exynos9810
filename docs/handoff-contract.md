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
