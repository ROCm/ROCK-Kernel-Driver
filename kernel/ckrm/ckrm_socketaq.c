/* ckrm_socketaq.c - Socket accept queue resource controller
 *
 * Copyright (C) Vivek Kashyap,      IBM Corp. 2004
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
 * Initial version
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
#include <net/tcp.h>

#define DEBUG_CKRM_SAQ 1

#define SAQ_MAX_HIERARCHY 0 	/*the user is not allowed to create a subclass*/

typedef struct ckrm_saq_res {
	struct ckrm_hnode  hnode;    /* build our own hierarchy */
	struct ckrm_shares shares;
	struct ckrm_core_class *core;
} ckrm_saq_res_t;

static int my_resid = -1;

#define get_parent(res) (container_of((res)->hnode.parent,ckrm_saq_res_t,hnode))


extern struct dentry * 
rcfs_create_internal(struct dentry *parent, const char *name, int mfmode, int magic) ;


/* Initialize rescls values
 */

static void
saq_res_initcls_zero(void *my_res)
{
	ckrm_saq_res_t *res = my_res;

	res->shares.my_guarantee     = CKRM_SHARE_DONTCARE;
	res->shares.my_limit         = CKRM_SHARE_DONTCARE;
	res->shares.total_guarantee  = 0; 
	res->shares.total_limit      = 0;
	res->shares.unused_guarantee = 0;
	res->shares.unused_limit     = 0;
	
	/* Don't initiate propagation to children here, caller will do it if needed */
}

static void
saq_res_initcls_one(void *my_res)
{
	ckrm_saq_res_t *res = my_res;

	res->shares.my_guarantee     = CKRM_SHARE_DONTCARE;
	res->shares.my_limit         = CKRM_SHARE_DONTCARE;
	res->shares.total_guarantee  = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.total_limit      = CKRM_SHARE_DFLT_TOTAL_LIMIT;
	res->shares.unused_guarantee = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.unused_limit     = CKRM_SHARE_DFLT_TOTAL_LIMIT;
	
	/* Don't initiate propagation to children here, caller will do it if needed */
}
static void
saq_res_initcls(void *my_res)
{
	ckrm_saq_res_t *res = my_res;

	res->shares.my_guarantee     = CKRM_SHARE_DONTCARE;
	res->shares.my_limit         = CKRM_SHARE_DONTCARE;
	res->shares.total_guarantee  = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.total_limit      = CKRM_SHARE_DFLT_TOTAL_LIMIT;
	res->shares.unused_guarantee = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.unused_limit     = CKRM_SHARE_DFLT_TOTAL_LIMIT;
	
	/* Don't initiate propagation to children here, caller will do it if needed */
}

static void *
saq_res_alloc(struct ckrm_core_class *core, struct ckrm_core_class *parent)
{
	ckrm_saq_res_t *res;

	res = kmalloc(sizeof(ckrm_saq_res_t), GFP_KERNEL);
	if (res) {
		struct ckrm_hnode *parhnode = NULL;
		
		if (parent) {
			ckrm_saq_res_t  *parres;
			parres = ckrm_get_res_class(parent,my_resid,
								ckrm_saq_res_t);
			parhnode = &parres->hnode;
		}
		res->core = core;
		ckrm_hnode_add(&res->hnode, parhnode);
		printk(KERN_ERR "saq_res_alloc: Adding saq res class %p to %p\n",res,parent);

		/* rescls in place, now initialize contents other than hierarchy pointers */
		saq_res_initcls_one(res);
	}
	return res;
}

static void
saq_res_free(void *my_res)
{
	ckrm_saq_res_t *res = my_res;
	ckrm_saq_res_t *parent;

	if (!res) 
		return;

	parent = get_parent(res);
	ckrm_hnode_remove(&res->hnode);

	// return child's limit/guarantee to parent node
	if (parent) {
		if (res->shares.my_guarantee >= 0)
			parent->shares.unused_guarantee += res->shares.my_guarantee;
		if (res->shares.my_limit >= 0)
			parent->shares.unused_limit += res->shares.my_limit;
	}
	kfree(res);
	return;
}

int
saq_set_aq_values(ckrm_saq_res_t *my_res, struct ckrm_shares *shares)
{

	int i = 0; 
	int j;
	char name[4];
	struct dentry *parent = my_res->core->dentry;

#ifdef CONFIG_ACCEPT_QUEUES
	int cnt = NUM_ACCEPT_QUEUES;
#else
	int cnt = 8;
#endif

	if (list_empty(&my_res->hnode.children)) {
		for( i = 1; i < cnt; i++) {
			j = sprintf(name, "%d",i);
			name[j] = '\0';
			rcfs_create_internal(parent,name, 
					parent->d_inode->i_mode,1);
		}
	}

	return 0;
	/*
 	* Set the shares
	*/
	// USEME
	// for the recalculated shares
	// 	find each socket in members
	//		update its accept queue weights
	//
}



static int
saq_set_share_values(void *my_res, struct ckrm_shares *shares)
{
	ckrm_saq_res_t *res = my_res;
	ckrm_saq_res_t *parent;
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
	rc = saq_set_aq_values(my_res,shares);
	goto out;

set_share_err:
#ifdef DEBUG_CKRM_DUMMY
	saq_res_initcls_one(res);
	rc = 0;
#endif
out:
	
	return rc;
}

static int
saq_get_share_values(void *my_res, struct ckrm_shares *shares)
{
	ckrm_saq_res_t *res = my_res;

	if (!res) 
		return -EINVAL;
	*shares = res->shares;
	return 0;
}

static int  
saq_get_stats(void *my_res, struct seq_file *sfile)
{
	ckrm_saq_res_t *res = my_res;

	if (!res) 
		return -EINVAL;

	seq_printf(sfile, "res=saq: these are my stats <none>\n");

	return 0;
}

static void
saq_change_resclass(struct task_struct *tsk, void *old, void *new)
{
	// does nothing
	return;
}

ckrm_res_callback_t saq_rcbs = {
	.res_name          = "saq",
	.res_type	   = CKRM_NET_CLASS,
	.resid		   = CKRM_RES_SAQ,
	.res_hdepth        = SAQ_MAX_HIERARCHY,
	.res_alloc         = saq_res_alloc,
	.res_free          = saq_res_free,
	.set_share_values  = saq_set_share_values,
	.get_share_values  = saq_get_share_values,
	.get_stats         = saq_get_stats,
	.change_resclass   = saq_change_resclass,
	.res_initcls       = saq_res_initcls,
};

int __init
init_ckrm_saq_res(void)
{
	my_resid = ckrm_register_res_ctlr(&saq_rcbs);
	printk(KERN_INFO "SAQ resid is %d\n",my_resid);
	return 0;
}	

void __exit
exit_ckrm_saq_res(void)
{
	ckrm_unregister_res_ctlr(my_resid);
}


module_init(init_ckrm_saq_res)
module_exit(exit_ckrm_saq_res)

MODULE_LICENSE("GPL");

