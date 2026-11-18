/* SPDX-License-Identifier: MIT
 * Host test of the loader's plain-C parts (tools/host-test.sh): CRC32 and SHA-256
 * (src/lib/crypto.c), SCN2 records and the re-entry/guard records (src/lib/verify.c), the guard,
 * command-line and menu decisions (loader/policy.c) and the SWEETXNU address plan (loader/xnu.c).
 *
 *   codecs-test            run every check; exit 1 on the first failure
 *   codecs-test sha N      print SHA-256 of the N-byte test pattern (host-test.sh compares with
 *                          python's hashlib for N = 0..300 and 1000000)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "verify.h"
#include "policy.h"
#include "xnu.h"

uint32_t crc32(const void *data, size_t len);
void sha256(const void *data, size_t len, uint8_t out[32]);
int crypto_selftest(void);

/* klib.c is not linked: crypto.c's lib.h only declares. */
void kprintf(const char *fmt, ...) { (void)fmt; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t pattern(size_t i) { return (uint8_t)(i * 7 + (i >> 8)); }

/* ---- SCN2 --------------------------------------------------------------------------------- */

static uint8_t *make_record(uint32_t kind, uint32_t flags, size_t n, uint64_t *avail)
{
	size_t total = 4096 + n;
	uint8_t *buf = calloc(1, total + 64);
	struct scion_stage2_hdr *h = (void *)buf;
	uint8_t *p = buf + 4096;
	for (size_t i = 0; i < n; i++)
		p[i] = pattern(i);
	h->magic = SCION_STAGE2_MAGIC;
	h->version = SCION_STAGE2_VERSION;
	h->header_size = 4096;
	h->flags = flags | (kind << SCION_S2_KIND_SHIFT);
	h->image_size = n;
	h->crc32 = crc32(p, n);
	sha256(p, n, h->sha256);
	strcpy(h->name, "test");
	*avail = total;
	return buf;
}

static void test_scn2(void)
{
	uint64_t avail;
	uint8_t d[32];
	uint8_t *b = make_record(SCION_S2_KIND_XNU, 0, 10000, &avail);
	struct scion_stage2_hdr *h = (void *)b;
	CHECK(scn2_check_header(h, avail) == SCN2_OK);
	CHECK(scn2_verify(h, avail, NULL, d) == SCN2_OK);
	CHECK(memcmp(d, h->sha256, 32) == 0);
	CHECK(scn2_record_span(h) == 4096 + 12288);

	/* sizes */
	CHECK(scn2_check_header(h, avail - 1) == SCN2_E_BOUNDS);
	CHECK(scn2_check_header(h, 50) == SCN2_E_MAGIC);
	h->image_size = UINT64_MAX - 10;
	CHECK(scn2_check_header(h, avail) == SCN2_E_BOUNDS);
	h->image_size = 10000;
	h->entry_offset = 10000;
	CHECK(scn2_check_header(h, avail) == SCN2_E_BOUNDS);
	h->entry_offset = 0;
	h->load_addr = 0x1000;
	CHECK(scn2_check_header(h, avail) == SCN2_E_BOUNDS);
	h->load_addr = 0;

	/* header fields */
	h->magic ^= 1;
	CHECK(scn2_check_header(h, avail) == SCN2_E_MAGIC);
	h->magic ^= 1;
	h->version = 2;
	CHECK(scn2_check_header(h, avail) == SCN2_E_VERSION);
	h->version = 1;
	h->header_size = 200;
	CHECK(scn2_check_header(h, avail) == SCN2_E_VERSION);
	h->header_size = 4096;
	h->flags = SCION_S2_KIND_RAW << SCION_S2_KIND_SHIFT;
	CHECK(scn2_check_header(h, avail) == SCN2_E_KIND);
	h->flags = (SCION_S2_KIND_XNU << SCION_S2_KIND_SHIFT) | SCION_S2_FLAG_SKIP_HASH;
	CHECK(scn2_check_header(h, avail) == SCN2_E_NOHASH);
	h->flags = SCION_S2_KIND_XNU << SCION_S2_KIND_SHIFT;

	/* damage: a flipped byte is a CRC failure; with the CRC fixed up, a SHA-256 failure */
	b[4096 + 777] ^= 0x40;
	CHECK(scn2_verify(h, avail, NULL, d) == SCN2_E_CRC);
	h->crc32 = crc32(b + 4096, 10000);
	CHECK(scn2_verify(h, avail, NULL, d) == SCN2_E_SHA);
	b[4096 + 777] ^= 0x40;
	h->crc32 = crc32(b + 4096, 10000);
	CHECK(scn2_verify(h, avail, NULL, NULL) == SCN2_OK);

	/* pins */
	uint8_t pins_ok[2][32], pins_no[1][32];
	memset(pins_ok[0], 0xaa, 32);
	memcpy(pins_ok[1], h->sha256, 32);
	memset(pins_no[0], 0xbb, 32);
	struct scn2_pins p1 = { pins_ok, 2 }, p2 = { pins_no, 1 }, p0 = { pins_no, 0 };
	CHECK(scn2_verify(h, avail, &p1, NULL) == SCN2_OK);
	CHECK(scn2_verify(h, avail, &p2, NULL) == SCN2_E_UNPINNED);
	CHECK(scn2_verify(h, avail, &p0, NULL) == SCN2_OK);
	CHECK(strcmp(scn2_strerror(SCN2_E_SHA), "SHA-256 mismatch (image changed)") == 0);
	free(b);
}

/* ---- re-entry and guard records ------------------------------------------------------------ */

static void test_records(void)
{
	struct scion_reentry r;
	memset(&r, 0, sizeof(r));
	r.reason = SCION_RE_REASON_TEST;
	r.flags = SCION_RE_BOOT_ENTRY;
	r.entry = 1;
	r.fdt_pa = 0x91000000;
	strcpy(r.cmdline, "-v");
	scion_reentry_seal(&r);
	CHECK(r.magic == SCION_REENTRY_MAGIC && r.size == 728);
	CHECK(scion_reentry_valid(&r));
	r.entry = 2;
	CHECK(!scion_reentry_valid(&r));        /* changed after sealing */
	scion_reentry_seal(&r);
	CHECK(scion_reentry_valid(&r));
	memset(r.cmdline, 'x', sizeof(r.cmdline));
	scion_reentry_seal(&r);
	CHECK(!scion_reentry_valid(&r));        /* line not terminated */
	r.cmdline[10] = 0;
	r.version = 2;
	scion_reentry_seal(&r);                 /* seal rewrites the version */
	CHECK(scion_reentry_valid(&r));
	r.size = 696;
	CHECK(!scion_reentry_valid(&r));

	struct scion_guard_rec g;
	memset(&g, 0xff, sizeof(g));
	CHECK(!scion_guard_valid(&g));
	CHECK(guard_plan(&g, 1) == GUARD_NORMAL);   /* garbage reads as no strikes */
	guard_clear(&g);
	CHECK(scion_guard_valid(&g) && g.strikes == 0);
}

/* ---- guard -------------------------------------------------------------------------------- */

static void test_guard(void)
{
	struct scion_guard_rec g;
	memset(&g, 0, sizeof(g));
	/* three automatic starts that never acknowledge */
	for (int i = 0; i < 3; i++) {
		CHECK(guard_plan(&g, 1) == GUARD_NORMAL);
		guard_before_start(&g, 0, 0, 0);
	}
	CHECK(g.strikes == 3);
	CHECK(guard_plan(&g, 1) == GUARD_SAFE);
	CHECK(guard_plan(&g, 0) == GUARD_RECOVERY);
	guard_before_start(&g, 1, 0, 1);            /* the known-good entry */
	CHECK(g.strikes == 4 && (g.flags & SCION_GUARD_F_SAFE_TRIED));
	CHECK(guard_plan(&g, 1) == GUARD_RECOVERY);
	/* the OS acknowledges: strikes 0 */
	g.strikes = 0;
	scion_guard_seal(&g);
	CHECK(guard_plan(&g, 1) == GUARD_NORMAL);
	/* a user's choice forgives earlier strikes */
	g.strikes = 7;
	scion_guard_seal(&g);
	guard_before_start(&g, 2, 1, 0);
	CHECK(g.strikes == 1 && g.flags == 0 && g.last_entry == 2);
	/* a loader hang */
	guard_strike(&g);
	CHECK(g.strikes == 2 && scion_guard_valid(&g));
	guard_clear(&g);
	CHECK(g.strikes == 0);
}

/* ---- command line and menu ---------------------------------------------------------------- */

static void test_cmdline(void)
{
	char l[64];
	strcpy(l, "-v serial=3 debug=0x8");
	CHECK(cmdline_has(l, "-v"));
	CHECK(cmdline_has(l, "serial="));
	CHECK(cmdline_has(l, "serial=3"));
	CHECK(!cmdline_has(l, "serial=4"));
	CHECK(!cmdline_has(l, "-"));
	CHECK(!cmdline_has(l, "debug"));
	CHECK(cmdline_toggle(l, sizeof(l), "serial=3") == 0 && strcmp(l, "-v debug=0x8") == 0);
	CHECK(cmdline_toggle(l, sizeof(l), "wdt=-1") == 0 && strcmp(l, "-v debug=0x8 wdt=-1") == 0);
	CHECK(cmdline_toggle(l, sizeof(l), "-v") == 0 && strcmp(l, "debug=0x8 wdt=-1") == 0);
	CHECK(cmdline_toggle(l, sizeof(l), "debug=") == 0 && strcmp(l, "wdt=-1") == 0);
	strcpy(l, "  a  b a ");
	CHECK(cmdline_toggle(l, sizeof(l), "a") == 0 && strcmp(l, "b") == 0);
	l[0] = 0;
	CHECK(cmdline_append(l, sizeof(l), "x=1") == 0 && strcmp(l, "x=1") == 0);
	char small[8] = "abc";
	CHECK(cmdline_append(small, sizeof(small), "defg") == -1 && strcmp(small, "abc") == 0);
	CHECK(cmdline_append(small, sizeof(small), "def") == 0 && strcmp(small, "abc def") == 0);
	CHECK(cmdline_toggle(small, sizeof(small), "z") == -1 && strcmp(small, "abc def") == 0);
	cmdline_copy(small, "0123456789", sizeof(small));
	CHECK(strcmp(small, "0123456") == 0);

	struct menu_state m = { 5, 0, 0 };
	CHECK(menu_feed(&m, MK_NONE) == -1 && !m.touched);
	CHECK(menu_feed(&m, MK_UP) == -1 && m.sel == 4 && m.touched);
	CHECK(menu_feed(&m, MK_DOWN) == -1 && m.sel == 0);
	CHECK(menu_feed(&m, MK_SELECT) == 0);
	CHECK(menu_feed(&m, MK_EDIT) == -2);
	CHECK(menu_feed(&m, MK_DIGIT0 + 3) == 2 && m.sel == 2);
	CHECK(menu_feed(&m, MK_DIGIT0 + 6) == -1 && m.sel == 2);
}

/* ---- SWEETXNU plan ------------------------------------------------------------------------ */

static void test_xnu(void)
{
	const uint64_t V = 0xfffffff007000000ull;
	size_t len = 0x6000;
	struct xnu_payload *p = calloc(1, len);
	memcpy(p->magic, "SWEETXNU", 8);
	p->version = 1;
	p->nsegs = 1;
	p->virt_base = V;
	p->entry_va = V + 0x4000;
	p->adt_off = 0x5000;
	p->adt_size = 0x400;
	p->kernel_end = V + 0x8000;
	p->phys_base = 0x92000000;
	p->mem_size = 0x10000000;
	p->segs[0] = (struct xnu_seg){ V + 0x4000, 0x4000, 0x1000, 0x2000 };
	struct xnu_window w[2] = { { 0x92000000, 0xaf800000 }, { 0x880000000ull, 0x900000000ull } };
	struct xnu_plan pl;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) == NULL);
	CHECK(pl.entry_pa == 0x92004000 && pl.kernel_end_pa == 0x92008000);
	CHECK(pl.adt_pa == 0x92008000 && pl.boot_args_pa == 0x92009000);
	CHECK(xnu_plan(p, len, 0x92001000, w, 2, &pl) != NULL);     /* overlaps its own window */
	CHECK(xnu_plan(p, len - 0x1000, 0x80100000, w, 2, &pl) != NULL);  /* ADT past the end */
	p->phys_base = 0x880000000ull;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) == NULL);     /* the second bank */
	p->phys_base = 0x90000000;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);     /* outside both windows */
	p->phys_base = 0xaf000000;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);     /* runs past the window */
	p->phys_base = 0x92000000;
	p->segs[0].vmsize = 0x5000;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);     /* segment past kernel_end */
	p->segs[0].vmsize = 0x4000;
	p->segs[0].file_size = 0x5000;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);     /* file larger than vm */
	p->segs[0].file_size = 0x2000;
	p->entry_va = V + 0x8000;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);     /* entry outside */
	p->entry_va = V + 0x4000;
	p->nsegs = 40;
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);
	p->nsegs = 1;
	p->cmdline[607] = 'x';
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);     /* line not terminated */
	p->cmdline[607] = 0;
	p->magic[0] = 'X';
	CHECK(xnu_plan(p, len, 0x80100000, w, 2, &pl) != NULL);
	free(p);
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "sha") == 0) {
		size_t n = strtoul(argv[2], NULL, 0);
		uint8_t *b = malloc(n + 1), d[32];
		for (size_t i = 0; i < n; i++)
			b[i] = pattern(i);
		sha256(b, n, d);
		for (int i = 0; i < 32; i++)
			printf("%02x", d[i]);
		printf(" %08x\n", crc32(b, n));
		free(b);
		return 0;
	}
	CHECK(crypto_selftest() == 0);
	CHECK(crc32("123456789", 9) == 0xCBF43926u);
	test_scn2();
	test_records();
	test_guard();
	test_cmdline();
	test_xnu();
	printf(fails ? "codecs-test: %d failure(s)\n" : "codecs-test: all passed\n", fails);
	return fails ? 1 : 0;
}
