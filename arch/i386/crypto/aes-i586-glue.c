/* 
 * 
 * Glue Code for optimized 586 assembler version of AES
 *
 * Copyright (c) 2001, Dr Brian Gladman <brg@gladman.uk.net>, Worcester, UK.
 * Copyright (c) 2003, Adam J. Richter <adam@yggdrasil.com> (conversion to
 * 2.5 API).
 * Copyright (c) 2003, 2004 Fruhwirth Clemens <clemens@endorphin.org>
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/linkage.h>

#define AES_MIN_KEY_SIZE	16
#define AES_MAX_KEY_SIZE	32
#define AES_BLOCK_SIZE		16
#define AES_KS_LENGTH   4 * AES_BLOCK_SIZE
#define AES_RC_LENGTH   (9 * AES_BLOCK_SIZE) / 8 - 8

typedef struct
{
    u_int32_t	 aes_Nkey;	// the number of words in the key input block
    u_int32_t	 aes_Nrnd;	// the number of cipher rounds
    u_int32_t	 aes_e_key[AES_KS_LENGTH];   // the encryption key schedule
    u_int32_t	 aes_d_key[AES_KS_LENGTH];   // the decryption key schedule
    u_int32_t	 aes_Ncol;	// the number of columns in the cipher state
} aes_context;

/*
 * The Cipher Interface
 */
 
asmlinkage void aes_set_key(void *, const unsigned char [], const int, const int);



/* Actually:
 * extern void aes_encrypt(const aes_context *, unsigned char [], const unsigned char []);
 * extern void aes_decrypt(const aes_context *, unsigned char [], const unsigned char []);
*/
 
asmlinkage void aes_encrypt(void*, unsigned char [], const unsigned char []);
asmlinkage void aes_decrypt(void*, unsigned char [], const unsigned char []);

static int aes_set_key_glue(void *cx, const u8 *key,unsigned int key_length, u32 *flags)
{
	if(key_length != 16 && key_length != 24 && key_length != 32)
	{
 		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}
	aes_set_key(cx, key,key_length,0);
	return 0;
}

#ifdef CONFIG_REGPARM
static void aes_encrypt_glue(void* a, unsigned char b[], const unsigned char c[]) {
	aes_encrypt(a,b,c);
}
static void aes_decrypt_glue(void* a, unsigned char b[], const unsigned char c[]) {
	aes_decrypt(a,b,c);
}
#else
#define aes_encrypt_glue aes_encrypt
#define aes_decrypt_glue aes_decrypt
#endif /* CONFIG_REGPARM */

static struct crypto_alg aes_alg = {
	.cra_name		=	"aes",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(aes_context),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(aes_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	AES_MIN_KEY_SIZE,
			.cia_max_keysize	=	AES_MAX_KEY_SIZE,
			.cia_setkey	   	= 	aes_set_key_glue,
			.cia_encrypt	 	=	aes_encrypt_glue,
			.cia_decrypt	  	=	aes_decrypt_glue
		}
	}
};

static int __init aes_init(void)
{
	return crypto_register_alg(&aes_alg);
}

static void __exit aes_fini(void)
{
	crypto_unregister_alg(&aes_alg);
}

module_init(aes_init);
module_exit(aes_fini);

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm, i586 asm optimized");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fruhwirth Clemens");
MODULE_ALIAS("aes");
