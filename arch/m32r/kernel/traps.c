/*
 *  linux/arch/m32r/kernel/traps.c
 *
 *  Copyright (C) 2001, 2002  Hirokazu Takata, Hiroyuki Kondo,
 *                            Hitoshi Yamamoto
 */

/* $Id$ */

/*
 * 'traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/processor.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>

#include <asm/smp.h>

#include <linux/module.h>

#if defined(CONFIG_MMU)
#define PIE_HANDLER "pie_handler"
#define ACE_HANDLER "ace_handler"
#define TME_HANDLER "tme_handler"
#else
#define PIE_HANDLER "default_eit_handler"
#define ACE_HANDLER "default_eit_handler"
#define TME_HANDLER "default_eit_handler"
#endif

asmlinkage void alignment_check(void);
asmlinkage void ei_handler(void);
asmlinkage void rie_handler(void);
asmlinkage void debug_trap(void);
asmlinkage void cache_flushing_handler(void);

asm (
	"	.section .eit_vector,\"ax\"	\n"
	"	.balign 4			\n"
	"	.global _RE			\n"
	"	.global default_eit_handler	\n"
	"	.global system_call		\n"
	"	.global " PIE_HANDLER "		\n"
	"	.global " ACE_HANDLER "		\n"
	"	.global " TME_HANDLER "		\n"
	"_RE:	seth	r0, 0x01		\n"
	"	bra	default_eit_handler	\n"
	"	.long	0,0			\n"
	"_SBI:	seth	r0, 0x10		\n"
	"	bra	default_eit_handler	\n"
	"	.long	0,0			\n"
	"_RIE:	bra	rie_handler		\n"
	"	.long	0,0,0			\n"
	"_AE:	bra	alignment_check		\n"
	"	.long	0,0,0			\n"
	"_TRAP0:				\n"
	"	bra	_TRAP0			\n"
	"_TRAP1:				\n"
	"	bra	debug_trap		\n"
	"_TRAP2:				\n"
	"	bra	system_call		\n"
	"_TRAP3:				\n"
	"	bra	_TRAP3			\n"
	"_TRAP4:				\n"
	"	bra	_TRAP4			\n"
	"_TRAP5:				\n"
	"	bra	_TRAP5			\n"
	"_TRAP6:				\n"
	"	bra	_TRAP6			\n"
	"_TRAP7:				\n"
	"	bra	_TRAP7			\n"
	"_TRAP8:				\n"
	"	bra	_TRAP8			\n"
	"_TRAP9:				\n"
	"	bra	_TRAP9			\n"
	"_TRAP10:				\n"
	"	bra	_TRAP10			\n"
	"_TRAP11:				\n"
	"	bra	_TRAP11			\n"
	"_TRAP12:				\n"
	"	bra	cache_flushing_handler	\n"
	"_TRAP13:				\n"
	"	bra	_TRAP13			\n"
	"_TRAP14:				\n"
	"	bra	_TRAP14			\n"
	"_TRAP15:				\n"
	"	bra	_TRAP15			\n"
	"_EI:	bra	ei_handler		\n"
	"	.long   0,0,0			\n"
	"	.previous			\n"
);

asm (
	"	.section .eit_vector1,\"ax\"	\n"
	"_BRA_SYSCAL:				\n"
	"	bra	system_call		\n"
	"	.long	0,0,0			\n"
	"	.previous			\n"
);

asm (
	"	.section .eit_vector2,\"ax\"	\n"
	"_PIE:					\n"
	"	bra	" PIE_HANDLER "		\n"
	"	.long   0,0,0			\n"
	"_TLB_ACE:				\n"
	"	bra	" ACE_HANDLER "		\n"
	"	.long   0,0,0			\n"
	"_TLB_MIS:				\n"
	"	bra	" TME_HANDLER "		\n"
	"	.long   0,0,0			\n"
	"	.previous 			\n"
);

#ifdef CONFIG_SMP
/*
 * for IPI
 */
asm (
	"	.section .eit_vector3,\"ax\"		\n"
	"	.global smp_reschedule_interrupt	\n"
	"	.global smp_invalidate_interrupt	\n"
	"	.global smp_call_function_interrupt	\n"
	"	.global smp_ipi_timer_interrupt		\n"
	"	.global smp_flush_cache_all_interrupt	\n"
	"	.global _EI_VEC_TABLE			\n"
	"_EI_VEC_TABLE:					\n"
	"	.fill 56, 4, 0				\n"
	"	.long smp_reschedule_interrupt		\n"
	"	.long smp_invalidate_interrupt		\n"
	"	.long smp_call_function_interrupt	\n"
	"	.long smp_ipi_timer_interrupt		\n"
	"	.long smp_flush_cache_all_interrupt	\n"
	"	.fill 4, 4, 0				\n"
	"	.previous				\n"
);

/*
 * for Boot AP function
 */
asm (
	"	.section .eit_vector4,\"ax\"	\n"
	"	.global _AP_RE			\n"
	"	.global startup_AP		\n"
	"_AP_RE:				\n"
	"	.fill 32, 4, 0			\n"
	"_AP_EI: bra	startup_AP		\n"
	"	.previous			\n"
);
#endif  /* CONFIG_SMP */

#define	set_eit_vector_entries(void)	do { } while (0)

void __init trap_init(void)
{
	set_eit_vector_entries();

	/*
	 * Should be a barrier for any external CPU state.
	 */
	cpu_init();
}

int kstack_depth_to_print = 24;

void show_trace(struct task_struct *task, unsigned long *stack)
{
	unsigned long addr;

	if (!stack)
		stack = (unsigned long*)&stack;

	printk("Call Trace: ");
	while (!kstack_end(stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr)) {
			printk("[<%08lx>] ", addr);
			print_symbol("%s\n", addr);
		}
	}
	printk("\n");
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	unsigned long  *stack;
	int  i;

	/*
	 * debugging aid: "show_stack(NULL);" prints the
	 * back trace for this cpu.
	 */

	if(sp==NULL) {
		if (task)
			sp = (unsigned long *)task->thread.sp;
		else
			sp=(unsigned long*)&sp;
	}

	stack = sp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % 4) == 0))
			printk("\n       ");
		printk("%08lx ", *stack++);
	}
	printk("\n");
	show_trace(task, sp);
}

void dump_stack(void)
{
	unsigned long stack;

	show_trace(current, &stack);
}

EXPORT_SYMBOL(dump_stack);

static void show_registers(struct pt_regs *regs)
{
	int i = 0;
	int in_kernel = 1;
	unsigned long sp;

	printk("CPU:    %d\n", smp_processor_id());
	show_regs(regs);

	sp = (unsigned long) (1+regs);
	if (user_mode(regs)) {
		in_kernel = 0;
		sp = regs->spu;
		printk("SPU: %08lx\n", sp);
	} else {
		printk("SPI: %08lx\n", sp);
	}
	printk("Process %s (pid: %d, process nr: %d, stackpage=%08lx)",
		current->comm, current->pid, 0xffff & i, 4096+(unsigned long)current);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {
		printk("\nStack: ");
		show_stack(current, (unsigned long*) sp);

		printk("\nCode: ");
		if (regs->bpc < PAGE_OFFSET)
			goto bad;

		for(i=0;i<20;i++) {
			unsigned char c;
			if (__get_user(c, &((unsigned char*)regs->bpc)[i])) {
bad:
				printk(" Bad PC value.");
				break;
			}
			printk("%02x ", c);
		}
	}
	printk("\n");
}

spinlock_t die_lock = SPIN_LOCK_UNLOCKED;

void die(const char * str, struct pt_regs * regs, long err)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("%s: %04lx\n", str, err & 0xffff);
	show_registers(regs);
	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static __inline__ void die_if_kernel(const char * str,
	struct pt_regs * regs, long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

static __inline__ void do_trap(int trapnr, int signr, const char * str,
	struct pt_regs * regs, long error_code, siginfo_t *info)
{
	if (user_mode(regs)) {
		/* trap_signal */
		struct task_struct *tsk = current;
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = trapnr;
		if (info)
			force_sig_info(signr, info, tsk);
		else
			force_sig(signr, tsk);
		return;
	} else {
		/* kernel_trap */
		if (!fixup_exception(regs))
			die(str, regs, error_code);
		return;
	}
}

#define DO_ERROR(trapnr, signr, str, name) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	do_trap(trapnr, signr, 0, regs, error_code, NULL); \
}

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void __user *)siaddr; \
	do_trap(trapnr, signr, str, regs, error_code, &info); \
}

DO_ERROR( 1, SIGTRAP, "debug trap", debug_trap)
DO_ERROR_INFO(0x20, SIGILL,  "reserved instruction ", rie_handler, ILL_ILLOPC, regs->bpc)
DO_ERROR_INFO(0x100, SIGILL,  "privilege instruction", pie_handler, ILL_PRVOPC, regs->bpc)

extern int handle_unaligned_access(unsigned long, struct pt_regs *);

/* This code taken from arch/sh/kernel/traps.c */
asmlinkage void do_alignment_check(struct pt_regs *regs, long error_code)
{
	mm_segment_t oldfs;
	unsigned long insn;
	int tmp;

	oldfs = get_fs();

	if (user_mode(regs)) {
		local_irq_enable();
		current->thread.error_code = error_code;
		current->thread.trap_no = 0x17;

		set_fs(USER_DS);
		if (copy_from_user(&insn, (void *)regs->bpc, 4)) {
			set_fs(oldfs);
			goto uspace_segv;
		}
		tmp = handle_unaligned_access(insn, regs);
		set_fs(oldfs);

		if (!tmp)
			return;

	uspace_segv:
		printk(KERN_NOTICE "Killing process \"%s\" due to unaligned "
			"access\n", current->comm);
		force_sig(SIGSEGV, current);
	} else {
		set_fs(KERNEL_DS);
		if (copy_from_user(&insn, (void *)regs->bpc, 4)) {
			set_fs(oldfs);
			die("insn faulting in do_address_error", regs, 0);
		}
		handle_unaligned_access(insn, regs);
		set_fs(oldfs);
	}
}

