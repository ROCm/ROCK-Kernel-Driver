/*
 *  arch/ppc/kernel/traps.c
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
#include <linux/module.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/xmon.h>
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif

extern int fix_alignment(struct pt_regs *);
extern void bad_page_fault(struct pt_regs *, unsigned long, int sig);

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
#else
#define debugger(regs)			do { } while (0)
#define debugger_bpt(regs)		0
#define debugger_sstep(regs)		0
#define debugger_iabr_match(regs)	0
#define debugger_dabr_match(regs)	0
#define debugger_fault_handler		((void (*)(struct pt_regs *))0)
#endif
#endif

/*
 * Trap & Exception support
 */


spinlock_t die_lock = SPIN_LOCK_UNLOCKED;

void die(const char * str, struct pt_regs * fp, long err)
{
	static int die_counter;
	console_verbose();
	spin_lock_irq(&die_lock);
#ifdef CONFIG_PMAC_BACKLIGHT
	set_backlight_enable(1);
	set_backlight_level(BACKLIGHT_MAX);
#endif
	printk("Oops: %s, sig: %ld [#%d]\n", str, err, ++die_counter);
	show_regs(fp);
	spin_unlock_irq(&die_lock);
	/* do_exit() should take care of panic'ing from an interrupt
	 * context so we don't handle it here
	 */
	do_exit(err);
}

void
_exception(int signr, struct pt_regs *regs)
{
	if (!user_mode(regs))
	{
		debugger(regs);
		die("Exception in kernel mode", regs, signr);
	}
	force_sig(signr, current);
}

/*
 * I/O accesses can cause machine checks on powermacs.
 * Check if the NIP corresponds to the address of a sync
 * instruction for which there is an entry in the exception
 * table.
 * Note that the 601 only takes a machine check on TEA
 * (transfer error ack) signal assertion, and does not
 * set any of the top 16 bits of SRR1.
 *  -- paulus.
 */
static inline int check_io_access(struct pt_regs *regs)
{
#ifdef CONFIG_PPC_PMAC
	unsigned long msr = regs->msr;
	const struct exception_table_entry *entry;
	unsigned int *nip = (unsigned int *)regs->nip;

	if (((msr & 0xffff0000) == 0 || (msr & (0x80000 | 0x40000)))
	    && (entry = search_exception_tables(regs->nip)) != NULL) {
		/*
		 * Check that it's a sync instruction, or somewhere
		 * in the twi; isync; nop sequence that inb/inw/inl uses.
		 * As the address is in the exception table
		 * we should be able to read the instr there.
		 * For the debug message, we look at the preceding
		 * load or store.
		 */
		if (*nip == 0x60000000)		/* nop */
			nip -= 2;
		else if (*nip == 0x4c00012c)	/* isync */
			--nip;
		if (*nip == 0x7c0004ac || (*nip >> 26) == 3) {
			/* sync or twi */
			unsigned int rb;

			--nip;
			rb = (*nip >> 11) & 0x1f;
			printk(KERN_DEBUG "%s bad port %lx at %p\n",
			       (*nip & 0x100)? "OUT to": "IN from",
			       regs->gpr[rb] - _IO_BASE, nip);
			regs->msr |= MSR_RI;
			regs->nip = entry->fixup;
			return 1;
		}
	}
#endif /* CONFIG_PPC_PMAC */
	return 0;
}

void
MachineCheckException(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		regs->msr |= MSR_RI;
		_exception(SIGSEGV, regs);
		return;
	}

#if defined(CONFIG_8xx) && defined(CONFIG_PCI)
	/* the qspan pci read routines can cause machine checks -- Cort */
	bad_page_fault(regs, regs->dar, SIGBUS);
	return;
#endif

	if (debugger_fault_handler) {
		debugger_fault_handler(regs);
		regs->msr |= MSR_RI;
		return;
	}

	if (check_io_access(regs))
		return;

#ifndef CONFIG_4xx
	printk(KERN_CRIT "Machine check in kernel mode.\n");
	printk(KERN_CRIT "Caused by (from SRR1=%lx): ", regs->msr);
	switch (regs->msr & 0x601F0000) {
	case 0x80000:
		printk("Machine check signal\n");
		break;
	case 0:		/* for 601 */
	case 0x40000:
	case 0x140000:	/* 7450 MSS error and TEA */
		printk("Transfer error ack signal\n");
		break;
	case 0x20000:
		printk("Data parity error signal\n");
		break;
	case 0x10000:
		printk("Address parity error signal\n");
		break;
	case 0x20000000:
		printk("L1 Data Cache error\n");
		break;
	case 0x40000000:
		printk("L1 Instruction Cache error\n");
		break;
	case 0x00100000:
		printk("L2 data cache parity error\n");
		break;
	default:
		printk("Unknown values in msr\n");
	}

#else /* CONFIG_4xx */
	/* Note that the ESR gets stored in regs->dsisr on 4xx. */
	if (regs->dsisr & ESR_MCI) {
		printk(KERN_CRIT "Instruction");
		mtspr(SPRN_ESR, regs->dsisr & ~ESR_MCI);
	} else
		printk(KERN_CRIT "Data");
	printk(" machine check in kernel mode.\n");
#endif /* CONFIG_4xx */

	debugger(regs);
	die("machine check", regs, SIGBUS);
}

void
SMIException(struct pt_regs *regs)
{
	debugger(regs);
#if !(defined(CONFIG_XMON) || defined(CONFIG_KGDB))
	show_regs(regs);
	panic("System Management Interrupt");
#endif
}

void
UnknownException(struct pt_regs *regs)
{
	printk("Bad trap at PC: %lx, MSR: %lx, vector=%lx    %s\n",
	       regs->nip, regs->msr, regs->trap, print_tainted());
	_exception(SIGTRAP, regs);
}

void
InstructionBreakpoint(struct pt_regs *regs)
{
	if (debugger_iabr_match(regs))
		return;
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

	retval = EINVAL;

	if (!user_mode(regs))
		return retval;
	CHECK_FULL_REGS(regs);

	if (get_user(instword, (uint *)(regs->nip)))
		return -EFAULT;

	/* Emulate the mfspr rD, PVR.
	 */
	if ((instword & INST_MFSPR_PVR_MASK) == INST_MFSPR_PVR) {
		rd = (instword >> 21) & 0x1f;
		regs->gpr[rd] = mfspr(PVR);
		retval = 0;
	}
	if (retval == 0)
		regs->nip += 4;
	return(retval);
}

void
ProgramCheckException(struct pt_regs *regs)
{
	int errcode;

#if defined(CONFIG_4xx)
	unsigned int esr = regs->dsisr;
	int isbpt = esr & ESR_PTR;
	extern int do_mathemu(struct pt_regs *regs);

#ifdef CONFIG_MATH_EMULATION
	if (!isbpt && do_mathemu(regs) == 0)
		return;
#endif /* CONFIG_MATH_EMULATION */

#else /* ! CONFIG_4xx */
	int isbpt = regs->msr & 0x20000;

	if (regs->msr & 0x100000) {
		/* IEEE FP exception */
		_exception(SIGFPE, regs);
		return;
	}
#endif /* ! CONFIG_4xx */

	if (isbpt) {
		/* trap exception */
		if (debugger_bpt(regs))
			return;
		_exception(SIGTRAP, regs);
		return;
	}

	/* Try to emulate it if we should. */
	if ((errcode = emulate_instruction(regs))) {
		if (errcode == -EFAULT)
			_exception(SIGBUS, regs);
		else
			_exception(SIGILL, regs);
	}
}

void
SingleStepException(struct pt_regs *regs)
{
	regs->msr &= ~MSR_SE;  /* Turn off 'trace' bit */
	if (debugger_sstep(regs))
		return;
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
			bad_page_fault(regs, regs->dar, SIGSEGV);
		return;
	}
	_exception(SIGBUS, regs);
}

void
StackOverflow(struct pt_regs *regs)
{
	printk(KERN_CRIT "Kernel stack overflow in process %p, r1=%lx\n",
	       current, regs->gpr[1]);
	debugger(regs);
	show_regs(regs);
	panic("kernel stack overflow");
}

void nonrecoverable_exception(struct pt_regs *regs)
{
	printk(KERN_ERR "Non-recoverable exception at PC=%lx MSR=%lx\n",
	       regs->nip, regs->msr);
	debugger(regs);
	die("nonrecoverable exception", regs, SIGKILL);
}

void
trace_syscall(struct pt_regs *regs)
{
	printk("Task: %p(%d), PC: %08lX/%08lX, Syscall: %3ld, Result: %s%ld    %s\n",
	       current, current->pid, regs->nip, regs->link, regs->gpr[0],
	       regs->ccr&0x10000000?"Error=":"", regs->gpr[3], print_tainted());
}

#ifdef CONFIG_8xx
void
SoftwareEmulation(struct pt_regs *regs)
{
	extern int do_mathemu(struct pt_regs *);
	extern int Soft_emulate_8xx(struct pt_regs *);
	int errcode;

	CHECK_FULL_REGS(regs);

	if (!user_mode(regs)) {
		debugger(regs);
		die("Kernel Mode Software FPU Emulation", regs, SIGFPE);
	}

#ifdef CONFIG_MATH_EMULATION
	errcode = do_mathemu(regs);
#else
	errcode = Soft_emulate_8xx(regs);
#endif
	if (errcode) {
		if (errcode > 0)
			_exception(SIGFPE, regs);
		else if (errcode == -EFAULT)
			_exception(SIGSEGV, regs);
		else
			_exception(SIGILL, regs);
	}
}
#endif /* CONFIG_8xx */

#if defined(CONFIG_4xx)

void DebugException(struct pt_regs *regs, unsigned long debug_status)
{
#if 0
	if (debug_status & DBSR_TIE) {		/* trap instruction*/
		if (!user_mode(regs) && debugger_bpt(regs))
			return;
		_exception(SIGTRAP, regs);

	}
#endif
	if (debug_status & DBSR_IC) {	/* instruction completion */
		if (!user_mode(regs) && debugger_sstep(regs))
			return;
		current->thread.dbcr0 &= ~DBCR0_IC;
		_exception(SIGTRAP, regs);
	}
}
#endif /* CONFIG_4xx */

#if !defined(CONFIG_TAU_INT)
void
TAUException(struct pt_regs *regs)
{
	printk("TAU trap at PC: %lx, MSR: %lx, vector=%lx    %s\n",
	       regs->nip, regs->msr, regs->trap, print_tainted());
}
#endif /* CONFIG_INT_TAU */

void __init trap_init(void)
{
}
