/*
 * Cryptographic API.
 *
 * Digest operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/scatterlist.h>
#include "internal.h"

static void init(struct crypto_tfm *tfm)
{
	tfm->__crt_alg->cra_digest.dia_init(tfm->crt_ctx);
}

static void update(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg)
{
	unsigned int i;
	
	for (i = 0; i < nsg; i++) {
		char *p = crypto_kmap(sg[i].page) + sg[i].offset;
		tfm->__crt_alg->cra_digest.dia_update(tfm->crt_ctx,
		                                      p, sg[i].length);
		crypto_kunmap(p);
		crypto_yield(tfm);
	}
}

static void final(struct crypto_tfm *tfm, u8 *out)
{
	tfm->__crt_alg->cra_digest.dia_final(tfm->crt_ctx, out);
}

static void digest(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg, u8 *out)
{
	unsigned int i;

	tfm->crt_digest.dit_init(tfm);
		
	for (i = 0; i < nsg; i++) {
		char *p = crypto_kmap(sg[i].page) + sg[i].offset;
		tfm->__crt_alg->cra_digest.dia_update(tfm->crt_ctx,
		                                      p, sg[i].length);
		crypto_kunmap(p);
		crypto_yield(tfm);
	}
	crypto_digest_final(tfm, out);
}

int crypto_init_digest_flags(struct crypto_tfm *tfm, u32 flags)
{
	return crypto_cipher_flags(flags) ? -EINVAL : 0;
}

void crypto_init_digest_ops(struct crypto_tfm *tfm)
{
	struct digest_tfm *ops = &tfm->crt_digest;
	
	ops->dit_init		= init;
	ops->dit_update		= update;
	ops->dit_final		= final;
	ops->dit_digest		= digest;
}
