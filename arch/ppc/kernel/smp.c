/*
 * $Id: smp.c,v 1.68 1999/09/17 19:38:05 cort Exp $
 *
 * Smp support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 * Support for PReP (Motorola MTX/MVME) SMP by Troy Benjegerdes
 * (troy@microux.com, hozer@drgw.net)
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
#include <linux/openpic.h>
#include <linux/spinlock.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/gemini.h>

#include <asm/time.h>
#include "open_pic.h"
int smp_threads_ready;
volatile int smp_commenced;
int smp_num_cpus = 1;
struct cpuinfo_PPC cpu_data[NR_CPUS];
struct klock_info_struct klock_info = { KLOCK_CLEAR, 0 };
volatile unsigned char active_kernel_processor = NO_PROC_ID;	/* Processor holding kernel spinlock		*/
volatile unsigned long ipi_count;
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;
unsigned int prof_multiplier[NR_CPUS];
unsigned int prof_counter[NR_CPUS];
cycles_t cacheflush_time;

/* this has to go in the data section because it is accessed from prom_init */
int smp_hw_index[NR_CPUS];

/* all cpu mappings are 1-1 -- Cort */
volatile unsigned long cpu_callin_map[NR_CPUS];

int start_secondary(void *);
extern int cpu_idle(void *unused);
u_int openpic_read(volatile u_int *addr);
void smp_call_function_interrupt(void);
void smp_message_pass(int target, int msg, unsigned long data, int wait);

/* register for interrupting the primary processor on the powersurge */
/* N.B. this is actually the ethernet ROM! */
#define PSURGE_PRI_INTR	0xf3019000
/* register for interrupting the secondary processor on the powersurge */
#define PSURGE_SEC_INTR	0xf80000c0
/* register for storing the start address for the secondary processor */
#define PSURGE_START	0xf2800000
/* virtual addresses for the above */
volatile u32 *psurge_pri_intr;
volatile u32 *psurge_sec_intr;
volatile u32 *psurge_start;

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers. */
#define PPC_MSG_CALL_FUNCTION	0
#define PPC_MSG_RESCHEDULE	1
#define PPC_MSG_INVALIDATE_TLB	2
#define PPC_MSG_XMON_BREAK	3

static inline void set_tb(unsigned int upper, unsigned int lower)
{
	mtspr(SPRN_TBWU, upper);
	mtspr(SPRN_TBWL, lower);
}

void smp_local_timer_interrupt(struct pt_regs * regs)
{
	int cpu = smp_processor_id();

	if (!--prof_counter[cpu]) {
		update_process_times(user_mode(regs));
		prof_counter[cpu]=prof_multiplier[cpu];
	}
}

void smp_message_recv(int msg, struct pt_regs *regs)
{
	ipi_count++;
	
	switch( msg ) {
	case PPC_MSG_CALL_FUNCTION:
		smp_call_function_interrupt();
		break;
	case PPC_MSG_RESCHEDULE: 
		current->need_resched = 1;
		break;
	case PPC_MSG_INVALIDATE_TLB:
		_tlbia();
		break;
#ifdef CONFIG_XMON
	case PPC_MSG_XMON_BREAK:
		xmon(regs);
		break;
#endif /* CONFIG_XMON */
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
}

/*
 * As it is now, if we're sending two message at the same time
 * we have race conditions on Pmac.  The PowerSurge doesn't easily
 * allow us to send IPI messages so we put the messages in
 * smp_message[].
 *
 * This is because don't have several IPI's on the PowerSurge even though
 * we do on the chrp.  It would be nice to use actual IPI's such as with
 * openpic rather than this.
 *  -- Cort
 */
int pmac_smp_message[NR_CPUS];
void pmac_smp_message_recv(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int msg;

	/* clear interrupt */
	if (cpu == 1)
		out_be32(psurge_sec_intr, ~0);

	if (smp_num_cpus < 2)
		return;

	/* make sure there is a message there */
	msg = pmac_smp_message[cpu];
	if (msg == 0)
		return;

 	/* reset message */
	pmac_smp_message[cpu] = 0;

	smp_message_recv(msg - 1, regs);
}

void
pmac_primary_intr(int irq, void *d, struct pt_regs *regs)
{
	pmac_smp_message_recv(regs);
}

/*
 * 750's don't broadcast tlb invalidates so
 * we have to emulate that behavior.
 *   -- Cort
 */
void smp_send_tlb_invalidate(int cpu)
{
	if ( (_get_PVR()>>16) == 8 )
		smp_message_pass(MSG_ALL_BUT_SELF, PPC_MSG_INVALIDATE_TLB, 0, 0);
}

void smp_send_reschedule(int cpu)
{
	/*
	 * This is only used if `cpu' is running an idle task,
	 * so it will reschedule itself anyway...
	 *
	 * This isn't the case anymore since the other CPU could be
	 * sleeping and won't reschedule until the next interrupt (such
	 * as the timer).
	 *  -- Cort
	 */
	/* This is only used if `cpu' is running an idle task,
	   so it will reschedule itself anyway... */
	smp_message_pass(cpu, PPC_MSG_RESCHEDULE, 0, 0);
}

#ifdef CONFIG_XMON
void smp_send_xmon_break(int cpu)
{
	smp_message_pass(cpu, PPC_MSG_XMON_BREAK, 0, 0);
}
#endif /* CONFIG_XMON */

static void stop_this_cpu(void *dummy)
{
	__cli();
	while (1)
		;
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
	smp_num_cpus = 1;
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 * Stolen from the i386 version.
 */
static spinlock_t call_lock = SPIN_LOCK_UNLOCKED;

static volatile struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
} *call_data;

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
			int wait)
/*
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> currently unused.
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler, you may call it from a bottom half handler.
 */
{
	struct call_data_struct data;
	int ret = -1, cpus = smp_num_cpus-1;
	int timeout;

	if (!cpus)
		return 0;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock_bh(&call_lock);
	call_data = &data;
	/* Send a message to all other CPUs and wait for them to respond */
	smp_message_pass(MSG_ALL_BUT_SELF, PPC_MSG_CALL_FUNCTION, 0, 0);

	/* Wait for response */
	timeout = 1000000;
	while (atomic_read(&data.started) != cpus) {
		if (--timeout == 0) {
			printk("smp_call_function on cpu %d: other cpus not responding (%d)\n",
			       smp_processor_id(), atomic_read(&data.started));
			goto out;
		}
		barrier();
		udelay(1);
	}

	if (wait) {
		timeout = 1000000;
		while (atomic_read(&data.finished) != cpus) {
			if (--timeout == 0) {
				printk("smp_call_function on cpu %d: other cpus not finishing (%d/%d)\n",
				       smp_processor_id(), atomic_read(&data.finished), atomic_read(&data.started));
				goto out;
			}
			barrier();
			udelay(1);
		}
	}
	ret = 0;

 out:
	spin_unlock_bh(&call_lock);
	return ret;
}

void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait)
		atomic_inc(&call_data->finished);
}

void smp_message_pass(int target, int msg, unsigned long data, int wait)
{
	if ( !(_machine & (_MACH_Pmac|_MACH_chrp|_MACH_prep|_MACH_gemini)) )
		return;

	switch (_machine) {
	case _MACH_Pmac:
		/*
		 * IPI's on the Pmac are a hack but without reasonable
		 * IPI hardware SMP on Pmac is a hack.
		 *
		 * We assume here that the msg is not -1.  If it is,
		 * the recipient won't know the message was destined
		 * for it. -- Cort
		 */
		if (smp_processor_id() == 0) {
			/* primary cpu */
			if (target == 1 || target == MSG_ALL_BUT_SELF
			    || target == MSG_ALL) {
				pmac_smp_message[1] = msg + 1;
				/* interrupt secondary processor */
				out_be32(psurge_sec_intr, ~0);
				out_be32(psurge_sec_intr, 0);
			}
		} else {
			/* secondary cpu */
			if (target == 0 || target == MSG_ALL_BUT_SELF
			    || target == MSG_ALL) {
				pmac_smp_message[0] = msg + 1;
				/* interrupt primary processor */
				in_be32(psurge_pri_intr);
			}
		}
		if (target == smp_processor_id() || target == MSG_ALL) {
			/* sending a message to ourself */
			/* XXX maybe we shouldn't do this if ints are off */
			smp_message_recv(msg, NULL);
		}
		break;
	case _MACH_chrp:
	case _MACH_prep:
	case _MACH_gemini:
#ifndef CONFIG_POWER4
		/* make sure we're sending something that translates to an IPI */
		if ( msg > 0x3 )
			break;
		switch ( target )
		{
		case MSG_ALL:
			openpic_cause_IPI(smp_processor_id(), msg, 0xffffffff);
			break;
		case MSG_ALL_BUT_SELF:
			openpic_cause_IPI(smp_processor_id(), msg,
					  0xffffffff & ~(1 << smp_processor_id()));
			break;
		default:
			openpic_cause_IPI(smp_processor_id(), msg, 1<<target);
			break;
		}
#else /* CONFIG_POWER4 */
		/* for now, only do reschedule messages
		   since we only have one IPI */
		if (msg != PPC_MSG_RESCHEDULE)
			break;
		for (i = 0; i < smp_num_cpus; ++i) {
			if (target == MSG_ALL || target == i
			    || (target == MSG_ALL_BUT_SELF
				&& i != smp_processor_id()))
				xics_cause_IPI(i);
		}
#endif /* CONFIG_POWER4 */
		break;
	}
}

void __init smp_boot_cpus(void)
{
	extern struct task_struct *current_set[NR_CPUS];
	extern unsigned long smp_chrp_cpu_nr;
	extern void __secondary_start_psurge(void);
	extern void __secondary_start_chrp(void);
	int i, cpu_nr;
	struct task_struct *p;
	unsigned long a;

	printk("Entering SMP Mode...\n");
	smp_num_cpus = 1;
        smp_store_cpu_info(0);

	/*
	 * assume for now that the first cpu booted is
	 * cpu 0, the master -- Cort
	 */
	cpu_callin_map[0] = 1;
        active_kernel_processor = 0;
	current->processor = 0;

	init_idle();

	for (i = 0; i < NR_CPUS; i++) {
		prof_counter[i] = 1;
		prof_multiplier[i] = 1;
	}

	/*
	 * XXX very rough, assumes 20 bus cycles to read a cache line,
	 * timebase increments every 4 bus cycles, 32kB L1 data cache.
	 */
	cacheflush_time = 5 * 1024;

	if ( !(_machine & (_MACH_Pmac|_MACH_chrp|_MACH_gemini)) )
	{
		printk("SMP not supported on this machine.\n");
		return;
	}
	
	switch ( _machine )
	{
	case _MACH_Pmac:
		/* assume powersurge board - 2 processors -- Cort */
		cpu_nr = 2;
		psurge_pri_intr = ioremap(PSURGE_PRI_INTR, 4);
		psurge_sec_intr = ioremap(PSURGE_SEC_INTR, 4);
		psurge_start = ioremap(PSURGE_START, 4);
		break;
	case _MACH_chrp:
		if (OpenPIC)
			for ( i = 0; i < 4 ; i++ )
				openpic_enable_IPI(i);
		cpu_nr = smp_chrp_cpu_nr;
		break;
	case _MACH_gemini:
		for ( i = 0; i < 4 ; i++ )
			openpic_enable_IPI(i);
                cpu_nr = (readb(GEMINI_CPUSTAT) & GEMINI_CPU_COUNT_MASK)>>2;
                cpu_nr = (cpu_nr == 0) ? 4 : cpu_nr;
		break;
	}

	/*
	 * only check for cpus we know exist.  We keep the callin map
	 * with cpus at the bottom -- Cort
	 */
	for ( i = 1 ; i < cpu_nr; i++ )
	{
		int c;
		struct pt_regs regs;
		
		/* create a process for the processor */
		/* we don't care about the values in regs since we'll
		   never reschedule the forked task. */
		if (do_fork(CLONE_VM|CLONE_PID, 0, &regs, 0) < 0)
			panic("failed fork for CPU %d", i);
		p = init_task.prev_task;
		if (!p)
			panic("No idle task for CPU %d", i);
		del_from_runqueue(p);
		unhash_process(p);
		init_tasks[i] = p;

		p->processor = i;
		p->has_cpu = 1;
		current_set[i] = p;

		/* need to flush here since secondary bats aren't setup */
		for (a = KERNELBASE; a < KERNELBASE + 0x800000; a += 32)
			asm volatile("dcbf 0,%0" : : "r" (a) : "memory");
		asm volatile("sync");

		/* wake up cpus */
		switch ( _machine )
		{
		case _MACH_Pmac:
			/* setup entry point of secondary processor */
			out_be32(psurge_start, __pa(__secondary_start_psurge));
			/* interrupt secondary to begin executing code */
			out_be32(psurge_sec_intr, ~0);
			udelay(1);
			out_be32(psurge_sec_intr, 0);
			break;
		case _MACH_chrp:
			*(unsigned long *)KERNELBASE = i;
			asm volatile("dcbf 0,%0"::"r"(KERNELBASE):"memory");
			break;
		case _MACH_gemini:
			openpic_init_processor( 1<<i );
			openpic_init_processor( 0 );
			break;
		}
		
		/*
		 * wait to see if the cpu made a callin (is actually up).
		 * use this value that I found through experimentation.
		 * -- Cort
		 */
		for ( c = 1000; c && !cpu_callin_map[i] ; c-- )
			udelay(100);
		
		if ( cpu_callin_map[i] )
		{
			printk("Processor %d found.\n", i);
			smp_num_cpus++;
		} else {
			printk("Processor %d is stuck.\n", i);
		}
	}

	if (OpenPIC && (_machine & (_MACH_gemini|_MACH_chrp|_MACH_prep)))
		do_openpic_setup_cpu();

	if ( _machine == _MACH_Pmac )
	{
		/* reset the entry point so if we get another intr we won't
		 * try to startup again */
		out_be32(psurge_start, 0x100);
		if (request_irq(30, pmac_primary_intr, 0, "primary IPI", 0))
			printk(KERN_ERR "Couldn't get primary IPI interrupt");
		/*
		 * The decrementers of both cpus are frozen at this point
		 * until we give the secondary cpu another interrupt.
		 * We set them both to decrementer_count and then send
		 * the interrupt.  This should get the decrementers
		 * synchronized.
		 * -- paulus.
		 */
		set_dec(tb_ticks_per_jiffy);
		if ((_get_PVR() >> 16) != 1) {
			set_tb(0, 0);	/* set timebase if not 601 */
			last_jiffy_stamp(0) = 0;
		}
		out_be32(psurge_sec_intr, ~0);
		udelay(1);
		out_be32(psurge_sec_intr, 0);
	}
}

void __init smp_commence(void)
{
	/*
	 *	Lets the callin's below out of their loop.
	 */
	wmb();
	smp_commenced = 1;
}

/* intel needs this */
void __init initialize_secondary(void)
{
}

/* Activate a secondary processor. */
int __init start_secondary(void *unused)
{
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	smp_callin();
	return cpu_idle(NULL);
}

void __init smp_callin(void)
{
        smp_store_cpu_info(current->processor);
	set_dec(tb_ticks_per_jiffy);
	if (_machine == _MACH_Pmac && (_get_PVR() >> 16) != 1) {
		set_tb(0, 0);	/* set timebase if not 601 */
		last_jiffy_stamp(current->processor) = 0;
	}
	init_idle();
	cpu_callin_map[current->processor] = 1;

#ifndef CONFIG_POWER4
	/*
	 * Each processor has to do this and this is the best
	 * place to stick it for now.
	 *  -- Cort
	 */
	if (OpenPIC && _machine & (_MACH_gemini|_MACH_chrp|_MACH_prep))
		do_openpic_setup_cpu();
#else
	xics_setup_cpu();
#endif /* CONFIG_POWER4 */
#ifdef CONFIG_GEMINI	
	if ( _machine == _MACH_gemini )
		gemini_init_l2();
#endif
	while(!smp_commenced)
		barrier();
	__sti();
}

void __init smp_setup(char *str, int *ints)
{
}

int __init setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

void __init smp_store_cpu_info(int id)
{
        struct cpuinfo_PPC *c = &cpu_data[id];

	/* assume bogomips are same for everything */
        c->loops_per_sec = loops_per_sec;
        c->pvr = _get_PVR();
}
