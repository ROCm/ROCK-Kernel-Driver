/* SHA-512 code by Jean-Luc Cooke <jlcooke@certainkey.com>
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2003 Kyle McMartin <kyle@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/crypto.h>

#include <asm/scatterlist.h>
#include <asm/byteorder.h>

#define SHA384_DIGEST_SIZE 48
#define SHA512_DIGEST_SIZE 64
#define SHA384_HMAC_BLOCK_SIZE  96
#define SHA512_HMAC_BLOCK_SIZE 128

struct sha512_ctx {
	u64 state[8];
	u32 count[4];
	u8 buf[128];
};

static inline u64 Ch(u64 x, u64 y, u64 z)
{
        return ((x & y) ^ (~x & z));
}

static inline u64 Maj(u64 x, u64 y, u64 z)
{
        return ((x & y) ^ (x & z) ^ (y & z));
}

static inline u64 RORu64(u64 x, u64 y)
{
        return (x >> y) | (x << (64 - y));
}

const u64 sha512_K[80] = {
        0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f,
        0xe9b5dba58189dbbc, 0x3956c25bf348b538, 0x59f111f1b605d019,
        0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242,
        0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
        0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
        0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3,
        0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 0x2de92c6f592b0275,
        0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
        0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f,
        0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
        0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc,
        0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
        0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6,
        0x92722c851482353b, 0xa2bfe8a14cf10364, 0xa81a664bbc423001,
        0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
        0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
        0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99,
        0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb,
        0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc,
        0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
        0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915,
        0xc67178f2e372532b, 0xca273eceea26619c, 0xd186b8c721c0c207,
        0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba,
        0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
        0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
        0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a,
        0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
};

#define e0(x)       (RORu64(x,28) ^ RORu64(x,34) ^ RORu64(x,39))
#define e1(x)       (RORu64(x,14) ^ RORu64(x,18) ^ RORu64(x,41))
#define s0(x)       (RORu64(x, 1) ^ RORu64(x, 8) ^ (x >> 7))
#define s1(x)       (RORu64(x,19) ^ RORu64(x,61) ^ (x >> 6))

/* H* initial state for SHA-512 */
#define H0         0x6a09e667f3bcc908
#define H1         0xbb67ae8584caa73b
#define H2         0x3c6ef372fe94f82b
#define H3         0xa54ff53a5f1d36f1
#define H4         0x510e527fade682d1
#define H5         0x9b05688c2b3e6c1f
#define H6         0x1f83d9abfb41bd6b
#define H7         0x5be0cd19137e2179

/* H'* initial state for SHA-384 */
#define HP0 0xcbbb9d5dc1059ed8
#define HP1 0x629a292a367cd507
#define HP2 0x9159015a3070dd17
#define HP3 0x152fecd8f70e5939
#define HP4 0x67332667ffc00b31
#define HP5 0x8eb44a8768581511
#define HP6 0xdb0c2e0d64f98fa7
#define HP7 0x47b5481dbefa4fa4

static inline void LOAD_OP(int I, u64 *W, const u8 *input)
{
        u64 t1  = input[(8*I)  ] & 0xff;
        t1 <<= 8;
        t1 |= input[(8*I)+1] & 0xff;
        t1 <<= 8;
        t1 |= input[(8*I)+2] & 0xff;
        t1 <<= 8;
        t1 |= input[(8*I)+3] & 0xff;
        t1 <<= 8;
        t1 |= input[(8*I)+4] & 0xff;
        t1 <<= 8;
        t1 |= input[(8*I)+5] & 0xff;
        t1 <<= 8;
        t1 |= input[(8*I)+6] & 0xff;
        t1 <<= 8;
        t1 |= input[(8*I)+7] & 0xff;
        W[I] = t1;
}

static inline void BLEND_OP(int I, u64 *W)
{
        W[I] = s1(W[I-2]) + W[I-7] + s0(W[I-15]) + W[I-16];
}

static void
sha512_transform(u64 *state, const u8 *input)
{
	u64 a, b, c, d, e, f, g, h, t1, t2;
	u64 W[80];

	int i;

	/* load the input */
        for (i = 0; i < 16; i++)
                LOAD_OP(i, W, input);

        for (i = 16; i < 80; i++) {
                BLEND_OP(i, W);
        }

	/* load the state into our registers */
	a=state[0];   b=state[1];   c=state[2];   d=state[3];  
	e=state[4];   f=state[5];   g=state[6];   h=state[7];  
  
	/* now iterate */
	for (i=0; i<80; i+=8) {
		t1 = h + e1(e) + Ch(e,f,g) + sha512_K[i  ] + W[i  ];
		t2 = e0(a) + Maj(a,b,c);    d+=t1;    h=t1+t2;
		t1 = g + e1(d) + Ch(d,e,f) + sha512_K[i+1] + W[i+1];
		t2 = e0(h) + Maj(h,a,b);    c+=t1;    g=t1+t2;
		t1 = f + e1(c) + Ch(c,d,e) + sha512_K[i+2] + W[i+2];
		t2 = e0(g) + Maj(g,h,a);    b+=t1;    f=t1+t2;
		t1 = e + e1(b) + Ch(b,c,d) + sha512_K[i+3] + W[i+3];
		t2 = e0(f) + Maj(f,g,h);    a+=t1;    e=t1+t2;
		t1 = d + e1(a) + Ch(a,b,c) + sha512_K[i+4] + W[i+4];
		t2 = e0(e) + Maj(e,f,g);    h+=t1;    d=t1+t2;
		t1 = c + e1(h) + Ch(h,a,b) + sha512_K[i+5] + W[i+5];
		t2 = e0(d) + Maj(d,e,f);    g+=t1;    c=t1+t2;
		t1 = b + e1(g) + Ch(g,h,a) + sha512_K[i+6] + W[i+6];
		t2 = e0(c) + Maj(c,d,e);    f+=t1;    b=t1+t2;
		t1 = a + e1(f) + Ch(f,g,h) + sha512_K[i+7] + W[i+7];
		t2 = e0(b) + Maj(b,c,d);    e+=t1;    a=t1+t2;
	}
  
	state[0] += a; state[1] += b; state[2] += c; state[3] += d;  
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;  

	/* erase our data */
	a = b = c = d = e = f = g = h = t1 = t2 = 0;
	memset(W, 0, 80 * sizeof(u64));
}

static void
sha512_init(void *ctx)
{
        struct sha512_ctx *sctx = ctx;
	sctx->state[0] = H0;
	sctx->state[1] = H1;
	sctx->state[2] = H2;
	sctx->state[3] = H3;
	sctx->state[4] = H4;
	sctx->state[5] = H5;
	sctx->state[6] = H6;
	sctx->state[7] = H7;
	sctx->count[0] = sctx->count[1] = sctx->count[2] = sctx->count[3] = 0;
	memset(sctx->buf, 0, sizeof(sctx->buf));
}

static void
sha384_init(void *ctx)
{
        struct sha512_ctx *sctx = ctx;
        sctx->state[0] = HP0;
        sctx->state[1] = HP1;
        sctx->state[2] = HP2;
        sctx->state[3] = HP3;
        sctx->state[4] = HP4;
        sctx->state[5] = HP5;
        sctx->state[6] = HP6;
        sctx->state[7] = HP7;
        sctx->count[0] = sctx->count[1] = sctx->count[2] = sctx->count[3] = 0;
        memset(sctx->buf, 0, sizeof(sctx->buf));
}

static void
sha512_update(void *ctx, const u8 *data, unsigned int len)
{
        struct sha512_ctx *sctx = ctx;

	unsigned int i, index, part_len;

	/* Compute number of bytes mod 128 */
	index = (unsigned int)((sctx->count[0] >> 3) & 0x7F);
	
	/* Update number of bits */
	if ((sctx->count[0] += (len << 3)) < (len << 3)) {
		if ((sctx->count[1] += 1) < 1)
			if ((sctx->count[2] += 1) < 1)
				sctx->count[3]++;
		sctx->count[1] += (len >> 29);
	}
	
        part_len = 128 - index;
	
	/* Transform as many times as possible. */
	if (len >= part_len) {
		memcpy(&sctx->buf[index], data, part_len);
		sha512_transform(sctx->state, sctx->buf);

		for (i = part_len; i + 127 < len; i+=128)
			sha512_transform(sctx->state, &data[i]);

		index = 0;
	} else {
		i = 0;
	}

	/* Buffer remaining input */
	memcpy(&sctx->buf[index], &data[i], len - i);
}

static void
sha512_final(void *ctx, u8 *hash)
{
        struct sha512_ctx *sctx = ctx;
	
        const static u8 padding[128] = { 0x80, };

        u32 t;
	u64 t2;
        u8 bits[128];
	unsigned int index, pad_len;
	int i, j;

        index = pad_len = t = i = j = 0;
        t2 = 0;

	/* Save number of bits */
	t = sctx->count[0];
	bits[15] = t; t>>=8;
	bits[14] = t; t>>=8;
	bits[13] = t; t>>=8;
	bits[12] = t; 
	t = sctx->count[1];
	bits[11] = t; t>>=8;
	bits[10] = t; t>>=8;
	bits[9 ] = t; t>>=8;
	bits[8 ] = t; 
	t = sctx->count[2];
	bits[7 ] = t; t>>=8;
	bits[6 ] = t; t>>=8;
	bits[5 ] = t; t>>=8;
	bits[4 ] = t; 
	t = sctx->count[3];
	bits[3 ] = t; t>>=8;
	bits[2 ] = t; t>>=8;
	bits[1 ] = t; t>>=8;
	bits[0 ] = t; 

	/* Pad out to 112 mod 128. */
	index = (sctx->count[0] >> 3) & 0x7f;
	pad_len = (index < 112) ? (112 - index) : ((128+112) - index);
	sha512_update(sctx, padding, pad_len);

	/* Append length (before padding) */
	sha512_update(sctx, bits, 16);

	/* Store state in digest */
	for (i = j = 0; i < 8; i++, j += 8) {
		t2 = sctx->state[i];
		hash[j+7] = (char)t2 & 0xff; t2>>=8;
		hash[j+6] = (char)t2 & 0xff; t2>>=8;
		hash[j+5] = (char)t2 & 0xff; t2>>=8;
		hash[j+4] = (char)t2 & 0xff; t2>>=8;
		hash[j+3] = (char)t2 & 0xff; t2>>=8;
		hash[j+2] = (char)t2 & 0xff; t2>>=8;
		hash[j+1] = (char)t2 & 0xff; t2>>=8;
		hash[j  ] = (char)t2 & 0xff;
	}
	
	/* Zeroize sensitive information. */
	memset(sctx, 0, sizeof(struct sha512_ctx));
}

static void sha384_final(void *ctx, u8 *hash)
{
        struct sha512_ctx *sctx = ctx;
        u8 D[64];

        sha512_final(sctx, D);

        memcpy(hash, D, 48);
        memset(D, 0, 64);
}

static struct crypto_alg sha512 = {
        .cra_name       = "sha512",
        .cra_flags      = CRYPTO_ALG_TYPE_DIGEST,
        .cra_blocksize  = SHA512_HMAC_BLOCK_SIZE,
        .cra_ctxsize    = sizeof(struct sha512_ctx),
        .cra_module     = THIS_MODULE,
        .cra_list       = LIST_HEAD_INIT(sha512.cra_list),
        .cra_u          = { .digest = {
                                .dia_digestsize = SHA512_DIGEST_SIZE,
                                .dia_init       = sha512_init,
                                .dia_update     = sha512_update,
                                .dia_final      = sha512_final }
        }
};

static struct crypto_alg sha384 = {
        .cra_name       = "sha384",
        .cra_flags      = CRYPTO_ALG_TYPE_DIGEST,
        .cra_blocksize  = SHA384_HMAC_BLOCK_SIZE,
        .cra_ctxsize    = sizeof(struct sha512_ctx),
        .cra_module     = THIS_MODULE,
        .cra_list       = LIST_HEAD_INIT(sha384.cra_list),
        .cra_u          = { .digest = {
                                .dia_digestsize = SHA384_DIGEST_SIZE,
                                .dia_init       = sha384_init,
                                .dia_update     = sha512_update,
                                .dia_final      = sha384_final }
        }
};

static int __init init(void)
{
        int ret = 0;

        if ((ret = crypto_register_alg(&sha384)) < 0)
                goto out;
        if ((ret = crypto_register_alg(&sha512)) < 0)
                crypto_unregister_alg(&sha384);
out:
        return ret;
}

static void __exit fini(void)
{
        crypto_unregister_alg(&sha384);
        crypto_unregister_alg(&sha512);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-512 and SHA-384 Secure Hash Algorithms");
