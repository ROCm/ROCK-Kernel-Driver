/* volume.h: AFS volume management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_VOLUME_H
#define _LINUX_AFS_VOLUME_H

#include "types.h"
#include "fsclient.h"
#include "kafstimod.h"
#include "kafsasyncd.h"
#include "cache-layout.h"

#define __packed __attribute__((packed))

typedef enum {
	AFS_VLUPD_SLEEP,		/* sleeping waiting for update timer to fire */
	AFS_VLUPD_PENDING,		/* on pending queue */
	AFS_VLUPD_INPROGRESS,		/* op in progress */
	AFS_VLUPD_BUSYSLEEP,		/* sleeping because server returned EBUSY */
	
} __attribute__((packed)) afs_vlocation_upd_t;

/*****************************************************************************/
/*
 * AFS volume location record
 */
struct afs_vlocation
{
	atomic_t		usage;
	struct list_head	link;		/* link in cell volume location list */
	afs_timer_t		timeout;	/* decaching timer */
	afs_cell_t		*cell;		/* cell to which volume belongs */
	struct list_head	caches;		/* backing caches */
	afsc_vldb_record_t	vldb;		/* volume information DB record */
	struct afs_volume	*vols[3];	/* volume access record pointer (index by type) */
	rwlock_t		lock;		/* access lock */
	unsigned long		read_jif;	/* time at which last read from vlserver */
	afs_timer_t		upd_timer;	/* update timer */
	afs_async_op_t		upd_op;		/* update operation */
	afs_vlocation_upd_t	upd_state;	/* update state */
	unsigned short		upd_first_svix;	/* first server index during update */
	unsigned short		upd_curr_svix;	/* current server index during update */
	unsigned short		upd_rej_cnt;	/* ENOMEDIUM count during update */
	unsigned short		upd_busy_cnt;	/* EBUSY count during update */
	unsigned short		valid;		/* T if valid */
};

extern int afs_vlocation_lookup(afs_cell_t *cell, const char *name, afs_vlocation_t **_vlocation);

#define afs_get_vlocation(V) do { atomic_inc(&(V)->usage); } while(0)

extern void __afs_put_vlocation(afs_vlocation_t *vlocation);
extern void afs_put_vlocation(afs_vlocation_t *vlocation);
extern void afs_vlocation_do_timeout(afs_vlocation_t *vlocation);

/*****************************************************************************/
/*
 * AFS volume access record
 */
struct afs_volume
{
	atomic_t		usage;
	afs_cell_t		*cell;		/* cell to which belongs (unrefd ptr) */
	afs_vlocation_t		*vlocation;	/* volume location */
	afs_volid_t		vid;		/* volume ID */
	afs_voltype_t __packed	type;		/* type of volume */
	char			type_force;	/* force volume type (suppress R/O -> R/W) */
	unsigned short		nservers;	/* number of server slots filled */
	unsigned short		rjservers;	/* number of servers discarded due to -ENOMEDIUM */
	afs_server_t		*servers[8];	/* servers on which volume resides (ordered) */
	struct rw_semaphore	server_sem;	/* lock for accessing current server */
};

extern int afs_volume_lookup(char *name, int ro, afs_volume_t **_volume);

#define afs_get_volume(V) do { atomic_inc(&(V)->usage); } while(0)

extern void afs_put_volume(afs_volume_t *volume);

extern int afs_volume_pick_fileserver(afs_volume_t *volume, afs_server_t **_server);

extern int afs_volume_release_fileserver(afs_volume_t *volume, afs_server_t *server, int result);

#endif /* _LINUX_AFS_VOLUME_H */
