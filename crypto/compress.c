/*
 * Cryptographic API.
 *
 * Compression operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include "internal.h"

/*
 * This code currently implements blazingly fast and
 * lossless Quadruple ROT13 compression.
 */
static void crypto_compress(struct crypto_tfm *tfm)
{
	return;
}

static void crypto_decompress(struct crypto_tfm *tfm)
{
	return;
}

int crypto_init_compress_flags(struct crypto_tfm *tfm, u32 flags)
{
	return crypto_cipher_flags(flags) ? -EINVAL : 0;
}

void crypto_init_compress_ops(struct crypto_tfm *tfm)
{
	struct compress_tfm *ops = &tfm->crt_compress;
	
	ops->cot_compress = crypto_compress;
	ops->cot_decompress = crypto_decompress;
}
