/*
 *
 *  arch/mips/kernel/smp.c
 *
 *  Copyright (C) 2000 Sibyte
 * 
 *  Written by Justin Carlson (carlson@sibyte.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */ 


#include <linux/config.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/cache.h>

#include <asm/atomic.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/mmu_context.h>
#include <asm/delay.h>
#include <asm/smp.h>

/*
 * This was written with the BRCM12500 MP SOC in mind, but tries to
 * be generic.  It's modelled on the mips64 smp.c code, which is
 * derived from Sparc, I'm guessing, which is derived from...
 * 
 * It's probably horribly designed for very large ccNUMA systems
 * as it doesn't take any node clustering into account.  
*/


/* Ze Big Kernel Lock! */
spinlock_t kernel_flag __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;
int smp_threads_ready;  /* Not used */
int smp_num_cpus;    
int global_irq_holder = NO_PROC_ID;
spinlock_t global_irq_lock = SPIN_LOCK_UNLOCKED;
struct mips_cpuinfo cpu_data[NR_CPUS];

struct smp_fn_call_struct smp_fn_call = 
{ SPIN_LOCK_UNLOCKED, ATOMIC_INIT(0), NULL, NULL};

static atomic_t cpus_booted = ATOMIC_INIT(0);


/* These are defined by the board-specific code. */

/* Cause the function described by smp_fn_call 
   to be executed on the passed cpu.  When the function
   has finished, increment the finished field of
   smp_fn_call. */

void core_call_function(int cpu);

/*
 * Clear all undefined state in the cpu, set up sp and gp to the passed
 * values, and kick the cpu into smp_bootstrap(); 
 */
void prom_boot_secondary(int cpu, unsigned long sp, unsigned long gp);

/*
 *  After we've done initial boot, this function is called to allow the
 *  board code to clean up state, if needed 
 */

void prom_init_secondary(void);


void cpu_idle(void);

/* Do whatever setup needs to be done for SMP at the board level.  Return
   the number of cpus in the system, including this one */
int prom_setup_smp(void);

int start_secondary(void *unused)
{
	prom_init_secondary();
	write_32bit_cp0_register(CP0_CONTEXT, smp_processor_id()<<23);
	current_pgd[smp_processor_id()] = init_mm.pgd;
	printk("Slave cpu booted successfully\n");
	atomic_inc(&cpus_booted);
	cpu_idle();
	return 0;
}

void __init smp_boot_cpus(void)
{
	int i;

	smp_num_cpus = prom_setup_smp();
	init_new_context(current, &init_mm);
	current->processor = 0;
	atomic_set(&cpus_booted, 1);  /* Master CPU is already booted... */
	init_idle();
	for (i = 1; i < smp_num_cpus; i++) {
		struct task_struct *p;
		struct pt_regs regs;
		printk("Starting CPU %d... ", i);

		/* Spawn a new process normally.  Grab a pointer to
		   its task struct so we can mess with it */
		do_fork(CLONE_VM|CLONE_PID, 0, &regs, 0);
		p = init_task.prev_task;

		/* Schedule the first task manually */
		p->processor = i;
		p->cpus_runnable = 1 << i; /* we schedule the first task manually */

		/* Attach to the address space of init_task. */
		atomic_inc(&init_mm.mm_count);
		p->active_mm = &init_mm;
		init_tasks[i] = p;

		del_from_runqueue(p);
		unhash_process(p);

		prom_boot_secondary(i,
				    (unsigned long)p + KERNEL_STACK_SIZE - 32,
				    (unsigned long)p);

#if 0

		/* This is copied from the ip-27 code in the mips64 tree */

		struct task_struct *p;

		/*
		 * The following code is purely to make sure
		 * Linux can schedule processes on this slave.
		 */
		kernel_thread(0, NULL, CLONE_PID);
		p = init_task.prev_task;
		sprintf(p->comm, "%s%d", "Idle", i);
		init_tasks[i] = p;
		p->processor = i;
		p->cpus_runnable = 1 << i; /* we schedule the first task manually *
		del_from_runqueue(p);
		unhash_process(p);
		/* Attach to the address space of init_task. */
		atomic_inc(&init_mm.mm_count);
		p->active_mm = &init_mm;
		prom_boot_secondary(i, 
				    (unsigned long)p + KERNEL_STACK_SIZE - 32,
				    (unsigned long)p);
#endif
	}

	/* Wait for everyone to come up */
	while (atomic_read(&cpus_booted) != smp_num_cpus);
}

void __init smp_commence(void)
{
	/* Not sure what to do here yet */
}

static void reschedule_this_cpu(void *dummy)
{
	current->need_resched = 1;
}

void FASTCALL(smp_send_reschedule(int cpu))
{
	smp_call_function(reschedule_this_cpu, NULL, 0, 0);
}


/*
 * The caller of this wants the passed function to run on every cpu.  If wait
 * is set, wait until all cpus have finished the function before returning.
 * The lock is here to protect the call structure.
 */
int smp_call_function (void (*func) (void *info), void *info, int retry, 
								int wait)
{
	int cpus = smp_num_cpus - 1;
	int i;

	if (smp_num_cpus < 2) {
		return 0;
	}

	spin_lock_bh(&smp_fn_call.lock);

	atomic_set(&smp_fn_call.finished, 0);
	smp_fn_call.fn = func;
	smp_fn_call.data = info;

	for (i = 0; i < smp_num_cpus; i++) {
		if (i != smp_processor_id()) {
			/* Call the board specific routine */
			core_call_function(i);
		}
	}

	if (wait) {
		while(atomic_read(&smp_fn_call.finished) != cpus) {}
	}

	spin_unlock_bh(&smp_fn_call.lock);
	return 0;
}

void synchronize_irq(void)
{
	panic("synchronize_irq");
}

static void stop_this_cpu(void *dummy)
{
	printk("Cpu stopping\n");
	for (;;);
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
	smp_num_cpus = 1;
}

/* Not really SMP stuff ... */
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}


/*
 * Most of this code is take from the mips64 tree (ip27-irq.c).  It's virtually
 * identical to the i386 implentation in arh/i386/irq.c, with translations for
 * the interrupt enable bit
 */

#define MAXCOUNT 		100000000
#define SYNC_OTHER_CORES(x)	udelay(x+1)

static inline void wait_on_irq(int cpu)
{
	int count = MAXCOUNT;

	for (;;) {

		/*
		 * Wait until all interrupts are gone. Wait
		 * for bottom half handlers unless we're
		 * already executing in one..
		 */
		if (!irqs_running())
			if (local_bh_count(cpu) || !spin_is_locked(&global_bh_lock))
				break;

		/* Duh, we have to loop. Release the lock to avoid deadlocks */
		spin_unlock(&global_irq_lock);

		for (;;) {
			if (!--count) {
				printk("Count spun out.  Huh?\n");
				count = ~0;
			}
			__sti();
			SYNC_OTHER_CORES(cpu);
			__cli();
			if (irqs_running())
				continue;
			if (spin_is_locked(&global_irq_lock))
				continue;
			if (!local_bh_count(cpu) && spin_is_locked(&global_bh_lock))
				continue;
			if (spin_trylock(&global_irq_lock))
				break;
		}
	}
}


static inline void get_irqlock(int cpu)
{
	if (!spin_trylock(&global_irq_lock)) {
		/* do we already hold the lock? */
		if ((unsigned char) cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it. Wait.. */
		spin_lock(&global_irq_lock);
	}
	/*
	 * We also to make sure that nobody else is running
	 * in an interrupt context.
	 */
	wait_on_irq(cpu);

	/*
	 * Ok, finally..
	 */
	global_irq_holder = cpu;
}


/*
 * A global "cli()" while in an interrupt context
 * turns into just a local cli(). Interrupts
 * should use spinlocks for the (very unlikely)
 * case that they ever want to protect against
 * each other.
 *
 * If we already have local interrupts disabled,
 * this will not turn a local disable into a
 * global one (problems with spinlocks: this makes
 * save_flags+cli+sti usable inside a spinlock).
 */
void __global_cli(void)
{
	unsigned int flags;

	__save_flags(flags);
	if (flags & ST0_IE) {
		int cpu = smp_processor_id();
		__cli();
		if (!local_irq_count(cpu))
			get_irqlock(cpu);
	}
}

void __global_sti(void)
{
	int cpu = smp_processor_id();

	if (!local_irq_count(cpu))
		release_irqlock(cpu);
	__sti();
}

/*
 * SMP flags value to restore to:
 * 0 - global cli
 * 1 - global sti
 * 2 - local cli
 * 3 - local sti
 */
unsigned long __global_save_flags(void)
{
	int retval;
	int local_enabled;
	unsigned long flags;
	int cpu = smp_processor_id();

	__save_flags(flags);
	local_enabled = (flags & ST0_IE);
	/* default to local */
	retval = 2 + local_enabled;

	/* check for global flags if we're not in an interrupt */
	if (!local_irq_count(cpu)) {
		if (local_enabled)
			retval = 1;
		if (global_irq_holder == cpu)
			retval = 0;
	}

	return retval;
}

void __global_restore_flags(unsigned long flags)
{
	switch (flags) {
		case 0:
			__global_cli();
			break;
		case 1:
			__global_sti();
			break;
		case 2:
			__cli();
			break;
		case 3:
			__sti();
			break;
		default:
			printk("global_restore_flags: %08lx\n", flags);
	}
}
