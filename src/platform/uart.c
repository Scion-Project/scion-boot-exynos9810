/*
 * UART console.
 *
 * Two backends, picked at compile time:
 *   - PL011   (QEMU virt) — complete, used by the smoke test
 *   - Exynos  (starlte)   — sketched; confirm divisor/line-control on hardware
 *
 * The rest of the loader only calls uart_init() and uart_putc().
 */
#include <stdint.h>
#include "exynos9810.h"

static uint64_t s_base;

static inline void mmio_w32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint32_t mmio_r32(uint64_t a) { return *(volatile uint32_t *)a; }

#ifdef UART_KIND_PL011
/* PL011 registers */
#define PL011_DR    0x00
#define PL011_FR    0x18
#define PL011_FR_TXFF (1u << 5)
#define PL011_IBRD  0x24
#define PL011_FBRD  0x28
#define PL011_LCRH  0x2C
#define PL011_CR    0x30

void uart_init(uint64_t base)
{
	s_base = base;
	/* QEMU models the divisors as no-ops; just enable TX + UART. */
	mmio_w32(s_base + PL011_CR, 0);
	mmio_w32(s_base + PL011_LCRH, (3u << 5));           /* 8n1, FIFO off */
	mmio_w32(s_base + PL011_CR, (1u << 0) | (1u << 8)); /* UARTEN | TXE */
}

void uart_putc(char c)
{
	while (mmio_r32(s_base + PL011_FR) & PL011_FR_TXFF)
		;
	mmio_w32(s_base + PL011_DR, (uint32_t)(uint8_t)c);
}
#endif /* PL011 */

#ifdef UART_KIND_EXYNOS
/*
 * Samsung/Exynos UART. Register offsets are the stable s5pv210-family layout.
 * TODO(starlte): the controller should already be clocked and configured by
 * the time we run (the primary bootloader used it), so for a first cut we only
 * poll TX-ready and write. If you re-init from cold, set ULCON/UCON/UBRDIV for
 * your console baud and the PCLK the CMU gives the port.
 */
#define S3C_UTRSTAT  0x10
#define S3C_UTXH     0x20
#define S3C_UTRSTAT_TXE (1u << 2)   /* transmitter empty */

void uart_init(uint64_t base)
{
	s_base = base;
	/* TODO(starlte): if cold-initialising:
	 *   mmio_w32(base + 0x00, 0x3);           // ULCON: 8n1
	 *   mmio_w32(base + 0x04, 0x5);           // UCON: tx/rx polling
	 *   set UBRDIV/UFRACVAL from PCLK + baud
	 * Left out until the CMU path is known, so we don't write guessed values.
	 */
}

void uart_putc(char c)
{
	while (!(mmio_r32(s_base + S3C_UTRSTAT) & S3C_UTRSTAT_TXE))
		;
	mmio_w32(s_base + S3C_UTXH, (uint32_t)(uint8_t)c);
}
#endif /* Exynos */
