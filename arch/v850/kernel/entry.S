/*
 * arch/v850/kernel/entry.S -- Low-level system-call handling, trap handlers,
 *	and context-switching
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/sys.h>

#include <asm/entry.h>
#include <asm/current.h>
#include <asm/thread_info.h>
#include <asm/clinkage.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/errno.h>

#include <asm/asm-consts.h>


/* Make a slightly more convenient alias for C_SYMBOL_NAME.  */
#define CSYM	C_SYMBOL_NAME


/* The offset of the struct pt_regs in a `state save frame' on the stack.  */
#define PTO	STATE_SAVE_PT_OFFSET


/* Save argument registers to the struct pt_regs pointed to by EP.  */
#define SAVE_ARG_REGS							      \
	sst.w	r6, PTO+PT_GPR(6)[ep];					      \
	sst.w	r7, PTO+PT_GPR(7)[ep];					      \
	sst.w	r8, PTO+PT_GPR(8)[ep];					      \
	sst.w	r9, PTO+PT_GPR(9)[ep];
/* Restore argument registers from the struct pt_regs pointed to by EP.  */
#define RESTORE_ARG_REGS						      \
	sld.w	PTO+PT_GPR(6)[ep], r6;					      \
	sld.w	PTO+PT_GPR(7)[ep], r7;					      \
	sld.w	PTO+PT_GPR(8)[ep], r8;					      \
	sld.w	PTO+PT_GPR(9)[ep], r9;

/* Save value return registers to the struct pt_regs pointed to by EP.  */
#define SAVE_RVAL_REGS							      \
	sst.w	r10, PTO+PT_GPR(10)[ep];				      \
	sst.w	r11, PTO+PT_GPR(11)[ep];
/* Restore value return registers from the struct pt_regs pointed to by EP.  */
#define RESTORE_RVAL_REGS						      \
	sld.w	PTO+PT_GPR(10)[ep], r10;				      \
	sld.w	PTO+PT_GPR(11)[ep], r11;


#define SAVE_CALL_CLOBBERED_REGS_BEFORE_ARGS				      \
	sst.w	r1, PTO+PT_GPR(1)[ep];					      \
	sst.w	r5, PTO+PT_GPR(5)[ep];
#define SAVE_CALL_CLOBBERED_REGS_AFTER_RVAL				      \
	sst.w	r12, PTO+PT_GPR(12)[ep];				      \
	sst.w	r13, PTO+PT_GPR(13)[ep];				      \
	sst.w	r14, PTO+PT_GPR(14)[ep];				      \
	sst.w	r15, PTO+PT_GPR(15)[ep];				      \
	sst.w	r16, PTO+PT_GPR(16)[ep];				      \
	sst.w	r17, PTO+PT_GPR(17)[ep];				      \
	sst.w	r18, PTO+PT_GPR(18)[ep];				      \
	sst.w	r19, PTO+PT_GPR(19)[ep];
#define RESTORE_CALL_CLOBBERED_REGS_BEFORE_ARGS				      \
	sld.w	PTO+PT_GPR(1)[ep], r1;					      \
	sld.w	PTO+PT_GPR(5)[ep], r5;
#define RESTORE_CALL_CLOBBERED_REGS_AFTER_RVAL				      \
	sld.w	PTO+PT_GPR(12)[ep], r12;				      \
	sld.w	PTO+PT_GPR(13)[ep], r13;				      \
	sld.w	PTO+PT_GPR(14)[ep], r14;				      \
	sld.w	PTO+PT_GPR(15)[ep], r15;				      \
	sld.w	PTO+PT_GPR(16)[ep], r16;				      \
	sld.w	PTO+PT_GPR(17)[ep], r17;				      \
	sld.w	PTO+PT_GPR(18)[ep], r18;				      \
	sld.w	PTO+PT_GPR(19)[ep], r19;

/* Save `call clobbered' registers to the struct pt_regs pointed to by EP.  */
#define SAVE_CALL_CLOBBERED_REGS					      \
	SAVE_CALL_CLOBBERED_REGS_BEFORE_ARGS;				      \
	SAVE_ARG_REGS;							      \
	SAVE_RVAL_REGS;							      \
	SAVE_CALL_CLOBBERED_REGS_AFTER_RVAL;
/* Restore `call clobbered' registers from the struct pt_regs pointed to
   by EP.  */
#define RESTORE_CALL_CLOBBERED_REGS					      \
	RESTORE_CALL_CLOBBERED_REGS_BEFORE_ARGS;			      \
	RESTORE_ARG_REGS;						      \
	RESTORE_RVAL_REGS;						      \
	RESTORE_CALL_CLOBBERED_REGS_AFTER_RVAL;

/* Save `call clobbered' registers except for the return-value registers
   to the struct pt_regs pointed to by EP.  */
#define SAVE_CALL_CLOBBERED_REGS_NO_RVAL				      \
	SAVE_CALL_CLOBBERED_REGS_BEFORE_ARGS;				      \
	SAVE_ARG_REGS;							      \
	SAVE_CALL_CLOBBERED_REGS_AFTER_RVAL;
/* Restore `call clobbered' registers except for the return-value registers
   from the struct pt_regs pointed to by EP.  */
#define RESTORE_CALL_CLOBBERED_REGS_NO_RVAL				      \
	RESTORE_CALL_CLOBBERED_REGS_BEFORE_ARGS;			      \
	RESTORE_ARG_REGS;						      \
	RESTORE_CALL_CLOBBERED_REGS_AFTER_RVAL;

/* Zero `call clobbered' registers except for the return-value registers.  */
#define ZERO_CALL_CLOBBERED_REGS_NO_RVAL				      \
	mov	r0, r1;   mov	r0, r5;					      \
	mov	r0, r12;  mov	r0, r13;  mov	r0, r14;  mov	r0, r15;      \
	mov	r0, r16;  mov	r0, r17;  mov	r0, r18;  mov	r0, r19;

/* Save `call saved' registers to the struct pt_regs pointed to by EP.  */
#define SAVE_CALL_SAVED_REGS						      \
	sst.w	r2, PTO+PT_GPR(2)[ep];					      \
	sst.w	r20, PTO+PT_GPR(20)[ep];				      \
	sst.w	r21, PTO+PT_GPR(21)[ep];				      \
	sst.w	r22, PTO+PT_GPR(22)[ep];				      \
	sst.w	r23, PTO+PT_GPR(23)[ep];				      \
	sst.w	r24, PTO+PT_GPR(24)[ep];				      \
	sst.w	r25, PTO+PT_GPR(25)[ep];				      \
	sst.w	r26, PTO+PT_GPR(26)[ep];				      \
	sst.w	r27, PTO+PT_GPR(27)[ep];				      \
	sst.w	r28, PTO+PT_GPR(28)[ep];				      \
	sst.w	r29, PTO+PT_GPR(29)[ep];
/* Restore `call saved' registers from the struct pt_regs pointed to by EP.  */
#define RESTORE_CALL_SAVED_REGS						      \
	sld.w	PTO+PT_GPR(2)[ep], r2;					      \
	sld.w	PTO+PT_GPR(20)[ep], r20;				      \
	sld.w	PTO+PT_GPR(21)[ep], r21;				      \
	sld.w	PTO+PT_GPR(22)[ep], r22;				      \
	sld.w	PTO+PT_GPR(23)[ep], r23;				      \
	sld.w	PTO+PT_GPR(24)[ep], r24;				      \
	sld.w	PTO+PT_GPR(25)[ep], r25;				      \
	sld.w	PTO+PT_GPR(26)[ep], r26;				      \
	sld.w	PTO+PT_GPR(27)[ep], r27;				      \
	sld.w	PTO+PT_GPR(28)[ep], r28;				      \
	sld.w	PTO+PT_GPR(29)[ep], r29;


/* Save system registers to the struct pt_regs pointed to by REG.  
   r19 is clobbered.  */
#define SAVE_SYS_REGS							      \
	stsr	SR_EIPC, r19;	/* user's PC, before interrupt */	      \
	sst.w	r19, PTO+PT_PC[ep];					      \
	stsr	SR_EIPSW, r19;	/* & PSW (XXX save this?) */		      \
	sst.w	r19, PTO+PT_PSW[ep];					      \
	stsr	SR_CTPC, r19;	/* (XXX maybe not used in kernel?) */	      \
	sst.w	r19, PTO+PT_CTPC[ep];					      \
	stsr	SR_CTPSW, r19;	/* " */					      \
	sst.w	r19, PTO+PT_CTPSW[ep];					      \
	stsr	SR_CTBP, r19;	/* " */					      \
	sst.w	r19, PTO+PT_CTBP[ep];
/* Restore system registers from the struct pt_regs pointed to by EP.  
   LP is clobbered (it is used as a scratch register because the POP_STATE
   macro restores it, and this macro is usually used inside POP_STATE).  */
#define RESTORE_SYS_REGS						      \
	sld.w	PTO+PT_PC[ep], lp;					      \
	ldsr	lp, SR_EIPC;	/* user's PC, before interrupt */	      \
	sld.w	PTO+PT_PSW[ep], lp;					      \
	ldsr	lp, SR_EIPSW;	/* & PSW (XXX save this?) */		      \
	sld.w	PTO+PT_CTPC[ep], lp;					      \
	ldsr	lp, SR_CTPC;	/* (XXX maybe not used in kernel?) */	      \
	sld.w	PTO+PT_CTPSW[ep], lp;					      \
	ldsr	lp, SR_CTPSW;	/* " */					      \
	sld.w	PTO+PT_CTBP[ep], lp;					      \
	ldsr	lp, SR_CTBP;	/* " */


/* Save system registers to the struct pt_regs pointed to by REG.  This is a
   NMI-specific version, because NMIs save the PC/PSW in a different place
   than other interrupt requests.  r19 is clobbered.  */
#define SAVE_SYS_REGS_FOR_NMI						      \
	stsr	SR_FEPC, r19;	/* user's PC, before NMI */		      \
	sst.w	r19, PTO+PT_PC[ep];					      \
	stsr	SR_FEPSW, r19;	/* & PSW (XXX save this?) */		      \
	sst.w	r19, PTO+PT_PSW[ep];					      \
	stsr	SR_CTPC, r19;	/* (XXX maybe not used in kernel?) */	      \
	sst.w	r19, PTO+PT_CTPC[ep];					      \
	stsr	SR_CTPSW, r19;	/* " */					      \
	sst.w	r19, PTO+PT_CTPSW[ep];					      \
	stsr	SR_CTBP, r19;	/* " */					      \
	sst.w	r19, PTO+PT_CTBP[ep];
/* Restore system registers from the struct pt_regs pointed to by EP.  This is
   a NMI-specific version, because NMIs save the PC/PSW in a different place
   than other interrupt requests.  LP is clobbered (it is used as a scratch
   register because the POP_STATE macro restores it, and this macro is usually
   used inside POP_STATE).  */
#define RESTORE_SYS_REGS_FOR_NMI					      \
	ldsr	lp, SR_FEPC;	/* user's PC, before NMI */		      \
	sld.w	PTO+PT_PC[ep], lp;					      \
	ldsr	lp, SR_FEPSW;	/* & PSW (XXX save this?) */		      \
	sld.w	PTO+PT_PSW[ep], lp;					      \
	ldsr	lp, SR_CTPC;	/* (XXX maybe not used in kernel?) */	      \
	sld.w	PTO+PT_CTPC[ep], lp;					      \
	ldsr	lp, SR_CTPSW;	/* " */					      \
	sld.w	PTO+PT_CTPSW[ep], lp;					      \
	ldsr	lp, SR_CTBP;	/* " */					      \
	sld.w	PTO+PT_CTBP[ep], lp;


/* Push register state, except for the stack pointer, on the stack in the form
   of a struct pt_regs, in preparation for a system call.  This macro makes
   sure that `special' registers, system registers; TYPE identifies the set of
   extra registers to be saved as well.  EP is clobbered.  */
#define PUSH_STATE(type)						      \
	addi	-STATE_SAVE_SIZE, sp, sp; /* Make room on the stack.  */      \
	st.w	ep, PTO+PT_GPR(GPR_EP)[sp];				      \
	mov	sp, ep;							      \
	sst.w	gp, PTO+PT_GPR(GPR_GP)[ep];				      \
	sst.w	lp, PTO+PT_GPR(GPR_LP)[ep];				      \
	type ## _STATE_SAVER;
/* Pop a register state, except for the stack pointer, from the struct pt_regs
   on the stack.  */
#define POP_STATE(type)							      \
	mov	sp, ep;							      \
	type ## _STATE_RESTORER;					      \
	sld.w	PTO+PT_GPR(GPR_GP)[ep], gp;				      \
	sld.w	PTO+PT_GPR(GPR_LP)[ep], lp;				      \
	sld.w	PTO+PT_GPR(GPR_EP)[ep], ep;				      \
	addi	STATE_SAVE_SIZE, sp, sp; /* Clean up our stack space.  */


/* Switch to the kernel stack if necessary, and push register state on
   the stack in the form of a struct pt_regs.  Also load the current
   task pointer if switching from user mode.  The stack-pointer (r3)
   should have already been saved to the memory location SP_SAVE_LOC
   (the reason for this is that the interrupt vectors may be beyond a
   22-bit signed offset jump from the actual interrupt handler, and this
   allows them to save the stack-pointer and use that register to do an
   indirect jump).  This macro makes sure that `special' registers,
   system registers, and the stack pointer are saved; TYPE identifies
   the set of extra registers to be saved as well.  SYSCALL_NUM is the
   register in which the system-call number this state is for is stored
   (r0 if this isn't a system call).  Interrupts should already be
   disabled when calling this.  */
#define SAVE_STATE(type, syscall_num, sp_save_loc)			      \
        tst1	0, KM;			/* See if already in kernel mode.  */ \
	bz	1f;							      \
        /* Kernel-mode state save.  */					      \
	ld.w	sp_save_loc, sp;	/* Reload kernel stack-pointer.  */   \
	st.w	sp, (PT_GPR(GPR_SP)-PT_SIZE)[sp]; /* Save original SP. */     \
        PUSH_STATE(type);						      \
	mov	1, r19;			/* Was in kernel-mode.  */	      \
        sst.w	r19, PTO+PT_KERNEL_MODE[ep]; /* [ep is set by PUSH_STATE] */  \
        br	2f;							      \
1:      /* User-mode state save.  */					      \
        ld.w    KSP, sp;		/* Switch to kernel stack.  */	      \
        PUSH_STATE(type);						      \
        sst.w	r0, PTO+PT_KERNEL_MODE[ep]; /* Was in user-mode.  */	      \
        ld.w    sp_save_loc, r19;				              \
	sst.w	r19, PTO+PT_GPR(GPR_SP)[ep]; /* Store user SP.  */	      \
	mov	1, r19;							      \
	st.b	r19, KM;		/* Now we're in kernel-mode.  */      \
	GET_CURRENT_TASK(CURRENT_TASK);	/* Fetch the current task pointer. */ \
2:      /* Save away the syscall number.  */				      \
        sst.w	syscall_num, PTO+PT_SYSCALL[ep]


/* Save register state not normally saved by PUSH_STATE for TYPE.  */
#define SAVE_EXTRA_STATE(type)						      \
        mov	sp, ep;							      \
	type ## _EXTRA_STATE_SAVER;
/* Restore register state not normally restored by POP_STATE for TYPE.  */
#define RESTORE_EXTRA_STATE(type)					      \
        mov	sp, ep;							      \
	type ## _EXTRA_STATE_RESTORER;

/* Save any call-clobbered registers not normally saved by PUSH_STATE
   for TYPE.  */
#define SAVE_EXTRA_STATE_FOR_FUNCALL(type)				      \
        mov	sp, ep;							      \
	type ## _FUNCALL_EXTRA_STATE_SAVER;
/* Restore any call-clobbered registers not normally restored by POP_STATE for
   TYPE.  */
#define RESTORE_EXTRA_STATE_FOR_FUNCALL(type)				      \
        mov	sp, ep;							      \
	type ## _FUNCALL_EXTRA_STATE_RESTORER;


/* These are extra_state_saver/restorer values for a user trap.  Note that we
   save the argument registers so that restarted syscalls will function
   properly (otherwise it wouldn't be necessary), and we must _not_ restore
   the return-value registers (so that traps can return a value!), but there
   are various options for what happens to other call-clobbered registers,
   selected by preprocessor conditionals.  */

#if TRAPS_PRESERVE_CALL_CLOBBERED_REGS
   
/* Traps save/restore all call-clobbered registers (except for rval regs).  */
#define TRAP_STATE_SAVER						      \
     SAVE_CALL_CLOBBERED_REGS_NO_RVAL;					      \
     SAVE_SYS_REGS
#define TRAP_STATE_RESTORER						      \
     RESTORE_CALL_CLOBBERED_REGS_NO_RVAL;				      \
     RESTORE_SYS_REGS

#else /* !TRAPS_PRESERVE_CALL_CLOBBERED_REGS */

/* Traps don't save call-clobbered registers (but do still save arg regs).  */
#define TRAP_STATE_SAVER						      \
     SAVE_ARG_REGS;							      \
     SAVE_SYS_REGS

#if TRAPS_ZERO_CALL_CLOBBERED_REGS
   
/* Traps zero call-clobbered registers (except for arg/rval regs) before
   returning from a system call, to avoid any internal values from leaking out
   of the kernel.  */
#define TRAP_STATE_RESTORER						      \
     ZERO_CALL_CLOBBERED_REGS_NO_ARGS_NO_RVAL;				      \
     RESTORE_ARG_REGS;							      \
     RESTORE_SYS_REGS

#else /* !TRAPS_ZERO_CALL_CLOBBERED_REGS */
   
/* When traps return, they just leave call-clobbered registers (except for arg
   regs) with whatever value they have from the kernel.  */
#define TRAP_STATE_RESTORER						      \
     RESTORE_ARG_REGS;							      \
     RESTORE_SYS_REGS

#endif /* TRAPS_ZERO_CALL_CLOBBERED_REGS */
#endif /* TRAPS_PRESERVE_CALL_CLOBBERED_REGS */

/* Save registers not normally saved by traps.  */
#define TRAP_EXTRA_STATE_SAVER						      \
   SAVE_RVAL_REGS;							      \
   SAVE_CALL_SAVED_REGS
#define TRAP_EXTRA_STATE_RESTORER					      \
   RESTORE_RVAL_REGS;							      \
   RESTORE_CALL_SAVED_REGS
#define TRAP_FUNCALL_EXTRA_STATE_SAVER					      \
   SAVE_RVAL_REGS
#define TRAP_FUNCALL_EXTRA_STATE_RESTORER				      \
   RESTORE_RVAL_REGS


/* Register saving/restoring for maskable interrupts.  */
#define IRQ_STATE_SAVER							      \
   SAVE_CALL_CLOBBERED_REGS;						      \
   SAVE_SYS_REGS
#define IRQ_STATE_RESTORER						      \
   RESTORE_CALL_CLOBBERED_REGS;						      \
   RESTORE_SYS_REGS
#define IRQ_EXTRA_STATE_SAVER						      \
   SAVE_CALL_SAVED_REGS
#define IRQ_EXTRA_STATE_RESTORER					      \
   RESTORE_CALL_SAVED_REGS
#define IRQ_FUNCALL_EXTRA_STATE_SAVER       /* nothing */
#define IRQ_FUNCALL_EXTRA_STATE_RESTORER    /* nothing */

/* Register saving/restoring for non-maskable interrupts.  */
#define NMI_STATE_SAVER							      \
   SAVE_CALL_CLOBBERED_REGS;						      \
   SAVE_SYS_REGS_FOR_NMI
#define NMI_STATE_RESTORER						      \
   RESTORE_CALL_CLOBBERED_REGS;						      \
   RESTORE_SYS_REGS_FOR_NMI
#define NMI_EXTRA_STATE_SAVER						      \
   SAVE_CALL_SAVED_REGS
#define NMI_EXTRA_STATE_RESTORER					      \
   RESTORE_CALL_SAVED_REGS
#define NMI_FUNCALL_EXTRA_STATE_SAVER       /* nothing */
#define NMI_FUNCALL_EXTRA_STATE_RESTORER    /* nothing */
   
/* Register saving/restoring for a context switch.  We don't need to save too
   many registers, because context-switching looks like a function call (via
   the function `switch_thread'), so callers will save any call-clobbered
   registers themselves.  The stack pointer and return value are handled by
   switch_thread itself.  */
#define SWITCH_STATE_SAVER						      \
   SAVE_CALL_SAVED_REGS
#define SWITCH_STATE_RESTORER						      \
   RESTORE_CALL_SAVED_REGS


/* Restore register state from the struct pt_regs on the stack, switch back
   to the user stack if necessary, and return from the trap/interrupt.
   EXTRA_STATE_RESTORER is a sequence of assembly language statements to
   restore anything not restored by this macro.  Only registers not saved by
   the C compiler are restored (that is, R3(sp), R4(gp), R31(lp), and
   anything restored by EXTRA_STATE_RESTORER).  */
#define RETURN(type)							      \
        ld.b	PTO+PT_KERNEL_MODE[sp], r19;				      \
	di;				/* Disable interrupts */	      \
	cmp	r19, r0;		/* See if returning to kernel mode, */\
	bne	2f;			/* ... if so, skip resched &c.  */    \
									      \
	/* We're returning to user mode, so check for various conditions that \
	   trigger rescheduling. */					      \
	GET_CURRENT_THREAD(r18);					      \
	ld.w	TI_FLAGS[r18], r19;					      \
	andi	_TIF_NEED_RESCHED, r19, r0;				      \
	bnz	3f;			/* Call the scheduler.  */	      \
	andi	_TIF_SIGPENDING, r19, r0;				      \
	bnz	4f;			/* Signals to handle, handle them */  \
									      \
/* Finally, return to user state.  */					      \
1:	st.b	r0, KM;			/* Now officially in user state. */   \
	POP_STATE(type);						      \
	st.w	sp, KSP;		/* Save the kernel stack pointer. */  \
	ld.w    PT_GPR(GPR_SP)-PT_SIZE[sp], sp;				      \
					/* Restore user stack pointer. */     \
	reti;								      \
									      \
/* Return to kernel state.  */						      \
2:	POP_STATE(type);						      \
	reti;								      \
									      \
/* Call the scheduler before returning from a syscall/trap. */		      \
3:	SAVE_EXTRA_STATE_FOR_FUNCALL(type); /* Prepare for funcall. */	      \
	jarl	CSYM(schedule), lp;	/* Call scheduler */		      \
	di;				/* The scheduler enables interrupts */\
	RESTORE_EXTRA_STATE_FOR_FUNCALL(type);				      \
	br	1b;							      \
									      \
/* Handle a signal return.  */						      \
4:      /* Not all registers are saved by the normal trap/interrupt entry     \
	   points (for instance, call-saved registers (because the normal     \
	   C-compiler calling sequence in the kernel makes sure they're	      \
	   preserved), and call-clobbered registers in the case of	      \
	   traps), but signal handlers may want to examine or change the      \
	   complete register state.  Here we save anything not saved by	      \
	   the normal entry sequence, so that it may be safely restored	      \
	   (in a possibly modified form) after do_signal returns.  */	      \
        SAVE_EXTRA_STATE(type)		/* Save state not saved by entry. */  \
	movea	PTO, sp, r6;		/* Arg 1: struct pt_regs *regs */     \
	mov	r0, r7;			/* Arg 2: sigset_t *oldset */	      \
	jarl	CSYM(do_signal), lp;	/* Handle any signals */	      \
	di;				/* sig handling enables interrupts */ \
        RESTORE_EXTRA_STATE(type);	/* Restore extra regs.  */	      \
	br	1b;


/* Jump to the appropriate function for the system call number in r12
   (r12 is not preserved), or return an error if r12 is not valid.  The
   LP register should point to the location where the called function
   should return.  [note that MAKE_SYS_CALL uses label 1]  */
#define MAKE_SYS_CALL							      \
	/* See if the system call number is valid.  */			      \
	addi	-NR_syscalls, r12, r0;					      \
	bnh	1f;							      \
	/* Figure out which function to use for this system call.  */	      \
	shl	2, r12;							      \
	mov	hilo(CSYM(sys_call_table)), r19;			      \
	add	r19, r12;						      \
	ld.w	0[r12], r12;						      \
	/* Make the system call.  */					      \
	jmp	[r12];							      \
	/* The syscall number is invalid, return an error.  */		      \
1:	addi	-ENOSYS, r0, r10;					      \
	jmp	[lp];


	.text

/*
 * User trap.
 *
 * Trap 0 system calls are also handled here.
 *
 * The stack-pointer (r3) should have already been saved to the memory
 * location ENTRY_SP (the reason for this is that the interrupt vectors may be
 * beyond a 22-bit signed offset jump from the actual interrupt handler, and
 * this allows them to save the stack-pointer and use that register to do an
 * indirect jump).
 *	
 * Syscall protocol:
 *   Syscall number in r12, args in r6-r9
 *   Return value in r10
 */
G_ENTRY(trap):
	SAVE_STATE (TRAP, r12, ENTRY_SP) // Save registers. 
	stsr	SR_ECR, r19		// Find out which trap it was.
	ei				// Enable interrupts.
	mov	hilo(ret_from_trap), lp	// where the trap should return

	// The following two shifts (1) clear out extraneous NMI data in the
	// upper 16-bits, (2) convert the 0x40 - 0x5f range of trap ECR
	// numbers into the (0-31) << 2 range we want, (3) set the flags.
	shl	27, r19			// chop off all high bits
	shr	25, r19			// scale back down and then << 2
	bnz	2f			// See if not trap 0. 

	// Trap 0 is a `short' system call, skip general trap table. 
	MAKE_SYS_CALL			// Jump to the syscall function. 

2:	// For other traps, use a table lookup. 
	mov	hilo(CSYM(trap_table)), r18
	add	r19, r18
	ld.w	0[r18], r18
	jmp	[r18]			// Jump to the trap handler.
END(trap)

/* This is just like ret_from_trap, but first restores extra registers
   saved by some wrappers.  */
L_ENTRY(restore_extra_regs_and_ret_from_trap):
	RESTORE_EXTRA_STATE(TRAP)
	// fall through
END(restore_extra_regs_and_ret_from_trap)

/* Entry point used to return from a syscall/trap.  */
L_ENTRY(ret_from_trap):
	RETURN(TRAP)
END(ret_from_trap)

/* This the initial entry point for a new child thread, with an appropriate
   stack in place that makes it look the the child is in the middle of an
   syscall.  This function is actually `returned to' from switch_thread
   (copy_thread makes ret_from_fork the return address in each new thread's
   saved context).  */
C_ENTRY(ret_from_fork):
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
	mov	r10, r6			// switch_thread returns the prev task.
	jarl	CSYM(schedule_tail), lp	// ...which is schedule_tail's arg
#endif
	mov	r0, r10			// Child's fork call should return 0.
	br	ret_from_trap		// Do normal trap return.
C_END(ret_from_fork)


/*
 * Trap 1: `long' system calls
 * `Long' syscall protocol:
 *   Syscall number in r12, args in r6-r9, r13-r14
 *   Return value in r10
 */
L_ENTRY(syscall_long):
	// Push extra arguments on the stack.  Note that by default, the trap
	// handler reserves enough stack space for 6 arguments, so we don't
	// have to make any additional room.
	st.w	r13, 16[sp]		// arg 5
	st.w	r14, 20[sp]		// arg 6

#if !TRAPS_PRESERVE_CALL_CLOBBERED_REGS
	// Make sure r13 and r14 are preserved, in case we have to restart a
	// system call because of a signal (ep has already been set by caller).
	st.w	r13, PTO+PT_GPR(13)[sp]
	st.w	r14, PTO+PT_GPR(13)[sp]
	mov	hilo(ret_from_long_syscall), lp
#endif /* !TRAPS_PRESERVE_CALL_CLOBBERED_REGS */

	MAKE_SYS_CALL			// Jump to the syscall function.
END(syscall_long)	

#if !TRAPS_PRESERVE_CALL_CLOBBERED_REGS
/* Entry point used to return from a long syscall.  Only needed to restore
   r13/r14 if the general trap mechanism doesnt' do so.  */
L_ENTRY(ret_from_long_syscall):
	ld.w	PTO+PT_GPR(13)[sp], r13 // Restore the extra registers
	ld.w	PTO+PT_GPR(13)[sp], r14
	br	ret_from_trap		// The rest is the same as other traps
END(ret_from_long_syscall)
#endif /* !TRAPS_PRESERVE_CALL_CLOBBERED_REGS */


/* These syscalls need access to the struct pt_regs on the stack, so we
   implement them in assembly (they're basically all wrappers anyway).  */

L_ENTRY(sys_fork_wrapper):
#ifdef CONFIG_MMU
	// Save state not saved by entry.  This is actually slight overkill;
	// it's actually only necessary to save any state restored by
	// switch_thread that's not saved by the trap entry.
	SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	addi	SIGCHLD, r0, r6		// Arg 0: flags
	ld.w	PTO+PT_GPR(GPR_SP)[sp], r7 // Arg 1: child SP (use parent's)
	movea	PTO, sp, r8		// Arg 2: parent context
	jr	CSYM(fork_common)	// Do real work (tail-call).
#else
	// fork almost works, enough to trick you into looking elsewhere :-(
	addi	-EINVAL, r0, r10
	jmp	[lp]
#endif
END(sys_fork_wrapper)

L_ENTRY(sys_vfork_wrapper):
	// Save state not saved by entry.  This is actually slight overkill;
	// it's actually only necessary to save any state restored by
	// switch_thread that's not saved by the trap entry.
	SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	addi	CLONE_VFORK | CLONE_VM | SIGCHLD, r0, r6 // Arg 0: flags
	ld.w	PTO+PT_GPR(GPR_SP)[sp], r7 // Arg 1: child SP (use parent's)
	movea	PTO, sp, r8		// Arg 2: parent context
	jr	CSYM(fork_common)	// Do real work (tail-call).
END(sys_vfork_wrapper)

L_ENTRY(sys_clone_wrapper):
	// Save state not saved by entry.  This is actually slight overkill;
	// it's actually only necessary to save any state restored by
	// switch_thread that's not saved by the trap entry.
	SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	ld.w	PTO+PT_GPR(GPR_SP)[sp], r19 // parent's stack pointer
        cmp	r7, r0			// See if child SP arg (arg 1) is 0.
	cmov	z, r19, r7, r7		// ... and use the parent's if so. 
	movea	PTO, sp, r8		// Arg 2: parent context
	jr	CSYM(fork_common)	// Do real work (tail-call).
END(sys_clone_wrapper)

L_ENTRY(sys_execve_wrapper):
	movea	PTO, sp, r9		// add user context as 4th arg
	jr	CSYM(sys_execve)	// Do real work (tail-call).
END(sys_execve_wrapper)

L_ENTRY(sys_sigsuspend_wrapper):
        SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	movea	PTO, sp, r7		// add user context as 2nd arg
	jarl	CSYM(sys_sigsuspend), lp// Do real work.
	br	restore_extra_regs_and_ret_from_trap
END(sys_sigsuspend_wrapper)
L_ENTRY(sys_rt_sigsuspend_wrapper):
        SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	movea	PTO, sp, r8		// add user context as 3rd arg
	jarl	CSYM(sys_rt_sigsuspend), lp	// Do real work.
	br	restore_extra_regs_and_ret_from_trap
END(sys_rt_sigsuspend_wrapper)

L_ENTRY(sys_sigreturn_wrapper):
        SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	movea	PTO, sp, r6		// add user context as 1st arg
	jarl	CSYM(sys_sigreturn), lp	// Do real work.
	br	restore_extra_regs_and_ret_from_trap
END(sys_sigreturn_wrapper)
L_ENTRY(sys_rt_sigreturn_wrapper):
        SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	movea	PTO, sp, r6		// add user context as 1st arg
	jarl	CSYM(sys_rt_sigreturn), lp	// Do real work.
	br	restore_extra_regs_and_ret_from_trap
END(sys_rt_sigreturn_wrapper)


/*
 * Hardware maskable interrupts.
 *
 * The stack-pointer (r3) should have already been saved to the memory
 * location ENTRY_SP (the reason for this is that the interrupt vectors may be
 * beyond a 22-bit signed offset jump from the actual interrupt handler, and
 * this allows them to save the stack-pointer and use that register to do an
 * indirect jump).
 */
G_ENTRY(irq):
	SAVE_STATE (IRQ, r0, ENTRY_SP)	// Save registers. 

	stsr	SR_ECR, r6		// Find out which interrupt it was.
	movea	PTO, sp, r7		// User regs are arg2

	// All v850 implementations I know about encode their interrupts as
	// multiples of 0x10, starting at 0x80 (after NMIs and software
	// interrupts).  Convert this number into a simple IRQ index for the
	// rest of the kernel.  We also clear the upper 16 bits, which hold
	// NMI info, and don't appear to be cleared when a NMI returns.
	shl	16, r6			// clear upper 16 bits
	shr	20, r6			// shift back, and remove lower nibble
	add	-8, r6			// remove bias for irqs

	// If the IRQ index is negative, it's actually one of the exception
	// traps below 0x80 (currently, the illegal instruction trap, and
	// the `debug trap').  Handle these separately.
	bn	exception

	// Call the high-level interrupt handling code.
	jarl	CSYM(handle_irq), lp
	// fall through

/* Entry point used to return from an interrupt (also used by exception
   handlers, below).  */
ret_from_irq:
	RETURN(IRQ)

exception:
	mov	hilo(ret_from_irq), lp	// where the handler should return
	
	cmp	-2, r6
	bne	1f
	// illegal instruction exception
	addi	SIGILL, r0, r6		// Arg 0: signal number
	mov	CURRENT_TASK, r7	// Arg 1: task
	jr	CSYM(force_sig)		// tail call

1:	cmp	-1, r6
	bne	bad_trap_wrapper
	// `dbtrap' exception
	movea	PTO, sp, r6		// Arg 0: user regs
	jr	CSYM(debug_trap)	// tail call

END(irq)


/*
 * Hardware non-maskable interrupts.
 *
 * The stack-pointer (r3) should have already been saved to the memory
 * location ENTRY_SP (the reason for this is that the interrupt vectors may be
 * beyond a 22-bit signed offset jump from the actual interrupt handler, and
 * this allows them to save the stack-pointer and use that register to do an
 * indirect jump).
 */
G_ENTRY(nmi):
	SAVE_STATE (NMI, r0, NMI_ENTRY_SP); /* Save registers.  */

	stsr	SR_ECR, r6;		/* Find out which nmi it was.  */
	shr	20, r6;			/* Extract NMI code in bits 20-24. */
	movea	PTO, sp, r7;		/* User regs are arg2.  */

	/* Non-maskable interrupts always lie right after maskable interrupts.
	   Call the generic IRQ handler, with two arguments, the IRQ number,
	   and a pointer to the user registers, to handle the specifics.
	   (we subtract one because the first NMI has code 1).  */
	addi	FIRST_NMI - 1, r6, r6;
	jarl	CSYM(handle_irq), lp

	RETURN(NMI)
END(nmi)


/*
 * Trap with no handler
 */
L_ENTRY(bad_trap_wrapper):
	mov	r19, r6			// Arg 0: trap number
	movea	PTO, sp, r7		// Arg 1: user regs
	jr	CSYM(bad_trap)		// tail call handler
END(bad_trap_wrapper)


/*
 * This is where we switch between two threads.  The arguments are:
 *   r6 -- pointer to the struct thread for the `current' process
 *   r7 -- pointer to the struct thread for the `new' process.  
 * when this function returns, it will return to the new thread.
 */
C_ENTRY(switch_thread):
	// Return the previous task (r10 is not clobbered by restore below)
	mov	CURRENT_TASK, r10
	// First, push the current processor state on the stack
	PUSH_STATE(SWITCH)
	// Now save the location of the kernel stack pointer for this thread;
	// since we've pushed all other state on the stack, this is enough to
	// restore it all later.
	st.w	sp, THREAD_KSP[r6]
	// Now restore the stack pointer from the new process
	ld.w	THREAD_KSP[r7], sp
	// ... and restore all state from that
	POP_STATE(SWITCH)
	// Update the current task pointer
	GET_CURRENT_TASK(CURRENT_TASK)
	// Now return into the new thread
	jmp	[lp]
C_END(switch_thread)


	.data

	.align 4
C_DATA(trap_table):
	.long bad_trap_wrapper		// trap 0, doesn't use trap table. 
	.long syscall_long		// trap 1, `long' syscall. 
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
C_END(trap_table)


	.section .rodata
	
	.align 4
C_DATA(sys_call_table):
	.long CSYM(sys_restart_syscall)	// 0
	.long CSYM(sys_exit)
	.long sys_fork_wrapper
	.long CSYM(sys_read)
	.long CSYM(sys_write)
	.long CSYM(sys_open)		// 5
	.long CSYM(sys_close)
	.long CSYM(sys_waitpid)
	.long CSYM(sys_creat)
	.long CSYM(sys_link)
	.long CSYM(sys_unlink)		// 10
	.long sys_execve_wrapper
	.long CSYM(sys_chdir)
	.long CSYM(sys_time)
	.long CSYM(sys_mknod)
	.long CSYM(sys_chmod)		// 15
	.long CSYM(sys_chown)
	.long CSYM(sys_ni_syscall)	// was: break
	.long CSYM(sys_ni_syscall)	// was: oldstat (aka stat)
	.long CSYM(sys_lseek)
	.long CSYM(sys_getpid)		// 20
	.long CSYM(sys_mount)
	.long CSYM(sys_oldumount)
	.long CSYM(sys_setuid)
	.long CSYM(sys_getuid)
	.long CSYM(sys_stime)		// 25
	.long CSYM(sys_ptrace)
	.long CSYM(sys_alarm)
	.long CSYM(sys_ni_syscall)	// was: oldfstat (aka fstat)
	.long CSYM(sys_pause)
	.long CSYM(sys_utime)		// 30
	.long CSYM(sys_ni_syscall)	// was: stty
	.long CSYM(sys_ni_syscall)	// was: gtty
	.long CSYM(sys_access)
	.long CSYM(sys_nice)
	.long CSYM(sys_ni_syscall)	// 35, was: ftime
	.long CSYM(sys_sync)
	.long CSYM(sys_kill)
	.long CSYM(sys_rename)
	.long CSYM(sys_mkdir)
	.long CSYM(sys_rmdir)		// 40
	.long CSYM(sys_dup)
	.long CSYM(sys_pipe)
	.long CSYM(sys_times)
	.long CSYM(sys_ni_syscall)	// was: prof
	.long CSYM(sys_brk)		// 45
	.long CSYM(sys_setgid)
	.long CSYM(sys_getgid)
	.long CSYM(sys_signal)
	.long CSYM(sys_geteuid)
	.long CSYM(sys_getegid)		// 50
	.long CSYM(sys_acct)
	.long CSYM(sys_umount)		// recycled never used phys()
	.long CSYM(sys_ni_syscall)	// was: lock
	.long CSYM(sys_ioctl)
	.long CSYM(sys_fcntl)		// 55
	.long CSYM(sys_ni_syscall)	// was: mpx
	.long CSYM(sys_setpgid)
	.long CSYM(sys_ni_syscall) 	// was: ulimit
	.long CSYM(sys_ni_syscall)
	.long CSYM(sys_umask)		// 60
	.long CSYM(sys_chroot)
	.long CSYM(sys_ustat)
	.long CSYM(sys_dup2)
	.long CSYM(sys_getppid)
	.long CSYM(sys_getpgrp)		// 65
	.long CSYM(sys_setsid)
	.long CSYM(sys_sigaction)
	.long CSYM(sys_sgetmask)
	.long CSYM(sys_ssetmask)
	.long CSYM(sys_setreuid)	// 70
	.long CSYM(sys_setregid)
	.long sys_sigsuspend_wrapper
	.long CSYM(sys_sigpending)
	.long CSYM(sys_sethostname)
	.long CSYM(sys_setrlimit)	// 75
	.long CSYM(sys_getrlimit)
	.long CSYM(sys_getrusage)
	.long CSYM(sys_gettimeofday)
	.long CSYM(sys_settimeofday)
	.long CSYM(sys_getgroups)	// 80
	.long CSYM(sys_setgroups)
	.long CSYM(sys_select)
	.long CSYM(sys_symlink)
	.long CSYM(sys_ni_syscall)	// was: oldlstat (aka lstat)
	.long CSYM(sys_readlink)	// 85
	.long CSYM(sys_uselib)
	.long CSYM(sys_swapon)
	.long CSYM(sys_reboot)
	.long CSYM(old_readdir)
	.long CSYM(sys_mmap)		// 90
	.long CSYM(sys_munmap)
	.long CSYM(sys_truncate)
	.long CSYM(sys_ftruncate)
	.long CSYM(sys_fchmod)
	.long CSYM(sys_fchown)		// 95
	.long CSYM(sys_getpriority)
	.long CSYM(sys_setpriority)
	.long CSYM(sys_ni_syscall) 	// was: profil
	.long CSYM(sys_statfs)
	.long CSYM(sys_fstatfs)		// 100
	.long CSYM(sys_ni_syscall)	// i386: ioperm
	.long CSYM(sys_socketcall)
	.long CSYM(sys_syslog)
	.long CSYM(sys_setitimer)
	.long CSYM(sys_getitimer)	// 105
	.long CSYM(sys_newstat)
	.long CSYM(sys_newlstat)
	.long CSYM(sys_newfstat)
	.long CSYM(sys_ni_syscall)	// was: olduname (aka uname)
	.long CSYM(sys_ni_syscall)	// 110, i386: iopl
	.long CSYM(sys_vhangup)
	.long CSYM(sys_ni_syscall)	// was: idle
	.long CSYM(sys_ni_syscall)	// i386: vm86old
	.long CSYM(sys_wait4)
	.long CSYM(sys_swapoff)		// 115
	.long CSYM(sys_sysinfo)
	.long CSYM(sys_ipc)
	.long CSYM(sys_fsync)
	.long sys_sigreturn_wrapper
	.long sys_clone_wrapper		// 120
	.long CSYM(sys_setdomainname)
	.long CSYM(sys_newuname)
	.long CSYM(sys_ni_syscall)	// i386: modify_ldt, m68k: cacheflush 
	.long CSYM(sys_adjtimex)
	.long CSYM(sys_ni_syscall)	// 125 - sys_mprotect
	.long CSYM(sys_sigprocmask)
	.long CSYM(sys_ni_syscall)	// sys_create_module
	.long CSYM(sys_init_module)
	.long CSYM(sys_delete_module)
	.long CSYM(sys_ni_syscall)	// 130 - sys_get_kernel_syms
	.long CSYM(sys_quotactl)
	.long CSYM(sys_getpgid)
	.long CSYM(sys_fchdir)
	.long CSYM(sys_bdflush)
	.long CSYM(sys_sysfs)		// 135
	.long CSYM(sys_personality)
	.long CSYM(sys_ni_syscall)	// for afs_syscall
	.long CSYM(sys_setfsuid)
	.long CSYM(sys_setfsgid)
	.long CSYM(sys_llseek)		// 140
	.long CSYM(sys_getdents)
	.long CSYM(sys_select)		// for backward compat; remove someday
	.long CSYM(sys_flock)
	.long CSYM(sys_ni_syscall)	// sys_msync
	.long CSYM(sys_readv)		// 145
	.long CSYM(sys_writev)
	.long CSYM(sys_getsid)
	.long CSYM(sys_fdatasync)
	.long CSYM(sys_sysctl)
	.long CSYM(sys_ni_syscall)	// 150 - sys_mlock
	.long CSYM(sys_ni_syscall)	// sys_munlock
	.long CSYM(sys_ni_syscall)	// sys_mlockall
	.long CSYM(sys_ni_syscall)	// sys_munlockall
	.long CSYM(sys_sched_setparam)
	.long CSYM(sys_sched_getparam)	// 155
	.long CSYM(sys_sched_setscheduler)
	.long CSYM(sys_sched_getscheduler)
	.long CSYM(sys_sched_yield)
	.long CSYM(sys_sched_get_priority_max)
	.long CSYM(sys_sched_get_priority_min)  // 160
	.long CSYM(sys_sched_rr_get_interval)
	.long CSYM(sys_nanosleep)
	.long CSYM(sys_ni_syscall)	// sys_mremap
	.long CSYM(sys_setresuid)
	.long CSYM(sys_getresuid)	// 165
	.long CSYM(sys_ni_syscall)	// for vm86
	.long CSYM(sys_ni_syscall)	// sys_query_module
	.long CSYM(sys_poll)
	.long CSYM(sys_nfsservctl)
	.long CSYM(sys_setresgid)	// 170
	.long CSYM(sys_getresgid)
	.long CSYM(sys_prctl)
	.long sys_rt_sigreturn_wrapper
	.long CSYM(sys_rt_sigaction)
	.long CSYM(sys_rt_sigprocmask)	// 175
	.long CSYM(sys_rt_sigpending)
	.long CSYM(sys_rt_sigtimedwait)
	.long CSYM(sys_rt_sigqueueinfo)
	.long sys_rt_sigsuspend_wrapper
	.long CSYM(sys_pread64)		// 180
	.long CSYM(sys_pwrite64)
	.long CSYM(sys_lchown)
	.long CSYM(sys_getcwd)
	.long CSYM(sys_capget)
	.long CSYM(sys_capset)		// 185
	.long CSYM(sys_sigaltstack)
	.long CSYM(sys_sendfile)
	.long CSYM(sys_ni_syscall)	// streams1
	.long CSYM(sys_ni_syscall)	// streams2
	.long sys_vfork_wrapper		// 190
	.long CSYM(sys_ni_syscall)
	.long CSYM(sys_mmap2)
	.long CSYM(sys_truncate64)
	.long CSYM(sys_ftruncate64)
	.long CSYM(sys_stat64)		// 195
	.long CSYM(sys_lstat64)
	.long CSYM(sys_fstat64)
	.long CSYM(sys_fcntl64)
	.long CSYM(sys_getdents64)
	.long CSYM(sys_pivot_root)	// 200
	.long CSYM(sys_gettid)
	.long CSYM(sys_tkill)
	.long CSYM(sys_ni_syscall)	// sys_mincore
	.long CSYM(sys_ni_syscall)	// sys_madvise

	.space (NR_syscalls-205)*4
C_END(sys_call_table)
