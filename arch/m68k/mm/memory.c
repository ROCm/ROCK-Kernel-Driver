/*
 *  linux/arch/m68k/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/pagemap.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/machdep.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif

struct pgtable_cache_struct quicklists;

void __bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_set(pmd, BAD_PAGETABLE);
}

void __bad_pmd(pgd_t *pgd)
{
	printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
	pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page(pte);
			__flush_page_to_ram((unsigned long)pte);
			flush_tlb_kernel_page((unsigned long)pte);
			nocache_page((unsigned long)pte);
			pmd_set(pmd, pte);
			return pte + offset;
		}
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	free_page((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *)__pmd_page(*pmd) + offset;
}

pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset)
{
	pmd_t *pmd;

	pmd = get_pointer_table();
	if (pgd_none(*pgd)) {
		if (pmd) {
			pgd_set(pgd, pmd);
			return pmd + offset;
		}
		pgd_set(pgd, (pmd_t *)BAD_PAGETABLE);
		return NULL;
	}
	free_pointer_table(pmd);
	if (pgd_bad(*pgd)) {
		__bad_pmd(pgd);
		return NULL;
	}
	return (pmd_t *)__pgd_page(*pgd) + offset;
}


/* ++andreas: {get,free}_pointer_table rewritten to use unused fields from
   struct page instead of separately kmalloced struct.  Stolen from
   arch/sparc/mm/srmmu.c ... */

typedef struct list_head ptable_desc;
static LIST_HEAD(ptable_list);

#define PD_PTABLE(page) ((ptable_desc *)virt_to_page(page))
#define PD_PAGE(ptable) (list_entry(ptable, struct page, list))
#define PD_MARKBITS(dp) (*(unsigned char *)&PD_PAGE(dp)->index)

#define PTABLE_SIZE (PTRS_PER_PMD * sizeof(pmd_t))

void __init init_pointer_table(unsigned long ptable)
{
	ptable_desc *dp;
	unsigned long page = ptable & PAGE_MASK;
	unsigned char mask = 1 << ((ptable - page)/PTABLE_SIZE);

	dp = PD_PTABLE(page);
	if (!(PD_MARKBITS(dp) & mask)) {
		PD_MARKBITS(dp) = 0xff;
		list_add(dp, &ptable_list);
	}

	PD_MARKBITS(dp) &= ~mask;
#ifdef DEBUG
	printk("init_pointer_table: %lx, %x\n", ptable, PD_MARKBITS(dp));
#endif

	/* unreserve the page so it's possible to free that page */
	PD_PAGE(dp)->flags &= ~(1 << PG_reserved);
	atomic_set(&PD_PAGE(dp)->count, 1);

	return;
}

pmd_t *get_pointer_table (void)
{
	ptable_desc *dp = ptable_list.next;
	unsigned char mask = PD_MARKBITS (dp);
	unsigned char tmp;
	unsigned int off;

	/*
	 * For a pointer table for a user process address space, a
	 * table is taken from a page allocated for the purpose.  Each
	 * page can hold 8 pointer tables.  The page is remapped in
	 * virtual address space to be noncacheable.
	 */
	if (mask == 0) {
		unsigned long page;
		ptable_desc *new;

		if (!(page = get_free_page (GFP_KERNEL)))
			return 0;

		flush_tlb_kernel_page(page);
		nocache_page (page);

		new = PD_PTABLE(page);
		PD_MARKBITS(new) = 0xfe;
		list_add_tail(new, dp);

		return (pmd_t *)page;
	}

	for (tmp = 1, off = 0; (mask & tmp) == 0; tmp <<= 1, off += PTABLE_SIZE)
		;
	PD_MARKBITS(dp) = mask & ~tmp;
	if (!PD_MARKBITS(dp)) {
		/* move to end of list */
		list_del(dp);
		list_add_tail(dp, &ptable_list);
	}
	return (pmd_t *) (page_address(PD_PAGE(dp)) + off);
}

int free_pointer_table (pmd_t *ptable)
{
	ptable_desc *dp;
	unsigned long page = (unsigned long)ptable & PAGE_MASK;
	unsigned char mask = 1 << (((unsigned long)ptable - page)/PTABLE_SIZE);

	dp = PD_PTABLE(page);
	if (PD_MARKBITS (dp) & mask)
		panic ("table already free!");

	PD_MARKBITS (dp) |= mask;

	if (PD_MARKBITS(dp) == 0xff) {
		/* all tables in page are free, free page */
		list_del(dp);
		cache_page (page);
		free_page (page);
		return 1;
	} else if (ptable_list.next != dp) {
		/*
		 * move this descriptor to the front of the list, since
		 * it has one or more free tables.
		 */
		list_del(dp);
		list_add(dp, &ptable_list);
	}
	return 0;
}

static unsigned long transp_transl_matches( unsigned long regval,
					    unsigned long vaddr )
{
    unsigned long base, mask;

    /* enabled? */
    if (!(regval & 0x8000))
	return( 0 );

    if (CPU_IS_030) {
	/* function code match? */
	base = (regval >> 4) & 7;
	mask = ~(regval & 7);
	if (((SUPER_DATA ^ base) & mask) != 0)
	    return 0;
    }
    else {
	/* must not be user-only */
	if ((regval & 0x6000) == 0)
	    return( 0 );
    }

    /* address match? */
    base = regval & 0xff000000;
    mask = ~(regval << 8) & 0xff000000;
    return (((unsigned long)vaddr ^ base) & mask) == 0;
}

#if DEBUG_INVALID_PTOV
int mm_inv_cnt = 5;
#endif

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
/*
 * The following two routines map from a physical address to a kernel
 * virtual address and vice versa.
 */
unsigned long mm_vtop(unsigned long vaddr)
{
	int i=0;
	unsigned long voff = (unsigned long)vaddr - PAGE_OFFSET;

	do {
		if (voff < m68k_memory[i].size) {
#ifdef DEBUGPV
			printk ("VTOP(%p)=%lx\n", vaddr,
				m68k_memory[i].addr + voff);
#endif
			return m68k_memory[i].addr + voff;
		}
		voff -= m68k_memory[i].size;
	} while (++i < m68k_num_memory);

	return mm_vtop_fallback(vaddr);
}
#endif

/* Separate function to make the common case faster (needs to save less
   registers) */
unsigned long mm_vtop_fallback(unsigned long vaddr)
{
	/* not in one of the memory chunks; test for applying transparent
	 * translation */

	if (CPU_IS_030) {
	    unsigned long ttreg;
	    
	    asm volatile( ".chip 68030\n\t"
			  "pmove %/tt0,%0@\n\t"
			  ".chip 68k"
			  : : "a" (&ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return (unsigned long)vaddr;
	    asm volatile( ".chip 68030\n\t"
			  "pmove %/tt1,%0@\n\t"
			  ".chip 68k"
			  : : "a" (&ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return (unsigned long)vaddr;
	}
	else if (CPU_IS_040_OR_060) {
	    unsigned long ttreg;
	    
	    asm volatile( ".chip 68040\n\t"
			  "movec %%dtt0,%0\n\t"
			  ".chip 68k"
			  : "=d" (ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return (unsigned long)vaddr;
	    asm volatile( ".chip 68040\n\t"
			  "movec %%dtt1,%0\n\t"
			  ".chip 68k"
			  : "=d" (ttreg) );
	    if (transp_transl_matches( ttreg, vaddr ))
		return (unsigned long)vaddr;
	}

	/* no match, too, so get the actual physical address from the MMU. */

	if (CPU_IS_060) {
	  mm_segment_t fs = get_fs();
	  unsigned long  paddr;

	  set_fs (MAKE_MM_SEG(SUPER_DATA));

	  /* The PLPAR instruction causes an access error if the translation
	   * is not possible. To catch this we use the same exception mechanism
	   * as for user space accesses in <asm/uaccess.h>. */
	  asm volatile (".chip 68060\n"
			"1: plpar (%0)\n"
			".chip 68k\n"
			"2:\n"
			".section .fixup,\"ax\"\n"
			"   .even\n"
			"3: lea -1,%0\n"
			"   jra 2b\n"
			".previous\n"
			".section __ex_table,\"a\"\n"
			"   .align 4\n"
			"   .long 1b,3b\n"
			".previous"
			: "=a" (paddr)
			: "0" (vaddr));
	  set_fs (fs);

	  return paddr;

	} else if (CPU_IS_040) {
	  unsigned long mmusr;
	  mm_segment_t fs = get_fs();

	  set_fs (MAKE_MM_SEG(SUPER_DATA));

	  asm volatile (".chip 68040\n\t"
			"ptestr (%1)\n\t"
			"movec %%mmusr, %0\n\t"
			".chip 68k"
			: "=r" (mmusr)
			: "a" (vaddr));
	  set_fs (fs);

	  if (mmusr & MMU_T_040) {
	    return (unsigned long)vaddr;	/* Transparent translation */
	  }
	  if (mmusr & MMU_R_040)
	    return (mmusr & PAGE_MASK) | ((unsigned long)vaddr & (PAGE_SIZE-1));

	  printk("VTOP040: bad virtual address %lx (%lx)", vaddr, mmusr);
	  return -1;
	} else {
	  volatile unsigned short temp;
	  unsigned short mmusr;
	  unsigned long *descaddr;

	  asm volatile ("ptestr #5,%2@,#7,%0\n\t"
			"pmove %/psr,%1@"
			: "=a&" (descaddr)
			: "a" (&temp), "a" (vaddr));
	  mmusr = temp;

	  if (mmusr & (MMU_I|MMU_B|MMU_L))
	    printk("VTOP030: bad virtual address %lx (%x)\n", vaddr, mmusr);

	  descaddr = phys_to_virt((unsigned long)descaddr);

	  switch (mmusr & MMU_NUM) {
	  case 1:
	    return (*descaddr & 0xfe000000) | ((unsigned long)vaddr & 0x01ffffff);
	  case 2:
	    return (*descaddr & 0xfffc0000) | ((unsigned long)vaddr & 0x0003ffff);
	  case 3:
	    return (*descaddr & PAGE_MASK) | ((unsigned long)vaddr & (PAGE_SIZE-1));
	  default:
	    printk("VTOP: bad levels (%u) for virtual address %lx\n", 
		   mmusr & MMU_NUM, vaddr);
	  }
	}

	printk("VTOP: bad virtual address %lx\n", vaddr);
	return -1;
}

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
unsigned long mm_ptov (unsigned long paddr)
{
	int i = 0;
	unsigned long poff, voff = PAGE_OFFSET;

	do {
		poff = paddr - m68k_memory[i].addr;
		if (poff < m68k_memory[i].size) {
#ifdef DEBUGPV
			printk ("PTOV(%lx)=%lx\n", paddr, poff + voff);
#endif
			return poff + voff;
		}
		voff += m68k_memory[i].size;
	} while (++i < m68k_num_memory);

#if DEBUG_INVALID_PTOV
	if (mm_inv_cnt > 0) {
		mm_inv_cnt--;
		printk("Invalid use of phys_to_virt(0x%lx) at 0x%p!\n",
			paddr, __builtin_return_address(0));
	}
#endif
	/*
	 * assume that the kernel virtual address is the same as the
	 * physical address.
	 *
	 * This should be reasonable in most situations:
	 *  1) They shouldn't be dereferencing the virtual address
	 *     unless they are sure that it is valid from kernel space.
	 *  2) The only usage I see so far is converting a page table
	 *     reference to some non-FASTMEM address space when freeing
         *     mmaped "/dev/mem" pages.  These addresses are just passed
	 *     to "free_page", which ignores addresses that aren't in
	 *     the memory list anyway.
	 *
	 */

#ifdef CONFIG_AMIGA
	/*
	 * if on an amiga and address is in first 16M, move it 
	 * to the ZTWO_VADDR range
	 */
	if (MACH_IS_AMIGA && paddr < 16*1024*1024)
		return ZTWO_VADDR(paddr);
#endif
	return -1;
}
#endif

/* invalidate page in both caches */
#define	clear040(paddr)					\
	__asm__ __volatile__ ("nop\n\t"			\
			      ".chip 68040\n\t"		\
			      "cinvp %%bc,(%0)\n\t"	\
			      ".chip 68k"		\
			      : : "a" (paddr))

/* invalidate page in i-cache */
#define	cleari040(paddr)				\
	__asm__ __volatile__ ("nop\n\t"			\
			      ".chip 68040\n\t"		\
			      "cinvp %%ic,(%0)\n\t"	\
			      ".chip 68k"		\
			      : : "a" (paddr))

/* push page in both caches */
#define	push040(paddr)					\
	__asm__ __volatile__ ("nop\n\t"			\
			      ".chip 68040\n\t"		\
			      "cpushp %%bc,(%0)\n\t"	\
			      ".chip 68k"		\
			      : : "a" (paddr))

/* push and invalidate page in both caches */
#define	pushcl040(paddr)			\
	do { push040(paddr);			\
	     if (CPU_IS_060) clear040(paddr);	\
	} while(0)

/* push page in both caches, invalidate in i-cache */
#define	pushcli040(paddr)			\
	do { push040(paddr);			\
	     if (CPU_IS_060) cleari040(paddr);	\
	} while(0)


/*
 * 040: Hit every page containing an address in the range paddr..paddr+len-1.
 * (Low order bits of the ea of a CINVP/CPUSHP are "don't care"s).
 * Hit every page until there is a page or less to go. Hit the next page,
 * and the one after that if the range hits it.
 */
/* ++roman: A little bit more care is required here: The CINVP instruction
 * invalidates cache entries WITHOUT WRITING DIRTY DATA BACK! So the beginning
 * and the end of the region must be treated differently if they are not
 * exactly at the beginning or end of a page boundary. Else, maybe too much
 * data becomes invalidated and thus lost forever. CPUSHP does what we need:
 * it invalidates the page after pushing dirty data to memory. (Thanks to Jes
 * for discovering the problem!)
 */
/* ... but on the '060, CPUSH doesn't invalidate (for us, since we have set
 * the DPI bit in the CACR; would it cause problems with temporarily changing
 * this?). So we have to push first and then additionally to invalidate.
 */


/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */

void cache_clear (unsigned long paddr, int len)
{
    if (CPU_IS_040_OR_060) {
	int tmp;

	/*
	 * We need special treatment for the first page, in case it
	 * is not page-aligned. Page align the addresses to work
	 * around bug I17 in the 68060.
	 */
	if ((tmp = -paddr & (PAGE_SIZE - 1))) {
	    pushcl040(paddr & PAGE_MASK);
	    if ((len -= tmp) <= 0)
		return;
	    paddr += tmp;
	}
	tmp = PAGE_SIZE;
	paddr &= PAGE_MASK;
	while ((len -= tmp) >= 0) {
	    clear040(paddr);
	    paddr += tmp;
	}
	if ((len += tmp))
	    /* a page boundary gets crossed at the end */
	    pushcl040(paddr);
    }
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
    	mach_l2_flush(0);
#endif
}


/*
 * cache_push() semantics: Write back any dirty cache data in the given area,
 * and invalidate the range in the instruction cache. It needs not (but may)
 * invalidate those entries also in the data cache. The range is defined by a
 * _physical_ address.
 */

void cache_push (unsigned long paddr, int len)
{
    if (CPU_IS_040_OR_060) {
	int tmp = PAGE_SIZE;

	/*
         * on 68040 or 68060, push cache lines for pages in the range;
	 * on the '040 this also invalidates the pushed lines, but not on
	 * the '060!
	 */
	len += paddr & (PAGE_SIZE - 1);

	/*
	 * Work around bug I17 in the 68060 affecting some instruction
	 * lines not being invalidated properly.
	 */
	paddr &= PAGE_MASK;

	do {
	    pushcli040(paddr);
	    paddr += tmp;
	} while ((len -= tmp) > 0);
    }
    /*
     * 68030/68020 have no writeback cache. On the other hand,
     * cache_push is actually a superset of cache_clear (the lines
     * get written back and invalidated), so we should make sure
     * to perform the corresponding actions. After all, this is getting
     * called in places where we've just loaded code, or whatever, so
     * flushing the icache is appropriate; flushing the dcache shouldn't
     * be required.
     */
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
    	mach_l2_flush(1);
#endif
}


#undef clear040
#undef cleari040
#undef push040
#undef pushcl040
#undef pushcli040

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
int mm_end_of_chunk (unsigned long addr, int len)
{
	int i;

	for (i = 0; i < m68k_num_memory; i++)
		if (m68k_memory[i].addr + m68k_memory[i].size == addr + len)
			return 1;
	return 0;
}
#endif
