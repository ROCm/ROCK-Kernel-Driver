/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains filters support.
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
#include <ci/driver/efab/hardware.h>
#include <ci/efhw/falcon.h>
#include <ci/efrm/vi_resource_manager.h>
#include <ci/efrm/private.h>
#include <ci/efrm/filter.h>
#include <ci/efrm/buffer_table.h>
#include <ci/efrm/efrm_client.h>
#include "efrm_internal.h"


struct filter_resource_manager {
	struct efrm_resource_manager rm;
	struct kfifo *free_ids;
};

static struct filter_resource_manager *efrm_filter_manager;


void efrm_filter_resource_free(struct filter_resource *frs)
{
	struct efhw_nic *nic = frs->rs.rs_client->nic;
	int id;

	EFRM_RESOURCE_ASSERT_VALID(&frs->rs, 1);

	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT, __func__,
		   EFRM_RESOURCE_PRI_ARG(frs->rs.rs_handle));

	efhw_nic_ipfilter_clear(nic, frs->filter_idx);
	frs->filter_idx = -1;
	efrm_vi_resource_release(frs->pt);

	/* Free this filter. */
	id = EFRM_RESOURCE_INSTANCE(frs->rs.rs_handle);
	EFRM_VERIFY_EQ(kfifo_put(efrm_filter_manager->free_ids,
				 (unsigned char *)&id, sizeof(id)),
		       sizeof(id));

	efrm_client_put(frs->rs.rs_client);
	EFRM_DO_DEBUG(memset(frs, 0, sizeof(*frs)));
	kfree(frs);
}
EXPORT_SYMBOL(efrm_filter_resource_free);


void efrm_filter_resource_release(struct filter_resource *frs)
{
	if (__efrm_resource_release(&frs->rs))
		efrm_filter_resource_free(frs);
}
EXPORT_SYMBOL(efrm_filter_resource_release);


static void filter_rm_dtor(struct efrm_resource_manager *rm)
{
	EFRM_TRACE("%s:", __func__);

	EFRM_RESOURCE_MANAGER_ASSERT_VALID(&efrm_filter_manager->rm);
	EFRM_ASSERT(&efrm_filter_manager->rm == rm);

	kfifo_vfree(efrm_filter_manager->free_ids);
	EFRM_TRACE("%s: done", __func__);
}

/**********************************************************************/
/**********************************************************************/
/**********************************************************************/

int efrm_create_filter_resource_manager(struct efrm_resource_manager **rm_out)
{
	int rc;

	EFRM_ASSERT(rm_out);

	efrm_filter_manager =
	    kmalloc(sizeof(struct filter_resource_manager), GFP_KERNEL);
	if (efrm_filter_manager == 0)
		return -ENOMEM;
	memset(efrm_filter_manager, 0, sizeof(*efrm_filter_manager));

	rc = efrm_resource_manager_ctor(&efrm_filter_manager->rm,
					filter_rm_dtor, "FILTER",
					EFRM_RESOURCE_FILTER);
	if (rc < 0)
		goto fail1;

	/* Create a pool of free instances */
	rc = efrm_kfifo_id_ctor(&efrm_filter_manager->free_ids,
				0, EFHW_IP_FILTER_NUM,
				&efrm_filter_manager->rm.rm_lock);
	if (rc != 0)
		goto fail2;

	*rm_out = &efrm_filter_manager->rm;
	EFRM_TRACE("%s: filter resources created - %d IDs",
		   __func__, kfifo_len(efrm_filter_manager->free_ids));
	return 0;

fail2:
	efrm_resource_manager_dtor(&efrm_filter_manager->rm);
fail1:
	memset(efrm_filter_manager, 0, sizeof(*efrm_filter_manager));
	kfree(efrm_filter_manager);
	return rc;

}


int efrm_filter_resource_clear(struct filter_resource *frs)
{
	struct efhw_nic *nic = frs->rs.rs_client->nic;

	efhw_nic_ipfilter_clear(nic, frs->filter_idx);
	frs->filter_idx = -1;
	return 0;
}
EXPORT_SYMBOL(efrm_filter_resource_clear);


int
__efrm_filter_resource_set(struct filter_resource *frs, int type,
			   unsigned saddr, uint16_t sport,
			   unsigned daddr, uint16_t dport)
{
	struct efhw_nic *nic = frs->rs.rs_client->nic;
	int vi_instance;

	EFRM_ASSERT(frs);

	if (efrm_nic_tablep->a_nic->devtype.variant >= 'B' &&
	    (frs->pt->flags & EFHW_VI_JUMBO_EN) == 0)
		type |= EFHW_IP_FILTER_TYPE_NOSCAT_B0_MASK;
	vi_instance = EFRM_RESOURCE_INSTANCE(frs->pt->rs.rs_handle);

	return efhw_nic_ipfilter_set(nic, type, &frs->filter_idx,
				     vi_instance, saddr, sport, daddr, dport);
}
EXPORT_SYMBOL(__efrm_filter_resource_set);;


int
efrm_filter_resource_alloc(struct vi_resource *vi_parent,
			   struct filter_resource **frs_out)
{
	struct filter_resource *frs;
	int rc, instance;

	EFRM_ASSERT(frs_out);
	EFRM_ASSERT(efrm_filter_manager);
	EFRM_RESOURCE_MANAGER_ASSERT_VALID(&efrm_filter_manager->rm);
	EFRM_ASSERT(vi_parent != NULL);
	EFRM_ASSERT(EFRM_RESOURCE_TYPE(vi_parent->rs.rs_handle) ==
		    EFRM_RESOURCE_VI);

	/* Allocate resource data structure. */
	frs = kmalloc(sizeof(struct filter_resource), GFP_KERNEL);
	if (!frs)
		return -ENOMEM;

	/* Allocate an instance. */
	rc = kfifo_get(efrm_filter_manager->free_ids,
		       (unsigned char *)&instance, sizeof(instance));
	if (rc != sizeof(instance)) {
		EFRM_TRACE("%s: out of instances", __func__);
		EFRM_ASSERT(rc == 0);
		rc = -EBUSY;
		goto fail1;
	}

	/* Initialise the resource DS. */
	efrm_resource_init(&frs->rs, EFRM_RESOURCE_FILTER, instance);
	frs->pt = vi_parent;
	efrm_resource_ref(&frs->pt->rs);
	frs->filter_idx = -1;

	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT " VI %d", __func__,
		   EFRM_RESOURCE_PRI_ARG(frs->rs.rs_handle),
		   EFRM_RESOURCE_INSTANCE(vi_parent->rs.rs_handle));

	efrm_client_add_resource(vi_parent->rs.rs_client, &frs->rs);
	*frs_out = frs;
	return 0;

fail1:
	memset(frs, 0, sizeof(*frs));
	kfree(frs);
	return rc;
}
EXPORT_SYMBOL(efrm_filter_resource_alloc);


int efrm_filter_resource_instance(struct filter_resource *frs)
{
	return EFRM_RESOURCE_INSTANCE(frs->rs.rs_handle);
}
EXPORT_SYMBOL(efrm_filter_resource_instance);


struct efrm_resource *
efrm_filter_resource_to_resource(struct filter_resource *frs)
{
	return &frs->rs;
}
EXPORT_SYMBOL(efrm_filter_resource_to_resource);


struct filter_resource *
efrm_filter_resource_from_resource(struct efrm_resource *rs)
{
	return filter_resource(rs);
}
EXPORT_SYMBOL(efrm_filter_resource_from_resource);
