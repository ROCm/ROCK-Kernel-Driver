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

#include <asm/sigp.h>
#include <asm/pgalloc.h>
#include <asm/irq.h>
#include <asm/s390_ext.h>
#include <asm/cpcmd.h>

/* prototypes */
extern int cpu_idle(void * unused);

extern __u16 boot_cpu_addr;
extern volatile int __cpu_logical_map[];

/*
 * An array with a pointer the lowcore of every CPU.
 */
static int       max_cpus = NR_CPUS;	  /* Setup configured maximum number of CPUs to activate	*/
int              smp_num_cpus;
struct _lowcore *lowcore_ptr[NR_CPUS];
unsigned int     prof_multiplier[NR_CPUS];
unsigned int     prof_old_multiplier[NR_CPUS];
unsigned int     prof_counter[NR_CPUS];
cycles_t         cacheflush_time=0;
int              smp_threads_ready=0;      /* Set when the idlers are all forked. */
static atomic_t  smp_commenced = ATOMIC_INIT(0);

spinlock_t       kernel_flag = SPIN_LOCK_UNLOCKED;

/*
 *      Setup routine for controlling SMP activation
 *
 *      Command-line option of "nosmp" or "maxcpus=0" will disable SMP
 *      activation entirely (the MPS table probe still happens, though).
 *
 *      Command-line option of "maxcpus=<NUM>", where <NUM> is an integer
 *      greater than 0, limits the maximum number of CPUs activated in
 *      SMP mode to <NUM>.
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

/*
 * Reboot, halt and power_off routines for SMP.
 */
extern char vmhalt_cmd[];
extern char vmpoff_cmd[];

extern void reipl(unsigned long devno);

void do_machine_restart(void)
{
        smp_send_stop();
	reipl(S390_lowcore.ipl_device);
}

void machine_restart(char * __unused) 
{
        if (smp_processor_id() != 0) {
                smp_ext_bitcall(0, ec_restart);
                for (;;);
        } else
                do_machine_restart();
}

void do_machine_halt(void)
{
        smp_send_stop();
        if (MACHINE_IS_VM && strlen(vmhalt_cmd) > 0)
                cpcmd(vmhalt_cmd, NULL, 0);
        signal_processor(smp_processor_id(), sigp_stop_and_store_status);
}

void machine_halt(void)
{
        if (smp_processor_id() != 0) {
                smp_ext_bitcall(0, ec_halt);
                for (;;);
        } else
                do_machine_halt();
}

void do_machine_power_off(void)
{
        smp_send_stop();
        if (MACHINE_IS_VM && strlen(vmpoff_cmd) > 0)
                cpcmd(vmpoff_cmd, NULL, 0);
        signal_processor(smp_processor_id(), sigp_stop_and_store_status);
}

void machine_power_off(void)
{
        if (smp_processor_id() != 0) {
                smp_ext_bitcall(0, ec_power_off);
                for (;;);
        } else
                do_machine_power_off();
}

/*
 * This is the main routine where commands issued by other
 * cpus are handled.
 */

void do_ext_call_interrupt(struct pt_regs *regs, __u16 code)
{
        ec_ext_call *ec, *next;
        unsigned long bits;

        /*
         * handle bit signal external calls
         *
         * For the ec_schedule signal we have to do nothing. All the work
         * is done automatically when we return from the interrupt.
	 * For the ec_restart, ec_halt and ec_power_off we call the
         * appropriate routine.
         */
	bits = xchg(&S390_lowcore.ext_call_fast, 0);

        if (test_bit(ec_restart, &bits))
		do_machine_restart();
        if (test_bit(ec_halt, &bits))
		do_machine_halt();
        if (test_bit(ec_power_off, &bits))
		do_machine_power_off();

        /*
         * Handle external call commands with a parameter area
         */
	ec = (ec_ext_call *) xchg(&S390_lowcore.ext_call_queue, 0);
        if (ec == NULL)
                return;   /* no command signals */

        /* Make a fifo out of the lifo */
        next = ec->next;
        ec->next = NULL;
        while (next != NULL) {
                ec_ext_call *tmp = next->next;
                next->next = ec;
                ec = next;
                next = tmp;
        }

        /* Execute every sigp command on the queue */
        while (ec != NULL) {
                switch (ec->cmd) {
                case ec_callback_async: {
                        void (*func)(void *info);
                        void *info;

                        func = ec->func;
                        info = ec->info;
                        atomic_set(&ec->status,ec_executing);
                        (func)(info);
                        return;
                }
                case ec_callback_sync:
                        atomic_set(&ec->status,ec_executing);
                        (ec->func)(ec->info);
                        atomic_set(&ec->status,ec_done);
                        return;
                default:
                }
                ec = ec->next;
        }
}

/*
 * Swap in a new request to external call queue 
 */
static inline void smp_add_ext_call(ec_ext_call *ec, struct _lowcore *lowcore)
{
	int success;

	while (1) {
		ec->next = (ec_ext_call*) lowcore->ext_call_queue;
		__asm__ __volatile__ (
                        "   lgr 0,%2\n"
			"   csg 0,%3,%1\n"
			"   ipm %0\n"
			"   srl %0,28\n"
			: "=d" (success), "+m" (lowcore->ext_call_queue)
			: "d" (ec->next), "d" (ec)
			: "cc", "0" );
		if (success == 0) break;
	}
}

/*
 * Send an external call sigp to another cpu and wait for its completion.
 */
sigp_ccode
smp_ext_call(int cpu, void (*func)(void *info), void *info, int wait)
{
        sigp_ccode ccode;
        ec_ext_call ec;

        ec.cmd = wait ? ec_callback_sync:ec_callback_async;
        atomic_set(&ec.status, ec_pending);
        ec.func = func;
	ec.info = info;
	/* swap in new request to external call queue */
	smp_add_ext_call(&ec, &get_cpu_lowcore(cpu));
        /*
         * We try once to deliver the signal. There are four possible
         * return codes:
         * 0) Order code accepted - can't show up on an external call
         * 1) Status stored - fine, wait for completion.
         * 2) Busy - there is another signal pending. Thats fine too, because
         *    do_ext_call from the pending signal will execute all signals on
         *    the queue. We wait for completion.
         * 3) Not operational - something very bad has happened to the cpu.
         *    do not wait for completion.
         */
        ccode = signal_processor(cpu, sigp_external_call);

        if (ccode != sigp_not_operational)
                /* wait for completion, FIXME: possible seed of a deadlock */
                while (atomic_read(&ec.status) != (wait?ec_done:ec_executing));

        return ccode;
}

/*
 * Send a callback sigp to every other cpu in the system.
 */
void smp_ext_call_others(void (*func)(void *info), void *info, int wait)
{
        ec_ext_call ec[NR_CPUS];
        sigp_ccode ccode;
        int i;

        for (i = 0; i < smp_num_cpus; i++) {
                if (smp_processor_id() == i)
                        continue;
		ec[i].cmd = wait ? ec_callback_sync : ec_callback_async;
		atomic_set(&ec[i].status, ec_pending);
		ec[i].func = func;
		ec[i].info = info;
		smp_add_ext_call(ec+i, &get_cpu_lowcore(i));
                ccode = signal_processor(i, sigp_external_call);
        }

        /* wait for completion, FIXME: possible seed of a deadlock */
        for (i = 0; i < smp_num_cpus; i++) {
                if (smp_processor_id() == i)
                        continue;
                while (atomic_read(&ec[i].status) != 
		       (wait ? ec_done:ec_executing));
        }
}

/*
 * Send an external call sigp to another cpu and return without waiting
 * for its completion.
 */
sigp_ccode smp_ext_bitcall(int cpu, ec_bit_sig sig)
{
        sigp_ccode ccode;

        /*
         * Set signaling bit in lowcore of target cpu and kick it
         */
	set_bit(sig, &(get_cpu_lowcore(cpu).ext_call_fast));
        ccode = signal_processor(cpu, sigp_external_call);
        return ccode;
}

/*
 * Send an external call sigp to every other cpu in the system and
 * return without waiting for its completion.
 */
void smp_ext_bitcall_others(ec_bit_sig sig)
{
        sigp_ccode ccode;
        int i;

        for (i = 0; i < smp_num_cpus; i++) {
                if (smp_processor_id() == i)
                        continue;
                /*
                 * Set signaling bit in lowcore of target cpu and kick it
                 */
		set_bit(sig, &(get_cpu_lowcore(i).ext_call_fast));
                ccode = signal_processor(i, sigp_external_call);
        }
}

/*
 * cycles through all the cpus,
 * returns early if info is not NULL & the processor has something
 * of intrest to report in the info structure.
 * it returns the next cpu to check if it returns early.
 * i.e. it should be used as follows if you wish to receive info.
 * next_cpu=0;
 * do
 * {
 *    info->cpu=next_cpu;
 *    next_cpu=smp_signal_others(order_code,parameter,1,info);
 *    ... check info here
 * } while(next_cpu<=smp_num_cpus)
 *
 *  if you are lazy just use it like
 * smp_signal_others(order_code,parameter,0,1,NULL);
 */
int smp_signal_others(sigp_order_code order_code, u32 parameter,
                      int spin, sigp_info *info)
{
        sigp_ccode   ccode;
        u32          dummy;
        u16          i;

        if (info)
                info->intresting = 0;
        for (i = (info ? info->cpu : 0); i < smp_num_cpus; i++) {
                if (smp_processor_id() != i) {
                        do {
                                ccode = signal_processor_ps(
                                        (info ? &info->status : &dummy),
                                        parameter, i, order_code);
                        } while(spin && ccode == sigp_busy);
                        if (info && ccode != sigp_order_code_accepted) {
                                info->intresting = 1;
                                info->cpu = i;
                                info->ccode = ccode;
                                i++;
                                break;
                        }
                }
        }
        return i;
}

/*
 * this function sends a 'stop' sigp to all other CPUs in the system.
 * it goes straight through.
 */

void smp_send_stop(void)
{
        int i;
        u32 dummy;
        unsigned long low_core_addr;

        /* write magic number to zero page (absolute 0) */

        get_cpu_lowcore(smp_processor_id()).panic_magic = __PANIC_MAGIC;

        /* stop all processors */

        smp_signal_others(sigp_stop, 0, 1, NULL);

        /* store status of all processors in their lowcores (real 0) */

        for (i =  0; i < smp_num_cpus; i++) {
                if (smp_processor_id() != i) {
                        int ccode;
                        low_core_addr = (unsigned long)&get_cpu_lowcore(i);
                        do {
                                ccode = signal_processor_ps(
                                   &dummy,
                                   low_core_addr,
                                   i,
                                   sigp_store_status_at_address);
                        } while(ccode == sigp_busy);
                }
        }
}

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
	__u64 orvals[16];
	__u64 andvals[16];
} ec_creg_mask_parms;

/*
 * callback for setting/clearing control bits
 */
void smp_ctl_bit_callback(void *info) {
	ec_creg_mask_parms *pp;
	u64 cregs[16];
	int i;
	
	pp = (ec_creg_mask_parms *) info;
	asm volatile ("   bras  1,0f\n"
		      "   stctg 0,0,0(%0)\n"
		      "0: ex    %1,0(1)\n"
		      : : "a" (cregs+pp->start_ctl),
		          "a" ((pp->start_ctl<<4) + pp->end_ctl)
		      : "memory", "1" );
	for (i = pp->start_ctl; i <= pp->end_ctl; i++)
		cregs[i] = (cregs[i] & pp->andvals[i]) | pp->orvals[i];
	asm volatile ("   bras  1,0f\n"
		      "   lctlg 0,0,0(%0)\n"
		      "0: ex    %1,0(1)\n"
		      : : "a" (cregs+pp->start_ctl),
		          "a" ((pp->start_ctl<<4) + pp->end_ctl)
		      : "memory", "1" );
}

/*
 * Set a bit in a control register of all cpus
 */
void smp_ctl_set_bit(int cr, int bit) {
        ec_creg_mask_parms parms;

        if (atomic_read(&smp_commenced) != 0) {
                parms.start_ctl = cr;
                parms.end_ctl = cr;
                parms.orvals[cr] = 1 << bit;
                parms.andvals[cr] = -1L;
                smp_ext_call_others(smp_ctl_bit_callback, &parms, 1);
        }
        __ctl_set_bit(cr, bit);
}

/*
 * Clear a bit in a control register of all cpus
 */
void smp_ctl_clear_bit(int cr, int bit) {
        ec_creg_mask_parms parms;

        if (atomic_read(&smp_commenced) != 0) {
                parms.start_ctl = cr;
                parms.end_ctl = cr;
                parms.orvals[cr] = 0;
                parms.andvals[cr] = ~(1L << bit);
                smp_ext_call_others(smp_ctl_bit_callback, &parms, 1);
        }
        __ctl_clear_bit(cr, bit);
}

/*
 * Call a function on all other processors
 */

int
smp_call_function(void (*func)(void *info), void *info, int retry, int wait)
/*
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <retry> currently unused.
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler, you may call it from a bottom half handler.
 */
{
        if (atomic_read(&smp_commenced) != 0)
                smp_ext_call_others(func, info, 1);
        (func)(info);
        return 0;
}

/*
 * Lets check how many CPUs we have.
 */

void smp_count_cpus(void)
{
        int curr_cpu;

        current->processor = 0;
        smp_num_cpus = 1;
        for (curr_cpu = 0;
             curr_cpu <= 65535 && smp_num_cpus < max_cpus; curr_cpu++) {
                if ((__u16) curr_cpu == boot_cpu_addr)
                        continue;
                __cpu_logical_map[smp_num_cpus] = (__u16) curr_cpu;
                if (signal_processor(smp_num_cpus, sigp_sense) ==
                    sigp_not_operational)
                        continue;
                smp_num_cpus++;
        }
        printk("Detected %d CPU's\n",(int) smp_num_cpus);
        printk("Boot cpu address %2X\n", boot_cpu_addr);
}


/*
 *      Activate a secondary processor.
 */
extern void init_100hz_timer(void);

int __init start_secondary(void *cpuvoid)
{
        /* Setup the cpu */
        cpu_init();
        /* Print info about this processor */
        print_cpu_info(&safe_get_cpu_lowcore(smp_processor_id()).cpu_data);
        /* Wait for completion of smp startup */
        while (!atomic_read(&smp_commenced))
                /* nothing */ ;
        /* init per CPU 100 hz timer */
        init_100hz_timer();
        /* cpu_idle will call schedule for us */
        return cpu_idle(NULL);
}

/*
 * The restart interrupt handler jumps to start_secondary directly
 * without the detour over initialize_secondary. We defined it here
 * so that the linker doesn't complain.
 */
void __init initialize_secondary(void)
{
}

static int __init fork_by_hand(void)
{
       struct pt_regs regs;
       /* don't care about the psw and regs settings since we'll never
          reschedule the forked task. */
       memset(&regs,0,sizeof(struct pt_regs));
       return do_fork(CLONE_VM|CLONE_PID, 0, &regs, 0);
}

static void __init do_boot_cpu(int cpu)
{
        struct task_struct *idle;
        struct _lowcore    *cpu_lowcore;

        /* We can't use kernel_thread since we must _avoid_ to reschedule
           the child. */
        if (fork_by_hand() < 0)
                panic("failed fork for CPU %d", cpu);

        /*
         * We remove it from the pidhash and the runqueue
         * once we got the process:
         */
        idle = init_task.prev_task;
        if (!idle)
                panic("No idle process for CPU %d",cpu);
        idle->processor = cpu;
        idle->has_cpu = 1; /* we schedule the first task manually */

        del_from_runqueue(idle);
        unhash_process(idle);
        init_tasks[cpu] = idle;

        cpu_lowcore=&get_cpu_lowcore(cpu);
        cpu_lowcore->kernel_stack=idle->thread.ksp;
        __asm__ __volatile__("stctg 0,15,%0\n\t"
                             "stam  0,15,%1"
                             : "=m" (cpu_lowcore->cregs_save_area[0]),
                               "=m" (cpu_lowcore->access_regs_save_area[0])
                             : : "memory");

        eieio();
        signal_processor(cpu,sigp_restart);
}

/*
 *      Architecture specific routine called by the kernel just before init is
 *      fired off. This allows the BP to have everything in order [we hope].
 *      At the end of this all the APs will hit the system scheduling and off
 *      we go. Each AP will load the system gdt's and jump through the kernel
 *      init into idle(). At this point the scheduler will one day take over
 *      and give them jobs to do. smp_callin is a standard routine
 *      we use to track CPUs as they power up.
 */

void __init smp_commence(void)
{
        /*
         *      Lets the callins below out of their loop.
         */
        atomic_set(&smp_commenced,1);
}

/*
 *	Cycle through the processors sending restart sigps to boot each.
 */

void __init smp_boot_cpus(void)
{
        struct _lowcore *curr_lowcore;
        sigp_ccode   ccode;
        int i;

        /* request the 0x1202 external interrupt */
        if (register_external_interrupt(0x1202, do_ext_call_interrupt) != 0)
                panic("Couldn't request external interrupt 0x1202");
        smp_count_cpus();
        memset(lowcore_ptr,0,sizeof(lowcore_ptr));  
        
        /*
         *      Initialize the logical to physical CPU number mapping
         *      and the per-CPU profiling counter/multiplier
         */
        
        for (i = 0; i < NR_CPUS; i++) {
                prof_counter[i] = 1;
                prof_old_multiplier[i] = 1;
                prof_multiplier[i] = 1;
        }

        print_cpu_info(&safe_get_cpu_lowcore(0).cpu_data);

        for(i = 0; i < smp_num_cpus; i++)
        {
                curr_lowcore = (struct _lowcore *)
                                    __get_free_pages(GFP_KERNEL|GFP_DMA, 1);
                if (curr_lowcore == NULL) {
                        printk("smp_boot_cpus failed to allocate prefix memory\n");
                        break;
                }
                lowcore_ptr[i] = curr_lowcore;
                memcpy(curr_lowcore, &S390_lowcore, sizeof(struct _lowcore));
                /*
                 * Most of the parameters are set up when the cpu is
                 * started up.
                 */
                if (smp_processor_id() == i)
                        set_prefix((u32)(u64)curr_lowcore);
                else {
                        ccode = signal_processor_p((u64)(curr_lowcore),
                                                   i, sigp_set_prefix);
                        if(ccode) {
                                /* if this gets troublesome I'll have to do
                                 * something about it. */
                                printk("ccode %d for cpu %d  returned when "
                                       "setting prefix in smp_boot_cpus not good.\n",
                                       (int) ccode, (int) i);
                        }
                        else
                                do_boot_cpu(i);
                }
        }
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

/*
 * Local timer interrupt handler. It does both profiling and
 * process statistics/rescheduling.
 *
 * We do profiling in every local tick, statistics/rescheduling
 * happen only every 'profiling multiplier' ticks. The default
 * multiplier is 1 and it can be changed by writing the new multiplier
 * value into /proc/profile.
 */

void smp_local_timer_interrupt(struct pt_regs * regs)
{
	int user = (user_mode(regs) != 0);
        int cpu = smp_processor_id();

        /*
         * The profiling function is SMP safe. (nothing can mess
         * around with "current", and the profiling counters are
         * updated with atomic operations). This is especially
         * useful with a profiling multiplier != 1
         */
        if (!user_mode(regs))
                s390_do_profile(regs->psw.addr);

        if (!--prof_counter[cpu]) {
                int system = 1-user;
                struct task_struct * p = current;

                /*
                 * The multiplier may have changed since the last time we got
                 * to this point as a result of the user writing to
                 * /proc/profile.  In this case we need to adjust the APIC
                 * timer accordingly.
                 *
                 * Interrupts are already masked off at this point.
                 */
                prof_counter[cpu] = prof_multiplier[cpu];
                if (prof_counter[cpu] != prof_old_multiplier[cpu]) {
                  prof_old_multiplier[cpu] = prof_counter[cpu];
                }

                /*
                 * After doing the above, we need to make like
                 * a normal interrupt - otherwise timer interrupts
                 * ignore the global interrupt lock, which is the
                 * WrongThing (tm) to do.
                 */

                irq_enter(cpu, 0);
		update_process_times(user);
                irq_exit(cpu, 0);
        }
}

EXPORT_SYMBOL(lowcore_ptr);
EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(smp_ctl_set_bit);
EXPORT_SYMBOL(smp_ctl_clear_bit);
EXPORT_SYMBOL(smp_num_cpus);
