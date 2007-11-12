/* 
 *   Creation Date: <2001/02/24 14:08:28 samuel>
 *   Time-stamp: <2003/09/03 12:34:20 samuel>
 *   
 *	<platform.S>
 *	
 *	Linux Kernel Hooks
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

	////////////////////////////////////////////////////////
	// flush_hash_page_hook
	//
	// Kernel hook
	//
	//    r3:	context
	//    r4:	virtual address
	//    r5:	2.4.6	pointer to linux PTE (2.4.6 or later)
	//		2.6	pmdval
	//    r6:	2.6	count
	//    r10:	return address
	//
	// [must not modify: r3-r5,r10 - otherwise normal C-function]
	// MMU is ON

.set STACK_SPACE,	32
.set STACK_LR,		STACK_SPACE+4
.set STACK_V0,		8
.set STACK_V1,		12
.set STACK_V2,		16
.set STACK_V3,		20
.set STACK_V4,		24
.set STACK_V5,		28

FHOOK( FHOOK_FLUSH_HASH_PAGE )
GLOBAL_SYMBOL( r__flush_hash_page_hook ):
	stwu	r1,-STACK_SPACE(r1)	// Push stackframe
	mflr	r0
	stw	r0,STACK_LR(r1)

	LOADI	r7,EXTERN(do_flush)
	mtctr	r7

	stw	r10,STACK_V1(r1)	// Save registers
	stw	r3,STACK_V2(r1)
	stw	r4,STACK_V3(r1)
	stw	r5,STACK_V4(r1)
#ifdef LINUX_26
	stw	r6,STACK_V5(r1)
#else
	li	r6,1
#endif	
	bctrl

	lwz	r10,STACK_V1(r1)	// Restore registers
	lwz	r3,STACK_V2(r1)
	lwz	r4,STACK_V3(r1)
	lwz	r5,STACK_V4(r1)
#ifdef LINUX_26
	lwz	r6,STACK_V5(r1)
#endif

	lwz	r0,STACK_LR(r1)		// Pop stackframe
	addi	r1,r1,STACK_SPACE
	mtlr	r0
	blr

	
/************************************************************************/
/*	lowmem reallocations						*/
/************************************************************************/

#ifdef CONFIG_SMP

	/* r3=ea, r4=pte_slot, r5=pte1, r6=pte2, r7=lock, r8=lockval, M: r0 */
RELOC_LOW(xx_store_pte_lowmem)
1:	lwarx	r0,0,r7
	cmpwi	r0,0
	bne-	1b
	stwcx.	r8,0,r7			// take hash lock
	bne-	1b
	// isync			// sync below is sufficient
	stw	r0,0(r4)		// clear old PTE[V] (if we evict something)
	sync				// probably not needed - no other CPU uses this PTE
	stw	r6,4(r4)		// store PTE2
	eieio
	stw	r5,0(r4)		// store PTE1
	tlbie	r3			// flush old pte
	eieio				// order tlbie before tlbsync
	tlbsync				// ensure tlbie finish on all processors
	sync				// ensure tlbsync completed
	stw	r0,0(r7)		// release hash lock
	blr
RELOC_LOW_END(xx_store_pte_lowmem)


	/* r3=ea, r7=lock, r8=lockval, M: r0 */
RELOC_LOW(xx_tlbie_lowmem)
1:	lwarx	r0,0,r7
	cmpwi	r0,0
	bne-	1b
	stwcx.	r8,0,r7			// take hash lock
	bne-	1b
	//isync				// needed if we get rid of the sync
	sync				// make sure any PTE zero-outs have finished
	tlbie	r3			// flush old pte
	eieio				// order tlbie before tlbsync
	tlbsync				// ensure tlbie finish on all processors
	sync				// ensure tlbsync completed
	stw	r0,0(r7)		// release hash lock
	blr
RELOC_LOW_END(xx_tlbie_lowmem)

#else
	
	/* r3=pte_slot, r4=pte0, r5=pte1 */
RELOC_LOW(xx_store_pte_lowmem)
	stw	r4,0(r3)		// interrupts are off and we won't take a page fault
	stw	r5,4(r3)		// so this is safe...
	blr	
RELOC_LOW_END(xx_store_pte_lowmem)

#endif
