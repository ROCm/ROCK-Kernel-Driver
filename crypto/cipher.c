/*
 * Cryptographic API.
 *
 * Cipher operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include "internal.h"

typedef void (cryptfn_t)(void *, u8 *, u8 *);
typedef void (procfn_t)(struct crypto_tfm *, u8 *, cryptfn_t, int enc);

static inline void xor_64(u8 *a, const u8 *b)
{
	((u32 *)a)[0] ^= ((u32 *)b)[0];
	((u32 *)a)[1] ^= ((u32 *)b)[1];
}

static inline size_t sglen(struct scatterlist *sg, size_t nsg)
{
	int i;
	size_t n;
	
	for (i = 0, n = 0; i < nsg; i++)
		n += sg[i].length;
			
	return n;
}

/*
 * Do not call this unless the total length of all of the fragments 
 * has been verified as multiple of the block size.
 */
static int copy_chunks(struct crypto_tfm *tfm, u8 *buf,
                       struct scatterlist *sg, int sgidx,
                       int rlen, int *last, int in)
{
	int i, copied, coff, j, aligned;
	size_t bsize = crypto_tfm_blocksize(tfm);

	for (i = sgidx, j = copied = 0, aligned = 0 ; copied < bsize; i++) {
		int len = sg[i].length;
		int clen;
		char *p;

		if (copied) {
			coff = 0;
			clen = min_t(int, len,  bsize - copied);
			
			if (len == bsize - copied)
				aligned = 1;	/* last + right aligned */
				
		} else {
			coff = len - rlen;
			clen = rlen;
		}

		p = crypto_kmap(tfm, sg[i].page) + sg[i].offset + coff;
		
		if (in)
			memcpy(&buf[copied], p, clen);
		else
			memcpy(p, &buf[copied], clen);
		
		crypto_kunmap(tfm, sg[i].page, p);
		*last = aligned ? 0 : clen;
		copied += clen;
	}
	
	return i - sgidx - 2 + aligned;
}

static inline int gather_chunks(struct crypto_tfm *tfm, u8 *buf,
                                struct scatterlist *sg,
                                int sgidx, int rlen, int *last)
{
	return copy_chunks(tfm, buf, sg, sgidx, rlen, last, 1);
}

static inline int scatter_chunks(struct crypto_tfm *tfm, u8 *buf,
                                 struct scatterlist *sg,
                                 int sgidx, int rlen, int *last)
{
	return copy_chunks(tfm, buf, sg, sgidx, rlen, last, 0);
}

/* 
 * Generic encrypt/decrypt wrapper for ciphers.
 *
 * If we find a a remnant at the end of a frag, we have to encrypt or
 * decrypt across possibly multiple page boundaries via a temporary
 * block, then continue processing with a chunk offset until the end
 * of a frag is block aligned.
 *
 * The code is further complicated by having to remap a page after
 * processing a block then yielding.  The data will be offset from the
 * start of page at the scatterlist offset, the chunking offset (coff)
 * and the block offset (boff).
 */
static int crypt(struct crypto_tfm *tfm, struct scatterlist *sg,
                 size_t nsg, cryptfn_t crfn, procfn_t prfn, int enc)
{
	int i, coff;
	size_t bsize = crypto_tfm_blocksize(tfm);
	u8 tmp[CRYPTO_MAX_BLOCK_SIZE];

	if (sglen(sg, nsg) % bsize) {
		tfm->crt_flags |= CRYPTO_BAD_BLOCK_LEN;
		return -EINVAL;
	}

	for (i = 0, coff = 0; i < nsg; i++) {
		int n = 0, boff = 0;
		int len = sg[i].length - coff;
		char *p = crypto_kmap(tfm, sg[i].page) + sg[i].offset + coff;

		while (len) {
			if (len < bsize) {
				crypto_kunmap(tfm, sg[i].page, p);
				n = gather_chunks(tfm, tmp, sg, i, len, &coff);
				prfn(tfm, tmp, crfn, enc);
				scatter_chunks(tfm, tmp, sg, i, len, &coff);
				crypto_yield(tfm);
				goto unmapped;
			} else {
				prfn(tfm, p, crfn, enc);
				crypto_kunmap(tfm, sg[i].page, p);
				crypto_yield(tfm);
				
				/* remap and point to recalculated offset */
				boff += bsize;
				p = crypto_kmap(tfm, sg[i].page)
					+ sg[i].offset + coff + boff;
				
				len -= bsize;
				
				/* End of frag with no remnant? */
				if (coff && len == 0)
					coff = 0;
			}
		}
		crypto_kunmap(tfm, sg[i].page, p);	
unmapped:
		i += n;

	}
	return 0;
}

static void cbc_process(struct crypto_tfm *tfm,
                        u8 *block, cryptfn_t fn, int enc)
{
	if (enc) {
		xor_64(tfm->crt_cipher.cit_iv, block);
		fn(tfm->crt_ctx, block, tfm->crt_cipher.cit_iv);
		memcpy(tfm->crt_cipher.cit_iv, block,
		       crypto_tfm_blocksize(tfm));
	} else {
		u8 buf[CRYPTO_MAX_BLOCK_SIZE];
		
		fn(tfm->crt_ctx, buf, block);
		xor_64(buf, tfm->crt_cipher.cit_iv);
		memcpy(tfm->crt_cipher.cit_iv, block,
		       crypto_tfm_blocksize(tfm));
		memcpy(block, buf, crypto_tfm_blocksize(tfm));
	}
}

static void ecb_process(struct crypto_tfm *tfm, u8 *block,
                        cryptfn_t fn, int enc)
{
	fn(tfm->crt_ctx, block, block);
}

static int setkey(struct crypto_tfm *tfm, const u8 *key, size_t keylen)
{
	return tfm->__crt_alg->cra_cipher.cia_setkey(tfm->crt_ctx, key,
	                                             keylen, &tfm->crt_flags);
}

static int ecb_encrypt(struct crypto_tfm *tfm,
                       struct scatterlist *sg, size_t nsg)
{
	return crypt(tfm, sg, nsg,
	             tfm->__crt_alg->cra_cipher.cia_encrypt, ecb_process, 1);
}

static int ecb_decrypt(struct crypto_tfm *tfm,
                       struct scatterlist *sg, size_t nsg)
{
	return crypt(tfm, sg, nsg,
	             tfm->__crt_alg->cra_cipher.cia_decrypt, ecb_process, 1);
}

static int cbc_encrypt(struct crypto_tfm *tfm,
                       struct scatterlist *sg, size_t nsg)
{
	return crypt(tfm, sg, nsg,
	             tfm->__crt_alg->cra_cipher.cia_encrypt, cbc_process, 1);
}

static int cbc_decrypt(struct crypto_tfm *tfm,
                       struct scatterlist *sg, size_t nsg)
{
	return crypt(tfm, sg, nsg,
	             tfm->__crt_alg->cra_cipher.cia_decrypt, cbc_process, 0);
}

static int nocrypt(struct crypto_tfm *tfm, struct scatterlist *sg, size_t nsg)
{
	return -ENOSYS;
}

void crypto_init_cipher_ops(struct crypto_tfm *tfm)
{
	struct cipher_tfm *ops = &tfm->crt_cipher;

	ops->cit_setkey = setkey;

	switch (tfm->crt_cipher.cit_mode) {
	case CRYPTO_MODE_ECB:
		ops->cit_encrypt = ecb_encrypt;
		ops->cit_decrypt = ecb_decrypt;
		break;
		
	case CRYPTO_MODE_CBC:
		ops->cit_encrypt = cbc_encrypt;
		ops->cit_decrypt = cbc_decrypt;
		break;
		
	case CRYPTO_MODE_CFB:
		ops->cit_encrypt = nocrypt;
		ops->cit_decrypt = nocrypt;
		break;
	
	case CRYPTO_MODE_CTR:
		ops->cit_encrypt = nocrypt;
		ops->cit_decrypt = nocrypt;
		break;

	default:
		BUG();
	}
}
