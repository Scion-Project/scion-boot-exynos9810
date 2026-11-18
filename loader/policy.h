/* SPDX-License-Identifier: MIT
 * The loader's decisions, as plain C over values (policy.c): the boot-loop guard's plan, the
 * command-line edits and the menu's cursor. No hardware, no output: tools/host-test.sh runs
 * every function here on the host.
 */
#ifndef SCION_POLICY_H
#define SCION_POLICY_H

#include <stdint.h>
#include "boot.h"

/* ---- boot-loop guard --------------------------------------------------------------------- */

enum guard_plan {
	GUARD_NORMAL,        /* fewer than SCION_GUARD_LIMIT strikes: menu countdown, then the default */
	GUARD_SAFE,          /* tripped, a known-good entry exists and was not tried yet: start it */
	GUARD_RECOVERY,      /* tripped and nothing left to try: no OS countdown, recovery is the default */
};

/* rec is what the retained RAM held (invalid -> zero strikes). */
enum guard_plan guard_plan(const struct scion_guard_rec *rec, int have_safe);
/* The record to store before starting entry `entry`: user_chose = the user picked it in the
 * menu (forgives earlier strikes: the user is there), safe = started by GUARD_SAFE. */
void guard_before_start(struct scion_guard_rec *rec, uint32_t entry, int user_chose, int safe);
/* The record after a deliberate exit (recovery, download, power off): no strikes. */
void guard_clear(struct scion_guard_rec *rec);
/* A loader hang (its own watchdog fired): one more strike. */
void guard_strike(struct scion_guard_rec *rec);

/* ---- command line ------------------------------------------------------------------------ */

/* 1 when `tok` is a whole word of `line`; for "key=" tokens, any "key=..." word matches. */
int cmdline_has(const char *line, const char *tok);
/* Add `tok` if absent, remove it (every matching word) if present. 0, or -1 when it does not fit
 * in cap bytes (the line is then unchanged). */
int cmdline_toggle(char *line, unsigned cap, const char *tok);
/* Append " tok" when it fits: 0, else -1 and the line unchanged. */
int cmdline_append(char *line, unsigned cap, const char *tok);
/* Copy at most cap-1 bytes and NUL-terminate. */
void cmdline_copy(char *dst, const char *src, unsigned cap);

/* ---- menu cursor ------------------------------------------------------------------------- */

enum menu_key { MK_NONE, MK_UP, MK_DOWN, MK_SELECT, MK_EDIT, MK_BACK, MK_DIGIT0 = 0x100 };

struct menu_state {
	unsigned n;          /* items */
	unsigned sel;        /* highlighted */
	int touched;         /* a key was pressed: the countdown stops */
};

/* Returns the item chosen (MK_SELECT / a digit), or -1. MK_EDIT returns -2 - sel. */
int menu_feed(struct menu_state *m, unsigned key);

#endif
