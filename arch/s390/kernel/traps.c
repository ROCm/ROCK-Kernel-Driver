/*
 *  arch/s390/kernel/traps.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *  Portions added by T. Halloran: (C) Copyright 2002 IBM Poughkeepsie, IBM Corporation
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
#include <linux/trigevent_hooks.h>

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
#if defined(CONFIG_NO_IDLE_HZ) || defined(CONFIG_VIRT_TIMER)
extern pgm_check_handler_t do_monitor_call;
#endif
#ifdef CONFIG_PFAULT
extern int pfault_init(void);
extern void pfault_fini(void);
extern void pfault_interrupt(struct pt_regs *regs, __u16 error_code);
static ext_int_info_t ext_int_pfault;
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
