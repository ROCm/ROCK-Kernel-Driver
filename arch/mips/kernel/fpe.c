/*
 * The real floating point exception handler.  While it doesn't really
 * make sense to have this in a module, it makes debugging of this code
 * in the kernel space a lot easier.  So far this handler in the released
 * kernel source is just a dummy.
 *
 * Copyright (C) 1997, 2000 Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/branch.h>
#include <asm/ptrace.h>

MODULE_AUTHOR("Ralf Baechle <ralf@gnu.org>");
MODULE_DESCRIPTION("Experimental floating point exception handler");
MODULE_SUPPORTED_DEVICE("MIPS FPU");

static void do_fpe(struct pt_regs *regs, unsigned int fcr31)
{
#ifdef CONF_DEBUG_EXCEPTIONS
	show_regs(regs);
#endif
	printk("Caught floating exception at epc == %08lx, fcr31 == %08x\n",
	       regs->cp0_epc, fcr31);
	if (compute_return_epc(regs))
		goto out;
	force_sig(SIGFPE, current);
out:
}

/*
 * For easier experimentation we never increment/decrement
 * the module usable counter.
 */
int register_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31));
int unregister_fpe(void (*handler)(struct pt_regs *regs, unsigned int fcr31));

int init_module(void)
{
	return register_fpe(do_fpe);
}

void cleanup_module(void)
{
	unregister_fpe(do_fpe);
}
