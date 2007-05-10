/*
 *	Copyright (C) 1998-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor internal prototypes
 */

#ifndef __APPARMOR_H
#define __APPARMOR_H

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/rcupdate.h>

/*
 * We use MAY_READ, MAY_WRITE, MAY_EXEC, and the following flags for
 * profile permissions (we don't use MAY_APPEND):
 */
#define AA_MAY_LINK			0x0010
#define AA_EXEC_INHERIT			0x0020
#define AA_EXEC_UNCONFINED		0x0040
#define AA_EXEC_PROFILE			0x0080
#define AA_EXEC_MMAP			0x0100
#define AA_EXEC_UNSAFE			0x0200

#define AA_EXEC_MODIFIERS		(AA_EXEC_INHERIT | \
					 AA_EXEC_UNCONFINED | \
					 AA_EXEC_PROFILE)

#define AA_SECURE_EXEC_NEEDED		1

/* Control parameters (0 or 1), settable thru module/boot flags or
 * via /sys/kernel/security/apparmor/control */
extern int apparmor_complain;
extern int apparmor_debug;
extern int apparmor_audit;
extern int apparmor_logsyscall;
extern unsigned int apparmor_path_max;

#define PROFILE_COMPLAIN(_profile) \
	(apparmor_complain == 1 || ((_profile) && (_profile)->flags.complain))

#define APPARMOR_COMPLAIN(_cxt) \
	(apparmor_complain == 1 || \
	 ((_cxt) && (_cxt)->profile && (_cxt)->profile->flags.complain))

#define PROFILE_AUDIT(_profile) \
	(apparmor_audit == 1 || ((_profile) && (_profile)->flags.audit))

#define APPARMOR_AUDIT(_cxt) \
	(apparmor_audit == 1 || \
	 ((_cxt) && (_cxt)->profile && (_cxt)->profile->flags.audit))

/*
 * DEBUG remains global (no per profile flag) since it is mostly used in sysctl
 * which is not related to profile accesses.
 */

#define AA_DEBUG(fmt, args...)						\
	do {								\
		if (apparmor_debug)					\
			printk(KERN_DEBUG "AppArmor: " fmt, ##args);	\
	} while (0)

#define AA_ERROR(fmt, args...)	printk(KERN_ERR "AppArmor: " fmt, ##args)

/* struct aa_profile - basic confinement data
 * @parent: non refcounted pointer to parent profile
 * @name: the profiles name
 * @file_rules: dfa containing the profiles file rules
 * @list: list this profile is on
 * @sub: profiles list of subprofiles (HATS)
 * @flags: flags controlling profile behavior
 * @null_profile: if needed per profile learning and null confinement profile
 * @isstale: flag indicating if profile is stale
 * @capabilities: capabilities granted by the process
 * @count: reference count of the profile
 *
 * The AppArmor profile contains the basic confinement data.  Each profile
 * has a name and potentially a list of sub profile entries. All non stale
 * profiles are on the profile_list.
 *
 * The task_contexts list and the isstale flag are protected by the
 * profile lock.
 *
 * If a task context is moved between two profiles, we first need to grab
 * both profile locks. lock_both_profiles() does that in a deadlock-safe
 * way.
 */
struct aa_profile {
	struct aa_profile *parent;
	char *name;
	struct aa_dfa *file_rules;
	struct list_head list;
	struct list_head sub;
	struct {
		int complain;
		int audit;
	} flags;
	struct aa_profile *null_profile;
	int isstale;

	kernel_cap_t capabilities;
	struct kref count;
	struct list_head task_contexts;
	spinlock_t lock;
	unsigned long int_flags;
};

extern struct list_head profile_list;
extern rwlock_t profile_list_lock;
extern struct mutex aa_interface_lock;

/**
 * struct aa_task_context - primary label for confined tasks
 * @profile: the current profile
 * @hat_magic: the magic token controling the ability to leave a hat
 * @list: list this aa_task_context is on
 * @task: task that the aa_task_context confines
 * @rcu: rcu head used when freeing the aa_task_context
 * @caps_logged: caps that have previously generated log entries
 *
 * Contains the task's current profile (which could change due to
 * change_hat).  Plus the hat_magic needed during change_hat.
 */
struct aa_task_context {
	struct aa_profile *profile;	/* The current profile */
	u64 hat_magic;			/* used with change_hat */
	struct list_head list;
	struct task_struct *task;
	struct rcu_head rcu;
	kernel_cap_t caps_logged;
};

extern struct aa_profile *null_complain_profile;

/* aa_audit - AppArmor auditing structure
 * Structure is populated by access control code and passed to aa_audit which
 * provides for a single point of logging.
 */

struct aa_audit {
	unsigned short type, flags;
	unsigned int result;
	gfp_t gfp_mask;
	int error_code;
	const char *name;
	char *buffer;
	union {
		int mask;
		int capability;
		struct {
			const char *name2;
			char *buffer2;
		};
		struct iattr *iattr;
		va_list vaval;
	};
};

/* audit types */
#define AA_MANGLE_NAME		32
#define AA_MANGLE_NAME2		64
#define AA_AUDITTYPE_FILE	(1 | AA_MANGLE_NAME)
#define AA_AUDITTYPE_DIR	(2 | AA_MANGLE_NAME)
#define AA_AUDITTYPE_ATTR	(3 | AA_MANGLE_NAME)
#define AA_AUDITTYPE_XATTR	(4 | AA_MANGLE_NAME)
#define AA_AUDITTYPE_LINK	(5 | AA_MANGLE_NAME | AA_MANGLE_NAME2)
#define AA_AUDITTYPE_CAP	6
#define AA_AUDITTYPE_MSG	7
#define AA_AUDITTYPE_SYSCALL	8

/* audit flags */
#define AA_AUDITFLAG_AUDITSS_SYSCALL 1 /* log syscall context */
#define AA_AUDITFLAG_LOGERR	     2 /* log operations that failed due to
					   non permission errors  */

/* Flags for the permission check functions */
#define AA_CHECK_FD	1  /* coming from a file descriptor */
#define AA_CHECK_DIR	2  /* file type is directory */
#define AA_CHECK_MANGLE	4  /* leave extra room for name mangling */

/* main.c */
extern int alloc_null_complain_profile(void);
extern void free_null_complain_profile(void);
extern int attach_nullprofile(struct aa_profile *profile);
extern int aa_audit_message(struct aa_profile *profile, gfp_t gfp,
			    const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern int aa_audit_syscallreject(struct aa_profile *profile, gfp_t gfp,
				  const char *);
extern int aa_audit(struct aa_profile *profile, struct aa_audit *);

extern int aa_attr(struct aa_profile *profile, struct dentry *dentry,
		   struct vfsmount *mnt, struct iattr *iattr);
extern int aa_perm_xattr(struct aa_profile *profile, struct dentry *dentry,
			 struct vfsmount *mnt, const char *operation, int mask,
			 int check);
extern int aa_capability(struct aa_task_context *cxt, int cap);
extern int aa_perm(struct aa_profile *profile, struct dentry *dentry,
		   struct vfsmount *mnt, int mask, int check);
extern int aa_perm_dir(struct aa_profile *profile, struct dentry *dentry,
		       struct vfsmount *mnt, const char *operation, int mask);
extern int aa_perm_path(struct aa_profile *, const char *, int);
extern int aa_link(struct aa_profile *profile,
		   struct dentry *link, struct vfsmount *link_mnt,
		   struct dentry *target, struct vfsmount *target_mnt);
extern int aa_clone(struct task_struct *task);
extern int aa_register(struct linux_binprm *bprm);
extern void aa_release(struct task_struct *task);
extern int aa_change_hat(const char *id, u64 hat_magic);
extern struct aa_profile *__aa_find_profile(const char *name,
					    struct list_head *list);
extern struct aa_profile *__aa_replace_profile(struct task_struct *task,
					       struct aa_profile *profile,
					       u32 hat_magic);
extern struct aa_task_context *lock_task_and_profiles(struct task_struct *task,
						      struct aa_profile *profile);
extern void aa_change_task_context(struct task_struct *task,
				   struct aa_task_context *new_cxt,
				   struct aa_profile *profile, u64 hat_magic);
extern int aa_may_ptrace(struct aa_task_context *cxt,
			 struct aa_profile *tracee);

/* list.c */
extern void aa_profilelist_release(void);

/* module_interface.c */
extern ssize_t aa_add_profile(void *, size_t);
extern ssize_t aa_replace_profile(void *, size_t);
extern ssize_t aa_remove_profile(const char *, size_t);
extern struct aa_profile *alloc_aa_profile(void);
extern void free_aa_profile(struct aa_profile *profile);
extern void free_aa_profile_kref(struct kref *kref);
extern void aa_unconfine_tasks(struct aa_profile *profile);

/* procattr.c */
extern int aa_getprocattr(struct aa_profile *profile, char **string,
			  unsigned *len);
extern int aa_setprocattr_changehat(char *args);
extern int aa_setprocattr_setprofile(struct task_struct *task, char *args);

/* apparmorfs.c */
extern int create_apparmorfs(void);
extern void destroy_apparmorfs(void);

/* match.c */
extern struct aa_dfa *aa_match_alloc(void);
extern void aa_match_free(struct aa_dfa *dfa);
extern int unpack_dfa(struct aa_dfa *dfa, void *blob, size_t size);
extern int verify_dfa(struct aa_dfa *dfa);
extern unsigned int aa_dfa_match(struct aa_dfa *dfa, const char *str);

#endif  /* __APPARMOR_H */
