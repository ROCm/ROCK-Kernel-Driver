/*
 *      MIPL Mobile IPv6 Message authentication algorithms        
 * 
 *      $Id: s.hmac.h 1.9 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _HMAC_H
#define _HMAC_H

#include <linux/types.h>
#include <linux/in6.h>

#define HAVE_LITTLE_ENDIAN

#define NO_EXPIRY 1  /* For sec_as */

#define ALG_AUTH_NONE           0
#define ALG_AUTH_HMAC_MD5       1
#define ALG_AUTH_HMAC_SHA1      2

struct sec_as;
struct ah_processing {
	void *context;
	struct sec_as *sas;
	u_int8_t *key_auth;
	u_int32_t key_auth_len;
};

struct antireplay {
	u_int32_t count;
	u_int32_t bitmap; 
};

typedef struct {
  u_int32_t A, B, C, D;
  u_int32_t bitlen[2];
  u_int8_t* buf_cur;
  u_int8_t buf[64];
} MD5_CTX;

typedef struct {
  u_int32_t A, B, C, D, E;
  u_int32_t bitlen[2];
  u_int8_t* buf_cur;
  u_int8_t buf[64];
} SHA1_CTX;



int ah_hmac_md5_init (struct ah_processing *ahp, u_int8_t *key, u_int32_t key_len);
void ah_hmac_md5_loop(struct ah_processing*, void*, u_int32_t);
void ah_hmac_md5_result(struct ah_processing*, char*);
int ah_hmac_sha1_init(struct ah_processing*, u_int8_t *key, u_int32_t key_len);
void ah_hmac_sha1_loop(struct ah_processing*, void*, u_int32_t);
void ah_hmac_sha1_result(struct ah_processing*, char*);


#define AH_HDR_LEN 12   /* # of bytes for Next Header, Payload Length,
                           RESERVED, Security Parameters Index and

                           Sequence Number Field */

void md5_init(MD5_CTX *ctx);
void md5_over_block(MD5_CTX *ctx, u_int8_t* data);
void create_M_blocks(u_int32_t* M, u_int8_t* data);
void md5_compute(MD5_CTX *ctx, u_int8_t* data, u_int32_t len);
void md5_final(MD5_CTX *ctx, u_int8_t* digest);

void sha1_init(SHA1_CTX *ctx);
void sha1_over_block(SHA1_CTX *ctx, u_int8_t* data);
void create_W_blocks(u_int32_t* W, u_int8_t* data);
void sha1_compute(SHA1_CTX *ctx, u_int8_t* data, u_int32_t len);
void sha1_final(SHA1_CTX *ctx, u_int8_t* digest);

struct mipv6_acq {
	struct in6_addr coa;
	struct in6_addr haddr;
	struct in6_addr peer;
	u_int32_t spi;
};
#define MIPV6_MAX_AUTH_DATA 20

#define HMAC_MD5_HASH_LEN   16
#define HMAC_SHA1_HASH_LEN  20
#define HMAC_SHA1_KEY_SIZE  20
#define HMAC_MD5_ICV_LEN   12 /* RFC 2403 */
#define HMAC_SHA1_ICV_LEN  12 /* RFC 2404 */

#endif /* _HMAC_H */
