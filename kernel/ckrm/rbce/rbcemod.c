/* Rule-based Classification Engine (RBCE) module
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 *           (C) Chandra Seetharaman, IBM Corp. 2003
 *           (C) Vivek Kashyap, IBM Corp. 2004 
 * 
 * Module for loading of classification policies and providing
 * a user API for Class-based Kernel Resource Management (CKRM)
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
 *        Created. First cut with much scope for cleanup !
 * 07 Nov 2003
 *        Made modifications to suit the new RBCE module.
 *        Made modifications to address sampling and delivery
 * 16 Mar 2004
 *        Integrated changes from original RBCE module
 * 25 Mar 2004
 *        Merged RBCE and CRBCE into common code base
 * 29 Mar 2004
 * 	  Incorporated listen call back and IPv4 match support
 * 23 Apr 2004
 *        Added Multi-Classtype Support
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/proc_fs.h>
#include <linux/limits.h>
#include <linux/pid.h>
#include <linux/sysctl.h>

#include <linux/ckrm_rc.h>
#include <linux/ckrm_ce.h>
#include <linux/ckrm_net.h>
#include "bitvector.h"
#include "rbce.h"

#define DEBUG

MODULE_DESCRIPTION(RBCE_MOD_DESCR);
MODULE_AUTHOR("Hubertus Franke, Chandra Seetharaman (IBM)");
MODULE_LICENSE("GPL");    

static char modname[] = RBCE_MOD_NAME;

/* ==================== typedef, global variables etc., ==================== */
struct named_obj_hdr {
	struct list_head link;
	int    referenced;
	char   *name;
};

#define GET_REF(x) ((x)->obj.referenced)
#define INC_REF(x) (GET_REF(x)++)
#define DEC_REF(x) (--GET_REF(x))
struct rbce_class {
	struct named_obj_hdr obj;
	int classtype;
	void  *classobj;
};

typedef enum {
	RBCE_RULE_CMD_PATH = 1,		// full qualified path
	RBCE_RULE_CMD,				// basename of the command
	RBCE_RULE_ARGS,				// arguments of the command
	RBCE_RULE_REAL_UID,			// task's real uid
	RBCE_RULE_REAL_GID,			// task's real gid
	RBCE_RULE_EFFECTIVE_UID,	// task's effective uid
	RBCE_RULE_EFFECTIVE_GID,	// task's effective gid
	RBCE_RULE_APP_TAG,			// task's application tag
	RBCE_RULE_IPV4,				// IP address of listen(), ipv4 format
	RBCE_RULE_IPV6,				// IP address of listen(), ipv6 format
	RBCE_RULE_DEP_RULE,			// dependent rule; must be the first term
	RBCE_RULE_INVALID,			// invalid, for filler
	RBCE_RULE_INVALID2,			// invalid, for filler
} rbce_rule_op_t;

typedef enum {
	RBCE_EQUAL = 1,
	RBCE_NOT,
	RBCE_LESS_THAN,
	RBCE_GREATER_THAN,
} rbce_operator_t;

struct rbce_rule_term {
	rbce_rule_op_t op;
	rbce_operator_t operator;
	union {
		char *string; // path, cmd, arg, tag, ipv4 and ipv6
		long id; // uid, gid, euid, egid
		struct rbce_rule *deprule;
	} u;
};

struct rbce_rule {
	struct named_obj_hdr obj;
	struct rbce_class *target_class;
	int classtype;
	int num_terms;
	int *terms;    // vector of indices into the global term vector
	int index;     // index of this rule into the global term vector
	int termflag;  // which term ids would require a recalculation
	int do_opt;    // do we have to consider this rule during optimize
	char *strtab;  // string table to store the strings of all terms
	int order;     // order of execution of this rule
	int state;     // enabled/disabled ? RBCE_RULE_ENABLED/RBCE_RULE_DISABLED
};

// rules states
#define RBCE_RULE_DISABLED 0
#define RBCE_RULE_ENABLED  1

///
// Data structures and macros used for optimization
#define RBCE_TERM_CMD   (0)
#define RBCE_TERM_UID   (1)
#define RBCE_TERM_GID   (2)
#define RBCE_TERM_TAG   (3)
#define RBCE_TERM_IPV4  (4)
#define RBCE_TERM_IPV6  (5)

#define NUM_TERM_MASK_VECTOR  (6)

// Rule flags. 1 bit for each type of rule term
#define RBCE_TERMFLAG_CMD   (1 << RBCE_TERM_CMD)
#define RBCE_TERMFLAG_UID   (1 << RBCE_TERM_UID)
#define RBCE_TERMFLAG_GID   (1 << RBCE_TERM_GID)
#define RBCE_TERMFLAG_TAG   (1 << RBCE_TERM_TAG)
#define RBCE_TERMFLAG_IPV4  (1 << RBCE_TERM_IPV4)
#define RBCE_TERMFLAG_IPV6  (1 << RBCE_TERM_IPV6)
#define RBCE_TERMFLAG_ALL (RBCE_TERMFLAG_CMD | RBCE_TERMFLAG_UID | \
							RBCE_TERMFLAG_GID | RBCE_TERMFLAG_TAG | \
							RBCE_TERMFLAG_IPV4 | RBCE_TERMFLAG_IPV6)

int termop_2_vecidx[RBCE_RULE_INVALID] = {
	[ RBCE_RULE_CMD_PATH ]		= RBCE_TERM_CMD,
	[ RBCE_RULE_CMD      ]		= RBCE_TERM_CMD,
	[ RBCE_RULE_ARGS     ]		= RBCE_TERM_CMD,
	[ RBCE_RULE_REAL_UID ]		= RBCE_TERM_UID,
	[ RBCE_RULE_REAL_GID ]		= RBCE_TERM_GID,
	[ RBCE_RULE_EFFECTIVE_UID ]	= RBCE_TERM_UID,
	[ RBCE_RULE_EFFECTIVE_GID ] = RBCE_TERM_GID,
	[ RBCE_RULE_APP_TAG  ]		= RBCE_TERM_TAG,
	[ RBCE_RULE_IPV4     ]		= RBCE_TERM_IPV4,
	[ RBCE_RULE_IPV6     ]		= RBCE_TERM_IPV6,
	[ RBCE_RULE_DEP_RULE ]		= -1
};

#define TERMOP_2_TERMFLAG(x)	(1 << termop_2_vecidx[x])
#define TERM_2_TERMFLAG(x)		(1 << x)

#define POLICY_INC_NUMTERMS	(BITS_PER_LONG)	// No. of terms added at a time

#define POLICY_ACTION_NEW_VERSION	0x01 // Force reallocation
#define POLICY_ACTION_REDO_ALL		0x02 // Recompute all rule flags
#define POLICY_ACTION_PACK_TERMS	0x04 // Time to pack the terms

struct ckrm_eng_callback ckrm_ecbs;

// Term vector state
//
static int gl_bitmap_version, gl_action, gl_num_terms;
static int gl_allocated, gl_released;
struct rbce_rule_term *gl_terms;
bitvector_t *gl_mask_vecs[NUM_TERM_MASK_VECTOR];

extern int errno;
static void optimize_policy(void);

#ifndef CKRM_MAX_CLASSTYPES 
#define CKRM_MAX_CLASSTYPES 32
#endif

struct list_head rules_list[CKRM_MAX_CLASSTYPES]; 
LIST_HEAD(class_list); // List of classes used

static int gl_num_rules;
static int gl_rules_version;
int rbce_enabled = 1;
static rwlock_t global_rwlock = RW_LOCK_UNLOCKED;
	/*
	 * One lock to protect them all !!!
	 * Additions, deletions to rules must
	 * happen with this lock being held in write mode.
	 * Access(read/write) to any of the data structures must happen 
	 * with this lock held in read mode.
	 * Since, rule related changes do not happen very often it is ok to
	 * have single rwlock.
	 */

/*
 * data structure rbce_private_data holds the bit vector 'eval' which 
 * specifies if rules and terms of rules are evaluated against the task
 * and if they were evaluated, bit vector 'true' holds the result of that
 * evaluation.
 *
 * This data structure is maintained in a task, and the bitvectors are
 * updated only when needed.
 *
 * Each rule and each term of a rule has a corresponding bit in the vector.
 *
 */
struct rbce_private_data {
	struct rbce_ext_private_data ext_data; 
	int evaluate;  // whether to evaluate rules or not ?
	int rules_version; // whether to evaluate rules or not ?
	char *app_tag;
	unsigned long bitmap_version;
	bitvector_t *eval;
	bitvector_t *true;
	char data[0]; // eval will point into this variable size data array
};

#define RBCE_DATA(tsk) ((struct rbce_private_data*)((tsk)->ce_data))

/* ======================= DEBUG  Functions ========================= */

#ifdef DEBUG

int rbcedebug = 0x00;

#define DBG_CLASSIFY_RES     ( 0x01 )
#define DBG_CLASSIFY_DETAILS ( 0x02 )
#define DBG_OPTIMIZATION     ( 0x04 )
#define DBG_SHOW_RESCTL      ( 0x08 )
#define DBG_CLASS            ( 0x10 )
#define DBG_RULE             ( 0x20 )
#define DBG_POLICY           ( 0x40 )

#define DPRINTK(x, y...)   if (rbcedebug & (x)) printk(y)
	// debugging selectively enabled through /proc/sys/debug/rbce

static void
print_context_vectors(void)
{
	int i;
	
	if ((rbcedebug & DBG_OPTIMIZATION) == 0) {
		return;
	}
	for (i = 0; i < NUM_TERM_MASK_VECTOR; i++) {
		printk("%d: ",i);
		bitvector_print(DBG_OPTIMIZATION, gl_mask_vecs[i]);
		printk("\n");
	}
}
#else

#define DPRINTK(x, y...)
#define print_context_vectors(x)
#endif

/* ======================= Helper Functions ========================= */

#include "token.c"

static struct ckrm_core_class *rbce_classify(struct task_struct *, struct ckrm_net_struct *, unsigned long, int classtype);

static inline struct rbce_rule *
find_rule_name(const char *name)
{
	struct named_obj_hdr *pos;
	int i;

	for (i = 0; i < CKRM_MAX_CLASSTYPES; i++) {
		list_for_each_entry(pos, &rules_list[i], link) {
			if (!strcmp(pos->name, name)) {
				return ((struct rbce_rule *) pos );
			}
		}
	}
	return NULL;
}

static inline struct rbce_class *
find_class_name(const char *name)
{
	struct named_obj_hdr *pos;

	list_for_each_entry(pos, &class_list, link) {
		if (!strcmp(pos->name, name)) 
			return (struct rbce_class *) pos;
	}
	return NULL;
}

/*
 * Insert the given rule at the specified order
 * 		order = -1 ==> insert at the tail.
 *
 * Caller must hold global_rwlock in write mode.
 */
static int
insert_rule(struct rbce_rule *rule, int order)
{
#define ORDER_COUNTER_INCR 10
	static int order_counter;
	int old_counter;
	struct list_head *head = &rules_list[rule->classtype];
	struct list_head *insert = head;
	struct rbce_rule *tmp;

	if (gl_num_rules == 0) {
		order_counter = 0;
	}

	switch (order) {
	case -1:
		rule->order = order_counter;
		// FIXME: order_counter overflow/wraparound!!
		order_counter += ORDER_COUNTER_INCR;
		break;
	default:
		old_counter = order_counter;
		if (order_counter < order) {
			order_counter = order;
		}
		rule->order = order;
		order_counter += ORDER_COUNTER_INCR;
		list_for_each_entry(tmp, head, obj.link) {
			if (rule->order == tmp->order) {
				order_counter = old_counter;
				return -EEXIST;
			}
			if (rule->order < tmp->order) {
				insert = &tmp->obj.link;
				break;
			}
		}
	}
	list_add_tail(&rule->obj.link, insert);
	// protect the module from removed when any rule is
	// defined
	try_module_get(THIS_MODULE);
	gl_num_rules++;
	gl_rules_version++;
	return 0;
}

/*
 * Remove the rule and reinsert at the specified order.
 *
 * Caller must hold global_rwlock in write mode.
 */
static int
reinsert_rule(struct rbce_rule *rule, int order)
{
	list_del(&rule->obj.link);
	gl_num_rules--;
	gl_rules_version++;
	module_put(THIS_MODULE);
	return insert_rule(rule, order);
}

/*
 * Get a refernece to the class, create one if it doesn't exist
 *
 * Caller need to hold global_rwlock in write mode.
 * __GFP_WAIT
 */

static struct rbce_class *
get_class(char *classname, int *classtype)
{
	static struct rbce_class *cls;
	void *classobj;

	if (!classname) {
		return NULL;
	}
	cls = find_class_name(classname);
	if (cls) {
		if (cls->classobj) {
			INC_REF(cls);
			*classtype = cls->classtype;
			return cls;
		}
		return NULL;
	}
	classobj = ckrm_classobj(classname, classtype);
	if (!classobj) {
		return NULL;
	}

	/* now walk through our list and see the mapping for that */

	if (*classtype >= CKRM_MAX_CLASSTYPES) {
		printk(KERN_ERR "ckrm_classobj returned %d as classtype which cannot "
				" be handled by RBCE\n", *classtype);
		return NULL;
	}

	cls = kmalloc(sizeof(struct rbce_class), GFP_ATOMIC);
	if (!cls) {
		return NULL;
	}
	cls->obj.name = kmalloc(strlen(classname) + 1, GFP_ATOMIC);
	if (cls->obj.name) {
		GET_REF(cls) = 1;
		cls->classobj = classobj;
		strcpy(cls->obj.name, classname);
		list_add_tail(&cls->obj.link, &class_list);
		notify_class_action(cls,1);
		cls->classtype = *classtype;
	} else {
		kfree(cls);
		cls = NULL;
	}
	return cls;
}

/*
 * Drop a refernece to the class, create one if it doesn't exist
 *
 * Caller need to hold global_rwlock in write mode.
 */
static void
put_class(struct rbce_class *cls)
{
	if (cls) {
		if (DEC_REF(cls) <= 0) {
			notify_class_action(cls,0);
			list_del(&cls->obj.link);
			kfree(cls->obj.name);
			kfree(cls);
		}
	}
	return;
}



/*
 * Callback from core when a class is added
 */

#ifdef RBCE_EXTENSION 
static void
rbce_class_addcb(const char *classname, void *clsobj)
{
	static struct rbce_class *cls;

	write_lock(&global_rwlock);
	cls = find_class_name((char*)classname);
	if (cls) {
		cls->classobj = clsobj; 
		notify_class_action(cls,1);
	}
	write_unlock(&global_rwlock);
	return;
}
#endif

/*
 * Callback from core when a class is deleted.
 */
static void
rbce_class_deletecb(const char *classname, void *classobj)
{
	static struct rbce_class *cls;
	struct named_obj_hdr *pos;
	struct rbce_rule *rule;

	write_lock(&global_rwlock);
	cls = find_class_name(classname);
	if (cls) {
		if (cls->classobj != classobj) {
			printk(KERN_ERR "rbce: class %s changed identity\n",
			classname);
	 	}
		notify_class_action(cls,0);
		cls->classobj = NULL;
		list_for_each_entry(pos, &rules_list[cls->classtype], link) {
			rule = (struct rbce_rule *) pos;
			if (rule->target_class) {
				if (!strcmp(rule->target_class->obj.name, classname)) {
					put_class(cls);
					rule->target_class = NULL;
					rule->classtype = -1;
				}
			}
		}
		if ((cls = find_class_name(classname)) != NULL) {
			printk(KERN_ERR "rbce ERROR: class %s exists in rbce after "
					"removal in core\n", classname);
		}
	}
	write_unlock(&global_rwlock);
	return;
}

/*
 * Allocate an index in the global term vector
 * On success, returns the index. On failure returns -errno.
 * Caller must hold the global_rwlock in write mode as global data is
 * written onto.
 */
static int 
alloc_term_index(void)
{
	int size = gl_allocated;

	if (gl_num_terms >= size) {
		int i;
		struct rbce_rule_term *oldv, *newv;
		int newsize = size + POLICY_INC_NUMTERMS;

		oldv = gl_terms;
		newv = kmalloc(newsize * sizeof(struct rbce_rule_term), GFP_ATOMIC);
		if (!newv) {
			return -ENOMEM;
		}
		memcpy(newv, oldv, size * sizeof(struct rbce_rule_term));
		for (i = size ; i < newsize ; i++) {
			newv[i].op = -1;
		}
		gl_terms = newv;
		gl_allocated = newsize;
		kfree(oldv);
		
		gl_action |= POLICY_ACTION_NEW_VERSION;  
		DPRINTK(DBG_OPTIMIZATION,
			"alloc_term_index: Expanding size from %d to %d\n", size, newsize);
	}
	return gl_num_terms++;
}

/*
 * Release an index in the global term vector
 *
 * Caller must hold the global_rwlock in write mode as the global data
 * is written onto.
 */
static void 
release_term_index(int idx)
{
	if ((idx < 0) || (idx > gl_num_terms)) 
		return;

	gl_terms[idx].op = -1;
	gl_released++;
	if ((gl_released > POLICY_INC_NUMTERMS) &&
			(gl_allocated >
			 (gl_num_terms - gl_released + POLICY_INC_NUMTERMS))) {
		gl_action |= POLICY_ACTION_PACK_TERMS;
	}
	return;
}

/*
 * Release the indices, string memory, and terms associated with the given
 * rule.
 *
 * Caller should be holding global_rwlock
 */
static void
__release_rule(struct rbce_rule *rule)
{
	int i, *terms =  rule->terms;

	// remove memory and references from other rules
	for (i = rule->num_terms; --i >= 0; ) {
		struct rbce_rule_term *term = &gl_terms[terms[i]];

		if (term->op == RBCE_RULE_DEP_RULE) {
			DEC_REF(term->u.deprule);
		}
		release_term_index(terms[i]);
	}
	rule->num_terms = 0;
	if (rule->strtab) {
		kfree(rule->strtab);
		rule->strtab = NULL;
	}
	if (rule->terms) {
		kfree(rule->terms);
		rule->terms = NULL;
	}
	return;
}

/*
 * delete the given rule and all memory associated with it.
 *
 * Caller is responsible for protecting the global data
 */
static inline int
__delete_rule(struct rbce_rule *rule)
{
	// make sure we are not referenced by other rules
	if (GET_REF(rule)) {
		return -EBUSY;
	}
	__release_rule(rule);
	put_class(rule->target_class);
	release_term_index(rule->index);
	list_del(&rule->obj.link);
	gl_num_rules--;
	gl_rules_version++;
	module_put(THIS_MODULE);
	kfree(rule->obj.name);
	kfree(rule);
	return 0;
}

/*
 * Optimize the rule evaluation logic
 *
 * Caller must hold global_rwlock in write mode.
 */
static void
optimize_policy(void)
{
	int i, ii;
	struct rbce_rule *rule;
	struct rbce_rule_term *terms;
	int num_terms;
	int bsize;
	bitvector_t **mask_vecs;
	int pack_terms = 0;
	int redoall;

	/*
	 * Due to dynamic rule addition/deletion of rules the term
	 * vector can get sparse. As a result the bitvectors grow as we don't
	 * reuse returned indices. If it becomes sparse enough we pack them
	 * closer.
	 */

	pack_terms = (gl_action & POLICY_ACTION_PACK_TERMS);
	DPRINTK(DBG_OPTIMIZATION,
		"----- Optimize Policy ----- act=%x pt=%d (a=%d n=%d r=%d)\n",
			gl_action, pack_terms, gl_allocated,
			gl_num_terms, gl_released);

	if (pack_terms) { 
		int nsz = ALIGN((gl_num_terms - gl_released),
						POLICY_INC_NUMTERMS);
		int newidx = 0;
		struct rbce_rule_term *newterms;

		terms = gl_terms;
		newterms = kmalloc(nsz*sizeof(struct rbce_rule_term), GFP_ATOMIC);
		if (newterms) {
			for (ii = 0; ii < CKRM_MAX_CLASSTYPES; ii++) {
				// FIXME: check only for task class types
				list_for_each_entry_reverse(rule, &rules_list[ii], obj.link) {
					rule->index = newidx++;
					for (i = rule->num_terms ; --i >= 0 ; )	{
						int idx = rule->terms[i];
						newterms[newidx] = terms[idx];
						rule->terms[i] = newidx++;
					}
				}
			}
			kfree(terms);
			gl_allocated = nsz;
			gl_released = 0;
			gl_num_terms = newidx;
			gl_terms = newterms;
	
			gl_action &= ~POLICY_ACTION_PACK_TERMS;
			gl_action |= POLICY_ACTION_NEW_VERSION;
		}
	}

	num_terms = gl_num_terms;
	bsize = gl_allocated/8 + sizeof(bitvector_t);
	mask_vecs = gl_mask_vecs;
	terms = gl_terms;

	if (gl_action & POLICY_ACTION_NEW_VERSION) {
		/* allocate new mask vectors */
		char *temp = kmalloc(NUM_TERM_MASK_VECTOR * bsize, GFP_ATOMIC);

		DPRINTK(DBG_OPTIMIZATION, "------ allocmasks act=%x -------  ver=%d\n",
					gl_action, gl_bitmap_version);
		if (!temp) {
			return;
		}
		if (mask_vecs[0]) { // index 0 has the alloc returned address
			kfree(mask_vecs[0]);
		}
		for (i = 0; i < NUM_TERM_MASK_VECTOR; i++) {
			mask_vecs[i] = (bitvector_t *) (temp + i * bsize);
			bitvector_init(mask_vecs[i], gl_allocated);
		}
		gl_action &= ~POLICY_ACTION_NEW_VERSION;
		gl_action |= POLICY_ACTION_REDO_ALL;
		gl_bitmap_version++;
	}


	/* We do two things here at once
	 * 1) recompute the rulemask for each required rule
	 *      we guarantee proper dependency order during creation time and
	 *      by reversely running through this list. 
	 * 2) recompute the mask for each term and rule, if required
	 */

	redoall = gl_action & POLICY_ACTION_REDO_ALL;
	gl_action &= ~POLICY_ACTION_REDO_ALL;

	DPRINTK(DBG_OPTIMIZATION, "------- run act=%x --------  redoall=%d\n",
				gl_action, redoall);
	for (ii = 0; ii < CKRM_MAX_CLASSTYPES; ii++) {
	// FIXME: check only for task class types
	list_for_each_entry_reverse(rule, &rules_list[ii], obj.link) {
		unsigned long termflag;

		if (!redoall && !rule->do_opt)
			continue;
		termflag = 0;
		for (i = rule->num_terms ; --i >= 0 ; )	{
			int j, idx = rule->terms[i];
			struct rbce_rule_term *term = &terms[idx];
			int vecidx = termop_2_vecidx[term->op];

			if (vecidx == -1) {
				termflag |= term->u.deprule->termflag;
				/* mark this term belonging to all contexts of deprule */
				for (j = 0; j < NUM_TERM_MASK_VECTOR; j++) {
					if (term->u.deprule->termflag & (1 << j)) {
						bitvector_set(idx, mask_vecs[j]);
					}
				}
			} else {
				termflag |= TERM_2_TERMFLAG(vecidx);
				/* mark this term belonging to a particular context */
				bitvector_set(idx, mask_vecs[vecidx]);
			}
		}
		for (i = 0; i < NUM_TERM_MASK_VECTOR; i++) {
			if (termflag & (1 << i)) {
				bitvector_set(rule->index, mask_vecs[i]);
			}
		}
		rule->termflag = termflag;
		rule->do_opt = 0;
		DPRINTK(DBG_OPTIMIZATION, "r-%s: %x %d\n", rule->obj.name,
							rule->termflag, rule->index);
	}
	}
	print_context_vectors();
	return;
}

/* ======================= Rule related Functions ========================= */

/*
 * Caller need to hold global_rwlock in write mode.
 */
static int
fill_rule(struct rbce_rule *newrule,
	  struct rbce_rule_term *terms, int nterms)
{
	char *class, *strtab;
	int i, j, order, state, real_nterms, index;
	int strtablen, rc = 0, counter;
	struct rbce_rule_term *term = NULL;
	struct rbce_class *targetcls = NULL;
	struct rbce_rule *deprule;


	if (!newrule) {
		return -EINVAL;
	}

	// Digest filled terms.
	real_nterms = 0;
	strtab = class = NULL;
	strtablen = 0;
	state = -1;
	order = -1;
	index = -1;
	for (i = 0; i < nterms; i++) {
		if (terms[i].op != RBCE_RULE_INVALID) {
			real_nterms++;

			switch(terms[i].op) {
				case RBCE_RULE_DEP_RULE:
					// check if the depend rule is valid
					//
					deprule = find_rule_name(terms[i].u.string);
					if (!deprule || deprule == newrule) {
						rc = -EINVAL;
						goto out;
					} else {
						// make sure _a_ depend rule appears in only
						// one term.
						for (j = 0; j < i; j++) {
							if (terms[j].op == RBCE_RULE_DEP_RULE &&
									terms[j].u.deprule == deprule) {
								rc = -EINVAL;
								goto out;
							}
						}
						terms[i].u.deprule = deprule;
					}

					// +depend is acceptable and -depend is not
					if (terms[i].operator != TOKEN_OP_DEP_DEL) {
						terms[i].operator = RBCE_EQUAL;
					} else {
						rc = -EINVAL;
						goto out;
					}
					break;

				case RBCE_RULE_CMD_PATH:
				case RBCE_RULE_CMD:
				case RBCE_RULE_ARGS:
				case RBCE_RULE_APP_TAG:
				case RBCE_RULE_IPV4:
				case RBCE_RULE_IPV6:
					// sum up the string length
					strtablen += strlen(terms[i].u.string) + 1;
					break;
				default:
					break;

			}
		} else {
			switch (terms[i].operator) {
				case TOKEN_OP_ORDER:
					order = terms[i].u.id;
					if (order < 0) {
						rc = -EINVAL;
						goto out;
					}
					break;
				case TOKEN_OP_STATE:
					state = terms[i].u.id != 0;
					break;
				case TOKEN_OP_CLASS:
					class = terms[i].u.string;
					break;
				default:
					break;
			}
		}
	}

	// Check if class was specified
	if (class != NULL) {
		int classtype;
		if ((targetcls = get_class(class, &classtype)) == NULL) {
			rc = -EINVAL;
			goto out;
		}
		put_class(newrule->target_class);

		newrule->target_class = targetcls;
		newrule->classtype = classtype;
	}
	if (!newrule->target_class) {
		rc = -EINVAL;
		goto out;
	}
	
	if (state != -1) {
		newrule->state = state;
	}
	if (order != -1) {
		newrule->order = order;
	}
	newrule->terms = kmalloc(real_nterms * sizeof(int), GFP_ATOMIC);
	if (!newrule->terms) {
		rc = -ENOMEM;
		goto out;
	}
	newrule->num_terms = real_nterms;
	if (strtablen && ((strtab = kmalloc(strtablen, GFP_ATOMIC)) == NULL)) {
		rc = -ENOMEM;
		goto out;
	}

	if (newrule->index == -1) {
		index = alloc_term_index(); 
		if (index < 0) {
			rc = -ENOMEM;
			goto out;
		}
		newrule->index = index;
		term = &gl_terms[newrule->index];
		term->op = RBCE_RULE_DEP_RULE;
		term->u.deprule = newrule;
	}
	newrule->strtab = strtab;
	newrule->termflag = 0;

	// Fill the term vector
	strtablen = 0;
	counter = 0;
	for (i = 0; i < nterms; i++) {
		if (terms[i].op == RBCE_RULE_INVALID) {
			continue;
		}

		newrule->terms[counter] = alloc_term_index();
		if (newrule->terms[counter] < 0) {
			for (j = 0; j < counter; j++) {
				release_term_index(newrule->terms[j]);
			}
			rc = -ENOMEM;
			goto out;
		}
		term = &gl_terms[newrule->terms[counter]];
		term->op = terms[i].op;
		term->operator = terms[i].operator;
		switch(terms[i].op) {
			case RBCE_RULE_CMD_PATH:
			case RBCE_RULE_CMD:
			case RBCE_RULE_ARGS:
			case RBCE_RULE_APP_TAG:
			case RBCE_RULE_IPV4:
			case RBCE_RULE_IPV6:
				term->u.string = &strtab[strtablen];
				strcpy(term->u.string, terms[i].u.string);
				strtablen = strlen(term->u.string) + 1;
				break;

			case RBCE_RULE_REAL_UID:
			case RBCE_RULE_REAL_GID:
			case RBCE_RULE_EFFECTIVE_UID:
			case RBCE_RULE_EFFECTIVE_GID:
				term->u.id = terms[i].u.id;
				break;

			case RBCE_RULE_DEP_RULE:
				term->u.deprule = terms[i].u.deprule;
				INC_REF(term->u.deprule);
				break;
			default:
				break;
		}
		counter++;
	}

out:
	if (rc) {
		if (targetcls) {
			put_class(targetcls);
		}
		if (index >= 0) {
			release_term_index(index);
		}
		kfree(newrule->terms);
		kfree(strtab);

	}
	return rc;
}

int
change_rule(const char *rname, char *rdefn)
{
	struct rbce_rule *rule = NULL, *deprule;
	struct rbce_rule_term *new_terms = NULL, *term, *terms;
	int nterms, new_term_mask = 0, oterms, tot_terms;
	int i, j, k, rc, new_order = 0;

	if ((nterms = rules_parse(rdefn, &new_terms, &new_term_mask)) <= 0) {
		return !nterms ? -EINVAL : nterms;
	}

	write_lock(&global_rwlock);
	rule = find_rule_name(rname);
	if (rule == NULL) {
		rule = kmalloc(sizeof(struct rbce_rule), GFP_ATOMIC);
		if (rule) {
			rule->obj.name = kmalloc(strlen(rname)+1, GFP_ATOMIC);
			if (rule->obj.name) {
				strcpy(rule->obj.name, rname);
				GET_REF(rule) = 0;
				rule->order = -1;
				rule->index = -1;
				rule->state = RBCE_RULE_ENABLED;
				rule->target_class = NULL;
				rule->classtype = -1;
				rule->terms = NULL;
				rule->do_opt = 1;
				INIT_LIST_HEAD(&rule->obj.link);
				rc = fill_rule(rule, new_terms, nterms);
				if (rc) {
					kfree(rule);
				} else {
					if ((rc = insert_rule(rule, rule->order)) == 0) {
						if (rbce_enabled) {
							optimize_policy();
						}
					} else {
						__delete_rule(rule);
					}
				}
			} else {
				kfree(rule);
				rc = -ENOMEM;
			}
			kfree(new_terms);
		} else {
			rc = -ENOMEM;
		}
		write_unlock(&global_rwlock);
		return rc;
	}

	oterms = rule->num_terms;
	tot_terms = nterms + oterms;

	terms = kmalloc(tot_terms * sizeof(struct rbce_rule_term), GFP_ATOMIC);

	if (!terms) {
		kfree(new_terms);
		write_unlock(&global_rwlock);
		return -ENOMEM;
	}

	new_term_mask &= ~(1 << RBCE_RULE_DEP_RULE);
			//ignore the new deprule terms for the first iteration.
			// taken care of later.
	for (i = 0; i < oterms; i++) {
		term = &gl_terms[rule->terms[i]];  // old term

		if ((1 << term->op) & new_term_mask) { // newrule has this attr/value
			for (j = 0; j < nterms; j++) {
				if (term->op == new_terms[j].op) {
					terms[i].op = new_terms[j].op;
					terms[i].operator = new_terms[j].operator;
					terms[i].u.string = new_terms[j].u.string;
					new_terms[j].op = RBCE_RULE_INVALID2;
					break;
				}
			}
		} else {
			terms[i].op = term->op;
			terms[i].operator = term->operator;
			terms[i].u.string = term->u.string;
		}
	}

	i = oterms; // for readability

	for (j = 0; j < nterms; j++) {
		// handled in the previous iteration
		if (new_terms[j].op == RBCE_RULE_INVALID2) {
			continue;
		}

		if (new_terms[j].op == RBCE_RULE_DEP_RULE) {
			if (new_terms[j].operator == TOKEN_OP_DEP) {
				// "depend=rule" deletes all depends in the original rule
				// so, delete all depend rule terms in the original rule
				for (k = 0; k < oterms; k++) {
					if (terms[k].op == RBCE_RULE_DEP_RULE) {
						terms[k].op = RBCE_RULE_INVALID;
					}
				}
				// must copy the new deprule term
			} else {
				// delete the depend rule term if was defined in the
				// original rule for both +depend and -depend
				deprule = find_rule_name(new_terms[j].u.string);
				if (deprule) {
					for (k = 0; k < oterms; k++) {
						if (terms[k].op == RBCE_RULE_DEP_RULE &&
								terms[k].u.deprule == deprule) {
							terms[k].op = RBCE_RULE_INVALID;
							break;
						}
					}
				}
				if (new_terms[j].operator == TOKEN_OP_DEP_DEL) {
					// No need to copy the new deprule term
					continue;
				}
			}
		} else {
			if ((new_terms[j].op == RBCE_RULE_INVALID) &&
					  (new_terms[j].operator == TOKEN_OP_ORDER)) {
				new_order++;
			}
		}
		terms[i].op = new_terms[j].op;
		terms[i].operator = new_terms[j].operator;
		terms[i].u.string = new_terms[j].u.string;
		i++;
		new_terms[j].op = RBCE_RULE_INVALID2;
	}

	tot_terms = i;

	// convert old deprule pointers to name pointers.
	for (i = 0; i < oterms; i++) {
		if (terms[i].op != RBCE_RULE_DEP_RULE)
			continue;
		terms[i].u.string = terms[i].u.deprule->obj.name;
	}

	// release the rule
	__release_rule(rule);

	rule->do_opt = 1;
	rc = fill_rule(rule, terms, tot_terms);
	if (rc == 0 && new_order) {
		rc = reinsert_rule(rule, rule->order);
	}
	if (rc != 0) { // rule creation/insertion failed
		__delete_rule(rule);
	}
	if (rbce_enabled) {
		optimize_policy();
	}
	write_unlock(&global_rwlock);
	kfree(new_terms);
	kfree(terms);
	return rc;
}

/*
 * Delete the specified rule.
 *
 */
int
delete_rule(const char *rname)
{
	int rc = 0;
	struct rbce_rule *rule;

	write_lock(&global_rwlock);

	if ((rule = find_rule_name(rname)) == NULL) {
		write_unlock(&global_rwlock);
		goto out;	
	}
	rc = __delete_rule(rule);
	if (rbce_enabled && (gl_action & POLICY_ACTION_PACK_TERMS)) {
		optimize_policy();
	}
	write_unlock(&global_rwlock);
out:
	DPRINTK(DBG_RULE, "delete rule %s\n", rname);
	return rc;
}

/*
 * copy the rule specified by rname and to the given result string.
 *
 */
void
get_rule(const char *rname, char *result)
{
	int i;
	struct rbce_rule *rule;
	struct rbce_rule_term *term;
	char *cp = result, oper, idtype[3], str[5];

	read_lock(&global_rwlock);

	rule = find_rule_name(rname);
	if (rule != NULL) {
		for (i = 0; i < rule->num_terms; i++) {
			term = gl_terms + rule->terms[i];
			switch(term->op) {
			case RBCE_RULE_REAL_UID:
				strcpy(idtype, "u");
				goto handleid;
			case RBCE_RULE_REAL_GID:
				strcpy(idtype, "g");
				goto handleid;
			case RBCE_RULE_EFFECTIVE_UID:
				strcpy(idtype, "eu");
				goto handleid;
			case RBCE_RULE_EFFECTIVE_GID:
				strcpy(idtype, "eg");
handleid:
				if (term->operator == RBCE_LESS_THAN) {
					oper = '<';
				} else if (term->operator == RBCE_GREATER_THAN) {
					oper = '>';
				} else if (term->operator == RBCE_NOT) {
					oper = '!';
				} else {
					oper = '=';
				}
				cp += sprintf(cp, "%sid%c%ld,", idtype, oper, term->u.id);
				break;
			case RBCE_RULE_CMD_PATH:
				strcpy(str, "path");
				goto handle_str;
			case RBCE_RULE_CMD:
				strcpy(str, "cmd");
				goto handle_str;
			case RBCE_RULE_ARGS:
				strcpy(str, "args");
				goto handle_str;
			case RBCE_RULE_APP_TAG:
				strcpy(str, "tag");
				goto handle_str;
			case RBCE_RULE_IPV4:
				strcpy(str, "ipv4");
				goto handle_str;
			case RBCE_RULE_IPV6:
				strcpy(str, "ipv6");
handle_str:
				cp += sprintf(cp, "%s=%s,", str, term->u.string);
				break;
			case RBCE_RULE_DEP_RULE:
				cp += sprintf(cp, "depend=%s,", term->u.deprule->obj.name);
				break;
			default:
				break;
			}
		}
		if (!rule->num_terms) {
			cp += sprintf(cp, "***** no terms defined ***** ");
		}

		cp += sprintf(cp, "classtype=%d,", rule->classtype);
		cp += sprintf(cp, "order=%d,state=%d,", rule->order, rule->state);
		cp += sprintf(cp, "class=%s", rule->target_class ? 
				rule->target_class->obj.name : "***** REMOVED *****");	
		*cp = '\0';
	} else {
		sprintf(result, "***** Rule %s doesn't exist *****", rname);
	}

	read_unlock(&global_rwlock);
	return;
}

/*
 * Change the name of the given rule "from_rname" to "to_rname"
 *
 */
int
rename_rule(const char *from_rname, const char *to_rname)
{
	struct rbce_rule *rule;
	int nlen, rc = -EINVAL;

	if (!to_rname || !*to_rname) {
		return rc;
	}
	write_lock(&global_rwlock);

	rule = find_rule_name(from_rname);
	if (rule != NULL) {
		if ((nlen = strlen(to_rname)) > strlen(rule->obj.name)) {
			char *name = kmalloc(nlen + 1, GFP_ATOMIC);
			if (!name) {
				return -ENOMEM;
			}
			kfree(rule->obj.name);
			rule->obj.name = name;
		}
		strcpy(rule->obj.name, to_rname);
		rc = 0;
	}
	write_unlock(&global_rwlock);
	return rc;
}

/*
 * Return TRUE if the given rule exists, FALSE otherwise
 *
 */
int
rule_exists(const char *rname)
{
	struct rbce_rule *rule;

	read_lock(&global_rwlock);
	rule = find_rule_name(rname);
	read_unlock(&global_rwlock);
	return rule != NULL;
}

/*====================== Magic file handling =======================*/
/*
 * Reclassify
 */
static struct rbce_private_data *
create_private_data(struct rbce_private_data *, int);

int rbce_ckrm_reclassify(int pid)
{
	printk("ckrm_reclassify_pid ignored\n");
	return -EINVAL;
}

int
reclassify_pid(int pid)
{
        struct task_struct *tsk;

        // FIXME: Need to treat -pid as process group
        if (pid < 0) {
                return -EINVAL;
        }

        if (pid == 0) {
                rbce_ckrm_reclassify(0); // just reclassify all tasks.
        }

        // if pid is +ve take control of the task, start evaluating it
        if ((tsk = find_task_by_pid(pid)) == NULL) {
                return -EINVAL;
        }

        if (unlikely(!RBCE_DATA(tsk))) {
                RBCE_DATA(tsk) = create_private_data(NULL,0);
                if (!RBCE_DATA(tsk)) {
                        return -ENOMEM;
                }
        }
        RBCE_DATA(tsk)->evaluate = 1;
        rbce_ckrm_reclassify(pid);
        return 0;
}

int
set_tasktag(int pid, char *tag)
{
	char *tp;
	struct task_struct *tsk;
	struct rbce_private_data *pdata;

	if (!tag) {
		return -EINVAL;
	}

	if ((tsk = find_task_by_pid(pid)) == NULL) {
		return -EINVAL;
	}

	tp = kmalloc(strlen(tag) + 1, GFP_ATOMIC);

	if (!tp) {
		return -ENOMEM;
	}

	if (unlikely(!RBCE_DATA(tsk))) {
		RBCE_DATA(tsk) = create_private_data(NULL,0);
		if (!RBCE_DATA(tsk)) {
			kfree(tp);
			return -ENOMEM;
		}
	}
	pdata = RBCE_DATA(tsk);
	if (pdata->app_tag) {
		kfree(pdata->app_tag);
	}
	pdata->app_tag = tp;
	strcpy(pdata->app_tag, tag);
	rbce_ckrm_reclassify(pid);

	return 0;
}

/*====================== Classification Functions =======================*/

/*
 * Match the given full path name with the command expression.
 * This function treats the folowing 2 charaters as special if seen in
 * cmd_exp, all other chanracters are compared as is:
 *		? - compares to any one single character
 *		* - compares to one or more single characters
 *
 * If fullpath is 1, tsk_comm is compared in full. otherwise only the command
 * name (basename(tsk_comm)) is compared.
 */
static int
match_cmd(const char *tsk_comm, const char *cmd_exp, int fullpath)
{
	const char *c, *t, *last_ast, *cmd = tsk_comm;
	char next_c;

	// get the command name if we don't have to match the fullpath
	if (!fullpath && ((c = strrchr (tsk_comm, '/')) != NULL)) {
		cmd = c + 1;
	}

	/* now faithfully assume the entire pathname is in cmd */

	/* we now have to effectively implement a regular expression 
	 * for now assume 
	 *    '?'   any single character 
	 *    '*'   one or more '?'
	 *    rest  must match
	 */

	c = cmd_exp;
	t = cmd;
	if (t == NULL || c == NULL) {
		return 0;
	}

	last_ast = NULL;
	next_c = '\0';

	while (*c && *t) {
		switch (*c) {
		case '?':
			if (*t == '/') {
				return 0;
			}
			c++; t++;
			continue;
		case '*':
			if (*t == '/') {
				return 0;
			}
			// eat up all '*' in c
			while (*(c+1) == '*') c++;	
			next_c = '\0';
			last_ast = c;
			//t++; // Add this for matching '*' with "one" or more chars.
			while (*t && (*t != *(c+1)) && *t != '/') t++;
			if (*t == *(c+1)) {
				c++; 
				if (*c != '/') {
					if (*c == '?') {
						if (*t == '/') {
							return 0;
						}
						t++; c++;
					}
					next_c = *c;
					if (*c) {
						if (*t == '/') {
							return 0;
						}
						t++; c++;
						if (!*c && *t)
							c = last_ast;	
					}
				} else {
					last_ast = NULL;
				}
				continue;
			}
			return 0;
		case '/':
			next_c = '\0';
			/*FALLTHRU*/
		default:
			if (*t == *c && next_c != *t) {
				c++, t++; 
				continue;
			} else {
				/* reset to last asterix and continue from there */
				if (last_ast) {
					c = last_ast;
				} else {
					return 0;
				}
			}
		}			       
	}

	/* check for trailing "*" */
	while (*c == '*') c++;

	return (!*c && !*t);
}

static void
reverse(char *str, int n)
{
	char s;
	int i, j = n-1;

	for (i = 0; i < j; i++,j--) {
		s = str[i];
		str[i] = str[j];
		str[j] = s;
	}
}

static int
itoa(int n, char *str)
{
	int i = 0, sz = 0;

	do {
		str[i++] = n % 10 + '0';
		sz++;
		n = n/10;
										        } while ( n > 0);

	(void)reverse(str,sz);
	return sz;
}

static int
v4toa(__u32 y, char *a)
{
	int i;
	int size = 0;

	for (i=0; i<4; i++) {
		size += itoa(y & 0xff, &a[size]);
		a[size++] = '.';
		y >>= 8;
	}
	return --size;
}

int
match_ipv4(struct ckrm_net_struct *ns, char **string)
{
	char *ptr = *string;
	int size;
	char a4[16];

	size = v4toa(ns->ns_daddrv4,a4);

	*string += size;
	return !strncmp(a4,ptr,size);
}

int
match_port(struct ckrm_net_struct *ns, char *ptr)
{
	char a[5];
	int size = itoa(ns->ns_dport, a);

	return !strncmp(a,ptr,size);
}


static int __evaluate_rule(struct task_struct *tsk, struct ckrm_net_struct *ns,
	       	struct rbce_rule *rule, bitvector_t *vec_eval, 
		bitvector_t *vec_true, char **filename);
/*
 * evaluate the given task against the given rule with the vec_eval and
 * vec_true in context. Return 1 if the task satisfies the given rule, 0
 * otherwise.
 *
 * If the bit corresponding to the rule is set in the vec_eval, then the
 * corresponding bit in vec_true is the result. If it is not set, evaluate
 * the rule and set the bits in both the vectors accordingly.
 *
 * On return, filename will have the pointer to the pathname of the task's
 * executable, if the rule had any command related terms.
 *
 * Caller must hold the global_rwlock atleast in read mode.
 */
static inline int
evaluate_rule(struct task_struct *tsk, struct ckrm_net_struct *ns, 
		struct rbce_rule *rule, bitvector_t *vec_eval, 
		bitvector_t *vec_true, char **filename)
{
	int tidx = rule->index;

	if (!bitvector_test(tidx, vec_eval)) {
		if (__evaluate_rule(tsk,ns,rule,vec_eval, vec_true, filename)) {
			bitvector_set(tidx, vec_true);
		}
		bitvector_set(tidx, vec_eval);
	}
	return bitvector_test(tidx, vec_true);
}

/*
 * evaluate the given task against every term in the given rule with
 * vec_eval and vec_true in context.
 *
 * If the bit corresponding to a rule term is set in the vec_eval, then the
 * corresponding bit in vec_true is the result for taht particular. If it is
 * not set, evaluate the rule term and set the bits in both the vectors
 * accordingly.
 *
 * This fucntions returns true only if all terms in the rule evaluate true.
 *
 * On return, filename will have the pointer to the pathname of the task's
 * executable, if the rule had any command related terms.
 *
 * Caller must hold the global_rwlock atleast in read mode.
 */
static int
__evaluate_rule(struct task_struct *tsk, struct ckrm_net_struct *ns, 
		struct rbce_rule *rule, bitvector_t *vec_eval,
	       	bitvector_t *vec_true, char **filename)
{
	int i;
	int no_ip = 1;

	for (i = rule->num_terms ; --i >= 0 ; )	{
		int rc = 1, tidx = rule->terms[i];

		if (!bitvector_test(tidx, vec_eval)) {
			struct rbce_rule_term *term = &gl_terms[tidx];

			switch(term->op) {

			case RBCE_RULE_CMD_PATH:
			case RBCE_RULE_CMD:
#if 0
				if (!*filename) {  /* get this once */
					if (((*filename = kmalloc(NAME_MAX,GFP_ATOMIC)) == NULL) || 
					    (get_exe_path_name(tsk, *filename, NAME_MAX) < 0))
					{
						rc = 0;
						break;
					}
				}
				rc = match_cmd(*filename, term->u.string,
						(term->op == RBCE_RULE_CMD_PATH));
#else
				rc = match_cmd(tsk->comm, term->u.string,
						(term->op == RBCE_RULE_CMD_PATH));
#endif
				break;
			case RBCE_RULE_REAL_UID:
				if (term->operator == RBCE_LESS_THAN) {
					rc = (tsk->uid < term->u.id);
				} else if (term->operator == RBCE_GREATER_THAN) {
					rc = (tsk->uid > term->u.id);
				} else if (term->operator == RBCE_NOT) {
					rc = (tsk->uid != term->u.id);
				} else {
					rc = (tsk->uid == term->u.id);
				}
				break;
			case RBCE_RULE_REAL_GID:
				if (term->operator == RBCE_LESS_THAN) {
					rc = (tsk->gid < term->u.id);
				} else if (term->operator == RBCE_GREATER_THAN) {
					rc = (tsk->gid > term->u.id);
				} else if (term->operator == RBCE_NOT) {
					rc = (tsk->gid != term->u.id);
				} else {
					rc = (tsk->gid == term->u.id);
				}
				break;
			case RBCE_RULE_EFFECTIVE_UID:
				if (term->operator == RBCE_LESS_THAN) {
					rc = (tsk->euid < term->u.id);
				} else if (term->operator == RBCE_GREATER_THAN) {
					rc = (tsk->euid > term->u.id);
				} else if (term->operator == RBCE_NOT) {
					rc = (tsk->euid != term->u.id);
				} else {
					rc = (tsk->euid == term->u.id);
				}
				break;
			case RBCE_RULE_EFFECTIVE_GID:
				if (term->operator == RBCE_LESS_THAN) {
					rc = (tsk->egid < term->u.id);
				} else if (term->operator == RBCE_GREATER_THAN) {
					rc = (tsk->egid > term->u.id);
				} else if (term->operator == RBCE_NOT) {
					rc = (tsk->egid != term->u.id);
				} else {
					rc = (tsk->egid == term->u.id);
				}
				break;
			case RBCE_RULE_APP_TAG:
				rc = (RBCE_DATA(tsk) && RBCE_DATA(tsk)->app_tag) ? 
						!strcmp(RBCE_DATA(tsk)->app_tag, term->u.string) : 0;
				break;
			case RBCE_RULE_DEP_RULE:
				rc = evaluate_rule(tsk, NULL, term->u.deprule, vec_eval, vec_true,
						filename);
				break;
					
			case RBCE_RULE_IPV4:
				// TBD: add NOT_EQUAL match. At present rbce
				// recognises EQUAL matches only.
				if (ns && term->operator == RBCE_EQUAL) {
					int ma = 0;
					int mp = 0;
					char *ptr = term->u.string;

					if (term->u.string[0] == '*')
						ma = 1;
					else
						ma = match_ipv4(ns,&ptr);

					if (*ptr != '\\') { // error
						rc = 0;
						break;
					}
					else {
						++ptr;
						if (*ptr == '*')
							mp = 1;
						else
							mp = match_port(ns,ptr);
					}
					rc = mp && ma;
				}
				else 
					rc = 0;
				no_ip = 0;
				break;

			case RBCE_RULE_IPV6: // no support yet
				rc = 0;
				no_ip = 0;
				break;

			default:
				rc = 0;
				printk(KERN_ERR "Error evaluate term op=%d\n",term->op);
				break;
			}
			if (!rc && no_ip) {
				bitvector_clear(tidx, vec_true);
			} else {
				bitvector_set(tidx, vec_true);
			}
			bitvector_set(tidx, vec_eval);
		} else {
			rc = bitvector_test(tidx, vec_true);
		}
		if (!rc) {
			return 0;
		}
	}
	return 1;
}

//#define PDATA_DEBUG
#ifdef PDATA_DEBUG

#define MAX_PDATA 10000
void *pdata_arr[MAX_PDATA];
int pdata_count, pdata_next;
static spinlock_t pdata_lock = SPIN_LOCK_UNLOCKED;

static inline int
valid_pdata(struct rbce_private_data *pdata)
{
	int i;

	if (!pdata) {
		return 1;
	}
	spin_lock(&pdata_lock);
	for (i = 0; i < MAX_PDATA; i++) {
		if (pdata_arr[i] == pdata) {
			spin_unlock(&pdata_lock);
			return 1;
		}
	}
	spin_unlock(&pdata_lock);
	printk("INVALID/CORRUPT PDATA %p\n", pdata);
	return 0;
}

static inline void
store_pdata(struct rbce_private_data *pdata)
{
	int i = 0;

	if (pdata) {
		spin_lock(&pdata_lock);

		while (i < MAX_PDATA) {
			if (pdata_arr[pdata_next] == NULL) {
				printk("storing %p at %d, count %d\n", pdata, pdata_next, pdata_count);
				pdata_arr[pdata_next++] = pdata;
				if (pdata_next == MAX_PDATA) {
					pdata_next = 0;
				}
				pdata_count++;
				break;
			}
			pdata_next++;
			i++;
		}
		spin_unlock(&pdata_lock);
	}
	if (i == MAX_PDATA) {
		printk("PDATA BUFFER FULL pdata_count %d pdata %p\n", pdata_count, pdata);
	}
}

static inline void
unstore_pdata(struct rbce_private_data *pdata)
{
	int i;
	if (pdata) {
		spin_lock(&pdata_lock);
		for (i = 0; i < MAX_PDATA; i++) {
			if (pdata_arr[i] == pdata) {
				printk("unstoring %p at %d, count %d\n", pdata, i, pdata_count);
				pdata_arr[i] = NULL;
				pdata_count--;
				pdata_next = i;
				break;
			}
		}
		spin_unlock(&pdata_lock);
		if (i == MAX_PDATA) {
			printk("pdata %p not found in the stored array\n", pdata);
		}
	}
	return;
}

#else // PDATA_DEBUG

#define valid_pdata(pdata) (1)
#define store_pdata(pdata)
#define unstore_pdata(pdata)

#endif // PDATA_DEBUG

const int use_persistent_state = 1;

/*
 * Allocate and initialize a rbce_private_data data structure.
 *
 * Caller must hold global_rwlock atleast in read mode.
 */

static inline void 
copy_ext_private_data(struct rbce_private_data *src, struct rbce_private_data *dst)
{
	if (src)
		dst->ext_data = src->ext_data;
	else
		memset(&dst->ext_data,0,sizeof(dst->ext_data));
}

static struct rbce_private_data *
create_private_data(struct rbce_private_data *src, int copy_sample)
{
	int vsize, psize, bsize;
	struct rbce_private_data *pdata;

	if (use_persistent_state) {
		vsize = gl_allocated;
		bsize = vsize/8 + sizeof(bitvector_t);
		psize = sizeof(struct rbce_private_data) + 2 * bsize;
	} else {
		psize = sizeof(struct rbce_private_data);
	}

	pdata = kmalloc(psize, GFP_ATOMIC);
	if (pdata != NULL) {
		if (use_persistent_state) {
			pdata->bitmap_version = gl_bitmap_version;
			pdata->eval = (bitvector_t*) &pdata->data[0];
			pdata->true = (bitvector_t*) &pdata->data[bsize];
			if (src && (src->bitmap_version == gl_bitmap_version)) {
				memcpy(pdata->data, src->data, 2 * bsize);
			} else {
				bitvector_init(pdata->eval, vsize);
				bitvector_init(pdata->true, vsize);
			}
		}
		copy_ext_private_data(src,pdata);
		//if (src) { // inherit evaluate and app_tag
		//      pdata->evaluate = src->evaluate;
		//      if(src->app_tag) {
		//              int len = strlen(src->app_tag)+1;
		//              printk("CREATE_PRIVATE: apptag %s len %d\n",
		//                          src->app_tag,len);
		//              pdata->app_tag = kmalloc(len, GFP_ATOMIC);
		//              if (pdata->app_tag) {
		//                      strcpy(pdata->app_tag, src->app_tag);
		//              }
		//      }
		//} else {
		pdata->evaluate = 1;
		pdata->rules_version = src ? src->rules_version : 0;
		pdata->app_tag = NULL;
		//}
	}
	store_pdata(pdata);
	return pdata;
}

static inline void
free_private_data(struct rbce_private_data *pdata)
{
	if (valid_pdata(pdata)) {
		unstore_pdata(pdata);
		kfree(pdata);
	}
}

static void 
free_all_private_data(void)
{
	struct task_struct *proc, *thread;

	read_lock(&tasklist_lock);
	do_each_thread(proc, thread) {
		struct rbce_private_data *pdata;
		
		pdata = RBCE_DATA(thread);
		RBCE_DATA(thread) = NULL;
		free_private_data(pdata);
	} while_each_thread(proc, thread);
	read_unlock(&tasklist_lock);
	return;
}

/*
 * reclassify function, which is called by all the callback functions.
 *
 * Takes that task to be reclassified and ruleflags that indicates the
 * attributes that caused this reclassification request.
 *
 * On success, returns the core class pointer to which the given task should
 * belong to.
 */
static struct ckrm_core_class *
rbce_classify(struct task_struct *tsk, struct ckrm_net_struct *ns, unsigned long termflag, int classtype)
{
	int i;
	struct rbce_rule *rule;
	bitvector_t *vec_true = NULL, *vec_eval = NULL;
	struct rbce_class *tgt = NULL;
	struct ckrm_core_class *cls = NULL;
	char *filename = NULL;

	if (!valid_pdata(RBCE_DATA(tsk))) {
		return NULL;
	}
	if (classtype >= CKRM_MAX_CLASSTYPES) {
		// can't handle more than CKRM_MAX_CLASSTYPES
		return NULL;
	}

	// fast path to avoid locking in case CE is not enabled or if no rules
	// are defined or if the tasks states that no evaluation is needed.
	if (!rbce_enabled || !gl_num_rules ||
			(RBCE_DATA(tsk) && !RBCE_DATA(tsk)->evaluate)) {
		return NULL;
	}

	// FIXME: optimize_policy should be called from here if
	// gl_action is non-zero. Also, it has to be called with the
	// global_rwlock held in write mode.
	
	read_lock(&global_rwlock);

	vec_eval = vec_true = NULL;
	if (use_persistent_state) {
		struct rbce_private_data *pdata = RBCE_DATA(tsk);

		if (!pdata || (pdata && (gl_bitmap_version != pdata->bitmap_version))) {
			struct rbce_private_data *new_pdata = create_private_data(pdata,1);

			if (new_pdata) {
				if (pdata) {
					new_pdata->rules_version = pdata->rules_version;
					new_pdata->evaluate = pdata->evaluate;
					new_pdata->app_tag  = pdata->app_tag;
					free_private_data(pdata);
				}
				pdata = RBCE_DATA(tsk) = new_pdata;
				termflag = RBCE_TERMFLAG_ALL;  // need to evaluate them all
			} else {
				// we shouldn't free the pdata as it has more details than
				// the vectors. But, this reclassification should go thru
				pdata = NULL;
			}
		}
		if (!pdata) {
			goto cls_determined;
		}
		vec_eval = pdata->eval;
		vec_true = pdata->true;
	} else {
		int bsize = gl_allocated;

		vec_eval = bitvector_alloc(bsize);
		vec_true = bitvector_alloc(bsize);
		
		if (vec_eval == NULL || vec_true == NULL) {
			goto cls_determined;
		}
		termflag = RBCE_TERMFLAG_ALL;  // need to evaluate all of them now
	}
	
	/* 
	 * using bit ops invalidate all terms related to this termflag
	 * context (only in per task vec)
	 */
	DPRINTK(DBG_CLASSIFY_DETAILS, "\nClassify: termflag=%lx\n", termflag); 
	DPRINTK(DBG_CLASSIFY_DETAILS, "     eval before: ");
	bitvector_print(DBG_CLASSIFY_DETAILS, vec_eval);
	DPRINTK(DBG_CLASSIFY_DETAILS, "\n     true before: ");
	bitvector_print(DBG_CLASSIFY_DETAILS, vec_true);
	DPRINTK(DBG_CLASSIFY_DETAILS, "\n     redo => ");

	if (termflag == RBCE_TERMFLAG_ALL) {
		DPRINTK(DBG_CLASSIFY_DETAILS, "  redoall ");
		bitvector_zero(vec_eval);
	} else {
		for (i = 0; i < NUM_TERM_MASK_VECTOR; i++) {
			if (test_bit(i, &termflag)) {
				bitvector_t *maskvec = gl_mask_vecs[i];

				DPRINTK(DBG_CLASSIFY_DETAILS, "  mask(%d) ",i);
				bitvector_print(DBG_CLASSIFY_DETAILS, maskvec);
				bitvector_and_not(vec_eval, vec_eval, maskvec);
			}
		}
	}
	bitvector_and(vec_true, vec_true, vec_eval);

	DPRINTK(DBG_CLASSIFY_DETAILS, "\n     eval now:    ");
	bitvector_print(DBG_CLASSIFY_DETAILS, vec_eval);
	DPRINTK(DBG_CLASSIFY_DETAILS, "\n");  

	/* run through the rules in order and see what needs evaluation */
	list_for_each_entry(rule, &rules_list[classtype], obj.link) {
		if (rule->state == RBCE_RULE_ENABLED &&
				rule->target_class &&
				rule->target_class->classobj &&
				evaluate_rule(tsk,ns, rule, vec_eval, vec_true, &filename)) {
			tgt = rule->target_class;
			cls = rule->target_class->classobj;
			break;
		}
	}

cls_determined:
	DPRINTK(DBG_CLASSIFY_RES, 
			"==> |%s|; pid %d; euid %d; egid %d; ruid %d; rgid %d;"
			"tag |%s| ===> class |%s|\n",
			filename ? filename : tsk->comm,
			tsk->pid,
			tsk->euid,
			tsk->egid,
			tsk->uid,
			tsk->gid,
			RBCE_DATA(tsk) ? RBCE_DATA(tsk)->app_tag : "",
			tgt ? tgt->obj.name : ""
			);
	DPRINTK(DBG_CLASSIFY_DETAILS, "     eval after: ");
	bitvector_print(DBG_CLASSIFY_DETAILS, vec_eval);
	DPRINTK(DBG_CLASSIFY_DETAILS, "\n     true after: ");
	bitvector_print(DBG_CLASSIFY_DETAILS, vec_true);
	DPRINTK(DBG_CLASSIFY_DETAILS, "\n");

	if (!use_persistent_state) {
		if (vec_eval) {
			bitvector_free(vec_eval);
		}
		if (vec_true) {
			bitvector_free(vec_true);
		}
	}
	ckrm_core_grab(cls);
	read_unlock(&global_rwlock);
	if (filename) {
		kfree(filename);
	}
	if (RBCE_DATA(tsk)) {
		RBCE_DATA(tsk)->rules_version = gl_rules_version;
	}
	return cls;
}



/*****************************************************************************
 *
 * Module specific utilization of core RBCE functionality
 *
 * Includes support for the various classtypes 
 * New classtypes will require extensions here
 * 
 *****************************************************************************/

/* helper functions that are required in the extended version */

static inline void
rbce_tc_manual(struct task_struct *tsk)
{
	read_lock(&global_rwlock);

	if (!RBCE_DATA(tsk)) {
		RBCE_DATA(tsk) = (void*) create_private_data(RBCE_DATA(tsk->parent),0);
	}
	if (RBCE_DATA(tsk)) {
		RBCE_DATA(tsk)->evaluate = 0;
	}
	read_unlock(&global_rwlock);
	return;
}


/*****************************************************************************
 *   load any extensions
 *****************************************************************************/

#ifdef RBCE_EXTENSION
#include "rbcemod_ext.c"
#endif

/*****************************************************************************
 *    VARIOUS CLASSTYPES
 *****************************************************************************/

// to enable type coercion of the function pointers

/*============================================================================
 *    TASKCLASS CLASSTYPE
 *============================================================================*/

int tc_classtype = -1;

/*
 * fork callback to be registered with core module.
 */
inline static void *
rbce_tc_forkcb(struct task_struct *tsk)
{
	int rule_version_changed = 1;
	struct ckrm_core_class *cls;
	read_lock(&global_rwlock);
	// dup ce_data
	RBCE_DATA(tsk) = (void*) create_private_data(RBCE_DATA(tsk->parent),0);
	read_unlock(&global_rwlock);

	if (RBCE_DATA(tsk->parent)) {
		rule_version_changed =
			(RBCE_DATA(tsk->parent)->rules_version != gl_rules_version);
	}
	cls = rule_version_changed ?
			rbce_classify(tsk, NULL, RBCE_TERMFLAG_ALL, tc_classtype): NULL;

	// note the fork notification to any user client will be sent through
	// the guaranteed fork-reclassification
	return cls;
}

/*
 * exit callback to be registered with core module.
 */
static void
rbce_tc_exitcb(struct task_struct *tsk)
{
	struct rbce_private_data* pdata;
	
	send_exit_notification(tsk);

	pdata = RBCE_DATA(tsk);		
	RBCE_DATA(tsk) = NULL;
	if (pdata) {
		if (pdata->app_tag) {
			kfree(pdata->app_tag);
		}
		free_private_data(pdata);
	}
	return;
}

#define AENT(x) [ CKRM_EVENT_##x] = #x
static const char* event_names[CKRM_NUM_EVENTS] = {
	AENT(NEWTASK),
	AENT(FORK),
	AENT(EXIT),
	AENT(EXEC),
	AENT(UID),
	AENT(GID),
	AENT(LOGIN),
	AENT(USERADD),
	AENT(USERDEL),
	AENT(LISTEN_START),
	AENT(LISTEN_STOP),
	AENT(APPTAG),
	AENT(RECLASSIFY),
	AENT(MANUAL),
};

void * rbce_tc_classify(enum ckrm_event event, struct task_struct* tsk)
{
	void *cls = NULL;

	/* we only have to deal with events between 
	 * [ CKRM_LATCHABLE_EVENTS .. CKRM_NONLATCHABLE_EVENTS ) 
	 */

	// printk("tc_classify %p:%d:%s '%s'\n",tsk,tsk->pid,tsk->comm,event_names[event]);

	switch (event) {
		
	case CKRM_EVENT_FORK:
		cls = rbce_tc_forkcb(tsk);
		break;

	case CKRM_EVENT_EXIT:
		rbce_tc_exitcb(tsk);
		break;

	case CKRM_EVENT_EXEC:
		cls = rbce_classify(tsk, NULL, RBCE_TERMFLAG_CMD, tc_classtype);
		break;

	case CKRM_EVENT_UID:
		cls = rbce_classify(tsk, NULL, RBCE_TERMFLAG_UID, tc_classtype);
		break;

	case CKRM_EVENT_GID:
		cls = rbce_classify(tsk, NULL, RBCE_TERMFLAG_GID, tc_classtype);
		break;
		
	case CKRM_EVENT_LOGIN:
	case CKRM_EVENT_USERADD:
	case CKRM_EVENT_USERDEL:
	case CKRM_EVENT_LISTEN_START:
	case CKRM_EVENT_LISTEN_STOP:
	case CKRM_EVENT_APPTAG:
		/* no interest in this events .. */
		break;
		
	default:
		/* catch all */
		break;


	case CKRM_EVENT_RECLASSIFY:
		cls = rbce_classify(tsk, NULL, RBCE_TERMFLAG_ALL, tc_classtype);
		break;
		
	}
	// printk("tc_classify %p:%d:%s '%s' ==> %p\n",tsk,tsk->pid,tsk->comm,event_names[event],cls);
	
	return cls;
}

#ifndef RBCE_EXTENSION
static void
rbce_tc_notify(int event, void *core, struct task_struct *tsk)
{
	printk("tc_manual %p:%d:%s '%s'\n",tsk,tsk->pid,tsk->comm,event_names[event]);
	if (event != CKRM_EVENT_MANUAL) 
		return;
	rbce_tc_manual(tsk);
}
#endif

static struct ckrm_eng_callback rbce_taskclass_ecbs = {
	.c_interest             = (unsigned long) (-1), // set whole bitmap
	.classify               = (ce_classify_fct_t)rbce_tc_classify,
        .class_delete           = rbce_class_deletecb,
#ifndef RBCE_EXTENSION
	.n_interest             = ( 1 << CKRM_EVENT_MANUAL),
	.notify                 = (ce_notify_fct_t)rbce_tc_notify,
	.always_callback        = 0,
#else
	.n_interest             = (unsigned long) (-1), // set whole bitmap
	.notify                 = (ce_notify_fct_t)rbce_tc_ext_notify,
	.class_add              = rbce_class_addcb,
	.always_callback        = 1,
#endif
};
 
/*============================================================================
 *    ACCEPTQ CLASSTYPE
 *============================================================================*/

int sc_classtype = -1;

void * rbce_sc_classify(enum ckrm_event event, 
			struct ckrm_net_struct *ns, struct task_struct *tsk)
{
	// no special consideratation
	void *result;

	result = rbce_classify(tsk, ns, RBCE_TERMFLAG_ALL,sc_classtype);

	DPRINTK(DBG_CLASSIFY_RES, 
		"==>  %d.%d.%d.%d\\%d , %p:%d:%s '%s' => %p\n",
		NIPQUAD(ns->ns_daddrv4),ns->ns_dport,
		tsk,tsk?tsk->pid:0,tsk?tsk->comm:"-",event_names[event],result);
	return result;
}

static struct ckrm_eng_callback rbce_acceptQclass_ecbs = {
	.c_interest             = (unsigned long) (-1),
	.always_callback        = 0,   // enable during debugging only
	.classify               = (ce_classify_fct_t) &rbce_sc_classify,
        .class_delete           = rbce_class_deletecb,
};

/*============================================================================
 *    Module Initialization ...
 *============================================================================*/

#define TASKCLASS_NAME  "taskclass"
#define SOCKCLASS_NAME  "socket_class"

struct ce_regtable_struct {
	const char               *name;
	struct ckrm_eng_callback *cbs;
	int                      *clsvar;
};

struct ce_regtable_struct ce_regtable[] = {
	{ TASKCLASS_NAME, &rbce_taskclass_ecbs ,   &tc_classtype },
	{ SOCKCLASS_NAME, &rbce_acceptQclass_ecbs, &sc_classtype },
	{ NULL }
};
		
static int register_classtype_engines(void)
{
	int rc;
	struct ce_regtable_struct *ceptr = ce_regtable;

	while (ceptr->name) {
		rc = ckrm_register_engine(ceptr->name, ceptr->cbs);
		printk("ce register with <%s> typeId=%d\n",ceptr->name,rc);
		if ((rc < 0) && (rc != -ENOENT))
			return (rc);
		if (rc != -ENOENT) 
			*ceptr->clsvar = rc;
		ceptr++;
	}
	return 0;
}

static void unregister_classtype_engines(void)
{
	int rc;
	struct ce_regtable_struct *ceptr = ce_regtable;

	while (ceptr->name) {
		if (*ceptr->clsvar >= 0) {
			printk("ce unregister with <%s>\n",ceptr->name);
			rc = ckrm_unregister_engine(ceptr->name);
			printk("ce unregister with <%s> rc=%d\n",ceptr->name,rc);
			*ceptr->clsvar = -1;
		}
		ceptr++;
	}
}

// =========== /proc/sysctl/debug/rbce debug stuff =============

#ifdef DEBUG
static struct ctl_table_header *rbce_sysctl_table_header;

#define CTL_RBCE_DEBUG (201)     // picked some number.. dont know algo to pick
static struct ctl_table rbce_entry_table[] = {
{
	.ctl_name       = CTL_RBCE_DEBUG,
	.procname       = "rbce",
	.data           = &rbcedebug,
	.maxlen         = sizeof(int),
	.mode           = 0644,
	.proc_handler   = &proc_dointvec,
},
{ 0 }
};

static struct ctl_table rbce_root_table[] = {
{
	.ctl_name       = CTL_DEBUG,
	.procname       = "debug",
	.data           = NULL,
	.maxlen         = 0,
	.mode           = 0555,
	.child          = rbce_entry_table
},
{ 0 }
};

static inline void
start_debug(void)
{
	rbce_sysctl_table_header = register_sysctl_table(rbce_root_table, 1);
}
static inline void
stop_debug(void)
{
	if (rbce_sysctl_table_header) 
		unregister_sysctl_table(rbce_sysctl_table_header);
}

#else

static inline void start_debug(void) { }
static inline void stop_debug(void) { }

#endif // DEBUG

extern int rbce_mkdir(struct inode *, struct dentry *, int );
extern int rbce_rmdir(struct inode *, struct dentry *);
rbce_eng_callback_t rcfs_ecbs = {
	rbce_mkdir,
	rbce_rmdir
};

/* ======================= Module definition Functions ======================== */

extern int rbce_create_magic(void);
extern int rbce_clear_magic(void);

int
init_rbce (void)
{
	int rc,i,line;

	printk("<1>\nInstalling \'%s\' module\n", modname);

	for (i = 0; i < CKRM_MAX_CLASSTYPES; i++) {
		INIT_LIST_HEAD(&rules_list[i]);
	}

	rc = init_rbce_ext_pre(); line=__LINE__;
	if (rc) goto out;

	rc = register_classtype_engines(); line=__LINE__;
	if (rc) goto out_unreg_ckrm;   // need to remove anyone opened
	
	/* register any other class type engine here */

	rc = rcfs_register_engine(&rcfs_ecbs);  line=__LINE__;
	if (rc) goto out_unreg_ckrm;

	rc = rbce_create_magic();  line=__LINE__;
	if (rc) goto out_unreg_ckrm;

	start_debug();

	rc = init_rbce_ext_post(); line=__LINE__;
	if (rc) goto out_debug;

	return 0;  // SUCCESS

 out_debug:
	stop_debug();
 out_unreg_ckrm:
	
	unregister_classtype_engines();
	exit_rbce_ext();
 out:

	printk("<1>%s: error installing rc=%d line=%d\n",__FUNCTION__,rc,line);
	return rc;
}

void
exit_rbce (void)
{
	int i;

	printk("<1>Removing \'%s\' module\n", modname);

	stop_debug();
	exit_rbce_ext();

	// Print warnings if lists are not empty, which is a bug
	if (!list_empty(&class_list)) {
		printk("exit_rbce: Class list is not empty\n");
	}

	for (i = 0; i < CKRM_MAX_CLASSTYPES; i++) {
		if (!list_empty(&rules_list[i])) {
			printk("exit_rbce: Rules list for classtype %d is not empty\n", i);
		}
	}
	rbce_clear_magic();

	rcfs_unregister_engine(&rcfs_ecbs);
	unregister_classtype_engines();
	free_all_private_data();
}

EXPORT_SYMBOL(get_rule);
EXPORT_SYMBOL(rule_exists);
EXPORT_SYMBOL(change_rule);
EXPORT_SYMBOL(delete_rule);
EXPORT_SYMBOL(rename_rule);
EXPORT_SYMBOL(reclassify_pid);
EXPORT_SYMBOL(set_tasktag);

module_init(init_rbce);
module_exit(exit_rbce);
