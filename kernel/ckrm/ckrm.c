/* ckrm.c - Class-based Kernel Resource Management (CKRM)
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 *           (C) Shailabh Nagar,  IBM Corp. 2003
 *           (C) Chandra Seetharaman,  IBM Corp. 2003
 *	     (C) Vivek Kashyap,	IBM Corp. 2004
 * 
 * 
 * Provides kernel API of CKRM for in-kernel,per-resource controllers 
 * (one each for cpu, memory, io, network) and callbacks for 
 * classification modules.
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 *
 * 28 Aug 2003
 *        Created.
 * 06 Nov 2003
 *        Made modifications to suit the new RBCE module.
 * 10 Nov 2003
 *        Fixed a bug in fork and exit callbacks. Added callbacks_active and
 *        surrounding logic. Added task paramter for all CE callbacks.
 * 23 Mar 2004
 *        moved to referenced counted class objects and correct locking
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <asm/errno.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/ckrm_ce.h>
#include <linux/ckrm_rc.h>
#include <net/sock.h>
#include <linux/ip.h>

/*
 * callback structures for each registered resource controller.
 */
ckrm_res_callback_t ckrm_res_ctlrs[CKRM_MAX_RES_CTLRS];
static spinlock_t ckrm_res_ctlrs_lock = SPIN_LOCK_UNLOCKED; // protects the array
long bit_res_ctlrs = 0; // bit set for registered resource controllers
atomic_t nr_resusers[CKRM_MAX_RES_CTLRS]; // no of users for each res ctlr
static int max_resid = 0; // highest resource id currently used.


EXPORT_SYMBOL(ckrm_res_ctlrs);
EXPORT_SYMBOL(nr_resusers);

/*
 * Callback registered by the classification engine.
 * The chances of two engines trying to register/unregister at the same
 * moment is very less. So, it is ok to not have any locking mechanism
 * protecting ckrm_eng_callbacks data structure.
 */
static ckrm_eng_callback_t ckrm_eng_callbacks;
static atomic_t nr_engusers; // Number of users using the data structure above.
// global variables
static int ckrm_num_classes = 2; // no need to hold lock for reading
static LIST_HEAD(ckrm_classes); // hold read lock for read, write lock for write
rwlock_t ckrm_class_lock = RW_LOCK_UNLOCKED;  // protect above 3 variables

// Default classes
struct ckrm_core_class ckrm_dflt_class = { .class_type = CKRM_TASK_CLASS };
struct ckrm_core_class ckrm_net_root   = { .class_type = CKRM_NET_CLASS };

EXPORT_SYMBOL(ckrm_dflt_class);
EXPORT_SYMBOL(ckrm_net_root);

// is classification engine callback active ?
static int callbacks_active = 0;


/**************************************************************************
 *                   Helper Functions                                     *
 **************************************************************************/

/*
 * Return TRUE if the given core class pointer is valid.
 */
inline unsigned int
is_core_valid(ckrm_core_class_t *core)
{
	return (core && (core->magic == CKRM_CORE_MAGIC) &&
		((core->class_type == CKRM_TASK_CLASS) || 
		 (core->class_type == CKRM_NET_CLASS)));
}

/*
 * Return TRUE if the given resource is registered.
 */
inline unsigned int
is_res_regd(int resid)
{
	if (resid < 0 || resid > CKRM_MAX_RES_CTLRS)
		return 0;
	else
		return (test_bit(resid, &bit_res_ctlrs));
}

int 
ckrm_resid_lookup(char *resname)
{
	int resid = -1;
	
	//for_each_resid(resid) {
	for (resid=0; resid < CKRM_MAX_RES_CTLRS; resid++) { 
		if (is_res_regd(resid)) {
			if (!strncmp(resname, ckrm_res_ctlrs[resid].res_name,
							CKRM_MAX_RES_NAME))
				return resid;
		}
	}
	return resid ;
}


/* given a classname return the class handle */
void *
ckrm_classobj(char *classname)
{
	struct ckrm_core_class *tmp, *core = NULL;

	if (!classname || !*classname) {
		return NULL;
	}
	read_lock(&ckrm_class_lock);
	list_for_each_entry(tmp, &ckrm_classes, clslist) {
		if (!strcmp(tmp->dentry->d_name.name, classname)) {
			core = tmp;
			break;
		}
	}
	read_unlock(&ckrm_class_lock);
	return core;
}


EXPORT_SYMBOL(is_core_valid);
EXPORT_SYMBOL(is_res_regd);
EXPORT_SYMBOL(ckrm_resid_lookup);
EXPORT_SYMBOL(ckrm_classobj);


/**************************************************************************
 *                   Internal Functions/macros                            *
 **************************************************************************/

#define hnode_2_core(ptr) \
		((ptr) ? container_of(ptr, struct ckrm_core_class, hnode) : NULL)

#define ce_protect()      (atomic_inc(&nr_engusers))
#define ce_release()      (atomic_dec(&nr_engusers))
#define CE_PROTECTED(cmd) do { ce_protect();  cmd ; ce_release(); } while (0)  // wrapper for code


// CE callback that takes one parameter and returns nothing
#define CECB_1ARG_NORET(fn, arg)				\
do {								\
	if (callbacks_active && ckrm_eng_callbacks.fn) {	\
		(*ckrm_eng_callbacks.fn)(arg);			\
	}							\
} while (0) 

// CE callback that takes one parameter and returns, whatever the function
// returns
#define CECB_1ARG_RET(fn, arg, ret)				\
do {								\
	if (callbacks_active && ckrm_eng_callbacks.fn) {	\
		ret = (*ckrm_eng_callbacks.fn)(arg);		\
	}							\
} while (0)

// CE callback that takes two parameteasr and returns nothing
#define CECB_2ARGS_NORET(fn, arg1, arg2)			\
do {								\
	if (callbacks_active && ckrm_eng_callbacks.fn) {	\
		(*ckrm_eng_callbacks.fn)(arg1, arg2);		\
	}							\
} while (0)

// CE callback that takes two parameters and returns whatever the function
// returns
#define CECB_2ARGS_RET(fn, arg1, arg2, ret)			\
{								\
	if (callbacks_active && ckrm_eng_callbacks.fn) {	\
		ret = (*ckrm_eng_callbacks.fn)(arg1, arg2);	\
	}							\
}

// CE callback that takes three parameters and returns nothing
#define CECB_3ARGS_NORET(fn, arg1, arg2, arg3)			\
do {								\
	if (callbacks_active && ckrm_eng_callbacks.fn) {	\
		(*ckrm_eng_callbacks.fn)(arg1, arg2, arg3);	\
	}							\
} while (0)



static inline void 
set_callbacks_active(void)
{
	callbacks_active = ((atomic_read(&nr_engusers) > 0) &&
			    (ckrm_eng_callbacks.always_callback || (ckrm_num_classes > 1)));
}

inline void
ckrm_core_grab(void *core)
{
	ckrm_core_class_t *lcore = core;
	if (lcore) {
		atomic_inc(&lcore->refcnt);
 	}
}

inline void
ckrm_core_drop(void *core)
{
	ckrm_core_class_t *lcore = core;
	if (lcore) {
		atomic_dec(&lcore->refcnt);
 	}
}

void
ckrm_reclassify_net(struct ckrm_net_struct *ns, ckrm_core_class_t *core, int action)
{
	int i;
	ckrm_res_callback_t *rcbs;
	ckrm_core_class_t *old_core;
	void  *old_res_class, *new_res_class;

	// remove the net_struct from the current class
	if ((old_core = ns->core) != NULL) {
		spin_lock(&old_core->ckrm_lock);
		list_del(&ns->ckrm_link);
		ns->core = NULL;
		spin_unlock(&old_core->ckrm_lock);
	}
	
	if (core != NULL) {
		spin_lock(&core->ckrm_lock);
		ns->core = core;
		list_add(&ns->ckrm_link, &core->tasklist);
		spin_unlock(&core->ckrm_lock);
	}

	if (core != old_core) {
		for (i = 0; i < max_resid; i++) {
			rcbs = &ckrm_res_ctlrs[i];
			old_res_class = old_core ? 
				old_core->res_class[i] : NULL;
			new_res_class = core ? core->res_class[i] : NULL;
			if (rcbs->change_resclass && 
				(old_res_class != new_res_class)) 
					(*rcbs->change_resclass)
						((void *)ns,old_res_class, 
						 	new_res_class);
		}
	}
	return;
}


/*
 * Change the core class of the given task.
 *
 * Change the task's core class  to "core" if the task's current 
 * core(task->ckrm_core) is same as given "oldcore", if it is non-NULL.
 *
 * Caller is responsible to make sure the task structure stays put through
 * this function.
 *
 * This function should be called with the following locks not held
 * 	- task_lock
 * 	- core->ckrm_lock, if core is NULL then ckrm_dflt_class.ckrm_lock
 * 	- tsk->ckrm_core->ckrm_lock
 * 
 * Function is also called with a ckrm_core_grab on the new core, hence
 * it needs to be dropped if no assignment takes place.
 */

static void
ckrm_set_taskclass(struct task_struct *tsk, ckrm_core_class_t *core, 
		   ckrm_core_class_t *oldcore, int action)
{
	int i;
	ckrm_res_callback_t *rcbs;
	ckrm_core_class_t *curr_core;
	void *old_res_class, *new_res_class;
	int drop_old_core;


	task_lock(tsk);
	curr_core = tsk->ckrm_core;

	// check whether compare_and_exchange should
	if (oldcore && (oldcore != curr_core)) {
		task_unlock(tsk);
		if (core) {
			/* compensate for previous grab */
			// printk("ckrm_set_taskclass(%s:%d): Race-condition caught <%s>\n",
			//	tsk->comm,tsk->pid,core->name);
			ckrm_core_drop(core);
		}
		return;
	}

	// make sure we have a real destination core
	if (!core) {
		core = &ckrm_dflt_class;
		ckrm_core_grab(core);
	}

	// take out of old class 
	// remember that we need to drop the oldcore
	if ((drop_old_core = (curr_core != NULL))) {
		spin_lock(&curr_core->ckrm_lock);
		if (core == curr_core) {
			// we are already in the destination class.
			// we still need to drop oldcore
			spin_unlock(&curr_core->ckrm_lock);
			task_unlock(tsk);
			goto out;
		}
		list_del(&tsk->ckrm_link);
		INIT_LIST_HEAD(&tsk->ckrm_link);
		tsk->ckrm_core = NULL;
		spin_unlock(&curr_core->ckrm_lock);
	}	

	// put into new class 
	spin_lock(&core->ckrm_lock);
	tsk->ckrm_core = core;
	list_add(&tsk->ckrm_link, &core->tasklist);
	spin_unlock(&core->ckrm_lock);

	if (core == curr_core) {
		task_unlock(tsk);
		goto out;
	}

	CECB_3ARGS_NORET(notify, tsk, core, action);

	task_unlock(tsk);

	for (i = 0; i < max_resid; i++) {
		atomic_inc(&nr_resusers[i]);
		rcbs = &ckrm_res_ctlrs[i];
		old_res_class = curr_core ? curr_core->res_class[i] : NULL;
		new_res_class = core ? core->res_class[i] : NULL;
		if (rcbs && rcbs->change_resclass && (old_res_class != new_res_class)) 
			(*rcbs->change_resclass)(tsk, old_res_class, new_res_class);
		atomic_dec(&nr_resusers[i]);
	}

 out:
	if (drop_old_core) 
		ckrm_core_drop(curr_core);
	return;
}

/**************************************************************************
 *                   Functions called from classification points          *
 **************************************************************************/

#define TASK_MAGIC 0xDEADB0DE
inline void
ckrm_new_task(struct task_struct *tsk)
{
	tsk->ckrm_core = NULL;
	tsk->ce_data   = NULL;
	INIT_LIST_HEAD(&tsk->ckrm_link);
	return;
}


#define CKRM_CB_TASK(function, tsk, action)					\
do {										\
	struct ckrm_core_class *core = NULL, *old_core = tsk->ckrm_core;	\
										\
	CECB_1ARG_RET(function, tsk, core);					\
	if (core) {								\
		/* called synchrously. no need to get task struct */		\
		ckrm_set_taskclass(tsk, core, old_core, action);		\
	}									\
} while (0)


#define CKRM_CB_NET(function, tsk, action)	\
do {						\
	struct ckrm_core_class *core = NULL;	\
						\
	CECB_1ARG_RET(function, tsk, core);	\
	if (core && (core != tsk->core)) {	\
		ckrm_reclassify_net(tsk, core, action);	\
	}					\
} while (0)

void
ckrm_cb_exec(const char *filename)
{
	struct ckrm_core_class *core = NULL, *old_core = current->ckrm_core;

	ce_protect();
	CECB_2ARGS_RET(exec, current, filename, core);

	// called synchrously. no need to get task struct
	ckrm_set_taskclass(current, core, old_core, CKRM_ACTION_EXEC);
	ce_release();
	return;
}

void
ckrm_cb_fork(struct task_struct *tsk)
{
	struct ckrm_core_class *core = NULL;

	ce_protect();
	CECB_1ARG_RET(fork, tsk, core);
	if (core == NULL) {
		task_lock(tsk->parent);
		core = tsk->parent->ckrm_core;
		ckrm_core_grab(core);
		task_unlock(tsk->parent);
	}
	if (!list_empty(&tsk->ckrm_link))
		printk("BUG in cb_fork.. tsk (%s:%d> already linked\n",
			tsk->comm,tsk->pid);

	// called synchrously. no need to get task struct
	ckrm_set_taskclass(tsk, core, NULL, CKRM_ACTION_FORK);
	ce_release();
	return;
}

void
ckrm_cb_listen(struct sock *sk)
{
	struct ckrm_net_struct *ns;
	struct ckrm_core_class *core;

	ns = (struct ckrm_net_struct *)
		kmalloc(sizeof(struct ckrm_net_struct), GFP_KERNEL);
	if (!ns)
		return;

	ns->family = sk->sk_family;
	ns->daddr4 = inet_sk(sk)->daddr;
	ns->dport = inet_sk(sk)->dport;

	CE_PROTECTED ( CKRM_CB_NET(listen, ns, CKRM_ACTION_LISTEN) );

	return ;        /* Hubertus (3/26):  <core> is never set */

	// FIXME: where is this core coming from ?
	// core in _CB4 is in different context
	// what if core is NULL ?
	spin_lock(&core->ckrm_lock);
	read_lock(&tasklist_lock);
	ns->core = core;
	printk("ckrm_cb_listen: adding %p to tasklist of %s\n", ns, core->name);
	list_add(&ns->ckrm_link, &core->tasklist);
	read_unlock(&tasklist_lock);
	spin_unlock(&core->ckrm_lock);	
}

void
ckrm_cb_exit(struct task_struct *tsk)
{
	ckrm_core_class_t *core;

	// Remove the task from the current core class
	

	task_lock(tsk);

	CE_PROTECTED ( CECB_1ARG_NORET(exit, tsk) );

	if ((core = tsk->ckrm_core) != NULL) {
		spin_lock(&core->ckrm_lock);
		tsk->ckrm_core = NULL;
		tsk->ce_data = NULL;
		list_del(&tsk->ckrm_link);
		ckrm_core_drop(core);
		spin_unlock(&core->ckrm_lock);
	} else {
		tsk->ce_data = NULL;
		INIT_LIST_HEAD(&tsk->ckrm_link);
	}
	task_unlock(tsk);

	return;
}

void
ckrm_cb_uid(void)
{
	CE_PROTECTED ( CKRM_CB_TASK(uid, current, CKRM_ACTION_UID) );
}

void
ckrm_cb_gid(void)
{
	CE_PROTECTED ( CKRM_CB_TASK(gid, current, CKRM_ACTION_GID) );
}


/***********************************************************************
 *
 * Asynchronous callback functions   (driven by RCFS)
 * 
 *    Async functions force a setting of the task structure
 *    synchronous callbacks are protected against race conditions 
 *    by using a cmpxchg on the core before setting it.
 *    Async calls need to be serialized to ensure they can't 
 *    race against each other 
 *
 ***********************************************************************/

DECLARE_MUTEX(async_serializer);    // serialize all async functions


static inline int
validate_and_grab_core(struct ckrm_core_class *core)
{
	int rc = 0;
	read_lock(&ckrm_class_lock);
	if (likely(is_core_valid(core))) {
		ckrm_core_grab(core);
		rc = 1;
	}
	read_unlock(&ckrm_class_lock);
	return rc;
}

/*
 * Go through the task list and reclassify all tasks according to the current
 * classification rules.
 *
 * We have the problem that we can not hold any lock (including the 
 * tasklist_lock) while classifying. Two methods possible
 *
 * (a) go through entire pidrange (0..pidmax) and if a task exists at 
 *     that pid then reclassify it
 * (b) go several time through task list and build a bitmap for a particular 
 *     subrange of pid otherwise the memory requirements ight be too much.
 * 
 * We use a hybrid by comparing ratio nr_threads/pidmax
 */

static void
ckrm_reclassify_all_tasks(void)
{
	extern int pid_max;

	struct task_struct *proc, *thread;
	int i;
	int curpidmax = pid_max;
	int ratio;
	int use_bitmap;


	ratio = curpidmax / nr_threads;
	if (curpidmax <= PID_MAX_DEFAULT) {
	     use_bitmap = 1;
	} else {
	     use_bitmap = (ratio >= 2);
	}

	ce_protect();

 retry:		
	if (use_bitmap == 0) {
		// go through it in one walk
		read_lock(&tasklist_lock);
		for ( i=0 ; i<curpidmax ; i++ ) {
			if ((thread = find_task_by_pid(i)) == NULL) 
				continue;
			get_task_struct(thread);
			read_unlock(&tasklist_lock);
			CKRM_CB_TASK(reclassify, thread, CKRM_ACTION_RECLASSIFY);
			put_task_struct(thread);
			read_lock(&tasklist_lock);
		}
		read_unlock(&tasklist_lock);
	} else {
		unsigned long *bitmap;
		int bitmapsize;
		int order = 0;
		int num_loops;
		int pid;


		bitmap = (unsigned long*) __get_free_pages(GFP_KERNEL,order);
		if (bitmap == NULL) {
			ratio = 0;
			goto retry;
		}

		bitmapsize = 8 * (1 << (order + PAGE_SHIFT));
		num_loops  = (curpidmax + bitmapsize - 1) >> order;

		for ( i=0 ; i < num_loops; i++) {
			int pid_start = i*bitmapsize; 
			int pid_end   = pid_start + bitmapsize;
			int num_found = 0;
			int pos;

			memset(bitmap, 0, bitmapsize/8); // start afresh

			read_lock(&tasklist_lock);
			do_each_thread(proc, thread) {
				pid = thread->pid;
				if ((pid < pid_start) || (pid >= pid_end))
					continue;
				pid -= pid_start;
				set_bit(pid, bitmap);
				num_found++;
			} while_each_thread(proc, thread);
			read_unlock(&tasklist_lock);
		
			if (num_found == 0) 
				continue;

			pos = 0;
			for ( ; num_found-- ; ) {
				pos = find_next_bit(bitmap, bitmapsize, pos);
				pid = pos + pid_start;

				read_lock(&tasklist_lock);
				if ((thread = find_task_by_pid(pid)) != NULL) {
					get_task_struct(thread);
					read_unlock(&tasklist_lock);
					CKRM_CB_TASK(reclassify, thread, CKRM_ACTION_RECLASSIFY);
					put_task_struct(thread);
				} else {
					read_unlock(&tasklist_lock);
				}
			}
		}

	}
	ce_release();
}

int
ckrm_reclassify(int pid)
{
	struct task_struct *tsk;
	int rc = 0;

	down(&async_serializer);   // protect again race condition
	if (pid < 0) {
		// do we want to treat this as process group .. should YES ToDo
		 rc = -EINVAL;
	} else if (pid == 0) {
		// reclassify all tasks in the system
		ckrm_reclassify_all_tasks();
	} else {
		// reclassify particular pid
		read_lock(&tasklist_lock);
		if ((tsk = find_task_by_pid(pid)) != NULL) {
			get_task_struct(tsk);
			read_unlock(&tasklist_lock);
			CE_PROTECTED ( CKRM_CB_TASK(reclassify, tsk, CKRM_ACTION_RECLASSIFY) );
			put_task_struct(tsk);
		} else {
			read_unlock(&tasklist_lock);
			rc = -EINVAL;
		}
	}
	up(&async_serializer);
	return rc;
}

/*
 * Reclassify all tasks in the given core class.
 */

static void
ckrm_reclassify_class_tasks(struct ckrm_core_class *core)
{

	if (!validate_and_grab_core(core))
		return;

	down(&async_serializer);   // protect again race condition

next_task:
	spin_lock(&core->ckrm_lock);
	// Reclassify all tasks in the given core class.
	if (!list_empty(&core->tasklist)) {
		ckrm_core_class_t *new_core = NULL;
		struct task_struct *tsk = 
				list_entry(core->tasklist.next ,struct task_struct, ckrm_link);
		
		get_task_struct(tsk);
		spin_unlock(&core->ckrm_lock);

		CECB_1ARG_RET(reclassify, tsk, new_core);
		if (core == new_core) {
			// don't allow reclassifying to the same class
			// as we are in the process of cleaning up this class
			ckrm_core_drop(new_core); // to compensate CE's grab
			new_core = NULL;
		}
		ckrm_set_taskclass(tsk, new_core, core, CKRM_ACTION_RECLASSIFY);
		put_task_struct(tsk);
		goto next_task;
	}
	ckrm_core_drop(core);
	spin_unlock(&core->ckrm_lock);

	up(&async_serializer);

	return ;
}

/*
 * Change the core class of the given task.
 */

void
ckrm_forced_reclassify_pid(int pid, ckrm_core_class_t *core)
{
	struct task_struct *tsk;

	if (!validate_and_grab_core(core))
		return;

	if (core->class_type == CKRM_TASK_CLASS) {
		read_lock(&tasklist_lock);
		if ((tsk = find_task_by_pid(pid)) == NULL) {
			read_unlock(&tasklist_lock);
			return;
		}
		get_task_struct(tsk);
		read_unlock(&tasklist_lock);

		down(&async_serializer);   // protect again race condition

		ce_protect();
		CECB_1ARG_NORET(manual, tsk);

		ckrm_set_taskclass(tsk, core, NULL, CKRM_ACTION_MANUAL);
		put_task_struct(tsk);
		ce_release();

		up(&async_serializer);
	}
	return;
}
EXPORT_SYMBOL(ckrm_forced_reclassify_pid);

/*
 * Change the core class of the given net struct
 */
void
ckrm_forced_reclassify_net(struct ckrm_net_struct *ns, ckrm_core_class_t *core)
{
	if (!is_core_valid(core))	
		return;

	if (core->class_type == CKRM_NET_CLASS) {
		ckrm_reclassify_net(ns, core, CKRM_ACTION_MANUAL);
	}
	return;
}

/****************************************************************************
 *           Interfaces for classification engine                           *
 ****************************************************************************/

/*
 * Registering a callback structure by the classification engine.
 *
 * Returns 0 on success -errno for failure.
 */
int
ckrm_register_engine(ckrm_eng_callback_t *ecbs)
{
	ce_protect();
	if (atomic_read(&nr_engusers) != 1) {
		// Some engine is acive, deregister it first.
		ce_release();
		return (-EBUSY);
	}
	
	if (strnlen(ecbs->ckrm_eng_name, CKRM_MAX_ENG_NAME) == 0 ||
	    ecbs->reclassify == NULL) {
		ce_release();
		return (-EINVAL);
	}

	ckrm_eng_callbacks = *ecbs;
	set_callbacks_active();
	return 0;
}

/*
 * Unregistering a callback structure by the classification engine.
 *
 * Returns 0 on success -errno for failure.
 */
int
ckrm_unregister_engine(ckrm_eng_callback_t *ecbs)
{
	callbacks_active = 0; 

	if (atomic_dec_and_test(&nr_engusers) != 1) {
		// Somebody is currently using the engine, cannot deregister.
		atomic_inc(&nr_engusers);
		return (-EBUSY);
	}

	if (strncmp(ckrm_eng_callbacks.ckrm_eng_name, ecbs->ckrm_eng_name,
				CKRM_MAX_ENG_NAME) != 0) {
		atomic_inc(&nr_engusers);
		// Somebody other than the owner is trying to unregister.
		return (-EINVAL);
	}
	memset(&ckrm_eng_callbacks, 0, sizeof(ckrm_eng_callbacks));
	set_callbacks_active();
	return 0;
}

/*
 * functions to manipulate class (core or resource) hierarchies 
 */

#ifndef NEW_HNODE_IMPLMN
/*
 * functions to manipulate class (core or resource) hierarchies 
 */

/* Caller must ensure ckrm_class_lock held */
void
ckrm_hnode_add(struct ckrm_hnode *node, struct ckrm_hnode *parent)
{
	node->parent = parent;
	INIT_LIST_HEAD(&node->children);
	INIT_LIST_HEAD(&node->siblings);

 	if (parent) {
		if (!is_core_valid(hnode_2_core(parent)))
			printk(KERN_ERR "hnode_add: non-NULL invalid parent\n");
		else {
			if (&parent->children)
				list_add(&node->siblings, &parent->children);
			else
				printk(KERN_ERR "hnode_add: parent->children not initialized\n");
		}
	}
	
}

/* Caller must ensure ckrm_class_lock held */
int
ckrm_hnode_remove(struct ckrm_hnode *node)
{
	/* ensure that the node does not have children */
	if (!list_empty(&node->children))
		return 0;
	list_del(&node->siblings);
	node->parent = NULL;
	return 1;
}

#else
/* 
 */
static void
ckrm_add_child(struct ckrm_core_class *parent, struct ckrm_core_class *child)
{
	struct ckrm_hnode *cnode = &child->hnode;

	if (!is_core_valid(child)) {
		printk(KERN_ERR "Invalid child %p given in ckrm_add_child\n", child);
		return;
	}
	
	spin_lock(&child->ckrm_lock);
	INIT_LIST_HEAD(&cnode->children);
	INIT_LIST_HEAD(&cnode->siblings);

 	if (parent) {
		struct ckrm_hnode *pnode;

		if (!is_core_valid(parent)) {
			printk(KERN_ERR "Invalid parent %p given in ckrm_add_child\n",
					parent);
			parent = NULL;
		} else {
			pnode = &parent->hnode;
			write_lock(&parent->hnode_rwlock);
			list_add(&cnode->siblings, &pnode->children);
			write_unlock(&parent->hnode_rwlock);
		}
	}
	cnode->parent = parent;
	spin_unlock(&child->ckrm_lock);
	return;
}

/* 
 */
static int
ckrm_remove_child(struct ckrm_core_class *child)
{
	struct ckrm_hnode *cnode, *pnode;
	struct ckrm_core_class *parent;

	if (!is_core_valid(child)) {
		printk(KERN_ERR "Invalid child %p given in ckrm_remove_child\n", child);
		return 0;
	}

	cnode = &child->hnode;
	parent = cnode->parent;
	if (!is_core_valid(parent)) {
		printk(KERN_ERR "Invalid parent %p in ckrm_remove_child\n", parent);
		return 0;
	}

	pnode = &parent->hnode;

	if (cnode->parent != parent) {
		printk(KERN_ERR "Invalid parent %p in child %p in ckrm_remove_child\n",
				parent, child);
		return 0;
	}

	spin_lock(&child->ckrm_lock);
	/* ensure that the node does not have children */
	if (!list_empty(&cnode->children)) {
		spin_unlock(&child->ckrm_lock);
		return 0;
	}
	write_lock(&parent->hnode_rwlock);
	list_del(&cnode->siblings);
	write_unlock(&parent->hnode_rwlock);
	cnode->parent = NULL;
	spin_unlock(&child->ckrm_lock);
	return 1;
}

void
ckrm_lock_hier(struct ckrm_core_class *parent)
{
	if (is_core_valid(parent)) {
		read_lock(&parent->hnode_rwlock);
	}
}

void 
ckrm_unlock_hier(struct ckrm_core_class *parent)
{
	if (is_core_valid(parent)) {
		read_unlock(&parent->hnode_rwlock);
	}
}

/*
 * hnode_rwlock of the parent core class must held in read mode.
 * external callers should 've called ckrm_lock_hier before calling this
 * function.
 */
struct ckrm_core_class *
ckrm_get_next_child(struct ckrm_core_class *parent,
			struct ckrm_core_class *child)
{
	struct ckrm_hnode *next_cnode;
	struct ckrm_core_class *next_childcore;

	if (!is_core_valid(parent)) {
		printk(KERN_ERR "Invalid parent %p in ckrm_get_next_child\n", parent);
		return NULL;
	}
	if (list_empty(&parent->hnode.children)) {
		return NULL;
	}

	if (child) {
		if (!is_core_valid(child)) {
			printk(KERN_ERR "Invalid child %p in ckrm_get_next_child\n", child);
			return NULL;
		}
		next_cnode = (struct ckrm_hnode *) child->hnode.siblings.next;
	} else {
		next_cnode = (struct ckrm_hnode *) parent->hnode.children.next;
	}
	next_childcore = hnode_2_core(next_cnode);

	if (next_childcore == parent) { // back at the anchor
		return NULL;
	}

	if (!is_core_valid(next_childcore)) {
		printk(KERN_ERR "Invalid next child %p in ckrm_get_next_child\n",
				next_childcore);
		return NULL;
	}
	return next_childcore;
}

#endif // NEW_HNODE_IMPLMN

static void 
ckrm_alloc_res_class(struct ckrm_core_class *core,
				 struct ckrm_core_class *parent,
				 int resid)
{
	/* 
	 * Allocate a resource class only if the resource controller has
	 * registered with core and the engine requests for the class.
	 */

	if (!is_core_valid(core))
		return ; 

	
	core->res_class[resid] = NULL;

	if (test_bit(resid, &bit_res_ctlrs)) {
		ckrm_res_callback_t *rcbs;

		atomic_inc(&nr_resusers[resid]);
		rcbs = &ckrm_res_ctlrs[resid];
		
		if (rcbs && rcbs->res_alloc) {
			core->res_class[resid] =(*rcbs->res_alloc)(core,parent);
			if (!core->res_class[resid]) {
				printk(KERN_ERR "Error creating res class\n");
				atomic_dec(&nr_resusers[resid]);
			}
		} else {
			atomic_dec(&nr_resusers[resid]);
		}
	}
}

/*
 * Allocate a core class, which in turn allocates resource classes as
 * specified by the res_mask parameter.
 *
 * Return the handle to the core class on success, NULL on failure.
 */


struct ckrm_core_class *
ckrm_alloc_core_class(struct ckrm_core_class *parent, struct dentry *dentry)
{
	int i;
	ckrm_core_class_t *dcore;

	if (!is_core_valid(parent))
		return NULL; 
	
	dcore = kmalloc(sizeof(ckrm_core_class_t), GFP_KERNEL);
	if (dcore == NULL) 
		return NULL;

	dcore->magic = CKRM_CORE_MAGIC;
	dcore->dentry = dentry;
	INIT_LIST_HEAD(&dcore->tasklist);
	dcore->ckrm_lock = SPIN_LOCK_UNLOCKED;

	atomic_set(&dcore->refcnt, 0);
	memset(dcore->name, 0, 16);
	strcpy(dcore->name, dentry->d_name.name);
	write_lock(&ckrm_class_lock);

	list_add(&dcore->clslist,&ckrm_classes);
#ifndef NEW_HNODE_IMPLMN
	ckrm_hnode_add(&dcore->hnode, parent ? &parent->hnode : NULL); 
	ckrm_num_classes++;
	set_callbacks_active();
	write_unlock(&ckrm_class_lock);

#else
	ckrm_num_classes++;
	set_callbacks_active();
	write_unlock(&ckrm_class_lock);

	ckrm_add_child(parent, dcore); 
#endif


	dcore->class_type = parent->class_type;
	for (i = 0; i < CKRM_MAX_RES_CTLRS; i++) {
		if (dcore->class_type == ckrm_res_ctlrs[i].res_type)
			ckrm_alloc_res_class(dcore,parent,i);
		else
			dcore->res_class[i] = NULL;
	}


	/* Inform CE at last, once core is ready for use */
	CE_PROTECTED ( CECB_2ARGS_NORET(class_add, dcore->dentry->d_name.name, (void *)dcore) );
	return dcore;
}


static void 
ckrm_free_res_class(struct ckrm_core_class *core, int resid)
{
	/* 
	 * Free a resource class only if the resource controller has
	 * registered with core 
	 */

	if (core->res_class[resid]) {
		ckrm_res_callback_t *rcbs;

		atomic_inc(&nr_resusers[resid]);
		rcbs = &ckrm_res_ctlrs[resid];

		if (rcbs->res_free) {
			(*rcbs->res_free)(core->res_class[resid]);
			atomic_dec(&nr_resusers[resid]); // for inc in alloc
			core->res_class[resid] = NULL;	
		}
		atomic_dec(&nr_resusers[resid]);
	}
}


/*
 * Free a core class 
 *   requires that all tasks were previously reassigned to another class
 *
 * Returns 0 on success -errno on failure.
 */
int
ckrm_free_core_class(struct ckrm_core_class *core)
{
	int i;
	int retry_count = 10;

	if (core == &ckrm_dflt_class) {
		// cannot remove the default class
		return -EINVAL;
	}

	if (!is_core_valid(core)) {
		// Invalid core
		return (-EINVAL);
	}

	/* Inform CE first, in case it needs any core class data */
	CE_PROTECTED ( CECB_2ARGS_NORET(class_delete, core->dentry->d_name.name, (void *)core) );

retry_class:
	ckrm_reclassify_class_tasks(core);

	if (atomic_read(&core->refcnt) > 0) {
		// at least try one more time
		printk("Core class <%s> removal failed. refcount non-zero %d  retries=%d\n",
		       core->name,atomic_read(&core->refcnt),retry_count-1);
		// ckrm_debug_free_core_class(core);
		if (--retry_count) { 
			schedule_timeout(100);
			goto retry_class;
		}
		
		return -EBUSY;
	}

#ifndef NEW_HNODE_IMPLMN
	if (ckrm_hnode_remove(&core->hnode) == 0) {
		printk("Core class removal failed. Chilren present\n");
		return -EBUSY;
	}
#else
	if (ckrm_remove_child(core) == 0) {
		printk("Core class removal failed. Chilren present\n");
		return -EBUSY;
	}
#endif

	for (i = 0; i < max_resid; i++) {
		ckrm_free_res_class(core,i);
	}

	write_lock(&ckrm_class_lock);

	// Clear the magic, so we would know if this core is reused.
	core->magic = 0;

	// Remove this core class from its linked list.
	list_del(&core->clslist);
	ckrm_num_classes--;
	set_callbacks_active();

	write_unlock(&ckrm_class_lock);

	kfree(core);
	return 0;
}

#ifndef NEW_HNODE_IMPLMN
EXPORT_SYMBOL(ckrm_hnode_add);
EXPORT_SYMBOL(ckrm_hnode_remove);
#endif

/****************************************************************************
 *           Interfaces for the resource controller                         *
 ****************************************************************************/
/*
 * Registering a callback structure by the resource controller.
 *
 * Returns the resource id(0 or +ve) on success, -errno for failure.
 */
static int
ckrm_register_res_ctlr_intern(ckrm_res_callback_t *rcbs)
{
	int resid, ret, i;
	
	if (!rcbs)
		return -EINVAL;

	resid = rcbs->resid;
	/*
	if (strnlen(rcbs->res_name, CKRM_MAX_RES_NAME) == 0 ||
	    rcbs->res_alloc        == NULL ||
	    rcbs->res_free         == NULL ||
	    rcbs->set_share_values == NULL ||
	    rcbs->get_share_values == NULL ||
	    rcbs->get_stats        == NULL ||
	    resid >= CKRM_MAX_RES_CTLRS ) 
	{
		// Name and the above functions are mandatory.
		return (-EINVAL);
	}

	*/
	spin_lock(&ckrm_res_ctlrs_lock);

//	ckrm_res_ctlrs[resid] = *rcbs;
	printk(KERN_WARNING "resid is %d name is %s %s\n", resid, rcbs->res_name,ckrm_res_ctlrs[resid].res_name);
//	atomic_set(&nr_resusers[resid], 0);
//	set_bit(resid, &bit_res_ctlrs);	
//	spin_unlock(&ckrm_res_ctlrs_lock);
//	return resid;

	if (resid >= 0) {
		if (strnlen(ckrm_res_ctlrs[resid].res_name,CKRM_MAX_RES_NAME) == 0) {
			ckrm_res_ctlrs[resid] = *rcbs;
			atomic_set(&nr_resusers[resid], 0);
			set_bit(resid, &bit_res_ctlrs);	
			ret = resid;
			if (resid >= max_resid) {
				max_resid = resid + 1;
			}
		} else {
			ret = -EBUSY;
		}
		spin_unlock(&ckrm_res_ctlrs_lock);
		return ret;
	}

	for (i = CKRM_RES_MAX_RSVD; i < CKRM_MAX_RES_CTLRS; i++) {
		if (strnlen(ckrm_res_ctlrs[i].res_name, CKRM_MAX_RES_NAME) == 0) {
			ckrm_res_ctlrs[i] = *rcbs;
			atomic_set(&nr_resusers[i], 0);
			set_bit(i, &bit_res_ctlrs);	
			if (i >= max_resid) {
				max_resid = i + 1;
			}
			spin_unlock(&ckrm_res_ctlrs_lock);
			return i;
		}
	}
	spin_unlock(&ckrm_res_ctlrs_lock);
	return (-ENOMEM);

}

int
ckrm_register_res_ctlr(ckrm_res_callback_t *rcbs)
{
	struct ckrm_core_class *core;
	int resid;

	resid = ckrm_register_res_ctlr_intern(rcbs);
	
	if (resid >= 0) {
		/* run through all classes and create the resource class object */
		
		read_lock(&ckrm_class_lock);
		list_for_each_entry(core, &ckrm_classes, clslist) {
			printk(KERN_ERR "CKRM .. create res clsobj for <%s>\n",rcbs->res_name);
#ifndef NEW_HNODE_IMPLMN
			ckrm_alloc_res_class(core, hnode_2_core(core->hnode.parent), resid);
#else
			ckrm_alloc_res_class(core, core->hnode.parent, resid);
#endif
		}
		read_unlock(&ckrm_class_lock);
	}
	return resid;
}

/*
 * Unregistering a callback structure by the resource controller.
 *
 * Returns 0 on success -errno for failure.
 */
int
ckrm_unregister_res_ctlr(int resid)
{
	ckrm_res_callback_t *rcbs;
//	struct ckrm_core_class *core;

	if (!is_res_regd(resid))
		return -EINVAL;

	rcbs = &ckrm_res_ctlrs[resid];

	if (atomic_read(&nr_resusers[resid])) {
		return -EBUSY;
	}

	spin_lock(&ckrm_res_ctlrs_lock);
	memset(&ckrm_res_ctlrs[resid], 0, sizeof(ckrm_res_callback_t));
	clear_bit(resid, &bit_res_ctlrs);	
	max_resid = fls(bit_res_ctlrs);
	spin_unlock(&ckrm_res_ctlrs_lock);

#if 0
	// FIXME: remove this part of code - chandra
	// NOT needed, as the original design was to not allow removal of
	// resource classes if any core class has a reference to the resource
	// class, made sure by the atomic variable nr_resusers.
	// Also if we want to change the design to work in this model, move this
	// code to the top, before we clear the resource controller data
	// structure with the memset above. - chandra
	//
	/* run through all classes and delete the resource class object */

	list_for_each_entry(core, &ckrm_classes, clslist) {
		printk(KERN_ERR "CKRM .. delete res clsobj for <%s>\n",rcbs->res_name);
		ckrm_free_res_class(core,resid);
	}
#endif
	return 0;
}

/*******************************************************************
 *   Initialization 
 *******************************************************************/

void __init
ckrm_init(void) 
{
	struct ckrm_core_class *core = &ckrm_dflt_class;
	struct task_struct *tsk;
	int i;

	while(core) {
		core->magic = CKRM_CORE_MAGIC;
		core->dentry = NULL;
		INIT_LIST_HEAD(&core->tasklist);
		core->ckrm_lock = SPIN_LOCK_UNLOCKED;
		for (i = 0; i < CKRM_MAX_RES_CTLRS; i++)
			core->res_class[i] = NULL;

#ifndef NEW_HNODE_IMPLMN
		ckrm_hnode_add(&core->hnode, NULL);
#else
		ckrm_add_child(NULL, core);
#endif

		if (core == &ckrm_dflt_class) {
			
			spin_lock(&core->ckrm_lock);
			memset(core->name, 0, 16);
			strcpy(core->name, "/rcfs");

			read_lock(&tasklist_lock);
			for_each_process(tsk) {
				task_lock(tsk);
				tsk->ckrm_core = core;
				INIT_LIST_HEAD(&tsk->ckrm_link);
				list_add(&tsk->ckrm_link, &core->tasklist);
				task_unlock(tsk);
				ckrm_core_grab(core);
				//printk("ckrm_init: Added %ld to %p\n",(long)tsk->pid,core);
			}
			read_unlock(&tasklist_lock);
			
			spin_unlock(&core->ckrm_lock);	
		} else {
			memset(core->name, 0, 16);
			strcpy(core->name, "/net");
		}

		// Add the default class to the global classes list.
		// ckrm_num_classes already incremented
		write_lock(&ckrm_class_lock);
		list_add(&core->clslist, &ckrm_classes);
		write_unlock(&ckrm_class_lock);

		core = (core==&ckrm_dflt_class)?&ckrm_net_root:NULL;
	}

	printk("CKRM Initialized\n");
}


EXPORT_SYMBOL(ckrm_register_engine);
EXPORT_SYMBOL(ckrm_unregister_engine);

EXPORT_SYMBOL(ckrm_register_res_ctlr);
EXPORT_SYMBOL(ckrm_unregister_res_ctlr);

EXPORT_SYMBOL(ckrm_alloc_core_class);
EXPORT_SYMBOL(ckrm_free_core_class);

EXPORT_SYMBOL(ckrm_reclassify);
EXPORT_SYMBOL(ckrm_core_grab);
EXPORT_SYMBOL(ckrm_core_drop);
