/*
 *  linux/arch/x86_64/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2002,2003 Andi Kleen <ak@suse.de>
 *
 *  Jun Nakajima <jun.nakajima@intel.com>
 *	Modified for Xen.
 */

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
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/pfn.h>
#include <linux/poison.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/memory_hotplug.h>
#include <linux/nmi.h>

#include <asm/processor.h>
#include <asm/bios_ebda.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/sections.h>
#include <asm/kdebug.h>
#include <asm/numa.h>
#include <asm/cacheflush.h>
#include <asm/init.h>
#include <asm/setup.h>

#include <xen/features.h>

#if CONFIG_XEN_COMPAT <= 0x030002
unsigned int __kernel_page_user;
EXPORT_SYMBOL(__kernel_page_user);
#endif

extern pmd_t level2_fixmap_pgt[PTRS_PER_PMD];
extern pte_t level1_fixmap_pgt[PTRS_PER_PTE];

/*
 * Use this until direct mapping is established, i.e. before __va() is 
 * available in init_memory_mapping().
 */

#define addr_to_page(addr, page)				\
	(addr) &= PHYSICAL_PAGE_MASK;				\
	(page) = ((unsigned long *) ((unsigned long)		\
	(((mfn_to_pfn((addr) >> PAGE_SHIFT)) << PAGE_SHIFT) +	\
	__START_KERNEL_map)))

pmd_t *__init early_get_pmd(unsigned long va)
{
	unsigned long addr;
	unsigned long *page = (unsigned long *)init_level4_pgt;

	addr = page[pgd_index(va)];
	addr_to_page(addr, page);

	addr = page[pud_index(va)];
	addr_to_page(addr, page);

	return (pmd_t *)&page[pmd_index(va)];
}

void __meminit early_make_page_readonly(void *va, unsigned int feature)
{
	unsigned long addr, _va = (unsigned long)va;
	pte_t pte, *ptep;
	unsigned long *page = (unsigned long *) init_level4_pgt;

	BUG_ON(after_bootmem);

	if (xen_feature(feature))
		return;

	addr = (unsigned long) page[pgd_index(_va)];
	addr_to_page(addr, page);

	addr = page[pud_index(_va)];
	addr_to_page(addr, page);

	addr = page[pmd_index(_va)];
	addr_to_page(addr, page);

	ptep = (pte_t *) &page[pte_index(_va)];

	pte.pte = ptep->pte & ~_PAGE_RW;
	if (HYPERVISOR_update_va_mapping(_va, pte, 0))
		BUG();
}

unsigned long __init early_arbitrary_virt_to_mfn(void *v)
{
	unsigned long va = (unsigned long)v, addr, *page;

	BUG_ON(va < __START_KERNEL_map);

	page = (void *)(xen_read_cr3() + __START_KERNEL_map);

	addr = page[pgd_index(va)];
	addr_to_page(addr, page);

	addr = page[pud_index(va)];
	addr_to_page(addr, page);

	addr = page[pmd_index(va)];
	addr_to_page(addr, page);

	return (page[pte_index(va)] & PHYSICAL_PAGE_MASK) >> PAGE_SHIFT;
}

#ifndef CONFIG_XEN
static int __init parse_direct_gbpages_off(char *arg)
{
	direct_gbpages = 0;
	return 0;
}
early_param("nogbpages", parse_direct_gbpages_off);

static int __init parse_direct_gbpages_on(char *arg)
{
	direct_gbpages = 1;
	return 0;
}
early_param("gbpages", parse_direct_gbpages_on);
#endif

/*
 * NOTE: pagetable_init alloc all the fixmap pagetables contiguous on the
 * physical space so we can cache the place of the first one and move
 * around without checking the pgd every time.
 */

pteval_t __supported_pte_mask __read_mostly = ~0UL;
EXPORT_SYMBOL_GPL(__supported_pte_mask);

int force_personality32;

/*
 * noexec32=on|off
 * Control non executable heap for 32bit processes.
 * To control the stack too use noexec=off
 *
 * on	PROT_READ does not imply PROT_EXEC for 32-bit processes (default)
 * off	PROT_READ implies PROT_EXEC
 */
static int __init nonx32_setup(char *str)
{
	if (!strcmp(str, "on"))
		force_personality32 &= ~READ_IMPLIES_EXEC;
	else if (!strcmp(str, "off"))
		force_personality32 |= READ_IMPLIES_EXEC;
	return 1;
}
__setup("noexec32=", nonx32_setup);

static __init unsigned long get_table_end(void)
{
	BUG_ON(!e820_table_end);
	if (xen_start_info->mfn_list < __START_KERNEL_map
	    && e820_table_end == xen_start_info->first_p2m_pfn) {
		e820_table_end += xen_start_info->nr_p2m_frames;
		e820_table_top += xen_start_info->nr_p2m_frames;
	}
	return e820_table_end++;
}

/*
 * NOTE: This function is marked __ref because it calls __init function
 * (alloc_bootmem_pages). It's safe to do it ONLY when after_bootmem == 0.
 */
static __ref void *spp_getpage(void)
{
	void *ptr;

	if (after_bootmem)
		ptr = (void *) get_zeroed_page(GFP_ATOMIC | __GFP_NOTRACK);
	else if (e820_table_end < e820_table_top) {
		ptr = __va(get_table_end() << PAGE_SHIFT);
		memset(ptr, 0, PAGE_SIZE);
	} else
		ptr = alloc_bootmem_pages(PAGE_SIZE);

	if (!ptr || ((unsigned long)ptr & ~PAGE_MASK)) {
		panic("set_pte_phys: cannot allocate page data %s\n",
			after_bootmem ? "after bootmem" : "");
	}

	pr_debug("spp_getpage %p\n", ptr);

	return ptr;
}

static pud_t *fill_pud(pgd_t *pgd, unsigned long vaddr)
{
	if (pgd_none(*pgd)) {
		pud_t *pud = (pud_t *)spp_getpage();
		make_page_readonly(pud, XENFEAT_writable_page_tables);
		if (!after_bootmem)
			xen_l4_entry_update(pgd, __pgd(__pa(pud) | _PAGE_TABLE));
		else
			pgd_populate(&init_mm, pgd, pud);
		if (pud != pud_offset(pgd, 0))
			printk(KERN_ERR "PAGETABLE BUG #00! %p <-> %p\n",
			       pud, pud_offset(pgd, 0));
	}
	return pud_offset(pgd, vaddr);
}

static pmd_t *fill_pmd(pud_t *pud, unsigned long vaddr)
{
	if (pud_none(*pud)) {
		pmd_t *pmd = (pmd_t *) spp_getpage();
		make_page_readonly(pmd, XENFEAT_writable_page_tables);
		if (!after_bootmem)
			xen_l3_entry_update(pud, __pud(__pa(pmd) | _PAGE_TABLE));
		else
			pud_populate(&init_mm, pud, pmd);
		if (pmd != pmd_offset(pud, 0))
			printk(KERN_ERR "PAGETABLE BUG #01! %p <-> %p\n",
			       pmd, pmd_offset(pud, 0));
	}
	return pmd_offset(pud, vaddr);
}

static pte_t *fill_pte(pmd_t *pmd, unsigned long vaddr)
{
	if (pmd_none(*pmd)) {
		pte_t *pte = (pte_t *) spp_getpage();
		make_page_readonly(pte, XENFEAT_writable_page_tables);
		pmd_populate_kernel(&init_mm, pmd, pte);
		if (pte != pte_offset_kernel(pmd, 0))
			printk(KERN_ERR "PAGETABLE BUG #02!\n");
	}
	return pte_offset_kernel(pmd, vaddr);
}

void set_pte_vaddr_pud(pud_t *pud_page, unsigned long vaddr, pte_t new_pte)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pud = pud_page + pud_index(vaddr);
	pmd = fill_pmd(pud, vaddr);
	pte = fill_pte(pmd, vaddr);

	set_pte(pte, new_pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

void set_pte_vaddr(unsigned long vaddr, pte_t pteval)
{
	pgd_t *pgd;
	pud_t *pud_page;

	pr_debug("set_pte_vaddr %lx to %lx\n", vaddr, __pte_val(pteval));

	pgd = pgd_offset_k(vaddr);
	if (pgd_none(*pgd)) {
		printk(KERN_ERR
			"PGD FIXMAP MISSING, it should be setup in head.S!\n");
		return;
	}
	pud_page = (pud_t*)pgd_page_vaddr(*pgd);
	set_pte_vaddr_pud(pud_page, vaddr, pteval);
}

pmd_t * __init populate_extra_pmd(unsigned long vaddr)
{
	pgd_t *pgd;
	pud_t *pud;

	pgd = pgd_offset_k(vaddr);
	pud = fill_pud(pgd, vaddr);
	return fill_pmd(pud, vaddr);
}

pte_t * __init populate_extra_pte(unsigned long vaddr)
{
	pmd_t *pmd;

	pmd = populate_extra_pmd(vaddr);
	return fill_pte(pmd, vaddr);
}

#ifndef CONFIG_XEN
/*
 * Create large page table mappings for a range of physical addresses.
 */
static void __init __init_extra_mapping(unsigned long phys, unsigned long size,
						pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	BUG_ON((phys & ~PMD_MASK) || (size & ~PMD_MASK));
	for (; size; phys += PMD_SIZE, size -= PMD_SIZE) {
		pgd = pgd_offset_k((unsigned long)__va(phys));
		if (pgd_none(*pgd)) {
			pud = (pud_t *) spp_getpage();
			set_pgd(pgd, __pgd(__pa(pud) | _KERNPG_TABLE |
						_PAGE_USER));
		}
		pud = pud_offset(pgd, (unsigned long)__va(phys));
		if (pud_none(*pud)) {
			pmd = (pmd_t *) spp_getpage();
			set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE |
						_PAGE_USER));
		}
		pmd = pmd_offset(pud, phys);
		BUG_ON(!pmd_none(*pmd));
		set_pmd(pmd, __pmd(phys | pgprot_val(prot)));
	}
}

void __init init_extra_mapping_wb(unsigned long phys, unsigned long size)
{
	__init_extra_mapping(phys, size, PAGE_KERNEL_LARGE);
}

void __init init_extra_mapping_uc(unsigned long phys, unsigned long size)
{
	__init_extra_mapping(phys, size, PAGE_KERNEL_LARGE_NOCACHE);
}

/*
 * The head.S code sets up the kernel high mapping:
 *
 *   from __START_KERNEL_map to __START_KERNEL_map + size (== _end-_text)
 *
 * phys_addr holds the negative offset to the kernel, which is added
 * to the compile time generated pmds. This results in invalid pmds up
 * to the point where we hit the physaddr 0 mapping.
 *
 * We limit the mappings to the region from _text to _end.  _end is
 * rounded up to the 2MB boundary. This catches the invalid pmds as
 * well, as they are located before _text:
 */
void __init cleanup_highmap(void)
{
	unsigned long vaddr = __START_KERNEL_map;
	unsigned long end = roundup((unsigned long)_end, PMD_SIZE) - 1;
	pmd_t *pmd = level2_kernel_pgt;
	pmd_t *last_pmd = pmd + PTRS_PER_PMD;

	for (; pmd < last_pmd; pmd++, vaddr += PMD_SIZE) {
		if (pmd_none(*pmd))
			continue;
		if (vaddr < (unsigned long) _text || vaddr > end)
			set_pmd(pmd, __pmd(0));
	}
}
#endif

static __ref void *alloc_low_page(unsigned long *phys)
{
	unsigned long pfn;
	void *adr;

	if (after_bootmem) {
		adr = (void *)get_zeroed_page(GFP_ATOMIC | __GFP_NOTRACK);
		*phys = __pa(adr);

		return adr;
	}

	pfn = get_table_end();
	if (pfn >= e820_table_top)
		panic("alloc_low_page: ran out of memory");

	adr = early_memremap(pfn * PAGE_SIZE, PAGE_SIZE);
	memset(adr, 0, PAGE_SIZE);
	*phys  = pfn * PAGE_SIZE;
	return adr;
}

static __ref void unmap_low_page(void *adr)
{
	if (after_bootmem)
		return;

	early_iounmap(adr, PAGE_SIZE);
}

static inline int __meminit make_readonly(unsigned long paddr)
{
	extern char __vsyscall_0;
	int readonly = 0;

	/* Make new page tables read-only on the first pass. */
	if (!xen_feature(XENFEAT_writable_page_tables)
	    && !max_pfn_mapped
	    && (paddr >= (e820_table_start << PAGE_SHIFT))) {
		unsigned long top = e820_table_top;

		/* Account for the range get_table_end() skips. */
		if (xen_start_info->mfn_list < __START_KERNEL_map
		    && e820_table_end <= xen_start_info->first_p2m_pfn
		    && top > xen_start_info->first_p2m_pfn)
			top += xen_start_info->nr_p2m_frames;
		if (paddr < (top << PAGE_SHIFT))
			readonly = 1;
	}
	/* Make old page tables read-only. */
	if (!xen_feature(XENFEAT_writable_page_tables)
	    && (paddr >= (xen_start_info->pt_base - __START_KERNEL_map))
	    && (paddr < (e820_table_end << PAGE_SHIFT)))
		readonly = 1;
	/* Make P->M table (and its page tables) read-only. */
	if (!xen_feature(XENFEAT_writable_page_tables)
	    && xen_start_info->mfn_list < __START_KERNEL_map
	    && paddr >= (xen_start_info->first_p2m_pfn << PAGE_SHIFT)
	    && paddr < (xen_start_info->first_p2m_pfn
			+ xen_start_info->nr_p2m_frames) << PAGE_SHIFT)
		readonly = 1;

	/*
	 * No need for writable mapping of kernel image. This also ensures that
	 * page and descriptor tables embedded inside don't have writable
	 * mappings. Exclude the vsyscall area here, allowing alternative
	 * instruction patching to work.
	 */
	if ((paddr >= __pa_symbol(&_text)) && (paddr < __pa(_brk_end))
	    && !(paddr >= __pa_symbol(&__vsyscall_0)
	         && paddr < __pa_symbol(&__vsyscall_0) + PAGE_SIZE))
		readonly = 1;

	return readonly;
}

static unsigned long __meminit
phys_pte_init(pte_t *pte_page, unsigned long addr, unsigned long end,
	      pgprot_t prot)
{
	unsigned pages = 0;
	unsigned long last_map_addr = end;
	int i;

	pte_t *pte = pte_page + pte_index(addr);

	for(i = pte_index(addr); i < PTRS_PER_PTE; i++, addr += PAGE_SIZE, pte++) {
		unsigned long pteval = addr | pgprot_val(prot);

		if (addr >= end ||
		    (!after_bootmem &&
		     (addr >> PAGE_SHIFT) >= xen_start_info->nr_pages))
			break;

		/*
		 * We will re-use the existing mapping.
		 * Xen for example has some special requirements, like mapping
		 * pagetable pages as RO. So assume someone who pre-setup
		 * these mappings are more intelligent.
		 */
		if (__pte_val(*pte)) {
			pages++;
			continue;
		}

		if (make_readonly(addr))
			pteval &= ~_PAGE_RW;
		if (0)
			printk("   pte=%p addr=%lx pte=%016lx\n",
			       pte, addr, pteval);
		pages++;
		if (!after_bootmem)
			*pte = __pte(pteval & __supported_pte_mask);
		else
			set_pte(pte, __pte(pteval & __supported_pte_mask));
		last_map_addr = (addr & PAGE_MASK) + PAGE_SIZE;
	}

	update_page_count(PG_LEVEL_4K, pages);

	return last_map_addr;
}

static unsigned long __meminit
phys_pte_update(pmd_t *pmd, unsigned long address, unsigned long end,
		pgprot_t prot)
{
	pte_t *pte = (pte_t *)pmd_page_vaddr(*pmd);

	BUG_ON(!max_pfn_mapped);
	return phys_pte_init(pte, address, end, prot);
}

static unsigned long __meminit
phys_pmd_init(pmd_t *pmd_page, unsigned long address, unsigned long end,
	      unsigned long page_size_mask, pgprot_t prot)
{
	unsigned long pages = 0;
	unsigned long last_map_addr = end;

	int i = pmd_index(address);

	for (; i < PTRS_PER_PMD; i++, address += PMD_SIZE) {
		unsigned long pte_phys;
		pmd_t *pmd = pmd_page + pmd_index(address);
		pte_t *pte;
		pgprot_t new_prot = prot;

		if (address >= end)
			break;

		if (__pmd_val(*pmd)) {
			if (!pmd_large(*pmd)) {
				spin_lock(&init_mm.page_table_lock);
				last_map_addr = phys_pte_update(pmd, address,
								end, prot);
				spin_unlock(&init_mm.page_table_lock);
				continue;
			}
			/*
			 * If we are ok with PG_LEVEL_2M mapping, then we will
			 * use the existing mapping,
			 *
			 * Otherwise, we will split the large page mapping but
			 * use the same existing protection bits except for
			 * large page, so that we don't violate Intel's TLB
			 * Application note (317080) which says, while changing
			 * the page sizes, new and old translations should
			 * not differ with respect to page frame and
			 * attributes.
			 */
			if (page_size_mask & (1 << PG_LEVEL_2M)) {
				pages++;
				continue;
			}
			new_prot = pte_pgprot(pte_clrhuge(*(pte_t *)pmd));
		}

		if (page_size_mask & (1<<PG_LEVEL_2M)) {
			pages++;
			spin_lock(&init_mm.page_table_lock);
			set_pte((pte_t *)pmd,
				pfn_pte(address >> PAGE_SHIFT,
					__pgprot(pgprot_val(prot) | _PAGE_PSE)));
			spin_unlock(&init_mm.page_table_lock);
			last_map_addr = (address & PMD_MASK) + PMD_SIZE;
			continue;
		}

		pte = alloc_low_page(&pte_phys);
		last_map_addr = phys_pte_init(pte, address, end, new_prot);
		unmap_low_page(pte);

		if (!after_bootmem) {
			if (max_pfn_mapped)
				make_page_readonly(__va(pte_phys),
						   XENFEAT_writable_page_tables);
			*pmd = __pmd(pte_phys | _PAGE_TABLE);
		} else {
			spin_lock(&init_mm.page_table_lock);
			pmd_populate_kernel(&init_mm, pmd, __va(pte_phys));
			spin_unlock(&init_mm.page_table_lock);
		}
	}
	update_page_count(PG_LEVEL_2M, pages);
	return last_map_addr;
}

static unsigned long __meminit
phys_pmd_update(pud_t *pud, unsigned long address, unsigned long end,
		unsigned long page_size_mask, pgprot_t prot)
{
	pmd_t *pmd = pmd_offset(pud, 0);
	unsigned long last_map_addr;

	BUG_ON(!max_pfn_mapped);
	last_map_addr = phys_pmd_init(pmd, address, end, page_size_mask, prot);
	__flush_tlb_all();
	return last_map_addr;
}

static unsigned long __meminit
phys_pud_init(pud_t *pud_page, unsigned long addr, unsigned long end,
			 unsigned long page_size_mask)
{
	unsigned long pages = 0;
	unsigned long last_map_addr = end;
	int i = pud_index(addr);

	for (; i < PTRS_PER_PUD; i++, addr = (addr & PUD_MASK) + PUD_SIZE) {
		unsigned long pmd_phys;
		pud_t *pud = pud_page + pud_index(addr);
		pmd_t *pmd;
		pgprot_t prot = PAGE_KERNEL;

		if (addr >= end)
			break;

		if (__pud_val(*pud)) {
			if (!pud_large(*pud)) {
				last_map_addr = phys_pmd_update(pud, addr, end,
							 page_size_mask, prot);
				continue;
			}
			/*
			 * If we are ok with PG_LEVEL_1G mapping, then we will
			 * use the existing mapping.
			 *
			 * Otherwise, we will split the gbpage mapping but use
			 * the same existing protection  bits except for large
			 * page, so that we don't violate Intel's TLB
			 * Application note (317080) which says, while changing
			 * the page sizes, new and old translations should
			 * not differ with respect to page frame and
			 * attributes.
			 */
			if (page_size_mask & (1 << PG_LEVEL_1G)) {
				pages++;
				continue;
			}
			prot = pte_pgprot(pte_clrhuge(*(pte_t *)pud));
		}

		if (page_size_mask & (1<<PG_LEVEL_1G)) {
			pages++;
			spin_lock(&init_mm.page_table_lock);
			set_pte((pte_t *)pud,
				pfn_pte(addr >> PAGE_SHIFT, PAGE_KERNEL_LARGE));
			spin_unlock(&init_mm.page_table_lock);
			last_map_addr = (addr & PUD_MASK) + PUD_SIZE;
			continue;
		}

		pmd = alloc_low_page(&pmd_phys);
		last_map_addr = phys_pmd_init(pmd, addr, end, page_size_mask,
					      prot);
		unmap_low_page(pmd);

		if (!after_bootmem) {
			if (max_pfn_mapped)
				make_page_readonly(__va(pmd_phys),
						   XENFEAT_writable_page_tables);
			if (page_size_mask & (1 << PG_LEVEL_NUM))
				xen_l3_entry_update(pud, __pud(pmd_phys | _PAGE_TABLE));
			else
				*pud = __pud(pmd_phys | _PAGE_TABLE);
		} else {
			spin_lock(&init_mm.page_table_lock);
			pud_populate(&init_mm, pud, __va(pmd_phys));
			spin_unlock(&init_mm.page_table_lock);
		}
	}
	__flush_tlb_all();

	update_page_count(PG_LEVEL_1G, pages);

	return last_map_addr;
}

static unsigned long __meminit
phys_pud_update(pgd_t *pgd, unsigned long addr, unsigned long end,
		 unsigned long page_size_mask)
{
	pud_t *pud;

	pud = (pud_t *)pgd_page_vaddr(*pgd);

	return phys_pud_init(pud, addr, end, page_size_mask | (1 << PG_LEVEL_NUM));
}

void __init xen_init_pt(void)
{
	unsigned long addr, *page;

	/* Find the initial pte page that was built for us. */
	page = (unsigned long *)xen_start_info->pt_base;
	addr = page[pgd_index(__START_KERNEL_map)];
	addr_to_page(addr, page);

#if CONFIG_XEN_COMPAT <= 0x030002
	/* On Xen 3.0.2 and older we may need to explicitly specify _PAGE_USER
	   in kernel PTEs. We check that here. */
	if (HYPERVISOR_xen_version(XENVER_version, NULL) <= 0x30000) {
		unsigned long *pg;
		pte_t pte;

		/* Mess with the initial mapping of page 0. It's not needed. */
		BUILD_BUG_ON(__START_KERNEL <= __START_KERNEL_map);
		addr = page[pud_index(__START_KERNEL_map)];
		addr_to_page(addr, pg);
		addr = pg[pmd_index(__START_KERNEL_map)];
		addr_to_page(addr, pg);
		pte.pte = pg[pte_index(__START_KERNEL_map)];
		BUG_ON(!(pte.pte & _PAGE_PRESENT));

		/* If _PAGE_USER isn't set, we obviously do not need it. */
		if (pte.pte & _PAGE_USER) {
			/* _PAGE_USER is needed, but is it set implicitly? */
			pte.pte &= ~_PAGE_USER;
			if ((HYPERVISOR_update_va_mapping(__START_KERNEL_map,
							  pte, 0) != 0) ||
			    !(pg[pte_index(__START_KERNEL_map)] & _PAGE_USER))
				/* We need to explicitly specify _PAGE_USER. */
				__kernel_page_user = _PAGE_USER;
		}
	}
#endif

	/* Construct mapping of initial pte page in our own directories. */
	init_level4_pgt[pgd_index(__START_KERNEL_map)] = 
		__pgd(__pa_symbol(level3_kernel_pgt) | _PAGE_TABLE);
	memcpy(level3_kernel_pgt + pud_index(__START_KERNEL_map),
	       page + pud_index(__START_KERNEL_map),
	       (PTRS_PER_PUD - pud_index(__START_KERNEL_map))
	       * sizeof(*level3_kernel_pgt));

	/* Copy the initial P->M table mappings if necessary. */
	addr = pgd_index(xen_start_info->mfn_list);
	if (addr < pgd_index(__START_KERNEL_map))
		init_level4_pgt[addr] =
			((pgd_t *)xen_start_info->pt_base)[addr];

	/* Do an early initialization of the fixmap area. */
	addr = __fix_to_virt(FIX_EARLYCON_MEM_BASE);
	if (pud_present(level3_kernel_pgt[pud_index(addr)])) {
		unsigned long adr = page[pud_index(addr)];

		addr_to_page(adr, page);
		memcpy(level2_fixmap_pgt, page, PAGE_SIZE);
	}
	level3_kernel_pgt[pud_index(addr)] =
		__pud(__pa_symbol(level2_fixmap_pgt) | _PAGE_TABLE);
	level2_fixmap_pgt[pmd_index(addr)] =
		__pmd(__pa_symbol(level1_fixmap_pgt) | _PAGE_TABLE);

	early_make_page_readonly(init_level4_pgt,
				 XENFEAT_writable_page_tables);
	early_make_page_readonly(level3_kernel_pgt,
				 XENFEAT_writable_page_tables);
	early_make_page_readonly(level3_user_pgt,
				 XENFEAT_writable_page_tables);
	early_make_page_readonly(level2_fixmap_pgt,
				 XENFEAT_writable_page_tables);
	early_make_page_readonly(level1_fixmap_pgt,
				 XENFEAT_writable_page_tables);

	if (!xen_feature(XENFEAT_writable_page_tables))
		xen_pgd_pin(init_level4_pgt);
}

void __init xen_finish_init_mapping(void)
{
	unsigned long start, end;
	struct mmuext_op mmuext;

	/* Re-vector virtual addresses pointing into the initial
	   mapping to the just-established permanent ones. */
	xen_start_info = __va(__pa(xen_start_info));
	xen_start_info->pt_base = (unsigned long)
		__va(__pa(xen_start_info->pt_base));
	if (!xen_feature(XENFEAT_auto_translated_physmap)
	    && xen_start_info->mfn_list >= __START_KERNEL_map)
		phys_to_machine_mapping =
			__va(__pa(xen_start_info->mfn_list));
	if (xen_start_info->mod_start)
		xen_start_info->mod_start = (unsigned long)
			__va(__pa(xen_start_info->mod_start));

	/* Unpin the no longer used Xen provided page tables. */
	mmuext.cmd = MMUEXT_UNPIN_TABLE;
	mmuext.arg1.mfn = virt_to_mfn(xen_start_info->pt_base);
	if (HYPERVISOR_mmuext_op(&mmuext, 1, NULL, DOMID_SELF))
		BUG();

	/* Destroy the Xen-created mappings beyond the kernel image. */
	start = PAGE_ALIGN(_brk_end);
	end   = __START_KERNEL_map + (e820_table_start << PAGE_SHIFT);
	for (; start < end; start += PAGE_SIZE)
		if (HYPERVISOR_update_va_mapping(start, __pte_ma(0), 0))
			BUG();

	WARN(e820_table_end != e820_table_top, "start=%lx cur=%lx top=%lx\n",
	     e820_table_start, e820_table_end, e820_table_top);
	if (e820_table_end > e820_table_top)
		e820_table_top = e820_table_end;
}

unsigned long __meminit
kernel_physical_mapping_init(unsigned long start,
			     unsigned long end,
			     unsigned long page_size_mask)
{

	unsigned long next, last_map_addr = end;

	start = (unsigned long)__va(start);
	end = (unsigned long)__va(end);

	for (; start < end; start = next) {
		pgd_t *pgd = pgd_offset_k(start);
		unsigned long pud_phys;
		pud_t *pud;

		next = (start + PGDIR_SIZE) & PGDIR_MASK;
		if (next > end)
			next = end;

		if (__pgd_val(*pgd)) {
			last_map_addr = phys_pud_update(pgd, __pa(start),
						 __pa(end), page_size_mask);
			continue;
		}

		pud = alloc_low_page(&pud_phys);
		last_map_addr = phys_pud_init(pud, __pa(start), __pa(next),
						 page_size_mask);
		unmap_low_page(pud);

		if(!after_bootmem) {
			if (max_pfn_mapped)
				make_page_readonly(__va(pud_phys),
						   XENFEAT_writable_page_tables);
			xen_l4_entry_update(pgd, __pgd(pud_phys | _PAGE_TABLE));
		} else {
			spin_lock(&init_mm.page_table_lock);
			pgd_populate(&init_mm, pgd, __va(pud_phys));
			spin_unlock(&init_mm.page_table_lock);
		}
	}

	return last_map_addr;
}

#ifndef CONFIG_NUMA
void __init initmem_init(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long bootmap_size, bootmap;

	e820_register_active_regions(0, start_pfn, end_pfn);
#ifdef CONFIG_XEN
	if (end_pfn > xen_start_info->nr_pages)
		end_pfn = xen_start_info->nr_pages;
#endif
	bootmap_size = bootmem_bootmap_pages(end_pfn)<<PAGE_SHIFT;
	bootmap = find_e820_area(0, end_pfn<<PAGE_SHIFT, bootmap_size,
				 PAGE_SIZE);
	if (bootmap == -1L)
		panic("Cannot find bootmem map of size %ld\n", bootmap_size);
	/* don't touch min_low_pfn */
	bootmap_size = init_bootmem_node(NODE_DATA(0), bootmap >> PAGE_SHIFT,
					 0, end_pfn);
	free_bootmem_with_active_regions(0, end_pfn);
	early_res_to_bootmem(0, end_pfn<<PAGE_SHIFT);
	reserve_bootmem(bootmap, bootmap_size, BOOTMEM_DEFAULT);
}
#endif

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
	max_zone_pfns[ZONE_NORMAL] = max_pfn;

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();

	/*
	 * clear the default setting with node 0
	 * note: don't use nodes_clear here, that is really clearing when
	 *	 numa support is not compiled in, and later node_set_state
	 *	 will not set it back.
	 */
	node_clear_state(0, N_NORMAL_MEMORY);

	free_area_init_nodes(max_zone_pfns);

	xen_init_pgd_pin();
}

/*
 * Memory hotplug specific functions
 */
#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Memory is added always to NORMAL zone. This means you will never get
 * additional DMA/DMA32 memory.
 */
int arch_add_memory(int nid, u64 start, u64 size)
{
	struct pglist_data *pgdat = NODE_DATA(nid);
	struct zone *zone = pgdat->node_zones + ZONE_NORMAL;
	unsigned long last_mapped_pfn, start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	last_mapped_pfn = init_memory_mapping(start, start + size);
	if (last_mapped_pfn > max_pfn_mapped)
		max_pfn_mapped = last_mapped_pfn;

	ret = __add_pages(nid, zone, start_pfn, nr_pages);
	WARN_ON_ONCE(ret);

	return ret;
}
EXPORT_SYMBOL_GPL(arch_add_memory);

#if !defined(CONFIG_ACPI_NUMA) && defined(CONFIG_NUMA)
int memory_add_physaddr_to_nid(u64 start)
{
	return 0;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif

#endif /* CONFIG_MEMORY_HOTPLUG */

static struct kcore_list kcore_vsyscall;

void __init mem_init(void)
{
	long codesize, reservedpages, datasize, initsize;
	unsigned long absent_pages;
	unsigned long pfn;

	pci_iommu_alloc();

	/* clear_bss() already clear the empty_zero_page */

	reservedpages = 0;

	/* this will put all low memory onto the freelists */
#ifdef CONFIG_NUMA
	totalram_pages = numa_free_all_bootmem();
#else
	totalram_pages = free_all_bootmem();
#endif

	/* XEN: init pages outside initial allocation. */
	for (pfn = xen_start_info->nr_pages; pfn < max_pfn; pfn++) {
		ClearPageReserved(pfn_to_page(pfn));
		init_page_count(pfn_to_page(pfn));
	}

	absent_pages = absent_pages_in_range(0, max_pfn);
	reservedpages = max_pfn - totalram_pages - absent_pages;
	after_bootmem = 1;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	/* Register memory areas for /proc/kcore */
	kclist_add(&kcore_vsyscall, (void *)VSYSCALL_START,
			 VSYSCALL_END - VSYSCALL_START, KCORE_OTHER);

	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
			 "%ldk absent, %ldk reserved, %ldk data, %ldk init)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		max_pfn << (PAGE_SHIFT-10),
		codesize >> 10,
		absent_pages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);
}

#ifdef CONFIG_DEBUG_RODATA
const int rodata_test_data = 0xC3;
EXPORT_SYMBOL_GPL(rodata_test_data);

static int kernel_set_to_readonly;

void set_kernel_text_rw(void)
{
	unsigned long start = PFN_ALIGN(_stext);
	unsigned long end = PFN_ALIGN(__start_rodata);

	if (!kernel_set_to_readonly)
		return;

	pr_debug("Set kernel text: %lx - %lx for read write\n",
		 start, end);

	set_memory_rw(start, (end - start) >> PAGE_SHIFT);
}

void set_kernel_text_ro(void)
{
	unsigned long start = PFN_ALIGN(_stext);
	unsigned long end = PFN_ALIGN(__start_rodata);

	if (!kernel_set_to_readonly)
		return;

	pr_debug("Set kernel text: %lx - %lx for read only\n",
		 start, end);

	set_memory_ro(start, (end - start) >> PAGE_SHIFT);
}

void mark_rodata_ro(void)
{
	unsigned long start = PFN_ALIGN(_stext), end = PFN_ALIGN(__end_rodata);
	unsigned long rodata_start =
		((unsigned long)__start_rodata + PAGE_SIZE - 1) & PAGE_MASK;

	printk(KERN_INFO "Write protecting the kernel read-only data: %luk\n",
	       (end - start) >> 10);
	set_memory_ro(start, (end - start) >> PAGE_SHIFT);

	kernel_set_to_readonly = 1;

	/*
	 * The rodata section (but not the kernel text!) should also be
	 * not-executable.
	 */
	set_memory_nx(rodata_start, (end - rodata_start) >> PAGE_SHIFT);

	rodata_test();

#ifdef CONFIG_CPA_DEBUG
	printk(KERN_INFO "Testing CPA: undo %lx-%lx\n", start, end);
	set_memory_rw(start, (end-start) >> PAGE_SHIFT);

	printk(KERN_INFO "Testing CPA: again\n");
	set_memory_ro(start, (end-start) >> PAGE_SHIFT);
#endif
}

#endif

int __init reserve_bootmem_generic(unsigned long phys, unsigned long len,
				   int flags)
{
#ifdef CONFIG_NUMA
	int nid, next_nid;
	int ret;
#endif
	unsigned long pfn = phys >> PAGE_SHIFT;

	if (pfn >= max_pfn) {
		/*
		 * This can happen with kdump kernels when accessing
		 * firmware tables:
		 */
		if (pfn < max_pfn_mapped)
			return -EFAULT;

		printk(KERN_ERR "reserve_bootmem: illegal reserve %lx %lu\n",
				phys, len);
		return -EFAULT;
	}

	/* Should check here against the e820 map to avoid double free */
#ifdef CONFIG_NUMA
	nid = phys_to_nid(phys);
	next_nid = phys_to_nid(phys + len - 1);
	if (nid == next_nid)
		ret = reserve_bootmem_node(NODE_DATA(nid), phys, len, flags);
	else
		ret = reserve_bootmem(phys, len, flags);

	if (ret != 0)
		return ret;

#else
	reserve_bootmem(phys, len, flags);
#endif

#ifndef CONFIG_XEN
	if (phys+len <= MAX_DMA_PFN*PAGE_SIZE) {
		dma_reserve += len / PAGE_SIZE;
		set_dma_reserve(dma_reserve);
	}
#endif

	return 0;
}

int kern_addr_valid(unsigned long addr)
{
	unsigned long above = ((long)addr) >> __VIRTUAL_MASK_SHIFT;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if (above != 0 && above != -1UL)
		return 0;

	pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd))
		return 0;

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud))
		return 0;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return pfn_valid(pmd_pfn(*pmd));

	pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte))
		return 0;

	return pfn_valid(pte_pfn(*pte));
}

/*
 * A pseudo VMA to allow ptrace access for the vsyscall page.  This only
 * covers the 64bit vsyscall page now. 32bit has a real VMA now and does
 * not need special handling anymore:
 */
static struct vm_area_struct gate_vma = {
	.vm_start	= VSYSCALL_START,
	.vm_end		= VSYSCALL_START + (VSYSCALL_MAPPED_PAGES * PAGE_SIZE),
	.vm_page_prot	= PAGE_READONLY_EXEC,
	.vm_flags	= VM_READ | VM_EXEC
};

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
#ifdef CONFIG_IA32_EMULATION
	if (test_tsk_thread_flag(tsk, TIF_IA32))
		return NULL;
#endif
	return &gate_vma;
}

int in_gate_area(struct task_struct *task, unsigned long addr)
{
	struct vm_area_struct *vma = get_gate_vma(task);

	if (!vma)
		return 0;

	return (addr >= vma->vm_start) && (addr < vma->vm_end);
}

/*
 * Use this when you have no reliable task/vma, typically from interrupt
 * context. It is less reliable than using the task's vma and may give
 * false positives:
 */
int in_gate_area_no_task(unsigned long addr)
{
	return (addr >= VSYSCALL_START) && (addr < VSYSCALL_END);
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	if (vma == &gate_vma)
		return "[vsyscall]";
	return NULL;
}

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * Initialise the sparsemem vmemmap using huge-pages at the PMD level.
 */
static long __meminitdata addr_start, addr_end;
static void __meminitdata *p_start, *p_end;
static int __meminitdata node_start;

int __meminit
vmemmap_populate(struct page *start_page, unsigned long size, int node)
{
	unsigned long addr = (unsigned long)start_page;
	unsigned long end = (unsigned long)(start_page + size);
	unsigned long next;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	for (; addr < end; addr = next) {
		void *p = NULL;

		pgd = vmemmap_pgd_populate(addr, node);
		if (!pgd)
			return -ENOMEM;

		pud = vmemmap_pud_populate(pgd, addr, node);
		if (!pud)
			return -ENOMEM;

		if (!cpu_has_pse) {
			next = (addr + PAGE_SIZE) & PAGE_MASK;
			pmd = vmemmap_pmd_populate(pud, addr, node);

			if (!pmd)
				return -ENOMEM;

			p = vmemmap_pte_populate(pmd, addr, node);

			if (!p)
				return -ENOMEM;

			addr_end = addr + PAGE_SIZE;
			p_end = p + PAGE_SIZE;
		} else {
			next = pmd_addr_end(addr, end);

			pmd = pmd_offset(pud, addr);
			if (pmd_none(*pmd)) {
				pte_t entry;

				p = vmemmap_alloc_block(PMD_SIZE, node);
				if (!p)
					return -ENOMEM;

				entry = pfn_pte(__pa(p) >> PAGE_SHIFT,
						PAGE_KERNEL_LARGE);
				set_pmd(pmd, __pmd_ma(__pte_val(entry)));

				/* check to see if we have contiguous blocks */
				if (p_end != p || node_start != node) {
					if (p_start)
						printk(KERN_DEBUG " [%lx-%lx] PMD -> [%p-%p] on node %d\n",
						       addr_start, addr_end-1, p_start, p_end-1, node_start);
					addr_start = addr;
					node_start = node;
					p_start = p;
				}

				addr_end = addr + PMD_SIZE;
				p_end = p + PMD_SIZE;
			} else
				vmemmap_verify((pte_t *)pmd, node, addr, next);
		}

	}
	return 0;
}

void __meminit vmemmap_populate_print_last(void)
{
	if (p_start) {
		printk(KERN_DEBUG " [%lx-%lx] PMD -> [%p-%p] on node %d\n",
			addr_start, addr_end-1, p_start, p_end-1, node_start);
		p_start = NULL;
		p_end = NULL;
		node_start = 0;
	}
}
#endif
