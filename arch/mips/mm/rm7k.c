/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * r4xx0.c: R4000 processor variant specific MMU/Cache routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998 Ralf Baechle ralf@gnu.org
 *
 * To do:
 *
 *  - this code is a overbloated pig
 *  - many of the bug workarounds are not efficient at all, but at
 *    least they are functional ...
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/mmu_context.h>

/* CP0 hazard avoidance. */
#define BARRIER __asm__ __volatile__(".set noreorder\n\t" \
				     "nop; nop; nop; nop; nop; nop;\n\t" \
				     ".set reorder\n\t")

/* Primary cache parameters. */
static int icache_size, dcache_size; /* Size in bytes */

#define ic_lsize	32		/* Fixed to 32 byte on RM7000  */
#define dc_lsize	32		/* Fixed to 32 byte on RM7000  */
#define sc_lsize	32		/* Fixed to 32 byte on RM7000  */
#define tc_pagesize	(32*128)

/* Secondary cache parameters. */
#define scache_size	(256*1024)	/* Fixed to 256KiB on RM7000 */

#include <asm/cacheops.h>
#include <asm/r4kcache.h>

int rm7k_tcache_enabled = 0;

/*
 * Not added to asm/r4kcache.h because it seems to be RM7000-specific.
 */
#define Page_Invalidate_T 0x16

static inline void invalidate_tcache_page(unsigned long addr)
{
	__asm__ __volatile__(
		".set\tnoreorder\t\t\t# invalidate_tcache_page\n\t"
		".set\tmips3\n\t"
		"cache\t%1, (%0)\n\t"
		".set\tmips0\n\t"
		".set\treorder"
		:
		: "r" (addr),
		  "i" (Page_Invalidate_T));
}

/*
 * Zero an entire page.  Note that while the RM7000 has a second level cache
 * it doesn't have a Create_Dirty_Excl_SD operation.
 */
static void rm7k_clear_page(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"cache\t%3,-32(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tmips0\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D)
		:"$1","memory");
}


/*
 * Copy an entire page.  Note that while the RM7000 has a second level cache
 * it doesn't have a Create_Dirty_Excl_SD operation.
 */
static void rm7k_copy_page(void * to, void * from)
{
	unsigned long dummy1, dummy2;
	unsigned long reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		".set\tmips3\n\t"
		"daddiu\t$1,%0,%8\n"
		"1:\tcache\t%9,(%0)\n\t"
		"lw\t%2,(%1)\n\t"
		"lw\t%3,4(%1)\n\t"
		"lw\t%4,8(%1)\n\t"
		"lw\t%5,12(%1)\n\t"
		"sw\t%2,(%0)\n\t"
		"sw\t%3,4(%0)\n\t"
		"sw\t%4,8(%0)\n\t"
		"sw\t%5,12(%0)\n\t"
		"lw\t%2,16(%1)\n\t"
		"lw\t%3,20(%1)\n\t"
		"lw\t%4,24(%1)\n\t"
		"lw\t%5,28(%1)\n\t"
		"sw\t%2,16(%0)\n\t"
		"sw\t%3,20(%0)\n\t"
		"sw\t%4,24(%0)\n\t"
		"sw\t%5,28(%0)\n\t"
		"cache\t%9,32(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"lw\t%2,-32(%1)\n\t"
		"lw\t%3,-28(%1)\n\t"
		"lw\t%4,-24(%1)\n\t"
		"lw\t%5,-20(%1)\n\t"
		"sw\t%2,-32(%0)\n\t"
		"sw\t%3,-28(%0)\n\t"
		"sw\t%4,-24(%0)\n\t"
		"sw\t%5,-20(%0)\n\t"
		"lw\t%2,-16(%1)\n\t"
		"lw\t%3,-12(%1)\n\t"
		"lw\t%4,-8(%1)\n\t"
		"lw\t%5,-4(%1)\n\t"
		"sw\t%2,-16(%0)\n\t"
		"sw\t%3,-12(%0)\n\t"
		"sw\t%4,-8(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sw\t%5,-4(%0)\n\t"
		".set\tmips0\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2),
		 "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from),
		 "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D));
}

static void __flush_cache_all_d32i32(void)
{
	blast_dcache32();
	blast_icache32();
}

static inline void rm7k_flush_cache_all_d32i32(void)
{
	/* Yes! Caches that don't suck ...  */
}

static void rm7k_flush_cache_range_d32i32(struct mm_struct *mm,
					 unsigned long start,
					 unsigned long end)
{
	/* RM7000 caches are sane ...  */
}

static void rm7k_flush_cache_mm_d32i32(struct mm_struct *mm)
{
	/* RM7000 caches are sane ...  */
}

static void rm7k_flush_cache_page_d32i32(struct vm_area_struct *vma,
					unsigned long page)
{
	/* RM7000 caches are sane ...  */
}

static void rm7k_flush_page_to_ram_d32i32(struct page * page)
{
	/* Yes!  Caches that don't suck!  */
}

static void rm7k_flush_icache_range(unsigned long start, unsigned long end)
{
	/*
	 * FIXME: This is overdoing things and harms performance.
	 */
	__flush_cache_all_d32i32();
}

static void rm7k_flush_icache_page(struct vm_area_struct *vma,
                                   struct page *page)
{
	/*
	 * FIXME: We should not flush the entire cache but establish some
	 * temporary mapping and use hit_invalidate operation to flush out
	 * the line from the cache.
	 */
	__flush_cache_all_d32i32();
}


/*
 * Writeback and invalidate the primary cache dcache before DMA.
 * (XXX These need to be fixed ...)
 */
static void
rm7k_dma_cache_wback_inv(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		flush_dcache_line(a);	/* Hit_Writeback_Inv_D */
		flush_icache_line(a);	/* Hit_Invalidate_I */
		flush_scache_line(a);	/* Hit_Writeback_Inv_SD */
		if (a == end) break;
		a += sc_lsize;
	}

	if (!rm7k_tcache_enabled) 
		return;

	a = addr & ~(tc_pagesize - 1);
	end = (addr + size) & ~(tc_pagesize - 1);
	while(1) {
		invalidate_tcache_page(a);	/* Page_Invalidate_T */
		if (a == end) break;
		a += tc_pagesize;
	}
}
	       
static void
rm7k_dma_cache_inv(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		invalidate_dcache_line(a);	/* Hit_Invalidate_D */
		flush_icache_line(a);		/* Hit_Invalidate_I */
		invalidate_scache_line(a);	/* Hit_Invalidate_SD */
		if (a == end) break;
		a += sc_lsize;
	}

	if (!rm7k_tcache_enabled) 
		return;

	a = addr & ~(tc_pagesize - 1);
	end = (addr + size) & ~(tc_pagesize - 1);
	while(1) {
		invalidate_tcache_page(a);	/* Page_Invalidate_T */
		if (a == end) break;
		a += tc_pagesize;
	}
}

static void
rm7k_dma_cache_wback(unsigned long addr, unsigned long size)
{
	panic("rm7k_dma_cache_wback called - should not happen.\n");
}

/*
 * While we're protected against bad userland addresses we don't care
 * very much about what happens in that case.  Usually a segmentation
 * fault will dump the process later on anyway ...
 */
static void rm7k_flush_cache_sigtramp(unsigned long addr)
{
	protected_writeback_dcache_line(addr & ~(dc_lsize - 1));
	protected_flush_icache_line(addr & ~(ic_lsize - 1));
}

/*
 * Undocumented RM7000:  Bit 29 in the info register of the RM7000 v2.0
 * indicates if the TLB has 48 or 64 entries.
 *
 * 29      1 =>    64 entry JTLB
 *         0 =>    48 entry JTLB
 */
static inline int __attribute__((const)) ntlb_entries(void)
{
	if (get_info() & (1 << 29))
		return 64;

	return 48;
}

void flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

	__save_and_cli(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = get_entryhi() & 0xff;
	set_entryhi(KSEG0);
	set_entrylo0(0);
	set_entrylo1(0);
	BARRIER;

	entry = get_wired();

	/* Blast 'em all away. */
	while (entry < ntlb_entries()) {
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
	if(mm->context != 0) {
		unsigned long flags;

		__save_and_cli(flags);
		get_new_mmu_context(mm, asid_cache);
		if (mm == current->mm)
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

		__save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if (size <= (ntlb_entries() / 2)) {
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
				set_entryhi(KSEG0);
				BARRIER;
				if(idx < 0)
					continue;
				tlb_write_indexed();
				BARRIER;
			}
			set_entryhi(oldpid);
		} else {
			get_new_mmu_context(mm, asid_cache);
			if(mm == current->mm)
				set_entryhi(mm->context & 0xff);
		}
		__restore_flags(flags);
	}
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if(vma->vm_mm->context != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

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
		set_entryhi(KSEG0);
		if(idx < 0)
			goto finish;
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

	for (i = 0; i < USER_PTRS_PER_PGD; i+=8) {
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
 * We will need multiple versions of update_mmu_cache(), one that just
 * updates the TLB with the new pte(s), and another which also checks
 * for the R4k "end of page" hardware bug and does the needy.
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
	if (idx < 0) {
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
	printk(KERN_INFO "$0 : %08lx %08lx %08lx %08lx\n",
	       0UL, regs->regs[1], regs->regs[2], regs->regs[3]);
	printk(KERN_INFO "$4 : %08lx %08lx %08lx %08lx\n",
               regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7]);
	printk(KERN_INFO "$8 : %08lx %08lx %08lx %08lx\n",
	       regs->regs[8], regs->regs[9], regs->regs[10], regs->regs[11]);
	printk(KERN_INFO "$12: %08lx %08lx %08lx %08lx\n",
               regs->regs[12], regs->regs[13], regs->regs[14], regs->regs[15]);
	printk(KERN_INFO "$16: %08lx %08lx %08lx %08lx\n",
	       regs->regs[16], regs->regs[17], regs->regs[18], regs->regs[19]);
	printk(KERN_INFO "$20: %08lx %08lx %08lx %08lx\n",
               regs->regs[20], regs->regs[21], regs->regs[22], regs->regs[23]);
	printk(KERN_INFO "$24: %08lx %08lx\n",
	       regs->regs[24], regs->regs[25]);
	printk(KERN_INFO "$28: %08lx %08lx %08lx %08lx\n",
	       regs->regs[28], regs->regs[29], regs->regs[30], regs->regs[31]);

	/* Saved cp0 registers. */
	printk(KERN_INFO "epc   : %08lx    %s\nStatus: %08lx\nCause : %08lx\n",
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

/* Used for loading TLB entries before trap_init() has started, when we
   don't actually want to add a wired entry which remains throughout the
   lifetime of the system */

static int temp_tlb_entry __initdata;

__init int add_temporary_entry(unsigned long entrylo0, unsigned long entrylo1,
                               unsigned long entryhi, unsigned long pagemask)
{
	int ret = 0;
        unsigned long flags;
        unsigned long wired;
        unsigned long old_pagemask;
        unsigned long old_ctx;

        __save_and_cli(flags);
        /* Save old context and create impossible VPN2 value */
        old_ctx = (get_entryhi() & 0xff);
        old_pagemask = get_pagemask();
        wired = get_wired();
        if (--temp_tlb_entry < wired) {
		printk(KERN_WARNING "No TLB space left for add_temporary_entry\n");
		ret = -ENOSPC;
		goto out;
	}

        set_index (temp_tlb_entry);
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
 out:
        __restore_flags(flags);
	return ret;
}



/* Detect and size the caches. */
static inline void probe_icache(unsigned long config)
{
	icache_size = 1 << (12 + ((config >> 9) & 7));

	printk(KERN_INFO "Primary instruction cache %dKiB.\n", icache_size >> 10);
}

static inline void probe_dcache(unsigned long config)
{
	dcache_size = 1 << (12 + ((config >> 6) & 7));

	printk(KERN_INFO "Primary data cache %dKiB.\n", dcache_size >> 10);
}


/* 
 * This function is executed in the uncached segment KSEG1.
 * It must not touch the stack, because the stack pointer still points
 * into KSEG0. 
 *
 * Three options:
 *	- Write it in assembly and guarantee that we don't use the stack.
 *	- Disable caching for KSEG0 before calling it.
 *	- Pray that GCC doesn't randomly start using the stack.
 *
 * This being Linux, we obviously take the least sane of those options -
 * following DaveM's lead in r4xx0.c
 *
 * It seems we get our kicks from relying on unguaranteed behaviour in GCC
 */
static __init void setup_scache(void)
{
	int register i;
	
	set_cp0_config(1<<3 /* CONF_SE */);

	set_taglo(0);
	set_taghi(0);
	
	for (i=0; i<scache_size; i+=sc_lsize) {
		__asm__ __volatile__ (
		      ".set noreorder\n\t"
		      ".set mips3\n\t"
		      "cache %1, (%0)\n\t"
		      ".set mips0\n\t"
		      ".set reorder"
		      :
		      : "r" (KSEG0ADDR(i)),
		        "i" (Index_Store_Tag_SD));
	}

}

static inline void probe_scache(unsigned long config)
{
	void (*func)(void) = KSEG1ADDR(&setup_scache);

	if ((config >> 31) & 1)
		return;

	printk(KERN_INFO "Secondary cache %dKiB, linesize %d bytes.\n",
	       (scache_size >> 10), sc_lsize);

	if ((config >> 3) & 1)
		return;

	printk(KERN_INFO "Enabling secondary cache...");
	func();
	printk("Done\n");
}
 
static inline void probe_tcache(unsigned long config)
{
	if ((config >> 17) & 1)
		return;

	/* We can't enable the L3 cache yet. There may be board-specific
	 * magic necessary to turn it on, and blindly asking the CPU to
	 * start using it would may give cache errors.
	 *
	 * Also, board-specific knowledge may allow us to use the 
	 * CACHE Flash_Invalidate_T instruction if the tag RAM supports
	 * it, and may specify the size of the L3 cache so we don't have
	 * to probe it. 
	 */
	printk(KERN_INFO "Tertiary cache present, %s enabled\n",
	       config&(1<<12) ? "already" : "not (yet)");

	if ((config >> 12) & 1)
		rm7k_tcache_enabled = 1;
}

void __init ld_mmu_rm7k(void)
{
	unsigned long config = read_32bit_cp0_register(CP0_CONFIG);
	unsigned long addr;

	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

        change_cp0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);

	/* RM7000 erratum #31. The icache is screwed at startup. */
	set_taglo(0);
	set_taghi(0);
	for (addr = KSEG0; addr <= KSEG0 + 4096; addr += ic_lsize) {
		__asm__ __volatile__ (
			".set noreorder\n\t"
			".set mips3\n\t"
			"cache\t%1, 0(%0)\n\t"
			"cache\t%1, 0x1000(%0)\n\t"
			"cache\t%1, 0x2000(%0)\n\t"
			"cache\t%1, 0x3000(%0)\n\t"
			"cache\t%2, 0(%0)\n\t"
			"cache\t%2, 0x1000(%0)\n\t"
			"cache\t%2, 0x2000(%0)\n\t"
			"cache\t%2, 0x3000(%0)\n\t"
			"cache\t%1, 0(%0)\n\t"
			"cache\t%1, 0x1000(%0)\n\t"
			"cache\t%1, 0x2000(%0)\n\t"
			"cache\t%1, 0x3000(%0)\n\t"
			".set\tmips0\n\t"
			".set\treorder\n\t"
			:
			: "r" (addr), "i" (Index_Store_Tag_I), "i" (Fill));
	}

#ifndef CONFIG_MIPS_UNCACHED
	change_cp0_config(CONF_CM_CMASK, CONF_CM_CACHABLE_NONCOHERENT);
#endif

	probe_icache(config);
	probe_dcache(config);
	probe_scache(config);
	probe_tcache(config);

	printk("TLB has %d entries.\n", ntlb_entries());

	_clear_page = rm7k_clear_page;
	_copy_page = rm7k_copy_page;

	_flush_cache_all = rm7k_flush_cache_all_d32i32;
	___flush_cache_all = __flush_cache_all_d32i32;
	_flush_cache_mm = rm7k_flush_cache_mm_d32i32;
	_flush_cache_range = rm7k_flush_cache_range_d32i32;
	_flush_cache_page = rm7k_flush_cache_page_d32i32;
	_flush_page_to_ram = rm7k_flush_page_to_ram_d32i32;
	_flush_cache_sigtramp = rm7k_flush_cache_sigtramp;
	_flush_icache_range = rm7k_flush_icache_range;
	_flush_icache_page = rm7k_flush_icache_page;

	_dma_cache_wback_inv = rm7k_dma_cache_wback_inv;
	_dma_cache_wback = rm7k_dma_cache_wback;
	_dma_cache_inv = rm7k_dma_cache_inv;

	__flush_cache_all_d32i32();
	write_32bit_cp0_register(CP0_WIRED, 0);
	temp_tlb_entry = ntlb_entries() - 1;
	write_32bit_cp0_register(CP0_PAGEMASK, PM_4K);
	flush_tlb_all();
}
