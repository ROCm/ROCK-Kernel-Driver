/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"

#ifdef CONFIG_SMP

#include "linux/sched.h"
#include "linux/module.h"
#include "linux/threads.h"
#include "linux/interrupt.h"
#include "linux/err.h"
#include "asm/smp.h"
#include "asm/processor.h"
#include "asm/spinlock.h"
#include "asm/hardirq.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "irq_user.h"
#include "os.h"

/* CPU online map, set by smp_boot_cpus */
unsigned long cpu_online_map = cpumask_of_cpu(0);

EXPORT_SYMBOL(cpu_online_map);

/* Per CPU bogomips and other parameters
 * The only piece used here is the ipi pipe, which is set before SMP is
 * started and never changed.
 */
struct cpuinfo_um cpu_data[NR_CPUS];

spinlock_t um_bh_lock = SPIN_LOCK_UNLOCKED;

atomic_t global_bh_count;

/* Not used by UML */
unsigned char global_irq_holder = NO_PROC_ID;
unsigned volatile long global_irq_lock;

/* Set when the idlers are all forked */
int smp_threads_ready = 0;

/* A statistic, can be a little off */
int num_reschedules_sent = 0;

/* Small, random number, never changed */
unsigned long cache_decay_ticks = 5;

/* Not changed after boot */
struct task_struct *idle_threads[NR_CPUS];

void smp_send_reschedule(int cpu)
{
	write(cpu_data[cpu].ipi_pipe[1], "R", 1);
	num_reschedules_sent++;
}

static void show(char * str)
{
	int cpu = smp_processor_id();

	printk(KERN_INFO "\n%s, CPU %d:\n", str, cpu);
}
	
#define MAXCOUNT 100000000

static inline void wait_on_bh(void)
{
	int count = MAXCOUNT;
	do {
		if (!--count) {
			show("wait_on_bh");
			count = ~0;
		}
		/* nothing .. wait for the other bh's to go away */
	} while (atomic_read(&global_bh_count) != 0);
}

/*
 * This is called when we want to synchronize with
 * bottom half handlers. We need to wait until
 * no other CPU is executing any bottom half handler.
 *
 * Don't wait if we're already running in an interrupt
 * context or are inside a bh handler. 
 */
void synchronize_bh(void)
{
	if (atomic_read(&global_bh_count) && !in_interrupt())
		wait_on_bh();
}

void smp_send_stop(void)
{
	int i;

	printk(KERN_INFO "Stopping all CPUs...");
	for(i = 0; i < num_online_cpus(); i++){
		if(i == current->thread_info->cpu)
			continue;
		write(cpu_data[i].ipi_pipe[1], "S", 1);
	}
	printk("done\n");
}

static cpumask_t smp_commenced_mask;
static cpumask_t smp_callin_map = CPU_MASK_NONE;

static int idle_proc(void *cpup)
{
	int cpu = (int) cpup, err;

	err = os_pipe(cpu_data[cpu].ipi_pipe, 1, 1);
	if(err)
		panic("CPU#%d failed to create IPI pipe, errno = %d", cpu, 
		      -err);

	activate_ipi(cpu_data[cpu].ipi_pipe[0], 
		     current->thread.mode.tt.extern_pid);
 
	wmb();
	if (cpu_test_and_set(cpu, &smp_callin_map)) {
		printk("huh, CPU#%d already present??\n", cpu);
		BUG();
	}

	while (!cpu_isset(cpu, &smp_commenced_mask))
		cpu_relax();

	cpu_set(cpu, cpu_online_map);
	default_idle();
	return(0);
}

static struct task_struct *idle_thread(int cpu)
{
	struct task_struct *new_task;
	unsigned char c;

        current->thread.request.u.thread.proc = idle_proc;
        current->thread.request.u.thread.arg = (void *) cpu;
	new_task = do_fork(CLONE_VM | CLONE_IDLETASK, 0, NULL, 0, NULL, NULL);
	if(IS_ERR(new_task)) panic("do_fork failed in idle_thread");

	cpu_tasks[cpu] = ((struct cpu_task) 
		          { .pid = 	new_task->thread.mode.tt.extern_pid,
			    .task = 	new_task } );
	idle_threads[cpu] = new_task;
	CHOOSE_MODE(write(new_task->thread.mode.tt.switch_pipe[1], &c, 
			  sizeof(c)),
		    ({ panic("skas mode doesn't support SMP"); }));
	return(new_task);
}

void smp_prepare_cpus(unsigned int maxcpus)
{
	struct task_struct *idle;
	unsigned long waittime;
	int err, cpu;

	cpu_set(0, cpu_online_map);
	cpu_set(0, smp_callin_map);

	err = os_pipe(cpu_data[0].ipi_pipe, 1, 1);
	if(err)	panic("CPU#0 failed to create IPI pipe, errno = %d", -err);

	activate_ipi(cpu_data[0].ipi_pipe[0], 
		     current->thread.mode.tt.extern_pid);

	for(cpu = 1; cpu < ncpus; cpu++){
		printk("Booting processor %d...\n", cpu);
		
		idle = idle_thread(cpu);

		init_idle(idle, cpu);
		unhash_process(idle);

		waittime = 200000000;
		while (waittime-- && !cpu_isset(cpu, smp_callin_map))
			cpu_relax();

		if (cpu_isset(cpu, smp_callin_map))
			printk("done\n");
		else printk("failed\n");
	}
}

void smp_prepare_boot_cpu(void)
{
	cpu_set(smp_processor_id(), cpu_online_map);
}

int __cpu_up(unsigned int cpu)
{
	cpu_set(cpu, smp_commenced_mask);
	while (!cpu_isset(cpu, cpu_online_map))
		mb();
	return(0);
}

int setup_profiling_timer(unsigned int multiplier)
{
	printk(KERN_INFO "setup_profiling_timer\n");
	return(0);
}

void smp_call_function_slave(int cpu);

void IPI_handler(int cpu)
{
	unsigned char c;
	int fd;

	fd = cpu_data[cpu].ipi_pipe[0];
	while (read(fd, &c, 1) == 1) {
		switch (c) {
		case 'C':
			smp_call_function_slave(cpu);
			break;

		case 'R':
			set_tsk_need_resched(current);
			break;

		case 'S':
			printk("CPU#%d stopping\n", cpu);
			while(1)
				pause();
			break;

		default:
			printk("CPU#%d received unknown IPI [%c]!\n", cpu, c);
			break;
		}
	}
}

int hard_smp_processor_id(void)
{
	return(pid_to_processor_id(os_getpid()));
}

static spinlock_t call_lock = SPIN_LOCK_UNLOCKED;
static atomic_t scf_started;
static atomic_t scf_finished;
static void (*func)(void *info);
static void *info;

void smp_call_function_slave(int cpu)
{
	atomic_inc(&scf_started);
	(*func)(info);
	atomic_inc(&scf_finished);
}

int smp_call_function(void (*_func)(void *info), void *_info, int nonatomic, 
		      int wait)
{
	int cpus = num_online_cpus() - 1;
	int i;

	if (!cpus)
		return 0;

	spin_lock_bh(&call_lock);
	atomic_set(&scf_started, 0);
	atomic_set(&scf_finished, 0);
	func = _func;
	info = _info;

	for (i=0;i<NR_CPUS;i++)
		if((i != current->thread_info->cpu) && 
		   cpu_isset(i, cpu_online_map))
			write(cpu_data[i].ipi_pipe[1], "C", 1);

	while (atomic_read(&scf_started) != cpus)
		barrier();

	if (wait)
		while (atomic_read(&scf_finished) != cpus)
			barrier();

	spin_unlock_bh(&call_lock);
	return 0;
}

#endif

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
