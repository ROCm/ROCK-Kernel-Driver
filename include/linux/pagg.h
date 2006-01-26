/*
 * PAGG (Process Aggregates) interface
 *
 *
 * Copyright (c) 2000-2002, 2004 Silicon Graphics, Inc.  All Rights Reserved.
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
 *
 * Contact information:  Silicon Graphics, Inc., 1500 Crittenden Lane,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

/*
 * Data structure definitions and function prototypes used to implement
 * process aggregates (paggs).
 *
 * Paggs provides a generalized way to implement process groupings or
 * containers.  Modules use these functions to register with the kernel as
 * providers of process aggregation containers. The pagg data structures
 * define the callback functions and data access pointers back into the
 * pagg modules.
 */

#ifndef _LINUX_PAGG_H
#define _LINUX_PAGG_H

#include <linux/sched.h>

#ifdef CONFIG_PAGG

#define PAGG_NAMELN	32		/* Max chars in PAGG module name */


/**
 * INIT_PAGG_LIST - used to initialize a pagg_list structure after declaration
 * @_l: Task struct to init the pagg_list and semaphore in
 *
 */
#define INIT_PAGG_LIST(_l)						\
do {									\
	INIT_LIST_HEAD(&(_l)->pagg_list);					\
	init_rwsem(&(_l)->pagg_sem);						\
} while(0)


/*
 * Used by task_struct to manage list of pagg attachments for the process.
 * Each pagg provides the link between the process and the
 * correct pagg container.
 *
 * STRUCT MEMBERS:
 *     hook:	Reference to pagg module structure.  That struct
 *     		holds the name key and function pointers.
 *     data:	Opaque data pointer - defined by pagg modules.
 *     entry:	List pointers
 */
struct pagg {
       struct pagg_hook	*hook;
       void		*data;
       struct list_head	entry;
};

/*
 * Used by pagg modules to define the callback functions into the
 * module.
 *
 * STRUCT MEMBERS:
 *     name:           The name of the pagg container type provided by
 *                     the module. This will be set by the pagg module.
 *     attach:         Function pointer to function used when attaching
 *                     a process to the pagg container referenced by
 *                     this struct.
 *     detach:         Function pointer to function used when detaching
 *                     a process to the pagg container referenced by
 *                     this struct.
 *     init:           Function pointer to initialization function.  This
 *                     function is used when the module is loaded to attach
 *                     existing processes to a default container as defined by
 *                     the pagg module. This is optional and may be set to
 *                     NULL if it is not needed by the pagg module.
 *     data:           Opaque data pointer - defined by pagg modules.
 *     module:         Pointer to kernel module struct.  Used to increment &
 *                     decrement the use count for the module.
 *     entry:	       List pointers
 *     exec:           Function pointer to function used when a process
 *                     in the pagg container exec's a new process. This
 *                     is optional and may be set to NULL if it is not
 *                     needed by the pagg module.
 *     refcnt:         Keep track of user count of the pagg hook
 */
struct pagg_hook {
       struct module	*module;
       char		*name;	/* Name Key - restricted to 32 characters */
       void		*data;	/* Opaque module specific data */
       struct list_head	entry;	/* List pointers */
		 atomic_t refcnt; /* usage counter */
       int		(*init)(struct task_struct *, struct pagg *);
       int		(*attach)(struct task_struct *, struct pagg *, void*);
       void		(*detach)(struct task_struct *, struct pagg *);
       void		(*exec)(struct task_struct *, struct pagg *);
};


/* Kernel service functions for providing PAGG support */
extern struct pagg *pagg_get(struct task_struct *task, char *key);
extern struct pagg *pagg_alloc(struct task_struct *task,
			       struct pagg_hook *pt);
extern void pagg_free(struct pagg *pagg);
extern int pagg_hook_register(struct pagg_hook *pt_new);
extern int pagg_hook_unregister(struct pagg_hook *pt_old);
extern int __pagg_attach(struct task_struct *to_task,
			 struct task_struct *from_task);
extern void __pagg_detach(struct task_struct *task);
extern int __pagg_exec(struct task_struct *task);

/**
 * pagg_list_empty - Check to see if the task's pagg_list is empty.
 * @task: The task in question
 *
 */
static inline int pagg_list_empty(const struct task_struct *task)
{
	return list_empty(&task->pagg_list);
}

/**
 * pagg_attach - child inherits attachment to pagg containers of its parent
 * @child: child task - to inherit
 * @parent: parenet task - child inherits pagg containers from this parent
 *
 * function used when a child process must inherit attachment to pagg
 * containers from the parent.
 *
 */
static inline int pagg_attach(struct task_struct *child,
			      struct task_struct *parent)
{
	INIT_PAGG_LIST(child);
	if (!pagg_list_empty(parent))
		return __pagg_attach(child, parent);

	return 0;
}


/**
 * pagg_detach - Detach a process from a pagg container it is a member of
 * @task: The task the pagg will be detached from
 *
 */
static inline void pagg_detach(struct task_struct *task)
{
	if (!pagg_list_empty(task))
		__pagg_detach(task);
}

/**
 * pagg_exec - Used when a process exec's
 * @task: The process doing the exec
 *
 */
static inline void pagg_exec(struct task_struct *task)
{
	if (!pagg_list_empty(task))
		__pagg_exec(task);
}

/**
 * INIT_TASK_PAGG - Used in INIT_TASK to set the head and sem of pagg_list
 * @tsk: The task work with
 *
 * Marco Used in INIT_TASK to set the head and sem of pagg_list.
 * If CONFIG_PAGG is off, it is defined as an empty macro below.
 *
 */
#define INIT_TASK_PAGG(tsk) \
	.pagg_list = LIST_HEAD_INIT(tsk.pagg_list),     \
	.pagg_sem  = __RWSEM_INITIALIZER(tsk.pagg_sem),

#else  /* CONFIG_PAGG */

/*
 * Replacement macros used when PAGG (Process Aggregates) support is not
 * compiled into the kernel.
 */
#define INIT_TASK_PAGG(tsk)
#define INIT_PAGG_LIST(l) do { } while(0)
#define pagg_attach(ct, pt)  (0)
#define pagg_detach(t)  do {  } while(0)
#define pagg_exec(t)  do {  } while(0)
#define pagg_list_empty(t) (1)

#endif /* CONFIG_PAGG */

#endif /* _LINUX_PAGG_H */
