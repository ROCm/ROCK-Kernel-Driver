/*
 *  linux/include/asm-arm/proc-armv/tlbflush.h
 *
 *  Copyright (C) 1999-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 *	TLB Management
 *	==============
 *
 *	The arch/arm/mm/tlb-*.S files implement these methods.
 *
 *	The TLB specific code is expected to perform whatever tests it
 *	needs to determine if it should invalidate the TLB for each
 *	call.  Start addresses are inclusive and end addresses are
 *	exclusive; it is safe to round these addresses down.
 *
 *	flush_tlb_all()
 *
 *		Invalidate the entire TLB.
 *
 *	flush_tlb_mm(mm)
 *
 *		Invalidate all TLB entries in a particular address
 *		space.
 *		- mm	- mm_struct describing address space
 *
 *	flush_tlb_range(mm,start,end)
 *
 *		Invalidate a range of TLB entries in the specified
 *		address space.
 *		- mm	- mm_struct describing address space
 *		- start - start address (may not be aligned)
 *		- end	- end address (exclusive, may not be aligned)
 *
 *	flush_tlb_page(vaddr,vma)
 *
 *		Invalidate the specified page in the specified address range.
 *		- vaddr - virtual address (may not be aligned)
 *		- vma	- vma_struct describing address range
 *
 *	flush_kern_tlb_page(kaddr)
 *
 *		Invalidate the TLB entry for the specified page.  The address
 *		will be in the kernels virtual memory space.  Current uses
 *		only require the D-TLB to be invalidated.
 *		- kaddr - Kernel virtual memory address
 */

struct cpu_tlb_fns {
	void (*flush_kern_all)(void);
	void (*flush_user_mm)(struct mm_struct *);
	void (*flush_user_range)(unsigned long, unsigned long, struct vm_area_struct *);
	void (*flush_user_page)(unsigned long, struct vm_area_struct *);
	void (*flush_kern_range)(unsigned long, unsigned long);
	void (*flush_kern_page)(unsigned long);
};

/*
 * Convert calls to our calling convention.
 */
#define flush_tlb_all()			__cpu_flush_kern_tlb_all()
#define flush_tlb_mm(mm)		__cpu_flush_user_tlb_mm(mm)
#define flush_tlb_range(vma,start,end)	__cpu_flush_user_tlb_range(start,end,vma)
#define flush_tlb_page(vma,vaddr)	__cpu_flush_user_tlb_page(vaddr,vma)
#define flush_tlb_kernel_range(s,e)	__cpu_flush_kern_tlb_range(s,e)
#define flush_tlb_kernel_page(kaddr)	__cpu_flush_kern_tlb_page(kaddr)

/*
 * Now select the calling method
 */
#ifdef MULTI_TLB

extern struct cpu_tlb_fns cpu_tlb;

#define __cpu_flush_kern_tlb_all	cpu_tlb.flush_kern_all
#define __cpu_flush_user_tlb_mm		cpu_tlb.flush_user_mm
#define __cpu_flush_user_tlb_range	cpu_tlb.flush_user_range
#define __cpu_flush_user_tlb_page	cpu_tlb.flush_user_page
#define __cpu_flush_kern_tlb_range	cpu_tlb.flush_kern_range
#define __cpu_flush_kern_tlb_page	cpu_tlb.flush_kern_page

#else

#define __cpu_flush_kern_tlb_all	__glue(_TLB,_flush_kern_tlb_all)
#define __cpu_flush_user_tlb_mm		__glue(_TLB,_flush_user_tlb_mm)
#define __cpu_flush_user_tlb_range	__glue(_TLB,_flush_user_tlb_range)
#define __cpu_flush_user_tlb_page	__glue(_TLB,_flush_user_tlb_page)
#define __cpu_flush_kern_tlb_range	__glue(_TLB,_flush_kern_tlb_range)
#define __cpu_flush_kern_tlb_page	__glue(_TLB,_flush_kern_tlb_page)

extern void __cpu_flush_kern_tlb_all(void);
extern void __cpu_flush_user_tlb_mm(struct mm_struct *);
extern void __cpu_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
extern void __cpu_flush_user_tlb_page(unsigned long, struct vm_area_struct *);
extern void __cpu_flush_kern_tlb_range(unsigned long, unsigned long);
extern void __cpu_flush_kern_tlb_page(unsigned long);

#endif

/*
 * if PG_dcache_dirty is set for the page, we need to ensure that any
 * cache entries for the kernels virtual memory range are written
 * back to the page.
 */
extern void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte);

/*
 * ARM processors do not cache TLB tables in RAM.
 */
#define flush_tlb_pgtables(mm,start,end)	do { } while (0)

/*
 * Old ARM MEMC stuff.  This supports the reversed mapping handling that
 * we have on the older 26-bit machines.  We don't have a MEMC chip, so...
 */
#define memc_update_all()		do { } while (0)
#define memc_update_mm(mm)		do { } while (0)
#define memc_update_addr(mm,pte,log)	do { } while (0)
#define memc_clear(mm,physaddr)		do { } while (0)

