#ifdef __KERNEL__
#ifndef _PPC_PGTABLE_H
#define _PPC_PGTABLE_H

#include <linux/config.h>

#ifndef __ASSEMBLY__
#include <linux/sched.h>
#include <linux/threads.h>
#include <asm/processor.h>		/* For TASK_SIZE */
#include <asm/mmu.h>
#include <asm/page.h>

#if defined(CONFIG_4xx)
extern void local_flush_tlb_all(void);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void local_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end);
extern inline void flush_hash_page(unsigned context, unsigned long va)
	{ }
#elif defined(CONFIG_8xx)
#define __tlbia()	asm volatile ("tlbia" : : )

extern inline void local_flush_tlb_all(void)
	{ __tlbia(); }
extern inline void local_flush_tlb_mm(struct mm_struct *mm)
	{ __tlbia(); }
extern inline void local_flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
	{ __tlbia(); }
extern inline void local_flush_tlb_range(struct mm_struct *mm,
				unsigned long start, unsigned long end)
	{ __tlbia(); }
extern inline void flush_hash_page(unsigned context, unsigned long va)
	{ }
#else
struct mm_struct;
struct vm_area_struct;
extern void local_flush_tlb_all(void);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void local_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			    unsigned long end);
#endif

#define flush_tlb_all local_flush_tlb_all
#define flush_tlb_mm local_flush_tlb_mm
#define flush_tlb_page local_flush_tlb_page
#define flush_tlb_range local_flush_tlb_range

extern inline void flush_tlb_pgtables(struct mm_struct *mm,
				unsigned long start, unsigned long end)
{
	/* PPC has hw page tables. */
}

/*
 * No cache flushing is required when address mappings are
 * changed, because the caches on PowerPCs are physically
 * addressed.
 * Also, when SMP we use the coherency (M) bit of the
 * BATs and PTEs.  -- Cort
 */
#define flush_cache_all()		do { } while (0)
#define flush_cache_mm(mm)		do { } while (0)
#define flush_cache_range(mm, a, b)	do { } while (0)
#define flush_cache_page(vma, p)	do { } while (0)
#define flush_icache_page(vma, page)	do { } while (0)

extern void flush_icache_range(unsigned long, unsigned long);
extern void __flush_page_to_ram(unsigned long page_va);
extern void flush_page_to_ram(struct page *page);

#define flush_dcache_page(page)			do { } while (0)

extern unsigned long va_to_phys(unsigned long address);
extern pte_t *va_to_pte(unsigned long address);
extern unsigned long ioremap_bot, ioremap_base;
#endif /* __ASSEMBLY__ */

/*
 * The PowerPC MMU uses a hash table containing PTEs, together with
 * a set of 16 segment registers (on 32-bit implementations), to define
 * the virtual to physical address mapping.
 *
 * We use the hash table as an extended TLB, i.e. a cache of currently
 * active mappings.  We maintain a two-level page table tree, much like
 * that used by the i386, for the sake of the Linux memory management code.
 * Low-level assembler code in head.S (procedure hash_page) is responsible
 * for extracting ptes from the tree and putting them into the hash table
 * when necessary, and updating the accessed and modified bits in the
 * page table tree.
 */

/*
 * The PowerPC MPC8xx uses a TLB with hardware assisted, software tablewalk.
 * We also use the two level tables, but we can put the real bits in them
 * needed for the TLB and tablewalk.  These definitions require Mx_CTR.PPM = 0,
 * Mx_CTR.PPCS = 0, and MD_CTR.TWAM = 1.  The level 2 descriptor has
 * additional page protection (when Mx_CTR.PPCS = 1) that allows TLB hit
 * based upon user/super access.  The TLB does not have accessed nor write
 * protect.  We assume that if the TLB get loaded with an entry it is
 * accessed, and overload the changed bit for write protect.  We use
 * two bits in the software pte that are supposed to be set to zero in
 * the TLB entry (24 and 25) for these indicators.  Although the level 1
 * descriptor contains the guarded and writethrough/copyback bits, we can
 * set these at the page level since they get copied from the Mx_TWC
 * register when the TLB entry is loaded.  We will use bit 27 for guard, since
 * that is where it exists in the MD_TWC, and bit 26 for writethrough.
 * These will get masked from the level 2 descriptor at TLB load time, and
 * copied to the MD_TWC before it gets loaded.
 */

/*
 * At present, all PowerPC 400-class processors share a similar TLB
 * architecture. The instruction and data sides share a unified,
 * 64-entry, fully-associative TLB which is maintained totally under
 * software control. In addition, the instruction side has a
 * hardware-managed, 4-entry, fully-associative TLB which serves as a
 * first level to the shared TLB. These two TLBs are known as the UTLB
 * and ITLB, respectively (see "mmu.h" for definitions).
 */

/* PMD_SHIFT determines the size of the area mapped by the second-level page tables */
#define PMD_SHIFT	22
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: our page-table tree is two-level, so
 * we don't really have any PMD directory.
 */
#define PTRS_PER_PTE	1024
#define PTRS_PER_PMD	1
#define PTRS_PER_PGD	1024
#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)
#define FIRST_USER_PGD_NR	0

#define USER_PGD_PTRS (PAGE_OFFSET >> PGDIR_SHIFT)
#define KERNEL_PGD_PTRS (PTRS_PER_PGD-USER_PGD_PTRS)

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 64MB value just means that there will be a 64MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 *
 * We no longer map larger than phys RAM with the BATs so we don't have
 * to worry about the VMALLOC_OFFSET causing problems.  We do have to worry
 * about clashes between our early calls to ioremap() that start growing down
 * from ioremap_base being run into the VM area allocations (growing upwards
 * from VMALLOC_START).  For this reason we have ioremap_bot to check when
 * we actually run into our mappings setup in the early boot with the VM
 * system.  This really does become a problem for machines with good amounts
 * of RAM.  -- Cort
 */
#define VMALLOC_OFFSET (0x1000000) /* 16M */
#define VMALLOC_START ((((long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1)))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	ioremap_bot

/*
 * Bits in a linux-style PTE.  These match the bits in the
 * (hardware-defined) PowerPC PTE as closely as possible.
 */

#if defined(CONFIG_4xx)
/* Definitions for 4xx embedded chips. */
#define	_PAGE_GUARDED	0x001	/* G: page is guarded from prefetch */
#define	_PAGE_COHERENT	0x002	/* M: enforece memory coherence */
#define	_PAGE_NO_CACHE	0x004	/* I: caching is inhibited */
#define	_PAGE_WRITETHRU	0x008	/* W: caching is write-through */
#define	_PAGE_USER	0x010	/* matches one of the zone permission bits */
#define	_PAGE_PRESENT	0x040	/* software: PTE contains a translation */
#define _PAGE_DIRTY	0x100	/* C: page changed */
#define	_PAGE_RW	0x200	/* Writes permitted */
#define _PAGE_ACCESSED	0x400	/* R: page referenced */
#define _PAGE_HWWRITE	0x800	/* software: _PAGE_RW & _PAGE_DIRTY */
#define	_PAGE_SHARED	0

#elif defined(CONFIG_8xx)
/* Definitions for 8xx embedded chips. */
#define _PAGE_PRESENT	0x0001	/* Page is valid */
#define _PAGE_NO_CACHE	0x0002	/* I: cache inhibit */
#define _PAGE_SHARED	0x0004	/* No ASID (context) compare */

/* These five software bits must be masked out when the entry is loaded
 * into the TLB.
 */
#define _PAGE_DIRTY	0x0008	/* software: page changed */
#define _PAGE_GUARDED	0x0010	/* software: guarded access */
#define _PAGE_WRITETHRU 0x0020	/* software: use writethrough cache */
#define _PAGE_RW	0x0040	/* software: user write access allowed */
#define _PAGE_ACCESSED	0x0080	/* software: page referenced */

#define _PAGE_HWWRITE	0x0100	/* C: page changed (write protect) */
#define _PAGE_USER	0x0800	/* One of the PP bits, the other must be 0 */

#else /* CONFIG_6xx */
/* Definitions for 60x, 740/750, etc. */
#define _PAGE_PRESENT	0x001	/* software: pte contains a translation */
#define _PAGE_USER	0x002	/* matches one of the PP bits */
#define _PAGE_RW	0x004	/* software: user write access allowed */
#define _PAGE_GUARDED	0x008
#define _PAGE_COHERENT	0x010	/* M: enforce memory coherence (SMP systems) */
#define _PAGE_NO_CACHE	0x020	/* I: cache inhibit */
#define _PAGE_WRITETHRU	0x040	/* W: cache write-through */
#define _PAGE_DIRTY	0x080	/* C: page changed */
#define _PAGE_ACCESSED	0x100	/* R: page referenced */
#define _PAGE_HWWRITE	0x200	/* software: _PAGE_RW & _PAGE_DIRTY */
#define _PAGE_SHARED	0
#endif

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#ifdef CONFIG_SMP
#define _PAGE_BASE	_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_COHERENT
#else
#define _PAGE_BASE	_PAGE_PRESENT | _PAGE_ACCESSED
#endif
#define _PAGE_WRENABLE	_PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE

#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_USER | \
				 _PAGE_SHARED)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_WRENABLE | _PAGE_SHARED)
#define PAGE_KERNEL_CI	__pgprot(_PAGE_BASE | _PAGE_WRENABLE | _PAGE_SHARED | \
				 _PAGE_NO_CACHE )

/*
 * The PowerPC can only do execute protection on a segment (256MB) basis,
 * not on a page basis.  So we consider execute permission the same as read.
 * Also, write permissions imply read permissions.
 * This is the closest we can get..
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

#ifndef __ASSEMBLY__
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[1024];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

#define BAD_PAGETABLE	__bad_pagetable()
#define BAD_PAGE	__bad_page()
#endif /* __ASSEMBLY__ */

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR	(8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK	(~(sizeof(void*)-1))

/* sizeof(void*) == 1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2	2

#define pte_none(pte)		(!pte_val(pte))
#define pte_present(pte)	(pte_val(pte) & _PAGE_PRESENT)
#define pte_clear(ptep)		do { pte_val(*(ptep)) = 0; } while (0)

#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_bad(pmd)		((pmd_val(pmd) & ~PAGE_MASK) != 0)
#define	pmd_present(pmd)	((pmd_val(pmd) & PAGE_MASK) != 0)
#define	pmd_clear(pmdp)		do { pmd_val(*(pmdp)) = 0; } while (0)

/*
 * Permanent address of a page.
 */
#define page_address(page)  ((page)->virtual)
#define pages_to_mb(x)		((x) >> (20-PAGE_SHIFT))
#define pte_page(x)		(mem_map+(unsigned long)((pte_val(x) >> PAGE_SHIFT)))

#ifndef __ASSEMBLY__
/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)		{ return 0; }
extern inline int pgd_bad(pgd_t pgd)		{ return 0; }
extern inline int pgd_present(pgd_t pgd)	{ return 1; }
#define pgd_clear(xp)				do { } while (0)

#define pgd_page(pgd) \
	((unsigned long) __va(pgd_val(pgd) & PAGE_MASK))

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_read(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_write(pte_t pte)		{ return pte_val(pte) & _PAGE_RW; }
extern inline int pte_exec(pte_t pte)		{ return pte_val(pte) & _PAGE_USER; }
extern inline int pte_dirty(pte_t pte)		{ return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)		{ return pte_val(pte) & _PAGE_ACCESSED; }

extern inline void pte_uncache(pte_t pte)       { pte_val(pte) |= _PAGE_NO_CACHE; }
extern inline void pte_cache(pte_t pte)         { pte_val(pte) &= ~_PAGE_NO_CACHE; }

extern inline pte_t pte_rdprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_exprotect(pte_t pte) {
	pte_val(pte) &= ~_PAGE_USER; return pte; }
extern inline pte_t pte_wrprotect(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_RW | _PAGE_HWWRITE); return pte; }
extern inline pte_t pte_mkclean(pte_t pte) {
	pte_val(pte) &= ~(_PAGE_DIRTY | _PAGE_HWWRITE); return pte; }
extern inline pte_t pte_mkold(pte_t pte) {
	pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }

extern inline pte_t pte_mkread(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkexec(pte_t pte) {
	pte_val(pte) |= _PAGE_USER; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_RW;
	if (pte_val(pte) & _PAGE_DIRTY)
		pte_val(pte) |= _PAGE_HWWRITE;
	return pte;
}
extern inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	if (pte_val(pte) & _PAGE_RW)
		pte_val(pte) |= _PAGE_HWWRITE;
	return pte;
}
extern inline pte_t pte_mkyoung(pte_t pte) {
	pte_val(pte) |= _PAGE_ACCESSED; return pte; }

/* Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval)	((*(pteptr)) = (pteval))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

extern inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
	pte_t pte;
	pte_val(pte) = physpage | pgprot_val(pgprot);
	return pte;
}

#define mk_pte(page,pgprot) \
({									\
	pte_t pte;							\
	pte_val(pte) = ((page - mem_map) << PAGE_SHIFT) | pgprot_val(pgprot); \
	pte;							\
})

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) = (pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot);
	return pte;
}

#define pmd_page(pmd)	(pmd_val(pmd))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
#define pgd_index(address)	 ((address) >> PGDIR_SHIFT)
#define pgd_offset(mm, address)	 ((mm)->pgd + pgd_index(address))

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
	return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */ 
extern inline pte_t * pte_offset(pmd_t * dir, unsigned long address)
{
	return (pte_t *) pmd_page(*dir) + ((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1));
}

extern pgd_t swapper_pg_dir[1024];
extern void paging_init(void);

/*
 * Page tables may have changed.  We don't need to do anything here
 * as entries are faulted into the hash table by the low-level
 * data/instruction access exception handlers.
 */
#define update_mmu_cache(vma, addr, pte)	do { } while (0)

/*
 * When flushing the tlb entry for a page, we also need to flush the
 * hash table entry.  flush_hash_page is assembler (for speed) in head.S.
 */
extern void flush_hash_segments(unsigned low_vsid, unsigned high_vsid);
extern void flush_hash_page(unsigned context, unsigned long va);

/* Encode and de-code a swap entry */
#define SWP_TYPE(entry)			(((entry).val >> 1) & 0x3f)
#define SWP_OFFSET(entry)		((entry).val >> 8)
#define SWP_ENTRY(type, offset)		((swp_entry_t) { ((type) << 1) | ((offset) << 8) })
#define pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define swp_entry_to_pte(x)		((pte_t) { (x).val })

/* CONFIG_APUS */
/* For virtual address to physical address conversion */
extern void cache_clear(__u32 addr, int length);
extern void cache_push(__u32 addr, int length);
extern int mm_end_of_chunk (unsigned long addr, int len);
extern unsigned long iopa(unsigned long addr);
extern unsigned long mm_ptov(unsigned long addr) __attribute__ ((const));

/* Values for nocacheflag and cmode */
/* These are not used by the APUS kernel_map, but prevents
   compilation errors. */
#define	KERNELMAP_FULL_CACHING		0
#define	KERNELMAP_NOCACHE_SER		1
#define	KERNELMAP_NOCACHE_NONSER	2
#define	KERNELMAP_NO_COPYBACK		3

/*
 * Map some physical address range into the kernel address space.
 */
extern unsigned long kernel_map(unsigned long paddr, unsigned long size,
				int nocacheflag, unsigned long *memavailp );

/*
 * Set cache mode of (kernel space) address range. 
 */
extern void kernel_set_cachemode (unsigned long address, unsigned long size,
                                 unsigned int cmode);

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)		(0)
#define kern_addr_valid(addr)	(1)

#define io_remap_page_range remap_page_range 

#include <asm-generic/pgtable.h>

#endif __ASSEMBLY__
#endif /* _PPC_PGTABLE_H */
#endif /* __KERNEL__ */
