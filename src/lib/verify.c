/* SPDX-License-Identifier: MIT
 * SCN2 record checks for the second-stage loader: see verify.h.
 */
#include "verify.h"

uint32_t crc32(const void *data, size_t len);
void sha256(const void *data, size_t len, uint8_t out[32]);

_Static_assert(sizeof(struct scion_stage2_hdr) == 112, "SCN2 header is 112 bytes");
_Static_assert(sizeof(struct scion_reentry) == 728, "re-entry record layout");
_Static_assert(__builtin_offsetof(struct scion_reentry, cmdline) == 24, "re-entry: cmdline");
_Static_assert(__builtin_offsetof(struct scion_reentry, method) == 632, "re-entry: method");
_Static_assert(__builtin_offsetof(struct scion_reentry, fdt_pa) == 688, "re-entry: fdt_pa");
_Static_assert(__builtin_offsetof(struct scion_reentry, image_sha256) == 696, "re-entry: image_sha256");
_Static_assert(sizeof(struct scion_guard_rec) == 32, "guard record layout");

static int same(const uint8_t *a, const uint8_t *b, unsigned n)
{
	uint8_t d = 0;
	for (unsigned i = 0; i < n; i++)
		d |= a[i] ^ b[i];
	return d == 0;
}

enum scn2_status scn2_check_header(const struct scion_stage2_hdr *h, uint64_t avail)
{
	if (avail < sizeof(*h) || h->magic != SCION_STAGE2_MAGIC)
		return SCN2_E_MAGIC;
	if (h->version != SCION_STAGE2_VERSION ||
	    (h->header_size != sizeof(*h) && h->header_size != SCION_S2_PAGE))
		return SCN2_E_VERSION;
	unsigned kind = SCION_S2_KIND(h->flags);
	if (kind != SCION_S2_KIND_XNU && kind != SCION_S2_KIND_IMAGE)
		return SCN2_E_KIND;
	if (h->load_addr != 0 || h->image_size == 0 || h->entry_offset >= h->image_size)
		return SCN2_E_BOUNDS;
	if (h->header_size > avail || h->image_size > avail - h->header_size)
		return SCN2_E_BOUNDS;
	if (h->flags & SCION_S2_FLAG_SKIP_HASH)
		return SCN2_E_NOHASH;
	return SCN2_OK;
}

enum scn2_status scn2_verify(const struct scion_stage2_hdr *h, uint64_t avail,
                             const struct scn2_pins *pins, uint8_t digest_out[32])
{
	uint8_t digest[32];
	enum scn2_status s = scn2_check_header(h, avail);
	if (s != SCN2_OK)
		return s;
	const uint8_t *payload = (const uint8_t *)h + h->header_size;
	if (crc32(payload, (size_t)h->image_size) != h->crc32)
		s = SCN2_E_CRC;
	sha256(payload, (size_t)h->image_size, digest);
	if (digest_out)
		for (unsigned i = 0; i < 32; i++)
			digest_out[i] = digest[i];
	if (s != SCN2_OK)
		return s;
	if (!same(digest, h->sha256, 32))
		return SCN2_E_SHA;
	if (pins && pins->n) {
		for (unsigned i = 0; i < pins->n; i++)
			if (same(digest, pins->sha256[i], 32))
				return SCN2_OK;
		return SCN2_E_UNPINNED;
	}
	return SCN2_OK;
}

uint64_t scn2_record_span(const struct scion_stage2_hdr *h)
{
	uint64_t n = (uint64_t)h->header_size + h->image_size;
	return (n + SCION_S2_PAGE - 1) & ~(uint64_t)(SCION_S2_PAGE - 1);
}

const char *scn2_strerror(enum scn2_status s)
{
	switch (s) {
	case SCN2_OK:         return "verified";
	case SCN2_E_MAGIC:    return "not an SCN2 record";
	case SCN2_E_VERSION:  return "unknown SCN2 header version";
	case SCN2_E_KIND:     return "payload kind not startable";
	case SCN2_E_BOUNDS:   return "payload size or entry out of bounds";
	case SCN2_E_NOHASH:   return "payload has no hash (SKIP_HASH refused)";
	case SCN2_E_CRC:      return "CRC32 mismatch (damaged image)";
	case SCN2_E_SHA:      return "SHA-256 mismatch (image changed)";
	case SCN2_E_UNPINNED: return "digest not in this loader's trusted list";
	}
	return "unknown error";
}

/* CRC over a record with its crc field taken as 0 (the field is at crc_off). */
static uint32_t crc_without(const void *rec, size_t size, size_t crc_off)
{
	uint8_t tmp[sizeof(struct scion_reentry)];
	const uint8_t *p = rec;
	if (size > sizeof(tmp))
		return 0;
	for (size_t i = 0; i < size; i++)
		tmp[i] = p[i];
	for (size_t i = 0; i < 4; i++)
		tmp[crc_off + i] = 0;
	return crc32(tmp, size);
}

int scion_reentry_valid(const struct scion_reentry *r)
{
	if (r->magic != SCION_REENTRY_MAGIC || r->version != SCION_REENTRY_VERSION ||
	    r->size != sizeof(*r))
		return 0;
	if (crc_without(r, sizeof(*r), __builtin_offsetof(struct scion_reentry, crc32)) != r->crc32)
		return 0;
	for (unsigned i = 0; i < SCION_CMDLINE_MAX; i++)
		if (!r->cmdline[i])
			return 1;
	return 0; /* the line must be NUL-terminated */
}

void scion_reentry_seal(struct scion_reentry *r)
{
	r->magic = SCION_REENTRY_MAGIC;
	r->version = SCION_REENTRY_VERSION;
	r->size = sizeof(*r);
	r->crc32 = 0;
	r->crc32 = crc_without(r, sizeof(*r), __builtin_offsetof(struct scion_reentry, crc32));
}

int scion_guard_valid(const struct scion_guard_rec *g)
{
	return g->magic == SCION_GUARD_MAGIC && g->version == SCION_GUARD_VERSION &&
	       g->size == sizeof(*g) &&
	       crc_without(g, sizeof(*g), __builtin_offsetof(struct scion_guard_rec, crc32)) == g->crc32;
}

void scion_guard_seal(struct scion_guard_rec *g)
{
	g->magic = SCION_GUARD_MAGIC;
	g->version = SCION_GUARD_VERSION;
	g->size = sizeof(*g);
	g->_reserved = 0;
	g->crc32 = crc_without(g, sizeof(*g), __builtin_offsetof(struct scion_guard_rec, crc32));
}
