/*
 *  arch/s390/kernel/smp.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  based on other smp stuff by 
 *    (c) 1995 Alan Cox, CymruNET Ltd  <alan@cymru.net>
 *    (c) 1998 Ingo Molnar
 *
 * We work with logical cpu numbering everywhere we can. The only
 * functions using the real cpu address (got from STAP) are the sigp
 * functions. For all other functions we use the identity mapping.
 * That means that cpu_number_map[i] == i for every cpu. cpu_number_map is
 * used e.g. to find the idle task belonging to a logical cpu. Every array
 * in the kernel is sorted by the logical cpu number and not by the physical
 * one which is causing all the confusion with __cpu_logical_map and
 * cpu_number_map in other architectures.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/smp_lock.h>

#include <linux/delay.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>

#include <asm/sigp.h>
#include <asm/pgalloc.h>
#include <asm/irq.h>
#include <asm/s390_ext.h>
#include <asm/cpcmd.h>
#include <asm/tlbflush.h>

/* prototypes */
extern int cpu_idle(void * unused);

extern volatile int __cpu_logical_map[];

/*
 * An array with a pointer the lowcore of every CPU.
 */

struct _lowcore *lowcore_ptr[NR_CPUS];
cycles_t         cacheflush_time=0;
int              smp_threads_ready=0;      /* Set when the idlers are all forked. */

cpumask_t cpu_online_map;
cpumask_t cpu_possible_map;
unsigned long    cache_decay_ticks = 0;

EXPORT_SYMBOL(cpu_online_map);

/*
 * Reboot, halt and power_off routines for SMP.
 */
extern char vmhalt_cmd[];
extern char vmpoff_cmd[];

extern void reipl(unsigned long devno);

static void smp_ext_bitcall(int, ec_bit_sig);
static void smp_ext_bitcall_others(ec_bit_sig);

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 */
static spinlock_t call_lock = SPIN_LOCK_UNLOCKED;

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
};

static struct call_data_struct * call_data;

/*
 * 'Call function' interrupt callback
 */
static void do_call_function(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	atomic_inc(&call_data->started);
	(*func)(info);
	if (wait)
		atomic_inc(&call_data->finished);
}

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
 * hardware interrupt handler or from a bottom half handler.
 */
{
	struct call_data_struct data;
	int cpus = num_online_cpus()-1;

	/* FIXME: get cpu lock -hc */
	if (cpus <= 0)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock(&call_lock);
	call_data = &data;
	/* Send a message to all other CPUs and wait for them to respond */
        smp_ext_bitcall_others(ec_call_function);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		cpu_relax();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			cpu_relax();
	spin_unlock(&call_lock);

	return 0;
}

/*
 * Call a function on one CPU
 * cpu : the CPU the function should be executed on
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler. You may call it from a bottom half.
 *
 * It is guaranteed that the called function runs on the specified CPU,
 * preemption is disabled.
 */
int smp_call_function_on(void (*func) (void *info), void *info,
			 int nonatomic, int wait, int cpu)
{
	struct call_data_struct data;
	int curr_cpu;

	if (!cpu_online(cpu))
		return -EINVAL;

	/* disable preemption for local function call */
	curr_cpu = get_cpu();

	if (curr_cpu == cpu) {
		/* direct call to function */
		func(info);
		put_cpu();
		return 0;
	}

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock_bh(&call_lock);
	call_data = &data;
	smp_ext_bitcall(cpu, ec_call_function);

	/* Wait for response */
	while (atomic_read(&data.started) != 1)
		cpu_relax();

	if (wait)
		while (atomic_read(&data.finished) != 1)
			cpu_relax();

	spin_unlock_bh(&call_lock);
	put_cpu();
	return 0;
}
EXPORT_SYMBOL(smp_call_function_on);

static inline void do_send_stop(void)
{
        unsigned long dummy;
        int i, rc;

        /* stop all processors */
        for (i =  0; i < NR_CPUS; i++) {
                if (!cpu_online(i) || smp_processor_id() == i)
			continue;
		do {
			rc = signal_processor_ps(&dummy, 0, i, sigp_stop);
		} while (rc == sigp_busy);
	}
}

static inline void do_store_status(void)
{
        unsigned long low_core_addr;
        unsigned long dummy;
        int i, rc;

        /* store status of all processors in their lowcores (real 0) */
        for (i =  0; i < NR_CPUS; i++) {
                if (!cpu_online(i) || smp_processor_id() == i) 
			continue;
		low_core_addr = (unsigned long) lowcore_ptr[i];
		do {
			rc = signal_processor_ps(&dummy, low_core_addr, i,
						 sigp_store_status_at_address);
		} while(rc == sigp_busy);
        }
}

/*
 * this function sends a 'stop' sigp to all other CPUs in the system.
 * it goes straight through.
 */
void smp_send_stop(void)
{
        /* write magic number to zero page (absolute 0) */
	lowcore_ptr[smp_processor_id()]->panic_magic = __PANIC_MAGIC;

	/* stop other processors. */
	do_send_stop();

	/* store status of other processors. */
	do_store_status();
}

/*
 * Reboot, halt and power_off routines for SMP.
 */
static cpumask_t cpu_restart_map;

static void do_machine_restart(void * __unused)
{
	cpu_clear(smp_processor_id(), cpu_restart_map);
	if (smp_processor_id() == 0) {
		/* Wait for all other cpus to enter do_machine_restart. */
		while (!cpus_empty(cpu_restart_map))
			cpu_relax();
		/* Store status of other cpus. */
		do_store_status();
		/*
		 * Finally call reipl. Because we waited for all other
		 * cpus to enter this function we know that they do
		 * not hold any s390irq-locks (the cpus have been
		 * interrupted by an external interrupt and s390irq
		 * locks are always held disabled).
		 */
		if (MACHINE_IS_VM)
			cpcmd ("IPL", NULL, 0);
		else
			reipl (0x10000 | S390_lowcore.ipl_device);
	}
	signal_processor(smp_processor_id(), sigp_stop);
}

void machine_restart_smp(char * __unused) 
{
	cpu_restart_map = cpu_online_map;
        on_each_cpu(do_machine_restart, NULL, 0, 0);
}

static void do_wait_for_stop(void)
{
	unsigned long cr[16];

	__ctl_store(cr, 0, 15);
	cr[0] &= ~0xffff;
	cr[6] = 0;
	__ctl_load(cr, 0, 15);
	for (;;)
		enabled_wait();
}

static void do_machine_halt(void * __unused)
{
	if (smp_processor_id() == 0) {
		smp_send_stop();
		if (MACHINE_IS_VM && strlen(vmhalt_cmd) > 0)
			cpcmd(vmhalt_cmd, NULL, 0);
		signal_processor(smp_processor_id(),
				 sigp_stop_and_store_status);
	}
	do_wait_for_stop();
}

void machine_halt_smp(void)
{
        on_each_cpu(do_machine_halt, NULL, 0, 0);
}

static void do_machine_power_off(void * __unused)
{
	if (smp_processor_id() == 0) {
		smp_send_stop();
		if (MACHINE_IS_VM && strlen(vmpoff_cmd) > 0)
			cpcmd(vmpoff_cmd, NULL, 0);
		signal_processor(smp_processor_id(),
				 sigp_stop_and_store_status);
	}
	do_wait_for_stop();
}

void machine_power_off_smp(void)
{
        on_each_cpu(do_machine_power_off, NULL, 0, 0);
}

/*
 * This is the main routine where commands issued by other
 * cpus are handled.
 */

void do_ext_call_interrupt(struct pt_regs *regs, __u16 code)
{
        unsigned long bits;

        /*
         * handle bit signal external calls
         *
         * For the ec_schedule signal we have to do nothing. All the work
         * is done automatically when we return from the interrupt.
         */
	bits = xchg(&S390_lowcore.ext_call_fast, 0);

	if (test_bit(ec_call_function, &bits)) 
		do_call_function();
}

/*
 * Send an external call sigp to another cpu and return without waiting
 * for its completion.
 */
static void smp_ext_bitcall(int cpu, ec_bit_sig sig)
{
        /*
         * Set signaling bit in lowcore of target cpu and kick it
         */
	set_bit(sig, (unsigned long *) &lowcore_ptr[cpu]->ext_call_fast);
	while(signal_processor(cpu, sigp_external_call) == sigp_busy)
		udelay(10);
}

/*
 * Send an external call sigp to every other cpu in the system and
 * return without waiting for its completion.
 */
static void smp_ext_bitcall_others(ec_bit_sig sig)
{
        int i;

        for (i = 0; i < NR_CPUS; i++) {
                if (!cpu_online(i) || smp_processor_id() == i)
                        continue;
                /*
                 * Set signaling bit in lowcore of target cpu and kick it
                 */
		set_bit(sig, (unsigned long *) &lowcore_ptr[i]->ext_call_fast);
                while (signal_processor(i, sigp_external_call) == sigp_busy)
			udelay(10);
        }
}

#ifndef CONFIG_ARCH_S390X
/*
 * this function sends a 'purge tlb' signal to another CPU.
 */
void smp_ptlb_callback(void *info)
{
	local_flush_tlb();
}

void smp_ptlb_all(void)
{
        on_each_cpu(smp_ptlb_callback, NULL, 0, 1);
}
EXPORT_SYMBOL(smp_ptlb_all);
#endif /* ! CONFIG_ARCH_S390X */

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
void smp_send_reschedule(int cpu)
{
        smp_ext_bitcall(cpu, ec_schedule);
}

/*
 * parameter area for the set/clear control bit callbacks
 */
typedef struct
{
	__u16 start_ctl;
	__u16 end_ctl;
	unsigned long orvals[16];
	unsigned long andvals[16];
} ec_creg_mask_parms;

/*
 * callback for setting/clearing control bits
 */
void smp_ctl_bit_callback(void *info) {
	ec_creg_mask_parms *pp;
	unsigned long cregs[16];
	int i;
	
	pp = (ec_creg_mask_parms *) info;
	__ctl_store(cregs[pp->start_ctl], pp->start_ctl, pp->end_ctl);
	for (i = pp->start_ctl; i <= pp->end_ctl; i++)
		cregs[i] = (cregs[i] & pp->andvals[i]) | pp->orvals[i];
	__ctl_load(cregs[pp->start_ctl], pp->start_ctl, pp->end_ctl);
}

/*
 * Set a bit in a control register of all cpus
 */
void smp_ctl_set_bit(int cr, int bit) {
        ec_creg_mask_parms parms;

	parms.start_ctl = cr;
	parms.end_ctl = cr;
	parms.orvals[cr] = 1 << bit;
	parms.andvals[cr] = -1L;
	preempt_disable();
	smp_call_function(smp_ctl_bit_callback, &parms, 0, 1);
        __ctl_set_bit(cr, bit);
	preempt_enable();
}

/*
 * Clear a bit in a control register of all cpus
 */
void smp_ctl_clear_bit(int cr, int bit) {
        ec_creg_mask_parms parms;

	parms.start_ctl = cr;
	parms.end_ctl = cr;
	parms.orvals[cr] = 0;
	parms.andvals[cr] = ~(1L << bit);
	preempt_disable();
	smp_call_function(smp_ctl_bit_callback, &parms, 0, 1);
        __ctl_clear_bit(cr, bit);
	preempt_enable();
}

/*
 * Lets check how many CPUs we have.
 */

void __init smp_check_cpus(unsigned int max_cpus)
{
        int curr_cpu, num_cpus;
	__u16 boot_cpu_addr;

	boot_cpu_addr = S390_lowcore.cpu_data.cpu_addr;
        current_thread_info()->cpu = 0;
        num_cpus = 1;
        for (curr_cpu = 0;
             curr_cpu <= 65535 && num_cpus < max_cpus; curr_cpu++) {
                if ((__u16) curr_cpu == boot_cpu_addr)
                        continue;
                __cpu_logical_map[num_cpus] = (__u16) curr_cpu;
                if (signal_processor(num_cpus, sigp_sense) ==
                    sigp_not_operational)
                        continue;
		cpu_set(num_cpus, cpu_possible_map);
                num_cpus++;
        }
        printk("Detected %d CPU's\n",(int) num_cpus);
        printk("Boot cpu address %2X\n", boot_cpu_addr);
}

/*
 *      Activate a secondary processor.
 */
extern void init_cpu_timer(void);
extern int pfault_init(void);
extern int pfault_token(void);

int __devinit start_secondary(void *cpuvoid)
{
        /* Setup the cpu */
        cpu_init();
        /* init per CPU timer */
        init_cpu_timer();
#ifdef CONFIG_PFAULT
	/* Enable pfault pseudo page faults on this cpu. */
	pfault_init();
#endif
	/* Mark this cpu as online */
	cpu_set(smp_processor_id(), cpu_online_map);
	/* Switch on interrupts */
	local_irq_enable();
        /* Print info about this processor */
        print_cpu_info(&S390_lowcore.cpu_data);
        /* cpu_idle will call schedule for us */
        return cpu_idle(NULL);
}

static struct task_struct *__devinit fork_by_hand(void)
{
       struct pt_regs regs;
       /* don't care about the psw and regs settings since we'll never
          reschedule the forked task. */
       memset(&regs,0,sizeof(struct pt_regs));
       return copy_process(CLONE_VM|CLONE_IDLETASK, 0, &regs, 0, NULL, NULL);
}

int __cpu_up(unsigned int cpu)
{
        struct task_struct *idle;
        struct _lowcore    *cpu_lowcore;
        sigp_ccode          ccode;

	/*
	 *  Set prefix page for new cpu
	 */

	ccode = signal_processor_p((unsigned long)(lowcore_ptr[cpu]),
				   cpu, sigp_set_prefix);
	if (ccode){
		printk("sigp_set_prefix failed for cpu %d "
		       "with condition code %d\n",
		       (int) cpu, (int) ccode);
		return -EIO;
	}

        /* We can't use kernel_thread since we must _avoid_ to reschedule
           the child. */
        idle = fork_by_hand();
	if (IS_ERR(idle)){
                printk("failed fork for CPU %d", cpu);
		return -EIO;
	}
	wake_up_forked_process(idle);

        /*
         * We remove it from the pidhash and the runqueue
         * once we got the process:
         */
	init_idle(idle, cpu);

        unhash_process(idle);

        cpu_lowcore = lowcore_ptr[cpu];
	cpu_lowcore->save_area[15] = idle->thread.ksp;
	cpu_lowcore->kernel_stack = (unsigned long)
		idle->thread_info + (THREAD_SIZE);
	__ctl_store(cpu_lowcore->cregs_save_area[0], 0, 15);
	__asm__ __volatile__("stam  0,15,0(%0)"
			     : : "a" (&cpu_lowcore->access_regs_save_area)
			     : "memory");
	cpu_lowcore->percpu_offset = __per_cpu_offset[cpu];
        cpu_lowcore->current_task = (unsigned long) idle;
        cpu_lowcore->cpu_data.cpu_nr = cpu;
	eieio();
	signal_processor(cpu,sigp_restart);

	while (!cpu_online(cpu));
	return 0;
}

/*
 *	Cycle through the processors and setup structures.
 */

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned long async_stack;
        int i;

        /* request the 0x1202 external interrupt */
        if (register_external_interrupt(0x1202, do_ext_call_interrupt) != 0)
                panic("Couldn't request external interrupt 0x1202");
        smp_check_cpus(max_cpus);
        memset(lowcore_ptr,0,sizeof(lowcore_ptr));  
        /*
         *  Initialize prefix pages and stacks for all possible cpus
         */
	print_cpu_info(&S390_lowcore.cpu_data);

        for(i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		lowcore_ptr[i] = (struct _lowcore *)
			__get_free_pages(GFP_KERNEL|GFP_DMA, 
					sizeof(void*) == 8 ? 1 : 0);
		async_stack = __get_free_pages(GFP_KERNEL,ASYNC_ORDER);
		if (lowcore_ptr[i] == NULL || async_stack == 0ULL)
			panic("smp_boot_cpus failed to allocate memory\n");

		*(lowcore_ptr[i]) = S390_lowcore;
		lowcore_ptr[i]->async_stack = async_stack + (ASYNC_SIZE);
	}
	set_prefix((u32)(unsigned long) lowcore_ptr[smp_processor_id()]);
}

void __devinit smp_prepare_boot_cpu(void)
{
	cpu_set(smp_processor_id(), cpu_online_map);
	cpu_set(smp_processor_id(), cpu_possible_map);
	S390_lowcore.percpu_offset = __per_cpu_offset[smp_processor_id()];
}

void smp_cpus_done(unsigned int max_cpus)
{
}

/*
 * the frequency of the profiling timer can be changed
 * by writing a multiplier value into /proc/profile.
 *
 * usually you want to run this on all CPUs ;)
 */
int setup_profiling_timer(unsigned int multiplier)
{
        return 0;
}

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static int __init topology_init(void)
{
	int cpu;
	int ret;

	for_each_cpu(cpu) {
		ret = register_cpu(&per_cpu(cpu_devices, cpu), cpu, NULL);
		if (ret)
			printk(KERN_WARNING "topology_init: register_cpu %d "
			       "failed (%d)\n", cpu, ret);
	}
	return 0;
}

subsys_initcall(topology_init);

EXPORT_SYMBOL(cpu_possible_map);
EXPORT_SYMBOL(lowcore_ptr);
EXPORT_SYMBOL(smp_ctl_set_bit);
EXPORT_SYMBOL(smp_ctl_clear_bit);
EXPORT_SYMBOL(smp_call_function);
