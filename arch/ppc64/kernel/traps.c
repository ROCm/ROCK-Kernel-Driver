/*
 *  linux/arch/ppc/kernel/traps.c
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
#include <linux/ptrace.h>
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
extern void bad_page_fault(struct pt_regs *, unsigned long);

/* This is true if we are using the firmware NMI handler (typically LPAR) */
extern int fwnmi_active;

#ifdef CONFIG_XMON
extern void xmon(struct pt_regs *regs);
extern int xmon_bpt(struct pt_regs *regs);
extern int xmon_sstep(struct pt_regs *regs);
extern int xmon_iabr_match(struct pt_regs *regs);
extern int xmon_dabr_match(struct pt_regs *regs);
extern void (*xmon_fault_handler)(struct pt_regs *regs);
#endif

#ifdef CONFIG_XMON
void (*debugger)(struct pt_regs *regs) = xmon;
int (*debugger_bpt)(struct pt_regs *regs) = xmon_bpt;
int (*debugger_sstep)(struct pt_regs *regs) = xmon_sstep;
int (*debugger_iabr_match)(struct pt_regs *regs) = xmon_iabr_match;
int (*debugger_dabr_match)(struct pt_regs *regs) = xmon_dabr_match;
void (*debugger_fault_handler)(struct pt_regs *regs);
#else
#ifdef CONFIG_KGDB
void (*debugger)(struct pt_regs *regs);
int (*debugger_bpt)(struct pt_regs *regs);
int (*debugger_sstep)(struct pt_regs *regs);
int (*debugger_iabr_match)(struct pt_regs *regs);
int (*debugger_dabr_match)(struct pt_regs *regs);
void (*debugger_fault_handler)(struct pt_regs *regs);
#endif
#endif
/*
 * Trap & Exception support
 */

void
_exception(int signr, struct pt_regs *regs)
{
	if (!user_mode(regs))
	{
		show_regs(regs);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		debugger(regs);
#endif
		print_backtrace((unsigned long *)regs->gpr[1]);
		panic("Exception in kernel pc %lx signal %d",regs->nip,signr);
#if defined(CONFIG_PPCDBG) && (defined(CONFIG_XMON) || defined(CONFIG_KGDB))
	/* Allow us to catch SIGILLs for 64-bit app/glibc debugging. -Peter */
	} else if (signr == SIGILL) {
		ifppcdebug(PPCDBG_SIGNALXMON)
			debugger(regs);
#endif
	}
	force_sig(signr, current);
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
	char *msg = "System Reset in kernel mode.\n";
	udbg_printf(msg); printk(msg);
	if (fwnmi_active) {
		unsigned long *r3 = __va(regs->gpr[3]); /* for FWNMI debug */
		struct rtas_error_log *errlog;

		msg = "FWNMI is active with save area at %016lx\n";
		udbg_printf(msg, r3); printk(msg, r3);
		errlog = FWNMI_get_errinfo(regs);
	}
#if defined(CONFIG_XMON)
	xmon(regs);
	udbg_printf("leaving xmon...\n");
#else
	for(;;);
#endif
}

void
MachineCheckException(struct pt_regs *regs)
{
	if (fwnmi_active) {
		struct rtas_error_log *errhdr = FWNMI_get_errinfo(regs);
		if (errhdr) {
			/* ToDo: attempt to recover from some errors here */
		}
		FWNMI_release_errinfo();
	}
	if ( !user_mode(regs) )
	{
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		if (debugger_fault_handler) {
			debugger_fault_handler(regs);
			return;
		}
#endif
		printk("Machine check in kernel mode.\n");
		printk("Caused by (from SRR1=%lx): ", regs->msr);
		show_regs(regs);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		debugger(regs);
#endif
		print_backtrace((unsigned long *)regs->gpr[1]);
		panic("machine check");
	}
	_exception(SIGSEGV, regs);	
}

void
SMIException(struct pt_regs *regs)
{
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	{
		debugger(regs);
		return;
	}
#endif
	show_regs(regs);
	print_backtrace((unsigned long *)regs->gpr[1]);
	panic("System Management Interrupt");
}

void
UnknownException(struct pt_regs *regs)
{
	printk("Bad trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);
	_exception(SIGTRAP, regs);	
}

void
InstructionBreakpointException(struct pt_regs *regs)
{
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_iabr_match(regs))
		return;
#endif
	_exception(SIGTRAP, regs);
}

void
ProgramCheckException(struct pt_regs *regs)
{
	if (regs->msr & 0x100000) {
		/* IEEE FP exception */
		_exception(SIGFPE, regs);
	} else if (regs->msr & 0x20000) {
		/* trap exception */
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		if (debugger_bpt(regs))
			return;
#endif
		_exception(SIGTRAP, regs);
	} else {
		_exception(SIGILL, regs);
	}
}

void
SingleStepException(struct pt_regs *regs)
{
	regs->msr &= ~MSR_SE;  /* Turn off 'trace' bit */
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_sstep(regs))
		return;
#endif
	_exception(SIGTRAP, regs);	
}

/* Dummy handler for Performance Monitor */

void
PerformanceMonitorException(struct pt_regs *regs)
{
	_exception(SIGTRAP, regs);
}

void
AlignmentException(struct pt_regs *regs)
{
	int fixed;

	fixed = fix_alignment(regs);
	if (fixed == 1) {
		ifppcdebug(PPCDBG_ALIGNFIXUP)
			if (!user_mode(regs))
				PPCDBG(PPCDBG_ALIGNFIXUP, "fix alignment at %lx\n", regs->nip);
		regs->nip += 4;	/* skip over emulated instruction */
		return;
	}
	if (fixed == -EFAULT) {
		/* fixed == -EFAULT means the operand address was bad */
		if (user_mode(regs))
			force_sig(SIGSEGV, current);
		else
			bad_page_fault(regs, regs->dar);
		return;
	}
	_exception(SIGBUS, regs);	
}

void __init trap_init(void)
{
}
