/*
 * arch/s390/kernel/profile.c
 *
 * Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
 */
#include <linux/proc_fs.h>

static struct proc_dir_entry * root_irq_dir;

static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	int len = cpumask_scnprintf(page, count, *(cpumask_t *)data);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int prof_cpu_mask_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	cpumask_t *mask = (cpumask_t *)data;
	unsigned long full_count = count, err;
	cpumask_t new_value;

	err = cpumask_parse(buffer, count, new_value);
	if (err)
		return err;

	*mask = new_value;
	return full_count;
}

cpumask_t prof_cpu_mask = CPU_MASK_ALL;

void init_irq_proc(void)
{
	struct proc_dir_entry *entry;

	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", 0);

	/* create /proc/irq/prof_cpu_mask */
	entry = create_proc_entry("prof_cpu_mask", 0600, root_irq_dir);

	if (!entry)
		return;

	entry->nlink = 1;
	entry->data = (void *)&prof_cpu_mask;
	entry->read_proc = prof_cpu_mask_read_proc;
	entry->write_proc = prof_cpu_mask_write_proc;
}
