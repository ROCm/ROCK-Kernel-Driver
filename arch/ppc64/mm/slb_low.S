/*
 * arch/ppc64/mm/slb_low.S
 *
 * Low-level SLB routines
 *
 * Copyright (C) 2004 David Gibson <dwg@au.ibm.com>, IBM
 *
 * Based on earlier C version:
 * Dave Engebretsen and Mike Corrigan {engebret|mikejc}@us.ibm.com
 *    Copyright (c) 2001 Dave Engebretsen
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>
#include <asm/cputable.h>

/* void slb_allocate(unsigned long ea);
 *
 * Create an SLB entry for the given EA (user or kernel).
 * 	r3 = faulting address, r13 = PACA
 *	r9, r10, r11 are clobbered by this function
 * No other registers are examined or changed.
 */
_GLOBAL(slb_allocate)
	/*
	 * First find a slot, round robin. Previously we tried to find
	 * a free slot first but that took too long. Unfortunately we
	 * dont have any LRU information to help us choose a slot.
	 */
	ld	r10,PACASTABRR(r13)
3:
	addi	r10,r10,1
	/* use a cpu feature mask if we ever change our slb size */
	cmpldi	r10,SLB_NUM_ENTRIES

	blt+	4f
	li	r10,SLB_NUM_BOLTED

	/*
	 * Never cast out the segment for our kernel stack. Since we
	 * dont invalidate the ERAT we could have a valid translation
	 * for the kernel stack during the first part of exception exit
	 * which gets invalidated due to a tlbie from another cpu at a
	 * non recoverable point (after setting srr0/1) - Anton
	 */
4:	slbmfee	r11,r10
	srdi	r11,r11,27
	/*
	 * Use paca->ksave as the value of the kernel stack pointer,
	 * because this is valid at all times.
	 * The >> 27 (rather than >> 28) is so that the LSB is the
	 * valid bit - this way we check valid and ESID in one compare.
	 * In order to completely close the tiny race in the context
	 * switch (between updating r1 and updating paca->ksave),
	 * we check against both r1 and paca->ksave.
	 */
	srdi	r9,r1,27
	ori	r9,r9,1			/* mangle SP for later compare */
	cmpd	r11,r9
	beq-	3b
	ld	r9,PACAKSAVE(r13)
	srdi	r9,r9,27
	ori	r9,r9,1
	cmpd	r11,r9
	beq-	3b

	std	r10,PACASTABRR(r13)

	/* r3 = faulting address, r10 = entry */

	srdi	r9,r3,60		/* get region */
	srdi	r3,r3,28		/* get esid */
	cmpldi	cr7,r9,0xc		/* cmp KERNELBASE for later use */

	/* r9 = region, r3 = esid, cr7 = <>KERNELBASE */

	rldicr.	r11,r3,32,16
	bne-	8f			/* invalid ea bits set */
	addi	r11,r9,-1
	cmpldi	r11,0xb
	blt-	8f			/* invalid region */

	/* r9 = region, r3 = esid, r10 = entry, cr7 = <>KERNELBASE */

	blt	cr7,0f			/* user or kernel? */

	/* kernel address */
	li	r11,SLB_VSID_KERNEL
BEGIN_FTR_SECTION
	bne	cr7,9f
	li	r11,(SLB_VSID_KERNEL|SLB_VSID_L)
END_FTR_SECTION_IFSET(CPU_FTR_16M_PAGE)
	b	9f

0:	/* user address */
	li	r11,SLB_VSID_USER
#ifdef CONFIG_HUGETLB_PAGE
BEGIN_FTR_SECTION
	/* check against the hugepage ranges */
	cmpldi	r3,(TASK_HPAGE_END>>SID_SHIFT)
	bge	6f			/* >= TASK_HPAGE_END */
	cmpldi	r3,(TASK_HPAGE_BASE>>SID_SHIFT)
	bge	5f			/* TASK_HPAGE_BASE..TASK_HPAGE_END */
	cmpldi	r3,16
	bge	6f			/* 4GB..TASK_HPAGE_BASE */

	lhz	r9,PACAHTLBSEGS(r13)
	srd	r9,r9,r3
	andi.	r9,r9,1
	beq	6f

5:	/* this is a hugepage user address */
	li	r11,(SLB_VSID_USER|SLB_VSID_L)
END_FTR_SECTION_IFSET(CPU_FTR_16M_PAGE)
#endif /* CONFIG_HUGETLB_PAGE */

6:	ld	r9,PACACONTEXTID(r13)

9:	/* r9 = "context", r3 = esid, r11 = flags, r10 = entry */

	rldimi	r9,r3,15,0		/* r9= VSID ordinal */

7:	rldimi	r10,r3,28,0		/* r10= ESID<<28 | entry */
	oris	r10,r10,SLB_ESID_V@h	/* r10 |= SLB_ESID_V */

	/* r9 = ordinal, r3 = esid, r11 = flags, r10 = esid_data */

	li	r3,VSID_RANDOMIZER@higher
	sldi	r3,r3,32
	oris	r3,r3,VSID_RANDOMIZER@h
	ori	r3,r3,VSID_RANDOMIZER@l

	mulld	r9,r3,r9		/* r9 = ordinal * VSID_RANDOMIZER */
	clrldi	r9,r9,28		/* r9 &= VSID_MASK */
	sldi	r9,r9,SLB_VSID_SHIFT	/* r9 <<= SLB_VSID_SHIFT */
	or	r9,r9,r11		/* r9 |= flags */

	/* r9 = vsid_data, r10 = esid_data, cr7 = <>KERNELBASE */

	/*
	 * No need for an isync before or after this slbmte. The exception
	 * we enter with and the rfid we exit with are context synchronizing.
	 */
	slbmte	r9,r10

	bgelr	cr7			/* we're done for kernel addresses */

	/* Update the slb cache */
	lhz	r3,PACASLBCACHEPTR(r13)	/* offset = paca->slb_cache_ptr */
	cmpldi	r3,SLB_CACHE_ENTRIES
	bge	1f

	/* still room in the slb cache */
	sldi	r11,r3,1		/* r11 = offset * sizeof(u16) */
	rldicl	r10,r10,36,28		/* get low 16 bits of the ESID */
	add	r11,r11,r13		/* r11 = (u16 *)paca + offset */
	sth	r10,PACASLBCACHE(r11)	/* paca->slb_cache[offset] = esid */
	addi	r3,r3,1			/* offset++ */
	b	2f
1:					/* offset >= SLB_CACHE_ENTRIES */
	li	r3,SLB_CACHE_ENTRIES+1
2:
	sth	r3,PACASLBCACHEPTR(r13)	/* paca->slb_cache_ptr = offset */
	blr

8:	/* invalid EA */
	li	r9,0			/* 0 VSID ordinal -> BAD_VSID */
	li	r11,SLB_VSID_USER	/* flags don't much matter */
	b	7b
