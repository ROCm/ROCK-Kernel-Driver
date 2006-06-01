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

#ifndef __SUBDOMAIN_H
#define __SUBDOMAIN_H

/* defn of iattr */
#include <linux/fs.h>

/* defn of linux_binprm */
#include <linux/binfmts.h>

#include "shared.h"

/* Control parameters (0 or 1), settable thru module/boot flags or
 * via /sys/kernel/security/subdomain/control */
extern int subdomain_complain;
extern int subdomain_debug;
extern int subdomain_audit;
extern int subdomain_logsyscall;

#define SD_UNCONSTRAINED "unconstrained"

/* $ echo -n subdomain.o | md5sum | cut -c -8 */
#define  SD_ID_MAGIC		0x8c235e38

#define PROFILE_COMPLAIN(_profile) \
	(subdomain_complain == 1 || ((_profile) && (_profile)->flags.complain))

#define SUBDOMAIN_COMPLAIN(_sd) \
	(subdomain_complain == 1 || \
	 ((_sd) && (_sd)->active && (_sd)->active->flags.complain))

#define SUBDOMAIN_AUDIT(_sd) \
	(subdomain_audit == 1 || \
	 ((_sd) && (_sd)->active && (_sd)->active->flags.audit))

/*
 * DEBUG remains global (no per profile flag) since it is mostly used in sysctl
 * which is not related to profile accesses.
 */

#define SD_DEBUG(fmt, args...)						\
	do {								\
		if (subdomain_debug)					\
			printk(KERN_DEBUG "AppArmor: " fmt, ##args);	\
	} while (0)
#define SD_INFO(fmt, args...)	printk(KERN_INFO "AppArmor: " fmt, ##args)
#define SD_WARN(fmt, args...)	printk(KERN_WARNING "AppArmor: " fmt, ##args)
#define SD_ERROR(fmt, args...)	printk(KERN_ERR "AppArmor: " fmt, ##args)

/* basic AppArmor data structures */

struct flagval {
	int debug;
	int complain;
	int audit;
};

enum entry_t {
	sd_entry_literal,
	sd_entry_tailglob,
	sd_entry_pattern,
	sd_entry_invalid
};

/**
 * sd_entry - file ACL *
 * Each entry describes a file and an allowed access mode.
 */
struct sd_entry {
	char *filename;
	int mode;		/* mode is 'or' of READ, WRITE, EXECUTE,
				 * INHERIT, UNCONSTRAINED, and LIBRARY
				 * (meaning don't prefetch). */

	enum entry_t entry_type;
	void *extradata;

	struct list_head list;
	struct list_head listp[POS_SD_FILE_MAX + 1];
};

#define SD_SECURE_EXEC_NEEDED 0x00000001

#define SD_EXEC_MODIFIER_MASK(mask) ((mask) & SD_EXEC_MODIFIERS)

#define SD_EXEC_MASK(mask) ((mask) & (SD_MAY_EXEC | SD_EXEC_MODIFIERS))

#define SD_EXEC_UNSAFE_MASK(mask) ((mask) & (SD_MAY_EXEC |\
					     SD_EXEC_MODIFIERS |\
					     SD_EXEC_UNSAFE))

/**
 * sdprofile - basic confinement data
 *
 * The AppArmor profile contains the basic confinement data.  Each profile
 * has a name and potentially a list of subdomain entries. The profiles are
 * connected in a list
 */
struct sdprofile {
	char *name;			/* profile name */

	struct list_head file_entry;	/* file ACL */
	struct list_head file_entryp[POS_SD_FILE_MAX + 1];
	struct list_head list;		/* list of profiles */
	struct list_head sub;		/* sub profiles, for change_hat */
	struct flagval flags;		/* per profile debug flags */

	int isstale;			/* is profile stale */

	int num_file_entries;
	int num_file_pentries[POS_SD_FILE_MAX + 1];

	kernel_cap_t capabilities;

	atomic_t count;			/* reference count */
};

enum sdfile_type {
	sd_file_default,
	sd_file_shmem
};

/**
 * sdfile - file pointer confinement data
 *
 * Data structure assigned to each open file (by subdomain_file_alloc_security)
 */
struct sdfile {
	enum sdfile_type type;
	struct sdprofile *profile;
};

/**
 * subdomain - a task's subdomain
 *
 * Contains the original profile obtained from execve() as well as the
 * current active profile (which could change due to change_hat).  Plus
 * the hat_magic needed during change_hat.
 */
struct subdomain {
	__u32 sd_magic;			/* magic value to distinguish blobs */
	struct sdprofile *profile;	/* The profile obtained from execve() */
	struct sdprofile *active;	/* The current active profile */
	__u32 sd_hat_magic;		/* used with change_hat */
	struct list_head list;		/* list of subdomains */
	struct task_struct *task;
};

typedef int (*sd_iter) (struct subdomain *, void *);

/* sd_path_data
 * temp (cookie) data used by sd_path_* functions, see inline.h
 */
struct sd_path_data {
	struct dentry *root, *dentry;
	struct namespace *namespace;
	struct list_head *head, *pos;
	int errno;
};

#define SD_SUBDOMAIN(sec)	((struct subdomain*)(sec))
#define SD_PROFILE(sec)		((struct sdprofile*)(sec))

/* Lock protecting access to 'struct subdomain' accesses */
extern rwlock_t sd_lock;

extern struct sdprofile *null_profile;
extern struct sdprofile *null_complain_profile;

/** sd_audit
 *
 * Auditing structure
 */

struct sd_audit {
	unsigned short type, flags;
	unsigned int result;
	unsigned int gfp_mask;
	int errorcode;

	const char *name;
	unsigned int ival;
	union{
		const void *pval;
		va_list vaval;
	};
};

/* audit types */
#define SD_AUDITTYPE_FILE	1
#define SD_AUDITTYPE_DIR	2
#define SD_AUDITTYPE_ATTR	3
#define SD_AUDITTYPE_XATTR	4
#define SD_AUDITTYPE_LINK	5
#define SD_AUDITTYPE_CAP	6
#define SD_AUDITTYPE_MSG	7
#define SD_AUDITTYPE_SYSCALL	8
#define SD_AUDITTYPE__END	9

/* audit flags */
#define SD_AUDITFLAG_AUDITSS_SYSCALL 1 /* log syscall context */
#define SD_AUDITFLAG_LOGERR	     2 /* log operations that failed due to
					   non permission errors  */

#define HINT_UNKNOWN_HAT "unknown_hat"
#define HINT_FORK "fork"
#define HINT_MANDPROF "missing_mandatory_profile"
#define HINT_CHGPROF "changing_profile"

#define LOG_HINT(sd, gfp, hint, fmt, args...) \
	do {\
		sd_audit_message(sd, gfp, 0, \
			"LOGPROF-HINT " hint " " fmt, ##args);\
	} while(0)

/* diroptype */
#define SD_DIR_MKDIR 0
#define SD_DIR_RMDIR 1

/* xattroptype */
#define SD_XATTR_GET    0
#define SD_XATTR_SET    1
#define SD_XATTR_LIST   2
#define SD_XATTR_REMOVE 3

/* main.c */
extern int alloc_nullprofiles(void);
extern void free_nullprofiles(void);
extern int sd_audit_message(struct subdomain *, unsigned int gfp, int,
			    const char *, ...);
extern int sd_audit_syscallreject(struct subdomain *, unsigned int gfp,
				  const char *);
extern int sd_audit(struct subdomain *, const struct sd_audit *);
extern char *sd_get_name(struct dentry *dentry, struct vfsmount *mnt);

extern int sd_attr(struct subdomain *sd, struct dentry *dentry,
		   struct iattr *iattr);
extern int sd_xattr(struct subdomain *sd, struct dentry *dentry,
		    const char *xattr, int xattroptype);
extern int sd_capability(struct subdomain *sd, int cap);
extern int sd_perm(struct subdomain *sd, struct dentry *dentry,
		   struct vfsmount *mnt, int mask);
extern int sd_perm_nameidata(struct subdomain *sd, struct nameidata *nd,
			     int mask);
extern int sd_perm_dentry(struct subdomain *sd, struct dentry *dentry,
			  int mask);
extern int sd_perm_dir(struct subdomain *sd, struct dentry *dentry,
		       int diroptype);
extern int sd_link(struct subdomain *sd,
		   struct dentry *link, struct dentry *target);
extern int sd_fork(struct task_struct *p);
extern int sd_register(struct linux_binprm *bprm);
extern void sd_release(struct task_struct *p);
extern int sd_change_hat(const char *id, __u32 hat_magic);
extern int sd_associate_filp(struct file *filp);

/* list.c */
extern struct sdprofile *sd_profilelist_find(const char *name);
extern int sd_profilelist_add(struct sdprofile *profile);
extern int sd_profilelist_remove(const char *name);
extern void sd_profilelist_release(void);
extern struct sdprofile *sd_profilelist_replace(struct sdprofile *profile);
extern void sd_profile_dump(struct sdprofile *);
extern void sd_profilelist_dump(void);
extern void sd_subdomainlist_add(struct subdomain *);
extern void sd_subdomainlist_remove(struct subdomain *);
extern void sd_subdomainlist_iterate(sd_iter, void *);
extern void sd_subdomainlist_iterateremove(sd_iter, void *);
extern void sd_subdomainlist_release(void);

/* subdomain_interface.c */
extern void free_sdprofile(struct sdprofile *profile);
extern int sd_sys_security(unsigned int id, unsigned call, unsigned long *args);

/* procattr.c */
extern size_t sd_getprocattr(struct subdomain *sd, char *str, size_t size);
extern int sd_setprocattr_changehat(char *hatinfo, size_t infosize);
extern int sd_setprocattr_setprofile(struct task_struct *p, char *profilename,
				     size_t profilesize);

/* apparmorfs.c */
extern int create_subdomainfs(void);
extern int destroy_subdomainfs(void);

/* capabilities.c */
extern const char *capability_to_name(unsigned int cap);

/* apparmor_version.c */
extern const char *apparmor_version(void);
extern const char *apparmor_version_nl(void);

#endif				/* __SUBDOMAIN_H */
