/* $Id: blockops.S,v 1.27 2000/07/14 01:12:49 davem Exp $
 * blockops.S: UltraSparc block zero optimized routines.
 *
 * Copyright (C) 1996, 1998, 1999, 2000 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997 Jakub Jelinek (jakub@redhat.com)
 */

#include "VIS.h"
#include <asm/visasm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/asm_offsets.h>

#define TOUCH(reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7)	\
	fmovd	%reg0, %f48; 	fmovd	%reg1, %f50;		\
	fmovd	%reg2, %f52; 	fmovd	%reg3, %f54;		\
	fmovd	%reg4, %f56; 	fmovd	%reg5, %f58;		\
	fmovd	%reg6, %f60; 	fmovd	%reg7, %f62;

#define	TLBTEMP_BASE	(8 * 1024 * 1024)
#define	DCACHE_SIZE	(PAGE_SIZE * 2)
#define TLBTEMP_ENT1	(61 << 3)
#define TLBTEMP_ENT2	(62 << 3)
#define TLBTEMP_ENTSZ	(1 << 3)

	.text

	.align		32
	.globl		_copy_page
	.type		_copy_page,@function
_copy_page: /* %o0=dest, %o1=src */
	VISEntry
	membar		#LoadStore | #StoreStore | #StoreLoad
	ldda		[%o1] ASI_BLK_P, %f0
	add		%o1, 0x40, %o1
	ldda		[%o1] ASI_BLK_P, %f16
	add		%o1, 0x40, %o1
	sethi		%hi(8192), %o2
1:	TOUCH(f0, f2, f4, f6, f8, f10, f12, f14)
	ldda		[%o1] ASI_BLK_P, %f32
	stda		%f48, [%o0] ASI_BLK_P
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f16, f18, f20, f22, f24, f26, f28, f30)
	ldda		[%o1] ASI_BLK_P, %f0
	stda		%f48, [%o0] ASI_BLK_P
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f32, f34, f36, f38, f40, f42, f44, f46)
	ldda		[%o1] ASI_BLK_P, %f16
	stda		%f48, [%o0] ASI_BLK_P
	sub		%o2, 0x40, %o2
	add		%o1, 0x40, %o1
	cmp		%o2, 0x80
	bne,pt		%xcc, 1b
	 add		%o0, 0x40, %o0
	membar		#Sync
	stda		%f0, [%o0] ASI_BLK_P
	add		%o0, 0x40, %o0
	stda		%f16, [%o0] ASI_BLK_P
	membar		#Sync
	VISExit
	retl
	 nop

	.globl		copy_user_page
	.type		copy_user_page,@function
copy_user_page: /* %o0=dest, %o1=src, %o2=vaddr */
	VISEntry
	sethi		%hi(PAGE_SIZE), %g3
	sub		%o0, %g4, %g1
	and		%o2, %g3, %o0
	sethi		%hi(TLBTEMP_BASE), %o3
	sethi		%uhi(_PAGE_VALID), %g3
	sub		%o1, %g4, %g2
	sllx		%g3, 32, %g3
	mov		TLB_TAG_ACCESS, %o2
	or		%g3, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W), %g3
	sethi		%hi(DCACHE_SIZE), %o1
	or		%g1, %g3, %g1
	or		%g2, %g3, %g2
	add		%o0, %o3, %o0
	add		%o0, %o1, %o1
	mov		TLBTEMP_ENT1, %o3
	rdpr		%pstate, %g3
	wrpr		%g3, PSTATE_IE, %pstate

	/* Do this now, before loading the fixed TLB entries for copying,
	 * so we do not risk a multiple TLB match condition later when
	 * restoring those entries.
	 */
	ldub		[%g6 + AOFF_task_thread + AOFF_thread_use_blkcommit], %g3

	/* Spitfire Errata #32 workaround */
	mov		0x8, %o4
	stxa		%g0, [%o4] ASI_DMMU
	sethi		%hi(empty_zero_page), %o4
	flush		%o4

	ldxa		[%o3] ASI_DTLB_TAG_READ, %o4

	/* Spitfire Errata #32 workaround */
	mov		0x8, %o5
	stxa		%g0, [%o5] ASI_DMMU
	sethi		%hi(empty_zero_page), %o5
	flush		%o5

	ldxa		[%o3] ASI_DTLB_DATA_ACCESS, %o5
	stxa		%o0, [%o2] ASI_DMMU
	stxa		%g1, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	add		%o3, (TLBTEMP_ENTSZ), %o3

	/* Spitfire Errata #32 workaround */
	mov		0x8, %g5
	stxa		%g0, [%g5] ASI_DMMU
	sethi		%hi(empty_zero_page), %g5
	flush		%g5

	ldxa		[%o3] ASI_DTLB_TAG_READ, %g5

	/* Spitfire Errata #32 workaround */
	mov		0x8, %g7
	stxa		%g0, [%g7] ASI_DMMU
	sethi		%hi(empty_zero_page), %g7
	flush		%g7

	ldxa		[%o3] ASI_DTLB_DATA_ACCESS, %g7
	stxa		%o1, [%o2] ASI_DMMU
	stxa		%g2, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync

	cmp		%g3, 0
	bne,pn		%xcc, copy_page_using_blkcommit
	 nop

	ldda		[%o1] ASI_BLK_P, %f0
	add		%o1, 0x40, %o1
	ldda		[%o1] ASI_BLK_P, %f16
	add		%o1, 0x40, %o1
	sethi		%hi(8192), %o2
1:	TOUCH(f0, f2, f4, f6, f8, f10, f12, f14)
	ldda		[%o1] ASI_BLK_P, %f32
	stda		%f48, [%o0] ASI_BLK_P
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f16, f18, f20, f22, f24, f26, f28, f30)
	ldda		[%o1] ASI_BLK_P, %f0
	stda		%f48, [%o0] ASI_BLK_P
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f32, f34, f36, f38, f40, f42, f44, f46)
	ldda		[%o1] ASI_BLK_P, %f16
	stda		%f48, [%o0] ASI_BLK_P
	sub		%o2, 0x40, %o2
	add		%o1, 0x40, %o1
	cmp		%o2, 0x80
	bne,pt		%xcc, 1b
	 add		%o0, 0x40, %o0
	membar		#Sync
	stda		%f0, [%o0] ASI_BLK_P
	add		%o0, 0x40, %o0
	stda		%f16, [%o0] ASI_BLK_P
copy_user_page_continue:
	membar		#Sync
	VISExit

	mov		TLB_TAG_ACCESS, %o2
	stxa		%g5, [%o2] ASI_DMMU
	stxa		%g7, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	sub		%o3, (TLBTEMP_ENTSZ), %o3
	stxa		%o4, [%o2] ASI_DMMU
	stxa		%o5, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	rdpr		%pstate, %g3
	jmpl		%o7 + 0x8, %g0
	 wrpr		%g3, PSTATE_IE, %pstate

copy_page_using_blkcommit:
	membar		#LoadStore | #StoreStore | #StoreLoad
	ldda		[%o1] ASI_BLK_P, %f0
	add		%o1, 0x40, %o1
	ldda		[%o1] ASI_BLK_P, %f16
	add		%o1, 0x40, %o1
	sethi		%hi(8192), %o2
1:	TOUCH(f0, f2, f4, f6, f8, f10, f12, f14)
	ldda		[%o1] ASI_BLK_P, %f32
	stda		%f48, [%o0] ASI_BLK_COMMIT_P
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f16, f18, f20, f22, f24, f26, f28, f30)
	ldda		[%o1] ASI_BLK_P, %f0
	stda		%f48, [%o0] ASI_BLK_COMMIT_P
	add		%o1, 0x40, %o1
	sub		%o2, 0x40, %o2
	add		%o0, 0x40, %o0
	TOUCH(f32, f34, f36, f38, f40, f42, f44, f46)
	ldda		[%o1] ASI_BLK_P, %f16
	stda		%f48, [%o0] ASI_BLK_COMMIT_P
	sub		%o2, 0x40, %o2
	add		%o1, 0x40, %o1
	cmp		%o2, 0x80
	bne,pt		%xcc, 1b
	 add		%o0, 0x40, %o0
	membar		#Sync
	stda		%f0, [%o0] ASI_BLK_COMMIT_P
	add		%o0, 0x40, %o0
	ba,pt		%xcc, copy_user_page_continue
	 stda		%f16, [%o0] ASI_BLK_COMMIT_P

	.align		32
	.globl		_clear_page
	.type		_clear_page,@function
_clear_page:	/* %o0=dest */
	VISEntryHalf
	ba,pt		%xcc, clear_page_common
	 clr		%o4

	.align		32
	.globl		clear_user_page
	.type		clear_user_page,@function
clear_user_page:	/* %o0=dest, %o1=vaddr */
	VISEntryHalf
	sethi		%hi(PAGE_SIZE), %g3
	sub		%o0, %g4, %g1
	and		%o1, %g3, %o0
	mov		TLB_TAG_ACCESS, %o2
	sethi		%uhi(_PAGE_VALID), %g3
	sethi		%hi(TLBTEMP_BASE), %o3
	sllx		%g3, 32, %g3
	or		%g3, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W), %g3
	or		%g1, %g3, %g1
	add		%o0, %o3, %o0
	mov		TLBTEMP_ENT2, %o3
	rdpr		%pstate, %g3
	wrpr		%g3, PSTATE_IE, %pstate

	/* Spitfire Errata #32 workaround */
	mov		0x8, %g5
	stxa		%g0, [%g5] ASI_DMMU
	sethi		%hi(empty_zero_page), %g5
	flush		%g5

	ldxa		[%o3] ASI_DTLB_TAG_READ, %g5

	/* Spitfire Errata #32 workaround */
	mov		0x8, %g7
	stxa		%g0, [%g7] ASI_DMMU
	sethi		%hi(empty_zero_page), %g7
	flush		%g7

	ldxa		[%o3] ASI_DTLB_DATA_ACCESS, %g7
	stxa		%o0, [%o2] ASI_DMMU
	stxa		%g1, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync

	mov		1, %o4

clear_page_common:
	membar		#StoreLoad | #StoreStore | #LoadStore	! LSU	Group
	fzero		%f0				! FPA	Group
	mov		32, %o1				! IEU0
	fzero		%f2				! FPA	Group
	faddd		%f0, %f2, %f4			! FPA	Group
	fmuld		%f0, %f2, %f6			! FPM
	faddd		%f0, %f2, %f8			! FPA	Group
	fmuld		%f0, %f2, %f10			! FPM

	faddd		%f0, %f2, %f12			! FPA	Group
	fmuld		%f0, %f2, %f14			! FPM
1:	stda		%f0, [%o0 + %g0] ASI_BLK_P	! Store	Group
	add		%o0, 0x40, %o0			! IEU0
	stda		%f0, [%o0 + %g0] ASI_BLK_P	! Store	Group
	add		%o0, 0x40, %o0			! IEU0
	stda		%f0, [%o0 + %g0] ASI_BLK_P	! Store	Group

	add		%o0, 0x40, %o0			! IEU0	Group
	stda		%f0, [%o0 + %g0] ASI_BLK_P	! Store	Group
	subcc		%o1, 1, %o1			! IEU1
	bne,pt		%icc, 1b			! CTI
	 add		%o0, 0x40, %o0			! IEU0	Group
	membar		#Sync				! LSU	Group
	VISExitHalf

	brnz,pt		%o4, 1f
	 nop

	retl
	 nop

1:	stxa		%g5, [%o2] ASI_DMMU
	stxa		%g7, [%o3] ASI_DTLB_DATA_ACCESS
	membar		#Sync
	jmpl		%o7 + 0x8, %g0
	 wrpr		%g3, 0x0, %pstate
