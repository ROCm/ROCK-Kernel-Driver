/*
 *      rr_cookie.c - Mobile IPv6 return routability crypto  
 *      Author :  Henrik Petander <henrik.petander@hut.fi>
 * 
 *      $Id: s.rr_crypto.c 1.20 03/04/10 13:02:40+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/module.h>

#include <net/ipv6.h>

#include "debug.h"
#include "hmac.h"
#include "rr_crypto.h"

#define DBG_RR 5

u8 k_CN[HMAC_SHA1_KEY_SIZE]; // secret key of CN 

u16 curr_index = 0;

struct nonce_timestamp nonce_table[MAX_NONCES];
spinlock_t nonce_lock = SPIN_LOCK_UNLOCKED;
void update_nonces(void);

/** nonce_is_fresh - whether the nonce was generated recently
 *  
 * @non_ts : table entry containing the nonce and a timestamp
 * @interval : if nonce was generated within interval seconds it is fresh
 *
 * Returns 1 if the nonce is fresh, 0 otherwise.
 */
static int nonce_is_fresh(struct nonce_timestamp *non_ts, unsigned long interval)
{
	if (time_before(jiffies, non_ts->timestamp + interval * HZ))
		return 1;
	return 0;
}

/* Returns a pointer to a new nonce  */
struct mipv6_rr_nonce * mipv6_rr_get_new_nonce(void)
{
	struct mipv6_rr_nonce *nce = kmalloc(sizeof(*nce), GFP_ATOMIC);

	if (!nce)
		return NULL;
	// Lock nonces here
	spin_lock_bh(&nonce_lock);
	// If nonce is not fresh create new one 
	if (!nonce_is_fresh(&nonce_table[curr_index], MIPV6_RR_NONCE_LIFETIME)) {
		// increment the last nonce pointer and create new nonce
		curr_index++;
		// Wrap around
		if (curr_index == MAX_NONCES)
			curr_index = 0;
		// Get random data to fill the nonce data
		get_random_bytes(nonce_table[curr_index].nonce.data, MIPV6_RR_NONCE_DATA_LENGTH);
		// Fill the index field
		nonce_table[curr_index].nonce.index = curr_index;
		nonce_table[curr_index].timestamp = jiffies;
	}
	spin_unlock_bh(&nonce_lock);
	memcpy(nce, &nonce_table[curr_index].nonce, sizeof(*nce));
	// Unlock nonces
	return nce;
}
/** mipv6_rr_nonce_get_by_index - returns a nonce for index 
 * @nonce_ind : index of the nonce
 *
 * Returns a nonce or NULL if the nonce index was invalid or the nonce 
 * for the index was not fresh.
 */
struct mipv6_rr_nonce * mipv6_rr_nonce_get_by_index(u16 nonce_ind)
{
	struct mipv6_rr_nonce *nce = NULL;
	
	spin_lock_bh(&nonce_lock);
	if (nonce_ind >= MAX_NONCES) {
		DEBUG(DBG_WARNING, "Nonce index field from BU invalid");

		/* Here a double of the nonce_lifetime is used for freshness 
		 * verification, since the nonces 
		 * are not created in response to every initiator packet
		 */
	} else if (nonce_is_fresh(&nonce_table[nonce_ind], 2 * MIPV6_RR_NONCE_LIFETIME)) {
		nce = kmalloc(sizeof(*nce), GFP_ATOMIC);
		memcpy(nce, &nonce_table[nonce_ind].nonce, sizeof(*nce));
	}
	spin_unlock_bh(&nonce_lock);

	return nce;
}

/* Fills rr test init cookies with random bytes */  
void mipv6_rr_mn_cookie_create(u8 *cookie)
{
	get_random_bytes(cookie, MIPV6_RR_COOKIE_LENGTH);
}

/** mipv6_rr_cookie_create - builds a home or care-of cookie
 * 
 * @addr : the home or care-of address from HoTI or CoTI
 * @ckie : memory where the cookie is copied to
 * @nce : pointer to a nonce used for the calculation, nce is freed during the function
 *
 */
int mipv6_rr_cookie_create(struct in6_addr *addr, u8 **ckie,
	       	u16 nonce_index)
{
	struct ah_processing ah_proc;
	u8 digest[HMAC_SHA1_HASH_LEN];
	struct mipv6_rr_nonce *nce;

	if ((nce = mipv6_rr_nonce_get_by_index(nonce_index))== NULL)
		return -1;

	if (*ckie == NULL && (*ckie = kmalloc(MIPV6_RR_COOKIE_LENGTH,
				       	GFP_ATOMIC)) == NULL) {
		kfree(nce);
		return -1;
	}
	/* Calculate the full hmac-sha1 digest from address and nonce using the secret key of cn */
	
	if (ah_hmac_sha1_init(&ah_proc, k_CN, HMAC_SHA1_KEY_SIZE) < 0) {
		DEBUG(DBG_ERROR, "Hmac sha1 initialization failed");
		kfree(nce);
		return -1;
	}

	ah_hmac_sha1_loop(&ah_proc, addr, sizeof(*addr));
	ah_hmac_sha1_loop(&ah_proc, nce->data,  MIPV6_RR_NONCE_DATA_LENGTH);
	ah_hmac_sha1_result(&ah_proc, digest);

	
	/* clean up nonce */
	kfree(nce);

	/* Copy first 64 bits of hash target to the cookie */ 
	memcpy(*ckie, digest, MIPV6_RR_COOKIE_LENGTH);
	return 0;
}

/** mipv6_rr_key_calc - creates BU authentication key
 * 
 * @hoc : Home Cookie 
 * @coc : Care-of Cookie 
 * 
 * Returns BU authentication key of length HMAC_SHA1_KEY_SIZE  or NULL in error cases, 
 * caller needs to free the key.
 */
u8 *mipv6_rr_key_calc(u8 *hoc, u8 *coc)
{
	
	u8 *key_bu = kmalloc(HMAC_SHA1_KEY_SIZE, GFP_ATOMIC);
	SHA1_CTX c;

	if (!key_bu) {
		DEBUG(DBG_CRITICAL, "Memory allocation failed, could nort create BU authentication key");
		return NULL;
	}

	/* Calculate the key from home and care-of cookies 
	 * Kbu = sha1(home_cookie | care-of cookie) 
	 * or KBu = sha1(home_cookie), if MN deregisters
	 */
	sha1_init(&c);
	sha1_compute(&c, hoc, MIPV6_RR_COOKIE_LENGTH);
	if (coc)
		sha1_compute(&c, coc, MIPV6_RR_COOKIE_LENGTH);
	sha1_final(&c, key_bu);
	DEBUG(DBG_RR, "Home and Care-of cookies used for calculating key ");
	debug_print_buffer(DBG_RR, hoc,  MIPV6_RR_COOKIE_LENGTH);
	if (coc)	
		debug_print_buffer(DBG_RR, coc,  MIPV6_RR_COOKIE_LENGTH);

	return key_bu;
}

void mipv6_rr_init(void)
{
	get_random_bytes(k_CN, HMAC_SHA1_KEY_SIZE);
	memset(nonce_table, 0, MAX_NONCES * sizeof(struct nonce_timestamp));
}

#ifdef TEST_MIPV6_RR_CRYPTO
void mipv6_test_rr(void)
{
	struct mipv6_rr_nonce *nonce;
	struct in6_addr a1, a2;
	int ind1, ind2;
	u8 *ckie1 = NULL, *ckie2 = NULL;
	u8 *key_mn = NULL, *key_cn = NULL;
	mipv6_init_rr();

	nonce = mipv6_rr_get_new_nonce();
	if (!nonce) {
		printk("mipv6_rr_get_new_nonce() failed, at 1! \n");
		return;
	}
	mipv6_rr_cookie_create(&a1, &ckie1, nonce->index);
	ind1 = nonce->index;
	kfree(nonce);

	nonce = mipv6_rr_get_new_nonce();
	if (!nonce) {
		printk("mipv6_rr_get_new_nonce() failed, at 2! \n");
		return;
	}

	mipv6_rr_cookie_create(&a2, &ckie2, nonce->index); 
	ind2 = nonce->index;
	key_mn =  mipv6_rr_key_calc(ckie1, ckie2);

	/* Create home and coa cookies based on indices */
	mipv6_rr_cookie_create(&a1, &ckie1, ind1);
	mipv6_rr_cookie_create(&a2, &ckie2, ind2);
	key_cn =  mipv6_rr_key_calc(ckie1, ckie2);	       
	if (!key_cn || !key_mn) {
		printk("creation of secret key failed!\n");
		return;
	}
	if(memcmp(key_cn, key_mn, HMAC_SHA1_KEY_SIZE))
		printk("mipv6_rr_key_calc produced different keys for MN and CN \n");
	else
		printk("mipv6_rr_crypto test OK\n");
	kfree(nonce);
	kfree(key_cn);
	kfree(key_mn);
}
#endif
EXPORT_SYMBOL(mipv6_rr_key_calc);
EXPORT_SYMBOL(mipv6_rr_mn_cookie_create);
