
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * Logic: we've got two memory sums for each process, "shared", and
 * "non-shared". Shared memory may get counted more then once, for
 * each process that owns it. Non-shared memory is counted
 * accurately.
 */
char *task_mem(struct mm_struct *mm, char *buffer)
{
	unsigned long bytes = 0, sbytes = 0, slack = 0;
	struct mm_tblock_struct *tblock;
        
	down_read(&mm->mmap_sem);
	for (tblock = mm->context.tblock; tblock; tblock = tblock->next) {
		if (!tblock->vma)
			continue;
		bytes += kobjsize(tblock);
		if (atomic_read(&mm->mm_count) > 1 ||
		    atomic_read(&tblock->vma->vm_usage) > 1) {
			sbytes += kobjsize((void *) tblock->vma->vm_start);
			sbytes += kobjsize(tblock->vma);
		} else {
			bytes += kobjsize((void *) tblock->vma->vm_start);
			bytes += kobjsize(tblock->vma);
			slack += kobjsize((void *) tblock->vma->vm_start) -
				(tblock->vma->vm_end - tblock->vma->vm_start);
		}
	}

	if (atomic_read(&mm->mm_count) > 1)
		sbytes += kobjsize(mm);
	else
		bytes += kobjsize(mm);
	
	if (current->fs && atomic_read(&current->fs->count) > 1)
		sbytes += kobjsize(current->fs);
	else
		bytes += kobjsize(current->fs);

	if (current->files && atomic_read(&current->files->count) > 1)
		sbytes += kobjsize(current->files);
	else
		bytes += kobjsize(current->files);

	if (current->sighand && atomic_read(&current->sighand->count) > 1)
		sbytes += kobjsize(current->sighand);
	else
		bytes += kobjsize(current->sighand);

	bytes += kobjsize(current); /* includes kernel stack */

	buffer += sprintf(buffer,
		"Mem:\t%8lu bytes\n"
		"Slack:\t%8lu bytes\n"
		"Shared:\t%8lu bytes\n",
		bytes, slack, sbytes);

	up_read(&mm->mmap_sem);
	return buffer;
}

unsigned long task_vsize(struct mm_struct *mm)
{
	struct mm_tblock_struct *tbp;
	unsigned long vsize = 0;

	down_read(&mm->mmap_sem);
	for (tbp = mm->context.tblock; tbp; tbp = tbp->next) {
		if (tbp->vma)
			vsize += kobjsize((void *) tbp->vma->vm_start);
	}
	up_read(&mm->mmap_sem);
	return vsize;
}

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	struct mm_tblock_struct *tbp;
	int size = kobjsize(mm);

	down_read(&mm->mmap_sem);
	for (tbp = mm->context.tblock; tbp; tbp = tbp->next) {
		size += kobjsize(tbp);
		if (tbp->vma) {
			size += kobjsize(tbp->vma);
			size += kobjsize((void *) tbp->vma->vm_start);
		}
	}

	size += (*text = mm->end_code - mm->start_code);
	size += (*data = mm->start_stack - mm->start_data);
	up_read(&mm->mmap_sem);
	*resident = size;
	return size;
}

int proc_exe_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	struct mm_tblock_struct *tblock;
	struct vm_area_struct * vma;
	int result = -ENOENT;
	struct task_struct *task = proc_task(inode);
	struct mm_struct * mm = get_task_mm(task);

	if (!mm)
		goto out;
	down_read(&mm->mmap_sem);

	tblock = mm->context.tblock;
	vma = NULL;
	while (tblock) {
		if ((tblock->vma->vm_flags & VM_EXECUTABLE) && tblock->vma->vm_file) {
			vma = tblock->vma;
			break;
		}
		tblock = tblock->next;
	}

	if (vma) {
		*mnt = mntget(vma->vm_file->f_vfsmnt);
		*dentry = dget(vma->vm_file->f_dentry);
		result = 0;
	}

	up_read(&mm->mmap_sem);
	mmput(mm);
out:
	return result;
}

/*
 * Albert D. Cahalan suggested to fake entries for the traditional
 * sections here.  This might be worth investigating.
 */
static int show_map(struct seq_file *m, void *v)
{
	return 0;
}
static void *m_start(struct seq_file *m, loff_t *pos)
{
	return NULL;
}
static void m_stop(struct seq_file *m, void *v)
{
}
static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}
struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};
