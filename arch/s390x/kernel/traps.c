/*
 *  arch/s390/kernel/traps.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Derived from "arch/i386/kernel/traps.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#if CONFIG_REMOTE_DEBUG
#include <asm/gdb-stub.h>
#endif
#include <asm/cpcmd.h>
#include <asm/s390_ext.h>

/* Called from entry.S only */
extern void handle_per_exception(struct pt_regs *regs);

typedef void pgm_check_handler_t(struct pt_regs *, long);
pgm_check_handler_t *pgm_check_table[128];

#ifdef CONFIG_SYSCTL
#ifdef CONFIG_PROCESS_DEBUG
int sysctl_userprocess_debug = 1;
#else
int sysctl_userprocess_debug = 0;
#endif
#endif

extern pgm_check_handler_t do_page_fault;
#ifdef CONFIG_PFAULT
extern int pfault_init(void);
extern void pfault_fini(void);
extern void pfault_interrupt(struct pt_regs *regs, __u16 error_code);
#endif

spinlock_t die_lock = SPIN_LOCK_UNLOCKED;

void die(const char * str, struct pt_regs * regs, long err)
{
        console_verbose();
        spin_lock_irq(&die_lock);
	bust_spinlocks(1);
        printk("%s: %04lx\n", str, err & 0xffff);
        show_regs(regs);
	bust_spinlocks(0);
        spin_unlock_irq(&die_lock);
        do_exit(SIGSEGV);
}

#define DO_ERROR(signr, str, name) \
asmlinkage void name(struct pt_regs * regs, long interruption_code) \
{ \
	do_trap(interruption_code, signr, str, regs, NULL); \
}

#define DO_ERROR_INFO(signr, str, name, sicode, siaddr) \
asmlinkage void name(struct pt_regs * regs, long interruption_code) \
{ \
        siginfo_t info; \
        info.si_signo = signr; \
        info.si_errno = 0; \
        info.si_code = sicode; \
        info.si_addr = (void *)siaddr; \
        do_trap(interruption_code, signr, str, regs, &info); \
}

static void inline do_trap(long interruption_code, int signr, char *str,
                           struct pt_regs *regs, siginfo_t *info)
{
        if (regs->psw.mask & PSW_PROBLEM_STATE) {
                struct task_struct *tsk = current;
                tsk->thread.trap_no = interruption_code;
		if (info)
			force_sig_info(signr, info, tsk);
		else
                	force_sig(signr, tsk);
#ifndef CONFIG_SYSCTL
#ifdef CONFIG_PROCESS_DEBUG
                printk("User process fault: interruption code 0x%lX\n",
                       interruption_code);
                show_regs(regs);
#endif
#else
		if (sysctl_userprocess_debug) {
			printk("User process fault: interruption code 0x%lX\n",
			       interruption_code);
			show_regs(regs);
		}
#endif
        } else {
                unsigned long fixup = search_exception_table(regs->psw.addr);
                if (fixup)
                        regs->psw.addr = fixup;
                else
                        die(str, regs, interruption_code);
        }
}

int do_debugger_trap(struct pt_regs *regs,int signal)
{
	if(regs->psw.mask&PSW_PROBLEM_STATE)
	{
		if(current->ptrace & PT_PTRACED)
			force_sig(signal,current);
		else
			return 1;
	}
	else
	{
#if CONFIG_REMOTE_DEBUG
		if(gdb_stub_initialised)
		{
			gdb_stub_handle_exception((gdb_pt_regs *)regs,signal);
			return 0;
		}
#endif
		return 1;
	}
	return 0;
}

DO_ERROR(SIGSEGV, "Unknown program exception", default_trap_handler)
DO_ERROR(SIGILL,  "privileged operation", privileged_op)
DO_ERROR(SIGILL,  "execute exception", execute_exception)
DO_ERROR(SIGSEGV, "addressing exception", addressing_exception)
DO_ERROR(SIGFPE,  "fixpoint divide exception", divide_exception)
DO_ERROR(SIGILL,  "translation exception", translation_exception)
DO_ERROR(SIGILL,  "special operand exception", special_op_exception)
DO_ERROR(SIGILL,  "operand exception", operand_exception)
DO_ERROR(SIGILL,  "specification exception", specification_exception);

asmlinkage void illegal_op(struct pt_regs * regs, long interruption_code)
{
        __u8 opcode[6];
	__u16 *location;
	int do_sig = 0;

	location = (__u16 *)(regs->psw.addr-S390_lowcore.pgm_ilc);
	/* WARNING don't change this check back to */
	/* int problem_state=(regs->psw.mask & PSW_PROBLEM_STATE); */
	/* & then doing if(problem_state) an int is too small for this */
	/* check on 64 bit. */
	if(regs->psw.mask & PSW_PROBLEM_STATE)
		get_user(*((__u16 *) opcode), location);
	else
		*((__u16 *)opcode)=*((__u16 *)location);
	if(*((__u16 *)opcode)==S390_BREAKPOINT_U16)
        {
		if(do_debugger_trap(regs,SIGTRAP))
			do_sig=1;
	}
	else
		do_sig = 1;
	if (do_sig)
		do_trap(interruption_code, SIGILL, "illegal operation", regs, NULL);
}

asmlinkage void data_exception(struct pt_regs * regs, long interruption_code)
{
	__u16 *location;
	int do_sig = 0;

	location = (__u16 *)(regs->psw.addr-S390_lowcore.pgm_ilc);
	__asm__ volatile ("stfpc %0\n\t" 
			  : "=m" (current->thread.fp_regs.fpc));
	/* Same code should work when we implement fpu emulation */
	/* provided we call data exception from the fpu emulator */
	if(current->thread.fp_regs.fpc&FPC_DXC_MASK)
	{
		current->thread.ieee_instruction_pointer=(addr_t)location;
		force_sig(SIGFPE, current);
	}
	else
		do_sig = 1;
        if (do_sig)
                do_trap(interruption_code, SIGILL, "data exception", regs, NULL);
}



/* init is done in lowcore.S and head.S */

void __init trap_init(void)
{
        int i;

        for (i = 0; i < 128; i++)
          pgm_check_table[i] = &default_trap_handler;
        pgm_check_table[1] = &illegal_op;
        pgm_check_table[2] = &privileged_op;
        pgm_check_table[3] = &execute_exception;
        pgm_check_table[5] = &addressing_exception;
        pgm_check_table[6] = &specification_exception;
        pgm_check_table[7] = &data_exception;
        pgm_check_table[9] = &divide_exception;
        pgm_check_table[0x12] = &translation_exception;
        pgm_check_table[0x13] = &special_op_exception;
        pgm_check_table[0x15] = &operand_exception;
        pgm_check_table[4] = &do_page_fault;
        pgm_check_table[0x10] = &do_page_fault;
        pgm_check_table[0x11] = &do_page_fault;
        pgm_check_table[0x1C] = &privileged_op;
        pgm_check_table[0x38] = &addressing_exception;
        pgm_check_table[0x3B] = &do_page_fault;
#ifdef CONFIG_PFAULT
	if (MACHINE_IS_VM) {
		/* request the 0x2603 external interrupt */
		if (register_external_interrupt(0x2603, pfault_interrupt) != 0)
			panic("Couldn't request external interrupt 0x2603");
		/*
		 * Try to get pfault pseudo page faults going.
		 */
		if (pfault_init() != 0) {
			/* Tough luck, no pfault. */
			unregister_external_interrupt(0x2603,
						      pfault_interrupt);
		}
	}
#endif
}


void handle_per_exception(struct pt_regs *regs)
{
	if(regs->psw.mask&PSW_PROBLEM_STATE)
	{
		per_struct *per_info=&current->thread.per_info;
		per_info->lowcore.words.perc_atmid=S390_lowcore.per_perc_atmid;
		per_info->lowcore.words.address=S390_lowcore.per_address;
		per_info->lowcore.words.access_id=S390_lowcore.per_access_id;
	}
	if(do_debugger_trap(regs,SIGTRAP))
	{
		/* I've seen this possibly a task structure being reused ? */
		printk("Spurious per exception detected\n");
		printk("switching off per tracing for this task.\n");
		show_regs(regs);
		/* Hopefully switching off per tracing will help us survive */
		regs->psw.mask &= ~PSW_PER_MASK;
	}
}

