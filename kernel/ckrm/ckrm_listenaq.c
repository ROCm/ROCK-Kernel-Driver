/* ckrm_socketaq.c - accept queue resource controller
 *
 * Copyright (C) Vivek Kashyap,      IBM Corp. 2004
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

#include <linux/ckrm_net.h>

#define hnode_2_core(ptr) \
                ((ptr) ? container_of(ptr, struct ckrm_core_class, hnode) : NULL)


#define CKRM_SAQ_MAX_DEPTH	3 // 0 => /rcfs
				  // 1 => socket_aq
				  // 2 => socket_aq/listen_class
				  // 3 => socket_aq/listen_class/accept_queues
				  // 4 => Not allowed

typedef struct ckrm_laq_res {
	spinlock_t		reslock;
	atomic_t		refcnt;
	struct ckrm_shares 	shares;
	struct ckrm_core_class *core;
	struct ckrm_core_class *pcore;
	int 			my_depth;
	int 			my_id;
} ckrm_laq_res_t;

static int my_resid = -1;

extern	struct ckrm_core_class *rcfs_create_under_netroot(char *, int, int);
extern struct ckrm_core_class *rcfs_make_core(struct dentry *, 
						struct ckrm_core_class * ) ;

void
laq_res_hold(struct ckrm_laq_res *res)
{
        atomic_inc(&res->refcnt);
	return;
}

void
laq_res_put(struct ckrm_laq_res *res)
{
	if (atomic_dec_and_test(&res->refcnt))
		kfree(res);
	return;
}

/* Initialize rescls values
 */
static void
laq_res_initcls(void *my_res)
{
	ckrm_laq_res_t *res = my_res;

	res->shares.my_guarantee     = CKRM_SHARE_DONTCARE;
	res->shares.my_limit         = CKRM_SHARE_DONTCARE;
	res->shares.total_guarantee  = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.max_limit        = CKRM_SHARE_DFLT_MAX_LIMIT;
	res->shares.unused_guarantee = CKRM_SHARE_DFLT_TOTAL_GUARANTEE;
	res->shares.cur_max_limit    = 0;
}

static int 
atoi(char *s)
{
	int k = 0;
	while(*s) 
		k = *s++ - '0' + (k * 10);
	return k;
}

static char *
laq_get_name(struct ckrm_core_class *c)
{
        char *p = (char *)c->name;

        while(*p)
                p++;
        while( *p != '/' && p != c->name)
                p--;

        return ++p;
}

static void *
laq_res_alloc(struct ckrm_core_class *core, struct ckrm_core_class *parent)
{
	ckrm_laq_res_t *res, *pres;
	int pdepth;

	if (parent)
		pres = ckrm_get_res_class(parent, my_resid, ckrm_laq_res_t);
	else
		pres = NULL;

	if (core == core->classtype->default_class)    
		pdepth = 1;
	else {
		if (!parent)
			return NULL;
		pdepth = 1 + pres->my_depth;
	}

	res = kmalloc(sizeof(ckrm_laq_res_t), GFP_ATOMIC);
	if (res) {
		memset(res, 0, sizeof(res));
		spin_lock_init(&res->reslock);
		laq_res_hold(res);
		res->my_depth  = pdepth;
		if (pdepth == 2)	// listen class
			res->my_id = 0;
		else if (pdepth == 3)
			res->my_id = atoi(laq_get_name(core));
		res->core = core;
		res->pcore = parent;

		// rescls in place, now initialize contents other than 
		// hierarchy pointers
		laq_res_initcls(res); // acts as initialising value
	}

	return res;
}

static void
laq_res_free(void *my_res)
{
	ckrm_laq_res_t *res = (ckrm_laq_res_t *)my_res;
	ckrm_laq_res_t *parent;

	if (!res) 
		return;

	if (res->my_depth != 3) {
		kfree(res);
		return;
	}

	parent = ckrm_get_res_class(res->pcore, my_resid, ckrm_laq_res_t);
	if (!parent)	// Should never happen
		return;

	spin_lock(&parent->reslock);
	spin_lock(&res->reslock);

	// return child's guarantee to parent node
	// Limits have no meaning for accept queue control
	child_guarantee_changed(&parent->shares, res->shares.my_guarantee, 0);

	spin_unlock(&res->reslock);
	laq_res_put(res);	
	spin_unlock(&parent->reslock);
	return;
}

/**************************************************************************
 * 			SHARES					        ***
 **************************************************************************/

void
laq_set_aq_values(ckrm_laq_res_t *my_res, ckrm_laq_res_t *parent, int updatep)
{

	struct ckrm_net_struct *ns;
	struct ckrm_core_class *core = parent->core;
	struct tcp_opt *tp;
	
	if (my_res->my_depth < 2) 
		return;
	
	// XXX Instead of holding a  class_lock introduce a rw
	// lock to be write locked by listen callbacks and read locked here.
	// - VK
	class_lock(core);
	list_for_each_entry(ns, &core->objlist,ckrm_link) { 
		tp = tcp_sk(ns->ns_sk);
		if (updatep)
			tp->acceptq[0].aq_ratio =
			       parent->shares.total_guarantee/
				parent->shares.unused_guarantee;	       

		tp->acceptq[my_res->my_id].aq_ratio =
		       my_res->shares.total_guarantee/
			parent->shares.my_guarantee;	       
	}
	class_unlock(core);
	return;
}

static int
laq_set_share_values(void *my_res, struct ckrm_shares *shares)
{
	ckrm_laq_res_t *res = my_res;
	ckrm_laq_res_t *parent, *child;
	struct ckrm_hnode *chnode; 
	int rc = 0;

	if (!res) 
		return -EINVAL;

	if (!res->pcore) { 
		// something is badly wrong
		printk(KERN_ERR "socketaq internal inconsistency\n");
		return -EBADF;
	}

	parent = ckrm_get_res_class(res->pcore, my_resid, ckrm_laq_res_t);
	if (!parent)	// socket_class does not have a share interface
		return -EINVAL;

	// Ensure that we ignore limit values
	shares->my_limit = shares->max_limit = CKRM_SHARE_UNCHANGED;

	switch (res->my_depth) {

	case 0: printk(KERN_ERR "socketaq bad entry\n");
		rc = -EBADF;
		break;

	case 1: // can't be written to. this is internal default.
		// return -EINVAL
		rc = -EINVAL;
		break;

	case 2: // nothing to inherit
		if (!shares->total_guarantee) {
			rc = -EINVAL;
			break;
		}

		ckrm_lock_hier(res->pcore);
		spin_lock(&res->reslock);
		rc = set_shares(shares, &res->shares, NULL);
		if (!rc) {
			list_for_each_entry(chnode,
					&res->core->hnode.children,siblings){
				child=hnode_2_core(chnode)->res_class[my_resid];
				laq_set_aq_values(child,res,(child->my_id==1));
			}
		}
		spin_unlock(&res->reslock);
		ckrm_unlock_hier(res->pcore);
		break;

	case 3: // accept queue itself. Check against parent.
		ckrm_lock_hier(parent->pcore);
		spin_lock(&parent->reslock);
		rc = set_shares(shares, &res->shares, &parent->shares);
		if (!rc) {
			laq_set_aq_values(res,parent,1);
		}
		spin_unlock(&parent->reslock);
		ckrm_unlock_hier(parent->pcore);
		break;
	}

	return rc;
}

static int
laq_get_share_values(void *my_res, struct ckrm_shares *shares)
{
	ckrm_laq_res_t *res = my_res;

	if (!res) 
		return -EINVAL;
	*shares = res->shares;
	return 0;
}

/**************************************************************************
 * 			STATS						***
 **************************************************************************/

void
laq_print_aq_stats(struct seq_file *sfile, struct tcp_acceptq_info *taq, int i)
{
	seq_printf(sfile, "Class %d connections:\n\taccepted: %u\n\t"
			  "queued: %u\n\twait_time: %lu\n\t",
			  i, taq->acceptq_count, taq->acceptq_qcount,
			  taq->acceptq_wait_time);

	if (i)
		return;

	for (i = 1; i < NUM_ACCEPT_QUEUES; i++) {
		taq[0].acceptq_wait_time += taq[i].acceptq_wait_time;
		taq[0].acceptq_qcount += taq[i].acceptq_qcount;
		taq[0].acceptq_count += taq[i].acceptq_count;
	}

	seq_printf(sfile, "Totals :\n\taccepted: %u\n\t"
			  "queued: %u\n\twait_time: %lu\n",
			   taq->acceptq_count, taq->acceptq_qcount,
			  taq->acceptq_wait_time);

	return;
}

void
laq_get_aq_stats(ckrm_laq_res_t *pres, ckrm_laq_res_t *mres, 
					struct tcp_acceptq_info *taq)
{
	struct ckrm_net_struct *ns;
	struct ckrm_core_class *core = pres->core;
	struct tcp_opt *tp;
	int a = mres->my_id;
	int z;

	if (a == 0)
		z = NUM_ACCEPT_QUEUES;
	else
		z = a+1;

	// XXX Instead of holding a  class_lock introduce a rw
	// lock to be write locked by listen callbacks and read locked here.
	// - VK
	class_lock(pres->core);
	list_for_each_entry(ns, &core->objlist,ckrm_link) { 
		tp = tcp_sk(ns->ns_sk);
		for (; a< z; a++) {
			taq->acceptq_wait_time += tp->acceptq[a].aq_wait_time;
			taq->acceptq_qcount += tp->acceptq[a].aq_qcount;
			taq->acceptq_count += tp->acceptq[a].aq_count;
			taq++;
		}
	}
	class_unlock(pres->core);
}


static int  
laq_get_stats(void *my_res, struct seq_file *sfile)
{
	ckrm_laq_res_t *res = my_res;
	ckrm_laq_res_t *parent;
	struct tcp_acceptq_info taq[NUM_ACCEPT_QUEUES];
	int rc = 0;

	if (!res) 
		return -EINVAL;
	
	if (!res->pcore) { 
		// something is badly wrong
		printk(KERN_ERR "socketaq internal inconsistency\n");
		return -EBADF;
	}

	parent = ckrm_get_res_class(res->pcore, my_resid, ckrm_laq_res_t);
	if (!parent) {	// socket_class does not have a stat interface
		printk(KERN_ERR "socketaq internal fs inconsistency\n");
		return -EINVAL;
	}

	memset(taq, 0, sizeof(struct tcp_acceptq_info) * NUM_ACCEPT_QUEUES);

	switch (res->my_depth) {

	default:
	case 0: printk(KERN_ERR "socket class bad entry\n");
		rc = -EBADF;
		break;

	case 1: // can't be read from. this is internal default.
		// return -EINVAL
		rc = -EINVAL;
		break;

	case 2: // return the default and total
		ckrm_lock_hier(res->core);	// block any deletes
		laq_get_aq_stats(res, res, &taq[0]);
		laq_print_aq_stats(sfile, &taq[0], 0);
		ckrm_unlock_hier(res->core);	// block any deletes
		break;

	case 3: 
		ckrm_lock_hier(parent->core);	// block any deletes
		laq_get_aq_stats(parent, res, &taq[res->my_id]);
		laq_print_aq_stats(sfile, &taq[res->my_id], res->my_id);
		ckrm_unlock_hier(parent->core);	// block any deletes
		break;
	}

	return rc;
}

/*
 * The network connection is reclassified to this class. Update its shares.
 * The socket lock is held. 
 */
static void
laq_change_resclass(void *n, void *old, void *r)
{
	struct ckrm_net_struct *ns = (struct ckrm_net_struct *)n;
	struct ckrm_laq_res *res = (struct ckrm_laq_res *)r;
	struct ckrm_hnode  *chnode = NULL;


	if (res->my_depth != 2) 
		return;	

	// a change to my_depth == 3 ie. the accept classes cannot happen.
	// there is no target file
	if (res->my_depth == 2) { // it is one of the socket classes
		struct ckrm_laq_res *reschild;
		struct sock *sk = ns->ns_sk; 
		struct tcp_opt *tp = tcp_sk(sk);

		// share rule: hold parent resource lock. then self.
		// However, since my_depth == 1 is a generic class it is not
		// needed here. Self lock is enough.
		spin_lock(&res->reslock);
		tp->acceptq[0].aq_ratio = res->shares.total_guarantee/
				res->shares.unused_guarantee;
		list_for_each_entry(chnode,&res->core->hnode.children,siblings){
			reschild = hnode_2_core(chnode)->res_class[my_resid];

			spin_lock(&reschild->reslock);
			tp->acceptq[reschild->my_id].aq_ratio=
				reschild->shares.total_guarantee/
					res->shares.my_guarantee;
			spin_unlock(&reschild->reslock);
		}
		spin_unlock(&res->reslock);
	}
	
	return;
}

struct ckrm_res_ctlr laq_rcbs = {
	.res_name          = "laq",
	.resid		   = -1 , // dynamically assigned
	.res_alloc         = laq_res_alloc,
	.res_free          = laq_res_free,
	.set_share_values  = laq_set_share_values,
	.get_share_values  = laq_get_share_values,
	.get_stats         = laq_get_stats,
	.change_resclass   = laq_change_resclass,
	//	.res_initcls       = laq_res_initcls,         // LAQ_HUBERTUS: no need for this !!
};

int __init
init_ckrm_laq_res(void)
{
	struct ckrm_classtype *clstype;
	int resid;

	clstype = ckrm_find_classtype_by_name("socket_class");
	if (clstype == NULL) {
		printk(KERN_INFO " Unknown ckrm classtype<socket_class>");
		return -ENOENT;
	}

	if (my_resid == -1) {
		resid = ckrm_register_res_ctlr(clstype,&laq_rcbs);
		if (resid >= 0)
			my_resid = resid;
		printk("........init_ckrm_listen_aq_res -> %d\n",my_resid);
	}
	return 0;

}	

void __exit
exit_ckrm_laq_res(void)
{
	ckrm_unregister_res_ctlr(&laq_rcbs);
	my_resid = -1;
}


module_init(init_ckrm_laq_res)
module_exit(exit_ckrm_laq_res)

MODULE_LICENSE("GPL");

