/*
 *	linux/mm/mmap.c
 *
 * Written by obz.
 */
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

extern void unmap_page_range(mmu_gather_t *,struct vm_area_struct *vma, unsigned long address, unsigned long size);
extern void clear_page_tables(mmu_gather_t *tlb, unsigned long first, int nr);

/*
 * WARNING: the debugging will use recursive algorithms so never enable this
 * unless you know what you are doing.
 */
#undef DEBUG_MM_RB

/* description of effects of mapping type and prot in current implementation.
 * this is due to the limited x86 page protection hardware.  The expected
 * behavior is in parens:
 *
 * map_type	prot
 *		PROT_NONE	PROT_READ	PROT_WRITE	PROT_EXEC
 * MAP_SHARED	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (yes) yes	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *		
 * MAP_PRIVATE	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (copy) copy	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 */
pgprot_t protection_map[16] = {
	__P000, __P001, __P010, __P011, __P100, __P101, __P110, __P111,
	__S000, __S001, __S010, __S011, __S100, __S101, __S110, __S111
};

int sysctl_overcommit_memory;

/* Check that a process has enough memory to allocate a
 * new virtual mapping.
 */
int vm_enough_memory(long pages)
{
	/* Stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	/* 23/11/98 NJC: Somewhat less stupid version of algorithm,
	 * which tries to do "TheRightThing".  Instead of using half of
	 * (buffers+cache), use the minimum values.  Allow an extra 2%
	 * of num_physpages for safety margin.
	 */

	unsigned long free;
	
        /* Sometimes we want to use more memory than we have. */
	if (sysctl_overcommit_memory)
	    return 1;

	/* The page cache contains buffer pages these days.. */
	free = get_page_cache_size();
	free += nr_free_pages();
	free += nr_swap_pages;

	/*
	 * This double-counts: the nrpages are both in the page-cache
	 * and in the swapper space. At the same time, this compensates
	 * for the swap-space over-allocation (ie "nr_swap_pages" being
	 * too small.
	 */
	free += swapper_space.nrpages;

	/*
	 * The code below doesn't account for free space in the inode
	 * and dentry slab cache, slab cache fragmentation, inodes and
	 * dentries which will become freeable under VM load, etc.
	 * Lets just hope all these (complex) factors balance out...
	 */
	free += (dentry_stat.nr_unused * sizeof(struct dentry)) >> PAGE_SHIFT;
	free += (inodes_stat.nr_unused * sizeof(struct inode)) >> PAGE_SHIFT;

	return free > pages;
}

/* Remove one vm structure from the inode's i_mapping address space. */
static inline void __remove_shared_vm_struct(struct vm_area_struct *vma)
{
	struct file * file = vma->vm_file;

	if (file) {
		struct inode *inode = file->f_dentry->d_inode;
		if (vma->vm_flags & VM_DENYWRITE)
			atomic_inc(&inode->i_writecount);
		list_del_init(&vma->shared);
	}
}

static inline void remove_shared_vm_struct(struct vm_area_struct *vma)
{
	lock_vma_mappings(vma);
	__remove_shared_vm_struct(vma);
	unlock_vma_mappings(vma);
}

void lock_vma_mappings(struct vm_area_struct *vma)
{
	struct address_space *mapping;

	mapping = NULL;
	if (vma->vm_file)
		mapping = vma->vm_file->f_dentry->d_inode->i_mapping;
	if (mapping)
		spin_lock(&mapping->i_shared_lock);
}

void unlock_vma_mappings(struct vm_area_struct *vma)
{
	struct address_space *mapping;

	mapping = NULL;
	if (vma->vm_file)
		mapping = vma->vm_file->f_dentry->d_inode->i_mapping;
	if (mapping)
		spin_unlock(&mapping->i_shared_lock);
}

/*
 *  sys_brk() for the most part doesn't need the global kernel
 *  lock, except when an application is doing something nasty
 *  like trying to un-brk an area that has already been mapped
 *  to a regular file.  in this case, the unmapping will need
 *  to invoke file system routines that need the global lock.
 */
asmlinkage unsigned long sys_brk(unsigned long brk)
{
	unsigned long rlim, retval;
	unsigned long newbrk, oldbrk;
	struct mm_struct *mm = current->mm;

	down_write(&mm->mmap_sem);

	if (brk < mm->end_code)
		goto out;
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk)
		goto set_brk;

	/* Always allow shrinking brk. */
	if (brk <= mm->brk) {
		if (!do_munmap(mm, newbrk, oldbrk-newbrk))
			goto set_brk;
		goto out;
	}

	/* Check against rlimit.. */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim < RLIM_INFINITY && brk - mm->start_data > rlim)
		goto out;

	/* Check against existing mmap mappings. */
	if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
		goto out;

	/* Check if we have enough memory.. */
	if (!vm_enough_memory((newbrk-oldbrk) >> PAGE_SHIFT))
		goto out;

	/* Ok, looks good - let it rip. */
	if (do_brk(oldbrk, newbrk-oldbrk) != oldbrk)
		goto out;
set_brk:
	mm->brk = brk;
out:
	retval = mm->brk;
	up_write(&mm->mmap_sem);
	return retval;
}

/* Combine the mmap "prot" and "flags" argument into one "vm_flags" used
 * internally. Essentially, translate the "PROT_xxx" and "MAP_xxx" bits
 * into "VM_xxx".
 */
static inline unsigned long calc_vm_flags(unsigned long prot, unsigned long flags)
{
#define _trans(x,bit1,bit2) \
((bit1==bit2)?(x&bit1):(x&bit1)?bit2:0)

	unsigned long prot_bits, flag_bits;
	prot_bits =
		_trans(prot, PROT_READ, VM_READ) |
		_trans(prot, PROT_WRITE, VM_WRITE) |
		_trans(prot, PROT_EXEC, VM_EXEC);
	flag_bits =
		_trans(flags, MAP_GROWSDOWN, VM_GROWSDOWN) |
		_trans(flags, MAP_DENYWRITE, VM_DENYWRITE) |
		_trans(flags, MAP_EXECUTABLE, VM_EXECUTABLE);
	return prot_bits | flag_bits;
#undef _trans
}

#ifdef DEBUG_MM_RB
static int browse_rb(rb_node_t * rb_node) {
	int i = 0;
	if (rb_node) {
		i++;
		i += browse_rb(rb_node->rb_left);
		i += browse_rb(rb_node->rb_right);
	}
	return i;
}

static void validate_mm(struct mm_struct * mm) {
	int bug = 0;
	int i = 0;
	struct vm_area_struct * tmp = mm->mmap;
	while (tmp) {
		tmp = tmp->vm_next;
		i++;
	}
	if (i != mm->map_count)
		printk("map_count %d vm_next %d\n", mm->map_count, i), bug = 1;
	i = browse_rb(mm->mm_rb.rb_node);
	if (i != mm->map_count)
		printk("map_count %d rb %d\n", mm->map_count, i), bug = 1;
	if (bug)
		BUG();
}
#else
#define validate_mm(mm) do { } while (0)
#endif

static struct vm_area_struct * find_vma_prepare(struct mm_struct * mm, unsigned long addr,
						struct vm_area_struct ** pprev,
						rb_node_t *** rb_link, rb_node_t ** rb_parent)
{
	struct vm_area_struct * vma;
	rb_node_t ** __rb_link, * __rb_parent, * rb_prev;

	__rb_link = &mm->mm_rb.rb_node;
	rb_prev = __rb_parent = NULL;
	vma = NULL;

	while (*__rb_link) {
		struct vm_area_struct *vma_tmp;

		__rb_parent = *__rb_link;
		vma_tmp = rb_entry(__rb_parent, struct vm_area_struct, vm_rb);

		if (vma_tmp->vm_end > addr) {
			vma = vma_tmp;
			if (vma_tmp->vm_start <= addr)
				return vma;
			__rb_link = &__rb_parent->rb_left;
		} else {
			rb_prev = __rb_parent;
			__rb_link = &__rb_parent->rb_right;
		}
	}

	*pprev = NULL;
	if (rb_prev)
		*pprev = rb_entry(rb_prev, struct vm_area_struct, vm_rb);
	*rb_link = __rb_link;
	*rb_parent = __rb_parent;
	return vma;
}

static inline void __vma_link_list(struct mm_struct * mm, struct vm_area_struct * vma, struct vm_area_struct * prev,
				   rb_node_t * rb_parent)
{
	if (prev) {
		vma->vm_next = prev->vm_next;
		prev->vm_next = vma;
	} else {
		mm->mmap = vma;
		if (rb_parent)
			vma->vm_next = rb_entry(rb_parent, struct vm_area_struct, vm_rb);
		else
			vma->vm_next = NULL;
	}
}

static inline void __vma_link_rb(struct mm_struct * mm, struct vm_area_struct * vma,
				 rb_node_t ** rb_link, rb_node_t * rb_parent)
{
	rb_link_node(&vma->vm_rb, rb_parent, rb_link);
	rb_insert_color(&vma->vm_rb, &mm->mm_rb);
}

static inline void __vma_link_file(struct vm_area_struct * vma)
{
	struct file * file;

	file = vma->vm_file;
	if (file) {
		struct inode * inode = file->f_dentry->d_inode;
		struct address_space *mapping = inode->i_mapping;

		if (vma->vm_flags & VM_DENYWRITE)
			atomic_dec(&inode->i_writecount);

		if (vma->vm_flags & VM_SHARED)
			list_add_tail(&vma->shared, &mapping->i_mmap_shared);
		else
			list_add_tail(&vma->shared, &mapping->i_mmap);
	}
}

static void __vma_link(struct mm_struct * mm, struct vm_area_struct * vma,  struct vm_area_struct * prev,
		       rb_node_t ** rb_link, rb_node_t * rb_parent)
{
	__vma_link_list(mm, vma, prev, rb_parent);
	__vma_link_rb(mm, vma, rb_link, rb_parent);
	__vma_link_file(vma);
}

static inline void vma_link(struct mm_struct * mm, struct vm_area_struct * vma, struct vm_area_struct * prev,
			    rb_node_t ** rb_link, rb_node_t * rb_parent)
{
	spin_lock(&mm->page_table_lock);
	lock_vma_mappings(vma);
	__vma_link(mm, vma, prev, rb_link, rb_parent);
	unlock_vma_mappings(vma);
	spin_unlock(&mm->page_table_lock);

	mm->map_count++;
	validate_mm(mm);
}

static int vma_merge(struct mm_struct * mm, struct vm_area_struct * prev,
		     rb_node_t * rb_parent, unsigned long addr, unsigned long end, unsigned long vm_flags)
{
	spinlock_t * lock = &mm->page_table_lock;
	if (!prev) {
		prev = rb_entry(rb_parent, struct vm_area_struct, vm_rb);
		goto merge_next;
	}
	if (prev->vm_end == addr && can_vma_merge(prev, vm_flags)) {
		struct vm_area_struct * next;

		spin_lock(lock);
		prev->vm_end = end;
		next = prev->vm_next;
		if (next && prev->vm_end == next->vm_start && can_vma_merge(next, vm_flags)) {
			prev->vm_end = next->vm_end;
			__vma_unlink(mm, next, prev);
			spin_unlock(lock);

			mm->map_count--;
			kmem_cache_free(vm_area_cachep, next);
			return 1;
		}
		spin_unlock(lock);
		return 1;
	}

	prev = prev->vm_next;
	if (prev) {
 merge_next:
		if (!can_vma_merge(prev, vm_flags))
			return 0;
		if (end == prev->vm_start) {
			spin_lock(lock);
			prev->vm_start = addr;
			spin_unlock(lock);
			return 1;
		}
	}

	return 0;
}

unsigned long do_mmap_pgoff(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long pgoff)
{
	struct mm_struct * mm = current->mm;
	struct vm_area_struct * vma, * prev;
	unsigned int vm_flags;
	int correct_wcount = 0;
	int error;
	rb_node_t ** rb_link, * rb_parent;

	if (file && (!file->f_op || !file->f_op->mmap))
		return -ENODEV;

	if (!len)
		return addr;

	if (len > TASK_SIZE)
		return -EINVAL;

	len = PAGE_ALIGN(len);

	/* offset overflow? */
	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EINVAL;

	/* Too many mappings? */
	if (mm->map_count > MAX_MAP_COUNT)
		return -ENOMEM;

	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */
	addr = get_unmapped_area(file, addr, len, pgoff, flags);
	if (addr & ~PAGE_MASK)
		return addr;

	/* Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */
	vm_flags = calc_vm_flags(prot,flags) | mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	/* mlock MCL_FUTURE? */
	if (vm_flags & VM_LOCKED) {
		unsigned long locked = mm->locked_vm << PAGE_SHIFT;
		locked += len;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			return -EAGAIN;
	}

	if (file) {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if ((prot & PROT_WRITE) && !(file->f_mode & FMODE_WRITE))
				return -EACCES;

			/* Make sure we don't allow writing to an append-only file.. */
			if (IS_APPEND(file->f_dentry->d_inode) && (file->f_mode & FMODE_WRITE))
				return -EACCES;

			/* make sure there are no mandatory locks on the file. */
			if (locks_verify_locked(file->f_dentry->d_inode))
				return -EAGAIN;

			vm_flags |= VM_SHARED | VM_MAYSHARE;
			if (!(file->f_mode & FMODE_WRITE))
				vm_flags &= ~(VM_MAYWRITE | VM_SHARED);

			/* fall through */
		case MAP_PRIVATE:
			if (!(file->f_mode & FMODE_READ))
				return -EACCES;
			break;

		default:
			return -EINVAL;
		}
	} else {
		vm_flags |= VM_SHARED | VM_MAYSHARE;
		switch (flags & MAP_TYPE) {
		default:
			return -EINVAL;
		case MAP_PRIVATE:
			vm_flags &= ~(VM_SHARED | VM_MAYSHARE);
			/* fall through */
		case MAP_SHARED:
			break;
		}
	}

	/* Clear old maps */
	error = -ENOMEM;
munmap_back:
	vma = find_vma_prepare(mm, addr, &prev, &rb_link, &rb_parent);
	if (vma && vma->vm_start < addr + len) {
		if (do_munmap(mm, addr, len))
			return -ENOMEM;
		goto munmap_back;
	}

	/* Check against address space limit. */
	if ((mm->total_vm << PAGE_SHIFT) + len
	    > current->rlim[RLIMIT_AS].rlim_cur)
		return -ENOMEM;

	/* Private writable mapping? Check memory availability.. */
	if ((vm_flags & (VM_SHARED | VM_WRITE)) == VM_WRITE &&
	    !(flags & MAP_NORESERVE)				 &&
	    !vm_enough_memory(len >> PAGE_SHIFT))
		return -ENOMEM;

	/* Can we just expand an old anonymous mapping? */
	if (!file && !(vm_flags & VM_SHARED) && rb_parent)
		if (vma_merge(mm, prev, rb_parent, addr, addr + len, vm_flags))
			goto out;

	/* Determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma)
		return -ENOMEM;

	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = vm_flags;
	vma->vm_page_prot = protection_map[vm_flags & 0x0f];
	vma->vm_ops = NULL;
	vma->vm_pgoff = pgoff;
	vma->vm_file = NULL;
	vma->vm_private_data = NULL;
	vma->vm_raend = 0;

	if (file) {
		error = -EINVAL;
		if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
			goto free_vma;
		if (vm_flags & VM_DENYWRITE) {
			error = deny_write_access(file);
			if (error)
				goto free_vma;
			correct_wcount = 1;
		}
		vma->vm_file = file;
		get_file(file);
		error = file->f_op->mmap(file, vma);
		if (error)
			goto unmap_and_free_vma;
	} else if (flags & MAP_SHARED) {
		error = shmem_zero_setup(vma);
		if (error)
			goto free_vma;
	}

	/* Can addr have changed??
	 *
	 * Answer: Yes, several device drivers can do it in their
	 *         f_op->mmap method. -DaveM
	 */
	addr = vma->vm_start;

	vma_link(mm, vma, prev, rb_link, rb_parent);
	if (correct_wcount)
		atomic_inc(&file->f_dentry->d_inode->i_writecount);

out:	
	mm->total_vm += len >> PAGE_SHIFT;
	if (vm_flags & VM_LOCKED) {
		mm->locked_vm += len >> PAGE_SHIFT;
		make_pages_present(addr, addr + len);
	}
	return addr;

unmap_and_free_vma:
	if (correct_wcount)
		atomic_inc(&file->f_dentry->d_inode->i_writecount);
	vma->vm_file = NULL;
	fput(file);

	/* Undo any partial mapping done by a device driver. */
	zap_page_range(vma, vma->vm_start, vma->vm_end - vma->vm_start);
free_vma:
	kmem_cache_free(vm_area_cachep, vma);
	return error;
}

/* Get an address range which is currently unmapped.
 * For shmat() with addr=0.
 *
 * Ugly calling convention alert:
 * Return value with the low bits set means error value,
 * ie
 *	if (ret & ~PAGE_MASK)
 *		error = ret;
 *
 * This function "knows" that -ENOMEM has the bits set.
 */
#ifndef HAVE_ARCH_UNMAPPED_AREA
static inline unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct *vma;

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(current->mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	addr = PAGE_ALIGN(TASK_UNMAPPED_BASE);

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = vma->vm_end;
	}
}
#else
extern unsigned long arch_get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
#endif	

unsigned long get_unmapped_area(struct file *file, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
	if (flags & MAP_FIXED) {
		if (addr > TASK_SIZE - len)
			return -ENOMEM;
		if (addr & ~PAGE_MASK)
			return -EINVAL;
		return addr;
	}

	if (file && file->f_op && file->f_op->get_unmapped_area)
		return file->f_op->get_unmapped_area(file, addr, len, pgoff, flags);

	return arch_get_unmapped_area(file, addr, len, pgoff, flags);
}

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr)
{
	struct vm_area_struct *vma = NULL;

	if (mm) {
		/* Check the cache first. */
		/* (Cache hit rate is typically around 35%.) */
		vma = mm->mmap_cache;
		if (!(vma && vma->vm_end > addr && vma->vm_start <= addr)) {
			rb_node_t * rb_node;

			rb_node = mm->mm_rb.rb_node;
			vma = NULL;

			while (rb_node) {
				struct vm_area_struct * vma_tmp;

				vma_tmp = rb_entry(rb_node, struct vm_area_struct, vm_rb);

				if (vma_tmp->vm_end > addr) {
					vma = vma_tmp;
					if (vma_tmp->vm_start <= addr)
						break;
					rb_node = rb_node->rb_left;
				} else
					rb_node = rb_node->rb_right;
			}
			if (vma)
				mm->mmap_cache = vma;
		}
	}
	return vma;
}

/* Same as find_vma, but also return a pointer to the previous VMA in *pprev. */
struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
				      struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma = NULL, *prev = NULL;
	rb_node_t * rb_node;
	if (!mm)
		goto out;

	/* Guard against addr being lower than the first VMA */
	vma = mm->mmap;

	/* Go through the RB tree quickly. */
	rb_node = mm->mm_rb.rb_node;

	while (rb_node) {
		struct vm_area_struct *vma_tmp;
		vma_tmp = rb_entry(rb_node, struct vm_area_struct, vm_rb);

		if (addr < vma_tmp->vm_end) {
			rb_node = rb_node->rb_left;
		} else {
			prev = vma_tmp;
			if (!prev->vm_next || (addr < prev->vm_next->vm_end))
				break;
			rb_node = rb_node->rb_right;
		}
	}

 out:
	*pprev = prev;
	return prev ? prev->vm_next : vma;
}

#ifdef ARCH_STACK_GROWSUP
struct vm_area_struct * find_extend_vma(struct mm_struct * mm, unsigned long addr)
{
	struct vm_area_struct *vma, *prev;

	addr &= PAGE_MASK;
	vma = find_vma_prev(mm, addr, &prev);
	if (vma && (vma->vm_start <= addr))
		return vma;
	if (!prev || expand_stack(prev, addr))
		return NULL;
	if (prev->vm_flags & VM_LOCKED) {
		make_pages_present(addr, prev->vm_end);
	}
	return prev;
}
#else
struct vm_area_struct * find_extend_vma(struct mm_struct * mm, unsigned long addr)
{
	struct vm_area_struct * vma;
	unsigned long start;

	addr &= PAGE_MASK;
	vma = find_vma(mm,addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	start = vma->vm_start;
	if (expand_stack(vma, addr))
		return NULL;
	if (vma->vm_flags & VM_LOCKED) {
		make_pages_present(addr, start);
	}
	return vma;
}
#endif

/*
 * Try to free as many page directory entries as we can,
 * without having to work very hard at actually scanning
 * the page tables themselves.
 *
 * Right now we try to free page tables if we have a nice
 * PGDIR-aligned area that got free'd up. We could be more
 * granular if we want to, but this is fast and simple,
 * and covers the bad cases.
 *
 * "prev", if it exists, points to a vma before the one
 * we just free'd - but there's no telling how much before.
 */
static void free_pgtables(mmu_gather_t *tlb, struct vm_area_struct *prev,
	unsigned long start, unsigned long end)
{
	unsigned long first = start & PGDIR_MASK;
	unsigned long last = end + PGDIR_SIZE - 1;
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
			first = prev->vm_end + PGDIR_SIZE - 1;
		break;
	}
no_mmaps:
	if (last < first)	/* needed for arches with discontiguous pgd indices */
		return;
	/*
	 * If the PGD bits are not consecutive in the virtual address, the
	 * old method of shifting the VA >> by PGDIR_SHIFT doesn't work.
	 */
	start_index = pgd_index(first);
	end_index = pgd_index(last);
	if (end_index > start_index) {
		clear_page_tables(tlb, start_index, end_index - start_index);
		flush_tlb_pgtables(mm, first & PGDIR_MASK, last & PGDIR_MASK);
	}
}

/* Normal function to fix up a mapping
 * This function is the default for when an area has no specific
 * function.  This may be used as part of a more specific routine.
 *
 * By the time this function is called, the area struct has been
 * removed from the process mapping list.
 */
static void unmap_vma(struct mm_struct *mm, struct vm_area_struct *area)
{
	size_t len = area->vm_end - area->vm_start;

	area->vm_mm->total_vm -= len >> PAGE_SHIFT;
	if (area->vm_flags & VM_LOCKED)
		area->vm_mm->locked_vm -= len >> PAGE_SHIFT;

	remove_shared_vm_struct(area);

	if (area->vm_ops && area->vm_ops->close)
		area->vm_ops->close(area);
	if (area->vm_file)
		fput(area->vm_file);
	kmem_cache_free(vm_area_cachep, area);
}


/*
 * Update the VMA and inode share lists.
 *
 * Ok - we have the memory areas we should free on the 'free' list,
 * so release them, and do the vma updates.
 */
static void unmap_vma_list(struct mm_struct *mm,
	struct vm_area_struct *mpnt)
{
	do {
		struct vm_area_struct *next = mpnt->vm_next;
		unmap_vma(mm, mpnt);
		mpnt = next;
	} while (mpnt != NULL);
	validate_mm(mm);
}

/*
 * Get rid of page table information in the indicated region.
 *
 * Called with the page table lock held.
 */
static void unmap_region(struct mm_struct *mm,
	struct vm_area_struct *mpnt,
	struct vm_area_struct *prev,
	unsigned long start,
	unsigned long end)
{
	mmu_gather_t *tlb;

	tlb = tlb_gather_mmu(mm, 0);

	do {
		unsigned long from, to;

		from = start < mpnt->vm_start ? mpnt->vm_start : start;
		to = end > mpnt->vm_end ? mpnt->vm_end : end;

		unmap_page_range(tlb, mpnt, from, to);
	} while ((mpnt = mpnt->vm_next) != NULL);

	free_pgtables(tlb, prev, start, end);
	tlb_finish_mmu(tlb, start, end);
}

/*
 * Create a list of vma's touched by the unmap,
 * removing them from the VM lists as we go..
 *
 * Called with the page_table_lock held.
 */
static struct vm_area_struct *touched_by_munmap(struct mm_struct *mm,
	struct vm_area_struct *mpnt,
	struct vm_area_struct *prev,
	unsigned long end)
{
	struct vm_area_struct **npp, *touched;

	npp = (prev ? &prev->vm_next : &mm->mmap);

	touched = NULL;
	do {
		struct vm_area_struct *next = mpnt->vm_next;
		mpnt->vm_next = touched;
		touched = mpnt;
		mm->map_count--;
		rb_erase(&mpnt->vm_rb, &mm->mm_rb);
		mpnt = next;
	} while (mpnt && mpnt->vm_start < end);
	*npp = mpnt;
	mm->mmap_cache = NULL;	/* Kill the cache. */
	return touched;
}

/*
 * Split a vma into two pieces at address 'addr', the original vma
 * will contain the first part, a new vma is allocated for the tail.
 */
static int splitvma(struct mm_struct *mm, struct vm_area_struct *mpnt, unsigned long addr)
{
	struct vm_area_struct *new;

	if (mm->map_count >= MAX_MAP_COUNT)
		return -ENOMEM;

	new = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!new)
		return -ENOMEM;

	/* most fields are the same, copy all, and then fixup */
	*new = *mpnt;

	new->vm_start = addr;
	new->vm_pgoff = mpnt->vm_pgoff + ((addr - mpnt->vm_start) >> PAGE_SHIFT);
	new->vm_raend = 0;
	if (mpnt->vm_file)
		get_file(mpnt->vm_file);

	if (mpnt->vm_ops && mpnt->vm_ops->open)
		mpnt->vm_ops->open(mpnt);
	mpnt->vm_end = addr;	/* Truncate area */

	spin_lock(&mm->page_table_lock);
	lock_vma_mappings(mpnt);
	__insert_vm_struct(mm, new);
	unlock_vma_mappings(mpnt);
	spin_unlock(&mm->page_table_lock);

	return 0;
}

/* Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardine <jeremy@sw.oz.au>
 */
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len)
{
	unsigned long end;
	struct vm_area_struct *mpnt, *prev, *last;

	if ((start & ~PAGE_MASK) || start > TASK_SIZE || len > TASK_SIZE-start)
		return -EINVAL;

	if ((len = PAGE_ALIGN(len)) == 0)
		return -EINVAL;

	/* Find the first overlapping VMA */
	mpnt = find_vma_prev(mm, start, &prev);
	if (!mpnt)
		return 0;
	/* we have  start < mpnt->vm_end  */

	/* if it doesn't overlap, we have nothing.. */
	end = start + len;
	if (mpnt->vm_start >= end)
		return 0;

	/*
	 * If we need to split any vma, do it now to save pain later.
	 */
	if (start > mpnt->vm_start) {
		if (splitvma(mm, mpnt, start))
			return -ENOMEM;
		prev = mpnt;
		mpnt = mpnt->vm_next;
	}

	/* Does it split the last one? */
	last = find_vma(mm, end);
	if (last && end > last->vm_start) {
		if (splitvma(mm, last, end))
			return -ENOMEM;
	}

	/*
	 * Remove the vma's, and unmap the actual pages
	 */
	spin_lock(&mm->page_table_lock);
	mpnt = touched_by_munmap(mm, mpnt, prev, end);
	unmap_region(mm, mpnt, prev, start, end);
	spin_unlock(&mm->page_table_lock);

	/* Fix up all other VM information */
	unmap_vma_list(mm, mpnt);

	return 0;
}

asmlinkage long sys_munmap(unsigned long addr, size_t len)
{
	int ret;
	struct mm_struct *mm = current->mm;

	down_write(&mm->mmap_sem);
	ret = do_munmap(mm, addr, len);
	up_write(&mm->mmap_sem);
	return ret;
}

/*
 *  this is really a simplified "do_mmap".  it only handles
 *  anonymous maps.  eventually we may be able to do some
 *  brk-specific accounting here.
 */
unsigned long do_brk(unsigned long addr, unsigned long len)
{
	struct mm_struct * mm = current->mm;
	struct vm_area_struct * vma, * prev;
	unsigned long flags;
	rb_node_t ** rb_link, * rb_parent;

	len = PAGE_ALIGN(len);
	if (!len)
		return addr;

	/*
	 * mlock MCL_FUTURE?
	 */
	if (mm->def_flags & VM_LOCKED) {
		unsigned long locked = mm->locked_vm << PAGE_SHIFT;
		locked += len;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			return -EAGAIN;
	}

	/*
	 * Clear old maps.  this also does some error checking for us
	 */
 munmap_back:
	vma = find_vma_prepare(mm, addr, &prev, &rb_link, &rb_parent);
	if (vma && vma->vm_start < addr + len) {
		if (do_munmap(mm, addr, len))
			return -ENOMEM;
		goto munmap_back;
	}

	/* Check against address space limits *after* clearing old maps... */
	if ((mm->total_vm << PAGE_SHIFT) + len
	    > current->rlim[RLIMIT_AS].rlim_cur)
		return -ENOMEM;

	if (mm->map_count > MAX_MAP_COUNT)
		return -ENOMEM;

	if (!vm_enough_memory(len >> PAGE_SHIFT))
		return -ENOMEM;

	flags = VM_DATA_DEFAULT_FLAGS | mm->def_flags;

	/* Can we just expand an old anonymous mapping? */
	if (rb_parent && vma_merge(mm, prev, rb_parent, addr, addr + len, flags))
		goto out;

	/*
	 * create a vma struct for an anonymous mapping
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma)
		return -ENOMEM;

	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = flags;
	vma->vm_page_prot = protection_map[flags & 0x0f];
	vma->vm_ops = NULL;
	vma->vm_pgoff = 0;
	vma->vm_file = NULL;
	vma->vm_private_data = NULL;

	vma_link(mm, vma, prev, rb_link, rb_parent);

out:
	mm->total_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED) {
		mm->locked_vm += len >> PAGE_SHIFT;
		make_pages_present(addr, addr + len);
	}
	return addr;
}

/* Build the RB tree corresponding to the VMA list. */
void build_mmap_rb(struct mm_struct * mm)
{
	struct vm_area_struct * vma;
	rb_node_t ** rb_link, * rb_parent;

	mm->mm_rb = RB_ROOT;
	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		__vma_link_rb(mm, vma, rb_link, rb_parent);
		rb_parent = &vma->vm_rb;
		rb_link = &rb_parent->rb_right;
	}
}

/* Release all mmaps. */
void exit_mmap(struct mm_struct * mm)
{
	mmu_gather_t *tlb;
	struct vm_area_struct * mpnt;

	release_segments(mm);
	spin_lock(&mm->page_table_lock);

	tlb = tlb_gather_mmu(mm, 1);

	flush_cache_mm(mm);
	mpnt = mm->mmap;
	while (mpnt) {
		unsigned long start = mpnt->vm_start;
		unsigned long end = mpnt->vm_end;

		mm->map_count--;
		remove_shared_vm_struct(mpnt);
		unmap_page_range(tlb, mpnt, start, end);
		mpnt = mpnt->vm_next;
	}

	/* This is just debugging */
	if (mm->map_count)
		BUG();

	clear_page_tables(tlb, FIRST_USER_PGD_NR, USER_PTRS_PER_PGD);
	tlb_finish_mmu(tlb, 0, TASK_SIZE);

	mpnt = mm->mmap;
	mm->mmap = mm->mmap_cache = NULL;
	mm->mm_rb = RB_ROOT;
	mm->rss = 0;
	mm->total_vm = 0;
	mm->locked_vm = 0;

	spin_unlock(&mm->page_table_lock);

	/*
	 * Walk the list again, actually closing and freeing it
	 * without holding any MM locks.
	 */
	while (mpnt) {
		struct vm_area_struct * next = mpnt->vm_next;
		if (mpnt->vm_ops) {
			if (mpnt->vm_ops->close)
				mpnt->vm_ops->close(mpnt);
		}
		if (mpnt->vm_file)
			fput(mpnt->vm_file);
		kmem_cache_free(vm_area_cachep, mpnt);
		mpnt = next;
	}
		
}

/* Insert vm structure into process list sorted by address
 * and into the inode's i_mmap ring.  If vm_file is non-NULL
 * then the i_shared_lock must be held here.
 */
void __insert_vm_struct(struct mm_struct * mm, struct vm_area_struct * vma)
{
	struct vm_area_struct * __vma, * prev;
	rb_node_t ** rb_link, * rb_parent;

	__vma = find_vma_prepare(mm, vma->vm_start, &prev, &rb_link, &rb_parent);
	if (__vma && __vma->vm_start < vma->vm_end)
		BUG();
	__vma_link(mm, vma, prev, rb_link, rb_parent);
	mm->map_count++;
	validate_mm(mm);
}

void insert_vm_struct(struct mm_struct * mm, struct vm_area_struct * vma)
{
	struct vm_area_struct * __vma, * prev;
	rb_node_t ** rb_link, * rb_parent;

	__vma = find_vma_prepare(mm, vma->vm_start, &prev, &rb_link, &rb_parent);
	if (__vma && __vma->vm_start < vma->vm_end)
		BUG();
	vma_link(mm, vma, prev, rb_link, rb_parent);
	validate_mm(mm);
}
