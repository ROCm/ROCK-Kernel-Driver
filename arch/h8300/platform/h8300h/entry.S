/* -*- mode: asm -*-
 *
 *  linux/arch/h8300/platform/h8300h/entry.S
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
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

LER3	=	0
LER2	=	4
LER1	=	8
LORIG	=	12
LCCR	=	16
LER0	=	18
LVEC	=	22
LRET	=	24

	.h8300h

/* CPU context save/restore macros. */
	
	.macro	SAVE_ALL
	mov.l	er0,@-sp
	stc	ccr,r0l
	orc	#0x10,ccr
	btst	#4,r0l
	bne	1f
	mov.l	sp,@SYMBOL_NAME(sw_usp)
	mov.l	@sp,er0
	mov.l	@SYMBOL_NAME(sw_ksp),sp
	mov.l	er0,@-sp
	stc	ccr,r0l
	and	#0xef,r0l
	mov.w	r0,@-sp
	mov.l	@(2:16,sp),er0
	bra	2f
1:
	mov.l	@sp,er0
	stc	ccr,@-sp
2:	
	mov.l	er0,@-sp
	mov.l	er1,@-sp
	mov.l	er2,@-sp
	mov.l	er3,@-sp
	.endm

	.macro RESTORE_REGS
	mov.w	@(LCCR:16,sp),r0
	btst	#4,r0l
	bne	1f
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	@(LER0:16,sp),er1
	mov.l	er1,@er0
	mov.l	@sp+,er3
	mov.l	@sp+,er2
	mov.l	@sp+,er1
	add.l	#10,sp
	mov.l	sp,@SYMBOL_NAME(sw_ksp)
	mov.l	er0,sp
	bra	2f
1:
	mov.l	@sp+,er3
	mov.l	@sp+,er2
	mov.l	@sp+,er1
	adds	#4,sp
	adds	#2,sp
2:
	mov.l	@sp+,er0
	adds	#4,sp
	.endm
	
	.macro	RESTORE_ALL
	RESTORE_REGS
	rte
	.endm

#define SWITCH_STACK_SIZE (3*4+12)	/* includes return address */

	.macro	SAVE_SWITCH_STACK
	mov.l	er4,@-sp
	mov.l	er5,@-sp
	mov.l	er6,@-sp
	.endm

	.macro	RESTORE_SWITCH_STACK
	mov.l	@sp+,er6
	mov.l	@sp+,er5
	mov.l	@sp+,er4
	.endm

.globl SYMBOL_NAME(system_call)
.globl SYMBOL_NAME(ret_from_exception)
.globl SYMBOL_NAME(ret_from_signal)
.globl SYMBOL_NAME(ret_from_interrupt), SYMBOL_NAME(bad_interrupt)
.globl SYMBOL_NAME(interrupt_redirect_table)
.globl SYMBOL_NAME(sw_ksp),SYMBOL_NAME(sw_usp)
.globl SYMBOL_NAME(resume)
.globl SYMBOL_NAME(sys_vfork)
.globl SYMBOL_NAME(syscall_trampoline)
	
	.section .int_redirect,"ax"
SYMBOL_NAME_LABEL(interrupt_redirect_table)
	.rept	7
	.long	0
	.endr
	jsr	@interrupt_entry		/* NMI */
	jmp	@SYMBOL_NAME(system_call)	/* TRAPA #0 (System call) */
	.long	0
	.long	0
	jmp	@SYMBOL_NAME(trace_break)	/* TRAPA #3 (breakpoint) */
	.rept	64-12
	jsr	@interrupt_entry
	.endr

	.section .text
	.align	2
interrupt_entry:
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
	sub.l	#SYMBOL_NAME(interrupt_redirect_table),er0
	shlr.l	er0
	shlr.l	er0
	dec.l	#1,er0
	mov.l	sp,er1
	jsr	@SYMBOL_NAME(process_int)
	mov.l	@SYMBOL_NAME(irq_stat)+SOFTIRQ_PENDING,er0
	beq	1f
	jsr	@SYMBOL_NAME(do_softirq)
1:
	jmp	@SYMBOL_NAME(ret_from_exception)

SYMBOL_NAME_LABEL(system_call)
	subs	#4,sp
	SAVE_ALL
	mov.l	er0,er3
	mov.l	#-ENOSYS,er0
	mov.l	er0,@(LER0:16,sp)

	/* save top of frame */
	mov.l	sp,er0
	mov.l	er3,@-sp
	jsr	@SYMBOL_NAME(set_esp0)
	mov.l	@sp+,er3
	cmp.l	#NR_syscalls,er3
	bcc	SYMBOL_NAME(ret_from_exception):16
	shll.l	er3
	shll.l	er3
	mov.l	#SYMBOL_NAME(sys_call_table),er0
	add.l	er3,er0
	mov.l	@er0,er0
	mov.l	er0,er3
	beq	SYMBOL_NAME(ret_from_exception):16	
	mov.l	@SYMBOL_NAME(_current_task),er2
	mov.b	@((TASK_FLAGS+3-(TIF_SYSCALL_TRACE >> 3)):16,er2),r2l
	btst	#(TIF_SYSCALL_TRACE & 7),r2l
	bne	1f
	mov.l	@(LER1:16,sp),er0
	mov.l	@(LER2:16,sp),er1
	mov.l	@(LER3:16,sp),er2
	mov.l	er5,@-sp
	mov.l	er4,@-sp
	jsr	@er3
	adds	#4,sp
	adds	#4,sp
	mov.l	er0,@(LER0,sp)		/* save the return value */
#if defined(CONFIG_SYSCALL_PRINT)
	jsr	@SYMBOL_NAME(syscall_print)
#endif
	jmp	@SYMBOL_NAME(ret_from_exception)
1:
	SAVE_SWITCH_STACK
	mov.l	er3,er5			/* save syscall entry */
	jsr	SYMBOL_NAME(syscall_trace)
	mov.l	er5,er3
	RESTORE_SWITCH_STACK
	mov.l	@(LER1:16,sp),er0
	mov.l	@(LER2:16,sp),er1
	mov.l	@(LER3:16,sp),er2
	mov.l	er5,@-sp
	mov.l	er4,@-sp
	jsr	@er3
	adds	#4,sp
	adds	#4,sp
	mov.l	er0,@(LER0:16,sp)		/* save the return value */
	SAVE_SWITCH_STACK
	jsr	SYMBOL_NAME(syscall_trace)

SYMBOL_NAME_LABEL(ret_from_signal)
	RESTORE_SWITCH_STACK

SYMBOL_NAME_LABEL(ret_from_exception)
	mov.b	@(LCCR+1:16,sp),r0l
	btst	#4,r0l			/* check if returning to kernel */
	bne	3f			/* if so, skip resched, signals */
	andc	#0x7f,ccr
	mov.l	@SYMBOL_NAME(_current_task),er0
	mov.l	@(TI_FLAGS:16,er2),er1
	and.l	#_TIF_WORK_MASK,er1
	bne	1f
	mov.l	@((TASK_THREAD+THREAD_VFORK):16,er0),er1 
	bne	Lvfork_return
3:	 
	RESTORE_ALL			/* Does RTE */
1:
	mov.l	@(TI_FLAGS:16,er2),er1
	btst	#TIF_NEED_RESCHED,r1l
	bne	@SYMBOL_NAME(reschedule):16

Lsignal_return:
	SAVE_SWITCH_STACK
	mov.l	sp,er1
	add	#12,er1
	mov.l	er2,er0
	jsr	@SYMBOL_NAME(do_signal)
	RESTORE_SWITCH_STACK
	mov.l	@SYMBOL_NAME(_current_task),er0
	mov.l	@((TASK_THREAD+THREAD_VFORK):16,er0),er1 
	bne	Lvfork_return
	RESTORE_ALL

Lvfork_return:
	sub.l	er2,er2
	mov.l	er2,@((TASK_THREAD+THREAD_VFORK):16,er0)
	mov.l	@SYMBOL_NAME(sw_usp),er0
	mov.l	er1,@(8:16,er0)
	RESTORE_ALL
	
SYMBOL_NAME_LABEL(reschedule)
	/* save top of frame */
	mov.l	sp,er0
	jsr	@SYMBOL_NAME(set_esp0)

	mov.l	#SYMBOL_NAME(ret_from_exception),er0
	mov.l	er0,@-sp
	jmp	@SYMBOL_NAME(schedule)

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
	mov.w	r3,@(THREAD_CCR:16,er0)
	SAVE_SWITCH_STACK

	/* disable interrupts */
	orc	#0x80,ccr
	mov.l	@SYMBOL_NAME(sw_usp),er3
	mov.l	er3,@(THREAD_USP:16,er0)
	mov.l	sp,@(THREAD_KSP:16,er0)
	
	/* get pointer to tss struct (a1 contains new task) */
	mov.l	er1,@SYMBOL_NAME(_current_task)

	/* Skip address space switching if they are the same. */
	/* FIXME: what did we hack out of here, this does nothing! */

	mov.l	@(THREAD_USP:16,er1),er0
	mov.l	er0,@SYMBOL_NAME(sw_usp)
	mov.l	@(THREAD_KSP:16,er1),sp
	RESTORE_SWITCH_STACK
			
	/* restore status register */
	mov.w	@(THREAD_CCR:16,er1),r3

	ldc	r3l,ccr

	rts
	
/* Handler for uninitialized and spurious interrupts */

SYMBOL_NAME_LABEL(bad_interrupt)
	mov.l	@SYMBOL_NAME(num_spurious),er0
	inc.l	#1,er0
	mov.l	er0,@SYMBOL_NAME(num_spurious)
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

SYMBOL_NAME_LABEL(sys_vfork)
	SAVE_SWITCH_STACK
	mov.l	@SYMBOL_NAME(sw_usp),er6
	mov.l	@(8:16,er6),er6
	mov.l	sp,er0
	add.l	#SWITCH_STACK_SIZE,er0
	jsr	@SYMBOL_NAME(h8300_vfork)
	mov.l	@SYMBOL_NAME(_current_task),er5
	mov.l	er6,@((TASK_THREAD+THREAD_VFORK):16,er5)
	RESTORE_SWITCH_STACK
	rts

SYMBOL_NAME_LABEL(syscall_trampoline)
	SAVE_SWITCH_STACK
	mov.l	er0,er6
	mov.l	sp,er0
	add.l	#SWITCH_STACK_SIZE,er0
	jsr	@er6
	RESTORE_SWITCH_STACK
	rts

	.section	.bss
SYMBOL_NAME_LABEL(sw_ksp)
	.space	4	
SYMBOL_NAME_LABEL(sw_usp)
	.space	4	
