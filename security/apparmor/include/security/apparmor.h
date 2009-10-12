/*
 * AppArmor security module
 *
 * This file contains AppArmor basic global and lib definitions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __APPARMOR_H
#define __APPARMOR_H

#include <linux/fs.h>

/* Control parameters settable thru module/boot flags or
 * via /sys/kernel/securitysecurity/apparmor/control */
extern enum audit_mode g_apparmor_audit;
extern int g_apparmor_audit_header;
extern int g_apparmor_debug;
extern int g_apparmor_lock_policy;
extern int g_apparmor_logsyscall;
extern unsigned int g_apparmor_path_max;


/*
 * DEBUG remains global (no per profile flag) since it is mostly used in sysctl
 * which is not related to profile accesses.
 */

#define AA_DEBUG(fmt, args...)						\
	do {								\
		if (g_apparmor_debug && printk_ratelimit())		\
			printk(KERN_DEBUG "AppArmor: " fmt, ##args);	\
	} while (0)

#define AA_ERROR(fmt, args...)						\
	do {								\
		if (printk_ratelimit())					\
			printk(KERN_ERR "AppArmor: " fmt, ##args);	\
	} while (0)

/* Flag indicating whether initialization completed */
extern int apparmor_initialized;
void apparmor_disable(void);

/* fn's in lib */
void info_message(const char *str);
char *aa_split_name_from_ns(char *args, char **ns_name);
char *new_compound_name(const char *n1, const char *n2);
int aa_strneq(const char *str, const char *sub, int len);
char *strchrnul(const char *s, int c);
const char *fqname_subname(const char *name);

static inline int mediated_filesystem(struct inode *inode)
{
	return !(inode->i_sb->s_flags & MS_NOUSER);
}

#endif	/* __APPARMOR_H */

