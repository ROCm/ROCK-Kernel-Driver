/* $Id: cache.c,v 1.10 2000/03/07 11:58:34 gniibe Exp $
 *
 *  linux/arch/sh/mm/cache.c
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 *
 */

#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#if defined(__sh3__)
#define CCR		0xffffffec	/* Address of Cache Control Register */
#define CCR_CACHE_VAL	0x00000005	/* 8k-byte cache, P1-wb, enable */
#define CCR_CACHE_INIT	0x0000000d	/* 8k-byte cache, CF, P1-wb, enable */
#define CCR_CACHE_ENABLE	 1

#define CACHE_IC_ADDRESS_ARRAY 0xf0000000 /* SH-3 has unified cache system */
#define CACHE_OC_ADDRESS_ARRAY 0xf0000000
#define CACHE_VALID	  1
#define CACHE_UPDATED	  2

/* 7709A/7729 has 16K cache (256-entry), while 7702 has only 2K(direct)
   7702 is not supported (yet) */
struct _cache_system_info {
	int way_shift;
	int entry_mask;
	int num_entries;
};

/* Data at BSS is cleared after setting this variable.
   So, we Should not placed this variable at BSS section.
   Initialize this, it is placed at data section. */
static struct _cache_system_info cache_system_info = {0,};

#define CACHE_OC_WAY_SHIFT	(cache_system_info.way_shift)
#define CACHE_IC_WAY_SHIFT	(cache_system_info.way_shift)
#define CACHE_OC_ENTRY_SHIFT    4
#define CACHE_OC_ENTRY_MASK	(cache_system_info.entry_mask)
#define CACHE_IC_ENTRY_MASK	(cache_system_info.entry_mask)
#define CACHE_OC_NUM_ENTRIES	(cache_system_info.num_entries)
#define CACHE_OC_NUM_WAYS	4
#define CACHE_IC_NUM_WAYS	4
#elif defined(__SH4__)
#define CCR		 0xff00001c	/* Address of Cache Control Register */
#define CCR_CACHE_VAL	 0x00000105	/* 8k+16k-byte cache,P1-wb,enable */
#define CCR_CACHE_INIT	 0x0000090d	/* ICI,ICE(8k), OCI,P1-wb,OCE(16k) */
#define CCR_CACHE_ENABLE 0x00000101

#define CACHE_IC_ADDRESS_ARRAY 0xf0000000
#define CACHE_OC_ADDRESS_ARRAY 0xf4000000
#define CACHE_VALID	  1
#define CACHE_UPDATED	  2

#define CACHE_OC_WAY_SHIFT       13
#define CACHE_IC_WAY_SHIFT       13
#define CACHE_OC_ENTRY_SHIFT      5
#define CACHE_IC_ENTRY_SHIFT      5
#define CACHE_OC_ENTRY_MASK		0x3fe0
#define CACHE_OC_ENTRY_PHYS_MASK	0x0fe0
#define CACHE_IC_ENTRY_MASK		0x1fe0
#define CACHE_IC_NUM_ENTRIES	256
#define CACHE_OC_NUM_ENTRIES	512
#define CACHE_OC_NUM_WAYS	  1
#define CACHE_IC_NUM_WAYS	  1
#endif


/*
 * Write back all the cache.
 *
 * For SH-4, we only need to flush (write back) Operand Cache,
 * as Instruction Cache doesn't have "updated" data.
 *
 * Assumes that this is called in interrupt disabled context, and P2.
 * Shuld be INLINE function.
 */
static inline void cache_wback_all(void)
{
	unsigned long addr, data, i, j;

	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		for (j=0; j<CACHE_OC_NUM_WAYS; j++) {
			addr = CACHE_OC_ADDRESS_ARRAY|(j<<CACHE_OC_WAY_SHIFT)|
				(i<<CACHE_OC_ENTRY_SHIFT);
			data = ctrl_inl(addr);
			if ((data & (CACHE_UPDATED|CACHE_VALID))
			    == (CACHE_UPDATED|CACHE_VALID)) {
				data &= ~CACHE_UPDATED;
				ctrl_outl(data, addr);
			}
		}
	}
}

static void __init
detect_cpu_and_cache_system(void)
{
#if defined(__sh3__)
	unsigned long addr0, addr1, data0, data1, data2, data3;

	jump_to_P2();
	/*
	 * Check if the entry shadows or not.
	 * When shadowed, it's 128-entry system.
	 * Otherwise, it's 256-entry system.
	 */
	addr0 = CACHE_OC_ADDRESS_ARRAY + (3 << 12);
	addr1 = CACHE_OC_ADDRESS_ARRAY + (1 << 12);

	/* First, write back & invalidate */
	data0  = ctrl_inl(addr0);
	ctrl_outl(data0&~(CACHE_VALID|CACHE_UPDATED), addr0);
	data1  = ctrl_inl(addr1);
	ctrl_outl(data1&~(CACHE_VALID|CACHE_UPDATED), addr1);

	/* Next, check if there's shadow or not */
	data0 = ctrl_inl(addr0);
	data0 ^= CACHE_VALID;
	ctrl_outl(data0, addr0);
	data1 = ctrl_inl(addr1);
	data2 = data1 ^ CACHE_VALID;
	ctrl_outl(data2, addr1);
	data3 = ctrl_inl(addr0);

	/* Lastly, invaliate them. */
	ctrl_outl(data0&~CACHE_VALID, addr0);
	ctrl_outl(data2&~CACHE_VALID, addr1);
	back_to_P1();

	if (data0 == data1 && data2 == data3) {	/* Shadow */
		cache_system_info.way_shift = 11;
		cache_system_info.entry_mask = 0x7f0;
		cache_system_info.num_entries = 128;
		cpu_data->type = CPU_SH7708;
	} else {				/* 7709A or 7729  */
		cache_system_info.way_shift = 12;
		cache_system_info.entry_mask = 0xff0;
		cache_system_info.num_entries = 256;
		cpu_data->type = CPU_SH7729;
	}
#elif defined(__SH4__)
	cpu_data->type = CPU_SH7750;
#endif
}

void __init cache_init(void)
{
	unsigned long ccr;

	detect_cpu_and_cache_system();

	ccr = ctrl_inl(CCR);
	jump_to_P2();
	if (ccr & CCR_CACHE_ENABLE)
		/*
		 * XXX: Should check RA here. 
		 * If RA was 1, we only need to flush the half of the caches.
		 */
		cache_wback_all();

	ctrl_outl(CCR_CACHE_INIT, CCR);
	back_to_P1();
}

#if defined(__SH4__)
/*
 * SH-4 has virtually indexed and physically tagged cache.
 */

/*
 * Write back the dirty D-caches, but not invalidate them.
 *
 * START, END: Virtual Address
 */
static void dcache_wback_range(unsigned long start, unsigned long end)
{
	unsigned long v;

	start &= ~(L1_CACHE_BYTES-1);
	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		asm volatile("ocbwb	%0"
			     : /* no output */
			     : "m" (__m(v)));
	}
}

/*
 * Invalidate I-caches.
 *
 * START, END: Virtual Address
 *
 */
static void icache_purge_range(unsigned long start, unsigned long end)
{
	unsigned long addr, data, v;

	start &= ~(L1_CACHE_BYTES-1);

	jump_to_P2();
	/*
	 * To handle the cache-line, we calculate the entry with virtual
	 * address: entry = vaddr & CACHE_IC_ENTRY_MASK.
	 *
	 * With A-bit "on", data written to is translated by MMU and
	 * compared the tag of cache and if it's not matched, nothing
	 * will be occurred.  (We can avoid flushing other caches.)
	 * 
	 * NOTE: We can use A-bit feature here, because we have valid
	 * entriy in TLB (at least in UTLB), as dcache_wback_range is
	 * called before this function is called.
	 */
	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		addr = CACHE_IC_ADDRESS_ARRAY |	(v&CACHE_IC_ENTRY_MASK)
			| 0x8 /* A-bit */;
		data = (v&0xfffffc00); /* Valid=0 */
		ctrl_outl(data, addr);
	}
	back_to_P1();
}

/*
 * Write back the range of D-cache, and purge the I-cache.
 *
 * Called from sh/kernel/signal.c, after accessing the memory
 * through U0 area.  START and END is the address of U0.
 */
void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	save_and_cli(flags);
	dcache_wback_range(start, end);
	icache_purge_range(start, end);
	restore_flags(flags);
}

/*
 * Invalidate the I-cache of the page (don't need to write back D-cache).
 *
 * Called from kernel/ptrace.c, mm/memory.c after flush_page_to_ram is called.
 */
void flush_icache_page(struct vm_area_struct *vma, struct page *pg)
{
	unsigned long phys, addr, data, i;

	/* Physical address of this page */
	phys = (pg - mem_map)*PAGE_SIZE + __MEMORY_START;

	jump_to_P2();
	/* Loop all the I-cache */
	for (i=0; i<CACHE_IC_NUM_ENTRIES; i++) {
		addr = CACHE_IC_ADDRESS_ARRAY| (i<<CACHE_IC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if ((data & CACHE_VALID) && (data&PAGE_MASK) == phys) {
			data &= ~CACHE_VALID;
			ctrl_outl(data, addr);
		}
	}
	back_to_P1();
}

/*
 * Write back & invalidate the D-cache of the page.
 * (To avoid "alias" issues)
 */
void flush_dcache_page(struct page *pg)
{
	unsigned long phys, addr, data, i;

	/* Physical address of this page */
	phys = (pg - mem_map)*PAGE_SIZE + __MEMORY_START;

	jump_to_P2();
	/* Loop all the D-cache */
	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		addr = CACHE_OC_ADDRESS_ARRAY| (i<<CACHE_OC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if ((data & CACHE_VALID) && (data&PAGE_MASK) == phys) {
			data &= ~(CACHE_VALID|CACHE_UPDATED);
			ctrl_outl(data, addr);
		}
	}
	back_to_P1();
}

void flush_cache_all(void)
{
	unsigned long addr, data, i;
	unsigned long flags;

	save_and_cli(flags);
	jump_to_P2();

	/* Loop all the D-cache */
	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		addr = CACHE_OC_ADDRESS_ARRAY|(i<<CACHE_OC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if (data & CACHE_VALID) {
			data &= ~(CACHE_UPDATED|CACHE_VALID);
			ctrl_outl(data, addr);
		}
	}

	/* Loop all the I-cache */
	for (i=0; i<CACHE_IC_NUM_ENTRIES; i++) {
		addr = CACHE_IC_ADDRESS_ARRAY| (i<<CACHE_IC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if (data & CACHE_VALID) {
			data &= ~CACHE_VALID;
			ctrl_outl(data, addr);
		}
	}

	back_to_P1();
	restore_flags(flags);
}

void flush_cache_mm(struct mm_struct *mm)
{
	/* Is there any good way? */
	/* XXX: possibly call flush_cache_range for each vm area */
	flush_cache_all();
}

/*
 * Write back and invalidate D-caches.
 *
 * START, END: Virtual Address (U0 address)
 *
 * NOTE: We need to flush the _physical_ page entry.
 * Flushing the cache lines for U0 only isn't enough.
 * We need to flush for P1 too, which may contain aliases.
 */
void flush_cache_range(struct mm_struct *mm, unsigned long start,
		       unsigned long end)
{
	/*
	 * We could call flush_cache_page for the pages of these range,
	 * but it's not efficient (scan the caches all the time...).
	 *
	 * We can't use A-bit magic, as there's the case we don't have
	 * valid entry on TLB.
	 */
	flush_cache_all();
}

/*
 * Write back and invalidate D-caches for the page.
 *
 * ADDR: Virtual Address (U0 address)
 *
 * NOTE: We need to flush the _physical_ page entry.
 * Flushing the cache lines for U0 only isn't enough.
 * We need to flush for P1 too, which may contain aliases.
 */
void flush_cache_page(struct vm_area_struct *vma, unsigned long addr)
{
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;
	unsigned long phys;
	struct page *pg;

	dir = pgd_offset(vma->vm_mm, addr);
	pmd = pmd_offset(dir, addr);
	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd))
		return;
	pte = pte_offset(pmd, addr);
	entry = *pte;
	if (pte_none(entry) || !pte_present(entry))
		return;
	phys = pte_val(entry)&PAGE_MASK;
	pg = virt_to_page(__va(phys));
	flush_dcache_page(pg);
}

/*
 * Write-back & invalidate the cache.
 *
 * After accessing the memory from kernel space (P1-area), we need to 
 * write back the cache line.
 *
 * We search the D-cache to see if we have the entries corresponding to
 * the page, and if found, write back them.
 */
void __flush_page_to_ram(void *kaddr)
{
	unsigned long phys, addr, data, i;

	/* Physical address of this page */
	phys = PHYSADDR(kaddr);

	jump_to_P2();
	/* Loop all the D-cache */
	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		addr = CACHE_OC_ADDRESS_ARRAY| (i<<CACHE_OC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if ((data & CACHE_VALID) && (data&PAGE_MASK) == phys) {
			data &= ~(CACHE_UPDATED|CACHE_VALID);
			ctrl_outl(data, addr);
		}
	}
	back_to_P1();
}

void flush_page_to_ram(struct page *pg)
{
	unsigned long phys;

	/* Physical address of this page */
	phys = (pg - mem_map)*PAGE_SIZE + __MEMORY_START;
	__flush_page_to_ram(phys_to_virt(phys));
}

/*
 * Check entries of the I-cache & D-cache of the page.
 * (To see "alias" issues)
 */
void check_cache_page(struct page *pg)
{
	unsigned long phys, addr, data, i;
	unsigned long kaddr;
	unsigned long cache_line_index;
	int bingo = 0;

	/* Physical address of this page */
	phys = (pg - mem_map)*PAGE_SIZE + __MEMORY_START;
	kaddr = phys + PAGE_OFFSET;
	cache_line_index = (kaddr&CACHE_OC_ENTRY_MASK)>>CACHE_OC_ENTRY_SHIFT;

	jump_to_P2();
	/* Loop all the D-cache */
	for (i=0; i<CACHE_OC_NUM_ENTRIES; i++) {
		addr = CACHE_OC_ADDRESS_ARRAY| (i<<CACHE_OC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if ((data & (CACHE_UPDATED|CACHE_VALID))
		    == (CACHE_UPDATED|CACHE_VALID)
		    && (data&PAGE_MASK) == phys) {
			data &= ~(CACHE_VALID|CACHE_UPDATED);
			ctrl_outl(data, addr);
			if ((i^cache_line_index)&0x180)
				bingo = 1;
		}
	}

	cache_line_index &= 0xff;
	/* Loop all the I-cache */
	for (i=0; i<CACHE_IC_NUM_ENTRIES; i++) {
		addr = CACHE_IC_ADDRESS_ARRAY| (i<<CACHE_IC_ENTRY_SHIFT);
		data = ctrl_inl(addr);
		if ((data & CACHE_VALID) && (data&PAGE_MASK) == phys) {
			data &= ~CACHE_VALID;
			ctrl_outl(data, addr);
			if (((i^cache_line_index)&0x80))
				bingo = 2;
		}
	}
	back_to_P1();

	if (bingo) {
		extern void dump_stack(void);

		if (bingo ==1)
			printk("BINGO!\n");
		else
			printk("Bingo!\n");
		dump_stack();
		printk("--------------------\n");
	}
}

/* Page is 4K, OC size is 16K, there are four lines. */
#define CACHE_ALIAS 0x00003000

void clear_user_page(void *to, unsigned long address)
{
	clear_page(to);
	if (((address ^ (unsigned long)to) & CACHE_ALIAS))
		__flush_page_to_ram(to);
}

void copy_user_page(void *to, void *from, unsigned long address)
{
	copy_page(to, from);
	if (((address ^ (unsigned long)to) & CACHE_ALIAS))
		__flush_page_to_ram(to);
}
#endif
