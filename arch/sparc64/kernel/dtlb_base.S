/* $Id: dtlb_base.S,v 1.8 2000/11/10 08:28:45 davem Exp $
 * dtlb_base.S:	Front end to DTLB miss replacement strategy.
 *              This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#define TAG_CONTEXT_BITS	0x3ff
#define VPTE_SHIFT		(PAGE_SHIFT - 3)
#define KERN_HIGHBITS		((_PAGE_VALID | _PAGE_SZ4MB) ^ 0xfffff80000000000)
#define KERN_LOWBITS		(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)
#define KERN_LOWBITS_IO		(_PAGE_E | _PAGE_P | _PAGE_W)
#define KERN_IOBITS		(KERN_LOWBITS ^ KERN_LOWBITS_IO)

/* %g1	TLB_SFSR	(%g1 + %g1 == TLB_TAG_ACCESS)
 * %g2	(KERN_HIGHBITS | KERN_LOWBITS)
 * %g3  VPTE base	(0xfffffffe00000000)	Spitfire/Blackbird (44-bit VA space)
 *			(0xffe0000000000000)	Cheetah		   (64-bit VA space)
 * %g7	__pa(current->mm->pgd)
 *
 * The VPTE base value is completely magic, but note that
 * nothing else in the kernel other than these TLB miss
 * handlers know anything about the VPTE mechanism or
 * how it works.  Consider the 44-bit VADDR Ultra-I/II
 * case as an example:
 *
 * VA[0 :  (1<<43)] produce VPTE index [%g3                        :   0]
 * VA[0 : -(1<<43)] produce VPTE index [%g3-(1<<(43-PAGE_SHIFT+3)) : %g3]
 *
 * For Cheetah's 64-bit VADDR space this is:
 *
 * VA[0 :  (1<<63)] produce VPTE index [%g3                        :   0]
 * VA[0 : -(1<<63)] produce VPTE index [%g3-(1<<(63-PAGE_SHIFT+3)) : %g3]
 *
 * If you're paying attention you'll notice that this means half of
 * the VPTE table is above %g3 and half is below, low VA addresses
 * map progressively upwards from %g3, and high VA addresses map
 * progressively downwards from %g3.  This trick was needed to make
 * the same 8 instruction handler work both for Spitfire/Blackbird's
 * peculiar VA space hole configuration and the full 64-bit VA space
 * one of Cheetah at the same time.
 */

/* Ways we can get here:
 *
 * 1) Nucleus loads and stores to/from PA-->VA direct mappings.
 * 2) Nucleus loads and stores to/from vmalloc() areas.
 * 3) User loads and stores.
 * 4) User space accesses by nucleus at tl0
 */

/* DTLB ** ICACHE line 1: Quick user TLB misses		*/
	ldxa		[%g1 + %g1] ASI_DMMU, %g4	! Get TAG_ACCESS
	andcc		%g4, TAG_CONTEXT_BITS, %g0	! From Nucleus?
	be,pn		%xcc, 3f			! Yep, special processing
	 srax		%g4, VPTE_SHIFT, %g6		! Create VPTE offset
	ldxa		[%g3 + %g6] ASI_S, %g5		! Load VPTE
1:	brlz,pt		%g5, 9f				! Valid, load into TLB
	 nop						! Delay-slot
	ba,a,pt		%xcc, 4f			! Invalid, branch out

/* DTLB ** ICACHE line 2: Quick kernel TLB misses	*/
3:	brlz,pt		%g4, 9f				! Kernel virtual map?
	 xor		%g2, %g4, %g5			! Finish bit twiddles
	ldxa		[%g3 + %g6] ASI_N, %g5		! Yep, load k-vpte
	ba,pt		%xcc, 1b			! Continue tlb reload
	 nop
9:	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Reload TLB
	retry						! Trap return
4:	rdpr		%pstate, %g5			! Move into alternate globals

/* DTLB ** ICACHE line 3: winfixups+real_faults		*/
	wrpr		%g5, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tl, %g4			! See where we came from.
	cmp		%g4, 1				! Is etrap/rtrap window fault?
	mov		TLB_TAG_ACCESS, %g4		! Prepare for fault processing
	ldxa		[%g4] ASI_DMMU, %g5		! Load faulting VA page
	be,pt		%xcc, sparc64_realfault_common	! Jump to normal fault handling
	 mov		FAULT_CODE_DTLB, %g4		! It was read from DTLB
	ba,a,pt		%xcc, winfix_trampoline		! Call window fixup code

/* DTLB ** ICACHE line 4: Unused...	*/
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

#undef TAG_CONTEXT_BITS
#undef VPTE_SHIFT
#undef KERN_HIGHBITS
#undef KERN_LOWBITS
#undef KERN_LOWBITS_IO
#undef KERN_IOBITS
