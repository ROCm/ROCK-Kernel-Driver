/*
 * mm/mmap.c
 *
 * Written by obz.
 *
 * Address space accounting code	<alan@redhat.com>
 */

/*
 * All modifications to vm_start/vm_pgoff must happen
 * under the semaphore (for file mappings) and under the
 * anon_vma->anon_vma_lock (for anon mappings), to serialize
 * against truncate and other objrmap users. See move_vma_start.
 *
 * We take the page_table_lock then the PG_maplock and finally
 * the anon_vma_lock (fork requires that ordering).
 */

#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/profile.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/objrmap.h>
#include <linux/audit.h>
#include <linux/err.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

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

int sysctl_overcommit_memory = 0;	/* default is heuristic overcommit */
int sysctl_overcommit_ratio = 50;	/* default is 50% */
int sysctl_max_map_count = DEFAULT_MAX_MAP_COUNT;
atomic_t vm_committed_space = ATOMIC_INIT(0);

EXPORT_SYMBOL(sysctl_overcommit_memory);
EXPORT_SYMBOL(sysctl_overcommit_ratio);
EXPORT_SYMBOL(sysctl_max_map_count);
EXPORT_SYMBOL(vm_committed_space);

int mmap_use_hugepages = 0;
int mmap_hugepages_map_sz = 256;

/*
 * Requires inode->i_mapping->i_shared_sem
 */
void
__remove_shared_vm_struct(struct vm_area_struct *vma, struct inode *inode,
			  struct address_space * mapping)
{
	if (inode) {
		if (vma->vm_flags & VM_DENYWRITE)
			atomic_inc(&inode->i_writecount);
		if (unlikely(vma->vm_flags & VM_NONLINEAR)) {
			list_del_init(&vma->shared.vm_set.list);
			INIT_VMA_SHARED(vma);
		}
		else if (vma->vm_flags & VM_SHARED)
			__vma_prio_tree_remove(&mapping->i_mmap_shared, vma);
		else
			__vma_prio_tree_remove(&mapping->i_mmap, vma);
	}
}

/*
 * Remove one vm structure from the inode's i_mapping address space.
 */
static void remove_shared_vm_struct(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;

	if (file) {
		struct address_space *mapping = file->f_mapping;
		down(&mapping->i_shared_sem);
		__remove_shared_vm_struct(vma, file->f_dentry->d_inode,
				mapping);
		up(&mapping->i_shared_sem);
	}
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

	audit_intercept(AUDIT_brk, brk);

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

	/* Ok, looks good - let it rip. */
	if (do_brk(oldbrk, newbrk-oldbrk) != oldbrk)
		goto out;
set_brk:
	mm->brk = brk;
out:
	retval = mm->brk;
	up_write(&mm->mmap_sem);
	return audit_lresult(retval);
}

#ifdef DEBUG_MM_RB
static int browse_rb(struct rb_root *root) {
	int i, j;
	struct rb_node *nd, *pn = NULL;
	i = 0;
	unsigned long prev = 0, pend = 0;

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct vm_area_struct *vma;
		vma = rb_entry(nd, struct vm_area_struct, vm_rb);
		if (vma->vm_start < prev)
			printk("vm_start %lx prev %lx\n", vma->vm_start, prev), i = -1;
		if (vma->vm_start < pend)
			printk("vm_start %lx pend %lx\n", vma->vm_start, pend);
		if (vma->vm_start > vma->vm_end)
			printk("vm_end %lx < vm_start %lx\n", vma->vm_end, vma->vm_start);
		i++;
		pn = nd;
	}
	j = 0;
	for (nd = pn; nd; nd = rb_prev(nd)) {
		j++;
	}
	if (i != j)
		printk("backwards %d, forwards %d\n", j, i), i = 0;
	return i;
}

void validate_mm(struct mm_struct * mm) {
	int bug = 0;
	int i = 0;
	struct vm_area_struct * tmp = mm->mmap;
	while (tmp) {
		tmp = tmp->vm_next;
		i++;
	}
	if (i != mm->map_count)
		printk("map_count %d vm_next %d\n", mm->map_count, i), bug = 1;
	i = browse_rb(&mm->mm_rb);
	if (i != mm->map_count)
		printk("map_count %d rb %d\n", mm->map_count, i), bug = 1;
	if (bug)
		BUG();
}
#else
#define validate_mm(mm) do { } while (0)
#endif

static struct vm_area_struct *
find_vma_prepare(struct mm_struct *mm, unsigned long addr,
		struct vm_area_struct **pprev, struct rb_node ***rb_link,
		struct rb_node ** rb_parent)
{
	struct vm_area_struct * vma;
	struct rb_node ** __rb_link, * __rb_parent, * rb_prev;

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

static inline void
__vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev, struct rb_node *rb_parent)
{
	if (prev) {
		vma->vm_next = prev->vm_next;
		prev->vm_next = vma;
	} else {
		mm->mmap = vma;
		if (rb_parent)
			vma->vm_next = rb_entry(rb_parent,
					struct vm_area_struct, vm_rb);
		else
			vma->vm_next = NULL;
	}
}

void __vma_link_rb(struct mm_struct *mm, struct vm_area_struct *vma,
		struct rb_node **rb_link, struct rb_node *rb_parent)
{
	rb_link_node(&vma->vm_rb, rb_parent, rb_link);
	rb_insert_color(&vma->vm_rb, &mm->mm_rb);
}

static inline void __vma_link_file(struct vm_area_struct *vma)
{
	struct file * file;

	file = vma->vm_file;
	if (file) {
		struct address_space *mapping = file->f_mapping;

		if (vma->vm_flags & VM_DENYWRITE)
			atomic_dec(&file->f_dentry->d_inode->i_writecount);

		if (unlikely(vma->vm_flags & VM_NONLINEAR)) {
			INIT_VMA_SHARED_LIST(vma);
			list_add_tail(&vma->shared.vm_set.list,
					&mapping->i_mmap_nonlinear);
		}
		else if (vma->vm_flags & VM_SHARED)
			__vma_prio_tree_insert(&mapping->i_mmap_shared, vma);
		else
			__vma_prio_tree_insert(&mapping->i_mmap, vma);
	}
}

static void
__vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, struct rb_node **rb_link,
	struct rb_node *rb_parent)
{
	__vma_link_list(mm, vma, prev, rb_parent);
	__vma_link_rb(mm, vma, rb_link, rb_parent);
	__vma_link_file(vma);
	__anon_vma_link(vma);
}

static void vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
			struct vm_area_struct *prev, struct rb_node **rb_link,
			struct rb_node *rb_parent)
{
	struct address_space *mapping = NULL;

	if (vma->vm_file)
		mapping = vma->vm_file->f_mapping;

	if (mapping)
		down(&mapping->i_shared_sem);
	anon_vma_lock(vma);
	__vma_link(mm, vma, prev, rb_link, rb_parent);
	anon_vma_unlock(vma);
	if (mapping)
		up(&mapping->i_shared_sem);

	mark_mm_hugetlb(mm, vma);
	mm->map_count++;
	validate_mm(mm);
}

/*
 * Insert vm structure into process list sorted by address and into the inode's
 * i_mmap ring. The caller should hold mm->page_table_lock and
 * ->f_mappping->i_shared_sem if vm_file is non-NULL.
 */
static void
__insert_vm_struct(struct mm_struct * mm, struct vm_area_struct * vma)
{
	struct vm_area_struct * __vma, * prev;
	struct rb_node ** rb_link, * rb_parent;

	__vma = find_vma_prepare(mm, vma->vm_start,&prev, &rb_link, &rb_parent);
	if (__vma && __vma->vm_start < vma->vm_end)
		BUG();
	__vma_link(mm, vma, prev, rb_link, rb_parent);
	mark_mm_hugetlb(mm, vma);
	mm->map_count++;
	validate_mm(mm);
}

/*
 * Return true if we can merge this (vm_flags,file,vm_pgoff,size)
 * in front of (at a lower virtual address and file offset than) the vma.
 *
 * We don't check here for the merged mmap wrapping around the end of pagecache
 * indices (16TB on ia32) because do_mmap_pgoff() does not permit mmap's which
 * wrap, nor mmaps which cover the final page at index -1UL.
 */
static int
can_vma_merge_before(struct vm_area_struct *prev,
		     struct vm_area_struct *vma, unsigned long vm_flags,
		     struct file *file, unsigned long vm_pgoff,
		     anon_vma_t ** anon_vma_cache)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_pgoff, anon_vma_cache)) {
		if (prev) {
			/*
			 * We can fill an hole only if the two
			 * anonymous mappings are queued in the same
			 * anon_vma, or if one of them is "direct"
			 * and it can be queued in the existing
			 * anon_vma.
			 *
			 * Must check this even if file != NULL
			 * for MAP_PRIVATE mappings.
			 */
			return is_mergeable_anon_vma(prev, vma);
		}
		return 1;
	}
	return 0;
}

/*
 * Return true if we can merge this (vm_flags,file,vm_pgoff)
 * beyond (at a higher virtual address and file offset than) the vma.
 */
static int
can_vma_merge_after(struct vm_area_struct *vma, unsigned long vm_flags,
		    struct file *file, unsigned long vm_pgoff,
		    anon_vma_t ** anon_vma_cache)
{
	unsigned long vma_size = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	return is_mergeable_vma(vma, file, vm_flags, vm_pgoff - vma_size, anon_vma_cache);
}

/*
 * Given a new mapping request (addr,end,vm_flags,file,pgoff), figure out
 * whether that can be merged with its predecessor or its successor.  Or
 * both (it neatly fills a hole).
 */
static int vma_merge(struct mm_struct *mm, struct vm_area_struct *prev,
		     struct rb_node *rb_parent, unsigned long addr, 
		     unsigned long end, unsigned long vm_flags,
		     struct file *file, unsigned long pgoff,
		     anon_vma_t ** anon_vma_cache, struct mempolicy *policy)
{
	struct inode *inode;
	struct address_space *mapping;
	struct semaphore *i_shared_sem;
	struct prio_tree_root *root;

	/*
	 * We later require that vma->vm_flags == vm_flags, so this tests
	 * vma->vm_flags & VM_SPECIAL, too.
	 */
	if (vm_flags & VM_SPECIAL)
		return 0;

	/*
	 * Only "root" and "inode" have to be NULL too if "file" is null,
	 * however mapping and i_shared_sem would cause gcc to warn about
	 * uninitialized usage so we set them to NULL too.
	 */
	inode = NULL;
	root = NULL;
	i_shared_sem = NULL;
	mapping = NULL;
	if (file) {
		inode = file->f_dentry->d_inode;
		mapping = file->f_mapping;
		i_shared_sem = &mapping->i_shared_sem;

		if (vm_flags & VM_SHARED) {
			if (likely(!(vm_flags & VM_NONLINEAR)))
				root = &mapping->i_mmap_shared;
		} else
			root = &mapping->i_mmap;
	}

	if (!prev) {
		prev = rb_entry(rb_parent, struct vm_area_struct, vm_rb);
		goto merge_next;
	}

	/*
	 * Can it merge with the predecessor?
	 */
	if (prev->vm_end == addr &&
	    mpol_equal(vma_policy(prev), policy) && 
	    can_vma_merge_after(prev, vm_flags, file, pgoff, anon_vma_cache)) {
		struct vm_area_struct *next;

		/*
		 * OK, it did.  Can we now merge in the successor as well?
		 */
		next = prev->vm_next;
		/* next cannot change under us, it's serialized by the mmap_sem */
		if (next && end == next->vm_start &&
		    vma_mpol_equal(prev, next) &&
		    can_vma_merge_before(prev, next, vm_flags, file,
					 pgoff + ((end - addr) >> PAGE_SHIFT),
					 anon_vma_cache)) {
			/* serialized by the mmap_sem */
			__vma_unlink(mm, next, prev);

			if (file)
				down(i_shared_sem);
			__vma_modify(root, prev, prev->vm_start,
					next->vm_end, prev->vm_pgoff);

			__remove_shared_vm_struct(next, inode, mapping);
			if (file)
				up(i_shared_sem);

			/*
			 * The anon_vma_lock is taken inside and
			 * we can race with the vm_end move on the right,
			 * that will not be a problem, moves on the right
			 * of vm_end are controlled races.
			 */
			anon_vma_merge(prev, next);

			if (file)
				fput(file);

			mm->map_count--;
			mpol_free(vma_policy(next));
			kmem_cache_free(vm_area_cachep, next);
			return 1;
		}

		/*
		 * this can happen outside the anon_vma_lock since it only
		 * enlarge the size of the vma, there are no ptes mapped in
		 * this new extended region anyways. As usual this is a move
		 * on the right of the vm_end.
		 */
		if (file)
			down(i_shared_sem);
		__vma_modify(root, prev, prev->vm_start, end, prev->vm_pgoff);
		if (file)
			up(i_shared_sem);

		return 1;
	}

	/*
	 * Can this new request be merged in front of prev->vm_next?
	 */
	prev = prev->vm_next;
	if (prev) {
 merge_next:
  		if (!mpol_equal(policy, vma_policy(prev)))
   			return 0;
		if (end == prev->vm_start &&
		    can_vma_merge_before(NULL, prev, vm_flags, file,
					 pgoff + ((end - addr) >> PAGE_SHIFT),
					 anon_vma_cache)) {
			if (file)
				down(i_shared_sem);
			anon_vma_lock(prev);
			__vma_modify(root, prev, addr, prev->vm_end,
				prev->vm_pgoff - ((end - addr) >> PAGE_SHIFT));
			anon_vma_unlock(prev);
			if (file)
				up(i_shared_sem);
			return 1;
		}
	}

	return 0;
}

/*
 * The caller must hold down_write(current->mm->mmap_sem).
 */

unsigned long __finish_do_mmap_pgoff(struct mm_struct *mm, struct file * file, 
			    unsigned long addr, unsigned long len,
			    unsigned long prot, unsigned long flags,
			    unsigned long pgoff)
{
	struct vm_area_struct * vma, * prev;
	struct inode *inode;
	unsigned int vm_flags;
	int correct_wcount = 0;
	int error;
	struct rb_node ** rb_link, * rb_parent;
	int accountable = 1;
	unsigned long charged = 0;
	anon_vma_t * anon_vma_cache;

	/* Obtain the address to map to. we verify (or select) it and
	 * ensure that it represents a valid section of the address
	 * space.  VM_HUGETLB will never appear in vm_flags when
	 * CONFIG_HUGETLB is unset.
	 */
	addr = get_unmapped_area(file, addr, len, pgoff, flags);
	if (addr & ~PAGE_MASK)
		return addr;

	/* Huge pages aren't accounted for here */
	if (file && is_file_hugepages(file))
		accountable = 0;

	/* Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */
	vm_flags = calc_vm_prot_bits(prot) | calc_vm_flag_bits(flags) |
			mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	if (flags & MAP_LOCKED) {
		if (!can_do_mlock())
			return -EPERM;
		vm_flags |= VM_LOCKED;
	}
	/* mlock MCL_FUTURE? */
	if (vm_flags & VM_LOCKED) {
		unsigned long locked = mm->locked_vm << PAGE_SHIFT;
		locked += len;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			return -EAGAIN;
	}

	inode = file ? file->f_dentry->d_inode : NULL;

	if (file) {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if ((prot&PROT_WRITE) && !(file->f_mode&FMODE_WRITE))
				return -EACCES;

			/*
			 * Make sure we don't allow writing to an append-only
			 * file..
			 */
			if (IS_APPEND(inode) && (file->f_mode & FMODE_WRITE))
				return -EACCES;

			/*
			 * Make sure there are no mandatory locks on the file.
			 */
			if (locks_verify_locked(inode))
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
			pgoff = addr >> PAGE_SHIFT;
			break;
		case MAP_SHARED:
			break;
		}
	}

	error = security_file_mmap(file, prot, flags);
	if (error)
		return error;
		
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

	if (accountable && (!(flags & MAP_NORESERVE) ||
			sysctl_overcommit_memory > 1)) {
		if (vm_flags & VM_SHARED) {
			/* Check memory availability in shmem_file_setup? */
			vm_flags |= VM_ACCOUNT;
		} else if (vm_flags & VM_WRITE) {
			/*
			 * Private writable mapping: check memory availability
			 */
			charged = len >> PAGE_SHIFT;
			if (security_vm_enough_memory(charged))
				return -ENOMEM;
			vm_flags |= VM_ACCOUNT;
		}
	}

	/* Can we just expand an old anonymous mapping? */
	anon_vma_cache = NULL;
	if (!file && !(vm_flags & VM_SHARED) && rb_parent)
		if (vma_merge(mm, prev, rb_parent, addr, addr + len,
					vm_flags, NULL, pgoff, &anon_vma_cache, NULL))
			goto out;

	/*
	 * Determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	error = -ENOMEM;
	if (!vma)
		goto unacct_error;

 	mpol_set_vma_default(vma);
	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = vm_flags;
	vma->vm_page_prot = protection_map[vm_flags & 0x0f];
	vma->vm_ops = NULL;
	vma->vm_pgoff = pgoff;
	vma->vm_file = NULL;
	vma->vm_private_data = NULL;
	vma->vm_next = NULL;
	INIT_VMA_SHARED(vma);

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
	} else if (vm_flags & VM_SHARED) {
		error = shmem_zero_setup(vma);
		if (error)
			goto free_vma;
	}

	/* We set VM_ACCOUNT in a shared mapping's vm_flags, to inform
	 * shmem_zero_setup (perhaps called through /dev/zero's ->mmap)
	 * that memory reservation must be checked; but that reservation
	 * belongs to shared memory object, not to vma: so now clear it.
	 */
	if ((vm_flags & (VM_SHARED|VM_ACCOUNT)) == (VM_SHARED|VM_ACCOUNT))
		vma->vm_flags &= ~VM_ACCOUNT;

	/* Can addr have changed??
	 *
	 * Answer: Yes, several device drivers can do it in their
	 *         f_op->mmap method. -DaveM
	 */
	addr = vma->vm_start;

	if (!file || !rb_parent || !vma_merge(mm, prev, rb_parent, addr,
					      addr + len, vma->vm_flags, file, pgoff,
					      &anon_vma_cache, vma_policy(vma))) {
		vma->anon_vma = anon_vma_cache;
		vma_link(mm, vma, prev, rb_link, rb_parent);
		if (correct_wcount)
			atomic_inc(&inode->i_writecount);
	} else {
		if (file) {
			if (correct_wcount)
				atomic_inc(&inode->i_writecount);
			fput(file);
		}
		mpol_free(vma_policy(vma));
		kmem_cache_free(vm_area_cachep, vma);
	}
out:	
	mm->total_vm += len >> PAGE_SHIFT;
	if (vm_flags & VM_LOCKED) {
		mm->locked_vm += len >> PAGE_SHIFT;
		make_pages_present(addr, addr + len);
	}
	if (flags & MAP_POPULATE) {
		up_write(&mm->mmap_sem);
		sys_remap_file_pages(addr, len, prot,
					pgoff, flags & MAP_NONBLOCK);
		down_write(&mm->mmap_sem);
	}
	return addr;

unmap_and_free_vma:
	if (correct_wcount)
		atomic_inc(&inode->i_writecount);

	/*
	 * Undo any partial mapping done by a device driver.  
	 * hugetlb wants to know the vma's file etc. so nuke  
	 * the file afterward.                                
	 */                                                   
	zap_page_range(vma, vma->vm_start, vma->vm_end - vma->vm_start);

	if (file)
		fput(vma->vm_file); 

free_vma:
	kmem_cache_free(vm_area_cachep, vma);
unacct_error:
	if (charged)
		vm_unacct_memory(charged);
	return error;
}

#ifdef CONFIG_HUGETLBFS
static int mmap_hugetlb_implicit(unsigned long len)
{
	/* Are we enabled? */
	if (!mmap_use_hugepages)
		return 0;
	/* Must be HPAGE aligned */
	if (len & ~HPAGE_MASK)
		return 0;
	/* Are we capable ? */
	if (!capable(CAP_IPC_LOCK))
		return -EPERM;
	/* Are we under the minimum size? */
	if (mmap_hugepages_map_sz
		&& len < (mmap_hugepages_map_sz << 20))
		return 0;
	/* Do we have enough huge pages ? */
	if (!is_hugepage_mem_enough(len))
		return 0;

	return 1;
}
#else
static inline int mmap_hugetlb_implicit(unsigned long len)
{
	return 0;
}
#endif /* CONFIG_HUGETLBFS */

/*
 * The caller must hold down_write(current->mm->mmap_sem).
 */
unsigned long __do_mmap_pgoff(struct mm_struct *mm, 
		struct file * file, unsigned long addr,
		unsigned long len, unsigned long prot,
		unsigned long flags, unsigned long pgoff)
{
	struct file *hugetlb_file = NULL;
	int hugetlb_implicit = 0;
	unsigned long result;

	if (file) {
		if ((flags & MAP_HUGETLB) && !is_file_hugepages(file))
			return -EINVAL;

		if (!file->f_op || !file->f_op->mmap)
			return -ENODEV;

		if ((prot & PROT_EXEC) && (file->f_vfsmnt->mnt_flags & MNT_NOEXEC))
			return -EPERM;
	}

	if (!len)
		return addr;

	/* Careful about overflows.. */
	len = PAGE_ALIGN(len);
	if (!len || len > TASK_SIZE)
		return -EINVAL;

	/* offset overflow? */
	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EINVAL;

	/* Too many mappings? */
	if (current->mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	/* Create an implicit hugetlb file if necessary */
	if (!file && ((flags & MAP_HUGETLB) ||
			(hugetlb_implicit = mmap_hugetlb_implicit(len)))) {
		file = hugetlb_file = hugetlb_zero_setup(len);
		if (IS_ERR(file)) {
			if (!hugetlb_implicit)
				return PTR_ERR(file);
			file = hugetlb_file = NULL;
			hugetlb_implicit = 0;
		}
	}

again:
	result = __finish_do_mmap_pgoff(mm, file, addr, len, prot, flags, pgoff);

	/* Drop reference to implicit hugetlb file, it's already been
	 * "gotten" in __finish_do_mmap_pgoff in case of success
	 */
	if (hugetlb_file)
		fput(hugetlb_file);

	/* If implicit huge tlb & we failed, try again without */
	if ((result & ~PAGE_MASK) && hugetlb_implicit) {
		hugetlb_implicit = 0;
		file = NULL;
		goto again;
	}

	return result;
}

EXPORT_SYMBOL(__do_mmap_pgoff);

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
static inline unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	start_addr = addr = mm->free_area_cache;

full_search:
	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = addr = TASK_UNMAPPED_BASE;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (!vma || addr + len <= vma->vm_start) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		addr = vma->vm_end;
	}
}
#else
extern unsigned long
arch_get_unmapped_area(struct file *, unsigned long, unsigned long,
			unsigned long, unsigned long);
#endif	

unsigned long
get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	if (flags & MAP_FIXED) {
		unsigned long ret;

		if (addr > TASK_SIZE - len)
			return -ENOMEM;
		if (addr & ~PAGE_MASK)
			return -EINVAL;
		if (file && is_file_hugepages(file))  {
			/*
			 * Check if the given range is hugepage aligned, and
			 * can be made suitable for hugepages.
			 */
			ret = prepare_hugepage_range(addr, len);
		} else {
			/*
			 * Ensure that a normal request is not falling in a
			 * reserved hugepage range.  For some archs like IA-64,
			 * there is a separate region for hugepages.
			 */
			ret = is_hugepage_only_range(addr, len);
		}
		if (ret)
			return -EINVAL;
		return addr;
	}

	if (file && file->f_op && file->f_op->get_unmapped_area)
		return file->f_op->get_unmapped_area(file, addr, len,
						pgoff, flags);

	return arch_get_unmapped_area(file, addr, len, pgoff, flags);
}

EXPORT_SYMBOL(get_unmapped_area);

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr)
{
	struct vm_area_struct *vma = NULL;

	if (mm) {
		/* Check the cache first. */
		/* (Cache hit rate is typically around 35%.) */
		vma = mm->mmap_cache;
		if (!(vma && vma->vm_end > addr && vma->vm_start <= addr)) {
			struct rb_node * rb_node;

			rb_node = mm->mm_rb.rb_node;
			vma = NULL;

			while (rb_node) {
				struct vm_area_struct * vma_tmp;

				vma_tmp = rb_entry(rb_node,
						struct vm_area_struct, vm_rb);

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

EXPORT_SYMBOL(find_vma);

/* Same as find_vma, but also return a pointer to the previous VMA in *pprev. */
struct vm_area_struct *
find_vma_prev(struct mm_struct *mm, unsigned long addr,
			struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma = NULL, *prev = NULL;
	struct rb_node * rb_node;
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

#ifdef CONFIG_STACK_GROWSUP
/*
 * vma is the first one with address > vma->vm_end.  Have to extend vma.
 */
int expand_stack(struct vm_area_struct * vma, unsigned long address)
{
	unsigned long grow;

	if (!(vma->vm_flags & VM_GROWSUP))
		return -EFAULT;

	/*
	 * We must make sure the anon-vma is allocated
	 * to make sure the anon-vma locking is not a noop.
	 */
	if (unlikely(anon_vma_prepare(vma)))
		return -ENOMEM;

	anon_vma_lock(vma);

	/*
	 * vma->vm_start/vm_end cannot change under us because the caller
	 * is required to hold the mmap_sem in read mode. We need to get
	 * the spinlock only before relocating the vma range ourself.
	 */
	address += 4 + PAGE_SIZE - 1;
	address &= PAGE_MASK;
	grow = (address - vma->vm_end) >> PAGE_SHIFT;

	/* Overcommit.. */
	if (unlikely(security_vm_enough_memory(grow))) {
		anon_vma_unlock(vma);
		return -ENOMEM;
	}
	
	if (unlikely(address - vma->vm_start > current->rlim[RLIMIT_STACK].rlim_cur ||
		     ((vma->vm_mm->total_vm + grow) << PAGE_SHIFT) >
		     current->rlim[RLIMIT_AS].rlim_cur)) {
		anon_vma_unlock(vma);
		vm_unacct_memory(grow);
		return -ENOMEM;
	}
	vma->vm_end = address;
	vma->vm_mm->total_vm += grow;
	if (vma->vm_flags & VM_LOCKED)
		vma->vm_mm->locked_vm += grow;

	anon_vma_unlock(vma);

	return 0;
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)
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
/*
 * vma is the first one with address < vma->vm_start.  Have to extend vma.
 */
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	unsigned long grow;

	/*
	 * We must make sure the anon-vma is allocated
	 * to make sure the anon-vma locking is not a noop.
	 */
	if (unlikely(anon_vma_prepare(vma)))
		return -ENOMEM;

	/*
	 * We must serialize against other thread and against
	 * objrmap while moving the vm_start/vm_pgoff of anon-vmas.
	 * The total_vm/locked_vm as well needs serialization
	 * against other threads, the serialization of
	 * locked_vm/total_vm against syscalls is provided by
	 * the mmap_sem that we hold in read mode here (all
	 * syscalls holds it in write mode).
	 */
	anon_vma_lock(vma);

	/*
	 * vma->vm_start/vm_end cannot change under us because the caller
	 * is required to hold the mmap_sem in read mode. We need to get
	 * the spinlock only before relocating the vma range ourself.
	 */
	address &= PAGE_MASK;
	grow = (vma->vm_start - address) >> PAGE_SHIFT;

	/* Overcommit.. */
	if (unlikely(security_vm_enough_memory(grow))) {
		anon_vma_unlock(vma);
		return -ENOMEM;
	}
	
	if (unlikely(vma->vm_end - address > current->rlim[RLIMIT_STACK].rlim_cur ||
		     ((vma->vm_mm->total_vm + grow) << PAGE_SHIFT) >
		     current->rlim[RLIMIT_AS].rlim_cur)) {
		anon_vma_unlock(vma);
		vm_unacct_memory(grow);
		return -ENOMEM;
	}
	vma->vm_start = address;
	vma->vm_pgoff -= grow;
	vma->vm_mm->total_vm += grow;
	if (vma->vm_flags & VM_LOCKED)
		vma->vm_mm->locked_vm += grow;

	anon_vma_unlock(vma);

	return 0;
}

struct vm_area_struct *
find_extend_vma(struct mm_struct * mm, unsigned long addr)
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
static void free_pgtables(struct mmu_gather *tlb, struct vm_area_struct *prev,
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
	if (last < first)	/* for arches with discontiguous pgd indices */
		return;
	/*
	 * If the PGD bits are not consecutive in the virtual address, the
	 * old method of shifting the VA >> by PGDIR_SHIFT doesn't work.
	 */
	start_index = pgd_index(first);
	if (start_index < FIRST_USER_PGD_NR)
		start_index = FIRST_USER_PGD_NR;
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
	/*
	 * Is this a new hole at the lowest possible address?
	 */
	if (area->vm_start >= TASK_UNMAPPED_BASE &&
				area->vm_start < area->vm_mm->free_area_cache)
	      area->vm_mm->free_area_cache = area->vm_start;

	remove_shared_vm_struct(area);

	mpol_free(vma_policy(area));
	if (area->vm_ops && area->vm_ops->close)
		area->vm_ops->close(area);
	if (area->vm_file)
		fput(area->vm_file);

	anon_vma_unlink(area);

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
	struct vm_area_struct *vma,
	struct vm_area_struct *prev,
	unsigned long start,
	unsigned long end)
{
	struct mmu_gather *tlb;
	unsigned long nr_accounted = 0;

	lru_add_drain();
	tlb = tlb_gather_mmu(mm, 0);
	unmap_vmas(&tlb, mm, vma, start, end, &nr_accounted);
	vm_unacct_memory(nr_accounted);

	if (is_hugepage_only_range(start, end - start))
		hugetlb_free_pgtables(tlb, prev, start, end);
	else
		free_pgtables(tlb, prev, start, end);
	tlb_finish_mmu(tlb, start, end);
}

/*
 * Create a list of vma's touched by the unmap, removing them from the mm's
 * vma list as we go..
 */
static void
detach_vmas_to_be_unmapped(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, unsigned long end)
{
	struct vm_area_struct **insertion_point;
	struct vm_area_struct *tail_vma = NULL;

	insertion_point = (prev ? &prev->vm_next : &mm->mmap);
	do {
		rb_erase(&vma->vm_rb, &mm->mm_rb);
		mm->map_count--;
		tail_vma = vma;
		vma = vma->vm_next;
	} while (vma && vma->vm_start < end);
	*insertion_point = vma;
	tail_vma->vm_next = NULL;
	mm->mmap_cache = NULL;		/* Kill the cache. */
}

/*
 * Split a vma into two pieces at address 'addr', a new vma is allocated
 * either for the first part or the the tail.
 */
int split_vma(struct mm_struct * mm, struct vm_area_struct * vma,
	      unsigned long addr, int new_below)
{
	struct mempolicy *pol;
	struct vm_area_struct *new;
	struct address_space *mapping = NULL;
	struct prio_tree_root *root = NULL;

	if (mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	new = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!new)
		return -ENOMEM;

	/* most fields are the same, copy all, and then fixup */
	*new = *vma;

	INIT_VMA_SHARED(new);

	if (new_below)
		new->vm_end = addr;
	else {
		new->vm_start = addr;
		new->vm_pgoff += ((addr - vma->vm_start) >> PAGE_SHIFT);
	}

	pol = mpol_copy(vma_policy(vma)); 
	if (IS_ERR(pol)) { 
		kmem_cache_free(vm_area_cachep, new); 
		return PTR_ERR(pol);
	} 
	vma_set_policy(new, pol);

	if (new->vm_file)
		get_file(new->vm_file);

	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	if (vma->vm_file) {
		 mapping = vma->vm_file->f_mapping;

		 if (vma->vm_flags & VM_SHARED) {
			 if (likely(!(vma->vm_flags & VM_NONLINEAR)))
			 	root = &mapping->i_mmap_shared;
		 }
		 else
			 root = &mapping->i_mmap;
	}

	if (mapping)
		down(&mapping->i_shared_sem);
	anon_vma_lock(vma);

	if (new_below)
		__vma_modify(root, vma, addr, vma->vm_end,
			vma->vm_pgoff + ((addr - new->vm_start) >> PAGE_SHIFT));
	else
		__vma_modify(root, vma, vma->vm_start, addr, vma->vm_pgoff);

	__insert_vm_struct(mm, new);

	anon_vma_unlock(vma);
	if (mapping)
		up(&mapping->i_shared_sem);

	return 0;
}

/* Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardinge <jeremy@goop.org>
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

	if (is_vm_hugetlb_page(mpnt)) {
		int ret = is_aligned_hugepage_range(start, len);

		if (ret)
			return ret;
	}

	/* if it doesn't overlap, we have nothing.. */
	end = start + len;
	if (mpnt->vm_start >= end)
		return 0;

	/* Something will probably happen, so notify. */
	if (mpnt->vm_file && (mpnt->vm_flags & VM_EXEC))
		profile_exec_unmap(mm);
 
	/*
	 * If we need to split any vma, do it now to save pain later.
	 *
	 * Note: mremap's move_vma VM_ACCOUNT handling assumes a partially
	 * unmapped vm_area_struct will remain in use: so lower split_vma
	 * places tmp vma above, and higher split_vma places tmp vma below.
	 */
	if (start > mpnt->vm_start) {
		if (split_vma(mm, mpnt, start, 0))
			return -ENOMEM;
		prev = mpnt;
	}

	/* Does it split the last one? */
	last = find_vma(mm, end);
	if (last && end > last->vm_start) {
		if (split_vma(mm, last, end, 1))
			return -ENOMEM;
	}
	mpnt = prev? prev->vm_next: mm->mmap;

	/*
	 * Remove the vma's, and unmap the actual pages
	 */
	detach_vmas_to_be_unmapped(mm, mpnt, prev, end);
	spin_lock(&mm->page_table_lock);
	unmap_region(mm, mpnt, prev, start, end);
	spin_unlock(&mm->page_table_lock);

	/* Fix up all other VM information */
	unmap_vma_list(mm, mpnt);

	return 0;
}

EXPORT_SYMBOL(do_munmap);

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
	struct rb_node ** rb_link, * rb_parent;
	unsigned long pgoff;
	anon_vma_t * anon_vma_cache;

	len = PAGE_ALIGN(len);
	if (!len)
		return addr;

	if ((addr + len) > TASK_SIZE || (addr + len) < addr)
		return -EINVAL;

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

	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	if (security_vm_enough_memory(len >> PAGE_SHIFT))
		return -ENOMEM;

	flags = VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;

	pgoff = addr >> PAGE_SHIFT;

	/* Can we just expand an old anonymous mapping? */
	anon_vma_cache = NULL;
	if (rb_parent && vma_merge(mm, prev, rb_parent, addr, addr + len,
				   flags, NULL, pgoff, &anon_vma_cache, NULL))
		goto out;

	/*
	 * create a vma struct for an anonymous mapping
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma) {
		vm_unacct_memory(len >> PAGE_SHIFT);
		return -ENOMEM;
	}
 	mpol_set_vma_default(vma);
	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = flags;
	vma->vm_page_prot = protection_map[flags & 0x0f];
	vma->vm_ops = NULL;
	vma->vm_pgoff = pgoff;
	vma->vm_file = NULL;
	vma->vm_private_data = NULL;
	INIT_VMA_SHARED(vma);
	vma->anon_vma = anon_vma_cache;

	vma_link(mm, vma, prev, rb_link, rb_parent);

out:
	mm->total_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED) {
		mm->locked_vm += len >> PAGE_SHIFT;
		make_pages_present(addr, addr + len);
	}
	return addr;
}

EXPORT_SYMBOL(do_brk);

/* Release all mmaps. */
void exit_mmap(struct mm_struct *mm)
{
	struct mmu_gather *tlb;
	struct vm_area_struct *vma;
	unsigned long nr_accounted = 0;

	profile_exit_mmap(mm);
 
	lru_add_drain();

	spin_lock(&mm->page_table_lock);

	tlb = tlb_gather_mmu(mm, 1);
	flush_cache_mm(mm);
	/* Use ~0UL here to ensure all VMAs in the mm are unmapped */
	mm->map_count -= unmap_vmas(&tlb, mm, mm->mmap, 0,
					~0UL, &nr_accounted);
	vm_unacct_memory(nr_accounted);
	BUG_ON(mm->map_count);	/* This is just debugging */
	clear_page_tables(tlb, FIRST_USER_PGD_NR, USER_PTRS_PER_PGD);
	tlb_finish_mmu(tlb, 0, MM_VM_SIZE(mm));

	vma = mm->mmap;
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
	while (vma) {
		struct vm_area_struct *next = vma->vm_next;
		remove_shared_vm_struct(vma);
		if (vma->vm_ops) {
			if (vma->vm_ops->close)
				vma->vm_ops->close(vma);
		}
 		mpol_free(vma_policy(vma));
		if (vma->vm_file)
			fput(vma->vm_file);
		anon_vma_unlink(vma);
		kmem_cache_free(vm_area_cachep, vma);
		vma = next;
	}
}

/* Insert vm structure into process list sorted by address
 * and into the inode's i_mmap ring.  If vm_file is non-NULL
 * then i_shared_sem is taken here.
 */
void insert_vm_struct(struct mm_struct * mm, struct vm_area_struct * vma)
{
	struct vm_area_struct * __vma, * prev;
	struct rb_node ** rb_link, * rb_parent;

	__vma = find_vma_prepare(mm,vma->vm_start,&prev,&rb_link,&rb_parent);
	if (__vma && __vma->vm_start < vma->vm_end)
		BUG();
	vma_link(mm, vma, prev, rb_link, rb_parent);
	validate_mm(mm);
}
