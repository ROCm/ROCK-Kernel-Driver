/*
 * Architecture specific (i386) functions for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 *
 * Copyright 1999 Silicon Graphics, Inc. All rights reserved.
 *
 * 2.3 kernel modifications by: Matt D. Robinson (yakker@turbolinux.com)
 * Copyright 2000 TurboLinux, Inc.  All rights reserved.
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
#include <linux/mm.h>
#include <linux/dump.h>
#include "dump_methods.h"
#include <linux/irq.h>

#include <asm/processor.h>
#include <asm/e820.h>
#include <asm/hardirq.h>
#include <asm/nmi.h>

static __s32 	     saved_irq_count;	/* saved preempt_count() flags */


static int
alloc_dha_stack(void)
{
	int i;
	void *ptr;
	
	if (dump_header_asm.dha_stack[0])
		return 0;

	ptr = vmalloc(THREAD_SIZE * num_online_cpus());
	if (!ptr) {
		printk("vmalloc for dha_stacks failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_online_cpus(); i++) {
		dump_header_asm.dha_stack[i] = (u32)((unsigned long)ptr +
				(i * THREAD_SIZE));
	}
	return 0;
}

static int
free_dha_stack(void) 
{
	if (dump_header_asm.dha_stack[0]) {
		vfree((void *)dump_header_asm.dha_stack[0]);	
		dump_header_asm.dha_stack[0] = 0;
	}
	return 0;
}


void 
__dump_save_regs(struct pt_regs *dest_regs, const struct pt_regs *regs)
{
	*dest_regs = *regs;

	/* In case of panic dumps, we collects regs on entry to panic.
	 * so, we shouldn't 'fix' ssesp here again. But it is hard to
	 * tell just looking at regs whether ssesp need fixing. We make
	 * this decision by looking at xss in regs. If we have better
	 * means to determine that ssesp are valid (by some flag which
	 * tells that we are here due to panic dump), then we can use
	 * that instead of this kludge.
	 */
	if (!user_mode(regs)) {
		if ((0xffff & regs->xss) == __KERNEL_DS) 
			/* already fixed up */
			return;
		dest_regs->esp = (unsigned long)&(regs->esp);
		__asm__ __volatile__ ("movw %%ss, %%ax;"
			:"=a"(dest_regs->xss));
	}
}


#ifdef CONFIG_SMP
extern unsigned long irq_affinity[];
extern irq_desc_t irq_desc[];
extern void dump_send_ipi(void);

static int dump_expect_ipi[NR_CPUS];
static atomic_t waiting_for_dump_ipi;
static unsigned long saved_affinity[NR_IRQS];

extern void stop_this_cpu(void *); /* exported by i386 kernel */

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
	case DUMP_HARD_SPIN_CPUS:	/* Spin until dump is complete */
		while (dump_oncpu) {
			barrier();    	/* paranoia */
			if (dump_silence_level != DUMP_HARD_SPIN_CPUS)
				goto level_changed;

			cpu_relax();	/* kill time nicely */
		}
		break;

	case DUMP_HALT_CPUS:		/* Execute halt */
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
	int other_cpus = num_online_cpus()-1;
	
	if (other_cpus > 0) {
		atomic_set(&waiting_for_dump_ipi, other_cpus);

		for (i = 0; i < NR_CPUS; i++) {
			dump_expect_ipi[i] = (i != cpu && cpu_online(i));
		}

		/* short circuit normal NMI handling temporarily */
		set_nmi_callback(dump_nmi_callback);
		wmb();

		dump_send_ipi();
		/* may be we dont need to wait for NMI to be processed. 
		   just write out the header at the end of dumping, if
		   this IPI is not processed until then, there probably
		   is a problem and we just fail to capture state of 
		   other cpus. */
		while(atomic_read(&waiting_for_dump_ipi) > 0) {
			cpu_relax();
		}

		unset_nmi_callback();
	}
}

/*
 * Routine to save the old irq affinities and change affinities of all irqs to
 * the dumping cpu.
 */
static void 
set_irq_affinity(void)
{
	int i;
	int cpu = smp_processor_id();

	memcpy(saved_affinity, irq_affinity, NR_IRQS * sizeof(unsigned long));
	for (i = 0; i < NR_IRQS; i++) {
		if (irq_desc[i].handler == NULL)
			continue;
		irq_affinity[i] = 1UL << cpu;
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
 * Name: __dump_irq_enable
 * Func: Reset system so interrupts are enabled.
 *	 This is used for dump methods that require interrupts
 *	 Eventually, all methods will have interrupts disabled
 *	 and this code can be removed.
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
 * Name: __dump_configure_header()
 * Func: Meant to fill in arch specific header fields except per-cpu state
 * already captured via __dump_save_context for all CPUs.
 */
int
__dump_configure_header(const struct pt_regs *regs)
{
	return (0);
}

/*
 * Name: __dump_init()
 * Func: Initialize the dumping routine process.
 */
void
__dump_init(uint64_t local_memory_start)
{
	dump_mbanks = 1;
	dump_mbank[ 0 ].start = 0;
	dump_mbank[ 0 ].end  = (((u64) max_mapnr) << PAGE_SHIFT) - 1;
	dump_mbank[ 0 ].type = DUMP_MBANK_TYPE_CONVENTIONAL_MEMORY;
	return;
}

/*
 * Name: __dump_open()
 * Func: Open the dump device (architecture specific).
 */
void
__dump_open(void)
{
	alloc_dha_stack();
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
}

extern int pfn_is_ram(unsigned long);

/*
 * Name: __dump_page_valid()
 * Func: Check if page is valid to dump.
 */ 
int 
__dump_page_valid(unsigned long index)
{
	if (!pfn_valid(index))
		return 0;

	return pfn_is_ram(index);
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
