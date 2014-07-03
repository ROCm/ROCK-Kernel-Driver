/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * Handle hardware traps and faults.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/context_tracking.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/kdebug.h>
#include <linux/kgdb.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/uprobes.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kexec.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/nmi.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/io.h>

#ifdef CONFIG_EISA
#include <linux/ioport.h>
#include <linux/eisa.h>
#endif

#if defined(CONFIG_EDAC)
#include <linux/edac.h>
#endif

#include <asm/kmemcheck.h>
#include <asm/stacktrace.h>
#include <asm/processor.h>
#include <asm/debugreg.h>
#include <linux/atomic.h>
#include <asm/ftrace.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/i387.h>
#include <asm/fpu-internal.h>
#include <asm/mce.h>
#include <asm/fixmap.h>
#include <asm/mach_traps.h>
#include <asm/alternative.h>

#ifdef CONFIG_X86_64
#include <asm/x86_init.h>
#include <asm/pgalloc.h>
#include <asm/proto.h>

#ifndef CONFIG_X86_NO_IDT
/* No need to be aligned, but done to keep all IDTs defined the same way. */
gate_desc debug_idt_table[NR_VECTORS] __page_aligned_bss;
#endif
#else
#include <asm/processor-flags.h>
#include <asm/setup.h>

asmlinkage int system_call(void);
#endif

#ifndef CONFIG_X86_NO_IDT
/* Must be page-aligned because the real IDT is used in a fixmap. */
gate_desc idt_table[NR_VECTORS] __page_aligned_bss;
#endif

#ifndef CONFIG_XEN
DECLARE_BITMAP(used_vectors, NR_VECTORS);
EXPORT_SYMBOL_GPL(used_vectors);
#endif

static inline void conditional_sti(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void preempt_conditional_sti(struct pt_regs *regs)
{
	preempt_count_inc();
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void conditional_cli(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_disable();
}

static inline void preempt_conditional_cli(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_disable();
	preempt_count_dec();
}

static nokprobe_inline int
do_trap_no_signal(struct task_struct *tsk, int trapnr, char *str,
		  struct pt_regs *regs,	long error_code)
{
#ifdef CONFIG_X86_32
	if (regs->flags & X86_VM_MASK) {
		/*
		 * Traps 0, 1, 3, 4, and 5 should be forwarded to vm86.
		 * On nmi (interrupt 2), do_trap should not be called.
		 */
		if (trapnr < X86_TRAP_UD) {
			if (!handle_vm86_trap((struct kernel_vm86_regs *) regs,
						error_code, trapnr))
				return 0;
		}
		return -1;
	}
#endif
	if (!user_mode(regs)) {
		if (!fixup_exception(regs)) {
			tsk->thread.error_code = error_code;
			tsk->thread.trap_nr = trapnr;
			die(str, regs, error_code);
		}
		return 0;
	}

	return -1;
}

static siginfo_t *fill_trap_info(struct pt_regs *regs, int signr, int trapnr,
				siginfo_t *info)
{
	unsigned long siaddr;
	int sicode;

	switch (trapnr) {
	default:
		return SEND_SIG_PRIV;

	case X86_TRAP_DE:
		sicode = FPE_INTDIV;
		siaddr = uprobe_get_trap_addr(regs);
		break;
	case X86_TRAP_UD:
		sicode = ILL_ILLOPN;
		siaddr = uprobe_get_trap_addr(regs);
		break;
	case X86_TRAP_AC:
		sicode = BUS_ADRALN;
		siaddr = 0;
		break;
	}

	info->si_signo = signr;
	info->si_errno = 0;
	info->si_code = sicode;
	info->si_addr = (void __user *)siaddr;
	return info;
}

static void
do_trap(int trapnr, int signr, char *str, struct pt_regs *regs,
	long error_code, siginfo_t *info)
{
	struct task_struct *tsk = current;


	if (!do_trap_no_signal(tsk, trapnr, str, regs, error_code))
		return;
	/*
	 * We want error_code and trap_nr set for userspace faults and
	 * kernelspace faults which result in die(), but not
	 * kernelspace faults which are fixed up.  die() gives the
	 * process no chance to handle the signal and notice the
	 * kernel fault information, so that won't result in polluting
	 * the information about previously queued, but not yet
	 * delivered, faults.  See also do_general_protection below.
	 */
	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = trapnr;

#ifdef CONFIG_X86_64
	if (show_unhandled_signals && unhandled_signal(tsk, signr) &&
	    printk_ratelimit()) {
		pr_info("%s[%d] trap %s ip:%lx sp:%lx error:%lx",
			tsk->comm, tsk->pid, str,
			regs->ip, regs->sp, error_code);
		print_vma_addr(" in ", regs->ip);
		pr_cont("\n");
	}
#endif

	force_sig_info(signr, info ?: SEND_SIG_PRIV, tsk);
}
NOKPROBE_SYMBOL(do_trap);

static void do_error_trap(struct pt_regs *regs, long error_code, char *str,
			  unsigned long trapnr, int signr)
{
	enum ctx_state prev_state = exception_enter();
	siginfo_t info;

	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) !=
			NOTIFY_STOP) {
		conditional_sti(regs);
		do_trap(trapnr, signr, str, regs, error_code,
			fill_trap_info(regs, signr, trapnr, &info));
	}

	exception_exit(prev_state);
}

#define DO_ERROR(trapnr, signr, str, name)				\
dotraplinkage void do_##name(struct pt_regs *regs, long error_code)	\
{									\
	do_error_trap(regs, error_code, str, trapnr, signr);		\
}

DO_ERROR(X86_TRAP_DE,     SIGFPE,  "divide error",		divide_error)
DO_ERROR(X86_TRAP_OF,     SIGSEGV, "overflow",			overflow)
DO_ERROR(X86_TRAP_BR,     SIGSEGV, "bounds",			bounds)
DO_ERROR(X86_TRAP_UD,     SIGILL,  "invalid opcode",		invalid_op)
DO_ERROR(X86_TRAP_OLD_MF, SIGFPE,  "coprocessor segment overrun",coprocessor_segment_overrun)
DO_ERROR(X86_TRAP_TS,     SIGSEGV, "invalid TSS",		invalid_TSS)
DO_ERROR(X86_TRAP_NP,     SIGBUS,  "segment not present",	segment_not_present)
#ifdef CONFIG_X86_32
DO_ERROR(X86_TRAP_SS,     SIGBUS,  "stack segment",		stack_segment)
#endif
DO_ERROR(X86_TRAP_AC,     SIGBUS,  "alignment check",		alignment_check)

#ifdef CONFIG_X86_64
/* Runs on IST stack */
dotraplinkage void do_stack_segment(struct pt_regs *regs, long error_code)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();
	if (notify_die(DIE_TRAP, "stack segment", regs, error_code,
		       X86_TRAP_SS, SIGBUS) != NOTIFY_STOP) {
		preempt_conditional_sti(regs);
		do_trap(X86_TRAP_SS, SIGBUS, "stack segment", regs, error_code, NULL);
		preempt_conditional_cli(regs);
	}
	exception_exit(prev_state);
}

dotraplinkage void do_double_fault(struct pt_regs *regs, long error_code)
{
	static const char str[] = "double fault";
	struct task_struct *tsk = current;

	exception_enter();
	/* Return not checked because double check cannot be ignored */
	notify_die(DIE_TRAP, str, regs, error_code, X86_TRAP_DF, SIGSEGV);

	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = X86_TRAP_DF;

#ifdef CONFIG_DOUBLEFAULT
	df_debug(regs, error_code);
#endif
	/*
	 * This is always a kernel trap and never fixable (and thus must
	 * never return).
	 */
	for (;;)
		die(str, regs, error_code);
}
#endif

dotraplinkage void
do_general_protection(struct pt_regs *regs, long error_code)
{
	struct task_struct *tsk;
	enum ctx_state prev_state;

	prev_state = exception_enter();
	conditional_sti(regs);

#ifdef CONFIG_X86_32
	if (regs->flags & X86_VM_MASK) {
		local_irq_enable();
		handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
		goto exit;
	}
#endif

	tsk = current;
	if (!user_mode(regs)) {
		if (fixup_exception(regs))
			goto exit;

		tsk->thread.error_code = error_code;
		tsk->thread.trap_nr = X86_TRAP_GP;
		if (notify_die(DIE_GPF, "general protection fault", regs, error_code,
			       X86_TRAP_GP, SIGSEGV) != NOTIFY_STOP)
			die("general protection fault", regs, error_code);
		goto exit;
	}

	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = X86_TRAP_GP;

	if (show_unhandled_signals && unhandled_signal(tsk, SIGSEGV) &&
			printk_ratelimit()) {
		pr_info("%s[%d] general protection ip:%lx sp:%lx error:%lx",
			tsk->comm, task_pid_nr(tsk),
			regs->ip, regs->sp, error_code);
		print_vma_addr(" in ", regs->ip);
		pr_cont("\n");
	}

	force_sig_info(SIGSEGV, SEND_SIG_PRIV, tsk);
exit:
	exception_exit(prev_state);
}
NOKPROBE_SYMBOL(do_general_protection);

/* May run on IST stack. */
dotraplinkage void notrace do_int3(struct pt_regs *regs, long error_code)
{
	enum ctx_state prev_state;

#ifdef CONFIG_DYNAMIC_FTRACE
	/*
	 * ftrace must be first, everything else may cause a recursive crash.
	 * See note by declaration of modifying_ftrace_code in ftrace.c
	 */
	if (unlikely(atomic_read(&modifying_ftrace_code)) &&
	    ftrace_int3_handler(regs))
		return;
#endif
	if (poke_int3_handler(regs))
		return;

	prev_state = exception_enter();
#ifdef CONFIG_KGDB_LOW_LEVEL_TRAP
	if (kgdb_ll_trap(DIE_INT3, "int3", regs, error_code, X86_TRAP_BP,
				SIGTRAP) == NOTIFY_STOP)
		goto exit;
#endif /* CONFIG_KGDB_LOW_LEVEL_TRAP */

#ifdef CONFIG_KPROBES
	if (kprobe_int3_handler(regs))
		goto exit;
#endif

	if (notify_die(DIE_INT3, "int3", regs, error_code, X86_TRAP_BP,
			SIGTRAP) == NOTIFY_STOP)
		goto exit;

	/*
	 * Let others (NMI) know that the debug stack is in use
	 * as we may switch to the interrupt stack.
	 */
	debug_stack_usage_inc();
	preempt_conditional_sti(regs);
	do_trap(X86_TRAP_BP, SIGTRAP, "int3", regs, error_code, NULL);
	preempt_conditional_cli(regs);
	debug_stack_usage_dec();
exit:
	exception_exit(prev_state);
}
NOKPROBE_SYMBOL(do_int3);

#if defined(CONFIG_X86_64) && !defined(CONFIG_XEN)
/*
 * Help handler running on IST stack to switch back to user stack
 * for scheduling or signal handling. The actual stack switch is done in
 * entry.S
 */
asmlinkage __visible struct pt_regs *sync_regs(struct pt_regs *eregs)
{
	struct pt_regs *regs = eregs;
	/* Did already sync */
	if (eregs == (struct pt_regs *)eregs->sp)
		;
	/* Exception from user space */
	else if (user_mode(eregs))
		regs = task_pt_regs(current);
	/*
	 * Exception from kernel and interrupts are enabled. Move to
	 * kernel process stack.
	 */
	else if (eregs->flags & X86_EFLAGS_IF)
		regs = (struct pt_regs *)(eregs->sp -= sizeof(struct pt_regs));
	if (eregs != regs)
		*regs = *eregs;
	return regs;
}
NOKPROBE_SYMBOL(sync_regs);
#endif

/*
 * Our handling of the processor debug registers is non-trivial.
 * We do not clear them on entry and exit from the kernel. Therefore
 * it is possible to get a watchpoint trap here from inside the kernel.
 * However, the code in ./ptrace.c has ensured that the user can
 * only set watchpoints on userspace addresses. Therefore the in-kernel
 * watchpoint trap can only occur in code which is reading/writing
 * from user space. Such code must not hold kernel locks (since it
 * can equally take a page fault), therefore it is safe to call
 * force_sig_info even though that claims and releases locks.
 *
 * Code in ./signal.c ensures that the debug control register
 * is restored before we deliver any signal, and therefore that
 * user code runs with the correct debug control register even though
 * we clear it here.
 *
 * Being careful here means that we don't have to be as careful in a
 * lot of more complicated places (task switching can be a bit lazy
 * about restoring all the debug state, and ptrace doesn't have to
 * find every occurrence of the TF bit that could be saved away even
 * by user code)
 *
 * May run on IST stack.
 */
dotraplinkage void do_debug(struct pt_regs *regs, long error_code)
{
	struct task_struct *tsk = current;
	enum ctx_state prev_state;
	int user_icebp = 0;
	unsigned long dr6;
	int si_code;

	prev_state = exception_enter();

	get_debugreg(dr6, 6);

	/* Filter out all the reserved bits which are preset to 1 */
	dr6 &= ~DR6_RESERVED;

	/*
	 * If dr6 has no reason to give us about the origin of this trap,
	 * then it's very likely the result of an icebp/int01 trap.
	 * User wants a sigtrap for that.
	 */
	if (!dr6 && user_mode(regs))
		user_icebp = 1;

	/* Catch kmemcheck conditions first of all! */
	if ((dr6 & DR_STEP) && kmemcheck_trap(regs))
		goto exit;

	/* DR6 may or may not be cleared by the CPU */
	set_debugreg(0, 6);

	/*
	 * The processor cleared BTF, so don't mark that we need it set.
	 */
	clear_tsk_thread_flag(tsk, TIF_BLOCKSTEP);

	/* Store the virtualized DR6 value */
	tsk->thread.debugreg6 = dr6;

#ifdef CONFIG_KPROBES
	if (kprobe_debug_handler(regs))
		goto exit;
#endif

	if (notify_die(DIE_DEBUG, "debug", regs, (long)&dr6, error_code,
							SIGTRAP) == NOTIFY_STOP)
		goto exit;

	/*
	 * Let others (NMI) know that the debug stack is in use
	 * as we may switch to the interrupt stack.
	 */
	debug_stack_usage_inc();

	/* It's safe to allow irq's after DR6 has been saved */
	preempt_conditional_sti(regs);

	if (regs->flags & X86_VM_MASK) {
		handle_vm86_trap((struct kernel_vm86_regs *) regs, error_code,
					X86_TRAP_DB);
		preempt_conditional_cli(regs);
		debug_stack_usage_dec();
		goto exit;
	}

	/*
	 * Single-stepping through system calls: ignore any exceptions in
	 * kernel space, but re-enable TF when returning to user mode.
	 *
	 * We already checked v86 mode above, so we can check for kernel mode
	 * by just checking the CPL of CS.
	 */
	if ((dr6 & DR_STEP) && !user_mode(regs)) {
		tsk->thread.debugreg6 &= ~DR_STEP;
		set_tsk_thread_flag(tsk, TIF_SINGLESTEP);
		regs->flags &= ~X86_EFLAGS_TF;
	}
	si_code = get_si_code(tsk->thread.debugreg6);
	if (tsk->thread.debugreg6 & (DR_STEP | DR_TRAP_BITS) || user_icebp)
		send_sigtrap(tsk, regs, error_code, si_code);
	preempt_conditional_cli(regs);
	debug_stack_usage_dec();

exit:
	exception_exit(prev_state);
}
NOKPROBE_SYMBOL(do_debug);

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
static void math_error(struct pt_regs *regs, int error_code, int trapnr)
{
	struct task_struct *task = current;
	siginfo_t info;
	unsigned short err;
	char *str = (trapnr == X86_TRAP_MF) ? "fpu exception" :
						"simd exception";

	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, SIGFPE) == NOTIFY_STOP)
		return;
	conditional_sti(regs);

	if (!user_mode_vm(regs))
	{
		if (!fixup_exception(regs)) {
			task->thread.error_code = error_code;
			task->thread.trap_nr = trapnr;
			die(str, regs, error_code);
		}
		return;
	}

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	save_init_fpu(task);
	task->thread.trap_nr = trapnr;
	task->thread.error_code = error_code;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_addr = (void __user *)uprobe_get_trap_addr(regs);
	if (trapnr == X86_TRAP_MF) {
		unsigned short cwd, swd;
		/*
		 * (~cwd & swd) will mask out exceptions that are not set to unmasked
		 * status.  0x3f is the exception bits in these regs, 0x200 is the
		 * C1 reg you need in case of a stack fault, 0x040 is the stack
		 * fault bit.  We should only be taking one exception at a time,
		 * so if this combination doesn't produce any single exception,
		 * then we have a bad program that isn't synchronizing its FPU usage
		 * and it will suffer the consequences since we won't be able to
		 * fully reproduce the context of the exception
		 */
		cwd = get_fpu_cwd(task);
		swd = get_fpu_swd(task);

		err = swd & ~cwd;
	} else {
		/*
		 * The SIMD FPU exceptions are handled a little differently, as there
		 * is only a single status/control register.  Thus, to determine which
		 * unmasked exception was caught we must mask the exception mask bits
		 * at 0x1f80, and then use these to mask the exception bits at 0x3f.
		 */
		unsigned short mxcsr = get_fpu_mxcsr(task);
		err = ~(mxcsr >> 7) & mxcsr;
	}

	if (err & 0x001) {	/* Invalid op */
		/*
		 * swd & 0x240 == 0x040: Stack Underflow
		 * swd & 0x240 == 0x240: Stack Overflow
		 * User must clear the SF bit (0x40) if set
		 */
		info.si_code = FPE_FLTINV;
	} else if (err & 0x004) { /* Divide by Zero */
		info.si_code = FPE_FLTDIV;
	} else if (err & 0x008) { /* Overflow */
		info.si_code = FPE_FLTOVF;
	} else if (err & 0x012) { /* Denormal, Underflow */
		info.si_code = FPE_FLTUND;
	} else if (err & 0x020) { /* Precision */
		info.si_code = FPE_FLTRES;
	} else {
		/*
		 * If we're using IRQ 13, or supposedly even some trap
		 * X86_TRAP_MF implementations, it's possible
		 * we get a spurious trap, which is not an error.
		 */
		return;
	}
	force_sig_info(SIGFPE, &info, task);
}

dotraplinkage void do_coprocessor_error(struct pt_regs *regs, long error_code)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();
	math_error(regs, error_code, X86_TRAP_MF);
	exception_exit(prev_state);
}

dotraplinkage void
do_simd_coprocessor_error(struct pt_regs *regs, long error_code)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();
	math_error(regs, error_code, X86_TRAP_XF);
	exception_exit(prev_state);
}

#ifndef CONFIG_XEN
dotraplinkage void
do_spurious_interrupt_bug(struct pt_regs *regs, long error_code)
{
	conditional_sti(regs);
#if 0
	/* No need to warn about this any longer. */
	pr_info("Ignoring P6 Local APIC Spurious Interrupt Bug...\n");
#endif
}

asmlinkage __visible void __attribute__((weak)) smp_thermal_interrupt(void)
{
}

asmlinkage __visible void __attribute__((weak)) smp_threshold_interrupt(void)
{
}
#endif /* CONFIG_XEN */

/*
 * 'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 *
 * Must be called with kernel preemption disabled (eg with local
 * local interrupts as in the case of do_device_not_available).
 */
static void _math_state_restore(void)
{
	struct task_struct *tsk = current;

	if (!tsk_used_math(tsk)) {
		stts();
		local_irq_enable();
		/*
		 * does a slab alloc which can sleep
		 */
		if (init_fpu(tsk)) {
			/*
			 * ran out of memory!
			 */
			do_group_exit(SIGKILL);
			return;
		}
		local_irq_disable();
		clts();
	}

	xen_thread_fpu_begin(tsk, NULL);

	/*
	 * Paranoid restore. send a SIGSEGV if we fail to restore the state.
	 */
	if (unlikely(restore_fpu_checking(tsk))) {
		drop_init_fpu(tsk);
		force_sig_info(SIGSEGV, SEND_SIG_PRIV, tsk);
		return;
	}

	tsk->thread.fpu_counter++;
}

void math_state_restore(void)
{
	clts();
	_math_state_restore();
}

dotraplinkage void
do_device_not_available(struct pt_regs *regs, long error_code)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();
	BUG_ON(use_eager_fpu());

#ifdef CONFIG_MATH_EMULATION
	if (read_cr0() & X86_CR0_EM) {
		struct math_emu_info info = { };

		conditional_sti(regs);

		info.regs = regs;
		math_emulate(&info);
		exception_exit(prev_state);
		return;
	}
#endif
	/* NB. 'clts' is done for us by Xen during virtual trap. */
	__this_cpu_and(xen_x86_cr0, ~X86_CR0_TS);
	_math_state_restore(); /* interrupts still off */
#ifdef CONFIG_X86_32
	conditional_sti(regs);
#endif
	exception_exit(prev_state);
}
NOKPROBE_SYMBOL(do_device_not_available);

#ifdef CONFIG_X86_32
dotraplinkage void do_iret_error(struct pt_regs *regs, long error_code)
{
	siginfo_t info;
	enum ctx_state prev_state;

	prev_state = exception_enter();
	local_irq_enable();

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_BADSTK;
	info.si_addr = NULL;
	if (notify_die(DIE_TRAP, "iret exception", regs, error_code,
			X86_TRAP_IRET, SIGILL) != NOTIFY_STOP) {
		do_trap(X86_TRAP_IRET, SIGILL, "iret exception", regs, error_code,
			&info);
	}
	exception_exit(prev_state);
}
#endif

/*
 * NB. All these are "trap gates" (i.e. events_mask isn't set) except
 * for those that specify <dpl>|4 in the second field.
 */
#ifdef CONFIG_X86_32
#define X 0
#else
#define X 4
#endif
static const trap_info_t __initconst early_trap_table[] = {
	{ X86_TRAP_DB, 0|4, __KERNEL_CS, (unsigned long)debug			},
	{ X86_TRAP_BP, 3|4, __KERNEL_CS, (unsigned long)int3			},
#ifdef CONFIG_X86_64
	{ }
};
static const trap_info_t __initconst early_trap_pf_table[] = {
#endif
	{ X86_TRAP_PF, 0|4, __KERNEL_CS, (unsigned long)page_fault		},
	{ }
};
static const trap_info_t trap_table[] = {
	{ X86_TRAP_DE, 0|X, __KERNEL_CS, (unsigned long)divide_error		},
	{ X86_TRAP_DB, 0|4, __KERNEL_CS, (unsigned long)debug			},
	{ X86_TRAP_BP, 3|4, __KERNEL_CS, (unsigned long)int3			},
	{ X86_TRAP_OF, 3|X, __KERNEL_CS, (unsigned long)overflow		},
	{ X86_TRAP_BR, 0|X, __KERNEL_CS, (unsigned long)bounds			},
	{ X86_TRAP_UD, 0|X, __KERNEL_CS, (unsigned long)invalid_op		},
	{ X86_TRAP_NM, 0|4, __KERNEL_CS, (unsigned long)device_not_available	},
	{ X86_TRAP_OLD_MF, 0|X, __KERNEL_CS, (unsigned long)coprocessor_segment_overrun },
	{ X86_TRAP_TS, 0|X, __KERNEL_CS, (unsigned long)invalid_TSS		},
	{ X86_TRAP_NP, 0|X, __KERNEL_CS, (unsigned long)segment_not_present	},
	{ X86_TRAP_SS, 0|X, __KERNEL_CS, (unsigned long)stack_segment		},
	{ X86_TRAP_GP, 0|X, __KERNEL_CS, (unsigned long)general_protection	},
	{ X86_TRAP_PF, 0|4, __KERNEL_CS, (unsigned long)page_fault		},
	{ X86_TRAP_MF, 0|X, __KERNEL_CS, (unsigned long)coprocessor_error	},
	{ X86_TRAP_AC, 0|X, __KERNEL_CS, (unsigned long)alignment_check		},
#ifdef CONFIG_X86_MCE
	{ X86_TRAP_MC, 0|X, __KERNEL_CS, (unsigned long)machine_check		},
#endif
	{ X86_TRAP_XF, 0|X, __KERNEL_CS, (unsigned long)simd_coprocessor_error	},
#ifdef CONFIG_X86_32
	{ X86_TRAP_SPURIOUS, 0, __KERNEL_CS, (unsigned long)fixup_4gb_segment	},
	{ SYSCALL_VECTOR,  3, __KERNEL_CS, (unsigned long)system_call	},
#elif defined(CONFIG_IA32_EMULATION)
	{ IA32_SYSCALL_VECTOR, 3, __KERNEL_CS, (unsigned long)ia32_syscall },
#endif
	{ }
};

#ifdef CONFIG_TRACING
static const trap_info_t trace_trap_table[] = {
	{ X86_TRAP_DE, 0|X, __KERNEL_CS, (unsigned long)trace_divide_error	},
	{ X86_TRAP_BR, 0|X, __KERNEL_CS, (unsigned long)trace_bounds		},
	{ X86_TRAP_UD, 0|X, __KERNEL_CS, (unsigned long)trace_invalid_op	},
	{ X86_TRAP_NM, 0|4, __KERNEL_CS, (unsigned long)trace_device_not_available },
	{ X86_TRAP_OLD_MF, 0|X, __KERNEL_CS, (unsigned long)trace_coprocessor_segment_overrun },
	{ X86_TRAP_TS, 0|X, __KERNEL_CS, (unsigned long)trace_invalid_TSS	},
	{ X86_TRAP_NP, 0|X, __KERNEL_CS, (unsigned long)trace_segment_not_present },
	{ X86_TRAP_GP, 0|X, __KERNEL_CS, (unsigned long)trace_general_protection },
	{ X86_TRAP_PF, 0|4, __KERNEL_CS, (unsigned long)trace_page_fault	},
	{ X86_TRAP_MF, 0|X, __KERNEL_CS, (unsigned long)trace_coprocessor_error	},
	{ X86_TRAP_AC, 0|X, __KERNEL_CS, (unsigned long)trace_alignment_check	},
	{ X86_TRAP_XF, 0|X, __KERNEL_CS, (unsigned long)trace_simd_coprocessor_error },
	{ }
};

void load_current_trap_table(void)
{
	const trap_info_t *tt = is_trace_trap_table_enabled()
				? trace_trap_table : trap_table;

	if (HYPERVISOR_set_trap_table(tt))
		BUG();
}
#endif

/* Set of traps needed for early debugging. */
void __init early_trap_init(void)
{
	int ret = HYPERVISOR_set_trap_table(early_trap_table);

	if (ret)
		printk("early set_trap_table failed (%d)\n", ret);
}

void __init early_trap_pf_init(void)
{
#ifdef CONFIG_X86_64
	int ret = HYPERVISOR_set_trap_table(early_trap_pf_table);

	if (ret)
		printk("early PF set_trap_table failed (%d)\n", ret);
#endif
}

void __init trap_init(void)
{
	int ret;

	ret = HYPERVISOR_set_trap_table(trap_table);
	if (ret)
		printk("HYPERVISOR_set_trap_table failed (%d)\n", ret);

	/*
	 * Should be a barrier for any external CPU state:
	 */
	cpu_init();

	x86_init.irqs.trap_init();
}

void smp_trap_init(trap_info_t *trap_ctxt)
{
	const trap_info_t *t;

	for (t = trap_table; t->address; t++) {
		trap_ctxt[t->vector].flags = t->flags;
		trap_ctxt[t->vector].cs = t->cs;
		trap_ctxt[t->vector].address = t->address;
	}
	TI_SET_IF(trap_ctxt + NMI_VECTOR, 1);
	trap_ctxt[NMI_VECTOR].cs = __KERNEL_CS;
	trap_ctxt[NMI_VECTOR].address = (unsigned long)nmi;
}
