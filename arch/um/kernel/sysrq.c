/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/kernel.h"
#include "linux/module.h"
#include "asm/page.h"
#include "asm/processor.h"
#include "sysrq.h"
#include "user_util.h"

void show_trace(unsigned long * stack)
{
        int i;
        unsigned long addr;

        if (!stack)
                stack = (unsigned long*) &stack;

        printk("Call Trace: ");
        i = 1;
        while (((long) stack & (THREAD_SIZE-1)) != 0) {
                addr = *stack++;
		if (kernel_text_address(addr)) {
			if (i && ((i % 6) == 0))
				printk("\n   ");
			printk("[<%08lx>] ", addr);
			i++;
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

void show_trace_task(struct task_struct *tsk)
{
	unsigned long esp = PT_REGS_SP(&tsk->thread.regs);

	/* User space on another CPU? */
	if ((esp ^ (unsigned long)tsk) & (PAGE_MASK<<1))
		return;
	show_trace((unsigned long *)esp);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
