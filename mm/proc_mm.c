/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/proc_mm.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>

#ifdef CONFIG_PROC_MM_DUMPABLE
/* Checks if a task must be considered dumpable
 *
 * XXX: copied from fs/proc/base.c, removed task_lock, added rmb(): this must be
 * called with task_lock(task) held. */
static int task_dumpable(struct task_struct *task)
{
	int dumpable = 0;
	struct mm_struct *mm;

	mm = task->mm;
	if (mm) {
		rmb();
		dumpable = mm->dumpable;
	}
	return dumpable;
}

/*
 * This is to be used in PTRACE_SWITCH_MM handling. We are going to set
 * child->mm to new, and we must first correctly set new->dumpable.
 * Since we take task_lock of child and it's needed also by the caller, we
 * return with it locked.
 */
void lock_fix_dumpable_setting(struct task_struct* child, struct mm_struct* new)
	__acquires(child->alloc_lock)
{
	int dumpable = 1;

	/* We must be safe.
	 * If the child is ptraced from a non-dumpable process,
	 * let's not be dumpable. If the child is non-dumpable itself,
	 * copy this property across mm's.
	 *
	 * Don't try to be smart for the opposite case and turn
	 * child->mm->dumpable to 1: I've not made sure it is safe.
	 */

	task_lock(current);
	if (unlikely(!task_dumpable(current))) {
		dumpable = 0;
	}
	task_unlock(current);

	task_lock(child);
	if (likely(dumpable) && unlikely(!task_dumpable(child))) {
		dumpable = 0;
	}

	if (!dumpable) {
		new->dumpable = 0;
		wmb();
	}
}
#endif

/* Naming conventions are a mess, so I note them down.
 *
 * Things ending in _mm can be for everything. It's only for
 * {open,release}_proc_mm.
 *
 * For the rest:
 *
 * _mm means /proc/mm, _mm64 means /proc/mm64. This is for the infrastructure
 * only (for instance proc_mm_get_mm checks whether the file is /proc/mm or
 * /proc/mm64; for instance the /proc handling).
 *
 * While for what is conversion dependant, we use the suffix _native and _emul.
 * In some cases, there is a mapping between these ones (defined by
 * <asm/proc_mm.h>).
 */

/*These two are common to everything.*/
static int open_proc_mm(struct inode *inode, struct file *file)
{
	struct mm_struct *mm = mm_alloc();
	int ret;

	ret = -ENOMEM;
	if(mm == NULL)
		goto out_mem;

	init_new_empty_context(mm);
	arch_pick_mmap_layout(mm);
#ifdef CONFIG_PROC_MM_DUMPABLE
	mm->dumpable = current->mm->dumpable;
	wmb();
#endif

	file->private_data = mm;

	return 0;

out_mem:
	return ret;
}

static int release_proc_mm(struct inode *inode, struct file *file)
{
	struct mm_struct *mm = file->private_data;

	mmput(mm);
	return 0;
}

static struct file_operations proc_mm_fops;

struct mm_struct *proc_mm_get_mm_native(int fd);

static ssize_t write_proc_mm_native(struct file *file, const char *buffer,
			     size_t count, loff_t *ppos)
{
	struct mm_struct *mm = file->private_data;
	struct proc_mm_op req;
	int n, ret;

	if(count > sizeof(req))
		return(-EINVAL);

	n = copy_from_user(&req, buffer, count);
	if(n != 0)
		return(-EFAULT);

	ret = count;
	switch(req.op){
	case MM_MMAP: {
		struct mm_mmap *map = &req.u.mmap;

		/* Nobody ever noticed it, but do_mmap_pgoff() calls
		 * get_unmapped_area() which checks current->mm, if
		 * MAP_FIXED is not set, so mmap() could replace
		 * an old mapping.
		 */
		if (! (map->flags & MAP_FIXED))
			return(-EINVAL);

		ret = __do_mmap(mm, map->addr, map->len, map->prot,
			       map->flags, map->fd, map->offset);
		if((ret & ~PAGE_MASK) == 0)
			ret = count;

		break;
	}
	case MM_MUNMAP: {
		struct mm_munmap *unmap = &req.u.munmap;

		down_write(&mm->mmap_sem);
		ret = do_munmap(mm, unmap->addr, unmap->len);
		up_write(&mm->mmap_sem);

		if(ret == 0)
			ret = count;
		break;
	}
	case MM_MPROTECT: {
		struct mm_mprotect *protect = &req.u.mprotect;

		ret = do_mprotect(mm, protect->addr, protect->len,
				  protect->prot);
		if(ret == 0)
			ret = count;
		break;
	}

	case MM_COPY_SEGMENTS: {
		struct mm_struct *from = proc_mm_get_mm_native(req.u.copy_segments);

		if(IS_ERR(from)){
			ret = PTR_ERR(from);
			break;
		}

		ret = copy_context(mm, from);
		if(ret == 0)
			ret = count;
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*These three are all for /proc/mm.*/
struct mm_struct *proc_mm_get_mm(int fd)
{
	struct mm_struct *ret = ERR_PTR(-EBADF);
	struct file *file;

	file = fget(fd);
	if (!file)
		goto out;

	ret = ERR_PTR(-EINVAL);
	if(file->f_op != &proc_mm_fops)
		goto out_fput;

	ret = file->private_data;
out_fput:
	fput(file);
out:
	return(ret);
}

static struct file_operations proc_mm_fops = {
	.open		= open_proc_mm,
	.release	= release_proc_mm,
	.write		= write_proc_mm,
};

/*Macro-ify it to avoid the duplication.*/
static int make_proc_mm(void)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("mm", 0222, &proc_root);
	if(ent == NULL){
		printk("make_proc_mm : Failed to register /proc/mm\n");
		return(0);
	}
	ent->proc_fops = &proc_mm_fops;

	return 0;
}

__initcall(make_proc_mm);

/*XXX: change the option.*/
#ifdef CONFIG_64BIT
static struct file_operations proc_mm64_fops = {
	.open		= open_proc_mm,
	.release	= release_proc_mm,
	.write		= write_proc_mm64,
};

static int make_proc_mm64(void)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("mm64", 0222, &proc_root);
	if(ent == NULL){
		printk("make_proc_mm : Failed to register /proc/mm64\n");
		return(0);
	}
	ent->proc_fops = &proc_mm64_fops;

	return 0;
}

__initcall(make_proc_mm64);

struct mm_struct *proc_mm_get_mm64(int fd)
{
	struct mm_struct *ret = ERR_PTR(-EBADF);
	struct file *file;

	file = fget(fd);
	if (!file)
		goto out;

	ret = ERR_PTR(-EINVAL);
	/*This is the only change.*/
	if(file->f_op != &proc_mm64_fops)
		goto out_fput;

	ret = file->private_data;
out_fput:
	fput(file);
out:
	return(ret);
}
#endif
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
