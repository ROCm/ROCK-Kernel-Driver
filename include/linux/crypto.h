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
 * Algorithm masks and types.
 */
#define CRYPTO_ALG_TYPE_MASK		0x000000ff
#define CRYPTO_ALG_TYPE_CIPHER		0x00000001
#define CRYPTO_ALG_TYPE_DIGEST		0x00000002
#define CRYPTO_ALG_TYPE_COMP		0x00000004


/*
 * Transform masks and values (for crt_flags).
 */
#define CRYPTO_TFM_MODE_MASK		0x000000ff
#define CRYPTO_TFM_REQ_MASK		0x000fff00
#define CRYPTO_TFM_RES_MASK		0xfff00000

#define CRYPTO_TFM_MODE_ECB		0x00000001
#define CRYPTO_TFM_MODE_CBC		0x00000002
#define CRYPTO_TFM_MODE_CFB		0x00000004
#define CRYPTO_TFM_MODE_CTR		0x00000008

#define CRYPTO_TFM_REQ_ATOMIC		0x00000100
#define CRYPTO_TFM_REQ_WEAK_KEY		0x00000200

#define CRYPTO_TFM_RES_WEAK_KEY		0x00100000
#define CRYPTO_TFM_RES_BAD_KEY_LEN   	0x00200000
#define CRYPTO_TFM_RES_BAD_KEY_SCHED 	0x00400000
#define CRYPTO_TFM_RES_BAD_BLOCK_LEN 	0x00800000
#define CRYPTO_TFM_RES_BAD_FLAGS 	0x01000000


/*
 * Miscellaneous stuff.
 */
#define CRYPTO_UNSPEC			0
#define CRYPTO_MAX_ALG_NAME		64
#define CRYPTO_MAX_BLOCK_SIZE		16

struct scatterlist;

/*
 * Algorithms: modular crypto algorithm implementations, managed
 * via crypto_register_alg() and crypto_unregister_alg().
 */
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
	int cra_flags;
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

/*
 * Transforms: user-instantiated objects which encapsulate algorithms
 * and core processing logic.  Managed via crypto_alloc_tfm() and
 * crypto_free_tfm(), as well as the various helpers below.
 */
struct crypto_tfm;

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
 * Transform user interface.
 */
 
/*
 * crypto_alloc_tfm() will first attempt to locate an already loaded algorithm.
 * If that fails and the kernel supports dynamically loadable modules, it
 * will then attempt to load a module of the same name or alias.  A refcount
 * is grabbed on the algorithm which is then associated with the new transform.
 *
 * crypto_free_tfm() frees up the transform and any associated resources,
 * then drops the refcount on the associated algorithm.
 */
struct crypto_tfm *crypto_alloc_tfm(char *alg_name, u32 tfm_flags);
void crypto_free_tfm(struct crypto_tfm *tfm);

/*
 * Transform helpers which query the underlying algorithm.
 */
static inline char *crypto_tfm_alg_name(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_name;
}

static inline const char *crypto_tfm_alg_modname(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	
	if (alg->cra_module)
		return alg->cra_module->name;
	else
		return NULL;
}

static inline u32 crypto_tfm_alg_type(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_flags & CRYPTO_ALG_TYPE_MASK;
}

static inline size_t crypto_tfm_alg_keysize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_cipher.cia_keysize;
}

static inline size_t crypto_tfm_alg_ivsize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_cipher.cia_ivsize;
}

static inline size_t crypto_tfm_alg_blocksize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_blocksize;
}

static inline size_t crypto_tfm_alg_digestsize(struct crypto_tfm *tfm)
{
	return tfm->__crt_alg->cra_digest.dia_digestsize;
}

/*
 * API wrappers.
 */
static inline void crypto_digest_init(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_init(tfm);
}

static inline void crypto_digest_update(struct crypto_tfm *tfm,
                                        struct scatterlist *sg, size_t nsg)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_update(tfm, sg, nsg);
}

static inline void crypto_digest_final(struct crypto_tfm *tfm, u8 *out)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_final(tfm, out);
}

static inline void crypto_digest_digest(struct crypto_tfm *tfm,
                                        struct scatterlist *sg,
                                        size_t nsg, u8 *out)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_digest(tfm, sg, nsg, out);
}
                                        
static inline void crypto_digest_hmac(struct crypto_tfm *tfm, u8 *key,
                                      size_t keylen, struct scatterlist *sg,
                                      size_t nsg, u8 *out)
                                      
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_DIGEST);
	tfm->crt_digest.dit_hmac(tfm, key, keylen, sg, nsg, out);
}

static inline int crypto_cipher_setkey(struct crypto_tfm *tfm,
                                       const u8 *key, size_t keylen)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_setkey(tfm, key, keylen);
}

static inline int crypto_cipher_encrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *sg, size_t nsg)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_encrypt(tfm, sg, nsg);
}                                        

static inline int crypto_cipher_decrypt(struct crypto_tfm *tfm,
                                        struct scatterlist *sg, size_t nsg)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	return tfm->crt_cipher.cit_decrypt(tfm, sg, nsg);
}

static inline void crypto_cipher_copy_iv(struct crypto_tfm *tfm,
                                         u8 *src, size_t len)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER);
	memcpy(tfm->crt_cipher.cit_iv, src, len);
}

static inline void crypto_comp_compress(struct crypto_tfm *tfm)
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_COMP);
	tfm->crt_compress.cot_compress(tfm);
}

static inline void crypto_comp_decompress(struct crypto_tfm *tfm) 
{
	BUG_ON(crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_COMP);
	tfm->crt_compress.cot_decompress(tfm);
}

#endif	/* _LINUX_CRYPTO_H */
