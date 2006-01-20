/*
 * Architecture specific (ia64) functions for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 * Contributions from SGI, IBM, and others.
 *
 * 2.4  kernel modifications by:  Matt D. Robinson (yakker@alacritech.com)
 * ia64 kernel modifications by: Piet Delaney (piet@www.piet.net)
 *
 * Copyright (C) 2001 - 2002 Matt D. Robinson (yakker@alacritech.com)
 * Copyright (C) 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2002 Free Software Foundation, Inc. All rights reserved.
 *
 * This code is released under version 2 of the GNU GPL.
 */

/*
 * The hooks for dumping the kernel virtual memory to disk are in this
 * file.  Any time a modification is made to the virtual memory mechanism,
 * these routines must be changed to use the new mechanisms.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/dump.h>
#include "dump_methods.h"
#include <linux/mm.h>
#include <asm/processor.h>
#include <asm-ia64/dump.h>
#include <asm/hardirq.h>
#include <linux/irq.h>
#include <linux/delay.h>

static __s32         saved_irq_count;   /* saved preempt_count() flags */


static int alloc_dha_stack(void)
{
	int i;
	void *ptr;
	
	if (dump_header_asm.dha_stack[0])
	{
		return 0;
	}
       	ptr = vmalloc(THREAD_SIZE * num_online_cpus());
       	if (!ptr) {
		printk("LKCD: vmalloc for dha_stacks failed\n");
		return -ENOMEM;
	}
	bzero(ptr,THREAD_SIZE );

	for (i = 0; i < num_online_cpus(); i++) {
		dump_header_asm.dha_stack[i] = (uint64_t)((unsigned long)ptr + (i * THREAD_SIZE));
	}
	return 0;
}

static int free_dha_stack(void) 
{
	if (dump_header_asm.dha_stack[0])
	{
		vfree((void*)dump_header_asm.dha_stack[0]);
		dump_header_asm.dha_stack[0] = 0;
	}
	return 0;
}

/* a structure to get arguments into the following callback routine */
struct unw_args {
	int cpu;
	struct task_struct *tsk;
};

static void
do_save_sw(struct unw_frame_info *info, void *arg)
{
	struct unw_args *uwargs = (struct unw_args *)arg;
	int cpu = uwargs->cpu;
	struct task_struct *tsk = uwargs->tsk;

	dump_header_asm.dha_stack_ptr[cpu] = (uint64_t)info->sw;

	if (tsk && dump_header_asm.dha_stack[cpu]) {
		memcpy((void *)dump_header_asm.dha_stack[cpu],
				STACK_START_POSITION(tsk),
				THREAD_SIZE);
	}
}

void
__dump_save_context(int cpu, const struct pt_regs *regs, 
	struct task_struct *tsk)
{
	struct unw_args uwargs;

	dump_header_asm.dha_smp_current_task[cpu] = (unsigned long)tsk;

	if (regs) {
		dump_header_asm.dha_smp_regs[cpu] = *regs;
	}

	/* save a snapshot of the stack in a nice state for unwinding */
	uwargs.cpu = cpu;
	uwargs.tsk = tsk;

	unw_init_running(do_save_sw, (void *)&uwargs);
}

#ifdef CONFIG_SMP

extern cpumask_t irq_affinity[];
extern irq_desc_t irq_desc[];
extern void dump_send_ipi(void);
static cpumask_t saved_affinity[NR_IRQS];

/*
 * Routine to save the old irq affinities and change affinities of all irqs to
 * the dumping cpu.
 */
static void
set_irq_affinity(void)
{
        int i;
	cpumask_t cpu = CPU_MASK_NONE;

	cpu_set(smp_processor_id(), cpu);
        memcpy(saved_affinity, irq_affinity, NR_IRQS * sizeof(unsigned long));
        for (i = 0; i < NR_IRQS; i++) {
                if (irq_desc[i].handler == NULL)
                        continue;
		irq_affinity[i] = cpu;
                if (irq_desc[i].handler->set_affinity != NULL)
                        irq_desc[i].handler->set_affinity(i, irq_affinity[i]);
        }
}

/*
 * Restore old irq affinities.
 */
static void
reset_irq_affinity(void)
{
        int i;

        memcpy(irq_affinity, saved_affinity, NR_IRQS * sizeof(unsigned long));
        for (i = 0; i < NR_IRQS; i++) {
                if (irq_desc[i].handler == NULL)
                        continue;
                if (irq_desc[i].handler->set_affinity != NULL)
                        irq_desc[i].handler->set_affinity(i, saved_affinity[i]);
        }
}

#else /* !CONFIG_SMP */
#define set_irq_affinity()      do { } while (0)
#define reset_irq_affinity()    do { } while (0)
#define save_other_cpu_states() do { } while (0)
#endif /* !CONFIG_SMP */

#ifdef CONFIG_SMP
static int dump_expect_ipi[NR_CPUS];
static atomic_t waiting_for_dump_ipi;
static int wait_for_dump_ipi = 2000; /* wait 2000 ms for ipi to be handled */
extern void (*dump_trace_ptr)(struct pt_regs *);


extern void stop_this_cpu(void);

static int
dump_nmi_callback(struct pt_regs *regs, int cpu)
{
        if (!dump_expect_ipi[cpu])
                return 0;

        dump_expect_ipi[cpu] = 0;

        dump_save_this_cpu(regs);
        atomic_dec(&waiting_for_dump_ipi);

 level_changed:
        switch (dump_silence_level) {
        case DUMP_HARD_SPIN_CPUS:       /* Spin until dump is complete */
                while (dump_oncpu) {
                        barrier();      /* paranoia */
                        if (dump_silence_level != DUMP_HARD_SPIN_CPUS)
                                goto level_changed;

                        cpu_relax();    /* kill time nicely */
                }
                break;

        case DUMP_HALT_CPUS:            /* Execute halt */
                stop_this_cpu();
                break;

        case DUMP_SOFT_SPIN_CPUS:
                /* Mark the task so it spins in schedule */
                set_tsk_thread_flag(current, TIF_NEED_RESCHED);
                break;
        }

        return 1;
}

int IPI_handler(struct pt_regs *regs)
{
	int cpu;
	cpu = task_cpu(current);
	return(dump_nmi_callback(regs, cpu));
}

/* save registers on other processors */
void
__dump_save_other_cpus(void)
{
        int i, cpu = smp_processor_id();
        int other_cpus = num_online_cpus()-1;
	int wait_time = wait_for_dump_ipi;

        if (other_cpus > 0) {
                atomic_set(&waiting_for_dump_ipi, other_cpus);

                for (i = 0; i < NR_CPUS; i++) {
                        dump_expect_ipi[i] = (i != cpu && cpu_online(i));
                }

		dump_ipi_function_ptr = IPI_handler;
		
                wmb();

                dump_send_ipi();
                /* may be we dont need to wait for IPI to be processed.
                 * just write out the header at the end of dumping, if
                 * this IPI is not processed until then, there probably
                 * is a problem and we just fail to capture state of
                 * other cpus. */
                while(wait_time-- && (atomic_read(&waiting_for_dump_ipi) > 0)) {
			barrier();
			mdelay(1);
                }
		if (wait_time <= 0) {
			printk("LKCD: dump ipi timeout, proceeding...\n");
		}
        }
}
#endif
/*
 * Kludge - dump from interrupt context is unreliable (Fixme)
 *
 * We do this so that softirqs initiated for dump i/o
 * get processed and we don't hang while waiting for i/o
 * to complete or in any irq synchronization attempt.
 *
 * This is not quite legal of course, as it has the side
 * effect of making all interrupts & softirqs triggered
 * while dump is in progress complete before currently
 * pending softirqs and the currently executing interrupt
 * code.
 */
static inline void
irq_bh_save(void)
{
        saved_irq_count = irq_count();
        preempt_count() &= ~(HARDIRQ_MASK|SOFTIRQ_MASK);
}

static inline void
irq_bh_restore(void)
{
        preempt_count() |= saved_irq_count;
}

/*
 * Name: __dump_kernel_addr()
 * Func: Return physical kernel load address.
 */
unsigned long
__dump_kernel_addr(void)
{
	return ia64_tpa(KERNEL_START);
}

/*
 * Name: __dump_configure_header()
 * Func: Configure the dump header with all proper values.
 */
int
__dump_configure_header(const struct pt_regs *regs)
{
	dump_header_asm.dha_kernel_addr = __dump_kernel_addr();
	return (0);
}


#define dim(x) (sizeof(x)/sizeof(*(x)))

/*
 * Name: __dump_irq_enable
 * Func: Reset system so interrupts are enabled.
 *       This is used for dump methods that require interrupts
 *       Eventually, all methods will have interrupts disabled
 *       and this code can be removed.
 *
 *     Change irq affinities
 *     Re-enable interrupts
 */
int
__dump_irq_enable(void)
{
        set_irq_affinity();
        irq_bh_save();
	ia64_srlz_d();
	/* 
	 * reduce the task priority level
  	 * to get disk interrupts
	 */
	ia64_setreg(_IA64_REG_CR_TPR, 0);
	ia64_srlz_d();
        local_irq_enable();
	return 0;
}

/*
 * Name: __dump_irq_restore
 * Func: Resume the system state in an architecture-specific way.

 */
void
__dump_irq_restore(void)
{
        local_irq_disable();
        reset_irq_affinity();
        irq_bh_restore();
}

/*
 * Name: __dump_page_valid()
 * Func: Check if page is valid to dump.
 */
int
__dump_page_valid(unsigned long index)
{
        if (!pfn_valid(index))
	{
                return 0;
	}
        return 1;
}

/*
 * Name: __dump_init()
 * Func: Initialize the dumping routine process.  This is in case
 *       it's necessary in the future.
 */
void
__dump_init(uint64_t local_memory_start)
{
	return;
}

/*
 * Name: __dump_open()
 * Func: Open the dump device (architecture specific).  This is in
 *       case it's necessary in the future.
 */
void
__dump_open(void)
{
	alloc_dha_stack();
	return;
}


/*
 * Name: __dump_cleanup()
 * Func: Free any architecture specific data structures. This is called
 *       when the dump module is being removed.
 */
void
__dump_cleanup(void)
{
	free_dha_stack();

	return;
}



int __dump_memcpy_mc_expected = 0;		/* Doesn't help yet */

/*
 * An ia64 version of memcpy() that trys to avoid machine checks.
 *
 * NB: 
 * 	By itself __dump_memcpy_mc_expected() ins't providing any
 *	protection against Machine Checks. We are looking into the
 *	possability of adding code to the arch/ia64/kernel/mca.c fuction
 *	ia64_mca_ucmc_handler() to restore state so that a IA64_MCA_CORRECTED
 *	can be returned to the firmware. Curently it always returns 
 *	IA64_MCA_COLD_BOOT and reboots the machine.
 */
/*
void * __dump_memcpy(void * dest, const void *src, size_t count)
{
	void *vp;

	if (__dump_memcpy_mc_expected) {
		ia64_pal_mc_expected((u64) 1, 0);
	}

	vp = memcpy(dest, src, count);

	if (__dump_memcpy_mc_expected) {
		ia64_pal_mc_expected((u64) 0, 0);
	}
	return(vp);
}
*/
/*
 * Name: manual_handle_crashdump()
 * Func: Interface for the lkcd dump command. Calls dump_execute()
 */
int
manual_handle_crashdump(void) {

        struct pt_regs regs;

        get_current_regs(&regs);
        dump_execute("manual", &regs);
        return 0;
}

/*
 * Name: __dump_clean_irq_state()
 * Func: Clean up from the previous IRQ handling state. Such as oops from 
 *       interrupt handler or bottom half.
 */
void
__dump_clean_irq_state(void)
{
    	unsigned long saved_tpr;
	unsigned long TPR_MASK = 0xFFFFFFFFFFFEFF0F;
	
	
	/* Get the processors task priority register */
	saved_tpr = ia64_getreg(_IA64_REG_CR_TPR);
	/* clear the mmi and mic bit's of the TPR to unmask interrupts */
	saved_tpr = saved_tpr & TPR_MASK; 
	ia64_setreg(_IA64_REG_CR_TPR, saved_tpr);
	ia64_srlz_d();

	/* Tell the processor we're done with the interrupt
	 * that got us here.
	 */
	
	ia64_eoi();

	/* local implementation of irq_exit(); */
	preempt_count() -= IRQ_EXIT_OFFSET;
	preempt_enable_no_resched();

	return;
}

