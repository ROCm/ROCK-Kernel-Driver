/*
 * TLB support routines.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 08/02/00 A. Mallick <asit.k.mallick@intel.com>	
 *		Modified RID allocation for SMP 
 *          Goutham Rao <goutham.rao@intel.com>
 *              IPI based ptc implementation and A-step IPI implementation.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>

#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/pal.h>
#include <asm/delay.h>

#define SUPPORTED_PGBITS (			\
		1 << _PAGE_SIZE_256M |		\
		1 << _PAGE_SIZE_64M  |		\
		1 << _PAGE_SIZE_16M  |		\
		1 << _PAGE_SIZE_4M   |		\
		1 << _PAGE_SIZE_1M   |		\
		1 << _PAGE_SIZE_256K |		\
		1 << _PAGE_SIZE_64K  |		\
		1 << _PAGE_SIZE_16K  |		\
		1 << _PAGE_SIZE_8K   |		\
		1 << _PAGE_SIZE_4K )

struct ia64_ctx ia64_ctx = {
	lock:	SPIN_LOCK_UNLOCKED,
	next:	1,
	limit:	(1 << 15) - 1,		/* start out with the safe (architected) limit */
	max_ctx: ~0U
};

/*
 * Seralize usage of ptc.g 
 */
spinlock_t ptcg_lock = SPIN_LOCK_UNLOCKED; /* see <asm/pgtable.h> */

#if defined(CONFIG_SMP) && !defined(CONFIG_ITANIUM_PTCG)

#include <linux/irq.h>

unsigned long 	flush_end, flush_start, flush_nbits, flush_rid;
atomic_t flush_cpu_count;

/*
 * flush_tlb_no_ptcg is called with ptcg_lock locked
 */
static inline void
flush_tlb_no_ptcg (unsigned long start, unsigned long end, unsigned long nbits)
{
	extern void smp_send_flush_tlb (void);
	unsigned long saved_tpr = 0;
	unsigned long flags;

	/*
	 * Some times this is called with interrupts disabled and causes
	 * dead-lock; to avoid this we enable interrupt and raise the TPR
	 * to enable ONLY IPI.
	 */
	__save_flags(flags);
	if (!(flags & IA64_PSR_I)) {
		saved_tpr = ia64_get_tpr();
		ia64_srlz_d();
		ia64_set_tpr(IPI_IRQ - 16);
		ia64_srlz_d();
		local_irq_enable();
	}

	spin_lock(&ptcg_lock);
	flush_rid = ia64_get_rr(start);
	ia64_srlz_d();
	flush_start = start;
	flush_end = end;
	flush_nbits = nbits;
	atomic_set(&flush_cpu_count, smp_num_cpus - 1);
	smp_send_flush_tlb();
	/*
	 * Purge local TLB entries. ALAT invalidation is done in ia64_leave_kernel.
	 */
	do {
		asm volatile ("ptc.l %0,%1" :: "r"(start), "r"(nbits<<2) : "memory");
		start += (1UL << nbits);
	} while (start < end);

	ia64_srlz_i();			/* srlz.i implies srlz.d */

	/*
	 * Wait for other CPUs to finish purging entries.
	 */
#if (defined(CONFIG_ITANIUM_ASTEP_SPECIFIC) || defined(CONFIG_ITANIUM_BSTEP_SPECIFIC))
	{
		unsigned long start = ia64_get_itc();
		while (atomic_read(&flush_cpu_count) > 0) {
			if ((ia64_get_itc() - start) > 40000UL) {
				atomic_set(&flush_cpu_count, smp_num_cpus - 1);
				smp_send_flush_tlb();
				start = ia64_get_itc();
			}
		}
	}
#else
	while (atomic_read(&flush_cpu_count)) {
		/* Nothing */
	}
#endif
	if (!(flags & IA64_PSR_I)) {
		local_irq_disable();
		ia64_set_tpr(saved_tpr);
		ia64_srlz_d();
	}
}

#endif /* CONFIG_SMP && !CONFIG_ITANIUM_PTCG */

/*
 * Acquire the ia64_ctx.lock before calling this function!
 */
void
wrap_mmu_context (struct mm_struct *mm)
{
	unsigned long tsk_context, max_ctx = ia64_ctx.max_ctx;
	struct task_struct *tsk;

	if (ia64_ctx.next > max_ctx)
		ia64_ctx.next = 300;	/* skip daemons */
	ia64_ctx.limit = max_ctx + 1;

	/*
	 * Scan all the task's mm->context and set proper safe range
	 */

	read_lock(&tasklist_lock);
  repeat:
	for_each_task(tsk) {
		if (!tsk->mm)
			continue;
		tsk_context = tsk->mm->context;
		if (tsk_context == ia64_ctx.next) {
			if (++ia64_ctx.next >= ia64_ctx.limit) {
				/* empty range: reset the range limit and start over */
				if (ia64_ctx.next > max_ctx) 
					ia64_ctx.next = 300;
				ia64_ctx.limit = max_ctx + 1;
				goto repeat;
			}
		}
		if ((tsk_context > ia64_ctx.next) && (tsk_context < ia64_ctx.limit))
			ia64_ctx.limit = tsk_context;
	}
	read_unlock(&tasklist_lock);
	flush_tlb_all();
}

void
__flush_tlb_all (void)
{
	unsigned long i, j, flags, count0, count1, stride0, stride1, addr;

	addr    = my_cpu_data.ptce_base;
	count0  = my_cpu_data.ptce_count[0];
	count1  = my_cpu_data.ptce_count[1];
	stride0 = my_cpu_data.ptce_stride[0];
	stride1 = my_cpu_data.ptce_stride[1];

	local_irq_save(flags);
	for (i = 0; i < count0; ++i) {
		for (j = 0; j < count1; ++j) {
			asm volatile ("ptc.e %0" :: "r"(addr));
			addr += stride1;
		}
		addr += stride0;
	}
	local_irq_restore(flags);
	ia64_insn_group_barrier();
	ia64_srlz_i();			/* srlz.i implies srlz.d */
	ia64_insn_group_barrier();
}

void
flush_tlb_range (struct mm_struct *mm, unsigned long start, unsigned long end)
{
	unsigned long size = end - start;
	unsigned long nbits;

	if (mm != current->active_mm) {
		/* this does happen, but perhaps it's not worth optimizing for? */
#ifdef CONFIG_SMP
		flush_tlb_all();
#else
		mm->context = 0;
#endif
		return;
	}

	nbits = ia64_fls(size + 0xfff);
	if (((1UL << nbits) & SUPPORTED_PGBITS) == 0) {
		if (nbits > _PAGE_SIZE_256M)
			nbits = _PAGE_SIZE_256M;
		else
			/*
			 * Some page sizes are not implemented in the
			 * IA-64 arch, so if we get asked to clear an
			 * unsupported page size, round up to the
			 * nearest page size.  Note that we depend on
			 * the fact that if page size N is not
			 * implemented, 2*N _is_ implemented.
			 */
			++nbits;
		if (((1UL << nbits) & SUPPORTED_PGBITS) == 0)
			panic("flush_tlb_range: BUG: nbits=%lu\n", nbits);
	}
	start &= ~((1UL << nbits) - 1);

#if defined(CONFIG_SMP) && !defined(CONFIG_ITANIUM_PTCG)
	flush_tlb_no_ptcg(start, end, nbits);
#else
	spin_lock(&ptcg_lock);
	do {
# ifdef CONFIG_SMP
		/*
		 * Flush ALAT entries also.
		 */
		asm volatile ("ptc.ga %0,%1;;srlz.i;;" :: "r"(start), "r"(nbits<<2) : "memory");
# else
		asm volatile ("ptc.l %0,%1" :: "r"(start), "r"(nbits<<2) : "memory");
# endif
		start += (1UL << nbits);
	} while (start < end);
#endif /* CONFIG_SMP && !defined(CONFIG_ITANIUM_PTCG) */
	spin_unlock(&ptcg_lock);
	ia64_insn_group_barrier();
	ia64_srlz_i();			/* srlz.i implies srlz.d */
	ia64_insn_group_barrier();
}

void __init
ia64_tlb_init (void)
{
	ia64_ptce_info_t ptce_info;

	ia64_get_ptce(&ptce_info);
	my_cpu_data.ptce_base = ptce_info.base;
	my_cpu_data.ptce_count[0] = ptce_info.count[0];
	my_cpu_data.ptce_count[1] = ptce_info.count[1];
	my_cpu_data.ptce_stride[0] = ptce_info.stride[0];
	my_cpu_data.ptce_stride[1] = ptce_info.stride[1];

	__flush_tlb_all();		/* nuke left overs from bootstrapping... */
}
