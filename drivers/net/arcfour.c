/*
 * arcfour.c
 * by Frank Cusack <frank@google.com>
 * 100% public domain
 *
 * Implemented from the description in _Applied Cryptography_, 2nd ed.
 *
 * ** Distribution ** of this software is unlimited and unrestricted.
 *
 * ** Use ** of this software is almost certainly legal; however, refer
 * to <http://theory.lcs.mit.edu/~rivest/faq.html>.
 */

#include "arcfour.h"
#if defined(__linux__)
#include <linux/string.h>
#endif

#define swap(a, b)		\
{				\
    unsigned char t = b;	\
    b = a;			\
    a = t;			\
}

/*
 * Initialize arcfour from a key.
 */
void
arcfour_setkey(arcfour_context *context, const unsigned char *key,
	       unsigned keylen)
{
    unsigned i, j;
    unsigned char K[256];

    context->i = context->j = 0;

    for (i = 0; i < 256; i++) {
	context->S[i] = i;
	K[i] = key[i % keylen];
    }

    j = 0;
    for (i = 0; i < 256; i++) {
	j = (j + context->S[i] + K[i]) % 256;
	swap(context->S[i], context->S[j]);
    }

    memset(K, 0, sizeof(K));
}

/*
 * plaintext -> ciphertext (or vice versa)
 */
void
arcfour_encrypt(arcfour_context *context, const unsigned char *in, unsigned len,
		unsigned char *out)
{
    unsigned i = context->i;
    unsigned j = context->j;
    unsigned char *S = context->S;
    unsigned char K;

    while (len--) {
	i = (i + 1) % 256;
	j = (j + S[i]) % 256;
	swap(S[i], S[j]);
	K = S[(S[i] + S[j]) % 256];
	*out++ = *in++ ^ K;
    }

    context->i = i;
    context->j = j;
}

