/*
 * SMP Support
 *
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * 
 * Lots of stuff stolen from arch/alpha/kernel/smp.c
 *
 *  00/09/11 David Mosberger <davidm@hpl.hp.com> Do loops_per_jiffy calibration on each CPU.
 *  00/08/23 Asit Mallick <asit.k.mallick@intel.com> fixed logical processor id
 *  00/03/31 Rohit Seth <rohit.seth@intel.com>	Fixes for Bootstrap Processor & cpu_online_map
 *			now gets done here (instead of setup.c)
 *  99/10/05 davidm	Update to bring it in sync with new command-line processing scheme.
 *  10/13/00 Goutham Rao <goutham.rao@intel.com> Updated smp_call_function and
 *		smp_call_function_single to resend IPI on timeouts
 */
#define __KERNEL_SYSCALLS__

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/efi.h>
#include <asm/machvec.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/unistd.h>

extern void __init calibrate_delay(void);
extern int cpu_idle(void * unused);
extern void machine_halt(void);
extern void start_ap(void);

extern int cpu_now_booting;			     /* Used by head.S to find idle task */
extern volatile unsigned long cpu_online_map;	     /* Bitmap of available cpu's */
extern struct cpuinfo_ia64 cpu_data[NR_CPUS];	     /* Duh... */

struct smp_boot_data smp_boot_data __initdata;

spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

char __initdata no_int_routing;

unsigned char smp_int_redirect;			/* are INT and IPI redirectable by the chipset? */
volatile int __cpu_physical_id[NR_CPUS] = { -1, };    /* Logical ID -> SAPIC ID */
int smp_num_cpus = 1;		
volatile int smp_threads_ready;			     /* Set when the idlers are all forked */
cycles_t cacheflush_time;
unsigned long ap_wakeup_vector = -1;		     /* External Int to use to wakeup AP's */

static volatile unsigned long cpu_callin_map;
static volatile int smp_commenced;

static int max_cpus = -1;			     /* Command line */
static unsigned long ipi_op[NR_CPUS];
struct smp_call_struct {
	void (*func) (void *info);
	void *info;
	long wait;
	atomic_t unstarted_count;
	atomic_t unfinished_count;
};
static volatile struct smp_call_struct *smp_call_function_data;

#define IPI_RESCHEDULE	        0
#define IPI_CALL_FUNC	        1
#define IPI_CPU_STOP	        2
#ifndef CONFIG_ITANIUM_PTCG
# define IPI_FLUSH_TLB		3
#endif	/*!CONFIG_ITANIUM_PTCG */

/*
 *	Setup routine for controlling SMP activation
 *
 *	Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 *      activation entirely (the MPS table probe still happens, though).
 *
 *	Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 *	greater than 0, limits the maximum number of CPUs activated in
 *	SMP mode to <NUM>.
 */

static int __init nosmp(char *str)
{
	max_cpus = 0;
	return 1;
}

__setup("nosmp", nosmp);

static int __init maxcpus(char *str)
{
	get_option(&str, &max_cpus);
	return 1;
}

__setup("maxcpus=", maxcpus);

static int __init
nointroute(char *str)
{
	no_int_routing = 1;
	return 1;
}

__setup("nointroute", nointroute);

/*
 * Yoink this CPU from the runnable list... 
 */
void
halt_processor(void) 
{
        clear_bit(smp_processor_id(), &cpu_online_map);
	max_xtp();
	__cli();
        for (;;)
		;

}

static inline int
pointer_lock(void *lock, void *data, int retry)
{
	volatile long *ptr = lock;
 again:
	if (cmpxchg_acq((void **) lock, 0, data) == 0)
		return 0;

	if (!retry)
		return -EBUSY;

	while (*ptr)
		;

	goto again;
}

void
handle_IPI(int irq, void *dev_id, struct pt_regs *regs) 
{
	int this_cpu = smp_processor_id();
	unsigned long *pending_ipis = &ipi_op[this_cpu];
	unsigned long ops;

	/* Count this now; we may make a call that never returns. */
	cpu_data[this_cpu].ipi_count++;

	mb();	/* Order interrupt and bit testing. */
	while ((ops = xchg(pending_ipis, 0)) != 0) {
	  mb();	/* Order bit clearing and data access. */
	  do {
		unsigned long which;

		which = ffz(~ops);
		ops &= ~(1 << which);
		
		switch (which) {
		case IPI_RESCHEDULE:
			/* 
			 * Reschedule callback.  Everything to be done is done by the 
			 * interrupt return path.  
			 */
			break;
			
		case IPI_CALL_FUNC: 
			{
				struct smp_call_struct *data;
				void (*func)(void *info);
				void *info;
				int wait;

				/* release the 'pointer lock' */
				data = (struct smp_call_struct *) smp_call_function_data;
				func = data->func;
				info = data->info;
				wait = data->wait;

				mb();
				atomic_dec(&data->unstarted_count);

				/* At this point the structure may be gone unless wait is true.  */
				(*func)(info);

				/* Notify the sending CPU that the task is done.  */
				mb();
				if (wait) 
					atomic_dec(&data->unfinished_count);
			}
			break;

		case IPI_CPU_STOP:
			halt_processor();
			break;

#ifndef CONFIG_ITANIUM_PTCG
		case IPI_FLUSH_TLB:
                {
			extern unsigned long flush_start, flush_end, flush_nbits, flush_rid;
			extern atomic_t flush_cpu_count;
			unsigned long saved_rid = ia64_get_rr(flush_start);
			unsigned long end = flush_end;
			unsigned long start = flush_start;
			unsigned long nbits = flush_nbits;

			/*
			 * Current CPU may be running with different
			 * RID so we need to reload the RID of flushed
			 * address.  Purging the translation also
			 * needs ALAT invalidation; we do not need
			 * "invala" here since it is done in
			 * ia64_leave_kernel.
			 */
			ia64_srlz_d();
			if (saved_rid != flush_rid) {
				ia64_set_rr(flush_start, flush_rid);
				ia64_srlz_d();
			}
			
			do {
				/*
				 * Purge local TLB entries.
				 */
				__asm__ __volatile__ ("ptc.l %0,%1" ::
						      "r"(start), "r"(nbits<<2) : "memory");
				start += (1UL << nbits);
			} while (start < end);

			ia64_insn_group_barrier();
			ia64_srlz_i();			/* srlz.i implies srlz.d */

			if (saved_rid != flush_rid) {
				ia64_set_rr(flush_start, saved_rid);
				ia64_srlz_d();
			}
			atomic_dec(&flush_cpu_count);
			break;
		}
#endif	/* !CONFIG_ITANIUM_PTCG */

		default:
			printk(KERN_CRIT "Unknown IPI on CPU %d: %lu\n", this_cpu, which);
			break;
		} /* Switch */
	  } while (ops);

	  mb();	/* Order data access and bit testing. */
	}
}

static inline void
send_IPI_single (int dest_cpu, int op) 
{
	
	if (dest_cpu == -1) 
                return;
        
	set_bit(op, &ipi_op[dest_cpu]);
	platform_send_ipi(dest_cpu, IPI_IRQ, IA64_IPI_DM_INT, 0);
}

static inline void
send_IPI_allbutself(int op)
{
	int i;
	
	for (i = 0; i < smp_num_cpus; i++) {
		if (i != smp_processor_id())
			send_IPI_single(i, op);
	}
}

static inline void
send_IPI_all(int op)
{
	int i;

	for (i = 0; i < smp_num_cpus; i++)
		send_IPI_single(i, op);
}

static inline void
send_IPI_self(int op)
{
	send_IPI_single(smp_processor_id(), op);
}

void
smp_send_reschedule(int cpu)
{
	send_IPI_single(cpu, IPI_RESCHEDULE);
}

void
smp_send_stop(void)
{
	send_IPI_allbutself(IPI_CPU_STOP);
}

#ifndef CONFIG_ITANIUM_PTCG
void
smp_send_flush_tlb(void)
{
	send_IPI_allbutself(IPI_FLUSH_TLB);
}
#endif	/* !CONFIG_ITANIUM_PTCG */

/*
 * Run a function on another CPU
 *  <func>	The function to run. This must be fast and non-blocking.
 *  <info>	An arbitrary pointer to pass to the function.
 *  <retry>	If true, keep retrying until ready.
 *  <wait>	If true, wait until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until the remote CPU is nearly ready to execute <func>
 * or is or has executed.
 */

int
smp_call_function_single (int cpuid, void (*func) (void *info), void *info, int retry, int wait)
{
	struct smp_call_struct data;
	unsigned long timeout;
	int cpus = 1;

	if (cpuid == smp_processor_id()) {
		printk(__FUNCTION__" trying to call self\n");
		return -EBUSY;
	}
	
	data.func = func;
	data.info = info;
	data.wait = wait;
	atomic_set(&data.unstarted_count, cpus);
	atomic_set(&data.unfinished_count, cpus);

	if (pointer_lock(&smp_call_function_data, &data, retry))
		return -EBUSY;

resend:
	/*  Send a message to all other CPUs and wait for them to respond  */
	send_IPI_single(cpuid, IPI_CALL_FUNC);

	/*  Wait for response  */
	timeout = jiffies + HZ;
	while ((atomic_read(&data.unstarted_count) > 0) && time_before(jiffies, timeout))
		barrier();
	if (atomic_read(&data.unstarted_count) > 0) {
#if (defined(CONFIG_ITANIUM_ASTEP_SPECIFIC) || defined(CONFIG_ITANIUM_BSTEP_SPECIFIC))
		goto resend;
#else
		smp_call_function_data = NULL;
		return -ETIMEDOUT;
#endif
	}
	if (wait)
		while (atomic_read(&data.unfinished_count) > 0)
			barrier();
	/* unlock pointer */
	smp_call_function_data = NULL;
	return 0;
}

/*
 * Run a function on all other CPUs.
 *  <func>	The function to run. This must be fast and non-blocking.
 *  <info>	An arbitrary pointer to pass to the function.
 *  <retry>	If true, keep retrying until ready.
 *  <wait>	If true, wait until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until remote CPUs are nearly ready to execute <func>
 * or are or have executed.
 */

int
smp_call_function (void (*func) (void *info), void *info, int retry, int wait)
{
	struct smp_call_struct data;
	unsigned long timeout;
	int cpus = smp_num_cpus - 1;

	if (cpus == 0)
		return 0;
	
	data.func = func;
	data.info = info;
	data.wait = wait;
	atomic_set(&data.unstarted_count, cpus);
	atomic_set(&data.unfinished_count, cpus);

	if (pointer_lock(&smp_call_function_data, &data, retry))
		return -EBUSY;

	/*  Send a message to all other CPUs and wait for them to respond  */
	send_IPI_allbutself(IPI_CALL_FUNC);

retry:
	/*  Wait for response  */
	timeout = jiffies + HZ;
	while ((atomic_read(&data.unstarted_count) > 0) && time_before(jiffies, timeout))
		barrier();
	if (atomic_read(&data.unstarted_count) > 0) {
#if (defined(CONFIG_ITANIUM_ASTEP_SPECIFIC) || defined(CONFIG_ITANIUM_BSTEP_SPECIFIC))
		int i;
		for (i = 0; i < smp_num_cpus; i++) {
			if (i != smp_processor_id())
				platform_send_ipi(i, IPI_IRQ, IA64_IPI_DM_INT, 0);
		}
		goto retry;
#else
		smp_call_function_data = NULL;
		return -ETIMEDOUT;
#endif
	}
	if (wait)
		while (atomic_read(&data.unfinished_count) > 0)
			barrier();
	/* unlock pointer */
	smp_call_function_data = NULL;
	return 0;
}

/*
 * Flush all other CPU's tlb and then mine.  Do this with smp_call_function() as we
 * want to ensure all TLB's flushed before proceeding.
 */
void
smp_flush_tlb_all(void)
{
	smp_call_function((void (*)(void *))__flush_tlb_all, NULL, 1, 1);
	__flush_tlb_all();
}

/*
 * Ideally sets up per-cpu profiling hooks.  Doesn't do much now...
 */
static inline void __init
smp_setup_percpu_timer(int cpuid)
{
        cpu_data[cpuid].prof_counter = 1;
        cpu_data[cpuid].prof_multiplier = 1;
}

void 
smp_do_timer(struct pt_regs *regs)
{
        int cpu = smp_processor_id();
        int user = user_mode(regs);
	struct cpuinfo_ia64 *data = &cpu_data[cpu];

        if (--data->prof_counter <= 0) {
		data->prof_counter = data->prof_multiplier;
		update_process_times(user);
	}
}


/*
 * AP's start using C here.
 */
void __init
smp_callin (void) 
{
	extern void ia64_rid_init(void);
	extern void ia64_init_itm(void);
	extern void ia64_cpu_local_tick(void);
#ifdef CONFIG_PERFMON
	extern void perfmon_init_percpu(void);
#endif
	int cpu = smp_processor_id();

	if (test_and_set_bit(cpu, &cpu_online_map)) {
		printk("CPU#%d already initialized!\n", cpu);
		machine_halt();
	}  

	efi_map_pal_code();
	cpu_init();

	smp_setup_percpu_timer(cpu);

	/* setup the CPU local timer tick */
	ia64_init_itm();

#ifdef CONFIG_PERFMON
	perfmon_init_percpu();
#endif

	/* Disable all local interrupts */
	ia64_set_lrr0(0, 1);	
	ia64_set_lrr1(0, 1);	

	local_irq_enable();		/* Interrupts have been off until now */

	calibrate_delay();
	my_cpu_data.loops_per_jiffy = loops_per_jiffy;

	/* allow the master to continue */
	set_bit(cpu, &cpu_callin_map);

	/* finally, wait for the BP to finish initialization: */
	while (!smp_commenced);

	cpu_idle(NULL);
}

/*
 * Create the idle task for a new AP.  DO NOT use kernel_thread() because
 * that could end up calling schedule() in the ia64_leave_kernel exit
 * path in which case the new idle task could get scheduled before we
 * had a chance to remove it from the run-queue...
 */
static int __init 
fork_by_hand(void)
{
	/*
	 * Don't care about the usp and regs settings since we'll never
	 * reschedule the forked task.
	 */
	return do_fork(CLONE_VM|CLONE_PID, 0, 0, 0);
}

/*
 * Bring one cpu online.  Return 0 if this fails for any reason.
 */
static int __init
smp_boot_one_cpu(int cpu)
{
	struct task_struct *idle;
	int cpu_phys_id = cpu_physical_id(cpu);
	long timeout;

	/* 
	 * Create an idle task for this CPU.  Note that the address we
	 * give to kernel_thread is irrelevant -- it's going to start
	 * where OS_BOOT_RENDEVZ vector in SAL says to start.  But
	 * this gets all the other task-y sort of data structures set
	 * up like we wish.   We need to pull the just created idle task 
	 * off the run queue and stuff it into the init_tasks[] array.  
	 * Sheesh . . .
	 */
	if (fork_by_hand() < 0) 
		panic("failed fork for CPU 0x%x", cpu_phys_id);
	/*
	 * We remove it from the pidhash and the runqueue
	 * once we got the process:
	 */
	idle = init_task.prev_task;
	if (!idle)
		panic("No idle process for CPU 0x%x", cpu_phys_id);
	init_tasks[cpu] = idle;
	del_from_runqueue(idle);
        unhash_process(idle);

	/* Schedule the first task manually.  */
	idle->processor = cpu;
	idle->has_cpu = 1;

	/* Let _start know what logical CPU we're booting (offset into init_tasks[] */
	cpu_now_booting = cpu;

	/* Kick the AP in the butt */
	platform_send_ipi(cpu, ap_wakeup_vector, IA64_IPI_DM_INT, 0);

	/* wait up to 10s for the AP to start  */
	for (timeout = 0; timeout < 100000; timeout++) {
		if (test_bit(cpu, &cpu_callin_map))
			return 1;
		udelay(100);
	}

	printk(KERN_ERR "SMP: Processor 0x%x is stuck.\n", cpu_phys_id);
	return 0;
}



/*
 * Called by smp_init bring all the secondaries online and hold them.  
 * XXX: this is ACPI specific; it uses "magic" variables exported from acpi.c 
 *      to 'discover' the AP's.  Blech.
 */
void __init
smp_boot_cpus(void)
{
	int i, cpu_count = 1;
	unsigned long bogosum;

	/* Take care of some initial bookkeeping.  */
	memset(&__cpu_physical_id, -1, sizeof(__cpu_physical_id));
	memset(&ipi_op, 0, sizeof(ipi_op));

	/* Setup BP mappings */
	__cpu_physical_id[0] = hard_smp_processor_id();

	/* on the BP, the kernel already called calibrate_delay_loop() in init/main.c */
	my_cpu_data.loops_per_jiffy = loops_per_jiffy;
#if 0
	smp_tune_scheduling();
#endif
 	smp_setup_percpu_timer(0);

	if (test_and_set_bit(0, &cpu_online_map)) {
		printk("CPU#%d already initialized!\n", smp_processor_id());
		machine_halt();
	}  
	init_idle();

	/* Nothing to do when told not to.  */
	if (max_cpus == 0) {
	        printk(KERN_INFO "SMP mode deactivated.\n");
		return;
	}

	if (max_cpus != -1) 
		printk("Limiting CPUs to %d\n", max_cpus);

	if (smp_boot_data.cpu_count > 1) {
		printk(KERN_INFO "SMP: starting up secondaries.\n");

		for (i = 0; i < smp_boot_data.cpu_count; i++) {
			/* skip performance restricted and bootstrap cpu: */
			if (smp_boot_data.cpu_phys_id[i] == -1
			    || smp_boot_data.cpu_phys_id[i] == hard_smp_processor_id())
				continue;

			cpu_physical_id(cpu_count) = smp_boot_data.cpu_phys_id[i];
			if (!smp_boot_one_cpu(cpu_count))
				continue;	/* failed */

			cpu_count++; /* Count good CPUs only... */
			/* 
			 * Bail if we've started as many CPUS as we've been told to.
			 */
			if (cpu_count == max_cpus)
				break;
		}
	}

	if (cpu_count == 1) {
		printk(KERN_ERR "SMP: Bootstrap processor only.\n");
	}

	bogosum = 0;
        for (i = 0; i < NR_CPUS; i++) {
		if (cpu_online_map & (1L << i))
			bogosum += cpu_data[i].loops_per_jiffy;
        }

	printk(KERN_INFO "SMP: Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
	       cpu_count, bogosum*HZ/500000, (bogosum*HZ/5000) % 100);

	smp_num_cpus = cpu_count;
}

/* 
 * Called when the BP is just about to fire off init.
 */
void __init 
smp_commence(void)
{
	smp_commenced = 1;
}

int __init
setup_profiling_timer(unsigned int multiplier)
{
        return -EINVAL;
}

/*
 * Assume that CPU's have been discovered by some platform-dependant
 * interface.  For SoftSDV/Lion, that would be ACPI.
 *
 * Setup of the IPI irq handler is done in irq.c:init_IRQ_SMP().
 *
 * This also registers the AP OS_MC_REDVEZ address with SAL.
 */
void __init
init_smp_config(void)
{
	struct fptr {
		unsigned long fp;
		unsigned long gp;
	} *ap_startup;
	long sal_ret;

	/* Tell SAL where to drop the AP's.  */
	ap_startup = (struct fptr *) start_ap;
	sal_ret = ia64_sal_set_vectors(SAL_VECTOR_OS_BOOT_RENDEZ,
				       __pa(ap_startup->fp), __pa(ap_startup->gp), 0, 
				       0, 0, 0);
	if (sal_ret < 0) {
		printk("SMP: Can't set SAL AP Boot Rendezvous: %s\n", ia64_sal_strerror(sal_ret));
		printk("     Forcing UP mode\n");
		max_cpus = 0;
		smp_num_cpus = 1; 
	}

}
