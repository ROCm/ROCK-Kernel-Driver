/*
 * signal32.c: Support 32bit signal syscalls.
 *
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/mm.h> 
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/elf.h>
#include <linux/compat.h>
#include <asm/ppc32.h>
#include <asm/uaccess.h>
#include <asm/ppcdebug.h>
#include <asm/unistd.h>
#include <asm/cacheflush.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))
/* 
 * These are the flags in the MSR that the user is allowed to change
 * by modifying the saved value of the MSR on the stack.  SE and BE
 * should not be in this list since gdb may want to change these.  I.e,
 * you should be able to step out of a signal handler to see what
 * instruction executes next after the signal handler completes.
 * Alternately, if you stepped into a signal handler, you should be
 * able to continue 'til the next breakpoint from within the signal
 * handler, even if the handler returns.
 */
#if 0
#define MSR_USERCHANGE	(MSR_FE0 | MSR_FE1)
#else
/*
 * glibc tries to set FE0/FE1 via a signal handler. Since it only ever
 * sets both bits and this is the default setting we now disable this
 * behaviour. This is done to insure the new prctl which alters FE0/FE1 does
 * not get overriden by glibc. Setting and clearing FE0/FE1 via signal
 * handler has always been bogus since load_up_fpu used to set FE0/FE1
 * unconditionally.
 */
#define MSR_USERCHANGE	0
#endif

struct sigregs32 {
	/*
	 * the gp_regs array is 32 bit representation of the pt_regs
	 * structure that was stored on the kernel stack during the
	 * system call that was interrupted for the signal.
	 *
	 * Note that the entire pt_regs regs structure will fit in
	 * the gp_regs structure because the ELF_NREG value is 48 for
	 * PPC and the pt_regs structure contains 44 registers
	 */
	elf_gregset_t32	gp_regs;
	double		fp_regs[ELF_NFPREG];
	unsigned int	tramp[2];
	/*
	 * Programs using the rs6000/xcoff abi can save up to 19 gp
	 * regs and 18 fp regs below sp before decrementing it.
	 */
	int		abigap[56];
};


struct rt_sigframe_32 {
	/*
	 * Unused space at start of frame to allow for storing of
	 * stack pointers
	 */
	unsigned long _unused;
	/*
	 * This is a 32 bit pointer in user address space 
	 * it is a pointer to the siginfo stucture in the rt stack frame 
	 */
	u32 pinfo;
	/*
	 * This is a 32 bit pointer in user address space
	 * it is a pointer to the user context in the rt stack frame
	 */
	u32 puc;
	struct siginfo32  info;
	struct ucontext32 uc;
};



/*
 *  Start of nonRT signal support
 *
 *     sigset_t is 32 bits for non-rt signals
 *
 *  System Calls
 *       sigaction                sys32_sigaction
 *       sigreturn                sys32_sigreturn
 *
 *  Note sigsuspend has no special 32 bit routine - uses the 64 bit routine
 *
 *  Other routines
 *        setup_frame32
 */

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
long sys32_sigsuspend(old_sigset_t mask, int p2, int p3, int p4, int p6, int p7,
	       struct pt_regs *regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->result = -EINTR;
	regs->gpr[3] = EINTR;
	regs->ccr |= 0x10000000;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal32(&saveset, regs))
			/*
			 * If a signal handler needs to be called,
			 * do_signal32() has set R3 to the signal number (the
			 * first argument of the signal handler), so don't
			 * overwrite that with EINTR !
			 * In the other cases, do_signal32() doesn't touch 
			 * R3, so it's still set to -EINTR (see above).
			 */
			return regs->gpr[3];
	}
}

long sys32_sigaction(int sig, struct old_sigaction32 *act,
		struct old_sigaction32 *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	
	if (sig < 0)
		sig = -sig;

	if (act) {
		compat_old_sigset_t mask;

		if (get_user((long)new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user((long)new_ka.sa.sa_restorer, &act->sa_restorer) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);
	if (!ret && oact) {
		if (put_user((long)old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}


/*
 * When we have signals to deliver, we set up on the
 * user stack, going down from the original stack pointer:
 *	a sigregs struct
 *	one or more sigcontext structs
 *	a gap of __SIGNAL_FRAMESIZE32 bytes
 *
 * Each of these things must be a multiple of 16 bytes in size.
 *
 */


/*
 * Do a signal return; undo the signal stack.
 */
long sys32_sigreturn(unsigned long r3, unsigned long r4, unsigned long r5,
		     unsigned long r6, unsigned long r7, unsigned long r8,
		     struct pt_regs *regs)
{
	struct sigcontext32 *sc, sigctx;
	struct sigregs32 *sr;
	int ret;
	elf_gregset_t32 saved_regs;  /* an array of ELF_NGREG unsigned ints (32 bits) */
	sigset_t set;
	int i;

	sc = (struct sigcontext32 *)(regs->gpr[1] + __SIGNAL_FRAMESIZE32);
	if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
		goto badframe;

	/*
	 * Note that PPC32 puts the upper 32 bits of the sigmask in the
	 * unused part of the signal stackframe
	 */
	set.sig[0] = sigctx.oldmask + ((long)(sigctx._unused[3]) << 32);
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	if (regs->msr & MSR_FP )
		giveup_fpu(current);
	/* Last stacked signal - restore registers */
	sr = (struct sigregs32*)(u64)sigctx.regs;
	/*
	 * copy the 32 bit register values off the user stack
	 * into the 32 bit register area
	 */
	if (copy_from_user(saved_regs, &sr->gp_regs, sizeof(sr->gp_regs)))
		goto badframe;
	/*
	 * The saved reg structure in the frame is an elf_grepset_t32,
	 * it is a 32 bit register save of the registers in the
	 * pt_regs structure that was stored on the kernel stack
	 * during the system call when the system call was interrupted
	 * for the signal. Only 32 bits are saved because the
	 * sigcontext contains a pointer to the regs and the sig
	 * context address is passed as a pointer to the signal
	 * handler.  
	 *
	 * The entries in the elf_grepset have the same index as the
	 * elements in the pt_regs structure.
	 */
	saved_regs[PT_MSR] = (regs->msr & ~MSR_USERCHANGE)
		| (saved_regs[PT_MSR] & MSR_USERCHANGE);
	/*
	 * Register 2 is the kernel toc - should be reset on
	 * any calls into the kernel 
	 */
	for (i = 0; i < 32; i++)
		regs->gpr[i] = (u64)(saved_regs[i]) & 0xFFFFFFFF;

	/*
	 *  restore the non gpr registers 
	 */
	regs->msr = (u64)(saved_regs[PT_MSR]) & 0xFFFFFFFF;
	/*
	 * Insure that the interrupt mode is 64 bit, during 32 bit
	 * execution. (This is necessary because we only saved
	 * lower 32 bits of msr.)
	 */
	regs->msr = regs->msr | MSR_ISF;  /* When this thread is interrupted it should run in 64 bit mode. */

	regs->nip = (u64)(saved_regs[PT_NIP]) & 0xFFFFFFFF;
	regs->orig_gpr3 = (u64)(saved_regs[PT_ORIG_R3]) & 0xFFFFFFFF; 
	regs->ctr = (u64)(saved_regs[PT_CTR]) & 0xFFFFFFFF; 
	regs->link = (u64)(saved_regs[PT_LNK]) & 0xFFFFFFFF; 
	regs->xer = (u64)(saved_regs[PT_XER]) & 0xFFFFFFFF; 
	regs->ccr = (u64)(saved_regs[PT_CCR]) & 0xFFFFFFFF;
	/* regs->softe is left unchanged (like the MSR.EE bit) */
	/******************************************************/
	/* the DAR and the DSISR are only relevant during a   */
	/*   data or instruction storage interrupt. The value */
	/*   will be set to zero.                             */
	/******************************************************/
	regs->dar = 0; 
	regs->dsisr = 0;
	regs->result = (u64)(saved_regs[PT_RESULT]) & 0xFFFFFFFF;

	if (copy_from_user(current->thread.fpr, &sr->fp_regs,
			   sizeof(sr->fp_regs)))
		goto badframe;

	ret = regs->result;
	return ret;

badframe:
	do_exit(SIGSEGV);
}	

/*
 * Set up a signal frame.
 */
static void setup_frame32(struct pt_regs *regs, struct sigregs32 *frame,
            unsigned int newsp)
{
	struct sigcontext32 *sc = (struct sigcontext32 *)(u64)newsp;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	if (verify_area(VERIFY_WRITE, frame, sizeof(*frame)))
		goto badframe;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	/*
	 * Copy the register contents for the pt_regs structure on the
	 *   kernel stack to the elf_gregset_t32 structure on the user
	 *   stack. This is a copy of 64 bit register values to 32 bit
	 *   register values. The high order 32 bits of the 64 bit
	 *   registers are not needed since a 32 bit application is
	 *   running and the saved registers are the contents of the
	 *   user registers at the time of a system call.
	 * 
	 * The values saved on the user stack will be restored into
	 *  the registers during the signal return processing
	 */
	for (i = 0; i < 32; i++) {
		if (__put_user((u32)regs->gpr[i], &frame->gp_regs[i]))
			goto badframe;
	}

	/*
	 * Copy the non gpr registers to the user stack
	 */
	if (__put_user((u32)regs->gpr[PT_NIP], &frame->gp_regs[PT_NIP])
	    || __put_user((u32)regs->gpr[PT_MSR], &frame->gp_regs[PT_MSR])
	    || __put_user((u32)regs->gpr[PT_ORIG_R3], &frame->gp_regs[PT_ORIG_R3])
	    || __put_user((u32)regs->gpr[PT_CTR], &frame->gp_regs[PT_CTR])
	    || __put_user((u32)regs->gpr[PT_LNK], &frame->gp_regs[PT_LNK])
	    || __put_user((u32)regs->gpr[PT_XER], &frame->gp_regs[PT_XER])
	    || __put_user((u32)regs->gpr[PT_CCR], &frame->gp_regs[PT_CCR])
#if 0
	    || __put_user((u32)regs->gpr[PT_MQ], &frame->gp_regs[PT_MQ])
#endif
	    || __put_user((u32)regs->gpr[PT_RESULT], &frame->gp_regs[PT_RESULT]))
		goto badframe;


	/*
	 * Now copy the floating point registers onto the user stack 
	 *
	 * Also set up so on the completion of the signal handler, the
	 * sys_sigreturn will get control to reset the stack
	 */
	if (__copy_to_user(&frame->fp_regs, current->thread.fpr,
			   ELF_NFPREG * sizeof(double))
	    /* li r0, __NR_sigreturn */
	    || __put_user(0x38000000U + __NR_sigreturn, &frame->tramp[0])
	    /* sc */
	    || __put_user(0x44000002U, &frame->tramp[1]))
		goto badframe;
	flush_icache_range((unsigned long)&frame->tramp[0],
			   (unsigned long)&frame->tramp[2]);
	current->thread.fpscr = 0;	/* turn off all fp exceptions */

	newsp -= __SIGNAL_FRAMESIZE32;
	if (put_user(regs->gpr[1], (u32*)(u64)newsp)
	    || get_user(regs->nip, &sc->handler)
	    || get_user(regs->gpr[3], &sc->signal))
		goto badframe;

	regs->gpr[1] = newsp & 0xFFFFFFFF;
	/*
	 * first parameter to the signal handler is the signal number
	 *  - the value is in gpr3
	 * second parameter to the signal handler is the sigcontext
	 *   - set the value into gpr4
	 */
	regs->gpr[4] = (unsigned long) sc;
	regs->link = (unsigned long) frame->tramp;
	return;

badframe:
#if DEBUG_SIG
	printk("badframe in setup_frame32, regs=%p frame=%p newsp=%lx\n",
	       regs, frame, newsp);
#endif
	do_exit(SIGSEGV);
}


/*
 *  Start of RT signal support
 *
 *     sigset_t is 64 bits for rt signals
 *
 *  System Calls
 *       sigaction                sys32_rt_sigaction
 *       sigpending               sys32_rt_sigpending
 *       sigprocmask              sys32_rt_sigprocmask
 *       sigreturn                sys32_rt_sigreturn
 *       sigtimedwait             sys32_rt_sigtimedwait
 *       sigqueueinfo             sys32_rt_sigqueueinfo
 *       sigsuspend               sys32_rt_sigsuspend
 *
 *  Other routines
 *        setup_rt_frame32
 *        copy_siginfo_to_user32
 *        siginfo32to64
 */

/*
 * This code executes after the rt signal handler in 32 bit mode has
 * completed and returned  
 */
long sys32_rt_sigreturn(unsigned long r3, unsigned long r4, unsigned long r5,
			unsigned long r6, unsigned long r7, unsigned long r8,
			struct pt_regs * regs)
{
	struct rt_sigframe_32 *rt_sf;
	struct sigcontext32 sigctx;
	struct sigregs32 *sr;
	int ret;
	elf_gregset_t32 saved_regs;   /* an array of 32 bit register values */
	sigset_t set; 
	stack_t st;
	int i;
	mm_segment_t old_fs;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/* Adjust the inputted reg1 to point to the first rt signal frame */
	rt_sf = (struct rt_sigframe_32 *)(regs->gpr[1] + __SIGNAL_FRAMESIZE32);
	/* Copy the information from the user stack  */
	if (copy_from_user(&sigctx, &rt_sf->uc.uc_mcontext, sizeof(sigctx))
	    || copy_from_user(&set, &rt_sf->uc.uc_sigmask, sizeof(set))
	    || copy_from_user(&st,&rt_sf->uc.uc_stack, sizeof(st)))
		goto badframe;

	/*
	 * Unblock the signal that was processed 
	 *   After a signal handler runs - 
	 *     if the signal is blockable - the signal will be unblocked  
	 *       (sigkill and sigstop are not blockable)
	 */
	sigdelsetmask(&set, ~_BLOCKABLE); 
	/* update the current based on the sigmask found in the rt_stackframe */
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	/* If currently owning the floating point - give them up */
	if (regs->msr & MSR_FP)
		giveup_fpu(current);
	/*
	 * Set to point to the next rt_sigframe - this is used to
	 * determine whether this is the last signal to process
	 */
	sr = (struct sigregs32 *)(u64)sigctx.regs;
	if (copy_from_user(saved_regs, &sr->gp_regs, sizeof(sr->gp_regs))) 
		goto badframe;
	/*
	 * The saved reg structure in the frame is an elf_grepset_t32,
	 * it is a 32 bit register save of the registers in the
	 * pt_regs structure that was stored on the kernel stack
	 * during the system call when the system call was interrupted
	 * for the signal. Only 32 bits are saved because the
	 * sigcontext contains a pointer to the regs and the sig
	 * context address is passed as a pointer to the signal handler
	 *
	 * The entries in the elf_grepset have the same index as
	 * the elements in the pt_regs structure.
	 */
	saved_regs[PT_MSR] = (regs->msr & ~MSR_USERCHANGE)
		| (saved_regs[PT_MSR] & MSR_USERCHANGE);
	/*
	 * Register 2 is the kernel toc - should be reset on any
	 * calls into the kernel
	 */
	for (i = 0; i < 32; i++)
		regs->gpr[i] = (u64)(saved_regs[i]) & 0xFFFFFFFF;
	/*
	 * restore the non gpr registers
	 */
	regs->msr = (u64)(saved_regs[PT_MSR]) & 0xFFFFFFFF;
	regs->nip = (u64)(saved_regs[PT_NIP]) & 0xFFFFFFFF;
	regs->orig_gpr3 = (u64)(saved_regs[PT_ORIG_R3]) & 0xFFFFFFFF; 
	regs->ctr = (u64)(saved_regs[PT_CTR]) & 0xFFFFFFFF; 
	regs->link = (u64)(saved_regs[PT_LNK]) & 0xFFFFFFFF; 
	regs->xer = (u64)(saved_regs[PT_XER]) & 0xFFFFFFFF; 
	regs->ccr = (u64)(saved_regs[PT_CCR]) & 0xFFFFFFFF;
	/* regs->softe is left unchanged (like MSR.EE) */
	/*
	 * the DAR and the DSISR are only relevant during a
	 *   data or instruction storage interrupt. The value
	 *   will be set to zero.
	 */
	regs->dar = 0; 
	regs->dsisr = 0;
	regs->result = (u64)(saved_regs[PT_RESULT]) & 0xFFFFFFFF;
	if (copy_from_user(current->thread.fpr, &sr->fp_regs,
			   sizeof(sr->fp_regs)))
		goto badframe;
	/* This function sets back the stack flags into
	   the current task structure.  */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	do_sigaltstack(&st, NULL, regs->gpr[1]);
	set_fs(old_fs);

	ret = regs->result;
	return ret;

 badframe:
	do_exit(SIGSEGV);     
}



long sys32_rt_sigaction(int sig, const struct sigaction32 *act,
		struct sigaction32 *oact, size_t sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_sigset_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		ret = get_user((long)new_ka.sa.sa_handler, &act->sa_handler);
		ret |= __copy_from_user(&set32, &act->sa_mask,
					sizeof(compat_sigset_t));
		switch (_NSIG_WORDS) {
		case 4: new_ka.sa.sa_mask.sig[3] = set32.sig[6]
				| (((long)set32.sig[7]) << 32);
		case 3: new_ka.sa.sa_mask.sig[2] = set32.sig[4]
				| (((long)set32.sig[5]) << 32);
		case 2: new_ka.sa.sa_mask.sig[1] = set32.sig[2]
				| (((long)set32.sig[3]) << 32);
		case 1: new_ka.sa.sa_mask.sig[0] = set32.sig[0]
				| (((long)set32.sig[1]) << 32);
		}
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		if (ret)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);
	if (!ret && oact) {
		switch (_NSIG_WORDS) {
		case 4:
			set32.sig[7] = (old_ka.sa.sa_mask.sig[3] >> 32);
			set32.sig[6] = old_ka.sa.sa_mask.sig[3];
		case 3:
			set32.sig[5] = (old_ka.sa.sa_mask.sig[2] >> 32);
			set32.sig[4] = old_ka.sa.sa_mask.sig[2];
		case 2:
			set32.sig[3] = (old_ka.sa.sa_mask.sig[1] >> 32);
			set32.sig[2] = old_ka.sa.sa_mask.sig[1];
		case 1:
			set32.sig[1] = (old_ka.sa.sa_mask.sig[0] >> 32);
			set32.sig[0] = old_ka.sa.sa_mask.sig[0];
		}
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __copy_to_user(&oact->sa_mask, &set32,
				      sizeof(compat_sigset_t));
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
	}
	return ret;
}

/*
 * Note: it is necessary to treat how as an unsigned int, with the
 * corresponding cast to a signed int to insure that the proper
 * conversion (sign extension) between the register representation
 * of a signed int (msr in 32-bit mode) and the register representation
 * of a signed int (msr in 64-bit mode) is performed.
 */
long sys32_rt_sigprocmask(u32 how, compat_sigset_t *set,
		compat_sigset_t *oset, size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (set) {
		if (copy_from_user (&s32, set, sizeof(compat_sigset_t)))
			return -EFAULT;
    
		switch (_NSIG_WORDS) {
		case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
		case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
		case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
		case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
		}
	}
	
	set_fs(KERNEL_DS);
	ret = sys_rt_sigprocmask((int)how, set ? &s : NULL, oset ? &s : NULL,
				 sigsetsize); 
	set_fs(old_fs);
	if (ret)
		return ret;
	if (oset) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (oset, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return 0;
}

long sys32_rt_sigpending(compat_sigset_t *set, compat_size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_rt_sigpending(&s, sigsetsize);
	set_fs(old_fs);
	if (!ret) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (set, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return ret;
}


static int copy_siginfo_to_user32(siginfo_t32 *d, siginfo_t *s)
{
	int err;

	if (!access_ok (VERIFY_WRITE, d, sizeof(*d)))
		return -EFAULT;

	err = __put_user(s->si_signo, &d->si_signo);
	err |= __put_user(s->si_errno, &d->si_errno);
	err |= __put_user((short)s->si_code, &d->si_code);
	if (s->si_signo >= SIGRTMIN) {
		err |= __put_user(s->si_pid, &d->si_pid);
		err |= __put_user(s->si_uid, &d->si_uid);
		err |= __put_user(s->si_int, &d->si_int);
	} else {
		switch (s->si_signo) {
		/* XXX: What about POSIX1.b timers */
		case SIGCHLD:
			err |= __put_user(s->si_pid, &d->si_pid);
			err |= __put_user(s->si_status, &d->si_status);
			err |= __put_user(s->si_utime, &d->si_utime);
			err |= __put_user(s->si_stime, &d->si_stime);
			break;
		case SIGSEGV:
		case SIGBUS:
		case SIGFPE:
		case SIGILL:
			err |= __put_user((long)(s->si_addr), &d->si_addr);
	        break;
		case SIGPOLL:
			err |= __put_user(s->si_band, &d->si_band);
			err |= __put_user(s->si_fd, &d->si_fd);
			break;
		default:
			err |= __put_user(s->si_pid, &d->si_pid);
			err |= __put_user(s->si_uid, &d->si_uid);
			break;
		}
	}
	return err;
}

long sys32_rt_sigtimedwait(compat_sigset_t *uthese, siginfo_t32 *uinfo,
		struct compat_timespec *uts, compat_size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();
	siginfo_t info;

	if (copy_from_user(&s32, uthese, sizeof(compat_sigset_t)))
		return -EFAULT;
	switch (_NSIG_WORDS) {
	case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
	case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
	case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
	case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	}
	if (uts && get_compat_timespec(&t, uts))
		return -EFAULT;
	set_fs(KERNEL_DS);
	ret = sys_rt_sigtimedwait(&s, uinfo ? &info : NULL, uts ? &t : NULL,
			sigsetsize);
	set_fs(old_fs);
	if (ret >= 0 && uinfo) {
		if (copy_siginfo_to_user32(uinfo, &info))
			return -EFAULT;
	}
	return ret;
}



static siginfo_t * siginfo32to64(siginfo_t *d, siginfo_t32 *s)
{
	d->si_signo = s->si_signo;
	d->si_errno = s->si_errno;
	d->si_code = s->si_code;
	if (s->si_signo >= SIGRTMIN) {
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		d->si_int = s->si_int;
	} else {
		switch (s->si_signo) {
		/* XXX: What about POSIX1.b timers */
		case SIGCHLD:
			d->si_pid = s->si_pid;
			d->si_status = s->si_status;
			d->si_utime = s->si_utime;
			d->si_stime = s->si_stime;
			break;
		case SIGSEGV:
		case SIGBUS:
		case SIGFPE:
		case SIGILL:
			d->si_addr = (void *)A(s->si_addr);
	  		break;
		case SIGPOLL:
			d->si_band = s->si_band;
			d->si_fd = s->si_fd;
			break;
		default:
			d->si_pid = s->si_pid;
			d->si_uid = s->si_uid;
			break;
		}
	}
	return d;
}

/*
 * Note: it is necessary to treat pid and sig as unsigned ints, with the
 * corresponding cast to a signed int to insure that the proper conversion
 * (sign extension) between the register representation of a signed int
 * (msr in 32-bit mode) and the register representation of a signed int
 * (msr in 64-bit mode) is performed.
 */
long sys32_rt_sigqueueinfo(u32 pid, u32 sig, siginfo_t32 *uinfo)
{
	siginfo_t info;
	siginfo_t32 info32;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_from_user (&info32, uinfo, sizeof(siginfo_t32)))
		return -EFAULT;
    	/* XXX: Is this correct? */
	siginfo32to64(&info, &info32);

	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo((int)pid, (int)sig, &info);
	set_fs (old_fs);
	return ret;
}

int sys32_rt_sigsuspend(compat_sigset_t* unewset, size_t sigsetsize, int p3,
		int p4, int p6, int p7, struct pt_regs *regs)
{
	sigset_t saveset, newset;
	compat_sigset_t s32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&s32, unewset, sizeof(s32)))
		return -EFAULT;

	/*
	 * Swap the 2 words of the 64-bit sigset_t (they are stored
	 * in the "wrong" endian in 32-bit user storage).
	 */
	switch (_NSIG_WORDS) {
	case 4: newset.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
	case 3: newset.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
	case 2: newset.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
	case 1: newset.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	}

	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->result = -EINTR;
	regs->gpr[3] = EINTR;
	regs->ccr |= 0x10000000;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal32(&saveset, regs))
			/*
			 * If a signal handler needs to be called,
			 * do_signal32() has set R3 to the signal number (the
			 * first argument of the signal handler), so don't
			 * overwrite that with EINTR !
			 * In the other cases, do_signal32() doesn't touch 
			 * R3, so it's still set to -EINTR (see above).
			 */
			return regs->gpr[3];
	}
}


/*
 * Set up a rt signal frame.
 */
static void setup_rt_frame32(struct pt_regs *regs, struct sigregs32 *frame,
            unsigned int newsp)
{
	unsigned int copyreg4, copyreg5;
	struct rt_sigframe_32 * rt_sf = (struct rt_sigframe_32 *) (u64)newsp;
	int i;
  
	if (verify_area(VERIFY_WRITE, frame, sizeof(*frame)))
		goto badframe;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	/*
	 * Copy the register contents for the pt_regs structure on the
	 *   kernel stack to the elf_gregset_t32 structure on the user
	 *   stack. This is a copy of 64 bit register values to 32 bit
	 *   register values. The high order 32 bits of the 64 bit
	 *   registers are not needed since a 32 bit application is
	 *   running and the saved registers are the contents of the
	 *   user registers at the time of a system call.
	 *
	 * The values saved on the user stack will be restored into
	 *  the registers during the signal return processing
	 */
	for (i = 0; i < 32; i++) {
		if (__put_user((u32)regs->gpr[i], &frame->gp_regs[i]))
			goto badframe;
	}

	/*
	 * Copy the non gpr registers to the user stack
	 */
	if (__put_user((u32)regs->gpr[PT_NIP], &frame->gp_regs[PT_NIP])
	    || __put_user((u32)regs->gpr[PT_MSR], &frame->gp_regs[PT_MSR])
	    || __put_user((u32)regs->gpr[PT_ORIG_R3], &frame->gp_regs[PT_ORIG_R3])
	    || __put_user((u32)regs->gpr[PT_CTR], &frame->gp_regs[PT_CTR])
	    || __put_user((u32)regs->gpr[PT_LNK], &frame->gp_regs[PT_LNK])
	    || __put_user((u32)regs->gpr[PT_XER], &frame->gp_regs[PT_XER])
	    || __put_user((u32)regs->gpr[PT_CCR], &frame->gp_regs[PT_CCR])
	    || __put_user((u32)regs->gpr[PT_RESULT], &frame->gp_regs[PT_RESULT]))
		goto badframe;


	/*
	 * Now copy the floating point registers onto the user stack
	 *
	 * Also set up so on the completion of the signal handler, the
	 * sys_sigreturn will get control to reset the stack
	 */
	if (__copy_to_user(&frame->fp_regs, current->thread.fpr,
			   ELF_NFPREG * sizeof(double))
	    || __put_user(0x38000000U + __NR_rt_sigreturn, &frame->tramp[0])    /* li r0, __NR_rt_sigreturn */
	    || __put_user(0x44000002U, &frame->tramp[1]))   /* sc */
		goto badframe;

	flush_icache_range((unsigned long) &frame->tramp[0],
			   (unsigned long) &frame->tramp[2]);
	current->thread.fpscr = 0;	/* turn off all fp exceptions */

	/*
	 * Retrieve rt_sigframe from stack and
	 * set up registers for signal handler
	 */
	newsp -= __SIGNAL_FRAMESIZE32;
      

	if (put_user((u32)(regs->gpr[1]), (unsigned int *)(u64)newsp)
	    || get_user(regs->nip, &rt_sf->uc.uc_mcontext.handler)
	    || get_user(regs->gpr[3], &rt_sf->uc.uc_mcontext.signal)
	    || get_user(copyreg4, &rt_sf->pinfo)
	    || get_user(copyreg5, &rt_sf->puc))
		goto badframe;

	regs->gpr[4] = copyreg4;
	regs->gpr[5] = copyreg5;
	regs->gpr[1] = newsp;
	regs->gpr[6] = (unsigned long) rt_sf;
	regs->link = (unsigned long) frame->tramp;

	return;

badframe:
#if DEBUG_SIG
	printk("badframe in setup_frame32, regs=%p frame=%p newsp=%lx\n",
	       regs, frame, newsp);
#endif
	do_exit(SIGSEGV);
}


/*
 * OK, we're invoking a handler
 */
static void handle_signal32(unsigned long sig, siginfo_t *info,
		sigset_t *oldset, struct pt_regs * regs, unsigned int *newspp,
		unsigned int frame)
{
	struct sigcontext32 *sc;
	struct rt_sigframe_32 *rt_sf;
	struct k_sigaction *ka = &current->sighand->action[sig-1];

	if (regs->trap == 0x0C00 /* System Call! */
	    && ((int)regs->result == -ERESTARTNOHAND ||
		(int)regs->result == -ERESTART_RESTARTBLOCK ||
		((int)regs->result == -ERESTARTSYS &&
		 !(ka->sa.sa_flags & SA_RESTART)))) {
		if ((int)regs->result == -ERESTART_RESTARTBLOCK)
			current_thread_info()->restart_block.fn
				= do_no_restart_syscall;
		regs->result = -EINTR;
	}

	/*
	 * Set up the signal frame
	 * Determine if a real time frame and a siginfo is required
	 */
	if (ka->sa.sa_flags & SA_SIGINFO) {
		*newspp -= sizeof(*rt_sf);
		rt_sf = (struct rt_sigframe_32 *)(u64)(*newspp);
		if (verify_area(VERIFY_WRITE, rt_sf, sizeof(*rt_sf)))
			goto badframe;
		if (__put_user((u32)(u64)ka->sa.sa_handler,
					&rt_sf->uc.uc_mcontext.handler)
		    || __put_user((u32)(u64)&rt_sf->info, &rt_sf->pinfo)
		    || __put_user((u32)(u64)&rt_sf->uc, &rt_sf->puc)
		    /*  put the siginfo on the user stack                    */
		    || copy_siginfo_to_user32(&rt_sf->info, info)
		    /*  set the ucontext on the user stack                   */ 
		    || __put_user(0, &rt_sf->uc.uc_flags)
		    || __put_user(0, &rt_sf->uc.uc_link)
		    || __put_user(current->sas_ss_sp, &rt_sf->uc.uc_stack.ss_sp)
		    || __put_user(sas_ss_flags(regs->gpr[1]),
			    &rt_sf->uc.uc_stack.ss_flags)
		    || __put_user(current->sas_ss_size,
			    &rt_sf->uc.uc_stack.ss_size)
		    || __copy_to_user(&rt_sf->uc.uc_sigmask,
			    oldset, sizeof(*oldset))
		    /* point the mcontext.regs to the pramble register frame  */
		    || __put_user(frame, &rt_sf->uc.uc_mcontext.regs)
		    || __put_user(sig,&rt_sf->uc.uc_mcontext.signal))
			goto badframe; 
	} else {
		/* Put a sigcontext on the stack */
		*newspp -= sizeof(*sc);
		sc = (struct sigcontext32 *)(u64)*newspp;
		if (verify_area(VERIFY_WRITE, sc, sizeof(*sc)))
			goto badframe;
		/*
		 * Note the upper 32 bits of the signal mask are stored
		 * in the unused part of the signal stack frame
		 */
		if (__put_user((u32)(u64)ka->sa.sa_handler, &sc->handler)
		    || __put_user(oldset->sig[0], &sc->oldmask)
		    || __put_user((oldset->sig[0] >> 32), &sc->_unused[3])
		    || __put_user((unsigned int)frame, &sc->regs)
		    || __put_user(sig, &sc->signal))
			goto badframe;
	}

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
	return;

badframe:
#if DEBUG_SIG
	printk("badframe in handle_signal32, regs=%p frame=%lx newsp=%lx\n",
	       regs, frame, *newspp);
	printk("sc=%p sig=%d ka=%p info=%p oldset=%p\n", sc, sig, ka, info, oldset);
#endif
	do_exit(SIGSEGV);
}


/*
 *  Start Alternate signal stack support
 *
 *  System Calls
 *       sigaltatck               sys32_sigaltstack
 */

int sys32_sigaltstack(u32 newstack, u32 oldstack, int p3,
		      int p4, int p6, int p7, struct pt_regs *regs)
{
	stack_t uss, uoss;
	int ret;
	mm_segment_t old_fs;
	unsigned long sp;

	/*
	 * set sp to the user stack on entry to the system call
	 * the system call router sets R9 to the saved registers
	 */
	sp = regs->gpr[1];

	/* Put new stack info in local 64 bit stack struct */
	if (newstack &&
		(get_user((long)uss.ss_sp,
			  &((stack_32_t *)(long)newstack)->ss_sp) ||
		 __get_user(uss.ss_flags,
			 &((stack_32_t *)(long)newstack)->ss_flags) ||
		 __get_user(uss.ss_size,
			 &((stack_32_t *)(long)newstack)->ss_size)))
		return -EFAULT; 

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = do_sigaltstack(newstack ? &uss : NULL, oldstack ? &uoss : NULL,
			sp);
	set_fs(old_fs);
	/* Copy the stack information to the user output buffer */
	if (!ret && oldstack  &&
		(put_user((long)uoss.ss_sp,
			  &((stack_32_t *)(long)oldstack)->ss_sp) ||
		 __put_user(uoss.ss_flags,
			 &((stack_32_t *)(long)oldstack)->ss_flags) ||
		 __put_user(uoss.ss_size,
			 &((stack_32_t *)(long)oldstack)->ss_size)))
		return -EFAULT;
	return ret;
}



/*
 *  Start of do_signal32 routine
 *
 *   This routine gets control when a pending signal needs to be processed
 *     in the 32 bit target thread -
 *
 *   It handles both rt and non-rt signals
 */

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */

int do_signal32(sigset_t *oldset, struct pt_regs *regs)
{
	siginfo_t info;
	struct k_sigaction *ka;
	unsigned int frame, newsp;
	int signr;

	if (!oldset)
		oldset = &current->blocked;

	newsp = frame = 0;

	signr = get_signal_to_deliver(&info, regs, NULL);
	if (signr > 0) {
		ka = &current->sighand->action[signr-1];
		if ((ka->sa.sa_flags & SA_ONSTACK)
		     && (!on_sig_stack(regs->gpr[1])))
			newsp = (current->sas_ss_sp + current->sas_ss_size);
		else
			newsp = regs->gpr[1];
		newsp = frame = newsp - sizeof(struct sigregs32);

		/* Whee!  Actually deliver the signal.  */
		handle_signal32(signr, &info, oldset, regs, &newsp, frame);
	}

	if (regs->trap == 0x0C00) {	/* System Call! */
		if ((int)regs->result == -ERESTARTNOHAND ||
		    (int)regs->result == -ERESTARTSYS ||
		    (int)regs->result == -ERESTARTNOINTR) {
			regs->gpr[3] = regs->orig_gpr3;
			regs->nip -= 4; /* Back up & retry system call */
			regs->result = 0;
		} else if ((int)regs->result == -ERESTART_RESTARTBLOCK) {
			regs->gpr[0] = __NR_restart_syscall;
			regs->nip -= 4;
			regs->result = 0;
		}
	}

	if (newsp == frame)
		return 0;		/* no signals delivered */

	/* Invoke correct stack setup routine */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame32(regs, (struct sigregs32*)(u64)frame, newsp);
	else
		setup_frame32(regs, (struct sigregs32*)(u64)frame, newsp);
	return 1;
}
