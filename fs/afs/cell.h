/* cell.h: AFS cell record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_CELL_H
#define _LINUX_AFS_CELL_H

#include "types.h"

extern volatile int afs_cells_being_purged; /* T when cells are being purged by rmmod */

/*****************************************************************************/
/*
 * AFS cell record
 */
struct afs_cell
{
	atomic_t		usage;
	struct list_head	link;		/* main cell list link */
	struct list_head	proc_link;	/* /proc cell list link */
	struct proc_dir_entry	*proc_dir;	/* /proc dir for this cell */
	struct list_head	caches;		/* list of caches currently backing this cell */

	/* server record management */
	rwlock_t		sv_lock;	/* active server list lock */
	struct list_head	sv_list;	/* active server list */
	struct list_head	sv_graveyard;	/* inactive server list */
	spinlock_t		sv_gylock;	/* inactive server list lock */

	/* volume location record management */
	struct rw_semaphore	vl_sem;		/* volume management serialisation semaphore */
	struct list_head	vl_list;	/* cell's active VL record list */
	struct list_head	vl_graveyard;	/* cell's inactive VL record list */
	spinlock_t		vl_gylock;	/* graveyard lock */
	unsigned short		vl_naddrs;	/* number of VL servers in addr list */
	unsigned short		vl_curr_svix;	/* current server index */
	struct in_addr		vl_addrs[16];	/* cell VL server addresses */

	char			name[0];	/* cell name - must go last */
};

extern int afs_cell_init(void);

extern int afs_cell_create(const char *name, char *vllist, afs_cell_t **_cell);

extern int afs_cell_lookup(const char *name, afs_cell_t **_cell);

#define afs_get_cell(C) do { atomic_inc(&(C)->usage); } while(0)

extern afs_cell_t *afs_get_cell_maybe(afs_cell_t **_cell);

extern void afs_put_cell(afs_cell_t *cell);

extern void afs_cell_purge(void);

#endif /* _LINUX_AFS_CELL_H */
