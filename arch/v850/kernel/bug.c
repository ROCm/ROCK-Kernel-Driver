/*
 * arch/v850/kernel/bug.c -- Bug reporting functions
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sched.h>

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/current.h>

/* We should use __builtin_return_address, but it doesn't work in gcc-2.90
   (which is currently our standard compiler on the v850).  */
#define ret_addr() ({ register u32 lp asm ("lp"); lp; })
#define stack_addr() ({ register u32 sp asm ("sp"); sp; })

void __bug ()
{
	printk (KERN_CRIT "kernel BUG at PC 0x%x (SP ~0x%x)!\n",
		ret_addr() - 4, /* - 4 for `jarl' */
		stack_addr());
	machine_halt ();
}

int bad_trap (int trap_num, struct pt_regs *regs)
{
	printk (KERN_CRIT
		"unimplemented trap %d called at 0x%08lx, pid %d!\n",
		trap_num, regs->pc, current->pid);
	return -ENOSYS;
}

int debug_trap (struct pt_regs *regs)
{
	printk (KERN_CRIT "debug trap at 0x%08lx!\n", regs->pc);
	return -ENOSYS;
}

#ifdef CONFIG_RESET_GUARD
void unexpected_reset (unsigned long ret_addr, unsigned long kmode,
		       struct task_struct *task, unsigned long sp)
{
	printk (KERN_CRIT
		"unexpected reset in %s mode, pid %d"
		" (ret_addr = 0x%lx, sp = 0x%lx)\n",
		kmode ? "kernel" : "user",
		task ? task->pid : -1,
		ret_addr, sp);

	machine_halt ();
}
#endif /* CONFIG_RESET_GUARD */
