# Scope and boundary

This repository is **bootloader development**: the low-level code that initialises
a machine and hands control to the next stage. That is standard systems
engineering — the same category of work as U-Boot SPL, ARM Trusted Firmware
BL1/BL2, or coreboot — and all of it builds and runs in QEMU with no physical
device involved.

## In scope

- EL3 reset entry, stack/BSS/C-runtime setup
- UART console
- Clock / PMIC / DRAM bring-up for Exynos 9810
- PSCI / secondary-core release
- Laying out and validating the hand-off to a second-stage loader

## Out of scope (not in this tree, by design)

- Any exploit or vulnerability trigger
- Patching or defeating a signature / secure-boot check
- Key, fuse, keybox, or attestation forgery or extraction
- Anything whose purpose is to make a verified boot chain accept code it
  otherwise would not

How a device comes to execute a given first stage is the device owner's own
concern on their own hardware, and nothing in this repo addresses it. This tree
assumes it is already running and does its job from there — which is exactly the
QEMU case.
