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
#include <linux/string.h>
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

static int mode = 0;
static char *xbuf;
static char *tvmem;

static void hexdump(unsigned char *buf, size_t len)
{
	while (len--)
		printk("%02x", *buf++);
	
	printk("\n");
}

static void test_md5(void)
{
	char *p;
	int i;
	struct scatterlist sg[2];
	char result[128];
	struct crypto_tfm *tfm;
	struct md5_testvec *md5_tv;
	struct hmac_md5_testvec *hmac_md5_tv;
	size_t tsize;

	printk("\ntesting md5\n");
	
	tsize = sizeof(md5_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	
	memcpy(tvmem, md5_tv_template, tsize);
	md5_tv = (void *)tvmem;
	
	tfm = crypto_alloc_tfm(CRYPTO_ALG_MD5);
	if (tfm == NULL) {
		printk("failed to load transform for CRYPTO_ALG_MD5\n");
		return;
	}
	
	for (i = 0; i < MD5_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);
		memset(result, 0, sizeof(result));
		
		p = md5_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = strlen(md5_tv[i].plaintext);

		crypto_digest_init(tfm);
		crypto_digest_update(tfm, sg, 1);
		crypto_digest_final(tfm, result);

		hexdump(result, crypto_tfm_digestsize(tfm));
		printk("%s\n", memcmp(result, md5_tv[i].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");
	}

	printk("\ntesting md5 across pages\n");
	
	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof(xbuf));
	memcpy(&xbuf[IDX1], "abcdefghijklm", 13);
	memcpy(&xbuf[IDX2], "nopqrstuvwxyz", 13);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 13;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 13;
	
	memset(result, 0, sizeof(result));
	crypto_digest_digest(tfm, sg, 2, result);
	hexdump(result, crypto_tfm_digestsize(tfm));
	
	printk("%s\n", memcmp(result, md5_tv[4].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");
	
	printk("\ntesting hmac_md5\n");
	
	tsize = sizeof(hmac_md5_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	
	memcpy(tvmem, hmac_md5_tv_template, tsize);
	hmac_md5_tv = (void *)tvmem;
	
	for (i = 0; i < HMAC_MD5_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);
		memset(result, 0, sizeof(result));
		
		p = hmac_md5_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = strlen(hmac_md5_tv[i].plaintext);
		
		crypto_digest_hmac(tfm, hmac_md5_tv[i].key, strlen(hmac_md5_tv[i].key),sg , 1, result);
		
		hexdump(result, crypto_tfm_digestsize(tfm));
		printk("%s\n", memcmp(result, hmac_md5_tv[i].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");
	}
	
	printk("\ntesting hmac_md5 across pages\n");
	
	memset(xbuf, 0, sizeof(xbuf));
	
	memcpy(&xbuf[IDX1], "what do ya want ", 16);
	memcpy(&xbuf[IDX2], "for nothing?", 12);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 16;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 12;
	
	memset(result, 0, sizeof(result));
	crypto_digest_hmac(tfm, hmac_md5_tv[1].key, strlen(hmac_md5_tv[1].key), sg, 2, result);
	hexdump(result, crypto_tfm_digestsize(tfm));
	
	printk("%s\n", memcmp(result, hmac_md5_tv[1].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");
	
	crypto_free_tfm(tfm);
}

static void test_sha1(void)
{
	char *p;
	int i;
	struct crypto_tfm *tfm;
	struct sha1_testvec *sha1_tv;
	struct hmac_sha1_testvec *hmac_sha1_tv;
	struct scatterlist sg[2];
	size_t tsize;
	char result[SHA1_DIGEST_SIZE];
	
	printk("\ntesting sha1\n");
	
	tsize = sizeof(sha1_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	
	memcpy(tvmem, sha1_tv_template, tsize);
	sha1_tv = (void *)tvmem;
	
	tfm = crypto_alloc_tfm(CRYPTO_ALG_SHA1);
	if (tfm == NULL) {
		printk("failed to load transform for CRYPTO_ALG_SHA1\n");
		return;
	}

	for (i = 0; i < SHA1_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);
		memset(result, 0, sizeof(result));
		
		p = sha1_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = strlen(sha1_tv[i].plaintext);
		                
		crypto_digest_init(tfm);
		crypto_digest_update(tfm, sg, 1);
		crypto_digest_final(tfm, result);

		hexdump(result, crypto_tfm_digestsize(tfm));
		printk("%s\n", memcmp(result, sha1_tv[i].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");
	}

	printk("\ntesting sha1 across pages\n");
	
	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof(xbuf));
	memcpy(&xbuf[IDX1], "abcdbcdecdefdefgefghfghighij", 28);
	memcpy(&xbuf[IDX2], "hijkijkljklmklmnlmnomnopnopq", 28);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 28;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 28;
	
	memset(result, 0, sizeof(result));
	crypto_digest_digest(tfm, sg, 2, result);
	hexdump(result, crypto_tfm_digestsize(tfm));
	
	printk("%s\n", memcmp(result, sha1_tv[1].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");

	printk("\ntesting hmac_sha1\n");

	tsize = sizeof(hmac_sha1_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	
	memcpy(tvmem, hmac_sha1_tv_template, tsize);
	hmac_sha1_tv = (void *)tvmem;
	
	for (i = 0; i < HMAC_SHA1_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);
		memset(result, 0, sizeof(result));
		
		p = hmac_sha1_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = strlen(hmac_sha1_tv[i].plaintext);
		
		crypto_digest_hmac(tfm, hmac_sha1_tv[i].key, strlen(hmac_sha1_tv[i].key),sg , 1, result);
		
		hexdump(result, sizeof(result));
		printk("%s\n", memcmp(result, hmac_sha1_tv[i].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");
	}

	printk("\ntesting hmac_sha1 across pages\n");
	
	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof(xbuf));
	
	memcpy(&xbuf[IDX1], "what do ya want ", 16);
	memcpy(&xbuf[IDX2], "for nothing?", 12);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 16;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 12;
	
	memset(result, 0, sizeof(result));
	crypto_digest_hmac(tfm, hmac_sha1_tv[1].key, strlen(hmac_sha1_tv[1].key), sg, 2, result);
	hexdump(result, crypto_tfm_digestsize(tfm));
	
	printk("%s\n", memcmp(result, hmac_sha1_tv[1].digest, crypto_tfm_digestsize(tfm)) ? "fail" : "pass");
	crypto_free_tfm(tfm);	
}

void test_des(void)
{
	int ret, i, len;
	size_t tsize;
	char *p, *q;
	struct crypto_tfm *tfm;
	char *key;
	char res[8];
	struct des_tv *des_tv;
	struct scatterlist sg[8];

	printk("\ntesting des encryption\n");
	
	tsize = sizeof(des_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	
	memcpy(tvmem, des_enc_tv_template, tsize);
	des_tv = (void *)tvmem;

	tfm = crypto_alloc_tfm(CRYPTO_ALG_DES_ECB);
	if (tfm == NULL) {
		printk("failed to load transform for CRYPTO_ALG_DES_ECB\n");
		return;
	}

	for (i = 0; i < DES_ENC_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);

		key = des_tv[i].key;
	
		tfm->crt_flags = CRYPTO_WEAK_KEY_CHECK;
		ret = crypto_cipher_setkey(tfm, key, 8);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			
			if (!des_tv[i].fail)
				goto out;
		}

		len = des_tv[i].len;
		
		p = des_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = len;
		ret = crypto_cipher_encrypt(tfm, sg, 1);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}
		
		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);
		
		printk("%s\n", memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	
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
	memset(xbuf, 0, sizeof(xbuf));
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 8);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 8 , 8);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 8;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 8;
	
	ret = crypto_cipher_encrypt(tfm, sg, 2);
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
	memset(xbuf, 0, sizeof(xbuf));
	
	/* Frag 1: 8 + 6 */
	memcpy(&xbuf[IDX3], des_tv[i].plaintext, 14);
	
	/* Frag 2: 2 + 8 */
	memcpy(&xbuf[IDX4], des_tv[i].plaintext + 14, 10);
	
	/* Frag 3: 8 */
	memcpy(&xbuf[IDX5], des_tv[i].plaintext + 24, 8);
	
	p = &xbuf[IDX3];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 14;
	
	p = &xbuf[IDX4];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 10;

	p = &xbuf[IDX5];
	sg[2].page = virt_to_page(p);
	sg[2].offset = ((long)p & ~PAGE_MASK);
	sg[2].length = 8;
	
	ret = crypto_cipher_encrypt(tfm, sg, 3);

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
	memset(xbuf, 0, sizeof(xbuf));
	
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
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 2;
	
	p = &xbuf[IDX4];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 1;

	p = &xbuf[IDX5];
	sg[2].page = virt_to_page(p);
	sg[2].offset = ((long)p & ~PAGE_MASK);
	sg[2].length = 3;

	p = &xbuf[IDX6];
	sg[3].page = virt_to_page(p);
	sg[3].offset = ((long)p & ~PAGE_MASK);
	sg[3].length = 18;
	
	ret = crypto_cipher_encrypt(tfm, sg, 4);

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
	memset(xbuf, 0, sizeof(xbuf));
	
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
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 2;
	
	p = &xbuf[IDX4];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 2;

	p = &xbuf[IDX5];
	sg[2].page = virt_to_page(p);
	sg[2].offset = ((long)p & ~PAGE_MASK);
	sg[2].length = 2;

	p = &xbuf[IDX6];
	sg[3].page = virt_to_page(p);
	sg[3].offset = ((long)p & ~PAGE_MASK);
	sg[3].length = 2;

	p = &xbuf[IDX7];
	sg[4].page = virt_to_page(p);
	sg[4].offset = ((long)p & ~PAGE_MASK);
	sg[4].length = 8;
	
	ret = crypto_cipher_encrypt(tfm, sg, 5);

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
	
	printk("\ntesting des ecb encryption chunking scenario D (atomic)\n");

	/*
	 * Scenario D, torture test, one byte per frag.
	 */
	i = 7;
	key = des_tv[i].key;
	tfm->crt_flags = CRYPTO_ATOMIC;
	
	ret = crypto_cipher_setkey(tfm, key, 8);
	if (ret) {
		printk("setkey() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}
	
	/* setup the dummy buffer first */
	memset(xbuf, 0, sizeof(xbuf));
	
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
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 1;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 1;

	p = &xbuf[IDX3];
	sg[2].page = virt_to_page(p);
	sg[2].offset = ((long)p & ~PAGE_MASK);
	sg[2].length = 1;

	p = &xbuf[IDX4];
	sg[3].page = virt_to_page(p);
	sg[3].offset = ((long)p & ~PAGE_MASK);
	sg[3].length = 1;

	p = &xbuf[IDX5];
	sg[4].page = virt_to_page(p);
	sg[4].offset = ((long)p & ~PAGE_MASK);
	sg[4].length = 1;

	p = &xbuf[IDX6];
	sg[5].page = virt_to_page(p);
	sg[5].offset = ((long)p & ~PAGE_MASK);
	sg[5].length = 1;

	p = &xbuf[IDX7];
	sg[6].page = virt_to_page(p);
	sg[6].offset = ((long)p & ~PAGE_MASK);
	sg[6].length = 1;

	p = &xbuf[IDX8];
	sg[7].page = virt_to_page(p);
	sg[7].offset = ((long)p & ~PAGE_MASK);
	sg[7].length = 1;
	
	ret = crypto_cipher_encrypt(tfm, sg, 8);

	if (ret) {
		printk("encrypt() failed flags=%x\n", tfm->crt_flags);
		goto out;
	}

	for (i = 0; i < 8; i++)
		res[i] = *(char *)(kmap(sg[i].page) + sg[i].offset);

	hexdump(res, 8);
	printk("%s\n", memcmp(res, des_tv[7].result, 8) ? "fail" : "pass");


	printk("\ntesting des decryption\n");

	tsize = sizeof(des_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	memcpy(tvmem, des_dec_tv_template, tsize);
	des_tv = (void *)tvmem;

	for (i = 0; i < DES_DEC_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);

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
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = len;
		
		ret = crypto_cipher_decrypt(tfm, sg, 1);
		if (ret) {
			printk("des_decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}
		
		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);
		
		printk("%s\n", memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	
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
	memset(xbuf, 0, sizeof(xbuf));
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 8);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 8 , 8);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 8;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 8;
	
	ret = crypto_cipher_decrypt(tfm, sg, 2);
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
	memset(xbuf, 0, sizeof(xbuf));
	
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 3);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 3, 12);
	memcpy(&xbuf[IDX3], des_tv[i].plaintext + 15, 1);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 3;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 12;

	p = &xbuf[IDX3];
	sg[2].page = virt_to_page(p);
	sg[2].offset = ((long)p & ~PAGE_MASK);
	sg[2].length = 1;
	
	ret = crypto_cipher_decrypt(tfm, sg, 3);

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

	tfm = crypto_alloc_tfm(CRYPTO_ALG_DES_CBC);
	if (tfm == NULL) {
		printk("failed to load transform for CRYPTO_ALG_DES_CBC\n");
		return;
	}

	printk("\ntesting des cbc encryption (atomic)\n");

	tsize = sizeof(des_cbc_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	memcpy(tvmem, des_cbc_enc_tv_template, tsize);
	des_tv = (void *)tvmem;

	for (i = 0; i < DES_CBC_ENC_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);

		tfm->crt_flags = CRYPTO_ATOMIC;
		key = des_tv[i].key;
		
		ret = crypto_cipher_setkey(tfm, key, 8);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}
		
		len = des_tv[i].len;
		p = des_tv[i].plaintext;
		
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = len;

		crypto_cipher_copy_iv(tfm, des_tv[i].iv, crypto_tfm_ivsize(tfm));
		
		ret = crypto_cipher_encrypt(tfm, sg, 1);
		if (ret) {
			printk("des_cbc_encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}
		
		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);
		
		printk("%s\n", memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	}

	/*
	 * Scenario F:
	 * 
	 *  F1       F2      
	 *  [8 + 5]  [3 + 8]
	 *
	 */
	printk("\ntesting des cbc encryption chunking scenario F\n"); 
	i = 4;
	
	tfm = crypto_alloc_tfm(CRYPTO_ALG_DES_CBC);
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
	memset(xbuf, 0, sizeof(xbuf));
	
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 13);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 13, 11);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 13;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 11;
	
	crypto_cipher_copy_iv(tfm, des_tv[i].iv, crypto_tfm_ivsize(tfm));
	
	ret = crypto_cipher_encrypt(tfm, sg, 2);
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


	tsize = sizeof(des_cbc_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	memcpy(tvmem, des_cbc_dec_tv_template, tsize);
	des_tv = (void *)tvmem;

	printk("\ntesting des cbc decryption\n");

	for (i = 0; i < DES_CBC_DEC_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);

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
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = len;

		crypto_cipher_copy_iv(tfm, des_tv[i].iv, crypto_tfm_blocksize(tfm));
		
		ret = crypto_cipher_decrypt(tfm, sg, 1);
		if (ret) {
			printk("des_cbc_decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}
		
		hexdump(tfm->crt_cipher.cit_iv, 8);
		
		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);
		
		printk("%s\n", memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
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
	memset(xbuf, 0, sizeof(xbuf));
	memcpy(&xbuf[IDX1], des_tv[i].plaintext, 4);
	memcpy(&xbuf[IDX2], des_tv[i].plaintext + 4, 4);
	
	p = &xbuf[IDX1];
	sg[0].page = virt_to_page(p);
	sg[0].offset = ((long)p & ~PAGE_MASK);
	sg[0].length = 4;
	
	p = &xbuf[IDX2];
	sg[1].page = virt_to_page(p);
	sg[1].offset = ((long)p & ~PAGE_MASK);
	sg[1].length = 4;
	
	crypto_cipher_copy_iv(tfm, des_tv[i].iv, crypto_tfm_ivsize(tfm));
	
	ret = crypto_cipher_decrypt(tfm, sg, 2);
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
	return;	
}

void test_des3_ede(void)
{
	int ret, i, len;
	size_t tsize;
	char *p, *q;
	struct crypto_tfm *tfm;
	char *key;
	/*char res[8];*/
	struct des_tv *des_tv;
	struct scatterlist sg[8];

	printk("\ntesting des3 ede encryption\n");
	
	tsize = sizeof(des3_ede_enc_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	
	memcpy(tvmem, des3_ede_enc_tv_template, tsize);
	des_tv = (void *)tvmem;

	tfm = crypto_alloc_tfm(CRYPTO_ALG_DES3_EDE_ECB);
	if (tfm == NULL) {
		printk("failed to load transform for CRYPTO_ALG_DES3_EDE_ECB\n");
		return;
	}

	for (i = 0; i < DES3_EDE_ENC_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);

		key = des_tv[i].key;
		
		tfm->crt_flags = CRYPTO_WEAK_KEY_CHECK;
		ret = crypto_cipher_setkey(tfm, key, 24);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			
			if (!des_tv[i].fail)
				goto out;
		}

		len = des_tv[i].len;
		
		p = des_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = len;
		ret = crypto_cipher_encrypt(tfm, sg, 1);
		if (ret) {
			printk("encrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}
		
		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);
		
		printk("%s\n", memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	}

	printk("\ntesting des3 ede decryption\n");
	
	tsize = sizeof(des3_ede_dec_tv_template);
	if (tsize > TVMEMSIZE) {
		printk("template (%Zd) too big for tvmem (%d)\n", tsize, TVMEMSIZE);
		return;
	}
	
	memcpy(tvmem, des3_ede_dec_tv_template, tsize);
	des_tv = (void *)tvmem;

	for (i = 0; i < DES3_EDE_DEC_TEST_VECTORS; i++) {
		printk("test %d:\n", i + 1);

		key = des_tv[i].key;
		
		tfm->crt_flags = CRYPTO_WEAK_KEY_CHECK;
		ret = crypto_cipher_setkey(tfm, key, 24);
		if (ret) {
			printk("setkey() failed flags=%x\n", tfm->crt_flags);
			
			if (!des_tv[i].fail)
				goto out;
		}

		len = des_tv[i].len;
		
		p = des_tv[i].plaintext;
		sg[0].page = virt_to_page(p);
		sg[0].offset = ((long)p & ~PAGE_MASK);
		sg[0].length = len;
		ret = crypto_cipher_decrypt(tfm, sg, 1);
		if (ret) {
			printk("decrypt() failed flags=%x\n", tfm->crt_flags);
			goto out;
		}
		
		q = kmap(sg[0].page) + sg[0].offset;
		hexdump(q, len);
		
		printk("%s\n", memcmp(q, des_tv[i].result, len) ? "fail" : "pass");
	}

	
out:
	crypto_free_tfm(tfm);
	return;	
}

static void do_test(void)
{
	switch (mode) {

	case 0:
		test_md5();
		test_sha1();
		test_des();
		test_des3_ede();
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
		
	default:
		/* useful for debugging */
		printk("not testing anything\n");
		break;
	}
}


static int __init init(void)
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
