/*
 *  linux/kernel/profile.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/profile.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <asm/sections.h>

unsigned int * prof_buffer;
unsigned long prof_len;
unsigned long prof_shift;
int prof_on;
cpumask_t prof_cpu_mask = CPU_MASK_ALL;

int __init profile_setup(char * str)
{
	int par;

	if (!strncmp(str, "schedule", 8)) {
		prof_on = 2;
		printk(KERN_INFO "kernel schedule profiling enabled\n");
		if (str[7] == ',')
			str += 8;
	}
	if (get_option(&str,&par)) {
		prof_shift = par;
		prof_on = 1;
		printk(KERN_INFO "kernel profiling enabled (shift: %ld)\n",
			prof_shift);
	}
	return 1;
}


void __init profile_init(void)
{
	unsigned int size;
 
	if (!prof_on) 
		return;
 
	/* only text is profiled */
	prof_len = _etext - _stext;
	prof_len >>= prof_shift;
		
	size = prof_len * sizeof(unsigned int) + PAGE_SIZE - 1;
	prof_buffer = (unsigned int *) alloc_bootmem(size);
}

/* Profile event notifications */
 
#ifdef CONFIG_PROFILING
 
static DECLARE_RWSEM(profile_rwsem);
static struct notifier_block * exit_task_notifier;
static struct notifier_block * exit_mmap_notifier;
static struct notifier_block * exec_unmap_notifier;
 
void profile_exit_task(struct task_struct * task)
{
	down_read(&profile_rwsem);
	notifier_call_chain(&exit_task_notifier, 0, task);
	up_read(&profile_rwsem);
}
 
void profile_exit_mmap(struct mm_struct * mm)
{
	down_read(&profile_rwsem);
	notifier_call_chain(&exit_mmap_notifier, 0, mm);
	up_read(&profile_rwsem);
}

void profile_exec_unmap(struct mm_struct * mm)
{
	down_read(&profile_rwsem);
	notifier_call_chain(&exec_unmap_notifier, 0, mm);
	up_read(&profile_rwsem);
}

int profile_event_register(enum profile_type type, struct notifier_block * n)
{
	int err = -EINVAL;
 
	down_write(&profile_rwsem);
 
	switch (type) {
		case EXIT_TASK:
			err = notifier_chain_register(&exit_task_notifier, n);
			break;
		case EXIT_MMAP:
			err = notifier_chain_register(&exit_mmap_notifier, n);
			break;
		case EXEC_UNMAP:
			err = notifier_chain_register(&exec_unmap_notifier, n);
			break;
	}
 
	up_write(&profile_rwsem);
 
	return err;
}

 
int profile_event_unregister(enum profile_type type, struct notifier_block * n)
{
	int err = -EINVAL;
 
	down_write(&profile_rwsem);
 
	switch (type) {
		case EXIT_TASK:
			err = notifier_chain_unregister(&exit_task_notifier, n);
			break;
		case EXIT_MMAP:
			err = notifier_chain_unregister(&exit_mmap_notifier, n);
			break;
		case EXEC_UNMAP:
			err = notifier_chain_unregister(&exec_unmap_notifier, n);
			break;
	}

	up_write(&profile_rwsem);
	return err;
}

static struct notifier_block * profile_listeners;
static rwlock_t profile_lock = RW_LOCK_UNLOCKED;
 
int register_profile_notifier(struct notifier_block * nb)
{
	int err;
	write_lock_irq(&profile_lock);
	err = notifier_chain_register(&profile_listeners, nb);
	write_unlock_irq(&profile_lock);
	return err;
}


int unregister_profile_notifier(struct notifier_block * nb)
{
	int err;
	write_lock_irq(&profile_lock);
	err = notifier_chain_unregister(&profile_listeners, nb);
	write_unlock_irq(&profile_lock);
	return err;
}


void profile_hook(struct pt_regs * regs)
{
	read_lock(&profile_lock);
	notifier_call_chain(&profile_listeners, 0, regs);
	read_unlock(&profile_lock);
}

EXPORT_SYMBOL_GPL(register_profile_notifier);
EXPORT_SYMBOL_GPL(unregister_profile_notifier);

#endif /* CONFIG_PROFILING */

EXPORT_SYMBOL_GPL(profile_event_register);
EXPORT_SYMBOL_GPL(profile_event_unregister);

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len = cpumask_scnprintf(page, count, *(cpumask_t *)data);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int prof_cpu_mask_write_proc (struct file *file, const char __user *buffer,
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

void create_prof_cpu_mask(struct proc_dir_entry *root_irq_dir)
{
	struct proc_dir_entry *entry;

	/* create /proc/irq/prof_cpu_mask */
	if (!(entry = create_proc_entry("prof_cpu_mask", 0600, root_irq_dir)))
		return;
	entry->nlink = 1;
	entry->data = (void *)&prof_cpu_mask;
	entry->read_proc = prof_cpu_mask_read_proc;
	entry->write_proc = prof_cpu_mask_write_proc;
}
#endif /* CONFIG_PROC_FS */
