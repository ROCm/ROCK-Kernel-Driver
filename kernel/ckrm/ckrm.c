/* ckrm.c - Class-based Kernel Resource Management (CKRM)
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003, 2004
 *           (C) Shailabh Nagar,  IBM Corp. 2003, 2004
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
 * 19 Apr 2004
 *        Integrated ckrm hooks, classtypes, ...
 *  
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
#include <linux/rcfs.h>
#include <net/sock.h>
#include <linux/ip.h>


rwlock_t ckrm_class_lock = RW_LOCK_UNLOCKED;  // protect classlists 

struct rcfs_functions rcfs_fn ;
EXPORT_SYMBOL(rcfs_fn);

/**************************************************************************
 *                   Helper Functions                                     *
 **************************************************************************/

/*
 * Return TRUE if the given core class pointer is valid.
 */

/*
 * Return TRUE if the given resource is registered.
 */
inline unsigned int
is_res_regd(struct ckrm_classtype *clstype, int resid)
{
	return ( (resid>=0) && (resid < clstype->max_resid) &&
		 test_bit(resid, &clstype->bit_res_ctlrs)
		);
}

struct ckrm_res_ctlr*
ckrm_resctlr_lookup(struct ckrm_classtype *clstype, const char *resname)
{
	int resid = -1;
	
	for (resid=0; resid < clstype->max_resid; resid++) { 
		if (test_bit(resid, &clstype->bit_res_ctlrs)) {
			struct ckrm_res_ctlr *rctrl = clstype->res_ctlrs[resid];
			if (!strncmp(resname, rctrl->res_name,CKRM_MAX_RES_NAME))
				return rctrl;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(ckrm_resctlr_lookup);

/* given a classname return the class handle and its classtype*/
void *
ckrm_classobj(char *classname, int *classTypeID)
{
	int i;

	*classTypeID = -1;
	if (!classname || !*classname) {
		return NULL;
	}

	read_lock(&ckrm_class_lock);
	for ( i=0 ; i<CKRM_MAX_CLASSTYPES; i++) {
		struct ckrm_classtype *ctype = ckrm_classtypes[i];
		struct ckrm_core_class *core;

		if (ctype == NULL) 
			continue;
		list_for_each_entry(core, &ctype->classes, clslist) {
			if (core->name && !strcmp(core->name, classname)) {
				// FIXME:   should grep reference..
				read_unlock(&ckrm_class_lock);
				*classTypeID = ctype->typeID;
				return core;
			}
		}
	}
	read_unlock(&ckrm_class_lock);
	return NULL;
}

EXPORT_SYMBOL(is_res_regd);
EXPORT_SYMBOL(ckrm_classobj);

/**************************************************************************
 *                   Internal Functions/macros                            *
 **************************************************************************/

static inline void 
set_callbacks_active(struct ckrm_classtype *ctype)
{
	ctype->ce_cb_active = ((atomic_read(&ctype->ce_nr_users) > 0) &&
			       (ctype->ce_callbacks.always_callback || (ctype->num_classes > 1)));
}

int
ckrm_validate_and_grab_core(struct ckrm_core_class *core)
{
	int rc = 0;
	read_lock(&ckrm_class_lock);
	if (likely(ckrm_is_core_valid(core))) {
		ckrm_core_grab(core);
		rc = 1;
	}
	read_unlock(&ckrm_class_lock);
	return rc;
}

/****************************************************************************
 *           Interfaces for classification engine                           *
 ****************************************************************************/

/*
 * Registering a callback structure by the classification engine.
 *
 * Returns typeId of class on success -errno for failure.
 */
int
ckrm_register_engine(const char *typename, ckrm_eng_callback_t *ecbs)
{
	struct ckrm_classtype *ctype;

	ctype = ckrm_find_classtype_by_name(typename);
	if (ctype == NULL) 
		return (-ENOENT);

	ce_protect(ctype);
	if (atomic_read(&ctype->ce_nr_users) != 1) {
		// Some engine is acive, deregister it first.
		ce_release(ctype);
		return (-EBUSY);
	}
	
	/* we require that either classify and class_delete are set (due to object reference)
	 * or that notify is set (in case no real classification is supported only notification
	 * also require that the function pointer be set the momement the mask is non-null
	 */
	if ( ! (((ecbs->classify) && (ecbs->class_delete)) || (ecbs->notify)) ||
	     (ecbs->c_interest && ecbs->classify == NULL) ||
	     (ecbs->n_interest && ecbs->notify == NULL) )
	{
		ce_release(ctype);
		return (-EINVAL);
	}
	

	/* Is any other engine registered for this classtype ? */
	if (ctype->ce_regd) {
		ce_release(ctype);
		return (-EINVAL);
	}
	
	ctype->ce_regd = 1;
	ctype->ce_callbacks = *ecbs;
	set_callbacks_active(ctype);
	if (ctype->ce_callbacks.class_add) 
		(*ctype->ce_callbacks.class_add)(ctype->default_class->name,ctype->default_class);
	return ctype->typeID;
}

/*
 * Unregistering a callback structure by the classification engine.
 *
 * Returns 0 on success -errno for failure.
 */
int
ckrm_unregister_engine(const char *typename)
{
	struct ckrm_classtype *ctype;

	ctype = ckrm_find_classtype_by_name(typename);
	if (ctype == NULL) 
		return (-ENOENT);

	ctype->ce_cb_active = 0; 

	if (atomic_dec_and_test(&ctype->ce_nr_users) != 1) {
		// Somebody is currently using the engine, cannot deregister.
		atomic_inc(&ctype->ce_nr_users);
		return (-EBUSY);
	}

	ctype->ce_regd = 0;
	memset(&ctype->ce_callbacks, 0, sizeof(ckrm_eng_callback_t));
	return 0;
}

/****************************************************************************
 *           Interfaces to manipulate class (core or resource) hierarchies 
 ****************************************************************************/

/* 
 */
static void
ckrm_add_child(struct ckrm_core_class *parent, struct ckrm_core_class *child)
{
	struct ckrm_hnode *cnode = &child->hnode;

	if (!ckrm_is_core_valid(child)) {
		printk(KERN_ERR "Invalid child %p given in ckrm_add_child\n", child);
		return;
	}
	
	spin_lock(&child->ckrm_lock);
	INIT_LIST_HEAD(&cnode->children);
	INIT_LIST_HEAD(&cnode->siblings);

 	if (parent) {
		struct ckrm_hnode *pnode;

		if (!ckrm_is_core_valid(parent)) {
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

	if (!ckrm_is_core_valid(child)) {
		printk(KERN_ERR "Invalid child %p given in ckrm_remove_child\n", child);
		return 0;
	}

	cnode = &child->hnode;
	parent = cnode->parent;
	if (!ckrm_is_core_valid(parent)) {
		printk(KERN_ERR "Invalid parent %p in ckrm_remove_child\n", parent);
		return 0;
	}

	pnode = &parent->hnode;

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
	if (ckrm_is_core_valid(parent)) {
		read_lock(&parent->hnode_rwlock);
	}
}

void 
ckrm_unlock_hier(struct ckrm_core_class *parent)
{
	if (ckrm_is_core_valid(parent)) {
		read_unlock(&parent->hnode_rwlock);
	}
}

/*
 * hnode_rwlock of the parent core class must held in read mode.
 * external callers should 've called ckrm_lock_hier before calling this
 * function.
 */
#define hnode_2_core(ptr) ((ptr) ? container_of(ptr, struct ckrm_core_class, hnode) : NULL)

struct ckrm_core_class *
ckrm_get_next_child(struct ckrm_core_class *parent,
			struct ckrm_core_class *child)
{
	struct list_head *cnode;
	struct ckrm_hnode *next_cnode;
	struct ckrm_core_class *next_childcore;

	if (!ckrm_is_core_valid(parent)) {
		printk(KERN_ERR "Invalid parent %p in ckrm_get_next_child\n", parent);
		return NULL;
	}
	if (list_empty(&parent->hnode.children)) {
		return NULL;
	}

	if (child) {
		if (!ckrm_is_core_valid(child)) {
			printk(KERN_ERR "Invalid child %p in ckrm_get_next_child\n", child);
			return NULL;
		}
		cnode = child->hnode.siblings.next;
	} else {
		cnode = parent->hnode.children.next;
	}

	if (cnode == &parent->hnode.children) { // back at the anchor
		return NULL;
	}

	next_cnode = container_of(cnode, struct ckrm_hnode, siblings);
	next_childcore = hnode_2_core(next_cnode);

	if (!ckrm_is_core_valid(next_childcore)) {
		printk(KERN_ERR "Invalid next child %p in ckrm_get_next_child\n",
				next_childcore);
		return NULL;
	}
	return next_childcore;
}

EXPORT_SYMBOL(ckrm_lock_hier);
EXPORT_SYMBOL(ckrm_unlock_hier);
EXPORT_SYMBOL(ckrm_get_next_child);

static void 
ckrm_alloc_res_class(struct ckrm_core_class *core,
		     struct ckrm_core_class *parent,
		     int resid)
{

	struct ckrm_classtype *clstype;

	/* 
	 * Allocate a resource class only if the resource controller has
	 * registered with core and the engine requests for the class.
	 */

	if (!ckrm_is_core_valid(core))
		return ; 

	clstype = core->classtype;
	core->res_class[resid] = NULL;

	if (test_bit(resid, &clstype->bit_res_ctlrs)) {
		ckrm_res_ctlr_t *rcbs;

		atomic_inc(&clstype->nr_resusers[resid]);
		rcbs = clstype->res_ctlrs[resid];
		
		if (rcbs && rcbs->res_alloc) {
			core->res_class[resid] =(*rcbs->res_alloc)(core,parent);
			if (core->res_class[resid])
				return;
			printk(KERN_ERR "Error creating res class\n");
		}
		atomic_dec(&clstype->nr_resusers[resid]);
	}
}

/*
 * Initialize a core class
 *
 */

int
ckrm_init_core_class(struct ckrm_classtype  *clstype,
		     struct ckrm_core_class *dcore,
		     struct ckrm_core_class *parent,
		     const char *name)
{
	// Hubertus   ... should replace name with dentry or add dentry ?
	int i;

	// Hubertus .. how is this used in initialization 

	printk("ckrm_init_core_class: name %s => %p\n", name?name:"default",dcore);
	
	if ((dcore != clstype->default_class) && ( !ckrm_is_core_valid(parent))) {
		printk("error not a valid parent %p\n", parent);
		return -EINVAL;
	}
#if 0  // Hubertus .. dynamic allocation still breaks when RCs registers. See def in ckrm_rc.h
	dcore->res_class = NULL;
	if (clstype->max_resid > 0) {
		dcore->res_class = (void**)kmalloc(clstype->max_resid * sizeof(void*) , GFP_KERNEL);
		if (dcore->res_class == NULL) {
			printk("error no mem\n");
			return -ENOMEM;
		}
	}
#endif

	dcore->classtype    = clstype;
	dcore->magic        = CKRM_CORE_MAGIC;
	dcore->name         = name;
	dcore->ckrm_lock    = SPIN_LOCK_UNLOCKED;
	dcore->hnode_rwlock = RW_LOCK_UNLOCKED;

	atomic_set(&dcore->refcnt, 0);
	write_lock(&ckrm_class_lock);

	INIT_LIST_HEAD(&dcore->objlist);
	list_add(&dcore->clslist,&clstype->classes);

	clstype->num_classes++;
	set_callbacks_active(clstype);

	write_unlock(&ckrm_class_lock);
	ckrm_add_child(parent, dcore); 

	for (i = 0; i < clstype->max_resid; i++) 
		ckrm_alloc_res_class(dcore,parent,i);

	// fix for race condition seen in stress with numtasks
	if (parent) 
		ckrm_core_grab(parent);

	ckrm_core_grab( dcore );
	return 0;
}


static void 
ckrm_free_res_class(struct ckrm_core_class *core, int resid)
{
	/* 
	 * Free a resource class only if the resource controller has
	 * registered with core 
	 */

	if (core->res_class[resid]) {
		ckrm_res_ctlr_t *rcbs;
		struct ckrm_classtype *clstype = core->classtype;

		atomic_inc(&clstype->nr_resusers[resid]);
		rcbs = clstype->res_ctlrs[resid];

		if (rcbs->res_free) {
			(*rcbs->res_free)(core->res_class[resid]);
			atomic_dec(&clstype->nr_resusers[resid]); // for inc in alloc
			core->res_class[resid] = NULL;	
		}
		atomic_dec(&clstype->nr_resusers[resid]);
	}
}


/*
 * Free a core class 
 *   requires that all tasks were previously reassigned to another class
 *
 * Returns 0 on success -errno on failure.
 */

void
ckrm_free_core_class(struct ckrm_core_class *core)
{
	int i;
	struct ckrm_classtype *clstype = core->classtype;
	struct ckrm_core_class *parent = core->hnode.parent;
	
	printk("%s: core=%p:%s parent=%p:%s\n",__FUNCTION__,core,core->name,parent,parent->name);
	if (core->magic == 0) {
		/* this core was marked as late */
		printk("class <%s> finally deleted %lu\n",core->name,jiffies);
	}
	if (ckrm_remove_child(core) == 0) {
		printk("Core class removal failed. Chilren present\n");
	}

	for (i = 0; i < clstype->max_resid; i++) {
		ckrm_free_res_class(core,i);
	}

	write_lock(&ckrm_class_lock);

	// Clear the magic, so we would know if this core is reused.
	core->magic = 0;
#if 0 // Dynamic not yet enabled
	core->res_class = NULL;
#endif
	// Remove this core class from its linked list.
	list_del(&core->clslist);
	clstype->num_classes--;
	set_callbacks_active(clstype);
	write_unlock(&ckrm_class_lock);

	// fix for race condition seen in stress with numtasks
	if (parent) 
		ckrm_core_drop(parent);
 
	kfree(core);
}

int
ckrm_release_core_class(struct ckrm_core_class *core)
{
	if (!ckrm_is_core_valid(core)) {
		// Invalid core
		return (-EINVAL);
	}

	if (core == core->classtype->default_class)
 		return 0;

	/* need to make sure that the classgot really dropped */
	if (atomic_read(&core->refcnt) != 1) {
		printk("class <%s> deletion delayed refcnt=%d jif=%ld\n",
		       core->name,core->refcnt,jiffies);
		core->magic = 0;  /* just so we have a ref point */
	}
	ckrm_core_drop(core);
	return 0;
}

/****************************************************************************
 *           Interfaces for the resource controller                         *
 ****************************************************************************/
/*
 * Registering a callback structure by the resource controller.
 *
 * Returns the resource id(0 or +ve) on success, -errno for failure.
 */
static int
ckrm_register_res_ctlr_intern(struct ckrm_classtype *clstype, ckrm_res_ctlr_t *rcbs)
{
	int  resid, ret,i;
	
	if (!rcbs)
		return -EINVAL;

	resid = rcbs->resid;
	
	spin_lock(&clstype->res_ctlrs_lock);
	
	printk(KERN_WARNING "resid is %d name is %s %s\n", 
	       resid, rcbs->res_name,clstype->res_ctlrs[resid]->res_name);

	if (resid >= 0) {
		if ((resid < CKRM_MAX_RES_CTLRS) && (clstype->res_ctlrs[resid] == NULL)) {
			clstype->res_ctlrs[resid] = rcbs;
			atomic_set(&clstype->nr_resusers[resid], 0);
			set_bit(resid, &clstype->bit_res_ctlrs);	
			ret = resid;
			if (resid >= clstype->max_resid) {
				clstype->max_resid = resid + 1;
			}
		} else {
			ret = -EBUSY;
		}
		spin_unlock(&clstype->res_ctlrs_lock);
		return ret;
	}

	for (i = clstype->resid_reserved; i < clstype->max_res_ctlrs; i++) {
		if (clstype->res_ctlrs[i] == NULL) {
			clstype->res_ctlrs[i] = rcbs;
			rcbs->resid = i;
			atomic_set(&clstype->nr_resusers[i], 0);
			set_bit(i, &clstype->bit_res_ctlrs);	
			if (i >= clstype->max_resid) {
				clstype->max_resid = i + 1;
			}
			spin_unlock(&clstype->res_ctlrs_lock);
			return i;
		}
	}
	
	spin_unlock(&clstype->res_ctlrs_lock);
	return (-ENOMEM);
}

int
ckrm_register_res_ctlr(struct ckrm_classtype *clstype, ckrm_res_ctlr_t *rcbs)
{
	struct ckrm_core_class *core;
	int resid;
	
	resid = ckrm_register_res_ctlr_intern(clstype,rcbs);
	
	if (resid >= 0) {
		/* run through all classes and create the resource class object and
		 * if necessary "initialize" class in context of this resource 
		 */
		read_lock(&ckrm_class_lock);
		list_for_each_entry(core, &clstype->classes, clslist) {
			printk("CKRM .. create res clsobj for resouce <%s> class <%s> par=%p\n", 
			       rcbs->res_name, core->name, core->hnode.parent);
			ckrm_alloc_res_class(core, core->hnode.parent, resid);
			if (clstype->add_resctrl)  // FIXME: this should be mandatory
				(*clstype->add_resctrl)(core,resid);
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
ckrm_unregister_res_ctlr(struct ckrm_res_ctlr *rcbs)
{	
	struct ckrm_classtype *clstype = rcbs->classtype;
	int resid = rcbs->resid;

	if ((clstype == NULL) || (resid < 0))
		return -EINVAL;
	
	if (atomic_read(&clstype->nr_resusers[resid]))
		return -EBUSY;
	
	// FIXME: probably need to also call deregistration function

	spin_lock(&clstype->res_ctlrs_lock);
	clstype->res_ctlrs[resid] = NULL;
	clear_bit(resid, &clstype->bit_res_ctlrs);	
	clstype->max_resid = fls(clstype->bit_res_ctlrs);
	rcbs->resid = -1;
	spin_unlock(&clstype->res_ctlrs_lock);
	
	return 0;
}

/*******************************************************************
 *   Class Type Registration
 *******************************************************************/

/* Hubertus ... we got to do some locking here */

struct ckrm_classtype* ckrm_classtypes[CKRM_MAX_CLASSTYPES];
EXPORT_SYMBOL(ckrm_classtypes);     // really should build a better interface for this

int
ckrm_register_classtype(struct ckrm_classtype *clstype)
{
	int tid = clstype->typeID;

	if (tid != -1) {
		if ((tid < 0) || (tid > CKRM_MAX_CLASSTYPES) || (ckrm_classtypes[tid]))
			return -EINVAL;
	} else {
		int i;
		for ( i=CKRM_RESV_CLASSTYPES ; i<CKRM_MAX_CLASSTYPES; i++) {
			if (ckrm_classtypes[i] == NULL) {
				tid = i;
				break;
			}
		}
	}
	if (tid == -1) 
		return -EBUSY;
	clstype->typeID = tid;
	ckrm_classtypes[tid] = clstype;
	
	/* Hubertus .. we need to call the callbacks of the RCFS client */
	if (rcfs_fn.register_classtype) {
		(* rcfs_fn.register_classtype)(clstype);
		// No error return for now ;
	}

	return tid;
}

int
ckrm_unregister_classtype(struct ckrm_classtype *clstype)
{
	int tid = clstype->typeID;

	if ((tid < 0) || (tid > CKRM_MAX_CLASSTYPES) || (ckrm_classtypes[tid] != clstype))
		return -EINVAL;

	if (rcfs_fn.deregister_classtype) {
		(* rcfs_fn.deregister_classtype)(clstype);
		// No error return for now
	}

	ckrm_classtypes[tid] = NULL;
	clstype->typeID = -1;
	return 0;
}

struct ckrm_classtype*
ckrm_find_classtype_by_name(const char *name)
{
	int i;
	for ( i=0 ; i<CKRM_MAX_CLASSTYPES; i++) {
		struct ckrm_classtype *ctype = ckrm_classtypes[i];
		if (ctype && !strncmp(ctype->name,name,CKRM_MAX_TYPENAME_LEN)) 
			return ctype;
	}
	return NULL;
}


/*******************************************************************
 *   Event callback invocation
 *******************************************************************/

struct ckrm_hook_cb* ckrm_event_callbacks[CKRM_NONLATCHABLE_EVENTS];

/* Registration / Deregistration / Invocation functions */

int
ckrm_register_event_cb(enum ckrm_event ev, struct ckrm_hook_cb *cb)
{
	struct ckrm_hook_cb **cbptr;

	if ((ev < CKRM_LATCHABLE_EVENTS) || (ev >= CKRM_NONLATCHABLE_EVENTS))
		return 1;
	cbptr = &ckrm_event_callbacks[ev];
	while (*cbptr != NULL) 
		cbptr = &((*cbptr)->next);
	*cbptr = cb;
	return 0;
}

int
ckrm_unregister_event_cb(enum ckrm_event ev, struct ckrm_hook_cb *cb)
{
	struct ckrm_hook_cb **cbptr;

	if ((ev < CKRM_LATCHABLE_EVENTS) || (ev >= CKRM_NONLATCHABLE_EVENTS))
		return -1;
	cbptr = &ckrm_event_callbacks[ev];
	while ((*cbptr != NULL) && (*cbptr != cb))
		cbptr = &((*cbptr)->next);
	if (*cbptr)
		(*cbptr)->next = cb->next;
	return (*cbptr == NULL);
}

int
ckrm_register_event_set(struct ckrm_event_spec especs[])
{
	struct ckrm_event_spec *espec = especs;

	for ( espec = especs ; espec->ev != -1 ; espec++ )
		ckrm_register_event_cb(espec->ev,&espec->cb);
	return 0;
}

int
ckrm_unregister_event_set(struct ckrm_event_spec especs[])
{
	struct ckrm_event_spec *espec = especs;

	for ( espec = especs ; espec->ev != -1 ; espec++ )
		ckrm_unregister_event_cb(espec->ev,&espec->cb);
	return 0;
}

#define ECC_PRINTK(fmt, args...) // printk("%s: " fmt, __FUNCTION__ , ## args)

void
ckrm_invoke_event_cb_chain(enum ckrm_event ev, void *arg)
{
	struct ckrm_hook_cb *cb, *anchor;

	ECC_PRINTK("%d %x\n",current,ev,arg);
	if ((anchor = ckrm_event_callbacks[ev]) != NULL) {
		for ( cb = anchor ; cb ; cb = cb->next ) 
			(*cb->fct)(arg);
	}
}

/*******************************************************************
 *   Generic Functions that can be used as default functions 
 *   in almost all classtypes
 *     (a) function iterator over all resource classes of a class
 *     (b) function invoker on a named resource
 *******************************************************************/

int                      
ckrm_class_show_shares(struct ckrm_core_class *core, struct seq_file *seq)
{
	int i;
	struct ckrm_res_ctlr *rcbs;
	struct ckrm_classtype *clstype = core->classtype;
	struct ckrm_shares shares;

	for (i = 0; i < clstype->max_resid; i++) {
		atomic_inc(&clstype->nr_resusers[i]);
		rcbs = clstype->res_ctlrs[i];
		if (rcbs && rcbs->get_share_values) {
			(*rcbs->get_share_values)(core->res_class[i], &shares);
			seq_printf(seq,"res=%s,guarantee=%d,limit=%d,total_guarantee=%d,max_limit=%d\n",
				   rcbs->res_name,
				   shares.my_guarantee,
				   shares.my_limit,
				   shares.total_guarantee,
				   shares.max_limit);
		}
		atomic_dec(&clstype->nr_resusers[i]);
	}
	return 0;
}

int                      
ckrm_class_show_stats(struct ckrm_core_class *core, struct seq_file *seq)
{
	int i;
	struct ckrm_res_ctlr *rcbs;
	struct ckrm_classtype *clstype = core->classtype;

	for (i = 0; i < clstype->max_resid; i++) {
		atomic_inc(&clstype->nr_resusers[i]);
		rcbs = clstype->res_ctlrs[i];
		if (rcbs && rcbs->get_stats) 
			(*rcbs->get_stats)(core->res_class[i], seq);
		atomic_dec(&clstype->nr_resusers[i]);
	}
	return 0;
}

int                      
ckrm_class_show_config(struct ckrm_core_class *core, struct seq_file *seq)
{
	int i;
	struct ckrm_res_ctlr *rcbs;
	struct ckrm_classtype *clstype = core->classtype;

	for (i = 0; i < clstype->max_resid; i++) {
		atomic_inc(&clstype->nr_resusers[i]);
		rcbs = clstype->res_ctlrs[i];
		if (rcbs && rcbs->show_config) 
			(*rcbs->show_config)(core->res_class[i], seq);
		atomic_dec(&clstype->nr_resusers[i]);
	}
	return 0;
}

int
ckrm_class_set_config(struct ckrm_core_class *core, const char *resname, const char *cfgstr)
{
	struct ckrm_classtype *clstype = core->classtype;
	struct ckrm_res_ctlr *rcbs = ckrm_resctlr_lookup(clstype,resname);
	int rc;

	if (rcbs == NULL || rcbs->set_config == NULL)
		return -EINVAL; 
	rc = (*rcbs->set_config)(core->res_class[rcbs->resid],cfgstr);
	return rc;
}

int
ckrm_class_set_shares(struct ckrm_core_class *core, const char *resname,
		      struct ckrm_shares *shares)
{
	struct ckrm_classtype *clstype = core->classtype;
	struct ckrm_res_ctlr *rcbs;
	int rc;

	printk("ckrm_class_set_shares(%s,%s)\n",core->name,resname);
	rcbs = ckrm_resctlr_lookup(clstype,resname);
	if (rcbs == NULL || rcbs->set_share_values == NULL)
		return -EINVAL; 
	rc = (*rcbs->set_share_values)(core->res_class[rcbs->resid],shares);
	return rc;
}

int 
ckrm_class_reset_stats(struct ckrm_core_class *core, const char *resname, const char *unused)
{
	struct ckrm_classtype *clstype = core->classtype;
	struct ckrm_res_ctlr *rcbs = ckrm_resctlr_lookup(clstype,resname);
	int rc;

	if (rcbs == NULL || rcbs->reset_stats == NULL)
		return -EINVAL; 
	rc = (*rcbs->reset_stats)(core->res_class[rcbs->resid]);
	return rc;
}	

/*******************************************************************
 *   Initialization 
 *******************************************************************/

void
ckrm_cb_newtask(struct task_struct *tsk)
{
	tsk->ce_data   = NULL;
	spin_lock_init(&tsk->ckrm_tsklock);
	ckrm_invoke_event_cb_chain(CKRM_EVENT_NEWTASK,tsk);
}

void 
ckrm_cb_exit(struct task_struct *tsk)
{
	ckrm_invoke_event_cb_chain(CKRM_EVENT_EXIT,tsk);
	tsk->ce_data = NULL;
}

void __init
ckrm_init(void) 
{
	printk("CKRM Initialization\n");
	
	// register/initialize the Metatypes
	
#ifdef CONFIG_CKRM_TYPE_TASKCLASS
	{ 
		extern void ckrm_meta_init_taskclass(void);
		ckrm_meta_init_taskclass();
	}
#endif
#ifdef CONFIG_CKRM_TYPE_SOCKETCLASS
	{ 
		extern void ckrm_meta_init_sockclass(void);
		ckrm_meta_init_sockclass();
	}
#endif
	// prepare init_task and then rely on inheritance of properties
	ckrm_cb_newtask(&init_task);
	printk("CKRM Initialization done\n");
}


EXPORT_SYMBOL(ckrm_register_engine);
EXPORT_SYMBOL(ckrm_unregister_engine);

EXPORT_SYMBOL(ckrm_register_res_ctlr);
EXPORT_SYMBOL(ckrm_unregister_res_ctlr);

EXPORT_SYMBOL(ckrm_init_core_class);
EXPORT_SYMBOL(ckrm_free_core_class);
EXPORT_SYMBOL(ckrm_release_core_class);

EXPORT_SYMBOL(ckrm_register_classtype);
EXPORT_SYMBOL(ckrm_unregister_classtype);
EXPORT_SYMBOL(ckrm_find_classtype_by_name);

EXPORT_SYMBOL(ckrm_core_grab);
EXPORT_SYMBOL(ckrm_core_drop);
EXPORT_SYMBOL(ckrm_is_core_valid);
EXPORT_SYMBOL(ckrm_validate_and_grab_core);

EXPORT_SYMBOL(ckrm_register_event_set);
EXPORT_SYMBOL(ckrm_unregister_event_set);
EXPORT_SYMBOL(ckrm_register_event_cb);
EXPORT_SYMBOL(ckrm_unregister_event_cb);

EXPORT_SYMBOL(ckrm_class_show_stats);
EXPORT_SYMBOL(ckrm_class_show_config);
EXPORT_SYMBOL(ckrm_class_show_shares);

EXPORT_SYMBOL(ckrm_class_set_config);
EXPORT_SYMBOL(ckrm_class_set_shares);

EXPORT_SYMBOL(ckrm_class_reset_stats);
