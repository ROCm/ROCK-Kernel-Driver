/* $Id: entry.S,v 1.120 2000/09/08 13:58:12 jj Exp $
 * arch/sparc64/kernel/entry.S:  Sparc64 trap low-level entry points.
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost        (ecd@skynet.be)
 * Copyright (C) 1996 Miguel de Icaza      (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996,98,99 Jakub Jelinek  (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/errno.h>

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/visasm.h>

/* #define SYSCALL_TRACING */

#define curptr      g6

#define NR_SYSCALLS 256      /* Each OS is different... */

	.text
	.align		32

	.globl		sparc64_vpte_patchme1
	.globl		sparc64_vpte_patchme2
sparc64_vpte_nucleus:
sparc64_vpte_patchme1:
	sethi		%hi(0), %g5			! This has to be patched
sparc64_vpte_patchme2:
	or		%g5, %lo(0), %g5		! This is patched too
	ba,pt		%xcc, sparc64_kpte_continue	! Part of dtlb_backend
	 add		%g1, %g1, %g1			! Finish PMD offset adjustment

	/* This is trivial with the new code... */
	.globl		do_fpdis
do_fpdis:
	ldub		[%g6 + AOFF_task_thread + AOFF_thread_fpsaved], %g5	! Load	Group
	sethi		%hi(TSTATE_PEF), %g4					! IEU0
	wr		%g0, FPRS_FEF, %fprs					! LSU	Group+4bubbles
	andcc		%g5, FPRS_FEF, %g0					! IEU1	Group
	be,a,pt		%icc, 1f						! CTI
	 clr		%g7							! IEU0
	ldub		[%g6 + AOFF_task_thread + AOFF_thread_gsr], %g7		! Load	Group
1:	andcc		%g5, FPRS_DL, %g0					! IEU1
	bne,pn		%icc, 2f						! CTI
	 fzero		%f0							! FPA
	andcc		%g5, FPRS_DU, %g0					! IEU1  Group
	bne,pn		%icc, 1f						! CTI
	 fzero		%f2							! FPA
	faddd		%f0, %f2, %f4
	fmuld		%f0, %f2, %f6
	faddd		%f0, %f2, %f8
	fmuld		%f0, %f2, %f10
	faddd		%f0, %f2, %f12
	fmuld		%f0, %f2, %f14
	faddd		%f0, %f2, %f16
	fmuld		%f0, %f2, %f18
	faddd		%f0, %f2, %f20
	fmuld		%f0, %f2, %f22
	faddd		%f0, %f2, %f24
	fmuld		%f0, %f2, %f26
	faddd		%f0, %f2, %f28
	fmuld		%f0, %f2, %f30
	faddd		%f0, %f2, %f32
	fmuld		%f0, %f2, %f34
	faddd		%f0, %f2, %f36
	fmuld		%f0, %f2, %f38
	faddd		%f0, %f2, %f40
	fmuld		%f0, %f2, %f42
	faddd		%f0, %f2, %f44
	fmuld		%f0, %f2, %f46
	faddd		%f0, %f2, %f48
	fmuld		%f0, %f2, %f50
	faddd		%f0, %f2, %f52
	fmuld		%f0, %f2, %f54
	faddd		%f0, %f2, %f56
	fmuld		%f0, %f2, %f58
	b,pt		%xcc, fpdis_exit2
	 faddd		%f0, %f2, %f60
1:	mov		SECONDARY_CONTEXT, %g3
	add		%g6, AOFF_task_fpregs + 0x80, %g1
	faddd		%f0, %f2, %f4
	fmuld		%f0, %f2, %f6
	ldxa		[%g3] ASI_DMMU, %g5
	add		%g6, AOFF_task_fpregs + 0xc0, %g2
	stxa		%g0, [%g3] ASI_DMMU
	faddd		%f0, %f2, %f8
	fmuld		%f0, %f2, %f10
	flush		%g6
	membar		#StoreLoad | #LoadLoad
	ldda		[%g1] ASI_BLK_S, %f32	! grrr, where is ASI_BLK_NUCLEUS 8-(
	ldda		[%g2] ASI_BLK_S, %f48
	faddd		%f0, %f2, %f12
	fmuld		%f0, %f2, %f14
	faddd		%f0, %f2, %f16
	fmuld		%f0, %f2, %f18
	faddd		%f0, %f2, %f20
	fmuld		%f0, %f2, %f22
	faddd		%f0, %f2, %f24
	fmuld		%f0, %f2, %f26
	faddd		%f0, %f2, %f28
	fmuld		%f0, %f2, %f30
	b,pt		%xcc, fpdis_exit
	 membar		#Sync
2:	andcc		%g5, FPRS_DU, %g0
	bne,pt		%icc, 3f
	 fzero		%f32
	mov		SECONDARY_CONTEXT, %g3
	fzero		%f34
	ldxa		[%g3] ASI_DMMU, %g5
	add		%g6, AOFF_task_fpregs, %g1
	stxa		%g0, [%g3] ASI_DMMU
	add		%g6, AOFF_task_fpregs + 0x40, %g2
	faddd		%f32, %f34, %f36
	fmuld		%f32, %f34, %f38
	flush		%g6
	membar		#StoreLoad | #LoadLoad
	ldda		[%g1] ASI_BLK_S, %f0	! grrr, where is ASI_BLK_NUCLEUS 8-(
	ldda		[%g2] ASI_BLK_S, %f16
	faddd		%f32, %f34, %f40
	fmuld		%f32, %f34, %f42
	faddd		%f32, %f34, %f44
	fmuld		%f32, %f34, %f46
	faddd		%f32, %f34, %f48
	fmuld		%f32, %f34, %f50
	faddd		%f32, %f34, %f52
	fmuld		%f32, %f34, %f54
	faddd		%f32, %f34, %f56
	fmuld		%f32, %f34, %f58
	faddd		%f32, %f34, %f60
	fmuld		%f32, %f34, %f62
	b,pt		%xcc, fpdis_exit
	 membar		#Sync
3:	mov		SECONDARY_CONTEXT, %g3
	add		%g6, AOFF_task_fpregs, %g1
	ldxa		[%g3] ASI_DMMU, %g5
	mov		0x40, %g2
	stxa		%g0, [%g3] ASI_DMMU
	flush		%g6
	membar		#StoreLoad | #LoadLoad
	ldda		[%g1] ASI_BLK_S, %f0		! grrr, where is ASI_BLK_NUCLEUS 8-(
	ldda		[%g1 + %g2] ASI_BLK_S, %f16
	add		%g1, 0x80, %g1
	ldda		[%g1] ASI_BLK_S, %f32
	ldda		[%g1 + %g2] ASI_BLK_S, %f48
	membar		#Sync
fpdis_exit:
	stxa		%g5, [%g3] ASI_DMMU
	flush		%g6
fpdis_exit2:
	wr		%g7, 0, %gsr
	ldx		[%g6 + AOFF_task_thread + AOFF_thread_xfsr], %fsr
	rdpr		%tstate, %g3
	or		%g3, %g4, %g3		! anal...
	wrpr		%g3, %tstate
	wr		%g0, FPRS_FEF, %fprs	! clean DU/DL bits
	retry

	.globl		do_fptrap
	.align		32
do_fptrap:
	ldub		[%g6 + AOFF_task_thread + AOFF_thread_fpsaved], %g3
	stx		%fsr, [%g6 + AOFF_task_thread + AOFF_thread_xfsr]
	rd		%fprs, %g1
	or		%g3, %g1, %g3
	stb		%g3, [%g6 + AOFF_task_thread + AOFF_thread_fpsaved]
	rd		%gsr, %g3
	stb		%g3, [%g6 + AOFF_task_thread + AOFF_thread_gsr]
	mov		SECONDARY_CONTEXT, %g3
	add		%g6, AOFF_task_fpregs, %g2
	ldxa		[%g3] ASI_DMMU, %g5
	stxa		%g0, [%g3] ASI_DMMU
	flush		%g6
	membar		#StoreStore | #LoadStore
	andcc		%g1, FPRS_DL, %g0
	be,pn		%icc, 4f
	 mov		0x40, %g3
	stda		%f0, [%g2] ASI_BLK_S
	stda		%f16, [%g2 + %g3] ASI_BLK_S
	andcc		%g1, FPRS_DU, %g0
	be,pn		%icc, 5f
4:       add		%g2, 128, %g2
	stda		%f32, [%g2] ASI_BLK_S
	stda		%f48, [%g2 + %g3] ASI_BLK_S
5:	mov		SECONDARY_CONTEXT, %g1
	membar		#Sync
	stxa		%g5, [%g1] ASI_DMMU
	flush		%g6
	ba,pt		%xcc, etrap
	 wr		%g0, 0, %fprs

	/* The registers for cross calls will be:
	 *
	 * DATA 0: [low 32-bits]  Address of function to call, jmp to this
	 *         [high 32-bits] MMU Context Argument 0, place in %g5
	 * DATA 1: Address Argument 1, place in %g6
	 * DATA 2: Address Argument 2, place in %g7
	 *
	 * With this method we can do most of the cross-call tlb/cache
	 * flushing very quickly.
	 *
	 * Current CPU's IRQ worklist table is locked into %g1,
	 * don't touch.
	 */
	.text
	.align		32
	.globl		do_ivec
do_ivec:
	mov		0x40, %g3
	ldxa		[%g3 + %g0] ASI_UDB_INTR_R, %g3
	sethi		%hi(KERNBASE), %g4
	cmp		%g3, %g4
	bgeu,pn		%xcc, do_ivec_xcall
	 srlx		%g3, 32, %g5
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync

	sethi		%hi(ivector_table), %g2
	sllx		%g3, 5, %g3
	or		%g2, %lo(ivector_table), %g2
	add		%g2, %g3, %g3
	ldx		[%g3 + 0x08], %g2	/* irq_info */
	ldub		[%g3 + 0x04], %g4	/* pil */
	brz,pn		%g2, do_ivec_spurious
	 mov		1, %g2

	sllx		%g2, %g4, %g2
	sllx		%g4, 2, %g4
	lduw		[%g6 + %g4], %g5	/* g5 = irq_work(cpu, pil) */
	stw		%g5, [%g3 + 0x00]	/* bucket->irq_chain = g5 */
	stw		%g3, [%g6 + %g4]	/* irq_work(cpu, pil) = bucket */
	wr		%g2, 0x0, %set_softint
	retry
do_ivec_xcall:
	mov		0x50, %g1

	ldxa		[%g1 + %g0] ASI_UDB_INTR_R, %g1
	srl		%g3, 0, %g3
	mov		0x60, %g7
	ldxa		[%g7 + %g0] ASI_UDB_INTR_R, %g7
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	jmpl		%g3, %g0
	 nop

do_ivec_spurious:
	stw		%g3, [%g6 + 0x00]	/* irq_work(cpu, 0) = bucket */
	rdpr		%pstate, %g5

	wrpr		%g5, PSTATE_IG | PSTATE_AG, %pstate
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	call		catch_disabled_ivec
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		save_alternate_globals
save_alternate_globals: /* %o0 = save_area */
	rdpr		%pstate, %o5
	andn		%o5, PSTATE_IE, %o1
	wrpr		%o1, PSTATE_AG, %pstate
	stx		%g0, [%o0 + 0x00]
	stx		%g1, [%o0 + 0x08]
	stx		%g2, [%o0 + 0x10]
	stx		%g3, [%o0 + 0x18]
	stx		%g4, [%o0 + 0x20]
	stx		%g5, [%o0 + 0x28]
	stx		%g6, [%o0 + 0x30]
	stx		%g7, [%o0 + 0x38]
	wrpr		%o1, PSTATE_IG, %pstate
	stx		%g0, [%o0 + 0x40]
	stx		%g1, [%o0 + 0x48]
	stx		%g2, [%o0 + 0x50]
	stx		%g3, [%o0 + 0x58]
	stx		%g4, [%o0 + 0x60]
	stx		%g5, [%o0 + 0x68]
	stx		%g6, [%o0 + 0x70]
	stx		%g7, [%o0 + 0x78]
	wrpr		%o1, PSTATE_MG, %pstate
	stx		%g0, [%o0 + 0x80]
	stx		%g1, [%o0 + 0x88]
	stx		%g2, [%o0 + 0x90]
	stx		%g3, [%o0 + 0x98]
	stx		%g4, [%o0 + 0xa0]
	stx		%g5, [%o0 + 0xa8]
	stx		%g6, [%o0 + 0xb0]
	stx		%g7, [%o0 + 0xb8]
	wrpr		%o5, 0x0, %pstate
	retl
	 nop

	.globl		restore_alternate_globals
restore_alternate_globals: /* %o0 = save_area */
	rdpr		%pstate, %o5
	andn		%o5, PSTATE_IE, %o1
	wrpr		%o1, PSTATE_AG, %pstate
	ldx		[%o0 + 0x00], %g0
	ldx		[%o0 + 0x08], %g1
	ldx		[%o0 + 0x10], %g2
	ldx		[%o0 + 0x18], %g3
	ldx		[%o0 + 0x20], %g4
	ldx		[%o0 + 0x28], %g5
	ldx		[%o0 + 0x30], %g6
	ldx		[%o0 + 0x38], %g7
	wrpr		%o1, PSTATE_IG, %pstate
	ldx		[%o0 + 0x40], %g0
	ldx		[%o0 + 0x48], %g1
	ldx		[%o0 + 0x50], %g2
	ldx		[%o0 + 0x58], %g3
	ldx		[%o0 + 0x60], %g4
	ldx		[%o0 + 0x68], %g5
	ldx		[%o0 + 0x70], %g6
	ldx		[%o0 + 0x78], %g7
	wrpr		%o1, PSTATE_MG, %pstate
	ldx		[%o0 + 0x80], %g0
	ldx		[%o0 + 0x88], %g1
	ldx		[%o0 + 0x90], %g2
	ldx		[%o0 + 0x98], %g3
	ldx		[%o0 + 0xa0], %g4
	ldx		[%o0 + 0xa8], %g5
	ldx		[%o0 + 0xb0], %g6
	ldx		[%o0 + 0xb8], %g7
	wrpr		%o5, 0x0, %pstate
	retl
	 nop

	.globl		getcc, setcc
getcc:
	ldx		[%o0 + PT_V9_TSTATE], %o1
	srlx		%o1, 32, %o1
	and		%o1, 0xf, %o1
	retl
	 stx		%o1, [%o0 + PT_V9_G1]
setcc:
	ldx		[%o0 + PT_V9_TSTATE], %o1
	ldx		[%o0 + PT_V9_G1], %o2
	or		%g0, %ulo(TSTATE_ICC), %o3
	sllx		%o3, 32, %o3
	andn		%o1, %o3, %o1
	sllx		%o2, 32, %o2
	and		%o2, %o3, %o2
	or		%o1, %o2, %o1
	retl
	 stx		%o1, [%o0 + PT_V9_TSTATE]

	.globl		utrap, utrap_ill
utrap:	brz,pn		%g1, etrap
	 nop
	save		%sp, -128, %sp
	rdpr		%tstate, %l6
	rdpr		%cwp, %l7
	andn		%l6, TSTATE_CWP, %l6
	wrpr		%l6, %l7, %tstate
	rdpr		%tpc, %l6
	rdpr		%tnpc, %l7
	wrpr		%g1, 0, %tnpc
	done
utrap_ill:
        call		bad_trap
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

#ifdef CONFIG_BLK_DEV_FD
	.globl		floppy_hardint
floppy_hardint:
	wr		%g0, (1 << 11), %clear_softint
	sethi		%hi(doing_pdma), %g1
	ld		[%g1 + %lo(doing_pdma)], %g2
	brz,pn		%g2, floppy_dosoftint
	 sethi		%hi(fdc_status), %g3
	ldx		[%g3 + %lo(fdc_status)], %g3
	sethi		%hi(pdma_vaddr), %g5
	ldx		[%g5 + %lo(pdma_vaddr)], %g4
	sethi		%hi(pdma_size), %g5
	ldx		[%g5 + %lo(pdma_size)], %g5

next_byte:
	lduba		[%g3] ASI_PHYS_BYPASS_EC_E, %g7
	andcc		%g7, 0x80, %g0
	be,pn		%icc, floppy_fifo_emptied
	 andcc		%g7, 0x20, %g0
	be,pn		%icc, floppy_overrun
	 andcc		%g7, 0x40, %g0
	be,pn		%icc, floppy_write
	 sub		%g5, 1, %g5

	inc		%g3
	lduba		[%g3] ASI_PHYS_BYPASS_EC_E, %g7
	dec		%g3
	orcc		%g0, %g5, %g0
	stb		%g7, [%g4]
	bne,pn		%xcc, next_byte
	 add		%g4, 1, %g4

	b,pt		%xcc, floppy_tdone
	 nop

floppy_write:
	ldub		[%g4], %g7
	orcc		%g0, %g5, %g0
	inc		%g3
	stba		%g7, [%g3] ASI_PHYS_BYPASS_EC_E
	dec		%g3
	bne,pn		%xcc, next_byte
	 add		%g4, 1, %g4

floppy_tdone:
	sethi		%hi(pdma_vaddr), %g1
	stx		%g4, [%g1 + %lo(pdma_vaddr)]
	sethi		%hi(pdma_size), %g1
	stx		%g5, [%g1 + %lo(pdma_size)]
	sethi		%hi(auxio_register), %g1
	ldx		[%g1 + %lo(auxio_register)], %g7
	ldub		[%g7], %g5
	or		%g5, 0xc2, %g5
	stb		%g5, [%g7]
	andn		%g5, 0x02, %g5

	nop; nop;  nop; nop;  nop; nop;
	nop; nop;  nop; nop;  nop; nop;

	stb		%g5, [%g7]
	sethi		%hi(doing_pdma), %g1
	b,pt		%xcc, floppy_dosoftint
	 st		%g0, [%g1 + %lo(doing_pdma)]

floppy_fifo_emptied:
	sethi		%hi(pdma_vaddr), %g1
	stx		%g4, [%g1 + %lo(pdma_vaddr)]
	sethi		%hi(pdma_size), %g1
	stx		%g5, [%g1 + %lo(pdma_size)]
	sethi		%hi(irq_action), %g1
	or		%g1, %lo(irq_action), %g1
	ldx		[%g1 + (11 << 3)], %g3		! irqaction[floppy_irq]
	ldx		[%g3 + 0x10], %g4		! action->mask == ino_bucket ptr
	ldx		[%g4 + 0x10], %g4		! bucket->iclr
	stwa		%g0, [%g4] ASI_PHYS_BYPASS_EC_E	! ICLR_IDLE
	membar		#Sync				! probably not needed...
	retry

floppy_overrun:
	sethi		%hi(pdma_vaddr), %g1
	stx		%g4, [%g1 + %lo(pdma_vaddr)]
	sethi		%hi(pdma_size), %g1
	stx		%g5, [%g1 + %lo(pdma_size)]
	sethi		%hi(doing_pdma), %g1
	st		%g0, [%g1 + %lo(doing_pdma)]

floppy_dosoftint:
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	sethi		%hi(109f), %g7
	b,pt		%xcc, etrap_irq
109:	 or		%g7, %lo(109b), %g7

	mov		11, %o0
	mov		0, %o1
	call		sparc_floppy_irq
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o2

	b,pt		%xcc, rtrap
	 clr		%l6

#endif /* CONFIG_BLK_DEV_FD */

	/* XXX Here is stuff we still need to write... -DaveM XXX */
	.globl		netbsd_syscall
netbsd_syscall:
	retl
	 nop

	/* These next few routines must be sure to clear the
	 * SFSR FaultValid bit so that the fast tlb data protection
	 * handler does not flush the wrong context and lock up the
	 * box.
	 */
	.globl		__do_data_access_exception
	.globl		__do_data_access_exception_tl1
__do_data_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	rdpr		%tl, %g3
	cmp		%g3, 1
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	bgu,pn		%icc, winfix_dax
	 rdpr		%tpc, %g3
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
	 or		%g7, %lo(109f), %g7	! Merge in below
__do_data_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		data_access_exception
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		__do_instruction_access_exception
	.globl		__do_instruction_access_exception_tl1
__do_instruction_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
	 or		%g7, %lo(109f), %g7	! Merge in below
__do_instruction_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		instruction_access_exception
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	/* This is the trap handler entry point for ECC correctable
	 * errors.  They are corrected, but we listen for the trap
	 * so that the event can be logged.
	 *
	 * Disrupting errors are either:
	 * 1) single-bit ECC errors during UDB reads to system
	 *    memory
	 * 2) data parity errors during write-back events
	 *
	 * As far as I can make out from the manual, the CEE trap
	 * is only for correctable errors during memory read
	 * accesses by the front-end of the processor.
	 *
	 * The code below is only for trap level 1 CEE events,
	 * as it is the only situation where we can safely record
	 * and log.  For trap level >1 we just clear the CE bit
	 * in the AFSR and return.
	 */

	/* Our trap handling infrastructure allows us to preserve
	 * two 64-bit values during etrap for arguments to
	 * subsequent C code.  Therefore we encode the information
	 * as follows:
	 *
	 * value 1) Full 64-bits of AFAR
	 * value 2) Low 33-bits of AFSR, then bits 33-->42
	 *          are UDBL error status and bits 43-->52
	 *          are UDBH error status
	 */
	.align	64
	.globl	cee_trap
cee_trap:
	ldxa	[%g0] ASI_AFSR, %g1		! Read AFSR
	ldxa	[%g0] ASI_AFAR, %g2		! Read AFAR
	sllx	%g1, 31, %g1			! Clear reserved bits
	srlx	%g1, 31, %g1			! in AFSR

	/* NOTE: UltraSparc-I/II have high and low UDB error
	 *       registers, corresponding to the two UDB units
	 *       present on those chips.  UltraSparc-IIi only
	 *       has a single UDB, called "SDB" in the manual.
	 *       For IIi the upper UDB register always reads
	 *       as zero so for our purposes things will just
	 *       work with the checks below.
	 */
	ldxa	[%g0] ASI_UDBL_ERROR_R, %g3	! Read UDB-Low error status
	andcc	%g3, (1 << 8), %g4		! Check CE bit
	sllx	%g3, (64 - 10), %g3		! Clear reserved bits
	srlx	%g3, (64 - 10), %g3		! in UDB-Low error status

	sllx	%g3, (33 + 0), %g3		! Shift up to encoding area
	or	%g1, %g3, %g1			! Or it in
	be,pn	%xcc, 1f			! Branch if CE bit was clear
	 nop
	stxa	%g4, [%g0] ASI_UDB_ERROR_W	! Clear CE sticky bit in UDBL
	membar	#Sync				! Synchronize ASI stores
1:	mov	0x18, %g5			! Addr of UDB-High error status
	ldxa	[%g5] ASI_UDBH_ERROR_R, %g3	! Read it

	andcc	%g3, (1 << 8), %g4		! Check CE bit
	sllx	%g3, (64 - 10), %g3		! Clear reserved bits
	srlx	%g3, (64 - 10), %g3		! in UDB-High error status
	sllx	%g3, (33 + 10), %g3		! Shift up to encoding area
	or	%g1, %g3, %g1			! Or it in
	be,pn	%xcc, 1f			! Branch if CE bit was clear
	 nop
	nop

	stxa	%g4, [%g5] ASI_UDB_ERROR_W	! Clear CE sticky bit in UDBH
	membar	#Sync				! Synchronize ASI stores
1:	mov	1, %g5				! AFSR CE bit is
	sllx	%g5, 20, %g5			! bit 20
	stxa	%g5, [%g0] ASI_AFSR		! Clear CE sticky bit in AFSR
	membar	#Sync				! Synchronize ASI stores
	sllx	%g2, (64 - 41), %g2		! Clear reserved bits
	srlx	%g2, (64 - 41), %g2		! in latched AFAR

	andn	%g2, 0x0f, %g2			! Finish resv bit clearing
	mov	%g1, %g4			! Move AFSR+UDB* into save reg
	mov	%g2, %g5			! Move AFAR into save reg
	rdpr	%pil, %g2
	wrpr	%g0, 15, %pil
	ba,pt	%xcc, etrap_irq
	 rd	%pc, %g7
	mov	%l4, %o0

	mov	%l5, %o1
	call	cee_log
	 add	%sp, STACK_BIAS + REGWIN_SZ, %o2
	ba,a,pt	%xcc, rtrap_clr_l6

	.globl		__do_privact
__do_privact:
	mov		TLB_SFSR, %g3
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	or		%g7, %lo(109b), %g7
	call		do_privact
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_mna
do_mna:
	rdpr		%tl, %g3
	cmp		%g3, 1

	/* Setup %g4/%g5 now as they are used in the
	 * winfixup code.
	 */
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ldxa		[%g3] ASI_DMMU, %g5
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	bgu,pn		%icc, winfix_dax
	 rdpr		%tpc, %g3

1:	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		mem_address_unaligned
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_lddfmna
do_lddfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_lddfmna
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_stdfmna
do_stdfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_stdfmna
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl	breakpoint_trap
breakpoint_trap:
	call		sparc_breakpoint
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
	ba,pt		%xcc, rtrap
	 nop

#if defined(CONFIG_SUNOS_EMUL) || defined(CONFIG_SOLARIS_EMUL) || \
    defined(CONFIG_SOLARIS_EMUL_MODULE)
	/* SunOS uses syscall zero as the 'indirect syscall' it looks
	 * like indir_syscall(scall_num, arg0, arg1, arg2...);  etc.
	 * This is complete brain damage.
	 */
	.globl	sunos_indir
sunos_indir:
	srl		%o0, 0, %o0
	mov		%o7, %l4
	cmp		%o0, NR_SYSCALLS
	blu,a,pt	%icc, 1f
	 sll		%o0, 0x2, %o0
	sethi		%hi(sunos_nosys), %l6
	b,pt		%xcc, 2f
	 or		%l6, %lo(sunos_nosys), %l6
1:	sethi		%hi(sunos_sys_table), %l7
	or		%l7, %lo(sunos_sys_table), %l7
	lduw		[%l7 + %o0], %l6
2:	mov		%o1, %o0
	mov		%o2, %o1
	mov		%o3, %o2
	mov		%o4, %o3
	mov		%o5, %o4
	call		%l6
	 mov		%l4, %o7

	.globl	sunos_getpid
sunos_getpid:
	call	sys_getppid
	 nop
	call	sys_getpid
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]

	/* SunOS getuid() returns uid in %o0 and euid in %o1 */
	.globl	sunos_getuid
sunos_getuid:
	call	sys32_geteuid16
	 nop
	call	sys32_getuid16
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]

	/* SunOS getgid() returns gid in %o0 and egid in %o1 */
	.globl	sunos_getgid
sunos_getgid:
	call	sys32_getegid16
	 nop
	call	sys32_getgid16
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]
#endif

	/* SunOS's execv() call only specifies the argv argument, the
	 * environment settings are the same as the calling processes.
	 */
	.globl	sunos_execv, sys_execve, sys32_execve
sys_execve:
	sethi		%hi(sparc_execve), %g1
	ba,pt		%xcc, execve_merge
	 or		%g1, %lo(sparc_execve), %g1
sunos_execv:
	stx		%g0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I2]
sys32_execve:
	sethi		%hi(sparc32_execve), %g1
	or		%g1, %lo(sparc32_execve), %g1
execve_merge:
	flushw
	jmpl		%g1, %g0
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o0

	.globl	sys_pipe, sys_sigpause, sys_nis_syscall
	.globl	sys_sigsuspend, sys_rt_sigsuspend, sys32_rt_sigsuspend
	.globl	sys_rt_sigreturn
	.globl	sys32_sigreturn, sys32_rt_sigreturn
	.globl	sys32_execve, sys_ptrace
	.globl	sys_sigaltstack, sys32_sigaltstack
	.globl	sys32_sigstack
	.align	32
sys_pipe:	ba,pt		%xcc, sparc_pipe
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
sys_nis_syscall:ba,pt		%xcc, c_sys_nis_syscall
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o0
sys_memory_ordering:
		ba,pt		%xcc, sparc_memory_ordering
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o1
sys_sigaltstack:ba,pt		%xcc, do_sigaltstack
		 add		%i6, STACK_BIAS, %o2
sys32_sigstack:	ba,pt		%xcc, do_sys32_sigstack
		 mov		%i6, %o2
sys32_sigaltstack:
		ba,pt		%xcc, do_sys32_sigaltstack
		 mov		%i6, %o2

		.align		32
sys_sigsuspend:	add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_sigsuspend
		 add		%o7, 1f-.-4, %o7
		nop
sys_rt_sigsuspend: /* NOTE: %o0,%o1 have a correct value already */
		add		%sp, STACK_BIAS + REGWIN_SZ, %o2
		call		do_rt_sigsuspend
		 add		%o7, 1f-.-4, %o7
		nop
sys32_rt_sigsuspend: /* NOTE: %o0,%o1 have a correct value already */
		srl		%o0, 0, %o0
		add		%sp, STACK_BIAS + REGWIN_SZ, %o2
		call		do_rt_sigsuspend32
		 add		%o7, 1f-.-4, %o7
		/* NOTE: %o0 has a correct value already */
sys_sigpause:	add		%sp, STACK_BIAS + REGWIN_SZ, %o1
		call		do_sigpause
		 add		%o7, 1f-.-4, %o7
		nop
sys32_sigreturn:
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_sigreturn32
		 add		%o7, 1f-.-4, %o7
		nop
sys_rt_sigreturn:
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_rt_sigreturn
		 add		%o7, 1f-.-4, %o7
		nop
sys32_rt_sigreturn:
		add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_rt_sigreturn32
		 add		%o7, 1f-.-4, %o7
		nop
sys_ptrace:	add		%sp, STACK_BIAS + REGWIN_SZ, %o0
		call		do_ptrace
		 add		%o7, 1f-.-4, %o7
		nop
		.align		32
1:		ldx		[%curptr + AOFF_task_ptrace], %l5
		andcc		%l5, 0x02, %g0
		be,pt		%icc, rtrap
		 clr		%l6
		call		syscall_trace
		 nop

		ba,pt		%xcc, rtrap
		 clr		%l6

	/* This is how fork() was meant to be done, 8 instruction entry.
	 *
	 * I questioned the following code briefly, let me clear things
	 * up so you must not reason on it like I did.
	 *
	 * Know the fork_kpsr etc. we use in the sparc32 port?  We don't
	 * need it here because the only piece of window state we copy to
	 * the child is the CWP register.  Even if the parent sleeps,
	 * we are safe because we stuck it into pt_regs of the parent
	 * so it will not change.
	 *
	 * XXX This raises the question, whether we can do the same on
	 * XXX sparc32 to get rid of fork_kpsr _and_ fork_kwim.  The
	 * XXX answer is yes.  We stick fork_kpsr in UREG_G0 and
	 * XXX fork_kwim in UREG_G1 (global registers are considered
	 * XXX volatile across a system call in the sparc ABI I think
	 * XXX if it isn't we can use regs->y instead, anyone who depends
	 * XXX upon the Y register being preserved across a fork deserves
	 * XXX to lose).
	 *
	 * In fact we should take advantage of that fact for other things
	 * during system calls...
	 */
	.globl	sys_fork, sys_vfork, sys_clone, sparc_exit
	.globl	ret_from_syscall
	.align	32
sys_vfork:	/* Under Linux, vfork and fork are just special cases of clone. */
		sethi		%hi(0x4000 | 0x0100 | SIGCHLD), %o0
		or		%o0, %lo(0x4000 | 0x0100 | SIGCHLD), %o0
		ba,pt		%xcc, sys_clone
sys_fork:	 clr		%o1
		mov		SIGCHLD, %o0
sys_clone:	flushw
		movrz		%o1, %fp, %o1
		mov		0, %o3
		ba,pt		%xcc, do_fork
		 add		%sp, STACK_BIAS + REGWIN_SZ, %o2
ret_from_syscall:
		/* Clear SPARC_FLAG_NEWCHILD, switch_to leaves thread.flags in
		 * %o7 for us.  Check performance counter stuff too.
		 */
		andn		%o7, SPARC_FLAG_NEWCHILD, %l0
		mov		%g5, %o0	/* 'prev' */
		call		schedule_tail
		 stb		%l0, [%g6 + AOFF_task_thread + AOFF_thread_flags]
		andcc		%l0, SPARC_FLAG_PERFCTR, %g0
		be,pt		%icc, 1f
		 nop
		ldx		[%g6 + AOFF_task_thread + AOFF_thread_pcr_reg], %o7
		wr		%g0, %o7, %pcr

		/* Blackbird errata workaround.  See commentary in
		 * smp.c:smp_percpu_timer_interrupt() for more
		 * information.
		 */
		ba,pt		%xcc, 99f
		 nop
		.align		64
99:		wr		%g0, %g0, %pic
		rd		%pic, %g0

1:		b,pt		%xcc, ret_sys_call
		 ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0], %o0
sparc_exit:	wrpr		%g0, (PSTATE_RMO | PSTATE_PEF | PSTATE_PRIV), %pstate
		rdpr		%otherwin, %g1
		rdpr		%cansave, %g3
		add		%g3, %g1, %g3
		wrpr		%g3, 0x0, %cansave
		wrpr		%g0, 0x0, %otherwin
		wrpr		%g0, (PSTATE_RMO | PSTATE_PEF | PSTATE_PRIV | PSTATE_IE), %pstate
		ba,pt		%xcc, sys_exit
		 stb		%g0, [%g6 + AOFF_task_thread + AOFF_thread_w_saved]

linux_sparc_ni_syscall:
	sethi		%hi(sys_ni_syscall), %l7
	b,pt		%xcc, 4f
	 or		%l7, %lo(sys_ni_syscall), %l7

linux_syscall_trace32:
	call		syscall_trace
	 nop
	srl		%i0, 0, %o0
	mov		%i4, %o4
	srl		%i1, 0, %o1
	srl		%i2, 0, %o2
	b,pt		%xcc, 2f
	 srl		%i3, 0, %o3

linux_syscall_trace:
	call		syscall_trace
	 nop
	mov		%i0, %o0
	mov		%i1, %o1
	mov		%i2, %o2
	mov		%i3, %o3
	b,pt		%xcc, 2f
	 mov		%i4, %o4


	/* Linux 32-bit and SunOS system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall32
linux_sparc_syscall32:
	/* Direct access to user regs, much faster. */
	cmp		%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn		%xcc, linux_sparc_ni_syscall		! CTI
	 srl		%i0, 0, %o0				! IEU0
	sll		%g1, 2, %l4				! IEU0	Group
#ifdef SYSCALL_TRACING
	add		%sp, STACK_BIAS + REGWIN_SZ, %o1
	call		syscall_trace_entry
	 mov		%g1, %o0
	srl		%i0, 0, %o0
#endif
	mov		%i4, %o4				! IEU1
	lduw		[%l7 + %l4], %l7			! Load
	srl		%i1, 0, %o1				! IEU0	Group
	ldx		[%curptr + AOFF_task_ptrace], %l0	! Load

	mov		%i5, %o5				! IEU1
	srl		%i2, 0, %o2				! IEU0	Group
	andcc		%l0, 0x02, %g0				! IEU0	Group
	bne,pn		%icc, linux_syscall_trace32		! CTI
	 mov		%i0, %l5				! IEU1
	call		%l7					! CTI	Group brk forced
	 srl		%i3, 0, %o3				! IEU0
	ba,a,pt		%xcc, 3f

	/* Linux native and SunOS system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall, ret_sys_call
linux_sparc_syscall:
	/* Direct access to user regs, much faster. */
	cmp		%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn		%xcc, linux_sparc_ni_syscall		! CTI
	 mov		%i0, %o0				! IEU0
	sll		%g1, 2, %l4				! IEU0	Group
#ifdef SYSCALL_TRACING
	add		%sp, STACK_BIAS + REGWIN_SZ, %o1
	call		syscall_trace_entry
	 mov		%g1, %o0
	mov		%i0, %o0
#endif
	mov		%i1, %o1				! IEU1
	lduw		[%l7 + %l4], %l7			! Load
4:	mov		%i2, %o2				! IEU0	Group
	ldx		[%curptr + AOFF_task_ptrace], %l0	! Load

	mov		%i3, %o3				! IEU1
	mov		%i4, %o4				! IEU0	Group
	andcc		%l0, 0x02, %g0				! IEU1	Group+1 bubble
	bne,pn		%icc, linux_syscall_trace		! CTI	Group
	 mov		%i0, %l5				! IEU0
2:	call		%l7					! CTI	Group brk forced
	 mov		%i5, %o5				! IEU0
	nop

3:	stx		%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]
ret_sys_call:
#ifdef SYSCALL_TRACING
	call		syscall_trace_exit
	 add		%sp, STACK_BIAS + REGWIN_SZ, %o1
#endif
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE], %g3
	ldx		[%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC], %l1 ! pc = npc
	sra		%o0, 0, %o0
	mov		%ulo(TSTATE_XCARRY | TSTATE_ICARRY), %g2
	cmp		%o0, -ENOIOCTLCMD
	sllx		%g2, 32, %g2
	bgeu,pn		%xcc, 1f

	 andcc		%l0, 0x02, %l6	
	andn		%g3, %g2, %g3		/* System call success, clear Carry condition code. */
	stx		%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE]	
	bne,pn		%icc, linux_syscall_trace2
	 add		%l1, 0x4, %l2				         ! npc = npc+4
	stx		%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC]
	ba,pt		%xcc, rtrap_clr_l6
	 stx		%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC]

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub		%g0, %o0, %o0
	or		%g3, %g2, %g3
	stx		%o0, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_I0]
	mov		1, %l6
	stx		%g3, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TSTATE]
	bne,pn		%icc, linux_syscall_trace2
	 add		%l1, 0x4, %l2				         !npc = npc+4
	stx		%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC]

	b,pt		%xcc, rtrap
	 stx		%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC]
linux_syscall_trace2:
	call		syscall_trace
	 nop
	stx		%l1, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TPC]
	ba,pt		%xcc, rtrap
	 stx		%l2, [%sp + STACK_BIAS + REGWIN_SZ + PT_V9_TNPC]

	.align		32
	.globl		__flushw_user
__flushw_user:
	rdpr		%otherwin, %g1
	brz,pn		%g1, 2f
	 clr		%g2
1:	save		%sp, -128, %sp
	rdpr		%otherwin, %g1
	brnz,pt		%g1, 1b
	 add		%g2, 1, %g2
1:	sub		%g2, 1, %g2
	brnz,pt		%g2, 1b
	 restore	%g0, %g0, %g0
2:	retl
	 nop

/* This need not obtain the xtime_lock as it is coded in
 * an implicitly SMP safe way already.
 */
	.align		64
	.globl		do_gettimeofday
do_gettimeofday:	/* %o0 = timevalp */
	/* Load doubles must be used on xtime so that what we get
	 * is guarenteed to be atomic, this is why we can run this
	 * with interrupts on full blast.  Don't touch this... -DaveM
	 *
	 * Note with time_t changes to the timeval type, I must now use
	 * nucleus atomic quad 128-bit loads.
	 */
	sethi	%hi(timer_tick_offset), %g3
	sethi	%hi(xtime), %g2
	sethi	%hi(timer_tick_compare), %g1
	ldx	[%g3 + %lo(timer_tick_offset)], %g3
	or	%g2, %lo(xtime), %g2
	or	%g1, %lo(timer_tick_compare), %g1
1:	ldda	[%g2] ASI_NUCLEUS_QUAD_LDD, %o4
	rd	%tick, %o1
	ldx	[%g1], %g7
	ldda	[%g2] ASI_NUCLEUS_QUAD_LDD, %o2
	xor	%o4, %o2, %o2
	xor	%o5, %o3, %o3
	orcc	%o2, %o3, %g0
	bne,pn	%xcc, 1b
	 sethi	%hi(wall_jiffies), %o2
	sethi	%hi(jiffies), %o3
	ldx	[%o2 + %lo(wall_jiffies)], %o2
	ldx	[%o3 + %lo(jiffies)], %o3
	sub	%o3, %o2, %o2
	sethi	%hi(timer_ticks_per_usec_quotient), %o3
	add	%g3, %o1, %o1
	ldx	[%o3 + %lo(timer_ticks_per_usec_quotient)], %o3
	sub	%o1, %g7, %o1
	mulx	%o3, %o1, %o1
	brz,pt	%o2, 1f
	 srlx	%o1, 32, %o1
	sethi	%hi(10000), %g2
	or	%g2, %lo(10000), %g2
	add	%o1, %g2, %o1
1:	sethi	%hi(1000000), %o2
	srlx	%o5, 32, %o5
	or	%o2, %lo(1000000), %o2
	add	%o5, %o1, %o5
	cmp	%o5, %o2
	bl,a,pn	%xcc, 1f
	 stx	%o4, [%o0 + 0x0]
	add	%o4, 0x1, %o4
	sub	%o5, %o2, %o5
	stx	%o4, [%o0 + 0x0]
1:	retl
	 st	%o5, [%o0 + 0x8]
