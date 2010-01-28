/******************************************************************************
 * mm/hypervisor.c
 * 
 * Update page tables via the hypervisor.
 * 
 * Copyright (c) 2002-2004, K A Fraser
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/hypervisor.h>
#include <xen/balloon.h>
#include <xen/features.h>
#include <xen/interface/memory.h>
#include <xen/interface/vcpu.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/highmem.h>
#include <asm/tlbflush.h>
#include <linux/highmem.h>

EXPORT_SYMBOL(hypercall_page);

shared_info_t *__read_mostly HYPERVISOR_shared_info = (shared_info_t *)empty_zero_page;
#ifndef CONFIG_XEN_VCPU_INFO_PLACEMENT
EXPORT_SYMBOL(HYPERVISOR_shared_info);
#else
DEFINE_PER_CPU(struct vcpu_info, vcpu_info) __aligned(sizeof(struct vcpu_info));
EXPORT_PER_CPU_SYMBOL(vcpu_info);

void __ref setup_vcpu_info(unsigned int cpu)
{
	struct vcpu_info *v = &per_cpu(vcpu_info, cpu);
	struct vcpu_register_vcpu_info info;
#ifdef CONFIG_X86_64
	static bool first = true;

	if (first) {
		first = false;
		info.mfn = early_arbitrary_virt_to_mfn(v);
	} else
#endif
		info.mfn = arbitrary_virt_to_mfn(v);
	info.offset = offset_in_page(v);

	if (HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info,
			       cpu, &info))
		BUG();
}

void __init adjust_boot_vcpu_info(void)
{
	unsigned long lpfn, rpfn, lmfn, rmfn;
	pte_t *lpte, *rpte;
	unsigned int level;
	mmu_update_t mmu[2];

	/*
	 * setup_vcpu_info() cannot be used more than once for a given (v)CPU,
	 * hence we must swap the underlying MFNs of the two pages holding old
	 * and new vcpu_info of the boot CPU.
	 *
	 * Do *not* use __get_cpu_var() or percpu_{write,...}() here, as the per-
	 * CPU segment didn't get reloaded yet. Using percpu_read(), as in
	 * arch_use_lazy_mmu_mode(), though undesirable, is safe except for the
	 * accesses to variables that were updated in setup_percpu_areas().
	 */
	lpte = lookup_address((unsigned long)&per_cpu_var(vcpu_info)
			      + (__per_cpu_load - __per_cpu_start),
			      &level);
	rpte = lookup_address((unsigned long)&per_cpu(vcpu_info, 0), &level);
	BUG_ON(!lpte || !(pte_flags(*lpte) & _PAGE_PRESENT));
	BUG_ON(!rpte || !(pte_flags(*rpte) & _PAGE_PRESENT));
	lmfn = __pte_mfn(*lpte);
	rmfn = __pte_mfn(*rpte);

	if (lmfn == rmfn)
		return;

	lpfn = mfn_to_local_pfn(lmfn);
	rpfn = mfn_to_local_pfn(rmfn);

	printk(KERN_INFO
	       "Swapping MFNs for PFN %lx and %lx (MFN %lx and %lx)\n",
	       lpfn, rpfn, lmfn, rmfn);

	xen_l1_entry_update(lpte, pfn_pte_ma(rmfn, pte_pgprot(*lpte)));
	xen_l1_entry_update(rpte, pfn_pte_ma(lmfn, pte_pgprot(*rpte)));
#ifdef CONFIG_X86_64
	if (HYPERVISOR_update_va_mapping((unsigned long)__va(lpfn<<PAGE_SHIFT),
					 pfn_pte_ma(rmfn, PAGE_KERNEL_RO), 0))
		BUG();
#endif
	if (HYPERVISOR_update_va_mapping((unsigned long)__va(rpfn<<PAGE_SHIFT),
					 pfn_pte_ma(lmfn, PAGE_KERNEL),
					 UVMF_TLB_FLUSH))
		BUG();

	set_phys_to_machine(lpfn, rmfn);
	set_phys_to_machine(rpfn, lmfn);

	mmu[0].ptr = ((uint64_t)lmfn << PAGE_SHIFT) | MMU_MACHPHYS_UPDATE;
	mmu[0].val = rpfn;
	mmu[1].ptr = ((uint64_t)rmfn << PAGE_SHIFT) | MMU_MACHPHYS_UPDATE;
	mmu[1].val = lpfn;
	if (HYPERVISOR_mmu_update(mmu, 2, NULL, DOMID_SELF))
		BUG();

	/*
	 * Copy over all contents of the page just replaced, except for the
	 * vcpu_info itself, as it may have got updated after having been
	 * copied from __per_cpu_load[].
	 */
	memcpy(__va(rpfn << PAGE_SHIFT),
	       __va(lpfn << PAGE_SHIFT),
	       (unsigned long)&per_cpu_var(vcpu_info) & (PAGE_SIZE - 1));
	level = (unsigned long)(&per_cpu_var(vcpu_info) + 1) & (PAGE_SIZE - 1);
	if (level)
		memcpy(__va(rpfn << PAGE_SHIFT) + level,
		       __va(lpfn << PAGE_SHIFT) + level,
		       PAGE_SIZE - level);
}
#endif

#define NR_MC     BITS_PER_LONG
#define NR_MMU    BITS_PER_LONG
#define NR_MMUEXT (BITS_PER_LONG / 4)

DEFINE_PER_CPU(bool, xen_lazy_mmu);
EXPORT_PER_CPU_SYMBOL(xen_lazy_mmu);
struct lazy_mmu {
	unsigned int nr_mc, nr_mmu, nr_mmuext;
	multicall_entry_t mc[NR_MC];
	mmu_update_t mmu[NR_MMU];
	struct mmuext_op mmuext[NR_MMUEXT];
};
static DEFINE_PER_CPU(struct lazy_mmu, lazy_mmu);

static inline bool use_lazy_mmu_mode(void)
{
#ifdef CONFIG_PREEMPT
	if (!preempt_count())
		return false;
#endif
	return !irq_count();
}

static void multicall_failed(const multicall_entry_t *mc, int rc)
{
	printk(KERN_EMERG "hypercall#%lu(%lx, %lx, %lx, %lx)"
			  " failed: %d (caller %lx)\n",
	       mc->op, mc->args[0], mc->args[1], mc->args[2], mc->args[3],
	       rc, mc->args[5]);
	BUG();
}

static int _xen_multicall_flush(bool ret_last) {
	struct lazy_mmu *lazy = &__get_cpu_var(lazy_mmu);
	multicall_entry_t *mc = lazy->mc;
	unsigned int count = lazy->nr_mc;

	if (!count)
		return 0;

	lazy->nr_mc = 0;
	lazy->nr_mmu = 0;
	lazy->nr_mmuext = 0;

	if (count == 1) {
		int rc = _hypercall(int, mc->op, mc->args[0], mc->args[1],
				    mc->args[2], mc->args[3], mc->args[4]);

		if (unlikely(rc)) {
			if (ret_last)
				return rc;
			multicall_failed(mc, rc);
		}
	} else {
		if (HYPERVISOR_multicall(mc, count))
			BUG();
		while (count-- > ret_last)
			if (unlikely(mc++->result))
				multicall_failed(mc - 1, mc[-1].result);
		if (ret_last)
			return mc->result;
	}

	return 0;
}

void xen_multicall_flush(bool force) {
	if (force || use_lazy_mmu_mode())
		_xen_multicall_flush(false);
}
EXPORT_SYMBOL(xen_multicall_flush);

int xen_multi_update_va_mapping(unsigned long va, pte_t pte,
				unsigned long uvmf)
{
	struct lazy_mmu *lazy = &__get_cpu_var(lazy_mmu);
	multicall_entry_t *mc;

	if (unlikely(!use_lazy_mmu_mode()))
#ifdef CONFIG_X86_PAE
		return _hypercall4(int, update_va_mapping, va,
				   pte.pte_low, pte.pte_high, uvmf);
#else
		return _hypercall3(int, update_va_mapping, va,
				   pte.pte, uvmf);
#endif

	if (unlikely(lazy->nr_mc == NR_MC))
		_xen_multicall_flush(false);

	mc = lazy->mc + lazy->nr_mc++;
	mc->op = __HYPERVISOR_update_va_mapping;
	mc->args[0] = va;
#ifndef CONFIG_X86_PAE
	mc->args[1] = pte.pte;
#else
	mc->args[1] = pte.pte_low;
	mc->args[2] = pte.pte_high;
#endif
	mc->args[MULTI_UVMFLAGS_INDEX] = uvmf;
	mc->args[5] = (long)__builtin_return_address(0);

	return 0;
}

static inline bool mmu_may_merge(const multicall_entry_t *mc,
				 unsigned int op, domid_t domid)
{
	return mc->op == op && !mc->args[2] && mc->args[3] == domid;
}

int xen_multi_mmu_update(mmu_update_t *src, unsigned int count,
			 unsigned int *success_count, domid_t domid)
{
	struct lazy_mmu *lazy = &__get_cpu_var(lazy_mmu);
	multicall_entry_t *mc = lazy->mc + lazy->nr_mc;
	mmu_update_t *dst;
	bool commit, merge;

	if (unlikely(!use_lazy_mmu_mode()))
		return _hypercall4(int, mmu_update, src, count,
				   success_count, domid);

	commit = (lazy->nr_mmu + count) > NR_MMU || success_count;
	merge = lazy->nr_mc && !commit
		&& mmu_may_merge(mc - 1, __HYPERVISOR_mmu_update, domid);
	if (unlikely(lazy->nr_mc == NR_MC) && !merge) {
		_xen_multicall_flush(false);
		mc = lazy->mc;
		commit = count > NR_MMU || success_count;
	}

	if (!lazy->nr_mc && unlikely(commit))
		return _hypercall4(int, mmu_update, src, count,
				   success_count, domid);

	dst = lazy->mmu + lazy->nr_mmu;
	lazy->nr_mmu += count;
	if (merge) {
		mc[-1].args[1] += count;
		memcpy(dst, src, count * sizeof(*src));
	} else {
		++lazy->nr_mc;
		mc->op = __HYPERVISOR_mmu_update;
		if (!commit) {
			mc->args[0] = (unsigned long)dst;
			memcpy(dst, src, count * sizeof(*src));
		} else
			mc->args[0] = (unsigned long)src;
		mc->args[1] = count;
		mc->args[2] = (unsigned long)success_count;
		mc->args[3] = domid;
		mc->args[5] = (long)__builtin_return_address(0);
	}

	while (!commit && count--)
		switch (src++->ptr & (sizeof(pteval_t) - 1)) {
		case MMU_NORMAL_PT_UPDATE:
		case MMU_PT_UPDATE_PRESERVE_AD:
			break;
		default:
			commit = true;
			break;
		}

	return commit ? _xen_multicall_flush(true) : 0;
}

int xen_multi_mmuext_op(struct mmuext_op *src, unsigned int count,
			unsigned int *success_count, domid_t domid)
{
	struct lazy_mmu *lazy = &__get_cpu_var(lazy_mmu);
	multicall_entry_t *mc;
	struct mmuext_op *dst;
	bool commit, merge;

	if (unlikely(!use_lazy_mmu_mode()))
		return _hypercall4(int, mmuext_op, src, count,
				   success_count, domid);

	/*
	 * While it could be useful in theory, I've never seen the body of
	 * this conditional to be reached, hence it seems more reasonable
	 * to disable it for the time being.
	 */
	if (0 && likely(count)
	    && likely(!success_count)
	    && likely(domid == DOMID_SELF)
	    && likely(lazy->nr_mc)
	    && lazy->mc[lazy->nr_mc - 1].op == __HYPERVISOR_update_va_mapping) {
		unsigned long oldf, newf = UVMF_NONE;

		switch (src->cmd) {
		case MMUEXT_TLB_FLUSH_ALL:
			newf = UVMF_TLB_FLUSH | UVMF_ALL;
			break;
		case MMUEXT_INVLPG_ALL:
			newf = UVMF_INVLPG | UVMF_ALL;
			break;
		case MMUEXT_TLB_FLUSH_MULTI:
			newf = UVMF_TLB_FLUSH | UVMF_MULTI
			       | (unsigned long)src->arg2.vcpumask.p;
			break;
		case MMUEXT_INVLPG_MULTI:
			newf = UVMF_INVLPG | UVMF_MULTI
			       | (unsigned long)src->arg2.vcpumask.p;
			break;
		case MMUEXT_TLB_FLUSH_LOCAL:
			newf = UVMF_TLB_FLUSH | UVMF_LOCAL;
			break;
		case MMUEXT_INVLPG_LOCAL:
			newf = UVMF_INVLPG | UVMF_LOCAL;
			break;
		}
		mc = lazy->mc + lazy->nr_mc - 1;
		oldf = mc->args[MULTI_UVMFLAGS_INDEX];
		if (newf == UVMF_NONE || oldf == UVMF_NONE
		    || newf == (UVMF_TLB_FLUSH | UVMF_ALL))
			;
		else if (oldf == (UVMF_TLB_FLUSH | UVMF_ALL))
			newf = UVMF_TLB_FLUSH | UVMF_ALL;
		else if ((newf & UVMF_FLUSHTYPE_MASK) == UVMF_INVLPG
			 && (oldf & UVMF_FLUSHTYPE_MASK) == UVMF_INVLPG
			 && ((src->arg1.linear_addr ^ mc->args[0])
			     >> PAGE_SHIFT))
			newf = UVMF_NONE;
		else if (((oldf | newf) & UVMF_ALL)
			 && !((oldf ^ newf) & UVMF_FLUSHTYPE_MASK))
			newf |= UVMF_ALL;
		else if ((oldf ^ newf) & ~UVMF_FLUSHTYPE_MASK)
			newf = UVMF_NONE;
		else if ((oldf & UVMF_FLUSHTYPE_MASK) == UVMF_TLB_FLUSH)
			newf = (newf & ~UVMF_FLUSHTYPE_MASK) | UVMF_TLB_FLUSH;
		else if ((newf & UVMF_FLUSHTYPE_MASK) != UVMF_TLB_FLUSH
			 && ((newf ^ oldf) & UVMF_FLUSHTYPE_MASK))
			newf = UVMF_NONE;
		if (newf != UVMF_NONE) {
			mc->args[MULTI_UVMFLAGS_INDEX] = newf;
			++src;
			if (!--count)
				return 0;
		}
	}

	mc = lazy->mc + lazy->nr_mc;
	commit = (lazy->nr_mmuext + count) > NR_MMUEXT || success_count;
	merge = lazy->nr_mc && !commit
		&& mmu_may_merge(mc - 1, __HYPERVISOR_mmuext_op, domid);
	if (unlikely(lazy->nr_mc == NR_MC) && !merge) {
		_xen_multicall_flush(false);
		mc = lazy->mc;
		commit = count > NR_MMUEXT || success_count;
	}

	if (!lazy->nr_mc && unlikely(commit))
		return _hypercall4(int, mmuext_op, src, count,
				   success_count, domid);

	dst = lazy->mmuext + lazy->nr_mmuext;
	lazy->nr_mmuext += count;
	if (merge) {
		mc[-1].args[1] += count;
		memcpy(dst, src, count * sizeof(*src));
	} else {
		++lazy->nr_mc;
		mc->op = __HYPERVISOR_mmuext_op;
		if (!commit) {
			mc->args[0] = (unsigned long)dst;
			memcpy(dst, src, count * sizeof(*src));
		} else
			mc->args[0] = (unsigned long)src;
		mc->args[1] = count;
		mc->args[2] = (unsigned long)success_count;
		mc->args[3] = domid;
		mc->args[5] = (long)__builtin_return_address(0);
	}

	while (!commit && count--)
		switch (src++->cmd) {
		case MMUEXT_PIN_L1_TABLE:
		case MMUEXT_PIN_L2_TABLE:
		case MMUEXT_PIN_L3_TABLE:
		case MMUEXT_PIN_L4_TABLE:
		case MMUEXT_UNPIN_TABLE:
		case MMUEXT_TLB_FLUSH_LOCAL:
		case MMUEXT_INVLPG_LOCAL:
		case MMUEXT_TLB_FLUSH_MULTI:
		case MMUEXT_INVLPG_MULTI:
		case MMUEXT_TLB_FLUSH_ALL:
		case MMUEXT_INVLPG_ALL:
			break;
		default:
			commit = true;
			break;
		}

	return commit ? _xen_multicall_flush(true) : 0;
}

void xen_l1_entry_update(pte_t *ptr, pte_t val)
{
	mmu_update_t u;
	u.ptr = ptep_to_machine(ptr);
	u.val = __pte_val(val);
	BUG_ON(HYPERVISOR_mmu_update(&u, 1, NULL, DOMID_SELF) < 0);
}
EXPORT_SYMBOL_GPL(xen_l1_entry_update);

static void do_lN_entry_update(mmu_update_t *mmu, unsigned int mmu_count,
                               struct page *page)
{
	if (likely(page)) {
		multicall_entry_t mcl[2];
		unsigned long pfn = page_to_pfn(page);

		MULTI_update_va_mapping(mcl,
					(unsigned long)__va(pfn << PAGE_SHIFT),
					pfn_pte(pfn, PAGE_KERNEL_RO), 0);
		SetPagePinned(page);
		MULTI_mmu_update(mcl + 1, mmu, mmu_count, NULL, DOMID_SELF);
		if (unlikely(HYPERVISOR_multicall_check(mcl, 2, NULL)))
			BUG();
	} else if (unlikely(HYPERVISOR_mmu_update(mmu, mmu_count,
						  NULL, DOMID_SELF) < 0))
		BUG();
}

void xen_l2_entry_update(pmd_t *ptr, pmd_t val)
{
	mmu_update_t u;
	struct page *page = NULL;

	if (likely(pmd_present(val)) && likely(!pmd_large(val))
	    && likely(mem_map)
	    && likely(PagePinned(virt_to_page(ptr)))) {
		page = pmd_page(val);
		if (unlikely(PagePinned(page)))
			page = NULL;
		else if (PageHighMem(page)) {
#ifndef CONFIG_HIGHPTE
			BUG();
#endif
			kmap_flush_unused();
			page = NULL;
		}
	}
	u.ptr = virt_to_machine(ptr);
	u.val = __pmd_val(val);
	do_lN_entry_update(&u, 1, page);
}

#if defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64)
void xen_l3_entry_update(pud_t *ptr, pud_t val)
{
	mmu_update_t u;
	struct page *page = NULL;

	if (likely(pud_present(val))
#ifdef CONFIG_X86_64
	    && likely(!pud_large(val))
#endif
	    && likely(mem_map)
	    && likely(PagePinned(virt_to_page(ptr)))) {
		page = pud_page(val);
		if (unlikely(PagePinned(page)))
			page = NULL;
	}
	u.ptr = virt_to_machine(ptr);
	u.val = __pud_val(val);
	do_lN_entry_update(&u, 1, page);
}
#endif

#ifdef CONFIG_X86_64
void xen_l4_entry_update(pgd_t *ptr, pgd_t val)
{
	mmu_update_t u[2];
	struct page *page = NULL;

	if (likely(pgd_present(val)) && likely(mem_map)
	    && likely(PagePinned(virt_to_page(ptr)))) {
		page = pgd_page(val);
		if (unlikely(PagePinned(page)))
			page = NULL;
	}
	u[0].ptr = virt_to_machine(ptr);
	u[0].val = __pgd_val(val);
	if (((unsigned long)ptr & ~PAGE_MASK)
	    <= pgd_index(TASK_SIZE_MAX) * sizeof(*ptr)) {
		ptr = __user_pgd(ptr);
		BUG_ON(!ptr);
		u[1].ptr = virt_to_machine(ptr);
		u[1].val = __pgd_val(val);
		do_lN_entry_update(u, 2, page);
	} else
		do_lN_entry_update(u, 1, page);
}
#endif /* CONFIG_X86_64 */

#ifdef CONFIG_X86_64
void xen_pt_switch(pgd_t *pgd)
{
	struct mmuext_op op;
	op.cmd = MMUEXT_NEW_BASEPTR;
	op.arg1.mfn = virt_to_mfn(pgd);
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}

void xen_new_user_pt(pgd_t *pgd)
{
	struct mmuext_op op;

	pgd = __user_pgd(pgd);
	op.cmd = MMUEXT_NEW_USER_BASEPTR;
	op.arg1.mfn = pgd ? virt_to_mfn(pgd) : 0;
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}
#endif

void xen_tlb_flush(void)
{
	struct mmuext_op op;
	op.cmd = MMUEXT_TLB_FLUSH_LOCAL;
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}
EXPORT_SYMBOL(xen_tlb_flush);

void xen_invlpg(unsigned long ptr)
{
	struct mmuext_op op;
	op.cmd = MMUEXT_INVLPG_LOCAL;
	op.arg1.linear_addr = ptr & PAGE_MASK;
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}
EXPORT_SYMBOL(xen_invlpg);

#ifdef CONFIG_SMP

void xen_tlb_flush_all(void)
{
	struct mmuext_op op;
	op.cmd = MMUEXT_TLB_FLUSH_ALL;
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}
EXPORT_SYMBOL_GPL(xen_tlb_flush_all);

void xen_tlb_flush_mask(const cpumask_t *mask)
{
	struct mmuext_op op;
	if ( cpus_empty(*mask) )
		return;
	op.cmd = MMUEXT_TLB_FLUSH_MULTI;
	set_xen_guest_handle(op.arg2.vcpumask, cpus_addr(*mask));
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}
EXPORT_SYMBOL_GPL(xen_tlb_flush_mask);

void xen_invlpg_all(unsigned long ptr)
{
	struct mmuext_op op;
	op.cmd = MMUEXT_INVLPG_ALL;
	op.arg1.linear_addr = ptr & PAGE_MASK;
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}
EXPORT_SYMBOL_GPL(xen_invlpg_all);

void xen_invlpg_mask(const cpumask_t *mask, unsigned long ptr)
{
	struct mmuext_op op;
	if ( cpus_empty(*mask) )
		return;
	op.cmd = MMUEXT_INVLPG_MULTI;
	op.arg1.linear_addr = ptr & PAGE_MASK;
	set_xen_guest_handle(op.arg2.vcpumask, cpus_addr(*mask));
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}
EXPORT_SYMBOL_GPL(xen_invlpg_mask);

#endif /* CONFIG_SMP */

#ifdef CONFIG_X86_64
#define NR_PGD_PIN_OPS 2
#else
#define NR_PGD_PIN_OPS 1
#endif

void xen_pgd_pin(pgd_t *pgd)
{
	struct mmuext_op op[NR_PGD_PIN_OPS];

	op[0].cmd = MMUEXT_PIN_L3_TABLE;
	op[0].arg1.mfn = virt_to_mfn(pgd);
#ifdef CONFIG_X86_64
	op[1].cmd = op[0].cmd = MMUEXT_PIN_L4_TABLE;
	pgd = __user_pgd(pgd);
	if (pgd)
		op[1].arg1.mfn = virt_to_mfn(pgd);
	else {
		op[1].cmd = MMUEXT_PIN_L3_TABLE;
		op[1].arg1.mfn = pfn_to_mfn(__pa_symbol(level3_user_pgt)
					    >> PAGE_SHIFT);
	}
#endif
	if (HYPERVISOR_mmuext_op(op, NR_PGD_PIN_OPS, NULL, DOMID_SELF) < 0)
		BUG();
}

void xen_pgd_unpin(pgd_t *pgd)
{
	struct mmuext_op op[NR_PGD_PIN_OPS];

	op[0].cmd = MMUEXT_UNPIN_TABLE;
	op[0].arg1.mfn = virt_to_mfn(pgd);
#ifdef CONFIG_X86_64
	pgd = __user_pgd(pgd);
	BUG_ON(!pgd);
	op[1].cmd = MMUEXT_UNPIN_TABLE;
	op[1].arg1.mfn = virt_to_mfn(pgd);
#endif
	if (HYPERVISOR_mmuext_op(op, NR_PGD_PIN_OPS, NULL, DOMID_SELF) < 0)
		BUG();
}

void xen_set_ldt(const void *ptr, unsigned int ents)
{
	struct mmuext_op op;
	op.cmd = MMUEXT_SET_LDT;
	op.arg1.linear_addr = (unsigned long)ptr;
	op.arg2.nr_ents     = ents;
	BUG_ON(HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) < 0);
}

/* Protected by balloon_lock. */
#define MAX_CONTIG_ORDER 9 /* 2MB */
static unsigned long discontig_frames[1<<MAX_CONTIG_ORDER];
static unsigned long limited_frames[1<<MAX_CONTIG_ORDER];
static multicall_entry_t cr_mcl[1<<MAX_CONTIG_ORDER];

/* Ensure multi-page extents are contiguous in machine memory. */
int xen_create_contiguous_region(
	unsigned long vstart, unsigned int order, unsigned int address_bits)
{
	unsigned long *in_frames = discontig_frames, out_frame;
	unsigned long  frame, flags;
	unsigned int   i;
	int            rc, success;
#ifdef CONFIG_64BIT
	pte_t         *ptep = NULL;
#endif
	struct xen_memory_exchange exchange = {
		.in = {
			.nr_extents   = 1UL << order,
			.extent_order = 0,
			.domid        = DOMID_SELF
		},
		.out = {
			.nr_extents   = 1,
			.extent_order = order,
			.address_bits = address_bits,
			.domid        = DOMID_SELF
		}
	};

	/*
	 * Currently an auto-translated guest will not perform I/O, nor will
	 * it require PAE page directories below 4GB. Therefore any calls to
	 * this function are redundant and can be ignored.
	 */
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	if (unlikely(order > MAX_CONTIG_ORDER))
		return -ENOMEM;

#ifdef CONFIG_64BIT
	if (unlikely(vstart > PAGE_OFFSET + MAXMEM)) {
		unsigned int level;

		if (vstart < __START_KERNEL_map
		    || vstart + (PAGE_SIZE << order) > _brk_end)
			return -EINVAL;
		ptep = lookup_address((unsigned long)__va(__pa(vstart)),
				      &level);
		if (ptep && pte_none(*ptep))
			ptep = NULL;
		if (vstart < __START_KERNEL && ptep)
			return -EINVAL;
		if (order > MAX_CONTIG_ORDER - 1)
			return -ENOMEM;
	}
#else
	if (unlikely(vstart + (PAGE_SIZE << order) > (unsigned long)high_memory))
		return -EINVAL;
#endif

	set_xen_guest_handle(exchange.in.extent_start, in_frames);
	set_xen_guest_handle(exchange.out.extent_start, &out_frame);

	scrub_pages((void *)vstart, 1 << order);

	balloon_lock(flags);

	/* 1. Zap current PTEs, remembering MFNs. */
	for (i = 0; i < (1U<<order); i++) {
		in_frames[i] = pfn_to_mfn((__pa(vstart) >> PAGE_SHIFT) + i);
		MULTI_update_va_mapping(cr_mcl + i, vstart + (i*PAGE_SIZE),
					__pte_ma(0), 0);
#ifdef CONFIG_64BIT
		if (ptep)
			MULTI_update_va_mapping(cr_mcl + i + (1U << order),
				(unsigned long)__va(__pa(vstart)) + (i*PAGE_SIZE),
				__pte_ma(0), 0);
#endif
		set_phys_to_machine((__pa(vstart)>>PAGE_SHIFT)+i,
			INVALID_P2M_ENTRY);
	}
#ifdef CONFIG_64BIT
	if (ptep)
		i += i;
#endif
	if (HYPERVISOR_multicall_check(cr_mcl, i, NULL))
		BUG();

	/* 2. Get a new contiguous memory extent. */
	out_frame = __pa(vstart) >> PAGE_SHIFT;
	rc = HYPERVISOR_memory_op(XENMEM_exchange, &exchange);
	success = (exchange.nr_exchanged == (1UL << order));
	BUG_ON(!success && ((exchange.nr_exchanged != 0) || (rc == 0)));
	BUG_ON(success && (rc != 0));
#if CONFIG_XEN_COMPAT <= 0x030002
	if (unlikely(rc == -ENOSYS)) {
		/* Compatibility when XENMEM_exchange is unsupported. */
		if (HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					 &exchange.in) != (1UL << order))
			BUG();
		success = (HYPERVISOR_memory_op(XENMEM_populate_physmap,
						&exchange.out) == 1);
		if (!success) {
			/* Couldn't get special memory: fall back to normal. */
			for (i = 0; i < (1U<<order); i++)
				in_frames[i] = (__pa(vstart)>>PAGE_SHIFT) + i;
			if (HYPERVISOR_memory_op(XENMEM_populate_physmap,
						 &exchange.in) != (1UL<<order))
				BUG();
		}
	}
#endif

	/* 3. Map the new extent in place of old pages. */
	for (i = 0; i < (1U<<order); i++) {
		frame = success ? (out_frame + i) : in_frames[i];
		MULTI_update_va_mapping(cr_mcl + i, vstart + (i*PAGE_SIZE),
					pfn_pte_ma(frame, PAGE_KERNEL), 0);
#ifdef CONFIG_64BIT
		if (ptep)
			MULTI_update_va_mapping(cr_mcl + i + (1U << order),
				(unsigned long)__va(__pa(vstart)) + (i*PAGE_SIZE),
				pfn_pte_ma(frame, PAGE_KERNEL_RO), 0);
#endif
		set_phys_to_machine((__pa(vstart)>>PAGE_SHIFT)+i, frame);
	}
#ifdef CONFIG_64BIT
	if (ptep)
		i += i;
#endif
	cr_mcl[i - 1].args[MULTI_UVMFLAGS_INDEX] = order
						   ? UVMF_TLB_FLUSH|UVMF_ALL
						   : UVMF_INVLPG|UVMF_ALL;
	if (HYPERVISOR_multicall_check(cr_mcl, i, NULL))
		BUG();

	balloon_unlock(flags);

	return success ? 0 : -ENOMEM;
}
EXPORT_SYMBOL_GPL(xen_create_contiguous_region);

void xen_destroy_contiguous_region(unsigned long vstart, unsigned int order)
{
	unsigned long *out_frames = discontig_frames, in_frame;
	unsigned long  frame, flags;
	unsigned int   i;
	int            rc, success;
	struct xen_memory_exchange exchange = {
		.in = {
			.nr_extents   = 1,
			.extent_order = order,
			.domid        = DOMID_SELF
		},
		.out = {
			.nr_extents   = 1UL << order,
			.extent_order = 0,
			.domid        = DOMID_SELF
		}
	};

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	if (unlikely(order > MAX_CONTIG_ORDER))
		return;

	set_xen_guest_handle(exchange.in.extent_start, &in_frame);
	set_xen_guest_handle(exchange.out.extent_start, out_frames);

	scrub_pages((void *)vstart, 1 << order);

	balloon_lock(flags);

	/* 1. Find start MFN of contiguous extent. */
	in_frame = pfn_to_mfn(__pa(vstart) >> PAGE_SHIFT);

	/* 2. Zap current PTEs. */
	for (i = 0; i < (1U<<order); i++) {
		MULTI_update_va_mapping(cr_mcl + i, vstart + (i*PAGE_SIZE),
					__pte_ma(0), 0);
		set_phys_to_machine((__pa(vstart)>>PAGE_SHIFT)+i,
			INVALID_P2M_ENTRY);
		out_frames[i] = (__pa(vstart) >> PAGE_SHIFT) + i;
	}
	if (HYPERVISOR_multicall_check(cr_mcl, i, NULL))
		BUG();

	/* 3. Do the exchange for non-contiguous MFNs. */
	rc = HYPERVISOR_memory_op(XENMEM_exchange, &exchange);
	success = (exchange.nr_exchanged == 1);
	BUG_ON(!success && ((exchange.nr_exchanged != 0) || (rc == 0)));
	BUG_ON(success && (rc != 0));
#if CONFIG_XEN_COMPAT <= 0x030002
	if (unlikely(rc == -ENOSYS)) {
		/* Compatibility when XENMEM_exchange is unsupported. */
		if (HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					 &exchange.in) != 1)
			BUG();
		if (HYPERVISOR_memory_op(XENMEM_populate_physmap,
					 &exchange.out) != (1UL << order))
			BUG();
		success = 1;
	}
#endif

	/* 4. Map new pages in place of old pages. */
	for (i = 0; i < (1U<<order); i++) {
		frame = success ? out_frames[i] : (in_frame + i);
		MULTI_update_va_mapping(cr_mcl + i, vstart + (i*PAGE_SIZE),
					pfn_pte_ma(frame, PAGE_KERNEL), 0);
		set_phys_to_machine((__pa(vstart)>>PAGE_SHIFT)+i, frame);
	}

	cr_mcl[i - 1].args[MULTI_UVMFLAGS_INDEX] = order
						   ? UVMF_TLB_FLUSH|UVMF_ALL
						   : UVMF_INVLPG|UVMF_ALL;
	if (HYPERVISOR_multicall_check(cr_mcl, i, NULL))
		BUG();

	balloon_unlock(flags);

	if (unlikely(!success)) {
		/* Try hard to get the special memory back to Xen. */
		exchange.in.extent_order = 0;
		set_xen_guest_handle(exchange.in.extent_start, &in_frame);

		for (i = 0; i < (1U<<order); i++) {
			struct page *page = alloc_page(__GFP_HIGHMEM|__GFP_COLD);
			unsigned long pfn;
			mmu_update_t mmu;
			unsigned int j = 0;

			if (!page) {
				printk(KERN_WARNING "Xen and kernel out of memory "
				       "while trying to release an order %u "
				       "contiguous region\n", order);
				break;
			}
			pfn = page_to_pfn(page);

			balloon_lock(flags);

			if (!PageHighMem(page)) {
				void *v = __va(pfn << PAGE_SHIFT);

				scrub_pages(v, 1);
				MULTI_update_va_mapping(cr_mcl + j, (unsigned long)v,
							__pte_ma(0), UVMF_INVLPG|UVMF_ALL);
				++j;
			}
#ifdef CONFIG_XEN_SCRUB_PAGES
			else {
				scrub_pages(kmap(page), 1);
				kunmap(page);
				kmap_flush_unused();
			}
#endif

			frame = pfn_to_mfn(pfn);
			set_phys_to_machine(pfn, INVALID_P2M_ENTRY);

			MULTI_update_va_mapping(cr_mcl + j, vstart,
						pfn_pte_ma(frame, PAGE_KERNEL),
						UVMF_INVLPG|UVMF_ALL);
			++j;

			pfn = __pa(vstart) >> PAGE_SHIFT;
			set_phys_to_machine(pfn, frame);
			if (!xen_feature(XENFEAT_auto_translated_physmap)) {
				mmu.ptr = ((uint64_t)frame << PAGE_SHIFT) | MMU_MACHPHYS_UPDATE;
				mmu.val = pfn;
				cr_mcl[j].op = __HYPERVISOR_mmu_update;
				cr_mcl[j].args[0] = (unsigned long)&mmu;
				cr_mcl[j].args[1] = 1;
				cr_mcl[j].args[2] = 0;
				cr_mcl[j].args[3] = DOMID_SELF;
				++j;
			}

			cr_mcl[j].op = __HYPERVISOR_memory_op;
			cr_mcl[j].args[0] = XENMEM_decrease_reservation;
			cr_mcl[j].args[1] = (unsigned long)&exchange.in;

			if (HYPERVISOR_multicall(cr_mcl, j + 1))
				BUG();
			BUG_ON(cr_mcl[j].result != 1);
			while (j--)
				BUG_ON(cr_mcl[j].result != 0);

			balloon_unlock(flags);

			free_empty_pages(&page, 1);

			in_frame++;
			vstart += PAGE_SIZE;
		}
	}
}
EXPORT_SYMBOL_GPL(xen_destroy_contiguous_region);

int __init early_create_contiguous_region(unsigned long pfn,
					  unsigned int order,
					  unsigned int address_bits)
{
	unsigned long *in_frames = discontig_frames, out_frame = pfn;
	unsigned int i;
	int rc, success;
	struct xen_memory_exchange exchange = {
		.in = {
			.nr_extents   = 1UL << order,
			.extent_order = 0,
			.domid        = DOMID_SELF
		},
		.out = {
			.nr_extents   = 1,
			.extent_order = order,
			.address_bits = address_bits,
			.domid        = DOMID_SELF
		}
	};

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	if (unlikely(order > MAX_CONTIG_ORDER))
		return -ENOMEM;

	for (i = 0; i < (1U << order); ++i) {
		in_frames[i] = pfn_to_mfn(pfn + i);
		set_phys_to_machine(pfn + i, INVALID_P2M_ENTRY);
	}

	set_xen_guest_handle(exchange.in.extent_start, in_frames);
	set_xen_guest_handle(exchange.out.extent_start, &out_frame);

	rc = HYPERVISOR_memory_op(XENMEM_exchange, &exchange);
	success = (exchange.nr_exchanged == (1UL << order));
	BUG_ON(!success && (exchange.nr_exchanged || !rc));
	BUG_ON(success && rc);
#if CONFIG_XEN_COMPAT <= 0x030002
	if (unlikely(rc == -ENOSYS)) {
		/* Compatibility when XENMEM_exchange is unavailable. */
		if (HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					 &exchange.in) != (1UL << order))
			BUG();
		success = (HYPERVISOR_memory_op(XENMEM_populate_physmap,
						&exchange.out) == 1);
		if (!success) {
			for (i = 0; i < (1U << order); ++i)
				in_frames[i] = pfn + i;
			if (HYPERVISOR_memory_op(XENMEM_populate_physmap,
						 &exchange.in) != (1UL << order))
				BUG();
		}
	}
#endif

	for (i = 0; i < (1U << order); ++i, ++out_frame) {
		if (!success)
			out_frame = in_frames[i];
		set_phys_to_machine(pfn + i, out_frame);
	}

	return success ? 0 : -ENOMEM;
}

static void undo_limit_pages(struct page *pages, unsigned int order)
{
	BUG_ON(xen_feature(XENFEAT_auto_translated_physmap));
	BUG_ON(order > MAX_CONTIG_ORDER);
	xen_limit_pages_to_max_mfn(pages, order, 0);
	ClearPageForeign(pages);
	__free_pages(pages, order);
}

int xen_limit_pages_to_max_mfn(
	struct page *pages, unsigned int order, unsigned int address_bits)
{
	unsigned long flags, frame;
	unsigned long *in_frames = discontig_frames, *out_frames = limited_frames;
	struct page *page;
	unsigned int i, n, nr_mcl;
	int rc, success;
	DECLARE_BITMAP(limit_map, 1 << MAX_CONTIG_ORDER);

	struct xen_memory_exchange exchange = {
		.in = {
			.extent_order = 0,
			.domid        = DOMID_SELF
		},
		.out = {
			.extent_order = 0,
			.address_bits = address_bits,
			.domid        = DOMID_SELF
		}
	};

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	if (unlikely(order > MAX_CONTIG_ORDER))
		return -ENOMEM;

	if (address_bits) {
		if (address_bits < PAGE_SHIFT)
			return -EINVAL;
		bitmap_zero(limit_map, 1U << order);
	} else if (order) {
		BUILD_BUG_ON(sizeof(pages->index) != sizeof(*limit_map));
		for (i = 0; i < BITS_TO_LONGS(1U << order); ++i)
			limit_map[i] = pages[i + 1].index;
	} else
		__set_bit(0, limit_map);

	set_xen_guest_handle(exchange.in.extent_start, in_frames);
	set_xen_guest_handle(exchange.out.extent_start, out_frames);

	/* 0. Scrub the pages. */
	for (i = 0, n = 0; i < 1U<<order ; i++) {
		page = &pages[i];
		if (address_bits) {
			if (!(pfn_to_mfn(page_to_pfn(page)) >> (address_bits - PAGE_SHIFT)))
				continue;
			__set_bit(i, limit_map);
		}

		if (!PageHighMem(page))
			scrub_pages(page_address(page), 1);
#ifdef CONFIG_XEN_SCRUB_PAGES
		else {
			scrub_pages(kmap(page), 1);
			kunmap(page);
			++n;
		}
#endif
	}
	if (bitmap_empty(limit_map, 1U << order))
		return 0;

	if (n)
		kmap_flush_unused();

	balloon_lock(flags);

	/* 1. Zap current PTEs (if any), remembering MFNs. */
	for (i = 0, n = 0, nr_mcl = 0; i < (1U<<order); i++) {
		if(!test_bit(i, limit_map))
			continue;
		page = &pages[i];

		out_frames[n] = page_to_pfn(page);
		in_frames[n] = pfn_to_mfn(out_frames[n]);

		if (!PageHighMem(page))
			MULTI_update_va_mapping(cr_mcl + nr_mcl++,
						(unsigned long)page_address(page),
						__pte_ma(0), 0);

		set_phys_to_machine(out_frames[n], INVALID_P2M_ENTRY);
		++n;
	}
	if (nr_mcl && HYPERVISOR_multicall_check(cr_mcl, nr_mcl, NULL))
		BUG();

	/* 2. Get new memory below the required limit. */
	exchange.in.nr_extents = n;
	exchange.out.nr_extents = n;
	rc = HYPERVISOR_memory_op(XENMEM_exchange, &exchange);
	success = (exchange.nr_exchanged == n);
	BUG_ON(!success && ((exchange.nr_exchanged != 0) || (rc == 0)));
	BUG_ON(success && (rc != 0));
#if CONFIG_XEN_COMPAT <= 0x030002
	if (unlikely(rc == -ENOSYS)) {
		/* Compatibility when XENMEM_exchange is unsupported. */
		if (HYPERVISOR_memory_op(XENMEM_decrease_reservation,
					 &exchange.in) != n)
			BUG();
		if (HYPERVISOR_memory_op(XENMEM_populate_physmap,
					 &exchange.out) != n)
			BUG();
		success = 1;
	}
#endif

	/* 3. Map the new pages in place of old pages. */
	for (i = 0, n = 0, nr_mcl = 0; i < (1U<<order); i++) {
		if(!test_bit(i, limit_map))
			continue;
		page = &pages[i];

		frame = success ? out_frames[n] : in_frames[n];

		if (!PageHighMem(page))
			MULTI_update_va_mapping(cr_mcl + nr_mcl++,
						(unsigned long)page_address(page),
						pfn_pte_ma(frame, PAGE_KERNEL), 0);

		set_phys_to_machine(page_to_pfn(page), frame);
		++n;
	}
	if (nr_mcl) {
		cr_mcl[nr_mcl - 1].args[MULTI_UVMFLAGS_INDEX] = order
							        ? UVMF_TLB_FLUSH|UVMF_ALL
							        : UVMF_INVLPG|UVMF_ALL;
		if (HYPERVISOR_multicall_check(cr_mcl, nr_mcl, NULL))
			BUG();
	}

	balloon_unlock(flags);

	if (!success)
		return -ENOMEM;

	if (address_bits) {
		if (order) {
			BUILD_BUG_ON(sizeof(*limit_map) != sizeof(pages->index));
			for (i = 0; i < BITS_TO_LONGS(1U << order); ++i)
				pages[i + 1].index = limit_map[i];
		}
		SetPageForeign(pages, undo_limit_pages);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xen_limit_pages_to_max_mfn);

int write_ldt_entry(struct desc_struct *ldt, int entry, const void *desc)
{
	maddr_t mach_lp = arbitrary_virt_to_machine(ldt + entry);
	return HYPERVISOR_update_descriptor(mach_lp, *(const u64*)desc);
}

int write_gdt_entry(struct desc_struct *gdt, int entry, const void *desc,
		    int type)
{
	maddr_t mach_gp = arbitrary_virt_to_machine(gdt + entry);
	return HYPERVISOR_update_descriptor(mach_gp, *(const u64*)desc);
}
