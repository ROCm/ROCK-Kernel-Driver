/* 
 * xfrm algorithm interface
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 */
#include <linux/kernel.h>
#include <linux/pfkeyv2.h>
#include <net/xfrm.h>

/*
 * Algorithms supported by IPsec.  These entries contain properties which
 * are used in key negotiation and xfrm processing, and are used to verify
 * that instantiated crypto transforms have correct parameters for IPsec
 * purposes.
 */
static struct xfrm_algo_desc aalg_list[] = {
{
	.name = "digest_null",
	
	.uinfo = {
		.auth = {
			.icv_truncbits = 0,
			.icv_fullbits = 0,
		}
	},
	
	.desc = {
		.sadb_alg_id = SADB_X_AALG_NULL,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 0,
		.sadb_alg_maxbits = 0
	}
},
{
	.name = "md5",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 128,
		}
	},
	
	.desc = {
		.sadb_alg_id = SADB_AALG_MD5HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 128
	}
},
{
	.name = "sha1",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 160,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_AALG_SHA1HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 160,
		.sadb_alg_maxbits = 160
	}
},
{
	.name = "sha256",

	.uinfo = {
		.auth = {
			.icv_truncbits = 128,
			.icv_fullbits = 256,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_AALG_SHA2_256HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 256,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "ripemd160",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 160,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_AALG_RIPEMD160HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 160,
		.sadb_alg_maxbits = 160
	}
},
};

static struct xfrm_algo_desc ealg_list[] = {
{
	.name = "cipher_null",
	
	.uinfo = {
		.encr = {
			.blockbits = 8,
			.defkeybits = 0,
		}
	},
	
	.desc = {
		.sadb_alg_id =	SADB_EALG_NULL,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 0,
		.sadb_alg_maxbits = 0
	}
},
{
	.name = "des",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 64,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_EALG_DESCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 64,
		.sadb_alg_maxbits = 64
	}
},
{
	.name = "des3_ede",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 192,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_EALG_3DESCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 192,
		.sadb_alg_maxbits = 192
	}
},
{
	.name = "cast128",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_EALG_CASTCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 40,
		.sadb_alg_maxbits = 128
	}
},
{
	.name = "blowfish",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_EALG_BLOWFISHCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 40,
		.sadb_alg_maxbits = 448
	}
},
{
	.name = "aes",

	.uinfo = {
		.encr = {
			.blockbits = 128,
			.defkeybits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AESCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
};

static inline int aalg_entries(void)
{
	return sizeof(aalg_list) / sizeof(aalg_list[0]);
}

static inline int ealg_entries(void)
{
	return sizeof(ealg_list) / sizeof(ealg_list[0]);
}

struct xfrm_algo_desc *xfrm_aalg_get_byid(int alg_id)
{
	int i;

	for (i = 0; i < aalg_entries(); i++) {
		if (aalg_list[i].desc.sadb_alg_id == alg_id) {
			if (aalg_list[i].available)
				return &aalg_list[i];
			else
				break;
		}
	}
	return NULL;
}

struct xfrm_algo_desc *xfrm_ealg_get_byid(int alg_id)
{
	int i;

	for (i = 0; i < ealg_entries(); i++) {
		if (ealg_list[i].desc.sadb_alg_id == alg_id) {
			if (ealg_list[i].available)
				return &ealg_list[i];
			else
				break;
		}
	}
	return NULL;
}

struct xfrm_algo_desc *xfrm_aalg_get_byname(char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i=0; i < aalg_entries(); i++) {
		if (strcmp(name, aalg_list[i].name) == 0) {
			if (aalg_list[i].available)
				return &aalg_list[i];
			else
				break;
		}
	}
	return NULL;
}

struct xfrm_algo_desc *xfrm_ealg_get_byname(char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i=0; i < ealg_entries(); i++) {
		if (strcmp(name, ealg_list[i].name) == 0) {
			if (ealg_list[i].available)
				return &ealg_list[i];
			else
				break;
		}
	}
	return NULL;
}

struct xfrm_algo_desc *xfrm_aalg_get_byidx(unsigned int idx)
{
	if (idx >= aalg_entries())
		return NULL;

	return &aalg_list[idx];
}

struct xfrm_algo_desc *xfrm_ealg_get_byidx(unsigned int idx)
{
	if (idx >= ealg_entries())
		return NULL;

	return &ealg_list[idx];
}

/*
 * Probe for the availability of crypto algorithms, and set the available
 * flag for any algorithms found on the system.  This is typically called by
 * pfkey during userspace SA add, update or register.
 */
void xfrm_probe_algs(void)
{
	int i, status;
	
	BUG_ON(in_softirq());

#ifdef CONFIG_CRYPTO
	for (i = 0; i < aalg_entries(); i++) {
		status = crypto_alg_available(aalg_list[i].name, 0);
		if (aalg_list[i].available != status)
			aalg_list[i].available = status;
	}
	
	for (i = 0; i < ealg_entries(); i++) {
		status = crypto_alg_available(ealg_list[i].name, 0);
		if (ealg_list[i].available != status)
			ealg_list[i].available = status;
	}
#endif
}

int xfrm_count_auth_supported(void)
{
	int i, n;

	for (i = 0, n = 0; i < aalg_entries(); i++)
		if (aalg_list[i].available)
			n++;
	return n;
}

int xfrm_count_enc_supported(void)
{
	int i, n;

	for (i = 0, n = 0; i < ealg_entries(); i++)
		if (ealg_list[i].available)
			n++;
	return n;
}
