/*
 * Architecture specific (x86-64) functions for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 *
 * Copyright 1999 Silicon Graphics, Inc. All rights reserved.
 *
 * 2.3 kernel modifications by: Matt D. Robinson (yakker@turbolinux.com)
 * Copyright 2000 TurboLinux, Inc.  All rights reserved.
 *
 * x86-64 port Copyright 2002 Andi Kleen, SuSE Labs
 * x86-64 port Sachin Sant ( sachinp@in.ibm.com )
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
#include <linux/rcupdate.h>
#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/kdebug.h>
#include <asm/uaccess.h>
#include <asm/nmi.h>
#include <asm/kdebug.h>

static __s32 	saved_irq_count; /* saved preempt_count() flag */

void (*dump_trace_ptr)(struct pt_regs *);

static int alloc_dha_stack(void)
{
	int i;
	void *ptr;
	
	if (dump_header_asm.dha_stack[0])
		return 0;

       	ptr = vmalloc(THREAD_SIZE * num_online_cpus());
	if (!ptr) {
		printk("LKCD: vmalloc for dha_stacks failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_online_cpus(); i++) {
		dump_header_asm.dha_stack[i] = 
			(uint64_t)((unsigned long)ptr + (i * THREAD_SIZE));
	}
	return 0;
}

static int free_dha_stack(void) 
{
	if (dump_header_asm.dha_stack[0]) {
		vfree((void *)dump_header_asm.dha_stack[0]);	
		dump_header_asm.dha_stack[0] = 0;
	}	
	return 0;
}

void
__dump_save_regs(struct pt_regs* dest_regs, const struct pt_regs* regs)
{
	if (regs)
		memcpy(dest_regs, regs, sizeof(struct pt_regs));
}

void
__dump_save_context(int cpu, const struct pt_regs *regs, 
	struct task_struct *tsk)
{
	dump_header_asm.dha_smp_current_task[cpu] = (unsigned long)tsk;
	__dump_save_regs(&dump_header_asm.dha_smp_regs[cpu], regs);

	/* take a snapshot of the stack */
	/* doing this enables us to tolerate slight drifts on this cpu */

	if (dump_header_asm.dha_stack[cpu]) {
		memcpy((void *)dump_header_asm.dha_stack[cpu],
				STACK_START_POSITION(tsk),
				THREAD_SIZE);
	}
	dump_header_asm.dha_stack_ptr[cpu] = (unsigned long)(tsk->thread_info);
}

#ifdef CONFIG_SMP
extern cpumask_t irq_affinity[];
extern irq_desc_t irq_desc[];
extern void dump_send_ipi(void);
static int dump_expect_ipi[NR_CPUS];
static atomic_t waiting_for_dump_ipi;
static cpumask_t saved_affinity[NR_IRQS];

extern void stop_this_cpu(void *);

static int
dump_nmi_callback(struct pt_regs *regs, int cpu) 
{
	if (!dump_expect_ipi[cpu]) {
		return 0;
	}
	
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
                stop_this_cpu(NULL);
                break;

        case DUMP_SOFT_SPIN_CPUS:
                /* Mark the task so it spins in schedule */
                set_tsk_thread_flag(current, TIF_NEED_RESCHED);
                break;
        }

	return 1;
}

/* save registers on other processors */
void 
__dump_save_other_cpus(void) 
{
	int i, cpu = smp_processor_id();
	int other_cpus = num_online_cpus() - 1;

	if (other_cpus > 0) {
		atomic_set(&waiting_for_dump_ipi, other_cpus);

		for (i = 0; i < NR_CPUS; i++)
			dump_expect_ipi[i] = (i != cpu && cpu_online(i));
		
		set_nmi_callback(dump_nmi_callback);
		wmb();

		dump_send_ipi();

		/* may be we dont need to wait for NMI to be processed. 
		   just write out the header at the end of dumping, if
		   this IPI is not processed untill then, there probably
		   is a problem and we just fail to capture state of 
		   other cpus. */
		while(atomic_read(&waiting_for_dump_ipi) > 0)
			cpu_relax();

		unset_nmi_callback();
	}
	return;
}

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
#define set_irq_affinity()	do { } while (0)
#define reset_irq_affinity()	do { } while (0)
#define save_other_cpu_states() do { } while (0)
#endif /* !CONFIG_SMP */

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
        local_irq_enable();
	return 0;
}

/*
 * Name: __dump_irq_restore
 * Func: Resume the system state in an architecture-speeific way.
 *
 */
void
__dump_irq_restore(void)
{
        local_irq_disable();
        reset_irq_affinity();
        irq_bh_restore();
}

/*
 * Name: __dump_kernel_addr()
 * Func: Return physical kernel load address.
 */
unsigned long
__dump_kernel_addr(void)
{
	/* Unsatisfactory - arch/x86_64/kernel/vmlinux.lds.S hard codes the
	 * offset to the start of the text.  It should be a global arch
	 * specific define.
	 */
	return virt_to_phys((void *)0xffffffff80100000UL);
}

/*
 * Name: __dump_configure_header()
 * Func: Configure the dump header with all proper values.
 */
int
__dump_configure_header(const struct pt_regs *regs)
{
	/* Dummy function - return */
	return (0);
}

static int notify(struct notifier_block *nb, unsigned long code, void *data)
{
	if (code == DIE_NMI_IPI && dump_oncpu)
		return NOTIFY_BAD; 
	return NOTIFY_DONE; 
} 

static struct notifier_block dump_notifier = { 
	.notifier_call = notify,	
}; 

/*
 * Name: __dump_init()
 * Func: Initialize the dumping routine process.
 */
void
__dump_init(uint64_t local_memory_start)
{
	notifier_chain_register(&die_chain, &dump_notifier);
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
	/* return */
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
	notifier_chain_unregister(&die_chain, &dump_notifier);
	synchronize_kernel(); 
	return;
}

extern int page_is_ram(unsigned long);

/*
 * Name: __dump_page_valid()
 * Func: Check if page is valid to dump.
 */
int
__dump_page_valid(unsigned long index)
{
	if (!pfn_valid(index))
		return 0;

	return page_is_ram(index);
}

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
    return;
}
