/* ckrm_dummy.c - Dummy Resource Manager for CKRM
 *
 * Copyright (C) Chandra Seetharaman,  IBM Corp. 2003
 * Copyright (C) Hubertus Franke,      IBM Corp. 2004
 * 
 * Provides a Dummy Resource controller for CKRM
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
 * 06 Nov 2003: Created
 * 03 Mar 2004: Adopted to new Interface
 * 
 */

/* Code Description: TBD
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/errno.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/ckrm.h>
#include <linux/ckrm_rc.h>

#define DEBUG_CKRM_DUMMY 1

typedef struct ckrm_dummy_res {
	struct ckrm_hnode  hnode;    /* build our own hierarchy */
	struct ckrm_shares shares;
} ckrm_dummy_res_t;

static int my_resid = -1;

#define get_parent(res)  (container_of((res)->hnode.parent,ckrm_dummy_res_t,hnode))



/* Initialize rescls values
 * May be called on each rcfs unmount or as part of error recovery
 * to make share values sane.
 * Does not traverse hierarchy reinitializing children.
 */

static void
dummy_res_initcls_one(void *my_res)
{
	ckrm_dummy_res_t *res = my_res;

	res->shares.my_guarantee     = CKRM_SHARE_DONTCARE;
	res->shares.my_limit         = CKRM_SHARE_DONTCARE;
	res->shares.total_guarantee  = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.total_limit      = CKRM_SHARE_DFLT_TOTAL_LIMIT;
	res->shares.unused_guarantee = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.unused_limit     = CKRM_SHARE_DFLT_TOTAL_LIMIT;
	
	/* Don't initiate propagation to children here, caller will do it if needed */
}

static void
dummy_res_initcls(void *my_res)
{
	ckrm_dummy_res_t *res = my_res;

	/* Write a version which propagates values all the way down 
	   and replace rcbs callback with that version */
	
}

ckrm_dummy_res_t *dres;
struct ckrm_hnode  *dparhnode;
ckrm_dummy_res_t *dparres;

static void *
dummy_res_alloc(struct ckrm_core_class *core, struct ckrm_core_class *parent)
{
	//ckrm_dummy_res_t *res;
	

	dres = kmalloc(sizeof(ckrm_dummy_res_t), GFP_ATOMIC);
	
	if (dres) {
		//struct ckrm_hnode *parhnode = NULL;
		dparhnode = NULL ;

		if (is_core_valid(parent)) {
			// ckrm_dummy_res_t  *parres;
			dparres = ckrm_get_res_class(parent,my_resid,ckrm_dummy_res_t);
			dparhnode = &dparres->hnode;
		}
		ckrm_hnode_add(&dres->hnode, dparhnode);
		printk(KERN_ERR "dummy_res_alloc: Adding dummy res class %p to %p\n",dres,parent);
		
		/* rescls in place, now initialize contents other than hierarchy pointers */
		dummy_res_initcls_one(dres);
		
		/*
		  else {
			kfree(dres);
			printk(KERN_ERR "dummy_res_alloc: Invalid core \n");
			return NULL;
		}
		*/
	}
	else
		printk(KERN_ERR "dummy_res_alloc: failed GFP_ATOMIC alloc\n");
	return dres;
}

ckrm_dummy_res_t *d2res, *d2parres;


static void
dummy_res_free(void *my_res)
{
	//ckrm_dummy_res_t *res = my_res;
	//ckrm_dummy_res_t *parres;

	d2res = my_res ;

	if (!d2res) 
		return;

	d2parres = get_parent(d2res);
	ckrm_hnode_remove(&d2res->hnode);

	// return child's limit/guarantee to parent node
	if (d2parres) {
		if (d2res->shares.my_guarantee >= 0)
			d2parres->shares.unused_guarantee += d2res->shares.my_guarantee;
		if (d2res->shares.my_limit >= 0)
			d2parres->shares.unused_limit += d2res->shares.my_limit;
	}
	kfree(d2res);
	return;
}

static int
dummy_set_share_values(void *my_res, struct ckrm_shares *shares)
{
	ckrm_dummy_res_t *res = my_res;
	ckrm_dummy_res_t *parent;
	int reduce_by;
	int rc = EINVAL;

	if (!res) 
		return -EINVAL;

	parent = get_parent(res);

	// we have to ensure that the set of parameters is OK

	// ensure that lim/guarantees are ok wrt to parent total values 
	// don't have to consider negative special values

	/* FIXME following doesn't appear to be working */
	if (parent) {
		if ((shares->my_guarantee > parent->shares.unused_guarantee) ||
		    (shares->my_limit > parent->shares.unused_limit))
			goto set_share_err;
	}

	// translate UNCHANGED to existing values
	if (shares->total_guarantee == CKRM_SHARE_UNCHANGED)
		shares->total_guarantee = res->shares.total_guarantee;
	if (shares->total_limit == CKRM_SHARE_UNCHANGED)
		shares->total_limit = res->shares.total_limit;

	// we don't allow DONTCARE for totals
	if ((shares->total_guarantee <= CKRM_SHARE_DONTCARE) || (shares->total_limit <= CKRM_SHARE_DONTCARE))
		goto set_share_err;

	// check whether total shares still exceeds sum of children (total - unused)
	if (((reduce_by = shares->total_guarantee - res->shares.total_guarantee) > 0) &&
	    (reduce_by > res->shares.unused_guarantee))
		goto set_share_err;
	if (((reduce_by = shares->total_limit - res->shares.total_limit) > 0) &&
	    (reduce_by > res->shares.unused_limit))
		goto set_share_err;


	/* Need more sanity checks (first two not being enforced currently):
	 *  1. guarantee < limit
         *  2. my_* < tot_*
	 *  etc. Do later.
	 */
	// values are OK now enforce them 

	if (shares->my_guarantee > 0) {
		if (parent) { 
			parent->shares.unused_guarantee -= shares->my_guarantee;
			if (res->shares.my_guarantee >= 0)
				parent->shares.unused_guarantee += res->shares.my_guarantee;
		}
		res->shares.my_guarantee = shares->my_guarantee;
	} else if (shares->my_guarantee == CKRM_SHARE_DONTCARE) {
		if (parent) 
			parent->shares.unused_guarantee += res->shares.my_guarantee;
		res->shares.my_guarantee = CKRM_SHARE_DONTCARE;
	}

	if (shares->my_limit > 0) {
		if (parent) { 
			parent->shares.unused_limit -= shares->my_limit;
			if (res->shares.my_limit >= 0)
				parent->shares.unused_limit += res->shares.my_limit;
		}
		res->shares.my_limit = shares->my_limit;
	} else if (shares->my_limit == CKRM_SHARE_DONTCARE) {
		if (parent) 
			parent->shares.unused_limit += res->shares.my_limit;
		res->shares.my_limit = CKRM_SHARE_DONTCARE;
	}


	res->shares.unused_guarantee += (shares->total_guarantee - res->shares.total_guarantee);
	res->shares.unused_limit     += (shares->total_limit     - res->shares.total_limit);

	res->shares.total_guarantee  = shares->total_guarantee;
	res->shares.total_limit      = shares->total_limit;

	/* Here we should force the propagation of share values */
	
	rc = 0;
	goto out;

set_share_err:
#ifdef DEBUG_CKRM_DUMMY
	dummy_res_initcls_one(res);
	rc = 0;
#endif
out:
	
	return rc;
}

static int
dummy_get_share_values(void *my_res, struct ckrm_shares *shares)
{
	ckrm_dummy_res_t *res = my_res;

	if (!res) 
		return -EINVAL;
	*shares = res->shares;
	return 0;
}

static int  
dummy_get_stats(void *my_res, struct seq_file *sfile)
{
	ckrm_dummy_res_t *res = my_res;

	if (!res) 
		return -EINVAL;

	seq_printf(sfile, "res=dummy: these are my stats <none>\n");

	return 0;
}

static void
dummy_change_resclass(struct task_struct *tsk, void *old, void *new)
{
	// does nothing
	return;
}

ckrm_res_callback_t dummy_rcbs = {
	.res_name          = "dummy",
	.res_hdepth	   = 1,
	.res_type	   = CKRM_TASK_CLASS,
	.resid		   = CKRM_RES_DUMMY,
	.res_alloc         = dummy_res_alloc,
	.res_free          = dummy_res_free,
	.set_share_values  = dummy_set_share_values,
	.get_share_values  = dummy_get_share_values,
	.get_stats         = dummy_get_stats,
	.change_resclass   = dummy_change_resclass,
	.res_initcls       = dummy_res_initcls_one,
};

int __init
init_ckrm_dummy_res(void)
{
	my_resid = ckrm_register_res_ctlr(&dummy_rcbs);
	return 0;
}	

void __exit
exit_ckrm_dummy_res(void)
{
	ckrm_unregister_res_ctlr(my_resid);
}


module_init(init_ckrm_dummy_res)
module_exit(exit_ckrm_dummy_res)

MODULE_LICENSE("GPL");

