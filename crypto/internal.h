/*
 * Cryptographic API.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#ifndef _CRYPTO_INTERNAL_H
#define _CRYPTO_INTERNAL_H

#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

static inline void *crypto_kmap(struct crypto_tfm *tfm, struct page *page)
{
	if (tfm->crt_flags & CRYPTO_TFM_REQ_ATOMIC) {
#ifdef CONFIG_HIGHMEM	/* XXX: remove this after the api change */
		local_bh_disable();
#endif
		return kmap_atomic(page, KM_CRYPTO_SOFTIRQ);
	} else
		return kmap_atomic(page, KM_CRYPTO_USER);
}

static inline void crypto_kunmap(struct crypto_tfm *tfm, void *vaddr)
{
	if (tfm->crt_flags & CRYPTO_TFM_REQ_ATOMIC) {
		kunmap_atomic(vaddr, KM_CRYPTO_SOFTIRQ);
#ifdef CONFIG_HIGHMEM	/* XXX: remove this after the api change */
		local_bh_enable();
#endif
	} else
		kunmap_atomic(vaddr, KM_CRYPTO_USER);
}

static inline void crypto_yield(struct crypto_tfm *tfm)
{
	if (!(tfm->crt_flags & CRYPTO_TFM_REQ_ATOMIC))
		cond_resched();
}

static inline int crypto_cipher_flags(u32 flags)
{
	return flags & (CRYPTO_TFM_MODE_MASK|CRYPTO_TFM_REQ_WEAK_KEY);
}

#ifdef CONFIG_KMOD
void crypto_alg_autoload(char *name);
#endif

int crypto_init_digest_flags(struct crypto_tfm *tfm, u32 flags);
int crypto_init_cipher_flags(struct crypto_tfm *tfm, u32 flags);
int crypto_init_compress_flags(struct crypto_tfm *tfm, u32 flags);

void crypto_init_digest_ops(struct crypto_tfm *tfm);
void crypto_init_cipher_ops(struct crypto_tfm *tfm);
void crypto_init_compress_ops(struct crypto_tfm *tfm);

#endif	/* _CRYPTO_INTERNAL_H */

