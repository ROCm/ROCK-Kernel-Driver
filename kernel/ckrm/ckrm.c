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

/*
 * callback structures for each registered resource controller.
 */
ckrm_res_callback_t ckrm_res_ctlrs[CKRM_MAX_RES_CTLRS];
static spinlock_t ckrm_res_ctlrs_lock = SPIN_LOCK_UNLOCKED; // protects the array
long bit_res_ctlrs = 0; // bit set for registered resource controllers
static atomic_t nr_resusers[CKRM_MAX_RES_CTLRS]; // no of users for each res ctlr
static int max_resid = 0; // highest resource id currently used.

EXPORT_SYMBOL(ckrm_res_ctlrs);

/*
 * Callback registered by the classification engine.
 * The chances of two engines trying to register/unregister at the same
 * moment is very less. So, it is ok to not have any locking mechanism
 * protecting ckrm_eng_callbacks data structure.
 */
static ckrm_eng_callback_t ckrm_eng_callbacks;
static atomic_t nr_engusers; // Number of users using the data structure above.
static int callbacks_active = 0;

/* 
 * Default Class and global variables
 */

static int ckrm_num_classes = 1;
static LIST_HEAD(ckrm_classes);

struct ckrm_core_class ckrm_dflt_class = { .class_type = CKRM_TASK_CLASS };
struct ckrm_core_class ckrm_net_root   = { .class_type = CKRM_NET_CLASS };

EXPORT_SYMBOL(ckrm_dflt_class);
EXPORT_SYMBOL(ckrm_net_root);

rwlock_t               ckrm_class_lock = RW_LOCK_UNLOCKED;  // protect the class and rc hierarchy structure add/del


/**************************************************************************
 *                   Helper Functions                                     *
 **************************************************************************/

/*
 * Return TRUE if the given core class pointer is valid.
 */
inline unsigned int
is_core_valid(ckrm_core_class_t *core)
{
	if (core && (core->magic == CKRM_CORE_MAGIC))
		return 1;
	return 0;
}

/*
 * Return TRUE if the given resource is registered.
 */
inline unsigned int
is_res_regd(int resid)
{
	return (test_bit(resid, &bit_res_ctlrs));
}

int 
ckrm_resid_lookup (char *resname)
{
	int resid = -1;
	
	//for_each_resid(resid) {
	for (resid=0; resid < CKRM_MAX_RES_CTLRS; resid++) { 
		if (is_res_regd(resid)) {
			if (!strncmp(resname, ckrm_res_ctlrs[resid].res_name,CKRM_MAX_RES_NAME))
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
 *                   Internal Functions                                   *
 **************************************************************************/

#define hnode_2_core(ptr)  ((ptr) ? container_of(ptr,struct ckrm_core_class,hnode) : NULL)

static inline
void set_callbacks_active(void)
{
	callbacks_active = ((atomic_read(&nr_engusers) > 0) &&
			    (ckrm_eng_callbacks.always_callback || (ckrm_num_classes > 1)));
}

/*
 * Change the core class of the given task.
 */
void
ckrm_reclassify_task(struct task_struct *tsk, ckrm_core_class_t *core)
{
	int i;
	ckrm_res_callback_t *rcbs;
	ckrm_core_class_t *old_core;
	void *old_res_class, *new_res_class;

	// Remove the task from the current core class
	if ((old_core = tsk->ckrm_core) != NULL) {
		spin_lock(&old_core->ckrm_lock);
		list_del(&tsk->ckrm_link);
		tsk->ckrm_core = NULL;
		spin_unlock(&old_core->ckrm_lock);
	}	

	if (core != NULL) {
		spin_lock(&core->ckrm_lock);
		tsk->ckrm_core = core;
		list_add(&tsk->ckrm_link, &core->tasklist);
		spin_unlock(&core->ckrm_lock);
	}

	if (core != old_core) {
		for (i = 0; i < max_resid; i++) {
			rcbs = &ckrm_res_ctlrs[i];
			old_res_class = old_core ? old_core->res_class[i] : NULL;
			new_res_class = core ? core->res_class[i] : NULL;
			if (rcbs->change_resclass && (old_res_class != new_res_class)) 
				(*rcbs->change_resclass)(tsk,old_res_class, new_res_class);
		}
	}
	return;
}

/*
 * Reclassify all tasks in the given core class.
 */
int
ckrm_reclassify_class_tasks(struct ckrm_core_class *core)
{
	int ret = 0;
  
	// Reclassify all tasks in the given core class.
	if (!list_empty(&core->tasklist)) {
		struct list_head *lh1, *lh2;
		ckrm_core_class_t *temp;
		struct task_struct *tsk;

		read_lock(&ckrm_class_lock);
		spin_lock(&core->ckrm_lock);

		list_for_each_safe(lh1, lh2, &core->tasklist) {
			list_del(lh1);
			tsk = container_of(lh1, struct task_struct, ckrm_link);

			if (ckrm_num_classes > 0) {
				temp = (*ckrm_eng_callbacks.reclassify)(tsk);
			} else {
				temp = &ckrm_dflt_class;
			}
			if (unlikely(temp == core)) {
				// Classification engine still using this core class,
				temp = NULL;
				ret = -EBUSY;
			}
			tsk->ckrm_core = NULL;
			ckrm_reclassify_task(tsk, temp);
		}
		
		spin_unlock(&core->ckrm_lock);
		read_unlock(&ckrm_class_lock);
	}
	return ret;
}

/*
 * Go through the task list and reclassify all tasks according to the current
 * classification rules.
 *
 */
static void
ckrm_reclassify_all_tasks(void)
{
	ckrm_core_class_t *temp;
	struct task_struct *proc, *thread;

	read_lock(&ckrm_class_lock);
	read_lock(&tasklist_lock);
	do_each_thread(proc, thread) {
		if (ckrm_num_classes > 0) {
			temp = (*ckrm_eng_callbacks.reclassify)(thread);
		} else {
			temp = &ckrm_dflt_class;
		}
		if (temp != thread->ckrm_core) {
			ckrm_reclassify_task(thread, temp);
		}
	} while_each_thread(proc, thread);
	read_unlock(&tasklist_lock);
	read_unlock(&ckrm_class_lock);
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

	if (atomic_read(&nr_engusers) > 0) {
		// Some engine is acive, deregister it first.
		return (-EBUSY);
	}
	
	if (strnlen(ecbs->ckrm_eng_name, CKRM_MAX_ENG_NAME) == 0 ||
				ecbs->reclassify == NULL) {
		return (-EINVAL);
	}

	ckrm_eng_callbacks = *ecbs;
	atomic_set(&nr_engusers, 1); // existence counted as 1
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
	if (atomic_dec_and_test(&nr_engusers) != 1) {
		// Somebody is currently using the engine, cannot deregister.
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

/* functions to manipulate class (core or resource) hierarchies 
 */


/* Caller must ensure ckrm_class_lock held */
void ckrm_hnode_add(struct ckrm_hnode *node,
	  	    struct ckrm_hnode *parent )
{
	node->parent = parent;
	INIT_LIST_HEAD(&node->children);
	INIT_LIST_HEAD(&node->siblings);

//	if (parent)
//		printk(KERN_ERR "hnode_add: %p\n",&parent->children);

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
int ckrm_hnode_remove(struct ckrm_hnode *node)
{
	/* ensure that the node does not have children */
	if (!list_empty(&node->children))
		return 0;
	list_del(&node->siblings);
	node->parent = NULL;
	return 1;
}


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
		ckrm_res_callback_t *rcbs = &ckrm_res_ctlrs[resid];
		
		if (rcbs->res_alloc) {
			core->res_class[resid] =(*rcbs->res_alloc)(core,parent);
			if (core->res_class[resid]) 
				atomic_inc(&nr_resusers[resid]);
			else
				printk(KERN_ERR "Error creating res class\n");
		}
	}
}

/*
 * Allocate a core class, which in turn allocates resource classes as
 * specified by the res_mask parameter.
 *
 * Return the handle to the core class on success, NULL on failure.
 */

ckrm_core_class_t *dcore;

struct ckrm_core_class *
ckrm_alloc_core_class(struct ckrm_core_class *parent, struct dentry *dentry)
{
	int i;

	if (!is_core_valid(parent))
		return NULL; 
	
	dcore = kmalloc(sizeof(ckrm_core_class_t), GFP_KERNEL);
	if (dcore == NULL) 
		return NULL;

	dcore->magic = CKRM_CORE_MAGIC;
	dcore->dentry = dentry;
	INIT_LIST_HEAD(&dcore->tasklist);
	dcore->ckrm_lock = SPIN_LOCK_UNLOCKED;

	write_lock(&ckrm_class_lock);

	list_add(&dcore->clslist,&ckrm_classes);
	ckrm_hnode_add(&dcore->hnode, parent ? &parent->hnode : NULL); 
	ckrm_num_classes++;
	set_callbacks_active();

	dcore->class_type = parent->class_type;
	for (i = 0; i < CKRM_MAX_RES_CTLRS; i++) {
		if (dcore->class_type == ckrm_res_ctlrs[i].res_type)
			ckrm_alloc_res_class(dcore,parent,i);
		else
			dcore->res_class[i] = NULL;
	}

	write_unlock(&ckrm_class_lock);

	/* Inform CE at last, once core is ready for use */
	if (callbacks_active && *ckrm_eng_callbacks.class_add) {
		(*ckrm_eng_callbacks.class_add)(dcore->dentry->d_name.name, (void *)dcore);
	}


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
		ckrm_res_callback_t *rcbs = &ckrm_res_ctlrs[resid];

		if (rcbs->res_free) {
			(*rcbs->res_free)(core->res_class[resid]);
			atomic_dec(&nr_resusers[resid]);
			core->res_class[resid] = NULL;	
		}	
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

	if (!is_core_valid(core)) {
		// Invalid core
		return (-EINVAL);
	}

	/* Inform CE first, in case it needs any core class data */
	if (callbacks_active && *ckrm_eng_callbacks.class_delete) {
		(*ckrm_eng_callbacks.class_delete)(core->dentry->d_name.name, 
								(void *)core);
	}
	// Clear the magic, so we would know if this core is reused.
	core->magic = 0;

	// Remove this core class from its linked list.
	write_lock(&ckrm_class_lock);

	ckrm_hnode_remove(&core->hnode);   /* Hubertus ; locking */
	list_del(&core->clslist);
	ckrm_num_classes--;

	set_callbacks_active();

	for (i = 0; i < max_resid; i++) {
//		if (core->res_class[i]) {
//		(*ckrm_res_ctlrs[i].res_free)(core->res_class[i]);
//			atomic_dec(&nr_resusers[i]);
//		}
		ckrm_free_res_class(core,i);
	}
	write_unlock(&ckrm_class_lock);

	kfree(core);
	return 0;
}

EXPORT_SYMBOL(ckrm_hnode_add);
EXPORT_SYMBOL(ckrm_hnode_remove);

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
	int resid;
	
	if (!rcbs)
		return -EINVAL;

	resid = rcbs->resid;
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

	spin_lock(&ckrm_res_ctlrs_lock);

	ckrm_res_ctlrs[resid] = *rcbs;
	atomic_set(&nr_resusers[resid], 0);
	set_bit(resid, &bit_res_ctlrs);	
	spin_unlock(&ckrm_res_ctlrs_lock);
	return resid;

#if 0
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
#endif
}

int
ckrm_register_res_ctlr(ckrm_res_callback_t *rcbs)
{
	struct ckrm_core_class *core;
	int resid;

	resid = ckrm_register_res_ctlr_intern(rcbs);
	
	if (resid >= 0) {
		/* run through all classes and create the resource class object */

		list_for_each_entry(core, &ckrm_classes, clslist) {
			printk(KERN_ERR "CKRM .. create res clsobj for <%s>\n",rcbs->res_name);
			ckrm_alloc_res_class(core,hnode_2_core(core->hnode.parent),resid);
		}
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
	struct ckrm_core_class *core;

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

	/* run through all classes and delete the resource class object */

	list_for_each_entry(core, &ckrm_classes, clslist) {
		printk(KERN_ERR "CKRM .. delete res clsobj for <%s>\n",rcbs->res_name);
		ckrm_free_res_class(core,resid);
	}

	return 0;
}

/**************************************************************************
 *                   Functions called from classification points          *
 **************************************************************************/

inline void
ckrm_new_task(struct task_struct *tsk)
{
	/* nothing has to be done at this point 
	 * but we keep it as a place holder for now
	 */
}


#define CKRM_CB1(function, tsk) \
{ \
	struct ckrm_core_class *core = NULL; \
\
	if (callbacks_active && ckrm_eng_callbacks.function) { \
		        read_lock(&ckrm_class_lock); \
                        core = (*ckrm_eng_callbacks.function)(tsk); \
	                if (core && (core != tsk->ckrm_core)) { \
		               ckrm_reclassify_task(tsk, core); \
	                } \
	                read_unlock(&ckrm_class_lock); \
	} \
	return; \
}

#define CKRM_CB1_ARGS(function, tsk, args) \
{ \
	struct ckrm_core_class *core = NULL; \
\
	if (callbacks_active && ckrm_eng_callbacks.function) { \
                read_lock(&ckrm_class_lock); \
                core = (*ckrm_eng_callbacks.function)(tsk, args); \
	        if (core && (core != tsk->ckrm_core)) { \
		        ckrm_reclassify_task(tsk, core); \
	        } \
		read_unlock(&ckrm_class_lock); \
	} \
	return; \
}

#define CKRM_CB2(function) \
{ \
	if (callbacks_active && ckrm_eng_callbacks.function) { \
                read_lock(&ckrm_class_lock); \
		(*ckrm_eng_callbacks.function)();\
		read_unlock(&ckrm_class_lock); \
	} \
	return;\
}

#define CKRM_CB3(function, arg) \
{\
	if (callbacks_active && ckrm_eng_callbacks.function) { \
                read_lock(&ckrm_class_lock); \
		(*ckrm_eng_callbacks.function)(arg); \
		read_unlock(&ckrm_class_lock); \
	} \
	return;\
}

void
ckrm_cb_exec(const char *filename)
{
	CKRM_CB1_ARGS(exec, current, filename);
}

void
ckrm_cb_fork(struct task_struct *tsk)
{
	CKRM_CB1(fork, tsk);
}

void
ckrm_reclassify(int pid)
{
	struct task_struct *tsk;

	if (pid < 0) {
		// do we want to treat this as process group ?
		return;
	}
	if (pid) {
		if ((tsk = find_task_by_pid(pid)) != NULL) {
			CKRM_CB1(reclassify, tsk);
		}
	} else { // reclassify all tasks in the system
		ckrm_reclassify_all_tasks();
	}
	return;
}

void
ckrm_cb_exit(struct task_struct *tsk)
{
	ckrm_core_class_t *core;

	// Remove the task from the current core class
	if ((core = tsk->ckrm_core) != NULL) {
		spin_lock(&core->ckrm_lock);
		list_del(&tsk->ckrm_link);
		tsk->ckrm_core = NULL;
		spin_unlock(&core->ckrm_lock);
	}	

	CKRM_CB3(exit, tsk);
}

void
ckrm_cb_uid(void)
{
	CKRM_CB1(uid, current);
}

void
ckrm_cb_gid(void)
{
	CKRM_CB1(gid, current);
}

void
ckrm_cb_manual(struct task_struct *tsk)
{
	if (callbacks_active) { 
		if (ckrm_eng_callbacks.manual) { 
			read_lock(&ckrm_class_lock); 
			(*ckrm_eng_callbacks.manual)(tsk); 
			read_unlock(&ckrm_class_lock); 
		} 
	} 
	return; 
}


void __init ckrm_init(void) 
{
	struct ckrm_core_class *core = &ckrm_dflt_class;
	struct task_struct *tsk;
	int i;


	if (!core)
		return;


	/* Initialize default core class */
	core->magic = CKRM_CORE_MAGIC;
	core->dentry = NULL;
	INIT_LIST_HEAD(&core->tasklist);
	core->ckrm_lock = SPIN_LOCK_UNLOCKED;
	for (i = 0; i < CKRM_MAX_RES_CTLRS; i++)
		core->res_class[i] = NULL;
	
	/* Add the default class to the global classes list 
	 * ckrm_num_classes initialized to 1, don't increment
	 */
	
	write_lock(&ckrm_class_lock);
	list_add(&core->clslist,&ckrm_classes);
	ckrm_hnode_add(&core->hnode,NULL);
	write_unlock(&ckrm_class_lock);
	
	spin_lock(&core->ckrm_lock);
	

	read_lock(&tasklist_lock);
	for_each_process(tsk) {
		task_lock(tsk);
		tsk->ckrm_core = core;
		INIT_LIST_HEAD(&tsk->ckrm_link);
		list_add(&tsk->ckrm_link, &core->tasklist);
			task_unlock(tsk);
			//printk("ckrm_init: Added %ld to %p\n",(long)tsk->pid,core);
	}
	read_unlock(&tasklist_lock);
	
	spin_unlock(&core->ckrm_lock);	
	printk("CKRM Initialized\n");
}


EXPORT_SYMBOL(ckrm_register_engine);
EXPORT_SYMBOL(ckrm_unregister_engine);

EXPORT_SYMBOL(ckrm_reclassify_task);

EXPORT_SYMBOL(ckrm_register_res_ctlr);
EXPORT_SYMBOL(ckrm_unregister_res_ctlr);

EXPORT_SYMBOL(ckrm_alloc_core_class);
EXPORT_SYMBOL(ckrm_free_core_class);

EXPORT_SYMBOL(ckrm_reclassify);
