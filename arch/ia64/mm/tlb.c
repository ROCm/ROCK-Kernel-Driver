/*
 * TLB support routines.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
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

static inline void
ia64_global_tlb_purge (unsigned long start, unsigned long end, unsigned long nbits)
{
	static spinlock_t ptcg_lock = SPIN_LOCK_UNLOCKED;

	/* HW requires global serialization of ptc.ga.  */
	spin_lock(&ptcg_lock);
	{
		do {
			/*
			 * Flush ALAT entries also.
			 */
			asm volatile ("ptc.ga %0,%1;;srlz.i;;" :: "r"(start), "r"(nbits<<2)
				      : "memory");
			start += (1UL << nbits);
		} while (start < end);
	}
	spin_unlock(&ptcg_lock);
}

void
__flush_tlb_all (void)
{
	unsigned long i, j, flags, count0, count1, stride0, stride1, addr;

	addr    = local_cpu_data->ptce_base;
	count0  = local_cpu_data->ptce_count[0];
	count1  = local_cpu_data->ptce_count[1];
	stride0 = local_cpu_data->ptce_stride[0];
	stride1 = local_cpu_data->ptce_stride[1];

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

# ifdef CONFIG_SMP
	platform_global_tlb_purge(start, end, nbits);
# else
	do {
		asm volatile ("ptc.l %0,%1" :: "r"(start), "r"(nbits<<2) : "memory");
		start += (1UL << nbits);
	} while (start < end);
# endif

	ia64_insn_group_barrier();
	ia64_srlz_i();			/* srlz.i implies srlz.d */
	ia64_insn_group_barrier();
}

void __init
ia64_tlb_init (void)
{
	ia64_ptce_info_t ptce_info;

	ia64_get_ptce(&ptce_info);
	local_cpu_data->ptce_base = ptce_info.base;
	local_cpu_data->ptce_count[0] = ptce_info.count[0];
	local_cpu_data->ptce_count[1] = ptce_info.count[1];
	local_cpu_data->ptce_stride[0] = ptce_info.stride[0];
	local_cpu_data->ptce_stride[1] = ptce_info.stride[1];

	__flush_tlb_all();		/* nuke left overs from bootstrapping... */
}
