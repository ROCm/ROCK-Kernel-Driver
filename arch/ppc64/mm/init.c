/*
 *  
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>		/* for initrd_* */
#endif

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/abs_addr.h>
#include <asm/prom.h>
#include <asm/lmb.h>
#include <asm/rtas.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/Naca.h>
#ifdef CONFIG_PPC_EEH
#include <asm/eeh.h>
#endif

#include <asm/ppcdebug.h>

#define	PGTOKB(pages)	(((pages) * PAGE_SIZE) >> 10)

#ifdef CONFIG_PPC_ISERIES
#include <asm/iSeries/iSeries_dma.h>
#endif

struct mmu_context_queue_t mmu_context_queue;
int mem_init_done;
unsigned long ioremap_bot = IMALLOC_BASE;

static int boot_mapsize;
static unsigned long totalram_pages;

extern pgd_t swapper_pg_dir[];
extern char __init_begin, __init_end;
extern char __chrp_begin, __chrp_end;
extern char __openfirmware_begin, __openfirmware_end;
extern struct _of_tce_table of_tce_table[];
extern char _start[], _end[];
extern char _stext[], etext[];
extern struct task_struct *current_set[NR_CPUS];
extern struct Naca *naca;

void mm_init_ppc64(void);

unsigned long *pmac_find_end_of_memory(void);
extern unsigned long *find_end_of_memory(void);

extern pgd_t ioremap_dir[];
pgd_t * ioremap_pgd = (pgd_t *)&ioremap_dir;

static void map_io_page(unsigned long va, unsigned long pa, int flags);
extern void die_if_kernel(char *,struct pt_regs *,long);

unsigned long klimit = (unsigned long)_end;

HPTE *Hash=0;
unsigned long Hash_size=0;
unsigned long _SDR1=0;
unsigned long _ASR=0;

/* max amount of RAM to use */
unsigned long __max_memory;

/* This is declared as we are using the more or less generic 
 * include/asm-ppc64/tlb.h file -- tgall
 */
mmu_gather_t     mmu_gathers[NR_CPUS];

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld buffermem pages\n", nr_buffermem_pages());
}

void si_meminfo(struct sysinfo *val)
{
 	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = 0;
	val->freehigh = 0;
	val->mem_unit = PAGE_SIZE;
}

void *
ioremap(unsigned long addr, unsigned long size)
{
#ifdef CONFIG_PPC_ISERIES
	return (void*)addr;
#else
#ifdef CONFIG_PPC_EEH
	if(mem_init_done && (addr >> 60UL)) {
		if (IS_EEH_TOKEN_DISABLED(addr))
			return IO_TOKEN_TO_ADDR(addr);
		return (void*)addr; /* already mapped address or EEH token. */
	}
#endif
	return __ioremap(addr, size, _PAGE_NO_CACHE);
#endif
}

extern struct vm_struct * get_im_area( unsigned long size );

void *
__ioremap(unsigned long addr, unsigned long size, unsigned long flags)
{
	unsigned long pa, ea, i;

	/*
	 * Choose an address to map it to.
	 * Once the imalloc system is running, we use it.
	 * Before that, we map using addresses going
	 * up from ioremap_bot.  imalloc will use
	 * the addresses from ioremap_bot through
	 * IMALLOC_END (0xE000001fffffffff)
	 * 
	 */
	pa = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - pa;

	if (size == 0)
		return NULL;

	if (mem_init_done) {
		struct vm_struct *area;
		area = get_im_area(size);
		if (area == 0)
			return NULL;
		ea = (unsigned long)(area->addr);
	} 
	else {
		ea = ioremap_bot;
		ioremap_bot += size;
        }

	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);
	if (flags & (_PAGE_NO_CACHE | _PAGE_WRITETHRU))
		flags |= _PAGE_GUARDED;

	for (i = 0; i < size; i += PAGE_SIZE) {
		map_io_page(ea+i, pa+i, flags);
	}

	return (void *) (ea + (addr & ~PAGE_MASK));
}

void iounmap(void *addr) 
{
#ifdef CONFIG_PPC_ISERIES
     /* iSeries I/O Remap is a noop              */
	return;
#else 
	/* DRENG / PPPBBB todo */
	return;
#endif
}

/*
 * map_io_page currently only called by __ioremap
 * map_io_page adds an entry to the ioremap page table
 * and adds an entry to the HPT, possibly bolting it
 */
static void map_io_page(unsigned long ea, unsigned long pa, int flags)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long vsid;
	
	if (mem_init_done) {
		spin_lock(&ioremap_mm.page_table_lock);
		pgdp = pgd_offset_i(ea);
		pmdp = pmd_alloc(&ioremap_mm, pgdp, ea);
		ptep = pte_alloc_kernel(&ioremap_mm, pmdp, ea);

		pa = absolute_to_phys(pa);
		set_pte(ptep, mk_pte_phys(pa & PAGE_MASK, __pgprot(flags)));
		spin_unlock(&ioremap_mm.page_table_lock);
	} else {
		/* If the mm subsystem is not fully up, we cannot create a
		 * linux page table entry for this mapping.  Simply bolt an
		 * entry in the hardware page table. 
 		 */
		vsid = get_kernel_vsid(ea);
		ppc_md.make_pte(htab_data.htab,
			(vsid << 28) | (ea & 0xFFFFFFF), // va (NOT the ea)
			pa, 
			_PAGE_NO_CACHE | _PAGE_GUARDED | PP_RWXX,
			htab_data.htab_hash_mask, 0);
	}
}

void
flush_tlb_mm(struct mm_struct *mm)
{
	if (mm->map_count) {
		struct vm_area_struct *mp;
		for (mp = mm->mmap; mp != NULL; mp = mp->vm_next)
			__flush_tlb_range(mm, mp->vm_start, mp->vm_end);
	} else {
		/* MIKEC: It is not clear why this is needed */
		/* paulus: it is needed to clear out stale HPTEs
		 * when an address space (represented by an mm_struct)
		 * is being destroyed. */
		__flush_tlb_range(mm, USER_START, USER_END);
	}

	/* XXX are there races with checking cpu_vm_mask? - Anton */
	mm->cpu_vm_mask = 0;
}

/*
 * Callers should hold the mm->page_table_lock
 */
void
flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	unsigned long context = 0;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep;
	pte_t pte;
	int local = 0;

	switch( REGION_ID(vmaddr) ) {
	case VMALLOC_REGION_ID:
		pgd = pgd_offset_k( vmaddr );
		break;
	case IO_REGION_ID:
		pgd = pgd_offset_i( vmaddr );
		break;
	case USER_REGION_ID:
		pgd = pgd_offset( vma->vm_mm, vmaddr );
		context = vma->vm_mm->context;

		/* XXX are there races with checking cpu_vm_mask? - Anton */
		if (vma->vm_mm->cpu_vm_mask == (1 << smp_processor_id()))
			local = 1;

		break;
	default:
		panic("flush_tlb_page: invalid region 0x%016lx", vmaddr);
	
	}

	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, vmaddr);
		if (!pmd_none(*pmd)) {
			ptep = pte_offset_kernel(pmd, vmaddr);
			/* Check if HPTE might exist and flush it if so */
			pte = __pte(pte_update(ptep, _PAGE_HPTEFLAGS, 0));
			if ( pte_val(pte) & _PAGE_HASHPTE ) {
				flush_hash_page(context, vmaddr, pte, local);
			}
		}
	}
}

struct tlb_batch_data tlb_batch_array[NR_CPUS][MAX_BATCH_FLUSH];

void
__flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep;
	pte_t pte;
	unsigned long pgd_end, pmd_end;
	unsigned long context;
	int i = 0;
	struct tlb_batch_data *ptes = &tlb_batch_array[smp_processor_id()][0];
	int local = 0;

	if ( start >= end )
		panic("flush_tlb_range: start (%016lx) greater than end (%016lx)\n", start, end );

	if ( REGION_ID(start) != REGION_ID(end) )
		panic("flush_tlb_range: start (%016lx) and end (%016lx) not in same region\n", start, end );
	
	context = 0;

	switch( REGION_ID(start) ) {
	case VMALLOC_REGION_ID:
		pgd = pgd_offset_k( start );
		break;
	case IO_REGION_ID:
		pgd = pgd_offset_i( start );
		break;
	case USER_REGION_ID:
		pgd = pgd_offset( mm, start );
		context = mm->context;

		/* XXX are there races with checking cpu_vm_mask? - Anton */
		if (mm->cpu_vm_mask == (1 << smp_processor_id())) {
			local = 1;
		}

		break;
	default:
		panic("flush_tlb_range: invalid region for start (%016lx) and end (%016lx)\n", start, end);
	
	}

	do {
		pgd_end = (start + PGDIR_SIZE) & PGDIR_MASK;
		if ( pgd_end > end ) 
			pgd_end = end;
		if ( !pgd_none( *pgd ) ) {
			pmd = pmd_offset( pgd, start );
			do {
				pmd_end = ( start + PMD_SIZE ) & PMD_MASK;
				if ( pmd_end > end )
					pmd_end = end;
				if ( !pmd_none( *pmd ) ) {
					ptep = pte_offset_kernel( pmd, start );
					do {
						if ( pte_val(*ptep) & _PAGE_HASHPTE ) {
							pte = __pte(pte_update(ptep, _PAGE_HPTEFLAGS, 0));
							if ( pte_val(pte) & _PAGE_HASHPTE ) {
								ptes->pte = pte;
								ptes->addr = start;
								ptes++;
								i++;
								if (i == MAX_BATCH_FLUSH) {
									flush_hash_range(context, MAX_BATCH_FLUSH, local);
									i = 0;
									ptes = &tlb_batch_array[smp_processor_id()][0];
								}
							}
						}
						start += PAGE_SIZE;
						++ptep;
					} while ( start < pmd_end );
				}
				else
					start = pmd_end;
				++pmd;
			} while ( start < pgd_end );
		}
		else
			start = pgd_end;
		++pgd;
	} while ( start < end );

	if (i)
		flush_hash_range(context, i, local);
}


void __init free_initmem(void)
{
	unsigned long a;
	unsigned long num_freed_pages = 0;
#define FREESEC(START,END,CNT) do { \
	a = (unsigned long)(&START); \
	for (; a < (unsigned long)(&END); a += PAGE_SIZE) { \
	  	clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags); \
		set_page_count(mem_map+MAP_NR(a), 1); \
		free_page(a); \
		CNT++; \
	} \
} while (0)

	FREESEC(__init_begin,__init_end,num_freed_pages);

	printk ("Freeing unused kernel memory: %ldk init\n",
		PGTOKB(num_freed_pages));
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	unsigned long xstart = start;
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(mem_map + MAP_NR(start));
		set_page_count(mem_map+MAP_NR(start), 1);
		free_page(start);
		totalram_pages++;
	}
	printk ("Freeing initrd memory: %ldk freed\n", (end - xstart) >> 10);
}
#endif



/*
 * Do very early mm setup.
 */
void __init mm_init_ppc64(void) {
	struct Paca *paca;
	unsigned long guard_page, index;

	ppc_md.progress("MM:init", 0);

	/* Reserve all contexts < FIRST_USER_CONTEXT for kernel use.
	 * The range of contexts [FIRST_USER_CONTEXT, NUM_USER_CONTEXT)
	 * are stored on a stack/queue for easy allocation and deallocation.
	 */
        mmu_context_queue.lock = SPIN_LOCK_UNLOCKED;
        mmu_context_queue.head = 0;
        mmu_context_queue.tail = NUM_USER_CONTEXT-1;
        mmu_context_queue.size = NUM_USER_CONTEXT;
	for(index=0; index < NUM_USER_CONTEXT ;index++) {
                mmu_context_queue.elements[index] = index+FIRST_USER_CONTEXT;
	}

	/* Setup guard pages for the Paca's */
	for (index = 0; index < NR_CPUS; index++) {
		paca = &xPaca[index];
		guard_page = ((unsigned long)paca) + 0x1000;
		ppc_md.hpte_updateboltedpp(PP_RXRX, guard_page);
	}

	ppc_md.progress("MM:exit", 0x211);
}



/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
void __init do_init_bootmem(void)
{
	unsigned long i;
	unsigned long start, bootmap_pages;
	unsigned long total_pages = lmb_end_of_DRAM() >> PAGE_SHIFT;

	PPCDBG(PPCDBG_MMINIT, "do_init_bootmem: start\n");
	/*
	 * Find an area to use for the bootmem bitmap.  Calculate the size of
	 * bitmap required as (Total Memory) / PAGE_SIZE / BITS_PER_BYTE.
	 * Add 1 additional page in case the address isn't page-aligned.
	 */
	bootmap_pages = bootmem_bootmap_pages(total_pages);

	start = (unsigned long)__a2p(lmb_alloc(bootmap_pages<<PAGE_SHIFT, PAGE_SIZE));
	if( start == 0 ) {
		udbg_printf("do_init_bootmem: failed to allocate a bitmap.\n");
		udbg_printf("\tbootmap_pages = 0x%lx.\n", bootmap_pages);
		PPCDBG_ENTER_DEBUGGER(); 
	}

	PPCDBG(PPCDBG_MMINIT, "\tstart               = 0x%lx\n", start);
	PPCDBG(PPCDBG_MMINIT, "\tbootmap_pages       = 0x%lx\n", bootmap_pages);
	PPCDBG(PPCDBG_MMINIT, "\tphysicalMemorySize  = 0x%lx\n", naca->physicalMemorySize);

	boot_mapsize = init_bootmem(start >> PAGE_SHIFT, total_pages);
	PPCDBG(PPCDBG_MMINIT, "\tboot_mapsize        = 0x%lx\n", boot_mapsize);

	/* add all physical memory to the bootmem map */
	for (i=0; i < lmb.memory.cnt ;i++) {
		unsigned long physbase, size;
		unsigned long type = lmb.memory.region[i].type;

		if ( type != LMB_MEMORY_AREA )
			continue;

		physbase = lmb.memory.region[i].physbase;
		size = lmb.memory.region[i].size;
		free_bootmem(physbase, size);
	}
	/* reserve the sections we're already using */
	for (i=0; i < lmb.reserved.cnt ;i++) {
		unsigned long physbase = lmb.reserved.region[i].physbase;
		unsigned long size = lmb.reserved.region[i].size;
#if 0 /* PPPBBB */
		if ( (physbase == 0) && (size < (16<<20)) ) {
			size = 16 << 20;
		}
#endif
		reserve_bootmem(physbase, size);
	}

	PPCDBG(PPCDBG_MMINIT, "do_init_bootmem: end\n");
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES], i;

	/*
	 * All pages are DMA-able so we put them all in the DMA zone.
	 */
	zones_size[0] = lmb_end_of_DRAM() >> PAGE_SHIFT;
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;
	free_area_init(zones_size);
}

extern unsigned long prof_shift;
extern unsigned long prof_len;
extern unsigned int * prof_buffer;
extern unsigned long dprof_shift;
extern unsigned long dprof_len;
extern unsigned int * dprof_buffer;

void initialize_paca_hardware_interrupt_stack(void);

void __init mem_init(void)
{
	extern char *sysmap; 
	extern unsigned long sysmap_size;
	unsigned long addr;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long va_rtas_base = (unsigned long)__va(rtas.base);

	max_mapnr = max_low_pfn;
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);
	num_physpages = max_mapnr;	/* RAM is assumed contiguous */
	max_pfn = max_low_pfn;

	totalram_pages += free_all_bootmem();

	ifppcdebug(PPCDBG_MMINIT) {
		udbg_printf("mem_init: totalram_pages = 0x%lx\n", totalram_pages);
		udbg_printf("mem_init: va_rtas_base   = 0x%lx\n", va_rtas_base); 
		udbg_printf("mem_init: va_rtas_end    = 0x%lx\n", PAGE_ALIGN(va_rtas_base+rtas.size)); 
		udbg_printf("mem_init: pinned start   = 0x%lx\n", __va(0)); 
		udbg_printf("mem_init: pinned end     = 0x%lx\n", PAGE_ALIGN(klimit)); 
	}

	if ( sysmap_size )
		for (addr = (unsigned long)sysmap;
		     addr < PAGE_ALIGN((unsigned long)sysmap+sysmap_size) ;
		     addr += PAGE_SIZE)
			SetPageReserved(mem_map + MAP_NR(addr));
	
	for (addr = KERNELBASE; addr <= (unsigned long)__va(lmb_end_of_DRAM());
	     addr += PAGE_SIZE) {
		if (!PageReserved(mem_map + MAP_NR(addr)))
			continue;
		if (addr < (ulong) etext)
			codepages++;

		else if (addr >= (unsigned long)&__init_begin
			 && addr < (unsigned long)&__init_end)
			initpages++;
		else if (addr < klimit)
			datapages++;
	}

        printk("Memory: %luk available (%dk kernel code, %dk data, %dk init) [%08lx,%08lx]\n",
	       (unsigned long)nr_free_pages()<< (PAGE_SHIFT-10),
	       codepages<< (PAGE_SHIFT-10), datapages<< (PAGE_SHIFT-10),
	       initpages<< (PAGE_SHIFT-10),
	       PAGE_OFFSET, (unsigned long)__va(lmb_end_of_DRAM()));
	mem_init_done = 1;

	/* set the last page of each hardware interrupt stack to be protected */
	initialize_paca_hardware_interrupt_stack();

#ifdef CONFIG_PPC_ISERIES
	create_virtual_bus_tce_table();
	/* HACK HACK This allows the iSeries profiling to use /proc/profile */
	prof_shift = dprof_shift;
	prof_len = dprof_len;
	prof_buffer = dprof_buffer;
#endif
}

/*
 * This is called when a page has been modified by the kernel.
 * It just marks the page as not i-cache clean.  We do the i-cache
 * flush later when the page is given to a user process, if necessary.
 */
void flush_dcache_page(struct page *page)
{
	clear_bit(PG_arch_1, &page->flags);
}

void flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	if (page->mapping && !PageReserved(page)
	    && !test_bit(PG_arch_1, &page->flags)) {
		__flush_dcache_icache(page_address(page));
		set_bit(PG_arch_1, &page->flags);
	}
}

void clear_user_page(void *page, unsigned long vaddr)
{
	clear_page(page);
}

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr)
{
	copy_page(vto, vfrom);
	__flush_dcache_icache(vto);
}

void flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			     unsigned long addr, int len)
{
	unsigned long maddr;

	maddr = (unsigned long)page_address(page) + (addr & ~PAGE_MASK);
	flush_icache_range(maddr, maddr + len);
}

extern pte_t *find_linux_pte(pgd_t *pgdir, unsigned long ea);
int __hash_page(unsigned long ea, unsigned long access, unsigned long vsid,
		pte_t *ptep);

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux PTE.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long ea,
		      pte_t pte)
{
	unsigned long vsid;
	void *pgdir;
	pte_t *ptep;

	/* We only want HPTEs for linux PTEs that have _PAGE_ACCESSED set */
	if (!pte_young(pte))
		return;

	pgdir = vma->vm_mm->pgd;
	if (pgdir == NULL)
		return;

	ptep = find_linux_pte(pgdir, ea);
	vsid = get_vsid(vma->vm_mm->context, ea);

	__hash_page(ea, pte_val(pte) & (_PAGE_USER|_PAGE_RW), vsid, ptep);
}
