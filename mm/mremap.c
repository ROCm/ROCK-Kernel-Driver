/*
 *	mm/mremap.c
 *
 *	(C) Copyright 1996 Linus Torvalds
 *
 *	Address space accounting code	<alan@redhat.com>
 *	(C) Copyright 2002 Red Hat Inc, All Rights Reserved
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/rmap-locking.h>
#include <linux/security.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static pte_t *get_one_pte_map_nested(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		goto end;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		goto end;
	}

	pmd = pmd_offset(pgd, addr);
	if (pmd_none(*pmd))
		goto end;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto end;
	}

	pte = pte_offset_map_nested(pmd, addr);
	if (pte_none(*pte)) {
		pte_unmap_nested(pte);
		pte = NULL;
	}
end:
	return pte;
}

static inline int page_table_present(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return 0;
	pmd = pmd_offset(pgd, addr);
	return pmd_present(*pmd);
}

static inline pte_t *alloc_one_pte_map(struct mm_struct *mm, unsigned long addr)
{
	pmd_t *pmd;
	pte_t *pte = NULL;

	pmd = pmd_alloc(mm, pgd_offset(mm, addr), addr);
	if (pmd)
		pte = pte_alloc_map(mm, pmd, addr);
	return pte;
}

static void
copy_one_pte(struct vm_area_struct *vma, unsigned long old_addr,
	     pte_t *src, pte_t *dst, struct pte_chain **pte_chainp)
{
	pte_t pte = ptep_clear_flush(vma, old_addr, src);
	set_pte(dst, pte);

	if (pte_present(pte)) {
		unsigned long pfn = pte_pfn(pte);
		if (pfn_valid(pfn)) {
			struct page *page = pfn_to_page(pfn);
			page_remove_rmap(page, src);
			*pte_chainp = page_add_rmap(page, dst, *pte_chainp);
		}
	}
}

static int
move_one_page(struct vm_area_struct *vma, unsigned long old_addr,
		unsigned long new_addr)
{
	struct mm_struct *mm = vma->vm_mm;
	int error = 0;
	pte_t *src, *dst;
	struct pte_chain *pte_chain;

	pte_chain = pte_chain_alloc(GFP_KERNEL);
	if (!pte_chain) {
		error = -ENOMEM;
		goto out;
	}
	spin_lock(&mm->page_table_lock);
	src = get_one_pte_map_nested(mm, old_addr);
	if (src) {
		/*
		 * Look to see whether alloc_one_pte_map needs to perform a
		 * memory allocation.  If it does then we need to drop the
		 * atomic kmap
		 */
		if (!page_table_present(mm, new_addr)) {
			pte_unmap_nested(src);
			src = NULL;
		}
		dst = alloc_one_pte_map(mm, new_addr);
		if (src == NULL)
			src = get_one_pte_map_nested(mm, old_addr);
		/*
		 * Since alloc_one_pte_map can drop and re-acquire
		 * page_table_lock, we should re-check the src entry...
		 */
		if (src) {
			if (dst)
				copy_one_pte(vma, old_addr, src,
						dst, &pte_chain);
			else
				error = -ENOMEM;
			pte_unmap_nested(src);
		}
		pte_unmap(dst);
	}
	spin_unlock(&mm->page_table_lock);
	pte_chain_free(pte_chain);
out:
	return error;
}

static int move_page_tables(struct vm_area_struct *vma,
	unsigned long new_addr, unsigned long old_addr, unsigned long len)
{
	unsigned long offset;

	flush_cache_range(vma, old_addr, old_addr + len);

	/*
	 * This is not the clever way to do this, but we're taking the
	 * easy way out on the assumption that most remappings will be
	 * only a few pages.. This also makes error recovery easier.
	 */
	for (offset = 0; offset < len; offset += PAGE_SIZE) {
		if (move_one_page(vma, old_addr+offset, new_addr+offset) < 0)
			break;
	}
	return offset;
}

static unsigned long move_vma(struct vm_area_struct *vma,
		unsigned long old_addr, unsigned long old_len,
		unsigned long new_len, unsigned long new_addr)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *new_vma;
	unsigned long vm_flags = vma->vm_flags;
	unsigned long new_pgoff;
	unsigned long moved_len;
	unsigned long excess = 0;
	int split = 0;

	new_pgoff = vma->vm_pgoff + ((old_addr - vma->vm_start) >> PAGE_SHIFT);
	new_vma = copy_vma(vma, new_addr, new_len, new_pgoff);
	if (!new_vma)
		return -ENOMEM;

	moved_len = move_page_tables(vma, new_addr, old_addr, old_len);
	if (moved_len < old_len) {
		/*
		 * On error, move entries back from new area to old,
		 * which will succeed since page tables still there,
		 * and then proceed to unmap new area instead of old.
		 */
		move_page_tables(new_vma, old_addr, new_addr, moved_len);
		vma = new_vma;
		old_len = new_len;
		old_addr = new_addr;
		new_addr = -ENOMEM;
	}

	/* Conceal VM_ACCOUNT so old reservation is not undone */
	if (vm_flags & VM_ACCOUNT) {
		vma->vm_flags &= ~VM_ACCOUNT;
		excess = vma->vm_end - vma->vm_start - old_len;
		if (old_addr > vma->vm_start &&
		    old_addr + old_len < vma->vm_end)
			split = 1;
	}

	if (do_munmap(mm, old_addr, old_len) < 0) {
		/* OOM: unable to split vma, just get accounts right */
		vm_unacct_memory(excess >> PAGE_SHIFT);
		excess = 0;
	}

	/* Restore VM_ACCOUNT if one or two pieces of vma left */
	if (excess) {
		vma->vm_flags |= VM_ACCOUNT;
		if (split)
			vma->vm_next->vm_flags |= VM_ACCOUNT;
	}

	mm->total_vm += new_len >> PAGE_SHIFT;
	if (vm_flags & VM_LOCKED) {
		mm->locked_vm += new_len >> PAGE_SHIFT;
		if (new_len > old_len)
			make_pages_present(new_addr + old_len,
					   new_addr + new_len);
	}

	return new_addr;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 *
 * MREMAP_FIXED option added 5-Dec-1999 by Benjamin LaHaise
 * This option implies MREMAP_MAYMOVE.
 */
unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;
	unsigned long charged = 0;

	if (flags & ~(MREMAP_FIXED | MREMAP_MAYMOVE))
		goto out;

	if (addr & ~PAGE_MASK)
		goto out;

	old_len = PAGE_ALIGN(old_len);
	new_len = PAGE_ALIGN(new_len);

	/*
	 * We allow a zero old-len as a special case
	 * for DOS-emu "duplicate shm area" thing. But
	 * a zero new-len is nonsensical.
	 */
	if (!new_len)
		goto out;

	/* new_addr is only valid if MREMAP_FIXED is specified */
	if (flags & MREMAP_FIXED) {
		if (new_addr & ~PAGE_MASK)
			goto out;
		if (!(flags & MREMAP_MAYMOVE))
			goto out;

		if (new_len > TASK_SIZE || new_addr > TASK_SIZE - new_len)
			goto out;

		/* Check if the location we're moving into overlaps the
		 * old location at all, and fail if it does.
		 */
		if ((new_addr <= addr) && (new_addr+new_len) > addr)
			goto out;

		if ((addr <= new_addr) && (addr+old_len) > new_addr)
			goto out;

		ret = do_munmap(current->mm, new_addr, new_len);
		if (ret)
			goto out;
	}

	/*
	 * Always allow a shrinking remap: that just unmaps
	 * the unnecessary pages..
	 * do_munmap does all the needed commit accounting
	 */
	if (old_len >= new_len) {
		ret = do_munmap(current->mm, addr+new_len, old_len - new_len);
		if (ret && old_len != new_len)
			goto out;
		ret = addr;
		if (!(flags & MREMAP_FIXED) || (new_addr == addr))
			goto out;
		old_len = new_len;
	}

	/*
	 * Ok, we need to grow..  or relocate.
	 */
	ret = -EFAULT;
	vma = find_vma(current->mm, addr);
	if (!vma || vma->vm_start > addr)
		goto out;
	if (is_vm_hugetlb_page(vma)) {
		ret = -EINVAL;
		goto out;
	}
	/* We can't remap across vm area boundaries */
	if (old_len > vma->vm_end - addr)
		goto out;
	if (vma->vm_flags & VM_DONTEXPAND) {
		if (new_len > old_len)
			goto out;
	}
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked = current->mm->locked_vm << PAGE_SHIFT;
		locked += new_len - old_len;
		ret = -EAGAIN;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			goto out;
	}
	ret = -ENOMEM;
	if ((current->mm->total_vm << PAGE_SHIFT) + (new_len - old_len)
	    > current->rlim[RLIMIT_AS].rlim_cur)
		goto out;

	if (vma->vm_flags & VM_ACCOUNT) {
		charged = (new_len - old_len) >> PAGE_SHIFT;
		if (security_vm_enough_memory(charged))
			goto out_nc;
	}

	/* old_len exactly to the end of the area..
	 * And we're not relocating the area.
	 */
	if (old_len == vma->vm_end - addr &&
	    !((flags & MREMAP_FIXED) && (addr != new_addr)) &&
	    (old_len != new_len || !(flags & MREMAP_MAYMOVE))) {
		unsigned long max_addr = TASK_SIZE;
		if (vma->vm_next)
			max_addr = vma->vm_next->vm_start;
		/* can we just expand the current mapping? */
		if (max_addr - addr >= new_len) {
			int pages = (new_len - old_len) >> PAGE_SHIFT;
			spin_lock(&vma->vm_mm->page_table_lock);
			vma->vm_end = addr + new_len;
			spin_unlock(&vma->vm_mm->page_table_lock);
			current->mm->total_vm += pages;
			if (vma->vm_flags & VM_LOCKED) {
				current->mm->locked_vm += pages;
				make_pages_present(addr + old_len,
						   addr + new_len);
			}
			ret = addr;
			goto out;
		}
	}

	/*
	 * We weren't able to just expand or shrink the area,
	 * we need to create a new one and move it..
	 */
	ret = -ENOMEM;
	if (flags & MREMAP_MAYMOVE) {
		if (!(flags & MREMAP_FIXED)) {
			unsigned long map_flags = 0;
			if (vma->vm_flags & VM_MAYSHARE)
				map_flags |= MAP_SHARED;

			new_addr = get_unmapped_area(vma->vm_file, 0, new_len,
						vma->vm_pgoff, map_flags);
			ret = new_addr;
			if (new_addr & ~PAGE_MASK)
				goto out;
		}
		ret = move_vma(vma, addr, old_len, new_len, new_addr);
	}
out:
	if (ret & ~PAGE_MASK)
		vm_unacct_memory(charged);
out_nc:
	return ret;
}

asmlinkage unsigned long sys_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	unsigned long ret;

	down_write(&current->mm->mmap_sem);
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
	up_write(&current->mm->mmap_sem);
	return ret;
}
