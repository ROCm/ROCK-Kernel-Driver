/* -*- mode: asm -*-
 *
 *  linux/arch/h8300/platform/h8300h/entry.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *  David McCullough <davidm@snapgear.com>
 *
 */

/*
 *  entry.S
 *  include exception/interrupt gateway
 *          system call entry
 */

#include <linux/sys.h>
#include <linux/config.h>
#include <asm/unistd.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
			
ENOSYS = 38

LSIGTRAP = 5

SOFTIRQ_PENDING = 0
		
/* the following macro is used when enabling interrupts */

LER4	=	0
LER5	=	4
LER6	=	8
LER3	=	12
LER2	=	16
LER1	=	20
LORIG	=	24
LCCR	=	28
LER0	=	30
LVEC	=	34
LRET	=	38

	.h8300h

/* CPU context save/restore macros. */
	
	.macro	SAVE_ALL
	mov.l	er0,@-sp

	stc	ccr,r0l				/* check kernel mode */
	orc	#0x10,ccr
	btst	#4,r0l
	bne	5f

	mov.l	sp,@SYMBOL_NAME(sw_usp)		/* user mode */
	mov.l	@sp,er0
	mov.l	@SYMBOL_NAME(sw_ksp),sp
	sub.l	#(LRET-LORIG),sp		/* allocate LORIG - LRET */ 
	mov.l	er0,@-sp
	mov.l	er1,@-sp
	mov.l   @SYMBOL_NAME(sw_usp),er0
	mov.l   @(8:16,er0),er1                 /* copy the RET addr */
	mov.l   er1,@(LRET-LER1:16,sp)

	mov.w	e1,r1				/* e1 highbyte = ccr */
	and	#0xef,r1h			/* mask mode? flag */
	sub.w	r0,r0
	mov.b	r1h,r0l
	mov.w	r0,@(LCCR-LER1:16,sp)		/* copy ccr */
	mov.l	@(LORIG-LER1:16,sp),er0
	mov.l	er0,@(LER0-LER1:16,sp)		/* copy ER0 */
	bra	6f
5:
	mov.l	@sp,er0				/* kernel mode */
	subs	#2,sp				/* dummy ccr */
	mov.l	er0,@-sp
	mov.l	er1,@-sp
	mov.w	@(LRET-LER1:16,sp),r1		/* copy old ccr */
	mov.b	r1h,r1l
	mov.b	#0,r1h
	mov.w	r1,@(LCCR-LER1:16,sp)		/* set ccr */
6:
	mov.l	er2,@-sp
	mov.l	er3,@-sp
	mov.l	er6,@-sp			/* syscall arg #6 */
	mov.l	er5,@-sp			/* syscall arg #5 */
	mov.l	er4,@-sp			/* syscall arg #4 */
	.endm

	.macro	RESTORE_ALL
	mov.l	@sp+,er4
	mov.l	@sp+,er5
	mov.l	@sp+,er6
	mov.l	@sp+,er3
	mov.l	@sp+,er2
	mov.w	@(LCCR-LER1:16,sp),r0		/* check kernel mode */
	btst	#4,r0l
	bne	7f

	orc	#0x80,ccr
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	@(LER0-LER1:16,sp),er1		/* restore ER0 */
	mov.l	er1,@er0
	mov.w	@(LCCR-LER1:16,sp),r1		/* restore the RET addr */
	mov.b	r1l,r1h
	mov.b	@(LRET+1-LER1:16,sp),r1l
	mov.w	r1,e1
	mov.w	@(LRET+2-LER1:16,sp),r1
	mov.l	er1,@(8:16,er0)

	mov.l	@sp+,er1
	add.l	#(LRET-LER1),sp			/* remove LORIG - LRET */ 
	mov.l	sp,@SYMBOL_NAME(sw_ksp)
	mov.l	er0,sp
	bra	8f
7:
	mov.l	@sp+,er1
	adds	#4,sp
	adds	#2,sp
8:
	mov.l	@sp+,er0
	adds	#4,sp				/* remove the sw created LVEC */
	rte
	.endm
	
.globl SYMBOL_NAME(system_call)
.globl SYMBOL_NAME(ret_from_exception)
.globl SYMBOL_NAME(ret_from_fork)
.globl SYMBOL_NAME(ret_from_signal)
.globl SYMBOL_NAME(ret_from_interrupt)
.globl SYMBOL_NAME(interrupt_redirect_table)
.globl SYMBOL_NAME(sw_ksp),SYMBOL_NAME(sw_usp)
.globl SYMBOL_NAME(resume)
.globl SYMBOL_NAME(syscall_trampoline)
.globl SYMBOL_NAME(interrupt_redirect_table)
.globl SYMBOL_NAME(interrupt_entry)
.globl SYMBOL_NAME(system_call)
.globl SYMBOL_NAME(trace_break)

#if defined(CONFIG_ROMKERNEL)
INTERRUPTS = 64		
	.section .int_redirect,"ax"
SYMBOL_NAME_LABEL(interrupt_redirect_table)
	.rept	7
	.long	0
	.endr
	jsr	@SYMBOL_NAME(interrupt_entry)	/* NMI */
	jmp	@SYMBOL_NAME(system_call)	/* TRAPA #0 (System call) */
	.long	0
	.long	0
	jmp	@SYMBOL_NAME(trace_break)	/* TRAPA #3 (breakpoint) */
	.rept	INTERRUPTS-12
	jsr	@SYMBOL_NAME(interrupt_entry)
	.endr
#endif
#if defined(CONFIG_RAMKERNEL)
.globl SYMBOL_NAME(interrupt_redirect_table)
	.section .bss
SYMBOL_NAME_LABEL(interrupt_redirect_table)
	.space	4
#endif

	.section .text
	.align	2
SYMBOL_NAME_LABEL(interrupt_entry)
	SAVE_ALL
	mov.w	@(LCCR,sp),r0
	btst	#4,r0l
	bne	1f
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	@(4:16,er0),er0
	bra	2f
1:
	mov.l	@(LVEC,sp),er0
2:
#if defined(CONFIG_ROMKERNEL)
	sub.l	#SYMBOL_NAME(interrupt_redirect_table),er0
#endif
#if defined(CONFIG_RAMKERNEL)
	mov.l	@SYMBOL_NAME(interrupt_redirect_table),er1
	sub.l	er1,er0
#endif
	shlr.l	er0
	shlr.l	er0
	dec.l	#1,er0
	mov.l	sp,er1
	subs	#4,er1				/* adjust ret_pc */
	jsr	@SYMBOL_NAME(process_int)
	mov.l	@SYMBOL_NAME(irq_stat)+SOFTIRQ_PENDING,er0
	beq	1f
	jsr	@SYMBOL_NAME(do_softirq)
1:
	jmp	@SYMBOL_NAME(ret_from_exception)
	
SYMBOL_NAME_LABEL(system_call)
	subs	#4,sp				/* dummy LVEC */
	SAVE_ALL
	mov.w	@(LCCR:16,sp),r1
	bset	#4,r1l
	ldc	r1l,ccr
	mov.l	er0,er4
	mov.l	#-ENOSYS,er0
	mov.l	er0,@(LER0:16,sp)

	/* save top of frame */
	mov.l	sp,er0
	jsr	@SYMBOL_NAME(set_esp0)
	cmp.l	#NR_syscalls,er4
	bcc	SYMBOL_NAME(ret_from_exception):16
	shll.l	er4
	shll.l	er4
	mov.l	#SYMBOL_NAME(sys_call_table),er0
	add.l	er4,er0
	mov.l	@er0,er4
	beq	SYMBOL_NAME(ret_from_exception):16	
	mov.l	sp,er2
	and.w	#0xe000,r2
	mov.b	@((TASK_FLAGS+3-(TIF_SYSCALL_TRACE >> 3)):16,er2),r2l
	btst	#(TIF_SYSCALL_TRACE & 7),r2l
	bne	1f
	mov.l	@(LER1:16,sp),er0
	mov.l	@(LER2:16,sp),er1
	mov.l	@(LER3:16,sp),er2
	jsr	@er4
	mov.l	er0,@(LER0,sp)			/* save the return value */
#if defined(CONFIG_SYSCALL_PRINT)
	jsr	@SYMBOL_NAME(syscall_print)
#endif
	jmp	@SYMBOL_NAME(ret_from_exception)
1:
	jsr	SYMBOL_NAME(syscall_trace)
	mov.l	@(LER1:16,sp),er0
	mov.l	@(LER2:16,sp),er1
	mov.l	@(LER3:16,sp),er2
	jsr	@er4
	mov.l	er0,@(LER0:16,sp)		/* save the return value */
	jsr	SYMBOL_NAME(syscall_trace)

SYMBOL_NAME_LABEL(ret_from_signal)

SYMBOL_NAME_LABEL(ret_from_exception)
	mov.b	@(LCCR+1:16,sp),r0l
	btst	#4,r0l			/* check if returning to kernel */
	bne	3f			/* if so, skip resched, signals */
	andc	#0x7f,ccr
	mov.l	sp,er2
	and.w	#0xe000,r2
	mov.l	@(TI_FLAGS:16,er2),er1
	and.l	#_TIF_WORK_MASK,er1
	beq	3f
1:
	mov.l	@(TI_FLAGS:16,er2),er1
	btst	#TIF_NEED_RESCHED,r1l
	bne	@SYMBOL_NAME(reschedule):16
	mov.l	sp,er1
	subs	#4,er1			/* adjust retpc */
	mov.l	er2,er0
	jsr	@SYMBOL_NAME(do_signal)
3:	 
	RESTORE_ALL			/* Does RTE */

SYMBOL_NAME_LABEL(reschedule)
	/* save top of frame */
	mov.l	sp,er0
	jsr	@SYMBOL_NAME(set_esp0)

	mov.l	#SYMBOL_NAME(ret_from_exception),er0
	mov.l	er0,@-sp
	jmp	@SYMBOL_NAME(schedule)

SYMBOL_NAME_LABEL(ret_from_fork)
	mov.l	er2,er0
	jsr	@SYMBOL_NAME(schedule_tail)
	jmp	@SYMBOL_NAME_LABEL(ret_from_exception)	

SYMBOL_NAME_LABEL(resume)
	/*
	 * Beware - when entering resume, offset of tss is in d1,
	 * prev (the current task) is in a0, next (the new task)
	 * is in a1 and d2.b is non-zero if the mm structure is
	 * shared between the tasks, so don't change these
	 * registers until their contents are no longer needed.
	 */

	/* save sr */
	sub.w	r3,r3
	stc	ccr,r3l
	mov.w	r3,@(THREAD_CCR+2:16,er0)

	/* disable interrupts */
	orc	#0x80,ccr
	mov.l	@SYMBOL_NAME(sw_usp),er3
	mov.l	er3,@(THREAD_USP:16,er0)
	mov.l	sp,@(THREAD_KSP:16,er0)

	/* Skip address space switching if they are the same. */
	/* FIXME: what did we hack out of here, this does nothing! */

	mov.l	@(THREAD_USP:16,er1),er0
	mov.l	er0,@SYMBOL_NAME(sw_usp)
	mov.l	@(THREAD_KSP:16,er1),sp
			
	/* restore status register */
	mov.w	@(THREAD_CCR+2:16,er1),r3

	ldc	r3l,ccr
	rts
	
SYMBOL_NAME_LABEL(trace_break)
	subs	#4,sp
	SAVE_ALL
	sub.l	er1,er1
	dec.l	#1,er1
	mov.l	er1,@(LORIG,sp)	
	mov.l	sp,er0
	jsr	@SYMBOL_NAME(set_esp0)
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	@er0,er1
	subs	#2,er1
	mov.l	er1,@er0	
	and.w	#0xff,e1
	mov.l	er1,er0
	jsr	@SYMBOL_NAME(trace_trap)
	jmp	@SYMBOL_NAME(ret_from_exception)	

SYMBOL_NAME_LABEL(syscall_trampoline)
	mov.l	er0,er6
	mov.l	sp,er0
	jmp	@er6

	.section	.bss
SYMBOL_NAME_LABEL(sw_ksp)
	.space	4	
SYMBOL_NAME_LABEL(sw_usp)
	.space	4	
