/* SPDX-License-Identifier: MIT
 * The second-stage loader (docs/STATUS.md): S-Boot starts it from the BOOT partition at EL1.
 *
 *   1. console, identity map, framebuffer; the caller's re-entry record when there is one
 *   2. the SCN2 records appended to this Image, listed by their headers (nothing hashed yet)
 *   3. the boot-loop guard's retained record and its plan
 *   4. the menu (countdown to the default, or the guard's choice), unless a re-entry asked for
 *      an entry directly
 *   5. the chosen entry: CRC32 + SHA-256 (+ pinned digests) of its whole payload, then its
 *      start; a failed check shows the error screen and comes back to the menu
 *
 * Nothing here writes any storage: everything the loader reads and writes is RAM and the
 * registers plat_*.c names.
 */
#include <stdint.h>
#include "loader.h"
#include "xnu.h"
#include "fbcon.h"
#include "plat.h"
#include "lib.h"
#include "pins.h"

extern char _start[], _image_end[];
void ldr_jump(uint64_t entry, uint64_t x0) __attribute__((noreturn));
int mmu_enable(void);

static struct ldr L;
struct ldr *ldr_global;


/* ---- guard ------------------------------------------------------------------------------- */

static void guard_load(void)
{
	volatile struct scion_guard_rec *slot = plat_guard_slot();
	uint8_t *d = (uint8_t *)&L.guard;
	for (unsigned i = 0; i < sizeof(L.guard); i++)
		d[i] = slot ? ((volatile uint8_t *)slot)[i] : 0;
	if (!scion_guard_valid(&L.guard))
		guard_clear(&L.guard);
}

void ldr_guard_store(struct ldr *l)
{
	volatile struct scion_guard_rec *slot = plat_guard_slot();
	const uint8_t *s = (const uint8_t *)&l->guard;
	if (!slot)
		return;
	for (unsigned i = 0; i < sizeof(l->guard); i++)
		((volatile uint8_t *)slot)[i] = s[i];
	__asm__ volatile("dsb sy" ::: "memory");
}

/* ---- the appended records ---------------------------------------------------------------- */

static void scan(void)
{
	uint64_t lo, hi;
	plat_image_area(&lo, &hi);
	uint64_t p = ((uint64_t)(uintptr_t)_image_end + SCION_S2_PAGE - 1) & ~(uint64_t)(SCION_S2_PAGE - 1);
	L.def = 0;
	L.safe = -1;
	while (L.n < SCION_S2_MAX_ENTRIES && p >= lo && p + SCION_S2_PAGE <= hi) {
		const struct scion_stage2_hdr *h = (const void *)(uintptr_t)p;
		enum scn2_status st = scn2_check_header(h, hi - p);
		if (st == SCN2_E_MAGIC)
			break;
		struct ldr_entry *e = &L.ent[L.n];
		e->h = h;
		e->avail = hi - p;
		e->state = st == SCN2_OK ? ES_LISTED : ES_FAILED;
		e->why = st;
		cmdline_copy(e->name, h->name[0] ? h->name : "unnamed", sizeof(e->name));
		if (st == SCN2_OK && SCION_S2_KIND(h->flags) == SCION_S2_KIND_XNU &&
		    h->image_size >= sizeof(struct xnu_payload)) {
			const struct xnu_payload *x = (const void *)(uintptr_t)(p + h->header_size);
			cmdline_copy(e->line, x->cmdline, sizeof(e->line));
		}
		kprintf("image %u: %s, %lu bytes at %p, %s\n", L.n + 1, e->name, h->image_size,
		        (void *)(uintptr_t)(p + h->header_size), st == SCN2_OK ? "header ok" : scn2_strerror(st));
		if (st == SCN2_OK && (h->flags & SCION_S2_FLAG_DEFAULT))
			L.def = L.n;
		if (st == SCN2_OK && (h->flags & SCION_S2_FLAG_SAFE) && L.safe < 0)
			L.safe = (int)L.n;
		L.n++;
		if (st != SCN2_OK)
			break;   /* the size of a bad record cannot be trusted to find the next one */
		p += scn2_record_span(h);
	}
}

static int verify(struct ldr_entry *e)
{
	char b[64];
	unsigned n = 0;
	if (e->state == ES_FAILED)
		return 0;
	b[0] = 0;
	for (const char *s = e->name; *s && n < 32; s++)
		b[n++] = *s;
	b[n] = 0;
	ui_progress("Checking SHA-256", b);
	/* Built at run time: a constant struct holding a pointer would be an absolute relocation
	 * (clang folds the initialiser into .rodata otherwise). */
	const uint8_t (*pp)[32] = ldr_pins;
	__asm__("" : "+r"(pp));
	struct scn2_pins pins = { pp, LDR_NPINS };
	enum scn2_status st = scn2_verify(e->h, e->avail, &pins, e->digest);
	e->why = st;
	e->state = st == SCN2_OK ? ES_VERIFIED : ES_FAILED;
	char hx[65];
	hex_digest(hx, e->digest, 32);
	kprintf("verify %s: %s (sha256 %s)\n", e->name, scn2_strerror(st), hx);
	return st == SCN2_OK;
}

static void bcopy8(uint64_t dst, uint64_t src, uint64_t n);
static void bzero8(uint64_t dst, uint64_t n);

/* ---- S-Boot's FDT ------------------------------------------------------------------------ */

static uint32_t be32(uint64_t pa)
{
	uint32_t v = *(volatile uint32_t *)(uintptr_t)pa;
	return __builtin_bswap32(v);
}

/* The FDT's size, or 0 when pa does not hold one. */
static uint32_t fdt_size(uint64_t pa)
{
	if (!pa || (pa & 7) || be32(pa) != 0xd00dfeedu)
		return 0;
	uint32_t n = be32(pa + 4);
	return n >= 40 && n <= plat_fdt_save_max() ? n : 0;
}

/* A line with sweetwarm= may come back through a warm restart, by which time S-Boot's FDT is
 * XNU's RAM: keep a copy where the OS side expects it (include/boot.h SCION_FDT_SAVE_*). */
static void save_fdt(const char *line)
{
	uint32_t n = fdt_size(L.fdt);
	if (L.reentry || !cmdline_has(line, "sweetwarm=") || !n)
		return;
	uint64_t dst = plat_fdt_save_area();
	bzero8(dst, SCION_FDT_SAVE_HDR);
	*(volatile uint64_t *)(uintptr_t)dst = SCION_FDT_SAVE_MAGIC;
	*(volatile uint64_t *)(uintptr_t)(dst + 8) = n;
	bcopy8(dst + SCION_FDT_SAVE_HDR, L.fdt, n);
	kprintf("fdt: %u bytes kept at %p for a warm restart\n", n, (void *)(uintptr_t)(dst + SCION_FDT_SAVE_HDR));
}

/* ---- starting an entry ------------------------------------------------------------------- */

static void bcopy8(uint64_t dst, uint64_t src, uint64_t n)
{
	uint64_t i = 0;
	if (!((dst | src) & 7))
		for (; i + 8 <= n; i += 8)
			*(volatile uint64_t *)(uintptr_t)(dst + i) = *(const volatile uint64_t *)(uintptr_t)(src + i);
	for (; i < n; i++)
		*(volatile uint8_t *)(uintptr_t)(dst + i) = *(const volatile uint8_t *)(uintptr_t)(src + i);
}

static void bzero8(uint64_t dst, uint64_t n)
{
	uint64_t i = 0;
	if (!(dst & 7))
		for (; i + 8 <= n; i += 8)
			*(volatile uint64_t *)(uintptr_t)(dst + i) = 0;
	for (; i < n; i++)
		*(volatile uint8_t *)(uintptr_t)(dst + i) = 0;
}

static void put_hex(char *b, uint64_t v)
{
	static const char hx[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
	                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	unsigned n = strlen(b), started = 0;
	b[n++] = '0';
	b[n++] = 'x';
	for (int s = 60; s >= 0; s -= 4) {
		unsigned d = (unsigned)(v >> s) & 15;
		if (d || started || s == 0) {
			b[n++] = hx[d];
			started = 1;
		}
	}
	b[n] = 0;
}

/* What the OS is told about the loader: where it lies (to keep it for a warm restart) and where
 * the guard's record is (to acknowledge a good boot). Added only when the line has room. */
static void add_loader_tokens(char *line, unsigned cap)
{
	char t[64];
	t[0] = 0;
	cmdline_copy(t, "scion_ldr=", sizeof(t));
	put_hex(t, (uint64_t)(uintptr_t)_start);
	uint64_t end = (uint64_t)(uintptr_t)_image_end;
	for (unsigned i = 0; i < L.n; i++) {
		uint64_t e = (uint64_t)(uintptr_t)L.ent[i].h + scn2_record_span(L.ent[i].h);
		if (L.ent[i].state != ES_FAILED && e > end)
			end = e;
	}
	unsigned n = strlen(t);
	t[n++] = ',';
	t[n] = 0;
	put_hex(t, end - (uint64_t)(uintptr_t)_start);
	if (cmdline_has(line, "scion_ldr=") || cmdline_append(line, cap, t))
		con_line("note: no room for scion_ldr= on the line");
	if (plat_guard_slot()) {
		cmdline_copy(t, "scion_guard=", sizeof(t));
		put_hex(t, (uint64_t)(uintptr_t)plat_guard_slot());
		if (cmdline_has(line, "scion_guard=") || cmdline_append(line, cap, t))
			con_line("note: no room for scion_guard= on the line");
	}
}

static void leave(enum plat_reboot why) __attribute__((noreturn));
static void leave(enum plat_reboot why)
{
	guard_clear(&L.guard);
	ldr_guard_store(&L);
	plat_wdt_stop();
	plat_reboot(why);
}

/* Start entry i; returns only when it could not (the error was shown). */
static void start(unsigned i, int user_chose, int safe)
{
	struct ldr_entry *e = &L.ent[i];
	if (!verify(e)) {
		if (!ui_verify_failed(e, scn2_strerror(e->why)))
			leave(PR_RECOVERY);
		return;
	}
	const uint8_t *payload = (const uint8_t *)e->h + e->h->header_size;
	uint64_t entry, x0;
	if (SCION_S2_KIND(e->h->flags) == SCION_S2_KIND_XNU) {
		unsigned nw;
		const struct xnu_window *w = plat_xnu_windows(&nw);
		const struct xnu_payload *p = (const void *)payload;
		struct xnu_plan pl;
		const char *bad = xnu_plan(p, e->h->image_size, (uint64_t)(uintptr_t)payload, w, nw, &pl);
		if (bad) {
			e->state = ES_FAILED;
			if (!ui_verify_failed(e, bad))
				leave(PR_RECOVERY);
			return;
		}
		guard_before_start(&L.guard, i, user_chose, safe);
		ldr_guard_store(&L);
		ui_progress("Copying the kernel", e->name);
		for (uint32_t s = 0; s < p->nsegs; s++) {
			const struct xnu_seg *g = &p->segs[s];
			uint64_t dst = p->phys_base + (g->vmaddr - p->virt_base);
			bcopy8(dst, (uint64_t)(uintptr_t)payload + g->file_off, g->file_size);
			bzero8(dst + g->file_size, g->vmsize - g->file_size);
		}
		bcopy8(pl.adt_pa, (uint64_t)(uintptr_t)payload + p->adt_off, p->adt_size);
		bzero8(pl.boot_args_pa, XNU_PAGE);
		volatile struct xnu_boot_args *ba = (volatile void *)(uintptr_t)pl.boot_args_pa;
		static char line[SCION_CMDLINE_MAX];
		cmdline_copy(line, e->line, sizeof(line));
		if (LDR_OS_TOKENS)
			add_loader_tokens(line, sizeof(line));
		save_fdt(line);
		ba->Revision = 2;
		ba->Version = 2;
		ba->virtBase = p->virt_base;
		ba->physBase = p->phys_base;
		ba->memSize = p->mem_size;
		ba->topOfKernelData = pl.boot_args_pa + XNU_PAGE;
		ba->deviceTreeP = p->virt_base + (pl.adt_pa - p->phys_base);
		ba->deviceTreeLength = (uint32_t)p->adt_size;
		for (unsigned k = 0; k < SCION_CMDLINE_MAX; k++)
			ba->CommandLine[k] = line[k];
		ba->memSizeActual = p->mem_size;
		entry = pl.entry_pa;
		x0 = pl.boot_args_pa;
		kprintf("xnu: entry %p boot_args %p device tree %p (%lu bytes)\nline: %s\n",
		        (void *)(uintptr_t)entry, (void *)(uintptr_t)x0, (void *)(uintptr_t)pl.adt_pa,
		        p->adt_size, line);
	} else {
		entry = (uint64_t)(uintptr_t)payload + e->h->entry_offset;
		x0 = L.fdt;
		if (entry & 3) {
			if (!ui_verify_failed(e, "image entry not aligned"))
				leave(PR_RECOVERY);
			return;
		}
		guard_before_start(&L.guard, i, user_chose, safe);
		ldr_guard_store(&L);
		kprintf("image: entry %p x0 %p\n", (void *)(uintptr_t)entry, (void *)(uintptr_t)x0);
	}
	char b[64];
	b[0] = 0;
	cmdline_copy(b, "EL1 at ", sizeof(b));
	put_hex(b, entry);
	ui_progress("Starting", b);
	kprintf("guard: %u unacknowledged start(s) recorded\n", L.guard.strikes);
	con_line("handoff");
	plat_wdt_stop();
	if (ldr_os_wdt && !(L.re.flags & SCION_RE_WDT_KEEP))
		plat_os_wdt_stop();
	plat_fb_handoff();
	ldr_jump(entry, x0);
}

/* ---- main -------------------------------------------------------------------------------- */

static void act(unsigned item, int user_chose, int safe)
{
	if (item < L.n) {
		start(item, user_chose, safe);
		return;
	}
	switch (item - L.n) {
	case LA_RECOVERY: leave(PR_RECOVERY);
	case LA_DOWNLOAD: leave(PR_DOWNLOAD);
	default:          leave(PR_POWEROFF);
	}
}

void loader_main(uint64_t x0, uint64_t reentry, uint64_t el)
{
	ldr_global = &L;
	plat_init();
	int mmu = mmu_enable();
	fb_init();
	key_init();
	fb_clear(FB_DARK);
	kprintf("\nscion-loader v%s (%s) at EL%lu, %s entry, image %p..%p, mmu %s\n", LDR_VERSION,
	        plat_name(), el, reentry ? "re-" : "cold", (void *)_start, (void *)_image_end,
	        mmu ? "off (map refused)" : "on");
	if (reentry) {
		const struct scion_reentry *r = (const void *)(uintptr_t)x0;
		uint64_t lo, hi;
		plat_image_area(&lo, &hi);
		if (x0 && !(x0 & 7) && scion_reentry_valid(r)) {
			L.re = *r;
			L.reentry = 1;
			ldr_os_wdt = (r->flags & SCION_RE_WDT_ARMED) != 0;
			kprintf("re-entry: reason %u flags 0x%x entry %d method %u fdt %p\n", r->reason, r->flags,
			        (int)r->entry, r->method, (void *)(uintptr_t)r->fdt_pa);
			if (r->image_pa && r->image_pa != (uint64_t)(uintptr_t)_start)
				kprintf("re-entry: note: the record says the image is at %p\n", (void *)(uintptr_t)r->image_pa);
		} else {
			con_line("re-entry: record missing or invalid: starting as a cold boot");
		}
		L.fdt = L.reentry && fdt_size(L.re.fdt_pa) ? L.re.fdt_pa : 0;
	} else {
		L.fdt = x0;
	}
	if (plat_wdt_start(LDR_WDT_MS))
		kprintf("watchdog: %u ms while the loader runs\n", LDR_WDT_MS);
	scan();
	guard_load();
	L.plan = guard_plan(&L.guard, L.safe >= 0);
	kprintf("guard: %u unacknowledged start(s), plan %u\n", L.guard.strikes, L.plan);
	if (!L.n)
		cmdline_copy(L.banner, "No startable image is appended to this loader.", sizeof(L.banner));

	unsigned item = L.def;
	uint32_t countdown = LDR_AUTOBOOT_MS;
	int guard_safe = 0;
	if (L.reentry) {
		if (L.re.flags & SCION_RE_MENU)
			countdown = 0;
		if (L.re.method != SCION_RE_METHOD_XNU) {
			cmdline_copy(L.banner, "A warm restart asked for the Android path: this loader starts only its own entries.", sizeof(L.banner));
			countdown = 0;
		} else if ((L.re.flags & SCION_RE_BOOT_ENTRY) && L.n) {
			unsigned i = L.re.entry < L.n ? L.re.entry : L.def;
			if ((L.re.flags & SCION_RE_CMDLINE) && (L.re.flags & SCION_RE_APPEND)) {
				if (cmdline_append(L.ent[i].line, sizeof(L.ent[i].line), L.re.cmdline))
					con_line("re-entry: the line to append does not fit: not appended");
			} else if (L.re.flags & SCION_RE_CMDLINE) {
				cmdline_copy(L.ent[i].line, L.re.cmdline, sizeof(L.ent[i].line));
			}
			if (L.re.flags & SCION_RE_CMDLINE)
				kprintf("re-entry: entry %u line: %s\n", i + 1, L.ent[i].line);
			int deliberate = L.re.reason == SCION_RE_REASON_USER || L.re.reason == SCION_RE_REASON_TEST;
			if (L.plan == GUARD_NORMAL || deliberate)
				start(i, deliberate, 0);
		}
	}
	if (plat_keys() & PK_UP)
		countdown = 0;   /* Volume Up held at start: the menu waits */
	if (L.plan == GUARD_SAFE) {
		item = (unsigned)L.safe;
		guard_safe = 1;
		cmdline_copy(L.banner, "Boot-loop guard: the last starts did not finish booting. Starting the known-good entry.", sizeof(L.banner));
		countdown = LDR_AUTOBOOT_MS;
	} else if (L.plan == GUARD_RECOVERY) {
		item = L.n + LA_RECOVERY;
		cmdline_copy(L.banner, "Boot-loop guard: nothing left to try. Rebooting to recovery unless you choose.", sizeof(L.banner));
		countdown = 2 * LDR_AUTOBOOT_MS;
	}
	if (!L.n && L.plan == GUARD_NORMAL)
		countdown = 0;
	for (;;) {
		int user;
		int r = ui_menu(&L, item, countdown, &user);
		act((unsigned)r, user, !user && guard_safe && (unsigned)r == (unsigned)L.safe);
		countdown = 0;   /* back from an error screen: wait for the user */
		L.banner[0] = 0;
	}
}
