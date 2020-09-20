/*  Written in 2016 by David Blackman and Sebastiano Vigna (vigna@acm.org)

To the extent possible under law, the author has dedicated all copyright
and related and neighboring rights to this software to the public domain
worldwide. This software is distributed without any warranty.

See <http://creativecommons.org/publicdomain/zero/1.0/>. */

#include <stdint.h>
#include <stdlib.h>

/* This is the successor to xorshift128+. It is the fastest full-period
   generator passing BigCrush without systematic failures, but due to the
   relatively short period it is acceptable only for applications with a
   mild amount of parallelism; otherwise, use a xorshift1024* generator.

   Beside passing BigCrush, this generator passes the PractRand test suite
   up to (and included) 16TB, with the exception of binary rank tests, as
   the lowest bit of this generator is an LSFR. The next bit is not an
   LFSR, but in the long run it will fail binary rank tests, too. The
   other bits have no LFSR artifacts.

   We suggest to use a sign test to extract a random Boolean value.

   Note that the generator uses a simulated rotate operation, which most C
   compilers will turn into a single instruction. In Java, you can use
   Long.rotateLeft(). In languages that do not make low-level rotation
   instructions accessible xorshift128+ could be faster.

   The state must be seeded so that it is not everywhere zero. If you have
   a 64-bit seed, we suggest to seed a splitmix64 generator and use its
   output to fill s. */

/* Modified for dbbench by Howard Chu 2017 */

#define ROTL(x, k)	((x << k) | (x >> (64 - k)))

struct rndctx {
	uint64_t s[2];
};

uint64_t DBB_random(struct rndctx *ctx) {
	uint64_t *s = ctx->s;
	const uint64_t s0 = s[0];
	uint64_t s1 = s[1];
	const uint64_t result = s0 + s1;

	s1 ^= s0;
	s[0] = ROTL(s0, 55) ^ s1 ^ (s1 << 14); // a, b
	s[1] = ROTL(s1, 36); // c

	return result;
}

/* This is the jump function for the generator. It is equivalent
   to 2^64 calls to next(); it can be used to generate 2^64
   non-overlapping subsequences for parallel computations. */

void DBB_randjump(struct rndctx *ctx1, struct rndctx *ctx2) {
	uint64_t *s = ctx2->s;
	static const uint64_t JUMP[] = { 0xbeac0467eba5facb, 0xd86b048b86aa9922 };

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	*ctx2 = *ctx1;
	for(int i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
		for(int b = 0; b < 64; b++) {
			if (JUMP[i] & UINT64_C(1) << b) {
				s0 ^= s[0];
				s1 ^= s[1];
			}
			DBB_random(ctx2);
		}

	s[0] = s0;
	s[1] = s1;
}

void DBB_srandom(struct rndctx *ctx, uint64_t seed) {
	uint64_t *s = ctx->s;
	s[0] = seed;
	s[1] = seed + 0x7fffff;
}

struct rndctx *DBB_randctx() {
	return malloc(sizeof(struct rndctx));
}
