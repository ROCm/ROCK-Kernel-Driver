// SPDX-License-Identifier: GPL-2.0-or-later
/* Module signature checker
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/module_signature.h>
#include <linux/string.h>
#include <linux/verification.h>
#include <crypto/public_key.h>
#include <crypto/hash.h>
#include <keys/system_keyring.h>
#include "module-internal.h"

static int mod_is_hash_blacklisted(const void *mod, size_t verifylen)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t digest_size, desc_size;
	u8 *digest;
	int ret = 0;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm))
		goto error_return;

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	digest_size = crypto_shash_digestsize(tfm);
	digest = kzalloc(digest_size + desc_size, GFP_KERNEL);
	if (!digest) {
		pr_err("digest memory buffer allocate fail\n");
		ret = -ENOMEM;
		goto error_digest;
	}
	desc = (void *)digest + digest_size;
	desc->tfm = tfm;
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error_shash;

	ret = crypto_shash_finup(desc, mod, verifylen, digest);
	if (ret < 0)
		goto error_shash;

	pr_debug("%ld digest: %*phN\n", verifylen, (int) digest_size, digest);

	ret = is_hash_blacklisted(digest, digest_size, "bin");
	if (ret == -EKEYREJECTED)
		pr_err("Module hash %*phN is blacklisted\n",
		       (int) digest_size, digest);

error_shash:
	kfree(digest);
error_digest:
	crypto_free_shash(tfm);
error_return:
	return ret;
}

/*
 * Verify the signature on a module.
 */
int mod_verify_sig(const void *mod, struct load_info *info)
{
	struct module_signature ms;
	size_t modlen = info->len, sig_len, wholelen;
	int ret;

	pr_devel("==>%s(,%zu)\n", __func__, modlen);

	if (modlen <= sizeof(ms))
		return -EBADMSG;

	wholelen = modlen + sizeof(MODULE_SIG_STRING) - 1;
	memcpy(&ms, mod + (modlen - sizeof(ms)), sizeof(ms));

	ret = mod_check_sig(&ms, modlen, info->name);
	if (ret)
		return ret;

	sig_len = be32_to_cpu(ms.sig_len);
	modlen -= sig_len + sizeof(ms);
	info->len = modlen;

	ret = verify_pkcs7_signature(mod, modlen, mod + modlen, sig_len,
				     VERIFY_USE_SECONDARY_KEYRING,
				     VERIFYING_MODULE_SIGNATURE,
				     NULL, NULL);
	if (ret == -ENOKEY && IS_ENABLED(CONFIG_INTEGRITY_PLATFORM_KEYRING)) {
		ret = verify_pkcs7_signature(mod, modlen, mod + modlen, sig_len,
					     VERIFY_USE_PLATFORM_KEYRING,
					     VERIFYING_MODULE_SIGNATURE,
					     NULL, NULL);
	}
	pr_devel("verify_pkcs7_signature() = %d\n", ret);

	/* checking hash of module is in blacklist */
	if (!ret)
		ret = mod_is_hash_blacklisted(mod, wholelen);

	return ret;
}
