/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/kernel.h"
#include "linux/module.h"
#include "linux/kallsyms.h"
#include "asm/page.h"
#include "asm/processor.h"
#include "sysrq.h"
#include "user_util.h"

void show_trace(unsigned long * stack)
{
	/* XXX: Copy the CONFIG_FRAME_POINTER stack-walking backtrace from
	 * arch/i386/kernel/traps.c. */
        unsigned long addr;

        if (!stack)
                stack = (unsigned long*) &stack;

        printk("Call Trace: \n");
        while (((long) stack & (THREAD_SIZE-1)) != 0) {
                addr = *stack++;
		if (__kernel_text_address(addr)) {
			printk(" [<%08lx>]", addr);
			print_symbol(" %s", addr);
			printk("\n");
                }
        }
        printk("\n");
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	unsigned long stack;

	show_trace(&stack);
}
EXPORT_SYMBOL(dump_stack);

void show_stack(struct task_struct *task, unsigned long *sp)
{
	show_trace(sp);
}
