/*
 * Exynos 9810 (starlte) platform constants.
 *
 * Addresses that are CONFIRMED from the public vendor device tree are given as
 * real values. Addresses that still need to be verified from the SoC datasheet
 * / vendor DT carry a TODO(starlte) and a deliberately obvious placeholder so a
 * wrong value can never masquerade as a confirmed one.
 *
 * Under PLATFORM=qemu these are overridden with QEMU 'virt' values so the
 * skeleton runs with no hardware.
 */
#ifndef EXYNOS9810_H
#define EXYNOS9810_H

#if defined(PLATFORM_QEMU)

#define PLATFORM_NAME   "qemu-virt (smoke test)"
#define UART_BASE       0x09000000UL   /* QEMU virt PL011 */
#define UART_KIND_PL011 1

#elif defined(PLATFORM_STARLTE)

#define PLATFORM_NAME   "starlte (Exynos 9810)"
#define UART_KIND_EXYNOS 1

/* CONFIRMED from vendor DT: pmu_system_controller: system-controller@14060000 */
#define PMU_BASE        0x14060000UL
#define PMU_INFORM0     (PMU_BASE + 0x0800) /* confirmed: S5P_INFORM0 */
#define PMU_INFORM3     (PMU_BASE + 0x080C) /* INFERRED from INFORM0/1/5 stride;
                                             * verify against exynos-regs-pmu.h */

/*
 * CONFIRMED from vendor DT (exynos9810-crownlte, same SoC as starlte):
 *   ecduart = uart0 = uart@10440000, compatible "samsung,exynos-uart".
 * This is the early-console debug UART — the one muxed out over USB-C UART.
 * reg = <0x10440000 0x100>, fifo-size 0x100, IRQ SPI 0x18d.
 *
 * NOTE: it is a USI-wrapped UART (samsung,usi-serial-v2). The USI block must
 * be in UART mode for the port to work. The stock bootloader already puts it
 * there, so the poll-TX-and-write path in uart.c works as-is on a warm handoff.
 * If you ever cold-init from the USI block, you must set USI mode first — the
 * USI config register is adjacent; pull its offset from the usi node before
 * touching it (left out here rather than guessed).
 */
#define UART_BASE       0x10440000UL   /* confirmed: uart0 / ecduart */

/* MUIC (for forcing the USB-C UART path if the handoff didn't leave it set):
 * CONFIRMED: max77705 @ i2c 0x66 on hsi2c@14360000. */
#define MUIC_I2C_BASE   0x14360000UL   /* confirmed: hsi2c@14360000 */
#define MUIC_I2C_ADDR   0x66

/*
 * TODO(starlte): CMU (clock management unit) bases for the DRAM path. The
 * console UART comes up on the clock the stock bootloader already left running
 * (gate_uart0), so you only need the CMU for DRAM/other peripherals. From the
 * vendor DT 'clock-controller@...' nodes.
 */
#define CMU_TOP_BASE    0xDEAD0001UL   /* PLACEHOLDER (DRAM path only) */

/*
 * TODO(starlte): DREX / DMC (DRAM controller) bases + PHY. The big one.
 * LPDDR4, 4 GB on this board (from sec_debug.lpddr4_size=4).
 */
#define DMC0_BASE       0xDEAD0003UL   /* PLACEHOLDER */
#define DMC_PHY0_BASE   0xDEAD0004UL   /* PLACEHOLDER */

#else
#error "Define PLATFORM_QEMU or PLATFORM_STARLTE"
#endif

/* A placeholder is any 0xDEADxxxx constant: fail fast if one is used live. */
#define IS_PLACEHOLDER(x) (((x) & 0xFFFF0000UL) == 0xDEAD0000UL)

#endif /* EXYNOS9810_H */
