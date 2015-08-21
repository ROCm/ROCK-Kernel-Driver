/*
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 */

#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mmiotrace.h>

#include <asm/cacheflush.h>
#include <asm/e820.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/pat.h>

#include "physaddr.h"

static int direct_remap_area_pte_fn(pte_t *pte,
				    struct page *pmd_page,
				    unsigned long address,
				    void *data)
{
	mmu_update_t **v = (mmu_update_t **)data;

	BUG_ON(!pte_none(*pte));

	(*v)->ptr = ((u64)pfn_to_mfn(page_to_pfn(pmd_page)) <<
		     PAGE_SHIFT) | ((unsigned long)pte & ~PAGE_MASK);
	(*v)++;

	return 0;
}

static int __direct_remap_pfn_range(struct mm_struct *mm,
				    unsigned long address,
				    unsigned long mfn,
				    const unsigned long *mfns,
				    unsigned long size,
				    pgprot_t prot,
				    domid_t  domid,
				    void (*cb)(unsigned int idx, int rc,
					       void *),
				    void *ctxt)
{
	int rc = 0;
	unsigned int i, idx;
	mmu_update_t *u, *v;

	u = (mmu_update_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	if (u == NULL)
		return -ENOMEM;

	pgprot_val(prot) |= _PAGE_IOMAP;

	flush_cache_all();

	for (i = idx = 0, v = u; ; ) {
		/*
		 * Fill in the machine address: PTE ptr is done later by
		 * apply_to_page_range().
		 */
		v->val = __pte_val(pte_mkspecial(pfn_pte_ma(mfn, prot)));

		i += PAGE_SIZE;
		v++;

		if (i >= size || v - u == PAGE_SIZE / sizeof(mmu_update_t)) {
			mmu_update_t *w = u;
			unsigned int nr = v - u;

			/* Flush a full batch after filling in the PTE ptrs. */
			rc = apply_to_page_range(mm, address, i,
						 direct_remap_area_pte_fn, &w);
			if (rc)
				break;

			v = u;
			do {
				unsigned int j, done;

				rc = HYPERVISOR_mmu_update(v, nr, &done,
							   domid);
				if (!cb)
					break;
				for (j = 0; j < done; ++j)
					cb(idx++, 0, ctxt);
				if (!rc)
					break;
				cb(idx++, rc, ctxt);
				rc = 0;
				v += done + 1;
				nr -= done + 1;
			} while (nr);

			if (rc || i >= size)
				break;

			size -= i;
			address += i;
			i = 0;
			v = u;
		}

		if (mfns)
			mfn = *++mfns;
		else
			++mfn;
	}

	flush_tlb_all();

	free_page((unsigned long)u);

	return rc;
}

int direct_remap_pfn_range(struct vm_area_struct *vma,
			   unsigned long address,
			   unsigned long mfn,
			   unsigned long size,
			   pgprot_t prot,
			   domid_t  domid)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return remap_pfn_range(vma, address, mfn, size, prot);

	if (domid == DOMID_SELF)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_mm->context.has_foreign_mappings = 1;

	return __direct_remap_pfn_range(vma->vm_mm, address, mfn, NULL, size,
					prot, domid, NULL, NULL);
}
EXPORT_SYMBOL(direct_remap_pfn_range);

int direct_kernel_remap_pfn_range(unsigned long address,
				  unsigned long mfn,
				  unsigned long size,
				  pgprot_t prot,
				  domid_t  domid)
{
	return __direct_remap_pfn_range(&init_mm, address, mfn, NULL, size,
					prot, domid, NULL, NULL);
}
EXPORT_SYMBOL(direct_kernel_remap_pfn_range);

int direct_remap_pfns_range(struct vm_area_struct *vma,
			    unsigned long address,
			    const unsigned long *mfns,
			    unsigned long size,
			    pgprot_t prot,
			    domid_t  domid,
			    void (*cb)(unsigned int idx, int rc, void *),
			    void *ctxt)
{
	if (xen_feature(XENFEAT_auto_translated_physmap)) {
		unsigned long offs;

		for (offs = 0; offs < size; offs += PAGE_SIZE, ++mfns) {
			int rc = remap_pfn_range(vma, address + offs, *mfns,
						 PAGE_SIZE, prot);
			cb(offs >> PAGE_SHIFT, rc, ctxt);
		}
		return 0;
	}

	if (domid == DOMID_SELF)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_mm->context.has_foreign_mappings = 1;

	return __direct_remap_pfn_range(vma->vm_mm, address, *mfns, mfns,
					size, prot, domid, cb, ctxt);
}

static int lookup_pte_fn(
	pte_t *pte, struct page *pmd_page, unsigned long addr, void *data)
{
	uint64_t *ptep = (uint64_t *)data;
	if (ptep)
		*ptep = ((uint64_t)pfn_to_mfn(page_to_pfn(pmd_page)) <<
			 PAGE_SHIFT) | ((unsigned long)pte & ~PAGE_MASK);
	return 0;
}

int create_lookup_pte_addr(struct mm_struct *mm,
			   unsigned long address,
			   uint64_t *ptep)
{
	return apply_to_page_range(mm, address, PAGE_SIZE,
				   lookup_pte_fn, ptep);
}

EXPORT_SYMBOL(create_lookup_pte_addr);

#ifdef CONFIG_MODULES
/*
 * Force the implementation of ioremap_page_range() to be pulled in from
 * lib/lib.a even if there is no other reference from the core kernel to it
 * (native uses it in __ioremap_caller()), so that it gets exported.
 */
static void *const __section(.discard.ioremap) __used
_ioremap_page_range = ioremap_page_range;
#endif

/*
 * Fix up the linear direct mapping of the kernel to avoid cache attribute
 * conflicts.
 */
static int ioremap_change_attr(unsigned long vaddr, unsigned long size,
			       enum page_cache_mode pcm)
{
	unsigned long nrpages = size >> PAGE_SHIFT;
	int err;

	switch (pcm) {
	case _PAGE_CACHE_MODE_UC:
	default:
		err = _set_memory_uc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_MODE_WC:
		err = _set_memory_wc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_MODE_WT:
		err = _set_memory_wt(vaddr, nrpages);
		break;
	case _PAGE_CACHE_MODE_WB:
		err = _set_memory_wb(vaddr, nrpages);
		break;
	}

	return err;
}

int ioremap_check_change_attr(unsigned long mfn, unsigned long size,
			      enum page_cache_mode pcm)
{
	unsigned long sz;
	int rc;

	for (sz = rc = 0; sz < size && !rc; ++mfn, sz += PAGE_SIZE) {
		unsigned long pfn = mfn_to_local_pfn(mfn);

		if (pfn >= max_low_pfn_mapped &&
		    (pfn < (1UL<<(32 - PAGE_SHIFT)) || pfn >= max_pfn_mapped))
			continue;
		rc = ioremap_change_attr((unsigned long)__va(pfn << PAGE_SHIFT),
					 PAGE_SIZE, pcm);
	}

	return rc;
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. It transparently creates kernel huge I/O mapping when
 * the physical address is aligned by a huge page size (1GB or 2MB) and
 * the requested size is at least the huge page size.
 *
 * NOTE: MTRRs can override PAT memory types with a 4KB granularity.
 * Therefore, the mapping code falls back to use a smaller page toward 4KB
 * when a mapping range is covered by non-WB type of MTRRs.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
static void __iomem *__ioremap_caller(resource_size_t phys_addr,
		unsigned long size, enum page_cache_mode pcm, void *caller)
{
	unsigned long mfn, last_mfn, offset, vaddr;
	resource_size_t last_addr;
	const resource_size_t unaligned_phys_addr = phys_addr;
	const unsigned long unaligned_size = size;
	struct vm_struct *area;
	enum page_cache_mode new_pcm;
	pgprot_t prot;
	int retval;
	domid_t domid = DOMID_IO;
	void __iomem *ret_addr;
	int ram_region;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	if (!phys_addr_valid(phys_addr)) {
		printk(KERN_WARNING "ioremap: invalid physical address %llx\n",
		       (unsigned long long)phys_addr);
		WARN_ON_ONCE(1);
		return NULL;
	}

	/*
	 * Don't remap the low PCI/ISA area, it's always mapped..
	 */
	if (is_initial_xendomain() && is_ISA_range(phys_addr, last_addr))
		return (__force void __iomem *)isa_bus_to_virt((unsigned long)phys_addr);

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	ram_region = is_initial_xendomain() ? region_is_ram(phys_addr, size)
					    : -1;
	last_mfn = PFN_DOWN(last_addr);
	for (mfn = PFN_DOWN(phys_addr);
	     ram_region < 0 && mfn <= last_mfn; mfn++) {
		unsigned long pfn = mfn_to_local_pfn(mfn);

		if (pfn_valid(pfn)) {
			if (!PageReserved(pfn_to_page(pfn)))
				ram_region = 1;
			domid = DOMID_SELF;
		}
	}
	if (WARN_ONCE(ram_region > 0, "ioremap on RAM at %pa - %pa\n",
		      &phys_addr, &last_addr))
		return NULL;
	WARN_ON_ONCE(domid == DOMID_SELF);

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PHYSICAL_PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	retval = reserve_memtype(phys_addr, (u64)phys_addr + size,
						pcm, &new_pcm);
	if (retval) {
		printk(KERN_ERR "ioremap reserve_memtype failed %d\n", retval);
		return NULL;
	}

	if (pcm != new_pcm) {
		if (!is_new_memtype_allowed(phys_addr, size, pcm, new_pcm)) {
			printk(KERN_ERR
		"ioremap error for 0x%llx-0x%llx, requested 0x%x, got 0x%x\n",
				(unsigned long long)phys_addr,
				(unsigned long long)(phys_addr + size),
				pcm, new_pcm);
			goto err_free_memtype;
		}
		pcm = new_pcm;
	}

	prot = PAGE_KERNEL_IO;
	switch (pcm) {
	case _PAGE_CACHE_MODE_UC:
	default:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_UC));
		break;
	case _PAGE_CACHE_MODE_UC_MINUS:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS));
		break;
	case _PAGE_CACHE_MODE_WC:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_WC));
		break;
	case _PAGE_CACHE_MODE_WT:
		prot = __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_WT));
		break;
	case _PAGE_CACHE_MODE_WB:
		break;
	}

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		goto err_free_memtype;
	area->phys_addr = phys_addr;
	vaddr = (unsigned long) area->addr;

	if (kernel_map_sync_memtype(phys_addr, size, pcm))
		goto err_free_area;

	if (__direct_remap_pfn_range(&init_mm, vaddr, PFN_DOWN(phys_addr),
				     NULL, size, prot, domid, NULL, NULL))
		goto err_free_area;

	ret_addr = (void __iomem *) (vaddr + offset);
	mmiotrace_ioremap(unaligned_phys_addr, unaligned_size, ret_addr);

	/*
	 * Check if the request spans more than any BAR in the iomem resource
	 * tree.
	 */
	WARN_ONCE(iomem_map_sanity_check(unaligned_phys_addr, unaligned_size),
		  KERN_INFO "Info: mapping multiple BARs. Your kernel is fine.");

	return ret_addr;
err_free_area:
	free_vm_area(area);
err_free_memtype:
	free_memtype(phys_addr, phys_addr + size);
	return NULL;
}

/**
 * ioremap_nocache     -   map bus memory into CPU space
 * @phys_addr:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_nocache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * on the CPU as well as honouring existing caching rules from things like
 * the PCI bus. Note that there are other caches and buffers on many
 * busses. In particular driver authors should read up on PCI writes
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_nocache(resource_size_t phys_addr, unsigned long size)
{
	/*
	 * Ideally, this should be:
	 *	pat_enabled() ? _PAGE_CACHE_MODE_UC : _PAGE_CACHE_MODE_UC_MINUS;
	 *
	 * Till we fix all X drivers to use ioremap_wc(), we will use
	 * UC MINUS. Drivers that are certain they need or can already
	 * be converted over to strong UC can use ioremap_uc().
	 */
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_UC_MINUS;

	return __ioremap_caller(phys_addr, size, pcm,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_nocache);

/**
 * ioremap_uc     -   map bus memory into CPU space as strongly uncachable
 * @phys_addr:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_uc performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked with a strong
 * preference as completely uncachable on the CPU when possible. For non-PAT
 * systems this ends up setting page-attribute flags PCD=1, PWT=1. For PAT
 * systems this will set the PAT entry for the pages as strong UC.  This call
 * will honor existing caching rules from things like the PCI bus. Note that
 * there are other caches and buffers on many busses. In particular driver
 * authors should read up on PCI writes.
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_uc(resource_size_t phys_addr, unsigned long size)
{
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_UC;

	return __ioremap_caller(phys_addr, size, pcm,
				__builtin_return_address(0));
}
EXPORT_SYMBOL_GPL(ioremap_uc);

/**
 * ioremap_wc	-	map memory into CPU space write combined
 * @phys_addr:	bus address of the memory
 * @size:	size of the resource to map
 *
 * This version of ioremap ensures that the memory is marked write combining.
 * Write combining allows faster writes to some hardware devices.
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_wc(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WC,
					__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_wc);

/**
 * ioremap_wt	-	map memory into CPU space write through
 * @phys_addr:	bus address of the memory
 * @size:	size of the resource to map
 *
 * This version of ioremap ensures that the memory is marked write through.
 * Write through stores data into memory while keeping the cache up-to-date.
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_wt(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WT,
					__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_wt);

void __iomem *ioremap_cache(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_MODE_WB,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);

void __iomem *ioremap_prot(resource_size_t phys_addr, unsigned long size,
				unsigned long prot_val)
{
	return __ioremap_caller(phys_addr, size,
				pgprot2cachemode(__pgprot(prot_val)),
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_prot);

/**
 * iounmap - Free a IO remapping
 * @addr: virtual address from ioremap_*
 *
 * Caller must ensure there is only one unmapping for the same pointer.
 */
void iounmap(volatile void __iomem *addr)
{
	struct vm_struct *p, *o;

	if ((void __force *)addr <= high_memory)
		return;

	/*
	 * __ioremap special-cases the PCI/ISA range by not instantiating a
	 * vm_area and by simply returning an address into the kernel mapping
	 * of ISA space.   So handle that here.
	 */
	if ((unsigned long)addr >= fix_to_virt(FIX_ISAMAP_BEGIN))
		return;

	addr = (volatile void __iomem *)
		(PAGE_MASK & (unsigned long __force)addr);

	mmiotrace_iounmap(addr);

	/* Use the vm area unlocked, assuming the caller
	   ensures there isn't another iounmap for the same address
	   in parallel. Reuse of the virtual address is prevented by
	   leaving it in the global lists until we're done with it.
	   cpa takes care of the direct mappings. */
	p = find_vm_area((void __force *)addr);

	if (!p) {
		printk(KERN_ERR "iounmap: bad address %p\n", addr);
		dump_stack();
		return;
	}

	free_memtype(p->phys_addr, p->phys_addr + get_vm_area_size(p));

	/* Finally remove it */
	o = remove_vm_area((void __force *)addr);
	BUG_ON(p != o || o == NULL);
	kfree(p);
}
EXPORT_SYMBOL(iounmap);

#ifndef CONFIG_XEN
int __init arch_ioremap_pud_supported(void)
{
#ifdef CONFIG_X86_64
	return cpu_has_gbpages;
#else
	return 0;
#endif
}

int __init arch_ioremap_pmd_supported(void)
{
	return cpu_has_pse;
}

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
void *xlate_dev_mem_ptr(phys_addr_t phys)
{
	unsigned long start  = phys &  PAGE_MASK;
	unsigned long offset = phys & ~PAGE_MASK;
	void *vaddr;

	/* If page is RAM, we can use __va. Otherwise ioremap and unmap. */
	if (page_is_ram(start >> PAGE_SHIFT))
		return __va(phys);

	vaddr = ioremap_cache(start, PAGE_SIZE);
	/* Only add the offset on success and return NULL if the ioremap() failed: */
	if (vaddr)
		vaddr += offset;

	return vaddr;
}

void unxlate_dev_mem_ptr(phys_addr_t phys, void *addr)
{
	if (page_is_ram(phys >> PAGE_SHIFT))
		return;

	iounmap((void __iomem *)((unsigned long)addr & PAGE_MASK));
}
#endif

static pte_t bm_pte[PAGE_SIZE/sizeof(pte_t)] __page_aligned_bss;

#ifdef CONFIG_X86_32
static inline pmd_t * __init early_ioremap_pmd(unsigned long addr)
{
	/* Don't assume we're using swapper_pg_dir at this point */
	pgd_t *base = __va(read_cr3());
	pgd_t *pgd = &base[pgd_index(addr)];
	pud_t *pud = pud_offset(pgd, addr);
	pmd_t *pmd = pmd_offset(pud, addr);

	return pmd;
}
#else
#define early_ioremap_pmd early_get_pmd
#undef make_lowmem_page_readonly
#define make_lowmem_page_readonly early_make_page_readonly
#endif

static inline pte_t * __init early_ioremap_pte(unsigned long addr)
{
	return &bm_pte[pte_index(addr)];
}

bool __init is_early_ioremap_ptep(pte_t *ptep)
{
	return ptep >= &bm_pte[0] && ptep < &bm_pte[PAGE_SIZE/sizeof(pte_t)];
}

void __init early_ioremap_init(void)
{
	pmd_t *pmd;

#ifdef CONFIG_X86_64
	BUILD_BUG_ON((fix_to_virt(0) + PAGE_SIZE) & ((1 << PMD_SHIFT) - 1));
#else
	WARN_ON((fix_to_virt(0) + PAGE_SIZE) & ((1 << PMD_SHIFT) - 1));
#endif

	early_ioremap_setup();

	pmd = early_ioremap_pmd(fix_to_virt(FIX_BTMAP_BEGIN));
	memset(bm_pte, 0, sizeof(bm_pte));
	make_lowmem_page_readonly(bm_pte, XENFEAT_writable_page_tables);
	pmd_populate_kernel(&init_mm, pmd, bm_pte);

	/*
	 * The boot-ioremap range spans multiple pmds, for which
	 * we are not prepared:
	 */
#define __FIXADDR_TOP (-PAGE_SIZE)
	BUILD_BUG_ON((__fix_to_virt(FIX_BTMAP_BEGIN) >> PMD_SHIFT)
		     != (__fix_to_virt(FIX_BTMAP_END) >> PMD_SHIFT));
#undef __FIXADDR_TOP
	if (pmd != early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END))) {
		WARN_ON(1);
		printk(KERN_WARNING "pmd %p != %p\n",
		       pmd, early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END)));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_BEGIN): %08lx\n",
			fix_to_virt(FIX_BTMAP_BEGIN));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_END):   %08lx\n",
			fix_to_virt(FIX_BTMAP_END));

		printk(KERN_WARNING "FIX_BTMAP_END:       %d\n", FIX_BTMAP_END);
		printk(KERN_WARNING "FIX_BTMAP_BEGIN:     %d\n",
		       FIX_BTMAP_BEGIN);
	}
}

void __init __early_set_fixmap(enum fixed_addresses idx,
			       phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	pte = early_ioremap_pte(addr);

	if (pgprot_val(flags))
		set_pte(pte, pfn_pte_ma(phys >> PAGE_SHIFT, flags));
	else
		pte_clear(&init_mm, addr, pte);
	__flush_tlb_one(addr);
}
