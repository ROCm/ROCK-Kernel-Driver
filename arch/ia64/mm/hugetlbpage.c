/*
 * IA-64 Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2002-2004 Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 2003-2004 Ken Chen <kenneth.w.chen@intel.com>
 *
 * Sep, 2003: add numa support
 * Feb, 2004: dynamic hugetlb page size via boot parameter
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

static long	htlbpagemem;
int		htlbpage_max;
static long	htlbzone_pages;
unsigned int	hpage_shift=HPAGE_SHIFT_DEFAULT;

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
		page = list_entry(hugepage_freelists[nid].next, struct page, lru);
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

void free_huge_page(struct page *page);

static struct page *alloc_hugetlb_page(void)
{
	int i;
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
	for (i = 0; i < (HPAGE_SIZE/PAGE_SIZE); ++i)
		clear_highpage(&page[i]);
	return page;
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
/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	if (REGION_NUMBER(addr) != REGION_HPAGE)
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
			get_page(page);
			pages[i] = page;
		}
		if (vmas)
			vmas[i] = vma;
		i++;
		len--;
		start += PAGE_SIZE;
		if (((start & HPAGE_MASK) == pstart) && len &&
				(start < vma->vm_end))
			goto back1;
	} while (len && start < vma->vm_end);
	*length = len;
	*st = start;
	return i;
}

struct vm_area_struct *hugepage_vma(struct mm_struct *mm, unsigned long addr)
{
	if (mm->used_hugetlb) {
		if (REGION_NUMBER(addr) == REGION_HPAGE) {
			struct vm_area_struct *vma = find_vma(mm, addr);
			if (vma && is_vm_hugetlb_page(vma))
				return vma;
		}
	}
	return NULL;
}

struct page *follow_huge_addr(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, int write)
{
	struct page *page;
	pte_t *ptep;

	ptep = huge_pte_offset(mm, addr);
	page = pte_page(*ptep);
	page += ((addr & ~HPAGE_MASK) >> PAGE_SHIFT);
	get_page(page);
	return page;
}
int pmd_huge(pmd_t pmd)
{
	return 0;
}
struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address, pmd_t *pmd, int write)
{
	return NULL;
}

void free_huge_page(struct page *page)
{
	BUG_ON(page_count(page));
	BUG_ON(page->mapping);

	INIT_LIST_HEAD(&page->lru);

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

/*
 * Same as generic free_pgtables(), except constant PGDIR_* and pgd_offset
 * are hugetlb region specific.
 */
void hugetlb_free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *prev,
	unsigned long start, unsigned long end)
{
	unsigned long first = start & HUGETLB_PGDIR_MASK;
	unsigned long last = end + HUGETLB_PGDIR_SIZE - 1;
	unsigned long start_index, end_index;
	struct mm_struct *mm = tlb->mm;

	if (!prev) {
		prev = mm->mmap;
		if (!prev)
			goto no_mmaps;
		if (prev->vm_end > start) {
			if (last > prev->vm_start)
				last = prev->vm_start;
			goto no_mmaps;
		}
	}
	for (;;) {
		struct vm_area_struct *next = prev->vm_next;

		if (next) {
			if (next->vm_start < start) {
				prev = next;
				continue;
			}
			if (last > next->vm_start)
				last = next->vm_start;
		}
		if (prev->vm_end > first)
			first = prev->vm_end + HUGETLB_PGDIR_SIZE - 1;
		break;
	}
no_mmaps:
	if (last < first)	/* for arches with discontiguous pgd indices */
		return;
	/*
	 * If the PGD bits are not consecutive in the virtual address, the
	 * old method of shifting the VA >> by PGDIR_SHIFT doesn't work.
	 */

	start_index = pgd_index(htlbpage_to_page(first));
	end_index = pgd_index(htlbpage_to_page(last));

	if (end_index > start_index) {
		clear_page_tables(tlb, start_index, end_index - start_index);
	}
}

void unmap_hugepage_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
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

void zap_hugepage_range(struct vm_area_struct *vma, unsigned long start, unsigned long length)
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

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct *vmm;

	if (len > RGN_MAP_LIMIT)
		return -ENOMEM;
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	/* This code assumes that REGION_HPAGE != 0. */
	if ((REGION_NUMBER(addr) != REGION_HPAGE) || (addr & (HPAGE_SIZE - 1)))
		addr = HPAGE_REGION_BASE;
	else
		addr = ALIGN(addr, HPAGE_SIZE);
	for (vmm = find_vma(current->mm, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (REGION_OFFSET(addr) + len > RGN_MAP_LIMIT)
			return -ENOMEM;
		if (!vmm || (addr + len) <= vmm->vm_start)
			return addr;
		addr = ALIGN(vmm->vm_end, HPAGE_SIZE);
	}
}
void update_and_free_page(struct page *page)
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

int try_to_free_low(int count)
{
	struct list_head *p;
	struct page *page, *map;

	map = NULL;
	spin_lock(&htlbpage_lock);
	list_for_each(p, &hugepage_freelists[0]) {
		if (map) {
			list_del(&map->lru);
			update_and_free_page(map);
			htlbpagemem--;
			map = NULL;
			if (++count == 0)
				break;
		}
		page = list_entry(p, struct page, lru);
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

int set_hugetlb_mem_size(int count)
{
	int  lcount;
	struct page *page ;

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

int hugetlb_sysctl_handler(ctl_table *table, int write, struct file *file, void *buffer, size_t *length)
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

static int __init hugetlb_setup_sz(char *str)
{
	u64 tr_pages;
	unsigned long long size;

	if (ia64_pal_vm_page_size(&tr_pages, NULL) != 0)
		/*
		 * shouldn't happen, but just in case.
		 */
		tr_pages = 0x15557000UL;

	size = memparse(str, &str);
	if (*str || (size & (size-1)) || !(tr_pages & size) ||
		size <= PAGE_SIZE ||
		size >= (1UL << PAGE_SHIFT << MAX_ORDER)) {
		printk(KERN_WARNING "Invalid huge page size specified\n");
		return 1;
	}

	hpage_shift = __ffs(size);
	/*
	 * boot cpu already executed ia64_mmu_init, and has HPAGE_SHIFT_DEFAULT
	 * override here with new page shift.
	 */
	ia64_set_rr(HPAGE_REGION_BASE, hpage_shift << 2);
	return 1;
}
__setup("hugepagesz=", hugetlb_setup_sz);

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
__initcall(hugetlb_init);

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
	if (size > (htlbpagemem << HPAGE_SHIFT))
		return 0;
	return 1;
}

/* Return the number pages of memory we physically have, in PAGE_SIZE units. */
unsigned long hugetlb_total_pages(void)
{
	return htlbzone_pages * (HPAGE_SIZE / PAGE_SIZE);
}
EXPORT_SYMBOL(hugetlb_total_pages);

static struct page *hugetlb_nopage(struct vm_area_struct * area, unsigned long address, int *unused)
{
	BUG();
	return NULL;
}

struct vm_operations_struct hugetlb_vm_ops = {
	.nopage =	hugetlb_nopage,
};
