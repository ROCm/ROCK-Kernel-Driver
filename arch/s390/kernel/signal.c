/*
 *  arch/s390/kernel/signal.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *    Based on Intel version
 * 
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/* pretcode & sig are used to store the return addr on Intel
   & the signal no as the first parameter we do this differently
   using gpr14 & gpr2. */

#define SIGFRAME_COMMON \
__u8     callee_used_stack[__SIGNAL_FRAMESIZE]; \
struct sigcontext sc; \
sigregs sregs; \
__u8 retcode[S390_SYSCALL_SIZE];

typedef struct 
{
	SIGFRAME_COMMON
} sigframe;

typedef struct 
{
	SIGFRAME_COMMON
	struct siginfo info;
	struct ucontext uc;
} rt_sigframe;

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);
asmlinkage int FASTCALL(do_signal(struct pt_regs *regs, sigset_t *oldset));

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(struct pt_regs * regs,int history0, int history1, old_sigset_t mask)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	regs->gprs[2] = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(struct pt_regs * regs,sigset_t *unewset, size_t sigsetsize)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	regs->gprs[2] = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage int
sys_sigaltstack(const stack_t *uss, stack_t *uoss)
{
	struct pt_regs *regs = (struct pt_regs *) &uss;
	return do_sigaltstack(uss, uoss, regs->gprs[15]);
}




static int save_sigregs(struct pt_regs *regs,sigregs *sregs)
{
	int err;
	s390_fp_regs fpregs;
  
	err = __copy_to_user(&sregs->regs,regs,sizeof(s390_regs_common));
	if(!err)
	{
		save_fp_regs(&fpregs);
		err=__copy_to_user(&sregs->fpregs,&fpregs,sizeof(fpregs));
	}
	return(err);
	
}

static int restore_sigregs(struct pt_regs *regs,sigregs *sregs)
{
	int err;
	s390_fp_regs fpregs;
	psw_t saved_psw=regs->psw;
	err=__copy_from_user(regs,&sregs->regs,sizeof(s390_regs_common));
	if(!err)
	{
		regs->orig_gpr2 = -1;		/* disable syscall checks */
		regs->psw.mask=(saved_psw.mask&~PSW_MASK_DEBUGCHANGE)|
		(regs->psw.mask&PSW_MASK_DEBUGCHANGE);
		regs->psw.addr=(saved_psw.addr&~PSW_ADDR_DEBUGCHANGE)|
		(regs->psw.addr&PSW_ADDR_DEBUGCHANGE);
		err=__copy_from_user(&fpregs,&sregs->fpregs,sizeof(fpregs));
		if(!err)
			restore_fp_regs(&fpregs);
	}
	return(err);
}

static int
restore_sigcontext(struct sigcontext *sc, pt_regs *regs,
		 sigregs *sregs,sigset_t *set)
{
	unsigned int err;

	err=restore_sigregs(regs,sregs);
	if(!err)
		err=__copy_from_user(&set->sig,&sc->oldmask,SIGMASK_COPY_SIZE);
		return(err);
}

int sigreturn_common(struct pt_regs *regs,int framesize)
{
	sigframe *frame = (sigframe *)regs->gprs[15];
	sigset_t set;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		return -1;
	if (restore_sigcontext(&frame->sc,regs,&frame->sregs,&set))
	        return -1;
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	return 0;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{

	if (sigreturn_common(regs,sizeof(sigframe)))
		goto badframe;
	return regs->gprs[2];

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}	

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	rt_sigframe *frame = (rt_sigframe *)regs->gprs[15];
	stack_t st;

	if (sigreturn_common(regs,sizeof(rt_sigframe)))
		goto badframe;
	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs->gprs[15]);
	return regs->gprs[2];

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}	

/*
 * Set up a signal frame.
 */


/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->gprs[15];

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! on_sig_stack(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* This is the legacy signal stack switching. */
	else if (!user_mode(regs) &&
		 !(ka->sa.sa_flags & SA_RESTORER) &&
		 ka->sa.sa_restorer) {
		sp = (unsigned long) ka->sa.sa_restorer;
	}

	return (void *)((sp - frame_size) & -8ul);
}

static void *setup_frame_common(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs * regs,
				int frame_size,u16 retcode)
{
	sigframe *frame;
	int err;

	frame = get_sigframe(ka, regs,frame_size);
	if (!access_ok(VERIFY_WRITE, frame,frame_size))
		return 0;
	err = save_sigregs(regs,&frame->sregs);
	if(!err)
		err=__put_user(&frame->sregs,&frame->sc.sregs);
	if(!err)

		err=__copy_to_user(&frame->sc.oldmask,&set->sig,SIGMASK_COPY_SIZE);
	if(!err)
	{
		regs->gprs[2]=(current->exec_domain
		           && current->exec_domain->signal_invmap
		           && sig < 32
		           ? current->exec_domain->signal_invmap[sig]
		           : sig);
		/* Set up registers for signal handler */
		regs->gprs[15] = (addr_t)frame;
		regs->psw.addr = FIX_PSW(ka->sa.sa_handler);
	}
	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
                regs->gprs[14] = FIX_PSW(ka->sa.sa_restorer);
	} else {
                regs->gprs[14] = FIX_PSW(frame->retcode);
		err |= __put_user(retcode, (u16 *)(frame->retcode));
	}
	return(err ? 0:frame);
}

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs * regs)
{

	if(!setup_frame_common(sig,ka,set,regs,sizeof(sigframe),
		    (S390_SYSCALL_OPCODE|__NR_sigreturn)))
		goto give_sigsegv;
#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->eip, frame->pretcode);
#endif
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	rt_sigframe *frame;
	addr_t      orig_sp=regs->gprs[15];
	int err;

	if((frame=setup_frame_common(sig,ka,set,regs,sizeof(rt_sigframe),
		    (S390_SYSCALL_OPCODE|__NR_rt_sigreturn)))==0)
		goto give_sigsegv;
	
	err = __copy_to_user(&frame->info, info, sizeof(*info));

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(orig_sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	regs->gprs[3] = (u32)&frame->info;
	regs->gprs[4] = (u32)&frame->uc;

	if (err)
		goto give_sigsegv;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->eip, frame->pretcode);
#endif
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

/*
 * OK, we're invoking a handler
 */	

static void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	/* Are we from a system call? */
	if (regs->orig_gpr2 >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->gprs[2]) {
			case -ERESTARTNOHAND:
				regs->gprs[2] = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->gprs[2] = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->gprs[2] = regs->orig_gpr2;
				regs->psw.addr -= 2;
		}
	}

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
	}
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	struct k_sigaction *ka;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (!oldset)
		oldset = &current->blocked;

	for (;;) {
		unsigned long signr;

		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);

		if (!signr)
			break;

		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->p_pptr->pid;
				info.si_uid = current->p_pptr->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}

		ka = &current->sig->action[signr-1];
		if (ka->sa.sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* Check for SIGCHLD: it's special.  */
			while (sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}

		if (ka->sa.sa_handler == SIG_DFL) {
			int exit_code = signr;

			/* Init gets no signals it doesn't want.  */
			if (current->pid == 1)
				continue;

			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
				/* FALLTHRU */

			case SIGSTOP:
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
                                if (do_coredump(signr, regs))
                                        exit_code |= 0x80;
                                /* FALLTHRU */

			default:
				lock_kernel();
				sigaddset(&current->pending.signal, signr);
				recalc_sigpending(current);
				current->flags |= PF_SIGNALED;
				do_exit(exit_code);
				/* NOTREACHED */
			}
		}

		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, ka, &info, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if ( regs->trap == 0x20 /* System Call! */ ) {
		/* Restart the system call - no handlers present */
		if (regs->gprs[2] == -ERESTARTNOHAND ||
		    regs->gprs[2] == -ERESTARTSYS ||
		    regs->gprs[2] == -ERESTARTNOINTR) {
			regs->gprs[2] = regs->orig_gpr2;
			regs->psw.addr -= 2;
		}
	}
	return 0;
}
