#ifndef __LINUX_HOOK_H
#define __LINUX_HOOK_H
/*
 * Kernel Hooks Interface.
 * 
 * Authors: Richard J Moore <richardj_moore@uk.ibm.com>
 *	    Vamsi Krishna S. <vamsi_krishna@in.ibm.com>
 * (C) Copyright IBM Corp. 2002, 2003
 */
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/list.h>

/* define the user (kernel module) hook structure */
struct hook_rec;
struct hook;

struct hook_rec {
	void *hook_exit;
	struct list_head exit_list;
	unsigned int hook_flags;
	unsigned int hook_index;
	struct hook *hook_head;
/*fields required for adding proc entries */
	char *hook_exit_name;
	struct proc_dir_entry *proc_entry;
	int proc_writable;
};

struct hook {
	struct list_head exit_list;
	unsigned int hook_flags;
	unsigned int hook_index;
	void *hook_addr;
	void *hook_ex_exit;
/*fields required for adding proc entries */
	struct proc_dir_entry *proc_entry;
	char *hook_id;
	atomic_t hook_activate;
	atomic_t hook_deactivate;
};

/* hook flags */
#define HOOK_ARMED		0x00000001
#define HOOK_PRIORITY_FIRST	0x00000004
#define HOOK_PRIORITY_LAST	0x00000008
#define HOOK_QUEUE_LIFO		0x00000010

/* flag groupings */
#define HOOK_PRIORITY		(HOOK_PRIORITY_FIRST | HOOK_PRIORITY_LAST)
#define HOOK_QUEUE		HOOK_QUEUE_LIFO
#define HOOK_VALID_USER_FLAGS	(HOOK_PRIORITY | HOOK_QUEUE)

/*
 * Use the DECLARE_HOOK macro to define the hook structure in global 
 * memory of a kernel module that is implementing a hook.
 */
#if defined(CONFIG_HOOK) || defined(CONFIG_HOOK_MODULE)

#define HOOK_SYM(h) _HOOK_SYM(h)
#define _HOOK_SYM(h) h##_hook

#define DECLARE_HOOK(name) _DECLARE_HOOK(name, HOOK_SYM(name))
#define _DECLARE_HOOK(name, hk) \
extern void hk; \
struct hook name = { \
	.hook_addr 	= &(hk), 			  \
	.exit_list	= LIST_HEAD_INIT(name.exit_list), \
	.hook_index	= 0,				  \
	.hook_id	= #name,			  \
}; \
EXPORT_SYMBOL(name);

#define DECLARE_EXCLUSIVE_HOOK(name) _DECLARE_EXCLUSIVE_HOOK(name, HOOK_SYM(name))
#define _DECLARE_EXCLUSIVE_HOOK(name, hk) \
extern void hk; \
struct hook name = { \
	.hook_addr 	= &(hk), 			  \
	.exit_list	= LIST_HEAD_INIT(name.exit_list), \
	.hook_flags	= HOOK_EXCLUSIVE,		  \
	.hook_index	= 0,				  \
	.hook_id	= #name,			  \
}; \
EXPORT_SYMBOL(name);

/*
 * Generic hooks are the same in all architectures and may be used to
 * place hooks even in inline functions. They don't define a symbol at hook
 * location.
 */ 

#define DECLARE_GENERIC_HOOK(name) _DECLARE_GENERIC_HOOK(name)
#define _DECLARE_GENERIC_HOOK(name) \
struct hook name = { \
	.hook_addr 	= &(name), 			  \
	.exit_list	= LIST_HEAD_INIT(name.exit_list), \
	.hook_id	= #name,			  \
}; \
EXPORT_SYMBOL(name);

#define USE_HOOK(name) _USE_HOOK(name)
#define _USE_HOOK(name) extern struct hook name

/* define head record only flags */
#define HOOK_ACTIVE	0x80000000
#define HOOK_ASM_HOOK	0x40000000
#define HOOK_EXCLUSIVE	0x20000000

/* global status flags */
#define HOOK_INIT       1

typedef int (*hook_fn_t)(struct hook *, ...);

#ifdef CONFIG_ASM_HOOK
#include <asm/hook.h>
#else
static inline int is_asm_hook(unsigned char *addr) {return 0;}
static inline void activate_asm_hook(struct hook *hook) { }
static inline void deactivate_asm_hook(struct hook *hook) { }
#endif

#ifndef IF_HOOK_ENABLED
#define IF_HOOK_ENABLED(h, hk) _IF_HOOK_ENABLED(h, #hk)
#define _IF_HOOK_ENABLED(h, hk) \
	__asm__ __volatile__ (".global "hk"; "hk":"); \
	if (unlikely(h.hook_flags & HOOK_ACTIVE))
#endif

#define HOOK_TEST(h) \
	extern struct hook h; \
	IF_HOOK_ENABLED(h, h##_hook)

#define CALL_EXIT(fn, parm, args ...) (((hook_fn_t)(fn))(parm , ##args))

#define DISPATCH_NORMAL(fn, parm, dsprc, args...) \
	dsprc = CALL_EXIT(fn, parm , ##args);

#define DISPATCH_RET(fn, parm, dsprc, args...) { \
	int rc; \
	dsprc = CALL_EXIT(fn, parm, &rc , ##args); \
	if (dsprc == HOOK_RETURN) \
		return rc; \
}

#define DISPATCH_RET_NORC(fn, parm, dsprc, args...) { \
	dsprc = CALL_EXIT(fn, parm , ##args); \
	if (dsprc == HOOK_RETURN) \
		return; \
}

#define HOOK_DISP_LOOP(h, dispatch, args...) { \
	register struct hook_rec *rec; \
	list_for_each_entry(rec, &h.exit_list, exit_list) { \
		register int dsprc; \
		if (rec->hook_flags & HOOK_ARMED) { \
			dispatch(rec->hook_exit, rec->hook_head, dsprc , ##args) \
			if (dsprc == HOOK_TERMINATE) \
				break; \
		} \
	} \
}

#define HOOK_DISP_EXCLUSIVE(h, dispatch, args...) { \
	register int dsprc; \
	if (h.hook_flags & HOOK_ACTIVE) { \
		dispatch(h.hook_ex_exit, &h, dsprc , ##args) \
	} \
}

#define HOOK(h, args...) { \
	HOOK_TEST(h) \
	HOOK_DISP_LOOP(h, DISPATCH_NORMAL , ##args); \
}

#define HOOK_RET(h, args...) { \
	HOOK_TEST(h) \
	HOOK_DISP_LOOP(h, DISPATCH_RET , ##args); \
}

#define HOOK_RET_NORC(h, args...) { \
	HOOK_TEST(h) \
	HOOK_DISP_LOOP(h, DISPATCH_RET_NORC , ##args); \
}

#define EXCLUSIVE_HOOK(h, args...) { \
	HOOK_TEST(h) \
	HOOK_DISP_EXCLUSIVE(h, DISPATCH_NORMAL , ##args); \
}

#define EXCLUSIVE_HOOK_RET(h, args...) { \
	HOOK_TEST(h) \
	HOOK_DISP_EXCLUSIVE(h, DISPATCH_RET , ##args); \
}

#define EXCLUSIVE_HOOK_RET_NORC(h, args...) { \
	HOOK_TEST(h) \
	HOOK_DISP_EXCLUSIVE(h, DISPATCH_RET_NORC , ##args); \
}

#define GENERIC_HOOK(h, args...) { \
	extern struct hook h; \
	if (unlikely(h.hook_flags & HOOK_ACTIVE)) { \
		HOOK_DISP_LOOP(h, DISPATCH_NORMAL , ##args); \
	} \
}

/* exported function prototypes */
extern int hook_exit_register(struct hook *, struct hook_rec *);
extern void hook_exit_deregister(struct hook_rec *);
extern void hook_exit_arm(struct hook_rec *);
extern void hook_exit_disarm(struct hook_rec *);

/* exported functions error codes */
#define EPRIORITY	1	/* reqd. priority not possible */
#define ERROR_HIGHER_PRIORITY_HOOK 		-2
#define ERROR_LOWER_PRIORITY_HOOK 		-4

/* Return values from Hook Exit routines */
#define HOOK_CONTINUE 	0
#define HOOK_TERMINATE 	1
#define HOOK_RETURN 	-1

#else
/* dummy macros when hooks are not compiled in */
#define DECLARE_HOOK(x)
#define DECLARE_GENERIC_HOOK(x)
#define USE_HOOK(x)
#define HOOK(h, args...)
#define HOOK_RET(h, args...)
#define HOOK_RET_NORC(h, args...)
#define EXCLUSIVE_HOOK(h, args...)
#define EXCLUSIVE_HOOK_RET(h, args...)
#define EXCLUSIVE_HOOK_RET_NORC(h, args...)
#define GENERIC_HOOK(h, args...)
#endif /* !(CONFIG_HOOK || CONFIG_HOOK_MODULE) */

#endif /* __LINUX_HOOK_H */
