/* 
 * Quick & dirty crypto testing module.
 *
 * This will only exist until we have a better testing mechanism
 * (e.g. a char device).
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Jean-Francois Dive <jef@linuxbe.org>
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <linux/highmem.h>
#include "tcrypt.h"

/*
 * Need to kmalloc() memory for testing kmap().
 */
#define TVMEMSIZE	4096
#define XBUFSIZE	32768

/*
 * Indexes into the xbuf to simulate cross-page access.
 */
#define IDX1		37
#define IDX2		32400
#define IDX3		1
#define IDX4		8193
#define IDX5		22222
#define IDX6		17101
#define IDX7		27333
#define IDX8		3000

static int mode;
static char *xbuf;
static char *tvmem;

static char *check[] = {
	"des", "md5", "des3_ede", "rot13", "sha1", "sha256", "blowfish",
	"twofish", "serpent", "sha384", "sha512", "md4", "aes", "cast6", 
	"deflate", NULL
};

static void
hexdump(unsigned char *buf, unsigned int len)
{
	while (len--)
		printk("%02x", *buf++);

	printk("\n");
}

static void
test_md5(void)
{
	char *p;
	unsigned int i;
	struct scatterlist sg[2];
	char result[128];
	struct crypto_tfm *tfm;
	struct md5_testvec *md5_tv;
	unsigned int tsize;

	printk("\ntesting md5\n");

	tsize = sizeof (md5_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, md5_tv_template, tsize);
	md5_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("md5", 0);
	if (tfm == NULL) {
		printk("failed to load transform for md5\n");
		return;
	}

	for (i = 0; i < MD5_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = md5_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(md5_tv[i].plaintext);

		crypto_digest_init(tfm);
		crypto_digest_update(tfm, sg, 1);
		crypto_digest_final(tfm, result);

		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, md5_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	printk("\ntesting md5 across pages\n");

	/* setup the dummy buffer first */
	memset(xbuf, 0, XBUFSIZE);
	memcpy(&xbuf[IDX1], "abcdefghijklm", 13);
	memcpy(&xbuf[IDX2], "nopqrstuvwxyz", 13);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 13;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 13;

	memset(result, 0, sizeof (result));
	crypto_digest_digest(tfm, sg, 2, result);
	hexdump(result, crypto_tfm_alg_digestsize(tfm));

	printk("%s\n",
	       memcmp(result, md5_tv[4].digest,
		      crypto_tfm_alg_digestsize(tfm)) ? "fail" : "pass");
	crypto_free_tfm(tfm);
}

#ifdef CONFIG_CRYPTO_HMAC
static void
test_hmac_md5(void)
{
	char *p;
	unsigned int i, klen;
	struct scatterlist sg[2];
	char result[128];
	struct crypto_tfm *tfm;
	struct hmac_md5_testvec *hmac_md5_tv;
	unsigned int tsize;

	tfm = crypto_alloc_tfm("md5", 0);
	if (tfm == NULL) {
		printk("failed to load transform for md5\n");
		return;
	}

	printk("\ntesting hmac_md5\n");
	
	tsize = sizeof (hmac_md5_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}

	memcpy(tvmem, hmac_md5_tv_template, tsize);
	hmac_md5_tv = (void *) tvmem;

	for (i = 0; i < HMAC_MD5_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = hmac_md5_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(hmac_md5_tv[i].plaintext);

		klen = strlen(hmac_md5_tv[i].key);
		crypto_hmac(tfm, hmac_md5_tv[i].key, &klen, sg, 1, result);

		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, hmac_md5_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	printk("\ntesting hmac_md5 across pages\n");

	memset(xbuf, 0, XBUFSIZE);

	memcpy(&xbuf[IDX1], "what do ya want ", 16);
	memcpy(&xbuf[IDX2], "for nothing?", 12);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 16;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 12;

	memset(result, 0, sizeof (result));
	klen = strlen(hmac_md5_tv[7].key);
	crypto_hmac(tfm, hmac_md5_tv[7].key, &klen, sg, 2, result);
	hexdump(result, crypto_tfm_alg_digestsize(tfm));

	printk("%s\n",
	       memcmp(result, hmac_md5_tv[7].digest,
		      crypto_tfm_alg_digestsize(tfm)) ? "fail" : "pass");
out:
	crypto_free_tfm(tfm);
}

static void
test_hmac_sha1(void)
{
	char *p;
	unsigned int i, klen;
	struct crypto_tfm *tfm;
	struct hmac_sha1_testvec *hmac_sha1_tv;
	struct scatterlist sg[2];
	unsigned int tsize;
	char result[SHA1_DIGEST_SIZE];

	tfm = crypto_alloc_tfm("sha1", 0);
	if (tfm == NULL) {
		printk("failed to load transform for sha1\n");
		return;
	}

	printk("\ntesting hmac_sha1\n");

	tsize = sizeof (hmac_sha1_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}

	memcpy(tvmem, hmac_sha1_tv_template, tsize);
	hmac_sha1_tv = (void *) tvmem;

	for (i = 0; i < HMAC_SHA1_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = hmac_sha1_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(hmac_sha1_tv[i].plaintext);

		klen = strlen(hmac_sha1_tv[i].key);
		
		crypto_hmac(tfm, hmac_sha1_tv[i].key, &klen, sg, 1, result);

		hexdump(result, sizeof (result));
		printk("%s\n",
		       memcmp(result, hmac_sha1_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	printk("\ntesting hmac_sha1 across pages\n");

	/* setup the dummy buffer first */
	memset(xbuf, 0, XBUFSIZE);

	memcpy(&xbuf[IDX1], "what do ya want ", 16);
	memcpy(&xbuf[IDX2], "for nothing?", 12);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 16;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 12;

	memset(result, 0, sizeof (result));
	klen = strlen(hmac_sha1_tv[7].key);
	crypto_hmac(tfm, hmac_sha1_tv[7].key, &klen, sg, 2, result);
	hexdump(result, crypto_tfm_alg_digestsize(tfm));

	printk("%s\n",
	       memcmp(result, hmac_sha1_tv[7].digest,
		      crypto_tfm_alg_digestsize(tfm)) ? "fail" : "pass");
out:
	crypto_free_tfm(tfm);
}

static void
test_hmac_sha256(void)
{
	char *p;
	unsigned int i, klen;
	struct crypto_tfm *tfm;
	struct hmac_sha256_testvec *hmac_sha256_tv;
	struct scatterlist sg[2];
	unsigned int tsize;
	char result[SHA256_DIGEST_SIZE];

	tfm = crypto_alloc_tfm("sha256", 0);
	if (tfm == NULL) {
		printk("failed to load transform for sha256\n");
		return;
	}

	printk("\ntesting hmac_sha256\n");

	tsize = sizeof (hmac_sha256_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}

	memcpy(tvmem, hmac_sha256_tv_template, tsize);
	hmac_sha256_tv = (void *) tvmem;

	for (i = 0; i < HMAC_SHA256_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = hmac_sha256_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(hmac_sha256_tv[i].plaintext);

		klen = strlen(hmac_sha256_tv[i].key);
	
		hexdump(hmac_sha256_tv[i].key, strlen(hmac_sha256_tv[i].key));
		crypto_hmac(tfm, hmac_sha256_tv[i].key, &klen, sg, 1, result);
		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, hmac_sha256_tv[i].digest,
		       crypto_tfm_alg_digestsize(tfm)) ? "fail" : "pass");
	}

out:
	crypto_free_tfm(tfm);
}

#endif	/* CONFIG_CRYPTO_HMAC */

static void
test_md4(void)
{
	char *p;
	unsigned int i;
	struct scatterlist sg[1];
	char result[128];
	struct crypto_tfm *tfm;
	struct md4_testvec *md4_tv;
	unsigned int tsize;

	printk("\ntesting md4\n");

	tsize = sizeof (md4_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, md4_tv_template, tsize);
	md4_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("md4", 0);
	if (tfm == NULL) {
		printk("failed to load transform for md4\n");
		return;
	}

	for (i = 0; i < MD4_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = md4_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(md4_tv[i].plaintext);

		crypto_digest_digest(tfm, sg, 1, result);

		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, md4_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	crypto_free_tfm(tfm);
}

static void
test_sha1(void)
{
	char *p;
	unsigned int i;
	struct crypto_tfm *tfm;
	struct sha1_testvec *sha1_tv;
	struct scatterlist sg[2];
	unsigned int tsize;
	char result[SHA1_DIGEST_SIZE];

	printk("\ntesting sha1\n");

	tsize = sizeof (sha1_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, sha1_tv_template, tsize);
	sha1_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("sha1", 0);
	if (tfm == NULL) {
		printk("failed to load transform for sha1\n");
		return;
	}

	for (i = 0; i < SHA1_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = sha1_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(sha1_tv[i].plaintext);

		crypto_digest_init(tfm);
		crypto_digest_update(tfm, sg, 1);
		crypto_digest_final(tfm, result);

		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, sha1_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	printk("\ntesting sha1 across pages\n");

	/* setup the dummy buffer first */
	memset(xbuf, 0, XBUFSIZE);
	memcpy(&xbuf[IDX1], "abcdbcdecdefdefgefghfghighij", 28);
	memcpy(&xbuf[IDX2], "hijkijkljklmklmnlmnomnopnopq", 28);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 28;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 28;

	memset(result, 0, sizeof (result));
	crypto_digest_digest(tfm, sg, 2, result);
	hexdump(result, crypto_tfm_alg_digestsize(tfm));
	printk("%s\n",
	       memcmp(result, sha1_tv[1].digest,
		      crypto_tfm_alg_digestsize(tfm)) ? "fail" : "pass");
	crypto_free_tfm(tfm);
}

static void
test_sha256(void)
{
	char *p;
	unsigned int i;
	struct crypto_tfm *tfm;
	struct sha256_testvec *sha256_tv;
	struct scatterlist sg[2];
	unsigned int tsize;
	char result[SHA256_DIGEST_SIZE];

	printk("\ntesting sha256\n");

	tsize = sizeof (sha256_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, sha256_tv_template, tsize);
	sha256_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("sha256", 0);
	if (tfm == NULL) {
		printk("failed to load transform for sha256\n");
		return;
	}

	for (i = 0; i < SHA256_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = sha256_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(sha256_tv[i].plaintext);

		crypto_digest_init(tfm);
		crypto_digest_update(tfm, sg, 1);
		crypto_digest_final(tfm, result);

		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, sha256_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	printk("\ntesting sha256 across pages\n");

	/* setup the dummy buffer first */
	memset(xbuf, 0, XBUFSIZE);
	memcpy(&xbuf[IDX1], "abcdbcdecdefdefgefghfghighij", 28);
	memcpy(&xbuf[IDX2], "hijkijkljklmklmnlmnomnopnopq", 28);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 28;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 28;

	memset(result, 0, sizeof (result));
	crypto_digest_digest(tfm, sg, 2, result);
	hexdump(result, crypto_tfm_alg_digestsize(tfm));
	printk("%s\n",
	       memcmp(result, sha256_tv[1].digest,
		      crypto_tfm_alg_digestsize(tfm)) ? "fail" : "pass");
		     
	crypto_free_tfm(tfm);
}

static void
test_sha384(void)
{
	char *p;
	unsigned int i;
	struct crypto_tfm *tfm;
	struct sha384_testvec *sha384_tv;
	struct scatterlist sg[2];
	unsigned int tsize;
	char result[SHA384_DIGEST_SIZE];

	printk("\ntesting sha384\n");

	tsize = sizeof (sha384_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, sha384_tv_template, tsize);
	sha384_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("sha384", 0);
	if (tfm == NULL) {
		printk("failed to load transform for sha384\n");
		return;
	}

	for (i = 0; i < SHA384_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = sha384_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(sha384_tv[i].plaintext);

		crypto_digest_init(tfm);
		crypto_digest_update(tfm, sg, 1);
		crypto_digest_final(tfm, result);

		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, sha384_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	crypto_free_tfm(tfm);
}

static void
test_sha512(void)
{
	char *p;
	unsigned int i;
	struct crypto_tfm *tfm;
	struct sha512_testvec *sha512_tv;
	struct scatterlist sg[2];
	unsigned int tsize;
	char result[SHA512_DIGEST_SIZE];

	printk("\ntesting sha512\n");

	tsize = sizeof (sha512_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, sha512_tv_template, tsize);
	sha512_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("sha512", 0);
	if (tfm == NULL) {
		printk("failed to load transform for sha512\n");
		return;
	}

	for (i = 0; i < SHA512_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		p = sha512_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = strlen(sha512_tv[i].plaintext);

		crypto_digest_init(tfm);
		crypto_digest_update(tfm, sg, 1);
		crypto_digest_final(tfm, result);

		hexdump(result, crypto_tfm_alg_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, sha512_tv[i].digest,
			      crypto_tfm_alg_digestsize(tfm)) ? "fail" :
		       "pass");
	}

	crypto_free_tfm(tfm);
}

void
test_des(void)
{
	unsigned int ret, i, len;
	unsigned int tsize;
	char *p, *q;
	struct crypto_tfm *tfm;
	char *key;
	char res[8];
	struct des_tv *des_tv;
	struct scatterlist sg[8];

	printk("\ntesting des encryption\n");

	tsize = sizeof (des_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, des_enc_tv_template, tsize);
	des_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("des", 0);
	if (tfm == NULL) {
		printk("failed to load transform for des (default ecb)\n");
		return;
	}

	for (i = 0; i < DES_ENC_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);

		key = des_tv[i].key;
		tfm->crt_flags |= CRYPTO_TFM_REQ_WEAK_KEY;

		ret = crypto_cipher_setkey(tfm, key, 8);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!des_tv[i].fail)
				goto out;
		}

		len = des_tv[i].len;

		p = des_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = len;
		ret = crypto_cipher_encrypt(tfm, sg, sg, len);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);

		printk("%s\n",
		       memcmp(q, des_tv[i].result, len) ? "fail" : "pass");

	}

	printk("\ntesting des ecb encryption across pages\n");

	i = 5;
	key = des_tv[i].key;
	tfm->crt_flags = 0;

	hexdump(key, 8);

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 8);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 8, 8);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 8;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 8;

	ret = crypto_cipher_encrypt(tfm, sg, sg, 16);
	if (ret) {
		printk("encrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 8);
	printk("%s\n", memcmp(q, des_tv[i].result, 8) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 8);
	printk("%s\n", memcmp(q, des_tv[i].result + 8, 8) ? "fail" : "pass");

	printk("\ntesting des ecb encryption chunking scenario A\n");

	/*
	 * Scenario A:
	 * 
	 *  F1       F2      F3
	 *  [8 + 6]  [2 + 8] [8]
	 *       ^^^^^^   ^
	 *       a    b   c
	 *
	 * Chunking should begin at a, then end with b, and
	 * continue encrypting at an offset of 2 until c.
	 *
	 */
	i = 7;

	key = des_tv[i].key;
	tfm->crt_flags = 0;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));

	/* Frag 1: 8 + 6 */
	memcpy(&xbuf[IDX3], des_tv[i].plaintext, 14);

	/* Frag 2: 2 + 8 */
	memcpy(&xbuf[IDX4], des_tv[i].plaintext + 14, 10);

	/* Frag 3: 8 */
	memcpy(&xbuf[IDX5], des_tv[i].plaintext + 24, 8);

	p = &xbuf[IDX3];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 14;

	p = &xbuf[IDX4];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 10;

	p = &xbuf[IDX5];
	sg[2].page = virt_to_page(p);
	sg[2].offset = offset_in_page(p);
	sg[2].length = 8;

	ret = crypto_cipher_encrypt(tfm, sg, sg, 32);

	if (ret) {
		printk("decrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 14);
	printk("%s\n", memcmp(q, des_tv[i].result, 14) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 10);
	printk("%s\n", memcmp(q, des_tv[i].result + 14, 10) ? "fail" : "pass");

	printk("page 3\n");
	q = kmap(sg[2].page) + sg[2].offset;
	hexdump(q, 8);
	printk("%s\n", memcmp(q, des_tv[i].result + 24, 8) ? "fail" : "pass");

	printk("\ntesting des ecb encryption chunking scenario B\n");

	/*
	 * Scenario B:
	 * 
	 *  F1  F2  F3  F4
	 *  [2] [1] [3] [2 + 8 + 8]
	 */
	i = 7;

	key = des_tv[i].key;
	tfm->crt_flags = 0;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));

	/* Frag 1: 2 */
	memcpy(&xbuf[IDX3], des_tv[i].plaintext, 2);

	/* Frag 2: 1 */
	memcpy(&xbuf[IDX4], des_tv[i].plaintext + 2, 1);

	/* Frag 3: 3 */
	memcpy(&xbuf[IDX5], des_tv[i].plaintext + 3, 3);

	/* Frag 4: 2 + 8 + 8 */
	memcpy(&xbuf[IDX6], des_tv[i].plaintext + 6, 18);

	p = &xbuf[IDX3];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 2;

	p = &xbuf[IDX4];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 1;

	p = &xbuf[IDX5];
	sg[2].page = virt_to_page(p);
	sg[2].offset = offset_in_page(p);
	sg[2].length = 3;

	p = &xbuf[IDX6];
	sg[3].page = virt_to_page(p);
	sg[3].offset = offset_in_page(p);
	sg[3].length = 18;

	ret = crypto_cipher_encrypt(tfm, sg, sg, 24);

	if (ret) {
		printk("encrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 2);
	printk("%s\n", memcmp(q, des_tv[i].result, 2) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 1);
	printk("%s\n", memcmp(q, des_tv[i].result + 2, 1) ? "fail" : "pass");

	printk("page 3\n");
	q = kmap(sg[2].page) + sg[2].offset;
	hexdump(q, 3);
	printk("%s\n", memcmp(q, des_tv[i].result + 3, 3) ? "fail" : "pass");

	printk("page 4\n");
	q = kmap(sg[3].page) + sg[3].offset;
	hexdump(q, 18);
	printk("%s\n", memcmp(q, des_tv[i].result + 6, 18) ? "fail" : "pass");

	printk("\ntesting des ecb encryption chunking scenario C\n");

	/*
	 * Scenario B:
	 * 
	 *  F1  F2  F3  F4  F5
	 *  [2] [2] [2] [2] [8]
	 */
	i = 7;

	key = des_tv[i].key;
	tfm->crt_flags = 0;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));

	/* Frag 1: 2 */
	memcpy(&xbuf[IDX3], des_tv[i].plaintext, 2);

	/* Frag 2: 2 */
	memcpy(&xbuf[IDX4], des_tv[i].plaintext + 2, 2);

	/* Frag 3: 2 */
	memcpy(&xbuf[IDX5], des_tv[i].plaintext + 4, 2);

	/* Frag 4: 2 */
	memcpy(&xbuf[IDX6], des_tv[i].plaintext + 6, 2);

	/* Frag 5: 8 */
	memcpy(&xbuf[IDX7], des_tv[i].plaintext + 8, 8);

	p = &xbuf[IDX3];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 2;

	p = &xbuf[IDX4];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 2;

	p = &xbuf[IDX5];
	sg[2].page = virt_to_page(p);
	sg[2].offset = offset_in_page(p);
	sg[2].length = 2;

	p = &xbuf[IDX6];
	sg[3].page = virt_to_page(p);
	sg[3].offset = offset_in_page(p);
	sg[3].length = 2;

	p = &xbuf[IDX7];
	sg[4].page = virt_to_page(p);
	sg[4].offset = offset_in_page(p);
	sg[4].length = 8;

	ret = crypto_cipher_encrypt(tfm, sg, sg, 16);

	if (ret) {
		printk("encrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 2);
	printk("%s\n", memcmp(q, des_tv[i].result, 2) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 2);
	printk("%s\n", memcmp(q, des_tv[i].result + 2, 2) ? "fail" : "pass");

	printk("page 3\n");
	q = kmap(sg[2].page) + sg[2].offset;
	hexdump(q, 2);
	printk("%s\n", memcmp(q, des_tv[i].result + 4, 2) ? "fail" : "pass");

	printk("page 4\n");
	q = kmap(sg[3].page) + sg[3].offset;
	hexdump(q, 2);
	printk("%s\n", memcmp(q, des_tv[i].result + 6, 2) ? "fail" : "pass");

	printk("page 5\n");
	q = kmap(sg[4].page) + sg[4].offset;
	hexdump(q, 8);
	printk("%s\n", memcmp(q, des_tv[i].result + 8, 8) ? "fail" : "pass");

	printk("\ntesting des ecb encryption chunking scenario D\n");

	/*
	 * Scenario D, torture test, one byte per frag.
	 */
	i = 7;
	key = des_tv[i].key;
	tfm->crt_flags = 0;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, XBUFSIZE);

	xbuf[IDX1] = des_tv[i].plaintext[0];
	xbuf[IDX2] = des_tv[i].plaintext[1];
	xbuf[IDX3] = des_tv[i].plaintext[2];
	xbuf[IDX4] = des_tv[i].plaintext[3];
	xbuf[IDX5] = des_tv[i].plaintext[4];
	xbuf[IDX6] = des_tv[i].plaintext[5];
	xbuf[IDX7] = des_tv[i].plaintext[6];
	xbuf[IDX8] = des_tv[i].plaintext[7];

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 1;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 1;

	p = &xbuf[IDX3];
	sg[2].page = virt_to_page(p);
	sg[2].offset = offset_in_page(p);
	sg[2].length = 1;

	p = &xbuf[IDX4];
	sg[3].page = virt_to_page(p);
	sg[3].offset = offset_in_page(p);
	sg[3].length = 1;

	p = &xbuf[IDX5];
	sg[4].page = virt_to_page(p);
	sg[4].offset = offset_in_page(p);
	sg[4].length = 1;

	p = &xbuf[IDX6];
	sg[5].page = virt_to_page(p);
	sg[5].offset = offset_in_page(p);
	sg[5].length = 1;

	p = &xbuf[IDX7];
	sg[6].page = virt_to_page(p);
	sg[6].offset = offset_in_page(p);
	sg[6].length = 1;

	p = &xbuf[IDX8];
	sg[7].page = virt_to_page(p);
	sg[7].offset = offset_in_page(p);
	sg[7].length = 1;

	ret = crypto_cipher_encrypt(tfm, sg, sg, 8);
	if (ret) {
		printk("encrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	for (i = 0; i < 8; i++)
		res[i] = *(char *) (kmap(sg[i].page) + sg[i].offset);

	hexdump(res, 8);
	printk("%s\n", memcmp(res, des_tv[7].result, 8) ? "fail" : "pass");

	printk("\ntesting des decryption\n");

	tsize = sizeof (des_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}
	memcpy(tvmem, des_dec_tv_template, tsize);
	des_tv = (void *) tvmem;

	for (i = 0; i < DES_DEC_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);

		key = des_tv[i].key;

		tfm->crt_flags = 0;
		ret = crypto_cipher_setkey(tfm, key, 8);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		len = des_tv[i].len;

		p = des_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = len;

		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("des_decrypt() failed flags=%x\n",
			       tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);

		printk("%s\n",
		       memcmp(q, des_tv[i].result, len) ? "fail" : "pass");

	}

	printk("\ntesting des ecb decryption across pages\n");

	i = 6;

	key = des_tv[i].key;
	tfm->crt_flags = 0;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 8);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 8, 8);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 8;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 8;

	ret = crypto_cipher_decrypt(tfm, sg, sg, 16);
	if (ret) {
		printk("decrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 8);
	printk("%s\n", memcmp(q, des_tv[i].result, 8) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 8);
	printk("%s\n", memcmp(q, des_tv[i].result + 8, 8) ? "fail" : "pass");

	/*
	 * Scenario E:
	 * 
	 *  F1   F2      F3
	 *  [3]  [5 + 7] [1]
	 *
	 */
	printk("\ntesting des ecb decryption chunking scenario E\n");
	i = 2;

	key = des_tv[i].key;
	tfm->crt_flags = 0;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));

	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 3);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 3, 12);
	memcpy(&xbuf[IDX3], des_tv[i].plaintext + 15, 1);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 3;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 12;

	p = &xbuf[IDX3];
	sg[2].page = virt_to_page(p);
	sg[2].offset = offset_in_page(p);
	sg[2].length = 1;

	ret = crypto_cipher_decrypt(tfm, sg, sg, 16);

	if (ret) {
		printk("decrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 3);
	printk("%s\n", memcmp(q, des_tv[i].result, 3) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 12);
	printk("%s\n", memcmp(q, des_tv[i].result + 3, 12) ? "fail" : "pass");

	printk("page 3\n");
	q = kmap(sg[2].page) + sg[2].offset;
	hexdump(q, 1);
	printk("%s\n", memcmp(q, des_tv[i].result + 15, 1) ? "fail" : "pass");

	crypto_free_tfm(tfm);

	tfm = crypto_alloc_tfm("des", CRYPTO_TFM_MODE_CBC);
	if (tfm == NULL) {
		printk("failed to load transform for des cbc\n");
		return;
	}

	printk("\ntesting des cbc encryption\n");

	tsize = sizeof (des_cbc_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}
	memcpy(tvmem, des_cbc_enc_tv_template, tsize);
	des_tv = (void *) tvmem;

	crypto_cipher_set_iv(tfm, des_tv[i].iv, crypto_tfm_alg_ivsize(tfm));
	crypto_cipher_get_iv(tfm, res, crypto_tfm_alg_ivsize(tfm));
	
	if (memcmp(res, des_tv[i].iv, sizeof(res))) {
		printk("crypto_cipher_[set|get]_iv() failed\n");
		goto out;
	}
	
	for (i = 0; i < DES_CBC_ENC_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);

		key = des_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, 8);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		len = des_tv[i].len;
		p = des_tv[i].plaintext;

		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = len;

		crypto_cipher_set_iv(tfm, des_tv[i].iv,
				     crypto_tfm_alg_ivsize(tfm));

		ret = crypto_cipher_encrypt(tfm, sg, sg, len);
		if (ret) {
			printk("des_cbc_encrypt() failed flags=%x\n",
			       tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);

		printk("%s\n",
		       memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	}

	crypto_free_tfm(tfm);

	/*
	 * Scenario F:
	 * 
	 *  F1       F2      
	 *  [8 + 5]  [3 + 8]
	 *
	 */
	printk("\ntesting des cbc encryption chunking scenario F\n");
	i = 4;

	tfm = crypto_alloc_tfm("des", CRYPTO_TFM_MODE_CBC);
	if (tfm == NULL) {
		printk("failed to load transform for CRYPTO_ALG_DES_CCB\n");
		return;
	}

	tfm->crt_flags = 0;
	key = des_tv[i].key;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));

	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 13);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 13, 11);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 13;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 11;

	crypto_cipher_set_iv(tfm, des_tv[i].iv, crypto_tfm_alg_ivsize(tfm));

	ret = crypto_cipher_encrypt(tfm, sg, sg, 24);
	if (ret) {
		printk("des_cbc_decrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 13);
	printk("%s\n", memcmp(q, des_tv[i].result, 13) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 11);
	printk("%s\n", memcmp(q, des_tv[i].result + 13, 11) ? "fail" : "pass");

	tsize = sizeof (des_cbc_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}
	memcpy(tvmem, des_cbc_dec_tv_template, tsize);
	des_tv = (void *) tvmem;

	printk("\ntesting des cbc decryption\n");

	for (i = 0; i < DES_CBC_DEC_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);

		tfm->crt_flags = 0;
		key = des_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, 8);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		len = des_tv[i].len;
		p = des_tv[i].plaintext;

		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = len;

		crypto_cipher_set_iv(tfm, des_tv[i].iv,
				      crypto_tfm_alg_blocksize(tfm));

		ret = crypto_cipher_decrypt(tfm, sg, sg, len);
		if (ret) {
			printk("des_cbc_decrypt() failed flags=%x\n",
			       tfm->crt_flags);
			goto out;
		}

		hexdump(tfm->crt_cipher.cit_iv, 8);

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);

		printk("%s\n",
		       memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	}

	/*
	 * Scenario G:
	 * 
	 *  F1   F2      
	 *  [4]  [4]
	 *
	 */
	printk("\ntesting des cbc decryption chunking scenario G\n");
	i = 3;

	tfm->crt_flags = 0;
	key = des_tv[i].key;

	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof (xbuf));
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 4);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 4, 4);

	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = offset_in_page(p);
	sg[0].length = 4;

	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = offset_in_page(p);
	sg[1].length = 4;

	crypto_cipher_set_iv(tfm, des_tv[i].iv, crypto_tfm_alg_ivsize(tfm));

	ret = crypto_cipher_decrypt(tfm, sg, sg, 8);
	if (ret) {
		printk("des_cbc_decrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	printk("page 1\n");
	q = kmap(sg[0].page) + sg[0].offset;
	hexdump(q, 4);
	printk("%s\n", memcmp(q, des_tv[i].result, 4) ? "fail" : "pass");

	printk("page 2\n");
	q = kmap(sg[1].page) + sg[1].offset;
	hexdump(q, 4);
	printk("%s\n", memcmp(q, des_tv[i].result + 4, 4) ? "fail" : "pass");

      out:
	crypto_free_tfm(tfm);
}

void
test_des3_ede(void)
{
	unsigned int ret, i, len;
	unsigned int tsize;
	char *p, *q;
	struct crypto_tfm *tfm;
	char *key;
	/*char res[8]; */
	struct des_tv *des_tv;
	struct scatterlist sg[8];

	printk("\ntesting des3 ede encryption\n");

	tsize = sizeof (des3_ede_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, des3_ede_enc_tv_template, tsize);
	des_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("des3_ede", CRYPTO_TFM_MODE_ECB);
	if (tfm == NULL) {
		printk("failed to load transform for 3des ecb\n");
		return;
	}

	for (i = 0; i < DES3_EDE_ENC_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);

		key = des_tv[i].key;
		ret = crypto_cipher_setkey(tfm, key, 24);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!des_tv[i].fail)
				goto out;
		}

		len = des_tv[i].len;

		p = des_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = len;
		ret = crypto_cipher_encrypt(tfm, sg, sg, len);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);

		printk("%s\n",
		       memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	}

	printk("\ntesting des3 ede decryption\n");

	tsize = sizeof (des3_ede_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, des3_ede_dec_tv_template, tsize);
	des_tv = (void *) tvmem;

	for (i = 0; i < DES3_EDE_DEC_TEST_VECTORS; i++) {
		printk("test %u:\n", i + 1);

		key = des_tv[i].key;
		ret = crypto_cipher_setkey(tfm, key, 24);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!des_tv[i].fail)
				goto out;
		}

		len = des_tv[i].len;

		p = des_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = len;
		ret = crypto_cipher_decrypt(tfm, sg, sg, len);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);

		printk("%s\n",
		       memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	}

      out:
	crypto_free_tfm(tfm);
}

void
test_blowfish(void)
{
	unsigned int ret, i;
	unsigned int tsize;
	char *p, *q;
	struct crypto_tfm *tfm;
	char *key;
	struct bf_tv *bf_tv;
	struct scatterlist sg[1];

	printk("\ntesting blowfish encryption\n");

	tsize = sizeof (bf_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, bf_enc_tv_template, tsize);
	bf_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("blowfish", 0);
	if (tfm == NULL) {
		printk("failed to load transform for blowfish (default ecb)\n");
		return;
	}

	for (i = 0; i < BF_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, bf_tv[i].keylen * 8);
		key = bf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, bf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!bf_tv[i].fail)
				goto out;
		}

		p = bf_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = bf_tv[i].plen;
		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, bf_tv[i].rlen);

		printk("%s\n", memcmp(q, bf_tv[i].result, bf_tv[i].rlen) ?
			"fail" : "pass");
	}

	printk("\ntesting blowfish decryption\n");

	tsize = sizeof (bf_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, bf_dec_tv_template, tsize);
	bf_tv = (void *) tvmem;

	for (i = 0; i < BF_DEC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, bf_tv[i].keylen * 8);
		key = bf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, bf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!bf_tv[i].fail)
				goto out;
		}

		p = bf_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = bf_tv[i].plen;
		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, bf_tv[i].rlen);

		printk("%s\n", memcmp(q, bf_tv[i].result, bf_tv[i].rlen) ?
			"fail" : "pass");
	}
	
	crypto_free_tfm(tfm);
	
	tfm = crypto_alloc_tfm("blowfish", CRYPTO_TFM_MODE_CBC);
	if (tfm == NULL) {
		printk("failed to load transform for blowfish cbc\n");
		return;
	}

	printk("\ntesting blowfish cbc encryption\n");

	tsize = sizeof (bf_cbc_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}
	memcpy(tvmem, bf_cbc_enc_tv_template, tsize);
	bf_tv = (void *) tvmem;

	for (i = 0; i < BF_CBC_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, bf_tv[i].keylen * 8);

		key = bf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, bf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		p = bf_tv[i].plaintext;

		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length =  bf_tv[i].plen;

		crypto_cipher_set_iv(tfm, bf_tv[i].iv,
				     crypto_tfm_alg_ivsize(tfm));

		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("blowfish_cbc_encrypt() failed flags=%x\n",
			       tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, bf_tv[i].rlen);

		printk("%s\n", memcmp(q, bf_tv[i].result, bf_tv[i].rlen)
			? "fail" : "pass");
	}

	printk("\ntesting blowfish cbc decryption\n");

	tsize = sizeof (bf_cbc_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}
	memcpy(tvmem, bf_cbc_dec_tv_template, tsize);
	bf_tv = (void *) tvmem;

	for (i = 0; i < BF_CBC_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, bf_tv[i].keylen * 8);
		key = bf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, bf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		p = bf_tv[i].plaintext;

		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length =  bf_tv[i].plen;

		crypto_cipher_set_iv(tfm, bf_tv[i].iv,
				     crypto_tfm_alg_ivsize(tfm));

		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("blowfish_cbc_decrypt() failed flags=%x\n",
			       tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, bf_tv[i].rlen);

		printk("%s\n", memcmp(q, bf_tv[i].result, bf_tv[i].rlen)
			? "fail" : "pass");
	}

out:
	crypto_free_tfm(tfm);
}


void
test_twofish(void)
{
	unsigned int ret, i;
	unsigned int tsize;
	char *p, *q;
	struct crypto_tfm *tfm;
	char *key;
	struct tf_tv *tf_tv;
	struct scatterlist sg[1];

	printk("\ntesting twofish encryption\n");

	tsize = sizeof (tf_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, tf_enc_tv_template, tsize);
	tf_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("twofish", 0);
	if (tfm == NULL) {
		printk("failed to load transform for blowfish (default ecb)\n");
		return;
	}

	for (i = 0; i < TF_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, tf_tv[i].keylen * 8);
		key = tf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, tf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!tf_tv[i].fail)
				goto out;
		}

		p = tf_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = tf_tv[i].plen;
		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, tf_tv[i].rlen);

		printk("%s\n", memcmp(q, tf_tv[i].result, tf_tv[i].rlen) ?
			"fail" : "pass");
	}

	printk("\ntesting twofish decryption\n");

	tsize = sizeof (tf_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, tf_dec_tv_template, tsize);
	tf_tv = (void *) tvmem;

	for (i = 0; i < TF_DEC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, tf_tv[i].keylen * 8);
		key = tf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, tf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!tf_tv[i].fail)
				goto out;
		}

		p = tf_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = tf_tv[i].plen;
		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, tf_tv[i].rlen);

		printk("%s\n", memcmp(q, tf_tv[i].result, tf_tv[i].rlen) ?
			"fail" : "pass");
	}

	crypto_free_tfm(tfm);
	
	tfm = crypto_alloc_tfm("twofish", CRYPTO_TFM_MODE_CBC);
	if (tfm == NULL) {
		printk("failed to load transform for twofish cbc\n");
		return;
	}

	printk("\ntesting twofish cbc encryption\n");

	tsize = sizeof (tf_cbc_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}
	memcpy(tvmem, tf_cbc_enc_tv_template, tsize);
	tf_tv = (void *) tvmem;

	for (i = 0; i < TF_CBC_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, tf_tv[i].keylen * 8);

		key = tf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, tf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		p = tf_tv[i].plaintext;

		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length =  tf_tv[i].plen;

		crypto_cipher_set_iv(tfm, tf_tv[i].iv,
				     crypto_tfm_alg_ivsize(tfm));

		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("blowfish_cbc_encrypt() failed flags=%x\n",
			       tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, tf_tv[i].rlen);

		printk("%s\n", memcmp(q, tf_tv[i].result, tf_tv[i].rlen)
			? "fail" : "pass");
	}

	printk("\ntesting twofish cbc decryption\n");

	tsize = sizeof (tf_cbc_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}
	memcpy(tvmem, tf_cbc_dec_tv_template, tsize);
	tf_tv = (void *) tvmem;

	for (i = 0; i < TF_CBC_DEC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, tf_tv[i].keylen * 8);

		key = tf_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, tf_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		p = tf_tv[i].plaintext;

		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length =  tf_tv[i].plen;

		crypto_cipher_set_iv(tfm, tf_tv[i].iv,
				     crypto_tfm_alg_ivsize(tfm));

		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("blowfish_cbc_decrypt() failed flags=%x\n",
			       tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, tf_tv[i].rlen);

		printk("%s\n", memcmp(q, tf_tv[i].result, tf_tv[i].rlen)
			? "fail" : "pass");
	}

out:	
	crypto_free_tfm(tfm);
}

void
test_serpent(void)
{
	unsigned int ret, i, tsize;
	u8 *p, *q, *key;
	struct crypto_tfm *tfm;
	struct serpent_tv *serp_tv;
	struct scatterlist sg[1];

	printk("\ntesting serpent encryption\n");

	tfm = crypto_alloc_tfm("serpent", 0);
	if (tfm == NULL) {
		printk("failed to load transform for serpent (default ecb)\n");
		return;
	}

	tsize = sizeof (serpent_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, serpent_enc_tv_template, tsize);
	serp_tv = (void *) tvmem;
	for (i = 0; i < SERPENT_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n", i + 1, serp_tv[i].keylen * 8);
		key = serp_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, serp_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!serp_tv[i].fail)
				goto out;
		}

		p = serp_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = sizeof(serp_tv[i].plaintext);
		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, sizeof(serp_tv[i].result));

		printk("%s\n", memcmp(q, serp_tv[i].result,
			sizeof(serp_tv[i].result)) ? "fail" : "pass");
	}

	printk("\ntesting serpent decryption\n");

	tsize = sizeof (serpent_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, serpent_dec_tv_template, tsize);
	serp_tv = (void *) tvmem;
	for (i = 0; i < SERPENT_DEC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n", i + 1, serp_tv[i].keylen * 8);
		key = serp_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, serp_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!serp_tv[i].fail)
				goto out;
		}

		p = serp_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = sizeof(serp_tv[i].plaintext);
		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, sizeof(serp_tv[i].result));

		printk("%s\n", memcmp(q, serp_tv[i].result,
			sizeof(serp_tv[i].result)) ? "fail" : "pass");
	}

out:
	crypto_free_tfm(tfm);
}

static void
test_cast6(void)
{
	unsigned int ret, i, tsize;
	u8 *p, *q, *key;
	struct crypto_tfm *tfm;
	struct cast6_tv *cast_tv;
	struct scatterlist sg[1];

	printk("\ntesting cast6 encryption\n");

	tfm = crypto_alloc_tfm("cast6", 0);
	if (tfm == NULL) {
		printk("failed to load transform for cast6 (default ecb)\n");
		return;
	}

	tsize = sizeof (cast6_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, cast6_enc_tv_template, tsize);
	cast_tv = (void *) tvmem;
	for (i = 0; i < CAST6_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n", i + 1, cast_tv[i].keylen * 8);
		key = cast_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, cast_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!cast_tv[i].fail)
				goto out;
		}

		p = cast_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long) p & ~PAGE_MASK);
		sg[0].length = sizeof(cast_tv[i].plaintext);
		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, sizeof(cast_tv[i].result));

		printk("%s\n", memcmp(q, cast_tv[i].result,
			sizeof(cast_tv[i].result)) ? "fail" : "pass");
	}

	printk("\ntesting cast6 decryption\n");

	tsize = sizeof (cast6_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, cast6_dec_tv_template, tsize);
	cast_tv = (void *) tvmem;
	for (i = 0; i < CAST6_DEC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n", i + 1, cast_tv[i].keylen * 8);
		key = cast_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, cast_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!cast_tv[i].fail)
				goto out;
		}

		p = cast_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long) p & ~PAGE_MASK);
		sg[0].length = sizeof(cast_tv[i].plaintext);
		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, sizeof(cast_tv[i].result));

		printk("%s\n", memcmp(q, cast_tv[i].result,
			sizeof(cast_tv[i].result)) ? "fail" : "pass");
	}

out:
	crypto_free_tfm(tfm);
}

void
test_aes(void)
{
	unsigned int ret, i;
	unsigned int tsize;
	char *p, *q;
	struct crypto_tfm *tfm;
	char *key;
	struct aes_tv *aes_tv;
	struct scatterlist sg[1];

	printk("\ntesting aes encryption\n");

	tsize = sizeof (aes_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, aes_enc_tv_template, tsize);
	aes_tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("aes", 0);
	if (tfm == NULL) {
		printk("failed to load transform for aes (default ecb)\n");
		return;
	}

	for (i = 0; i < AES_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, aes_tv[i].keylen * 8);
		key = aes_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, aes_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!aes_tv[i].fail)
				goto out;
		}

		p = aes_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = aes_tv[i].plen;
		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, aes_tv[i].rlen);

		printk("%s\n", memcmp(q, aes_tv[i].result, aes_tv[i].rlen) ?
			"fail" : "pass");
	}
	
	printk("\ntesting aes decryption\n");

	tsize = sizeof (aes_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, aes_dec_tv_template, tsize);
	aes_tv = (void *) tvmem;

	for (i = 0; i < AES_DEC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n",
			i + 1, aes_tv[i].keylen * 8);
		key = aes_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, aes_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!aes_tv[i].fail)
				goto out;
		}

		p = aes_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = offset_in_page(p);
		sg[0].length = aes_tv[i].plen;
		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, aes_tv[i].rlen);

		printk("%s\n", memcmp(q, aes_tv[i].result, aes_tv[i].rlen) ?
			"fail" : "pass");
	}

out:
	crypto_free_tfm(tfm);
}

void
test_cast5(void)
{
	unsigned int ret, i, tsize;
	u8 *p, *q, *key;
	struct crypto_tfm *tfm;
	struct cast5_tv *c5_tv;
	struct scatterlist sg[1];

	printk("\ntesting cast5 encryption\n");

	tfm = crypto_alloc_tfm("cast5", 0);
	if (tfm == NULL) {
		printk("failed to load transform for cast5 (default ecb)\n");
		return;
	}

	tsize = sizeof (cast5_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, cast5_enc_tv_template, tsize);
	c5_tv = (void *) tvmem;
	for (i = 0; i < CAST5_ENC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n", i + 1, c5_tv[i].keylen * 8);
		key = c5_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, c5_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!c5_tv[i].fail)
				goto out;
		}

		p = c5_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long) p & ~PAGE_MASK);
		sg[0].length = sizeof(c5_tv[i].plaintext);
		ret = crypto_cipher_encrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, sizeof(c5_tv[i].ciphertext));

		printk("%s\n", memcmp(q, c5_tv[i].ciphertext,
			sizeof(c5_tv[i].ciphertext)) ? "fail" : "pass");
	}
	
	tsize = sizeof (cast5_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, cast5_dec_tv_template, tsize);
	c5_tv = (void *) tvmem;
	for (i = 0; i < CAST5_DEC_TEST_VECTORS; i++) {
		printk("test %u (%d bit key):\n", i + 1, c5_tv[i].keylen * 8);
		key = c5_tv[i].key;

		ret = crypto_cipher_setkey(tfm, key, c5_tv[i].keylen);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);

			if (!c5_tv[i].fail)
				goto out;
		}

		p = c5_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long) p & ~PAGE_MASK);
		sg[0].length = sizeof(c5_tv[i].plaintext);
		ret = crypto_cipher_decrypt(tfm, sg, sg, sg[0].length);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}

		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, sizeof(c5_tv[i].ciphertext));

		printk("%s\n", memcmp(q, c5_tv[i].ciphertext,
			sizeof(c5_tv[i].ciphertext)) ? "fail" : "pass");
	}
out:
	crypto_free_tfm (tfm);
}

static void
test_deflate(void)
{
	unsigned int i;
	char result[COMP_BUF_SIZE];
	struct crypto_tfm *tfm;
	struct comp_testvec *tv;
	unsigned int tsize;

	printk("\ntesting deflate compression\n");

	tsize = sizeof (deflate_comp_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		return;
	}

	memcpy(tvmem, deflate_comp_tv_template, tsize);
	tv = (void *) tvmem;

	tfm = crypto_alloc_tfm("deflate", 0);
	if (tfm == NULL) {
		printk("failed to load transform for deflate\n");
		return;
	}

	for (i = 0; i < DEFLATE_COMP_TEST_VECTORS; i++) {
		int ilen, ret, dlen = COMP_BUF_SIZE;
		
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		ilen = tv[i].inlen;
		ret = crypto_comp_compress(tfm, tv[i].input,
		                           ilen, result, &dlen);
		if (ret) {
			printk("fail: ret=%d\n", ret);
			continue;
		}
		hexdump(result, dlen);
		printk("%s (ratio %d:%d)\n",
		       memcmp(result, tv[i].output, dlen) ? "fail" : "pass",
		       ilen, dlen);
	}

	printk("\ntesting deflate decompression\n");

	tsize = sizeof (deflate_decomp_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}

	memcpy(tvmem, deflate_decomp_tv_template, tsize);
	tv = (void *) tvmem;

	for (i = 0; i < DEFLATE_DECOMP_TEST_VECTORS; i++) {
		int ilen, ret, dlen = COMP_BUF_SIZE;
		
		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		ilen = tv[i].inlen;
		ret = crypto_comp_decompress(tfm, tv[i].input,
		                             ilen, result, &dlen);
		if (ret) {
			printk("fail: ret=%d\n", ret);
			continue;
		}
		hexdump(result, dlen);
		printk("%s (ratio %d:%d)\n",
		       memcmp(result, tv[i].output, dlen) ? "fail" : "pass",
		       ilen, dlen);
	}
out:
	crypto_free_tfm(tfm);
}

static void
test_available(void)
{
	char **name = check;
	
	while (*name) {
		printk("alg %s ", *name);
		printk((crypto_alg_available(*name, 0)) ?
			"found\n" : "not found\n");
		name++;
	}	
}

static void
do_test(void)
{
	switch (mode) {

	case 0:
		test_md5();
		test_sha1();
		test_des();
		test_des3_ede();
		test_md4();
		test_sha256();
		test_blowfish();
		test_twofish();
		test_serpent();
		test_cast6();
		test_aes();
		test_sha384();
		test_sha512();
		test_deflate();
		test_cast5();
		test_cast6();
#ifdef CONFIG_CRYPTO_HMAC
		test_hmac_md5();
		test_hmac_sha1();
		test_hmac_sha256();
#endif		
		break;

	case 1:
		test_md5();
		break;

	case 2:
		test_sha1();
		break;

	case 3:
		test_des();
		break;

	case 4:
		test_des3_ede();
		break;

	case 5:
		test_md4();
		break;
		
	case 6:
		test_sha256();
		break;
	
	case 7:
		test_blowfish();
		break;

	case 8:
		test_twofish();
		break;

	case 9:
		test_serpent();
		break;

	case 10:
		test_aes();
		break;

	case 11:
		test_sha384();
		break;
		
	case 12:
		test_sha512();
		break;

	case 13:
		test_deflate();
		break;

	case 14:
		test_cast5();
		break;

	case 15:
		test_cast6();
		break;

#ifdef CONFIG_CRYPTO_HMAC
	case 100:
		test_hmac_md5();
		break;
		
	case 101:
		test_hmac_sha1();
		break;
	
	case 102:
		test_hmac_sha256();
		break;

#endif

	case 1000:
		test_available();
		break;
		
	default:
		/* useful for debugging */
		printk("not testing anything\n");
		break;
	}
}

static int __init
init(void)
{
	tvmem = kmalloc(TVMEMSIZE, GFP_KERNEL);
	if (tvmem == NULL)
		return -ENOMEM;

	xbuf = kmalloc(XBUFSIZE, GFP_KERNEL);
	if (xbuf == NULL) {
		kfree(tvmem);
		return -ENOMEM;
	}

	do_test();

	kfree(xbuf);
	kfree(tvmem);
	return 0;
}

module_init(init);

MODULE_PARM(mode, "i");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Quick & dirty crypto testing module");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
