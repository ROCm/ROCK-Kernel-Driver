/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "linux/init.h"
#include "linux/proc_fs.h"
#include "linux/proc_mm.h"
#include "linux/file.h"
#include "linux/mman.h"
#include "asm/uaccess.h"
#include "asm/mmu_context.h"

static struct file_operations proc_mm_fops;

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

extern long do_mmap2(struct mm_struct *mm, unsigned long addr,
		     unsigned long len, unsigned long prot,
		     unsigned long flags, unsigned long fd,
		     unsigned long pgoff);

static ssize_t write_proc_mm(struct file *file, const char *buffer,
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

		ret = do_mmap2(mm, map->addr, map->len, map->prot,
			       map->flags, map->fd, map->offset >> PAGE_SHIFT);
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
		struct mm_struct *from = proc_mm_get_mm(req.u.copy_segments);

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

	return(ret);
}

static int open_proc_mm(struct inode *inode, struct file *file)
{
	struct mm_struct *mm = mm_alloc();
	int ret;

	ret = -ENOMEM;
	if(mm == NULL)
		goto out_mem;

	init_new_empty_context(mm);

	spin_lock(&mmlist_lock);
	list_add(&mm->mmlist, &current->mm->mmlist);
	mmlist_nr++;
	spin_unlock(&mmlist_lock);

	file->private_data = mm;

	return(0);

 out_mem:
	return(ret);
}

static int release_proc_mm(struct inode *inode, struct file *file)
{
	struct mm_struct *mm = file->private_data;

	mmput(mm);
	return(0);
}

static struct file_operations proc_mm_fops = {
	.open		= open_proc_mm,
	.release	= release_proc_mm,
	.write		= write_proc_mm,
};

static int make_proc_mm(void)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("mm", 0222, &proc_root);
	if(ent == NULL){
		printk("make_proc_mm : Failed to register /proc/mm\n");
		return(0);
	}
	ent->proc_fops = &proc_mm_fops;

	return(0);
}

__initcall(make_proc_mm);

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
