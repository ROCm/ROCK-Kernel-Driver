/*
 *  ==FILEVERSION 20020521==
 *
 * ppp_mppe_compress.c - interface MPPE to the PPP code.
 * This version is for use with Linux kernel 2.2.19+ and 2.4.x.
 *
 * By Frank Cusack <frank@google.com>.
 * Copyright (c) 2002 Google, Inc.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, provided that the above copyright
 * notice appears in all copies.  This software is provided without any
 * warranty, express or implied.
 *
 * Changelog:
 *      2/15/04 - TS: added #include <version.h> and testing for Kernel
 *                    version before using 
 *                    MOD_DEC_USAGE_COUNT/MOD_INC_USAGE_COUNT which are
 *                    depreciated in 2.6
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/ppp_defs.h>
#include <linux/ppp-comp.h>

#include "arcfour.h"
#include "sha1.h"

/*
 * State for an MPPE (de)compressor.
 */
typedef struct ppp_mppe_state {
    unsigned char	master_key[MPPE_MAX_KEY_LEN];
    unsigned char	session_key[MPPE_MAX_KEY_LEN];
    arcfour_context	arcfour_context; /* encryption state */
    unsigned		keylen;		/* key length in bytes             */
					/* NB: 128-bit == 16, 40-bit == 8! */
					/* If we want to support 56-bit,   */
					/* the unit has to change to bits  */
    unsigned char	bits;		/* MPPE control bits */
    unsigned		ccount;		/* 12-bit coherency count (seqno)  */
    unsigned		stateful;	/* stateful mode flag */
    int			discard;	/* stateful mode packet loss flag */
    int			sanity_errors;	/* take down LCP if too many */
    int			unit;
    int			debug;
    struct compstat	stats;
} ppp_mppe_state;

/* ppp_mppe_state.bits definitions */
#define MPPE_BIT_A	0x80	/* Encryption table were (re)inititalized */
#define MPPE_BIT_B	0x40	/* MPPC only (not implemented) */
#define MPPE_BIT_C	0x20	/* MPPC only (not implemented) */
#define MPPE_BIT_D	0x10	/* This is an encrypted frame */

#define MPPE_BIT_FLUSHED	MPPE_BIT_A
#define MPPE_BIT_ENCRYPTED	MPPE_BIT_D

#define MPPE_BITS(p) ((p)[4] & 0xf0)
#define MPPE_CCOUNT(p) ((((p)[4] & 0x0f) << 8) + (p)[5])
#define MPPE_CCOUNT_SPACE 0x1000	/* The size of the ccount space */

#define MPPE_OVHD	2		/* MPPE overhead/packet */
#define SANITY_MAX	1600		/* Max bogon factor we will tolerate */

static void	GetNewKeyFromSHA __P((unsigned char *StartKey,
				      unsigned char *SessionKey,
				      unsigned SessionKeyLength,
				      unsigned char *InterimKey));
static void	mppe_rekey __P((ppp_mppe_state *state, int));
static void	*mppe_alloc __P((unsigned char *options, int optlen));
static void	mppe_free __P((void *state));
static int	mppe_init __P((void *state, unsigned char *options,
			       int optlen, int unit, int debug, const char *));
static int	mppe_comp_init __P((void *state, unsigned char *options,
				    int optlen,
				    int unit, int hdrlen, int debug));
static int	mppe_decomp_init __P((void *state, unsigned char *options,
				      int optlen, int unit,
				      int hdrlen, int mru, int debug));
static int	mppe_compress __P((void *state, unsigned char *ibuf,
				   unsigned char *obuf,
				   int isize, int osize));
static void	mppe_incomp __P((void *state, unsigned char *ibuf, int icnt));
static int	mppe_decompress __P((void *state, unsigned char *ibuf,
				     int isize, unsigned char *obuf,int osize));
static void	mppe_comp_reset __P((void *state));
static void	mppe_decomp_reset __P((void *state));
static void	mppe_comp_stats __P((void *state, struct compstat *stats));


/*
 * Key Derivation, from RFC 3078, RFC 3079.
 * Equivalent to Get_Key() for MS-CHAP as described in RFC 3079.
 */
static void
GetNewKeyFromSHA(unsigned char *MasterKey, unsigned char *SessionKey,
		 unsigned SessionKeyLength, unsigned char *InterimKey)
{
    SHA1_CTX Context;
    unsigned char Digest[SHA1_SIGNATURE_SIZE];

    unsigned char SHApad1[40] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    unsigned char SHApad2[40] =
    { 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
      0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
      0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2,
      0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2, 0xf2 };

    /* assert(SessionKeyLength <= SHA1_SIGNATURE_SIZE); */

    SHA1_Init(&Context);
    SHA1_Update(&Context, MasterKey, SessionKeyLength);
    SHA1_Update(&Context, SHApad1, sizeof(SHApad1));
    SHA1_Update(&Context, SessionKey, SessionKeyLength);
    SHA1_Update(&Context, SHApad2, sizeof(SHApad2));
    SHA1_Final(Digest, &Context);

    memcpy(InterimKey, Digest, SessionKeyLength);
}

/*
 * Perform the MPPE rekey algorithm, from RFC 3078, sec. 7.3.
 * Well, not what's written there, but rather what they meant.
 */
static void
mppe_rekey(ppp_mppe_state *state, int initial_key)
{
    unsigned char InterimKey[MPPE_MAX_KEY_LEN];

    GetNewKeyFromSHA(state->master_key, state->session_key,
		     state->keylen, InterimKey);
    if (!initial_key) {
	arcfour_setkey(&state->arcfour_context, InterimKey, state->keylen);
	arcfour_encrypt(&state->arcfour_context, InterimKey, state->keylen,
			state->session_key);
    } else {
	memcpy(state->session_key, InterimKey, state->keylen);
    }
    if (state->keylen == 8) {
	/* See RFC 3078 */
	state->session_key[0] = 0xd1;
	state->session_key[1] = 0x26;
	state->session_key[2] = 0x9e;
    }
    arcfour_setkey(&state->arcfour_context, state->session_key, state->keylen);
}


/*
 * Allocate space for a (de)compressor.
 */
static void *
mppe_alloc(unsigned char *options, int optlen)
{
    ppp_mppe_state *state;

    if (optlen != CILEN_MPPE + sizeof(state->master_key)
	|| options[0] != CI_MPPE
	|| options[1] != CILEN_MPPE)
	return NULL;

    state = (ppp_mppe_state *) kmalloc(sizeof(*state), GFP_KERNEL);
    if (state == NULL)
	return NULL;

/* 
   Added to avoid module warnings about MOD_INC 
   being depreciated in 2.6.x 
*/ 
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) )
    try_module_get(THIS_MODULE);
#else
    MOD_INC_USE_COUNT;
#endif
    memset(state, 0, sizeof(*state));

    /* Save keys. */
    memcpy(state->master_key, &options[CILEN_MPPE], sizeof(state->master_key));
    memcpy(state->session_key, state->master_key, sizeof(state->master_key));
    /*
     * We defer initial key generation until mppe_init(), as mppe_alloc()
     * is called frequently during negotiation.
     */

    return (void *) state;
}

/*
 * Deallocate space for a (de)compressor.
 */
static void
mppe_free(void *arg)
{
    ppp_mppe_state *state = (ppp_mppe_state *) arg;

    if (state) {
	kfree(state);
/* 
   Added to avoid module warnings about MOD_DEC_USE_COUNT 
   being depreciated in 2.6.x 
*/ 
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) )
        module_put(THIS_MODULE);
#else
	MOD_DEC_USE_COUNT;
#endif
    }
}


/* 
 * Initialize (de)compressor state.
 */
static int
mppe_init(void *arg, unsigned char *options, int optlen, int unit, int debug,
	  const char *debugstr)
{
    ppp_mppe_state *state = (ppp_mppe_state *) arg;
    unsigned char mppe_opts;

    if (optlen != CILEN_MPPE
	|| options[0] != CI_MPPE
	|| options[1] != CILEN_MPPE)
	return 0;

    MPPE_CI_TO_OPTS(&options[2], mppe_opts);
    if (mppe_opts & MPPE_OPT_128)
	state->keylen = 16;
    else if (mppe_opts & MPPE_OPT_40)
	state->keylen = 8;
    else {
	printk(KERN_WARNING "%s[%d]: unknown key length\n", debugstr, unit);
	return 0;
    }
    if (mppe_opts & MPPE_OPT_STATEFUL)
	state->stateful = 1;

    /* Generate the initial session key. */
    mppe_rekey(state, 1);

    if (debug) {
	int i;
	char mkey[sizeof(state->master_key) * 2 + 1];
	char skey[sizeof(state->session_key) * 2 + 1];

	printk(KERN_DEBUG "%s[%d]: initialized with %d-bit %s mode\n", debugstr,
	       unit, (state->keylen == 16)? 128: 40,
	       (state->stateful)? "stateful": "stateless");

	for (i = 0; i < sizeof(state->master_key); i++)
	    sprintf(mkey + i * 2, "%.2x", state->master_key[i]);
	for (i = 0; i < sizeof(state->session_key); i++)
	    sprintf(skey + i * 2, "%.2x", state->session_key[i]);
	printk(KERN_DEBUG "%s[%d]: keys: master: %s initial session: %s\n",
	       debugstr, unit, mkey, skey);
    }

    /*
     * Initialize the coherency count.  The initial value is not specified
     * in RFC 3078, but we can make a reasonable assumption that it will
     * start at 0.  Setting it to the max here makes the comp/decomp code
     * do the right thing (determined through experiment).
     */
    state->ccount = MPPE_CCOUNT_SPACE - 1;

    /*
     * Note that even though we have initialized the key table, we don't
     * set the FLUSHED bit.  This is contrary to RFC 3078, sec. 3.1.
     */
    state->bits = MPPE_BIT_ENCRYPTED;

    state->unit  = unit;
    state->debug = debug;

    return 1;
}



static int
mppe_comp_init(void *arg, unsigned char *options, int optlen, int unit,
	       int hdrlen, int debug)
{
    /* ARGSUSED */
    return mppe_init(arg, options, optlen, unit, debug, "mppe_comp_init");
}

/*
 * We received a CCP Reset-Request (actually, we are sending a Reset-Ack),
 * tell the compressor to rekey.  Note that we MUST NOT rekey for
 * every CCP Reset-Request; we only rekey on the next xmit packet.
 * We might get multiple CCP Reset-Requests if our CCP Reset-Ack is lost.
 * So, rekeying for every CCP Reset-Request is broken as the peer will not
 * know how many times we've rekeyed.  (If we rekey and THEN get another
 * CCP Reset-Request, we must rekey again.)
 */
static void
mppe_comp_reset(void *arg)
{
    ppp_mppe_state *state = (ppp_mppe_state *) arg;

    state->bits |= MPPE_BIT_FLUSHED;
}

/*
 * Compress (encrypt) a packet.
 * It's strange to call this a compressor, since the output is always
 * MPPE_OVHD + 2 bytes larger than the input.
 */
int
mppe_compress(void *arg, unsigned char *ibuf, unsigned char *obuf,
	      int isize, int osize)
{
    ppp_mppe_state *state = (ppp_mppe_state *) arg;
    int proto;

    /*
     * Check that the protocol is in the range we handle.
     */
    proto = PPP_PROTOCOL(ibuf);
    if (proto < 0x0021 || proto > 0x00fa)
	return 0;

    /* Make sure we have enough room to generate an encrypted packet. */
    if (osize < isize + MPPE_OVHD + 2) {
	/* Drop the packet if we should encrypt it, but can't. */
	printk(KERN_DEBUG "mppe_compress[%d]: osize too small! "
	       "(have: %d need: %d)\n", state->unit,
	       osize, osize + MPPE_OVHD + 2);
	return -1;
    }

    osize = isize + MPPE_OVHD + 2;

    /*
     * Copy over the PPP header and set control bits.
     */
    obuf[0] = PPP_ADDRESS(ibuf);
    obuf[1] = PPP_CONTROL(ibuf);
    obuf[2] = PPP_COMP >> 8;		/* isize + MPPE_OVHD + 1 */
    obuf[3] = PPP_COMP;			/* isize + MPPE_OVHD + 2 */
    obuf += PPP_HDRLEN;

    state->ccount = (state->ccount + 1) % MPPE_CCOUNT_SPACE;
    if (state->debug >= 7)
	printk(KERN_DEBUG "mppe_compress[%d]: ccount %d\n", state->unit,
	       state->ccount);
    obuf[0] = state->ccount >> 8;
    obuf[1] = state->ccount & 0xff;

    if (!state->stateful ||			/* stateless mode     */
	((state->ccount & 0xff) == 0xff) ||	/* "flag" packet      */
	(state->bits & MPPE_BIT_FLUSHED)) {	/* CCP Reset-Request  */
	/* We must rekey */
	if (state->debug && state->stateful)
	    printk(KERN_DEBUG "mppe_compress[%d]: rekeying\n", state->unit);
	mppe_rekey(state, 0);
	state->bits |= MPPE_BIT_FLUSHED;
    }
    obuf[0] |= state->bits;
    state->bits &= ~MPPE_BIT_FLUSHED;	/* reset for next xmit */

    obuf  += MPPE_OVHD;
    ibuf  += 2;	/* skip to proto field */
    isize -= 2;

    /* Encrypt packet */
    arcfour_encrypt(&state->arcfour_context, ibuf, isize, obuf);

    state->stats.unc_bytes += isize;
    state->stats.unc_packets++;
    state->stats.comp_bytes += osize;
    state->stats.comp_packets++;

    return osize;
}

/*
 * Since every frame grows by MPPE_OVHD + 2 bytes, this is always going
 * to look bad ... and the longer the link is up the worse it will get.
 */
static void
mppe_comp_stats(void *arg, struct compstat *stats)
{
    ppp_mppe_state *state = (ppp_mppe_state *) arg;

    *stats = state->stats;
}


static int
mppe_decomp_init(void *arg, unsigned char *options, int optlen, int unit,
		 int hdrlen, int mru, int debug)
{
    /* ARGSUSED */
    return mppe_init(arg, options, optlen, unit, debug, "mppe_decomp_init");
}

/*
 * We received a CCP Reset-Ack.  Just ignore it.
 */
static void
mppe_decomp_reset(void *arg)
{
    /* ARGSUSED */
    return;
}

/*
 * Decompress (decrypt) an MPPE packet.
 */
int
mppe_decompress(void *arg, unsigned char *ibuf, int isize, unsigned char *obuf,
		int osize)
{
    ppp_mppe_state *state = (ppp_mppe_state *) arg;
    unsigned ccount;
    int flushed = MPPE_BITS(ibuf) & MPPE_BIT_FLUSHED;
    int sanity = 0;

    if (isize <= PPP_HDRLEN + MPPE_OVHD) {
	if (state->debug)
	    printk(KERN_DEBUG "mppe_decompress[%d]: short pkt (%d)\n",
		   state->unit, isize);
	return DECOMP_ERROR;
    }

    /* Make sure we have enough room to decrypt the packet. */
    if (osize < isize - MPPE_OVHD - 2) {
	printk(KERN_DEBUG "mppe_decompress[%d]: osize too small! "
	       "(have: %d need: %d)\n", state->unit,
	       osize, isize - MPPE_OVHD - 2);
	return DECOMP_ERROR;
    }
    osize = isize - MPPE_OVHD - 2;

    ccount = MPPE_CCOUNT(ibuf);
    if (state->debug >= 7)
	printk(KERN_DEBUG "mppe_decompress[%d]: ccount %d\n", state->unit,
	       ccount);

    /* sanity checks -- terminate with extreme prejudice */
    if (!(MPPE_BITS(ibuf) & MPPE_BIT_ENCRYPTED)) {
	printk(KERN_DEBUG "mppe_decompress[%d]: ENCRYPTED bit not set!\n",
	       state->unit);
	state->sanity_errors += 100;
	sanity = 1;
    }
    if (!state->stateful && !flushed) {
	printk(KERN_DEBUG "mppe_decompress[%d]: FLUSHED bit not set in "
	       "stateless mode!\n", state->unit);
	state->sanity_errors += 100;
	sanity = 1;
    }
    if (state->stateful && ((ccount & 0xff) == 0xff) && !flushed) {
	printk(KERN_DEBUG "mppe_decompress[%d]: FLUSHED bit not set on "
	       "flag packet!\n", state->unit);
	state->sanity_errors += 100;
	sanity = 1;
    }

    if (sanity) {
	if (state->sanity_errors < SANITY_MAX)
	    return DECOMP_ERROR;
	else
	    /*
	     * Take LCP down if the peer is sending too many bogons.
	     * We don't want to do this for a single or just a few
	     * instances since it could just be due to packet corruption.
	     */
	    return DECOMP_FATALERROR;
    }

    /*
     * Check the coherency count.
     */

    if (!state->stateful) {
	/* RFC 3078, sec 8.1.  Rekey for every packet. */
	while (state->ccount != ccount) {
	    mppe_rekey(state, 0);
	    state->ccount = (state->ccount + 1) % MPPE_CCOUNT_SPACE;
	}
    } else {
	/* RFC 3078, sec 8.2. */
	if (!state->discard) {
	    /* normal state */
	    state->ccount = (state->ccount + 1) % MPPE_CCOUNT_SPACE;
	    if (ccount != state->ccount) {
		/*
		 * (ccount > state->ccount)
		 * Packet loss detected, enter the discard state.
		 * Signal the peer to rekey (by sending a CCP Reset-Request).
		 */
		state->discard = 1;
		return DECOMP_ERROR;
	    }
	} else {
	    /* discard state */
	   if (!flushed) {
		/* ccp.c will be silent (no additional CCP Reset-Requests). */
		return DECOMP_ERROR;
	    } else {
		/* Rekey for every missed "flag" packet. */
		while ((ccount & ~0xff) != (state->ccount & ~0xff)) {
		    mppe_rekey(state, 0);
		    state->ccount = (state->ccount + 256) % MPPE_CCOUNT_SPACE;
		}

		/* reset */
		state->discard = 0;
		state->ccount = ccount;
		/*
		 * Another problem with RFC 3078 here.  It implies that the
		 * peer need not send a Reset-Ack packet.  But RFC 1962
		 * requires it.  Hopefully, M$ does send a Reset-Ack; even
		 * though it isn't required for MPPE synchronization, it is
		 * required to reset CCP state.
		 */
	    }
	}
	if (flushed)
	    mppe_rekey(state, 0);
    }

    /*
     * Fill in the first part of the PPP header.  The protocol field
     * comes from the decrypted data.
     */
    obuf[0] = PPP_ADDRESS(ibuf);	/* +1 */
    obuf[1] = PPP_CONTROL(ibuf);	/* +1 */
    obuf  += 2;
    ibuf  += PPP_HDRLEN + MPPE_OVHD;
    isize -= PPP_HDRLEN + MPPE_OVHD;	/* -6 */
					/* net osize: isize-4 */

    /* And finally, decrypt the packet. */
    arcfour_decrypt(&state->arcfour_context, ibuf, isize, obuf);

    state->stats.unc_bytes += osize;
    state->stats.unc_packets++;
    state->stats.comp_bytes += isize;
    state->stats.comp_packets++;

    /* good packet credit */
    state->sanity_errors >>= 1;

    return osize;
}

/*
 * Incompressible data has arrived (this should never happen!).
 * We should probably drop the link if the protocol is in the range
 * of what should be encrypted.  At the least, we should drop this
 * packet.  (How to do this?)
 */
static void
mppe_incomp(void *arg, unsigned char *ibuf, int icnt)
{
    ppp_mppe_state *state = (ppp_mppe_state *) arg;

    if (state->debug &&
	(PPP_PROTOCOL(ibuf) >= 0x0021 && PPP_PROTOCOL(ibuf) <= 0x00fa))
	printk(KERN_DEBUG "mppe_incomp[%d]: incompressible (unencrypted) data! "
	       "(proto %04x)\n", state->unit, PPP_PROTOCOL(ibuf));

    state->stats.inc_bytes += icnt;
    state->stats.inc_packets++;
    state->stats.unc_bytes += icnt;
    state->stats.unc_packets++;
}

/*************************************************************
 * Module interface table
 *************************************************************/

/* These are in ppp.c (2.2.x) or ppp_generic.c (2.4.x) */
extern int  ppp_register_compressor   (struct compressor *cp);
extern void ppp_unregister_compressor (struct compressor *cp);

/*
 * Procedures exported to if_ppp.c.
 */
struct compressor ppp_mppe = {
    CI_MPPE,		/* compress_proto */
    mppe_alloc,		/* comp_alloc */
    mppe_free,		/* comp_free */
    mppe_comp_init,	/* comp_init */
    mppe_comp_reset,	/* comp_reset */
    mppe_compress,	/* compress */
    mppe_comp_stats,	/* comp_stat */
    mppe_alloc,		/* decomp_alloc */
    mppe_free,		/* decomp_free */
    mppe_decomp_init,	/* decomp_init */
    mppe_decomp_reset,	/* decomp_reset */
    mppe_decompress,	/* decompress */
    mppe_incomp,	/* incomp */
    mppe_comp_stats,	/* decomp_stat */
};

/* 2.2 compatibility defines */
#ifndef __init
#define __init
#endif
#ifndef __exit
#define __exit
#endif
#ifndef MODULE_LICENSE
#define MODULE_LICENSE(license)
#endif

int __init
ppp_mppe_init(void)
{  
    int answer = ppp_register_compressor(&ppp_mppe);

    if (answer == 0)
	printk(KERN_INFO "PPP MPPE Compression module registered\n");
    return answer;
}

void __exit
ppp_mppe_cleanup(void)
{
    ppp_unregister_compressor(&ppp_mppe);
}

module_init(ppp_mppe_init);
module_exit(ppp_mppe_cleanup);
MODULE_LICENSE("BSD without advertisement clause");
