/*
 * Kernel Hooks Interface.
 * 
 * Authors: Richard J Moore <richardj_moore@uk.ibm.com>
 *	    Vamsi Krishna S. <vamsi_krishna@in.ibm.com>
 * 22 Aug 2003 : Added code for /proc entries and hook priority.
 * 	  Prasanna S Panchamukhi < prasanna@in.ibm.com>
 * (C) Copyright IBM Corp. 2002, 2003
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/hook.h>
#include <asm/system.h>
#include <asm/uaccess.h>

static spinlock_t hook_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_MUTEX(hook_proc_sem);

static int hook_global_status = 0;

static void calculate_indicies(struct hook *hook)
{
	struct hook_rec *rec, *rec_prev;
	list_for_each_entry(rec, &hook->exit_list, exit_list) {
		rec_prev = list_entry(rec->exit_list.prev, struct hook_rec, exit_list);
		rec->hook_index = (rec_prev->hook_index) + 1;
	}
	return;
}

static inline void deactivate_hook(struct hook *hook)
{
	hook->hook_flags &= ~HOOK_ACTIVE;
	if(hook->hook_flags & HOOK_ASM_HOOK)
		deactivate_asm_hook(hook);
}

static inline void activate_hook(struct hook *hook)
{
	hook->hook_flags |= HOOK_ACTIVE;
	if(hook->hook_flags & HOOK_ASM_HOOK)
		activate_asm_hook(hook);
}

static void disarm_one_hook_exit(struct hook_rec *hook_rec)
{
	struct hook_rec *rec;
	hook_rec->hook_flags &= ~HOOK_ARMED;
	list_for_each_entry(rec, &hook_rec->hook_head->exit_list, exit_list) {
		if(rec->hook_flags & HOOK_ARMED) {
			return;
		}
	}
	deactivate_hook(hook_rec->hook_head);
}
#ifdef CONFIG_HOOK_PROCFS
static struct proc_dir_entry *proc_hooks_dir;
#define PROC_HOOKS_ROOT "hooks"

static int hook_read_proc(char *page, char **start, off_t off, int count, 
		int *eof, void *data)
{
	int len;
	struct hook_rec *hook_rec = (struct hook_rec *)data;
	len = sprintf(page, "%d\n", (hook_rec->hook_flags & HOOK_ARMED));
	return len;
}

static int hook_write_proc(struct file *file, const char *buffer, 
		unsigned long count, void *data)
{
	struct hook_rec *hook_rec = (struct hook_rec *)data;
	char input;
	int armed;

	if (copy_from_user(&input, buffer, 1)) {
		return -EFAULT;
	}
	armed = input - '0';
	if (armed && !(hook_rec->hook_flags & HOOK_ARMED)) {
		hook_exit_arm(hook_rec);
	} else if (!armed && (hook_rec->hook_flags & HOOK_ARMED)) {
		hook_exit_disarm(hook_rec);
	}
	return count;
}

static void hook_create_proc(struct hook_rec *hook_rec)
{
	char *proc_entry_name;
	struct hook *hook = (struct hook *) (hook_rec->hook_head);

	if (hook->proc_entry) {
		char tmp[32];
		proc_entry_name = hook_rec->hook_exit_name;
		if (!proc_entry_name) {
			sprintf(tmp, "%p", hook_rec);
			proc_entry_name = tmp;
		}
		
		if (!hook_rec->proc_writable) {
		
			hook_rec->proc_entry = create_proc_read_entry(
					proc_entry_name, 0444, 
					hook->proc_entry, 
					hook_read_proc,
					hook_rec);
		} else {
			hook_rec->proc_entry = create_proc_entry(
					proc_entry_name, 0644, 
					hook->proc_entry);
			if (hook_rec->proc_entry) {
				hook_rec->proc_entry->data = hook_rec;
				hook_rec->proc_entry->read_proc = hook_read_proc;
				hook_rec->proc_entry->write_proc = hook_write_proc;
			}
		}
	}
}

static inline void create_hook_exit_proc_entry(struct hook_rec *hook_rec)
{
	down(&hook_proc_sem);
	if (hook_global_status & HOOK_INIT) {
		hook_create_proc(hook_rec);
	}
	up(&hook_proc_sem);
}

static inline void remove_hook_exit_proc_entry(struct hook_rec *hook_rec)
{
	char tmp[32];
	char *proc_entry_name;

	down(&hook_proc_sem);
	if (hook_rec->proc_entry) {
		proc_entry_name = hook_rec->hook_exit_name;
		if (!proc_entry_name) {
			sprintf(tmp, "%p", hook_rec);
			proc_entry_name = tmp;
		}
		hook_rec->proc_entry->data = NULL;
		remove_proc_entry(proc_entry_name, hook_rec->hook_head->proc_entry);
	}
	up(&hook_proc_sem);
}

static inline void create_hook_proc_dir(struct hook *hook)
{
	down(&hook_proc_sem);
	if (hook_global_status & HOOK_INIT) {
		hook->proc_entry = proc_mkdir(hook->hook_id, proc_hooks_dir);
	}
	up(&hook_proc_sem);
}

static inline void remove_hook_proc_dir(struct hook *hook)
{
	down(&hook_proc_sem);
	if (hook->proc_entry) {
		remove_proc_entry(hook->hook_id, proc_hooks_dir);
	}
	up(&hook_proc_sem);
}

static void __init init_hook_procfs(void)
{
	down(&hook_proc_sem);
	proc_hooks_dir = proc_mkdir(PROC_HOOKS_ROOT, NULL);
	up(&hook_proc_sem);
}

static void __exit cleanup_hook_procfs(void)
{
	down(&hook_proc_sem);
	remove_proc_entry(PROC_HOOKS_ROOT, NULL);
	up(&hook_proc_sem);
}
#else
static inline void hook_create_proc(struct hook_rec *hook_rec) {}
static inline void create_hook_exit_proc_entry(struct hook_rec *hook_rec) {}
static inline void remove_hook_exit_proc_entry(struct hook_rec *hook_rec) {}
static inline void create_hook_proc_dir(struct hook *hook) {}
static inline void remove_hook_proc_dir(struct hook *hook) {}
static inline void init_hook_procfs(void) {}
static inline void cleanup_hook_procfs(void) {}
#endif /* CONFIG_HOOK_PROCFS */

int hook_exit_register(struct hook *hook, struct hook_rec *hook_rec)
{
	unsigned long flags;
	struct hook_rec *rec_next, *rec_prev, *rec;
	struct list_head *hook_link;

	/* During the registeration of hook exits, hook entries are also created
	 * in the /proc. proc entries cannot be created with irq's disabled.
	 */
	if (list_empty(&hook->exit_list)) 
		create_hook_proc_dir(hook);

	spin_lock_irqsave(&hook_lock, flags);
	if(hook->hook_flags & HOOK_EXCLUSIVE) {
		if (!list_empty(&hook->exit_list)) {
			spin_unlock_irqrestore(&hook_lock, flags);
			return -EPRIORITY;
		}
		hook->hook_ex_exit = hook_rec->hook_exit;
	}
	hook_link = &hook->exit_list;
	rec_next = list_entry(hook->exit_list.next, struct hook_rec, exit_list);
	rec_prev = list_entry(hook->exit_list.prev, struct hook_rec, exit_list);
	if ((hook_rec->hook_flags) & HOOK_PRIORITY_FIRST) {
		if ((!list_empty(&hook->exit_list)) && ((rec_next->hook_flags) & HOOK_PRIORITY_FIRST)) {
			spin_unlock_irqrestore(&hook_lock, flags);
			return ERROR_HIGHER_PRIORITY_HOOK;
		} 
	}
	
	if ((hook_rec->hook_flags) & HOOK_PRIORITY_LAST) {
		if (!list_empty(&hook->exit_list)) {
			if ((rec_prev->hook_flags) & HOOK_PRIORITY_LAST) {
				spin_unlock_irqrestore(&hook_lock, flags);
				return ERROR_LOWER_PRIORITY_HOOK;
			} else {
				hook_link = &rec_prev->exit_list;
			}
		}
	}

	if (!((hook_rec->hook_flags) & HOOK_PRIORITY)) {
		if ((hook_rec->hook_flags) & HOOK_QUEUE_LIFO) {
			if ((!list_empty(&hook->exit_list)) && ((rec_next->hook_flags) & HOOK_PRIORITY_FIRST)) 
				hook_link = &rec_next->exit_list;
		} else if ((!list_empty(&hook->exit_list)) && ((rec_prev->hook_flags) & HOOK_PRIORITY_LAST)) {
			rec = list_entry(rec_prev->exit_list.prev, struct hook_rec, exit_list);
			hook_link = &rec->exit_list;
		} else 
			hook_link = &rec_prev->exit_list;
	}
	
	list_add(&hook_rec->exit_list, hook_link);
	hook_rec->hook_head = hook;

	calculate_indicies(hook_rec->hook_head);

	if(is_asm_hook(hook->hook_addr))
		hook->hook_flags |= HOOK_ASM_HOOK;

	try_module_get(THIS_MODULE);
	spin_unlock_irqrestore(&hook_lock, flags);

	/* Creates entries in /proc, for hook exits.*/
	create_hook_exit_proc_entry(hook_rec);
	return 0;
}

void hook_exit_deregister(struct hook_rec *rec)
{
	unsigned long flags;
	struct hook *hook;

	spin_lock_irqsave(&hook_lock, flags);
	if(rec->hook_flags & HOOK_ARMED)
		disarm_one_hook_exit(rec);
	if(rec->hook_head->hook_flags & HOOK_EXCLUSIVE)
		rec->hook_head->hook_ex_exit = NULL;
	list_del(&rec->exit_list);
	hook = rec->hook_head;
	calculate_indicies(rec->hook_head);
	module_put(THIS_MODULE);
	spin_unlock_irqrestore(&hook_lock, flags);

	/* Remove hook exit entries from /proc. */
	remove_hook_exit_proc_entry(rec);

	/* Remove hook entries from /proc. */
	if (list_empty(&hook->exit_list))
		remove_hook_proc_dir(hook);
		 
}

void hook_exit_arm(struct hook_rec *rec)
{
	unsigned long flags;

	spin_lock_irqsave(&hook_lock, flags);
	rec->hook_flags |= HOOK_ARMED;
	if(!(rec->hook_head->hook_flags & HOOK_ACTIVE))
		activate_hook(rec->hook_head);
	spin_unlock_irqrestore(&hook_lock, flags);
}

void hook_exit_disarm(struct hook_rec *rec)
{
	unsigned long flags;

	spin_lock_irqsave(&hook_lock, flags);
	disarm_one_hook_exit(rec);
	spin_unlock_irqrestore(&hook_lock, flags);
}

static int __init hook_init_module(void)
{
	hook_global_status |= HOOK_INIT;
	init_hook_procfs();
	printk(KERN_INFO "Kernel Hooks Interface installed.\n");
	return 0;
}

static void __exit hook_cleanup_module(void)
{
	cleanup_hook_procfs();
	printk(KERN_INFO "Kernel Hooks Interface terminated.\n");
}

module_init(hook_init_module);
module_exit(hook_cleanup_module);

EXPORT_SYMBOL(hook_exit_deregister);
EXPORT_SYMBOL(hook_exit_arm);
EXPORT_SYMBOL(hook_exit_disarm);
EXPORT_SYMBOL(hook_exit_register);

MODULE_LICENSE("GPL");
