/* $Id: rtrap.S,v 1.53 2000/08/06 05:20:35 davem Exp $
 * rtrap.S: Preparing for return from trap on Sparc V9.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/head.h>
#include <asm/visasm.h>
#include <asm/processor.h>

#define		PTREGS_OFF		(STACK_BIAS + REGWIN_SZ)
#define		RTRAP_PSTATE		(PSTATE_RMO|PSTATE_PEF|PSTATE_PRIV|PSTATE_IE)
#define		RTRAP_PSTATE_IRQOFF	(PSTATE_RMO|PSTATE_PEF|PSTATE_PRIV)
#define		RTRAP_PSTATE_AG_IRQOFF	(PSTATE_RMO|PSTATE_PEF|PSTATE_PRIV|PSTATE_AG)

#if 0
#define		RTRAP_CHECK		call rtrap_check;  add %sp, (STACK_BIAS+REGWIN_SZ), %o0;
#else
#define		RTRAP_CHECK
#endif

		.text

		.align			32
__handle_softirq:
		call			do_softirq
		 nop
		ba,a,pt			%xcc, __handle_softirq_continue
		 nop
__handle_preemption:
		call			schedule
		 nop
		ba,pt			%xcc, __handle_preemption_continue
		 nop
__handle_user_windows:
		wrpr			%g0, RTRAP_PSTATE, %pstate
		call			fault_in_user_windows
		 nop
		ba,pt			%xcc, __handle_user_windows_continue
		 nop
__handle_perfctrs:
		/* Don't forget to preserve user window invariants. */
		wrpr			%g0, RTRAP_PSTATE, %pstate
		call			update_perfctrs
		 nop
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		ldub			[%g6 + AOFF_task_thread + AOFF_thread_w_saved], %o2
		brz,pt			%o2, __handle_perfctrs_continue
		 sethi			%hi(TSTATE_PEF), %l6
		wrpr			%g0, RTRAP_PSTATE, %pstate

		call			fault_in_user_windows
		 nop
		ba,pt			%xcc, __handle_perfctrs_continue
		 nop
__handle_userfpu:
		rd			%fprs, %l5
		andcc			%l5, FPRS_FEF, %g0
		be,a,pn			%icc, __handle_userfpu_continue
		 andn			%l1, %l6, %l1
		ba,a,pt			%xcc, __handle_userfpu_continue
__handle_signal:
		clr			%o0
		mov			%l5, %o2
		mov			%l6, %o3
		call			do_signal
		 add			%sp, STACK_BIAS + REGWIN_SZ, %o1
		clr			%l6

		/* Signal delivery can modify pt_regs tstate, so we must
		 * reload it.
		 */
		ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
		sethi			%hi(0xf << 20), %l4
		and			%l1, %l4, %l4
		ba,pt			%xcc, __handle_signal_continue
		 andn			%l1, %l4, %l1

		.align			64
		.globl			rtrap_clr_l6, rtrap
rtrap_clr_l6:	clr			%l6
rtrap:		lduw			[%g6 + AOFF_task_processor], %l0
		sethi			%hi(irq_stat), %l2	! &softirq_active
		or			%l2, %lo(irq_stat), %l2	! &softirq_active
		sllx			%l0, 6, %l0
		ldx			[%l2 + %l0], %l1	! softirq_active + softirq_mask
		srlx			%l1, 32, %l2
		andcc			%l1, %l2, %g0

		bne,pn			%icc, __handle_softirq
		 ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
__handle_softirq_continue:
		sethi			%hi(0xf << 20), %l4
		andcc			%l1, TSTATE_PRIV, %l3
		and			%l1, %l4, %l4
		bne,pn			%icc, to_kernel
		 andn			%l1, %l4, %l1
to_user:	ldx			[%g6 + AOFF_task_need_resched], %l0

		brnz,pn			%l0, __handle_preemption
__handle_preemption_continue:
		 lduw			[%g6 + AOFF_task_sigpending], %l0
		brnz,pn			%l0, __handle_signal
		 nop
__handle_signal_continue:
check_user_wins:
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		ldub			[%g6 + AOFF_task_thread + AOFF_thread_w_saved], %o2
		brnz,pn			%o2, __handle_user_windows
		 sethi			%hi(TSTATE_PEF), %l6

__handle_user_windows_continue:
		RTRAP_CHECK
		ldub			[%g6 + AOFF_task_thread + AOFF_thread_flags], %l5
		andcc			%l5, SPARC_FLAG_PERFCTR, %g0
		bne,pn			%xcc, __handle_perfctrs
__handle_perfctrs_continue:
		 andcc			%l1, %l6, %g0
		bne,pn			%xcc, __handle_userfpu
		 stb			%g0, [%g6 + AOFF_task_thread + AOFF_thread_fpdepth] ! This is neccessary for non-syscall rtraps only
__handle_userfpu_continue:

rt_continue:	ldx			[%sp + PTREGS_OFF + PT_V9_G1], %g1
		ldx			[%sp + PTREGS_OFF + PT_V9_G2], %g2

		ldx			[%sp + PTREGS_OFF + PT_V9_G3], %g3
		ldx			[%sp + PTREGS_OFF + PT_V9_G4], %g4
		ldx			[%sp + PTREGS_OFF + PT_V9_G5], %g5
		ldx			[%sp + PTREGS_OFF + PT_V9_G6], %g6
		ldx			[%sp + PTREGS_OFF + PT_V9_G7], %g7
		wrpr			%g0, RTRAP_PSTATE_AG_IRQOFF, %pstate
		ldx			[%sp + PTREGS_OFF + PT_V9_I0], %i0
		ldx			[%sp + PTREGS_OFF + PT_V9_I1], %i1

		ldx			[%sp + PTREGS_OFF + PT_V9_I2], %i2
		ldx			[%sp + PTREGS_OFF + PT_V9_I3], %i3
		ldx			[%sp + PTREGS_OFF + PT_V9_I4], %i4
		ldx			[%sp + PTREGS_OFF + PT_V9_I5], %i5
		ldx			[%sp + PTREGS_OFF + PT_V9_I6], %i6
		ldx			[%sp + PTREGS_OFF + PT_V9_I7], %i7
		ldx			[%sp + PTREGS_OFF + PT_V9_TPC], %l2
		ldx			[%sp + PTREGS_OFF + PT_V9_TNPC], %o2

		ld			[%sp + PTREGS_OFF + PT_V9_Y], %o3
		wr			%o3, %g0, %y
		srl			%l4, 20, %l4
		wrpr			%l4, 0x0, %pil
		wrpr			%g0, 0x1, %tl
		wrpr			%l1, %g0, %tstate
		wrpr			%l2, %g0, %tpc
		wrpr			%o2, %g0, %tnpc

		brnz,pn			%l3, kern_rtt
		 mov			PRIMARY_CONTEXT, %l7
		ldxa			[%l7 + %l7] ASI_DMMU, %l0
		stxa			%l0, [%l7] ASI_DMMU
		flush			%g6
		rdpr			%wstate, %l1
		rdpr			%otherwin, %l2
		srl			%l1, 3, %l1

		wrpr			%l2, %g0, %canrestore
		wrpr			%l1, %g0, %wstate
		wrpr			%g0, %g0, %otherwin
		restore
		rdpr			%canrestore, %g1
		wrpr			%g1, 0x0, %cleanwin
		retry
		nop

kern_rtt:	restore
		retry
to_kernel:	ldub			[%g6 + AOFF_task_thread + AOFF_thread_fpdepth], %l5
		brz,pt			%l5, rt_continue
		 srl			%l5, 1, %o0
		add			%g6, AOFF_task_thread + AOFF_thread_fpsaved, %l6
		ldub			[%l6 + %o0], %l2
		sub			%l5, 2, %l5

		add			%g6, AOFF_task_thread + AOFF_thread_gsr, %o1
		andcc			%l2, (FPRS_FEF|FPRS_DU), %g0
		be,pt			%icc, 2f
		 and			%l2, FPRS_DL, %l6
		andcc			%l2, FPRS_FEF, %g0
		be,pn			%icc, 5f
		 sll			%o0, 3, %o5
		rd			%fprs, %g5

		wr			%g5, FPRS_FEF, %fprs
		ldub			[%o1 + %o0], %g5
		add			%g6, AOFF_task_thread + AOFF_thread_xfsr, %o1
		membar			#StoreLoad | #LoadLoad
		sll			%o0, 8, %o2
		add			%g6, AOFF_task_fpregs, %o3
		brz,pn			%l6, 1f
		 add			%g6, AOFF_task_fpregs+0x40, %o4

		ldda			[%o3 + %o2] ASI_BLK_P, %f0
		ldda			[%o4 + %o2] ASI_BLK_P, %f16
1:		andcc			%l2, FPRS_DU, %g0
		be,pn			%icc, 1f
		 wr			%g5, 0, %gsr
		add			%o2, 0x80, %o2
		ldda			[%o3 + %o2] ASI_BLK_P, %f32
		ldda			[%o4 + %o2] ASI_BLK_P, %f48

1:		membar			#Sync
		ldx			[%o1 + %o5], %fsr
2:		stb			%l5, [%g6 + AOFF_task_thread + AOFF_thread_fpdepth]
		ba,pt			%xcc, rt_continue
		 nop
5:		wr			%g0, FPRS_FEF, %fprs
		membar			#StoreLoad | #LoadLoad
		sll			%o0, 8, %o2

		add			%g6, AOFF_task_fpregs+0x80, %o3
		add			%g6, AOFF_task_fpregs+0xc0, %o4
		ldda			[%o3 + %o2] ASI_BLK_P, %f32
		ldda			[%o4 + %o2] ASI_BLK_P, %f48
		membar			#Sync
		wr			%g0, FPRS_DU, %fprs
		ba,pt			%xcc, rt_continue
		 stb			%l5, [%g6 + AOFF_task_thread + AOFF_thread_fpdepth]

#undef PTREGS_OFF
