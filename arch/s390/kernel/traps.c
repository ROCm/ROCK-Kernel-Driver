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
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/mathemu.h>
#include <asm/cpcmd.h>
#include <asm/s390_ext.h>
#include <asm/lowcore.h>

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

extern pgm_check_handler_t do_protection_exception;
extern pgm_check_handler_t do_segment_exception;
extern pgm_check_handler_t do_region_exception;
extern pgm_check_handler_t do_page_exception;
extern pgm_check_handler_t do_pseudo_page_fault;
#ifdef CONFIG_PFAULT
extern int pfault_init(void);
extern void pfault_fini(void);
extern void pfault_interrupt(struct pt_regs *regs, __u16 error_code);
static ext_int_info_t ext_int_pfault;
#endif
#ifdef CONFIG_VIRT_TIMER
extern pgm_check_handler_t do_monitor_call;
#endif

#define stack_pointer ({ void **sp; asm("la %0,0(15)" : "=&d" (sp)); sp; })

#ifndef CONFIG_ARCH_S390X
#define RET_ADDR 56
#define FOURLONG "%08lx %08lx %08lx %08lx\n"
static int kstack_depth_to_print = 12;

#else /* CONFIG_ARCH_S390X */
#define RET_ADDR 112
#define FOURLONG "%016lx %016lx %016lx %016lx\n"
static int kstack_depth_to_print = 20;

#endif /* CONFIG_ARCH_S390X */

void show_trace(struct task_struct *task, unsigned long * stack)
{
	unsigned long backchain, low_addr, high_addr, ret_addr;

	if (!stack)
		stack = (task == NULL) ? *stack_pointer : &(task->thread.ksp);

	printk("Call Trace:\n");
	low_addr = ((unsigned long) stack) & PSW_ADDR_INSN;
	high_addr = (low_addr & (-THREAD_SIZE)) + THREAD_SIZE;
	/* Skip the first frame (biased stack) */
	backchain = *((unsigned long *) low_addr) & PSW_ADDR_INSN;
	/* Print up to 8 lines */
	while  (backchain > low_addr && backchain <= high_addr) {
		ret_addr = *((unsigned long *) (backchain+RET_ADDR)) & PSW_ADDR_INSN;
		printk(" [<%016lx>] ", ret_addr);
		print_symbol("%s\n", ret_addr);
		low_addr = backchain;
		backchain = *((unsigned long *) backchain) & PSW_ADDR_INSN;
	}
	printk("\n");
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	unsigned long *stack;
	int i;

	// debugging aid: "show_stack(NULL);" prints the
	// back trace for this cpu.

	if (!sp) {
		if (task)
			sp = (unsigned long *) task->thread.ksp;
		else
			sp = *stack_pointer;
	}

	stack = sp;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (((addr_t) stack & (THREAD_SIZE-1)) == 0)
			break;
		if (i && ((i * sizeof (long) % 32) == 0))
			printk("\n       ");
		printk("%p ", (void *)*stack++);
	}
	printk("\n");
	show_trace(task, sp);
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	show_stack(0, 0);
}

EXPORT_SYMBOL(dump_stack);

void show_registers(struct pt_regs *regs)
{
	mm_segment_t old_fs;
	char *mode;
	int i;

	mode = (regs->psw.mask & PSW_MASK_PSTATE) ? "User" : "Krnl";
	printk("%s PSW : %p %p",
	       mode, (void *) regs->psw.mask,
	       (void *) regs->psw.addr);
	print_symbol(" (%s)\n", regs->psw.addr & PSW_ADDR_INSN);
	printk("%s GPRS: " FOURLONG, mode,
	       regs->gprs[0], regs->gprs[1], regs->gprs[2], regs->gprs[3]);
	printk("           " FOURLONG,
	       regs->gprs[4], regs->gprs[5], regs->gprs[6], regs->gprs[7]);
	printk("           " FOURLONG,
	       regs->gprs[8], regs->gprs[9], regs->gprs[10], regs->gprs[11]);
	printk("           " FOURLONG,
	       regs->gprs[12], regs->gprs[13], regs->gprs[14], regs->gprs[15]);

#if 0
	/* FIXME: this isn't needed any more but it changes the ksymoops
	 * input. To remove or not to remove ... */
	save_access_regs(regs->acrs);
	printk("%s ACRS: %08x %08x %08x %08x\n", mode,
	       regs->acrs[0], regs->acrs[1], regs->acrs[2], regs->acrs[3]);
	printk("           %08x %08x %08x %08x\n",
	       regs->acrs[4], regs->acrs[5], regs->acrs[6], regs->acrs[7]);
	printk("           %08x %08x %08x %08x\n",
	       regs->acrs[8], regs->acrs[9], regs->acrs[10], regs->acrs[11]);
	printk("           %08x %08x %08x %08x\n",
	       regs->acrs[12], regs->acrs[13], regs->acrs[14], regs->acrs[15]);
#endif

	/*
	 * Print the first 20 byte of the instruction stream at the
	 * time of the fault.
	 */
	old_fs = get_fs();
	if (regs->psw.mask & PSW_MASK_PSTATE)
		set_fs(USER_DS);
	else
		set_fs(KERNEL_DS);
	printk("%s Code: ", mode);
	for (i = 0; i < 20; i++) {
		unsigned char c;
		if (__get_user(c, (char *)(regs->psw.addr + i))) {
			printk(" Bad PSW.");
			break;
		}
		printk("%02x ", c);
	}
	set_fs(old_fs);

	printk("\n");
}	

/* This is called from fs/proc/array.c */
char *task_show_regs(struct task_struct *task, char *buffer)
{
	struct pt_regs *regs;

	regs = __KSTK_PTREGS(task);
	buffer += sprintf(buffer, "task: %p, ksp: %p\n",
		       task, (void *)task->thread.ksp);
	buffer += sprintf(buffer, "User PSW : %p %p\n",
		       (void *) regs->psw.mask, (void *)regs->psw.addr);

	buffer += sprintf(buffer, "User GPRS: " FOURLONG,
			  regs->gprs[0], regs->gprs[1],
			  regs->gprs[2], regs->gprs[3]);
	buffer += sprintf(buffer, "           " FOURLONG,
			  regs->gprs[4], regs->gprs[5],
			  regs->gprs[6], regs->gprs[7]);
	buffer += sprintf(buffer, "           " FOURLONG,
			  regs->gprs[8], regs->gprs[9],
			  regs->gprs[10], regs->gprs[11]);
	buffer += sprintf(buffer, "           " FOURLONG,
			  regs->gprs[12], regs->gprs[13],
			  regs->gprs[14], regs->gprs[15]);
	buffer += sprintf(buffer, "User ACRS: %08x %08x %08x %08x\n",
			  task->thread.acrs[0], task->thread.acrs[1],
			  task->thread.acrs[2], task->thread.acrs[3]);
	buffer += sprintf(buffer, "           %08x %08x %08x %08x\n",
			  task->thread.acrs[4], task->thread.acrs[5],
			  task->thread.acrs[6], task->thread.acrs[7]);
	buffer += sprintf(buffer, "           %08x %08x %08x %08x\n",
			  task->thread.acrs[8], task->thread.acrs[9],
			  task->thread.acrs[10], task->thread.acrs[11]);
	buffer += sprintf(buffer, "           %08x %08x %08x %08x\n",
			  task->thread.acrs[12], task->thread.acrs[13],
			  task->thread.acrs[14], task->thread.acrs[15]);
	return buffer;
}

spinlock_t die_lock = SPIN_LOCK_UNLOCKED;

void die(const char * str, struct pt_regs * regs, long err)
{
	static int die_counter;
        console_verbose();
        spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("%s: %04lx [#%d]\n", str, err & 0xffff, ++die_counter);
        show_regs(regs);
	bust_spinlocks(0);
        spin_unlock_irq(&die_lock);
	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception: panic_on_oops");
        do_exit(SIGSEGV);
}

static void inline do_trap(long interruption_code, int signr, char *str,
                           struct pt_regs *regs, siginfo_t *info)
{
	/*
	 * We got all needed information from the lowcore and can
	 * now safely switch on interrupts.
	 */
        if (regs->psw.mask & PSW_MASK_PSTATE)
		local_irq_enable();

        if (regs->psw.mask & PSW_MASK_PSTATE) {
                struct task_struct *tsk = current;

                tsk->thread.trap_no = interruption_code & 0xffff;
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
                const struct exception_table_entry *fixup;
                fixup = search_exception_tables(regs->psw.addr & PSW_ADDR_INSN);
                if (fixup)
                        regs->psw.addr = fixup->fixup | PSW_ADDR_AMODE;
                else
                        die(str, regs, interruption_code);
        }
}

static inline void *get_check_address(struct pt_regs *regs)
{
	return (void *)((regs->psw.addr-S390_lowcore.pgm_ilc) & PSW_ADDR_INSN);
}

int do_debugger_trap(struct pt_regs *regs)
{
	if ((regs->psw.mask & PSW_MASK_PSTATE) &&
	    (current->ptrace & PT_PTRACED)) {
		force_sig(SIGTRAP,current);
		return 0;
	}
	return 1;
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

DO_ERROR(SIGSEGV, "Unknown program exception", default_trap_handler)

DO_ERROR_INFO(SIGBUS, "addressing exception", addressing_exception,
	      BUS_ADRERR, get_check_address(regs))
DO_ERROR_INFO(SIGILL,  "execute exception", execute_exception,
	      ILL_ILLOPN, get_check_address(regs))
DO_ERROR_INFO(SIGFPE,  "fixpoint divide exception", divide_exception,
	      FPE_INTDIV, get_check_address(regs))
DO_ERROR_INFO(SIGILL,  "operand exception", operand_exception,
	      ILL_ILLOPN, get_check_address(regs))
DO_ERROR_INFO(SIGILL,  "privileged operation", privileged_op,
	      ILL_PRVOPC, get_check_address(regs))
DO_ERROR_INFO(SIGILL,  "special operation exception", special_op_exception,
	      ILL_ILLOPN, get_check_address(regs))
DO_ERROR_INFO(SIGILL,  "translation exception", translation_exception,
	      ILL_ILLOPN, get_check_address(regs))

static inline void
do_fp_trap(struct pt_regs *regs, void *location,
           int fpc, long interruption_code)
{
	siginfo_t si;

	si.si_signo = SIGFPE;
	si.si_errno = 0;
	si.si_addr = location;
	si.si_code = 0;
	/* FPC[2] is Data Exception Code */
	if ((fpc & 0x00000300) == 0) {
		/* bits 6 and 7 of DXC are 0 iff IEEE exception */
		if (fpc & 0x8000) /* invalid fp operation */
			si.si_code = FPE_FLTINV;
		else if (fpc & 0x4000) /* div by 0 */
			si.si_code = FPE_FLTDIV;
		else if (fpc & 0x2000) /* overflow */
			si.si_code = FPE_FLTOVF;
		else if (fpc & 0x1000) /* underflow */
			si.si_code = FPE_FLTUND;
		else if (fpc & 0x0800) /* inexact */
			si.si_code = FPE_FLTRES;
	}
	current->thread.ieee_instruction_pointer = (addr_t) location;
	do_trap(interruption_code, SIGFPE,
		"floating point exception", regs, &si);
}

asmlinkage void illegal_op(struct pt_regs * regs, long interruption_code)
{
        __u8 opcode[6];
	__u16 *location;
	int signal = 0;

	location = (__u16 *) get_check_address(regs);

	/*
	 * We got all needed information from the lowcore and can
	 * now safely switch on interrupts.
	 */
	if (regs->psw.mask & PSW_MASK_PSTATE)
		local_irq_enable();

	if (regs->psw.mask & PSW_MASK_PSTATE)
		get_user(*((__u16 *) opcode), location);
	else
		*((__u16 *)opcode)=*((__u16 *)location);
	if (*((__u16 *)opcode)==S390_BREAKPOINT_U16)
        {
		if(do_debugger_trap(regs))
			signal = SIGILL;
	}
#ifdef CONFIG_MATHEMU
        else if (regs->psw.mask & PSW_MASK_PSTATE)
	{
		if (opcode[0] == 0xb3) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_b3(opcode, regs);
                } else if (opcode[0] == 0xed) {
			get_user(*((__u32 *) (opcode+2)),
				 (__u32 *)(location+1));
			signal = math_emu_ed(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb299) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_srnm(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb29c) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_stfpc(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb29d) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_lfpc(opcode, regs);
		} else
			signal = SIGILL;
        }
#endif 
	else
		signal = SIGILL;
        if (signal == SIGFPE)
		do_fp_trap(regs, location,
                           current->thread.fp_regs.fpc, interruption_code);
        else if (signal)
		do_trap(interruption_code, signal,
			"illegal operation", regs, NULL);
}


#ifdef CONFIG_MATHEMU
asmlinkage void 
specification_exception(struct pt_regs * regs, long interruption_code)
{
        __u8 opcode[6];
	__u16 *location = NULL;
	int signal = 0;

	location = (__u16 *) get_check_address(regs);

	/*
	 * We got all needed information from the lowcore and can
	 * now safely switch on interrupts.
	 */
	if (regs->psw.mask & PSW_MASK_PSTATE)
		local_irq_enable();
		
        if (regs->psw.mask & PSW_MASK_PSTATE) {
		get_user(*((__u16 *) opcode), location);
		switch (opcode[0]) {
		case 0x28: /* LDR Rx,Ry   */
			signal = math_emu_ldr(opcode);
			break;
		case 0x38: /* LER Rx,Ry   */
			signal = math_emu_ler(opcode);
			break;
		case 0x60: /* STD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_std(opcode, regs);
			break;
		case 0x68: /* LD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ld(opcode, regs);
			break;
		case 0x70: /* STE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ste(opcode, regs);
			break;
		case 0x78: /* LE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_le(opcode, regs);
			break;
		default:
			signal = SIGILL;
			break;
                }
        } else
		signal = SIGILL;
        if (signal == SIGFPE)
		do_fp_trap(regs, location,
                           current->thread.fp_regs.fpc, interruption_code);
        else if (signal) {
		siginfo_t info;
		info.si_signo = signal;
		info.si_errno = 0;
		info.si_code = ILL_ILLOPN;
		info.si_addr = location;
		do_trap(interruption_code, signal, 
			"specification exception", regs, &info);
	}
}
#else
DO_ERROR_INFO(SIGILL, "specification exception", specification_exception,
	      ILL_ILLOPN, get_check_address(regs));
#endif

asmlinkage void data_exception(struct pt_regs * regs, long interruption_code)
{
	__u16 *location;
	int signal = 0;

	location = (__u16 *) get_check_address(regs);

	/*
	 * We got all needed information from the lowcore and can
	 * now safely switch on interrupts.
	 */
	if (regs->psw.mask & PSW_MASK_PSTATE)
		local_irq_enable();

	if (MACHINE_HAS_IEEE)
		__asm__ volatile ("stfpc %0\n\t" 
				  : "=m" (current->thread.fp_regs.fpc));

#ifdef CONFIG_MATHEMU
        else if (regs->psw.mask & PSW_MASK_PSTATE) {
        	__u8 opcode[6];
		get_user(*((__u16 *) opcode), location);
		switch (opcode[0]) {
		case 0x28: /* LDR Rx,Ry   */
			signal = math_emu_ldr(opcode);
			break;
		case 0x38: /* LER Rx,Ry   */
			signal = math_emu_ler(opcode);
			break;
		case 0x60: /* STD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_std(opcode, regs);
			break;
		case 0x68: /* LD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ld(opcode, regs);
			break;
		case 0x70: /* STE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ste(opcode, regs);
			break;
		case 0x78: /* LE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_le(opcode, regs);
			break;
		case 0xb3:
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_b3(opcode, regs);
			break;
                case 0xed:
			get_user(*((__u32 *) (opcode+2)),
				 (__u32 *)(location+1));
			signal = math_emu_ed(opcode, regs);
			break;
	        case 0xb2:
			if (opcode[1] == 0x99) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				signal = math_emu_srnm(opcode, regs);
			} else if (opcode[1] == 0x9c) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				signal = math_emu_stfpc(opcode, regs);
			} else if (opcode[1] == 0x9d) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				signal = math_emu_lfpc(opcode, regs);
			} else
				signal = SIGILL;
			break;
		default:
			signal = SIGILL;
			break;
                }
        }
#endif 
	if (current->thread.fp_regs.fpc & FPC_DXC_MASK)
		signal = SIGFPE;
	else
		signal = SIGILL;
        if (signal == SIGFPE)
		do_fp_trap(regs, location,
                           current->thread.fp_regs.fpc, interruption_code);
        else if (signal) {
		siginfo_t info;
		info.si_signo = signal;
		info.si_errno = 0;
		info.si_code = ILL_ILLOPN;
		info.si_addr = location;
		do_trap(interruption_code, signal, 
			"data exception", regs, &info);
	}
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
        pgm_check_table[4] = &do_protection_exception;
        pgm_check_table[5] = &addressing_exception;
        pgm_check_table[6] = &specification_exception;
        pgm_check_table[7] = &data_exception;
        pgm_check_table[9] = &divide_exception;
        pgm_check_table[0x10] = &do_segment_exception;
        pgm_check_table[0x11] = &do_page_exception;
        pgm_check_table[0x12] = &translation_exception;
        pgm_check_table[0x13] = &special_op_exception;
#ifndef CONFIG_ARCH_S390X
 	pgm_check_table[0x14] = &do_pseudo_page_fault;
#else /* CONFIG_ARCH_S390X */
        pgm_check_table[0x38] = &addressing_exception;
        pgm_check_table[0x3B] = &do_region_exception;
#endif /* CONFIG_ARCH_S390X */
        pgm_check_table[0x15] = &operand_exception;
        pgm_check_table[0x1C] = &privileged_op;
#ifdef CONFIG_VIRT_TIMER
	pgm_check_table[0x40] = &do_monitor_call;
#endif
	if (MACHINE_IS_VM) {
		/*
		 * First try to get pfault pseudo page faults going.
		 * If this isn't available turn on pagex page faults.
		 */
#ifdef CONFIG_PFAULT
		/* request the 0x2603 external interrupt */
		if (register_early_external_interrupt(0x2603, pfault_interrupt,
						      &ext_int_pfault) != 0)
			panic("Couldn't request external interrupt 0x2603");

		if (pfault_init() == 0) 
			return;
		
		/* Tough luck, no pfault. */
		unregister_early_external_interrupt(0x2603, pfault_interrupt,
						    &ext_int_pfault);
#endif
#ifndef CONFIG_ARCH_S390X
		cpcmd("SET PAGEX ON", NULL, 0);
#endif
	}
}
