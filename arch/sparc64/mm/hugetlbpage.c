/*
 * SPARC64 Huge TLB page support.
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
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
#include <asm/cacheflush.h>

static struct vm_operations_struct hugetlb_vm_ops;
struct list_head htlbpage_freelist;
spinlock_t htlbpage_lock = SPIN_LOCK_UNLOCKED;
extern long htlbpagemem;

static void zap_hugetlb_resources(struct vm_area_struct *);
void free_huge_page(struct page *page);

#define MAX_ID 	32
struct htlbpagekey {
	struct inode *in;
	int key;
} htlbpagek[MAX_ID];

static struct inode *find_key_inode(int key)
{
	int i;

	for (i = 0; i < MAX_ID; i++) {
		if (htlbpagek[i].key == key)
			return htlbpagek[i].in;
	}
	return NULL;
}

static struct page *alloc_hugetlb_page(void)
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
	page->lru.prev = (void *)free_huge_page;
	memset(page_address(page), 0, HPAGE_SIZE);

	return page;
}

static void free_hugetlb_page(struct page *page)
{
	spin_lock(&htlbpage_lock);
	if ((page->mapping != NULL) && (page_count(page) == 2)) {
		struct inode *inode = page->mapping->host;
		int i;

		ClearPageDirty(page);
		remove_from_page_cache(page);
		set_page_count(page, 1);
		if ((inode->i_size -= HPAGE_SIZE) == 0) {
			for (i = 0; i < MAX_ID; i++) {
				if (htlbpagek[i].key == inode->i_ino) {
					htlbpagek[i].key = 0;
					htlbpagek[i].in = NULL;
					break;
				}
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

static pte_t *huge_pte_alloc_map(struct mm_struct *mm, unsigned long addr)
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

static pte_t *huge_pte_offset_map(struct mm_struct *mm, unsigned long addr)
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

static pte_t *huge_pte_offset_map_nested(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd) {
		pmd = pmd_offset(pgd, addr);
		if (pmd)
			pte = pte_offset_map_nested(pmd, addr);
	}
	return pte;
}

#define mk_pte_huge(entry) do { pte_val(entry) |= _PAGE_SZ4MB; } while (0)

static void set_huge_pte(struct mm_struct *mm, struct vm_area_struct *vma,
			 struct page *page, pte_t * page_table, int write_access)
{
	pte_t entry;
	unsigned long i;

	mm->rss += (HPAGE_SIZE / PAGE_SIZE);

	for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
		if (write_access)
			entry = pte_mkwrite(pte_mkdirty(mk_pte(page,
							       vma->vm_page_prot)));
		else
			entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));
		entry = pte_mkyoung(entry);
		mk_pte_huge(entry);
		pte_val(entry) += (i << PAGE_SHIFT);
		set_pte(page_table, entry);
		page_table++;
	}
}

static int anon_get_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
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
make_hugetlb_pages_present(unsigned long addr, unsigned long end, int flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	pte_t *pte;
	int write;

	vma = find_vma(mm, addr);
	if (!vma)
		goto out_error1;

	write = (vma->vm_flags & VM_WRITE) != 0;
	if ((vma->vm_end - vma->vm_start) & (HPAGE_SIZE - 1))
		goto out_error1;

	spin_lock(&mm->page_table_lock);
	do {
		int err;

		pte = huge_pte_alloc_map(mm, addr);
		err = (!pte ||
		       !pte_none(*pte) ||
		       (anon_get_hugetlb_page(mm, vma,
					      write ? VM_WRITE : VM_READ,
					      pte) == -1));
		if (pte)
			pte_unmap(pte);
		if (err)
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
		flush_cache_range(vma, vma->vm_start, vma->vm_end);
		zap_hugetlb_resources(vma);
		flush_tlb_range(vma, vma->vm_start, vma->vm_end);
		vma->vm_end = end;
	}
	spin_unlock(&mm->page_table_lock);
 out_error1:
	return -1;
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
		unsigned long i;

		dst_pte = huge_pte_alloc_map(dst, addr);
		if (!dst_pte)
			goto nomem;

		src_pte = huge_pte_offset_map_nested(src, addr);
		entry = *src_pte;
		pte_unmap_nested(src_pte);

		ptepage = pte_page(entry);
		get_page(ptepage);
		for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
			set_pte(dst_pte, entry);
			pte_val(entry) += PAGE_SIZE;
			dst_pte++;
		}
		pte_unmap(dst_pte - (1 << HUGETLB_PAGE_ORDER));

		dst->rss += (HPAGE_SIZE / PAGE_SIZE);
		addr += HPAGE_SIZE;
	}
	return 0;

nomem:
	return -ENOMEM;
}

int follow_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
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
		ptep = huge_pte_offset_map(mm, start);
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

		pte_unmap(ptep);
	} while (len && start < vma->vm_end);

	*length = len;
	*st = start;
	return i;
}

static void zap_hugetlb_resources(struct vm_area_struct *mpnt)
{
	struct mm_struct *mm = mpnt->vm_mm;
	unsigned long len, addr, end;
	struct page *page;
	pte_t *ptep;

	addr = mpnt->vm_start;
	end = mpnt->vm_end;
	len = end - addr;
	do {
		unsigned long i;

		ptep = huge_pte_offset_map(mm, addr);
		page = pte_page(*ptep);
		for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
			pte_clear(ptep);
			ptep++;
		}
		pte_unmap(ptep - (1 << HUGETLB_PAGE_ORDER));
		free_hugetlb_page(page);
		addr += HPAGE_SIZE;
	} while (addr < end);
	mm->rss -= (len >> PAGE_SHIFT);
	mpnt->vm_ops = NULL;
}

static void unlink_vma(struct vm_area_struct *mpnt)
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

int free_hugepages(struct vm_area_struct *mpnt)
{
	unlink_vma(mpnt);

	flush_cache_range(mpnt, mpnt->vm_start, mpnt->vm_end);
	zap_hugetlb_resources(mpnt);
	flush_tlb_range(mpnt, mpnt->vm_start, mpnt->vm_end);

	kmem_cache_free(vm_area_cachep, mpnt);
	return 1;
}

static struct inode *set_new_inode(unsigned long len, int prot, int flag, int key)
{
	struct inode *inode;
	int i;

	for (i = 0; i < MAX_ID; i++) {
		if (htlbpagek[i].key == 0)
			break;
	}
	if (i == MAX_ID)
		return NULL;
	inode = kmalloc(sizeof (struct inode), GFP_KERNEL);
	if (inode == NULL)
		return NULL;

	inode_init_once(inode);
	atomic_inc(&inode->i_writecount);
	inode->i_mapping = &inode->i_data;
	inode->i_mapping->host = inode;
	inode->i_ino = (unsigned long)key;

	htlbpagek[i].key = key;
	htlbpagek[i].in = inode;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_mode = prot;
	inode->i_size = len;
	return inode;
}

static int check_size_prot(struct inode *inode, unsigned long len, int prot, int flag)
{
	if (inode->i_uid != current->fsuid)
		return -1;
	if (inode->i_gid != current->fsgid)
		return -1;
	if (inode->i_size != len)
		return -1;
	return 0;
}

static int alloc_shared_hugetlb_pages(int key, unsigned long addr, unsigned long len,
				      int prot, int flag)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct inode *inode;
	struct address_space *mapping;
	struct page *page;
	int idx;
	int retval = -ENOMEM;
	int newalloc = 0;

try_again:
	spin_lock(&htlbpage_lock);

	inode = find_key_inode(key);
	if (inode == NULL) {
		if (!capable(CAP_SYS_ADMIN)) {
			if (!in_group_p(0)) {
				retval = -EPERM;
				goto out_err;
			}
		}
		if (!(flag & IPC_CREAT)) {
			retval = -ENOENT;
			goto out_err;
		}
		inode = set_new_inode(len, prot, flag, key);
		if (inode == NULL)
			goto out_err;
		newalloc = 1;
	} else {
		if (check_size_prot(inode, len, prot, flag) < 0) {
			retval = -EINVAL;
			goto out_err;
		} else if (atomic_read(&inode->i_writecount)) {
			spin_unlock(&htlbpage_lock);
			goto try_again;
		}
	}
	spin_unlock(&htlbpage_lock);
	mapping = inode->i_mapping;

	addr = do_mmap_pgoff(NULL, addr, len, (unsigned long) prot,
			     MAP_NORESERVE|MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0);
	if (IS_ERR((void *) addr))
		goto freeinode;

	vma = find_vma(mm, addr);
	if (!vma) {
		retval = -EINVAL;
		goto freeinode;
	}

	spin_lock(&mm->page_table_lock);
	do {
		pte_t *pte = huge_pte_alloc_map(mm, addr);

		if (!pte || !pte_none(pte)) {
			if (pte)
				pte_unmap(pte);
			goto out;
		}

		idx = (addr - vma->vm_start) >> HPAGE_SHIFT;
		page = find_get_page(mapping, idx);
		if (page == NULL) {
			page = alloc_hugetlb_page();
			if (page == NULL) {
				pte_unmap(pte);
				retval = -ENOMEM;
				goto out;
			}
			retval = add_to_page_cache(page, mapping,
						idx, GFP_ATOMIC);
			if (retval) {
				pte_unmap(pte);
				free_hugetlb_page(page);
				goto out;
			}
		}
		set_huge_pte(mm, vma, page, pte,
			     (vma->vm_flags & VM_WRITE));
		pte_unmap(pte);

		addr += HPAGE_SIZE;
	} while (addr < vma->vm_end);

	retval = 0;
	vma->vm_flags |= (VM_HUGETLB | VM_RESERVED);
	vma->vm_ops = &hugetlb_vm_ops;
	spin_unlock(&mm->page_table_lock);
	spin_lock(&htlbpage_lock);
	atomic_set(&inode->i_writecount, 0);
	spin_unlock(&htlbpage_lock);

	return retval;

out:
	if (addr > vma->vm_start) {
		unsigned long raddr;
		raddr = vma->vm_end;
		vma->vm_end = addr;

		flush_cache_range(vma, vma->vm_start, vma->vm_end);
		zap_hugetlb_resources(vma);
		flush_tlb_range(vma, vma->vm_start, vma->vm_end);

		vma->vm_end = raddr;
	}
	spin_unlock(&mm->page_table_lock);
	do_munmap(mm, vma->vm_start, len);
	if (newalloc)
		goto freeinode;

	return retval;

out_err:
	spin_unlock(&htlbpage_lock);

freeinode:
	if (newalloc) {
		for (idx = 0; idx < MAX_ID; idx++) {
			if (htlbpagek[idx].key == inode->i_ino) {
				htlbpagek[idx].key = 0;
				htlbpagek[idx].in = NULL;
				break;
			}
		}
		kfree(inode);
	}
	return retval;
}

static int alloc_private_hugetlb_pages(int key, unsigned long addr, unsigned long len,
				       int prot, int flag)
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

int alloc_hugetlb_pages(int key, unsigned long addr, unsigned long len, int prot,
		    int flag)
{
	if (key > 0)
		return alloc_shared_hugetlb_pages(key, addr, len, prot, flag);
	return alloc_private_hugetlb_pages(key, addr, len, prot, flag);
}

extern long htlbzone_pages;
extern struct list_head htlbpage_freelist;

int set_hugetlb_mem_size(int count)
{
	int j, lcount;
	struct page *page, *map;

	if (count < 0)
		lcount = count;
	else
		lcount = count - htlbzone_pages;

	if (lcount > 0) {	/* Increase the mem size. */
		while (lcount--) {
			page = alloc_pages(GFP_ATOMIC, HUGETLB_PAGE_ORDER);
			if (page == NULL)
				break;
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
			map->flags &= ~(1UL << PG_locked | 1UL << PG_error |
					1UL << PG_referenced |
					1UL << PG_dirty | 1UL << PG_active |
					1UL << PG_private | 1UL << PG_writeback);
			set_page_count(page, 0);
			map++;
		}
		set_page_count(page, 1);
		__free_pages(page, HUGETLB_PAGE_ORDER);
	}
	return (int) htlbzone_pages;
}

static struct page *
hugetlb_nopage(struct vm_area_struct *vma, unsigned long address, int unused)
{
	BUG();
	return NULL;
}

static struct vm_operations_struct hugetlb_vm_ops = {
	.nopage = hugetlb_nopage,
	.close	= zap_hugetlb_resources,
};
