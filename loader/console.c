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

/*
 * Time. On the S9, S-Boot leaves CNTFRQ_EL0 at 0 (scion-kernel's sboot reads the 26 MHz counter for
 * that reason, xnuloader/sboot/menu.c); the first loader on the phone took that as "no clock", its
 * countdown never moved and the first key press waited forever. So: the board's known frequency when
 * CNTFRQ is 0 or not believable, and if the counter itself stands still (never seen, but nothing
 * else would bound the waits) a clock counted in calls instead, so that every wait still ends.
 */
#define LDR_HZ_MIN       1000000ull         /* below 1 MHz or above 1 GHz: not a real counter */
#define LDR_HZ_MAX       1000000000ull
#define LDR_STUCK_READS  (1u << 22)          /* the same count this many reads in a row: stopped */
#define LDR_CALLS_PER_MS 2000u               /* the counted clock: rough, only bounds the waits */

static uint64_t hz_cached;
static int clock_soft;
static uint64_t last_ticks, soft_calls, soft_base_ms;
static uint32_t same_reads;

uint64_t ldr_hz(void)
{
	if (!hz_cached) {
		uint64_t f = plat_freq();
		hz_cached = (f >= LDR_HZ_MIN && f <= LDR_HZ_MAX) ? f : plat_freq_default();
		if (!hz_cached)
			hz_cached = 24000000ull;
	}
	return hz_cached;
}

int ldr_clock_state(void)
{
	if (clock_soft)
		return 2;
	uint64_t f = plat_freq();
	return (f >= LDR_HZ_MIN && f <= LDR_HZ_MAX) ? 0 : 1;
}

uint64_t now_ms(void)
{
	if (clock_soft)
		return soft_base_ms + ++soft_calls / LDR_CALLS_PER_MS;
	uint64_t t = plat_ticks();
	uint64_t ms = t / (ldr_hz() / 1000);
	if (t != last_ticks) {
		last_ticks = t;
		same_reads = 0;
	} else if (++same_reads >= LDR_STUCK_READS) {
		clock_soft = 1;
		soft_base_ms = ms;
	}
	return ms;
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
