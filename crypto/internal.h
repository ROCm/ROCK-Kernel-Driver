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

#include <linux/highmem.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>

static inline void *crypto_kmap(struct crypto_tfm *tfm, struct page *page)
{
	if (tfm->crt_flags & CRYPTO_ATOMIC) {
#ifdef CONFIG_HIGHMEM
		local_bh_disable();
#endif
		return kmap_atomic(page, KM_CRYPTO);
	} else
		return kmap(page);
}

static inline void crypto_kunmap(struct crypto_tfm *tfm,
                                 struct page *page, void *vaddr)
{
	if (tfm->crt_flags & CRYPTO_ATOMIC) {
		kunmap_atomic(vaddr, KM_CRYPTO);
#ifdef CONFIG_HIGHMEM
		local_bh_enable();
#endif
	} else
		kunmap(page);
}

static inline void crypto_yield(struct crypto_tfm *tfm)
{
	if (!(tfm->crt_flags & CRYPTO_ATOMIC))
		cond_resched();
}

void crypto_init_digest_ops(struct crypto_tfm *tfm);
void crypto_init_cipher_ops(struct crypto_tfm *tfm);
void crypto_init_compress_ops(struct crypto_tfm *tfm);

#endif	/* _CRYPTO_INTERNAL_H */

