/*
 *
 * Optimized version of the standard copy_page() function
 *
 * Based on comments from ddd. Try not to overflow write buffer.
 * 
 * Inputs:
 * 	in0:	address of target page
 *	in1:	address of source page
 * Output:
 * 	no return value
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 */
#include <asm/asmmacro.h>
#include <asm/page.h>

#define PIPE_DEPTH	6
#define EPI		p[PIPE_DEPTH-1]

#define lcount		r16
#define saved_pr	r17
#define saved_lc	r18
#define saved_pfs	r19
#define src1		r20
#define src2		r21
#define tgt1		r22
#define tgt2		r23

	.text
	.psr abi64
	.psr lsb
	.lsb

GLOBAL_ENTRY(copy_page)
	UNW(.prologue)
	UNW(.save ar.pfs, saved_pfs)
	alloc saved_pfs=ar.pfs,3,((2*PIPE_DEPTH+7)&~7),0,((2*PIPE_DEPTH+7)&~7)

	.rotr t1[PIPE_DEPTH], t2[PIPE_DEPTH]
	.rotp p[PIPE_DEPTH]

	UNW(.save ar.lc, saved_lc)
	mov saved_lc=ar.lc	// save ar.lc ahead of time
	UNW(.save pr, saved_pr)
	mov saved_pr=pr		// rotating predicates are preserved
				// resgisters we must save.
	UNW(.body)

	mov src1=in1		// initialize 1st stream source 
	adds src2=8,in1		// initialize 2nd stream source 
	mov lcount=PAGE_SIZE/16-1 // as many 16bytes as there are on a page
				  // -1 is because br.ctop is repeat/until

	adds tgt2=8,in0		// initialize 2nd stream target
	mov tgt1=in0		// initialize 1st stream target
	;;
	mov pr.rot=1<<16	// pr16=1 & pr[17-63]=0 , 63 not modified

	mov ar.lc=lcount	// set loop counter
	mov ar.ec=PIPE_DEPTH	// ar.ec must match pipeline depth
	;;

	// We need to preload the n-1 stages of the pipeline (n=depth).
	// We do this during the "prolog" of the loop: we execute
	// n-1 times the "load" bundle. Then both loads & stores are
	// enabled until we reach the end of the last word of the page
	// on the load side. Then, we enter the epilog (controlled by ec)
	// where we just do the stores and no loads n times : drain the pipe
	// (we exit the loop when ec=1).
	//
	// The initialization of the prolog is done via the predicate registers:
	// the choice of EPI DEPENDS on the depth of the pipeline (n).
	// When lc > 0 pr63=1 and it is fed back into pr16 and pr16-pr62 
	// are then shifted right at every iteration,
	// Thus by initializing pr16=1 and the rest to 0 before the loop
	// we get EPI=1 after n iterations.
	//
1:				// engage loop now, let the magic happen...
(p16)	ld8 t1[0]=[src1],16	// new data on top of pipeline in 1st stream
(p16)	ld8 t2[0]=[src2],16	// new data on top of pipeline in 2nd stream
	nop.i 0x0
(EPI)	st8 [tgt1]=t1[PIPE_DEPTH-1],16	// store top of  1st pipeline
(EPI)	st8 [tgt2]=t2[PIPE_DEPTH-1],16	// store top of 2nd pipeline
	br.ctop.dptk.few 1b	// once lc==0, ec-- & p16=0
				// stores but no loads anymore
	;;
	mov pr=saved_pr,0xffffffffffff0000	// restore predicates
	mov ar.pfs=saved_pfs	// restore ar.ec 
	mov ar.lc=saved_lc	// restore saved lc
	br.ret.sptk.few rp	// bye...
END(copy_page)
