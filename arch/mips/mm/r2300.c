/*
 * r2300.c: R2000 and R3000 specific mmu/cache code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * with a lot of changes to make this thing work for R3000s
 * Tx39XX R4k style caches added. HK
 * Copyright (C) 1998, 1999, 2000 Harald Koerfgen
 * Copyright (C) 1998 Gleb Raiko & Vladimir Roganov
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/isadep.h>
#include <asm/io.h>
#include <asm/wbflush.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>

/*
 * According to the paper written by D. Miller about Linux cache & TLB
 * flush implementation, DMA/Driver coherence should be done at the 
 * driver layer.  Thus, normally, we don't need flush dcache for R3000.
 * Define this if driver does not handle cache consistency during DMA ops.
 */

/* For R3000 cores with R4000 style caches */
static unsigned long icache_size, dcache_size;		/* Size in bytes */
static unsigned long icache_lsize, dcache_lsize;	/* Size in bytes */
static unsigned long scache_size;

#include <asm/cacheops.h>
#include <asm/r4kcache.h>

#undef DEBUG_TLB
#undef DEBUG_CACHE

/* page functions */
void r3k_clear_page(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"addiu\t$1,%0,%2\n"
		"1:\tsw\t$0,(%0)\n\t"
		"sw\t$0,4(%0)\n\t"
		"sw\t$0,8(%0)\n\t"
		"sw\t$0,12(%0)\n\t"
		"addiu\t%0,32\n\t"
		"sw\t$0,-16(%0)\n\t"
		"sw\t$0,-12(%0)\n\t"
		"sw\t$0,-8(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sw\t$0,-4(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE)
		:"$1","memory");
}

static void r3k_copy_page(void * to, void * from)
{
	unsigned long dummy1, dummy2;
	unsigned long reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"addiu\t$1,%0,%8\n"
		"1:\tlw\t%2,(%1)\n\t"
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
		"addiu\t%0,64\n\t"
		"addiu\t%1,64\n\t"
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
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2),
		 "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from),
		 "I" (PAGE_SIZE));
}

unsigned long __init r3k_cache_size(unsigned long ca_flags)
{
	unsigned long flags, status, dummy, size;
	volatile unsigned long *p;

	p = (volatile unsigned long *) KSEG0;

	flags = read_32bit_cp0_register(CP0_STATUS);

	/* isolate cache space */
	write_32bit_cp0_register(CP0_STATUS, (ca_flags|flags)&~ST0_IEC);

	*p = 0xa5a55a5a;
	dummy = *p;
	status = read_32bit_cp0_register(CP0_STATUS);

	if (dummy != 0xa5a55a5a || (status & ST0_CM)) {
		size = 0;
	} else {
		for (size = 128; size <= 0x40000; size <<= 1)
			*(p + size) = 0;
		*p = -1;
		for (size = 128; 
		     (size <= 0x40000) && (*(p + size) == 0); 
		     size <<= 1)
			;
		if (size > 0x40000)
			size = 0;
	}

	write_32bit_cp0_register(CP0_STATUS, flags);

	return size * sizeof(*p);
}

unsigned long __init r3k_cache_lsize(unsigned long ca_flags)
{
	unsigned long flags, status, lsize, i, j;
	volatile unsigned long *p;

	p = (volatile unsigned long *) KSEG0;

	flags = read_32bit_cp0_register(CP0_STATUS);

	/* isolate cache space */
	write_32bit_cp0_register(CP0_STATUS, (ca_flags|flags)&~ST0_IEC);

	for (i = 0; i < 128; i++)
		*(p + i) = 0;
	*(volatile unsigned char *)p = 0;
	for (lsize = 1; lsize < 128; lsize <<= 1) {
		*(p + lsize);
		status = read_32bit_cp0_register(CP0_STATUS);
		if (!(status & ST0_CM))
			break;
	}
	for (i = 0; i < 128; i += lsize)
		*(volatile unsigned char *)(p + i) = 0;

	write_32bit_cp0_register(CP0_STATUS, flags);

	return lsize * sizeof(*p);
}

static void __init r3k_probe_cache(void)
{
	dcache_size = r3k_cache_size(ST0_ISC);
	if (dcache_size)
		dcache_lsize = r3k_cache_lsize(ST0_ISC);
	

	icache_size = r3k_cache_size(ST0_ISC|ST0_SWC);
	if (icache_size)
		icache_lsize = r3k_cache_lsize(ST0_ISC|ST0_SWC);
}

static void r3k_flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long size, i, flags;
	volatile unsigned char *p = (char *)start;

	size = end - start;
	if (size > icache_size)
		size = icache_size;

	flags = read_32bit_cp0_register(CP0_STATUS);

	/* isolate cache space */
	write_32bit_cp0_register(CP0_STATUS, (ST0_ISC|ST0_SWC|flags)&~ST0_IEC);

	for (i = 0; i < size; i += 0x080) {
		asm ( 	"sb\t$0,0x000(%0)\n\t"
			"sb\t$0,0x004(%0)\n\t"
			"sb\t$0,0x008(%0)\n\t"
			"sb\t$0,0x00c(%0)\n\t"
			"sb\t$0,0x010(%0)\n\t"
			"sb\t$0,0x014(%0)\n\t"
			"sb\t$0,0x018(%0)\n\t"
			"sb\t$0,0x01c(%0)\n\t"
		 	"sb\t$0,0x020(%0)\n\t"
			"sb\t$0,0x024(%0)\n\t"
			"sb\t$0,0x028(%0)\n\t"
			"sb\t$0,0x02c(%0)\n\t"
			"sb\t$0,0x030(%0)\n\t"
			"sb\t$0,0x034(%0)\n\t"
			"sb\t$0,0x038(%0)\n\t"
			"sb\t$0,0x03c(%0)\n\t"
			"sb\t$0,0x040(%0)\n\t"
			"sb\t$0,0x044(%0)\n\t"
			"sb\t$0,0x048(%0)\n\t"
			"sb\t$0,0x04c(%0)\n\t"
			"sb\t$0,0x050(%0)\n\t"
			"sb\t$0,0x054(%0)\n\t"
			"sb\t$0,0x058(%0)\n\t"
			"sb\t$0,0x05c(%0)\n\t"
		 	"sb\t$0,0x060(%0)\n\t"
			"sb\t$0,0x064(%0)\n\t"
			"sb\t$0,0x068(%0)\n\t"
			"sb\t$0,0x06c(%0)\n\t"
			"sb\t$0,0x070(%0)\n\t"
			"sb\t$0,0x074(%0)\n\t"
			"sb\t$0,0x078(%0)\n\t"
			"sb\t$0,0x07c(%0)\n\t"
			: : "r" (p) );
		p += 0x080;
	}

	write_32bit_cp0_register(CP0_STATUS,flags);
}

static void r3k_flush_dcache_range(unsigned long start, unsigned long end)
{
	unsigned long size, i, flags;
	volatile unsigned char *p = (char *)start;

	size = end - start;
	if (size > dcache_size)
		size = dcache_size;

	flags = read_32bit_cp0_register(CP0_STATUS);

	/* isolate cache space */
	write_32bit_cp0_register(CP0_STATUS, (ST0_ISC|flags)&~ST0_IEC);

	for (i = 0; i < size; i += 0x080) {
		asm ( 	"sb\t$0,0x000(%0)\n\t"
			"sb\t$0,0x004(%0)\n\t"
			"sb\t$0,0x008(%0)\n\t"
			"sb\t$0,0x00c(%0)\n\t"
		 	"sb\t$0,0x010(%0)\n\t"
			"sb\t$0,0x014(%0)\n\t"
			"sb\t$0,0x018(%0)\n\t"
			"sb\t$0,0x01c(%0)\n\t"
		 	"sb\t$0,0x020(%0)\n\t"
			"sb\t$0,0x024(%0)\n\t"
			"sb\t$0,0x028(%0)\n\t"
			"sb\t$0,0x02c(%0)\n\t"
		 	"sb\t$0,0x030(%0)\n\t"
			"sb\t$0,0x034(%0)\n\t"
			"sb\t$0,0x038(%0)\n\t"
			"sb\t$0,0x03c(%0)\n\t"
		 	"sb\t$0,0x040(%0)\n\t"
			"sb\t$0,0x044(%0)\n\t"
			"sb\t$0,0x048(%0)\n\t"
			"sb\t$0,0x04c(%0)\n\t"
		 	"sb\t$0,0x050(%0)\n\t"
			"sb\t$0,0x054(%0)\n\t"
			"sb\t$0,0x058(%0)\n\t"
			"sb\t$0,0x05c(%0)\n\t"
		 	"sb\t$0,0x060(%0)\n\t"
			"sb\t$0,0x064(%0)\n\t"
			"sb\t$0,0x068(%0)\n\t"
			"sb\t$0,0x06c(%0)\n\t"
		 	"sb\t$0,0x070(%0)\n\t"
			"sb\t$0,0x074(%0)\n\t"
			"sb\t$0,0x078(%0)\n\t"
			"sb\t$0,0x07c(%0)\n\t"
			: : "r" (p) );
		p += 0x080;
	}

	write_32bit_cp0_register(CP0_STATUS,flags);
}

static inline unsigned long get_phys_page (unsigned long addr,
					   struct mm_struct *mm)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long physpage;

	pgd = pgd_offset(mm, addr);
	pmd = pmd_offset(pgd, addr);
	pte = pte_offset(pmd, addr);

	if ((physpage = pte_val(*pte)) & _PAGE_VALID)
		return KSEG0ADDR(physpage & PAGE_MASK);

	return 0;
}

static inline void r3k_flush_cache_all(void)
{
	r3k_flush_icache_range(KSEG0, KSEG0 + icache_size);
}
 
static void r3k_flush_cache_mm(struct mm_struct *mm)
{
	if (mm->context != 0) {

#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r3k_flush_cache_all();
	}
}

static void r3k_flush_cache_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end)
{
	struct vm_area_struct *vma;

	if (mm->context == 0) 
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if (!vma)
		return;

	if (mm->context != current->active_mm->context) {
		flush_cache_all();
	} else {
		unsigned long flags, physpage;

		save_and_cli(flags);
		while (start < end) {
			if ((physpage = get_phys_page(start, mm)))
				r3k_flush_icache_range(physpage,
				                       physpage + PAGE_SIZE);
			start += PAGE_SIZE;
		}
		restore_flags(flags);
	}
}

static void r3k_flush_cache_page(struct vm_area_struct *vma,
				   unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	if (vma->vm_flags & VM_EXEC) {
		unsigned long physpage;

		if ((physpage = get_phys_page(page, vma->vm_mm)))
			r3k_flush_icache_range(physpage, physpage + PAGE_SIZE);
	}
}

static void r3k_flush_page_to_ram(struct page * page)
{
	/*
	 * Nothing to be done
	 */
}

static void r3k_flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long physpage;

	if (mm->context == 0)
		return;

	if (!(vma->vm_flags & VM_EXEC))
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif

	physpage = (unsigned long) page_address(page);
	if (physpage)
		r3k_flush_icache_range(physpage, physpage + PAGE_SIZE);
}

static void r3k_flush_cache_sigtramp(unsigned long addr)
{
	unsigned long flags;

#ifdef DEBUG_CACHE
	printk("csigtramp[%08lx]", addr);
#endif

	flags = read_32bit_cp0_register(CP0_STATUS);

	write_32bit_cp0_register(CP0_STATUS, flags&~ST0_IEC);

	/* Fill the TLB to avoid an exception with caches isolated. */
	asm ( 	"lw\t$0,0x000(%0)\n\t"
		"lw\t$0,0x004(%0)\n\t"
		: : "r" (addr) );

	write_32bit_cp0_register(CP0_STATUS, (ST0_ISC|ST0_SWC|flags)&~ST0_IEC);

	asm ( 	"sb\t$0,0x000(%0)\n\t"
		"sb\t$0,0x004(%0)\n\t"
		: : "r" (addr) );

	write_32bit_cp0_register(CP0_STATUS, flags);
}

static void r3k_dma_cache_wback_inv(unsigned long start, unsigned long size)
{
	wbflush();
	r3k_flush_dcache_range(start, start + size);
}

/* TLB operations. */
void flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	save_and_cli(flags);
	old_ctx = (get_entryhi() & 0xfc0);
	write_32bit_cp0_register(CP0_ENTRYLO0, 0);
	for (entry = 8; entry < mips_cpu.tlbsize; entry++) {
		write_32bit_cp0_register(CP0_INDEX, entry << 8);
		write_32bit_cp0_register(CP0_ENTRYHI, ((entry | 0x80000) << 12));
		__asm__ __volatile__("tlbwi");
	}
	set_entryhi(old_ctx);
	restore_flags(flags);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm->context != 0) {
		unsigned long flags;

#ifdef DEBUG_TLB
		printk("[tlbmm<%lu>]", (unsigned long) mm->context);
#endif
		save_and_cli(flags);
		get_new_mmu_context(mm, asid_cache);
		if (mm == current->active_mm)
			set_entryhi(mm->context & 0xfc0);
		restore_flags(flags);
	}
}

void flush_tlb_range(struct mm_struct *mm, unsigned long start,
                     unsigned long end)
{
	if (mm->context != 0) {
		unsigned long flags;
		int size;

#ifdef DEBUG_TLB
		printk("[tlbrange<%lu,0x%08lx,0x%08lx>]",
			(mm->context & 0xfc0), start, end);
#endif
		save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		if(size <= mips_cpu.tlbsize) {
			int oldpid = (get_entryhi() & 0xfc0);
			int newpid = (mm->context & 0xfc0);

			start &= PAGE_MASK;
			end += (PAGE_SIZE - 1);
			end &= PAGE_MASK;
			while(start < end) {
				int idx;

				set_entryhi(start | newpid);
				start += PAGE_SIZE;
				tlb_probe();
				idx = get_index();
				set_entrylo0(0);
				set_entryhi(KSEG0);
				if(idx < 0)
					continue;
				tlb_write_indexed();
			}
			set_entryhi(oldpid);
		} else {
			get_new_mmu_context(mm, asid_cache);
			if (mm == current->active_mm)
				set_entryhi(mm->context & 0xfc0);
		}
		restore_flags(flags);
	}
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if(vma->vm_mm->context != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%lu,0x%08lx>]", vma->vm_mm->context, page);
#endif
		newpid = (vma->vm_mm->context & 0xfc0);
		page &= PAGE_MASK;
		save_and_cli(flags);
		oldpid = (get_entryhi() & 0xfc0);
		set_entryhi(page | newpid);
		tlb_probe();
		idx = get_index();
		set_entrylo0(0);
		set_entryhi(KSEG0);
		if(idx < 0)
			goto finish;
		tlb_write_indexed();

finish:
		set_entryhi(oldpid);
		restore_flags(flags);
	}
}

/*
 * Initialize new page directory with pointers to invalid ptes
 */
void pgd_init(unsigned long page)
{
	unsigned long dummy1, dummy2;

	/*
	 * The plain and boring version for the R3000.  No cache flushing
	 * stuff is implemented since the R3000 has physical caches.
	 */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tsw\t%2,(%0)\n\t"
		"sw\t%2,4(%0)\n\t"
		"sw\t%2,8(%0)\n\t"
		"sw\t%2,12(%0)\n\t"
		"sw\t%2,16(%0)\n\t"
		"sw\t%2,20(%0)\n\t"
		"sw\t%2,24(%0)\n\t"
		"sw\t%2,28(%0)\n\t"
		"subu\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,32\n\t"
		".set\treorder"
		:"=r" (dummy1),
		 "=r" (dummy2)
		:"r" ((unsigned long) invalid_pte_table),
		 "0" (page),
		 "1" (PAGE_SIZE/(sizeof(pmd_t)*8)));
}

void update_mmu_cache(struct vm_area_struct * vma, unsigned long address,
                      pte_t pte)
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

	pid = get_entryhi() & 0xfc0;

#ifdef DEBUG_TLB
	if((pid != (vma->vm_mm->context & 0xfc0)) || (vma->vm_mm->context == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%lu tlbpid=%d\n",
		       (vma->vm_mm->context & 0xfc0), pid);
	}
#endif

	save_and_cli(flags);
	address &= PAGE_MASK;
	set_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	tlb_probe();
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	set_entrylo0(pte_val(*ptep));
	set_entryhi(address | (pid));
	if(idx < 0) {
		tlb_write_random();
#if 0
		printk("[MISS]");
#endif
	} else {
		tlb_write_indexed();
#if 0
		printk("[HIT]");
#endif
	}
	set_entryhi(pid);
	restore_flags(flags);
}

void show_regs(struct pt_regs * regs)
{
	/*
	 * Saved main processor registers
	 */
	printk("$0 : %08x %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       0, (unsigned long) regs->regs[1], (unsigned long) regs->regs[2],
	       (unsigned long) regs->regs[3], (unsigned long) regs->regs[4],
	       (unsigned long) regs->regs[5], (unsigned long) regs->regs[6],
	       (unsigned long) regs->regs[7]);
	printk("$8 : %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[8], (unsigned long) regs->regs[9],
	       (unsigned long) regs->regs[10], (unsigned long) regs->regs[11],
               (unsigned long) regs->regs[12], (unsigned long) regs->regs[13],
	       (unsigned long) regs->regs[14], (unsigned long) regs->regs[15]);
	printk("$16: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[16], (unsigned long) regs->regs[17],
	       (unsigned long) regs->regs[18], (unsigned long) regs->regs[19],
               (unsigned long) regs->regs[20], (unsigned long) regs->regs[21],
	       (unsigned long) regs->regs[22], (unsigned long) regs->regs[23]);
	printk("$24: %08lx %08lx                   %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[24], (unsigned long) regs->regs[25],
	       (unsigned long) regs->regs[28], (unsigned long) regs->regs[29],
               (unsigned long) regs->regs[30], (unsigned long) regs->regs[31]);

	/*
	 * Saved cp0 registers
	 */
	printk("epc  : %08lx    %s\nStatus: %08x\nCause : %08x\n",
	       (unsigned long) regs->cp0_epc,
	       print_tainted(),
	       (unsigned int) regs->cp0_status,
	       (unsigned int) regs->cp0_cause);
}

/* Todo: handle r4k-style TX39 TLB */
void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
                     unsigned long entryhi, unsigned long pagemask)
{
	unsigned long flags;
	unsigned long old_ctx;
	static unsigned long wired = 0;
	
	if (wired < 8) {
		save_and_cli(flags);
		old_ctx = get_entryhi() & 0xfc0;
		set_entrylo0(entrylo0);
		set_entryhi(entryhi);
		set_index(wired);
		wired++;
		tlb_write_indexed();
		set_entryhi(old_ctx);
	        flush_tlb_all();    
		restore_flags(flags);
	}
}

static void tx39_flush_icache_all(void )
{

	unsigned long start = KSEG0;
	unsigned long end = (start + icache_size);
	unsigned long dummy = 0;

	/* disable icache and stop streaming */
	__asm__ __volatile__(
	".set\tnoreorder\n\t"
	"mfc0\t%0,$3\n\t"
	"xori\t%0,32\n\t"
	"mtc0\t%0,$3\n\t"
	"j\t1f\n\t"
	"nop\n\t"
	"1:\t.set\treorder\n\t"
	: : "r"(dummy));

	/* invalidate icache */
	while (start < end) {
		cache16_unroll32(start,Index_Invalidate_I);
		start += 0x200;
	}

	/* enable icache */
	__asm__ __volatile__(
	".set\tnoreorder\n\t"
	"mfc0\t%0,$3\n\t"
	"xori\t%0,32\n\t"
	"mtc0\t%0,$3\n\t"
	".set\treorder\n\t"
	: : "r"(dummy));
}

static __init void tx39_probe_cache(void)
{
	unsigned long	config;

	config = read_32bit_cp0_register(CP0_CONF);

	icache_size = 1 << (10 + ((config >> 19) & 3));
	icache_lsize = 16;

	dcache_size = 1 << (10 + ((config >> 16) & 3));
	dcache_lsize = 4;
}

void __init ld_mmu_r23000(void)
{
	unsigned long config;

	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

	_clear_page = r3k_clear_page;
	_copy_page = r3k_copy_page;

	switch (mips_cpu.cputype) {
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_R3081:
	case CPU_R3081E:

		r3k_probe_cache();

		_flush_cache_all = r3k_flush_cache_all;
		___flush_cache_all = r3k_flush_cache_all;
		_flush_cache_mm = r3k_flush_cache_mm;
		_flush_cache_range = r3k_flush_cache_range;
		_flush_cache_page = r3k_flush_cache_page;
		_flush_cache_sigtramp = r3k_flush_cache_sigtramp;
		_flush_page_to_ram = r3k_flush_page_to_ram;
		_flush_icache_page = r3k_flush_icache_page;
		_flush_icache_range = r3k_flush_icache_range;

		_dma_cache_wback_inv = r3k_dma_cache_wback_inv;
		break;

	case CPU_TX3912:
	case CPU_TX3922:
	case CPU_TX3927:

		config=read_32bit_cp0_register(CP0_CONF);
		config &= ~TX39_CONF_WBON;
		write_32bit_cp0_register(CP0_CONF, config);

		tx39_probe_cache();

		_flush_cache_all = tx39_flush_icache_all;
		___flush_cache_all = tx39_flush_icache_all;
		_flush_cache_mm = tx39_flush_icache_all;
		_flush_cache_range = tx39_flush_icache_all;
		_flush_cache_page = tx39_flush_icache_all;
		_flush_cache_sigtramp = tx39_flush_icache_all;
		_flush_page_to_ram = r3k_flush_page_to_ram;
		_flush_icache_page = tx39_flush_icache_all;
		_flush_icache_range = tx39_flush_icache_all;

		_dma_cache_wback_inv = r3k_dma_cache_wback_inv;

		break;
	}

	printk("Primary instruction cache %dkb, linesize %d bytes\n",
		(int) (icache_size >> 10), (int) icache_lsize);
	printk("Primary data cache %dkb, linesize %d bytes\n",
		(int) (dcache_size >> 10), (int) dcache_lsize);

	flush_tlb_all();
}
