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
#include <linux/malloc.h>
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

extern int fix_alignment(struct pt_regs *);
extern void bad_page_fault(struct pt_regs *, unsigned long);

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
	}
	force_sig(signr, current);
}

void
MachineCheckException(struct pt_regs *regs)
{
#ifdef CONFIG_ALL_PPC
	unsigned long fixup;
#endif /* CONFIG_ALL_PPC */

	if (user_mode(regs)) {
		_exception(SIGSEGV, regs);	
		return;
	}

#if defined(CONFIG_8xx) && defined(CONFIG_PCI)
	/* the qspan pci read routines can cause machine checks -- Cort */
	bad_page_fault(regs, regs->dar);
	return;
#endif
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_fault_handler) {
		debugger_fault_handler(regs);
		return;
	}
#endif

#ifdef CONFIG_ALL_PPC
	/*
	 * I/O accesses can cause machine checks on powermacs.
	 * Check if the NIP corresponds to the address of a sync
	 * instruction for which there is an entry in the exception
	 * table.
	 */
	if (regs->msr & (0x80000 | 0x40000)
	    && (fixup = search_exception_table(regs->nip)) != 0) {
		/*
		 * Check that it's a sync instruction.
		 * As the address is in the exception table
		 * we should be able to read the instr there.
		 */
		if (*(unsigned int *)regs->nip == 0x7c0004ac) {
			unsigned int lsi = ((unsigned int *)regs->nip)[-1];
			int rb = (lsi >> 11) & 0x1f;
			printk(KERN_DEBUG "%s bad port %lx at %lx\n",
			       (lsi & 0x100)? "OUT to": "IN from",
			       regs->gpr[rb] - _IO_BASE, regs->nip);
			regs->nip = fixup;
			return;
		}
	}
#endif /* CONFIG_ALL_PPC */
	printk("Machine check in kernel mode.\n");
	printk("Caused by (from SRR1=%lx): ", regs->msr);
	switch (regs->msr & 0xF0000) {
	case 0x80000:
		printk("Machine check signal\n");
		break;
	case 0x40000:
		printk("Transfer error ack signal\n");
		break;
	case 0x20000:
		printk("Data parity error signal\n");
		break;
	case 0x10000:
		printk("Address parity error signal\n");
		break;
	default:
		printk("Unknown values in msr\n");
	}
	show_regs(regs);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	debugger(regs);
#endif
	print_backtrace((unsigned long *)regs->gpr[1]);
	panic("machine check");
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
InstructionBreakpoint(struct pt_regs *regs)
{
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_iabr_match(regs))
		return;
#endif
	_exception(SIGTRAP, regs);
}

void
RunModeException(struct pt_regs *regs)
{
	_exception(SIGTRAP, regs);	
}

/* Illegal instruction emulation support.  Originally written to
 * provide the PVR to user applications using the mfspr rd, PVR.
 * Return non-zero if we can't emulate, or EFAULT if the associated
 * memory access caused an access fault.  Return zero on success.
 *
 * There are a couple of ways to do this, either "decode" the instruction
 * or directly match lots of bits.  In this case, matching lots of
 * bits is faster and easier.
 *
 */
#define INST_MFSPR_PVR		0x7c1f42a6
#define INST_MFSPR_PVR_MASK	0xfc1fffff

static int
emulate_instruction(struct pt_regs *regs)
{
	uint    instword;
	uint    rd;
	uint    retval;

	retval = EFAULT;

	if (!user_mode(regs))
		return retval;

	if (get_user(instword, (uint *)(regs->nip)))
		return retval;

	/* Emulate the mfspr rD, PVR.
	 */
	if ((instword & INST_MFSPR_PVR_MASK) == INST_MFSPR_PVR) {
		rd = (instword >> 21) & 0x1f;
		regs->gpr[rd] = _get_PVR();
		retval = 0;
	}
	if (retval == 0)
		regs->nip += 4;
	return(retval);
}

void
ProgramCheckException(struct pt_regs *regs)
{
#if defined(CONFIG_4xx)
	unsigned int esr = mfspr(SPRN_ESR);

	if (esr & ESR_PTR) {
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		if (debugger_bpt(regs))
			return;
#endif
		_exception(SIGTRAP, regs);
	} else {
		_exception(SIGILL, regs);
	}
#else
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
		/* Try to emulate it if we should. */
		int errcode;
		if ((errcode = emulate_instruction(regs))) {
			if (errcode == EFAULT)
				_exception(SIGBUS, regs);
			else
				_exception(SIGILL, regs);
		}
	}
#endif
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

void
AlignmentException(struct pt_regs *regs)
{
	int fixed;

	fixed = fix_alignment(regs);
	if (fixed == 1) {
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

void
StackOverflow(struct pt_regs *regs)
{
	printk(KERN_CRIT "Kernel stack overflow in process %p, r1=%lx\n",
	       current, regs->gpr[1]);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	debugger(regs);
#endif
	show_regs(regs);
	print_backtrace((unsigned long *)regs->gpr[1]);
	panic("kernel stack overflow");
}

void
trace_syscall(struct pt_regs *regs)
{
	printk("Task: %p(%d), PC: %08lX/%08lX, Syscall: %3ld, Result: %s%ld\n",
	       current, current->pid, regs->nip, regs->link, regs->gpr[0],
	       regs->ccr&0x10000000?"Error=":"", regs->gpr[3]);
}

#ifdef CONFIG_8xx
void
SoftwareEmulation(struct pt_regs *regs)
{
	extern int do_mathemu(struct pt_regs *);
	int errcode;

	if (!user_mode(regs)) {
		show_regs(regs);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		debugger(regs);
#endif
		print_backtrace((unsigned long *)regs->gpr[1]);
		panic("Kernel Mode Software FPU Emulation");
	}

#ifdef CONFIG_MATH_EMULATION
	if ((errcode = do_mathemu(regs))) {
#else
	if ((errcode = Soft_emulate_8xx(regs))) {
#endif
		if (errcode > 0)
			_exception(SIGFPE, regs);
		else if (errcode == -EFAULT)
			_exception(SIGSEGV, regs);
		else
			_exception(SIGILL, regs);
	}
}
#endif

void
TAUException(struct pt_regs *regs)
{
	printk("TAU trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);
}

void __init trap_init(void)
{
}
