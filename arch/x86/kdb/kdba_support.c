/*
 * Kernel Debugger Architecture Independent Support Functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/hw_irq.h>
#include <asm/desc.h>

#ifdef CONFIG_KDB_KDUMP
void kdba_kdump_prepare(struct pt_regs *regs)
{
	int i;
	struct pt_regs r;
	if (regs == NULL)
		regs = &r;

	for (i = 1; i < NR_CPUS; ++i) {
		if (!cpu_online(i))
			continue;

		KDB_STATE_SET_CPU(KEXEC, i);
	}

	machine_crash_shutdown(regs);
}

extern void halt_current_cpu(struct pt_regs *);

void kdba_kdump_shutdown_slave(struct pt_regs *regs)
{
#ifndef CONFIG_XEN
	halt_current_cpu(regs);
#endif /* CONFIG_XEN */
}

#endif
