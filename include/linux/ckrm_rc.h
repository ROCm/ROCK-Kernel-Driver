/* ckrm_rc.h - Header file to be used by Resource controllers of CKRM
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 *           (C) Shailabh Nagar,  IBM Corp. 2003
 *           (C) Chandra Seetharaman, IBM Corp. 2003
 *	     (C) Vivek Kashyap , IBM Corp. 2004
 * 
 * Provides data structures, macros and kernel API of CKRM for 
 * resource controllers.
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
 * 12 Nov 2003
 *        Created.
 */

#ifndef _LINUX_CKRM_RC_H
#define _LINUX_CKRM_RC_H

#ifdef __KERNEL__

#ifdef CONFIG_CKRM

#include <linux/list.h>
#include <linux/ckrm.h>
#include <linux/ckrm_ce.h>    
#include <linux/seq_file.h>


/* maximum number of class types */
#define CKRM_MAX_CLASSTYPES         32       
/* maximum classtype name length */
#define CKRM_MAX_CLASSTYPE_NAME     32       

/* maximum resource controllers per classtype */
#define CKRM_MAX_RES_CTLRS           8     
/* maximum resource controller name length */
#define CKRM_MAX_RES_NAME          128       


struct ckrm_core_class;
struct ckrm_classtype;

/********************************************************************************
 * Share specifications
 *******************************************************************************/

typedef struct ckrm_shares {
	int my_guarantee;
	int my_limit;
	int total_guarantee;
	int max_limit;
	int unused_guarantee;  // not used as parameters
	int cur_max_limit;     // not used as parameters
} ckrm_shares_t;

#define CKRM_SHARE_UNCHANGED     (-1)  // value to indicate no change
#define CKRM_SHARE_DONTCARE      (-2)  // value to indicate don't care.
#define CKRM_SHARE_DFLT_TOTAL_GUARANTEE (100) // Start off with these values
#define CKRM_SHARE_DFLT_MAX_LIMIT     (100) // to simplify set_res_shares logic


/********************************************************************************
 * RESOURCE CONTROLLERS
 *******************************************************************************/

/* resource controller callback structure */

typedef struct ckrm_res_ctlr {
	char res_name[CKRM_MAX_RES_NAME];
	int  res_hdepth;	          // maximum hierarchy
	int  resid;         	          // (for now) same as the enum resid
	struct ckrm_classtype *classtype; // classtype owning this resource controller

	/* allocate/free new resource class object for resource controller */
	void *(*res_alloc)  (struct ckrm_core_class *this, struct ckrm_core_class *parent);
	void  (*res_free)   (void *);

	/* set/get limits/guarantees for a resource controller class */
	int  (*set_share_values) (void* , struct ckrm_shares *shares);
	int  (*get_share_values) (void* , struct ckrm_shares *shares);

	/* statistics and configuration access */
	int  (*get_stats)    (void* , struct seq_file *);
	int  (*reset_stats)  (void *);
	int  (*show_config)  (void* , struct seq_file *);
	int  (*set_config)   (void* , const char *cfgstr);

	void (*change_resclass)(void *, void *, void *);

} ckrm_res_ctlr_t;

/***************************************************************************************
 * CKRM_CLASSTYPE
 *
 *   A <struct ckrm_classtype> object describes a dimension for CKRM to classify 
 *   along. I needs to provide methods to create and manipulate class objects in
 *   this dimension
 ***************************************************************************************/

/* list of predefined class types, we always recognize */
#define CKRM_CLASSTYPE_TASK_CLASS    0
#define CKRM_CLASSTYPE_SOCKET_CLASS 1
#define CKRM_RESV_CLASSTYPES         2  /* always +1 of last known type */

#define CKRM_MAX_TYPENAME_LEN       32


typedef struct ckrm_classtype {
	/* Hubertus:   Rearrange slots so that they are more cache friendly during access */

	/* resource controllers */
	spinlock_t        res_ctlrs_lock;        /* protect data below (other than atomics) */
	int               max_res_ctlrs;         /* maximum number of resource controller allowed */
	int               max_resid;             /* maximum resid used                      */
	int               resid_reserved;        /* maximum number of reserved controllers  */
	long              bit_res_ctlrs;         /* bitmap of resource ID used              */
	atomic_t          nr_resusers[CKRM_MAX_RES_CTLRS];
	ckrm_res_ctlr_t*  res_ctlrs[CKRM_MAX_RES_CTLRS];

	/* state about my classes */

	struct ckrm_core_class   *default_class; // pointer to default class
	struct list_head          classes;       // listhead to link up all classes of this classtype
	int                       num_classes;    // how many classes do exist

	/* state about my ce interaction */
	int                       ce_regd;       // Has a CE been registered for this classtype
	int                       ce_cb_active;  // are callbacks active
	atomic_t                  ce_nr_users;   // how many transient calls active
	struct ckrm_eng_callback  ce_callbacks;  // callback engine

 	// Begin classtype-rcfs private data. No rcfs/fs specific types used. 
 	int               mfidx;             // Index into genmfdesc array used to initialize
 	                                     // mfdesc and mfcount 
 	void              *mfdesc;           // Array of descriptors of root and magic files
 	int               mfcount;           // length of above array 
 	void              *rootde;           // root dentry created by rcfs
 	// End rcfs private data 

	char name[CKRM_MAX_TYPENAME_LEN];    // currently same as mfdesc[0]->name but could be different
 	int  typeID;			       /* unique TypeID                         */
	int  maxdepth;                         /* maximum depth supported               */

	/* functions to be called on any class type by external API's */
	struct ckrm_core_class*  (*alloc)(struct ckrm_core_class *parent, const char *name);   /* alloc class instance */
	int                      (*free) (struct ckrm_core_class *cls);                        /* free  class instance */
	
	int                      (*show_members)(struct ckrm_core_class *, struct seq_file *);
	int                      (*show_stats)  (struct ckrm_core_class *, struct seq_file *);
	int                      (*show_config) (struct ckrm_core_class *, struct seq_file *);
	int                      (*show_shares) (struct ckrm_core_class *, struct seq_file *);

	int                      (*reset_stats) (struct ckrm_core_class *, const char *resname, 
						 const char *);
	int                      (*set_config)  (struct ckrm_core_class *, const char *resname,
						 const char *cfgstr);
	int                      (*set_shares)  (struct ckrm_core_class *, const char *resname,
						 struct ckrm_shares *shares);
	int                      (*forced_reclassify)(struct ckrm_core_class *, const char *);

  
	/* functions to be called on a class type by ckrm internals */
	void                     (*add_resctrl)(struct ckrm_core_class *, int resid);     // class initialization for new RC
 
} ckrm_classtype_t;

/******************************************************************************************
 * CKRM CORE CLASS
 *      common part to any class structure (i.e. instance of a classtype)
 ******************************************************************************************/

/* basic definition of a hierarchy that is to be used by the the CORE classes
 * and can be used by the resource class objects
 */

#define CKRM_CORE_MAGIC		0xBADCAFFE

typedef struct ckrm_hnode {
        struct ckrm_core_class *parent;
	struct list_head   siblings; /* linked list of siblings */
	struct list_head   children; /* anchor for children     */
} ckrm_hnode_t;

typedef struct ckrm_core_class {
	struct ckrm_classtype *classtype; // what type does this core class belong to
        void* res_class[CKRM_MAX_RES_CTLRS];                 // pointer to array of resource classes
  	spinlock_t class_lock;             // to protect the list and the array above
	struct list_head objlist;         // generic list for any object list to be maintained by class
	struct list_head clslist;         // to link up all classes in a single list type wrt to type
	struct dentry  *dentry;           // dentry of inode in the RCFS
	int magic;
	struct ckrm_hnode  hnode;    // hierarchy
	rwlock_t hnode_rwlock; // rw_clock protecting the hnode above.
	atomic_t refcnt;
	const char *name;
	int delayed;                      // core deletion delayed because of race conditions
} ckrm_core_class_t;

/* type coerce between derived class types and ckrm core class type */
#define class_type(type,coreptr)   container_of(coreptr,type,core)
#define class_core(clsptr)         (&(clsptr)->core)
/* locking classes */
#define class_lock(coreptr)        spin_lock(&(coreptr)->class_lock)
#define class_unlock(coreptr)      spin_unlock(&(coreptr)->class_lock)
/* what type is a class of ISA */
#define class_isa(clsptr)          (class_core(clsptr)->classtype)


/******************************************************************************************
 * OTHER
 ******************************************************************************************/

#define ckrm_get_res_class(rescls,resid,type)   ((type*)((rescls)->res_class[resid]))

extern int ckrm_register_res_ctlr   (struct ckrm_classtype *, ckrm_res_ctlr_t *);
extern int ckrm_unregister_res_ctlr (ckrm_res_ctlr_t *);

extern int ckrm_validate_and_grab_core(struct ckrm_core_class *core);
extern int ckrm_init_core_class(struct ckrm_classtype  *clstype,struct ckrm_core_class *dcore,
				struct ckrm_core_class *parent, const char *name);
extern int ckrm_release_core_class(struct ckrm_core_class *);   // Hubertus .. can disappear after cls del debugging
extern struct ckrm_res_ctlr *ckrm_resctlr_lookup(struct ckrm_classtype *type, const char *resname);

#if 0

// Hubertus ... need to straighten out all these I don't think we will even call thsie ore are we 

/* interface to the RCFS filesystem */
extern struct ckrm_core_class *ckrm_alloc_core_class(struct ckrm_core_class *, const char *, int);

// Reclassify the given pid to the given core class by force
extern void ckrm_forced_reclassify_pid(int, struct ckrm_core_class *);

// Reclassify the given net_struct  to the given core class by force
extern void ckrm_forced_reclassify_laq(struct ckrm_net_struct *, 
		struct ckrm_core_class *);

#endif

extern void ckrm_lock_hier(struct ckrm_core_class *);
extern void ckrm_unlock_hier(struct ckrm_core_class *);
extern struct ckrm_core_class * ckrm_get_next_child(struct ckrm_core_class *,
		            struct ckrm_core_class *);

extern void child_guarantee_changed(struct ckrm_shares *, int, int);
extern void child_maxlimit_changed(struct ckrm_shares *, int);
extern int  set_shares(struct ckrm_shares *, struct ckrm_shares *, struct ckrm_shares *);

/* classtype registration and lookup */
extern int ckrm_register_classtype  (struct ckrm_classtype *clstype);
extern int ckrm_unregister_classtype(struct ckrm_classtype *clstype);
extern struct ckrm_classtype* ckrm_find_classtype_by_name(const char *name);

/* default functions that can be used in classtypes's function table */
extern int ckrm_class_show_shares(struct ckrm_core_class *core, struct seq_file *seq);
extern int ckrm_class_show_stats(struct ckrm_core_class *core, struct seq_file *seq);
extern int ckrm_class_show_config(struct ckrm_core_class *core, struct seq_file *seq);
extern int ckrm_class_set_config(struct ckrm_core_class *core, const char *resname, const char *cfgstr);
extern int ckrm_class_set_shares(struct ckrm_core_class *core, const char *resname, struct ckrm_shares *shares);
extern int ckrm_class_reset_stats(struct ckrm_core_class *core, const char *resname, const char *unused);

#if 0
extern void ckrm_ns_hold(struct ckrm_net_struct *);
extern void ckrm_ns_put(struct ckrm_net_struct *);
extern void *ckrm_set_rootcore_byname(char *, void *);
#endif

static inline void ckrm_core_grab(struct ckrm_core_class *core)  
{ 
	if (core) atomic_inc(&core->refcnt);
}

static inline void ckrm_core_drop(struct ckrm_core_class *core) 
{ 
	// only make definition available in this context
	extern void ckrm_free_core_class(struct ckrm_core_class *core);   
	if (core && (atomic_dec_and_test(&core->refcnt)))
	    ckrm_free_core_class(core);
}

static inline unsigned int
ckrm_is_core_valid(ckrm_core_class_t *core)
{
	return (core && (core->magic == CKRM_CORE_MAGIC));
}

// iterate through all associate resource controllers:
// requires following arguments (ckrm_core_class *cls, 
//                               ckrm_res_ctrl   *ctlr,
//                               void            *robj,
//                               int              bmap)
#define forall_class_resobjs(cls,rcbs,robj,bmap)									\
       for ( bmap=((cls->classtype)->bit_res_ctlrs) ;									\
	     ({ int rid; ((rid=ffs(bmap)-1) >= 0) && 									\
	                 (bmap&=~(1<<rid),((rcbs=cls->classtype->res_ctlrs[rid]) && (robj=cls->res_class[rid]))); }) ;	\
           )

extern struct ckrm_classtype* ckrm_classtypes[]; /* should provide a different interface */


/*-----------------------------------------------------------------------------
 * CKRM event callback specification for the classtypes or resource controllers 
 *   typically an array is specified using CKRM_EVENT_SPEC terminated with 
 *   CKRM_EVENT_SPEC_LAST and then that array is registered using
 *   ckrm_register_event_set.
 *   Individual registration of event_cb is also possible
 *-----------------------------------------------------------------------------*/

struct ckrm_event_spec {
	enum ckrm_event     ev;
	struct ckrm_hook_cb cb;
};
#define CKRM_EVENT_SPEC(EV,FCT) { CKRM_EVENT_##EV, { (ckrm_event_cb)FCT, NULL } }

int ckrm_register_event_set(struct ckrm_event_spec especs[]);
int ckrm_unregister_event_set(struct ckrm_event_spec especs[]);
int ckrm_register_event_cb(enum ckrm_event ev, struct ckrm_hook_cb *cb);
int ckrm_unregister_event_cb(enum ckrm_event ev, struct ckrm_hook_cb *cb);

/******************************************************************************************
 * CE Invocation interface
 ******************************************************************************************/

#define ce_protect(ctype)      (atomic_inc(&((ctype)->ce_nr_users)))
#define ce_release(ctype)      (atomic_dec(&((ctype)->ce_nr_users)))

// CE Classification callbacks with 

#define CE_CLASSIFY_NORET(ctype, event, objs_to_classify...)					\
do {												\
	if ((ctype)->ce_cb_active && (test_bit(event,&(ctype)->ce_callbacks.c_interest)))	\
		(*(ctype)->ce_callbacks.classify)(event, objs_to_classify);			\
} while (0)

#define CE_CLASSIFY_RET(ret, ctype, event, objs_to_classify...)					\
do {												\
	if ((ctype)->ce_cb_active && (test_bit(event,&(ctype)->ce_callbacks.c_interest)))	\
		ret = (*(ctype)->ce_callbacks.classify)(event, objs_to_classify);		\
} while (0)

#define CE_NOTIFY(ctype, event, cls, objs_to_classify)						\
do {												\
	if ((ctype)->ce_cb_active && (test_bit(event,&(ctype)->ce_callbacks.n_interest)))	\
		(*(ctype)->ce_callbacks.notify)(event,cls,objs_to_classify);			\
} while (0)


#endif // CONFIG_CKRM

#endif // __KERNEL__

#endif // _LINUX_CKRM_RC_H





