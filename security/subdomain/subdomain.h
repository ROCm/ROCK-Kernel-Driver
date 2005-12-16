/*
 *	Copyright (C) 1998-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	SubDomain internal prototypes
 */

#ifndef __SUBDOMAIN_H
#define __SUBDOMAIN_H

/* defn of iattr */
#include <linux/fs.h>

#include "immunix.h"

extern int subdomain_debug;	/* 0 or 1 */
extern int subdomain_complain;	/* 0 or 1 */
extern int subdomain_audit;	/* 0 or 1 */

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
			printk(KERN_DEBUG "SubDomain: " fmt, ##args);	\
	} while (0)
#define SD_INFO(fmt, args...)	printk(KERN_INFO "SubDomain: " fmt, ##args)
#define SD_WARN(fmt, args...)	printk(KERN_WARNING "SubDomain: " fmt, ##args)
#define SD_ERROR(fmt, args...)	printk(KERN_ERR "SubDomain: " fmt, ##args)

/* basic SubDomain data structures */

struct flagval {
	int debug;
	int complain;
	int audit;
};

/**
 * sd_entry - file ACL *
 * Each SubDomain entry describes a file and an allowed access mode.
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

#define SD_EXEC_MODIFIER_MASK(mask) ((mask) & (SD_EXEC_UNCONSTRAINED |\
		      		    SD_EXEC_INHERIT |\
		      		    SD_EXEC_PROFILE))

#define SD_EXEC_MASK(mask) ((mask) & (SD_MAY_EXEC |\
		      		    SD_EXEC_UNCONSTRAINED |\
		      		    SD_EXEC_INHERIT |\
		      		    SD_EXEC_PROFILE))

/**
 * sdprofile - basic confinement data
 *
 * The SubDomain profile contains the basic confinement data.  Each profile
 * has a name and potentially a list of SubDomain and NetDomain entries
 * (files and network access control information).  The profiles are
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

/* main.c */
extern char *__sd_get_name(struct dentry *dentry, struct vfsmount *mnt);
extern int sd_attr(struct subdomain *sd, struct dentry *dentry,
		   struct iattr *iattr);
extern int sd_xattr(struct subdomain *sd, struct dentry *dentry,
		    const char *attr, int flags);
extern int sd_capability(struct subdomain *sd, int cap);
extern int sd_perm(struct subdomain *sd, struct dentry *dentry,
		   struct vfsmount *mnt, int mask);
extern int sd_perm_nameidata(struct subdomain *sd, struct nameidata *nd,
			     int mask);
extern int sd_perm_dentry(struct subdomain *sd, struct dentry *dentry,
			  int mask);
extern int sd_file_perm(struct subdomain *sd, const char *name,
			int mask, int log);
extern int sd_link(struct subdomain *sd,
		   struct dentry *link, struct dentry *target);
extern int sd_fork(struct task_struct *p);
extern int sd_register(struct file *file);
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
extern int sd_subdomainlist_remove(struct subdomain *);
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

/* subdomainfs.c */
extern int create_subdomainfs(void);
extern int destroy_subdomainfs(void);

/* capabilities.c */
extern const char *capability_to_name(unsigned int cap);

/* subdomain_version.c */
extern const char *subdomain_version(void);
extern const char *subdomain_version_nl(void);

#endif				/* __SUBDOMAIN_H */
