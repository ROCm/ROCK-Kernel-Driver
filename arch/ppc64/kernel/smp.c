/*
 * SMP support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 * PowerPC-64 Support added by Dave Engebretsen, Peter Bergner, and
 * Mike Corrigan {engebret|bergner|mikec}@us.ibm.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/naca.h>
#include <asm/paca.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCall.h>
#include <asm/iSeries/HvCallCfg.h>
#include <asm/time.h>
#include <asm/ppcdebug.h>
#include "open_pic.h"
#include <asm/machdep.h>
#include <asm/xics.h>
#include <asm/cputable.h>
#include <asm/system.h>

int smp_threads_ready;
unsigned long cache_decay_ticks;

cpumask_t cpu_possible_map = CPU_MASK_NONE;
cpumask_t cpu_online_map = CPU_MASK_NONE;
cpumask_t cpu_available_map = CPU_MASK_NONE;
cpumask_t cpu_present_at_boot = CPU_MASK_NONE;

EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(cpu_possible_map);

struct smp_ops_t *smp_ops;

static volatile unsigned int cpu_callin_map[NR_CPUS];

extern unsigned char stab_array[];

extern int cpu_idle(void *unused);
void smp_call_function_interrupt(void);
extern long register_vpa(unsigned long flags, unsigned long proc,
			 unsigned long vpa);

/* Low level assembly function used to backup CPU 0 state */
extern void __save_cpu_setup(void);

#ifdef CONFIG_PPC_ISERIES
static unsigned long iSeries_smp_message[NR_CPUS];

void iSeries_smp_message_recv( struct pt_regs * regs )
{
	int cpu = smp_processor_id();
	int msg;

	if ( num_online_cpus() < 2 )
		return;

	for ( msg = 0; msg < 4; ++msg )
		if ( test_and_clear_bit( msg, &iSeries_smp_message[cpu] ) )
			smp_message_recv( msg, regs );
}

static inline void smp_iSeries_do_message(int cpu, int msg)
{
	set_bit(msg, &iSeries_smp_message[cpu]);
	HvCall_sendIPI(&(paca[cpu]));
}

static void smp_iSeries_message_pass(int target, int msg)
{
	int i;

	if (target < NR_CPUS)
		smp_iSeries_do_message(target, msg);
	else {
		for_each_online_cpu(i) {
			if (target == MSG_ALL_BUT_SELF
			    && i == smp_processor_id())
				continue;
			smp_iSeries_do_message(i, msg);
		}
	}
}

static int smp_iSeries_numProcs(void)
{
	unsigned np, i;
	struct ItLpPaca * lpPaca;

	np = 0;
        for (i=0; i < NR_CPUS; ++i) {
                lpPaca = paca[i].xLpPacaPtr;
                if ( lpPaca->xDynProcStatus < 2 ) {
			cpu_set(i, cpu_available_map);
			cpu_set(i, cpu_possible_map);
			cpu_set(i, cpu_present_at_boot);
                        ++np;
                }
        }
	return np;
}

static int smp_iSeries_probe(void)
{
	unsigned i;
	unsigned np = 0;
	struct ItLpPaca *lpPaca;

	for (i=0; i < NR_CPUS; ++i) {
		lpPaca = paca[i].xLpPacaPtr;
		if (lpPaca->xDynProcStatus < 2) {
			/*paca[i].active = 1;*/
			++np;
		}
	}

	return np;
}

static void smp_iSeries_kick_cpu(int nr)
{
	struct ItLpPaca *lpPaca;

	BUG_ON(nr < 0 || nr >= NR_CPUS);

	/* Verify that our partition has a processor nr */
	lpPaca = paca[nr].xLpPacaPtr;
	if (lpPaca->xDynProcStatus >= 2)
		return;

	/* The processor is currently spinning, waiting
	 * for the xProcStart field to become non-zero
	 * After we set xProcStart, the processor will
	 * continue on to secondary_start in iSeries_head.S
	 */
	paca[nr].xProcStart = 1;
}

static void __devinit smp_iSeries_setup_cpu(int nr)
{
}

static struct smp_ops_t iSeries_smp_ops = {
	.message_pass = smp_iSeries_message_pass,
	.probe        = smp_iSeries_probe,
	.kick_cpu     = smp_iSeries_kick_cpu,
	.setup_cpu    = smp_iSeries_setup_cpu,
};

/* This is called very early. */
void __init smp_init_iSeries(void)
{
	smp_ops = &iSeries_smp_ops;
	systemcfg->processorCount	= smp_iSeries_numProcs();
}
#endif

#ifdef CONFIG_PPC_PSERIES
void smp_openpic_message_pass(int target, int msg)
{
	/* make sure we're sending something that translates to an IPI */
	if ( msg > 0x3 ){
		printk("SMP %d: smp_message_pass: unknown msg %d\n",
		       smp_processor_id(), msg);
		return;
	}
	switch ( target )
	{
	case MSG_ALL:
		openpic_cause_IPI(msg, 0xffffffff);
		break;
	case MSG_ALL_BUT_SELF:
		openpic_cause_IPI(msg,
				  0xffffffff & ~(1 << smp_processor_id()));
		break;
	default:
		openpic_cause_IPI(msg, 1<<target);
		break;
	}
}

static int __init smp_openpic_probe(void)
{
	int nr_cpus;

	nr_cpus = cpus_weight(cpu_possible_map);

	if (nr_cpus > 1)
		openpic_request_IPIs();

	return nr_cpus;
}

static void __devinit smp_openpic_setup_cpu(int cpu)
{
	do_openpic_setup_cpu();
}

#ifdef CONFIG_HOTPLUG_CPU
/* Get state of physical CPU.
 * Return codes:
 *	0	- The processor is in the RTAS stopped state
 *	1	- stop-self is in progress
 *	2	- The processor is not in the RTAS stopped state
 *	-1	- Hardware Error
 *	-2	- Hardware Busy, Try again later.
 */
static int query_cpu_stopped(unsigned int pcpu)
{
	long cpu_status;
	int status, qcss_tok;

	qcss_tok = rtas_token("query-cpu-stopped-state");
	BUG_ON(qcss_tok == RTAS_UNKNOWN_SERVICE);
	status = rtas_call(qcss_tok, 1, 2, &cpu_status, pcpu);
	if (status != 0) {
		printk(KERN_ERR
		       "RTAS query-cpu-stopped-state failed: %i\n", status);
		return status;
	}

	return cpu_status;
}

int __cpu_disable(void)
{
	/* FIXME: go put this in a header somewhere */
	extern void xics_migrate_irqs_away(void);

	systemcfg->processorCount--;

	/*fix boot_cpuid here*/
	if (smp_processor_id() == boot_cpuid)
		boot_cpuid = any_online_cpu(cpu_online_map);

	/* FIXME: abstract this to not be platform specific later on */
	xics_migrate_irqs_away();
	return 0;
}

void __cpu_die(unsigned int cpu)
{
	int tries;
	int cpu_status;
	unsigned int pcpu = get_hard_smp_processor_id(cpu);

	for (tries = 0; tries < 5; tries++) {
		cpu_status = query_cpu_stopped(pcpu);

		if (cpu_status == 0)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ);
	}
	if (cpu_status != 0) {
		printk("Querying DEAD? cpu %i (%i) shows %i\n",
		       cpu, pcpu, cpu_status);
	}

	/* Isolation and deallocation are definatly done by
	 * drslot_chrp_cpu.  If they were not they would be
	 * done here.  Change isolate state to Isolate and
	 * change allocation-state to Unusable.
	 */
	paca[cpu].xProcStart = 0;

	/* So we can recognize if it fails to come up next time. */
	cpu_callin_map[cpu] = 0;
}

/* Kill this cpu */
void cpu_die(void)
{
	local_irq_disable();
	rtas_stop_self();
	/* Should never get here... */
	BUG();
	for(;;);
}

/* Search all cpu device nodes for an offline logical cpu.  If a
 * device node has a "ibm,my-drc-index" property (meaning this is an
 * LPAR), paranoid-check whether we own the cpu.  For each "thread"
 * of a cpu, if it is offline and has the same hw index as before,
 * grab that in preference.
 */
static unsigned int find_physical_cpu_to_start(unsigned int old_hwindex)
{
	struct device_node *np = NULL;
	unsigned int best = -1U;

	while ((np = of_find_node_by_type(np, "cpu"))) {
		int nr_threads, len;
		u32 *index = (u32 *)get_property(np, "ibm,my-drc-index", NULL);
		u32 *tid = (u32 *)
			get_property(np, "ibm,ppc-interrupt-server#s", &len);

		if (!tid)
			tid = (u32 *)get_property(np, "reg", &len);

		if (!tid)
			continue;

		/* If there is a drc-index, make sure that we own
		 * the cpu.
		 */
		if (index) {
			int state;
			int rc = rtas_get_sensor(9003, *index, &state);
			if (rc != 0 || state != 1)
				continue;
		}

		nr_threads = len / sizeof(u32);

		while (nr_threads--) {
			if (0 == query_cpu_stopped(tid[nr_threads])) {
				best = tid[nr_threads];
				if (best == old_hwindex)
					goto out;
			}
		}
	}
out:
	of_node_put(np);
	return best;
}

/**
 * smp_startup_cpu() - start the given cpu
 *
 * At boot time, there is nothing to do.  At run-time, call RTAS with
 * the appropriate start location, if the cpu is in the RTAS stopped
 * state.
 *
 * Returns:
 *	0	- failure
 *	1	- success
 */
static inline int __devinit smp_startup_cpu(unsigned int lcpu)
{
	int status;
	extern void (*pseries_secondary_smp_init)(unsigned int cpu);
	unsigned long start_here = __pa(pseries_secondary_smp_init);
	unsigned int pcpu;

	/* At boot time the cpus are already spinning in hold
	 * loops, so nothing to do. */
 	if (system_state == SYSTEM_BOOTING)
		return 1;

	pcpu = find_physical_cpu_to_start(get_hard_smp_processor_id(lcpu));
	if (pcpu == -1U) {
		printk(KERN_INFO "No more cpus available, failing\n");
		return 0;
	}

	/* Fixup atomic count: it exited inside IRQ handler. */
	((struct task_struct *)paca[lcpu].xCurrent)->thread_info->preempt_count
		= 0;
	/* Fixup SLB round-robin so next segment (kernel) goes in segment 0 */
	paca[lcpu].xStab_data.next_round_robin = 0;

	/* At boot this is done in prom.c. */
	paca[lcpu].xHwProcNum = pcpu;

	status = rtas_call(rtas_token("start-cpu"), 3, 1, NULL,
			   pcpu, start_here, lcpu);
	if (status != 0) {
		printk(KERN_ERR "start-cpu failed: %i\n", status);
		return 0;
	}
	return 1;
}

static inline void look_for_more_cpus(void)
{
	int num_addr_cell, num_size_cell, len, i, maxcpus;
	struct device_node *np;
	unsigned int *ireg;

	/* Find the property which will tell us about how many CPUs
	 * we're allowed to have. */
	if ((np = find_path_device("/rtas")) == NULL) {
		printk(KERN_ERR "Could not find /rtas in device tree!");
		return;
	}
	num_addr_cell = prom_n_addr_cells(np);
	num_size_cell = prom_n_size_cells(np);

	ireg = (unsigned int *)get_property(np, "ibm,lrdr-capacity", &len);
	if (ireg == NULL) {
		/* FIXME: make sure not marked as lrdr_capable() */
		return;
	}

	maxcpus = ireg[num_addr_cell + num_size_cell];
	/* DRENG need to account for threads here too */

	if (maxcpus > NR_CPUS) {
		printk(KERN_WARNING
		       "Partition configured for %d cpus, "
		       "operating system maximum is %d.\n", maxcpus, NR_CPUS);
		maxcpus = NR_CPUS;
	} else
		printk(KERN_INFO "Partition configured for %d cpus.\n",
		       maxcpus);

	/* Make those cpus (which might appear later) possible too. */
	for (i = 0; i < maxcpus; i++)
		cpu_set(i, cpu_possible_map);
}
#else /* ... CONFIG_HOTPLUG_CPU */
static inline int __devinit smp_startup_cpu(unsigned int lcpu)
{
	return 1;
}
static inline void look_for_more_cpus(void)
{
}
#endif /* CONFIG_HOTPLUG_CPU */

static void smp_pSeries_kick_cpu(int nr)
{
	BUG_ON(nr < 0 || nr >= NR_CPUS);

	if (!smp_startup_cpu(nr))
		return;

	/* The processor is currently spinning, waiting
	 * for the xProcStart field to become non-zero
	 * After we set xProcStart, the processor will
	 * continue on to secondary_start
	 */
	paca[nr].xProcStart = 1;
}
#endif /* CONFIG_PPC_PSERIES */

static void __init smp_space_timers(unsigned int max_cpus)
{
	int i;
	unsigned long offset = tb_ticks_per_jiffy / max_cpus;
	unsigned long previous_tb = paca[boot_cpuid].next_jiffy_update_tb;

	for_each_cpu(i) {
		if (i != boot_cpuid) {
			paca[i].next_jiffy_update_tb =
				previous_tb + offset;
			previous_tb = paca[i].next_jiffy_update_tb;
		}
	}
}

#ifdef CONFIG_PPC_PSERIES
void vpa_init(int cpu)
{
	unsigned long flags;

	/* Register the Virtual Processor Area (VPA) */
	printk(KERN_INFO "register_vpa: cpu 0x%x\n", cpu);
	flags = 1UL << (63 - 18);
	paca[cpu].xLpPaca.xSLBCount = 64; /* SLB restore highwater mark */
	register_vpa(flags, cpu, __pa((unsigned long)&(paca[cpu].xLpPaca))); 
}

static inline void smp_xics_do_message(int cpu, int msg)
{
	set_bit(msg, &xics_ipi_message[cpu].value);
	mb();
	xics_cause_IPI(cpu);
}

static void smp_xics_message_pass(int target, int msg)
{
	unsigned int i;

	if (target < NR_CPUS) {
		smp_xics_do_message(target, msg);
	} else {
		for_each_online_cpu(i) {
			if (target == MSG_ALL_BUT_SELF
			    && i == smp_processor_id())
				continue;
			smp_xics_do_message(i, msg);
		}
	}
}

extern void xics_request_IPIs(void);

static int __init smp_xics_probe(void)
{
#ifdef CONFIG_SMP
	xics_request_IPIs();
#endif

	return cpus_weight(cpu_possible_map);
}

static void __devinit smp_xics_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xics_setup_cpu();
}

static spinlock_t timebase_lock = SPIN_LOCK_UNLOCKED;
static unsigned long timebase = 0;

static void __devinit pSeries_give_timebase(void)
{
	spin_lock(&timebase_lock);
	rtas_call(rtas_token("freeze-time-base"), 0, 1, NULL);
	timebase = get_tb();
	spin_unlock(&timebase_lock);

	while (timebase)
		barrier();
	rtas_call(rtas_token("thaw-time-base"), 0, 1, NULL);
}

static void __devinit pSeries_take_timebase(void)
{
	while (!timebase)
		barrier();
	spin_lock(&timebase_lock);
	set_tb(timebase >> 32, timebase & 0xffffffff);
	timebase = 0;
	spin_unlock(&timebase_lock);
}

static struct smp_ops_t pSeries_openpic_smp_ops = {
	.message_pass	= smp_openpic_message_pass,
	.probe		= smp_openpic_probe,
	.kick_cpu	= smp_pSeries_kick_cpu,
	.setup_cpu	= smp_openpic_setup_cpu,
};

static struct smp_ops_t pSeries_xics_smp_ops = {
	.message_pass	= smp_xics_message_pass,
	.probe		= smp_xics_probe,
	.kick_cpu	= smp_pSeries_kick_cpu,
	.setup_cpu	= smp_xics_setup_cpu,
};

/* This is called very early */
void __init smp_init_pSeries(void)
{

	if (naca->interrupt_controller == IC_OPEN_PIC)
		smp_ops = &pSeries_openpic_smp_ops;
	else
		smp_ops = &pSeries_xics_smp_ops;

	/* Non-lpar has additional take/give timebase */
	if (systemcfg->platform == PLATFORM_PSERIES) {
		smp_ops->give_timebase = pSeries_give_timebase;
		smp_ops->take_timebase = pSeries_take_timebase;
	}
}
#endif

void smp_local_timer_interrupt(struct pt_regs * regs)
{
	if (!--(get_paca()->prof_counter)) {
		update_process_times(user_mode(regs));
		(get_paca()->prof_counter)=get_paca()->prof_multiplier;
	}
}

void smp_message_recv(int msg, struct pt_regs *regs)
{
	switch(msg) {
	case PPC_MSG_CALL_FUNCTION:
		smp_call_function_interrupt();
		break;
	case PPC_MSG_RESCHEDULE: 
		/* XXX Do we have to do this? */
		set_need_resched();
		break;
#if 0
	case PPC_MSG_MIGRATE_TASK:
		/* spare */
		break;
#endif
#ifdef CONFIG_DEBUGGER
	case PPC_MSG_DEBUGGER_BREAK:
		debugger_ipi(regs);
		break;
#endif
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
}

void smp_send_reschedule(int cpu)
{
	smp_ops->message_pass(cpu, PPC_MSG_RESCHEDULE);
}

#ifdef CONFIG_DEBUGGER
void smp_send_debugger_break(int cpu)
{
	smp_ops->message_pass(cpu, PPC_MSG_DEBUGGER_BREAK);
}
#endif

static void stop_this_cpu(void *dummy)
{
	local_irq_disable();
	while (1)
		;
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 * Stolen from the i386 version.
 */
static spinlock_t call_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;

static struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
} *call_data;

/* delay of at least 8 seconds on 1GHz cpu */
#define SMP_CALL_TIMEOUT (1UL << (30 + 3))

/*
 * This function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 *
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> currently unused.
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
		       int wait)
{ 
	struct call_data_struct data;
	int ret = -1, cpus;
	unsigned long timeout;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock(&call_lock);
	/* Must grab online cpu count with preempt disabled, otherwise
	 * it can change. */
	cpus = num_online_cpus() - 1;
	if (!cpus) {
		ret = 0;
		goto out;
	}

	call_data = &data;
	wmb();
	/* Send a message to all other CPUs and wait for them to respond */
	smp_ops->message_pass(MSG_ALL_BUT_SELF, PPC_MSG_CALL_FUNCTION);

	/* Wait for response */
	timeout = SMP_CALL_TIMEOUT;
	while (atomic_read(&data.started) != cpus) {
		HMT_low();
		if (--timeout == 0) {
			printk("smp_call_function on cpu %d: other cpus not "
			       "responding (%d)\n", smp_processor_id(),
			       atomic_read(&data.started));
			debugger(0);
			goto out;
		}
	}

	if (wait) {
		timeout = SMP_CALL_TIMEOUT;
		while (atomic_read(&data.finished) != cpus) {
			HMT_low();
			if (--timeout == 0) {
				printk("smp_call_function on cpu %d: other "
				       "cpus not finishing (%d/%d)\n",
				       smp_processor_id(),
				       atomic_read(&data.finished),
				       atomic_read(&data.started));
				debugger(0);
				goto out;
			}
		}
	}

	ret = 0;

out:
	call_data = NULL;
	HMT_medium();
	spin_unlock(&call_lock);
	return ret;
}

void smp_call_function_interrupt(void)
{
	void (*func) (void *info);
	void *info;
	int wait;

	/* call_data will be NULL if the sender timed out while
	 * waiting on us to receive the call.
	 */
	if (!call_data)
		return;

	func = call_data->func;
	info = call_data->info;
	wait = call_data->wait;

	if (!wait)
		smp_mb__before_atomic_inc();

	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait) {
		smp_mb__before_atomic_inc();
		atomic_inc(&call_data->finished);
	}
}

extern unsigned long decr_overclock;
extern struct gettimeofday_struct do_gtod;

struct thread_info *current_set[NR_CPUS];

DECLARE_PER_CPU(unsigned int, pvr);

static void __devinit smp_store_cpu_info(int id)
{
	per_cpu(pvr, id) = _get_PVR();
}

static void __init smp_create_idle(unsigned int cpu)
{
	struct pt_regs regs;
	struct task_struct *p;

	/* create a process for the processor */
	/* only regs.msr is actually used, and 0 is OK for it */
	memset(&regs, 0, sizeof(struct pt_regs));
	p = copy_process(CLONE_VM | CLONE_IDLETASK,
			 0, &regs, 0, NULL, NULL);
	if (IS_ERR(p))
		panic("failed fork for CPU %u: %li", cpu, PTR_ERR(p));

	wake_up_forked_process(p);
	init_idle(p, cpu);
	unhash_process(p);

	paca[cpu].xCurrent = (u64)p;
	current_set[cpu] = p->thread_info;
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int cpu;

	/* 
	 * setup_cpu may need to be called on the boot cpu. We havent
	 * spun any cpus up but lets be paranoid.
	 */
	BUG_ON(boot_cpuid != smp_processor_id());

	/* Fixup boot cpu */
	smp_store_cpu_info(boot_cpuid);
	cpu_callin_map[boot_cpuid] = 1;
	paca[boot_cpuid].prof_counter = 1;
	paca[boot_cpuid].prof_multiplier = 1;

#ifndef CONFIG_PPC_ISERIES
	paca[boot_cpuid].next_jiffy_update_tb = tb_last_stamp = get_tb();

	/*
	 * Should update do_gtod.stamp_xsec.
	 * For now we leave it which means the time can be some
	 * number of msecs off until someone does a settimeofday()
	 */
	do_gtod.tb_orig_stamp = tb_last_stamp;

	look_for_more_cpus();
#endif

	max_cpus = smp_ops->probe();
 
	/* Backup CPU 0 state if necessary */
	__save_cpu_setup();

	smp_space_timers(max_cpus);

	for_each_cpu(cpu)
		if (cpu != boot_cpuid)
			smp_create_idle(cpu);
}

void __devinit smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != boot_cpuid);

	/* cpu_possible is set up in prom.c */
	cpu_set(boot_cpuid, cpu_online_map);

	paca[boot_cpuid].xCurrent = (u64)current;
	current_set[boot_cpuid] = current->thread_info;
}

int __devinit __cpu_up(unsigned int cpu)
{
	int c;

	/* At boot, don't bother with non-present cpus -JSCHOPP */
	if (system_state == SYSTEM_BOOTING && !cpu_present_at_boot(cpu))
		return -ENOENT;

	paca[cpu].prof_counter = 1;
	paca[cpu].prof_multiplier = 1;
	paca[cpu].default_decr = tb_ticks_per_jiffy / decr_overclock;

	if (!(cur_cpu_spec->cpu_features & CPU_FTR_SLB)) {
		void *tmp;

		/* maximum of 48 CPUs on machines with a segment table */
		if (cpu >= 48)
			BUG();

		tmp = &stab_array[PAGE_SIZE * cpu];
		memset(tmp, 0, PAGE_SIZE); 
		paca[cpu].xStab_data.virt = (unsigned long)tmp;
		paca[cpu].xStab_data.real = virt_to_abs(tmp);
	}

	/* The information for processor bringup must
	 * be written out to main store before we release
	 * the processor.
	 */
	mb();

	/* wake up cpus */
	smp_ops->kick_cpu(cpu);

	/*
	 * wait to see if the cpu made a callin (is actually up).
	 * use this value that I found through experimentation.
	 * -- Cort
	 */
	if (system_state == SYSTEM_BOOTING)
		for (c = 5000; c && !cpu_callin_map[cpu]; c--)
			udelay(100);
#ifdef CONFIG_HOTPLUG_CPU
	else
		/*
		 * CPUs can take much longer to come up in the
		 * hotplug case.  Wait five seconds.
		 */
		for (c = 25; c && !cpu_callin_map[cpu]; c--) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/5);
		}
#endif

	if (!cpu_callin_map[cpu]) {
		printk("Processor %u is stuck.\n", cpu);
		return -ENOENT;
	}

	printk("Processor %u found.\n", cpu);

	if (smp_ops->give_timebase)
		smp_ops->give_timebase();
	cpu_set(cpu, cpu_online_map);
	return 0;
}

extern unsigned int default_distrib_server;
/* Activate a secondary processor. */
int __devinit start_secondary(void *unused)
{
	unsigned int cpu = smp_processor_id();

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	smp_store_cpu_info(cpu);
	set_dec(paca[cpu].default_decr);
	cpu_callin_map[cpu] = 1;

	smp_ops->setup_cpu(cpu);
	if (smp_ops->take_timebase)
		smp_ops->take_timebase();

	get_paca()->yielded = 0;

#ifdef CONFIG_PPC_PSERIES
	if (cur_cpu_spec->firmware_features & FW_FEATURE_SPLPAR) {
		vpa_init(cpu); 
	}

#ifdef CONFIG_IRQ_ALL_CPUS
	/* Put the calling processor into the GIQ.  This is really only
	 * necessary from a secondary thread as the OF start-cpu interface
	 * performs this function for us on primary threads.
	 */
	/* TODO: 9005 is #defined in rtas-proc.c -- move to a header */
	rtas_set_indicator(9005, default_distrib_server, 1);
#endif
#endif

	local_irq_enable();

	return cpu_idle(NULL);
}

int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	cpumask_t old_mask;

	/* We want the setup_cpu() here to be called from CPU 0, but our
	 * init thread may have been "borrowed" by another CPU in the meantime
	 * se we pin us down to CPU 0 for a short while
	 */
	old_mask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(boot_cpuid));
	
	smp_ops->setup_cpu(boot_cpuid);

	/* XXX fix this, xics currently relies on it - Anton */
	smp_threads_ready = 1;

	set_cpus_allowed(current, old_mask);
}

#ifdef CONFIG_SCHED_SMT
#ifdef CONFIG_NUMA
static struct sched_group sched_group_cpus[NR_CPUS];
static struct sched_group sched_group_phys[NR_CPUS];
static struct sched_group sched_group_nodes[MAX_NUMNODES];
static DEFINE_PER_CPU(struct sched_domain, cpu_domains);
static DEFINE_PER_CPU(struct sched_domain, phys_domains);
static DEFINE_PER_CPU(struct sched_domain, node_domains);
__init void arch_init_sched_domains(void)
{
	int i;
	struct sched_group *first = NULL, *last = NULL;

	/* Set up domains */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		struct sched_domain *phys_domain = &per_cpu(phys_domains, i);
		struct sched_domain *node_domain = &per_cpu(node_domains, i);
		int node = cpu_to_node(i);
		cpumask_t nodemask = node_to_cpumask(node);
		cpumask_t my_cpumask = cpumask_of_cpu(i);
		cpumask_t sibling_cpumask = cpumask_of_cpu(i ^ 0x1);

		*cpu_domain = SD_SIBLING_INIT;
		if (cur_cpu_spec->cpu_features & CPU_FTR_SMT)
			cpus_or(cpu_domain->span, my_cpumask, sibling_cpumask);
		else
			cpu_domain->span = my_cpumask;
		cpu_domain->parent = phys_domain;
		cpu_domain->groups = &sched_group_cpus[i];

		*phys_domain = SD_CPU_INIT;
		phys_domain->span = nodemask;
		phys_domain->parent = node_domain;
		phys_domain->groups = &sched_group_phys[first_cpu(cpu_domain->span)];

		*node_domain = SD_NODE_INIT;
		node_domain->span = cpu_possible_map;
		node_domain->groups = &sched_group_nodes[node];
	}

	/* Set up CPU (sibling) groups */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		int j;
		first = last = NULL;

		if (i != first_cpu(cpu_domain->span))
			continue;

		for_each_cpu_mask(j, cpu_domain->span) {
			struct sched_group *cpu = &sched_group_cpus[j];

			cpus_clear(cpu->cpumask);
			cpu_set(j, cpu->cpumask);
			cpu->cpu_power = SCHED_LOAD_SCALE;

			if (!first)
				first = cpu;
			if (last)
				last->next = cpu;
			last = cpu;
		}
		last->next = first;
	}

	for (i = 0; i < MAX_NUMNODES; i++) {
		int j;
		cpumask_t nodemask;
		struct sched_group *node = &sched_group_nodes[i];
		cpumask_t node_cpumask = node_to_cpumask(i);
		cpus_and(nodemask, node_cpumask, cpu_possible_map);

		if (cpus_empty(nodemask))
			continue;

		first = last = NULL;
		/* Set up physical groups */
		for_each_cpu_mask(j, nodemask) {
			struct sched_domain *cpu_domain = &per_cpu(cpu_domains, j);
			struct sched_group *cpu = &sched_group_phys[j];

			if (j != first_cpu(cpu_domain->span))
				continue;

			cpu->cpumask = cpu_domain->span;
			/*
			 * Make each extra sibling increase power by 10% of
			 * the basic CPU. This is very arbitrary.
			 */
			cpu->cpu_power = SCHED_LOAD_SCALE + SCHED_LOAD_SCALE*(cpus_weight(cpu->cpumask)-1) / 10;
			node->cpu_power += cpu->cpu_power;

			if (!first)
				first = cpu;
			if (last)
				last->next = cpu;
			last = cpu;
		}
		last->next = first;
	}

	/* Set up nodes */
	first = last = NULL;
	for (i = 0; i < MAX_NUMNODES; i++) {
		struct sched_group *cpu = &sched_group_nodes[i];
		cpumask_t nodemask;
		cpumask_t node_cpumask = node_to_cpumask(i);
		cpus_and(nodemask, node_cpumask, cpu_possible_map);

		if (cpus_empty(nodemask))
			continue;

		cpu->cpumask = nodemask;
		/* ->cpu_power already setup */

		if (!first)
			first = cpu;
		if (last)
			last->next = cpu;
		last = cpu;
	}
	last->next = first;

	mb();
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		cpu_attach_domain(cpu_domain, i);
	}
}
#else /* !CONFIG_NUMA */
static struct sched_group sched_group_cpus[NR_CPUS];
static struct sched_group sched_group_phys[NR_CPUS];
static DEFINE_PER_CPU(struct sched_domain, cpu_domains);
static DEFINE_PER_CPU(struct sched_domain, phys_domains);
__init void arch_init_sched_domains(void)
{
	int i;
	struct sched_group *first = NULL, *last = NULL;

	/* Set up domains */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		struct sched_domain *phys_domain = &per_cpu(phys_domains, i);
		cpumask_t my_cpumask = cpumask_of_cpu(i);
		cpumask_t sibling_cpumask = cpumask_of_cpu(i ^ 0x1);

		*cpu_domain = SD_SIBLING_INIT;
		if (cur_cpu_spec->cpu_features & CPU_FTR_SMT)
			cpus_or(cpu_domain->span, my_cpumask, sibling_cpumask);
		else
			cpu_domain->span = my_cpumask;
		cpu_domain->parent = phys_domain;
		cpu_domain->groups = &sched_group_cpus[i];

		*phys_domain = SD_CPU_INIT;
		phys_domain->span = cpu_possible_map;
		phys_domain->groups = &sched_group_phys[first_cpu(cpu_domain->span)];
	}

	/* Set up CPU (sibling) groups */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		int j;
		first = last = NULL;

		if (i != first_cpu(cpu_domain->span))
			continue;

		for_each_cpu_mask(j, cpu_domain->span) {
			struct sched_group *cpu = &sched_group_cpus[j];

			cpus_clear(cpu->cpumask);
			cpu_set(j, cpu->cpumask);
			cpu->cpu_power = SCHED_LOAD_SCALE;

			if (!first)
				first = cpu;
			if (last)
				last->next = cpu;
			last = cpu;
		}
		last->next = first;
	}

	first = last = NULL;
	/* Set up physical groups */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		struct sched_group *cpu = &sched_group_phys[i];

		if (i != first_cpu(cpu_domain->span))
			continue;

		cpu->cpumask = cpu_domain->span;
		/* See SMT+NUMA setup for comment */
		cpu->cpu_power = SCHED_LOAD_SCALE + SCHED_LOAD_SCALE*(cpus_weight(cpu->cpumask)-1) / 10;

		if (!first)
			first = cpu;
		if (last)
			last->next = cpu;
		last = cpu;
	}
	last->next = first;

	mb();
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		cpu_attach_domain(cpu_domain, i);
	}
}
#endif /* CONFIG_NUMA */
#endif /* CONFIG_SCHED_SMT */
