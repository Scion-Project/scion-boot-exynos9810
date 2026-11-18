/* SPDX-License-Identifier: MIT
 * The loader's decisions: see policy.h.
 */
#include "policy.h"
#include "verify.h"

/* ---- boot-loop guard --------------------------------------------------------------------- */

enum guard_plan guard_plan(const struct scion_guard_rec *rec, int have_safe)
{
	if (!scion_guard_valid(rec) || rec->strikes < SCION_GUARD_LIMIT)
		return GUARD_NORMAL;
	if (have_safe && !(rec->flags & SCION_GUARD_F_SAFE_TRIED))
		return GUARD_SAFE;
	return GUARD_RECOVERY;
}

void guard_before_start(struct scion_guard_rec *rec, uint32_t entry, int user_chose, int safe)
{
	if (!scion_guard_valid(rec) || user_chose) {
		rec->strikes = 0;
		rec->flags = 0;
	}
	if (rec->strikes < 0xffffu)
		rec->strikes++;
	if (safe)
		rec->flags |= SCION_GUARD_F_SAFE_TRIED;
	rec->last_entry = entry;
	scion_guard_seal(rec);
}

void guard_clear(struct scion_guard_rec *rec)
{
	rec->strikes = 0;
	rec->flags = 0;
	rec->last_entry = ~0u;
	scion_guard_seal(rec);
}

void guard_strike(struct scion_guard_rec *rec)
{
	if (!scion_guard_valid(rec)) {
		rec->strikes = 0;
		rec->flags = 0;
		rec->last_entry = ~0u;
	}
	if (rec->strikes < 0xffffu)
		rec->strikes++;
	scion_guard_seal(rec);
}

/* ---- command line ------------------------------------------------------------------------ */

static unsigned slen(const char *s)
{
	unsigned n = 0;
	while (s[n])
		n++;
	return n;
}

/* Length of the key part of tok when it ends in '=' ("debug=" -> 6), else 0. */
static unsigned key_len(const char *tok)
{
	unsigned n = slen(tok);
	return (n && tok[n - 1] == '=') ? n : 0;
}

/* Does the word w (wn bytes) match tok? */
static int word_matches(const char *w, unsigned wn, const char *tok)
{
	unsigned k = key_len(tok), tn = slen(tok);
	if (k) {
		if (wn < k)
			return 0;
		for (unsigned i = 0; i < k; i++)
			if (w[i] != tok[i])
				return 0;
		return 1;
	}
	if (wn != tn)
		return 0;
	for (unsigned i = 0; i < wn; i++)
		if (w[i] != tok[i])
			return 0;
	return 1;
}

int cmdline_has(const char *line, const char *tok)
{
	if (!tok[0])
		return 0;
	for (unsigned i = 0; line[i];) {
		while (line[i] == ' ')
			i++;
		unsigned s = i;
		while (line[i] && line[i] != ' ')
			i++;
		if (i > s && word_matches(line + s, i - s, tok))
			return 1;
	}
	return 0;
}

void cmdline_copy(char *dst, const char *src, unsigned cap)
{
	unsigned i = 0;
	if (!cap)
		return;
	for (; i + 1 < cap && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
}

int cmdline_append(char *line, unsigned cap, const char *tok)
{
	unsigned n = slen(line), t = slen(tok), sp = n ? 1 : 0;
	if (!t)
		return 0;
	if (n + sp + t + 1 > cap)
		return -1;
	if (sp)
		line[n++] = ' ';
	for (unsigned i = 0; i < t; i++)
		line[n++] = tok[i];
	line[n] = 0;
	return 0;
}

int cmdline_toggle(char *line, unsigned cap, const char *tok)
{
	if (!tok[0])
		return 0;
	if (!cmdline_has(line, tok))
		return cmdline_append(line, cap, tok);
	/* Remove every matching word, keep single spaces between the rest. */
	unsigned o = 0;
	for (unsigned i = 0; line[i];) {
		while (line[i] == ' ')
			i++;
		unsigned s = i;
		while (line[i] && line[i] != ' ')
			i++;
		if (i == s || word_matches(line + s, i - s, tok))
			continue;
		if (o)
			line[o++] = ' ';
		for (unsigned k = s; k < i; k++)
			line[o++] = line[k];
	}
	line[o] = 0;
	return 0;
}

/* ---- menu cursor ------------------------------------------------------------------------- */

int menu_feed(struct menu_state *m, unsigned key)
{
	if (key == MK_NONE || !m->n)
		return -1;
	m->touched = 1;
	if (key == MK_UP)
		m->sel = m->sel ? m->sel - 1 : m->n - 1;
	else if (key == MK_DOWN)
		m->sel = (m->sel + 1) % m->n;
	else if (key == MK_SELECT)
		return (int)m->sel;
	else if (key == MK_EDIT)
		return -2 - (int)m->sel;
	else if (key > MK_DIGIT0 && key <= MK_DIGIT0 + 9 && key - MK_DIGIT0 <= m->n) {
		m->sel = key - MK_DIGIT0 - 1;
		return (int)m->sel;
	}
	return -1;
}
