/* Freestanding string + a tiny printf (%s %c %u %x %p %%). No libc. */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "lib.h"

void *memset(void *d, int c, size_t n)
{ unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }

void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }

void kputs(const char *s) { while (*s) uart_putc(*s++); }

static void put_unsigned(uint64_t v, unsigned base)
{
	char buf[32]; int i = 0;
	const char *d = "0123456789abcdef";
	if (v == 0) { uart_putc('0'); return; }
	while (v) { buf[i++] = d[v % base]; v /= base; }
	while (i--) uart_putc(buf[i]);
}

void kprintf(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	for (; *fmt; fmt++) {
		if (*fmt != '%') { uart_putc(*fmt); continue; }
		switch (*++fmt) {
		case 's': kputs(va_arg(ap, const char *)); break;
		case 'c': uart_putc((char)va_arg(ap, int)); break;
		case 'u': put_unsigned(va_arg(ap, unsigned), 10); break;
		case 'x': put_unsigned(va_arg(ap, unsigned), 16); break;
		case 'p': kputs("0x"); put_unsigned((uint64_t)(uintptr_t)va_arg(ap, void *), 16); break;
		case '%': uart_putc('%'); break;
		default:  uart_putc('%'); uart_putc(*fmt); break;
		}
	}
	va_end(ap);
}
