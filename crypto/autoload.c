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
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/crypto.h>
#include "internal.h"

/*
 * A far more intelligent version of this is planned.  For now, just
 * try an exact match on the name of the algorithm.
 */
void crypto_alg_autoload(char *name)
{
	request_module(name);
	return;
}
