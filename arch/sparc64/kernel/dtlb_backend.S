/* $Id: dtlb_backend.S,v 1.9 2000/01/31 04:59:12 davem Exp $
 * dtlb_backend.S: Back end to DTLB miss replacement strategy.
 *                 This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#define TAG_CONTEXT_BITS	0x3ff
#define VPTE_SHIFT		(PAGE_SHIFT - 3)
#define PMD_SHIFT		(23 - PAGE_SHIFT + 3)
#define PGD_SHIFT		(34 - PAGE_SHIFT + 3)
#define VPTE_BITS		(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)

/* Ways we can get here:
 *
 * 1) Nucleus loads and stores to/from PA-->VA direct mappings at tl>1.
 * 2) Nucleus loads and stores to/from user/kernel window save areas.
 * 3) VPTE misses from dtlb_base, dtlb_prot, and itlb_base.  But this only
 *    happens for non-nucleus contexts.  Nucleus VPTE's cannot work because
 *    of how OBP uses the same part of the address space in ctx 0.
 */

/* TLB1 ** ICACHE line 1: tl1 DTLB and quick VPTE miss	*/
	ldxa		[%g1 + %g1] ASI_DMMU, %g4	! Get TAG_ACCESS
	add		%g3, %g3, %g5			! Compute VPTE base
	cmp		%g4, %g5			! VPTE miss?
	blu,pn		%xcc, .-0x4004			! Fall to tl0 miss
	 andcc		%g4, TAG_CONTEXT_BITS, %g5	! From Nucleus? (for tl0 miss)
	sllx		%g6, VPTE_SHIFT, %g4		! Position TAG_ACCESS
	or		%g4, %g5, %g4			! Prepare TAG_ACCESS
	mov		TSB_REG, %g1			! Grab TSB reg

/* TLB1 ** ICACHE line 2: Quick VPTE miss	  	*/
	ldxa		[%g1] ASI_DMMU, %g5		! Doing PGD caching?
	srlx		%g6, (PMD_SHIFT - 1), %g1	! Position PMD offset
	be,pn		%xcc, sparc64_vpte_nucleus	! Is it from Nucleus?
	 and		%g1, 0xffe, %g1			! Mask PMD offset bits
	brnz,pt		%g5, sparc64_vpte_continue	! Yep, go like smoke
	 add		%g1, %g1, %g1			! Position PMD offset some more
	srlx		%g6, (PGD_SHIFT - 2), %g5	! Position PGD offset
	and		%g5, 0xffc, %g5			! Mask PGD offset

/* TLB1 ** ICACHE line 3: Quick VPTE miss	  	*/
	lduwa		[%g7 + %g5] ASI_PHYS_USE_EC, %g5! Load PGD
	brz,pn		%g5, vpte_noent			! Valid?
sparc64_kpte_continue:
	 sllx		%g5, 11, %g5			! Shift into place
sparc64_vpte_continue:
	lduwa		[%g5 + %g1] ASI_PHYS_USE_EC, %g5! Load PMD
	sllx		%g5, 11, %g5			! Shift into place
	brz,pn		%g5, vpte_noent			! Valid?
	 sllx		%g2, 62, %g1			! Put _PAGE_VALID into %g1
	or		%g5, VPTE_BITS, %g5		! Prepare VPTE data

/* TLB1 ** ICACHE line 4: Quick VPTE miss	  	*/
	or		%g5, %g1, %g5			! ...
	mov		TLB_SFSR, %g1			! Restore %g1 value
	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Load VPTE into TLB
	stxa		%g4, [%g1 + %g1] ASI_DMMU	! Restore previous TAG_ACCESS
	retry						! Load PTE once again
vpte_noent:
	mov		TLB_SFSR, %g1			! Restore %g1 value
	stxa		%g4, [%g1 + %g1] ASI_DMMU	! Restore previous TAG_ACCESS
	done						! Slick trick

#undef TAG_CONTEXT_BITS
#undef VPTE_SHIFT
#undef PMD_SHIFT
#undef PGD_SHIFT
#undef VPTE_BITS
