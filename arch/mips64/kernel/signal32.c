/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
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

#include <asm/asm.h>
#include <asm/bitops.h>
#include <asm/pgalloc.h>
#include <asm/stackframe.h>
#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include <asm/system.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

extern asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
                         int options, unsigned long *ru);
extern asmlinkage int do_signal32(sigset_t *oldset, struct pt_regs *regs);
extern asmlinkage int save_fp_context(struct sigcontext *sc);
extern asmlinkage int restore_fp_context(struct sigcontext *sc);

extern asmlinkage void syscall_trace(void);

/* 32-bit compatibility types */

#define _NSIG32_BPW	32
#define _NSIG32_WORDS	(_NSIG / _NSIG32_BPW)

typedef struct {
	unsigned int sig[_NSIG32_WORDS];
} sigset32_t;

typedef unsigned int __sighandler32_t;
typedef void (*vfptr_t)(void);

struct sigaction32 {
	unsigned int		sa_flags;
	__sighandler32_t	sa_handler;
	sigset32_t		sa_mask;
	unsigned int		sa_restorer;
	int			sa_resv[1];     /* reserved */
};

/* IRIX compatible stack_t  */
typedef struct sigaltstack32 {
	s32 ss_sp;
	__kernel_size_t32 ss_size;
	int ss_flags;
} stack32_t;


static inline int store_fp_context(struct sigcontext *sc)
{
	unsigned int fcr0;
	int err = 0;

	err |= __copy_to_user(&sc->sc_fpregs[0], 
		&current->thread.fpu.hard.fp_regs[0], NUM_FPU_REGS * 
						sizeof(unsigned long));
	err |= __copy_to_user(&sc->sc_fpc_csr, &current->thread.fpu.hard.control,
						sizeof(unsigned int));
	__asm__ __volatile__("cfc1 %0, $0\n\t" : "=r" (fcr0));
	err |= __copy_to_user(&sc->sc_fpc_eir, &fcr0, sizeof(unsigned int));

	return err;
}

static inline int refill_fp_context(struct sigcontext *sc)
{
	int err = 0;

	if (verify_area(VERIFY_READ, sc, sizeof(*sc)))
		return -EFAULT;
	err |= __copy_from_user(&current->thread.fpu.hard.fp_regs[0], 
			&sc->sc_fpregs[0], NUM_FPU_REGS * sizeof(unsigned long));
	err |= __copy_from_user(&current->thread.fpu.hard.control, &sc->sc_fpc_csr,
							sizeof(unsigned int));
	return err;
}

extern void __put_sigset_unknown_nsig(void);
extern void __get_sigset_unknown_nsig(void);

static inline int
put_sigset(const sigset_t *kbuf, sigset32_t *ubuf)
{
	int err = 0;

	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(*ubuf)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	default:
		__put_sigset_unknown_nsig();
	case 2:
		err |= __put_user (kbuf->sig[1] >> 32, &ubuf->sig[3]);
		err |= __put_user (kbuf->sig[1] & 0xffffffff, &ubuf->sig[2]);
	case 1:
		err |= __put_user (kbuf->sig[0] >> 32, &ubuf->sig[1]);
		err |= __put_user (kbuf->sig[0] & 0xffffffff, &ubuf->sig[0]);
	}

	return err;
}

static inline int
get_sigset(sigset_t *kbuf, const sigset32_t *ubuf)
{
	int err = 0;
	unsigned long sig[4];

	if (!access_ok(VERIFY_READ, ubuf, sizeof(*ubuf)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	default:
		__get_sigset_unknown_nsig();
	case 2:
		err |= __get_user (sig[3], &ubuf->sig[3]);
		err |= __get_user (sig[2], &ubuf->sig[2]);
		kbuf->sig[1] = sig[2] | (sig[3] << 32);
	case 1:
		err |= __get_user (sig[1], &ubuf->sig[1]);
		err |= __get_user (sig[0], &ubuf->sig[0]);
		kbuf->sig[0] = sig[0] | (sig[1] << 32);
	}

	return err;
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage inline int
sys32_sigsuspend(abi64_no_regargs, struct pt_regs regs)
{
	sigset32_t *uset;
	sigset_t newset, saveset;

#if DEBUG_MIPS64
printk("%s called.\n", __FUNCTION__);
#endif

	save_static(&regs);
	uset = (sigset32_t *) regs.regs[4];
	if (get_sigset(&newset, uset))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	regs.regs[2] = EINTR;
	regs.regs[7] = 1;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal32(&saveset, &regs))
			return -EINTR;
	}
}

asmlinkage int
sys32_rt_sigsuspend(abi64_no_regargs, struct pt_regs regs)
{
	sigset32_t *uset;
	sigset_t newset, saveset;
        size_t sigsetsize;

#if DEBUG_MIPS64
printk("%s called.\n", __FUNCTION__);
#endif

	save_static(&regs);
	/* XXX Don't preclude handling different sized sigset_t's.  */
	sigsetsize = regs.regs[5];
	if (sigsetsize != sizeof(sigset32_t))
		return -EINVAL;

	uset = (sigset32_t *) regs.regs[4];
	if (get_sigset(&newset, uset))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
        recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	regs.regs[2] = EINTR;
	regs.regs[7] = 1;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal32(&saveset, &regs))
			return -EINTR;
	}
}

asmlinkage int sys32_sigaction(int sig, const struct sigaction32 *act,
                               struct sigaction32 *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	int err = 0;

	if (act) {
		old_sigset_t mask;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user((u32)(u64)new_ka.sa.sa_handler,
		                  &act->sa_handler);
		err |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		err |= __get_user(mask, &act->sa_mask.sig[0]);
		err |= __get_user((u32)(u64)new_ka.sa.sa_restorer,
		                   &act->sa_restorer);
		if (err)
			return -EFAULT;

		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)))
                        return -EFAULT;
		err |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		err |= __put_user((u32)(u64)old_ka.sa.sa_handler,
		                  &oact->sa_handler);
		err |= __put_user(old_ka.sa.sa_mask.sig[0], oact->sa_mask.sig);
                err |= __put_user(0, &oact->sa_mask.sig[1]);
                err |= __put_user(0, &oact->sa_mask.sig[2]);
                err |= __put_user(0, &oact->sa_mask.sig[3]);
		err |= __put_user((u32)(u64)old_ka.sa.sa_restorer,
		                  &oact->sa_restorer);
                if (err)
			return -EFAULT;
	}

	return ret;
}

asmlinkage int
sys32_sigaltstack(abi64_no_regargs, struct pt_regs regs)
{
	const stack32_t *uss = (const stack32_t *) regs.regs[4];
	stack32_t *uoss = (stack32_t *) regs.regs[5];
	unsigned long usp = regs.regs[29];
	stack_t kss, koss;
	int ret, err = 0;
	mm_segment_t old_fs = get_fs();
	s32 sp;

	if (uss) {
		if (!access_ok(VERIFY_READ, uss, sizeof(*uss)))
			return -EFAULT;
		err |= __get_user(sp, &uss->ss_sp);
		kss.ss_size = (long) sp;
		err |= __get_user(kss.ss_size, &uss->ss_size);
		err |= __get_user(kss.ss_flags, &uss->ss_flags);
		if (err)
			return -EFAULT;
	}

	set_fs (KERNEL_DS);
	ret = do_sigaltstack(uss ? &kss : NULL , uoss ? &koss : NULL, usp);
	set_fs (old_fs);

	if (!ret && uoss) {
		if (!access_ok(VERIFY_WRITE, uoss, sizeof(*uoss)))
			return -EFAULT;
		sp = (int) (long) koss.ss_sp;
		err |= __put_user(sp, &uoss->ss_sp);
		err |= __put_user(koss.ss_size, &uoss->ss_size);
		err |= __put_user(koss.ss_flags, &uoss->ss_flags);
		if (err)
			return -EFAULT;
	}
	return ret;
}

static asmlinkage int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc)
{
	int owned_fp;
	int err = 0;

	err |= __get_user(regs->cp0_epc, &sc->sc_pc);
	err |= __get_user(regs->hi, &sc->sc_mdhi);
	err |= __get_user(regs->lo, &sc->sc_mdlo);

#define restore_gp_reg(i) do {						\
	err |= __get_user(regs->regs[i], &sc->sc_regs[i]);		\
} while(0)
	restore_gp_reg( 1); restore_gp_reg( 2); restore_gp_reg( 3);
	restore_gp_reg( 4); restore_gp_reg( 5); restore_gp_reg( 6);
	restore_gp_reg( 7); restore_gp_reg( 8); restore_gp_reg( 9);
	restore_gp_reg(10); restore_gp_reg(11); restore_gp_reg(12);
	restore_gp_reg(13); restore_gp_reg(14); restore_gp_reg(15);
	restore_gp_reg(16); restore_gp_reg(17); restore_gp_reg(18);
	restore_gp_reg(19); restore_gp_reg(20); restore_gp_reg(21);
	restore_gp_reg(22); restore_gp_reg(23); restore_gp_reg(24);
	restore_gp_reg(25); restore_gp_reg(26); restore_gp_reg(27);
	restore_gp_reg(28); restore_gp_reg(29); restore_gp_reg(30);
	restore_gp_reg(31);
#undef restore_gp_reg

	err |= __get_user(owned_fp, &sc->sc_ownedfp);
	if (owned_fp) {
		if (IS_FPU_OWNER()) {
			CLEAR_FPU_OWNER();
			regs->cp0_status &= ~ST0_CU1;
		}
		current->used_math = 1;
		err |= refill_fp_context(sc);
	}

	return err;
}

struct sigframe {
	u32 sf_ass[4];			/* argument save space for o32 */
	u32 sf_code[2];			/* signal trampoline */
	struct sigcontext sf_sc;
	sigset_t sf_mask;
};

struct rt_sigframe {
	u32 rs_ass[4];			/* argument save space for o32 */
	u32 rs_code[2];			/* signal trampoline */
	struct siginfo rs_info;
	struct ucontext rs_uc;
};

asmlinkage void
sys32_sigreturn(abi64_no_regargs, struct pt_regs regs)
{
	struct sigframe *frame;
	sigset_t blocked;

#if DEBUG_MIPS64
printk("%s called.\n", __FUNCTION__);
#endif

	frame = (struct sigframe *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&blocked, &frame->sf_mask, sizeof(blocked)))
		goto badframe;

	sigdelsetmask(&blocked, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = blocked;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	if (restore_sigcontext(&regs, &frame->sf_sc))
		goto badframe;

	/*
	 * Don't let your children do this ...
	 */
	if (current->ptrace & PT_TRACESYS)
		syscall_trace();
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tret_from_sys_call"
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

asmlinkage void
sys32_rt_sigreturn(abi64_no_regargs, struct pt_regs regs)
{
	struct rt_sigframe *frame;
	sigset_t set;
	stack_t st;
printk("%s called.\n", __FUNCTION__);

	frame = (struct rt_sigframe *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	if (restore_sigcontext(&regs, &frame->rs_uc.uc_mcontext))
		goto badframe;

	if (__copy_from_user(&st, &frame->rs_uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs.regs[29]);

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tret_from_sys_call"
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

static int inline
setup_sigcontext(struct pt_regs *regs, struct sigcontext *sc)
{
	int err = 0;

	err |= __put_user(regs->cp0_epc, &sc->sc_pc);

#define save_gp_reg(i) {						\
	err |= __put_user(regs->regs[i], &sc->sc_regs[i]);		\
} while(0)
	__put_user(0, &sc->sc_regs[0]); save_gp_reg(1); save_gp_reg(2);
	save_gp_reg(3); save_gp_reg(4); save_gp_reg(5); save_gp_reg(6);
	save_gp_reg(7); save_gp_reg(8); save_gp_reg(9); save_gp_reg(10);
	save_gp_reg(11); save_gp_reg(12); save_gp_reg(13); save_gp_reg(14);
	save_gp_reg(15); save_gp_reg(16); save_gp_reg(17); save_gp_reg(18);
	save_gp_reg(19); save_gp_reg(20); save_gp_reg(21); save_gp_reg(22);
	save_gp_reg(23); save_gp_reg(24); save_gp_reg(25); save_gp_reg(26);
	save_gp_reg(27); save_gp_reg(28); save_gp_reg(29); save_gp_reg(30);
	save_gp_reg(31);
#undef save_gp_reg

	err |= __put_user(regs->hi, &sc->sc_mdhi);
	err |= __put_user(regs->lo, &sc->sc_mdlo);
	err |= __put_user(regs->cp0_cause, &sc->sc_cause);
	err |= __put_user(regs->cp0_badvaddr, &sc->sc_badvaddr);

	if (current->used_math) {	/* fp is active.  */
		if (IS_FPU_OWNER()) {
			lazy_fpu_switch(current, 0);
			CLEAR_FPU_OWNER();
			regs->cp0_status &= ~ST0_CU1;
		}
		err |= __put_user(1, &sc->sc_ownedfp);
		err |= store_fp_context(sc);
		current->used_math = 0;
	} else {
		err |= __put_user(0, &sc->sc_ownedfp);
	}
	err |= __put_user(regs->cp0_status, &sc->sc_status);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->regs[29];

	/* This is the X/Open sanctioned signal stack switching.  */
	if ((ka->sa.sa_flags & SA_ONSTACK) && ! on_sig_stack(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void *)((sp - frame_size) & ALMASK);
}

static void inline
setup_frame(struct k_sigaction * ka, struct pt_regs *regs,
            int signr, sigset_t *set)
{
	struct sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub already
	   in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER)
		regs->regs[31] = (unsigned long) ka->sa.sa_restorer;
	else {
		/*
		 * Set up the return code ...
		 *
		 *         li      v0, __NR_Linux32_sigreturn
		 *         syscall
		 */
		err |= __put_user(0x24020000 + __NR_Linux32_sigreturn,
		                  frame->sf_code + 0);
		err |= __put_user(0x0000000c                 ,
		                  frame->sf_code + 1);
		flush_cache_sigtramp((unsigned long) frame->sf_code);
	}

	err |= setup_sigcontext(regs, &frame->sf_sc);
	err |= __copy_to_user(&frame->sf_mask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to struct sigcontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to the
	 * struct sigframe.
	 */
	regs->regs[ 4] = signr;
	regs->regs[ 5] = 0;
	regs->regs[ 6] = (unsigned long) &frame->sf_sc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) frame->sf_code;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=0x%p pc=0x%p ra=0x%p\n",
	       current->comm, current->pid, frame, regs->cp0_epc, frame->code);
#endif
        return;

give_sigsegv:
	if (signr == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void inline
setup_rt_frame(struct k_sigaction * ka, struct pt_regs *regs,
               int signr, sigset_t *set, siginfo_t *info)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub already
	   in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER)
		regs->regs[31] = (unsigned long) ka->sa.sa_restorer;
	else {
		/*
		 * Set up the return code ...
		 *
		 *         li      v0, __NR_Linux32_sigreturn
		 *         syscall
		 */
		err |= __put_user(0x24020000 + __NR_Linux32_sigreturn,
		                  frame->rs_code + 0);
		err |= __put_user(0x0000000c                 ,
		                  frame->rs_code + 1);
		flush_cache_sigtramp((unsigned long) frame->rs_code);
	}

	/* Create siginfo.  */
	err |= __copy_to_user(&frame->rs_info, info, sizeof(*info));

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->rs_uc.uc_flags);
	err |= __put_user(0, &frame->rs_uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
	                  &frame->rs_uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->regs[29]),
	                  &frame->rs_uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size,
	                  &frame->rs_uc.uc_stack.ss_size);
	err |= setup_sigcontext(regs, &frame->rs_uc.uc_mcontext);
	err |= __copy_to_user(&frame->rs_uc.uc_sigmask, set, sizeof(*set));

	if (err)
		goto give_sigsegv;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to ucontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to
	 * the struct rt_sigframe.
	 */
	regs->regs[ 4] = signr;
	regs->regs[ 5] = (unsigned long) &frame->rs_info;
	regs->regs[ 6] = (unsigned long) &frame->rs_uc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) frame->rs_code;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=0x%p pc=0x%p ra=0x%p\n",
	       current->comm, current->pid, frame, regs->cp0_epc, frame->code);
#endif
	return;

give_sigsegv:
	if (signr == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static inline void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(ka, regs, sig, oldset, info);
	else
		setup_frame(ka, regs, sig, oldset);

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

static inline void
syscall_restart(struct pt_regs *regs, struct k_sigaction *ka)
{
	switch(regs->regs[0]) {
	case ERESTARTNOHAND:
		regs->regs[2] = EINTR;
		break;
	case ERESTARTSYS:
		if(!(ka->sa.sa_flags & SA_RESTART)) {
			regs->regs[2] = EINTR;
			break;
		}
	/* fallthrough */
	case ERESTARTNOINTR:		/* Userland will reload $v0.  */
		regs->regs[7] = regs->regs[26];
		regs->cp0_epc -= 8;
	}

	regs->regs[0] = 0;		/* Don't deal with this again.  */
}

asmlinkage int do_signal32(sigset_t *oldset, struct pt_regs *regs)
{
	struct k_sigaction *ka;
	siginfo_t info;

#if DEBUG_MIPS64
printk("%s: delivering signal.\n", current->comm);
#endif

	if (!oldset)
		oldset = &current->blocked;

	for (;;) {
		unsigned long signr;

		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);

		if (!signr)
			break;

		if ((current->ptrace & PT_PTRACED) && signr != SIGKILL) {
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
			case SIGBUS: case SIGSYS: case SIGXCPU: case SIGXFSZ:
				if (do_coredump(signr, regs))
					exit_code |= 0x80;
				/* FALLTHRU */

			default:
				sigaddset(&current->pending.signal, signr);
				recalc_sigpending(current);
				current->flags |= PF_SIGNALED;
				do_exit(exit_code);
				/* NOTREACHED */
			}
		}

		if (regs->regs[0])
			syscall_restart(regs, ka);
		/* Whee!  Actually deliver the signal.  */
#if DEBUG_MIPS64
printk("%s: delivering signal.\n", __FUNCTION__);
#endif
		handle_signal(signr, ka, &info, oldset, regs);
		return 1;
	}

	/*
	 * Who's code doesn't conform to the restartable syscall convention
	 * dies here!!!  The li instruction, a single machine instruction,
	 * must directly be followed by the syscall instruction.
	 */
	if (regs->regs[0]) {
		if (regs->regs[2] == ERESTARTNOHAND ||
		    regs->regs[2] == ERESTARTSYS ||
		    regs->regs[2] == ERESTARTNOINTR) {
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 8;
		}
	}
	return 0;
}

extern asmlinkage int sys_sigprocmask(int how, old_sigset_t *set,
						old_sigset_t *oset);

asmlinkage int sys32_sigprocmask(int how, old_sigset_t32 *set, 
				 old_sigset_t32 *oset)
{
	old_sigset_t s;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (set && get_user (s, set))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL);
	set_fs (old_fs);
	if (!ret && oset && put_user (s, oset))
		return -EFAULT;
	return ret;
}

asmlinkage long sys_sigpending(old_sigset_t *set);

asmlinkage int sys32_sigpending(old_sigset_t32 *set)
{
	old_sigset_t pending;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs (KERNEL_DS);
	ret = sys_sigpending(&pending);
	set_fs (old_fs);

	if (put_user(pending, set))
		return -EFAULT;

	return ret;
}

asmlinkage int sys32_rt_sigaction(int sig, const struct sigaction32 *act,
				  struct sigaction32 *oact,
				  unsigned int sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		int err = 0;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user((u32)(u64)new_sa.sa.sa_handler,
		                  &act->sa_handler);
		err |= __get_user(new_sa.sa.sa_flags, &act->sa_flags);
		err |= __get_user((u32)(u64)new_sa.sa.sa_restorer,
		                  &act->sa_restorer);
		err |= get_sigset(&new_sa.sa.sa_mask, &act->sa_mask);
		if (err)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		int err = 0;

		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)))
			return -EFAULT;

		err |= __put_user((u32)(u64)old_sa.sa.sa_handler,
		                   &oact->sa_handler);
		err |= __put_user(old_sa.sa.sa_flags, &oact->sa_flags);
		err |= __put_user((u32)(u64)old_sa.sa.sa_restorer,
		                  &oact->sa_restorer);
		err |= put_sigset(&old_sa.sa.sa_mask, &oact->sa_mask);
		if (err)
			return -EFAULT;
	}
out:
	return ret;
}

asmlinkage long sys_rt_sigprocmask(int how, sigset_t *set, sigset_t *oset,
				   size_t sigsetsize);

asmlinkage int sys32_rt_sigprocmask(int how, sigset32_t *set, sigset32_t *oset,
				    unsigned int sigsetsize)
{
	sigset_t old_set, new_set;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (set && get_sigset(&new_set, set))
		return -EFAULT;
	
	set_fs (KERNEL_DS);
	ret = sys_rt_sigprocmask(how, set ? &new_set : NULL,
				 oset ? &old_set : NULL, sigsetsize);
	set_fs (old_fs);

	if (!ret && oset && put_sigset(&old_set, oset))
		return -EFAULT;

	return ret;
}

asmlinkage long sys_rt_sigpending(sigset_t *set, size_t sigsetsize);

asmlinkage int sys32_rt_sigpending(sigset32_t *uset, unsigned int sigsetsize)
{
	int ret;
	sigset_t set;
	mm_segment_t old_fs = get_fs();

	set_fs (KERNEL_DS);
	ret = sys_rt_sigpending(&set, sigsetsize);
	set_fs (old_fs);

	if (!ret && put_sigset(&set, uset))
		return -EFAULT;

	return ret;
}

asmlinkage void sys32_rt_sigtimedwait(void) { panic(__FUNCTION__ " called."); }
asmlinkage void sys32_rt_sigqueueinfo(void) { panic(__FUNCTION__ " called."); }
