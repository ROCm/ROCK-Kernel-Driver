/* $Id: trampoline.S,v 1.12 1999/12/15 15:45:12 davem Exp $
 * trampoline.S: Jump start slave processors on sparc64.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/lsu.h>
#include <asm/pstate.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/spitfire.h>
#include <asm/asm_offsets.h>

	.data
	.align	8
call_method:
	.asciz	"call-method"
	.align	8
itlb_load:
	.asciz	"SUNW,itlb-load"
	.align	8
dtlb_load:
	.asciz	"SUNW,dtlb-load"

	.text
	.align		8
	.globl		sparc64_cpu_startup, sparc64_cpu_startup_end
sparc64_cpu_startup:
	flushw

	mov	(LSU_CONTROL_IC | LSU_CONTROL_DC | LSU_CONTROL_IM | LSU_CONTROL_DM), %g1
	stxa	%g1, [%g0] ASI_LSU_CONTROL
	membar	#Sync

	wrpr	%g0, 15, %pil
	wr	%g0, 0, %tick_cmpr

	/* Call OBP by hand to lock KERNBASE into i/d tlbs. */
	mov	%o0, %l0

	sethi	%hi(prom_entry_lock), %g2
1:	ldstub	[%g2 + %lo(prom_entry_lock)], %g1
	brnz,pn	%g1, 1b
	 membar	#StoreLoad | #StoreStore

	sethi	%hi(p1275buf), %g2
	or	%g2, %lo(p1275buf), %g2
	ldx	[%g2 + 0x10], %l2
	mov	%sp, %l1
	add	%l2, -(192 + 128), %sp
	flushw

	sethi	%hi(call_method), %g2
	or	%g2, %lo(call_method), %g2
	stx	%g2, [%sp + 2047 + 128 + 0x00]
	mov	5, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x08]
	mov	1, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x10]
	sethi	%hi(itlb_load), %g2
	or	%g2, %lo(itlb_load), %g2
	stx	%g2, [%sp + 2047 + 128 + 0x18]
	sethi	%hi(mmu_ihandle_cache), %g2
	lduw	[%g2 + %lo(mmu_ihandle_cache)], %g2
	stx	%g2, [%sp + 2047 + 128 + 0x20]
	sethi	%hi(KERNBASE), %g2
	stx	%g2, [%sp + 2047 + 128 + 0x28]
	sethi	%hi(kern_locked_tte_data), %g2
	ldx	[%g2 + %lo(kern_locked_tte_data)], %g2
	stx	%g2, [%sp + 2047 + 128 + 0x30]
	mov	63, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x38]
	sethi	%hi(p1275buf), %g2
	or	%g2, %lo(p1275buf), %g2
	ldx	[%g2 + 0x08], %o1
	call	%o1
	 add	%sp, (2047 + 128), %o0

	sethi	%hi(call_method), %g2
	or	%g2, %lo(call_method), %g2
	stx	%g2, [%sp + 2047 + 128 + 0x00]
	mov	5, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x08]
	mov	1, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x10]
	sethi	%hi(dtlb_load), %g2
	or	%g2, %lo(dtlb_load), %g2
	stx	%g2, [%sp + 2047 + 128 + 0x18]
	sethi	%hi(mmu_ihandle_cache), %g2
	lduw	[%g2 + %lo(mmu_ihandle_cache)], %g2
	stx	%g2, [%sp + 2047 + 128 + 0x20]
	sethi	%hi(KERNBASE), %g2
	stx	%g2, [%sp + 2047 + 128 + 0x28]
	sethi	%hi(kern_locked_tte_data), %g2
	ldx	[%g2 + %lo(kern_locked_tte_data)], %g2
	stx	%g2, [%sp + 2047 + 128 + 0x30]
	mov	63, %g2
	stx	%g2, [%sp + 2047 + 128 + 0x38]
	sethi	%hi(p1275buf), %g2
	or	%g2, %lo(p1275buf), %g2
	ldx	[%g2 + 0x08], %o1
	call	%o1
	 add	%sp, (2047 + 128), %o0

	sethi	%hi(prom_entry_lock), %g2
	stb	%g0, [%g2 + %lo(prom_entry_lock)]
	membar	#StoreStore | #StoreLoad

	mov	%l1, %sp
	flushw

	mov	%l0, %o0

	wrpr	%g0, (PSTATE_PRIV | PSTATE_PEF), %pstate
	wr	%g0, 0, %fprs

	sethi	%uhi(PAGE_OFFSET), %g4
	sllx	%g4, 32, %g4

	/* XXX Buggy PROM... */
	srl	%o0, 0, %o0
	ldx	[%o0], %g6

	wr	%g0, ASI_P, %asi

	mov	PRIMARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync
	mov	SECONDARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	mov	1, %g5
	sllx	%g5, (PAGE_SHIFT + 1), %g5
	sub	%g5, (REGWIN_SZ + STACK_BIAS), %g5
	add	%g6, %g5, %sp
	mov	0, %fp

	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0, %tl

	/* Setup the trap globals, then we can resurface. */
	rdpr	%pstate, %o1
	mov	%g6, %o2
	wrpr	%o1, PSTATE_AG, %pstate
	sethi	%hi(sparc64_ttable_tl0), %g5
	wrpr	%g5, %tba
	mov	%o2, %g6

	wrpr	%o1, PSTATE_MG, %pstate
#define KERN_HIGHBITS		((_PAGE_VALID | _PAGE_SZ4MB) ^ 0xfffff80000000000)
#define KERN_LOWBITS		(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)
#ifdef THIS_IS_CHEETAH
#error Dave, make sure you took care of other issues in rest of sparc64 code...
#define VPTE_BASE		0xffe0000000000000
#else /* Spitfire/Blackbird */
#define VPTE_BASE		0xfffffffe00000000
#endif
	mov	TSB_REG, %g1
	stxa	%g0, [%g1] ASI_DMMU
	membar	#Sync
	mov	TLB_SFSR, %g1
	sethi	%uhi(KERN_HIGHBITS), %g2
	or	%g2, %ulo(KERN_HIGHBITS), %g2
	sllx	%g2, 32, %g2
	or	%g2, KERN_LOWBITS, %g2
	sethi	%uhi(VPTE_BASE), %g3
	or	%g3, %ulo(VPTE_BASE), %g3
	sllx	%g3, 32, %g3
	clr	%g7
#undef KERN_HIGHBITS
#undef KERN_LOWBITS
#undef VPTE_BASE

	/* Setup interrupt globals, we are always SMP. */
	wrpr	%o1, PSTATE_IG, %pstate

	/* Get our UPA MID. */
	lduw	[%o2 + AOFF_task_processor], %g1
	sethi	%hi(cpu_data), %g5
	or	%g5, %lo(cpu_data), %g5

	/* In theory this is: &(cpu_data[this_upamid].irq_worklists[0]) */
	sllx	%g1, 7, %g1
	add	%g5, %g1, %g1
	add	%g1, 64, %g6

	wrpr	%g0, 0, %wstate
	or	%o1, PSTATE_IE, %o1
	wrpr	%o1, 0, %pstate

	call	prom_set_trap_table
	 sethi	%hi(sparc64_ttable_tl0), %o0

	call	smp_callin
	 nop
	call	cpu_idle
	 mov	0, %o0
	call	cpu_panic
	 nop
1:	b,a,pt	%xcc, 1b

	.align		8
sparc64_cpu_startup_end:
