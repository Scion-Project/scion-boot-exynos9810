/*
 * XMODEM-CRC (128-byte packet) serial receiver over UART.
 *
 * Allows uploading and testing a Stage-2 binary ('SCN2' image or raw payload)
 * directly into volatile RAM over the USB-C UART debug cable (via `loadx`)
 * without flashing or modifying non-volatile storage.
 */
#include <stdint.h>
#include <stddef.h>
#include "lib.h"

#define XMODEM_SOH  0x01
#define XMODEM_EOT  0x04
#define XMODEM_ACK  0x06
#define XMODEM_NAK  0x15
#define XMODEM_CAN  0x18
#define XMODEM_CRC  'C'

static int uart_getc_timeout(unsigned timeout_ms)
{
	uint64_t f = timer_get_freq();
	uint64_t wait_ticks = (f * timeout_ms) / 1000ULL;
	uint64_t start = timer_get_ticks();

	while ((timer_get_ticks() - start) < wait_ticks) {
		if (uart_tstc())
			return uart_getc();
	}
	return -1;
}

static uint16_t crc16_ccitt(const uint8_t *buf, size_t len)
{
	uint16_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)buf[i] << 8;
		for (int b = 0; b < 8; b++) {
			if (crc & 0x8000u)
				crc = (uint16_t)((crc << 1) ^ 0x1021u);
			else
				crc <<= 1;
		}
	}
	return crc;
}

long xmodem_receive(void *dst, size_t max_len)
{
	uint64_t dst_pa = (uint64_t)(uintptr_t)dst;
	if (!dst || max_len < 128 || platform_addr_is_write_protected(dst_pa, max_len))
		return -1;

	uint8_t *out = dst;
	size_t   total = 0;
	uint8_t  expected_seq = 1;
	uint8_t  pkt[128];
	int      started = 0;
	unsigned retries = 0;

	while (retries < 20) {
		platform_wdt_kick();
		if (!started)
			uart_putc(XMODEM_CRC);

		int hdr = uart_getc_timeout(1000);
		if (hdr < 0) {
			retries++;
			continue;
		}

		if (hdr == XMODEM_EOT) {
			uart_putc(XMODEM_ACK);
			return (long)total;
		}
		if (hdr == XMODEM_CAN) {
			uart_putc(XMODEM_ACK);
			return -2;
		}
		if (hdr != XMODEM_SOH) {
			retries++;
			continue;
		}

		started = 1;
		int seq     = uart_getc_timeout(500);
		int seq_inv = uart_getc_timeout(500);
		if (seq < 0 || seq_inv < 0) {
			uart_putc(XMODEM_NAK);
			retries++;
			continue;
		}

		int ok = 1;
		for (unsigned i = 0; i < 128; i++) {
			int b = uart_getc_timeout(500);
			if (b < 0) {
				ok = 0;
				break;
			}
			pkt[i] = (uint8_t)b;
		}
		int crc_hi = ok ? uart_getc_timeout(500) : -1;
		int crc_lo = (crc_hi >= 0) ? uart_getc_timeout(500) : -1;

		if (!ok || crc_hi < 0 || crc_lo < 0) {
			uart_putc(XMODEM_NAK);
			retries++;
			continue;
		}

		uint16_t rx_crc = ((uint16_t)crc_hi << 8) | (uint16_t)crc_lo;
		if (((uint8_t)seq + (uint8_t)seq_inv) != 0xFFu ||
		    crc16_ccitt(pkt, 128) != rx_crc) {
			uart_putc(XMODEM_NAK);
			retries++;
			continue;
		}

		if ((uint8_t)seq == expected_seq) {
			if (total + 128 > max_len) {
				uart_putc(XMODEM_CAN);
				return -3;
			}
			memcpy(out + total, pkt, 128);
			total += 128;
			expected_seq++;
			retries = 0;
			uart_putc(XMODEM_ACK);
		} else if ((uint8_t)seq == (uint8_t)(expected_seq - 1)) {
			/* Duplicate packet; re-ACK */
			uart_putc(XMODEM_ACK);
		} else {
			uart_putc(XMODEM_CAN);
			return -4;
		}
	}
	return -5;
}
