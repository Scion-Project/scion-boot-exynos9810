/* SPDX-License-Identifier: MIT
 * What the test payloads report (fake_start.S): for fakexnu, the boot_args the loader built; for
 * chain, the FDT it was given. Everything is checked here and printed as one PASS/FAIL line.
 */
#include <stdint.h>

#define UART 0x09000000ul

#ifndef CHAIN_IMAGE
static unsigned char zeroed[256];   /* in .bss: the loader must zero a segment's tail */
#endif

static void putc_(char c)
{
	while (*(volatile uint32_t *)(UART + 0x18) & (1u << 5))
		;
	*(volatile uint32_t *)UART = (uint8_t)c;
}

static void puts_(const char *s)
{
	while (*s)
		putc_(*s++);
}

static void hex(uint64_t v)
{
	puts_("0x");
	for (int s = 60; s >= 0; s -= 4)
		putc_("0123456789abcdef"[(v >> s) & 15]);
}

#ifdef CHAIN_IMAGE
void fake_main(uint64_t x0)
{
	puts_("CHAIN: started, x0=");
	hex(x0);
	putc_('\n');
	uint32_t m = x0 ? *(volatile uint32_t *)x0 : 0;
	puts_(m == 0xedfe0dd0u ? "CHAIN: PASS fdt\n" : "CHAIN: FAIL no fdt\n");
}
#else
struct ba {
	uint16_t rev, ver;
	uint64_t virt, phys, mem, top;
	uint64_t video[6];
	uint32_t machine;
	uint64_t dt;
	uint32_t dt_len;
	char line[608];
	uint64_t flags, mem_actual;
};

static int starts(const char *p, const char *w)
{
	while (*w && *p == *w) {
		p++;
		w++;
	}
	return !*w;
}

static int has(const char *line, const char *w)
{
	for (const char *p = line; *p; p++)
		if (starts(p, w))
			return 1;
	return 0;
}

static uint64_t parse_hex(const char *p)
{
	uint64_t v = 0;
	if (p[0] == '0' && p[1] == 'x')
		p += 2;
	for (;; p++) {
		unsigned d;
		if (*p >= '0' && *p <= '9') d = (unsigned)(*p - '0');
		else if (*p >= 'a' && *p <= 'f') d = (unsigned)(*p - 'a' + 10);
		else return v;
		v = v * 16 + d;
	}
}

static uint32_t crc32_(const uint8_t *p, unsigned n)
{
	uint32_t c = 0xffffffffu;
	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & -(c & 1u));
	}
	return ~c;
}

/* The warm-restart contract (include/boot.h struct scion_reentry): ask the loader, whose base is
 * on the line as scion_ldr=, to start entry 2 (index 1) with a line of our own. */
struct reentry {
	uint32_t magic;
	uint16_t version, size;
	uint32_t reason, flags, entry, crc;
	char line[608];
	uint32_t method, wdt_ms;
	uint64_t image_pa, image_size, kernel_pa, kernel_size, ramdisk_pa, ramdisk_size, fdt_pa;
	uint8_t image_sha256[32];
};

static void reenter(const char *line)
{
	const char *p = line;
	while (*p && !starts(p, "scion_ldr="))
		p++;
	if (!*p) {
		puts_("FAKEXNU: FAIL no scion_ldr= on the line\n");
		return;
	}
	uint64_t base = parse_hex(p + 10);
	struct reentry *r = (struct reentry *)0x5f000000ul;
	uint8_t *z = (uint8_t *)r;
	for (unsigned i = 0; i < sizeof(*r); i++)
		z[i] = 0;
	r->magic = 0x524e4353u;
	r->version = 1;
	r->size = sizeof(*r);
	r->reason = 3;
	r->flags = 1 | (1u << 4);   /* boot the entry directly; "a watchdog is armed" */
	r->image_pa = base;
	r->fdt_pa = 0x5f801000ul;   /* SCION_FDT_SAVE_QEMU + 0x1000: the FDT the loader kept */
	if (*(volatile uint64_t *)0x5f800000ul != 0x31544446524e4353ull) {
		puts_("FAKEXNU: FAIL no SCNRFDT1 header at the FDT save area\n");
		return;
	}
	/* the mark after the re-entry branch */
	if (*(volatile uint32_t *)(uintptr_t)(base + 0x44) != 0x524e4353u ||
	    *(volatile uint32_t *)(uintptr_t)(base + 0x48) != 1) {
		puts_("FAKEXNU: FAIL no SCNR mark at base+0x44\n");
		return;
	}
	r->flags |= 2u | (1u << 6);   /* append to the entry's line */
	r->line[0] = 'r';
	r->line[1] = 'e';
	r->line[2] = '=';
	r->line[3] = '1';
	r->entry = 1;
	r->crc = crc32_(z, sizeof(*r));
	puts_("FAKEXNU: re-entering the loader at ");
	hex(base + 0x40);
	putc_('\n');
	void (*fn)(uint64_t) = (void (*)(uint64_t))(uintptr_t)(base + 0x40);
	fn((uint64_t)(uintptr_t)r);
}

void fake_main(struct ba *b)
{
	int ok = 1;
	puts_("FAKEXNU: started, boot_args=");
	hex((uint64_t)(uintptr_t)b);
	putc_('\n');
	puts_("FAKEXNU: line=");
	puts_(b->line);
	putc_('\n');
	const char *adt = (const char *)(uintptr_t)(b->dt - b->virt + b->phys);
	puts_("FAKEXNU: adt=");
	for (int i = 0; i < 8; i++)
		putc_(adt[i]);
	putc_('\n');
	if (b->rev != 2 || b->ver != 2) ok = 0;
	if (adt[0] != 'F' || adt[1] != 'A' || adt[2] != 'K' || adt[3] != 'E') ok = 0;
	for (unsigned i = 0; i < sizeof(zeroed); i++)
		if (zeroed[i]) ok = 0;
	if (b->top != (uint64_t)(uintptr_t)b + 4096) ok = 0;
	puts_(ok ? "FAKEXNU: PASS boot_args\n" : "FAKEXNU: FAIL boot_args\n");
	if (has(b->line, "fake_reenter=1"))
		reenter(b->line);
}
#endif
