/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000 by Ralf Baechle at alii
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGTABLE_H
#define _ASM_PGTABLE_H

#include <asm/addrspace.h>
#include <asm/page.h>

#ifndef _LANGUAGE_ASSEMBLY

#include <linux/linkage.h>
#include <linux/config.h>
#include <linux/mmzone.h>
#include <asm/cachectl.h>

/* Cache flushing:
 *
 *  - flush_cache_all() flushes entire cache
 *  - flush_cache_mm(mm) flushes the specified mm context's cache lines
 *  - flush_cache_page(mm, vmaddr) flushes a single page
 *  - flush_cache_range(mm, start, end) flushes a range of pages
 *  - flush_page_to_ram(page) write back kernel page to ram
 */
extern void (*_flush_cache_mm)(struct mm_struct *mm);
extern void (*_flush_cache_range)(struct mm_struct *mm, unsigned long start,
                                 unsigned long end);
extern void (*_flush_cache_page)(struct vm_area_struct *vma, unsigned long page);
extern void (*_flush_page_to_ram)(struct page * page);

#define flush_cache_all()		do { } while(0)
#define flush_dcache_page(page)		do { } while (0)

#ifndef CONFIG_CPU_R10000
#define flush_cache_mm(mm)		_flush_cache_mm(mm)
#define flush_cache_range(mm,start,end)	_flush_cache_range(mm,start,end)
#define flush_cache_page(vma,page)	_flush_cache_page(vma, page)
#define flush_page_to_ram(page)		_flush_page_to_ram(page)

#define flush_icache_range(start, end)	_flush_cache_l1()

#define flush_icache_page(vma, page)					\
do {									\
	unsigned long addr;						\
	addr = (unsigned long) page_address(page);			\
	_flush_cache_page(vma, addr);					\
} while (0)                                                              
#else /* !CONFIG_CPU_R10000 */
/*
 * Since the r10k handles VCEs in hardware, most of the flush cache
 * routines are not needed. Only the icache on a processor is not
 * coherent with the dcache of the _same_ processor, so we must flush
 * the icache so that it does not contain stale contents of physical
 * memory. No flushes are needed for dma coherency, since the o200s 
 * are io coherent. The only place where we might be overoptimizing 
 * out icache flushes are from mprotect (when PROT_EXEC is added).
 */
extern void andes_flush_icache_page(unsigned long);
#define flush_cache_mm(mm)		do { } while(0)
#define flush_cache_range(mm,start,end)	do { } while(0)
#define flush_cache_page(vma,page)	do { } while(0)
#define flush_page_to_ram(page)		do { } while(0)
#define flush_icache_range(start, end)	_flush_cache_l1()
#define flush_icache_page(vma, page)					\
do {									\
	if ((vma)->vm_flags & VM_EXEC)					\
		andes_flush_icache_page(page_address(page));		\
} while (0)
#endif /* !CONFIG_CPU_R10000 */

/*
 * The foll cache flushing routines are MIPS specific.
 * flush_cache_l2 is needed only during initialization.
 */
extern void (*_flush_cache_sigtramp)(unsigned long addr);
extern void (*_flush_cache_l2)(void);
extern void (*_flush_cache_l1)(void);

#define flush_cache_sigtramp(addr)	_flush_cache_sigtramp(addr)
#define flush_cache_l2()		_flush_cache_l2()
#define flush_cache_l1()		_flush_cache_l1()

/*
 * Each address space has 2 4K pages as its page directory, giving 1024
 * (== PTRS_PER_PGD) 8 byte pointers to pmd tables. Each pmd table is a
 * pair of 4K pages, giving 1024 (== PTRS_PER_PMD) 8 byte pointers to
 * page tables. Each page table is a single 4K page, giving 512 (==
 * PTRS_PER_PTE) 8 byte ptes. Each pgde is initialized to point to
 * invalid_pmd_table, each pmde is initialized to point to 
 * invalid_pte_table, each pte is initialized to 0. When memory is low,
 * and a pmd table or a page table allocation fails, empty_bad_pmd_table
 * and empty_bad_page_table is returned back to higher layer code, so
 * that the failure is recognized later on. Linux does not seem to 
 * handle these failures very well though. The empty_bad_page_table has
 * invalid pte entries in it, to force page faults.
 * Vmalloc handling: vmalloc uses swapper_pg_dir[0] (returned by 
 * pgd_offset_k), which is initalized to point to kpmdtbl. kpmdtbl is 
 * the only single page pmd in the system. kpmdtbl entries point into 
 * kptbl[] array. We reserve 1<<KPTBL_PAGE_ORDER pages to hold the
 * vmalloc range translations, which the fault handler looks at.
 */

#endif /* !defined (_LANGUAGE_ASSEMBLY) */

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT - 3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PMD_SHIFT + (PAGE_SHIFT + 1 - 3))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Entries per page directory level: we use two-level, so we don't really
   have any PMD directory physically.  */
#define PTRS_PER_PGD	1024
#define PTRS_PER_PMD	1024
#define PTRS_PER_PTE	512
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)
#define FIRST_USER_PGD_NR	0

#define KPTBL_PAGE_ORDER  1
#define VMALLOC_START     XKSEG
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END       \
  (VMALLOC_START + ((1 << KPTBL_PAGE_ORDER) * PTRS_PER_PTE * PAGE_SIZE))

/* Note that we shift the lower 32bits of each EntryLo[01] entry
 * 6 bits to the left. That way we can convert the PFN into the
 * physical address by a single 'and' operation and gain 6 additional
 * bits for storing information which isn't present in a normal
 * MIPS page table.
 *
 * Similar to the Alpha port, we need to keep track of the ref
 * and mod bits in software.  We have a software "yeah you can read
 * from this page" bit, and a hardware one which actually lets the
 * process read from the page.  On the same token we have a software
 * writable bit and the real hardware one which actually lets the
 * process write to the page, this keeps a mod bit via the hardware
 * dirty bit.
 *
 * Certain revisions of the R4000 and R5000 have a bug where if a
 * certain sequence occurs in the last 3 instructions of an executable
 * page, and the following page is not mapped, the cpu can do
 * unpredictable things.  The code (when it is written) to deal with
 * this problem will be in the update_mmu_cache() code for the r4k.
 */
#define _PAGE_PRESENT               (1<<0)  /* implemented in software */
#define _PAGE_READ                  (1<<1)  /* implemented in software */
#define _PAGE_WRITE                 (1<<2)  /* implemented in software */
#define _PAGE_ACCESSED              (1<<3)  /* implemented in software */
#define _PAGE_MODIFIED              (1<<4)  /* implemented in software */
#define _PAGE_R4KBUG                (1<<5)  /* workaround for r4k bug  */
#define _PAGE_GLOBAL                (1<<6)
#define _PAGE_VALID                 (1<<7)
#define _PAGE_SILENT_READ           (1<<7)  /* synonym                 */
#define _PAGE_DIRTY                 (1<<8)  /* The MIPS dirty bit      */
#define _PAGE_SILENT_WRITE          (1<<8)
#define _CACHE_CACHABLE_NO_WA       (0<<9)  /* R4600 only              */
#define _CACHE_CACHABLE_WA          (1<<9)  /* R4600 only              */
#define _CACHE_UNCACHED             (2<<9)  /* R4[0246]00              */
#define _CACHE_CACHABLE_NONCOHERENT (3<<9)  /* R4[0246]00              */
#define _CACHE_CACHABLE_CE          (4<<9)  /* R4[04]00 only           */
#define _CACHE_CACHABLE_COW         (5<<9)  /* R4[04]00 only           */
#define _CACHE_CACHABLE_CUW         (6<<9)  /* R4[04]00 only           */
#define _CACHE_CACHABLE_ACCELERATED (7<<9)  /* R10000 only             */
#define _CACHE_MASK                 (7<<9)

#define __READABLE	(_PAGE_READ | _PAGE_SILENT_READ | _PAGE_ACCESSED)
#define __WRITEABLE	(_PAGE_WRITE | _PAGE_SILENT_WRITE | _PAGE_MODIFIED)

#define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_MODIFIED | _CACHE_MASK)

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _CACHE_CACHABLE_COW)
#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
			_CACHE_CACHABLE_COW)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_READ | \
			_CACHE_CACHABLE_COW)
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_READ | \
			_CACHE_CACHABLE_COW)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
			_CACHE_CACHABLE_COW)
#define PAGE_USERIO     __pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
			_CACHE_UNCACHED)
#define PAGE_KERNEL_UNCACHED __pgprot(_PAGE_PRESENT | __READABLE | __WRITEABLE | \
			_CACHE_UNCACHED)

/*
 * MIPS can't do page protection for execute, and considers that the same like
 * read. Also, write permissions imply read permissions. This is the closest
 * we can get by reasonable means..
 */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY
#define __P101	PAGE_READONLY
#define __P110	PAGE_COPY
#define __P111	PAGE_COPY

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY
#define __S101	PAGE_READONLY
#define __S110	PAGE_SHARED
#define __S111	PAGE_SHARED

#if !defined (_LANGUAGE_ASSEMBLY)

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t *__bad_pagetable(void);
extern pmd_t *__bad_pmd_table(void);

extern unsigned long empty_zero_page;
extern unsigned long zero_page_mask;

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PMDTABLE __bad_pmd_table()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE(vaddr) \
	(virt_to_page(empty_zero_page + (((unsigned long)(vaddr)) & zero_page_mask)))

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR			(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK			(~(sizeof(void*)-1))

/*
 * sizeof(void*) == (1 << SIZEOF_PTR_LOG2)
 */
#define SIZEOF_PTR_LOG2			3

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

extern pte_t invalid_pte_table[PAGE_SIZE/sizeof(pte_t)];
extern pte_t empty_bad_page_table[PAGE_SIZE/sizeof(pte_t)];
extern pmd_t invalid_pmd_table[2*PAGE_SIZE/sizeof(pmd_t)];
extern pmd_t empty_bad_pmd_table[2*PAGE_SIZE/sizeof(pmd_t)];

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern inline unsigned long pmd_page(pmd_t pmd)
{
	return pmd_val(pmd);
}

extern inline unsigned long pgd_page(pgd_t pgd)
{
	return pgd_val(pgd);
}

extern inline void pmd_set(pmd_t * pmdp, pte_t * ptep)
{
	pmd_val(*pmdp) = (((unsigned long) ptep) & PAGE_MASK);
}

extern inline void pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	pgd_val(*pgdp) = (((unsigned long) pmdp) & PAGE_MASK);
}

extern inline int pte_none(pte_t pte)
{
	return !pte_val(pte);
}

extern inline int pte_present(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
extern inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

extern inline void pte_clear(pte_t *ptep)
{
	set_pte(ptep, __pte(0));
}

/*
 * Empty pmd entries point to the invalid_pte_table.
 */
extern inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == (unsigned long) invalid_pte_table;
}

extern inline int pmd_bad(pmd_t pmd)
{
	return pmd_val(pmd) == (unsigned long) empty_bad_page_table;
}

extern inline void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = ((unsigned long) invalid_pte_table);
}

/*
 * Empty pgd entries point to the invalid_pmd_table.
 */
extern inline int pgd_none(pgd_t pgd)
{
	return pgd_val(pgd) == (unsigned long) invalid_pmd_table;
}

extern inline int pgd_bad(pgd_t pgd)
{
	return pgd_val(pgd) == (unsigned long) empty_bad_pmd_table;
}

extern inline void pgd_clear(pgd_t *pgdp)
{
	pgd_val(*pgdp) = ((unsigned long) invalid_pmd_table);
}

/*
 * Permanent address of a page.  On MIPS64 we never have highmem, so this
 * is simple.
 * called on a highmem page.
 */
#define page_address(page)	((page)->virtual)
#ifndef CONFIG_DISCONTIGMEM
#define pte_page(x)		(mem_map+(unsigned long)((pte_val(x) >> PAGE_SHIFT)))
#else
#define mips64_pte_pagenr(x) \
	(PLAT_NODE_DATA_STARTNR(PHYSADDR_TO_NID(pte_val(x))) + \
	PLAT_NODE_DATA_LOCALNR(pte_val(x), PHYSADDR_TO_NID(pte_val(x))))
#define pte_page(x)		(mem_map+mips64_pte_pagenr(x))
#endif

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)
{
	return pte_val(pte) & _PAGE_READ;
}

extern inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_WRITE;
}

extern inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_MODIFIED;
}

extern inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

extern inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_WRITE | _PAGE_SILENT_WRITE);
	return pte;
}

extern inline pte_t pte_rdprotect(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_READ | _PAGE_SILENT_READ);
	return pte;
}

extern inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_MODIFIED|_PAGE_SILENT_WRITE);
	return pte;
}

extern inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~(_PAGE_ACCESSED|_PAGE_SILENT_READ);
	return pte;
}

extern inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_MODIFIED)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}

extern inline pte_t pte_mkread(pte_t pte)
{
	pte_val(pte) |= _PAGE_READ;
	if (pte_val(pte) & _PAGE_ACCESSED)
		pte_val(pte) |= _PAGE_SILENT_READ;
	return pte;
}

extern inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_MODIFIED;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) |= _PAGE_SILENT_WRITE;
	return pte;
}

extern inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	if (pte_val(pte) & _PAGE_READ)
		pte_val(pte) |= _PAGE_SILENT_READ;
	return pte;
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#ifndef CONFIG_DISCONTIGMEM
#define PAGE_TO_PA(page)	((page - mem_map) << PAGE_SHIFT)
#else
#define PAGE_TO_PA(page) \
		((((page)-(page)->zone->zone_mem_map) << PAGE_SHIFT) \
		+ ((page)->zone->zone_start_paddr))
#endif
#define mk_pte(page, pgprot)						\
({									\
	pte_t	__pte;							\
									\
	pte_val(__pte) = ((unsigned long)(PAGE_TO_PA(page))) |		\
						pgprot_val(pgprot);	\
									\
	__pte;								\
})

extern inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
	return __pte(physpage | pgprot_val(pgprot));
}

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

#define page_pte(page) page_pte_prot(page, __pgprot(0))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, 0)

#define pgd_index(address)	((address >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))

/* to find an entry in a page-table-directory */
extern inline pgd_t *pgd_offset(struct mm_struct *mm, unsigned long address)
{
	return mm->pgd + pgd_index(address);
}

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) pgd_page(*dir) +
	       ((address >> PMD_SHIFT) & (PTRS_PER_PMD - 1));
}

/* Find an entry in the third-level page table.. */ 
extern inline pte_t *pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) (pmd_page(*dir)) +
	       ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

/*
 * Initialize a new pgd / pmd table with invalid pointers.
 */
extern void pgd_init(unsigned long page);
extern void pmd_init(unsigned long page, unsigned long pagetable);

extern pgd_t swapper_pg_dir[1024];
extern void paging_init(void);

extern void (*update_mmu_cache)(struct vm_area_struct *vma,
				unsigned long address, pte_t pte);

/*
 * Non-present pages:  high 24 bits are offset, next 8 bits type,
 * low 32 bits zero.
 */
extern inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = (type << 32) | (offset << 40); return pte; }

#define SWP_TYPE(x)		(((x).val >> 32) & 0xff)
#define SWP_OFFSET(x)		((x).val >> 40)
#define SWP_ENTRY(type,offset)	((swp_entry_t) { pte_val(mk_swap_pte((type),(offset))) })
#define pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define swp_entry_to_pte(x)	((pte_t) { (x).val })

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)
#ifndef CONFIG_DISCONTIGMEM
#define kern_addr_valid(addr)	(1)
#endif

/* TLB operations. */
extern inline void tlb_probe(void)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbp\n\t"
		".set reorder");
}

extern inline void tlb_read(void)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbr\n\t"
		".set reorder");
}

extern inline void tlb_write_indexed(void)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbwi\n\t"
		".set reorder");
}

extern inline void tlb_write_random(void)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"tlbwr\n\t"
		".set reorder");
}

/* Dealing with various CP0 mmu/cache related registers. */

/* CP0_PAGEMASK register */
extern inline unsigned long get_pagemask(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"mfc0 %0, $5\n\t"
		".set reorder"
		: "=r" (val));
	return val;
}

extern inline void set_pagemask(unsigned long val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"mtc0 %0, $5\n\t"
		".set reorder"
		: : "r" (val));
}

/* CP0_ENTRYLO0 and CP0_ENTRYLO1 registers */
extern inline unsigned long get_entrylo0(void)
{
	unsigned long val;

	__asm__ __volatile__(	
		".set noreorder\n\t"
		"dmfc0 %0, $2\n\t"
		".set reorder"
		: "=r" (val));
	return val;
}

extern inline void set_entrylo0(unsigned long val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"dmtc0 %0, $2\n\t"
		".set reorder"
		: : "r" (val));
}

extern inline unsigned long get_entrylo1(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"dmfc0 %0, $3\n\t"
		".set reorder" : "=r" (val));

	return val;
}

extern inline void set_entrylo1(unsigned long val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"dmtc0 %0, $3\n\t"
		".set reorder"
		: : "r" (val));
}

/* CP0_ENTRYHI register */
extern inline unsigned long get_entryhi(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"dmfc0 %0, $10\n\t"
		".set reorder"
		: "=r" (val));

	return val;
}

extern inline void set_entryhi(unsigned long val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"dmtc0 %0, $10\n\t"
		".set reorder"
		: : "r" (val));
}

/* CP0_INDEX register */
extern inline unsigned int get_index(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"mfc0 %0, $0\n\t"
		".set reorder"
		: "=r" (val));
	return val;
}

extern inline void set_index(unsigned int val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"mtc0 %0, $0\n\t"
		".set reorder\n\t"
		: : "r" (val));
}

/* CP0_WIRED register */
extern inline unsigned long get_wired(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"mfc0 %0, $6\n\t"
		".set reorder\n\t"
		: "=r" (val));
	return val;
}

extern inline void set_wired(unsigned long val)
{
	__asm__ __volatile__(
		"\n\t.set noreorder\n\t"
		"mtc0 %0, $6\n\t"
		".set reorder"
		: : "r" (val));
}

extern inline unsigned long get_info(void)
{
	unsigned long val;

	__asm__(
		".set push\n\t"
		".set reorder\n\t"
		"mfc0 %0, $7\n\t"
		".set pop"
		: "=r" (val));
	return val;
}

/* CP0_TAGLO and CP0_TAGHI registers */
extern inline unsigned long get_taglo(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"mfc0 %0, $28\n\t"
		".set reorder"
		: "=r" (val));
	return val;
}

extern inline void set_taglo(unsigned long val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"mtc0 %0, $28\n\t"
		".set reorder"
		: : "r" (val));
}

extern inline unsigned long get_taghi(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"mfc0 %0, $29\n\t"
		".set reorder"
		: "=r" (val));
	return val;
}

extern inline void set_taghi(unsigned long val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"mtc0 %0, $29\n\t"
		".set reorder"
		: : "r" (val));
}

/* CP0_CONTEXT register */
extern inline unsigned long get_context(void)
{
	unsigned long val;

	__asm__ __volatile__(
		".set noreorder\n\t"
		"mfc0 %0, $4\n\t"
		".set reorder"
		: "=r" (val));

	return val;
}

extern inline void set_context(unsigned long val)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		"mtc0 %0, $4\n\t"
		".set reorder"
		: : "r" (val));
}

#include <asm-generic/pgtable.h>

#endif /* !defined (_LANGUAGE_ASSEMBLY) */

#endif /* _ASM_PGTABLE_H */
