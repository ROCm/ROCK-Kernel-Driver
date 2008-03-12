#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/system.h>

#define __flush_tlb() xen_tlb_flush()
#define __flush_tlb_global() xen_tlb_flush()
#define __flush_tlb_single(addr) xen_invlpg(addr)
#define __flush_tlb_all() xen_tlb_flush()
#define __flush_tlb_one(addr) xen_invlpg(addr)

#ifdef CONFIG_X86_32
# define TLB_FLUSH_ALL	0xffffffff
#else
# define TLB_FLUSH_ALL	-1ULL
#endif

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 *
 * x86-64 can only flush individual pages or full VMs. For a range flush
 * we always do the full VM. Might be worth trying if for a small
 * range a few INVLPGs in a row are a win.
 */

#ifndef CONFIG_SMP

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() __flush_tlb_all()
#define local_flush_tlb() __flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long addr)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb();
}

#else  /* SMP */

#include <asm/smp.h>

#define local_flush_tlb() __flush_tlb()

#define flush_tlb_all xen_tlb_flush_all
#define flush_tlb_current_task() xen_tlb_flush_mask(&current->mm->cpu_vm_mask)
#define flush_tlb_mm(mm) xen_tlb_flush_mask(&(mm)->cpu_vm_mask)
#define flush_tlb_page(vma, va) xen_invlpg_mask(&(vma)->vm_mm->cpu_vm_mask, va)

#define flush_tlb()	flush_tlb_current_task()

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

#define TLBSTATE_OK	1
#define TLBSTATE_LAZY	2

#ifdef CONFIG_X86_32
struct tlb_state
{
	struct mm_struct *active_mm;
	int state;
	char __cacheline_padding[L1_CACHE_BYTES-8];
};
DECLARE_PER_CPU(struct tlb_state, cpu_tlbstate);
#endif

#endif	/* SMP */

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	flush_tlb_all();
}

#endif /* _ASM_X86_TLBFLUSH_H */
