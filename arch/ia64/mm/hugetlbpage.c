/*
 * IA-64 Huge TLB Page Support for Kernel.
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

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

static struct vm_operations_struct hugetlb_vm_ops;
struct list_head htlbpage_freelist;
spinlock_t htlbpage_lock = SPIN_LOCK_UNLOCKED;
extern long htlbpagemem;

static void zap_hugetlb_resources (struct vm_area_struct *);

static struct page *
alloc_hugetlb_page (void)
{
	struct list_head *curr, *head;
	struct page *page;

	spin_lock(&htlbpage_lock);

	head = &htlbpage_freelist;
	curr = head->next;

	if (curr == head) {
		spin_unlock(&htlbpage_lock);
		return NULL;
	}
	page = list_entry(curr, struct page, list);
	list_del(curr);
	htlbpagemem--;
	spin_unlock(&htlbpage_lock);
	set_page_count(page, 1);
	memset(page_address(page), 0, HPAGE_SIZE);
	return page;
}

static void
free_hugetlb_page (struct page *page)
{
	spin_lock(&htlbpage_lock);
	if ((page->mapping != NULL) && (page_count(page) == 2)) {
		struct inode *inode = page->mapping->host;
		int i;

		ClearPageDirty(page);
		remove_from_page_cache(page);
		set_page_count(page, 1);
		if ((inode->i_size -= HPAGE_SIZE) == 0) {
			for (i = 0; i < MAX_ID; i++)
				if (htlbpagek[i].key == inode->i_ino) {
					htlbpagek[i].key = 0;
					htlbpagek[i].in = NULL;
					break;
				}
			kfree(inode);
		}
	}
	if (put_page_testzero(page)) {
		list_add(&page->list, &htlbpage_freelist);
		htlbpagemem++;
	}
	spin_unlock(&htlbpage_lock);
}

static pte_t *
huge_pte_alloc (struct mm_struct *mm, unsigned long addr)
{
	unsigned long taddr = htlbpage_to_page(addr);
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, taddr);
	pmd = pmd_alloc(mm, pgd, taddr);
	if (pmd)
		pte = pte_alloc_map(mm, pmd, taddr);
	return pte;
}

static pte_t *
huge_pte_offset (struct mm_struct *mm, unsigned long addr)
{
	unsigned long taddr = htlbpage_to_page(addr);
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, taddr);
	pmd = pmd_offset(pgd, taddr);
	pte = pte_offset_map(pmd, taddr);
	return pte;
}

#define mk_pte_huge(entry) { pte_val(entry) |= _PAGE_P; }

static void
set_huge_pte (struct mm_struct *mm, struct vm_area_struct *vma,
	      struct page *page, pte_t * page_table, int write_access)
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
	return;
}

static int
anon_get_hugetlb_page (struct mm_struct *mm, struct vm_area_struct *vma,
		       int write_access, pte_t * page_table)
{
	struct page *page;

	page = alloc_hugetlb_page();
	if (page == NULL)
		return -1;
	set_huge_pte(mm, vma, page, page_table, write_access);
	return 1;
}

static int
make_hugetlb_pages_present (unsigned long addr, unsigned long end, int flags)
{
	int write;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	pte_t *pte;

	vma = find_vma(mm, addr);
	if (!vma)
		goto out_error1;

	write = (vma->vm_flags & VM_WRITE) != 0;
	if ((vma->vm_end - vma->vm_start) & (HPAGE_SIZE - 1))
		goto out_error1;
	spin_lock(&mm->page_table_lock);
	do {
		pte = huge_pte_alloc(mm, addr);
		if ((pte) && (pte_none(*pte))) {
			if (anon_get_hugetlb_page(mm, vma, write ? VM_WRITE : VM_READ, pte) == -1)
				goto out_error;
		} else
			goto out_error;
		addr += HPAGE_SIZE;
	} while (addr < end);
	spin_unlock(&mm->page_table_lock);
	vma->vm_flags |= (VM_HUGETLB | VM_RESERVED);
	if (flags & MAP_PRIVATE)
		vma->vm_flags |= VM_DONTCOPY;
	vma->vm_ops = &hugetlb_vm_ops;
	return 0;
out_error:
	if (addr > vma->vm_start) {
		vma->vm_end = addr;
		zap_hugetlb_resources(vma);
		vma->vm_end = end;
	}
	spin_unlock(&mm->page_table_lock);
out_error1:
	return -1;
}

int
copy_hugetlb_page_range (struct mm_struct *dst, struct mm_struct *src, struct vm_area_struct *vma)
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
follow_hugetlb_page (struct mm_struct *mm, struct vm_area_struct *vma,
		     struct page **pages, struct vm_area_struct **vmas,
		     unsigned long *st, int *length, int i)
{
	pte_t *ptep, pte;
	unsigned long start = *st;
	unsigned long pstart;
	int len = *length;
	struct page *page;

	do {
		pstart = start & HPAGE_MASK;
		ptep = huge_pte_offset(mm, start);
		pte = *ptep;

back1:
		page = pte_page(pte);
		if (pages) {
			page += ((start & ~HPAGE_MASK) >> PAGE_SHIFT);
			pages[i] = page;
		}
		if (vmas)
			vmas[i] = vma;
		i++;
		len--;
		start += PAGE_SIZE;
		if (((start & HPAGE_MASK) == pstart) && len
		    && (start < vma->vm_end))
			goto back1;
	} while (len && start < vma->vm_end);
	*length = len;
	*st = start;
	return i;
}

static void
zap_hugetlb_resources (struct vm_area_struct *mpnt)
{
	struct mm_struct *mm = mpnt->vm_mm;
	unsigned long len, addr, end;
	pte_t *ptep;
	struct page *page;

	addr = mpnt->vm_start;
	end = mpnt->vm_end;
	len = end - addr;
	do {
		ptep = huge_pte_offset(mm, addr);
		page = pte_page(*ptep);
		pte_clear(ptep);
		free_hugetlb_page(page);
		addr += HPAGE_SIZE;
	} while (addr < end);
	mm->rss -= (len >> PAGE_SHIFT);
	mpnt->vm_ops = NULL;
	flush_tlb_range(mpnt, end - len, end);
}

static void
unlink_vma (struct vm_area_struct *mpnt)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	vma = mm->mmap;
	if (vma == mpnt) {
		mm->mmap = vma->vm_next;
	} else {
		while (vma->vm_next != mpnt) {
			vma = vma->vm_next;
		}
		vma->vm_next = mpnt->vm_next;
	}
	rb_erase(&mpnt->vm_rb, &mm->mm_rb);
	mm->mmap_cache = NULL;
	mm->map_count--;
}

int
set_hugetlb_mem_size (int count)
{
	int j, lcount;
	struct page *page, *map;
	extern long htlbzone_pages;
	extern struct list_head htlbpage_freelist;

	if (count < 0)
		lcount = count;
	else
		lcount = count - htlbzone_pages;
	if (lcount > 0) {	/*Increase the mem size. */
		while (lcount--) {
			page = alloc_pages(__GFP_HIGHMEM, HUGETLB_PAGE_ORDER);
			if (page == NULL)
				break;
			map = page;
			for (j = 0; j < (HPAGE_SIZE / PAGE_SIZE); j++) {
				SetPageReserved(map);
				map++;
			}
			spin_lock(&htlbpage_lock);
			list_add(&page->list, &htlbpage_freelist);
			htlbpagemem++;
			htlbzone_pages++;
			spin_unlock(&htlbpage_lock);
		}
		return (int) htlbzone_pages;
	}
	/*Shrink the memory size. */
	while (lcount++) {
		page = alloc_hugetlb_page();
		if (page == NULL)
			break;
		spin_lock(&htlbpage_lock);
		htlbzone_pages--;
		spin_unlock(&htlbpage_lock);
		map = page;
		for (j = 0; j < (HPAGE_SIZE / PAGE_SIZE); j++) {
			map->flags &= ~(1 << PG_locked | 1 << PG_error | 1 << PG_referenced |
					1 << PG_dirty | 1 << PG_active | 1 << PG_reserved |
					1 << PG_private | 1<< PG_writeback);
			map++;
		}
		set_page_count(page, 1);
		__free_pages(page, HUGETLB_PAGE_ORDER);
	}
	return (int) htlbzone_pages;
}

static struct vm_operations_struct hugetlb_vm_ops = {
	.close =	zap_hugetlb_resources
};
