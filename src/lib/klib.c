/*
 * Freestanding string, number parsing, and formatted output routines.
 * Supports %s %c %d %u %x %X %p %% with optional 0-padding, width, and l/ll/z
 * length modifiers. No libc dependencies.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "lib.h"

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a < b || a >= b + n) {
		while (n--)
			*a++ = *b++;
	} else if (a > b) {
		a += n;
		b += n;
		while (n--)
			*--a = *--b;
	}
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = a;
	const unsigned char *q = b;
	while (n--) {
		if (*p != *q)
			return (int)*p - (int)*q;
		p++;
		q++;
	}
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
	while (*a && (*a == *b)) {
		a++;
		b++;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && (*a == *b)) {
		a++;
		b++;
		n--;
	}
	if (n == 0)
		return 0;
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int parse_u64(const char *s, uint64_t *out)
{
	if (!s || !*s)
		return -1;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!*s)
		return -1;

	unsigned base = 10;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
		if (!*s)
			return -1;
	}

	uint64_t val = 0;
	while (*s) {
		char c = *s++;
		unsigned digit;
		if (c >= '0' && c <= '9')
			digit = (unsigned)(c - '0');
		else if (base == 16 && c >= 'a' && c <= 'f')
			digit = (unsigned)(c - 'a' + 10);
		else if (base == 16 && c >= 'A' && c <= 'F')
			digit = (unsigned)(c - 'A' + 10);
		else
			return -1;
		if (digit >= base)
			return -1;
		val = val * base + digit;
	}
	*out = val;
	return 0;
}

void kputs(const char *s)
{
	if (!s)
		s = "(null)";
	while (*s)
		uart_putc(*s++);
}

static void put_uint_fmt(uint64_t v, unsigned base, int width, char pad, int upper)
{
	char buf[32];
	int i = 0;
	const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	if (v == 0) {
		buf[i++] = '0';
	} else {
		while (v) {
			buf[i++] = d[v % base];
			v /= base;
		}
	}
	for (int p = i; p < width; p++)
		uart_putc(pad);
	while (i--)
		uart_putc(buf[i]);
}

void kprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			uart_putc(*fmt);
			continue;
		}
		fmt++;

		char pad = ' ';
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}

		int width = 0;
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}

		int is_long = 0;
		if (*fmt == 'l') {
			is_long = 1;
			fmt++;
			if (*fmt == 'l') {
				is_long = 2;
				fmt++;
			}
		} else if (*fmt == 'z') {
			is_long = 1;
			fmt++;
		}

		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			int len = (int)strlen(s);
			while (len < width) {
				uart_putc(' ');
				len++;
			}
			kputs(s);
			break;
		}
		case 'c':
			uart_putc((char)va_arg(ap, int));
			break;
		case 'd':
		case 'i': {
			int64_t sv = is_long ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
			uint64_t uv;
			if (sv < 0) {
				uart_putc('-');
				uv = (uint64_t)(-sv);
				if (width > 0)
					width--;
			} else {
				uv = (uint64_t)sv;
			}
			put_uint_fmt(uv, 10, width, pad, 0);
			break;
		}
		case 'u': {
			uint64_t uv = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned);
			put_uint_fmt(uv, 10, width, pad, 0);
			break;
		}
		case 'x': {
			uint64_t uv = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned);
			put_uint_fmt(uv, 16, width, pad, 0);
			break;
		}
		case 'X': {
			uint64_t uv = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned);
			put_uint_fmt(uv, 16, width, pad, 1);
			break;
		}
		case 'p':
			kputs("0x");
			put_uint_fmt((uint64_t)(uintptr_t)va_arg(ap, void *), 16, width, pad, 0);
			break;
		case '%':
			uart_putc('%');
			break;
		case '\0':
			goto out;
		default:
			uart_putc('%');
			uart_putc(*fmt);
			break;
		}
	}
out:
	va_end(ap);
}
