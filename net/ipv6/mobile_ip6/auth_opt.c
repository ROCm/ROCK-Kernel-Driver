/*
 *	MIPv6 Binding Authentication Data Option functions
 *	
 *      Authors: 
 *      Henrik Petander         <lpetande@tml.hut.fi>
 * 
 *      $Id: s.auth_opt.c 1.35 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/autoconf.h>
#include <linux/icmpv6.h>
#include <linux/module.h>
#include <net/mipv6.h>

#include "debug.h"
#include "hmac.h"
#include "mobhdr.h"

#define DBG_KEY 5

int mipv6_auth_build(struct in6_addr *cn_addr, struct in6_addr *coa, 
		     __u8 *mh, __u8 *aud_data, __u8 *k_bu)
{
	/* First look up the peer from sadb based on his address */ 
	struct ah_processing ahp;

	/* Don't add any other options or this system is screwed */

	__u8 buf[MAX_HASH_LENGTH];  
	
	
	if (!k_bu) {
		DEBUG(DBG_ERROR, "k_bu missing, aborting");
		return -1;
	}
	DEBUG(DBG_KEY, "Key for building authenticator:");
	debug_print_buffer(DBG_KEY, k_bu, HMAC_SHA1_KEY_SIZE);

	if (ah_hmac_sha1_init(&ahp, k_bu,  HMAC_SHA1_KEY_SIZE) < 0) {
		DEBUG(DBG_ERROR, "Failed to initialize hmac sha1");
                return -1; 
        } 

	DEBUG(DBG_KEY, "coa: ");
	debug_print_buffer(DBG_KEY, coa, 16);
	DEBUG(DBG_KEY, "cn_addr: ");
	debug_print_buffer(DBG_KEY, cn_addr, 16);
	DEBUG(DBG_KEY, "MH contents: ");
	debug_print_buffer(DBG_KEY, mh, aud_data - mh);

	/* First the common part */
	ah_hmac_sha1_loop(&ahp, coa, sizeof(struct in6_addr));
	ah_hmac_sha1_loop(&ahp, cn_addr, sizeof(struct in6_addr));
	ah_hmac_sha1_loop(&ahp, mh, aud_data - mh);
	ah_hmac_sha1_result(&ahp, buf);

	memcpy(aud_data, buf,  MIPV6_RR_MAC_LENGTH);

	return 0;
}

int mipv6_auth_check(struct in6_addr *cn_addr, struct in6_addr *coa,
		     __u8 *opt, __u8 optlen, 
		     struct mipv6_mo_bauth_data *aud, __u8 *k_bu)
{
	int ret = -1;
	struct ah_processing ahp;
	__u8 htarget[MAX_HASH_LENGTH];

	/* Look up peer by home address */ 
	if (!k_bu) {
		DEBUG(DBG_ERROR, "k_bu missing, aborting"); 
		return -1;
	}

	DEBUG(DBG_KEY, "Key for checking authenticator:");
	debug_print_buffer(DBG_KEY, k_bu, HMAC_SHA1_KEY_SIZE);

	if (!aud || !coa) {
		DEBUG(DBG_INFO, "%s is NULL", aud ? "coa" : "aud");
		goto out;
	}

	if (aud->length != MIPV6_RR_MAC_LENGTH) {
		DEBUG(DBG_ERROR,
			 ": Incorrect authentication option length %d", aud->length); 
		goto out; 
	}
	
	if (ah_hmac_sha1_init(&ahp, k_bu, HMAC_SHA1_KEY_SIZE) < 0) { 
                DEBUG(DBG_ERROR,
			 "internal error in initialization of authentication algorithm");
		goto out;
        } 
	DEBUG(DBG_KEY, "coa: ");
	debug_print_buffer(DBG_KEY, coa, 16);
	DEBUG(DBG_KEY, "cn_addr: ");
	debug_print_buffer(DBG_KEY, cn_addr, 16);
	DEBUG(DBG_KEY, "MH contents: ");
	debug_print_buffer(DBG_KEY, opt, (u8*) aud->data - opt);

	ah_hmac_sha1_loop(&ahp, coa, sizeof(struct in6_addr));
	ah_hmac_sha1_loop(&ahp, cn_addr, sizeof(struct in6_addr));

	/* 
	 * Process MH + options till the start of the authenticator in
	 * Auth. data option
	 */
	ah_hmac_sha1_loop(&ahp, opt,  (u8 *)aud->data - opt);
	ah_hmac_sha1_result(&ahp, htarget);
	if (memcmp(htarget, aud->data, MIPV6_RR_MAC_LENGTH) == 0)
		ret = 0;

	DEBUG(DBG_ERROR, "returning %d", ret);
out:	
	return ret;
}

EXPORT_SYMBOL(mipv6_auth_check);
