/* SPDX-License-Identifier: MIT
 * The loader's console: the board's UART for text (klib's kprintf lands in uart_putc here) and
 * one key reader for both the UART and the phone's buttons.
 */
#include <stdint.h>
#include "loader.h"
#include "plat.h"
#include "lib.h"

void uart_putc(char c)
{
	if (c == '\n')
		plat_uart_putc('\r');
	plat_uart_putc(c);
}

void con_line(const char *s)
{
	kputs(s);
	uart_putc('\n');
}

uint64_t now_ms(void)
{
	uint64_t f = plat_freq();
	return f ? plat_ticks() / (f / 1000) : 0;
}

void delay_ms(uint32_t ms)
{
	uint64_t end = now_ms() + ms;
	while (now_ms() < end)
		;
}

void hex_digest(char *out, const uint8_t *d, unsigned bytes)
{
	static const char hx[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
	                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	for (unsigned i = 0; i < bytes; i++) {
		out[2 * i] = hx[d[i] >> 4];
		out[2 * i + 1] = hx[d[i] & 15];
	}
	out[2 * bytes] = 0;
}

/* UART: wait up to 20 ms for the next byte of an escape sequence. */
static int uart_next(void)
{
	uint64_t end = now_ms() + 20;
	int c;
	while ((c = plat_uart_getc()) < 0 && now_ms() < end)
		;
	return c;
}

static unsigned uart_key(void)
{
	int c = plat_uart_getc();
	if (c < 0)
		return MK_NONE;
	switch (c) {
	case 'k': case 'w': return MK_UP;
	case 'j': case 's': return MK_DOWN;
	case '\r': case '\n': case ' ': return MK_SELECT;
	case 'e': return MK_EDIT;
	case 'b': return MK_BACK;
	case 0x1b:
		if (uart_next() == '[') {
			int d = uart_next();
			if (d == 'A') return MK_UP;
			if (d == 'B') return MK_DOWN;
			return MK_NONE;
		}
		return MK_BACK;
	default:
		if (c >= '1' && c <= '9')
			return MK_DIGIT0 + (unsigned)(c - '0');
		if (c == 'H' || c == 'X')
			return 0x200u + (unsigned)c;     /* test hooks: ui.c passes them on */
		return MK_NONE;
	}
}

/* Buttons: one event per press, after 30 ms stable, released before the next. */
static unsigned held;

static unsigned button_key(void)
{
	unsigned k = plat_keys();
	if (!k) {
		held = 0;
		return MK_NONE;
	}
	if (held)
		return MK_NONE;
	delay_ms(30);
	if (plat_keys() != k)
		return MK_NONE;
	held = k;
	if (k & PK_UP) return MK_UP;
	if (k & PK_DOWN) return MK_DOWN;
	if (k & PK_SELECT) return MK_SELECT;
	if (k & PK_EDIT) return MK_EDIT;
	return MK_NONE;
}

static int last_uart;

int ldr_os_wdt;   /* main.c: set while a watchdog left by the previous OS runs */

unsigned key_poll(void)
{
	plat_wdt_kick();
	if (ldr_os_wdt)
		plat_os_wdt_kick();
	unsigned k = uart_key();
	if (k != MK_NONE) {
		last_uart = 1;
		return k;
	}
	k = button_key();
	if (k != MK_NONE)
		last_uart = 0;
	return k;
}

int key_from_uart(void)
{
	return last_uart;
}

/* Keys already held when the loader starts (Volume Up = menu without countdown) do not count
 * as presses until released. */
void key_init(void)
{
	held = plat_keys();
}
