/*
 *  linux/arch/ppc64/kernel/traps.c
 *
 *  Copyright (C) 1995-1996  Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  and Paul Mackerras (paulus@cs.anu.edu.au)
 */

/*
 * This file handles the architecture-dependent parts of hardware exceptions
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/ppcdebug.h>

extern int fix_alignment(struct pt_regs *);
extern void bad_page_fault(struct pt_regs *, unsigned long, int);

/* This is true if we are using the firmware NMI handler (typically LPAR) */
extern int fwnmi_active;

#ifdef CONFIG_DEBUG_KERNEL
void (*debugger)(struct pt_regs *regs);
int (*debugger_bpt)(struct pt_regs *regs);
int (*debugger_sstep)(struct pt_regs *regs);
int (*debugger_iabr_match)(struct pt_regs *regs);
int (*debugger_dabr_match)(struct pt_regs *regs);
void (*debugger_fault_handler)(struct pt_regs *regs);
#endif

/*
 * Trap & Exception support
 */

/* Should we panic on bad kernel exceptions or try to recover */
#undef PANIC_ON_ERROR

static spinlock_t die_lock = SPIN_LOCK_UNLOCKED;

void die(const char *str, struct pt_regs *regs, long err)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("Oops: %s, sig: %ld\n", str, err);
	show_regs(regs);
	print_backtrace((unsigned long *)regs->gpr[1]);
	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);

#ifdef PANIC_ON_ERROR
	panic(str);
#else
	do_exit(SIGSEGV);
#endif
}

static void
_exception(int signr, siginfo_t *info, struct pt_regs *regs)
{
	if (!user_mode(regs)) {
		if (debugger)
			debugger(regs);
		die("Exception in kernel mode\n", regs, signr);
	}

	force_sig_info(signr, info, current);
}

/* Get the error information for errors coming through the
 * FWNMI vectors.  The pt_regs' r3 will be updated to reflect
 * the actual r3 if possible, and a ptr to the error log entry
 * will be returned if found.
 */
static struct rtas_error_log *FWNMI_get_errinfo(struct pt_regs *regs)
{
	unsigned long errdata = regs->gpr[3];
	struct rtas_error_log *errhdr = NULL;
	unsigned long *savep;

	if ((errdata >= 0x7000 && errdata < 0x7fff0) ||
	    (errdata >= rtas.base && errdata < rtas.base + rtas.size - 16)) {
		savep = __va(errdata);
		regs->gpr[3] = savep[0];	/* restore original r3 */
		errhdr = (struct rtas_error_log *)(savep + 1);
	} else {
		printk("FWNMI: corrupt r3\n");
	}
	return errhdr;
}

/* Call this when done with the data returned by FWNMI_get_errinfo.
 * It will release the saved data area for other CPUs in the
 * partition to receive FWNMI errors.
 */
static void FWNMI_release_errinfo(void)
{
	unsigned long ret = rtas_call(rtas_token("ibm,nmi-interlock"), 0, 1, NULL);
	if (ret != 0)
		printk("FWNMI: nmi-interlock failed: %ld\n", ret);
}

void
SystemResetException(struct pt_regs *regs)
{
	if (fwnmi_active) {
		unsigned long *r3 = __va(regs->gpr[3]); /* for FWNMI debug */
		struct rtas_error_log *errlog;

		udbg_printf("FWNMI is active with save area at %016lx\n", r3);
		errlog = FWNMI_get_errinfo(regs);
		FWNMI_release_errinfo();
	}

	if (debugger)
		debugger(regs);

#ifdef PANIC_ON_ERROR
	panic("System Reset");
#else
	/* Must die if the interrupt is not recoverable */
	if (!(regs->msr & MSR_RI))
		panic("Unrecoverable System Reset");
#endif

	/* What should we do here? We could issue a shutdown or hard reset. */
}

static int power4_handle_mce(struct pt_regs *regs)
{
	return 0;
}

void
MachineCheckException(struct pt_regs *regs)
{
	siginfo_t info;

	if (fwnmi_active) {
		struct rtas_error_log *errhdr = FWNMI_get_errinfo(regs);
		if (errhdr) {
			/* ToDo: attempt to recover from some errors here */
		}
		FWNMI_release_errinfo();
	}

	if (!user_mode(regs)) {
		/* Attempt to recover if the interrupt is recoverable */
		if (regs->msr & MSR_RI) {
			if ((__is_processor(PV_POWER4) ||
			     __is_processor(PV_POWER4p)) &&
			     power4_handle_mce(regs))
				return;
		}

		if (debugger_fault_handler) {
			debugger_fault_handler(regs);
			return;
		}
		if (debugger)
			debugger(regs);

		console_verbose();
		spin_lock_irq(&die_lock);
		bust_spinlocks(1);
		printk("Machine check in kernel mode.\n");
		printk("Caused by (from SRR1=%lx): ", regs->msr);
		show_regs(regs);
		print_backtrace((unsigned long *)regs->gpr[1]);
		bust_spinlocks(0);
		spin_unlock_irq(&die_lock);
		panic("Unrecoverable Machine Check");
	}

	/*
	 * XXX we should check RI bit on exception exit and kill the
	 * task if it was cleared
	 */
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *)regs->nip;
	_exception(SIGSEGV, &info, regs);
}

void
UnknownException(struct pt_regs *regs)
{
	siginfo_t info;

	printk("Bad trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);

	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = 0;
	info.si_addr = 0;
	_exception(SIGTRAP, &info, regs);	
}

void
InstructionBreakpointException(struct pt_regs *regs)
{
	siginfo_t info;

	if (debugger_iabr_match && debugger_iabr_match(regs))
		return;

	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_BRKPT;
	info.si_addr = (void *)regs->nip;
	_exception(SIGTRAP, &info, regs);
}

static void parse_fpe(struct pt_regs *regs)
{
	siginfo_t info;
	unsigned long fpscr;

	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	fpscr = current->thread.fpscr;

	/* Invalid operation */
	if ((fpscr & FPSCR_VE) && (fpscr & FPSCR_VX))
		info.si_code = FPE_FLTINV;

	/* Overflow */
	else if ((fpscr & FPSCR_OE) && (fpscr & FPSCR_OX))
		info.si_code = FPE_FLTOVF;

	/* Underflow */
	else if ((fpscr & FPSCR_UE) && (fpscr & FPSCR_UX))
		info.si_code = FPE_FLTUND;

	/* Divide by zero */
	else if ((fpscr & FPSCR_ZE) && (fpscr & FPSCR_ZX))
		info.si_code = FPE_FLTDIV;

	/* Inexact result */
	else if ((fpscr & FPSCR_XE) && (fpscr & FPSCR_XX))
		info.si_code = FPE_FLTRES;

	else
		info.si_code = 0;

	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_addr = (void *)regs->nip;
	_exception(SIGFPE, &info, regs);
}

void
ProgramCheckException(struct pt_regs *regs)
{
	siginfo_t info;

	if (regs->msr & 0x100000) {
		/* IEEE FP exception */

		parse_fpe(regs);
	} else if (regs->msr & 0x40000) {
		/* Privileged instruction */

		info.si_signo = SIGILL;
		info.si_errno = 0;
		info.si_code = ILL_PRVOPC;
		info.si_addr = (void *)regs->nip;
		_exception(SIGILL, &info, regs);
	} else if (regs->msr & 0x20000) {
		/* trap exception */

		if (debugger_bpt && debugger_bpt(regs))
			return;

		info.si_signo = SIGTRAP;
		info.si_errno = 0;
		info.si_code = TRAP_BRKPT;
		info.si_addr = (void *)regs->nip;
		_exception(SIGTRAP, &info, regs);
	} else {
		/* Illegal instruction */

		info.si_signo = SIGILL;
		info.si_errno = 0;
		info.si_code = ILL_ILLTRP;
		info.si_addr = (void *)regs->nip;
		_exception(SIGILL, &info, regs);
	}
}

void
SingleStepException(struct pt_regs *regs)
{
	siginfo_t info;

	regs->msr &= ~MSR_SE;  /* Turn off 'trace' bit */

	if (debugger_sstep && debugger_sstep(regs))
		return;

	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_TRACE;
	info.si_addr = (void *)regs->nip;
	_exception(SIGTRAP, &info, regs);	
}

void
PerformanceMonitorException(struct pt_regs *regs)
{
	siginfo_t info;

	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_BRKPT;
	info.si_addr = 0;
	_exception(SIGTRAP, &info, regs);
}

void
AlignmentException(struct pt_regs *regs)
{
	int fixed;
	siginfo_t info;

	fixed = fix_alignment(regs);

	if (fixed == 1) {
		if (!user_mode(regs))
			PPCDBG(PPCDBG_ALIGNFIXUP, "fix alignment at %lx\n",
			       regs->nip);
		regs->nip += 4;	/* skip over emulated instruction */
		return;
	}

	/* Operand address was bad */	
	if (fixed == -EFAULT) {
		if (user_mode(regs)) {
			info.si_signo = SIGSEGV;
			info.si_errno = 0;
			info.si_code = SEGV_MAPERR;
			info.si_addr = (void *)regs->dar;
			force_sig_info(SIGSEGV, &info, current);
		} else {
			/* Search exception table */
			bad_page_fault(regs, regs->dar, SIGSEGV);
		}

		return;
	}

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void *)regs->nip;
	_exception(SIGBUS, &info, regs);	
}

void __init trap_init(void)
{
}
