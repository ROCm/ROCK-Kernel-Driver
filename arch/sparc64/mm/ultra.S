/* $Id: ultra.S,v 1.48 2000/11/06 06:59:04 davem Exp $
 * ultra.S: Don't expand these all over the place...
 *
 * Copyright (C) 1997, 2000 David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <asm/asi.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/spitfire.h>

	/* This file is meant to be read efficiently by the CPU, not humans.
	 * Staraj sie tego nikomu nie pierdolnac...
	 */
	.text
	.align		32
	.globl		__flush_tlb_page, __flush_tlb_mm, __flush_tlb_range
__flush_tlb_page: /* %o0=(ctx & 0x3ff), %o1=page&PAGE_MASK, %o2=SECONDARY_CONTEXT */
/*IC1*/	ldxa		[%o2] ASI_DMMU, %g2
	cmp		%g2, %o0
	bne,pn		%icc, __flush_tlb_page_slow
	 or		%o1, 0x10, %g3
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	retl
	 flush		%g6
__flush_tlb_mm: /* %o0=(ctx & 0x3ff), %o1=SECONDARY_CONTEXT */
/*IC2*/	ldxa		[%o1] ASI_DMMU, %g2
	cmp		%g2, %o0
	bne,pn		%icc, __flush_tlb_mm_slow
	 mov		0x50, %g3
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	retl
	 flush		%g6
__flush_tlb_range: /* %o0=(ctx&0x3ff), %o1=start&PAGE_MASK, %o2=SECONDARY_CONTEXT,
		    * %o3=end&PAGE_MASK, %o4=PAGE_SIZE, %o5=(end - start)
		    */
#define TLB_MAGIC	207 /* Students, do you know how I calculated this?  -DaveM */
/*IC3*/	cmp		%o5, %o4
	bleu,pt		%xcc, __flush_tlb_page
	 srlx		%o5, 13, %g5
	cmp		%g5, TLB_MAGIC
	bgeu,pn		%icc, __flush_tlb_range_constant_time
	 or		%o1, 0x10, %g5
	ldxa		[%o2] ASI_DMMU, %g2
	cmp		%g2, %o0
__flush_tlb_range_page_by_page:
/*IC4*/	bne,pn		%icc, __flush_tlb_range_pbp_slow
	 sub		%o5, %o4, %o5
1:	stxa		%g0, [%g5 + %o5] ASI_DMMU_DEMAP
	stxa		%g0, [%g5 + %o5] ASI_IMMU_DEMAP
	brnz,pt		%o5, 1b
	 sub		%o5, %o4, %o5
	retl
	 flush		%g6
__flush_tlb_range_constant_time: /* %o0=ctx, %o1=start, %o3=end */
/*IC5*/	rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	mov		TLB_TAG_ACCESS, %g3
	mov		(62 << 3), %g2

	/* Spitfire Errata #32 workaround. */
	mov		0x8, %o4
	stxa		%g0, [%o4] ASI_DMMU
	flush		%g6

1:	ldxa		[%g2] ASI_ITLB_TAG_READ, %o4
	and		%o4, 0x3ff, %o5
	cmp		%o5, %o0
	bne,pt		%icc, 2f
/*IC6*/	 andn		%o4, 0x3ff, %o4
	cmp		%o4, %o1
	blu,pt		%xcc, 2f
	 cmp		%o4, %o3
	blu,pn		%xcc, 4f
2:	 ldxa		[%g2] ASI_DTLB_TAG_READ, %o4
	and		%o4, 0x3ff, %o5
	cmp		%o5, %o0
/*IC7*/	andn		%o4, 0x3ff, %o4
	bne,pt		%icc, 3f
	 cmp		%o4, %o1
	blu,pt		%xcc, 3f
	 cmp		%o4, %o3
	blu,pn		%xcc, 5f
	 nop
3:	brnz,pt		%g2, 1b
/*IC8*/	 sub		%g2, (1 << 3), %g2
	retl
	 wrpr		%g1, 0x0, %pstate
4:	stxa		%g0, [%g3] ASI_IMMU
	stxa		%g0, [%g2] ASI_ITLB_DATA_ACCESS
	flush		%g6

	/* Spitfire Errata #32 workaround. */
	mov		0x8, %o4
	stxa		%g0, [%o4] ASI_DMMU
	flush		%g6

	ba,pt		%xcc, 2b
	 nop

5:	stxa		%g0, [%g3] ASI_DMMU
/*IC9*/	stxa		%g0, [%g2] ASI_DTLB_DATA_ACCESS
	flush		%g6

	/* Spitfire Errata #32 workaround. */
	mov		0x8, %o4
	stxa		%g0, [%o4] ASI_DMMU
	flush		%g6

	ba,pt		%xcc, 3b
	 nop

	.align		32
__flush_tlb_mm_slow:
/*IC10*/rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%o1] ASI_DMMU
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	flush		%g6
	stxa		%g2, [%o1] ASI_DMMU
	flush		%g6
/*IC11*/retl
	 wrpr		%g1, 0, %pstate

	.align		32
__flush_tlb_page_slow:
/*IC12*/rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%o2] ASI_DMMU
	stxa		%g0, [%g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g3] ASI_IMMU_DEMAP
	flush		%g6
	stxa		%g2, [%o2] ASI_DMMU
	flush		%g6
/*IC13*/retl
	 wrpr		%g1, 0, %pstate

	.align		32
__flush_tlb_range_pbp_slow:
/*IC13*/rdpr		%pstate, %g1
	wrpr		%g1, PSTATE_IE, %pstate
	stxa		%o0, [%o2] ASI_DMMU
2:	stxa		%g0, [%g5 + %o5] ASI_DMMU_DEMAP
	stxa		%g0, [%g5 + %o5] ASI_IMMU_DEMAP
	brnz,pt		%o5, 2b
	 sub		%o5, %o4, %o5
	flush		%g6
/*IC14*/stxa		%g2, [%o2] ASI_DMMU
	flush		%g6
	retl
	 wrpr		%g1, 0x0, %pstate

	.align		32
	.globl		__flush_icache_page
__flush_icache_page:	/* %o0 = phys_page */
	sethi		%hi(1 << 13), %o2	! IC_set bit
	mov		1, %g1
	srlx		%o0, 5, %o0
	clr		%o1			! IC_addr
	sllx		%g1, 36, %g1
	ldda		[%o1] ASI_IC_TAG, %o4
	sub		%g1, 1, %g2
	or		%o0, %g1, %o0		! VALID+phys-addr comparitor

	sllx		%g2, 1, %g2
	andn		%g2, 0xfe, %g2		! IC_tag mask
	nop
	nop
	nop
	nop
	nop
	nop

1:	addx		%g0, %g0, %g0
	ldda		[%o1 + %o2] ASI_IC_TAG, %g4
	addx		%g0, %g0, %g0
	and		%o5, %g2, %g3
	cmp		%g3, %o0
	add		%o1, 0x20, %o1
	ldda		[%o1] ASI_IC_TAG, %o4
	be,pn		%xcc, iflush1

2:	 nop
	and		%g5, %g2, %g5
	cmp		%g5, %o0
	be,pn		%xcc, iflush2
3:	 cmp		%o1, %o2
	bne,pt		%xcc, 1b
	 addx		%g0, %g0, %g0
	nop

	sethi		%uhi(PAGE_OFFSET), %g4
	retl
	 sllx		%g4, 32, %g4

iflush1:sub		%o1, 0x20, %g3
	stxa		%g0, [%g3] ASI_IC_TAG
	flush		%g6
	ba,a,pt		%xcc, 2b
iflush2:sub		%o1, 0x20, %g3
	stxa		%g0, [%o1 + %o2] ASI_IC_TAG
	flush		%g6
	ba,a,pt		%xcc, 3b

	.align		64
	.globl		__flush_dcache_page
__flush_dcache_page:	/* %o0=kaddr, %o1=flush_icache */
	sub		%o0, %g4, %o0
	clr		%o4
	srlx		%o0, 11, %o0
	sethi		%hi(1 << 14), %o2
1:	ldxa		[%o4] ASI_DCACHE_TAG, %o3	! LSU	Group
	add		%o4, (1 << 5), %o4		! IEU0
	ldxa		[%o4] ASI_DCACHE_TAG, %g1	! LSU	Group
	add		%o4, (1 << 5), %o4		! IEU0
	ldxa		[%o4] ASI_DCACHE_TAG, %g2	! LSU	Group	o3 available
	add		%o4, (1 << 5), %o4		! IEU0
	andn		%o3, 0x3, %o3			! IEU1
	ldxa		[%o4] ASI_DCACHE_TAG, %g3	! LSU	Group
	add		%o4, (1 << 5), %o4		! IEU0
	andn		%g1, 0x3, %g1			! IEU1
	cmp		%o0, %o3			! IEU1	Group
	be,a,pn		%xcc, dflush1			! CTI
	 sub		%o4, (4 << 5), %o4		! IEU0	(Group)
	cmp		%o0, %g1			! IEU1	Group
	andn		%g2, 0x3, %g2			! IEU0
	be,a,pn		%xcc, dflush2			! CTI
	 sub		%o4, (3 << 5), %o4		! IEU0	(Group)
	cmp		%o0, %g2			! IEU1	Group
	andn		%g3, 0x3, %g3			! IEU0
	be,a,pn		%xcc, dflush3			! CTI
	 sub		%o4, (2 << 5), %o4		! IEU0	(Group)
	cmp		%o0, %g3			! IEU1	Group
	be,a,pn		%xcc, dflush4			! CTI
	 sub		%o4, (1 << 5), %o4		! IEU0
2:	cmp		%o4, %o2			! IEU1	Group
	bne,pt		%xcc, 1b			! CTI
	 nop						! IEU0

	/* The I-cache does not snoop local stores so we
	 * better flush that too when necessary.
	 */
	brnz,pt		%o1, __flush_icache_page
	 sllx		%o0, 11, %o0
	retl
	 nop

dflush1:stxa		%g0, [%o4] ASI_DCACHE_TAG
	add		%o4, (1 << 5), %o4
dflush2:stxa		%g0, [%o4] ASI_DCACHE_TAG
	add		%o4, (1 << 5), %o4
dflush3:stxa		%g0, [%o4] ASI_DCACHE_TAG
	add		%o4, (1 << 5), %o4
dflush4:stxa		%g0, [%o4] ASI_DCACHE_TAG
	add		%o4, (1 << 5), %o4
	membar		#Sync
	ba,pt		%xcc, 2b
	 nop

	.align		32
__prefill_dtlb:
	rdpr		%pstate, %g7
	wrpr		%g7, PSTATE_IE, %pstate
	mov		TLB_TAG_ACCESS, %g1
	stxa		%o0, [%g1] ASI_DMMU
	stxa		%o1, [%g0] ASI_DTLB_DATA_IN
	flush		%g6
	retl
	 wrpr		%g7, %pstate
__prefill_itlb:
	rdpr		%pstate, %g7
	wrpr		%g7, PSTATE_IE, %pstate
	mov		TLB_TAG_ACCESS, %g1
	stxa		%o0, [%g1] ASI_IMMU
	stxa		%o1, [%g0] ASI_ITLB_DATA_IN
	flush		%g6
	retl
	 wrpr		%g7, %pstate

	.globl		__update_mmu_cache
__update_mmu_cache:	/* %o0=vma, %o1=address, %o2=pte */
	ldub		[%g6 + AOFF_task_thread + AOFF_thread_fault_code], %o3
	srlx		%o1, 13, %o1
	ldx		[%o0 + 0x0], %o4		/* XXX vma->vm_mm */
	brz,pn		%o3, 1f
	 sllx		%o1, 13, %o0
	ldx		[%o4 + AOFF_mm_context], %o5
	andcc		%o3, FAULT_CODE_DTLB, %g0
	mov		%o2, %o1
	and		%o5, 0x3ff, %o5
	bne,pt		%xcc, __prefill_dtlb
	 or		%o0, %o5, %o0
	ba,a,pt		%xcc, __prefill_itlb
1:	retl
	 nop

#ifdef CONFIG_SMP
	/* These are all called by the slaves of a cross call, at
	 * trap level 1, with interrupts fully disabled.
	 *
	 * Register usage:
	 *   %g5	mm->context	(all tlb flushes)
	 *   %g1	address arg 1	(tlb page and range flushes)
	 *   %g7	address arg 2	(tlb range flush only)
	 *
	 *   %g6	ivector table, don't touch
	 *   %g2	scratch 1
	 *   %g3	scratch 2
	 *   %g4	scratch 3
	 *
	 * TODO: Make xcall TLB range flushes use the tricks above... -DaveM
	 */
	.align		32
	.globl		xcall_flush_tlb_page, xcall_flush_tlb_mm, xcall_flush_tlb_range
xcall_flush_tlb_page:
	mov		SECONDARY_CONTEXT, %g2
	or		%g1, 0x10, %g4
	ldxa		[%g2] ASI_DMMU, %g3
	stxa		%g5, [%g2] ASI_DMMU
	stxa		%g0, [%g4] ASI_DMMU_DEMAP
	stxa		%g0, [%g4] ASI_IMMU_DEMAP
	stxa		%g3, [%g2] ASI_DMMU
	retry

xcall_flush_tlb_mm:
	mov		SECONDARY_CONTEXT, %g2
	mov		0x50, %g4
	ldxa		[%g2] ASI_DMMU, %g3
	stxa		%g5, [%g2] ASI_DMMU
	stxa		%g0, [%g4] ASI_DMMU_DEMAP
	stxa		%g0, [%g4] ASI_IMMU_DEMAP
	stxa		%g3, [%g2] ASI_DMMU
	retry

xcall_flush_tlb_range:
	sethi		%hi(8192 - 1), %g2
	or		%g2, %lo(8192 - 1), %g2
	andn		%g1, %g2, %g1
	andn		%g7, %g2, %g7
	sub		%g7, %g1, %g3
	add		%g2, 1, %g2
	orcc		%g1, 0x10, %g1
	srlx		%g3, 13, %g4

	cmp		%g4, 96
	bgu,pn		%icc, xcall_flush_tlb_mm
	 mov		SECONDARY_CONTEXT, %g4
	ldxa		[%g4] ASI_DMMU, %g7
	sub		%g3, %g2, %g3
	stxa		%g5, [%g4] ASI_DMMU
	nop
	nop

1:	stxa		%g0, [%g1 + %g3] ASI_DMMU_DEMAP
	stxa		%g0, [%g1 + %g3] ASI_IMMU_DEMAP
	brnz,pt		%g3, 1b
	 sub		%g3, %g2, %g3
	stxa		%g7, [%g4] ASI_DMMU
	retry
	nop
	nop

	.globl		xcall_report_regs
xcall_report_regs:
	rdpr		%pstate, %g2
	wrpr		%g2, PSTATE_IG | PSTATE_AG, %pstate
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7
	call		__show_regs
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	b,pt		%xcc, rtrap
	 clr		%l6

	.globl		xcall_capture
xcall_capture:
	rdpr		%pstate, %g2
	wrpr		%g2, PSTATE_IG | PSTATE_AG, %pstate
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7
	call		smp_penguin_jailcell
	 nop
	b,pt		%xcc, rtrap
	 clr		%l6

	.globl		xcall_promstop
xcall_promstop:
	rdpr		%pstate, %g2
	wrpr		%g2, PSTATE_IG | PSTATE_AG, %pstate
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7
	flushw
	call		prom_stopself
	 nop
	/* We should not return, just spin if we do... */
1:	b,a,pt		%xcc, 1b
	nop

	.globl		xcall_receive_signal
xcall_receive_signal:
	rdpr		%pstate, %g2
	wrpr		%g2, PSTATE_IG | PSTATE_AG, %pstate
	rdpr		%tstate, %g1
	andcc		%g1, TSTATE_PRIV, %g0
	/* If we did not trap from user space, just ignore. */
	bne,pn		%xcc, 99f
	 sethi		%hi(109f), %g7
	b,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	b,pt		%xcc, rtrap
	 clr		%l6
99:	retry

	.data

errata32_hwbug:
	.xword	0

	.text

	/* These two are not performance critical... */
	.globl		xcall_flush_tlb_all
xcall_flush_tlb_all:

	/* Spitfire Errata #32 workaround. */
	sethi		%hi(errata32_hwbug), %g4
	stx		%g0, [%g4 + %lo(errata32_hwbug)]

	clr		%g2
	clr		%g3
1:	ldxa		[%g3] ASI_DTLB_DATA_ACCESS, %g4
	and		%g4, _PAGE_L, %g5
	brnz,pn		%g5, 2f
	 mov		TLB_TAG_ACCESS, %g7

	stxa		%g0, [%g7] ASI_DMMU
	membar		#Sync
	stxa		%g0, [%g3] ASI_DTLB_DATA_ACCESS
	membar		#Sync

	/* Spitfire Errata #32 workaround. */
	sethi		%hi(errata32_hwbug), %g4
	stx		%g0, [%g4 + %lo(errata32_hwbug)]

2:	ldxa		[%g3] ASI_ITLB_DATA_ACCESS, %g4
	and		%g4, _PAGE_L, %g5
	brnz,pn		%g5, 2f
	 mov		TLB_TAG_ACCESS, %g7

	stxa		%g0, [%g7] ASI_IMMU
	membar		#Sync
	stxa		%g0, [%g3] ASI_ITLB_DATA_ACCESS
	membar		#Sync

	/* Spitfire Errata #32 workaround. */
	sethi		%hi(errata32_hwbug), %g4
	stx		%g0, [%g4 + %lo(errata32_hwbug)]

2:	add		%g2, 1, %g2
	cmp		%g2, 63
	ble,pt		%icc, 1b
	 sll		%g2, 3, %g3
	flush		%g6
	retry

	.globl		xcall_flush_cache_all
xcall_flush_cache_all:
	sethi		%hi(16383), %g2
	or		%g2, %lo(16383), %g2
	clr		%g3
1:	stxa		%g0, [%g3] ASI_IC_TAG
	add		%g3, 32, %g3
	cmp		%g3, %g2
	bleu,pt		%xcc, 1b
	 nop
	flush		%g6
	retry

	.globl		xcall_call_function
xcall_call_function:
	mov		TLB_TAG_ACCESS, %g5		! wheee...
	stxa		%g1, [%g5] ASI_IMMU		! save call_data here for a bit
	membar		#Sync
	rdpr		%pstate, %g2
	wrpr		%g2, PSTATE_IG | PSTATE_AG, %pstate
	mov		TLB_TAG_ACCESS, %g2
	ldxa		[%g2] ASI_IMMU, %g5
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7
	call		smp_call_function_client
	 mov		%l5, %o0
	b,pt		%xcc, rtrap
	 clr		%l6

#endif /* CONFIG_SMP */
