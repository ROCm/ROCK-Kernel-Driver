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
#include <asm/fpu/internal.h>
#include <asm/mce.h>
#include <asm/fixmap.h>
#include <asm/mach_traps.h>
#include <asm/alternative.h>
#include <asm/fpu/xstate.h>
#include <asm/trace/mpx.h>
#include <asm/mpx.h>

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
#include <asm/proto.h>
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

enum ctx_state ist_enter(struct pt_regs *regs)
{
	enum ctx_state prev_state;

	if (user_mode(regs)) {
		/* Other than that, we're just an exception. */
		prev_state = exception_enter();
	} else {
		/*
		 * We might have interrupted pretty much anything.  In
		 * fact, if we're a machine check, we can even interrupt
		 * NMI processing.  We don't want in_nmi() to return true,
		 * but we need to notify RCU.
		 */
		rcu_nmi_enter();
		prev_state = CONTEXT_KERNEL;  /* the value is irrelevant. */
	}

	/*
	 * We are atomic because we're on the IST stack (or we're on x86_32,
	 * in which case we still shouldn't schedule).
	 *
	 * This must be after exception_enter(), because exception_enter()
	 * won't do anything if in_interrupt() returns true.
	 */
	preempt_count_add(HARDIRQ_OFFSET);

	/* This code is a bit fragile.  Test it. */
	rcu_lockdep_assert(rcu_is_watching(), "ist_enter didn't work");

	return prev_state;
}

void ist_exit(struct pt_regs *regs, enum ctx_state prev_state)
{
	/* Must be before exception_exit. */
	preempt_count_sub(HARDIRQ_OFFSET);

	if (user_mode(regs))
		return exception_exit(prev_state);
	else
		rcu_nmi_exit();
}

/**
 * ist_begin_non_atomic() - begin a non-atomic section in an IST exception
 * @regs:	regs passed to the IST exception handler
 *
 * IST exception handlers normally cannot schedule.  As a special
 * exception, if the exception interrupted userspace code (i.e.
 * user_mode(regs) would return true) and the exception was not
 * a double fault, it can be safe to schedule.  ist_begin_non_atomic()
 * begins a non-atomic section within an ist_enter()/ist_exit() region.
 * Callers are responsible for enabling interrupts themselves inside
 * the non-atomic section, and callers must call is_end_non_atomic()
 * before ist_exit().
 */
void ist_begin_non_atomic(struct pt_regs *regs)
{
	BUG_ON(!user_mode(regs));

	/*
	 * Sanity check: we need to be on the normal thread stack.  This
	 * will catch asm bugs and any attempt to use ist_preempt_enable
	 * from double_fault.
	 */
	BUG_ON((unsigned long)(current_top_of_stack() -
			       current_stack_pointer()) >= THREAD_SIZE);

	preempt_count_sub(HARDIRQ_OFFSET);
}

/**
 * ist_end_non_atomic() - begin a non-atomic section in an IST exception
 *
 * Ends a non-atomic section started with ist_begin_non_atomic().
 */
void ist_end_non_atomic(void)
{
	preempt_count_add(HARDIRQ_OFFSET);
}

static nokprobe_inline int
do_trap_no_signal(struct task_struct *tsk, int trapnr, char *str,
		  struct pt_regs *regs,	long error_code)
{
	if (v8086_mode(regs)) {
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
DO_ERROR(X86_TRAP_UD,     SIGILL,  "invalid opcode",		invalid_op)
DO_ERROR(X86_TRAP_OLD_MF, SIGFPE,  "coprocessor segment overrun",coprocessor_segment_overrun)
DO_ERROR(X86_TRAP_TS,     SIGSEGV, "invalid TSS",		invalid_TSS)
DO_ERROR(X86_TRAP_NP,     SIGBUS,  "segment not present",	segment_not_present)
DO_ERROR(X86_TRAP_SS,     SIGBUS,  "stack segment",		stack_segment)
DO_ERROR(X86_TRAP_AC,     SIGBUS,  "alignment check",		alignment_check)

#ifdef CONFIG_X86_64
/* Runs on IST stack */
dotraplinkage void do_double_fault(struct pt_regs *regs, long error_code)
{
	static const char str[] = "double fault";
	struct task_struct *tsk = current;

#ifdef CONFIG_X86_ESPFIX64
	extern unsigned char native_irq_return_iret[];

	/*
	 * If IRET takes a non-IST fault on the espfix64 stack, then we
	 * end up promoting it to a doublefault.  In that case, modify
	 * the stack to make it look like we just entered the #GP
	 * handler from user space, similar to bad_iret.
	 *
	 * No need for ist_enter here because we don't use RCU.
	 */
	if (((long)regs->sp >> PGDIR_SHIFT) == ESPFIX_PGD_ENTRY &&
		regs->cs == __KERNEL_CS &&
		regs->ip == (unsigned long)native_irq_return_iret)
	{
		struct pt_regs *normal_regs = task_pt_regs(current);

		/* Fake a #GP(0) from userspace. */
		memmove(&normal_regs->ip, (void *)regs->sp, 5*8);
		normal_regs->orig_ax = 0;  /* Missing (lost) #GP error code */
		regs->ip = (unsigned long)general_protection;
		regs->sp = (unsigned long)&normal_regs->orig_ax;

		return;
	}
#endif

	ist_enter(regs);  /* Discard prev_state because we won't return. */
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

dotraplinkage void do_bounds(struct pt_regs *regs, long error_code)
{
	enum ctx_state prev_state;
	const struct bndcsr *bndcsr;
	siginfo_t *info;

	prev_state = exception_enter();
	if (notify_die(DIE_TRAP, "bounds", regs, error_code,
			X86_TRAP_BR, SIGSEGV) == NOTIFY_STOP)
		goto exit;
	conditional_sti(regs);

	if (!user_mode(regs))
		die("bounds", regs, error_code);

	if (!cpu_feature_enabled(X86_FEATURE_MPX)) {
		/* The exception is not from Intel MPX */
		goto exit_trap;
	}

	/*
	 * We need to look at BNDSTATUS to resolve this exception.
	 * A NULL here might mean that it is in its 'init state',
	 * which is all zeros which indicates MPX was not
	 * responsible for the exception.
	 */
	bndcsr = get_xsave_field_ptr(XSTATE_BNDCSR);
	if (!bndcsr)
		goto exit_trap;

	trace_bounds_exception_mpx(bndcsr);
	/*
	 * The error code field of the BNDSTATUS register communicates status
	 * information of a bound range exception #BR or operation involving
	 * bound directory.
	 */
	switch (bndcsr->bndstatus & MPX_BNDSTA_ERROR_CODE) {
	case 2:	/* Bound directory has invalid entry. */
		if (mpx_handle_bd_fault())
			goto exit_trap;
		break; /* Success, it was handled */
	case 1: /* Bound violation. */
		info = mpx_generate_siginfo(regs);
		if (IS_ERR(info)) {
			/*
			 * We failed to decode the MPX instruction.  Act as if
			 * the exception was not caused by MPX.
			 */
			goto exit_trap;
		}
		/*
		 * Success, we decoded the instruction and retrieved
		 * an 'info' containing the address being accessed
		 * which caused the exception.  This information
		 * allows and application to possibly handle the
		 * #BR exception itself.
		 */
		do_trap(X86_TRAP_BR, SIGSEGV, "bounds", regs, error_code, info);
		kfree(info);
		break;
	case 0: /* No exception caused by Intel MPX operations. */
		goto exit_trap;
	default:
		die("bounds", regs, error_code);
	}

exit:
	exception_exit(prev_state);
	return;
exit_trap:
	/*
	 * This path out is for all the cases where we could not
	 * handle the exception in some way (like allocating a
	 * table or telling userspace about it.  We will also end
	 * up here if the kernel has MPX turned off at compile
	 * time..
	 */
	do_trap(X86_TRAP_BR, SIGSEGV, "bounds", regs, error_code, NULL);
	exception_exit(prev_state);
}

dotraplinkage void
do_general_protection(struct pt_regs *regs, long error_code)
{
	struct task_struct *tsk;
	enum ctx_state prev_state;

	prev_state = exception_enter();
	conditional_sti(regs);

	if (v8086_mode(regs)) {
		local_irq_enable();
		handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
		goto exit;
	}

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

	prev_state = ist_enter(regs);
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
	ist_exit(regs, prev_state);
}
NOKPROBE_SYMBOL(do_int3);

#if defined(CONFIG_X86_64) && !defined(CONFIG_XEN)
/*
 * Help handler running on IST stack to switch off the IST stack if the
 * interrupted code was in user mode. The actual stack switch is done in
 * entry_64.S
 */
asmlinkage __visible notrace struct pt_regs *sync_regs(struct pt_regs *eregs)
{
	struct pt_regs *regs = task_pt_regs(current);
	*regs = *eregs;
	return regs;
}
NOKPROBE_SYMBOL(sync_regs);

struct bad_iret_stack {
	void *error_entry_ret;
	struct pt_regs regs;
};

asmlinkage __visible notrace
struct bad_iret_stack *fixup_bad_iret(struct bad_iret_stack *s)
{
	/*
	 * This is called from entry_64.S early in handling a fault
	 * caused by a bad iret to user mode.  To handle the fault
	 * correctly, we want move our stack frame to task_pt_regs
	 * and we want to pretend that the exception came from the
	 * iret target.
	 */
	struct bad_iret_stack *new_stack =
		container_of(task_pt_regs(current),
			     struct bad_iret_stack, regs);

	/* Copy the IRET target to the new stack. */
	memmove(&new_stack->regs.ip, (void *)s->regs.sp, 5*8);

	/* Copy the remainder of the stack from the current stack. */
	memmove(new_stack, s, offsetof(struct bad_iret_stack, regs.ip));

	BUG_ON(!user_mode(&new_stack->regs));
	return new_stack;
}
NOKPROBE_SYMBOL(fixup_bad_iret);
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

	prev_state = ist_enter(regs);

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

	if (v8086_mode(regs)) {
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
	ist_exit(regs, prev_state);
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
	struct fpu *fpu = &task->thread.fpu;
	siginfo_t info;
	char *str = (trapnr == X86_TRAP_MF) ? "fpu exception" :
						"simd exception";

	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, SIGFPE) == NOTIFY_STOP)
		return;
	conditional_sti(regs);

	if (!user_mode(regs)) {
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
	fpu__save(fpu);

	task->thread.trap_nr	= trapnr;
	task->thread.error_code = error_code;
	info.si_signo		= SIGFPE;
	info.si_errno		= 0;
	info.si_addr		= (void __user *)uprobe_get_trap_addr(regs);

	info.si_code = fpu__exception_code(fpu, trapnr);

	/* Retry when we get spurious exceptions: */
	if (!info.si_code)
		return;

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
}
#endif /* CONFIG_XEN */

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
	fpu__restore(&current->thread.fpu); /* interrupts still off */
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
	{ IA32_SYSCALL_VECTOR,  3, __KERNEL_CS, (unsigned long)entry_INT80_32	},
#elif defined(CONFIG_IA32_EMULATION)
	{ IA32_SYSCALL_VECTOR, 3, __KERNEL_CS, (unsigned long)entry_INT80_compat },
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
