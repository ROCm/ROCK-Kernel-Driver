/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * Smp support for CHRP machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/residual.h>
#include <asm/feature.h>
#include <asm/time.h>

#include "open_pic.h"

extern unsigned long smp_chrp_cpu_nr;

static int __init
smp_chrp_probe(void)
{
	if (smp_chrp_cpu_nr > 1)
		openpic_request_IPIs();

	return smp_chrp_cpu_nr;
}

static void __init
smp_chrp_kick_cpu(int nr)
{
	*(unsigned long *)KERNELBASE = nr;
	asm volatile("dcbf 0,%0"::"r"(KERNELBASE):"memory");
}

static void __init
smp_chrp_setup_cpu(int cpu_nr)
{
	static atomic_t ready = ATOMIC_INIT(1);
	static volatile int frozen = 0;

	if (cpu_nr == 0) {
		/* wait for all the others */
		while (atomic_read(&ready) < smp_num_cpus)
			barrier();
		atomic_set(&ready, 1);
		/* freeze the timebase */
		call_rtas("freeze-time-base", 0, 1, NULL);
		mb();
		frozen = 1;
		/* XXX assumes this is not a 601 */
		set_tb(0, 0);
		last_jiffy_stamp(0) = 0;
		while (atomic_read(&ready) < smp_num_cpus)
			barrier();
		/* thaw the timebase again */
		call_rtas("thaw-time-base", 0, 1, NULL);
		mb();
		frozen = 0;
		smp_tb_synchronized = 1;
	} else {
		atomic_inc(&ready);
		while (!frozen)
			barrier();
		set_tb(0, 0);
		last_jiffy_stamp(0) = 0;
		mb();
		atomic_inc(&ready);
		while (frozen)
			barrier();
	}

	if (OpenPIC_Addr)
		do_openpic_setup_cpu();
}

#ifdef CONFIG_POWER4
static void __chrp
smp_xics_message_pass(int target, int msg, unsigned long data, int wait)
{
	/* for now, only do reschedule messages
	   since we only have one IPI */
	if (msg != PPC_MSG_RESCHEDULE)
		return;
	for (i = 0; i < smp_num_cpus; ++i) {
		if (target == MSG_ALL || target == i
		    || (target == MSG_ALL_BUT_SELF
			&& i != smp_processor_id()))
			xics_cause_IPI(i);
	}
}

static int __chrp
smp_xics_probe(void)
{
	return smp_chrp_cpu_nr;
}

static void __chrp
smp_xics_setup_cpu(int cpu_nr)
{
	if (cpu_nr > 0)
		xics_setup_cpu();
}
#endif /* CONFIG_POWER4 */

/* CHRP with openpic */
struct smp_ops_t chrp_smp_ops __chrpdata = {
	smp_openpic_message_pass,
	smp_chrp_probe,
	smp_chrp_kick_cpu,
	smp_chrp_setup_cpu,
};

#ifdef CONFIG_POWER4
/* CHRP with new XICS interrupt controller */
struct smp_ops_t xics_smp_ops __chrpdata = {
	smp_xics_message_pass,
	smp_xics_probe,
	smp_chrp_kick_cpu,
	smp_xics_setup_cpu,
};
#endif /* CONFIG_POWER4 */
