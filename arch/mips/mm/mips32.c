/*
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * MIPS32 CPU variant specific MMU/Cache routines.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/bcache.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mmu_context.h>

/* CP0 hazard avoidance. */
#define BARRIER __asm__ __volatile__(".set noreorder\n\t" \
				     "nop; nop; nop; nop; nop; nop;\n\t" \
				     ".set reorder\n\t")

/* Primary cache parameters. */
static int icache_size, dcache_size; /* Size in bytes */
static int ic_lsize, dc_lsize;       /* LineSize in bytes */

/* Secondary cache (if present) parameters. */
static unsigned int scache_size, sc_lsize;	/* Again, in bytes */

#include <asm/cacheops.h>
#include <asm/mips32_cache.h>

#undef DEBUG_CACHE

/*
 * Dummy cache handling routines for machines without boardcaches
 */
static void no_sc_noop(void) {}

static struct bcache_ops no_sc_ops = {
	(void *)no_sc_noop, (void *)no_sc_noop,
	(void *)no_sc_noop, (void *)no_sc_noop
};

struct bcache_ops *bcops = &no_sc_ops;


/*
 * Zero an entire page.
 */

static void mips32_clear_page_dc(unsigned long page)
{
	unsigned long i;

        if (mips_cpu.options & MIPS_CPU_CACHE_CDEX) {
	        for (i=page; i<page+PAGE_SIZE; i+=dc_lsize) {
		        __asm__ __volatile__(
			        ".set\tnoreorder\n\t"
				".set\tnoat\n\t"
				".set\tmips3\n\t"
				"cache\t%2,(%0)\n\t"
				".set\tmips0\n\t"
				".set\tat\n\t"
				".set\treorder"
				:"=r" (i)
				:"0" (i),
				"I" (Create_Dirty_Excl_D));
		}
	}
	for (i=page; i<page+PAGE_SIZE; i+=4)
	        *(unsigned long *)(i) = 0;
}

static void mips32_clear_page_sc(unsigned long page)
{
	unsigned long i;

        if (mips_cpu.options & MIPS_CPU_CACHE_CDEX) {
	        for (i=page; i<page+PAGE_SIZE; i+=sc_lsize) {
		        __asm__ __volatile__(
				".set\tnoreorder\n\t"
				".set\tnoat\n\t"
				".set\tmips3\n\t"
				"cache\t%2,(%0)\n\t"
				".set\tmips0\n\t"
				".set\tat\n\t"
				".set\treorder"
				:"=r" (i)
				:"0" (i),
				"I" (Create_Dirty_Excl_SD));
		}
	}
	for (i=page; i<page+PAGE_SIZE; i+=4)
	        *(unsigned long *)(i) = 0;
}

static void mips32_copy_page_dc(unsigned long to, unsigned long from)
{
	unsigned long i;

        if (mips_cpu.options & MIPS_CPU_CACHE_CDEX) {
	        for (i=to; i<to+PAGE_SIZE; i+=dc_lsize) {
		        __asm__ __volatile__(
			        ".set\tnoreorder\n\t"
				".set\tnoat\n\t"
				".set\tmips3\n\t"
				"cache\t%2,(%0)\n\t"
				".set\tmips0\n\t"
				".set\tat\n\t"
				".set\treorder"
				:"=r" (i)
				:"0" (i),
				"I" (Create_Dirty_Excl_D));
		}
	}
	for (i=0; i<PAGE_SIZE; i+=4)
	        *(unsigned long *)(to+i) = *(unsigned long *)(from+i);
}

static void mips32_copy_page_sc(unsigned long to, unsigned long from)
{
	unsigned long i;

        if (mips_cpu.options & MIPS_CPU_CACHE_CDEX) {
	        for (i=to; i<to+PAGE_SIZE; i+=sc_lsize) {
		        __asm__ __volatile__(
				".set\tnoreorder\n\t"
				".set\tnoat\n\t"
				".set\tmips3\n\t"
				"cache\t%2,(%0)\n\t"
				".set\tmips0\n\t"
				".set\tat\n\t"
				".set\treorder"
				:"=r" (i)
				:"0" (i),
				"I" (Create_Dirty_Excl_SD));
		}
	}
	for (i=0; i<PAGE_SIZE; i+=4)
	        *(unsigned long *)(to+i) = *(unsigned long *)(from+i);
}

static inline void mips32_flush_cache_all_sc(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache(); blast_icache(); blast_scache();
	__restore_flags(flags);
}

static inline void mips32_flush_cache_all_pc(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache(); blast_icache();
	__restore_flags(flags);
}

static void
mips32_flush_cache_range_sc(struct mm_struct *mm,
			 unsigned long start,
			 unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if(mm->context == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if(mm->context != current->mm->context) {
			mips32_flush_cache_all_sc();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void mips32_flush_cache_range_pc(struct mm_struct *mm,
				     unsigned long start,
				     unsigned long end)
{
	if(mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
		__save_and_cli(flags);
		blast_dcache(); blast_icache();
		__restore_flags(flags);
	}
}

/*
 * On architectures like the Sparc, we could get rid of lines in
 * the cache created only by a certain context, but on the MIPS
 * (and actually certain Sparc's) we cannot.
 */
static void mips32_flush_cache_mm_sc(struct mm_struct *mm)
{
	if(mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		mips32_flush_cache_all_sc();
	}
}

static void mips32_flush_cache_mm_pc(struct mm_struct *mm)
{
	if(mm->context != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		mips32_flush_cache_all_pc();
	}
}





static void mips32_flush_cache_page_sc(struct vm_area_struct *vma,
				    unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm->context != current->active_mm->context) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache_page_indexed(page);
		blast_scache_page_indexed(page);
	} else
		blast_scache_page(page);
out:
	__restore_flags(flags);
}

static void mips32_flush_cache_page_pc(struct vm_area_struct *vma,
				    unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if (!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since Mips32 caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (mm == current->active_mm) {
		blast_dcache_page(page);
	} else {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache_page_indexed(page);
	}
out:
	__restore_flags(flags);
}

/* If the addresses passed to these routines are valid, they are
 * either:
 *
 * 1) In KSEG0, so we can do a direct flush of the page.
 * 2) In KSEG2, and since every process can translate those
 *    addresses all the time in kernel mode we can do a direct
 *    flush.
 * 3) In KSEG1, no flush necessary.
 */
static void mips32_flush_page_to_ram_sc(struct page *page)
{
	blast_scache_page((unsigned long)page_address(page));
}

static void mips32_flush_page_to_ram_pc(struct page *page)
{
	blast_dcache_page((unsigned long)page_address(page));
}

static void
mips32_flush_icache_page_s(struct vm_area_struct *vma, struct page *page)
{
	/*
	 * We did an scache flush therefore PI is already clean.
	 */
}

static void
mips32_flush_icache_range(unsigned long start, unsigned long end)
{
	flush_cache_all();
}

static void
mips32_flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	int address;

	if (!(vma->vm_flags & VM_EXEC))
		return;

	address = KSEG0 + ((unsigned long)page_address(page) & PAGE_MASK & (dcache_size - 1));
	blast_icache_page_indexed(address);
}

/*
 * Writeback and invalidate the primary cache dcache before DMA.
 */
static void
mips32_dma_cache_wback_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;
	unsigned int flags;

	if (size >= dcache_size) {
		flush_cache_all();
	} else {
	        __save_and_cli(flags);
		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
		__restore_flags(flags);
	}
	bc_wback_inv(addr, size);
}

static void
mips32_dma_cache_wback_inv_sc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= scache_size) {
		flush_cache_all();
		return;
	}

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		flush_scache_line(a);	/* Hit_Writeback_Inv_SD */
		if (a == end) break;
		a += sc_lsize;
	}
}

static void
mips32_dma_cache_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;
	unsigned int flags;

	if (size >= dcache_size) {
		flush_cache_all();
	} else {
	        __save_and_cli(flags);
		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
		__restore_flags(flags);
	}

	bc_inv(addr, size);
}

static void
mips32_dma_cache_inv_sc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= scache_size) {
		flush_cache_all();
		return;
	}

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		flush_scache_line(a); /* Hit_Writeback_Inv_SD */
		if (a == end) break;
		a += sc_lsize;
	}
}

static void
mips32_dma_cache_wback(unsigned long addr, unsigned long size)
{
	panic("mips32_dma_cache called - should not happen.\n");
}

/*
 * While we're protected against bad userland addresses we don't care
 * very much about what happens in that case.  Usually a segmentation
 * fault will dump the process later on anyway ...
 */
static void mips32_flush_cache_sigtramp(unsigned long addr)
{
	protected_writeback_dcache_line(addr & ~(dc_lsize - 1));
	protected_flush_icache_line(addr & ~(ic_lsize - 1));
}

#undef DEBUG_TLB
#undef DEBUG_TLBUPDATE

void flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	__save_and_cli(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = (get_entryhi() & 0xff);
	set_entryhi(KSEG0);
	set_entrylo0(0);
	set_entrylo1(0);
	BARRIER;

	entry = get_wired();

	/* Blast 'em all away. */
	while(entry < mips_cpu.tlbsize) {
	        /* Make sure all entries differ. */  
	        set_entryhi(KSEG0+entry*0x2000);
		set_index(entry);
		BARRIER;
		tlb_write_indexed();
		BARRIER;
		entry++;
	}
	BARRIER;
	set_entryhi(old_ctx);
	__restore_flags(flags);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_TLB
		printk("[tlbmm<%d>]", mm->context);
#endif
		__save_and_cli(flags);
		get_new_mmu_context(mm, asid_cache);
		if (mm == current->active_mm)
			set_entryhi(mm->context & 0xff);
		__restore_flags(flags);
	}
}

void flush_tlb_range(struct mm_struct *mm, unsigned long start,
				unsigned long end)
{
	if(mm->context != 0) {
		unsigned long flags;
		int size;

#ifdef DEBUG_TLB
		printk("[tlbrange<%02x,%08lx,%08lx>]", (mm->context & 0xff),
		       start, end);
#endif
		__save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if(size <= mips_cpu.tlbsize/2) {
			int oldpid = (get_entryhi() & 0xff);
			int newpid = (mm->context & 0xff);

			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
			while(start < end) {
				int idx;

				set_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				BARRIER;
				tlb_probe();
				BARRIER;
				idx = get_index();
				set_entrylo0(0);
				set_entrylo1(0);
				if(idx < 0)
					continue;
				/* Make sure all entries differ. */
				set_entryhi(KSEG0+idx*0x2000);
				BARRIER;
				tlb_write_indexed();
				BARRIER;
			}
			set_entryhi(oldpid);
		} else {
			get_new_mmu_context(mm, asid_cache);
			if (mm == current->active_mm)
				set_entryhi(mm->context & 0xff);
		}
		__restore_flags(flags);
	}
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (vma->vm_mm->context != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%d,%08lx>]", vma->vm_mm->context, page);
#endif
		newpid = (vma->vm_mm->context & 0xff);
		page &= (PAGE_MASK << 1);
		__save_and_cli(flags);
		oldpid = (get_entryhi() & 0xff);
		set_entryhi(page | newpid);
		BARRIER;
		tlb_probe();
		BARRIER;
		idx = get_index();
		set_entrylo0(0);
		set_entrylo1(0);
		if(idx < 0)
			goto finish;
		/* Make sure all entries differ. */  
		set_entryhi(KSEG0+idx*0x2000);
		BARRIER;
		tlb_write_indexed();

	finish:
		BARRIER;
		set_entryhi(oldpid);
		__restore_flags(flags);
	}
}

void pgd_init(unsigned long page)
{
	unsigned long *p = (unsigned long *) page;
	int i;

	for(i = 0; i < USER_PTRS_PER_PGD; i+=8) {
		p[i + 0] = (unsigned long) invalid_pte_table;
		p[i + 1] = (unsigned long) invalid_pte_table;
		p[i + 2] = (unsigned long) invalid_pte_table;
		p[i + 3] = (unsigned long) invalid_pte_table;
		p[i + 4] = (unsigned long) invalid_pte_table;
		p[i + 5] = (unsigned long) invalid_pte_table;
		p[i + 6] = (unsigned long) invalid_pte_table;
		p[i + 7] = (unsigned long) invalid_pte_table;
	}
}

/* 
 * Updates the TLB with the new pte(s).
 */
void update_mmu_cache(struct vm_area_struct * vma,
		      unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	pid = get_entryhi() & 0xff;

#ifdef DEBUG_TLB
	if((pid != (vma->vm_mm->context & 0xff)) || (vma->vm_mm->context == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%d tlbpid=%d\n",
		       (int) (vma->vm_mm->context & 0xff), pid);
	}
#endif

	__save_and_cli(flags);
	address &= (PAGE_MASK << 1);
	set_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	BARRIER;
	tlb_probe();
	BARRIER;
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	BARRIER;
	set_entrylo0(pte_val(*ptep++) >> 6);
	set_entrylo1(pte_val(*ptep) >> 6);
	set_entryhi(address | (pid));
	BARRIER;
	if(idx < 0) {
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	BARRIER;
	set_entryhi(pid);
	BARRIER;
	__restore_flags(flags);
}

void show_regs(struct pt_regs * regs)
{
	/* Saved main processor registers. */
	printk("$0 : %08lx %08lx %08lx %08lx\n",
	       0UL, regs->regs[1], regs->regs[2], regs->regs[3]);
	printk("$4 : %08lx %08lx %08lx %08lx\n",
               regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7]);
	printk("$8 : %08lx %08lx %08lx %08lx\n",
	       regs->regs[8], regs->regs[9], regs->regs[10], regs->regs[11]);
	printk("$12: %08lx %08lx %08lx %08lx\n",
               regs->regs[12], regs->regs[13], regs->regs[14], regs->regs[15]);
	printk("$16: %08lx %08lx %08lx %08lx\n",
	       regs->regs[16], regs->regs[17], regs->regs[18], regs->regs[19]);
	printk("$20: %08lx %08lx %08lx %08lx\n",
               regs->regs[20], regs->regs[21], regs->regs[22], regs->regs[23]);
	printk("$24: %08lx %08lx\n",
	       regs->regs[24], regs->regs[25]);
	printk("$28: %08lx %08lx %08lx %08lx\n",
	       regs->regs[28], regs->regs[29], regs->regs[30], regs->regs[31]);

	/* Saved cp0 registers. */
	printk("epc   : %08lx    %s\nStatus: %08lx\nCause : %08lx\n",
	       regs->cp0_epc, print_tainted(), regs->cp0_status, regs->cp0_cause);
}
			
void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
				      unsigned long entryhi, unsigned long pagemask)
{
        unsigned long flags;
        unsigned long wired;
        unsigned long old_pagemask;
        unsigned long old_ctx;

        __save_and_cli(flags);
        /* Save old context and create impossible VPN2 value */
        old_ctx = (get_entryhi() & 0xff);
        old_pagemask = get_pagemask();
        wired = get_wired();
        set_wired (wired + 1);
        set_index (wired);
        BARRIER;    
        set_pagemask (pagemask);
        set_entryhi(entryhi);
        set_entrylo0(entrylo0);
        set_entrylo1(entrylo1);
        BARRIER;    
        tlb_write_indexed();
        BARRIER;    
    
        set_entryhi(old_ctx);
        BARRIER;    
        set_pagemask (old_pagemask);
        flush_tlb_all();    
        __restore_flags(flags);
}

/* Detect and size the various caches. */
static void __init probe_icache(unsigned long config)
{
        unsigned long config1;
	unsigned int lsize;

        if (!(config & (1 << 31))) {
	        /* 
		 * Not a MIPS32 complainant CPU. 
		 * Config 1 register not supported, we assume R4k style.
		 */
	        icache_size = 1 << (12 + ((config >> 9) & 7));
		ic_lsize = 16 << ((config >> 5) & 1);
		mips_cpu.icache.linesz = ic_lsize;
		
		/* 
		 * We cannot infer associativity - assume direct map
		 * unless probe template indicates otherwise
		 */
		if(!mips_cpu.icache.ways) mips_cpu.icache.ways = 1;
		mips_cpu.icache.sets = 
			(icache_size / ic_lsize) / mips_cpu.icache.ways;
	} else {
	       config1 = read_mips32_cp0_config1(); 

	       if ((lsize = ((config1 >> 19) & 7)))
		       mips_cpu.icache.linesz = 2 << lsize;
	       else 
		       mips_cpu.icache.linesz = lsize;
	       mips_cpu.icache.sets = 64 << ((config1 >> 22) & 7);
	       mips_cpu.icache.ways = 1 + ((config1 >> 16) & 7);

	       ic_lsize = mips_cpu.icache.linesz;
	       icache_size = mips_cpu.icache.sets * mips_cpu.icache.ways * 
		             ic_lsize;
	}
	printk("Primary instruction cache %dkb, linesize %d bytes (%d ways)\n",
	       icache_size >> 10, ic_lsize, mips_cpu.icache.ways);
}

static void __init probe_dcache(unsigned long config)
{
        unsigned long config1;
	unsigned int lsize;

        if (!(config & (1 << 31))) {
	        /* 
		 * Not a MIPS32 complainant CPU. 
		 * Config 1 register not supported, we assume R4k style.
		 */  
		dcache_size = 1 << (12 + ((config >> 6) & 7));
		dc_lsize = 16 << ((config >> 4) & 1);
		mips_cpu.dcache.linesz = dc_lsize;
		/* 
		 * We cannot infer associativity - assume direct map
		 * unless probe template indicates otherwise
		 */
		if(!mips_cpu.dcache.ways) mips_cpu.dcache.ways = 1;
		mips_cpu.dcache.sets = 
			(dcache_size / dc_lsize) / mips_cpu.dcache.ways;
	} else {
	        config1 = read_mips32_cp0_config1();

		if ((lsize = ((config1 >> 10) & 7)))
		        mips_cpu.dcache.linesz = 2 << lsize;
		else 
		        mips_cpu.dcache.linesz= lsize;
		mips_cpu.dcache.sets = 64 << ((config1 >> 13) & 7);
		mips_cpu.dcache.ways = 1 + ((config1 >> 7) & 7);

		dc_lsize = mips_cpu.dcache.linesz;
		dcache_size = 
			mips_cpu.dcache.sets * mips_cpu.dcache.ways
			* dc_lsize;
	}
	printk("Primary data cache %dkb, linesize %d bytes (%d ways)\n",
	       dcache_size >> 10, dc_lsize, mips_cpu.dcache.ways);
}


/* If you even _breathe_ on this function, look at the gcc output
 * and make sure it does not pop things on and off the stack for
 * the cache sizing loop that executes in KSEG1 space or else
 * you will crash and burn badly.  You have been warned.
 */
static int __init probe_scache(unsigned long config)
{
	extern unsigned long stext;
	unsigned long flags, addr, begin, end, pow2;
	int tmp;

	if (mips_cpu.scache.flags == MIPS_CACHE_NOT_PRESENT)
		return 0;

	tmp = ((config >> 17) & 1);
	if(tmp)
		return 0;
	tmp = ((config >> 22) & 3);
	switch(tmp) {
	case 0:
		sc_lsize = 16;
		break;
	case 1:
		sc_lsize = 32;
		break;
	case 2:
		sc_lsize = 64;
		break;
	case 3:
		sc_lsize = 128;
		break;
	}

	begin = (unsigned long) &stext;
	begin &= ~((4 * 1024 * 1024) - 1);
	end = begin + (4 * 1024 * 1024);

	/* This is such a bitch, you'd think they would make it
	 * easy to do this.  Away you daemons of stupidity!
	 */
	__save_and_cli(flags);

	/* Fill each size-multiple cache line with a valid tag. */
	pow2 = (64 * 1024);
	for(addr = begin; addr < end; addr = (begin + pow2)) {
		unsigned long *p = (unsigned long *) addr;
		__asm__ __volatile__("nop" : : "r" (*p)); /* whee... */
		pow2 <<= 1;
	}

	/* Load first line with zero (therefore invalid) tag. */
	set_taglo(0);
	set_taghi(0);
	__asm__ __volatile__("nop; nop; nop; nop;"); /* avoid the hazard */
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     ".set mips3\n\t"
			     "cache 8, (%0)\n\t"
			     ".set mips0\n\t"
			     ".set reorder\n\t" : : "r" (begin));
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     ".set mips3\n\t"
			     "cache 9, (%0)\n\t"
			     ".set mips0\n\t"
			     ".set reorder\n\t" : : "r" (begin));
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     ".set mips3\n\t"
			     "cache 11, (%0)\n\t"
			     ".set mips0\n\t"
			     ".set reorder\n\t" : : "r" (begin));

	/* Now search for the wrap around point. */
	pow2 = (128 * 1024);
	tmp = 0;
	for(addr = (begin + (128 * 1024)); addr < (end); addr = (begin + pow2)) {
		__asm__ __volatile__("\n\t.set noreorder\n\t"
				     ".set mips3\n\t"
				     "cache 7, (%0)\n\t"
				     ".set mips0\n\t"
				     ".set reorder\n\t" : : "r" (addr));
		__asm__ __volatile__("nop; nop; nop; nop;"); /* hazard... */
		if(!get_taglo())
			break;
		pow2 <<= 1;
	}
	__restore_flags(flags);
	addr -= begin;
	printk("Secondary cache sized at %dK linesize %d bytes.\n",
	       (int) (addr >> 10), sc_lsize);
	scache_size = addr;
	return 1;
}

static void __init setup_noscache_funcs(void)
{
	_clear_page = (void *)mips32_clear_page_dc;
	_copy_page = (void *)mips32_copy_page_dc;
	_flush_cache_all = mips32_flush_cache_all_pc;
	___flush_cache_all = mips32_flush_cache_all_pc;
	_flush_cache_mm = mips32_flush_cache_mm_pc;
	_flush_cache_range = mips32_flush_cache_range_pc;
	_flush_cache_page = mips32_flush_cache_page_pc;
	_flush_page_to_ram = mips32_flush_page_to_ram_pc;

	_flush_icache_page = mips32_flush_icache_page;

	_dma_cache_wback_inv = mips32_dma_cache_wback_inv_pc;
	_dma_cache_wback = mips32_dma_cache_wback;
	_dma_cache_inv = mips32_dma_cache_inv_pc;
}

static void __init setup_scache_funcs(void)
{
        _flush_cache_all = mips32_flush_cache_all_sc;
        ___flush_cache_all = mips32_flush_cache_all_sc;
	_flush_cache_mm = mips32_flush_cache_mm_sc;
	_flush_cache_range = mips32_flush_cache_range_sc;
	_flush_cache_page = mips32_flush_cache_page_sc;
	_flush_page_to_ram = mips32_flush_page_to_ram_sc;
	_clear_page = (void *)mips32_clear_page_sc;
	_copy_page = (void *)mips32_copy_page_sc;

	_flush_icache_page = mips32_flush_icache_page_s;

	_dma_cache_wback_inv = mips32_dma_cache_wback_inv_sc;
	_dma_cache_wback = mips32_dma_cache_wback;
	_dma_cache_inv = mips32_dma_cache_inv_sc;
}

typedef int (*probe_func_t)(unsigned long);

static inline void __init setup_scache(unsigned int config)
{
	probe_func_t probe_scache_kseg1;
	int sc_present = 0;

	/* Maybe the cpu knows about a l2 cache? */
	probe_scache_kseg1 = (probe_func_t) (KSEG1ADDR(&probe_scache));
	sc_present = probe_scache_kseg1(config);

	if (sc_present) {
	  	mips_cpu.scache.linesz = sc_lsize;
		/* 
		 * We cannot infer associativity - assume direct map
		 * unless probe template indicates otherwise
		 */
		if(!mips_cpu.scache.ways) mips_cpu.scache.ways = 1;
		mips_cpu.scache.sets = 
		  (scache_size / sc_lsize) / mips_cpu.scache.ways;

		setup_scache_funcs();
		return;
	}

	setup_noscache_funcs();
}

static void __init probe_tlb(unsigned long config)
{
        unsigned long config1;

        if (!(config & (1 << 31))) {
	        /* 
		 * Not a MIPS32 complainant CPU. 
		 * Config 1 register not supported, we assume R4k style.
		 */  
	        mips_cpu.tlbsize = 48;
	} else {
	        config1 = read_mips32_cp0_config1();
		if (!((config >> 7) & 3))
		        panic("No MMU present");
		else
		        mips_cpu.tlbsize = ((config1 >> 25) & 0x3f) + 1;
	}	

	printk("Number of TLB entries %d.\n", mips_cpu.tlbsize);
}

void __init ld_mmu_mips32(void)
{
	unsigned long config = read_32bit_cp0_register(CP0_CONFIG);

	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

#ifdef CONFIG_MIPS_UNCACHED
	change_cp0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
#else
	change_cp0_config(CONF_CM_CMASK, CONF_CM_CACHABLE_NONCOHERENT);
#endif

	probe_icache(config);
	probe_dcache(config);
	setup_scache(config);
	probe_tlb(config);

	_flush_cache_sigtramp = mips32_flush_cache_sigtramp;
	_flush_icache_range = mips32_flush_icache_range;	/* Ouch */

	__flush_cache_all();
	write_32bit_cp0_register(CP0_WIRED, 0);

	/*
	 * You should never change this register:
	 *   - The entire mm handling assumes the c0_pagemask register to
	 *     be set for 4kb pages.
	 */
	write_32bit_cp0_register(CP0_PAGEMASK, PM_4K);
	flush_tlb_all();
}
