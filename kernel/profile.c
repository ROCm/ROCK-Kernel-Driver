/*
 *  linux/kernel/profile.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/profile.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/mm.h>

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

#endif /* CONFIG_PROFILING */

EXPORT_SYMBOL_GPL(profile_event_register);
EXPORT_SYMBOL_GPL(profile_event_unregister);
