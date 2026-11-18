/* SPDX-License-Identifier: MIT
 * The loader's screens: the menu, the command-line editor, the verification error and progress.
 * Each is drawn on the framebuffer and echoed to the UART (the QEMU tests read the UART).
 * No tables of pointers: the Image has no absolute relocations.
 */
#include <stdint.h>
#include "loader.h"
#include "fbcon.h"
#include "plat.h"
#include "lib.h"

static const char act_names[LA_COUNT][40] = {
	"Reboot to recovery (Scion recovery)", "Reboot to Download mode", "Power off"
};

static void fmt_u(char *b, unsigned *n, uint64_t v)
{
	char d[20];
	unsigned k = 0;
	do {
		d[k++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (k)
		b[(*n)++] = d[--k];
	b[*n] = 0;
}

static void cat(char *b, unsigned cap, const char *s)
{
	unsigned n = strlen(b);
	while (*s && n + 1 < cap)
		b[n++] = *s++;
	b[n] = 0;
}

static const char *state_word(const struct ldr_entry *e)
{
	if (e->state == ES_VERIFIED)
		return "verified";
	if (e->state == ES_FAILED)
		return "FAILED";
	return SCION_S2_KIND(e->h->flags) == SCION_S2_KIND_XNU ? "iOS" : "image";
}

/* Wrap s over rows starting at row, at most max rows, width w. Returns rows used. */
static unsigned wrap(unsigned row, unsigned max, const char *s, uint32_t fg)
{
	unsigned w = fb_cols() > 2 ? fb_cols() - 2 : 1, used = 0;
	char buf[128];
	while (*s && used < max) {
		unsigned k = 0;
		while (s[k] && k < w && k + 1 < sizeof(buf)) {
			buf[k] = s[k];
			k++;
		}
		buf[k] = 0;
		fb_text(1, row + used, buf, fg, FB_DARK);
		s += k;
		used++;
	}
	return used;
}

/* s from (1, row) with fg on bg, wrapped at the screen's width, continuation lines indented by
 * indent columns; at most max rows. Returns rows used. */
static unsigned wrap_bg(unsigned row, unsigned max, const char *s, uint32_t fg, uint32_t bg, unsigned indent)
{
	unsigned cols = fb_cols(), used = 0;
	char buf[128];
	while (*s && used < max) {
		unsigned pad = used ? indent : 0, w = cols > 1 + pad ? cols - 1 - pad : 1, k = 0;
		for (unsigned i = 0; i < pad; i++)
			buf[k++] = ' ';
		while (*s && k < pad + w && k + 1 < sizeof(buf))
			buf[k++] = *s++;
		buf[k] = 0;
		fb_text(1, row + used, buf, fg, bg);
		used++;
	}
	return used;
}

/* A sign of life drawn from a loop count, not from the clock: it turns while the loader polls,
 * whatever the timer does. */
static void heartbeat(void)
{
	static uint32_t n;
	static const char spin[4] = { '|', '/', '-', '\\' };
	if (!fb_ok() || (++n & 0x3fffu))
		return;
	char b[2] = { spin[(n >> 14) & 3], 0 };
	fb_text(fb_cols() - 1, 0, b, FB_WHITE, FB_BLUE);
}

static void draw_menu(struct ldr *L, unsigned sel, unsigned total, const char *count_line)
{
	char b[128];
	fb_clear(FB_DARK);
	fb_fill_rows(0, 2, FB_BLUE);
	b[0] = 0;
	cat(b, sizeof(b), "Scion loader v" LDR_VERSION "  ");
	cat(b, sizeof(b), plat_name());
	fb_text(1, 0, b, FB_WHITE, FB_BLUE);
	fb_text(1, 1, "Choose what to start", FB_WHITE, FB_BLUE);
	unsigned row = 3;
	if (ldr_clock_state()) {
		const char *cs = ldr_clock_state() == 1 ? "timer: CNTFRQ_EL0 is 0, board rate used" :
		                                          "timer: the counter does not move, counting loops";
		row += wrap_bg(row, 2, cs, FB_GREY, FB_DARK, 0);
		con_line(cs);
	}
	if (L->banner[0]) {
		row += wrap(row, 3, L->banner, FB_AMBER);
		row++;
	}
	con_line("---- menu ----");
	if (L->banner[0]) {
		kputs("!! ");
		con_line(L->banner);
	}
	for (unsigned i = 0; i < total; i++) {
		unsigned n = 0;
		b[0] = i == sel ? '>' : ' ';
		b[1] = ' ';
		n = 2;
		fmt_u(b, &n, i + 1);
		b[n++] = '.';
		b[n++] = ' ';
		b[n] = 0;
		if (i < L->n) {
			cat(b, sizeof(b), L->ent[i].name);
			cat(b, sizeof(b), "  [");
			cat(b, sizeof(b), state_word(&L->ent[i]));
			cat(b, sizeof(b), "]");
			if ((int)i == L->safe)
				cat(b, sizeof(b), " (known good)");
		} else {
			cat(b, sizeof(b), act_names[i - L->n]);
		}
		row += wrap_bg(row, 2, b, i == sel ? FB_WHITE : FB_GREY, i == sel ? FB_BLUE : FB_DARK, 5);
		con_line(b);
	}
	row++;
	if (sel < L->n && SCION_S2_KIND(L->ent[sel].h->flags) == SCION_S2_KIND_XNU) {
		fb_text(1, row++, "Command line:", FB_GREY, FB_DARK);
		row += wrap(row, 16, L->ent[sel].line[0] ? L->ent[sel].line : "(empty)", FB_WHITE);
		kputs("line: ");
		con_line(L->ent[sel].line);
	}
	if (sel < L->n && L->ent[sel].state == ES_FAILED) {
		fb_text(1, ++row, scn2_strerror(L->ent[sel].why), FB_RED, FB_DARK);
		row++;
	}
	unsigned last = fb_rows() ? fb_rows() - 1 : 0;
	fb_text(1, last - 4, "Volume Up/Down: move", FB_GREY, FB_DARK);
	fb_text(1, last - 3, "Power: start", FB_GREY, FB_DARK);
	fb_text(1, last - 2, "Bixby: edit the line", FB_GREY, FB_DARK);
	if (count_line && count_line[0]) {
		fb_text(1, last - 1, count_line, FB_AMBER, FB_DARK);
		fb_text(1, last, "(any key stops)", FB_AMBER, FB_DARK);
		con_line(count_line);
	}
}

static void count_text(char *b, struct ldr *L, unsigned item, uint64_t left_ms)
{
	unsigned n = 0;
	b[0] = 0;
	cat(b, 96, "autoboot: ");
	cat(b, 96, item < L->n ? L->ent[item].name : act_names[item - L->n]);
	cat(b, 96, " in ");
	n = strlen(b);
	fmt_u(b, &n, (left_ms + 999) / 1000);
	cat(b, 96, " s");
}

static void test_hook(unsigned k)
{
	if (!plat_test_hooks())
		return;
	if (k == 0x200u + 'H') {
		con_line("test: hanging the loader (the watchdog should fire)");
		for (;;)
			__asm__ volatile("nop");
	}
	if (k == 0x200u + 'X') {
		con_line("test: loading from an unmapped address");
		(void)*(volatile uint64_t *)(uintptr_t)0x1000000000ull;
	}
}

int ui_menu(struct ldr *L, unsigned countdown_item, uint32_t countdown_ms, int *user)
{
	struct menu_state m = { L->n + LA_COUNT, countdown_ms ? countdown_item : L->def, 0 };
	uint64_t deadline = countdown_ms ? now_ms() + countdown_ms : 0;
	uint64_t shown = ~0ull;
	char cl[96];
	*user = 0;
	cl[0] = 0;
	if (deadline)
		count_text(cl, L, countdown_item, countdown_ms);
	draw_menu(L, m.sel, m.n, cl);
	for (;;) {
		heartbeat();
		unsigned k = key_poll();
		if (k >= 0x200u) {
			test_hook(k);
			continue;
		}
		if (k != MK_NONE) {
			*user = 1;
			deadline = 0;
			int r = menu_feed(&m, k);
			if (r >= 0)
				return r;
			if (r <= -2) {
				ui_edit(L, (unsigned)(-2 - r));
			}
			draw_menu(L, m.sel, m.n, "");
			continue;
		}
		if (deadline) {
			uint64_t t = now_ms();
			if (t >= deadline)
				return (int)countdown_item;
			uint64_t s = (deadline - t + 999) / 1000;
			if (s != shown) {
				shown = s;
				count_text(cl, L, countdown_item, deadline - t);
				fb_fill_rows(fb_rows() - 2, 1, FB_DARK);
				fb_text(1, fb_rows() - 2, cl, FB_AMBER, FB_DARK);
				con_line(cl);
			}
		}
	}
}

/* ---- command-line editor ----------------------------------------------------------------- */

/* The n-th word of LDR_TOGGLES into out; 0 when there is none. */
static int toggle_word(unsigned n, char *out, unsigned cap)
{
	const char *t = LDR_TOGGLES;
	for (unsigned i = 0;; i++) {
		while (*t == ' ')
			t++;
		if (!*t)
			return 0;
		unsigned k = 0;
		while (t[k] && t[k] != ' ')
			k++;
		if (i == n) {
			unsigned c = k < cap - 1 ? k : cap - 1;
			for (unsigned j = 0; j < c; j++)
				out[j] = t[j];
			out[c] = 0;
			return 1;
		}
		t += k;
	}
}

static unsigned toggle_count(void)
{
	char w[48];
	unsigned n = 0;
	while (toggle_word(n, w, sizeof(w)))
		n++;
	return n;
}

/* UART: a plain line editor (type, Backspace, Ctrl-U clears, Enter keeps, Esc drops). */
static void edit_uart(struct ldr_entry *e)
{
	char buf[SCION_CMDLINE_MAX];
	unsigned n = 0;
	cmdline_copy(buf, e->line, sizeof(buf));
	n = strlen(buf);
	kputs("edit> ");
	kputs(buf);
	for (;;) {
		plat_wdt_kick();
		int c = plat_uart_getc();
		if (c < 0)
			continue;
		if (c == '\r' || c == '\n') {
			cmdline_copy(e->line, buf, sizeof(e->line));
			uart_putc('\n');
			kputs("line now: ");
			con_line(e->line);
			return;
		}
		if (c == 0x1b) {
			con_line("\nedit dropped");
			return;
		}
		if ((c == 0x7f || c == 8) && n) {
			buf[--n] = 0;
			kputs("\b \b");
		} else if (c == 0x15) {
			n = 0;
			buf[0] = 0;
			kputs("\nedit> ");
		} else if (c >= 0x20 && c < 0x7f && n + 1 < sizeof(buf)) {
			buf[n++] = (char)c;
			buf[n] = 0;
			uart_putc((char)c);
		}
	}
}

/* Buttons: a list of the build's toggles, then "restore" and "done". */
static void edit_keys(struct ldr *L, struct ldr_entry *e)
{
	unsigned nt = toggle_count();
	struct menu_state m = { nt + 2, 0, 0 };
	char w[48], b[96];
	for (;;) {
		fb_clear(FB_DARK);
		fb_fill_rows(0, 2, FB_BLUE);
		fb_text(1, 0, "Edit the command line", FB_WHITE, FB_BLUE);
		fb_text(1, 1, e->name, FB_WHITE, FB_BLUE);
		unsigned row = 3 + wrap(3, 10, e->line[0] ? e->line : "(empty)", FB_WHITE) + 1;
		for (unsigned i = 0; i < m.n; i++) {
			b[0] = 0;
			if (i < nt) {
				toggle_word(i, w, sizeof(w));
				cat(b, sizeof(b), cmdline_has(e->line, w) ? "[x] " : "[ ] ");
				cat(b, sizeof(b), w);
			} else {
				cat(b, sizeof(b), i == nt ? "Restore the image's line" : "Done");
			}
			fb_text(1, row + i, b, i == m.sel ? FB_WHITE : FB_GREY, i == m.sel ? FB_BLUE : FB_DARK);
		}
		int r;
		do {
			heartbeat();
			r = menu_feed(&m, key_poll());
		} while (r == -1 && !m.touched);
		m.touched = 0;
		if (r < 0)
			continue;
		if ((unsigned)r < nt) {
			toggle_word((unsigned)r, w, sizeof(w));
			if (cmdline_toggle(e->line, sizeof(e->line), w))
				fb_text(1, fb_rows() - 1, "no room on the line", FB_RED, FB_DARK);
		} else if ((unsigned)r == nt) {
			const struct xnu_payload *p = (const void *)((const uint8_t *)e->h + e->h->header_size);
			cmdline_copy(e->line, p->cmdline, sizeof(e->line));
		} else {
			return;
		}
	}
	(void)L;
}

void ui_edit(struct ldr *L, unsigned i)
{
	if (i >= L->n || SCION_S2_KIND(L->ent[i].h->flags) != SCION_S2_KIND_XNU) {
		con_line("edit: only an iOS entry has a command line here");
		return;
	}
	if (key_from_uart())
		edit_uart(&L->ent[i]);
	else
		edit_keys(L, &L->ent[i]);
}

/* ---- verification failure and progress --------------------------------------------------- */

int ui_verify_failed(const struct ldr_entry *e, const char *what)
{
	char hx[65], b[96];
	fb_clear(FB_DARK);
	fb_fill_rows(0, 3, FB_RED);
	fb_text(1, 0, "NOT STARTED: CHECK FAILED", FB_WHITE, FB_RED);
	fb_text(1, 1, e->name, FB_WHITE, FB_RED);
	wrap_bg(4, 2, what, FB_WHITE, FB_DARK, 0);
	con_line("==== NOT STARTED: CHECK FAILED ====");
	kprintf("entry: %s\nreason: %s\n", e->name, what);
	if (e->state == ES_FAILED && (e->why == SCN2_E_SHA || e->why == SCN2_E_CRC || e->why == SCN2_E_UNPINNED)) {
		hex_digest(hx, e->h->sha256, 32);
		b[0] = 0;
		cat(b, sizeof(b), "expected ");
		cat(b, sizeof(b), hx);
		wrap(6, 3, b, FB_GREY);
		con_line(b);
		hex_digest(hx, e->digest, 32);
		b[0] = 0;
		cat(b, sizeof(b), "computed ");
		cat(b, sizeof(b), hx);
		wrap(10, 3, b, FB_GREY);
		con_line(b);
	}
	wrap_bg(14, 3, "Nothing was started. Rebuild or reflash this BOOT image (docs/phone-test-plan.md).",
	        FB_WHITE, FB_DARK, 0);
	struct menu_state m = { 2, 0, 0 };
	for (;;) {
		fb_text(1, 17, "Back to the menu", m.sel == 0 ? FB_WHITE : FB_GREY, m.sel == 0 ? FB_BLUE : FB_DARK);
		fb_text(1, 18, "Reboot to recovery", m.sel == 1 ? FB_WHITE : FB_GREY, m.sel == 1 ? FB_BLUE : FB_DARK);
		con_line("1. Back to the menu\n2. Reboot to recovery (Scion recovery)");
		int r;
		do {
			heartbeat();
			r = menu_feed(&m, key_poll());
		} while (r == -1 && !m.touched);
		m.touched = 0;
		if (r == 0)
			return 1;
		if (r == 1)
			return 0;
	}
}

void ui_progress(const char *title, const char *detail)
{
	fb_fill_rows(fb_rows() / 2 - 1, 4, FB_DARK);
	fb_text(1, fb_rows() / 2, title, FB_WHITE, FB_DARK);
	if (detail)
		fb_text(1, fb_rows() / 2 + 1, detail, FB_GREY, FB_DARK);
	kputs(title);
	if (detail) {
		kputs(": ");
		kputs(detail);
	}
	uart_putc('\n');
}
