/*
 *  arch/ppc64/kernel/entry.S
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  MPC8xx modifications Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This file contains the system call entry code, context switch
 *  code, and exception/interrupt return code for PowerPC.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <asm/unistd.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>
#include <asm/cputable.h>

#ifdef CONFIG_PPC_ISERIES
#define DO_SOFT_DISABLE
#endif

#undef SHOW_SYSCALLS
#undef SHOW_SYSCALLS_TASK

#ifdef SHOW_SYSCALLS_TASK
	.data
show_syscalls_task:
	.long	-1
#endif

	.section	".toc","aw"
.SYS_CALL_TABLE:
	.tc .sys_call_table[TC],.sys_call_table

.SYS_CALL_TABLE32:
	.tc .sys_call_table32[TC],.sys_call_table32

	.section	".text"
	.align 3

/*
 * Handle a system call.
 */
_GLOBAL(DoSyscall)
	std	r3,ORIG_GPR3(r1)
	ld	r11,_CCR(r1)	/* Clear SO bit in CR */
	lis	r10,0x1000
	andc	r11,r11,r10
	std	r11,_CCR(r1)
#ifdef SHOW_SYSCALLS
#ifdef SHOW_SYSCALLS_TASK
	LOADBASE(r31,show_syscalls_task)
	ld	r31,show_syscalls_task@l(r31)
	ld	r10,PACACURRENT(r13)
	cmp	0,r10,r31
	bne	1f
#endif
	LOADADDR(r3,7f)
	ld	r4,GPR0(r1)
	ld	r5,GPR3(r1)
	ld	r6,GPR4(r1)
	ld	r7,GPR5(r1)
	ld	r8,GPR6(r1)
	ld	r9,GPR7(r1)
	bl	.printk
	LOADADDR(r3,77f)
	ld	r4,GPR8(r1)
	ld	r5,GPR9(r1)
	ld	r6, PACACURRENT(r13)
	bl	.printk
	ld	r0,GPR0(r1)
	ld	r3,GPR3(r1)
	ld	r4,GPR4(r1)
	ld	r5,GPR5(r1)
	ld	r6,GPR6(r1)
	ld	r7,GPR7(r1)
	ld	r8,GPR8(r1)
1:
#endif /* SHOW_SYSCALLS */
	clrrdi	r10,r1,THREAD_SHIFT
	ld	r10,TI_FLAGS(r10)
	andi.	r11,r10,_TIF_SYSCALL_T_OR_A
	bne-	50f
	cmpli	0,r0,NR_syscalls
	bge-	66f
/*
 * Need to vector to 32 Bit or default sys_call_table here,
 * based on caller's run-mode / personality.
 */
	andi.	r11,r10,_TIF_32BIT
	beq-	15f
	ld	r10,.SYS_CALL_TABLE32@toc(2)
/*
 * We now zero extend all six arguments (r3 - r8), the compatibility
 * layer assumes this.
 */
	clrldi	r3,r3,32
	clrldi	r4,r4,32
	clrldi	r5,r5,32
	clrldi	r6,r6,32
	clrldi	r7,r7,32
	clrldi	r8,r8,32
	b	17f
15:
	ld	r10,.SYS_CALL_TABLE@toc(2)
17:	slwi	r0,r0,3
	ldx	r10,r10,r0	/* Fetch system call handler [ptr] */
	mtlr	r10
	addi	r9,r1,STACK_FRAME_OVERHEAD
	blrl			/* Call handler */
_GLOBAL(ret_from_syscall_1)
	std	r3,RESULT(r1)	/* Save result */
#ifdef SHOW_SYSCALLS
#ifdef SHOW_SYSCALLS_TASK
	ld	r10, PACACURRENT(13)
	cmp	0,r10,r31
	bne	91f
#endif
	mr	r4,r3
	LOADADDR(r3,79f)
	bl	.printk
	ld	r3,RESULT(r1)
91:
#endif
	li	r10,-_LAST_ERRNO
	cmpld	0,r3,r10
	blt	30f
	neg	r3,r3
22:	ld	r10,_CCR(r1)	/* Set SO bit in CR */
	oris	r10,r10,0x1000
	std	r10,_CCR(r1)
30:	std	r3,GPR3(r1)	/* Update return value */
	b	.ret_from_except
66:	li	r3,ENOSYS
	b	22b
        
/* Traced system call support */
50:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_syscall_trace_enter
	ld	r0,GPR0(r1)	/* Restore original registers */
	ld	r3,GPR3(r1)
	ld	r4,GPR4(r1)
	ld	r5,GPR5(r1)
	ld	r6,GPR6(r1)
	ld	r7,GPR7(r1)
	ld	r8,GPR8(r1)
	/* XXX check this - Anton */
	ld	r9,GPR9(r1)
	cmpli	0,r0,NR_syscalls
	bge-	66f
/*
 * Need to vector to 32 Bit or default sys_call_table here,
 * based on caller's run-mode / personality.
 */
	clrrdi	r10,r1,THREAD_SHIFT
	ld	r10,TI_FLAGS(r10)
	andi.	r11,r10,_TIF_32BIT
	beq-	55f
	ld	r10,.SYS_CALL_TABLE32@toc(2)
/*
 * We now zero extend all six arguments (r3 - r8), the compatibility
 * layer assumes this.
 */
	clrldi	r3,r3,32
	clrldi	r4,r4,32
	clrldi	r5,r5,32
	clrldi	r6,r6,32
	clrldi	r7,r7,32
	clrldi	r8,r8,32
	b	57f
55:
	ld	r10,.SYS_CALL_TABLE@toc(2)
57:
	slwi	r0,r0,3
	ldx	r10,r10,r0	/* Fetch system call handler [ptr] */
	mtlr	r10
	addi	r9,r1,STACK_FRAME_OVERHEAD
	blrl			/* Call handler */
_GLOBAL(ret_from_syscall_2)
	std	r3,RESULT(r1)	/* Save result */	
	li	r10,-_LAST_ERRNO
	cmpld	0,r3,r10
	blt	60f
	neg	r3,r3
57:	ld	r10,_CCR(r1)	/* Set SO bit in CR */
	oris	r10,r10,0x1000
	std	r10,_CCR(r1)
60:	std	r3,GPR3(r1)	/* Update return value */
	bl	.do_syscall_trace_leave
	b	.ret_from_except
66:	li	r3,ENOSYS
	b	57b
#ifdef SHOW_SYSCALLS
7:	.string	"syscall %d(%x, %x, %x, %x, %x, "
77:	.string	"%x, %x), current=%p\n"
79:	.string	" -> %x\n"
	.align	2,0
#endif

	
_GLOBAL(ppc32_swapcontext)
	bl	.sys32_swapcontext
	b	80f
	
_GLOBAL(ppc64_swapcontext)
	bl	.sys_swapcontext
	b	80f

_GLOBAL(ppc32_sigreturn)
	bl	.sys32_sigreturn
	b	80f

_GLOBAL(ppc32_rt_sigreturn)
	bl	.sys32_rt_sigreturn
	b	80f

_GLOBAL(ppc64_rt_sigreturn)
	bl	.sys_rt_sigreturn

80:	clrrdi	r4,r1,THREAD_SHIFT
	ld	r4,TI_FLAGS(r4)
	andi.	r4,r4,_TIF_SYSCALL_T_OR_A
	bne-	81f
	cmpi	0,r3,0
	bge	.ret_from_except
	b	.ret_from_syscall_1
81:	cmpi	0,r3,0
	blt	.ret_from_syscall_2
	bl	.do_syscall_trace_leave
	b	.ret_from_except

/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process, via ret_from_except.
 * On entry, r3 points to the THREAD for the current task, r4
 * points to the THREAD for the new task.
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this you'll have to change
 * the fork code also.
 *
 * The code which creates the new task context is in 'copy_thread'
 * in arch/ppc64/kernel/process.c
 */
_GLOBAL(_switch)
	mflr	r0
	std	r0,16(r1)
	stdu	r1,-SWITCH_FRAME_SIZE(r1)
	/* r3-r13 are caller saved -- Cort */
	SAVE_8GPRS(14, r1)
	SAVE_10GPRS(22, r1)
	mflr	r20		/* Return to switch caller */
	mfmsr	r22
	li	r0, MSR_FP
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	oris	r0,r0,MSR_VEC@h	/* Disable altivec */
	mfspr	r24,SPRN_VRSAVE	/* save vrsave register value */
	std	r24,THREAD_VRSAVE(r3)
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */
	and.	r0,r0,r22
	beq+	1f
	andc	r22,r22,r0
	mtmsrd	r22
	isync
1:	std	r20,_NIP(r1)
	mfcr	r23
	std	r23,_CCR(r1)
	std	r1,KSP(r3)	/* Set old stack pointer */

#ifdef CONFIG_SMP
	/* We need a sync somewhere here to make sure that if the
	 * previous task gets rescheduled on another CPU, it sees all
	 * stores it has performed on this one.
	 */
	sync
#endif /* CONFIG_SMP */

	addi	r6,r4,-THREAD	/* Convert THREAD to 'current' */
	std	r6,PACACURRENT(r13)	/* Set new 'current' */

	ld	r8,KSP(r4)	/* new stack pointer */
BEGIN_FTR_SECTION
	clrrdi	r6,r8,28	/* get its ESID */
	clrrdi	r9,r1,28	/* get current sp ESID */
	clrldi.	r0,r6,2		/* is new ESID c00000000? */
	cmpd	cr1,r6,r9	/* or is new ESID the same as current ESID? */
	cror	eq,4*cr1+eq,eq
	beq	2f		/* if yes, don't slbie it */
	oris	r6,r6,0x0800	/* set C (class) bit */
	slbie	r6
2:
END_FTR_SECTION_IFSET(CPU_FTR_SLB)
	clrrdi	r7,r8,THREAD_SHIFT	/* base of new stack */
	addi	r7,r7,THREAD_SIZE-INT_FRAME_SIZE

	mr	r1,r8		/* start using new stack pointer */
	std	r7,PACAKSAVE(r13)

	ld	r6,_CCR(r1)
	mtcrf	0xFF,r6

#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	ld	r0,THREAD_VRSAVE(r4)
	mtspr	SPRN_VRSAVE,r0		/* if G4, restore VRSAVE reg */
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif /* CONFIG_ALTIVEC */

	/* r3-r13 are destroyed -- Cort */
	REST_8GPRS(14, r1)
	REST_10GPRS(22, r1)

#ifdef CONFIG_PPC_ISERIES
	clrrdi	r7,r1,THREAD_SHIFT	/* get current_thread_info() */
	ld	r7,TI_FLAGS(r7)		/* Get run light flag */
	mfspr	r9,CTRLF
	srdi	r7,r7,TIF_RUN_LIGHT
	insrdi	r9,r7,1,63		/* Insert run light into CTRL */
	mtspr	CTRLT,r9
#endif

	/* convert old thread to its task_struct for return value */
	addi	r3,r3,-THREAD
	ld	r7,_NIP(r1)	/* Return to _switch caller in new task */
	mtlr	r7
	addi	r1,r1,SWITCH_FRAME_SIZE
	blr

_GLOBAL(ret_from_fork)
	bl	.schedule_tail
	clrrdi	r4,r1,THREAD_SHIFT
	ld	r4,TI_FLAGS(r4)
	andi.	r4,r4,_TIF_SYSCALL_T_OR_A
	beq+	.ret_from_except
	bl	.do_syscall_trace_leave
	b	.ret_from_except

_GLOBAL(ret_from_except)
	/*
	 * Disable interrupts so that current_thread_info()->flags
	 * can't change between when we test it and when we return
	 * from the interrupt.
	 */
	mfmsr	r10		/* Get current interrupt state */
	li	r4,0
	ori	r4,r4,MSR_EE
	andc	r9,r10,r4	/* clear MSR_EE */
	mtmsrd	r9,1		/* Update machine state */

	ld	r3,_MSR(r1)	/* Returning to user mode? */
	andi.	r3,r3,MSR_PR
	beq	restore		/* if not, just restore regs and return */

	/* Check current_thread_info()->flags */
	clrrdi	r3,r1,THREAD_SHIFT
	ld	r3,TI_FLAGS(r3)
	andi.	r0,r3,_TIF_USER_WORK_MASK
	bne	do_work

	addi	r0,r1,INT_FRAME_SIZE	/* size of frame */
	ld	r4,PACACURRENT(r13)
	std	r0,THREAD+KSP(r4)	/* save kernel stack pointer */

	/*
	 * r13 is our per cpu area, only restore it if we are returning to
	 * userspace
	 */
	REST_GPR(13,r1)

restore:
#ifdef CONFIG_PPC_ISERIES
	ld	r5,SOFTE(r1)
	mfspr	r4,SPRG3		/* get paca address */
	cmpdi	0,r5,0
	beq	4f
	/* Check for pending interrupts (iSeries) */
	/* this is CHECKANYINT except that we already have the paca address */
	ld	r3,PACALPPACA+LPPACAANYINT(r4)
	cmpdi	r3,0
	beq+	4f			/* skip do_IRQ if no interrupts */

	mfspr	r13,SPRG3		/* get paca pointer back */
	li	r3,0
	stb	r3,PACAPROCENABLED(r13)	/* ensure we are soft-disabled */
	mtmsrd	r10			/* hard-enable again */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_IRQ
	b	.ret_from_except		/* loop back and handle more */

4:	stb	r5,PACAPROCENABLED(r4)
#endif

	ld	r3,_MSR(r1)
	andi.	r3,r3,MSR_RI
	beq-	unrecov_restore

	ld	r3,_CTR(r1)
	ld	r0,_LINK(r1)
	mtctr	r3
	mtlr	r0
	ld	r3,_XER(r1)
	mtspr	XER,r3

	REST_8GPRS(5, r1)
	REST_10GPRS(14, r1)
	REST_8GPRS(24, r1)

	stdcx.	r0,0,r1		/* to clear the reservation */

	mfmsr	r0
	li	r2, MSR_RI
	andc	r0,r0,r2
	mtmsrd	r0,1

	ld	r0,_MSR(r1)
	mtspr	SRR1,r0

	ld	r2,_CCR(r1)
	mtcrf	0xFF,r2
	ld	r2,_NIP(r1)
	mtspr	SRR0,r2

	ld	r0,GPR0(r1)
	ld	r2,GPR2(r1)
	ld	r3,GPR3(r1)
	ld	r4,GPR4(r1)
	ld	r1,GPR1(r1)

	rfid

/* Note: this must change if we start using the  TIF_NOTIFY_RESUME bit */
do_work:
	/* Enable interrupts */
	mtmsrd	r10,1

	andi.	r0,r3,_TIF_NEED_RESCHED
	beq	1f
	bl	.schedule
	b	.ret_from_except

1:	andi.	r0,r3,_TIF_SIGPENDING
	beq	.ret_from_except
	li	r3,0
	addi	r4,r1,STACK_FRAME_OVERHEAD
	bl	.do_signal
	b	.ret_from_except

unrecov_restore:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.unrecoverable_exception
	b	unrecov_restore

#ifdef CONFIG_PPC_PSERIES
/*
 * On CHRP, the Run-Time Abstraction Services (RTAS) have to be
 * called with the MMU off.
 *
 * In addition, we need to be in 32b mode, at least for now.
 * 
 * Note: r3 is an input parameter to rtas, so don't trash it...
 */
_GLOBAL(enter_rtas)
	mflr	r0
	std	r0,16(r1)
        stdu	r1,-RTAS_FRAME_SIZE(r1)	/* Save SP and create stack space. */

	/* Because RTAS is running in 32b mode, it clobbers the high order half
	 * of all registers that it saves.  We therefore save those registers
	 * RTAS might touch to the stack.  (r0, r3-r13 are caller saved)
   	 */
	SAVE_GPR(2, r1)			/* Save the TOC */
	SAVE_GPR(13, r1)		/* Save paca */
	SAVE_8GPRS(14, r1)		/* Save the non-volatiles */
	SAVE_10GPRS(22, r1)		/* ditto */

	mfcr	r4
	std	r4,_CCR(r1)
	mfctr	r5
	std	r5,_CTR(r1)
	mfspr	r6,XER
	std	r6,_XER(r1)
	mfdar	r7
	std	r7,_DAR(r1)
	mfdsisr	r8
	std	r8,_DSISR(r1)
	mfsrr0	r9
	std	r9,_SRR0(r1)
	mfsrr1	r10
	std	r10,_SRR1(r1)

	/* Unfortunately, the stack pointer and the MSR are also clobbered,
	 * so they are saved in the PACA which allows us to restore
	 * our original state after RTAS returns.
         */
	std	r1,PACAR1(r13)
	mfmsr	r6
        std	r6,PACASAVEDMSR(r13)

	/* Setup our real return addr */	
	SET_REG_TO_LABEL(r4,.rtas_return_loc)
	SET_REG_TO_CONST(r9,KERNELBASE)
	sub	r4,r4,r9
       	mtlr	r4

	li	r0,0
	ori	r0,r0,MSR_EE|MSR_SE|MSR_BE|MSR_RI
	andc	r0,r6,r0
	
        li      r9,1
        rldicr  r9,r9,MSR_SF_LG,(63-MSR_SF_LG)
	ori	r9,r9,MSR_IR|MSR_DR|MSR_FE0|MSR_FE1|MSR_FP
	andc	r6,r0,r9
	ori	r6,r6,MSR_RI
	sync				/* disable interrupts so SRR0/1 */
	mtmsrd	r0			/* don't get trashed */

	SET_REG_TO_LABEL(r4,rtas)
	ld	r5,RTASENTRY(r4)	/* get the rtas->entry value */
	ld	r4,RTASBASE(r4)		/* get the rtas->base value */
	
	mtspr	SRR0,r5
	mtspr	SRR1,r6
	rfid

_STATIC(rtas_return_loc)
	/* relocation is off at this point */
	mfspr	r4,SPRG3	        /* Get PACA */
	SET_REG_TO_CONST(r5, KERNELBASE)
        sub     r4,r4,r5                /* RELOC the PACA base pointer */

	mfmsr   r6
	li	r0,MSR_RI
	andc	r6,r6,r0
	sync	
	mtmsrd  r6
        
        ld	r1,PACAR1(r4)           /* Restore our SP */
	LOADADDR(r3,.rtas_restore_regs)
        ld	r4,PACASAVEDMSR(r4)     /* Restore our MSR */

	mtspr	SRR0,r3
	mtspr	SRR1,r4
	rfid

_STATIC(rtas_restore_regs)
	/* relocation is on at this point */
	REST_GPR(2, r1)			/* Restore the TOC */
	REST_GPR(13, r1)		/* Restore paca */
	REST_8GPRS(14, r1)		/* Restore the non-volatiles */
	REST_10GPRS(22, r1)		/* ditto */

	mfspr	r13,SPRG3

	ld	r4,_CCR(r1)
	mtcr	r4
	ld	r5,_CTR(r1)
	mtctr	r5
	ld	r6,_XER(r1)
	mtspr	XER,r6
	ld	r7,_DAR(r1)
	mtdar	r7
	ld	r8,_DSISR(r1)
	mtdsisr	r8
	ld	r9,_SRR0(r1)
	mtsrr0	r9
	ld	r10,_SRR1(r1)
	mtsrr1	r10

        addi	r1,r1,RTAS_FRAME_SIZE	/* Unstack our frame */
	ld	r0,16(r1)		/* get return address */

	mtlr    r0
        blr				/* return to caller */

_GLOBAL(enter_prom)
	mflr	r0
	std	r0,16(r1)
        stdu	r1,-PROM_FRAME_SIZE(r1)	/* Save SP and create stack space */

	/* Because PROM is running in 32b mode, it clobbers the high order half
	 * of all registers that it saves.  We therefore save those registers
	 * PROM might touch to the stack.  (r0, r3-r13 are caller saved)
   	 */
	SAVE_8GPRS(2, r1)
	SAVE_GPR(13, r1)
	SAVE_8GPRS(14, r1)
	SAVE_10GPRS(22, r1)
	mfcr	r4
	std	r4,_CCR(r1)
	mfctr	r5
	std	r5,_CTR(r1)
	mfspr	r6,XER
	std	r6,_XER(r1)
	mfdar	r7
	std	r7,_DAR(r1)
	mfdsisr	r8
	std	r8,_DSISR(r1)
	mfsrr0	r9
	std	r9,_SRR0(r1)
	mfsrr1	r10
	std	r10,_SRR1(r1)
	mfmsr	r11
	std	r11,_MSR(r1)

	/* Get the PROM entrypoint */
        bl      .reloc_offset
	LOADADDR(r12,prom)
	sub	r12,r12,r3
	ld	r12,PROMENTRY(r12)
	mtlr	r12

	/* Switch MSR to 32 bits mode
	 */
        mfmsr   r11
        li      r12,1
        rldicr  r12,r12,MSR_SF_LG,(63-MSR_SF_LG)
        andc    r11,r11,r12
        li      r12,1
        rldicr  r12,r12,MSR_ISF_LG,(63-MSR_ISF_LG)
        andc    r11,r11,r12
        mtmsrd  r11
        isync

	/* Restore arguments & enter PROM here... */
	ld	r3,GPR3(r1)
	blrl

	/* Just make sure that r1 top 32 bits didn't get
	 * corrupt by OF
	 */
	rldicl	r1,r1,0,32

	/* Restore the MSR (back to 64 bits) */
	ld	r0,_MSR(r1)
	mtmsrd	r0
        isync

	/* Restore other registers */
	REST_GPR(2, r1)
	REST_GPR(13, r1)
	REST_8GPRS(14, r1)
	REST_10GPRS(22, r1)
	ld	r4,_CCR(r1)
	mtcr	r4
	ld	r5,_CTR(r1)
	mtctr	r5
	ld	r6,_XER(r1)
	mtspr	XER,r6
	ld	r7,_DAR(r1)
	mtdar	r7
	ld	r8,_DSISR(r1)
	mtdsisr	r8
	ld	r9,_SRR0(r1)
	mtsrr0	r9
	ld	r10,_SRR1(r1)
	mtsrr1	r10
	
        addi	r1,r1,PROM_FRAME_SIZE
	ld	r0,16(r1)
	mtlr    r0
        blr
	
#endif	/* defined(CONFIG_PPC_PSERIES) */
