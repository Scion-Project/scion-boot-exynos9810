/*
 * UART console driver (TX + RX).
 *
 * Two backends, selected at compile time:
 *   - PL011   (QEMU virt)
 *   - Exynos  (starlte USI-UART @ 0x10440000)
 */
#include <stdint.h>
#include "exynos9810.h"
#include "lib.h"

static uint64_t s_base;

#ifdef UART_KIND_PL011
/* PrimeCell PL011 register offsets */
#define PL011_DR      0x00
#define PL011_FR      0x18
#define PL011_FR_BUSY (1u << 3)
#define PL011_FR_RXFE (1u << 4)
#define PL011_FR_TXFF (1u << 5)
#define PL011_IBRD    0x24
#define PL011_FBRD    0x28
#define PL011_LCRH    0x2C
#define PL011_CR      0x30
#define PL011_ICR     0x44

void uart_init(uint64_t base)
{
	s_base = base;
	/* Enable UART + TXE + RXE without flushing any queued RX byte in QEMU */
	mmio_w32(s_base + PL011_ICR, 0x7FFu);
	mmio_w32(s_base + PL011_LCRH, (3u << 5) | (1u << 4)); /* 8N1, FIFO enabled */
	mmio_w32(s_base + PL011_CR, (1u << 0) | (1u << 8) | (1u << 9));
}

static void raw_putc(char c)
{
	while (mmio_r32(s_base + PL011_FR) & PL011_FR_TXFF)
		;
	mmio_w32(s_base + PL011_DR, (uint32_t)(uint8_t)c);
}

void uart_putc(char c)
{
	if (c == '\n')
		raw_putc('\r');
	raw_putc(c);
}

int uart_tstc(void)
{
	return !(mmio_r32(s_base + PL011_FR) & PL011_FR_RXFE);
}

int uart_getc(void)
{
	while (!uart_tstc())
		;
	return (int)(mmio_r32(s_base + PL011_DR) & 0xFFu);
}

void uart_flush(void)
{
	while (mmio_r32(s_base + PL011_FR) & PL011_FR_BUSY)
		;
}
#endif /* UART_KIND_PL011 */

#ifdef UART_KIND_EXYNOS
/*
 * Samsung Exynos USI-v2 UART register offsets (s5pv210 / exynos-uart layout).
 */
#define S3C_ULCON         0x00  /* Line Control */
#define S3C_UCON          0x04  /* Control */
#define S3C_UFCON         0x08  /* FIFO Control */
#define S3C_UMCON         0x0C  /* Modem Control */
#define S3C_UTRSTAT       0x10  /* Tx/Rx Status */
#define S3C_UERSTAT       0x14  /* Rx Error Status */
#define S3C_UFSTAT        0x18  /* FIFO Status */
#define S3C_UMSTAT        0x1C  /* Modem Status */
#define S3C_UTXH          0x20  /* Transmit Buffer */
#define S3C_URXH          0x24  /* Receive Buffer */
#define S3C_UBRDIV        0x28  /* Baud Rate Divisor */
#define S3C_UFRACVAL      0x2C  /* Divisor Fractional Value */
#define S3C_UINTP         0x30  /* Interrupt Pending */
#define S3C_UINTS         0x34  /* Interrupt Source */
#define S3C_UINTM         0x38  /* Interrupt Mask */
#define EXYNOS_USI_CON    0xC4  /* USI Configuration (bit 0: SW_CONF reset) */
#define EXYNOS_USI_OPTION 0xC8  /* USI Clock/Stop Option */

/* UTRSTAT bits */
#define S3C_UTRSTAT_RXDR  (1u << 0)  /* Receive buffer data ready */
#define S3C_UTRSTAT_TXFE  (1u << 1)  /* Transmit buffer empty */
#define S3C_UTRSTAT_TXE   (1u << 2)  /* Transmitter empty */

/* UFSTAT bits */
#define S3C_UFSTAT_RX_CNT_MASK 0xFFu
#define S3C_UFSTAT_RX_FULL     (1u << 8)
#define S3C_UFSTAT_TX_FULL     (1u << 24)

void uart_init(uint64_t base)
{
	s_base = base;
	/*
	 * On Exynos 9810, S-Boot has already configured USI0 into UART mode
	 * and set UBRDIV/UFRACVAL for 115200 8N1. Mask all UART interrupts so
	 * polled TX/RX is deterministic, and clear any sticky error flags.
	 */
	mmio_w32(s_base + S3C_UINTM, 0xFu);
	mmio_w32(s_base + S3C_UINTP, 0xFu);
	(void)mmio_r32(s_base + S3C_UERSTAT);
}

static void raw_putc(char c)
{
	/* If FIFO mode is enabled in UFCON (bit 0), check UFSTAT TX_FULL;
	 * otherwise check UTRSTAT TXE. */
	if (mmio_r32(s_base + S3C_UFCON) & 1u) {
		while (mmio_r32(s_base + S3C_UFSTAT) & S3C_UFSTAT_TX_FULL)
			;
	} else {
		while (!(mmio_r32(s_base + S3C_UTRSTAT) & S3C_UTRSTAT_TXE))
			;
	}
	mmio_w32(s_base + S3C_UTXH, (uint32_t)(uint8_t)c);
}

void uart_putc(char c)
{
	if (c == '\n')
		raw_putc('\r');
	raw_putc(c);
}

int uart_tstc(void)
{
	if (mmio_r32(s_base + S3C_UFCON) & 1u) {
		uint32_t ufstat = mmio_r32(s_base + S3C_UFSTAT);
		return ((ufstat & S3C_UFSTAT_RX_CNT_MASK) != 0) ||
		       ((ufstat & S3C_UFSTAT_RX_FULL) != 0);
	}
	return (mmio_r32(s_base + S3C_UTRSTAT) & S3C_UTRSTAT_RXDR) != 0;
}

int uart_getc(void)
{
	while (!uart_tstc())
		;
	(void)mmio_r32(s_base + S3C_UERSTAT);
	return (int)(mmio_r32(s_base + S3C_URXH) & 0xFFu);
}

void uart_flush(void)
{
	while (!(mmio_r32(s_base + S3C_UTRSTAT) & S3C_UTRSTAT_TXE))
		;
}
#endif /* UART_KIND_EXYNOS */
