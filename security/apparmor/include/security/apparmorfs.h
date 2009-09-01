/*
 * AppArmor security module
 *
 * This file contains AppArmor filesystem definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_APPARMORFS_H
#define __AA_APPARMORFS_H

extern struct dentry *apparmorfs_null;
extern struct vfsmount *apparmorfs_mnt;

extern int create_apparmorfs(void);
extern void destroy_apparmorfs(void);

#endif	/* __AA_APPARMORFS_H */
