/*
 *  linux/mm/nommu.c
 *
 *  Replacement code for mm functions to support CPU's that don't
 *  have any form of memory management unit (thus no virtual memory).
 *
 *  Copyright (c) 2000-2003 David McCullough <davidm@snapgear.com>
 *  Copyright (c) 2000-2001 D Jeff Dionne <jeff@uClinux.org>
 *  Copyright (c) 2002      Greg Ungerer <gerg@snapgear.com>
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>

#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

void *high_memory;
struct page *mem_map;
unsigned long max_mapnr;
unsigned long num_physpages;
unsigned long askedalloc, realalloc;
atomic_t vm_committed_space = ATOMIC_INIT(0);
int sysctl_overcommit_memory; /* default is heuristic overcommit */
int sysctl_overcommit_ratio = 50; /* default is 50% */

int sysctl_max_map_count = DEFAULT_MAX_MAP_COUNT;
EXPORT_SYMBOL(sysctl_max_map_count);

/*
 * Handle all mappings that got truncated by a "truncate()"
 * system call.
 *
 * NOTE! We have to be ready to update the memory sharing
 * between the file and the memory map for a potential last
 * incomplete page.  Ugly, but necessary.
 */
int vmtruncate(struct inode *inode, loff_t offset)
{
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	if (inode->i_size < offset)
		goto do_expand;
	i_size_write(inode, offset);

	truncate_inode_pages(mapping, offset);
	goto out_truncate;

do_expand:
	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY && offset > limit)
		goto out_sig;
	if (offset > inode->i_sb->s_maxbytes)
		goto out;
	i_size_write(inode, offset);

out_truncate:
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out:
	return -EFBIG;
}

/*
 * Return the total memory allocated for this pointer, not
 * just what the caller asked for.
 *
 * Doesn't have to be accurate, i.e. may have races.
 */
unsigned int kobjsize(const void *objp)
{
	struct page *page;

	if (!objp || !((page = virt_to_page(objp))))
		return 0;

	if (PageSlab(page))
		return ksize(objp);

	BUG_ON(page->index < 0);
	BUG_ON(page->index >= MAX_ORDER);

	return (PAGE_SIZE << page->index);
}

/*
 * The nommu dodgy version :-)
 */
int get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
	unsigned long start, int len, int write, int force,
	struct page **pages, struct vm_area_struct **vmas)
{
	int i;
	static struct vm_area_struct dummy_vma;

	for (i = 0; i < len; i++) {
		if (pages) {
			pages[i] = virt_to_page(start);
			if (pages[i])
				page_cache_get(pages[i]);
		}
		if (vmas)
			vmas[i] = &dummy_vma;
		start += PAGE_SIZE;
	}
	return(i);
}

rwlock_t vmlist_lock = RW_LOCK_UNLOCKED;
struct vm_struct *vmlist;

void vfree(void *addr)
{
	kfree(addr);
}

void *__vmalloc(unsigned long size, int gfp_mask, pgprot_t prot)
{
	/*
	 * kmalloc doesn't like __GFP_HIGHMEM for some reason
	 */
	return kmalloc(size, gfp_mask & ~__GFP_HIGHMEM);
}

struct page * vmalloc_to_page(void *addr)
{
	return virt_to_page(addr);
}

long vread(char *buf, char *addr, unsigned long count)
{
	memcpy(buf, addr, count);
	return count;
}

long vwrite(char *buf, char *addr, unsigned long count)
{
	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;
	
	memcpy(addr, buf, count);
	return(count);
}

/*
 *	vmalloc  -  allocate virtually continguos memory
 *
 *	@size:		allocation size
 *
 *	Allocate enough pages to cover @size from the page level
 *	allocator and map them into continguos kernel virtual space.
 *
 *	For tight cotrol over page level allocator and protection flags
 *	use __vmalloc() instead.
 */
void *vmalloc(unsigned long size)
{
       return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL);
}

/*
 *	vmalloc_32  -  allocate virtually continguos memory (32bit addressable)
 *
 *	@size:		allocation size
 *
 *	Allocate enough 32bit PA addressable pages to cover @size from the
 *	page level allocator and map them into continguos kernel virtual space.
 */
void *vmalloc_32(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL, PAGE_KERNEL);
}

void *vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot)
{
	BUG();
	return NULL;
}

void vunmap(void *addr)
{
	BUG();
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
	struct mm_struct *mm = current->mm;

	if (brk < mm->end_code || brk < mm->start_brk || brk > mm->context.end_brk)
		return mm->brk;

	if (mm->brk == brk)
		return mm->brk;

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= mm->brk) {
		mm->brk = brk;
		return brk;
	}

	/*
	 * Ok, looks good - let it rip.
	 */
	return mm->brk = brk;
}

/*
 * Combine the mmap "prot" and "flags" argument into one "vm_flags" used
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

#ifdef DEBUG
static void show_process_blocks(void)
{
	struct mm_tblock_struct *tblock;

	printk("Process blocks %d:", current->pid);

	for (tblock = &current->mm->context.tblock; tblock; tblock = tblock->next) {
		printk(" %p: %p", tblock, tblock->rblock);
		if (tblock->rblock)
			printk(" (%d @%p #%d)", kobjsize(tblock->rblock->kblock), tblock->rblock->kblock, tblock->rblock->refcount);
		printk(tblock->next ? " ->" : ".\n");
	}
}
#endif /* DEBUG */

unsigned long do_mmap_pgoff(
	struct file * file,
	unsigned long addr,
	unsigned long len,
	unsigned long prot,
	unsigned long flags,
	unsigned long pgoff)
{
	void * result;
	struct mm_tblock_struct * tblock;
	unsigned int vm_flags;

	/*
	 * Get the !CONFIG_MMU specific checks done first
	 */
	if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && (file)) {
		printk("MAP_SHARED not supported (cannot write mappings to disk)\n");
		return -EINVAL;
	}
	
	if ((prot & PROT_WRITE) && (flags & MAP_PRIVATE)) {
		printk("Private writable mappings not supported\n");
		return -EINVAL;
	}
	
	/*
	 *	now all the standard checks
	 */
	if (file && (!file->f_op || !file->f_op->mmap))
		return -ENODEV;

	if (PAGE_ALIGN(len) == 0)
		return addr;

	if (len > TASK_SIZE)
		return -EINVAL;

	/* offset overflow? */
	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EINVAL;

	/* Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */
	vm_flags = calc_vm_flags(prot,flags) /* | mm->def_flags */ | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	/*
	 * determine the object being mapped and call the appropriate
	 * specific mapper. 
	 */
	if (file) {
		struct vm_area_struct vma;
		int error;

		if (!file->f_op)
			return -ENODEV;

		vma.vm_start = addr;
		vma.vm_end = addr + len;
		vma.vm_flags = vm_flags;
		vma.vm_pgoff = pgoff;

#ifdef MAGIC_ROM_PTR
		/* First, try simpler routine designed to give us a ROM pointer. */

		if (file->f_op->romptr && !(prot & PROT_WRITE)) {
			error = file->f_op->romptr(file, &vma);
#ifdef DEBUG
			printk("romptr mmap returned %d, start 0x%.8x\n", error,
					vma.vm_start);
#endif
			if (!error)
				return vma.vm_start;
			else if (error != -ENOSYS)
				return error;
		} else
#endif /* MAGIC_ROM_PTR */
		/* Then try full mmap routine, which might return a RAM pointer,
		   or do something truly complicated. */
		   
		if (file->f_op->mmap) {
			error = file->f_op->mmap(file, &vma);
				   
#ifdef DEBUG
			printk("f_op->mmap() returned %d/%lx\n", error, vma.vm_start);
#endif
			if (!error)
				return vma.vm_start;
			else if (error != -ENOSYS)
				return error;
		} else
			return -ENODEV; /* No mapping operations defined */

		/* An ENOSYS error indicates that mmap isn't possible (as opposed to
		   tried but failed) so we'll fall through to the copy. */
	}

	tblock = (struct mm_tblock_struct *)
                        kmalloc(sizeof(struct mm_tblock_struct), GFP_KERNEL);
	if (!tblock) {
		printk("Allocation of tblock for %lu byte allocation from process %d failed\n", len, current->pid);
		show_free_areas();
		return -ENOMEM;
	}

	tblock->rblock = (struct mm_rblock_struct *)
			kmalloc(sizeof(struct mm_rblock_struct), GFP_KERNEL);

	if (!tblock->rblock) {
		printk("Allocation of rblock for %lu byte allocation from process %d failed\n", len, current->pid);
		show_free_areas();
		kfree(tblock);
		return -ENOMEM;
	}

	result = kmalloc(len, GFP_KERNEL);
	if (!result) {
		printk("Allocation of length %lu from process %d failed\n", len,
				current->pid);
		show_free_areas();
		kfree(tblock->rblock);
		kfree(tblock);
		return -ENOMEM;
	}

	tblock->rblock->refcount = 1;
	tblock->rblock->kblock = result;
	tblock->rblock->size = len;
	
	realalloc += kobjsize(result);
	askedalloc += len;

#ifdef WARN_ON_SLACK	
	if ((len+WARN_ON_SLACK) <= kobjsize(result))
		printk("Allocation of %lu bytes from process %d has %lu bytes of slack\n", len, current->pid, kobjsize(result)-len);
#endif
	
	if (file) {
		int error;
		mm_segment_t old_fs = get_fs();
		set_fs(KERNEL_DS);
		error = file->f_op->read(file, (char *) result, len, &file->f_pos);
		set_fs(old_fs);
		if (error < 0) {
			kfree(result);
			kfree(tblock->rblock);
			kfree(tblock);
			return error;
		}
		if (error < len)
			memset(result+error, '\0', len-error);
	} else {
		memset(result, '\0', len);
	}

	realalloc += kobjsize(tblock);
	askedalloc += sizeof(struct mm_tblock_struct);

	realalloc += kobjsize(tblock->rblock);
	askedalloc += sizeof(struct mm_rblock_struct);

	tblock->next = current->mm->context.tblock.next;
	current->mm->context.tblock.next = tblock;

#ifdef DEBUG
	printk("do_mmap:\n");
	show_process_blocks();
#endif	  

	return (unsigned long)result;
}

int do_munmap(struct mm_struct * mm, unsigned long addr, size_t len)
{
	struct mm_tblock_struct * tblock, *tmp;

#ifdef MAGIC_ROM_PTR
	/*
	 * For efficiency's sake, if the pointer is obviously in ROM,
	 * don't bother walking the lists to free it.
	 */
	if (is_in_rom(addr))
		return 0;
#endif

#ifdef DEBUG
	printk("do_munmap:\n");
#endif

	tmp = &mm->context.tblock; /* dummy head */
	while ((tblock=tmp->next) && tblock->rblock &&
			tblock->rblock->kblock != (void*)addr) 
		tmp = tblock;
		
	if (!tblock) {
		printk("munmap of non-mmaped memory by process %d (%s): %p\n",
				current->pid, current->comm, (void*)addr);
		return -EINVAL;
	}
	if (tblock->rblock) {
		if (!--tblock->rblock->refcount) {
			if (tblock->rblock->kblock) {
				realalloc -= kobjsize(tblock->rblock->kblock);
				askedalloc -= tblock->rblock->size;
				kfree(tblock->rblock->kblock);
			}
			
			realalloc -= kobjsize(tblock->rblock);
			askedalloc -= sizeof(struct mm_rblock_struct);
			kfree(tblock->rblock);
		}
	}
	tmp->next = tblock->next;
	realalloc -= kobjsize(tblock);
	askedalloc -= sizeof(struct mm_tblock_struct);
	kfree(tblock);

#ifdef DEBUG
	show_process_blocks();
#endif	  

	return 0;
}

/* Release all mmaps. */
void exit_mmap(struct mm_struct * mm)
{
	struct mm_tblock_struct *tmp;

	if (!mm)
		return;

#ifdef DEBUG
	printk("Exit_mmap:\n");
#endif

	while((tmp = mm->context.tblock.next)) {
		if (tmp->rblock) {
			if (!--tmp->rblock->refcount) {
				if (tmp->rblock->kblock) {
					realalloc -= kobjsize(tmp->rblock->kblock);
					askedalloc -= tmp->rblock->size;
					kfree(tmp->rblock->kblock);
				}
				realalloc -= kobjsize(tmp->rblock);
				askedalloc -= sizeof(struct mm_rblock_struct);
				kfree(tmp->rblock);
			}
			tmp->rblock = 0;
		}
		mm->context.tblock.next = tmp->next;
		realalloc -= kobjsize(tmp);
		askedalloc -= sizeof(struct mm_tblock_struct);
		kfree(tmp);
	}

#ifdef DEBUG
	show_process_blocks();
#endif	  
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

unsigned long do_brk(unsigned long addr, unsigned long len)
{
	return -ENOMEM;
}

struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr)
{
	return NULL;
}

struct page * follow_page(struct mm_struct *mm, unsigned long addr, int write)
{
	return NULL;
}

struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}

int remap_page_range(struct vm_area_struct *vma, unsigned long from,
		unsigned long to, unsigned long size, pgprot_t prot)
{
	return -EPERM;
}

unsigned long get_unmapped_area(struct file *file, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return -ENOMEM;
}

void swap_unplug_io_fn(struct backing_dev_info *)
{
}
