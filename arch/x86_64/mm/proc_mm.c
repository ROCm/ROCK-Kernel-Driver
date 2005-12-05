#include <linux/proc_mm.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>

ssize_t write_proc_mm_emul(struct file *file, const char *buffer,
			     size_t count, loff_t *ppos)
{
	struct mm_struct *mm = file->private_data;
	struct proc_mm_op32 req;
	int n, ret;

	if(count > sizeof(req))
		return(-EINVAL);

	n = copy_from_user(&req, buffer, count);
	if(n != 0)
		return(-EFAULT);

	ret = count;
	switch(req.op){
	case MM_MMAP: {
		struct mm_mmap32 *map = &req.u.mmap;

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
		struct mm_munmap32 *unmap = &req.u.munmap;

		down_write(&mm->mmap_sem);
		ret = do_munmap(mm, unmap->addr, unmap->len);
		up_write(&mm->mmap_sem);

		if(ret == 0)
			ret = count;
		break;
	}
	case MM_MPROTECT: {
		struct mm_mprotect32 *protect = &req.u.mprotect;

		ret = do_mprotect(mm, protect->addr, protect->len,
				  protect->prot);
		if(ret == 0)
			ret = count;
		break;
	}

	case MM_COPY_SEGMENTS: {
		struct mm_struct *from = proc_mm_get_mm_emul(req.u.copy_segments);

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

