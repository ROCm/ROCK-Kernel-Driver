/* ckrm_numtasks.c - "Number of tasks" resource controller for CKRM
 *
 * Copyright (C) Chandra Seetharaman,  IBM Corp. 2003
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
 * 31 Mar 2004: Created
 * 
 */

/*
 * Code Description: TBD
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/errno.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/ckrm.h>
#include <linux/ckrm_rc.h>
#include <linux/ckrm_tc.h>

#define TOTAL_NUM_TASKS (131072) // 128 K
#define NUMTASKS_DEBUG
#define NUMTASKS_NAME "numtasks"

typedef struct ckrm_numtasks {
	struct ckrm_core_class *core; // the core i am part of...
	struct ckrm_core_class *parent; // parent of the core above.
	struct ckrm_shares shares;
	spinlock_t cnt_lock; // always grab parent's lock first and then child's
	int cnt_guarantee; // num_tasks guarantee in local units
	int cnt_unused; // has to borrow if more than this is needed
	int cnt_limit; // no tasks over this limit.
	atomic_t cnt_cur_alloc; // current alloc from self
	atomic_t cnt_borrowed; // borrowed from the parent

	int over_guarantee; //turn on/off when cur_alloc goes over/under guarantee

	// internally maintained statictics to compare with max numbers
	int limit_failures; // no. of failures 'cause the request was over the limit
	int borrow_sucesses; // no. of successful borrows
	int borrow_failures; // no. of borrow faileures

	// Maximum the specific statictics has reached.
	int max_limit_failures;
	int max_borrow_sucesses;
	int max_borrow_failures;

	// Total number of specific statistics
	int tot_limit_failures;
	int tot_borrow_sucesses;
	int tot_borrow_failures;
} ckrm_numtasks_t;

struct ckrm_res_ctlr numtasks_rcbs;

/* Initialize rescls values
 * May be called on each rcfs unmount or as part of error recovery
 * to make share values sane.
 * Does not traverse hierarchy reinitializing children.
 */
static void
numtasks_res_initcls_one(ckrm_numtasks_t *res)
{
	res->shares.my_guarantee     = CKRM_SHARE_DONTCARE;
	res->shares.my_limit         = CKRM_SHARE_DONTCARE;
	res->shares.total_guarantee  = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.max_limit        = CKRM_SHARE_DFLT_MAX_LIMIT;
	res->shares.unused_guarantee = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.cur_max_limit    = 0;

	res->cnt_guarantee           = CKRM_SHARE_DONTCARE;
	res->cnt_unused              = CKRM_SHARE_DONTCARE;
	res->cnt_limit               = CKRM_SHARE_DONTCARE;

	res->over_guarantee          = 0;

	res->limit_failures          = 0;
	res->borrow_sucesses         = 0;
	res->borrow_failures         = 0;

	res->max_limit_failures      = 0;
	res->max_borrow_sucesses     = 0;
	res->max_borrow_failures     = 0;

	res->tot_limit_failures      = 0;
	res->tot_borrow_sucesses     = 0;
	res->tot_borrow_failures     = 0;

	atomic_set(&res->cnt_cur_alloc, 0);
	atomic_set(&res->cnt_borrowed, 0);
	return;
}

#if 0	
static void
numtasks_res_initcls(void *my_res)
{
	ckrm_numtasks_t *res = my_res;

	/* Write a version which propagates values all the way down 
	   and replace rcbs callback with that version */
	
}
#endif

int
numtasks_get_ref(void *arg, int force)
{
	int rc, resid = numtasks_rcbs.resid;
	ckrm_numtasks_t *res;
	ckrm_core_class_t *core = arg;

	if ((resid < 0) || (core == NULL))
		return 1;

	res = ckrm_get_res_class(core, resid, ckrm_numtasks_t);
	if (res == NULL) 
		return 1;

	atomic_inc(&res->cnt_cur_alloc);

	rc = 1;
	if (((res->parent) && (res->cnt_unused == CKRM_SHARE_DONTCARE)) ||
			(atomic_read(&res->cnt_cur_alloc) > res->cnt_unused)) {

		rc = 0;
		if (!force && (res->cnt_limit != CKRM_SHARE_DONTCARE) && 
				(atomic_read(&res->cnt_cur_alloc) > res->cnt_limit)) {
			res->limit_failures++;
			res->tot_limit_failures++;
		} else if (res->parent != NULL) {
			if ((rc = numtasks_get_ref(res->parent, force)) == 1) {
				atomic_inc(&res->cnt_borrowed);
				res->borrow_sucesses++;
				res->tot_borrow_sucesses++;
				res->over_guarantee = 1;
			} else {
				res->borrow_failures++;
				res->tot_borrow_failures++;
			}
		} else {
			rc = force;
		}
	} else if (res->over_guarantee) {
		res->over_guarantee = 0;

		if (res->max_limit_failures < res->limit_failures) {
			res->max_limit_failures = res->limit_failures;
		}
		if (res->max_borrow_sucesses < res->borrow_sucesses) {
			res->max_borrow_sucesses = res->borrow_sucesses;
		}
		if (res->max_borrow_failures < res->borrow_failures) {
			res->max_borrow_failures = res->borrow_failures;
		}
		res->limit_failures = 0;
		res->borrow_sucesses = 0;
		res->borrow_failures = 0;
	}

	if (!rc) {
		atomic_dec(&res->cnt_cur_alloc);
	}
	return rc;
}

void
numtasks_put_ref(void *arg)
{
	int resid = numtasks_rcbs.resid;
	ckrm_numtasks_t *res;
	ckrm_core_class_t *core = arg;

	if ((resid == -1) || (core == NULL)) {
		return;
	}

	res = ckrm_get_res_class(core, resid, ckrm_numtasks_t);
	if (res == NULL) 
		return;
	atomic_dec(&res->cnt_cur_alloc);
	if (atomic_read(&res->cnt_borrowed) > 0) {
		atomic_dec(&res->cnt_borrowed);
		numtasks_put_ref(res->parent);
	}
	return;
}

static void *
numtasks_res_alloc(struct ckrm_core_class *core, struct ckrm_core_class *parent)
{
	ckrm_numtasks_t *res;
	
	res = kmalloc(sizeof(ckrm_numtasks_t), GFP_ATOMIC);
	
	if (res) {
		res->core = core;
		res->parent = parent;
		numtasks_res_initcls_one(res);
		res->cnt_lock = SPIN_LOCK_UNLOCKED;
		if (parent == NULL) {
			// I am part of root class. so set the max tasks to available
			// default
			res->cnt_guarantee = TOTAL_NUM_TASKS;
			res->cnt_unused =  TOTAL_NUM_TASKS;
			res->cnt_limit = TOTAL_NUM_TASKS;
		}
	} else {
		printk(KERN_ERR "numtasks_res_alloc: failed GFP_ATOMIC alloc\n");
	}
	return res;
}

/*
 * No locking of this resource class object necessary as we are not
 * supposed to be assigned (or used) when/after this function is called.
 */
static void
numtasks_res_free(void *my_res)
{
	ckrm_numtasks_t *res = my_res, *parres, *childres;
	ckrm_core_class_t *child = NULL;
	int i, borrowed, maxlimit, resid = numtasks_rcbs.resid;

	if (!res) 
		return;

	// Assuming there will be no children when this function is called
	
	parres = ckrm_get_res_class(res->parent, resid, ckrm_numtasks_t);

	if (unlikely(atomic_read(&res->cnt_cur_alloc) != 0 ||
				atomic_read(&res->cnt_borrowed))) {
		printk(KERN_ERR "numtasks_res_free: resource still alloc'd %p\n", res);
		if ((borrowed = atomic_read(&res->cnt_borrowed)) > 0) {
			for (i = 0; i < borrowed; i++) {
				numtasks_put_ref(parres->core);
			}
		}
	}

	// return child's limit/guarantee to parent node
	spin_lock(&parres->cnt_lock);
	child_guarantee_changed(&parres->shares, res->shares.my_guarantee, 0);

	// run thru parent's children and get the new max_limit of the parent
	ckrm_lock_hier(parres->core);
	maxlimit = 0;
	while ((child = ckrm_get_next_child(parres->core, child)) != NULL) {
		childres = ckrm_get_res_class(child, resid, ckrm_numtasks_t);
		if (maxlimit < childres->shares.my_limit) {
			maxlimit = childres->shares.my_limit;
		}
	}
	ckrm_unlock_hier(parres->core);
	if (parres->shares.cur_max_limit < maxlimit) {
		parres->shares.cur_max_limit = maxlimit;
	}

	spin_unlock(&parres->cnt_lock);
	kfree(res);
	return;
}
/*
 * Recalculate the guarantee and limit in real units... and propagate the
 * same to children.
 * Caller is responsible for protecting res and for the integrity of parres
 */
static void
recalc_and_propagate(ckrm_numtasks_t *res, ckrm_numtasks_t *parres)
{
	ckrm_core_class_t *child = NULL;
	ckrm_numtasks_t *childres;
	int resid = numtasks_rcbs.resid;

	if (parres) {
		struct ckrm_shares *par = &parres->shares;
		struct ckrm_shares *self = &res->shares;

		// calculate cnt_guarantee and cnt_limit
		//
		if (parres->cnt_guarantee == CKRM_SHARE_DONTCARE) {
			res->cnt_guarantee = CKRM_SHARE_DONTCARE;
		} else {
			res->cnt_guarantee = (self->my_guarantee * parres->cnt_guarantee) 
					/ par->total_guarantee;
		}
		if (parres->cnt_limit == CKRM_SHARE_DONTCARE) {
			res->cnt_limit = CKRM_SHARE_DONTCARE;
		} else {
			res->cnt_limit = (self->my_limit * parres->cnt_limit)
					/ par->max_limit;
		}

		// Calculate unused units
		if (res->cnt_guarantee == CKRM_SHARE_DONTCARE) {
			res->cnt_unused = CKRM_SHARE_DONTCARE;
		} else {
			res->cnt_unused = (self->unused_guarantee *
					res->cnt_guarantee) / self->total_guarantee;
		}
	}

	// propagate to children
	ckrm_lock_hier(res->core);
	while ((child = ckrm_get_next_child(res->core, child)) != NULL) {
		childres = ckrm_get_res_class(child, resid, ckrm_numtasks_t);

		spin_lock(&childres->cnt_lock);
		recalc_and_propagate(childres, res);
		spin_unlock(&childres->cnt_lock);
	}
	ckrm_unlock_hier(res->core);
	return;
}

static int
numtasks_set_share_values(void *my_res, struct ckrm_shares *new)
{
	ckrm_numtasks_t *parres, *res = my_res;
	struct ckrm_shares *cur = &res->shares, *par;
	int rc = -EINVAL, resid = numtasks_rcbs.resid;

	if (!res) 
		return rc;

	if (res->parent) {
		parres = ckrm_get_res_class(res->parent, resid, ckrm_numtasks_t);
		spin_lock(&parres->cnt_lock);
		spin_lock(&res->cnt_lock);
		par = &parres->shares;
	} else {
		spin_lock(&res->cnt_lock);
		par = NULL;
		parres = NULL;
	}

	rc = set_shares(new, cur, par);

	if ((rc == 0) && parres) {
		// Calculate parent's unused units
		if (parres->cnt_guarantee == CKRM_SHARE_DONTCARE) {
			parres->cnt_unused = CKRM_SHARE_DONTCARE;
		} else {
			parres->cnt_unused = (par->unused_guarantee *
					parres->cnt_guarantee) / par->total_guarantee;
		}

		recalc_and_propagate(res, parres);
	}
	spin_unlock(&res->cnt_lock);
	if (res->parent) {
		spin_unlock(&parres->cnt_lock);
	}
	return rc;
}


static int
numtasks_get_share_values(void *my_res, struct ckrm_shares *shares)
{
	ckrm_numtasks_t *res = my_res;

	if (!res) 
		return -EINVAL;
	*shares = res->shares;
	return 0;
}

static int  
numtasks_get_stats(void *my_res, struct seq_file *sfile)
{
	ckrm_numtasks_t *res = my_res;

	if (!res) 
		return -EINVAL;

	seq_printf(sfile, "Number of tasks resource:\n");
	seq_printf(sfile, "Total Over limit failures: %d\n",
			res->tot_limit_failures);
	seq_printf(sfile, "Total Over guarantee sucesses: %d\n",
			res->tot_borrow_sucesses);
	seq_printf(sfile, "Total Over guarantee failures: %d\n",
			res->tot_borrow_failures);

	seq_printf(sfile, "Maximum Over limit failures: %d\n",
			res->max_limit_failures);
	seq_printf(sfile, "Maximum Over guarantee sucesses: %d\n",
			res->max_borrow_sucesses);
	seq_printf(sfile, "Maximum Over guarantee failures: %d\n",
			res->max_borrow_failures);
#ifdef NUMTASKS_DEBUG
	seq_printf(sfile, "cur_alloc %d; borrowed %d; cnt_guar %d; cnt_limit %d "
			"unused_guarantee %d, cur_max_limit %d\n",
			atomic_read(&res->cnt_cur_alloc),
			atomic_read(&res->cnt_borrowed),
			res->cnt_guarantee,
			res->cnt_limit,
			res->shares.unused_guarantee,
			res->shares.cur_max_limit);
#endif

	return 0;
}

static int  
numtasks_show_config(void *my_res, struct seq_file *sfile)
{
	ckrm_numtasks_t *res = my_res;

	if (!res) 
		return -EINVAL;

	seq_printf(sfile, "res=%s,parameter=somevalue\n",NUMTASKS_NAME);
	return 0;
}

static int  
numtasks_set_config(void *my_res, const char *cfgstr)
{
	ckrm_numtasks_t *res = my_res;

	if (!res) 
		return -EINVAL;
	printk("numtasks config='%s'\n",cfgstr);
	return 0;
}

static void
numtasks_change_resclass(void *task, void *old, void *new)
{
	ckrm_numtasks_t *oldres = old;
	ckrm_numtasks_t *newres = new;

	if (oldres != (void *) -1) {
		struct task_struct *tsk = task;
		if (!oldres) {
			struct ckrm_core_class *old_core = &(tsk->parent->taskclass->core);
			oldres = ckrm_get_res_class(old_core, numtasks_rcbs.resid,
					ckrm_numtasks_t);
		}
		numtasks_put_ref(oldres->core);
	}
	if (newres) {
		(void) numtasks_get_ref(newres->core, 1);
	}
}

struct ckrm_res_ctlr numtasks_rcbs = {
	.res_name          = NUMTASKS_NAME,
	.res_hdepth        = 1,
	.resid             = -1,
	.res_alloc         = numtasks_res_alloc,
	.res_free          = numtasks_res_free,
	.set_share_values  = numtasks_set_share_values,
	.get_share_values  = numtasks_get_share_values,
	.get_stats         = numtasks_get_stats,
	.show_config       = numtasks_show_config,
	.set_config        = numtasks_set_config,
	.change_resclass   = numtasks_change_resclass,
};

int __init
init_ckrm_numtasks_res(void)
{
	struct ckrm_classtype *clstype;
	int resid = numtasks_rcbs.resid;

	clstype = ckrm_find_classtype_by_name("taskclass");
	if (clstype == NULL) {
		printk(KERN_INFO " Unknown ckrm classtype<taskclass>");
		return -ENOENT;
	}

	if (resid == -1) {
		resid = ckrm_register_res_ctlr(clstype,&numtasks_rcbs);
		printk("........init_ckrm_numtasks_res -> %d\n",resid);
	}
	return 0;
}	

void __exit
exit_ckrm_numtasks_res(void)
{
	ckrm_unregister_res_ctlr(&numtasks_rcbs);
	numtasks_rcbs.resid = -1;
}

module_init(init_ckrm_numtasks_res)
module_exit(exit_ckrm_numtasks_res)

EXPORT_SYMBOL(numtasks_get_ref);
EXPORT_SYMBOL(numtasks_put_ref);

MODULE_LICENSE("GPL");

