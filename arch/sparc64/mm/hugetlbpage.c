/*
 * SPARC64 Huge TLB page support.
 *
 * Copyright (C) 2002, 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd) {
		pmd = pmd_alloc(mm, pgd, addr);
		if (pmd)
			pte = pte_alloc_map(mm, pmd, addr);
	}
	return pte;
}

static pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd) {
		pmd = pmd_offset(pgd, addr);
		if (pmd)
			pte = pte_offset_map(pmd, addr);
	}
	return pte;
}

#define mk_pte_huge(entry) do { pte_val(entry) |= _PAGE_SZHUGE; } while (0)

void set_huge_pte(struct mm_struct *mm, struct vm_area_struct *vma,
			 struct page *page, pte_t * page_table, int write_access)
{
	unsigned long i;
	pte_t entry;

	mm->rss += (HPAGE_SIZE / PAGE_SIZE);

	if (write_access)
		entry = pte_mkwrite(pte_mkdirty(mk_pte(page,
						       vma->vm_page_prot)));
	else
		entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));
	entry = pte_mkyoung(entry);
	mk_pte_huge(entry);

	for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
		set_pte(page_table, entry);
		page_table++;

		pte_val(entry) += PAGE_SIZE;
	}
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
	int i;

	for (; addr < end; addr += HPAGE_SIZE) {
		src_pte = huge_pte_offset(src, addr);
		if (!src_pte || pte_none(*src_pte))
			continue;
		dst_pte = huge_pte_alloc(dst, addr);
		if (!dst_pte)
			goto nomem;
		entry = *src_pte;
		ptepage = pte_page(entry);
		get_page(ptepage);
		for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
			set_pte(dst_pte, entry);
			pte_val(entry) += PAGE_SIZE;
			dst_pte++;
		}
		dst->rss += (HPAGE_SIZE / PAGE_SIZE);
	}
	return 0;

nomem:
	return -ENOMEM;
}

int follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
			struct page **pages, struct vm_area_struct **vmas,
			unsigned long *position, int *length, int i)
{
	unsigned long vaddr = *position;
	int remainder = *length;

	WARN_ON(!is_vm_hugetlb_page(vma));

	while (vaddr < vma->vm_end && remainder) {
		if (pages) {
			pte_t *pte;
			struct page *page;

			pte = huge_pte_offset(mm, vaddr);

			/* hugetlb should be locked, and hence, prefaulted */
			BUG_ON(!pte || pte_none(*pte));

			page = pte_page(*pte);

			WARN_ON(!PageCompound(page));

			get_page(page);
			pages[i] = page;
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		--remainder;
		++i;
	}

	*length = remainder;
	*position = vaddr;

	return i;
}

struct page *follow_huge_addr(struct mm_struct *mm,
			      unsigned long address, int write)
{
	return ERR_PTR(-EINVAL);
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

struct page *follow_huge_pmd(struct mm_struct *mm, unsigned long address,
			     pmd_t *pmd, int write)
{
	return NULL;
}

void unmap_hugepage_range(struct vm_area_struct *vma,
			  unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *pte;
	struct page *page;
	int i;

	BUG_ON(start & (HPAGE_SIZE - 1));
	BUG_ON(end & (HPAGE_SIZE - 1));

	for (address = start; address < end; address += HPAGE_SIZE) {
		pte = huge_pte_offset(mm, address);
		if (!pte || pte_none(*pte))
			continue;
		page = pte_page(*pte);
		put_page(page);
		for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
			pte_clear(pte);
			pte++;
		}
	}
	mm->rss -= (end - start) >> PAGE_SHIFT;
	flush_tlb_range(vma, start, end);
}
