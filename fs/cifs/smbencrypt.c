/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-2000
   Copyright (C) Luke Kenneth Casson Leighton 1996-2000
   Modified by Jeremy Allison 1995.
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2002-2003
   Modified by Steve French (sfrench@us.ibm.com) 2002-2003
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

extern int DEBUGLEVEL;

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include "cifs_unicode.h"
#include "cifspdu.h"
#include "md5.h"
#include "cifs_debug.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* following came from the other byteorder.h to avoid include conflicts */
#define CVAL(buf,pos) (((unsigned char *)(buf))[pos])
#define SSVALX(buf,pos,val) (CVAL(buf,pos)=(val)&0xFF,CVAL(buf,pos+1)=(val)>>8)
#define SIVALX(buf,pos,val) (SSVALX(buf,pos,val&0xFFFF),SSVALX(buf,pos+2,val>>16))
#define SSVAL(buf,pos,val) SSVALX((buf),(pos),((__u16)(val)))
#define SIVAL(buf,pos,val) SIVALX((buf),(pos),((__u32)(val)))

/*The following definitions come from  lib/md4.c  */

void mdfour(unsigned char *out, unsigned char *in, int n);

/*The following definitions come from  libsmb/smbdes.c  */

void E_P16(unsigned char *p14, unsigned char *p16);
void E_P24(unsigned char *p21, unsigned char *c8, unsigned char *p24);
void D_P16(unsigned char *p14, unsigned char *in, unsigned char *out);
void E_old_pw_hash(unsigned char *p14, unsigned char *in, unsigned char *out);
void cred_hash1(unsigned char *out, unsigned char *in, unsigned char *key);
void cred_hash2(unsigned char *out, unsigned char *in, unsigned char *key);
void cred_hash3(unsigned char *out, unsigned char *in, unsigned char *key,
		int forw);

/*The following definitions come from  libsmb/smbencrypt.c  */

void SMBencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24);
void E_md4hash(const unsigned char *passwd, unsigned char *p16);
void nt_lm_owf_gen(char *pwd, unsigned char nt_p16[16], unsigned char p16[16]);
void SMBOWFencrypt(unsigned char passwd[16], unsigned char *c8,
		   unsigned char p24[24]);
void NTLMSSPOWFencrypt(unsigned char passwd[8],
		       unsigned char *ntlmchalresp, unsigned char p24[24]);
void SMBNTencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24);
int decode_pw_buffer(char in_buffer[516], char *new_pwrd,
		     int new_pwrd_size, __u32 * new_pw_len);

/*
   This implements the X/Open SMB password encryption
   It takes a password, a 8 byte "crypt key" and puts 24 bytes of 
   encrypted password into p24 */
/* Note that password must be uppercased and null terminated */
void
SMBencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24)
{
	unsigned char p14[15], p21[21];

	memset(p21, '\0', 21);
	memset(p14, '\0', 14);
	strncpy((char *) p14, (char *) passwd, 14);

/*	strupper((char *)p14); *//* BB at least uppercase the easy range */
	E_P16(p14, p21);

	SMBOWFencrypt(p21, c8, p24);
	
#ifdef DEBUG_PASSWORD
	DEBUG(100, ("SMBencrypt: lm#, challenge, response\n"));
	dump_data(100, (char *) p21, 16);
	dump_data(100, (char *) c8, 8);
	dump_data(100, (char *) p24, 24);
#endif
	memset(p14,0,15);
	memset(p21,0,21);
}

/* Routines for Windows NT MD4 Hash functions. */
static int
_my_wcslen(__u16 * str)
{
	int len = 0;
	while (*str++ != 0)
		len++;
	return len;
}

/*
 * Convert a string into an NT UNICODE string.
 * Note that regardless of processor type 
 * this must be in intel (little-endian)
 * format.
 */

static int
_my_mbstowcs(__u16 * dst, const unsigned char *src, int len)
{				/* not a very good conversion routine - change/fix */
	int i;
	__u16 val;

	for (i = 0; i < len; i++) {
		val = *src;
		SSVAL(dst, 0, val);
		dst++;
		src++;
		if (val == 0)
			break;
	}
	return i;
}

/* 
 * Creates the MD4 Hash of the users password in NT UNICODE.
 */

void
E_md4hash(const unsigned char *passwd, unsigned char *p16)
{
	int len;
	__u16 wpwd[129];

	/* Password cannot be longer than 128 characters */
	len = strlen((char *) passwd);
	if (len > 128)
		len = 128;
	/* Password must be converted to NT unicode */
	_my_mbstowcs(wpwd, passwd, len);
	wpwd[len] = 0;		/* Ensure string is null terminated */
	/* Calculate length in bytes */
	len = _my_wcslen(wpwd) * sizeof (__u16);

	mdfour(p16, (unsigned char *) wpwd, len);
	memset(wpwd,0,129 * 2);
}

/* Does both the NT and LM owfs of a user's password */
void
nt_lm_owf_gen(char *pwd, unsigned char nt_p16[16], unsigned char p16[16])
{
	char passwd[514];

	memset(passwd, '\0', 514);
	if (strlen(pwd) < 513)
		strcpy(passwd, pwd);
	else
		memcpy(passwd, pwd, 512);
	/* Calculate the MD4 hash (NT compatible) of the password */
	memset(nt_p16, '\0', 16);
	E_md4hash(passwd, nt_p16);

#ifdef DEBUG_PASSWORD
	DEBUG(100, ("nt_lm_owf_gen: pwd, nt#\n"));
	dump_data(120, passwd, strlen(passwd));
	dump_data(100, (char *) nt_p16, 16);
#endif

	/* Mangle the passwords into Lanman format */
	passwd[14] = '\0';
/*	strupper(passwd); */

	/* Calculate the SMB (lanman) hash functions of the password */

	memset(p16, '\0', 16);
	E_P16((unsigned char *) passwd, (unsigned char *) p16);

#ifdef DEBUG_PASSWORD
	DEBUG(100, ("nt_lm_owf_gen: pwd, lm#\n"));
	dump_data(120, passwd, strlen(passwd));
	dump_data(100, (char *) p16, 16);
#endif
	/* clear out local copy of user's password (just being paranoid). */
	memset(passwd, '\0', sizeof (passwd));
}

/* Does the NTLMv2 owfs of a user's password */
void
ntv2_owf_gen(const unsigned char owf[16], const char *user_n,
		const char *domain_n, unsigned char kr_buf[16],
		const struct nls_table *nls_codepage)
{
	wchar_t * user_u;
	wchar_t * dom_u;
	int user_l, domain_l;
	struct HMACMD5Context ctx;

	/* might as well do one alloc to hold both (user_u and dom_u) */
	user_u = kmalloc(2048 * sizeof(wchar_t),GFP_KERNEL); 
	if(user_u == NULL)
		return;
	dom_u = user_u + 1024;
    
	/* push_ucs2(NULL, user_u, user_n, (user_l+1)*2, STR_UNICODE|STR_NOALIGN|STR_TERMINATE|STR_UPPER);
	   push_ucs2(NULL, dom_u, domain_n, (domain_l+1)*2, STR_UNICODE|STR_NOALIGN|STR_TERMINATE|STR_UPPER); */

	/* BB user and domain may need to be uppercased */
	user_l = cifs_strtoUCS(user_u, user_n, 511, nls_codepage);
	domain_l = cifs_strtoUCS(dom_u, domain_n, 511, nls_codepage);

	user_l++;		/* trailing null */
	domain_l++;

	hmac_md5_init_limK_to_64(owf, 16, &ctx);
	hmac_md5_update((const unsigned char *) user_u, user_l * 2, &ctx);
	hmac_md5_update((const unsigned char *) dom_u, domain_l * 2, &ctx);
	hmac_md5_final(kr_buf, &ctx);

#ifdef DEBUG_PASSWORD
	DEBUG(100, ("ntv2_owf_gen: user, domain, owfkey, kr\n"));
	dump_data(100, user_u, user_l * 2);
	dump_data(100, dom_u, domain_l * 2);
	dump_data(100, owf, 16);
	dump_data(100, kr_buf, 16);
#endif
	kfree(user_u);
}

/* Does the des encryption from the NT or LM MD4 hash. */
void
SMBOWFencrypt(unsigned char passwd[16], unsigned char *c8,
	      unsigned char p24[24])
{
	unsigned char p21[21];

	memset(p21, '\0', 21);

	memcpy(p21, passwd, 16);
	E_P24(p21, c8, p24);
}

/* Does the des encryption from the FIRST 8 BYTES of the NT or LM MD4 hash. */
void
NTLMSSPOWFencrypt(unsigned char passwd[8],
		  unsigned char *ntlmchalresp, unsigned char p24[24])
{
	unsigned char p21[21];

	memset(p21, '\0', 21);
	memcpy(p21, passwd, 8);
	memset(p21 + 8, 0xbd, 8);

	E_P24(p21, ntlmchalresp, p24);
#ifdef DEBUG_PASSWORD
	DEBUG(100, ("NTLMSSPOWFencrypt: p21, c8, p24\n"));
	dump_data(100, (char *) p21, 21);
	dump_data(100, (char *) ntlmchalresp, 8);
	dump_data(100, (char *) p24, 24);
#endif
}

/* Does the NT MD4 hash then des encryption. */

void
SMBNTencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24)
{
	unsigned char p21[21];

	memset(p21, '\0', 21);

	E_md4hash(passwd, p21);
	SMBOWFencrypt(p21, c8, p24);

#ifdef DEBUG_PASSWORD
	DEBUG(100, ("SMBNTencrypt: nt#, challenge, response\n"));
	dump_data(100, (char *) p21, 16);
	dump_data(100, (char *) c8, 8);
	dump_data(100, (char *) p24, 24);
#endif
}

/* Does the md5 encryption from the NT hash for NTLMv2. */
void
SMBOWFencrypt_ntv2(const unsigned char kr[16],
                   const struct data_blob * srv_chal,
                   const struct data_blob * cli_chal, unsigned char resp_buf[16])
{
        struct HMACMD5Context ctx;

        hmac_md5_init_limK_to_64(kr, 16, &ctx);
        hmac_md5_update(srv_chal->data, srv_chal->length, &ctx);
        hmac_md5_update(cli_chal->data, cli_chal->length, &ctx);
        hmac_md5_final(resp_buf, &ctx);

#ifdef DEBUG_PASSWORD
        DEBUG(100, ("SMBOWFencrypt_ntv2: srv_chal, cli_chal, resp_buf\n"));
        dump_data(100, srv_chal->data, srv_chal->length);
        dump_data(100, cli_chal->data, cli_chal->length);
        dump_data(100, resp_buf, 16);
#endif
}

static struct data_blob LMv2_generate_response(const unsigned char ntlm_v2_hash[16],
                                        const struct data_blob * server_chal)
{
        unsigned char lmv2_response[16];
	struct data_blob lmv2_client_data/* = data_blob(NULL, 8)*/; /* BB Fix BB */
        struct data_blob final_response /* = data_blob(NULL, 24)*/; /* BB Fix BB */

        /* LMv2 */
        /* client-supplied random data */
        get_random_bytes(lmv2_client_data.data, lmv2_client_data.length);
        /* Given that data, and the challenge from the server, generate a response */
        SMBOWFencrypt_ntv2(ntlm_v2_hash, server_chal, &lmv2_client_data, lmv2_response);
        memcpy(final_response.data, lmv2_response, sizeof(lmv2_response));

        /* after the first 16 bytes is the random data we generated above,
           so the server can verify us with it */
        memcpy(final_response.data+sizeof(lmv2_response),
               lmv2_client_data.data, lmv2_client_data.length);

/*        data_blob_free(&lmv2_client_data); */ /* BB fix BB */

        return final_response;
}

void
SMBsesskeygen_ntv2(const unsigned char kr[16],
		   const unsigned char *nt_resp, __u8 sess_key[16])
{
	struct HMACMD5Context ctx;

	hmac_md5_init_limK_to_64(kr, 16, &ctx);
	hmac_md5_update(nt_resp, 16, &ctx);
	hmac_md5_final((unsigned char *) sess_key, &ctx);

#ifdef DEBUG_PASSWORD
	DEBUG(100, ("SMBsesskeygen_ntv2:\n"));
	dump_data(100, sess_key, 16);
#endif
}

void
SMBsesskeygen_ntv1(const unsigned char kr[16],
		   const unsigned char *nt_resp, __u8 sess_key[16])
{
	mdfour((unsigned char *) sess_key, (unsigned char *) kr, 16);

#ifdef DEBUG_PASSWORD
	DEBUG(100, ("SMBsesskeygen_ntv1:\n"));
	dump_data(100, sess_key, 16);
#endif
}

/***********************************************************
 encode a password buffer.  The caller gets to figure out 
 what to put in it.
************************************************************/
int
encode_pw_buffer(char buffer[516], char *new_pw, int new_pw_length)
{
	get_random_bytes(buffer, sizeof (buffer));

	memcpy(&buffer[512 - new_pw_length], new_pw, new_pw_length);

	/* 
	 * The length of the new password is in the last 4 bytes of
	 * the data buffer.
	 */
	SIVAL(buffer, 512, new_pw_length);

	return TRUE;
}

int SMBNTLMv2encrypt(const char *user, const char *domain, const char *password,
                      const struct data_blob *server_chal,
                      const struct data_blob *names_blob,
                      struct data_blob *lm_response, struct data_blob *nt_response,
                      struct data_blob *nt_session_key,struct nls_table * nls_codepage)
{
        unsigned char nt_hash[16];
        unsigned char ntlm_v2_hash[16];
        E_md4hash(password, nt_hash);

        /* We don't use the NT# directly.  Instead we use it mashed up with
           the username and domain.
           This prevents username swapping during the auth exchange
        */
        ntv2_owf_gen(nt_hash, user, domain, ntlm_v2_hash,nls_codepage);

        if (nt_response) {
/*                *nt_response = NTLMv2_generate_response(ntlm_v2_hash, server_chal,
                                                        names_blob); */ /* BB fix BB */
                if (nt_session_key) {
/*                        *nt_session_key = data_blob(NULL, 16); */ /* BB fix BB */

                        /* The NTLMv2 calculations also provide a session key, for signing etc later */
                        /* use only the first 16 bytes of nt_response for session key */
                        SMBsesskeygen_ntv2(ntlm_v2_hash, nt_response->data, nt_session_key->data);
                }
        }

        /* LMv2 */

        if (lm_response) {
                *lm_response = LMv2_generate_response(ntlm_v2_hash, server_chal);
        }

        return TRUE;
}
