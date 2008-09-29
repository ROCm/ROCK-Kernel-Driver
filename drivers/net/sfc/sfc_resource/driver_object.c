/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains support for the global driver variables.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 * Certain parts of the driver were implemented by
 *          Alexandra Kossovsky <Alexandra.Kossovsky@oktetlabs.ru>
 *          OKTET Labs Ltd, Russia,
 *          http://oktetlabs.ru, <info@oktetlabs.ru>
 *          by request of Solarflare Communications
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <ci/efrm/nic_table.h>
#include <ci/efrm/resource.h>
#include <ci/efrm/debug.h>
#include <ci/efrm/efrm_client.h>
#include <ci/efrm/efrm_nic.h>
#include "efrm_internal.h"

/* We use #define rather than static inline here so that the Windows
 * "prefast" compiler can see its own locking primitive when these
 * two function are used (and then perform extra checking where they
 * are used)
 *
 * Both macros operate on an irq_flags_t
*/

#define efrm_driver_lock(irqlock_state) \
	spin_lock_irqsave(&efrm_nic_tablep->lock, irqlock_state)

#define efrm_driver_unlock(irqlock_state)		\
	spin_unlock_irqrestore(&efrm_nic_tablep->lock,	\
			       irqlock_state);

/* These routines are all methods on the architecturally singleton
   global variables: efrm_nic_table, efrm_rm_table.

   I hope we never find a driver model that does not allow global
   structure variables :) (but that would break almost every driver I've
   ever seen).
*/

/*! Exported driver state */
static struct efrm_nic_table efrm_nic_table;
struct efrm_nic_table *efrm_nic_tablep;
EXPORT_SYMBOL(efrm_nic_tablep);


/* Internal table with resource managers.
 * We'd like to not export it, but we are still using efrm_rm_table
 * in the char driver. So, it is declared in the private header with
 * a purpose. */
struct efrm_resource_manager *efrm_rm_table[EFRM_RESOURCE_NUM];
EXPORT_SYMBOL(efrm_rm_table);


/* List of registered nics. */
static LIST_HEAD(efrm_nics);


void efrm_driver_ctor(void)
{
	efrm_nic_tablep = &efrm_nic_table;
	spin_lock_init(&efrm_nic_tablep->lock);
	EFRM_TRACE("%s: driver created", __func__);
}

void efrm_driver_dtor(void)
{
	EFRM_ASSERT(!efrm_nic_table_held());

	spin_lock_destroy(&efrm_nic_tablep->lock);
	memset(&efrm_nic_table, 0, sizeof(efrm_nic_table));
	memset(&efrm_rm_table, 0, sizeof(efrm_rm_table));
	EFRM_TRACE("%s: driver deleted", __func__);
}

int efrm_driver_register_nic(struct efrm_nic *rnic, int nic_index,
			     int ifindex)
{
	struct efhw_nic *nic = &rnic->efhw_nic;
	struct efrm_nic_per_vi *vis;
	int max_vis, rc = 0;
	irq_flags_t lock_flags;

	EFRM_ASSERT(nic_index >= 0);
	EFRM_ASSERT(ifindex >= 0);

	max_vis = 4096; /* TODO: Get runtime value. */
	vis = vmalloc(max_vis * sizeof(rnic->vis[0]));
	if (vis == NULL) {
		EFRM_ERR("%s: Out of memory", __func__);
		return -ENOMEM;
	}

	efrm_driver_lock(lock_flags);

	if (efrm_nic_table_held()) {
		EFRM_ERR("%s: driver object is in use", __func__);
		rc = -EBUSY;
		goto done;
	}

	if (efrm_nic_tablep->nic_count == EFHW_MAX_NR_DEVS) {
		EFRM_ERR("%s: filled up NIC table size %d", __func__,
			 EFHW_MAX_NR_DEVS);
		rc = -E2BIG;
		goto done;
	}

	rnic->vis = vis;

	EFRM_ASSERT(efrm_nic_tablep->nic[nic_index] == NULL);
	efrm_nic_tablep->nic[nic_index] = nic;
	nic->index = nic_index;
	nic->ifindex = ifindex;

	if (efrm_nic_tablep->a_nic == NULL)
		efrm_nic_tablep->a_nic = nic;

	efrm_nic_tablep->nic_count++;

	INIT_LIST_HEAD(&rnic->clients);
	list_add(&rnic->link, &efrm_nics);

	efrm_driver_unlock(lock_flags);
	return 0;

done:
	efrm_driver_unlock(lock_flags);
	vfree(vis);
	return rc;
}

int efrm_driver_unregister_nic(struct efrm_nic *rnic)
{
	struct efhw_nic *nic = &rnic->efhw_nic;
	int rc = 0;
	int nic_index = nic->index;
	irq_flags_t lock_flags;

	EFRM_ASSERT(nic_index >= 0);

	efrm_driver_lock(lock_flags);

	if (efrm_nic_table_held()) {
		EFRM_ERR("%s: driver object is in use", __func__);
		rc = -EBUSY;
		goto done;
	}
	if (!list_empty(&rnic->clients)) {
		EFRM_ERR("%s: nic has active clients", __func__);
		rc = -EBUSY;
		goto done;
	}

	EFRM_ASSERT(efrm_nic_tablep->nic[nic_index] == nic);
	EFRM_ASSERT(list_empty(&rnic->clients));

	list_del(&rnic->link);

	nic->index = -1;
	efrm_nic_tablep->nic[nic_index] = NULL;

	--efrm_nic_tablep->nic_count;

	if (efrm_nic_tablep->a_nic == nic) {
		if (efrm_nic_tablep->nic_count == 0) {
			efrm_nic_tablep->a_nic = NULL;
		} else {
			for (nic_index = 0; nic_index < EFHW_MAX_NR_DEVS;
			     nic_index++) {
				if (efrm_nic_tablep->nic[nic_index] != NULL)
					efrm_nic_tablep->a_nic =
					    efrm_nic_tablep->nic[nic_index];
			}
			EFRM_ASSERT(efrm_nic_tablep->a_nic);
		}
	}

done:
	efrm_driver_unlock(lock_flags);
	return rc;
}


int efrm_nic_pre_reset(struct efhw_nic *nic)
{
	struct efrm_nic *rnic = efrm_nic(nic);
	struct efrm_client *client;
	struct efrm_resource *rs;
	struct list_head *client_link;
	struct list_head *rs_link;
	irq_flags_t lock_flags;

	spin_lock_irqsave(&efrm_nic_tablep->lock, lock_flags);
	list_for_each(client_link, &rnic->clients) {
		client = container_of(client_link, struct efrm_client, link);
		EFRM_ERR("%s: client %p", __func__, client);
		if (client->callbacks->pre_reset)
			client->callbacks->pre_reset(client, client->user_data);
		list_for_each(rs_link, &client->resources) {
			rs = container_of(rs_link, struct efrm_resource,
					  rs_client_link);
			EFRM_ERR("%s: resource %p", __func__, rs);
			/* TODO: mark rs defunct */
		}
	}
	spin_unlock_irqrestore(&efrm_nic_tablep->lock, lock_flags);

	return 0;
}


int efrm_nic_stop(struct efhw_nic *nic)
{
	/* TODO */
	return 0;
}


int efrm_nic_resume(struct efhw_nic *nic)
{
	/* TODO */
	return 0;
}


static void efrm_client_nullcb(struct efrm_client *client, void *user_data)
{
}

static struct efrm_client_callbacks efrm_null_callbacks = {
	efrm_client_nullcb,
	efrm_client_nullcb,
	efrm_client_nullcb
};


int efrm_client_get(int ifindex, struct efrm_client_callbacks *callbacks,
		    void *user_data, struct efrm_client **client_out)
{
	struct efrm_nic *n, *rnic = NULL;
	irq_flags_t lock_flags;
	struct list_head *link;
	struct efrm_client *client;

	if (callbacks == NULL)
		callbacks = &efrm_null_callbacks;

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;

	spin_lock_irqsave(&efrm_nic_tablep->lock, lock_flags);
	list_for_each(link, &efrm_nics) {
		n = container_of(link, struct efrm_nic, link);
		if (n->efhw_nic.ifindex == ifindex || ifindex < 0) {
			rnic = n;
			break;
		}
	}
	if (rnic) {
		client->user_data = user_data;
		client->callbacks = callbacks;
		client->nic = &rnic->efhw_nic;
		client->ref_count = 1;
		INIT_LIST_HEAD(&client->resources);
		list_add(&client->link, &rnic->clients);
	}
	spin_unlock_irqrestore(&efrm_nic_tablep->lock, lock_flags);

	if (rnic == NULL)
		return -ENODEV;

	*client_out = client;
	return 0;
}
EXPORT_SYMBOL(efrm_client_get);


void efrm_client_put(struct efrm_client *client)
{
	irq_flags_t lock_flags;

	EFRM_ASSERT(client->ref_count > 0);

	spin_lock_irqsave(&efrm_nic_tablep->lock, lock_flags);
	if (--client->ref_count > 0)
		client = NULL;
	else
		list_del(&client->link);
	spin_unlock_irqrestore(&efrm_nic_tablep->lock, lock_flags);
	kfree(client);
}
EXPORT_SYMBOL(efrm_client_put);


struct efhw_nic *efrm_client_get_nic(struct efrm_client *client)
{
	return client->nic;
}
EXPORT_SYMBOL(efrm_client_get_nic);
