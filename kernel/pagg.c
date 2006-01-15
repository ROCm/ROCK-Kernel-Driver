/*
 * PAGG (Process Aggregates) interface
 *
 *
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Contact information:  Silicon Graphics, Inc., 1500 Crittenden Lane,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/pagg.h>
#include <asm/semaphore.h>

/* list of pagg hook entries that reference the "module" implementations */
static LIST_HEAD(pagg_hook_list);
static DECLARE_RWSEM(pagg_hook_list_sem);


/**
 * pagg_get - get a pagg given a search key
 * @task: We examine the pagg_list from the given task
 * @key: Key name of pagg we wish to retrieve
 *
 * Given a pagg_list list structure, this function will return
 * a pointer to the pagg struct that matches the search
 * key.  If the key is not found, the function will return NULL.
 *
 * The caller should hold at least a read lock on the pagg_list
 * for task using down_read(&task->pagg_list.sem).
 *
 */
struct pagg *
pagg_get(struct task_struct *task, char *key)
{
	struct pagg *pagg;

	list_for_each_entry(pagg, &task->pagg_list, entry) {
		if (!strcmp(pagg->hook->name,key))
			return pagg;
	}
	return NULL;
}


/**
 * pagg_alloc - Insert a new pagg in to the pagg_list for a task
 * @task: Task we want to insert the pagg in to
 * @pagg_hook: Pagg hook to associate with the new pagg
 *
 * Given a task and a pagg hook, this function will allocate
 * a new pagg structure, initialize the settings, and insert the pagg into
 * the pagg_list for the task.
 *
 * The caller for this function should hold at least a read lock on the
 * pagg_hook_list_sem - or ensure that the pagg hook entry cannot be
 * removed. If this function was called from the pagg module (usually the
 * case), then the caller need not hold this lock. The caller should hold
 * a write lock on for the tasks pagg_sem.  This can be locked using
 * down_write(&task->pagg_sem)
 *
 */
struct pagg *
pagg_alloc(struct task_struct *task, struct pagg_hook *pagg_hook)
{
	struct pagg *pagg;

	pagg = kmalloc(sizeof(struct pagg), GFP_KERNEL);
	if (!pagg)
		return NULL;

	pagg->hook = pagg_hook;
	pagg->data = NULL;
	atomic_inc(&pagg_hook->refcnt);  /* Increase hook's reference count */
	list_add_tail(&pagg->entry, &task->pagg_list);
	return pagg;
}


/**
 * pagg_free - Delete pagg from the list and free its memory
 * @pagg: The pagg to free
 *
 * This function will ensure the pagg is deleted form
 * the list of pagg entries for the task. Finally, the memory for the
 * pagg is discarded.
 *
 * The caller of this function should hold a write lock on the pagg_sem
 * for the task. This can be locked using down_write(&task->pagg_sem).
 *
 * Prior to calling pagg_free, the pagg should have been detached from the
 * pagg container represented by this pagg.  That is usually done using
 * p->hook->detach(task, pagg);
 *
 */
void
pagg_free(struct pagg *pagg)
{
	atomic_dec(&pagg->hook->refcnt); /* decr the reference count on the hook */
	list_del(&pagg->entry);
	kfree(pagg);
}


/**
 * get_pagg_hook - Get the pagg hook matching the requested name
 * @key: The name of the pagg hook to get
 *
 * Given a pagg hook name key, this function will return a pointer
 * to the pagg_hook struct that matches the name.
 *
 * You should hold either the write or read lock for pagg_hook_list_sem
 * before using this function.  This will ensure that the pagg_hook_list
 * does not change while iterating through the list entries.
 *
 */
static struct pagg_hook *
get_pagg_hook(char *key)
{
	struct pagg_hook *pagg_hook;

	list_for_each_entry(pagg_hook, &pagg_hook_list, entry) {
		if (!strcmp(pagg_hook->name, key)) {
			return pagg_hook;
		}
	}
	return NULL;
}

/**
 * remove_client_paggs_from_all_tasks - Remove all paggs associated with hook
 * @php: Pagg hook associated with paggs to purge
 *
 * Given a pagg hook, this function will remove all paggs associated with that
 * pagg hook from all tasks calling the provided function on each pagg.
 *
 * If there is a detach function associated with the pagg, it is called
 * before the pagg is freed.
 *
 * This is meant to be used by pagg_hook_register and pagg_hook_unregister
 *
 */
static void
remove_client_paggs_from_all_tasks(struct pagg_hook *php)
{
	if (php == NULL)
		return;

	/* Because of internal race conditions we can't gaurantee
	 * getting every task in just one pass so we just keep going
	 * until there are no tasks with paggs from this hook attached.
	 * The inefficiency of this should be tempered by the fact that this
	 * happens at most once for each registered client.
	 */
	while (atomic_read(&php->refcnt) != 0) {
		struct task_struct *p = NULL;

		read_lock(&tasklist_lock);
		for_each_process(p) {
			struct pagg *paggp;

			get_task_struct(p);
			read_unlock(&tasklist_lock);
			down_write(&p->pagg_sem);
			paggp = pagg_get(p, php->name);
			if (paggp != NULL) {
				(void)php->detach(p, paggp);
				pagg_free(paggp);
			}
			up_write(&p->pagg_sem);
			read_lock(&tasklist_lock);

			/* If a PAGG got removed from the list while we're going through
			 * each process, the tasks list for the process would be empty.  In
			 * that case, break out of this for_each_process so we can do it
			 * again. */
			if (list_empty(&p->tasks)) {
				put_task_struct(p);
				break;
			} else
				put_task_struct(p);

		}
		read_unlock(&tasklist_lock);
	}
}

/**
 * pagg_hook_register - Register a new pagg hook and enter it the list
 * @pagg_hook_new: The new pagg hook to register
 *
 * Used to register a new pagg hook and enter it into the pagg_hook_list.
 * The service name for a pagg hook is restricted to 32 characters.
 *
 * If an "init()" function is supplied in the hook being registered then a
 * pagg will be attached to all existing tasks and the supplied "init()"
 * function will be applied to it.  If any call to the supplied "init()"
 * function returns a non zero result the registration will be aborted. As
 * part of the abort process, all paggs belonging to the new client will be
 * removed from all tasks and the supplied "detach()" function will be
 * called on them.
 *
 * If a memory error is encountered, the pagg hook is unregistered and any
 * tasks that have been attached to the initial pagg container are detached
 * from that container.
 *
 * The init function pointer return values have these meanings:
 * 0  - Success
 * >0 - Sucess, but don't track this process - get rid of the pagg for it
 *
 */
int
pagg_hook_register(struct pagg_hook *pagg_hook_new)
{
	struct pagg_hook *pagg_hook = NULL;

	/* Add new pagg module to access list */
	if (!pagg_hook_new)
		return -EINVAL;			/* error */
	if (!list_empty(&pagg_hook_new->entry))
		return -EINVAL;			/* error */
	if (pagg_hook_new->name == NULL || strlen(pagg_hook_new->name) > PAGG_NAMELN)
		return -EINVAL;			/* error */
	if (!pagg_hook_new->attach || !pagg_hook_new->detach)
		return -EINVAL;                 /* error */

	/* Try to insert new hook entry into the pagg hook list */
	down_write(&pagg_hook_list_sem);

	pagg_hook = get_pagg_hook(pagg_hook_new->name);

	if (pagg_hook) {
		up_write(&pagg_hook_list_sem);
		printk(KERN_WARNING "Attempt to register duplicate"
				" PAGG support (name=%s)\n", pagg_hook_new->name);
		return -EBUSY;
	}

	/* Okay, we can insert into the pagg hook list */
	list_add_tail(&pagg_hook_new->entry, &pagg_hook_list);
	/* set the ref count to zero */
	atomic_set(&pagg_hook_new->refcnt, 0);

	/* Now we can call the initializer function (if present) for each task */
	if (pagg_hook_new->init != NULL) {
		int init_result = 0;
		int task_exited = 0;

		/* Because of internal race conditions we can't gaurantee
		 * getting every task in just one pass so we just keep going
		 * until we don't find any unitialized tasks.  The inefficiency
		 * of this should be tempered by the fact that this happens
		 * at most once for each registered client.
		 */
		do {
			struct task_struct *p = NULL;

			read_lock(&tasklist_lock);
			for_each_process(p) {
				struct pagg *paggp;

				get_task_struct(p);
				read_unlock(&tasklist_lock);
				down_write(&p->pagg_sem);
				paggp = pagg_get(p, pagg_hook_new->name);
				if (paggp == NULL) {
					paggp = pagg_alloc(p, pagg_hook_new);
					if (paggp != NULL) {
						init_result = pagg_hook_new->init(p, paggp);
						/* Success, but init function
						 * pointer doesn't want pagg
						 * assocation. */
						if (init_result > 0)
							pagg_free(paggp);
					}
					else
						init_result = -ENOMEM;
				}
				up_write(&p->pagg_sem);
				read_lock(&tasklist_lock);
				/* Like in remove_client_paggs_from_all_tasks, if the task
				 * disappeared on us while we were going through the
				 * for_each_process loop, we need to start over with that loop.
				 * That's why we have the list_empty here */
				task_exited = list_empty(&p->tasks);
				put_task_struct(p);
				if ((init_result < 0) || task_exited) {
					break;
				}
			}
			read_unlock(&tasklist_lock);
		} while ((init_result >= 0) && task_exited);

		/*
		 * if anything went wrong during initialisation abandon the
		 * registration process
		 */
		if (init_result < 0) {
			remove_client_paggs_from_all_tasks(pagg_hook_new);
			list_del_init(&pagg_hook_new->entry);
			up_write(&pagg_hook_list_sem);

			printk(KERN_WARNING "Registering PAGG support for"
				" (name=%s) failed\n", pagg_hook_new->name);

			return init_result; /* hook init function error result */
		}
	}

	up_write(&pagg_hook_list_sem);

	printk(KERN_INFO "Registering PAGG support for (name=%s)\n",
			pagg_hook_new->name);

	return 0;					/* success */

}

/**
 * pagg_hook_unregister - Unregister pagg hook and remove it from the list
 * @pagg_hook_old: The hook to unregister and remove
 *
 * Used to unregister pagg hooks and remove them from the pagg_hook_list.
 * Once the pagg hook entry in the pagg_hook_list is found, paggs associated
 * with the hook (if any) will have their detach function called and will
 * be detached.
 *
 */
int
pagg_hook_unregister(struct pagg_hook *pagg_hook_old)
{
	struct pagg_hook *pagg_hook;

	/* Check the validity of the arguments */
	if (!pagg_hook_old)
		return -EINVAL;			/* error */
	if (list_empty(&pagg_hook_old->entry))
		return -EINVAL;			/* error */
	if (pagg_hook_old->name == NULL)
		return -EINVAL;			/* error */

	down_write(&pagg_hook_list_sem);

	pagg_hook = get_pagg_hook(pagg_hook_old->name);

	if (pagg_hook && pagg_hook == pagg_hook_old) {
		remove_client_paggs_from_all_tasks(pagg_hook);
		list_del_init(&pagg_hook->entry);
		up_write(&pagg_hook_list_sem);

		printk(KERN_INFO "Unregistering PAGG support for"
				" (name=%s)\n", pagg_hook_old->name);

		return 0;			/* success */
	}

	up_write(&pagg_hook_list_sem);

	printk(KERN_WARNING "Attempt to unregister PAGG support (name=%s)"
			" failed - not found\n", pagg_hook_old->name);

	return -EINVAL;				/* error */
}


/**
 * __pagg_attach - Attach a new task to the same containers of its parent
 * @to_task: The child task that will inherit the parent's containers
 * @from_task: The parent task
 *
 * Used to attach a new task to the same pagg containers to which it's parent
 * is attached.
 *
 * The "from" argument is the parent task.  The "to" argument is the child
 * task.
 *
 * The user-defined pagg hook attach function pointer is run for the new
 * task.  The return values are interpreted this way:
 * <0 - Fail the process, results in fork failure in copy_process
 *  0 - Success
 * >0 - Sucess, but don't track this process - get rid of the pagg for it
 *
 *
 */
int
__pagg_attach(struct task_struct *to_task, struct task_struct *from_task)
{
	struct pagg *from_pagg;
	int ret;

	/* lock the parents pagg_list we are copying from */
	down_read(&from_task->pagg_sem); /* read lock the pagg list */

	list_for_each_entry(from_pagg, &from_task->pagg_list, entry) {
		struct pagg *to_pagg = NULL;

		to_pagg = pagg_alloc(to_task, from_pagg->hook);
		if (!to_pagg) {
			/* Failed to get memory.
			 * We don't need a write lock on the pagg
			 * list because the child is in construction.
			 * pagg_detach is run in copy_process for failed
			 * forks and will clean up the other paggs.
			 */
			up_read(&from_task->pagg_sem);
			return -ENOMEM;
		}

		ret = to_pagg->hook->attach(to_task, to_pagg,
		  from_pagg->data);

		if (ret < 0) {
			/* Propagates to copy_process as a fork failure.
			 * Since the child is in construction, we don't
			 * need a write lock on the pagg list.
			 * pagg_detach is run in copy_process for failed
			 * forks and will clean up the other paggs.
			 */
			pagg_free(to_pagg);
			up_read(&from_task->pagg_sem);
			return ret; /* Fork failure */
		}
		else if (ret > 0) {
			/* Success, but fork function pointer in the
			 * pagg_hook structure doesn't want the kernel
			 * module pagg assocation.  This is an in-construction
			 * child so we don't need to write lock */
			pagg_free(to_pagg);
		}
	}

	/* unlock parent's pagg list */
	up_read(&from_task->pagg_sem);

	return 0; /* success */
}


/**
 * __pagg_detach - Detach a task from all pagg containers it is attached to
 * @task: Task to detach from pagg containers
 *
 * Used to detach a task from all pagg containers to which it is attached.
 *
 */
void
__pagg_detach(struct task_struct *task)
{
	struct pagg *pagg;
	struct pagg *paggtmp;

	/* Remove ref. to paggs from task immediately */
	down_write(&task->pagg_sem); /* write lock pagg list */

	list_for_each_entry_safe(pagg, paggtmp, &task->pagg_list, entry) {
		pagg->hook->detach(task, pagg);
		pagg_free(pagg);
	}

	up_write(&task->pagg_sem); /* write unlock the pagg list */

	return;   /* 0 = success, else return last code for failure */
}


/**
 * __pagg_exec - Execute callback when a process in a container execs
 * @task: We go through the pagg list in the given task
 *
 * Used to when a process that is in a pagg container does an exec.
 *
 * The "from" argument is the task.  The "name" argument is the name
 * of the process being exec'ed.
 *
 */
int
__pagg_exec(struct task_struct *task)
{
	struct pagg	*pagg;

	/* lock the parents pagg_list we are copying from */
	down_read(&task->pagg_sem); /* lock the pagg list */

	list_for_each_entry(pagg, &task->pagg_list, entry) {
		if (pagg->hook->exec) /* conditional because it's optional */
			pagg->hook->exec(task, pagg);
	}

	up_read(&task->pagg_sem); /* unlock the pagg list */
	return 0;
}


EXPORT_SYMBOL_GPL(pagg_get);
EXPORT_SYMBOL_GPL(pagg_alloc);
EXPORT_SYMBOL_GPL(pagg_free);
EXPORT_SYMBOL_GPL(pagg_hook_register);
EXPORT_SYMBOL_GPL(pagg_hook_unregister);
