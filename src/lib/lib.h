#ifndef SCION_LIB_H
#define SCION_LIB_H
#include <stddef.h>
void  uart_putc(char c);
void  kputs(const char *s);
void  kprintf(const char *fmt, ...);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
#endif
