/* ckrm_res.h - Header file to be used by Resource controllers of CKRM
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

#ifndef _LINUX_CKRM_RES_H
#define _LINUX_CKRM_RES_H

#ifdef __KERNEL__

#ifdef CONFIG_CKRM

#include <linux/list.h>
#include <linux/ckrm.h>
#include <linux/seq_file.h>

// Class types
enum {
	CKRM_TASK_CLASS=1,
	CKRM_NET_CLASS,
};

// Modes of CKRM   Hubertus... Mode can disappear ? no implicitely through DONTCARE'S
#define CKRM_MONITOR_MODE		1
#define CKRM_MANAGE_MODE		2

extern int ckrm_mode;   /* are we in monitor or in managed mode */

// predefined constants
#define CKRM_MAX_RES_CTLRS 32
#define CKRM_MAX_RES_NAME		128

// Predefined macros for known kernel resources
enum resid {
	CKRM_RES_DUMMY=0,
	CKRM_RES_CPU,
	CKRM_RES_MEM,
	CKRM_RES_IO,
	CKRM_RES_SAQ,
	CKRM_RES_MAX_RSVD,
};

#define CKRM_CORE_MAGIC		0xBADCAFFE

// Share specifications

typedef struct ckrm_shares {
	int my_guarantee;
	int my_limit;
	int total_guarantee;
	int total_limit;
	int unused_guarantee;  // not used as parameters
	int unused_limit;      // not used as parameters
} ckrm_shares_t;

#define CKRM_SHARE_UNCHANGED     (-1)  // value to indicate no change
#define CKRM_SHARE_DONTCARE      (-2)  // value to indicate don't care.
#define CKRM_SHARE_DFLT_TOTAL_GUARANTEE (100) // Start off with these values
#define CKRM_SHARE_DFLT_TOTAL_LIMIT     (100) // to simplify set_res_shares logic

/* basic definition of a hierarchy that is to be used by the the CORE classes
 * and can be used by the resource class objects
 */

struct ckrm_hnode {
	struct ckrm_hnode *parent;
	struct list_head   siblings; /* anchor for sibling list */
	struct list_head   children; /* anchor for children     */
};

/* core class definition */
	
typedef struct ckrm_core_class {
	struct list_head tasklist; // list of tasks in this core class; anchor
	int class_type;			// task class or otherwise
  	spinlock_t ckrm_lock;           // to protect the list and the array above
	struct list_head clslist;       // to link up all classes in a single list
	struct dentry  *dentry;           // dentry of inode in the RCFS
	int magic;
	void *res_class[CKRM_MAX_RES_CTLRS]; // per registered resource
	struct ckrm_hnode  hnode;    // hierarchy
} ckrm_core_class_t;

#define ckrm_get_res_class(rescls,resid,type)   ((type*)((rescls)->res_class[resid]))

/* resource controller callback structure */

typedef struct ckrm_res_callback {
	char res_name[CKRM_MAX_RES_NAME];
	int  res_hdepth;	// maximum hierarchy
	int  res_type;	        // class type to which resource belongs
	int  resid;		// (for now) same as the enum resid

	/* allocate/free new resource class object for resource controller */
	void * (*res_alloc)  (struct ckrm_core_class *core, 
					struct ckrm_core_class *parent);
	void (*res_free)     (void *);
	/* reinitialize existing resource class object */
	void (*res_initcls)  (void *);

	/* set/get limits/guarantees for a resource controller class */
	int  (*set_share_values)    (void *, struct ckrm_shares *);
	int  (*get_share_values)    (void *, struct ckrm_shares *);

	/* statistics access */
	int  (*get_stats)    (void *, struct seq_file *s);

	void (*change_resclass)(struct task_struct *, void *, void *);
} ckrm_res_callback_t;

extern int ckrm_register_res_ctlr(ckrm_res_callback_t *);
extern int ckrm_unregister_res_ctlr(int);

extern inline unsigned int is_core_valid(ckrm_core_class_t *core);
extern inline unsigned int is_res_regd(int resid);
extern inline int ckrm_resid_lookup (char *resname);

#define for_each_resid(rid) \
	for (rid=0; rid < CKRM_MAX_RES_CTLRS; rid++) 


/* interface to the RCFS filesystem */

extern struct ckrm_core_class ckrm_dflt_class;
extern struct ckrm_core_class ckrm_net_root;

extern struct ckrm_core_class *ckrm_alloc_core_class(struct ckrm_core_class *parent, struct dentry *dentry);
extern int ckrm_free_core_class(struct ckrm_core_class *cls);

// Reclassify the given task to the given core class.
extern void ckrm_reclassify_task(struct task_struct *, struct ckrm_core_class *);

// Reclassify the given task to the given core class by force
extern void ckrm_forced_reclassify_task(struct task_struct *, struct ckrm_core_class *);

extern void ckrm_hnode_add(struct ckrm_hnode *node,struct ckrm_hnode *parent );
extern int  ckrm_hnode_remove(struct ckrm_hnode *node);


#endif // CONFIG_CKRM

#endif // __KERNEL__

#endif // _LINUX_CKRM_RES_H


