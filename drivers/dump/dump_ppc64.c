/*
 * Architecture specific (ppc64) functions for Linux crash dumps.
 *
 * Created by: Matt Robinson (yakker@sgi.com)
 *
 * Copyright 1999 Silicon Graphics, Inc. All rights reserved.
 * 
 * 2.3 kernel modifications by: Matt D. Robinson (yakker@turbolinux.com)
 * Copyright 2000 TurboLinux, Inc.  All rights reserved.
 * Copyright 2003, 2004 IBM Corporation
 * 
 * This code is released under version 2 of the GNU GPL.
 */

/*
 * The hooks for dumping the kernel virtual memory to disk are in this
 * file.  Any time a modification is made to the virtual memory mechanism,
 * these routines must be changed to use the new mechanisms.
 */
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/dump.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/syscalls.h> 
#include <asm/hardirq.h>
#include "dump_methods.h"
#include <linux/irq.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/page.h>
#if defined(CONFIG_KDB) && !defined(CONFIG_DUMP_MODULE)
#include <linux/kdb.h>
#endif

extern cpumask_t irq_affinity[];

static cpumask_t saved_affinity[NR_IRQS];

static __s32         saved_irq_count;   /* saved preempt_count() flags */

static int alloc_dha_stack(void)
{
        int i;
        void *ptr;

        if (dump_header_asm.dha_stack[0])
                return 0;

        ptr = (void *)vmalloc(THREAD_SIZE * num_possible_cpus());
        if (!ptr) {
                return -ENOMEM;
        }

        for (i = 0; i < num_possible_cpus(); i++) {
                dump_header_asm.dha_stack[i] = 
			(uint64_t)((unsigned long)ptr + (i * THREAD_SIZE));
	}
	return 0;
}

static int free_dha_stack(void)
{
        if (dump_header_asm.dha_stack[0]) {
                vfree((void*)dump_header_asm.dha_stack[0]);
		dump_header_asm.dha_stack[0] = 0;
	}
        return 0;
}
#ifdef CONFIG_SMP
static int dump_expect_ipi[NR_CPUS];
static atomic_t waiting_for_dump_ipi;

extern void stop_this_cpu(void *);
static int
dump_ipi_handler(struct pt_regs *regs) 
{
	int cpu = smp_processor_id();

	if (!dump_expect_ipi[cpu])
		return 0;
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

/* save registers on other processors
 * If the other cpus don't respond we simply do not get their states.
 */
void 
__dump_save_other_cpus(void)
{
	int i, cpu = smp_processor_id();
	int other_cpus = num_online_cpus()-1;
	
	if (other_cpus > 0) {
		atomic_set(&waiting_for_dump_ipi, other_cpus);
		for (i = 0; i < NR_CPUS; i++)
			dump_expect_ipi[i] = (i != cpu && cpu_online(i));

		printk("LKCD: sending IPI to other cpus...\n");
		dump_send_ipi(dump_ipi_handler);
		/*
		 * may be we dont need to wait for IPI to be processed.
		 * just write out the header at the end of dumping, if
		 * this IPI is not processed until then, there probably
		 * is a problem and we just fail to capture state of
		 * other cpus.
		 * However, we will wait 10 secs for other CPUs to respond. 
		 * If not, proceed the dump process even though we failed
		 * to capture other CPU states. 
		 */
		i = 10000; /* wait max of 10 seconds */
		while ((atomic_read(&waiting_for_dump_ipi) > 0) && (--i > 0)) {
			barrier();
			mdelay(1);
		} 
		printk("LKCD: done waiting: %d cpus not responding\n",
		       atomic_read(&waiting_for_dump_ipi));
		dump_send_ipi(NULL);	/* clear handler */
	}
}

/*
 * Restore old irq affinities.
 */
static void
__dump_reset_irq_affinity(void)
{
	int i;
	irq_desc_t *irq_d;

	memcpy(irq_affinity, saved_affinity, NR_IRQS * sizeof(unsigned long));

	for_each_irq(i) {
		irq_d = get_irq_desc(i);
		if (irq_d->handler == NULL) {
			continue;
		}
		if (irq_d->handler->set_affinity != NULL) {
			irq_d->handler->set_affinity(i, saved_affinity[i]);
		}
	}
}

/*
 * Routine to save the old irq affinities and change affinities of all irqs to
 * the dumping cpu.
 *
 * NB: Need to be expanded to multiple nodes.
 */
static void
__dump_set_irq_affinity(void)
{
	int i;
	cpumask_t cpu = CPU_MASK_NONE;
	irq_desc_t *irq_d;

	cpu_set(smp_processor_id(), cpu);

	memcpy(saved_affinity, irq_affinity, NR_IRQS * sizeof(unsigned long));

	for_each_irq(i) {
		irq_d = get_irq_desc(i);
		if (irq_d->handler == NULL) {
			continue;
		}
		irq_affinity[i] = cpu;
		if (irq_d->handler->set_affinity != NULL) {
			irq_d->handler->set_affinity(i, irq_affinity[i]);
		}
	}
}
#else /* !CONFIG_SMP */
#define __dump_save_other_cpus() do { } while (0)
#define __dump_set_irq_affinity()      do { } while (0)
#define __dump_reset_irq_affinity()    do { } while (0)
#endif /* !CONFIG_SMP */

void
__dump_save_regs(struct pt_regs *dest_regs, const struct pt_regs *regs)
{
	if (regs) {
		memcpy(dest_regs, regs, sizeof(struct pt_regs));
	} 
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

/*
 * Name: __dump_kernel_addr()
 * Func: Return physical kernel load address.
 */
unsigned long
__dump_kernel_addr(void)
{
	return 0 /* FIXME */ ;
}

/*
 * Name: __dump_configure_header()
 * Func: Configure the dump header with all proper values.
 */
int
__dump_configure_header(const struct pt_regs *regs)
{
	return (0);
}

#if defined(CONFIG_KDB) && !defined(CONFIG_DUMP_MODULE)
int
kdb_sysdump(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	kdb_printf("Dumping to disk...\n");
	dump("dump from kdb", regs);
	kdb_printf("Dump Complete\n");
	return 0;
}
#endif

/*
 * Name: __dump_init()
 * Func: Initialize the dumping routine process.  This is in case
 *       it's necessary in the future.
 */
void
__dump_init(uint64_t local_memory_start)
{
#if defined(FIXME) && defined(CONFIG_KDB) && !defined(CONFIG_DUMP_MODULE)
	/* This won't currently work because interrupts are off in kdb
	 * and the dump process doesn't understand how to recover.
	 */
	/* ToDo: add a command to query/set dump configuration */
	kdb_register_repeat("sysdump", kdb_sysdump, "", "use lkcd to dump the system to disk (if configured)", 0, KDB_REPEAT_NONE);
#endif

	/* return */
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
 * This is used for dump methods that require interrupts
 * Eventually, all methods will have interrupts disabled
 * and this code can be removed.
 *
 * Change irq affinities
 * Re-enable interrupts
 */
int
__dump_irq_enable(void)
{
	__dump_set_irq_affinity();
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
	__dump_reset_irq_affinity();
	irq_bh_restore(); 
}

#if 0
/* Cheap progress hack.  It estimates pages to write and
 * assumes all pages will go -- so it may get way off.
 * As the progress is not displayed for other architectures, not used at this 
 * moment.
 */
void
__dump_progress_add_page(void)
{
	unsigned long total_pages = nr_free_pages() + nr_inactive_pages + nr_active_pages;
	unsigned int percent = (dump_header.dh_num_dump_pages * 100) / total_pages;
	char buf[30];

	if (percent > last_percent && percent <= 100) {
		sprintf(buf, "Dump %3d%%     ", percent);
		ppc64_dump_msg(0x2, buf);
		last_percent = percent;
	}

}
#endif

extern int dump_page_is_ram(unsigned long);
/*
 * Name: __dump_page_valid()
 * Func: Check if page is valid to dump.
 */
int
__dump_page_valid(unsigned long index)
{
	if (!pfn_valid(index))
		return 0;

	return dump_page_is_ram(index);
}

/*
 * Name: manual_handle_crashdump()
 * Func: Interface for the lkcd dump command. Calls dump_execute()
 */
int
manual_handle_crashdump(void)
{
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
