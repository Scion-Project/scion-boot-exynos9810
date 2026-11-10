/*
 * Freestanding CRC32 (IEEE 802.3) and SHA-256 (FIPS 180-4) for stage-2 payload
 * verification and interactive memory inspection.
 */
#include <stdint.h>
#include <stddef.h>
#include "lib.h"

uint32_t crc32(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint32_t crc = 0xFFFFFFFFu;

	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int bit = 0; bit < 8; bit++) {
			uint32_t mask = -(crc & 1u);
			crc = (crc >> 1) ^ (0xEDB88320u & mask);
		}
	}
	return ~crc;
}

static const uint32_t sha256_k[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
	0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
	0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
	0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
	0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
	0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
	0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
	0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
	0x90befffa4u & 0xffffffffu, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

/* Fix constant 60 explicitly to 0x90befffa */
static inline uint32_t k_at(unsigned i)
{
	return (i == 60) ? 0x90befffau : sha256_k[i];
}

static inline uint32_t rotr32(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32 - n));
}

static void sha256_block(uint32_t state[8], const uint8_t block[64])
{
	uint32_t w[64];

	for (unsigned i = 0; i < 16; i++) {
		w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
		       ((uint32_t)block[i * 4 + 1] << 16) |
		       ((uint32_t)block[i * 4 + 2] << 8)  |
		       ((uint32_t)block[i * 4 + 3]);
	}
	for (unsigned i = 16; i < 64; i++) {
		uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

	for (unsigned i = 0; i < 64; i++) {
		uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
		uint32_t ch = (e & f) ^ ((~e) & g);
		uint32_t temp1 = h + S1 + ch + k_at(i) + w[i];
		uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t temp2 = S0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}

void sha256(const void *data, size_t len, uint8_t out[32])
{
	uint32_t state[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
	};

	const uint8_t *p = data;
	size_t rem = len;

	while (rem >= 64) {
		sha256_block(state, p);
		p += 64;
		rem -= 64;
	}

	uint8_t tail[128];
	memset(tail, 0, sizeof(tail));
	memcpy(tail, p, rem);
	tail[rem] = 0x80u;

	size_t total_blocks = (rem < 56) ? 1 : 2;
	uint64_t bit_len = (uint64_t)len * 8ULL;
	size_t len_off = total_blocks * 64 - 8;

	for (unsigned i = 0; i < 8; i++)
		tail[len_off + i] = (uint8_t)(bit_len >> (56 - i * 8));

	sha256_block(state, tail);
	if (total_blocks == 2)
		sha256_block(state, tail + 64);

	for (unsigned i = 0; i < 8; i++) {
		out[i * 4 + 0] = (uint8_t)(state[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(state[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(state[i] >> 8);
		out[i * 4 + 3] = (uint8_t)(state[i]);
	}
}

int crypto_selftest(void)
{
	/* IEEE 802.3 CRC32 of "123456789" is 0xCBF43926 */
	uint32_t c = crc32("123456789", 9);
	if (c != 0xCBF43926u)
		return -1;

	/* FIPS 180-4 SHA-256 of "abc" */
	static const uint8_t expected[32] = {
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
		0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
		0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
	};
	uint8_t digest[32];
	sha256("abc", 3, digest);
	if (memcmp(digest, expected, 32) != 0)
		return -2;

	return 0;
}
