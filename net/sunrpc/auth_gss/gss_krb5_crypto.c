/*
 *  linux/net/sunrpc/gss_krb5_crypto.c
 *
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 *  Bruce Fields   <bfields@umich.edu>
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include <linux/sunrpc/gss_krb5.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

u32
krb5_encrypt(
	struct crypto_tfm *tfm,
	void * iv,
	void * in,
	void * out,
	int length)
{
	u32 ret = -EINVAL;
        struct scatterlist sg[1];
	u8 local_iv[16] = {0};

	dprintk("RPC: gss_k5encrypt: TOP in %p out %p\nin data:\n", out, in);
	print_hexl((u32 *)in, length, 0);

	if (length % crypto_tfm_alg_blocksize(tfm) != 0)
		goto out;

	if (crypto_tfm_alg_ivsize(tfm) > 16) {
		dprintk("RPC: gss_k5encrypt: tfm iv size to large %d\n",
		         crypto_tfm_alg_ivsize(tfm));
		goto out;
	}

	if (iv)
		memcpy(local_iv, iv, crypto_tfm_alg_ivsize(tfm));
	crypto_cipher_set_iv(tfm, local_iv, crypto_tfm_alg_ivsize(tfm));

	memcpy(out, in, length);
	sg[0].page = virt_to_page(out);
	sg[0].offset = offset_in_page(out);
	sg[0].length = length;

	ret = crypto_cipher_encrypt(tfm, sg, sg, length);

out:
	dprintk("gss_k5encrypt returns %d\n",ret);
	return(ret);
}

u32
krb5_decrypt(
     struct crypto_tfm *tfm,
     void * iv,
     void * in,
     void * out,
     int length)
{
	u32 ret = -EINVAL;
	struct scatterlist sg[1];
	u8 local_iv[16] = {0};

	dprintk("RPC: gss_k5decrypt: TOP in %p out %p\nin data:\n", in, out);
	print_hexl((u32 *)in,length,0);

	if (length % crypto_tfm_alg_blocksize(tfm) != 0)
		goto out;

	if (crypto_tfm_alg_ivsize(tfm) > 16) {
		dprintk("RPC: gss_k5decrypt: tfm iv size to large %d\n",
			crypto_tfm_alg_ivsize(tfm));
		goto out;
	}
	if (iv)
		memcpy(local_iv,iv, crypto_tfm_alg_ivsize(tfm));
	crypto_cipher_set_iv(tfm, local_iv, crypto_tfm_alg_blocksize(tfm));

	memcpy(out, in, length);
	sg[0].page = virt_to_page(out);
	sg[0].offset = offset_in_page(out);
	sg[0].length = length;

	ret = crypto_cipher_decrypt(tfm, sg, sg, length);

out:
	dprintk("gss_k5decrypt returns %d\n",ret);
	return(ret);
}

s32
krb5_make_checksum(s32 cksumtype, struct xdr_netobj *input,
		   struct xdr_netobj *cksum)
{
	s32			ret = -EINVAL;
	struct scatterlist	sg[1];
	char			*cksumname;
	struct crypto_tfm	*tfm;

	switch (cksumtype) {
		case CKSUMTYPE_RSA_MD5:
			cksumname = "md5";
			break;
		default:
			dprintk("RPC: krb5_make_checksum:"
				" unsupported checksum %d", cksumtype);
			goto out;
	}
	if (!(tfm = crypto_alloc_tfm(cksumname, 0)))
		goto out;
	cksum->len = crypto_tfm_alg_digestsize(tfm);

	if ((cksum->data = kmalloc(cksum->len, GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto out_free_tfm;
	}
	sg[0].page = virt_to_page(input->data);
	sg[0].offset = offset_in_page(input->data);
	sg[0].length = input->len;

	crypto_digest_init(tfm);
	crypto_digest_update(tfm, sg, 1);
	crypto_digest_final(tfm, cksum->data);

	ret = 0;

out_free_tfm:
	crypto_free_tfm(tfm);
out:
	dprintk("RPC: gss_k5cksum: returning %d\n", ret);
	return (ret);
}
