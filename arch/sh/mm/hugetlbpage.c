/*
 * arch/sh/mm/hugetlbpage.c
 *
 * SuperH HugeTLB page support.
 *
 * Cloned from sparc64 by Paul Mundt.
 *
 * Copyright (C) 2002, 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <linux/init.h>
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

static long	htlbpagemem;
int		htlbpage_max;
static long	htlbzone_pages;

static struct list_head hugepage_freelists[MAX_NUMNODES];
static spinlock_t htlbpage_lock = SPIN_LOCK_UNLOCKED;

static void enqueue_huge_page(struct page *page)
{
	list_add(&page->lru,
		 &hugepage_freelists[page_zone(page)->zone_pgdat->node_id]);
}

static struct page *dequeue_huge_page(void)
{
	int nid = numa_node_id();
	struct page *page = NULL;

	if (list_empty(&hugepage_freelists[nid])) {
		for (nid = 0; nid < MAX_NUMNODES; ++nid)
			if (!list_empty(&hugepage_freelists[nid]))
				break;
	}
	if (nid >= 0 && nid < MAX_NUMNODES &&
	    !list_empty(&hugepage_freelists[nid])) {
		page = list_entry(hugepage_freelists[nid].next,
				  struct page, list);
		list_del(&page->lru);
	}
	return page;
}

static struct page *alloc_fresh_huge_page(void)
{
	static int nid = 0;
	struct page *page;
	page = alloc_pages_node(nid, GFP_HIGHUSER, HUGETLB_PAGE_ORDER);
	nid = (nid + 1) % numnodes;
	return page;
}

static void free_huge_page(struct page *page);

static struct page *alloc_hugetlb_page(void)
{
	struct page *page;

	spin_lock(&htlbpage_lock);
	page = dequeue_huge_page();
	if (!page) {
		spin_unlock(&htlbpage_lock);
		return NULL;
	}
	htlbpagemem--;
	spin_unlock(&htlbpage_lock);
	set_page_count(page, 1);
	page->lru.prev = (void *)free_huge_page;
	memset(page_address(page), 0, HPAGE_SIZE);
	return page;
}

static pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr)
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

static void set_huge_pte(struct mm_struct *mm, struct vm_area_struct *vma,
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

	while (addr < end) {
		dst_pte = huge_pte_alloc(dst, addr);
		if (!dst_pte)
			goto nomem;
		src_pte = huge_pte_offset(src, addr);
		BUG_ON(!src_pte || pte_none(*src_pte));
		entry = *src_pte;
		ptepage = pte_page(entry);
		get_page(ptepage);
		for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
			set_pte(dst_pte, entry);
			pte_val(entry) += PAGE_SIZE;
			dst_pte++;
		}
		dst->rss += (HPAGE_SIZE / PAGE_SIZE);
		addr += HPAGE_SIZE;
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
			      struct vm_area_struct *vma,
			      unsigned long address, int write)
{
	return NULL;
}

struct vm_area_struct *hugepage_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
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

static void free_huge_page(struct page *page)
{
	BUG_ON(page_count(page));
	BUG_ON(page->mapping);

	spin_lock(&htlbpage_lock);
	enqueue_huge_page(page);
	htlbpagemem++;
	spin_unlock(&htlbpage_lock);
}

void huge_page_release(struct page *page)
{
	if (!put_page_testzero(page))
		return;

	free_huge_page(page);
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
		BUG_ON(!pte);
		if (pte_none(*pte))
			continue;
		page = pte_page(*pte);
		huge_page_release(page);
		for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
			pte_clear(pte);
			pte++;
		}
	}
	mm->rss -= (end - start) >> PAGE_SHIFT;
	flush_tlb_range(vma, start, end);
}

void zap_hugepage_range(struct vm_area_struct *vma,
			unsigned long start, unsigned long length)
{
	struct mm_struct *mm = vma->vm_mm;

	spin_lock(&mm->page_table_lock);
	unmap_hugepage_range(vma, start, start + length);
	spin_unlock(&mm->page_table_lock);
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
			page = alloc_hugetlb_page();
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

static void update_and_free_page(struct page *page)
{
	int j;
	struct page *map;

	map = page;
	htlbzone_pages--;
	for (j = 0; j < (HPAGE_SIZE / PAGE_SIZE); j++) {
		map->flags &= ~(1 << PG_locked | 1 << PG_error | 1 << PG_referenced |
				1 << PG_dirty | 1 << PG_active | 1 << PG_reserved |
				1 << PG_private | 1<< PG_writeback);
		set_page_count(map, 0);
		map++;
	}
	set_page_count(page, 1);
	__free_pages(page, HUGETLB_PAGE_ORDER);
}

static int try_to_free_low(int count)
{
	struct list_head *p;
	struct page *page, *map;

	map = NULL;
	spin_lock(&htlbpage_lock);
	/* all lowmem is on node 0 */
	list_for_each(p, &hugepage_freelists[0]) {
		if (map) {
			list_del(&map->lru);
			update_and_free_page(map);
			htlbpagemem--;
			map = NULL;
			if (++count == 0)
				break;
		}
		page = list_entry(p, struct page, list);
		if (!PageHighMem(page))
			map = page;
	}
	if (map) {
		list_del(&map->lru);
		update_and_free_page(map);
		htlbpagemem--;
		count++;
	}
	spin_unlock(&htlbpage_lock);
	return count;
}

static int set_hugetlb_mem_size(int count)
{
	int lcount;
	struct page *page;

	if (count < 0)
		lcount = count;
	else
		lcount = count - htlbzone_pages;

	if (lcount == 0)
		return (int)htlbzone_pages;
	if (lcount > 0) {	/* Increase the mem size. */
		while (lcount--) {
			page = alloc_fresh_huge_page();
			if (page == NULL)
				break;
			spin_lock(&htlbpage_lock);
			enqueue_huge_page(page);
			htlbpagemem++;
			htlbzone_pages++;
			spin_unlock(&htlbpage_lock);
		}
		return (int) htlbzone_pages;
	}
	/* Shrink the memory size. */
	lcount = try_to_free_low(lcount);
	while (lcount++) {
		page = alloc_hugetlb_page();
		if (page == NULL)
			break;
		spin_lock(&htlbpage_lock);
		update_and_free_page(page);
		spin_unlock(&htlbpage_lock);
	}
	return (int) htlbzone_pages;
}

int hugetlb_sysctl_handler(struct ctl_table *table, int write,
			   struct file *file, void *buffer, size_t *length)
{
	proc_dointvec(table, write, file, buffer, length);
	htlbpage_max = set_hugetlb_mem_size(htlbpage_max);
	return 0;
}

static int __init hugetlb_setup(char *s)
{
	if (sscanf(s, "%d", &htlbpage_max) <= 0)
		htlbpage_max = 0;
	return 1;
}
__setup("hugepages=", hugetlb_setup);

static int __init hugetlb_init(void)
{
	int i;
	struct page *page;

	for (i = 0; i < MAX_NUMNODES; ++i)
		INIT_LIST_HEAD(&hugepage_freelists[i]);

	for (i = 0; i < htlbpage_max; ++i) {
		page = alloc_fresh_huge_page();
		if (!page)
			break;
		spin_lock(&htlbpage_lock);
		enqueue_huge_page(page);
		spin_unlock(&htlbpage_lock);
	}
	htlbpage_max = htlbpagemem = htlbzone_pages = i;
	printk("Total HugeTLB memory allocated, %ld\n", htlbpagemem);
	return 0;
}
module_init(hugetlb_init);

int hugetlb_report_meminfo(char *buf)
{
	return sprintf(buf,
			"HugePages_Total: %5lu\n"
			"HugePages_Free:  %5lu\n"
			"Hugepagesize:    %5lu kB\n",
			htlbzone_pages,
			htlbpagemem,
			HPAGE_SIZE/1024);
}

int is_hugepage_mem_enough(size_t size)
{
	return (size + ~HPAGE_MASK)/HPAGE_SIZE <= htlbpagemem;
}

/* Return the number pages of memory we physically have, in PAGE_SIZE units. */
unsigned long hugetlb_total_pages(void)
{
	return htlbzone_pages * (HPAGE_SIZE / PAGE_SIZE);
}
EXPORT_SYMBOL(hugetlb_total_pages);

/*
 * We cannot handle pagefaults against hugetlb pages at all.  They cause
 * handle_mm_fault() to try to instantiate regular-sized pages in the
 * hugegpage VMA.  do_page_fault() is supposed to trap this, so BUG is we get
 * this far.
 */
static struct page *hugetlb_nopage(struct vm_area_struct *vma,
				   unsigned long address, int *unused)
{
	BUG();
	return NULL;
}

struct vm_operations_struct hugetlb_vm_ops = {
	.nopage = hugetlb_nopage,
};
