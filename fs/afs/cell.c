/* cell.c: AFS cell and server record management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <rxrpc/peer.h>
#include <rxrpc/connection.h>
#include "volume.h"
#include "cell.h"
#include "server.h"
#include "transport.h"
#include "vlclient.h"
#include "kafstimod.h"
#include "super.h"
#include "internal.h"

DECLARE_RWSEM(afs_proc_cells_sem);
LIST_HEAD(afs_proc_cells);

static struct list_head afs_cells = LIST_HEAD_INIT(afs_cells);
static rwlock_t afs_cells_lock = RW_LOCK_UNLOCKED;
static DECLARE_RWSEM(afs_cells_sem); /* add/remove serialisation */
static afs_cell_t *afs_cell_root;

static char *rootcell;

MODULE_PARM(rootcell,"s");
MODULE_PARM_DESC(rootcell,"root AFS cell name and VL server IP addr list");

/*****************************************************************************/
/*
 * create a cell record
 * - "name" is the name of the cell
 * - "vllist" is a colon separated list of IP addresses in "a.b.c.d" format
 */
int afs_cell_create(const char *name, char *vllist, afs_cell_t **_cell)
{
	afs_cell_t *cell;
	char *next;
	int ret;

	_enter("%s",name);

	if (!name) BUG(); /* TODO: want to look up "this cell" in the cache */

	down_write(&afs_cells_sem);

	/* allocate and initialise a cell record */
	cell = kmalloc(sizeof(afs_cell_t) + strlen(name) + 1,GFP_KERNEL);
	if (!cell) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	memset(cell,0,sizeof(afs_cell_t));
	atomic_set(&cell->usage,0);

	INIT_LIST_HEAD(&cell->link);
	INIT_LIST_HEAD(&cell->caches);

	rwlock_init(&cell->sv_lock);
	INIT_LIST_HEAD(&cell->sv_list);
	INIT_LIST_HEAD(&cell->sv_graveyard);
	spin_lock_init(&cell->sv_gylock);

	init_rwsem(&cell->vl_sem);
	INIT_LIST_HEAD(&cell->vl_list);
	INIT_LIST_HEAD(&cell->vl_graveyard);
	spin_lock_init(&cell->vl_gylock);

	strcpy(cell->name,name);

	/* fill in the VL server list from the rest of the string */
	ret = -EINVAL;
	do {
		unsigned a, b, c, d;

		next = strchr(vllist,':');
		if (next) *next++ = 0;

		if (sscanf(vllist,"%u.%u.%u.%u",&a,&b,&c,&d)!=4)
			goto badaddr;

		if (a>255 || b>255 || c>255 || d>255)
			goto badaddr;

		cell->vl_addrs[cell->vl_naddrs++].s_addr =
			htonl((a<<24)|(b<<16)|(c<<8)|d);

		if (cell->vl_naddrs>=16)
			break;

	} while(vllist=next, vllist);

	/* add a proc dir for this cell */
	ret = afs_proc_cell_setup(cell);
	if (ret<0)
		goto error;

	/* add to the cell lists */
	write_lock(&afs_cells_lock);
	list_add_tail(&cell->link,&afs_cells);
	write_unlock(&afs_cells_lock);

	down_write(&afs_proc_cells_sem);
	list_add_tail(&cell->proc_link,&afs_proc_cells);
	up_write(&afs_proc_cells_sem);

	*_cell = cell;
	up_write(&afs_cells_sem);

	_leave(" = 0 (%p)",cell);
	return 0;

 badaddr:
	printk("kAFS: bad VL server IP address: '%s'\n",vllist);
 error:
	up_write(&afs_cells_sem);
	kfree(afs_cell_root);
	return ret;
} /* end afs_cell_create() */

/*****************************************************************************/
/*
 * initialise the cell database from module parameters
 */
int afs_cell_init(void)
{
	char *cp;
	int ret;

	_enter("");

	if (!rootcell) {
		printk("kAFS: no root cell specified\n");
		return -EINVAL;
	}

	cp = strchr(rootcell,':');
	if (!cp) {
		printk("kAFS: no VL server IP addresses specified\n");
		return -EINVAL;
	}

	/* allocate a cell record for the root cell */
	*cp++ = 0;
	ret = afs_cell_create(rootcell,cp,&afs_cell_root);
	if (ret==0)
		afs_get_cell(afs_cell_root);

	_leave(" = %d",ret);
	return ret;

} /* end afs_cell_init() */

/*****************************************************************************/
/*
 * lookup a cell record
 */
int afs_cell_lookup(const char *name, afs_cell_t **_cell)
{
	struct list_head *_p;
	afs_cell_t *cell;

	_enter("\"%s\",",name?name:"*thiscell*");

	cell = afs_cell_root;

	if (name) {
		/* if the cell was named, look for it in the cell record list */
		cell = NULL;
		read_lock(&afs_cells_lock);

		list_for_each(_p,&afs_cells) {
			cell = list_entry(_p,afs_cell_t,link);
			if (strcmp(cell->name,name)==0)
				break;
			cell = NULL;
		}

		read_unlock(&afs_cells_lock);
	}

	if (cell)
		afs_get_cell(cell);

	*_cell = cell;
	_leave(" = %d (%p)",cell?0:-ENOENT,cell);
	return cell ? 0 : -ENOENT;

} /* end afs_cell_lookup() */

/*****************************************************************************/
/*
 * try and get a cell record
 */
afs_cell_t *afs_get_cell_maybe(afs_cell_t **_cell)
{
	afs_cell_t *cell;

	write_lock(&afs_cells_lock);

	cell = *_cell;
	if (cell && !list_empty(&cell->link))
		atomic_inc(&cell->usage);
	else 
		cell = NULL;

	write_unlock(&afs_cells_lock);

	return cell;
} /* end afs_get_cell_maybe() */

/*****************************************************************************/
/*
 * destroy a cell record
 */
void afs_put_cell(afs_cell_t *cell)
{
	_enter("%p{%d,%s}",cell,atomic_read(&cell->usage),cell->name);

	/* sanity check */
	if (atomic_read(&cell->usage)<=0)
		BUG();

	/* to prevent a race, the decrement and the dequeue must be effectively atomic */
	write_lock(&afs_cells_lock);

	if (likely(!atomic_dec_and_test(&cell->usage))) {
		write_unlock(&afs_cells_lock);
		_leave("");
		return;
	}

	write_unlock(&afs_cells_lock);

	if (!list_empty(&cell->sv_list))	BUG();
	if (!list_empty(&cell->sv_graveyard))	BUG();
	if (!list_empty(&cell->vl_list))	BUG();
	if (!list_empty(&cell->vl_graveyard))	BUG();

	_leave(" [unused]");
} /* end afs_put_cell() */

/*****************************************************************************/
/*
 * destroy a cell record
 */
static void afs_cell_destroy(afs_cell_t *cell)
{
	_enter("%p{%d,%s}",cell,atomic_read(&cell->usage),cell->name);

	/* to prevent a race, the decrement and the dequeue must be effectively atomic */
	write_lock(&afs_cells_lock);

	/* sanity check */
	if (atomic_read(&cell->usage)!=0)
		BUG();

	list_del_init(&cell->link);

	write_unlock(&afs_cells_lock);

	down_write(&afs_cells_sem);

	afs_proc_cell_remove(cell);

	down_write(&afs_proc_cells_sem);
	list_del_init(&cell->proc_link);
	up_write(&afs_proc_cells_sem);

	up_write(&afs_cells_sem);

	if (!list_empty(&cell->sv_list))	BUG();
	if (!list_empty(&cell->sv_graveyard))	BUG();
	if (!list_empty(&cell->vl_list))	BUG();
	if (!list_empty(&cell->vl_graveyard))	BUG();

	/* finish cleaning up the cell */
	kfree(cell);

	_leave(" [destroyed]");
} /* end afs_cell_destroy() */

/*****************************************************************************/
/*
 * lookup the server record corresponding to an Rx RPC peer
 */
int afs_server_find_by_peer(const struct rxrpc_peer *peer, afs_server_t **_server)
{
	struct list_head *_pc, *_ps;
	afs_server_t *server;
	afs_cell_t *cell;

	_enter("%p{a=%08x},",peer,ntohl(peer->addr.s_addr));

	/* search the cell list */
	read_lock(&afs_cells_lock);

	list_for_each(_pc,&afs_cells) {
		cell = list_entry(_pc,afs_cell_t,link);

		_debug("? cell %s",cell->name);

		write_lock(&cell->sv_lock);

		/* check the active list */
		list_for_each(_ps,&cell->sv_list) {
			server = list_entry(_ps,afs_server_t,link);

			_debug("?? server %08x",ntohl(server->addr.s_addr));

			if (memcmp(&server->addr,&peer->addr,sizeof(struct in_addr))==0)
				goto found_server;
		}

		/* check the inactive list */
		spin_lock(&cell->sv_gylock);
		list_for_each(_ps,&cell->sv_graveyard) {
			server = list_entry(_ps,afs_server_t,link);

			_debug("?? dead server %08x",ntohl(server->addr.s_addr));

			if (memcmp(&server->addr,&peer->addr,sizeof(struct in_addr))==0)
				goto found_dead_server;
		}
		spin_unlock(&cell->sv_gylock);

		write_unlock(&cell->sv_lock);
	}
	read_unlock(&afs_cells_lock);

	_leave(" = -ENOENT");
	return -ENOENT;

	/* we found it in the graveyard - resurrect it */
 found_dead_server:
	list_del(&server->link);
	list_add_tail(&server->link,&cell->sv_list);
	afs_get_server(server);
	afs_kafstimod_del_timer(&server->timeout);
	spin_unlock(&cell->sv_gylock);
	goto success;

	/* we found it - increment its ref count and return it */
 found_server:
	afs_get_server(server);

 success:
	write_unlock(&cell->sv_lock);
	read_unlock(&afs_cells_lock);

	*_server = server;
	_leave(" = 0 (s=%p c=%p)",server,cell);
	return 0;

} /* end afs_server_find_by_peer() */

/*****************************************************************************/
/*
 * purge in-memory cell database on module unload
 * - the timeout daemon is stopped before calling this
 */
void afs_cell_purge(void)
{
	afs_vlocation_t *vlocation;
	afs_cell_t *cell;

	_enter("");

	if (afs_cell_root)
		afs_put_cell(afs_cell_root);

	while (!list_empty(&afs_cells)) {
		cell = NULL;

		/* remove the next cell from the front of the list */
		write_lock(&afs_cells_lock);

		if (!list_empty(&afs_cells)) {
			cell = list_entry(afs_cells.next,afs_cell_t,link);
			list_del_init(&cell->link);
		}

		write_unlock(&afs_cells_lock);

		if (cell) {
			_debug("PURGING CELL %s (%d)",cell->name,atomic_read(&cell->usage));

			if (!list_empty(&cell->sv_list)) BUG();
			if (!list_empty(&cell->vl_list)) BUG();

			/* purge the cell's VL graveyard list */
			_debug(" - clearing VL graveyard");

			spin_lock(&cell->vl_gylock);

			while (!list_empty(&cell->vl_graveyard)) {
				vlocation = list_entry(cell->vl_graveyard.next,
						       afs_vlocation_t,link);
				list_del_init(&vlocation->link);

				afs_kafstimod_del_timer(&vlocation->timeout);

				spin_unlock(&cell->vl_gylock);

				afs_vlocation_do_timeout(vlocation);
				/* TODO: race if move to use krxtimod instead of kafstimod */

				spin_lock(&cell->vl_gylock);
			}

			spin_unlock(&cell->vl_gylock);

			/* purge the cell's server graveyard list */
			_debug(" - clearing server graveyard");

			spin_lock(&cell->sv_gylock);

			while (!list_empty(&cell->sv_graveyard)) {
				afs_server_t *server;

				server = list_entry(cell->sv_graveyard.next,afs_server_t,link);
				list_del_init(&server->link);

				afs_kafstimod_del_timer(&server->timeout);

				spin_unlock(&cell->sv_gylock);

				afs_server_do_timeout(server);

				spin_lock(&cell->sv_gylock);
			}

			spin_unlock(&cell->sv_gylock);

			/* now the cell should be left with no references */
			afs_cell_destroy(cell);
		}
	}

	_leave("");
} /* end afs_cell_purge() */
