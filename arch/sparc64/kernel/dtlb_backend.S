/* $Id: dtlb_backend.S,v 1.13 2001/08/17 04:55:09 kanoj Exp $
 * dtlb_backend.S: Back end to DTLB miss replacement strategy.
 *                 This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <asm/pgtable.h>

#if PAGE_SHIFT == 13
#define FILL_VALID_SZ_BITS1(r1) \
	 sllx		%g2, 62, r1
#define FILL_VALID_SZ_BITS2(r1)
#define FILL_VALID_SZ_BITS_NOP nop
#else /* PAGE_SHIFT */
#define FILL_VALID_SZ_BITS1(r1) \
	or		%g0, 5, r1
#define FILL_VALID_SZ_BITS2(r1) \
	sllx		r1, 61, r1
#define FILL_VALID_SZ_BITS_NOP
#endif /* PAGE_SHIFT */

#define VPTE_BITS		(_PAGE_CP | _PAGE_CV | _PAGE_P )
#define VPTE_SHIFT		(PAGE_SHIFT - 3)
#define TLB_PMD_SHIFT		(PAGE_SHIFT - 3 + 3)
#define TLB_PGD_SHIFT		(PAGE_SHIFT - 2 + PAGE_SHIFT - 3 + 3)
#define TLB_PMD_MASK		(((1 << PMD_BITS) - 1) << 1)
#define TLB_PGD_MASK		(((1 << (VA_BITS - PAGE_SHIFT - (PAGE_SHIFT - 3) - PMD_BITS)) - 1) << 2)

/* Ways we can get here:
 *
 * 1) Nucleus loads and stores to/from PA-->VA direct mappings at tl>1.
 * 2) Nucleus loads and stores to/from user/kernel window save areas.
 * 3) VPTE misses from dtlb_base and itlb_base.
 */

/* TLB1 ** ICACHE line 1: tl1 DTLB and quick VPTE miss	*/
	ldxa		[%g1 + %g1] ASI_DMMU, %g4	! Get TAG_ACCESS
	add		%g3, %g3, %g5			! Compute VPTE base
	cmp		%g4, %g5			! VPTE miss?
	blu,pn		%xcc, from_tl1_trap		! Fall to tl0 miss
	 andcc		%g4, TAG_CONTEXT_BITS, %g5	! From Nucleus? (for tl0 miss)
	sllx		%g6, VPTE_SHIFT, %g4		! Position TAG_ACCESS
	or		%g4, %g5, %g4			! Prepare TAG_ACCESS
	mov		TSB_REG, %g1			! Grab TSB reg

/* TLB1 ** ICACHE line 2: Quick VPTE miss	  	*/
	ldxa		[%g1] ASI_DMMU, %g5		! Doing PGD caching?
	srlx		%g6, (TLB_PMD_SHIFT - 1), %g1	! Position PMD offset
	be,pn		%xcc, sparc64_vpte_nucleus	! Is it from Nucleus?
	 and		%g1, TLB_PMD_MASK, %g1		! Mask PMD offset bits
	brnz,pt		%g5, sparc64_vpte_continue	! Yep, go like smoke
	 add		%g1, %g1, %g1			! Position PMD offset some more
	srlx		%g6, (TLB_PGD_SHIFT - 2), %g5	! Position PGD offset
	and		%g5, TLB_PGD_MASK, %g5		! Mask PGD offset

/* TLB1 ** ICACHE line 3: Quick VPTE miss	  	*/
	lduwa		[%g7 + %g5] ASI_PHYS_USE_EC, %g5! Load PGD
	brz,pn		%g5, vpte_noent			! Valid?
sparc64_kpte_continue:
	 sllx		%g5, 11, %g5			! Shift into place
sparc64_vpte_continue:
	lduwa		[%g5 + %g1] ASI_PHYS_USE_EC, %g5! Load PMD
	sllx		%g5, 11, %g5			! Shift into place
	brz,pn		%g5, vpte_noent			! Valid?
	 FILL_VALID_SZ_BITS1(%g1)			! Put _PAGE_VALID into %g1
	FILL_VALID_SZ_BITS2(%g1)			! Put _PAGE_VALID into %g1
	or		%g5, VPTE_BITS, %g5		! Prepare VPTE data

/* TLB1 ** ICACHE line 4: Quick VPTE miss	  	*/
	or		%g5, %g1, %g5			! ...
	mov		TLB_SFSR, %g1			! Restore %g1 value
	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Load VPTE into TLB
	stxa		%g4, [%g1 + %g1] ASI_DMMU	! Restore previous TAG_ACCESS
	retry						! Load PTE once again
	nop
	nop
	FILL_VALID_SZ_BITS_NOP

#undef VPTE_SHIFT
#undef TLB_PMD_SHIFT
#undef TLB_PGD_SHIFT
#undef VPTE_BITS
#undef TLB_PMD_MASK
#undef TLB_PGD_MASK
#undef FILL_VALID_SZ_BITS1
#undef FILL_VALID_SZ_BITS2
#undef FILL_VALID_SZ_BITS_NOP

