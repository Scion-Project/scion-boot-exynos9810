/* SPDX-License-Identifier: MIT
 * SCN2 record checks for the second-stage loader (verify.c). Plain C over
 * bytes: no hardware, no output. tools/host-test.sh runs it on the host.
 */
#ifndef SCION_VERIFY_H
#define SCION_VERIFY_H

#include <stddef.h>
#include <stdint.h>
#include "boot.h"

enum scn2_status {
	SCN2_OK = 0,
	SCN2_E_MAGIC,     /* not an SCN2 record */
	SCN2_E_VERSION,   /* a header version or size this loader does not read */
	SCN2_E_KIND,      /* a payload kind the loader does not start */
	SCN2_E_BOUNDS,    /* the payload runs past what was loaded */
	SCN2_E_NOHASH,    /* SKIP_HASH: the loader never starts an unhashed payload */
	SCN2_E_CRC,       /* CRC32 mismatch: damaged in storage or in RAM */
	SCN2_E_SHA,       /* SHA-256 mismatch */
	SCN2_E_UNPINNED,  /* hash fine, but not one of the digests this loader was built to accept */
};

/* Pinned digests: the loader accepts only these (n == 0: any digest that matches its header). */
struct scn2_pins {
	const uint8_t (*sha256)[32];
	unsigned n;
};

/* Header only: magic, version, sizes and that the payload fits in avail bytes from the header.
 * Cheap: used to list the entries before anything is hashed. */
enum scn2_status scn2_check_header(const struct scion_stage2_hdr *h, uint64_t avail);

/* Full check: header, CRC32 and SHA-256 of the payload (which starts header_size bytes after h),
 * then the pins. digest_out (may be NULL) gets the computed SHA-256 for the error screen. */
enum scn2_status scn2_verify(const struct scion_stage2_hdr *h, uint64_t avail,
                             const struct scn2_pins *pins, uint8_t digest_out[32]);

/* Bytes from h to the next record (header + payload, rounded up to SCION_S2_PAGE). */
uint64_t scn2_record_span(const struct scion_stage2_hdr *h);

/* A short English reason for the screen. */
const char *scn2_strerror(enum scn2_status s);

/* The record of the re-entry and guard structs: magic/version/size/crc32 check (1 = valid). */
int scion_reentry_valid(const struct scion_reentry *r);
void scion_reentry_seal(struct scion_reentry *r);
int scion_guard_valid(const struct scion_guard_rec *g);
void scion_guard_seal(struct scion_guard_rec *g);

#endif
