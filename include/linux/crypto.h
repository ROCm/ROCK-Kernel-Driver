/*
 * Scatterlist Cryptographic API.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 David S. Miller (davem@redhat.com)
 *
 * Portions derived from Cryptoapi, by Alexander Kjeldaas <astor@fast.no>
 * and Nettle, by Niels Möller.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#ifndef _LINUX_CRYPTO_H
#define _LINUX_CRYPTO_H

/* 
 * Crypto context flags.
 */
#define CRYPTO_WEAK_KEY_CHECK	0x0001
#define CRYPTO_WEAK_KEY		0x0008
#define CRYPTO_BAD_KEY_LEN	0x0010
#define CRYPTO_BAD_KEY_SCHED	0x0020
#define CRYPTO_BAD_BLOCK_LEN	0x0040
#define CRYPTO_ATOMIC		0x1000

/*
 * Algorithm identifiers.  These may be expanded later to 64 bits
 * and include vendor id info, so we can have multiple versions
 * (e.g. asm, various hardware versions etc).
 *
 * Todo: sadb translation.
 */
#define CRYPTO_TYPE_MASK        0xf0000000
#define CRYPTO_MODE_MASK        0x0ff00000
#define CRYPTO_ALG_MASK		0x000fffff

#define CRYPTO_TYPE_CIPHER	0x10000000
#define CRYPTO_TYPE_DIGEST	0x20000000
#define CRYPTO_TYPE_COMP	0x40000000

#define CRYPTO_MODE_ECB		0x00100000
#define CRYPTO_MODE_CBC		0x00200000
#define CRYPTO_MODE_CFB		0x00400000
#define CRYPTO_MODE_CTR		0x00800000

#define CRYPTO_ALG_NULL		0x00000001

#define CRYPTO_ALG_DES		(0x00000002|CRYPTO_TYPE_CIPHER)
#define CRYPTO_ALG_DES_ECB	(CRYPTO_ALG_DES|CRYPTO_MODE_ECB)
#define CRYPTO_ALG_DES_CBC	(CRYPTO_ALG_DES|CRYPTO_MODE_CBC)

#define CRYPTO_ALG_DES3_EDE	(0x00000003|CRYPTO_TYPE_CIPHER)
#define CRYPTO_ALG_DES3_EDE_ECB	(CRYPTO_ALG_DES3_EDE|CRYPTO_MODE_ECB)
#define CRYPTO_ALG_DES3_EDE_CBC	(CRYPTO_ALG_DES3_EDE|CRYPTO_MODE_CBC)

#define CRYPTO_ALG_MD4		(0x00000f00|CRYPTO_TYPE_DIGEST)
#define CRYPTO_ALG_MD5		(0x00000f01|CRYPTO_TYPE_DIGEST)
#define CRYPTO_ALG_SHA1		(0x00000f02|CRYPTO_TYPE_DIGEST)

#define CRYPTO_UNSPEC		0
#define CRYPTO_MAX_ALG_NAME	64
#define CRYPTO_MAX_BLOCK_SIZE	16

struct scatterlist;

struct cipher_alg {
	size_t cia_keysize;
	size_t cia_ivsize;
	int (*cia_setkey)(void *ctx, const u8 *key, size_t keylen, int *flags);
	void (*cia_encrypt)(void *ctx, u8 *dst, u8 *src);
	void (*cia_decrypt)(void *ctx, u8 *dst, u8 *src);
};

struct digest_alg {
	size_t dia_digestsize;
	void (*dia_init)(void *ctx);
	void (*dia_update)(void *ctx, const u8 *data, size_t len);
	void (*dia_final)(void *ctx, u8 *out);
};

struct compress_alg {
	void (*coa_compress)(void);
	void (*coa_decompress)(void);
};

#define cra_cipher	cra_u.cipher
#define cra_digest	cra_u.digest
#define cra_compress	cra_u.compress

struct crypto_alg {
	struct list_head cra_list;
	u32 cra_id;
	size_t cra_blocksize;
	size_t cra_ctxsize;
	char cra_name[CRYPTO_MAX_ALG_NAME];

	union {
		struct cipher_alg cipher;
		struct digest_alg digest;
		struct compress_alg compress;
	} cra_u;
	
	struct module *cra_module;
};

/*
 * Algorithm registration interface.
 */
int crypto_register_alg(struct crypto_alg *alg);
int crypto_unregister_alg(struct crypto_alg *alg);

struct crypto_tfm;

/*
 * Transformations: user-instantiated algorithms.
 */
struct cipher_tfm {
	void *cit_iv;
	u32 cit_mode;
	int (*cit_setkey)(struct crypto_tfm *tfm, const u8 *key, size_t keylen);
	int (*cit_encrypt)(struct crypto_tfm *tfm,
	                   struct scatterlist *sg, size_t nsg);
	int (*cit_decrypt)(struct crypto_tfm *tfm,
	                   struct scatterlist *sg, size_t nsg);
};

struct digest_tfm {
	void (*dit_init)(struct crypto_tfm *tfm);
	void (*dit_update)(struct crypto_tfm *tfm,
	                   struct scatterlist *sg, size_t nsg);
	void (*dit_final)(struct crypto_tfm *tfm, u8 *out);
	void (*dit_digest)(struct crypto_tfm *tfm, struct scatterlist *sg,
	                   size_t nsg, u8 *out);
	void (*dit_hmac)(struct crypto_tfm *tfm, u8 *key, size_t keylen,
	                 struct scatterlist *sg, size_t nsg, u8 *out);
};

struct compress_tfm {
	void (*cot_compress)(struct crypto_tfm *tfm);
	void (*cot_decompress)(struct crypto_tfm *tfm);
};

#define crt_cipher	crt_u.cipher
#define crt_digest	crt_u.digest
#define crt_compress	crt_u.compress

struct crypto_tfm {

	void *crt_ctx;
	int crt_flags;
	
	union {
		struct cipher_tfm cipher;
		struct digest_tfm digest;
		struct compress_tfm compress;
	} crt_u;
	
	struct crypto_alg *__crt_alg;
};

/*
 * Finds specified algorithm, allocates and returns a transform for it.
 * Will try an load a module based on the name if not present
 * in the kernel.  Increments its algorithm refcount.
 */
struct crypto_tfm *crypto_alloc_tfm(u32 id);

/*
 * Frees the transform and decrements its algorithm's recount.
 */
void crypto_free_tfm(struct crypto_tfm *tfm);

/*
 * API wrappers.
 */
static inline void crypto_digest_init(struct crypto_tfm *tfm)
{
	tfm->crt_digest.dit_init(tfm);
}

static inline void crypto_digest_update(struct crypto_tfm *tfm,
                                        struct scatterlist *sg, size_t nsg)
{
	tfm->crt_digest.dit_update(tfm, sg, nsg);
}

static inline void crypto_digest_final(struct crypto_tfm *tfm, u8 *out)
{
	tfm->crt_digest.dit_final(tfm, out);
}

static inline void crypto_digest_digest(struct crypto_tfm *tfm,
                                        struct scatterlist *sg,
                                        size_t nsg, u8 *out)
{
	tfm->crt_digest.dit_digest(tfm, sg, nsg, out);
}
                                        
static inline void crypto_digest_hmac(struct crypto_tfm *tfm, u8 *key,
                                      size_t keylen, struct scatterlist *sg,
                                      size_t nsg, u8 *out)
                                      
{
	tfm->crt_digest.dit_hmac(tfm, key, keylen, sg, nsg, out);
}

static inline int crypto_cipher_setkey(struct crypto_tfm *tfm,
                                       const u8 *key, size_t keylen)
{
	return tfm->crt_cipher.cit_setkey(tfm, key, keylen);
}

static inline int crypto_cipher_encrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *sg, size_t nsg)
{
	return tfm->crt_cipher.cit_encrypt(tfm, sg, nsg);
}                                        

static inline int crypto_cipher_decrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *sg, size_t nsg)
{
	return tfm->crt_cipher.cit_decrypt(tfm, sg, nsg);
}

static inline void crypto_cipher_copy_iv(struct crypto_tfm *tfm,
                                         u8 *src, size_t len)
{
	memcpy(tfm->crt_cipher.cit_iv, src, len);
}

static inline void crypto_comp_compress(struct crypto_tfm *tfm)
{
	tfm->crt_compress.cot_compress(tfm);
}

static inline void crypto_comp_decompress(struct crypto_tfm *tfm) 
{
	tfm->crt_compress.cot_decompress(tfm);
}

/*
 * Transform helpers which allow the underlying algorithm to be queried.
 */
static inline int crypto_tfm_id(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_id;
}

static inline int crypto_tfm_alg(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_id & CRYPTO_ALG_MASK;
}

static inline char *crypto_tfm_name(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_name;
}

static inline u32 crypto_tfm_type(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_id & CRYPTO_TYPE_MASK;
}

static inline size_t crypto_tfm_keysize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_cipher.cia_keysize;
}

static inline size_t crypto_tfm_ivsize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_cipher.cia_ivsize;
}

static inline size_t crypto_tfm_blocksize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_blocksize;
}

static inline size_t crypto_tfm_digestsize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_digest.dia_digestsize;
}

#endif	/* _LINUX_CRYPTO_H */
