/*
 *  linux/arch/h8300/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * uClinux H8/300 support by Yoshinori Sato <ysato@users.sourceforge.jp>
 *                and David McCullough <davidm@snapgear.com>
 *
 * Based on
 * Linux/m68k by Hamish Macdonald
 */

/*
 * ++roman (07/09/96): implemented signal stacks (specially for tosemu on
 * Atari :-) Current limitation: Only one sigstack can be active at one time.
 * If a second signal with SA_ONSTACK set arrives while working on a sigstack,
 * SA_ONSTACK is ignored. This behaviour avoids lots of trouble with nested
 * signal handlers!
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/highuid.h>
#include <linux/personality.h>
#include <linux/tty.h>
#include <linux/binfmts.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/ucontext.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage long sys_wait4(pid_t pid, unsigned int * stat_addr, int options,
			struct rusage * ru);
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int do_sigsuspend(struct pt_regs *regs)
{
	old_sigset_t mask = regs->er3;
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->er0 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return -EINTR;
	}
}

asmlinkage int
do_rt_sigsuspend(struct pt_regs *regs)
{
	sigset_t *unewset = (sigset_t *)regs->er1;
	size_t sigsetsize = (size_t)regs->er2;
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->er0 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
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
	return do_sigaltstack(uss, uoss, rdusp());
}


/*
 * Do a signal return; undo the signal stack.
 *
 * Keep the return code on the stack quadword aligned!
 * That makes the cache flush below easier.
 */

struct sigframe
{
	long dummy_er0;
	long dummy_vector;
#if defined(CONFIG_CPU_H8S)
	short dummy_exr;
#endif
	long dummy_pc;
	char *pretcode;
	unsigned char retcode[8];
	unsigned long extramask[_NSIG_WORDS-1];
	struct sigcontext sc;
} __attribute__((aligned(2),packed));

struct rt_sigframe
{
	long dummy_er0;
	long dummy_vector;
#if defined(CONFIG_CPU_H8S)
	short dummy_exr;
#endif
	long dummy_pc;
	char *pretcode;
	unsigned char retcode[8];
	struct siginfo info;
	struct ucontext uc;
} __attribute__((aligned(2),packed));

static inline int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *usc, void *fp,
		   int *pd0)
{
	struct sigcontext context;
	int err = 0;

	/* get previous context */
	if (copy_from_user(&context, usc, sizeof(context)))
		goto badframe;
	
	/* restore passed registers */
	regs->er1 = context.sc_er1;
	regs->er2 = context.sc_er2;
	regs->er3 = context.sc_er3;
	regs->ccr = (regs->ccr & 0x10)|(context.sc_ccr & 0xef);
	regs->pc = context.sc_pc;
	regs->orig_er0 = -1;		/* disable syscall checks */
	wrusp(context.sc_usp);

	*pd0 = context.sc_er0;
	return err;

badframe:
	return 1;
}

static inline int
rt_restore_ucontext(struct pt_regs *regs, struct ucontext *uc, int *pd0)
{
	int temp;
	greg_t *gregs = uc->uc_mcontext.gregs;
	unsigned long usp;
	int err;

	err = __get_user(temp, &uc->uc_mcontext.version);
	if (temp != MCONTEXT_VERSION)
		goto badframe;
	/* restore passed registers */
	err |= __get_user(regs->er0, &gregs[0]);
	err |= __get_user(regs->er1, &gregs[1]);
	err |= __get_user(regs->er2, &gregs[2]);
	err |= __get_user(regs->er3, &gregs[3]);
	err |= __get_user(regs->er4, &gregs[4]);
	err |= __get_user(regs->er5, &gregs[5]);
	err |= __get_user(regs->er6, &gregs[6]);
	err |= __get_user(usp, &gregs[7]);
	wrusp(usp);
	err |= __get_user(regs->pc, &gregs[8]);
	err |= __get_user(temp, &gregs[9]);
	regs->ccr = (regs->ccr & 0x10) | (temp & 0xef);
	regs->orig_er0 = -1;		/* disable syscall checks */

	if (do_sigaltstack(&uc->uc_stack, NULL, usp) == -EFAULT)
		goto badframe;

	*pd0 = regs->er0;
	return err;

badframe:
	return 1;
}

asmlinkage int do_sigreturn(unsigned long __unused,...)
{
	struct pt_regs *regs = (struct pt_regs *) (&__unused - 1);
	unsigned long usp = rdusp();
	struct sigframe *frame = (struct sigframe *)(usp - 4);
	sigset_t set;
	int er0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.sc_mask) ||
	    (_NSIG_WORDS > 1 &&
	     __copy_from_user(&set.sig[1], &frame->extramask,
			      sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (restore_sigcontext(regs, &frame->sc, frame + 1, &er0))
		goto badframe;
	return er0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int do_rt_sigreturn(unsigned long __unused,...)
{
	struct pt_regs *regs = (struct pt_regs *) &__unused;
	unsigned long usp = rdusp();
	struct rt_sigframe *frame = (struct rt_sigframe *)(usp - 4);
	sigset_t set;
	int er0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_unlock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_lock_irq(&current->sighand->siglock);
	
	if (rt_restore_ucontext(regs, &frame->uc, &er0))
		goto badframe;
	return er0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static void setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs,
			     unsigned long mask)
{
	sc->sc_mask = mask;
	sc->sc_usp = rdusp();
	sc->sc_er0 = regs->er0;
	sc->sc_er1 = regs->er1;
	sc->sc_er2 = regs->er2;
	sc->sc_er3 = regs->er3;
	sc->sc_ccr = regs->ccr;
	sc->sc_pc = regs->pc;
}

static inline int rt_setup_ucontext(struct ucontext *uc, struct pt_regs *regs)
{
	greg_t *gregs = uc->uc_mcontext.gregs;
	int err = 0;

	err |= __put_user(MCONTEXT_VERSION, &uc->uc_mcontext.version);
	err |= __put_user(regs->er0, &gregs[0]);
	err |= __put_user(regs->er1, &gregs[1]);
	err |= __put_user(regs->er2, &gregs[2]);
	err |= __put_user(regs->er3, &gregs[3]);
	err |= __put_user(regs->er4, &gregs[4]);
	err |= __put_user(regs->er5, &gregs[5]);
	err |= __put_user(regs->er6, &gregs[6]);
	err |= __put_user(rdusp(), &gregs[7]);
	err |= __put_user(regs->pc, &gregs[8]);
	err |= __put_user(regs->ccr, &gregs[9]);
	return err;
}

static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	unsigned long usp;

	/* Default to using normal stack.  */
	usp = rdusp();

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (!on_sig_stack(usp))
			usp = current->sas_ss_sp + current->sas_ss_size;
	}
	return (void *)((usp - frame_size) & -8UL);
}

static void setup_frame (int sig, struct k_sigaction *ka,
			 sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	struct sigcontext context;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (_NSIG_WORDS > 1)
		err |= copy_to_user(frame->extramask, &set->sig[1],
				    sizeof(frame->extramask));

	setup_sigcontext(&context, regs, set->sig[0]);
	err |= copy_to_user (&frame->sc, &context, sizeof(context));

	/* Set up to return from userspace.  */
	err |= __put_user(frame->retcode, &frame->pretcode);

	/* sub.l er0,er0; mov.b #__NR_sigreturn,r0l; trapa #0 */
	err != __put_user(0x1a80f800 + (__NR_sigreturn & 0xff),
			(unsigned long *)(frame->retcode + 0));
	err |= __put_user(0x5700, (unsigned short *)(frame->retcode + 4));


	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	wrusp ((unsigned long) frame);
	regs->pc = (unsigned long) ka->sa.sa_handler;
	regs->er0 = (current_thread_info()->exec_domain
			   && current_thread_info()->exec_domain->signal_invmap
			   && sig < 32
			   ? current_thread_info()->exec_domain->signal_invmap[sig]
		          : sig);
	regs->er1 = (unsigned long)&(frame->sc);

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame (int sig, struct k_sigaction *ka, siginfo_t *info,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	err |= copy_siginfo_to_user(&frame->info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(rdusp()),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= rt_setup_ucontext(&frame->uc, regs);
	err |= copy_to_user (&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  */
	err |= __put_user(frame->retcode, &frame->pretcode);

	/* sub.l er0,er0; mov.b #__NR_rt_sigreturn,r0l; trapa #0 */
	err != __put_user(0x1a80f800 + (__NR_rt_sigreturn & 0xff),
			(long *)(frame->retcode + 0));
	err |= __put_user(0x5700, (short *)(frame->retcode + 4));

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	wrusp ((unsigned long) frame);
	regs->pc  = (unsigned long) ka->sa.sa_handler;
	regs->er0 = (current_thread_info()->exec_domain
		     && current_thread_info()->exec_domain->signal_invmap
		     && sig < 32
		     ? current_thread_info()->exec_domain->signal_invmap[sig]
		     : sig);
	regs->er1 = (unsigned long)&(frame->info);
	regs->er2 = (unsigned long)&frame->uc;

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static inline void
handle_restart(struct pt_regs *regs, struct k_sigaction *ka, int has_handler)
{
	switch (regs->er0) {
	case -ERESTARTNOHAND:
		if (!has_handler)
			goto do_restart;
		regs->er0 = -EINTR;
		break;

	case -ERESTARTSYS:
		if (has_handler && !(ka->sa.sa_flags & SA_RESTART)) {
			regs->er0 = -EINTR;
			break;
		}
	/* fallthrough */
	case -ERESTARTNOINTR:
	do_restart:
		regs->er0 = regs->orig_er0;
		regs->pc -= 2;
		break;
	}
}

/*
 * OK, we're invoking a handler
 */
static void
handle_signal(int sig, struct k_sigaction *ka, siginfo_t *info,
	      sigset_t *oldset, struct pt_regs *regs)
{
	/* are we from a system call? */
	if (regs->orig_er0 >= 0)
		/* If so, check system call restarting.. */
		handle_restart(regs, ka, 1);

	/* set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals
 * that the kernel can handle, and then we build all the user-level signal
 * handling stack-frames in one go after that.
 */
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	siginfo_t info;
	struct k_sigaction *ka;

	current->thread.esp0 = (unsigned long) regs;

	if (!oldset)
		oldset = &current->blocked;

	for (;;) {
		int signr;

		signr = get_signal_to_deliver(&info, regs, NULL);

		if (!signr)
			break;

		if ((current->ptrace & PT_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;

			/* Did we come from a system call? */
			if (regs->orig_er0 >= 0) {
				/* Restart the system call the same way as
				   if the process were not traced.  */
				struct k_sigaction *ka =
					&current->sighand->action[signr-1];
				int has_handler =
					(ka->sa.sa_handler != SIG_IGN &&
					 ka->sa.sa_handler != SIG_DFL);
				handle_restart(regs, ka, has_handler);
			}
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code)) {
			discard_frame:
			    continue;
			}
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				goto discard_frame;

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->parent->pid;
				info.si_uid = current->parent->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}

		ka = &current->sighand->action[signr-1];
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

			if (current->pid == 1)
				continue;

			switch (signr) {
			case SIGCONT: case SIGCHLD:
			case SIGWINCH: case SIGURG:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(process_group(current)))
					continue;
				/* FALLTHRU */

			case SIGSTOP: {
				struct sighand_struct *sig;
				current->state = TASK_STOPPED;
				current->exit_code = signr;
                                sig = current->parent->sighand;
                                if (sig && !(sig->action[SIGCHLD-1].sa.sa_flags 
& SA_NOCLDSTOP))
                                        notify_parent(current, SIGCHLD);
				schedule();
				continue;
			}

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGIOT: case SIGFPE: case SIGSEGV:
			case SIGBUS: case SIGSYS: case SIGXCPU: case SIGXFSZ:
				if (do_coredump(signr, exit_code, regs))
					exit_code |= 0x80;
				/* FALLTHRU */

			default:
				sigaddset(&current->pending.signal, signr);
				recalc_sigpending();
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
	if (regs->orig_er0 >= 0)
		/* Restart the system call - no handlers present */
		handle_restart(regs, NULL, 0);

	return 0;
}
