/*
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu).  Subsequently hacked
 * on by Tom Rini, Armin Kuster, Paul Mackerras and others.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/machdep.h>

void default_idle(void)
{
	void (*powersave)(void);

	powersave = ppc_md.power_save;

	if (!need_resched()) {
		if (powersave != NULL)
			powersave();
#ifdef CONFIG_SMP
		else {
			set_thread_flag(TIF_POLLING_NRFLAG);
			while (!need_resched())
				barrier();
			clear_thread_flag(TIF_POLLING_NRFLAG);
		}
#endif
	}
	if (need_resched())
		schedule();
}

/*
 * The body of the idle task.
 */
int cpu_idle(void)
{
	for (;;)
		if (ppc_md.idle != NULL)
			ppc_md.idle();
		else
			default_idle();
	return 0;
}
