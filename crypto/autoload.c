/*
 * Cryptographic API.
 *
 * Algorithm autoloader.
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
#include <linux/list.h>
#include <linux/string.h>
#include <linux/kmod.h>
#include <linux/crypto.h>
#include "internal.h"

static struct {
	u32 algid;
	char *name;
} alg_modmap[] = {
	{ CRYPTO_ALG_DES,	"des"	},
	{ CRYPTO_ALG_DES3_EDE,	"des"	},
	{ CRYPTO_ALG_MD5,	"md5"	},
	{ CRYPTO_ALG_SHA1,	"sha1"	},
};
#define ALG_MAX_MODMAP 4

void crypto_alg_autoload(u32 algid)
{
	int i;

	for (i = 0; i < ALG_MAX_MODMAP ; i++) {
		if ((alg_modmap[i].algid & CRYPTO_ALG_MASK) == algid) {
			request_module(alg_modmap[i].name);
			break;
		}
	}
	return;
}
