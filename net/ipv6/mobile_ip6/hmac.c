/*     Authentication algorithms       
 *	
 *      Authors: 
 *       Alexis Olivereau              <Alexis.Olivereau@crm.mot.com>
 * 
 *      $Id: s.hmac.c 1.14 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      Changes: 
 *      Henrik Petander     :     Cleaned up unused parts
 *
 */

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/in6.h>

#include "hmac.h"
#define LROLL(x, s) (((x) << (s)) | ((x) >> (32 - (s))))

/* MD5 */
#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

#define FF(a, b, c, d, m, s, t) { \
 (a) += F ((b), (c), (d)) + (m) + (t); \
 (a) = LROLL((a), (s)); \
 (a) += (b); \
 }
#define GG(a, b, c, d, m, s, t) { \
 (a) += G ((b), (c), (d)) + (m) + (t); \
 (a) = LROLL((a), (s)); \
 (a) += (b); \
 }
#define HH(a, b, c, d, m, s, t) { \
 (a) += H ((b), (c), (d)) + (m) + (t); \
 (a) = LROLL((a), (s)); \
 (a) += (b); \
 }
#define II(a, b, c, d, m, s, t) { \
 (a) += I ((b), (c), (d)) + (m) + (t); \
 (a) = LROLL((a), (s)); \
 (a) += (b); \
 }

#define s11  7
#define s12 12
#define s13 17
#define s14 22
#define s21  5
#define s22  9
#define s23 14
#define s24 20
#define s31  4
#define s32 11
#define s33 16
#define s34 23
#define s41  6
#define s42 10
#define s43 15
#define s44 21

/* SHA-1 */
#define f(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define g(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define h(x, y, z) ((x) ^ (y) ^ (z))

#define K1 0x5a827999
#define K2 0x6ed9eba1
#define K3 0x8f1bbcdc
#define K4 0xca62c1d6

int ah_hmac_md5_init(struct ah_processing *ahp, u_int8_t *key, u_int32_t key_len)
{
	int i;
	int key_up4;
	uint32_t ipad = 0x36363636;
	uint8_t extkey[64];

	ahp->key_auth = key;
	ahp->key_auth_len = key_len;
	ahp->context = (void *) kmalloc(sizeof(MD5_CTX), GFP_ATOMIC);
	if (ahp->context == NULL)
		return -1;
	md5_init((MD5_CTX *) ahp->context);
	if ((64 * sizeof(uint8_t)) < ahp->key_auth_len) {
		printk("buffer overflow!");
		return -1;
	}
	memcpy(extkey, ahp->key_auth, ahp->key_auth_len);
	if (ahp->key_auth_len % 4) {
		memset(extkey + ahp->key_auth_len, 0,
		       4 - (ahp->key_auth_len % 4));
	}
	key_up4 = ((ahp->key_auth_len + 0x3) & 0xFFFFFFFC) / 4;

	for (i = 0; i < key_up4; i++)
		((uint32_t *) extkey)[i] = ((uint32_t *) extkey)[i] ^ ipad;
	for (i = key_up4; i < 16; i++)
		((uint32_t *) extkey)[i] = ipad;

	md5_compute((MD5_CTX *) ahp->context, extkey, 64);
	return 0;
}

void ah_hmac_md5_loop(struct ah_processing *ahp, void *str, uint32_t len)
{
	md5_compute((MD5_CTX *) ahp->context, str, len);
}

void ah_hmac_md5_result(struct ah_processing *ahp, char *digest)
{
	uint8_t inner[HMAC_MD5_HASH_LEN];
	int i;
	int key_up4;
	uint32_t opad = 0x5c5c5c5c;
	uint8_t extkey[64];

	md5_final((MD5_CTX *) ahp->context, inner);
	md5_init((MD5_CTX *) ahp->context);

	memcpy(extkey, ahp->key_auth, ahp->key_auth_len);
	if (ahp->key_auth_len % 4) {
		memset(extkey + ahp->key_auth_len, 0,
		       4 - (ahp->key_auth_len % 4));
	}
	key_up4 = ((ahp->key_auth_len + 0x3) & 0xFFFFFFFC) / 4;

	for (i = 0; i < key_up4; i++)
		((uint32_t *) extkey)[i] = ((uint32_t *) extkey)[i] ^ opad;
	for (i = key_up4; i < 16; i++)
		((uint32_t *) extkey)[i] = opad;

	md5_compute((MD5_CTX *) ahp->context, extkey, 64);
	md5_compute((MD5_CTX *) ahp->context, inner, HMAC_MD5_HASH_LEN);

	md5_final((MD5_CTX *) ahp->context, digest);

	kfree(ahp->context);
}

int ah_hmac_sha1_init(struct ah_processing *ahp, u_int8_t *key, u_int32_t key_len)
{
	int i;
	int key_up4;
	uint32_t ipad = 0x36363636;
	uint8_t extkey[64];

	ahp->key_auth = key;
	ahp->key_auth_len = key_len;

	ahp->context = (void *) kmalloc(sizeof(SHA1_CTX), GFP_ATOMIC);
	//if (ahp->context == NULL)
	//	return -1;

	sha1_init((SHA1_CTX *) ahp->context);

	memcpy(extkey, ahp->key_auth, ahp->key_auth_len);
	if (ahp->key_auth_len % 4) {
		memset(extkey + ahp->key_auth_len, 0,
		       4 - (ahp->key_auth_len % 4));
	}
	key_up4 = ((ahp->key_auth_len + 0x3) & 0xFFFFFFFC) / 4;

	for (i = 0; i < key_up4; i++)
		((uint32_t *) extkey)[i] = ((uint32_t *) extkey)[i] ^ ipad;
	for (i = key_up4; i < 16; i++)
		((uint32_t *) extkey)[i] = ipad;

	sha1_compute((SHA1_CTX *) ahp->context, extkey, 64);
	return 0;
}

void ah_hmac_sha1_loop(struct ah_processing *ahp, void *str, uint32_t len)
{
	if (!ahp)
		return;
	sha1_compute((SHA1_CTX *) ahp->context, str, len);
}

void ah_hmac_sha1_result(struct ah_processing *ahp, char *digest)
{
	uint8_t inner[HMAC_SHA1_HASH_LEN];
	int i;
	int key_up4;
	uint32_t opad = 0x5c5c5c5c;
	uint8_t extkey[64];

	if (!ahp)
		return;
	sha1_final((SHA1_CTX *) ahp->context, inner);
	sha1_init((SHA1_CTX *) ahp->context);

	memcpy(extkey, ahp->key_auth, ahp->key_auth_len);
	if (ahp->key_auth_len % 4) {
		memset(extkey + ahp->key_auth_len, 0,
		       4 - (ahp->key_auth_len % 4));
	}
	key_up4 = ((ahp->key_auth_len + 0x3) & 0xFFFFFFFC) / 4;

	for (i = 0; i < key_up4; i++)
		((uint32_t *) extkey)[i] = ((uint32_t *) extkey)[i] ^ opad;
	for (i = key_up4; i < 16; i++)
		((uint32_t *) extkey)[i] = opad;

	sha1_compute((SHA1_CTX *) ahp->context, extkey, 64);
	sha1_compute((SHA1_CTX *) ahp->context, inner,
		     HMAC_SHA1_HASH_LEN);

	sha1_final((SHA1_CTX *) ahp->context, digest);

	kfree(ahp->context);
}

void md5_init(MD5_CTX * ctx)
{
	ctx->A = 0x67452301;
	ctx->B = 0xefcdab89;
	ctx->C = 0x98badcfe;
	ctx->D = 0x10325476;
	ctx->buf_cur = ctx->buf;
	ctx->bitlen[0] = ctx->bitlen[1] = 0;
	memset(ctx->buf, 0, 64);
}

void md5_over_block(MD5_CTX * ctx, uint8_t * data)
{
	uint32_t M[16];
	uint32_t a = ctx->A;
	uint32_t b = ctx->B;
	uint32_t c = ctx->C;
	uint32_t d = ctx->D;

	create_M_blocks(M, data);

	/* Round 1 */
	FF(a, b, c, d, M[0], s11, 0xd76aa478);	/*  1 */
	FF(d, a, b, c, M[1], s12, 0xe8c7b756);	/*  2 */
	FF(c, d, a, b, M[2], s13, 0x242070db);	/*  3 */
	FF(b, c, d, a, M[3], s14, 0xc1bdceee);	/*  4 */
	FF(a, b, c, d, M[4], s11, 0xf57c0faf);	/*  5 */
	FF(d, a, b, c, M[5], s12, 0x4787c62a);	/*  6 */
	FF(c, d, a, b, M[6], s13, 0xa8304613);	/*  7 */
	FF(b, c, d, a, M[7], s14, 0xfd469501);	/*  8 */
	FF(a, b, c, d, M[8], s11, 0x698098d8);	/*  9 */
	FF(d, a, b, c, M[9], s12, 0x8b44f7af);	/* 10 */
	FF(c, d, a, b, M[10], s13, 0xffff5bb1);	/* 11 */
	FF(b, c, d, a, M[11], s14, 0x895cd7be);	/* 12 */
	FF(a, b, c, d, M[12], s11, 0x6b901122);	/* 13 */
	FF(d, a, b, c, M[13], s12, 0xfd987193);	/* 14 */
	FF(c, d, a, b, M[14], s13, 0xa679438e);	/* 15 */
	FF(b, c, d, a, M[15], s14, 0x49b40821);	/* 16 */

	/* Round 2 */
	GG(a, b, c, d, M[1], s21, 0xf61e2562);	/* 17 */
	GG(d, a, b, c, M[6], s22, 0xc040b340);	/* 18 */
	GG(c, d, a, b, M[11], s23, 0x265e5a51);	/* 19 */
	GG(b, c, d, a, M[0], s24, 0xe9b6c7aa);	/* 20 */
	GG(a, b, c, d, M[5], s21, 0xd62f105d);	/* 21 */
	GG(d, a, b, c, M[10], s22, 0x02441453);	/* 22 */
	GG(c, d, a, b, M[15], s23, 0xd8a1e681);	/* 23 */
	GG(b, c, d, a, M[4], s24, 0xe7d3fbc8);	/* 24 */
	GG(a, b, c, d, M[9], s21, 0x21e1cde6);	/* 25 */
	GG(d, a, b, c, M[14], s22, 0xc33707d6);	/* 26 */
	GG(c, d, a, b, M[3], s23, 0xf4d50d87);	/* 27 */
	GG(b, c, d, a, M[8], s24, 0x455a14ed);	/* 28 */
	GG(a, b, c, d, M[13], s21, 0xa9e3e905);	/* 29 */
	GG(d, a, b, c, M[2], s22, 0xfcefa3f8);	/* 30 */
	GG(c, d, a, b, M[7], s23, 0x676f02d9);	/* 31 */
	GG(b, c, d, a, M[12], s24, 0x8d2a4c8a);	/* 32 */

	/* Round 3 */
	HH(a, b, c, d, M[5], s31, 0xfffa3942);	/* 33 */
	HH(d, a, b, c, M[8], s32, 0x8771f681);	/* 34 */
	HH(c, d, a, b, M[11], s33, 0x6d9d6122);	/* 35 */
	HH(b, c, d, a, M[14], s34, 0xfde5380c);	/* 36 */
	HH(a, b, c, d, M[1], s31, 0xa4beea44);	/* 37 */
	HH(d, a, b, c, M[4], s32, 0x4bdecfa9);	/* 38 */
	HH(c, d, a, b, M[7], s33, 0xf6bb4b60);	/* 39 */
	HH(b, c, d, a, M[10], s34, 0xbebfbc70);	/* 40 */
	HH(a, b, c, d, M[13], s31, 0x289b7ec6);	/* 41 */
	HH(d, a, b, c, M[0], s32, 0xeaa127fa);	/* 42 */
	HH(c, d, a, b, M[3], s33, 0xd4ef3085);	/* 43 */
	HH(b, c, d, a, M[6], s34, 0x4881d05);	/* 44 */
	HH(a, b, c, d, M[9], s31, 0xd9d4d039);	/* 45 */
	HH(d, a, b, c, M[12], s32, 0xe6db99e5);	/* 46 */
	HH(c, d, a, b, M[15], s33, 0x1fa27cf8);	/* 47 */
	HH(b, c, d, a, M[2], s34, 0xc4ac5665);	/* 48 */

	/* Round 4 */
	II(a, b, c, d, M[0], s41, 0xf4292244);	/* 49 */
	II(d, a, b, c, M[7], s42, 0x432aff97);	/* 50 */
	II(c, d, a, b, M[14], s43, 0xab9423a7);	/* 51 */
	II(b, c, d, a, M[5], s44, 0xfc93a039);	/* 52 */
	II(a, b, c, d, M[12], s41, 0x655b59c3);	/* 53 */
	II(d, a, b, c, M[3], s42, 0x8f0ccc92);	/* 54 */
	II(c, d, a, b, M[10], s43, 0xffeff47d);	/* 55 */
	II(b, c, d, a, M[1], s44, 0x85845dd1);	/* 56 */
	II(a, b, c, d, M[8], s41, 0x6fa87e4f);	/* 57 */
	II(d, a, b, c, M[15], s42, 0xfe2ce6e0);	/* 58 */
	II(c, d, a, b, M[6], s43, 0xa3014314);	/* 59 */
	II(b, c, d, a, M[13], s44, 0x4e0811a1);	/* 60 */
	II(a, b, c, d, M[4], s41, 0xf7537e82);	/* 61 */
	II(d, a, b, c, M[11], s42, 0xbd3af235);	/* 62 */
	II(c, d, a, b, M[2], s43, 0x2ad7d2bb);	/* 63 */
	II(b, c, d, a, M[9], s44, 0xeb86d391);	/* 64 */

	ctx->A += a;
	ctx->B += b;
	ctx->C += c;
	ctx->D += d;
}

void create_M_blocks(uint32_t * M, uint8_t * data)
{
#ifdef HAVE_LITTLE_ENDIAN
	memcpy((uint8_t *) M, data, 64);
#endif				/* HAVE_LITTLE_ENDIAN */

#ifdef HAVE_BIG_ENDIAN
	int i;
	for (i = 0; i < 16; i++, data += 4) {
		((uint8_t *) (&M[i]))[0] = data[3];
		((uint8_t *) (&M[i]))[1] = data[2];
		((uint8_t *) (&M[i]))[2] = data[1];
		((uint8_t *) (&M[i]))[3] = data[0];
	}
#endif				/* HAVE_BIG_ENDIAN */
}

void md5_compute(MD5_CTX * ctx, uint8_t * data, uint32_t len)
{
	uint8_t pos = ((ctx->bitlen[0] >> 3) & 0x3f);

	/* First we update the bit length */
	if ((ctx->bitlen[0] += (len << 3)) < (len << 3))
		ctx->bitlen[1]++;
	ctx->bitlen[1] += (len >> 29);	/* len is expressed in bytes */

	if (pos) {
		/* Buffer is not empty */
		if (64 - pos >= len) {
			memcpy(ctx->buf_cur, data, len);
			ctx->buf_cur += len;
			pos += len;
			if (pos == 64) {
				/* The current block is over */
				md5_over_block(ctx, ctx->buf);
				ctx->buf_cur = ctx->buf;
			}
			return;
		} else {
			memcpy(ctx->buf_cur, data, 64 - pos);
			md5_over_block(ctx, ctx->buf);
			len -= (64 - pos);
			data += (64 - pos);
			ctx->buf_cur = ctx->buf;
		}
	}
	while (len >= 64) {
		md5_over_block(ctx, data);
		len -= 64;
		data += 64;
	}
	if (len) {
		memcpy(ctx->buf_cur, data, len);
		ctx->buf_cur += len;
	}
}

void md5_final(MD5_CTX * ctx, uint8_t * digest)
{
	uint32_t rem_size;
	uint8_t *buf_cur = ctx->buf_cur;
	int i;

	rem_size = 64 - ((ctx->bitlen[0] >> 3) & 0x3f);
	*(buf_cur++) = 0x80;

	if (rem_size > 8 + 1) {
		/* We have enough room in the current block */
		for (i = 0; i < rem_size - 8 - 1; i++) {
			*(buf_cur++) = 0;
		}
	} else {
		/* We do not have enough room and need therefore to add a new
		   64-byte block */
		for (i = 0; i < rem_size - 1; i++) {
			*(buf_cur++) = 0;
		}
		md5_over_block(ctx, ctx->buf);

		buf_cur = ctx->buf;
		for (i = 0; i < 64 - 8; i++) {
			*(buf_cur++) = 0;
		}
	}
#ifdef HAVE_LITTLE_ENDIAN
	memcpy(buf_cur, (uint8_t *) ctx->bitlen, 8);
#endif				/* HAVE_LITTLE_ENDIAN */

#ifdef HAVE_BIG_ENDIAN
	*(buf_cur++) = (ctx->bitlen[0] >> 24) & 0xff;
	*(buf_cur++) = (ctx->bitlen[0] >> 16) & 0xff;
	*(buf_cur++) = (ctx->bitlen[0] >> 8) & 0xff;
	*(buf_cur++) = (ctx->bitlen[0] >> 0) & 0xff;
	*(buf_cur++) = (ctx->bitlen[1] >> 24) & 0xff;
	*(buf_cur++) = (ctx->bitlen[1] >> 16) & 0xff;
	*(buf_cur++) = (ctx->bitlen[1] >> 8) & 0xff;
	*(buf_cur++) = (ctx->bitlen[1] >> 0) & 0xff;
#endif				/* HAVE_BIG_ENDIAN */

	md5_over_block(ctx, ctx->buf);

#ifdef HAVE_LITTLE_ENDIAN
	memcpy(digest + 0, (uint8_t *) (&(ctx->A)), sizeof(uint32_t));
	memcpy(digest + 4, (uint8_t *) (&(ctx->B)), sizeof(uint32_t));
	memcpy(digest + 8, (uint8_t *) (&(ctx->C)), sizeof(uint32_t));
	memcpy(digest + 12, (uint8_t *) (&(ctx->D)), sizeof(uint32_t));
#endif				/* HAVE_LITTLE_ENDIAN */

#ifdef HAVE_BIG_ENDIAN
	digest[0] = ((ctx->A) >> 24) & 0xff;
	digest[1] = ((ctx->A) >> 16) & 0xff;
	digest[2] = ((ctx->A) >> 8) & 0xff;
	digest[3] = ((ctx->A) >> 0) & 0xff;
	digest[4] = ((ctx->B) >> 24) & 0xff;
	digest[5] = ((ctx->B) >> 16) & 0xff;
	digest[6] = ((ctx->B) >> 8) & 0xff;
	digest[7] = ((ctx->B) >> 0) & 0xff;
	digest[8] = ((ctx->C) >> 24) & 0xff;
	digest[9] = ((ctx->C) >> 16) & 0xff;
	digest[10] = ((ctx->C) >> 8) & 0xff;
	digest[11] = ((ctx->C) >> 0) & 0xff;
	digest[12] = ((ctx->D) >> 24) & 0xff;
	digest[13] = ((ctx->D) >> 16) & 0xff;
	digest[14] = ((ctx->D) >> 8) & 0xff;
	digest[15] = ((ctx->D) >> 0) & 0xff;
#endif				/* HAVE_BIG_ENDIAN */
}

void sha1_init(SHA1_CTX * ctx)
{
	ctx->A = 0x67452301;
	ctx->B = 0xefcdab89;
	ctx->C = 0x98badcfe;
	ctx->D = 0x10325476;
	ctx->E = 0xc3d2e1f0;
	ctx->buf_cur = ctx->buf;
	ctx->bitlen[0] = ctx->bitlen[1] = 0;
	memset(ctx->buf, 0, 64);
}

void sha1_over_block(SHA1_CTX * ctx, uint8_t * data)
{
	int i;
	uint32_t W[80];
	uint32_t a = ctx->A;
	uint32_t b = ctx->B;
	uint32_t c = ctx->C;
	uint32_t d = ctx->D;
	uint32_t e = ctx->E;
	uint32_t temp;

	create_W_blocks(W, data);

	/* Round 1 */
	for (i = 0; i < 20; i++) {
		temp = LROLL(a, 5) + f(b, c, d) + e + W[i] + K1;
		e = d;
		d = c;
		c = LROLL(b, 30);
		b = a;
		a = temp;
	}

	/* Round 2 */
	for (i = 20; i < 40; i++) {
		temp = LROLL(a, 5) + h(b, c, d) + e + W[i] + K2;
		e = d;
		d = c;
		c = LROLL(b, 30);
		b = a;
		a = temp;
	}

	/* Round 3 */
	for (i = 40; i < 60; i++) {
		temp = LROLL(a, 5) + g(b, c, d) + e + W[i] + K3;
		e = d;
		d = c;
		c = LROLL(b, 30);
		b = a;
		a = temp;
	}

	/* Round 4 */
	for (i = 60; i < 80; i++) {
		temp = LROLL(a, 5) + h(b, c, d) + e + W[i] + K4;
		e = d;
		d = c;
		c = LROLL(b, 30);
		b = a;
		a = temp;
	}

	ctx->A += a;
	ctx->B += b;
	ctx->C += c;
	ctx->D += d;
	ctx->E += e;
}

void create_W_blocks(uint32_t * W, uint8_t * data)
{
	int i;

#ifdef HAVE_BIG_ENDIAN
	memcpy((uint8_t *) W, data, 64);
#endif				/* HAVE_BIG_ENDIAN */

#ifdef HAVE_LITTLE_ENDIAN
	for (i = 0; i < 16; i++, data += 4) {
		((uint8_t *) (&W[i]))[0] = data[3];
		((uint8_t *) (&W[i]))[1] = data[2];
		((uint8_t *) (&W[i]))[2] = data[1];
		((uint8_t *) (&W[i]))[3] = data[0];
	}
#endif				/* HAVE_LITTLE_ENDIAN */
	for (i = 16; i < 80; i++) {
		W[i] = W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16];
		W[i] = LROLL(W[i], 1);
	}
}

void sha1_compute(SHA1_CTX * ctx, uint8_t * data, uint32_t len)
{
	uint8_t pos = ((ctx->bitlen[0] >> 3) & 0x3f);

	/* First we update the bit length */
	if ((ctx->bitlen[0] += (len << 3)) < (len << 3))
		ctx->bitlen[1]++;
	ctx->bitlen[1] += (len >> 29);	/* len is expressed in bytes */

	if (pos) {
		/* Buffer is not empty */
		if (64 - pos >= len) {
			memcpy(ctx->buf_cur, data, len);
			ctx->buf_cur += len;
			pos += len;
			if (pos == 64) {
				/* The current block is over */
				sha1_over_block(ctx, ctx->buf);
				ctx->buf_cur = ctx->buf;
			}
			return;
		} else {
			memcpy(ctx->buf_cur, data, 64 - pos);
			sha1_over_block(ctx, ctx->buf);
			len -= (64 - pos);
			data += (64 - pos);
			ctx->buf_cur = ctx->buf;
		}
	}
	while (len >= 64) {
		sha1_over_block(ctx, data);
		len -= 64;
		data += 64;
	}
	if (len) {
		memcpy(ctx->buf_cur, data, len);
		ctx->buf_cur += len;
	}
}

void sha1_final(SHA1_CTX * ctx, uint8_t * digest)
{
	uint32_t rem_size;
	uint8_t *buf_cur = ctx->buf_cur;
	int i;

	rem_size = 64 - ((ctx->bitlen[0] >> 3) & 0x3f);
	*(buf_cur++) = 0x80;

	if (rem_size > 8 + 1) {
		/* We have enough room in the current block */
		for (i = 0; i < rem_size - 8 - 1; i++) {
			*(buf_cur++) = 0;
		}
	} else {
		/* We do not have enough room and need therefore to add a new
		   64-byte block */
		for (i = 0; i < rem_size - 1; i++) {
			*(buf_cur++) = 0;
		}
		sha1_over_block(ctx, ctx->buf);

		buf_cur = ctx->buf;
		for (i = 0; i < 64 - 8; i++) {
			*(buf_cur++) = 0;
		}
	}
#ifdef HAVE_BIG_ENDIAN
	memcpy(buf_cur, (uint8_t *) ctx->bitlen, 8);
#endif				/* HAVE_BIG_ENDIAN */

#ifdef HAVE_LITTLE_ENDIAN
	*(buf_cur++) = (ctx->bitlen[1] >> 24) & 0xff;
	*(buf_cur++) = (ctx->bitlen[1] >> 16) & 0xff;
	*(buf_cur++) = (ctx->bitlen[1] >> 8) & 0xff;
	*(buf_cur++) = (ctx->bitlen[1] >> 0) & 0xff;
	*(buf_cur++) = (ctx->bitlen[0] >> 24) & 0xff;
	*(buf_cur++) = (ctx->bitlen[0] >> 16) & 0xff;
	*(buf_cur++) = (ctx->bitlen[0] >> 8) & 0xff;
	*(buf_cur++) = (ctx->bitlen[0] >> 0) & 0xff;
#endif				/* HAVE_LITTLE_ENDIAN */

	sha1_over_block(ctx, ctx->buf);

#ifdef HAVE_BIG_ENDIAN
	memcpy(digest + 0, (uint8_t *) (&(ctx->A)), sizeof(uint32_t));
	memcpy(digest + 4, (uint8_t *) (&(ctx->B)), sizeof(uint32_t));
	memcpy(digest + 8, (uint8_t *) (&(ctx->C)), sizeof(uint32_t));
	memcpy(digest + 12, (uint8_t *) (&(ctx->D)), sizeof(uint32_t));
	memcpy(digest + 16, (uint8_t *) (&(ctx->E)), sizeof(uint32_t));
#endif				/* HAVE_BIG_ENDIAN */

#ifdef HAVE_LITTLE_ENDIAN
	digest[0] = ((ctx->A) >> 24) & 0xff;
	digest[1] = ((ctx->A) >> 16) & 0xff;
	digest[2] = ((ctx->A) >> 8) & 0xff;
	digest[3] = ((ctx->A) >> 0) & 0xff;
	digest[4] = ((ctx->B) >> 24) & 0xff;
	digest[5] = ((ctx->B) >> 16) & 0xff;
	digest[6] = ((ctx->B) >> 8) & 0xff;
	digest[7] = ((ctx->B) >> 0) & 0xff;
	digest[8] = ((ctx->C) >> 24) & 0xff;
	digest[9] = ((ctx->C) >> 16) & 0xff;
	digest[10] = ((ctx->C) >> 8) & 0xff;
	digest[11] = ((ctx->C) >> 0) & 0xff;
	digest[12] = ((ctx->D) >> 24) & 0xff;
	digest[13] = ((ctx->D) >> 16) & 0xff;
	digest[14] = ((ctx->D) >> 8) & 0xff;
	digest[15] = ((ctx->D) >> 0) & 0xff;
	digest[16] = ((ctx->E) >> 24) & 0xff;
	digest[17] = ((ctx->E) >> 16) & 0xff;
	digest[18] = ((ctx->E) >> 8) & 0xff;
	digest[19] = ((ctx->E) >> 0) & 0xff;
#endif				/* HAVE_LITTLE_ENDIAN */
}
