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
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

static long    htlbpagemem;
int     htlbpage_max;
static long    htlbzone_pages;

struct vm_operations_struct hugetlb_vm_ops;
static LIST_HEAD(htlbpage_freelist);
static spinlock_t htlbpage_lock = SPIN_LOCK_UNLOCKED;

#define MAX_ID 	32

struct hugetlb_key {
	struct radix_tree_root tree;
	atomic_t count;
	spinlock_t lock;
	int key;
	int busy;
	uid_t uid;
	gid_t gid;
	umode_t mode;
	loff_t size;
};

static struct hugetlb_key htlbpagek[MAX_ID];

static void mark_key_busy(struct hugetlb_key *hugetlb_key)
{
	hugetlb_key->busy = 1;
}

static void clear_key_busy(struct hugetlb_key *hugetlb_key)
{
	hugetlb_key->busy = 0;
}

static int key_busy(struct hugetlb_key *hugetlb_key)
{
	return hugetlb_key->busy;
}

static struct hugetlb_key *find_key(int key)
{
	int i;

	for (i = 0; i < MAX_ID; i++) {
		if (htlbpagek[i].key == key)
			return &htlbpagek[i];
	}
	return NULL;
}

static int check_size_prot(struct hugetlb_key *key, unsigned long len, int prot, int flag);
/*
 * Call without htlbpage_lock, returns with htlbpage_lock held.
 */
struct hugetlb_key *alloc_key(int key, unsigned long len, int prot, int flag, int *new)
{
	struct hugetlb_key *hugetlb_key;

	do {
		spin_lock(&htlbpage_lock);
		hugetlb_key = find_key(key);
		if (!hugetlb_key) {
			if (!capable(CAP_SYS_ADMIN) || !in_group_p(0))
				hugetlb_key = ERR_PTR(-EPERM);
			else if (!(flag & IPC_CREAT))
				hugetlb_key = ERR_PTR(-ENOENT);
			else {
				int i;
				for (i = 0; i < MAX_ID; ++i)
					if (!htlbpagek[i].key)
						break;
				if (i == MAX_ID) {
					hugetlb_key = ERR_PTR(-ENOMEM);
				} else {
					hugetlb_key = &htlbpagek[i];
					mark_key_busy(hugetlb_key);
					hugetlb_key->key = key;
					INIT_RADIX_TREE(&hugetlb_key->tree, GFP_ATOMIC);
					hugetlb_key->uid = current->fsuid;
					hugetlb_key->gid = current->fsgid;
					hugetlb_key->mode = prot;
					hugetlb_key->size = len;
					atomic_set(&hugetlb_key->count, 1);
					*new = 1;
				}
			}
		} else if (key_busy(hugetlb_key)) {
			hugetlb_key = ERR_PTR(-EAGAIN);
			spin_unlock(&htlbpage_lock);
		} else if (check_size_prot(hugetlb_key, len, prot, flag) < 0) {
			hugetlb_key->key = 0;
			hugetlb_key = ERR_PTR(-EINVAL);
		} else
			*new = 0;
	} while (hugetlb_key == ERR_PTR(-EAGAIN));
	return hugetlb_key;
}

void hugetlb_release_key(struct hugetlb_key *key)
{
	unsigned long index;

	if (!atomic_dec_and_lock(&key->count, &htlbpage_lock))
		return;	

	for (index = 0; index < key->size; ++index) {
		struct page *page = radix_tree_lookup(&key->tree, index);
		if (!page)
			continue;
		huge_page_release(page);
	}
	key->key = 0;
	INIT_RADIX_TREE(&key->tree, GFP_ATOMIC);
	spin_unlock(&htlbpage_lock);
}

static struct page *alloc_hugetlb_page(void)
{
	int i;
	struct page *page;

	spin_lock(&htlbpage_lock);
	if (list_empty(&htlbpage_freelist)) {
		spin_unlock(&htlbpage_lock);
		return NULL;
	}

	page = list_entry(htlbpage_freelist.next, struct page, list);
	list_del(&page->list);
	htlbpagemem--;
	spin_unlock(&htlbpage_lock);
	set_page_count(page, 1);
	for (i = 0; i < (HPAGE_SIZE/PAGE_SIZE); ++i)
		clear_highpage(&page[i]);
	return page;
}

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

#define mk_pte_huge(entry) {entry.pte_low |= (_PAGE_PRESENT | _PAGE_PSE);}

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

static int anon_get_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma, int write_access, pte_t *page_table)
{
	struct page *page = alloc_hugetlb_page();
	if (page)
		set_huge_pte(mm, vma, page, page_table, write_access);
	return page ? 1 : -1;
}

static int make_hugetlb_pages_present(unsigned long addr, unsigned long end, int flags)
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
			if (anon_get_hugetlb_page(mm, vma,
					   write ? VM_WRITE : VM_READ,
					   pte) == -1)
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
out_error:		/* Error case, remove the partial lp_resources. */
	if (addr > vma->vm_start) {
		vma->vm_end = addr;
		zap_hugepage_range(vma, vma->vm_start, vma->vm_end - vma->vm_start);
		vma->vm_end = end;
	}
	spin_unlock(&mm->page_table_lock);
      out_error1:
	return -1;
}

int
copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
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
		pstart = start;
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
		if (((start & HPAGE_MASK) == pstart) && len &&
				(start < vma->vm_end))
			goto back1;
	} while (len && start < vma->vm_end);
	*length = len;
	*st = start;
	return i;
}

void free_huge_page(struct page *page)
{
	BUG_ON(page_count(page));
	BUG_ON(page->mapping);

	INIT_LIST_HEAD(&page->list);

	spin_lock(&htlbpage_lock);
	list_add(&page->list, &htlbpage_freelist);
	htlbpagemem++;
	spin_unlock(&htlbpage_lock);
}

void huge_page_release(struct page *page)
{
	if (!put_page_testzero(page))
		return;

	free_huge_page(page);
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

static int check_size_prot(struct hugetlb_key *key, unsigned long len, int prot, int flag)
{
	if (key->uid != current->fsuid)
		return -1;
	if (key->gid != current->fsgid)
		return -1;
	if (key->size != len)
		return -1;
	return 0;
}

struct page *key_find_page(struct hugetlb_key *key, unsigned long index)
{
	struct page *page = radix_tree_lookup(&key->tree, index);
	if (page)
		get_page(page);
	return page;
}

int key_add_page(struct page *page, struct hugetlb_key *key, unsigned long index)
{
	int error = radix_tree_insert(&key->tree, index, page);
	if (!error)
		get_page(page);
	return error;
}

static int prefault_key(struct hugetlb_key *key, struct vm_area_struct *vma)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;

	BUG_ON(vma->vm_start & ~HPAGE_MASK);
	BUG_ON(vma->vm_end & ~HPAGE_MASK);

	spin_lock(&mm->page_table_lock);
	spin_lock(&key->lock);
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
		page = key_find_page(key, idx);
		if (!page) {
			page = alloc_hugetlb_page();
			if (!page) {
				spin_unlock(&key->lock);
				ret = -ENOMEM;
				goto out;
			}
			key_add_page(page, key, idx);
			unlock_page(page);
		}
		set_huge_pte(mm, vma, page, pte, vma->vm_flags & VM_WRITE);
	}
	spin_unlock(&key->lock);
out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

static int alloc_shared_hugetlb_pages(int key, unsigned long addr, unsigned long len, int prot, int flag)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct hugetlb_key *hugetlb_key;
	int retval = -ENOMEM;
	int newalloc = 0;

	hugetlb_key = alloc_key(key, len, prot, flag, &newalloc);
	if (IS_ERR(hugetlb_key)) {
		spin_unlock(&htlbpage_lock);
		return PTR_ERR(hugetlb_key);
	} else
		spin_unlock(&htlbpage_lock);

	addr = do_mmap_pgoff(NULL, addr, len, (unsigned long) prot,
			MAP_NORESERVE|MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0);
	if (IS_ERR((void *) addr))
		goto out_release;

	vma = find_vma(mm, addr);
	if (!vma) {
		retval = -EINVAL;
		goto out_release;
	}

	retval = prefault_key(hugetlb_key, vma);
	if (retval)
		goto out;

	vma->vm_flags |= (VM_HUGETLB | VM_RESERVED);
	vma->vm_ops = &hugetlb_vm_ops;
	vma->vm_private_data = hugetlb_key;
	spin_lock(&htlbpage_lock);
	clear_key_busy(hugetlb_key);
	spin_unlock(&htlbpage_lock);
	return retval;
out:
	if (addr > vma->vm_start) {
		unsigned long raddr;
		raddr = vma->vm_end;
		vma->vm_end = addr;
		zap_hugepage_range(vma, vma->vm_start, vma->vm_end - vma->vm_start);
		vma->vm_end = raddr;
	}
	spin_lock(&mm->page_table_lock);
	do_munmap(mm, vma->vm_start, len);
	spin_unlock(&mm->page_table_lock);
out_release:
	hugetlb_release_key(hugetlb_key);
	return retval;
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
			page = alloc_hugetlb_page();
			if (!page) {
				ret = -ENOMEM;
				goto out;
			}
			add_to_page_cache(page, mapping, idx);
			unlock_page(page);
		}
		set_huge_pte(mm, vma, page, pte, vma->vm_flags & VM_WRITE);
	}
out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

static int alloc_private_hugetlb_pages(int key, unsigned long addr, unsigned long len, int prot, int flag)
{
	if (!capable(CAP_SYS_ADMIN)) {
		if (!in_group_p(0))
			return -EPERM;
	}
	addr = do_mmap_pgoff(NULL, addr, len, prot,
			MAP_NORESERVE|MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, 0);
	if (IS_ERR((void *) addr))
		return -ENOMEM;
	if (make_hugetlb_pages_present(addr, (addr + len), flag) < 0) {
		do_munmap(current->mm, addr, len);
		return -ENOMEM;
	}
	return 0;
}

int alloc_hugetlb_pages(int key, unsigned long addr, unsigned long len, int prot, int flag)
{
	if (key > 0)
		return alloc_shared_hugetlb_pages(key, addr, len, prot, flag);
	return alloc_private_hugetlb_pages(key, addr, len, prot, flag);
}

int set_hugetlb_mem_size(int count)
{
	int j, lcount;
	struct page *page, *map;
	extern long htlbzone_pages;
	extern struct list_head htlbpage_freelist;

	if (count < 0)
		lcount = count;
	else
		lcount = count - htlbzone_pages;

	if (lcount > 0) {	/* Increase the mem size. */
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
	/* Shrink the memory size. */
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
			set_page_count(map, 0);
			map++;
		}
		set_page_count(page, 1);
		__free_pages(page, HUGETLB_PAGE_ORDER);
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

static int __init hugetlb_init(void)
{
	int i, j;
	struct page *page;

	for (i = 0; i < htlbpage_max; ++i) {
		page = alloc_pages(__GFP_HIGHMEM, HUGETLB_PAGE_ORDER);
		if (!page)
			break;
		for (j = 0; j < HPAGE_SIZE/PAGE_SIZE; ++j)
			SetPageReserved(&page[j]);
		spin_lock(&htlbpage_lock);
		list_add(&page->list, &htlbpage_freelist);
		spin_unlock(&htlbpage_lock);
	}
	htlbpage_max = htlbpagemem = htlbzone_pages = i;
	printk("Total HugeTLB memory allocated, %ld\n", htlbpagemem);
	for (i = 0; i < MAX_ID; ++i) {
		atomic_set(&htlbpagek[i].count, 0);
		spin_lock_init(&htlbpagek[i].lock);
	}
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

static struct page * hugetlb_nopage(struct vm_area_struct * area, unsigned long address, int unused)
{
	BUG();
	return NULL;
}

struct vm_operations_struct hugetlb_vm_ops = {
	.nopage = hugetlb_nopage,
};
