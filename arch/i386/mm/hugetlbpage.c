/*
 * IA-32 Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

static pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	pmd = pmd_alloc(mm, pgd, addr);
	return (pte_t *) pmd;
}

static pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	pmd = pmd_offset(pgd, addr);
	return (pte_t *) pmd;
}

static void set_huge_pte(struct mm_struct *mm, struct vm_area_struct *vma, struct page *page, pte_t * page_table, int write_access)
{
	pte_t entry;

	mm->rss += (HPAGE_SIZE / PAGE_SIZE);
	if (write_access) {
		entry =
		    pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	} else
		entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));
	entry = pte_mkyoung(entry);
	mk_pte_huge(entry);
	set_pte(page_table, entry);
}

/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	return 0;
}

int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	pte_t *src_pte, *dst_pte, entry;
	struct page *ptepage;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;

	while (addr < end) {
		dst_pte = huge_pte_alloc(dst, addr);
		if (!dst_pte)
			goto nomem;
		src_pte = huge_pte_offset(src, addr);
		entry = *src_pte;
		ptepage = pte_page(entry);
		get_page(ptepage);
		set_pte(dst_pte, entry);
		dst->rss += (HPAGE_SIZE / PAGE_SIZE);
		addr += HPAGE_SIZE;
	}
	return 0;

nomem:
	return -ENOMEM;
}

int
follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
		    struct page **pages, struct vm_area_struct **vmas,
		    unsigned long *position, int *length, int i)
{
	unsigned long vpfn, vaddr = *position;
	int remainder = *length;

	WARN_ON(!is_vm_hugetlb_page(vma));

	vpfn = vaddr/PAGE_SIZE;
	while (vaddr < vma->vm_end && remainder) {

		if (pages) {
			pte_t *pte;
			struct page *page;

			pte = huge_pte_offset(mm, vaddr);

			/* hugetlb should be locked, and hence, prefaulted */
			WARN_ON(!pte || pte_none(*pte));

			page = &pte_page(*pte)[vpfn % (HPAGE_SIZE/PAGE_SIZE)];

			WARN_ON(!PageCompound(page));

			get_page(page);
			pages[i] = page;
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++vpfn;
		--remainder;
		++i;
	}

	*length = remainder;
	*position = vaddr;

	return i;
}

#if 0	/* This is just for testing */
struct page *
follow_huge_addr(struct mm_struct *mm,
	struct vm_area_struct *vma, unsigned long address, int write)
{
	unsigned long start = address;
	int length = 1;
	int nr;
	struct page *page;

	nr = follow_hugetlb_page(mm, vma, &page, NULL, &start, &length, 0);
	if (nr == 1)
		return page;
	return NULL;
}

/*
 * If virtual address `addr' lies within a huge page, return its controlling
 * VMA, else NULL.
 */
struct vm_area_struct *hugepage_vma(struct mm_struct *mm, unsigned long addr)
{
	if (mm->used_hugetlb) {
		struct vm_area_struct *vma = find_vma(mm, addr);
		if (vma && is_vm_hugetlb_page(vma))
			return vma;
	}
	return NULL;
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	return NULL;
}

#else

struct page *
follow_huge_addr(struct mm_struct *mm,
	struct vm_area_struct *vma, unsigned long address, int write)
{
	return NULL;
}

struct vm_area_struct *hugepage_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}

int pmd_huge(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_PSE);
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	struct page *page;

	page = pte_page(*(pte_t *)pmd);
	if (page) {
		page += ((address & ~HPAGE_MASK) >> PAGE_SHIFT);
		get_page(page);
	}
	return page;
}
#endif

void unmap_hugepage_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *pte;
	struct page *page;

	BUG_ON(start & (HPAGE_SIZE - 1));
	BUG_ON(end & (HPAGE_SIZE - 1));

	for (address = start; address < end; address += HPAGE_SIZE) {
		pte = huge_pte_offset(mm, address);
		if (pte_none(*pte))
			continue;
		page = pte_page(*pte);
		huge_page_release(page);
		pte_clear(pte);
	}
	mm->rss -= (end - start) >> PAGE_SHIFT;
	flush_tlb_range(vma, start, end);
}

int hugetlb_prefault(struct address_space *mapping, struct vm_area_struct *vma)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;

	BUG_ON(vma->vm_start & ~HPAGE_MASK);
	BUG_ON(vma->vm_end & ~HPAGE_MASK);

	spin_lock(&mm->page_table_lock);
	for (addr = vma->vm_start; addr < vma->vm_end; addr += HPAGE_SIZE) {
		unsigned long idx;
		pte_t *pte = huge_pte_alloc(mm, addr);
		struct page *page;

		if (!pte) {
			ret = -ENOMEM;
			goto out;
		}
		if (!pte_none(*pte))
			continue;

		idx = ((addr - vma->vm_start) >> HPAGE_SHIFT)
			+ (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));
		page = find_get_page(mapping, idx);
		if (!page) {
			/* charge the fs quota first */
			if (hugetlb_get_quota(mapping)) {
				ret = -ENOMEM;
				goto out;
			}
			page = alloc_huge_page();
			if (!page) {
				hugetlb_put_quota(mapping);
				ret = -ENOMEM;
				goto out;
			}
			ret = add_to_page_cache(page, mapping, idx, GFP_ATOMIC);
			unlock_page(page);
			if (ret) {
				hugetlb_put_quota(mapping);
				free_huge_page(page);
				goto out;
			}
		}
		set_huge_pte(mm, vma, page, pte, vma->vm_flags & VM_WRITE);
	}
out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}
