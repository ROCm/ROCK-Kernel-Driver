/*
 *
 * Optimized function to clear a page of memory.
 *
 * Inputs:
 *	in0:	address of page
 *
 * Output:
 * 	none
 *
 * Copyright (C) 1999-2001 Hewlett-Packard Co
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 1/06/01 davidm	Tuned for Itanium.
 */
#include <asm/asmmacro.h>
#include <asm/page.h>

#define saved_lc	r2
#define dst0		in0
#define dst1		r8
#define dst2		r9
#define dst3		r10
#define dst_fetch	r11

GLOBAL_ENTRY(clear_page)
	.prologue
	.regstk 1,0,0,0
	mov r16 = PAGE_SIZE/64-1	// -1 = repeat/until
	;;
	.save ar.lc, saved_lc
	mov saved_lc = ar.lc
	.body
	mov ar.lc = r16
	adds dst1 = 16, dst0
	adds dst2 = 32, dst0
	adds dst3 = 48, dst0
	adds dst_fetch = 512, dst0
	;;
1:	stf.spill.nta [dst0] = f0, 64
	stf.spill.nta [dst1] = f0, 64
	stf.spill.nta [dst2] = f0, 64
	stf.spill.nta [dst3] = f0, 64

	lfetch [dst_fetch], 64
	br.cloop.dptk.few 1b
	;;
	mov ar.lc = r2		// restore lc
	br.ret.sptk.many rp
END(clear_page)
