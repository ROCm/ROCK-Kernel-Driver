/*
 *	linux/mm/madvise.c
 *
 * Copyright (C) 1999  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/slab.h>


static inline void setup_read_behavior(struct vm_area_struct * vma,
				       int behavior)
{
	VM_ClearReadHint(vma);

	switch (behavior) {
	case MADV_SEQUENTIAL:
		vma->vm_flags |= VM_SEQ_READ;
		break;
	case MADV_RANDOM:
		vma->vm_flags |= VM_RAND_READ;
		break;
	default:
		break;
	}
}

static long madvise_fixup_start(struct vm_area_struct * vma,
				unsigned long end, int behavior)
{
	struct vm_area_struct * n;
	struct mm_struct * mm = vma->vm_mm;

	n = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	n->vm_end = end;
	setup_read_behavior(n, behavior);
	n->vm_raend = 0;
	if (n->vm_file)
		get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	vma->vm_pgoff += (end - vma->vm_start) >> PAGE_SHIFT;
	lock_vma_mappings(vma);
	spin_lock(&mm->page_table_lock);
	vma->vm_start = end;
	__insert_vm_struct(mm, n);
	spin_unlock(&mm->page_table_lock);
	unlock_vma_mappings(vma);
	return 0;
}

static long madvise_fixup_end(struct vm_area_struct * vma,
			      unsigned long start, int behavior)
{
	struct vm_area_struct * n;
	struct mm_struct * mm = vma->vm_mm;

	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	n->vm_start = start;
	n->vm_pgoff += (n->vm_start - vma->vm_start) >> PAGE_SHIFT;
	setup_read_behavior(n, behavior);
	n->vm_raend = 0;
	if (n->vm_file)
		get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	lock_vma_mappings(vma);
	spin_lock(&mm->page_table_lock);
	vma->vm_end = start;
	__insert_vm_struct(mm, n);
	spin_unlock(&mm->page_table_lock);
	unlock_vma_mappings(vma);
	return 0;
}

static long madvise_fixup_middle(struct vm_area_struct * vma, unsigned long start,
				 unsigned long end, int behavior)
{
	struct vm_area_struct * left, * right;
	struct mm_struct * mm = vma->vm_mm;

	left = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!left)
		return -EAGAIN;
	right = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!right) {
		kmem_cache_free(vm_area_cachep, left);
		return -EAGAIN;
	}
	*left = *vma;
	*right = *vma;
	left->vm_end = start;
	right->vm_start = end;
	right->vm_pgoff += (right->vm_start - left->vm_start) >> PAGE_SHIFT;
	left->vm_raend = 0;
	right->vm_raend = 0;
	if (vma->vm_file)
		atomic_add(2, &vma->vm_file->f_count);

	if (vma->vm_ops && vma->vm_ops->open) {
		vma->vm_ops->open(left);
		vma->vm_ops->open(right);
	}
	vma->vm_pgoff += (start - vma->vm_start) >> PAGE_SHIFT;
	vma->vm_raend = 0;
	lock_vma_mappings(vma);
	spin_lock(&mm->page_table_lock);
	vma->vm_start = start;
	vma->vm_end = end;
	setup_read_behavior(vma, behavior);
	__insert_vm_struct(mm, left);
	__insert_vm_struct(mm, right);
	spin_unlock(&mm->page_table_lock);
	unlock_vma_mappings(vma);
	return 0;
}

/*
 * We can potentially split a vm area into separate
 * areas, each area with its own behavior.
 */
static long madvise_behavior(struct vm_area_struct * vma, unsigned long start,
			     unsigned long end, int behavior)
{
	int error = 0;

	/* This caps the number of vma's this process can own */
	if (vma->vm_mm->map_count > MAX_MAP_COUNT)
		return -ENOMEM;

	if (start == vma->vm_start) {
		if (end == vma->vm_end) {
			setup_read_behavior(vma, behavior);
			vma->vm_raend = 0;
		} else
			error = madvise_fixup_start(vma, end, behavior);
	} else {
		if (end == vma->vm_end)
			error = madvise_fixup_end(vma, start, behavior);
		else
			error = madvise_fixup_middle(vma, start, end, behavior);
	}

	return error;
}

/*
 * Schedule all required I/O operations, then run the disk queue
 * to make sure they are started.  Do not wait for completion.
 */
static long madvise_willneed(struct vm_area_struct * vma,
			     unsigned long start, unsigned long end)
{
	long error = -EBADF;
	struct file * file;
	unsigned long size, rlim_rss;

	/* Doesn't work if there's no mapped file. */
	if (!vma->vm_file)
		return error;
	file = vma->vm_file;
	size = (file->f_dentry->d_inode->i_size + PAGE_CACHE_SIZE - 1) >>
							PAGE_CACHE_SHIFT;

	start = ((start - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (end > vma->vm_end)
		end = vma->vm_end;
	end = ((end - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	/* Make sure this doesn't exceed the process's max rss. */
	error = -EIO;
	rlim_rss = current->rlim ?  current->rlim[RLIMIT_RSS].rlim_cur :
				LONG_MAX; /* default: see resource.h */
	if ((vma->vm_mm->rss + (end - start)) > rlim_rss)
		return error;

	do_page_cache_readahead(file, start, end - start);
	return 0;
}

/*
 * Application no longer needs these pages.  If the pages are dirty,
 * it's OK to just throw them away.  The app will be more careful about
 * data it wants to keep.  Be sure to free swap resources too.  The
 * zap_page_range call sets things up for refill_inactive to actually free
 * these pages later if no one else has touched them in the meantime,
 * although we could add these pages to a global reuse list for
 * refill_inactive to pick up before reclaiming other pages.
 *
 * NB: This interface discards data rather than pushes it out to swap,
 * as some implementations do.  This has performance implications for
 * applications like large transactional databases which want to discard
 * pages in anonymous maps after committing to backing store the data
 * that was kept in them.  There is no reason to write this data out to
 * the swap area if the application is discarding it.
 *
 * An interface that causes the system to free clean pages and flush
 * dirty pages is already available as msync(MS_INVALIDATE).
 */
static long madvise_dontneed(struct vm_area_struct * vma,
			     unsigned long start, unsigned long end)
{
	if (vma->vm_flags & VM_LOCKED)
		return -EINVAL;

	zap_page_range(vma, start, end - start);
	return 0;
}

static long madvise_vma(struct vm_area_struct * vma, unsigned long start,
			unsigned long end, int behavior)
{
	long error = -EBADF;

	switch (behavior) {
	case MADV_NORMAL:
	case MADV_SEQUENTIAL:
	case MADV_RANDOM:
		error = madvise_behavior(vma, start, end, behavior);
		break;

	case MADV_WILLNEED:
		error = madvise_willneed(vma, start, end);
		break;

	case MADV_DONTNEED:
		error = madvise_dontneed(vma, start, end);
		break;

	default:
		error = -EINVAL;
		break;
	}
		
	return error;
}

/*
 * The madvise(2) system call.
 *
 * Applications can use madvise() to advise the kernel how it should
 * handle paging I/O in this VM area.  The idea is to help the kernel
 * use appropriate read-ahead and caching techniques.  The information
 * provided is advisory only, and can be safely disregarded by the
 * kernel without affecting the correct operation of the application.
 *
 * behavior values:
 *  MADV_NORMAL - the default behavior is to read clusters.  This
 *		results in some read-ahead and read-behind.
 *  MADV_RANDOM - the system should read the minimum amount of data
 *		on any access, since it is unlikely that the appli-
 *		cation will need more than what it asks for.
 *  MADV_SEQUENTIAL - pages in the given range will probably be accessed
 *		once, so they can be aggressively read ahead, and
 *		can be freed soon after they are accessed.
 *  MADV_WILLNEED - the application is notifying the system to read
 *		some pages ahead.
 *  MADV_DONTNEED - the application is finished with the given range,
 *		so the kernel can free resources associated with it.
 *
 * return values:
 *  zero    - success
 *  -EINVAL - start + len < 0, start is not page-aligned,
 *		"behavior" is not a valid value, or application
 *		is attempting to release locked or shared pages.
 *  -ENOMEM - addresses in the specified range are not currently
 *		mapped, or are outside the AS of the process.
 *  -EIO    - an I/O error occurred while paging in data.
 *  -EBADF  - map exists, but area maps something that isn't a file.
 *  -EAGAIN - a kernel resource was temporarily unavailable.
 */
asmlinkage long sys_madvise(unsigned long start, size_t len, int behavior)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error = 0;
	int error = -EINVAL;

	down_write(&current->mm->mmap_sem);

	if (start & ~PAGE_MASK)
		goto out;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		goto out;

	error = 0;
	if (end == start)
		goto out;

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 */
	vma = find_vma(current->mm, start);
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;

		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}

		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = madvise_vma(vma, start, end,
							behavior);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}

		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = madvise_vma(vma, start, vma->vm_end, behavior);
		if (error)
			goto out;
		start = vma->vm_end;
		vma = vma->vm_next;
	}

out:
	up_write(&current->mm->mmap_sem);
	return error;
}
