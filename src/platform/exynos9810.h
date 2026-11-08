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
 * TODO(starlte): set the console UART base. Samsung "exynos4210"-compatible
 * serial. Pull the exact base for the console port from the vendor DT
 * (chosen/stdout-path -> serial_N -> reg). Do NOT guess.
 */
#define UART_BASE       0xDEAD0000UL   /* PLACEHOLDER — replace from DT */

/*
 * TODO(starlte): CMU (clock management unit) bases for the console UART path
 * and the DRAM path. From the vendor DT 'clock-controller@...' nodes.
 */
#define CMU_TOP_BASE    0xDEAD0001UL   /* PLACEHOLDER */
#define CMU_PERIC_BASE  0xDEAD0002UL   /* PLACEHOLDER */

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
