/*********************************************************************
 *                
 * Filename:      irda_deflate.c
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri Oct  9 02:52:08 1998
 * Modified at:   Mon Dec 14 19:48:54 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *  ==FILEVERSION 980319==
 *
 * irda_deflate.c - interface the zlib procedures for Deflate compression
 * and decompression (as used by gzip) to the IrDA code.
 * This version is for use with Linux kernel 2.1.X.
 *
 * Copyright (c) 1994 The Australian National University.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, provided that the above copyright
 * notice appears in all copies.  This software is provided without any
 * warranty, express or implied. The Australian National University
 * makes no representations about the suitability of this software for
 * any purpose.
 *
 * IN NO EVENT SHALL THE AUSTRALIAN NATIONAL UNIVERSITY BE LIABLE TO ANY
 * PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE AUSTRALIAN NATIONAL UNIVERSITY HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * THE AUSTRALIAN NATIONAL UNIVERSITY SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUSTRALIAN NATIONAL UNIVERSITY HAS NO
 * OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS,
 * OR MODIFICATIONS.
 *
 * From: deflate.c,v 1.1 1996/01/18 03:17:48 paulus Exp
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h> 
#include <linux/errno.h>
#include <linux/string.h>	/* used in new tty drivers */
#include <linux/signal.h>	/* used in new tty drivers */

#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/ioctl.h>

#include <linux/ppp_defs.h>
#include <linux/ppp-comp.h>

/*
 * This is not that nice, but what can we do when the code has been placed
 * elsewhere
 */

#include "../../../drivers/net/zlib.c"

/*
 * State for a Deflate (de)compressor.
 */
struct irda_deflate_state {
    int		seqno;
    int		w_size;
    int		unit;
    int		mru;
    int		debug;
    z_stream	strm;
    struct compstat stats;
};

#define DEFLATE_OVHD	2		/* Deflate overhead/packet */

static void	*zalloc __P((void *, unsigned int items, unsigned int size));
static void	*zalloc_init __P((void *, unsigned int items,
				  unsigned int size));
static void	zfree __P((void *, void *ptr));
static void	*z_comp_alloc __P((unsigned char *options, int opt_len));
static void	*z_decomp_alloc __P((unsigned char *options, int opt_len));
static void	z_comp_free __P((void *state));
static void	z_decomp_free __P((void *state));
static int	z_comp_init __P((void *state, unsigned char *options,
				 int opt_len,
				 int unit, int hdrlen, int debug));
static int	z_decomp_init __P((void *state, unsigned char *options,
				   int opt_len,
				   int unit, int hdrlen, int mru, int debug));
static int	z_compress __P((void *state, unsigned char *rptr,
				unsigned char *obuf,
				int isize, int osize));
static void	z_incomp __P((void *state, unsigned char *ibuf, int icnt));
static int	z_decompress __P((void *state, unsigned char *ibuf,
				int isize, unsigned char *obuf, int osize));
static void	z_comp_reset __P((void *state));
static void	z_decomp_reset __P((void *state));
static void	z_comp_stats __P((void *state, struct compstat *stats));

struct chunk_header {
	int valloced;		/* allocated with valloc, not kmalloc */
	int guard;		/* check for overwritten header */
};

#define GUARD_MAGIC	0x77a8011a
#define MIN_VMALLOC	2048	/* use kmalloc for blocks < this */

/*
 * Space allocation and freeing routines for use by zlib routines.
 */
void zfree(void *arg, void *ptr)
{
	struct chunk_header *hdr = ((struct chunk_header *)ptr) - 1;
	
	if (hdr->guard != GUARD_MAGIC) {
		printk(KERN_WARNING "zfree: header corrupted (%x %x) at %p\n",
		       hdr->valloced, hdr->guard, hdr);
		return;
	}
	if (hdr->valloced)
		vfree(hdr);
	else
		kfree(hdr);
}

void *
zalloc(arg, items, size)
    void *arg;
    unsigned int items, size;
{
	struct chunk_header *hdr;
	unsigned nbytes;

	nbytes = items * size + sizeof(*hdr);
	hdr = kmalloc(nbytes, GFP_ATOMIC);
	if (hdr == 0)
		return 0;
	hdr->valloced = 0;
	hdr->guard = GUARD_MAGIC;
	return (void *) (hdr + 1);
}

void * zalloc_init(void *arg, unsigned int items, unsigned int size)
{
	struct chunk_header *hdr;
	unsigned nbytes;

	nbytes = items * size + sizeof(*hdr);
	if (nbytes >= MIN_VMALLOC)
		hdr = vmalloc(nbytes);
	else
		hdr = kmalloc(nbytes, GFP_KERNEL);
	if (hdr == 0)
		return 0;
	hdr->valloced = nbytes >= MIN_VMALLOC;
	hdr->guard = GUARD_MAGIC;
	return (void *) (hdr + 1);
}

static void
z_comp_free(arg)
    void *arg;
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;

	if (state) {
		deflateEnd(&state->strm);
		kfree(state);
		MOD_DEC_USE_COUNT;
	}
}

/*
 * Allocate space for a compressor.
 */
static void *
z_comp_alloc(options, opt_len)
    unsigned char *options;
    int opt_len;
{
	struct irda_deflate_state *state;
	int w_size;

	if (opt_len != CILEN_DEFLATE
	    || (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT)
	    || options[1] != CILEN_DEFLATE
	    || DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL
	    || options[3] != DEFLATE_CHK_SEQUENCE)
		return NULL;
	w_size = DEFLATE_SIZE(options[2]);
	w_size = MAX_WBITS;
	if (w_size < DEFLATE_MIN_SIZE || w_size > DEFLATE_MAX_SIZE)
		return NULL;

	state = (struct irda_deflate_state *) kmalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	MOD_INC_USE_COUNT;
	memset (state, 0, sizeof (struct irda_deflate_state));
	state->strm.next_in = NULL;
	state->strm.zalloc  = zalloc_init;
	state->strm.zfree   = zfree;
	state->w_size       = w_size;

	if (deflateInit2(&state->strm, Z_DEFAULT_COMPRESSION,
			 DEFLATE_METHOD_VAL, -w_size, 8, Z_DEFAULT_STRATEGY)
	    != Z_OK)
		goto out_free;
	state->strm.zalloc = zalloc;
	return (void *) state;

out_free:
	z_comp_free(state);
	MOD_DEC_USE_COUNT;
	return NULL;
}

static int
z_comp_init(arg, options, opt_len, unit, hdrlen, debug)
    void *arg;
    unsigned char *options;
    int opt_len, unit, hdrlen, debug;
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;

	if (opt_len < CILEN_DEFLATE
	    || (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT)
	    || options[1] != CILEN_DEFLATE
	    || DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL
	    || DEFLATE_SIZE(options[2]) != state->w_size
	    || options[3] != DEFLATE_CHK_SEQUENCE)
		return 0;

	state->seqno = 0;
	state->unit  = unit;
	state->debug = debug;

	deflateReset(&state->strm);

	return 1;
}

static void z_comp_reset(void *arg)
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;

	state->seqno = 0;
	deflateReset(&state->strm);
}

int
z_compress(arg, rptr, obuf, isize, osize)
    void *arg;
    unsigned char *rptr;	/* uncompressed packet (in) */
    unsigned char *obuf;	/* compressed packet (out) */
    int isize, osize;
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;
	int r, olen, oavail;

	olen = 0;

	/* Don't generate compressed packets which are larger than
	   the uncompressed packet. */
	if (osize > isize)
		osize = isize;

	state->strm.next_out = obuf;
	state->strm.avail_out = oavail = osize;

	state->strm.next_in = rptr;
	state->strm.avail_in = isize;

	for (;;) {
		r = deflate(&state->strm, Z_PACKET_FLUSH);
		if (r != Z_OK) {
			if (state->debug)
				printk(KERN_ERR
				       "z_compress: deflate returned %d\n", r);
			break;
		}
		if (state->strm.avail_out == 0) {
			olen += oavail;
			state->strm.next_out = NULL;
			state->strm.avail_out = oavail = 1000000;
		} else {
			break;		/* all done */
		}
	}
	olen += oavail - state->strm.avail_out;

	/*
	 * See if we managed to reduce the size of the packet.
	 */
	if (olen < isize) {
		state->stats.comp_bytes += olen;
		state->stats.comp_packets++;
	} else {
		state->stats.inc_bytes += isize;
		state->stats.inc_packets++;
		olen = 0;
	}
	state->stats.unc_bytes += isize;
	state->stats.unc_packets++;

	return olen;
}

static void
z_comp_stats(arg, stats)
    void *arg;
    struct compstat *stats;
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;

	*stats = state->stats;
}

static void z_decomp_free(void *arg)
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;

	if (state) {
		inflateEnd(&state->strm);
		kfree(state);
		MOD_DEC_USE_COUNT;
	}
}

/*
 * Allocate space for a decompressor.
 */
static void *
z_decomp_alloc(options, opt_len)
    unsigned char *options;
    int opt_len;
{
	struct irda_deflate_state *state;
	int w_size;

	if (opt_len != CILEN_DEFLATE
	    || (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT)
	    || options[1] != CILEN_DEFLATE
	    || DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL
	    || options[3] != DEFLATE_CHK_SEQUENCE)
		return NULL;
	w_size = DEFLATE_SIZE(options[2]);
	w_size = MAX_WBITS;
	if (w_size < DEFLATE_MIN_SIZE || w_size > DEFLATE_MAX_SIZE)
		return NULL;

	state = (struct irda_deflate_state *) kmalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	MOD_INC_USE_COUNT;
	memset (state, 0, sizeof (struct irda_deflate_state));
	state->w_size        = w_size;
	state->strm.next_out = NULL;
	state->strm.zalloc   = zalloc_init;
	state->strm.zfree    = zfree;

	if (inflateInit2(&state->strm, -w_size) != Z_OK)
		goto out_free;
	state->strm.zalloc = zalloc;
	return (void *) state;

out_free:
	z_decomp_free(state);
	MOD_DEC_USE_COUNT;
	return NULL;
}

static int
z_decomp_init(arg, options, opt_len, unit, hdrlen, mru, debug)
    void *arg;
    unsigned char *options;
    int opt_len, unit, hdrlen, mru, debug;
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;

	if (opt_len < CILEN_DEFLATE
	    || (options[0] != CI_DEFLATE && options[0] != CI_DEFLATE_DRAFT)
	    || options[1] != CILEN_DEFLATE
	    || DEFLATE_METHOD(options[2]) != DEFLATE_METHOD_VAL
	    || DEFLATE_SIZE(options[2]) != state->w_size
	    || options[3] != DEFLATE_CHK_SEQUENCE)
		return 0;

	state->seqno = 0;
	state->unit  = unit;
	state->debug = debug;
	state->mru   = mru;

	inflateReset(&state->strm);

	return 1;
}

static void z_decomp_reset(void *arg)
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;

	state->seqno = 0;
	inflateReset(&state->strm);
}

/*
 * Decompress a Deflate-compressed packet.
 *
 * Because of patent problems, we return DECOMP_ERROR for errors
 * found by inspecting the input data and for system problems, but
 * DECOMP_FATALERROR for any errors which could possibly be said to
 * be being detected "after" decompression.  For DECOMP_ERROR,
 * we can issue a CCP reset-request; for DECOMP_FATALERROR, we may be
 * infringing a patent of Motorola's if we do, so we take CCP down
 * instead.
 *
 * Given that the frame has the correct sequence number and a good FCS,
 * errors such as invalid codes in the input most likely indicate a
 * bug, so we return DECOMP_FATALERROR for them in order to turn off
 * compression, even though they are detected by inspecting the input.
 */
int
z_decompress(arg, ibuf, isize, obuf, osize)
    void *arg;
    unsigned char *ibuf;
    int isize;
    unsigned char *obuf;
    int osize;
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;
	int olen, r;
	int overflow;
	unsigned char overflow_buf[1];

	if (isize <= DEFLATE_OVHD) {
		if (state->debug)
			printk(KERN_DEBUG "z_decompress%d: short pkt (%d)\n",
			       state->unit, isize);
		return DECOMP_ERROR;
	}

	/*
	 * Set up to call inflate.
	 */
	state->strm.next_in = ibuf;
	state->strm.avail_in = isize;
	state->strm.next_out = obuf;
	state->strm.avail_out = osize;
	overflow = 0;

	/*
	 * Call inflate, supplying more input or output as needed.
	 */
	for (;;) {
		r = inflate(&state->strm, Z_PACKET_FLUSH);
		if (r != Z_OK) {
			if (state->debug)
				printk(KERN_DEBUG "z_decompress%d: inflate returned %d (%s)\n",
				       state->unit, r, (state->strm.msg? state->strm.msg: ""));
			return DECOMP_FATALERROR;
		}
		if (state->strm.avail_out != 0)
			break;		/* all done */
		
		if (!overflow) {
			/*
			 * We've filled up the output buffer; the only way to
			 * find out whether inflate has any more characters
			 * left is to give it another byte of output space.
			 */
			state->strm.next_out = overflow_buf;
			state->strm.avail_out = 1;
			overflow = 1;
		} else {
			if (state->debug)
				printk(KERN_DEBUG "z_decompress%d: ran out of mru\n",
				       state->unit);
			return DECOMP_FATALERROR;
		}
	}

	olen = osize + overflow - state->strm.avail_out;
	state->stats.unc_bytes += olen;
	state->stats.unc_packets++;
	state->stats.comp_bytes += isize;
	state->stats.comp_packets++;

	return olen;
}

/*
 * Incompressible data has arrived - add it to the history.
 */
static void
z_incomp(arg, ibuf, icnt)
    void *arg;
    unsigned char *ibuf;
    int icnt;
{
	struct irda_deflate_state *state = (struct irda_deflate_state *) arg;
	int r;

	/*
	 * Check that the protocol is one we handle.
	 */

	/*
	 * We start at the either the 1st or 2nd byte of the protocol field,
	 * depending on whether the protocol value is compressible.
	 */
	state->strm.next_in = ibuf;
	state->strm.avail_in = icnt;

	r = inflateIncomp(&state->strm);
	if (r != Z_OK) {
		/* gak! */
		if (state->debug) {
			printk(KERN_DEBUG "z_incomp%d: inflateIncomp returned %d (%s)\n",
			       state->unit, r, (state->strm.msg? state->strm.msg: ""));
		}
		return;
	}

	/*
	 * Update stats.
	 */
	state->stats.inc_bytes += icnt;
	state->stats.inc_packets++;
	state->stats.unc_bytes += icnt;
	state->stats.unc_packets++;
}

/*************************************************************
 * Module interface table
 *************************************************************/

/* These are in ppp.c */
extern int  irda_register_compressor   (struct compressor *cp);
extern void irda_unregister_compressor (struct compressor *cp);

/*
 * Procedures exported to if_ppp.c.
 */
static struct compressor irda_deflate = {
compress_proto:	CI_DEFLATE,
comp_alloc:	z_comp_alloc,
comp_free:	z_comp_free,
comp_init:	z_comp_init,
comp_reset:	z_comp_reset,
compress:	z_compress,
comp_stat:	z_comp_stats,
decomp_alloc:	z_decomp_alloc,
decomp_free:	z_decomp_free,
decomp_init:	z_decomp_init,
decomp_reset:	z_decomp_reset,
decompress:	z_decompress,
incomp:		z_incomp,
decomp_stat:	z_comp_stats
};

static struct compressor irda_deflate_draft = {
compress_proto:	CI_DEFLATE_DRAFT,
comp_alloc:	z_comp_alloc,
comp_free:	z_comp_free,
comp_init:	z_comp_init,
comp_reset:	z_comp_reset,
compress:	z_compress,
comp_stat:	z_comp_stats,
decomp_alloc:	z_decomp_alloc,
decomp_free:	z_decomp_free,
decomp_init:	z_decomp_init,
decomp_reset:	z_decomp_reset,
decompress:	z_decompress,
incomp:		z_incomp,
decomp_stat:	z_comp_stats
};

int __init irda_deflate_init(void)
{
        int answer = irda_register_compressor ( &irda_deflate);
        if (answer == 0)
                printk (KERN_INFO
			"IrDA Deflate Compression module registered\n");
	irda_register_compressor( &irda_deflate_draft);
        return answer;
}

void irda_deflate_cleanup(void)
{

	irda_unregister_compressor (&irda_deflate);
	irda_unregister_compressor (&irda_deflate_draft);
}

#ifdef MODULE
/*************************************************************
 * Module support routines
 *************************************************************/

int init_module(void)
{  
	return irda_deflate_init();
}
     
void
cleanup_module(void)
{
	if (MOD_IN_USE)
		printk (KERN_INFO
			"Deflate Compression module busy, remove delayed\n");
	else {
		irda_deflate_cleanup();
	}
}
#endif
