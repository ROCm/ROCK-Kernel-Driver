/*
 *  linux/mm/memory.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

/*
 * 05.04.94  -  Multi-page memory management added for v1.1.
 * 		Idea by Alex Bligh (alex@cconcepts.co.uk)
 *
 * 16.07.99  -  Support of BIGMEM added by Gerhard Wichert, Siemens AG
 *		(Gerhard.Wichert@pdb.siemens.de)
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/swapctl.h>
#include <linux/iobuf.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>


unsigned long max_mapnr;
unsigned long num_physpages;
void * high_memory;
struct page *highmem_start_page;

/*
 * We special-case the C-O-W ZERO_PAGE, because it's such
 * a common occurrence (no need to read the page to know
 * that it's zero - better for the cache and memory subsystem).
 */
static inline void copy_cow_page(struct page * from, struct page * to, unsigned long address)
{
	if (from == ZERO_PAGE(address)) {
		clear_user_highpage(to, address);
		return;
	}
	copy_user_highpage(to, from, address);
}

mem_map_t * mem_map;

/*
 * Note: this doesn't free the actual pages themselves. That
 * has been handled earlier when unmapping all the memory regions.
 */
static inline void free_one_pmd(pmd_t * dir)
{
	pte_t * pte;

	if (pmd_none(*dir))
		return;
	if (pmd_bad(*dir)) {
		pmd_ERROR(*dir);
		pmd_clear(dir);
		return;
	}
	pte = pte_offset(dir, 0);
	pmd_clear(dir);
	pte_free(pte);
}

static inline void free_one_pgd(pgd_t * dir)
{
	int j;
	pmd_t * pmd;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, 0);
	pgd_clear(dir);
	for (j = 0; j < PTRS_PER_PMD ; j++)
		free_one_pmd(pmd+j);
	pmd_free(pmd);
}

/* Low and high watermarks for page table cache.
   The system should try to have pgt_water[0] <= cache elements <= pgt_water[1]
 */
int pgt_cache_water[2] = { 25, 50 };

/* Returns the number of pages freed */
int check_pgt_cache(void)
{
	return do_check_pgt_cache(pgt_cache_water[0], pgt_cache_water[1]);
}


/*
 * This function clears all user-level page tables of a process - this
 * is needed by execve(), so that old pages aren't in the way.
 */
void clear_page_tables(struct mm_struct *mm, unsigned long first, int nr)
{
	pgd_t * page_dir = mm->pgd;

	page_dir += first;
	do {
		free_one_pgd(page_dir);
		page_dir++;
	} while (--nr);

	/* keep the page table cache within bounds */
	check_pgt_cache();
}

#define PTE_TABLE_MASK	((PTRS_PER_PTE-1) * sizeof(pte_t))
#define PMD_TABLE_MASK	((PTRS_PER_PMD-1) * sizeof(pmd_t))

/*
 * copy one vm_area from one task to the other. Assumes the page tables
 * already present in the new task to be cleared in the whole range
 * covered by this vma.
 *
 * 08Jan98 Merged into one routine from several inline routines to reduce
 *         variable count and make things faster. -jj
 */
int copy_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	pgd_t * src_pgd, * dst_pgd;
	unsigned long address = vma->vm_start;
	unsigned long end = vma->vm_end;
	unsigned long cow = (vma->vm_flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;

	src_pgd = pgd_offset(src, address)-1;
	dst_pgd = pgd_offset(dst, address)-1;
	
	for (;;) {
		pmd_t * src_pmd, * dst_pmd;

		src_pgd++; dst_pgd++;
		
		/* copy_pmd_range */
		
		if (pgd_none(*src_pgd))
			goto skip_copy_pmd_range;
		if (pgd_bad(*src_pgd)) {
			pgd_ERROR(*src_pgd);
			pgd_clear(src_pgd);
skip_copy_pmd_range:	address = (address + PGDIR_SIZE) & PGDIR_MASK;
			if (!address || (address >= end))
				goto out;
			continue;
		}
		if (pgd_none(*dst_pgd)) {
			if (!pmd_alloc(dst_pgd, 0))
				goto nomem;
		}
		
		src_pmd = pmd_offset(src_pgd, address);
		dst_pmd = pmd_offset(dst_pgd, address);

		do {
			pte_t * src_pte, * dst_pte;
		
			/* copy_pte_range */
		
			if (pmd_none(*src_pmd))
				goto skip_copy_pte_range;
			if (pmd_bad(*src_pmd)) {
				pmd_ERROR(*src_pmd);
				pmd_clear(src_pmd);
skip_copy_pte_range:		address = (address + PMD_SIZE) & PMD_MASK;
				if (address >= end)
					goto out;
				goto cont_copy_pmd_range;
			}
			if (pmd_none(*dst_pmd)) {
				if (!pte_alloc(dst_pmd, 0))
					goto nomem;
			}
			
			src_pte = pte_offset(src_pmd, address);
			dst_pte = pte_offset(dst_pmd, address);
			
			do {
				pte_t pte = *src_pte;
				struct page *ptepage;
				
				/* copy_one_pte */

				if (pte_none(pte))
					goto cont_copy_pte_range_noset;
				if (!pte_present(pte)) {
					swap_duplicate(pte_to_swp_entry(pte));
					goto cont_copy_pte_range;
				}
				ptepage = pte_page(pte);
				if ((!VALID_PAGE(ptepage)) || 
				    PageReserved(ptepage))
					goto cont_copy_pte_range;

				/* If it's a COW mapping, write protect it both in the parent and the child */
				if (cow) {
					ptep_set_wrprotect(src_pte);
					pte = *src_pte;
				}

				/* If it's a shared mapping, mark it clean in the child */
				if (vma->vm_flags & VM_SHARED)
					pte = pte_mkclean(pte);
				pte = pte_mkold(pte);
				get_page(ptepage);

cont_copy_pte_range:		set_pte(dst_pte, pte);
cont_copy_pte_range_noset:	address += PAGE_SIZE;
				if (address >= end)
					goto out;
				src_pte++;
				dst_pte++;
			} while ((unsigned long)src_pte & PTE_TABLE_MASK);
		
cont_copy_pmd_range:	src_pmd++;
			dst_pmd++;
		} while ((unsigned long)src_pmd & PMD_TABLE_MASK);
	}
out:
	return 0;

nomem:
	return -ENOMEM;
}

/*
 * Return indicates whether a page was freed so caller can adjust rss
 */
static inline int free_pte(pte_t pte)
{
	if (pte_present(pte)) {
		struct page *page = pte_page(pte);
		if ((!VALID_PAGE(page)) || PageReserved(page))
			return 0;
		/* 
		 * free_page() used to be able to clear swap cache
		 * entries.  We may now have to do it manually.  
		 */
		if (pte_dirty(pte) && page->mapping)
			set_page_dirty(page);
		free_page_and_swap_cache(page);
		return 1;
	}
	swap_free(pte_to_swp_entry(pte));
	return 0;
}

static inline void forget_pte(pte_t page)
{
	if (!pte_none(page)) {
		printk("forget_pte: old mapping existed!\n");
		free_pte(page);
	}
}

static inline int zap_pte_range(struct mm_struct *mm, pmd_t * pmd, unsigned long address, unsigned long size)
{
	pte_t * pte;
	int freed;

	if (pmd_none(*pmd))
		return 0;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return 0;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	if (address + size > PMD_SIZE)
		size = PMD_SIZE - address;
	size >>= PAGE_SHIFT;
	freed = 0;
	for (;;) {
		pte_t page;
		if (!size)
			break;
		page = ptep_get_and_clear(pte);
		pte++;
		size--;
		if (pte_none(page))
			continue;
		freed += free_pte(page);
	}
	return freed;
}

static inline int zap_pmd_range(struct mm_struct *mm, pgd_t * dir, unsigned long address, unsigned long size)
{
	pmd_t * pmd;
	unsigned long end;
	int freed;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return 0;
	}
	pmd = pmd_offset(dir, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	freed = 0;
	do {
		freed += zap_pte_range(mm, pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK; 
		pmd++;
	} while (address < end);
	return freed;
}

/*
 * remove user pages in a given range.
 */
void zap_page_range(struct mm_struct *mm, unsigned long address, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;
	int freed = 0;

	dir = pgd_offset(mm, address);

	/*
	 * This is a long-lived spinlock. That's fine.
	 * There's no contention, because the page table
	 * lock only protects against kswapd anyway, and
	 * even if kswapd happened to be looking at this
	 * process we _want_ it to get stuck.
	 */
	if (address >= end)
		BUG();
	spin_lock(&mm->page_table_lock);
	do {
		freed += zap_pmd_range(mm, dir, address, end - address);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	spin_unlock(&mm->page_table_lock);
	/*
	 * Update rss for the mm_struct (not necessarily current->mm)
	 * Notice that rss is an unsigned long.
	 */
	if (mm->rss > freed)
		mm->rss -= freed;
	else
		mm->rss = 0;
}


/*
 * Do a quick page-table lookup for a single page. 
 */
static struct page * follow_page(unsigned long address) 
{
	pgd_t *pgd;
	pmd_t *pmd;

	pgd = pgd_offset(current->mm, address);
	pmd = pmd_offset(pgd, address);
	if (pmd) {
		pte_t * pte = pte_offset(pmd, address);
		if (pte && pte_present(*pte))
			return pte_page(*pte);
	}
	
	return NULL;
}

/* 
 * Given a physical address, is there a useful struct page pointing to
 * it?  This may become more complex in the future if we start dealing
 * with IO-aperture pages in kiobufs.
 */

static inline struct page * get_page_map(struct page *page)
{
	if (!VALID_PAGE(page))
		return 0;
	return page;
}

/*
 * Force in an entire range of pages from the current process's user VA,
 * and pin them in physical memory.  
 */

#define dprintk(x...)
int map_user_kiobuf(int rw, struct kiobuf *iobuf, unsigned long va, size_t len)
{
	unsigned long		ptr, end;
	int			err;
	struct mm_struct *	mm;
	struct vm_area_struct *	vma = 0;
	struct page *		map;
	int			i;
	int			datain = (rw == READ);
	
	/* Make sure the iobuf is not already mapped somewhere. */
	if (iobuf->nr_pages)
		return -EINVAL;

	mm = current->mm;
	dprintk ("map_user_kiobuf: begin\n");
	
	ptr = va & PAGE_MASK;
	end = (va + len + PAGE_SIZE - 1) & PAGE_MASK;
	err = expand_kiobuf(iobuf, (end - ptr) >> PAGE_SHIFT);
	if (err)
		return err;

	down(&mm->mmap_sem);

	err = -EFAULT;
	iobuf->locked = 0;
	iobuf->offset = va & ~PAGE_MASK;
	iobuf->length = len;
	
	i = 0;
	
	/* 
	 * First of all, try to fault in all of the necessary pages
	 */
	while (ptr < end) {
		if (!vma || ptr >= vma->vm_end) {
			vma = find_vma(current->mm, ptr);
			if (!vma) 
				goto out_unlock;
			if (vma->vm_start > ptr) {
				if (!(vma->vm_flags & VM_GROWSDOWN))
					goto out_unlock;
				if (expand_stack(vma, ptr))
					goto out_unlock;
			}
			if (((datain) && (!(vma->vm_flags & VM_WRITE))) ||
					(!(vma->vm_flags & VM_READ))) {
				err = -EACCES;
				goto out_unlock;
			}
		}
		if (handle_mm_fault(current->mm, vma, ptr, datain) <= 0) 
			goto out_unlock;
		spin_lock(&mm->page_table_lock);
		map = follow_page(ptr);
		if (!map) {
			spin_unlock(&mm->page_table_lock);
			dprintk (KERN_ERR "Missing page in map_user_kiobuf\n");
			goto out_unlock;
		}
		map = get_page_map(map);
		if (map) {
			flush_dcache_page(map);
			atomic_inc(&map->count);
		} else
			printk (KERN_INFO "Mapped page missing [%d]\n", i);
		spin_unlock(&mm->page_table_lock);
		iobuf->maplist[i] = map;
		iobuf->nr_pages = ++i;
		
		ptr += PAGE_SIZE;
	}

	up(&mm->mmap_sem);
	dprintk ("map_user_kiobuf: end OK\n");
	return 0;

 out_unlock:
	up(&mm->mmap_sem);
	unmap_kiobuf(iobuf);
	dprintk ("map_user_kiobuf: end %d\n", err);
	return err;
}


/*
 * Unmap all of the pages referenced by a kiobuf.  We release the pages,
 * and unlock them if they were locked. 
 */

void unmap_kiobuf (struct kiobuf *iobuf) 
{
	int i;
	struct page *map;
	
	for (i = 0; i < iobuf->nr_pages; i++) {
		map = iobuf->maplist[i];
		if (map) {
			if (iobuf->locked)
				UnlockPage(map);
			__free_page(map);
		}
	}
	
	iobuf->nr_pages = 0;
	iobuf->locked = 0;
}


/*
 * Lock down all of the pages of a kiovec for IO.
 *
 * If any page is mapped twice in the kiovec, we return the error -EINVAL.
 *
 * The optional wait parameter causes the lock call to block until all
 * pages can be locked if set.  If wait==0, the lock operation is
 * aborted if any locked pages are found and -EAGAIN is returned.
 */

int lock_kiovec(int nr, struct kiobuf *iovec[], int wait)
{
	struct kiobuf *iobuf;
	int i, j;
	struct page *page, **ppage;
	int doublepage = 0;
	int repeat = 0;
	
 repeat:
	
	for (i = 0; i < nr; i++) {
		iobuf = iovec[i];

		if (iobuf->locked)
			continue;
		iobuf->locked = 1;

		ppage = iobuf->maplist;
		for (j = 0; j < iobuf->nr_pages; ppage++, j++) {
			page = *ppage;
			if (!page)
				continue;
			
			if (TryLockPage(page))
				goto retry;
		}
	}

	return 0;
	
 retry:
	
	/* 
	 * We couldn't lock one of the pages.  Undo the locking so far,
	 * wait on the page we got to, and try again.  
	 */
	
	unlock_kiovec(nr, iovec);
	if (!wait)
		return -EAGAIN;
	
	/* 
	 * Did the release also unlock the page we got stuck on?
	 */
	if (!PageLocked(page)) {
		/* 
		 * If so, we may well have the page mapped twice
		 * in the IO address range.  Bad news.  Of
		 * course, it _might_ just be a coincidence,
		 * but if it happens more than once, chances
		 * are we have a double-mapped page. 
		 */
		if (++doublepage >= 3) 
			return -EINVAL;
		
		/* Try again...  */
		wait_on_page(page);
	}
	
	if (++repeat < 16)
		goto repeat;
	return -EAGAIN;
}

/*
 * Unlock all of the pages of a kiovec after IO.
 */

int unlock_kiovec(int nr, struct kiobuf *iovec[])
{
	struct kiobuf *iobuf;
	int i, j;
	struct page *page, **ppage;
	
	for (i = 0; i < nr; i++) {
		iobuf = iovec[i];

		if (!iobuf->locked)
			continue;
		iobuf->locked = 0;
		
		ppage = iobuf->maplist;
		for (j = 0; j < iobuf->nr_pages; ppage++, j++) {
			page = *ppage;
			if (!page)
				continue;
			UnlockPage(page);
		}
	}
	return 0;
}

static inline void zeromap_pte_range(pte_t * pte, unsigned long address,
                                     unsigned long size, pgprot_t prot)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t zero_pte = pte_wrprotect(mk_pte(ZERO_PAGE(address), prot));
		pte_t oldpage = ptep_get_and_clear(pte);
		set_pte(pte, zero_pte);
		forget_pte(oldpage);
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
}

static inline int zeromap_pmd_range(pmd_t * pmd, unsigned long address,
                                    unsigned long size, pgprot_t prot)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		pte_t * pte = pte_alloc(pmd, address);
		if (!pte)
			return -ENOMEM;
		zeromap_pte_range(pte, address, end - address, prot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

int zeromap_page_range(unsigned long address, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = address;
	unsigned long end = address + size;

	dir = pgd_offset(current->mm, address);
	flush_cache_range(current->mm, beg, end);
	if (address >= end)
		BUG();
	do {
		pmd_t *pmd = pmd_alloc(dir, address);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = zeromap_pmd_range(pmd, address, end - address, prot);
		if (error)
			break;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	flush_tlb_range(current->mm, beg, end);
	return error;
}

/*
 * maps a range of physical memory into the requested pages. the old
 * mappings are removed. any references to nonexistent pages results
 * in null mappings (currently treated as "copy-on-access")
 */
static inline void remap_pte_range(pte_t * pte, unsigned long address, unsigned long size,
	unsigned long phys_addr, pgprot_t prot)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		struct page *page;
		pte_t oldpage;
		oldpage = ptep_get_and_clear(pte);

		page = virt_to_page(__va(phys_addr));
		if ((!VALID_PAGE(page)) || PageReserved(page))
 			set_pte(pte, mk_pte_phys(phys_addr, prot));
		forget_pte(oldpage);
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
}

static inline int remap_pmd_range(pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long phys_addr, pgprot_t prot)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	do {
		pte_t * pte = pte_alloc(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_pte_range(pte, address, end - address, address + phys_addr, prot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

/*  Note: this is only safe if the mm semaphore is held when called. */
int remap_page_range(unsigned long from, unsigned long phys_addr, unsigned long size, pgprot_t prot)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = from;
	unsigned long end = from + size;

	phys_addr -= from;
	dir = pgd_offset(current->mm, from);
	flush_cache_range(current->mm, beg, end);
	if (from >= end)
		BUG();
	do {
		pmd_t *pmd = pmd_alloc(dir, from);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = remap_pmd_range(pmd, from, end - from, phys_addr + from, prot);
		if (error)
			break;
		from = (from + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (from && (from < end));
	flush_tlb_range(current->mm, beg, end);
	return error;
}

/*
 * Establish a new mapping:
 *  - flush the old one
 *  - update the page tables
 *  - inform the TLB about the new one
 */
static inline void establish_pte(struct vm_area_struct * vma, unsigned long address, pte_t *page_table, pte_t entry)
{
	set_pte(page_table, entry);
	flush_tlb_page(vma, address);
	update_mmu_cache(vma, address, entry);
}

static inline void break_cow(struct vm_area_struct * vma, struct page *	old_page, struct page * new_page, unsigned long address, 
		pte_t *page_table)
{
	copy_cow_page(old_page,new_page,address);
	flush_page_to_ram(new_page);
	flush_cache_page(vma, address);
	establish_pte(vma, address, page_table, pte_mkwrite(pte_mkdirty(mk_pte(new_page, vma->vm_page_prot))));
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * Goto-purists beware: the only reason for goto's here is that it results
 * in better assembly code.. The "default" path will see no jumps at all.
 *
 * Note that this routine assumes that the protection checks have been
 * done by the caller (the low-level page fault routine in most cases).
 * Thus we can safely just mark it writable once we've done any necessary
 * COW.
 *
 * We also mark the page dirty at this point even though the page will
 * change only once the write actually happens. This avoids a few races,
 * and potentially makes it more efficient.
 *
 * We enter with the page table read-lock held, and need to exit without
 * it.
 */
static int do_wp_page(struct mm_struct *mm, struct vm_area_struct * vma,
	unsigned long address, pte_t *page_table, pte_t pte)
{
	struct page *old_page, *new_page;

	old_page = pte_page(pte);
	if (!VALID_PAGE(old_page))
		goto bad_wp_page;
	
	/*
	 * We can avoid the copy if:
	 * - we're the only user (count == 1)
	 * - the only other user is the swap cache,
	 *   and the only swap cache user is itself,
	 *   in which case we can just continue to
	 *   use the same swap cache (it will be
	 *   marked dirty).
	 */
	switch (page_count(old_page)) {
	case 2:
		/*
		 * Lock the page so that no one can look it up from
		 * the swap cache, grab a reference and start using it.
		 * Can not do lock_page, holding page_table_lock.
		 */
		if (!PageSwapCache(old_page) || TryLockPage(old_page))
			break;
		if (is_page_shared(old_page)) {
			UnlockPage(old_page);
			break;
		}
		UnlockPage(old_page);
		/* FallThrough */
	case 1:
		flush_cache_page(vma, address);
		establish_pte(vma, address, page_table, pte_mkyoung(pte_mkdirty(pte_mkwrite(pte))));
		spin_unlock(&mm->page_table_lock);
		return 1;	/* Minor fault */
	}

	/*
	 * Ok, we need to copy. Oh, well..
	 */
	spin_unlock(&mm->page_table_lock);
	new_page = page_cache_alloc();
	if (!new_page)
		return -1;
	spin_lock(&mm->page_table_lock);

	/*
	 * Re-check the pte - we dropped the lock
	 */
	if (pte_same(*page_table, pte)) {
		if (PageReserved(old_page))
			++mm->rss;
		break_cow(vma, old_page, new_page, address, page_table);

		/* Free the old page.. */
		new_page = old_page;
	}
	spin_unlock(&mm->page_table_lock);
	page_cache_release(new_page);
	return 1;	/* Minor fault */

bad_wp_page:
	spin_unlock(&mm->page_table_lock);
	printk("do_wp_page: bogus page at address %08lx (page 0x%lx)\n",address,(unsigned long)old_page);
	return -1;
}

static void vmtruncate_list(struct vm_area_struct *mpnt,
			    unsigned long pgoff, unsigned long partial)
{
	do {
		struct mm_struct *mm = mpnt->vm_mm;
		unsigned long start = mpnt->vm_start;
		unsigned long end = mpnt->vm_end;
		unsigned long len = end - start;
		unsigned long diff;

		/* mapping wholly truncated? */
		if (mpnt->vm_pgoff >= pgoff) {
			flush_cache_range(mm, start, end);
			zap_page_range(mm, start, len);
			flush_tlb_range(mm, start, end);
			continue;
		}

		/* mapping wholly unaffected? */
		len = len >> PAGE_SHIFT;
		diff = pgoff - mpnt->vm_pgoff;
		if (diff >= len)
			continue;

		/* Ok, partially affected.. */
		start += diff << PAGE_SHIFT;
		len = (len - diff) << PAGE_SHIFT;
		flush_cache_range(mm, start, end);
		zap_page_range(mm, start, len);
		flush_tlb_range(mm, start, end);
	} while ((mpnt = mpnt->vm_next_share) != NULL);
}
			      

/*
 * Handle all mappings that got truncated by a "truncate()"
 * system call.
 *
 * NOTE! We have to be ready to update the memory sharing
 * between the file and the memory map for a potential last
 * incomplete page.  Ugly, but necessary.
 */
void vmtruncate(struct inode * inode, loff_t offset)
{
	unsigned long partial, pgoff;
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	if (inode->i_size < offset)
		goto do_expand;
	inode->i_size = offset;
	truncate_inode_pages(mapping, offset);
	spin_lock(&mapping->i_shared_lock);
	if (!mapping->i_mmap && !mapping->i_mmap_shared)
		goto out_unlock;

	pgoff = (offset + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	partial = (unsigned long)offset & (PAGE_CACHE_SIZE - 1);

	if (mapping->i_mmap != NULL)
		vmtruncate_list(mapping->i_mmap, pgoff, partial);
	if (mapping->i_mmap_shared != NULL)
		vmtruncate_list(mapping->i_mmap_shared, pgoff, partial);

out_unlock:
	spin_unlock(&mapping->i_shared_lock);
	/* this should go into ->truncate */
	inode->i_size = offset;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	return;

do_expand:
	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY) {
		if (inode->i_size >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (offset > limit) {
			send_sig(SIGXFSZ, current, 0);
			offset = limit;
		}
	}
	inode->i_size = offset;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
out:
	return;
}



/* 
 * Primitive swap readahead code. We simply read an aligned block of
 * (1 << page_cluster) entries in the swap area. This method is chosen
 * because it doesn't cost us any seek time.  We also make sure to queue
 * the 'original' request together with the readahead ones...  
 */
void swapin_readahead(swp_entry_t entry)
{
	int i, num;
	struct page *new_page;
	unsigned long offset;

	/*
	 * Get the number of handles we should do readahead io to. Also,
	 * grab temporary references on them, releasing them as io completes.
	 */
	num = valid_swaphandles(entry, &offset);
	for (i = 0; i < num; offset++, i++) {
		/* Don't block on I/O for read-ahead */
		if (atomic_read(&nr_async_pages) >= pager_daemon.swap_cluster
				* (1 << page_cluster)) {
			while (i++ < num)
				swap_free(SWP_ENTRY(SWP_TYPE(entry), offset++));
			break;
		}
		/* Ok, do the async read-ahead now */
		new_page = read_swap_cache_async(SWP_ENTRY(SWP_TYPE(entry), offset), 0);
		if (new_page != NULL)
			page_cache_release(new_page);
		swap_free(SWP_ENTRY(SWP_TYPE(entry), offset));
	}
	return;
}

static int do_swap_page(struct mm_struct * mm,
	struct vm_area_struct * vma, unsigned long address,
	pte_t * page_table, swp_entry_t entry, int write_access)
{
	struct page *page = lookup_swap_cache(entry);
	pte_t pte;

	if (!page) {
		lock_kernel();
		swapin_readahead(entry);
		page = read_swap_cache(entry);
		unlock_kernel();
		if (!page)
			return -1;

		flush_page_to_ram(page);
		flush_icache_page(vma, page);
	}

	mm->rss++;

	pte = mk_pte(page, vma->vm_page_prot);

	/*
	 * Freeze the "shared"ness of the page, ie page_count + swap_count.
	 * Must lock page before transferring our swap count to already
	 * obtained page count.
	 */
	lock_page(page);
	swap_free(entry);
	if (write_access && !is_page_shared(page))
		pte = pte_mkwrite(pte_mkdirty(pte));
	UnlockPage(page);

	set_pte(page_table, pte);
	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, pte);
	return 1;	/* Minor fault */
}

/*
 * This only needs the MM semaphore
 */
static int do_anonymous_page(struct mm_struct * mm, struct vm_area_struct * vma, pte_t *page_table, int write_access, unsigned long addr)
{
	struct page *page = NULL;
	pte_t entry = pte_wrprotect(mk_pte(ZERO_PAGE(addr), vma->vm_page_prot));
	if (write_access) {
		page = alloc_page(GFP_HIGHUSER);
		if (!page)
			return -1;
		clear_user_highpage(page, addr);
		entry = pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
		mm->rss++;
		flush_page_to_ram(page);
	}
	set_pte(page_table, entry);
	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, addr, entry);
	return 1;	/* Minor fault */
}

/*
 * do_no_page() tries to create a new page mapping. It aggressively
 * tries to share with existing pages, but makes a separate copy if
 * the "write_access" parameter is true in order to avoid the next
 * page fault.
 *
 * As this is called only for pages that do not currently exist, we
 * do not need to flush old virtual caches or the TLB.
 *
 * This is called with the MM semaphore held.
 */
static int do_no_page(struct mm_struct * mm, struct vm_area_struct * vma,
	unsigned long address, int write_access, pte_t *page_table)
{
	struct page * new_page;
	pte_t entry;

	if (!vma->vm_ops || !vma->vm_ops->nopage)
		return do_anonymous_page(mm, vma, page_table, write_access, address);

	/*
	 * The third argument is "no_share", which tells the low-level code
	 * to copy, not share the page even if sharing is possible.  It's
	 * essentially an early COW detection.
	 */
	new_page = vma->vm_ops->nopage(vma, address & PAGE_MASK, (vma->vm_flags & VM_SHARED)?0:write_access);
	if (new_page == NULL)	/* no page was available -- SIGBUS */
		return 0;
	if (new_page == NOPAGE_OOM)
		return -1;
	++mm->rss;
	/*
	 * This silly early PAGE_DIRTY setting removes a race
	 * due to the bad i386 page protection. But it's valid
	 * for other architectures too.
	 *
	 * Note that if write_access is true, we either now have
	 * an exclusive copy of the page, or this is a shared mapping,
	 * so we can make it writable and dirty to avoid having to
	 * handle that later.
	 */
	flush_page_to_ram(new_page);
	flush_icache_page(vma, new_page);
	entry = mk_pte(new_page, vma->vm_page_prot);
	if (write_access) {
		entry = pte_mkwrite(pte_mkdirty(entry));
	} else if (page_count(new_page) > 1 &&
		   !(vma->vm_flags & VM_SHARED))
		entry = pte_wrprotect(entry);
	set_pte(page_table, entry);
	/* no need to invalidate: a not-present page shouldn't be cached */
	update_mmu_cache(vma, address, entry);
	return 2;	/* Major fault */
}

/*
 * These routines also need to handle stuff like marking pages dirty
 * and/or accessed for architectures that don't do it in hardware (most
 * RISC architectures).  The early dirtying is also good on the i386.
 *
 * There is also a hook called "update_mmu_cache()" that architectures
 * with external mmu caches can use to update those (ie the Sparc or
 * PowerPC hashed page tables that act as extended TLBs).
 *
 * Note the "page_table_lock". It is to protect against kswapd removing
 * pages from under us. Note that kswapd only ever _removes_ pages, never
 * adds them. As such, once we have noticed that the page is not present,
 * we can drop the lock early.
 *
 * The adding of pages is protected by the MM semaphore (which we hold),
 * so we don't need to worry about a page being suddenly been added into
 * our VM.
 */
static inline int handle_pte_fault(struct mm_struct *mm,
	struct vm_area_struct * vma, unsigned long address,
	int write_access, pte_t * pte)
{
	pte_t entry;

	/*
	 * We need the page table lock to synchronize with kswapd
	 * and the SMP-safe atomic PTE updates.
	 */
	spin_lock(&mm->page_table_lock);
	entry = *pte;
	if (!pte_present(entry)) {
		/*
		 * If it truly wasn't present, we know that kswapd
		 * and the PTE updates will not touch it later. So
		 * drop the lock.
		 */
		spin_unlock(&mm->page_table_lock);
		if (pte_none(entry))
			return do_no_page(mm, vma, address, write_access, pte);
		return do_swap_page(mm, vma, address, pte, pte_to_swp_entry(entry), write_access);
	}

	if (write_access) {
		if (!pte_write(entry))
			return do_wp_page(mm, vma, address, pte, entry);

		entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	establish_pte(vma, address, pte, entry);
	spin_unlock(&mm->page_table_lock);
	return 1;
}

/*
 * By the time we get here, we already hold the mm semaphore
 */
int handle_mm_fault(struct mm_struct *mm, struct vm_area_struct * vma,
	unsigned long address, int write_access)
{
	int ret = -1;
	pgd_t *pgd;
	pmd_t *pmd;

	pgd = pgd_offset(mm, address);
	pmd = pmd_alloc(pgd, address);

	if (pmd) {
		pte_t * pte = pte_alloc(pmd, address);
		if (pte)
			ret = handle_pte_fault(mm, vma, address, write_access, pte);
	}
	return ret;
}

/*
 * Simplistic page force-in..
 */
int make_pages_present(unsigned long addr, unsigned long end)
{
	int write;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct * vma;

	vma = find_vma(mm, addr);
	write = (vma->vm_flags & VM_WRITE) != 0;
	if (addr >= end)
		BUG();
	do {
		if (handle_mm_fault(mm, vma, addr, write) < 0)
			return -1;
		addr += PAGE_SIZE;
	} while (addr < end);
	return 0;
}
