/*
 * Immunix SubDomain
 *
 *  Original 2.2 work
 *  Copyright 1998, 1999, 2000, 2001 Wirex Communications &
 *			Oregon Graduate Institute
 * 
 * 	Written by Steve Beattie <steve@wirex.net>
 *
 * Updated 2.4/5 work
 * Copyright (C) 2002 WireX Communications, Inc.
 *
 * Ported from 2.2 by Chris Wright <chris@wirex.com>
 *
 */

#ifndef __SUBDOMAIN_H
#define __SUBDOMAIN_H

#define __INLINE__ inline

// #define SUBDOMAIN_FS

/* for IFNAMSIZ */
#include <linux/if.h>
#include <linux/fs.h>

#include "immunix.h"
#include "pcre_exec.h"

extern int subdomain_debug;	/* 0 or 1 */
extern int subdomain_complain;  /* 0 or 1 */
extern int subdomain_audit;	/* 0 or 1 */
extern int subdomain_owlsm;	/* 0 or 1 */


#define SD_UNCONSTRAINED "unconstrained"

typedef char ifname_t[IFNAMSIZ];

#define PROFILE_COMPLAIN(_profile) (subdomain_complain == 1 || ((_profile) && (_profile)->flags.complain))

#define SUBDOMAIN_COMPLAIN(_sd) (subdomain_complain == 1 || ((_sd) && (_sd)->active && (_sd)->active->flags.complain))

#define SUBDOMAIN_AUDIT(_sd) (subdomain_audit == 1 || ((_sd) && (_sd)->active && (_sd)->active->flags.audit))

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

#define ND_DEBUG(fmt, args...)						\
	do {								\
		if (subdomain_debug)					\
			printk(KERN_DEBUG "NetDomain: " fmt, ##args);	\
	} while (0)
#define ND_INFO(fmt, args...)	printk(KERN_INFO "NetDomain: " fmt, ##args)
#define ND_WARN(fmt, args...)	printk(KERN_WARNING "NetDomain: " fmt, ##args)
#define ND_ERROR(fmt, args...)	printk(KERN_ERR "NetDomain: " fmt, ##args)

// Lock protecting access to 'struct subdomain' accesses
extern rwlock_t sd_lock;
#define SD_RLOCK read_lock(&sd_lock)
#define SD_RUNLOCK read_unlock(&sd_lock)
#define SD_WLOCK write_lock(&sd_lock)
#define SD_WUNLOCK write_unlock(&sd_lock)

/* basic SubDomain data structures */

/**
 * sd_entry - file ACL *
 * Each SubDomain entry describes a file and an allowed access mode.
 */
struct sd_entry {
	char		*filename;
	int		mode;	/* mode is 'or' of READ, WRITE, EXECUTE,
			 	 * INHERIT, UNCONSTRAINED, and LIBRARY
			 	 * (meaning don't prefetch). */

	pattern_t 	pattern_type;
	char		*regex;
	pcre		*compiled;

	struct sd_entry	*next;
	struct sd_entry *nextp[POS_KERN_COD_FILE_MAX + 1];
};

#define SD_EXEC_MODIFIER_MASK(mask) ((mask) & (KERN_COD_EXEC_UNCONSTRAINED |\
		      		    KERN_COD_EXEC_INHERIT |\
		      		    KERN_COD_EXEC_PROFILE))

#define SD_EXEC_MASK(mask) ((mask) & (KERN_COD_MAY_EXEC |\
		      		    KERN_COD_EXEC_UNCONSTRAINED |\
		      		    KERN_COD_EXEC_INHERIT |\
		      		    KERN_COD_EXEC_PROFILE))

/**
 * nd_entry - network ACL
 *
 * Each NetDomain entry decribes a network based ACL.  An entry is
 * similar to an ipchains entry.
 */
struct nd_entry {
	__u32		saddr, smask;	/* source address and mask */
	__u32		daddr, dmask;	/* dest address and mask */
	__u16		src_port[2], dst_port[2]; /* Network byte ordered */
	char		*iface;		/* interface */
	int		mode;		/* mode...err, what are the modes? */
	struct nd_entry	*next;
	struct nd_entry *nextp[POS_KERN_COD_NET_MAX - POS_KERN_COD_NET_MIN + 1];
};

#define NET_POS_TO_INDEX(x)	(x - POS_KERN_COD_NET_MIN)

/**
 * sdprofile - basic confinement data
 *
 * The SubDomain profile contains the basic confinement data.  Each profile
 * has a name and potentially a list of SubDomain and NetDomain entries
 * (files and network access control information).  The profiles are
 * connected in a list
 */
struct sdprofile {
	char		*name;		/* profile name */
	char		*sub_name;	/* XXX WTF? */

	struct sd_entry	*file_entry;	/* file ACL */
	struct sd_entry *file_entryp[POS_KERN_COD_FILE_MAX + 1];
	struct nd_entry	*net_entry;	/* network ACL */
	struct nd_entry *net_entryp[POS_KERN_COD_NET_MAX - POS_KERN_COD_NET_MIN + 1];
	struct list_head list;		/* list of profiles */
	struct list_head sub;		/* sub profiles, for change_hat */
	struct flagval   flags;		/* per profile debug flags */

	int	isstale;		/* is profile stale */

	int	num_file_entries;	
	int	num_file_pentries[POS_KERN_COD_FILE_MAX + 1];
	int	num_net_entries; 
	int	num_net_pentries[POS_KERN_COD_NET_MAX - POS_KERN_COD_NET_MIN + 1];

	kernel_cap_t	capabilities;

	atomic_t	count;		/* reference count */
};

/**
 * subdomain - a task's subdomain
 *
 * Contains the original profile obtained from execve() as well as the
 * current active profile (which could change due to change_hat).  Plus
 * the hat_magic needed during change_hat.
 */
struct subdomain {
	__u32		sd_magic;	/* magic value to distinguish blobs */
	struct sdprofile *profile;	/* The profile obtained from execve() */
	struct sdprofile *active;	/* The current active profile */
	__u32		sd_hat_magic;	/* used with change_hat */
	struct list_head list;		/* list of subdomains */
	struct task_struct *task;
};

typedef int (*sd_iter)(struct subdomain *, void *);

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

extern struct sdprofile null_profile;
extern struct sdprofile *null_complain_profile;

/* main.c */
extern char * __sd_get_name(struct dentry *dentry, struct vfsmount *mnt);
extern int __nd_network_perm (struct task_struct *tsk, struct subdomain *sd, 
		__u32 saddr, __u16 sport, __u32 daddr, __u16 dport, 
		char * ifname, int mode);

extern int nd_network_perm (struct subdomain *sd, 
		__u32 saddr, __u16 sport, __u32 daddr, __u16 dport,
		char * ifname, int mode);
extern int sd_attr(struct dentry *dentry, struct subdomain *sd,  struct iattr *iattr);
extern int sd_capability(int cap, struct subdomain *sd);
extern int sd_perm(struct inode *inode, struct subdomain *sd, struct nameidata *nd, int mask);
extern int sd_perm_dentry(struct dentry *dentry, struct subdomain *sd, int mask);
extern int sd_file_perm(const char *name, struct subdomain *sd, int mask, BOOL log);
extern int sd_link(struct dentry *link, struct dentry *target, struct subdomain *sd);
extern int sd_symlink(struct dentry *link, const char *name, struct subdomain *sd);
extern int sd_fork(struct task_struct *p);
extern int sd_register(struct file *file);
extern void sd_release(struct task_struct *p);
extern int sd_change_hat(const char *id, __u32 hat_magic);
extern int sd_associate_filp(struct file *filp);

/* list.c */
extern struct sdprofile * sd_profilelist_find(const char *name);
extern void sd_profilelist_add(struct sdprofile *profile);
extern int sd_profilelist_remove(const char *name);
extern void sd_profilelist_release(void);
extern struct sdprofile * sd_profilelist_replace(struct sdprofile *profile);
extern void sd_profile_dump(struct sdprofile *);
extern void sd_profilelist_dump(void);
extern void sd_subdomainlist_add(struct subdomain *);
extern int sd_subdomainlist_remove(struct subdomain *);
extern void sd_subdomainlist_iterate(sd_iter, void *);
extern void sd_subdomainlist_iterateremove(sd_iter, void *);
extern void sd_subdomainlist_release(void);

/* sysctl.c */
extern void free_sdprofile(struct sdprofile *profile);
extern int sd_sys_security(unsigned int id, unsigned call, unsigned long *args);
extern int sd_setprocattr_changehat(char *hatinfo, size_t infosize);
extern int sd_setprocattr_setprofile(struct task_struct *p, char *profilename, size_t profilesize);

/* subdomainfs.c */
extern struct inode * sd_new_inode(struct super_block *sb, int mode, unsigned long ino);
extern int sd_fill_root(struct dentry * root);

/* capabilities.c */
extern char * capability_to_name(int cap);

/* subdomain_version.c */
extern const char *subdomain_version(void);
extern const char *subdomain_version_nl(void);

#if defined (PRINTK_TEMPFIX) && ( defined (CONFIG_SMP) || defined (CONFIG_PREEMPT))
extern volatile int sd_log_buf_has_data;
extern void dump_sdprintk (void);
#endif

#endif // __SUBDOMAIN_H
