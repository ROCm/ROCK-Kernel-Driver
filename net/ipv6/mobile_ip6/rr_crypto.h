/*
 *      MIPL Mobile IPv6 Return routability crypto prototypes
 *
 *      $Id:$
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _RR_CRYPTO
#define _RR_CRYPTO

#include <linux/in6.h>

/* Macros and data structures */

#define MIPV6_RR_NONCE_LIFETIME 60 
#define MIPV6_RR_NONCE_DATA_LENGTH 8
#define MIPV6_RR_COOKIE_LENGTH 8
#define COOKIE_SIZE 8
#define MAX_NONCES 4
#define HMAC_SHA1_KEY_SIZE 20
 
struct mipv6_rr_nonce {
	u_int16_t index;
	u_int8_t data[MIPV6_RR_NONCE_DATA_LENGTH];
};

struct nonce_timestamp {
	struct  mipv6_rr_nonce nonce;
	unsigned long timestamp;
};

/* Function definitions */

/* Return 1 if equal, 0 if not */
static __inline__ int mipv6_equal_cookies(u8 *c1, u8 *c2)
{
	return (memcmp(c1, c2, MIPV6_RR_COOKIE_LENGTH) == 0);
}

/* Function declarations */

/* Create cookie for HoTi and CoTi */
extern void mipv6_rr_mn_cookie_create(u8 *cookie);

/* Create cookie for HoT and CoT */
extern int mipv6_rr_cookie_create(struct in6_addr *addr, u8 **ckie, u16 nonce_index);

/* Calculate return routability key from home and care-of cookies, key length is 
 *  HMAC_SHA1_KEY_SIZE  
 */
extern u_int8_t *mipv6_rr_key_calc(u8 *hoc, u8 *coc);

extern struct mipv6_rr_nonce *mipv6_rr_get_new_nonce(void);

/*
 * initializes the return routability crypto
 */

void mipv6_rr_init(void);

#ifdef TEST_MIPV6_RR_CRYPTO
void mipv6_test_rr(void);
#endif /* TEST_MIPV6_RR_CRYPTO */

#endif /* RR_CRYPTO */
