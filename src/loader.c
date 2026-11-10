/*
 * Stage-2 image loader and integrity verifier (CRC32 + SHA-256).
 */
#include <stdint.h>
#include <stddef.h>
#include "boot.h"
#include "lib.h"

int stage2_build_From_fn(struct scion_stage2_hdr *hdr, void *dst_payload,
                         const void *fn_src, size_t fn_size, const char *name)
{
	if (!hdr || !fn_src || fn_size == 0)
		return -1;

	const void *payload = fn_src;
	if (dst_payload && dst_payload != fn_src) {
		memcpy(dst_payload, fn_src, fn_size);
		payload = dst_payload;
	}

	memset(hdr, 0, sizeof(*hdr));
	hdr->magic        = SCION_STAGE2_MAGIC;
	hdr->version      = SCION_STAGE2_VERSION;
	hdr->header_size  = sizeof(*hdr);
	hdr->flags        = SCION_S2_FLAG_PIC;
	hdr->load_addr    = (uint64_t)(uintptr_t)payload;
	hdr->entry_offset = 0;
	hdr->image_size   = fn_size;
	hdr->crc32        = crc32(payload, fn_size);
	sha256(payload, fn_size, hdr->sha256);

	if (name) {
		size_t n = strlen(name);
		if (n >= sizeof(hdr->name))
			n = sizeof(hdr->name) - 1;
		memcpy(hdr->name, name, n);
		hdr->name[n] = '\0';
	}
	return 0;
}

int stage2_load_and_verify(struct scion_boot_info *bi,
                           const struct scion_stage2_hdr *hdr,
                           uint64_t *entry_out)
{
	if (!bi || !hdr || !entry_out)
		return -1;

	if (hdr->magic != SCION_STAGE2_MAGIC) {
		kprintf("loader: bad stage2 magic 0x%08x (expected 0x%08x)\n",
		        hdr->magic, SCION_STAGE2_MAGIC);
		return -2;
	}
	if (hdr->version != SCION_STAGE2_VERSION || hdr->header_size != sizeof(*hdr)) {
		kprintf("loader: unsupported stage2 hdr v%u size %u\n",
		        hdr->version, hdr->header_size);
		return -3;
	}
	if (hdr->image_size == 0 || hdr->entry_offset >= hdr->image_size) {
		kprintf("loader: invalid stage2 size=%lu entry_off=%lu\n",
		        hdr->image_size, hdr->entry_offset);
		return -4;
	}

	/* If load_addr is set and points to a pre-placed image, verify from
	 * load_addr; otherwise payload immediately follows the header. */
	const uint8_t *src = (hdr->flags & SCION_S2_FLAG_PIC) && hdr->load_addr
	                     ? (const uint8_t *)(uintptr_t)hdr->load_addr
	                     : (const uint8_t *)(hdr + 1);

	uint32_t calc_crc = crc32(src, (size_t)hdr->image_size);
	if (calc_crc != hdr->crc32) {
		kprintf("loader: CRC32 mismatch! got 0x%08x, expected 0x%08x\n",
		        calc_crc, hdr->crc32);
		return -5;
	}

	if (!(hdr->flags & SCION_S2_FLAG_SKIP_HASH)) {
		uint8_t digest[32];
		sha256(src, (size_t)hdr->image_size, digest);
		if (memcmp(digest, hdr->sha256, 32) != 0) {
			kprintf("loader: SHA-256 verification FAILED!\n");
			return -6;
		}
	}

	uint64_t dst_pa = hdr->load_addr ? hdr->load_addr : (uint64_t)(uintptr_t)src;
	if (dst_pa != (uint64_t)(uintptr_t)src) {
		memmove((void *)(uintptr_t)dst_pa, src, (size_t)hdr->image_size);
		boot_mem_add(bi, dst_pa, hdr->image_size,
		             SCION_MEM_RESERVED | SCION_MEM_STAGE2);
	}

	bi->stage2_base = dst_pa;
	bi->stage2_size = hdr->image_size;
	bi->boot_flags |= SCION_BF_STAGE2_OK;

	kprintf("loader: verified '%s' (%lu bytes @ %p, crc32=0x%08x, sha256=%02x%02x%02x%02x...)\n",
	        hdr->name[0] ? hdr->name : "stage2",
	        hdr->image_size, (void *)dst_pa, calc_crc,
	        hdr->sha256[0], hdr->sha256[1], hdr->sha256[2], hdr->sha256[3]);

	*entry_out = dst_pa + hdr->entry_offset;
	return 0;
}
