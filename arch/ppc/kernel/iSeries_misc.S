 /*
  * This file contains miscellaneous low-level functions.
  *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
  *
  * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
  * and Paul Mackerras.
  * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
  * updated by Dave Boutcher (boutcher@us.ibm.com)
  * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/ppc_asm.h>
#include "ppc_defs.h"
#include "iSeries_asm.h"

	.text
	.align	5

#ifdef CONFIG_SMP
	.comm	hash_table_lock,4
#endif /* CONFIG_SMP */

_GLOBAL(is_msr_enabled)
	mfmsr	r3
	andi.	r3,r3,MSR_EE
	beqlr		/* Return r3=0 indicating disabled */
	li	r3,1
	blr		/* Return r3=1 indicating enabled */

_GLOBAL(is_soft_enabled)
	mfspr	r3,SPRG1
	lbz	r3,PACAPROCENABLED(r3)
	blr

_GLOBAL(get_tb64)
	/* hard disable because we are using a 64-bit register
	 * and we don't want it to get trashed in an interrupt
	 * handler
	 */
	
	mfmsr	r5
	rlwinm	r0,r5,0,17,15	/* clear MSR_EE in r0 */
	mtmsr	r0		/* hard disable */

	mftb	r3
	rldicl	r4,r3,0,32
	rldicl	r3,r3,32,32

	mtmsr	r5		/* restore MSR_EE */
	blr

/* void __save_flags(unsigned long *flags) */
_GLOBAL(__save_flags_ptr)
	mfspr	r4,SPRG1	/* Get Paca pointer */
	lbz	r4,PACAPROCENABLED(r4)
	stw	r4,0(r3)
	blr
_GLOBAL(__save_flags_ptr_end)

/* void __restore_flags(unsigned long flags) */	
_GLOBAL(__restore_flags)
	cmpi	0,r3,0		/* Are we enabling? */
	beq	0f		/* No - then skip interrupt checks */

3:	
	mfspr	r4,SPRG1
	lbz	r4,PACAPROCENABLED(r4)
	cmpi	0,r4,0		/* Are we already enabled? */
	bne	0f		/* Yes - then skip interrupt checks */

	CHECKDECR(r4,r5)
	bne-	do_fake_decrementer

	CHECKLPQUEUE(r4,r5,r6)
	bne-	do_lost_interrupts
2:
	mfmsr	r0
	rlwinm	r0,r0,0,17,15		/* hard disable */
	mtmsr	r0

	CHECKANYINT(r4,r5,r6)
	ori	r0,r0,MSR_EE
	beq	1f
	mtmsr	r0			/* hard enable */
	b	3b			/* process more interrupts */
1:
	mfspr	r4,SPRG1
	stb	r3,PACAPROCENABLED(r4)
	mtmsr	r0			/* hard enable */
	blr
0:
	mfspr	r4,SPRG1
	stb	r3,PACAPROCENABLED(r4)
	blr
_GLOBAL(__restore_flags_end)
	
_GLOBAL(__cli)
	mfspr	r4,SPRG1
	li	r3,0
	stb	r3,PACAPROCENABLED(r4)
	blr			/* Done */
_GLOBAL(__cli_end)

_GLOBAL(__sti)
	li	r3,1
	b	__restore_flags
_GLOBAL(__sti_end)

/*
 * We were about to enable interrupts but we have to simulate
 * some interrupts that were lost by enable_irq first.
 */
_GLOBAL(do_lost_interrupts)
	stwu	r1,-32(r1)
	mflr	r0
	stw	r0,36(r1)
	stw	r3,28(r1)
1:	bl	fake_interrupt
	bl	check_softirqs
	mfmsr	r0
	rlwinm	r0,r0,0,17,15		/* hard disable */
	mtmsr	r0

	CHECKANYINT(r4,r5,r6)
	ori	r0,r0,MSR_EE
	beq	2f
	mtmsr	r0			/* hard enable */
	b	1b			/* process more interrupts */

2:	lwz	r3,28(r1)
	mfspr	r4,SPRG1
	stb	r3,PACAPROCENABLED(r4)	/* restore soft interrupt state */
	mtmsr	r0			/* hard enable */
	lwz	r0,36(r1)
	mtlr	r0

	addi	r1,r1,32
	blr

/*
 * Simulate a decrementer interrupt
 */
do_fake_decrementer:
	stwu	r1,-32(r1)
	mflr	r0
	stw	r0,36(r1)
	stw	r3,28(r1)
	bl	fake_decrementer
	bl	check_softirqs
	lwz	r0,36(r1)
	mtlr	r0

	lwz	r3,28(r1)
	addi	r1,r1,32

	mfmsr	r0			/* hard disable */
	rlwinm	r0,r0,0,17,15
	mtmsr	r0
	
	CHECKANYINT(r4,r5,r6)		/* Check for any interrupts pending */
	ori	r0,r0,MSR_EE
	beq	1f
	mtmsr	r0			/* hard enable */
	b	do_lost_interrupts	/* Handle more interrupts */
	
1:	mfspr	r4,SPRG1
	stb	r3,PACAPROCENABLED(r4)	/* soft enable */
	mtmsr	r0			/* hard enable */
	blr

/*
 * do softirqs if necessary
 */
check_softirqs:
	stwu	r1,-32(r1)
	mflr	r0
	stw	r0,36(r1)
	
	lis	r4,irq_stat@ha
	addi	r4,r4,irq_stat@l
#ifdef CONFIG_SMP
	lwz	r3,CPU(r2)
	slwi	r3,r3,7
	add	r4,r4,r3
#endif
	lwz	r5,0(r4)
	lwz	r4,4(r4)
	and.	r5,r5,r4
	beq+	2f
	bl	do_softirq
2:
	lwz	r0,36(r1)
	mtlr	r0

	addi	r1,r1,32
	blr


/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 *
 * flush_icache_range(unsigned long start, unsigned long stop)
 */

_GLOBAL(flush_icache_range)


/*
 * Flush the data cache to memory 
 * 
 * Different models of iSeries's have different cache line sizes
 * and in some cases i-cache and d-cache line sizes differ from
 * each other.
 */

	lis	r7,iSeries_dcache_line_size@ha
	lhz	r7,iSeries_dcache_line_size@l(r7)
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lis	r9,iSeries_log_dcache_line_size@ha
	lhz	r9,iSeries_log_dcache_line_size@l(r9)
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
1:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	1b
	sync

/* Now invalidate the instruction cache */
	
	lis	r7,iSeries_icache_line_size@ha
	lhz	r7,iSeries_icache_line_size@l(r7)
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5
	lis	r9,iSeries_log_icache_line_size@ha
	lhz	r9,iSeries_log_icache_line_size@l(r9)
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
2:	icbi	0,r6
	add	r6,r6,r7
	bdnz	2b
	sync
	isync
	blr
	

/*
 * Like above, but only do the D-cache.
 *
 * flush_dcache_range(unsigned long start, unsigned long stop)
 */
_GLOBAL(flush_dcache_range)

/*
 * Flush the data cache to memory 
 * 
 * Different models of iSeries's have different cache line sizes
 */

	lis	r7,iSeries_dcache_line_size@ha
	lhz	r7,iSeries_dcache_line_size@l(r7)
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lis	r9,iSeries_log_dcache_line_size@ha
	lhz	r9,iSeries_log_dcache_line_size@l(r9)
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
0:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	0b
	sync
	blr


/*
 * Flush a particular page from the data cache to RAM.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 *
 *	void __flush_page_to_ram(void *page)
 *	void __flush_dcache_icache(void *page)
 */
_GLOBAL(__flush_page_to_ram)
_GLOBAL(__flush_dcache_icache)

/*
 * Flush the data cache to memory 
 * 
 * Different models of iSeries's have different cache line sizes
 */

/* Flush the dcache */

	rlwinm	r3,r3,0,0,19		/* Page align */
	lis	r4,iSeries_dcache_lines_per_page@ha
	lhz	r4,iSeries_dcache_lines_per_page@l(r4)
	lis	r5,iSeries_dcache_line_size@ha
	lhz	r5,iSeries_dcache_line_size@l(r5)
	mr	r6,r3
	mtctr	r4
0:	dcbst	0,r6
	add	r6,r6,r5
	bdnz	0b
	sync

/* Now invalidate the icache */	

	lis	r4,iSeries_icache_lines_per_page@ha
	lhz	r4,iSeries_icache_lines_per_page@l(r4)
	lis	r5,iSeries_icache_line_size@ha
	lhz	r5,iSeries_icache_line_size@l(r5)
	mtctr	r4
1:	icbi	0,r3
	add	r3,r3,r5
	bdnz	1b
	sync	
	isync
	blr
	

/*
 * Flush a particular page from the instruction cache.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 *
 *	void __flush_icache_page(void *page)
 */
_GLOBAL(__flush_icache_page)


/*
 * Different models of iSeries's have different cache line sizes
 */
 
/* Invalidate the icache */

	rlwinm	r3,r3,0,0,19		/* Page align */
	lis	r4,iSeries_icache_lines_per_page@ha
	lhz	r4,iSeries_icache_lines_per_page@l(r4)
	lis	r5,iSeries_icache_line_size@ha
	lhz	r5,iSeries_icache_line_size@l(r5)
	mtctr	r4
1:	icbi	0,r3
	add	r3,r3,r5
	bdnz	1b
	sync	
	isync
	blr
	
/*
 * Clear a page using the dcbz instruction, which doesn't cause any
 * memory traffic (except to write out any cache lines which get
 * displaced).  This only works on cacheable memory.
 */
_GLOBAL(clear_page)

	rlwinm	r3,r3,0,0,19		/* Page align */
	lis	r4,iSeries_dcache_lines_per_page@ha
	lhz	r4,iSeries_dcache_lines_per_page@l(r4)
	lis	r5,iSeries_dcache_line_size@ha
	lhz	r5,iSeries_dcache_line_size@l(r5)
	mtctr	r4
0:	dcbz	0,r3
	add	r3,r3,r5
	bdnz	0b
	blr


/*
 * Copy a whole page.  We use the dcbz instruction on the destination
 * to reduce memory traffic (it eliminates the unnecessary reads of
 * the destination into cache).  This requires that the destination
 * is cacheable.
 */
#define COPY_16_BYTES		\
	lwz	r6,4(r4);	\
	lwz	r7,8(r4);	\
	lwz	r8,12(r4);	\
	lwzu	r9,16(r4);	\
	stw	r6,4(r3);	\
	stw	r7,8(r3);	\
	stw	r8,12(r3);	\
	stwu	r9,16(r3)

_GLOBAL(copy_page)

	rlwinm	r3,r3,0,0,19		/* Page align */
	rlwinm	r4,r4,0,0,19
	lis	r5,iSeries_dcache_lines_per_page@ha
	lhz	r5,iSeries_dcache_lines_per_page@l(r5)
	lis	r6,iSeries_dcache_line_size@ha
	lhz	r0,iSeries_dcache_line_size@l(r6)
	mtctr	r5
	addi	r3,r3,-4
	addi	r4,r4,-4
	li	r10,4

	cmpi	0,r0,32
	beq	do_32_byte_line
	cmpi	0,r0,64
	beq	do_64_byte_line
	cmpi	0,r0,128
	beq	do_128_byte_line

	/* We don't have code specifically for this cache line size */
	/* Assume that the cache line size is at least 16 (and of */
	/* course a multiple of 16)  */
	/* This code will work for all power-of-2 cache line sizes */
	/* from 16 to 4096 */
	
1:	mr	r5,r0
	dcbz	r10,r3
0:	COPY_16_BYTES
	addi	r5,r5,-16
	or.	r5,r5,r5
	bne	0b
	bdnz	1b
	blr

do_32_byte_line:
	dcbz	r10,r3
	COPY_16_BYTES
	COPY_16_BYTES
	bdnz	do_32_byte_line
	blr

do_64_byte_line:
	dcbz	r10,r3
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	bdnz	do_64_byte_line
	blr

do_128_byte_line:	
	dcbz	r10,r3
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	bdnz	do_128_byte_line
	blr
