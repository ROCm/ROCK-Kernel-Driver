#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/seq_file.h>
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

static int show_map(struct seq_file *m, void *v)
{
	struct vm_area_struct *map = v;
	struct file *file = map->vm_file;
	int flags = map->vm_flags;
	unsigned long ino = 0;
	dev_t dev = 0;
	int len;

	if (file) {
		struct inode *inode = map->vm_file->f_dentry->d_inode;
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
	}

	seq_printf(m, "%08lx-%08lx %c%c%c%c %08lx %02x:%02x %lu %n",
			map->vm_start,
			map->vm_end,
			flags & VM_READ ? 'r' : '-',
			flags & VM_WRITE ? 'w' : '-',
			flags & VM_EXEC ? 'x' : '-',
			flags & VM_MAYSHARE ? 's' : 'p',
			map->vm_pgoff << PAGE_SHIFT,
			MAJOR(dev), MINOR(dev), ino, &len);

	if (map->vm_file) {
		len = 25 + sizeof(void*) * 6 - len;
		if (len < 1)
			len = 1;
		seq_printf(m, "%*c", len, ' ');
		seq_path(m, file->f_vfsmnt, file->f_dentry, " \t\n\\");
	}
	seq_putc(m, '\n');
	return 0;
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct task_struct *task = m->private;
	struct mm_struct *mm = get_task_mm(task);
	struct vm_area_struct * map;
	loff_t l = *pos;

	if (!mm)
		return NULL;

	down_read(&mm->mmap_sem);
	map = mm->mmap;
	while (l-- && map)
		map = map->vm_next;
	if (!map) {
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
	return map;
}

static void m_stop(struct seq_file *m, void *v)
{
	struct vm_area_struct *map = v;
	if (map) {
		struct mm_struct *mm = map->vm_mm;
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct vm_area_struct *map = v;
	(*pos)++;
	if (map->vm_next)
		return map->vm_next;
	m_stop(m, v);
	return NULL;
}

struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};
