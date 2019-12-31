/* Hidden area
 *
 * Copyright (C) 2017 Lee, Chun-Yi <jlee@suse.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <crypto/aes.h>
#include <crypto/skcipher.h>

#include <asm/page.h>

void *hidden_area_va;
unsigned long hidden_area_size;
unsigned long hidden_area_pa;
unsigned long hidden_area_pfn;

static void *cursor;
static u8 iv[AES_BLOCK_SIZE];
static void *encrypted_area_va;

DEFINE_SPINLOCK(area_lock);

void * memcpy_to_hidden_area(const void *source, unsigned long size)
{
	void * start_addr = NULL;

	if (!source || !hidden_area_va)
		return NULL;

	if (page_to_pfn(virt_to_page(cursor + size)) != hidden_area_pfn)
		return NULL;

	spin_lock(&area_lock);
	start_addr = memcpy(cursor, source, size);
	if (start_addr)
		cursor += size;
	spin_unlock(&area_lock);

	return start_addr;
}
EXPORT_SYMBOL(memcpy_to_hidden_area);

bool page_is_hidden(struct page *page)
{
	if (!page || !hidden_area_va)
		return false;

	return (page_to_pfn(page) == hidden_area_pfn);
}
EXPORT_SYMBOL(page_is_hidden);

void clean_hidden_area(void)
{
	spin_lock(&area_lock);
	memset(hidden_area_va, 0, hidden_area_size);
	spin_unlock(&area_lock);
}
EXPORT_SYMBOL(clean_hidden_area);

/**
  * encrypt and backup hidden area - Encrypt and backup hidden area.
  * @key: The key is used to encrypt.
  * @key_len: The size (bytes) of key. It must bigger than 32 (bytes).
  *
  * This routine will encrypt the plain text in hidden area. The cipher
  * text will be put to a buffer as backup. The cipher text can be restored
  * to the hidden area later.
  */
int encrypt_backup_hidden_area(void *key, unsigned long key_len)
{
	struct scatterlist src[1], dst[1];
	struct crypto_skcipher *tfm;
	struct skcipher_request *req;
	u8 iv_tmp[AES_BLOCK_SIZE];
	u8 *tmp;
	int ret;

	if (!key || key_len < 32)
		return -EINVAL;

	if (!encrypted_area_va) {
		encrypted_area_va = (void *)get_zeroed_page(GFP_KERNEL);
		if (!encrypted_area_va)
			return -ENOMEM;
	}

	tmp = kmemdup(hidden_area_va, hidden_area_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tfm = crypto_alloc_skcipher("ctr(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		pr_err("failed to allocate skcipher (%d)\n", ret);
		goto tfm_fail;
	}

	ret = crypto_skcipher_setkey(tfm, key, 32);
	if (ret) {
		pr_err("failed to setkey (%d)\n", ret);
		goto set_fail;
	}

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("failed to allocate request\n");
		ret = -ENOMEM;
		goto set_fail;
	}

	spin_lock(&area_lock);
	/* get initial vector from random pool */
	get_random_bytes(iv, AES_BLOCK_SIZE);
	memcpy(iv_tmp, iv, sizeof(iv));

	memset(encrypted_area_va, 0, hidden_area_size);
	sg_init_one(src, tmp, hidden_area_size);
	sg_init_one(dst, encrypted_area_va, hidden_area_size);
	skcipher_request_set_crypt(req, src, dst, hidden_area_size, iv_tmp);
	ret = crypto_skcipher_encrypt(req);
	spin_unlock(&area_lock);

	skcipher_request_free(req);
set_fail:
	crypto_free_skcipher(tfm);
tfm_fail:
	memset(tmp, 0, hidden_area_size);
	kfree(tmp);

	return ret;
}
EXPORT_SYMBOL(encrypt_backup_hidden_area);

int decrypt_restore_hidden_area(void *key, unsigned long key_len)
{
	struct scatterlist src[1], dst[1];
	struct crypto_skcipher *tfm;
	struct skcipher_request *req;
	u8 iv_tmp[AES_BLOCK_SIZE];
	void *tmp;
	int ret;

	if (!key || key_len < 32)
		return -EINVAL;

	if (!encrypted_area_va) {
		pr_err("hidden area is not encrypted yet\n");
		return -EINVAL;
	}

	/* allocate tmp buffer for decrypted data */
	tmp = (void *)get_zeroed_page(GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tfm = crypto_alloc_skcipher("ctr(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		pr_err("failed to allocate skcipher (%ld)\n", PTR_ERR(tfm));
		goto tfm_fail;
	}

	ret = crypto_skcipher_setkey(tfm, key, 32);
	if (ret) {
		pr_err("failed to setkey (%d)\n", ret);
		goto set_fail;
	}

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("failed to allocate request\n");
		ret = -ENOMEM;
		goto set_fail;
	}

	spin_lock(&area_lock);
	/* decrypt data to tmp buffer */
	sg_init_one(src, encrypted_area_va, hidden_area_size);
	sg_init_one(dst, tmp, hidden_area_size);
	memcpy(iv_tmp, iv, sizeof(iv));
	skcipher_request_set_crypt(req, src, dst, hidden_area_size, iv_tmp);
	ret = crypto_skcipher_decrypt(req);

	/* restore hidden area from tmp buffer */
	if (!ret) {
		memset(hidden_area_va, 0, hidden_area_size);
		memcpy(hidden_area_va, tmp, hidden_area_size);
		memset(encrypted_area_va, 0, hidden_area_size);
		free_pages((unsigned long)encrypted_area_va, 0);
		encrypted_area_va = NULL;
	}
	spin_unlock(&area_lock);

	skcipher_request_free(req);
set_fail:
	crypto_free_skcipher(tfm);
tfm_fail:
	memset(tmp, 0, hidden_area_size);
	free_pages((unsigned long)tmp, 0);
	return ret;
}
EXPORT_SYMBOL(decrypt_restore_hidden_area);

void __init hidden_area_init(void)
{
	cursor = NULL;
	hidden_area_va = (void *) get_zeroed_page(GFP_KERNEL);
	if (!hidden_area_va) {
		pr_err("Hidden Area: allocate page failed\n");
	} else {
		hidden_area_pa = virt_to_phys(hidden_area_va);
		hidden_area_pfn = page_to_pfn(virt_to_page(hidden_area_va));
		hidden_area_size = PAGE_SIZE;
		cursor = hidden_area_va;
	}
}
