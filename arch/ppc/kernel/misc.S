/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
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
#include "ppc_asm.h"

#if defined(CONFIG_4xx) || defined(CONFIG_8xx)
#define CACHE_LINE_SIZE		16
#define LG_CACHE_LINE_SIZE	4
#define MAX_COPY_PREFETCH	1
#elif !defined(CONFIG_PPC64BRIDGE)
#define CACHE_LINE_SIZE		32
#define LG_CACHE_LINE_SIZE	5
#define MAX_COPY_PREFETCH	4
#else
#define CACHE_LINE_SIZE		128
#define LG_CACHE_LINE_SIZE	7
#define MAX_COPY_PREFETCH	1
#endif /* CONFIG_4xx || CONFIG_8xx */

	.text

/*
 * Returns (address we're running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.
 */

_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	lis	r4,1b@ha
	addi	r4,r4,1b@l
	subf	r3,r4,r3
	mtlr	r0
	blr

/* void __no_use_save_flags(unsigned long *flags) */
_GLOBAL(__no_use_save_flags)
	mfmsr	r4
	stw	r4,0(r3)
	blr

/* void __no_use_restore_flags(unsigned long flags) */	
_GLOBAL(__no_use_restore_flags)
/*
 * Just set/clear the MSR_EE bit through restore/flags but do not
 * change anything else.  This is needed by the RT system and makes
 * sense anyway.
 *    -- Cort
 */
	mfmsr 	r4
	/* Copy all except the MSR_EE bit from r4 (current MSR value)
	   to r3.  This is the sort of thing the rlwimi instruction is
	   designed for.  -- paulus. */
	rlwimi	r3,r4,0,17,15
	 /* Check if things are setup the way we want _already_. */
	cmpw	0,r3,r4
	beqlr
	/* are we enabling interrupts? */
	rlwinm.	r0,r3,0,16,16
	beq	1f
	/* if so, check if there are any lost interrupts */
	lis	r7,ppc_n_lost_interrupts@ha
	lwz	r7,ppc_n_lost_interrupts@l(r7)
	cmpi	0,r7,0		/* lost interrupts to process first? */
	bne-	do_lost_interrupts
1:	sync
	mtmsr	r3
	isync
	blr
	
_GLOBAL(__no_use_cli)
	mfmsr	r0		/* Get current interrupt state */
	rlwinm	r3,r0,16+1,32-1,31	/* Extract old value of 'EE' */
	rlwinm	r0,r0,0,17,15	/* clear MSR_EE in r0 */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0		/* Update machine state */
	blr			/* Done */

_GLOBAL(__no_use_sti)
	lis	r4,ppc_n_lost_interrupts@ha
	lwz	r4,ppc_n_lost_interrupts@l(r4)
	mfmsr	r3		/* Get current state */
	ori	r3,r3,MSR_EE	/* Turn on 'EE' bit */
	cmpi	0,r4,0		/* lost interrupts to process first? */
	bne-	do_lost_interrupts
	sync			/* Some chip revs have problems here... */
	mtmsr	r3		/* Update machine state */
	blr

/*
 * We were about to enable interrupts but we have to simulate
 * some interrupts that were lost by enable_irq first.
 */
_GLOBAL(do_lost_interrupts)
	stwu	r1,-16(r1)
	mflr	r0
	stw	r0,20(r1)
	stw	r3,8(r1)
1:	bl	fake_interrupt
	lis	r4,ppc_n_lost_interrupts@ha
	lwz	r4,ppc_n_lost_interrupts@l(r4)
	cmpi	0,r4,0
	bne-	1b
	lwz	r3,8(r1)
	sync
	mtmsr	r3
	lwz	r0,20(r1)
	mtlr	r0
	addi	r1,r1,16
	blr


/*
 * complement mask on the msr then "or" some values on.
 *     _nmask_and_or_msr(nmask, value_to_or)
 */
	_GLOBAL(_nmask_and_or_msr)
	mfmsr	r0		/* Get current msr */
	andc	r0,r0,r3	/* And off the bits set in r3 (first parm) */
	or	r0,r0,r4		/* Or on the bits in r4 (second parm) */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0		/* Update machine state */
	blr			/* Done */


/*
 * Flush MMU TLB
 */
_GLOBAL(_tlbia)
#if defined(CONFIG_SMP)
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,10
10:	lwarx	r7,0,r9
	cmpi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
#endif /* CONFIG_SMP */
	sync
	tlbia
	sync
#ifdef CONFIG_SMP
	tlbsync
	sync
	li	r0,0
	stw	r0,0(r9)		/* clear hash_table_lock */
	mtmsr	r10
	SYNC
#endif
	blr	

/*
 * Flush MMU TLB for a particular address
 */
_GLOBAL(_tlbie)
#if defined(CONFIG_SMP)
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,11
10:	lwarx	r7,0,r9
	cmpi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
#endif /* CONFIG_SMP */
	tlbie	r3
	sync
#ifdef CONFIG_SMP
	tlbsync
	sync
	li	r0,0
	stw	r0,0(r9)		/* clear hash_table_lock */
	mtmsr	r10
	SYNC
#endif
	blr

/*
 * Flush instruction cache.
 * This is a no-op on the 601.
 */
_GLOBAL(flush_instruction_cache)
#ifdef CONFIG_8xx
	isync
	lis	r5, IDC_INVALL@h
	mtspr	IC_CST, r5
#else
	mfspr	r3,PVR
	rlwinm	r3,r3,16,16,31
	cmpi	0,r3,1
	beqlr			/* for 601, do nothing */
	/* 603/604 processor - use invalidate-all bit in HID0 */
	mfspr	r3,HID0
	ori	r3,r3,HID0_ICFI
	mtspr	HID0,r3
#endif /* CONFIG_8xx */
	SYNC
	blr

/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 * This is a no-op on the 601.
 *
 * flush_icache_range(unsigned long start, unsigned long stop)
 */
_GLOBAL(flush_icache_range)
	mfspr	r5,PVR
	rlwinm	r5,r5,16,16,31
	cmpi	0,r5,1
	beqlr				/* for 601, do nothing */
	li	r5,CACHE_LINE_SIZE-1
	andc	r3,r3,r5
	subf	r4,r3,r4
	add	r4,r4,r5
	srwi.	r4,r4,LG_CACHE_LINE_SIZE
	beqlr
	mtctr	r4
	mr	r6,r3
1:	dcbst	0,r3
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	1b
	sync				/* wait for dcbst's to get to ram */
	mtctr	r4
2:	icbi	0,r6
	addi	r6,r6,CACHE_LINE_SIZE
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
       li      r5,CACHE_LINE_SIZE-1
       andc    r3,r3,r5
       subf    r4,r3,r4
       add     r4,r4,r5
       srwi.   r4,r4,LG_CACHE_LINE_SIZE
       beqlr
       mtctr   r4

1:     dcbst   0,r3
       addi    r3,r3,CACHE_LINE_SIZE
       bdnz    1b
       sync                            /* wait for dcbst's to get to ram */
       blr

/*
 * Flush a particular page from the data cache to RAM.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 * This is a no-op on the 601 which has a unified cache.
 *
 *	void __flush_page_to_ram(void *page)
 */
_GLOBAL(__flush_page_to_ram)
	mfspr	r5,PVR
	rlwinm	r5,r5,16,16,31
	cmpi	0,r5,1
	beqlr				/* for 601, do nothing */
	rlwinm	r3,r3,0,0,19		/* Get page base address */
	li	r4,4096/CACHE_LINE_SIZE	/* Number of lines in a page */
	mtctr	r4
	mr	r6,r3
0:	dcbst	0,r3			/* Write line to ram */
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	0b
	sync
	mtctr	r4
1:	icbi	0,r6
	addi	r6,r6,CACHE_LINE_SIZE
	bdnz	1b
	sync
	isync
	blr

/*
 * Flush a particular page from the instruction cache.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 * This is a no-op on the 601 which has a unified cache.
 *
 *	void __flush_icache_page(void *page)
 */
_GLOBAL(__flush_icache_page)
	mfspr	r5,PVR
	rlwinm	r5,r5,16,16,31
	cmpi	0,r5,1
	beqlr				/* for 601, do nothing */
	li	r4,4096/CACHE_LINE_SIZE	/* Number of lines in a page */
	mtctr	r4
1:	icbi	0,r3
	addi	r3,r3,CACHE_LINE_SIZE
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
	li	r0,4096/CACHE_LINE_SIZE
	mtctr	r0
#ifdef CONFIG_8xx
	li	r4, 0
1:	stw	r4, 0(r3)
	stw	r4, 4(r3)
	stw	r4, 8(r3)
	stw	r4, 12(r3)
#else
1:	dcbz	0,r3
#endif
	addi	r3,r3,CACHE_LINE_SIZE
	bdnz	1b
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
	addi	r3,r3,-4
	addi	r4,r4,-4
	li	r5,4

#ifndef CONFIG_8xx
#if MAX_COPY_PREFETCH > 1
	li	r0,MAX_COPY_PREFETCH
	li	r11,4
	mtctr	r0
11:	dcbt	r11,r4
	addi	r11,r11,CACHE_LINE_SIZE
	bdnz	11b
#else /* MAX_COPY_PREFETCH == 1 */
	dcbt	r5,r4
	li	r11,CACHE_LINE_SIZE+4
#endif /* MAX_COPY_PREFETCH */
#endif /* CONFIG_8xx */

	li	r0,4096/CACHE_LINE_SIZE
	mtctr	r0
1:
#ifndef CONFIG_8xx
	dcbt	r11,r4
	dcbz	r5,r3
#endif
	COPY_16_BYTES
#if CACHE_LINE_SIZE >= 32
	COPY_16_BYTES
#if CACHE_LINE_SIZE >= 64
	COPY_16_BYTES
	COPY_16_BYTES
#if CACHE_LINE_SIZE >= 128
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
#endif
#endif
#endif
	bdnz	1b
	blr

/*
 * Atomic [test&set] exchange
 *
 *	unsigned long xchg_u32(void *ptr, unsigned long val)
 * Changes the memory location '*ptr' to be val and returns
 * the previous value stored there.
 */
_GLOBAL(xchg_u32)
	mr	r5,r3		/* Save pointer */
10:	lwarx	r3,0,r5		/* Fetch old value & reserve */
	stwcx.	r4,0,r5		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr

/*
 * Try to acquire a spinlock.
 * Only does the stwcx. if the load returned 0 - the Programming
 * Environments Manual suggests not doing unnecessary stcwx.'s
 * since they may inhibit forward progress by other CPUs in getting
 * a lock.
 */
_GLOBAL(__spin_trylock)
	mr	r4,r3
	eieio			/* prevent reordering of stores */
	li	r5,-1
	lwarx	r3,0,r4		/* fetch old value, establish reservation */
	cmpwi	0,r3,0		/* is it 0? */
	bnelr-			/* return failure if not */
	stwcx.	r5,0,r4		/* try to update with new value */
	bne-	1f		/* if we failed */
	eieio			/* prevent reordering of stores */
	blr
1:	li	r3,1		/* return non-zero for failure */
	blr

/*
 * Atomic add/sub/inc/dec operations
 *
 * void atomic_add(int c, int *v)
 * void atomic_sub(int c, int *v)
 * void atomic_inc(int *v)
 * void atomic_dec(int *v)
 * int atomic_dec_and_test(int *v)
 * int atomic_inc_return(int *v)
 * int atomic_dec_return(int *v)
 * void atomic_clear_mask(atomic_t mask, atomic_t *addr)
 * void atomic_set_mask(atomic_t mask, atomic_t *addr);
 */
#if 0 /* now inline - paulus */
_GLOBAL(atomic_add)
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	add	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_add_return)
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	add	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5
	blr
_GLOBAL(atomic_sub)
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	sub	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_inc)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	addi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_inc_return)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	addi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5		/* Return new value */
	blr
_GLOBAL(atomic_dec)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_dec_return)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5		/* Return new value */
	blr
_GLOBAL(atomic_dec_and_test)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	cntlzw	r3,r5
	srwi	r3,r3,5
	blr
#endif /* 0 */
_GLOBAL(atomic_clear_mask)
10:	lwarx	r5,0,r4
	andc	r5,r5,r3
	stwcx.	r5,0,r4
	bne-	10b
	blr
_GLOBAL(atomic_set_mask)
10:	lwarx	r5,0,r4
	or	r5,r5,r3
	stwcx.	r5,0,r4
	bne-	10b
	blr

/*
 * I/O string operations
 *
 * insb(port, buf, len)
 * outsb(port, buf, len)
 * insw(port, buf, len)
 * outsw(port, buf, len)
 * insl(port, buf, len)
 * outsl(port, buf, len)
 * insw_ns(port, buf, len)
 * outsw_ns(port, buf, len)
 * insl_ns(port, buf, len)
 * outsl_ns(port, buf, len)
 *
 * The *_ns versions don't do byte-swapping.
 */
_GLOBAL(_insb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbz	r5,0(r3)
	eieio
	stbu	r5,1(r4)
	bdnz	00b
	blr

_GLOBAL(_outsb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbzu	r5,1(r4)
	stb	r5,0(r3)
	eieio
	bdnz	00b
	blr	

_GLOBAL(_insw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhbrx	r5,0,r3
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(_outsw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	eieio
	sthbrx	r5,0,r3	
	bdnz	00b
	blr	

_GLOBAL(_insl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwbrx	r5,0,r3
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	blr

_GLOBAL(_outsl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stwbrx	r5,0,r3
	eieio
	bdnz	00b
	blr	

_GLOBAL(ide_insw)
_GLOBAL(_insw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhz	r5,0(r3)
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(ide_outsw)
_GLOBAL(_outsw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	sth	r5,0(r3)
	eieio
	bdnz	00b
	blr	

_GLOBAL(_insl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwz	r5,0(r3)
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	blr

_GLOBAL(_outsl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stw	r5,0(r3)
	eieio
	bdnz	00b
	blr	

/*
 * Extended precision shifts.
 * 
 * Updated to be valid for shift counts from 0 to 63 inclusive.
 * -- Gabriel
 *
 * R3/R4 has 64 bit value
 * R5    has shift count
 * result in R3/R4
 *
 *  ashrdi3: arithmetic right shift (sign propagation)	    
 *  lshrdi3: logical right shift	
 *  ashldi3: left shift
 */
_GLOBAL(__ashrdi3)
	subfic	r6,r5,32	
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	rlwinm	r8,r7,0,32	# t3 = (count < 32) ? 32 : 0
	sraw	r7,r3,r7	# t2 = MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	slw	r7,r7,r8	# t2 = (count < 32) ? 0 : t2
	sraw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2
	blr

_GLOBAL(__ashldi3)
	subfic	r6,r5,32	
	slw	r3,r3,r5	# MSW = count > 31 ? 0 : MSW << count
	addi	r7,r5,32	# could be xori, or addi with -32
	srw	r6,r4,r6	# t1 = count > 31 ? 0 : LSW >> (32-count)
	slw	r7,r4,r7	# t2 = count < 32 ? 0 : LSW << (count-32)
	or	r3,r3,r6	# MSW |= t1
	slw	r4,r4,r5	# LSW = LSW << count
	or	r3,r3,r7	# MSW |= t2
	blr

_GLOBAL(__lshrdi3)
	subfic	r6,r5,32	
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	srw	r7,r3,r7	# t2 = count < 32 ? 0 : MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	srw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2 
	blr

_GLOBAL(abs)
	srawi	r4,r3,31
	xor	r3,r3,r4
	sub	r3,r3,r4
	blr

_GLOBAL(_get_SP)
	mr	r3,r1		/* Close enough */
	blr

_GLOBAL(_get_THRM1)
	mfspr	r3,THRM1
	blr

_GLOBAL(_get_THRM2)
	mfspr	r3,THRM2
	blr

_GLOBAL(_get_THRM3)
	mfspr	r3,THRM3
	blr
		
_GLOBAL(_set_THRM1)
	mtspr	THRM1,r3
	blr

_GLOBAL(_set_THRM2)
	mtspr	THRM2,r3
	blr

_GLOBAL(_set_THRM3)
	mtspr	THRM3,r3
	blr
	
_GLOBAL(_get_PVR)
	mfspr	r3,PVR
	blr

#ifdef CONFIG_8xx
_GLOBAL(_get_IMMR)
	mfspr	r3, 638
	blr
#endif
	
_GLOBAL(_get_HID0)
	mfspr	r3,HID0
	blr

_GLOBAL(_get_ICTC)
	mfspr	r3,ICTC
	blr

_GLOBAL(_set_ICTC)
	mtspr	ICTC,r3
	blr

	
/*
	L2CR functions
	Copyright © 1997-1998 by PowerLogix R & D, Inc.
	
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
/*
	Thur, Dec. 12, 1998.
	- First public release, contributed by PowerLogix.
	
	Author:	Terry Greeniaus (tgree@phys.ualberta.ca)
	Please e-mail updates to this file to me, thanks!
*/
/* Usage:
	
	When setting the L2CR register, you must do a few special
	things.  If you are enabling the cache, you must perform a
	global invalidate.  If you are disabling the cache, you must
	flush the cache contents first.  This routine takes care of
	doing these things.  When first enabling the cache, make sure
	you pass in the L2CR you want, as well as passing in the
	global invalidate bit set.  A global invalidate will only be
	performed if the L2I bit is set in applyThis.  When enabling
	the cache, you should also set the L2E bit in applyThis.  If
	you want to modify the L2CR contents after the cache has been
	enabled, the recommended procedure is to first call
	__setL2CR(0) to disable the cache and then call it again with
	the new values for L2CR.  Examples:

	_setL2CR(0)		- disables the cache
	_setL2CR(0xB3A04000)	- enables my G3 upgrade card:
				- L2E set to turn on the cache
				- L2SIZ set to 1MB
				- L2CLK set to 1:1
				- L2RAM set to pipelined synchronous late-write
				- L2I set to perform a global invalidation
				- L2OH set to 0.5 nS
				- L2DF set because this upgrade card
				  requires it

	A similar call should work for your card.  You need to know
	the correct setting for your card and then place them in the
	fields I have outlined above.  Other fields support optional
	features, such as L2DO which caches only data, or L2TS which
	causes cache pushes from the L1 cache to go to the L2 cache
	instead of to main memory.
*/

_GLOBAL(_set_L2CR)
	/* Make sure this is a 750 chip */
	mfspr	r4,PVR
	rlwinm	r4,r4,16,16,31
	cmplwi	r4,0x0008
	beq	thisIs750
	cmplwi	r4,0x000c
	beq thisIs750
	li	r3,-1
	blr
	
thisIs750:
	/* Get the current enable bit of the L2CR into r4 */
	mfspr	r4,L2CR
	mfmsr	r7
	
	/* See if we want to perform a global inval this time. */
	rlwinm	r6,r3,0,10,10	/* r6 contains the new invalidate bit */
	rlwinm.	r5,r3,0,0,0	/* r5 contains the new enable bit */
	rlwinm	r3,r3,0,11,9	/* Turn off the invalidate bit */
	rlwimi	r3,r4,0,0,0	/* Keep the enable bit the same as it was. */
	bne	dontDisableCache /* Only disable the cache if L2CRApply
				    has the enable bit off */

disableCache:
	/* Disable the cache.  First, we turn off interrupts.
	   An interrupt while we are flushing the cache could bring
	   in data which may not get properly flushed. */
	rlwinm	r4,r7,0,17,15	/* Turn off EE bit */
	sync
	mtmsr	r4
	sync
	
/*
	Now, read the first 2MB of memory to put new data in the cache.
	(Actually we only need the size of the L2 cache plus the size
	of the L1 cache, but 2MB will cover everything just to be safe).
*/
	lis	r4,0x0001
	mtctr	r4
	lis	r4,KERNELBASE@h
1:	lwzx	r0,r0,r4
	addi	r4,r4,0x0020		/* Go to start of next cache line */
	bdnz	1b
	
	/* Now, flush the first 2MB of memory */
	lis	r4,0x0001
	mtctr	r4
	lis	r4,KERNELBASE@h
	sync
2:	dcbf	r0,r4
	addi	r4,r4,0x0020	/* Go to start of next cache line */
	bdnz	2b
	
	/* Turn off the L2CR enable bit. */
	rlwinm	r3,r3,0,1,31
	
dontDisableCache:
	/* Set up the L2CR configuration bits */
	sync
	mtspr	L2CR,r3
	sync

	/* Reenable interrupts if necessary. */
	mtmsr	r7
	sync
	
	cmplwi	r6,0
	beq	noInval
	
	/* Perform a global invalidation */
	oris	r3,r3,0x0020
	sync
	mtspr	L2CR,r3
	sync

	/* Wait for the invalidation to complete */
3:	mfspr	r3,L2CR
	rlwinm.	r4,r3,0,31,31
	bne	3b
	
	rlwinm	r3,r3,0,11,9		/* Turn off the L2I bit */
	sync
	mtspr	L2CR,r3
	sync
	
noInval:
	/* See if we need to enable the cache */
	cmplwi	r5,0
	beqlr

	/* Enable the cache */
	oris	r3,r3,0x8000
	mtspr	L2CR,r3
	sync
	blr

_GLOBAL(_get_L2CR)
	/* Make sure this is a 750 chip */
	mfspr	r3,PVR
	rlwinm	r3,r3,16,16,31
	cmplwi	r3,0x0008
	beq	1f
	cmplwi	r3,0x000c
	li	r3,0
	bnelr
1:	
	/* Return the L2CR contents */
	mfspr	r3,L2CR
	blr

/* --- End of PowerLogix code ---
 */

/*
_GLOBAL(_get_L2CR)
	mfspr	r3,L2CR
	blr

_GLOBAL(_set_L2CR)
	mtspr	L2CR,r3
	blr
		
*/

/*
 * These are used in the alignment trap handler when emulating
 * single-precision loads and stores.
 * We restore and save the fpscr so the task gets the same result
 * and exceptions as if the cpu had performed the load or store.
 */

#if defined(CONFIG_4xx)
_GLOBAL(cvt_fd)
	lfs	0,0(r3)
	stfd	0,0(r4)
	blr

_GLOBAL(cvt_df)
	lfd	0,0(r3)
	stfs	0,0(r4)
	blr
#else
_GLOBAL(cvt_fd)
	lfd	0,-4(r5)	/* load up fpscr value */
	mtfsf	0xff,0
	lfs	0,0(r3)
	stfd	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,-4(r5)
	blr

_GLOBAL(cvt_df)
	lfd	0,-4(r5)	/* load up fpscr value */
	mtfsf	0xff,0
	lfd	0,0(r3)
	stfs	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,-4(r5)
	blr
#endif

_GLOBAL(__clear_msr_me)
        mfmsr   r0                      /* Get current interrupt state */
        lis     r3,0
        ori     r3,r3,MSR_ME
        andc    r0,r0,r3                /* Clears bit in (r4) */
        sync                            /* Some chip revs have problems here */
        mtmsr   r0                      /* Update machine state */
        blr

/*
 * Create a kernel thread
 *   kernel_thread(fn, arg, flags)
 */
_GLOBAL(kernel_thread)
	mr	r6,r3		/* function */
	ori	r3,r5,CLONE_VM	/* flags */
	li	r0,__NR_clone
	sc
	cmpi	0,r3,0		/* parent or child? */
	bnelr			/* return if parent */
	li	r0,0		/* clear out p->thread.regs */
	stw	r0,THREAD+PT_REGS(r2)	/* since we don't have user ctx */
	mtlr	r6              /* fn addr in lr */
	mr	r3,r4	        /* load arg and call fn */
	blrl
	li	r0,__NR_exit	/* exit after child exits */
        li	r3,0
	sc

/*
 * This routine is just here to keep GCC happy - sigh...
 */	
_GLOBAL(__main)
	blr

#define SYSCALL(name) \
_GLOBAL(name) \
	li	r0,__NR_##name; \
	sc; \
	bnslr; \
	lis	r4,errno@ha; \
	stw	r3,errno@l(r4); \
	li	r3,-1; \
	blr

#define __NR__exit __NR_exit

SYSCALL(sync)
SYSCALL(setsid)
SYSCALL(write)
SYSCALL(dup)
SYSCALL(execve)
SYSCALL(open)
SYSCALL(close)
SYSCALL(waitpid)
SYSCALL(fork)
SYSCALL(delete_module)
SYSCALL(_exit)
SYSCALL(lseek)
SYSCALL(read)

/* Why isn't this a) automatic, b) written in 'C'? */	
	.data
	.align 4
_GLOBAL(sys_call_table)
	.long sys_ni_syscall	/* 0  -  old "setup()" system call */
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink	/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_lchown
	.long sys_ni_syscall			/* old break syscall holder */
	.long sys_stat
	.long sys_lseek
	.long sys_getpid	/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid
	.long sys_getuid
	.long sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_fstat
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_ni_syscall			/* old stty syscall holder */
	.long sys_ni_syscall			/* old gtty syscall holder */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall	/* 35 */	/* old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall			/* old prof syscall holder */
	.long sys_brk		/* 45 */
	.long sys_setgid
	.long sys_getgid
	.long sys_signal
	.long sys_geteuid
	.long sys_getegid	/* 50 */
	.long sys_acct
	.long sys_umount			/* recycled never used phys() */
	.long sys_ni_syscall			/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall			/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall			/* old ulimit syscall holder */
	.long sys_olduname
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid	/* 70 */
	.long sys_setregid
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_old_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups	/* 80 */
	.long sys_setgroups
	.long ppc_select
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long old_readdir
	.long sys_mmap		/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown	/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall			/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs	/* 100 */
	.long sys_ioperm
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_uname
	.long sys_iopl		/* 110 */
	.long sys_vhangup
	.long sys_ni_syscall	/* old 'idle' syscall */
	.long sys_vm86
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_modify_ldt
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_create_module
	.long sys_init_module
	.long sys_delete_module
	.long sys_get_kernel_syms	/* 130 */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long sys_ni_syscall	/* for afs_syscall */
	.long sys_setfsuid
	.long sys_setfsgid
	.long sys_llseek	/* 140 */
        .long sys_getdents
	.long ppc_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam	/* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid
	.long sys_getresuid	/* 165 */
	.long sys_query_module
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid
	.long sys_getresgid	/* 170 */
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	
	.long sys_rt_sigpending	/* 175 */
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread
	.long sys_pwrite	/* 180 */
	.long sys_chown
	.long sys_getcwd
	.long sys_capget
	.long sys_capset
	.long sys_sigaltstack	/* 185 */
	.long sys_sendfile
	.long sys_ni_syscall		/* streams1 */
	.long sys_ni_syscall		/* streams2 */
	.long sys_vfork
	.long sys_getrlimit	/* 190 */
	.long sys_ni_syscall		/* 191 */	/* Unused */
	.long sys_ni_syscall		/* 192 - reserved - mmap2 */
	.long sys_ni_syscall		/* 193 - reserved - truncate64 */
	.long sys_ni_syscall		/* 194 - reserved - ftruncate64 */
	.long sys_ni_syscall		/* 195 - reserved - stat64 */
	.long sys_ni_syscall		/* 196 - reserved - lstat64 */
	.long sys_ni_syscall		/* 197 - reserved - fstat64 */
	.long sys_pciconfig_read	/* 198 */
	.long sys_pciconfig_write 	/* 199 */
	.long sys_pciconfig_iobase 	/* 200 */
	.long sys_ni_syscall		/* 201 - reserved - MacOnLinux - new */
	.long sys_getdents64		/* 202 */
	.rept NR_syscalls-(.-sys_call_table)/4
		.long sys_ni_syscall
	.endr
