/*
 *	Copyright (C) 1998-2005 Novell/SUSE
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

#include <linux/fs.h>	/* Include for defn of iattr */
#include <linux/binfmts.h>	/* defn of linux_binprm */
#include <linux/rcupdate.h>

#include "shared.h"

/* Control parameters (0 or 1), settable thru module/boot flags or
 * via /sys/kernel/security/apparmor/control */
extern int apparmor_complain;
extern int apparmor_debug;
extern int apparmor_audit;
extern int apparmor_logsyscall;

/* PIPEFS_MAGIC */
#include <linux/pipe_fs_i.h>
/* from net/socket.c */
#define SOCKFS_MAGIC 0x534F434B
/* from inotify.c  */
#define INOTIFYFS_MAGIC 0xBAD1DEA

#define VALID_FSTYPE(inode) ((inode)->i_sb->s_magic != PIPEFS_MAGIC && \
                             (inode)->i_sb->s_magic != SOCKFS_MAGIC && \
                             (inode)->i_sb->s_magic != INOTIFYFS_MAGIC)

#define PROFILE_COMPLAIN(_profile) \
	(apparmor_complain == 1 || ((_profile) && (_profile)->flags.complain))

#define SUBDOMAIN_COMPLAIN(_sd) \
	(apparmor_complain == 1 || \
	 ((_sd) && (_sd)->active && (_sd)->active->flags.complain))

#define PROFILE_AUDIT(_profile) \
	(apparmor_audit == 1 || ((_profile) && (_profile)->flags.audit))

#define SUBDOMAIN_AUDIT(_sd) \
	(apparmor_audit == 1 || \
	 ((_sd) && (_sd)->active && (_sd)->active->flags.audit))

/*
 * DEBUG remains global (no per profile flag) since it is mostly used in sysctl
 * which is not related to profile accesses.
 */

#define AA_DEBUG(fmt, args...)						\
	do {								\
		if (apparmor_debug)					\
			printk(KERN_DEBUG "AppArmor: " fmt, ##args);	\
	} while (0)
#define AA_INFO(fmt, args...)	printk(KERN_INFO "AppArmor: " fmt, ##args)
#define AA_WARN(fmt, args...)	printk(KERN_WARNING "AppArmor: " fmt, ##args)
#define AA_ERROR(fmt, args...)	printk(KERN_ERR "AppArmor: " fmt, ##args)

/* basic AppArmor data structures */

struct flagval {
	int debug;
	int complain;
	int audit;
};

enum entry_match_type {
	aa_entry_literal,
	aa_entry_tailglob,
	aa_entry_pattern,
	aa_entry_invalid
};

/* struct aa_entry - file ACL *
 * @filename: filename controlled by this ACL
 * @mode: permissions granted by ACL
 * @type: type of match to perform against @filename
 * @extradata: any extra data needed by an extended matching type
 * @list: list the ACL is on
 * @listp: permission partitioned lists this ACL is on.
 *
 * Each entry describes a file and an allowed access mode.
 */
struct aa_entry {
	char *filename;
	int mode;		/* mode is 'or' of READ, WRITE, EXECUTE,
				 * INHERIT, UNCONSTRAINED, and LIBRARY
				 * (meaning don't prefetch). */

	enum entry_match_type type;
	void *extradata;

	struct list_head list;
	struct list_head listp[POS_AA_FILE_MAX + 1];
};

#define AA_SECURE_EXEC_NEEDED 0x00000001

#define AA_EXEC_MODIFIER_MASK(mask) ((mask) & AA_EXEC_MODIFIERS)
#define AA_EXEC_MASK(mask) ((mask) & (AA_MAY_EXEC | AA_EXEC_MODIFIERS))
#define AA_EXEC_UNSAFE_MASK(mask) ((mask) & (AA_MAY_EXEC | AA_EXEC_MODIFIERS |\
					     AA_EXEC_UNSAFE))

/* struct aaprofile - basic confinement data
 * @parent: non refcounted pointer to parent profile
 * @name: the profiles name
 * @file_entry: file ACL
 * @file_entryp: vector of file ACL by permission granted
 * @list: list this profile is on
 * @sub: profiles list of subprofiles (HATS)
 * @flags: flags controlling profile behavior
 * @null_profile: if needed per profile learning and null confinement profile
 * @isstale: flag to indicate the profile is stale
 * @num_file_entries: number of file entries the profile contains
 * @num_file_pentries: number of file entries for each partitioned list
 * @capabilities: capabilities granted by the process
 * @rcu: rcu head used when freeing the profile
 * @count: reference count of the profile
 *
 * The AppArmor profile contains the basic confinement data.  Each profile
 * has a name and potentially a list of profile entries. The profiles are
 * connected in a list
 */
struct aaprofile {
	struct aaprofile *parent;
	char *name;

	struct list_head file_entry;
	struct list_head file_entryp[POS_AA_FILE_MAX + 1];
	struct list_head list;
	struct list_head sub;
	struct flagval flags;
	struct aaprofile *null_profile;
	int isstale;

	int num_file_entries;
	int num_file_pentries[POS_AA_FILE_MAX + 1];

	kernel_cap_t capabilities;

	struct rcu_head rcu;

	struct kref count;
};

enum aafile_type {
	aa_file_default,
	aa_file_shmem
};

/**
 * aafile - file pointer confinement data
 *
 * Data structure assigned to each open file (by apparmor_file_alloc_security)
 */
struct aafile {
	enum aafile_type type;
	struct aaprofile *profile;
};

/**
 * struct subdomain - primary label for confined tasks
 * @active: the current active profile
 * @hat_magic: the magic token controling the ability to leave a hat
 * @list: list this subdomain is on
 * @task: task that the subdomain confines
 *
 * Contains the tasks current active profile (which could change due to
 * change_hat).  Plus the hat_magic needed during change_hat.
 *
 * N.B AppArmor's previous product name SubDomain was derived from the name
 * of this structure/concept (changehat reducing a task into a sub-domain).
 */
struct subdomain {
	struct aaprofile *active;	/* The current active profile */
	u32 hat_magic;			/* used with change_hat */
	struct list_head list;		/* list of subdomains */
	struct task_struct *task;
};

typedef int (*aa_iter) (struct subdomain *, void *);

/* aa_path_data
 * temp (cookie) data used by aa_path_* functions, see inline.h
 */
struct aa_path_data {
	struct dentry *root, *dentry;
	struct namespace *namespace;
	struct list_head *head, *pos;
	int errno;
};

#define AA_SUBDOMAIN(sec)	((struct subdomain*)(sec))
#define AA_PROFILE(sec)		((struct aaprofile*)(sec))

/* Lock protecting access to 'struct subdomain' accesses */
extern spinlock_t sd_lock;

extern struct aaprofile *null_complain_profile;

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
	unsigned int ival;
	union {
		const void *pval;
		va_list vaval;
	};
};

/* audit types */
#define AA_AUDITTYPE_FILE	1
#define AA_AUDITTYPE_DIR	2
#define AA_AUDITTYPE_ATTR	3
#define AA_AUDITTYPE_XATTR	4
#define AA_AUDITTYPE_LINK	5
#define AA_AUDITTYPE_CAP	6
#define AA_AUDITTYPE_MSG	7
#define AA_AUDITTYPE_SYSCALL	8
#define AA_AUDITTYPE__END	9

/* audit flags */
#define AA_AUDITFLAG_AUDITSS_SYSCALL 1 /* log syscall context */
#define AA_AUDITFLAG_LOGERR	     2 /* log operations that failed due to
					   non permission errors  */

#define HINT_UNKNOWN_HAT "unknown_hat"
#define HINT_FORK "fork"
#define HINT_MANDPROF "missing_mandatory_profile"
#define HINT_CHGPROF "changing_profile"

#define LOG_HINT(p, gfp, hint, fmt, args...) \
	do {\
		aa_audit_message(p, gfp, 0, \
			"LOGPROF-HINT " hint " " fmt, ##args);\
	} while(0)

/* directory op type, for aa_perm_dir */
enum aa_diroptype {
	aa_dir_mkdir,
	aa_dir_rmdir
};

/* xattr op type, for aa_xattr */
enum aa_xattroptype {
	aa_xattr_get,
	aa_xattr_set,
	aa_xattr_list,
	aa_xattr_remove
};

#define BASE_PROFILE(p) ((p)->parent ? (p)->parent : (p))
#define IN_SUBPROFILE(p) ((p)->parent)

/* main.c */
extern int alloc_null_complain_profile(void);
extern void free_null_complain_profile(void);
extern int attach_nullprofile(struct aaprofile *profile);
extern int aa_audit_message(struct aaprofile *active, gfp_t gfp, int,
			    const char *, ...);
extern int aa_audit_syscallreject(struct aaprofile *active, gfp_t gfp,
				  const char *);
extern int aa_audit(struct aaprofile *active, const struct aa_audit *);
extern char *aa_get_name(struct dentry *dentry, struct vfsmount *mnt);

extern int aa_attr(struct aaprofile *active, struct dentry *dentry,
		   struct iattr *iattr);
extern int aa_xattr(struct aaprofile *active, struct dentry *dentry,
		    const char *xattr, enum aa_xattroptype xattroptype);
extern int aa_capability(struct aaprofile *active, int cap);
extern int aa_perm(struct aaprofile *active, struct dentry *dentry,
		   struct vfsmount *mnt, int mask);
extern int aa_perm_nameidata(struct aaprofile *active, struct nameidata *nd,
			     int mask);
extern int aa_perm_dentry(struct aaprofile *active, struct dentry *dentry,
			  int mask);
extern int aa_perm_dir(struct aaprofile *active, struct dentry *dentry,
		       enum aa_diroptype diroptype);
extern int aa_link(struct aaprofile *active,
		   struct dentry *link, struct dentry *target);
extern int aa_fork(struct task_struct *p);
extern int aa_register(struct linux_binprm *bprm);
extern void aa_release(struct task_struct *p);
extern int aa_change_hat(const char *id, u32 hat_magic);
extern int aa_associate_filp(struct file *filp);

/* list.c */
extern struct aaprofile *aa_profilelist_find(const char *name);
extern int aa_profilelist_add(struct aaprofile *profile);
extern struct aaprofile *aa_profilelist_remove(const char *name);
extern void aa_profilelist_release(void);
extern struct aaprofile *aa_profilelist_replace(struct aaprofile *profile);
extern void aa_profile_dump(struct aaprofile *);
extern void aa_profilelist_dump(void);
extern void aa_subdomainlist_add(struct subdomain *);
extern void aa_subdomainlist_remove(struct subdomain *);
extern void aa_subdomainlist_iterate(aa_iter, void *);
extern void aa_subdomainlist_iterateremove(aa_iter, void *);
extern void aa_subdomainlist_release(void);

/* module_interface.c */
extern ssize_t aa_file_prof_add(void *, size_t);
extern ssize_t aa_file_prof_repl(void *, size_t);
extern ssize_t aa_file_prof_remove(const char *, size_t);
extern void free_aaprofile(struct aaprofile *profile);
extern void free_aaprofile_kref(struct kref *kref);

/* procattr.c */
extern size_t aa_getprocattr(struct aaprofile *active, char *str, size_t size);
extern int aa_setprocattr_changehat(char *hatinfo, size_t infosize);
extern int aa_setprocattr_setprofile(struct task_struct *p, char *profilename,
				     size_t profilesize);

/* apparmorfs.c */
extern int create_apparmorfs(void);
extern void destroy_apparmorfs(void);

/* capabilities.c */
extern const char *capability_to_name(unsigned int cap);

#endif				/* __APPARMOR_H */
