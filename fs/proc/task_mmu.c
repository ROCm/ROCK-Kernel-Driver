
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/uaccess.h>

char *task_mem(struct mm_struct *mm, char *buffer)
{
	unsigned long data = 0, stack = 0, exec = 0, lib = 0;
	struct vm_area_struct *vma;

	down_read(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		unsigned long len = (vma->vm_end - vma->vm_start) >> 10;
		if (!vma->vm_file) {
			data += len;
			if (vma->vm_flags & VM_GROWSDOWN)
				stack += len;
			continue;
		}
		if (vma->vm_flags & VM_WRITE)
			continue;
		if (vma->vm_flags & VM_EXEC) {
			exec += len;
			if (vma->vm_flags & VM_EXECUTABLE)
				continue;
			lib += len;
		}
	}
	buffer += sprintf(buffer,
		"VmSize:\t%8lu kB\n"
		"VmLck:\t%8lu kB\n"
		"VmRSS:\t%8lu kB\n"
		"VmData:\t%8lu kB\n"
		"VmStk:\t%8lu kB\n"
		"VmExe:\t%8lu kB\n"
		"VmLib:\t%8lu kB\n",
		mm->total_vm << (PAGE_SHIFT-10),
		mm->locked_vm << (PAGE_SHIFT-10),
		mm->rss << (PAGE_SHIFT-10),
		data - stack, stack,
		exec - lib, lib);
	up_read(&mm->mmap_sem);
	return buffer;
}

unsigned long task_vsize(struct mm_struct *mm)
{
	return PAGE_SIZE * mm->total_vm;
}

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	struct vm_area_struct *vma;
	int size = 0;

	*resident = mm->rss;
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		int pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;

		size += pages;
		if (is_vm_hugetlb_page(vma)) {
			if (!(vma->vm_flags & VM_DONTCOPY))
				*shared += pages;
			continue;
		}
		if (vma->vm_flags & VM_SHARED || !list_empty(&vma->shared))
			*shared += pages;
		if (vma->vm_flags & VM_EXECUTABLE)
			*text += pages;
		else
			*data += pages;
	}

	return size;
}

/*
 * The way we support synthetic files > 4K
 * - without storing their contents in some buffer and
 * - without walking through the entire synthetic file until we reach the
 *   position of the requested data
 * is to cleverly encode the current position in the file's f_pos field.
 * There is no requirement that a read() call which returns `count' bytes
 * of data increases f_pos by exactly `count'.
 *
 * This idea is Linus' one. Bruno implemented it.
 */

/*
 * For the /proc/<pid>/maps file, we use fixed length records, each containing
 * a single line.
 *
 * f_pos = (number of the vma in the task->mm->mmap list) * PAGE_SIZE
 *         + (index into the line)
 */
/* for systems with sizeof(void*) == 4: */
#define MAPS_LINE_FORMAT4	  "%08lx-%08lx %s %08lx %02x:%02x %lu"
#define MAPS_LINE_MAX4	49 /* sum of 8  1  8  1 4 1 8 1 5 1 10 1 */

/* for systems with sizeof(void*) == 8: */
#define MAPS_LINE_FORMAT8	  "%016lx-%016lx %s %016lx %02x:%02x %lu"
#define MAPS_LINE_MAX8	73 /* sum of 16  1  16  1 4 1 16 1 5 1 10 1 */

#define MAPS_LINE_FORMAT	(sizeof(void*) == 4 ? MAPS_LINE_FORMAT4 : MAPS_LINE_FORMAT8)
#define MAPS_LINE_MAX	(sizeof(void*) == 4 ?  MAPS_LINE_MAX4 :  MAPS_LINE_MAX8)

static int proc_pid_maps_get_line (char *buf, struct vm_area_struct *map)
{
	/* produce the next line */
	char *line;
	char str[5];
	int flags;
	dev_t dev;
	unsigned long ino;
	int len;

	flags = map->vm_flags;

	str[0] = flags & VM_READ ? 'r' : '-';
	str[1] = flags & VM_WRITE ? 'w' : '-';
	str[2] = flags & VM_EXEC ? 'x' : '-';
	str[3] = flags & VM_MAYSHARE ? 's' : 'p';
	str[4] = 0;

	dev = 0;
	ino = 0;
	if (map->vm_file != NULL) {
		struct inode *inode = map->vm_file->f_dentry->d_inode;
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		line = d_path(map->vm_file->f_dentry,
			      map->vm_file->f_vfsmnt,
			      buf, PAGE_SIZE);
		buf[PAGE_SIZE-1] = '\n';
		line -= MAPS_LINE_MAX;
		if(line < buf)
			line = buf;
	} else
		line = buf;

	len = sprintf(line,
		      MAPS_LINE_FORMAT,
		      map->vm_start, map->vm_end, str, map->vm_pgoff << PAGE_SHIFT,
		      MAJOR(dev), MINOR(dev), ino);

	if(map->vm_file) {
		int i;
		for(i = len; i < MAPS_LINE_MAX; i++)
			line[i] = ' ';
		len = buf + PAGE_SIZE - line;
		memmove(buf, line, len);
	} else
		line[len++] = '\n';
	return len;
}

ssize_t proc_pid_read_maps(struct task_struct *task, struct file *file,
			   char *buf, size_t count, loff_t *ppos)
{
	struct mm_struct *mm;
	struct vm_area_struct * map;
	char *tmp, *kbuf;
	long retval;
	int off, lineno, loff;

	/* reject calls with out of range parameters immediately */
	retval = 0;
	if (*ppos > LONG_MAX)
		goto out;
	if (count == 0)
		goto out;
	off = (long)*ppos;
	/*
	 * We might sleep getting the page, so get it first.
	 */
	retval = -ENOMEM;
	kbuf = (char*)__get_free_page(GFP_KERNEL);
	if (!kbuf)
		goto out;

	tmp = (char*)__get_free_page(GFP_KERNEL);
	if (!tmp)
		goto out_free1;

	mm = get_task_mm(task);
 
	retval = 0;
	if (!mm)
		goto out_free2;

	down_read(&mm->mmap_sem);
	map = mm->mmap;
	lineno = 0;
	loff = 0;
	if (count > PAGE_SIZE)
		count = PAGE_SIZE;
	while (map) {
		int len;
		if (off > PAGE_SIZE) {
			off -= PAGE_SIZE;
			goto next;
		}
		len = proc_pid_maps_get_line(tmp, map);
		len -= off;
		if (len > 0) {
			if (retval+len > count) {
				/* only partial line transfer possible */
				len = count - retval;
				/* save the offset where the next read
				 * must start */
				loff = len+off;
			}
			memcpy(kbuf+retval, tmp+off, len);
			retval += len;
		}
		off = 0;
next:
		if (!loff)
			lineno++;
		if (retval >= count)
			break;
		if (loff) BUG();
		map = map->vm_next;
	}
	up_read(&mm->mmap_sem);
	mmput(mm);

	if (retval > count) BUG();
	if (copy_to_user(buf, kbuf, retval))
		retval = -EFAULT;
	else
		*ppos = (lineno << PAGE_SHIFT) + loff;

out_free2:
	free_page((unsigned long)tmp);
out_free1:
	free_page((unsigned long)kbuf);
out:
	return retval;
}
