
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/seq_file.h>

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
	for (tblock = &mm->context.tblock; tblock; tblock = tblock->next) {
		if (!tblock->rblock)
			continue;
		bytes += kobjsize(tblock);
		if (atomic_read(&mm->mm_count) > 1 ||
		    tblock->rblock->refcount > 1) {
			sbytes += kobjsize(tblock->rblock->kblock);
			sbytes += kobjsize(tblock->rblock);
		} else {
			bytes += kobjsize(tblock->rblock->kblock);
			bytes += kobjsize(tblock->rblock);
			slack += kobjsize(tblock->rblock->kblock) -
					tblock->rblock->size;
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
	for (tbp = &mm->context.tblock; tbp; tbp = tbp->next) {
		if (tbp->rblock)
			vsize += kobjsize(tbp->rblock->kblock);
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
	for (tbp = &mm->context.tblock; tbp; tbp = tbp->next) {
		if (tbp->next)
			size += kobjsize(tbp->next);
		if (tbp->rblock) {
			size += kobjsize(tbp->rblock);
			size += kobjsize(tbp->rblock->kblock);
		}
	}

	size += (*text = mm->end_code - mm->start_code);
	size += (*data = mm->start_stack - mm->start_data);
	up_read(&mm->mmap_sem);
	*resident = size;
	return size;
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
