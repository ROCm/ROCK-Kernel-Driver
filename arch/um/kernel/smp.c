/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"

/* CPU online map */
unsigned long cpu_online_map = 1;

#ifdef CONFIG_SMP

#include "linux/sched.h"
#include "linux/threads.h"
#include "linux/interrupt.h"
#include "asm/smp.h"
#include "asm/processor.h"
#include "asm/spinlock.h"
#include "asm/softirq.h"
#include "asm/hardirq.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "os.h"

/* The 'big kernel lock' */
spinlock_t kernel_flag __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;

/* Per CPU bogomips and other parameters */
struct cpuinfo_um cpu_data[NR_CPUS];

spinlock_t um_bh_lock = SPIN_LOCK_UNLOCKED;

atomic_t global_bh_count;

unsigned char global_irq_holder = NO_PROC_ID;
unsigned volatile long global_irq_lock;

/* Set when the idlers are all forked */
int smp_threads_ready = 0;
int num_reschedules_sent = 0;

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
	printk(KERN_INFO "Stopping all CPUs\n");
}


static atomic_t smp_commenced = ATOMIC_INIT(0);
static volatile unsigned long smp_callin_map = 0;

void smp_commence(void)
{
	printk("All CPUs are go!\n");

	wmb();
	atomic_set(&smp_commenced, 1);
}

static int idle_proc(void *unused)
{
	int cpu, err;

	set_current(current);
	del_from_runqueue(current);
	unhash_process(current);

	cpu = current->processor;
	err = os_pipe(cpu_data[cpu].ipi_pipe, 1, 1);
	if(err)
		panic("CPU#%d failed to create IPI pipe, errno = %d", cpu, 
		      -err);

	activate_ipi(cpu_data[cpu].ipi_pipe[0], current->thread.extern_pid);
 
	wmb();
	if (test_and_set_bit(current->processor, &smp_callin_map)) {
		printk("huh, CPU#%d already present??\n", current->processor);
		BUG();
	}

	while (!atomic_read(&smp_commenced))
		cpu_relax();

	init_idle();
	default_idle();
	return(0);
}

int inited_cpus = 1;

static int idle_thread(int (*fn)(void *), int cpu)
{
	struct task_struct *p;
	int pid;
	unsigned char c;

        current->thread.request.u.thread.proc = fn;
        current->thread.request.u.thread.arg = NULL;
	p = do_fork(CLONE_VM | CLONE_PID, 0, NULL, 0);
	if(IS_ERR(p)) panic("do_fork failed in idle_thread");

	cpu_tasks[cpu].pid = p->thread.extern_pid;
	cpu_tasks[cpu].task = p;
	inited_cpus++;
	init_tasks[cpu] = p;
	p->processor = cpu;
	p->cpus_allowed = 1 << cpu;
	p->cpus_runnable = p->cpus_allowed;
	write(p->thread.switch_pipe[1], &c, sizeof(c));
	return(p->thread.extern_pid);
}

void smp_boot_cpus(void)
{
	int err;

	set_bit(0, &cpu_online_map);
	set_bit(0, &smp_callin_map);

	err = os_pipe(cpu_data[0].ipi_pipe, 1, 1);
	if(err)	panic("CPU#0 failed to create IPI pipe, errno = %d", -err);

	activate_ipi(cpu_data[0].ipi_pipe[0], current->thread.extern_pid);

	if(ncpus < 1){
		printk(KERN_INFO "ncpus set to 1\n");
		ncpus = 1;
	}
	else if(ncpus > NR_CPUS){
		printk(KERN_INFO 
		       "ncpus can't be greater than NR_CPUS, set to %d\n",
		       NR_CPUS);
		ncpus = NR_CPUS;
	}

	if(ncpus > 1){
		int i, pid;

		printk(KERN_INFO "Starting up other processors:\n");
		for(i=1;i<ncpus;i++){
			int waittime;

			/* Do this early, for hard_smp_processor_id()  */
			cpu_tasks[i].pid = -1;
			set_bit(i, &cpu_online_map);

			pid = idle_thread(idle_proc, i);
			printk(KERN_INFO "\t#%d - idle thread pid = %d.. ",
			       i, pid);

			waittime = 200000000;
			while (waittime-- && !test_bit(i, &smp_callin_map))
				cpu_relax();

			if (test_bit(i, &smp_callin_map))
				printk("online\n");
			else {
				printk("failed\n");
				clear_bit(i, &cpu_online_map);
			}
		}
	}
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
			current->need_resched = 1;
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
		if (i != current->processor && test_bit(i, &cpu_online_map))
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
