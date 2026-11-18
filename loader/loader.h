/* SPDX-License-Identifier: MIT
 * The second-stage loader's internal interfaces (main.c, ui.c, crash.c, console.c).
 */
#ifndef SCION_LOADER_H
#define SCION_LOADER_H

#include <stdint.h>
#include "boot.h"
#include "verify.h"
#include "policy.h"

#define LDR_VERSION "1"

#ifndef LDR_AUTOBOOT_MS
#define LDR_AUTOBOOT_MS 5000u
#endif
/* 1: an XNU entry's line gets scion_ldr= and scion_guard= when it has room (main.c add_loader_tokens).
 * 0: the line is passed exactly as packed. The S9's kernel appends its built-in arguments to the
 * line only while its 1024 bytes have room, so 54 bytes more from the loader can push the last
 * built-ins out; 0 keeps a boot identical to sboot's for a comparison, at the cost of the OS not
 * learning where the loader and the guard's record are (nothing in the kernel uses either yet). */
#ifndef LDR_OS_TOKENS
#define LDR_OS_TOKENS 1
#endif
#ifndef LDR_WDT_MS
#define LDR_WDT_MS 30000u
#endif
#ifndef LDR_TOGGLES
#define LDR_TOGGLES "-v serial=3 sweetwdog=60 wdt=-1"
#endif

enum ent_state { ES_LISTED, ES_VERIFIED, ES_FAILED };

struct ldr_entry {
	const struct scion_stage2_hdr *h;
	uint64_t avail;                 /* bytes from h to the end of the image area */
	enum ent_state state;
	enum scn2_status why;
	uint8_t digest[32];             /* computed, once hashed */
	char name[33];
	char line[SCION_CMDLINE_MAX];   /* XNU: the line this boot will use (editable) */
};

/* Menu actions after the entries. */
enum ldr_action { LA_RECOVERY, LA_DOWNLOAD, LA_POWEROFF, LA_COUNT };

struct ldr {
	struct ldr_entry ent[SCION_S2_MAX_ENTRIES];
	unsigned n;
	unsigned def;                   /* default entry */
	int safe;                       /* the known-good entry, -1 none */
	uint64_t fdt;                   /* S-Boot's x0 (cold), 0 on re-entry */
	int reentry;
	struct scion_reentry re;        /* copy of the caller's record when valid */
	struct scion_guard_rec guard;   /* RAM copy of the retained record */
	enum guard_plan plan;
	char banner[96];
};

/* console.c */
void con_line(const char *s);            /* UART and nothing else */
unsigned key_poll(void);                 /* MK_* or MK_NONE, never blocks */
void delay_ms(uint32_t ms);
uint64_t now_ms(void);
void hex_digest(char *out, const uint8_t *d, unsigned bytes);
void key_init(void);
int key_from_uart(void);            /* the last key came from the UART */

/* ui.c */
/* The menu: returns the item chosen (entry index, or n + ldr_action). countdown_ms 0 = none;
 * countdown_item = what the countdown starts. *user is set when a key was pressed. */
int ui_menu(struct ldr *L, unsigned countdown_item, uint32_t countdown_ms, int *user);
void ui_edit(struct ldr *L, unsigned i);
/* The verification failure screen; returns 1 = back to the menu, 0 = reboot to recovery. */
int ui_verify_failed(const struct ldr_entry *e, const char *what);
void ui_progress(const char *title, const char *detail);

/* main.c */
extern int ldr_os_wdt;
void ldr_os_wdt_done(void);
void ldr_guard_store(struct ldr *L);
extern struct ldr *ldr_global;

#endif
