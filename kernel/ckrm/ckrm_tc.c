/* ckrm_tc.c - Class-based Kernel Resource Management (CKRM)
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003,2004
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
 * 12 Apr 2004
 *        introduced adopted to emerging classtype interface
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
#include <linux/ckrm_rc.h>

#include <linux/ckrm_tc.h>



#define TC_DEBUG(fmt, args...) do { /* printk("%s: " fmt, __FUNCTION__ , ## args); */ } while (0)


static struct ckrm_task_class  taskclass_dflt_class = {
};

const char *dflt_taskclass_name = TASK_CLASS_TYPE_NAME;

static struct ckrm_core_class *ckrm_alloc_task_class(struct ckrm_core_class *parent, const char *name);
static int ckrm_free_task_class(struct ckrm_core_class *core);

static int  tc_forced_reclassify(ckrm_core_class_t *target, const char *resname);
static int  tc_show_members(struct ckrm_core_class *core, struct seq_file *seq);
static void tc_add_resctrl(struct ckrm_core_class *core, int resid);

struct ckrm_classtype CT_taskclass = {
	.mfidx          = TC_MF_IDX,
	.name           = TASK_CLASS_TYPE_NAME,
	.typeID         = CKRM_CLASSTYPE_TASK_CLASS, 
	.maxdepth       = 3,                           // Hubertus .. just to start 
	.resid_reserved = 4,                           // Hubertus .. reservation
	.max_res_ctlrs  = CKRM_MAX_RES_CTLRS,        
	.max_resid      = 0,
	.bit_res_ctlrs  = 0L,
	.res_ctlrs_lock = SPIN_LOCK_UNLOCKED,
	.classes        = LIST_HEAD_INIT(CT_taskclass.classes),

	.default_class  = &taskclass_dflt_class.core,
	
	// private version of functions 
	.alloc          = &ckrm_alloc_task_class,
	.free           = &ckrm_free_task_class,
	.show_members   = &tc_show_members,
	.forced_reclassify = &tc_forced_reclassify,

	// use of default functions 
	.show_shares    = &ckrm_class_show_shares,
	.show_stats     = &ckrm_class_show_stats,
	.show_config    = &ckrm_class_show_config,
	.set_config     = &ckrm_class_set_config,
	.set_shares     = &ckrm_class_set_shares,
	.reset_stats    = &ckrm_class_reset_stats,

	// mandatory private version .. no dflt available
	.add_resctrl    = &tc_add_resctrl,	
};

/**************************************************************************
 *                   Helper Functions                                     *
 **************************************************************************/

static inline void
ckrm_init_task_lock(struct task_struct *tsk)
{
	tsk->ckrm_tsklock = SPIN_LOCK_UNLOCKED;
}

// Hubertus .. following functions should move to ckrm_rc.h

static inline void
ckrm_task_lock(struct task_struct *tsk)
{
  	spin_lock(&tsk->ckrm_tsklock);
}

static inline void
ckrm_task_unlock(struct task_struct *tsk)
{
  	spin_unlock(&tsk->ckrm_tsklock);
}

/*
 * Change the task class of the given task.
 *
 * Change the task's task class  to "newcls" if the task's current 
 * class (task->taskclass) is same as given "oldcls", if it is non-NULL.
 *
 * Caller is responsible to make sure the task structure stays put through
 * this function.
 *
 * This function should be called with the following locks NOT held
 * 	- tsk->ckrm_task_lock
 * 	- core->ckrm_lock, if core is NULL then ckrm_dflt_class.ckrm_lock
 * 	- tsk->taskclass->ckrm_lock 
 * 
 * Function is also called with a ckrm_core_grab on the new core, hence
 * it needs to be dropped if no assignment takes place.
 */

static void
ckrm_set_taskclass(struct task_struct *tsk, ckrm_task_class_t *newcls, 
		   ckrm_task_class_t *oldcls, enum ckrm_event event)
{
	int i;
	ckrm_classtype_t  *clstype;
	ckrm_res_ctlr_t   *rcbs;
	ckrm_task_class_t *curcls;
	void *old_res_class, *new_res_class;
	int drop_old_cls;

	ckrm_task_lock(tsk);
	curcls = tsk->taskclass;

	// check whether compare_and_exchange should
	if (oldcls && (oldcls != curcls)) {
		ckrm_task_unlock(tsk);
		if (newcls) {
			/* compensate for previous grab */
			TC_DEBUG("(%s:%d): Race-condition caught <%s> %d\n",
				 tsk->comm,tsk->pid,class_core(newcls)->name,event);
			ckrm_core_drop(class_core(newcls));
		}
		return;
	}

	// make sure we have a real destination core
	if (!newcls) {
		newcls = &taskclass_dflt_class;
		ckrm_core_grab(class_core(newcls));
	}

	// take out of old class 
	// remember that we need to drop the oldcore
	if ((drop_old_cls = (curcls != NULL))) {
		class_lock(class_core(curcls));
		if (newcls == curcls) {
			// we are already in the destination class.
			// we still need to drop oldcore
			class_unlock(class_core(curcls));
			ckrm_task_unlock(tsk);
			goto out;
		}
		list_del(&tsk->taskclass_link);
		INIT_LIST_HEAD(&tsk->taskclass_link);
		tsk->taskclass = NULL;
		class_unlock(class_core(curcls));
	}	

	// put into new class 
	class_lock(class_core(newcls));
	tsk->taskclass = newcls;
	list_add(&tsk->taskclass_link, &class_core(newcls)->objlist);
	class_unlock(class_core(newcls));

	if (newcls == curcls) {
		ckrm_task_unlock(tsk);
		goto out;
	}

	CE_NOTIFY(&CT_taskclass,event,newcls,tsk);

	ckrm_task_unlock(tsk);

	clstype = class_isa(newcls);                      // Hubertus .. can hardcode ckrm_CT_taskclass
	if (clstype->bit_res_ctlrs) {   // avoid running through the entire list if non is registered
		for (i = 0; i < clstype->max_resid; i++) {
			if (clstype->res_ctlrs[i] == NULL) 
				continue;
			atomic_inc(&clstype->nr_resusers[i]);
			old_res_class = curcls ? class_core(curcls)->res_class[i] : NULL;
			new_res_class = newcls ? class_core(newcls)->res_class[i] : NULL;
			rcbs = clstype->res_ctlrs[i];
			if (rcbs && rcbs->change_resclass && (old_res_class != new_res_class)) 
				(*rcbs->change_resclass)(tsk, old_res_class, new_res_class);
			atomic_dec(&clstype->nr_resusers[i]);
		}
	}

 out:
	if (drop_old_cls) 
		ckrm_core_drop(class_core(curcls));
	return;
}

// HF SUGGEST: we could macro-tize this for other types DEF_FUNC_ADD_RESCTRL(funcname,link)
//          would DEF_FUNC_ADD_RESCTRL(tc_add_resctrl,taskclass_link)

static void
tc_add_resctrl(struct ckrm_core_class *core, int resid)
{
	struct task_struct *tsk;
	struct ckrm_res_ctlr *rcbs;

	if ((resid < 0) || (resid >= CKRM_MAX_RES_CTLRS) || ((rcbs = core->classtype->res_ctlrs[resid]) == NULL)) 
		return;

	class_lock(core);
	list_for_each_entry(tsk, &core->objlist, taskclass_link) {
		if (rcbs->change_resclass)
			(*rcbs->change_resclass)(tsk, (void *) -1, core->res_class[resid]);
	}
	class_unlock(core);
}


/**************************************************************************
 *                   Functions called from classification points          *
 **************************************************************************/

#define ECB_PRINTK(fmt, args...) // do { if (CT_taskclass.ce_regd) printk("%s: " fmt, __FUNCTION__ , ## args); } while (0)

#define CE_CLASSIFY_TASK(event, tsk)						\
do {										\
	struct ckrm_task_class *newcls = NULL, *oldcls = tsk->taskclass;	\
										\
	CE_CLASSIFY_RET(newcls,&CT_taskclass,event,tsk);			\
	if (newcls) {								\
		/* called synchrously. no need to get task struct */		\
		ckrm_set_taskclass(tsk, newcls, oldcls, event);			\
	}									\
} while (0)

#define CE_CLASSIFY_TASK_PROTECT(event, tsk)	\
do {						\
	ce_protect(&CT_taskclass);		\
	CE_CLASSIFY_TASK(event,tsk);		\
	ce_release(&CT_taskclass);              \
} while (0)




static void
cb_taskclass_newtask(struct task_struct *tsk)
{
	tsk->taskclass = NULL;
	INIT_LIST_HEAD(&tsk->taskclass_link);
}


static void
cb_taskclass_fork(struct task_struct *tsk)
{
	struct ckrm_task_class *cls = NULL;

	ECB_PRINTK("%p:%d:%s\n",tsk,tsk->pid,tsk->comm);

	ce_protect(&CT_taskclass);
	CE_CLASSIFY_RET(cls,&CT_taskclass,CKRM_EVENT_FORK,tsk);	     
	if (cls == NULL) {
		ckrm_task_lock(tsk->parent);
		cls = tsk->parent->taskclass;
		ckrm_core_grab(class_core(cls));
		ckrm_task_unlock(tsk->parent);
	}
	if (!list_empty(&tsk->taskclass_link))
		printk("BUG in cb_fork.. tsk (%s:%d> already linked\n",
			tsk->comm,tsk->pid);

	ckrm_set_taskclass(tsk, cls, NULL, CKRM_EVENT_FORK);
	ce_release(&CT_taskclass);
}

static void
cb_taskclass_exit(struct task_struct *tsk)
{
	ckrm_task_class_t *cls;

	// Remove the task from the current core class
	
	ECB_PRINTK("%p:%d:%s\n",tsk,tsk->pid,tsk->comm);
	ckrm_task_lock(tsk);

	CE_CLASSIFY_NORET( &CT_taskclass, CKRM_EVENT_EXIT, tsk);

	if ((cls = tsk->taskclass) != NULL) {
		class_lock(class_core(cls));
		tsk->taskclass = NULL;
		list_del(&tsk->taskclass_link);
		class_unlock(class_core(cls));
		ckrm_core_drop(class_core(cls));
	} else {
		INIT_LIST_HEAD(&tsk->taskclass_link);
	}
	ckrm_task_unlock(tsk);
}

static void
cb_taskclass_exec(const char *filename)
{
	ECB_PRINTK("%p:%d:%s <%s>\n",current,current->pid,current->comm,filename);
	CE_CLASSIFY_TASK_PROTECT(CKRM_EVENT_EXEC, current);
}

static void
cb_taskclass_uid(void)
{
	ECB_PRINTK("%p:%d:%s\n",current,current->pid,current->comm);
	CE_CLASSIFY_TASK_PROTECT(CKRM_EVENT_UID, current);
}

static void
cb_taskclass_gid(void)
{
	ECB_PRINTK("%p:%d:%s\n",current,current->pid,current->comm);
	CE_CLASSIFY_TASK_PROTECT(CKRM_EVENT_GID, current);
}

static struct ckrm_event_spec taskclass_events_callbacks[] = {
	CKRM_EVENT_SPEC( NEWTASK, cb_taskclass_newtask ),
	CKRM_EVENT_SPEC( EXEC   , cb_taskclass_exec ),
	CKRM_EVENT_SPEC( FORK   , cb_taskclass_fork ),
	CKRM_EVENT_SPEC( EXIT   , cb_taskclass_exit ),
	CKRM_EVENT_SPEC( UID    , cb_taskclass_uid  ),
	CKRM_EVENT_SPEC( GID    , cb_taskclass_gid  ),
	{ -1 }
};

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

	ce_protect(&CT_taskclass);

 retry:		
	if (use_bitmap == 0) {
		// go through it in one walk
		read_lock(&tasklist_lock);
		for ( i=0 ; i<curpidmax ; i++ ) {
			if ((thread = find_task_by_pid(i)) == NULL) 
				continue;
			get_task_struct(thread);
			read_unlock(&tasklist_lock);
			CE_CLASSIFY_TASK(CKRM_EVENT_RECLASSIFY, thread);
			put_task_struct(thread);
			read_lock(&tasklist_lock);
		}
		read_unlock(&tasklist_lock);
	} else {
		unsigned long *bitmap;
		int bitmapsize;
		int order = 0;
		int num_loops;
		int pid, do_next;


		bitmap = (unsigned long*) __get_free_pages(GFP_KERNEL,order);
		if (bitmap == NULL) {
			use_bitmap = 0;
			goto retry;
		}

		bitmapsize = 8 * (1 << (order + PAGE_SHIFT));
		num_loops  = (curpidmax + bitmapsize - 1) / bitmapsize;

		do_next = 1;
		for ( i=0 ; i < num_loops && do_next; i++) {
			int pid_start = i*bitmapsize; 
			int pid_end   = pid_start + bitmapsize;
			int num_found = 0;
			int pos;

			memset(bitmap, 0, bitmapsize/8); // start afresh
			do_next = 0;

			read_lock(&tasklist_lock);
			do_each_thread(proc, thread) {
				pid = thread->pid;
				if ((pid < pid_start) || (pid >= pid_end)) {
					if (pid >= pid_end) {
						do_next = 1;
					}
					continue;
				}
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
					CE_CLASSIFY_TASK(CKRM_EVENT_RECLASSIFY, thread);
					put_task_struct(thread);
				} else {
					read_unlock(&tasklist_lock);
				}
			}
		}

	}
	ce_release(&CT_taskclass);
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
			CE_CLASSIFY_TASK_PROTECT(CKRM_EVENT_RECLASSIFY, tsk);
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
ckrm_reclassify_class_tasks(struct ckrm_task_class *cls)
{
	int ce_regd;
	struct ckrm_hnode *cnode;
	struct ckrm_task_class *parcls;
	int num = 0;

	if (!ckrm_validate_and_grab_core(&cls->core))
		return;

	down(&async_serializer);   // protect again race condition
	TC_DEBUG("start %p:%s:%d:%d\n",cls,cls->core.name,
		atomic_read(&cls->core.refcnt),atomic_read(&cls->core.hnode.parent->refcnt));
	// If no CE registered for this classtype, following will be needed repeatedly;
	ce_regd =  class_core(cls)->classtype->ce_regd;
	cnode = &(class_core(cls)->hnode);
	parcls = class_type(ckrm_task_class_t, cnode->parent);

next_task:
	class_lock(class_core(cls));
	if (!list_empty(&class_core(cls)->objlist)) {
		struct ckrm_task_class *newcls = NULL;
		struct task_struct *tsk = 
			list_entry(class_core(cls)->objlist.next,
				   struct task_struct, taskclass_link);
		
		get_task_struct(tsk);
		class_unlock(class_core(cls));

		if (ce_regd) {
			CE_CLASSIFY_RET(newcls,&CT_taskclass,CKRM_EVENT_RECLASSIFY,tsk);
			if (cls == newcls) {
				// don't allow reclassifying to the same class
				// as we are in the process of cleaning up this class
				ckrm_core_drop(class_core(newcls)); // to compensate CE's grab
				newcls = NULL;
			}
		}
		if (newcls == NULL) {
			newcls = parcls;
			ckrm_core_grab(class_core(newcls));
		}
		ckrm_set_taskclass(tsk, newcls, cls, CKRM_EVENT_RECLASSIFY);
		put_task_struct(tsk);
		num++;
		goto next_task;
	}
	TC_DEBUG("stop  %p:%s:%d:%d   %d\n",cls,cls->core.name,
		atomic_read(&cls->core.refcnt),atomic_read(&cls->core.hnode.parent->refcnt),num);
	class_unlock(class_core(cls));
	ckrm_core_drop(class_core(cls));

	up(&async_serializer);

	return ;
}

/*
 * Change the core class of the given task.
 */

int 
ckrm_forced_reclassify_pid(pid_t pid, struct ckrm_task_class *cls)
{
	struct task_struct *tsk;

	if (!ckrm_validate_and_grab_core(class_core(cls)))
		return - EINVAL;

	read_lock(&tasklist_lock);
	if ((tsk = find_task_by_pid(pid)) == NULL) {
		read_unlock(&tasklist_lock);
		ckrm_core_drop(class_core(cls));
		return -EINVAL;
	}
	get_task_struct(tsk);
	read_unlock(&tasklist_lock);
	
	down(&async_serializer);   // protect again race condition
	
	ce_protect(&CT_taskclass);
	ckrm_set_taskclass(tsk, cls, NULL, CKRM_EVENT_MANUAL);
	ce_release(&CT_taskclass);
	put_task_struct(tsk);
	
	up(&async_serializer);
	return 0;
}

static struct ckrm_core_class *
ckrm_alloc_task_class(struct ckrm_core_class *parent, const char *name)
{
	struct ckrm_task_class *taskcls;
	taskcls = kmalloc(sizeof(struct ckrm_task_class), GFP_KERNEL);
	if (taskcls == NULL) 
		return NULL;

	ckrm_init_core_class(&CT_taskclass,
			     class_core(taskcls),parent,name);

	ce_protect(&CT_taskclass);
	if (CT_taskclass.ce_cb_active && CT_taskclass.ce_callbacks.class_add)
		(*CT_taskclass.ce_callbacks.class_add)(name,taskcls);
	ce_release(&CT_taskclass);

	return class_core(taskcls);
}

static int
ckrm_free_task_class(struct ckrm_core_class *core)
{
	struct ckrm_task_class *taskcls;

	if (!ckrm_is_core_valid(core)) {
		// Invalid core
		return (-EINVAL);
	}
	if (core == core->classtype->default_class) {
		// reset the name tag
		core->name = dflt_taskclass_name;
 		return 0;
	}
	
	TC_DEBUG("%p:%s:%d\n",core,core->name,atomic_read(&core->refcnt));
	 
	taskcls = class_type(struct ckrm_task_class, core);

	ce_protect(&CT_taskclass);

	if (CT_taskclass.ce_cb_active && CT_taskclass.ce_callbacks.class_delete)
		(*CT_taskclass.ce_callbacks.class_delete)(core->name,taskcls);
	ckrm_reclassify_class_tasks( taskcls );

	ce_release(&CT_taskclass);

	ckrm_release_core_class(core);  // Hubertus .... could just drop the class .. error message
	return 0;
}


void __init
ckrm_meta_init_taskclass(void)
{
	printk("...... Initializing ClassType<%s> ........\n",CT_taskclass.name);
	// intialize the default class
	ckrm_init_core_class(&CT_taskclass, class_core(&taskclass_dflt_class),
			     NULL,dflt_taskclass_name);

	// register classtype and initialize default task class
	ckrm_register_classtype(&CT_taskclass);
	ckrm_register_event_set(taskclass_events_callbacks);

	// note registeration of all resource controllers will be done later dynamically 
	// as these are specified as modules
}



static int                      
tc_show_members(struct ckrm_core_class *core, struct seq_file *seq) 
{
	struct list_head *lh;
	struct task_struct *tsk;

	class_lock(core);
	list_for_each(lh, &core->objlist) {	
		tsk = container_of(lh, struct task_struct, taskclass_link);
		seq_printf(seq,"%ld\n", (long)tsk->pid);
	}
	class_unlock(core);

	return 0;
}

static int
tc_forced_reclassify(struct ckrm_core_class *target,const char *obj)
{	
	pid_t pid;
	int rc = -EINVAL;

	pid = (pid_t) simple_strtoul(obj,NULL,10);
	if (pid > 0) {
		rc = ckrm_forced_reclassify_pid(pid,
				class_type(ckrm_task_class_t,target));
	}
	return rc;
} 
	
#if 1

/***************************************************************************************
 * Debugging Task Classes:  Utility functions
 **************************************************************************************/

void
check_tasklist_sanity(struct ckrm_task_class *cls)
{
	struct ckrm_core_class *core = class_core(cls);
	struct list_head *lh1, *lh2;
	int count = 0;

	if (core) {
		class_lock(core);
		if (list_empty(&core->objlist)) {
			class_lock(core);
			printk("check_tasklist_sanity: class %s empty list\n",
					core->name);
			return;
		}
		list_for_each_safe(lh1, lh2, &core->objlist) {
			struct task_struct *tsk = container_of(lh1, struct task_struct, taskclass_link);
			if (count++ > 20000) {
				printk("list is CORRUPTED\n");
				break;
			}
			if (tsk->taskclass != cls) {
				const char *tclsname;
				tclsname = (tsk->taskclass) ? class_core(tsk->taskclass)->name 
					                    : "NULL";
				printk("sanity: task %s:%d has ckrm_core |%s| but in list |%s|\n",
				       tsk->comm,tsk->pid,tclsname,core->name);
			}
		}
		class_unlock(core);
	}
}

void 
ckrm_debug_free_task_class(struct ckrm_task_class *tskcls)
{
	struct task_struct *proc, *thread;
	int count = 0;

	printk("Analyze Error <%s> %d\n",
	       class_core(tskcls)->name,atomic_read(&(class_core(tskcls)->refcnt)));

	read_lock(&tasklist_lock);
	class_lock(class_core(tskcls));
	do_each_thread(proc, thread) {
		count += (tskcls == thread->taskclass);
		if ((thread->taskclass == tskcls) || (tskcls == NULL)) {
			const char *tclsname;
			tclsname = (thread->taskclass) ? class_core(thread->taskclass)->name : "NULL";
			printk("%d thread=<%s:%d>  -> <%s> <%lx>\n",
			       count,thread->comm,thread->pid,tclsname, thread->flags & PF_EXITING);
		}
	} while_each_thread(proc, thread);
	class_unlock(class_core(tskcls));
	read_unlock(&tasklist_lock);

	printk("End Analyze Error <%s> %d\n",
	       class_core(tskcls)->name,atomic_read(&(class_core(tskcls)->refcnt)));
} 

#endif
