/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains the VI resource manager.
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
#include <ci/efrm/private.h>
#include <ci/efrm/vi_resource_private.h>
#include "efrm_internal.h"


int efrm_pt_pace(struct vi_resource *virs, unsigned int val)
{
	struct efhw_nic *nic = virs->rs.rs_client->nic;
	int instance;

	EFRM_RESOURCE_ASSERT_VALID(&virs->rs, 0);
	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
	falcon_nic_pace(nic, instance, val);
	EFRM_TRACE("%s[%d]=%d DONE", __func__, instance, val);
	return 0;
}
EXPORT_SYMBOL(efrm_pt_pace);

/*** Resource manager creation/destruction *******************************/

static void efrm_vi_rm_dtor(struct efrm_resource_manager *rm);

static int
efrm_create_or_destroy_vi_resource_manager(
				struct efrm_resource_manager **rm_in_out,
				const struct vi_resource_dimensions *dims,
				bool destroy)
{
	struct vi_resource *virs;
	struct list_head *pos, *temp;
	struct list_head flush_pending;
	irq_flags_t lock_flags;
	int rc;
	unsigned dmaq_min, dmaq_lim;

	EFRM_ASSERT(rm_in_out);

	if (destroy)
		goto destroy;

	EFRM_ASSERT(dims);
	EFRM_NOTICE("vi_resource_manager: evq_int=%u-%u evq_timer=%u-%u",
		    dims->evq_int_min, dims->evq_int_lim,
		    dims->evq_timer_min, dims->evq_timer_lim);
	EFRM_NOTICE("vi_resource_manager: rxq=%u-%u txq=%u-%u",
		    dims->rxq_min, dims->rxq_lim,
		    dims->txq_min, dims->txq_lim);

	efrm_vi_manager = kmalloc(sizeof(*efrm_vi_manager), GFP_KERNEL);
	if (efrm_vi_manager == NULL) {
		rc = -ENOMEM;
		goto fail_alloc;
	}

	memset(efrm_vi_manager, 0, sizeof(*efrm_vi_manager));

	efrm_vi_manager->iscsi_dmaq_instance_is_free = true;

	dmaq_min = max(dims->rxq_min, dims->txq_min);
	dmaq_lim = min(dims->rxq_lim, dims->txq_lim);

	efrm_vi_manager->with_timer_base =
	    max(dmaq_min, dims->evq_timer_min);
	efrm_vi_manager->with_timer_limit =
	    min(dmaq_lim, dims->evq_timer_lim);
	rc = efrm_kfifo_id_ctor(&efrm_vi_manager->instances_with_timer,
				efrm_vi_manager->with_timer_base,
				efrm_vi_manager->with_timer_limit,
				&efrm_vi_manager->rm.rm_lock);
	if (rc < 0)
		goto fail_with_timer_id_pool;

	efrm_vi_manager->with_interrupt_base =
	    max(dmaq_min, dims->evq_int_min);
	efrm_vi_manager->with_interrupt_limit =
	    min(dmaq_lim, dims->evq_int_lim);
	efrm_vi_manager->with_interrupt_limit =
		max(efrm_vi_manager->with_interrupt_limit,
		    efrm_vi_manager->with_interrupt_base);
	rc = efrm_kfifo_id_ctor(&efrm_vi_manager->instances_with_interrupt,
				efrm_vi_manager->with_interrupt_base,
				efrm_vi_manager->with_interrupt_limit,
				&efrm_vi_manager->rm.rm_lock);
	if (rc < 0)
		goto fail_with_int_id_pool;

	INIT_LIST_HEAD(&efrm_vi_manager->rx_flush_waiting_list);
	INIT_LIST_HEAD(&efrm_vi_manager->rx_flush_outstanding_list);
	INIT_LIST_HEAD(&efrm_vi_manager->tx_flush_outstanding_list);
	efrm_vi_manager->rx_flush_outstanding_count = 0;

	INIT_LIST_HEAD(&efrm_vi_manager->close_pending);
	efrm_vi_manager->workqueue = create_workqueue("sfc_vi");
	if (efrm_vi_manager->workqueue == NULL)
		goto fail_create_workqueue;
	INIT_WORK(&efrm_vi_manager->work_item, efrm_vi_rm_delayed_free);

	/* NB.  This must be the last step to avoid things getting tangled.
	 * efrm_resource_manager_dtor calls the vi_rm_dtor which ends up in
	 * this function. */
	rc = efrm_resource_manager_ctor(&efrm_vi_manager->rm, efrm_vi_rm_dtor,
					"VI", EFRM_RESOURCE_VI);
	if (rc < 0)
		goto fail_rm_ctor;

	*rm_in_out = &efrm_vi_manager->rm;
	return 0;

destroy:
	rc = 0;
	EFRM_RESOURCE_MANAGER_ASSERT_VALID(*rm_in_out);

	/* Abort outstanding flushes.  Note, a VI resource can be on more
	 * than one of these lists.  We handle this by starting with the TX
	 * list and then append VIs to this list if they aren't on the TX
	 * list already.  A VI is on the TX flush list if tx_flushing
	 * is not empty. */
	spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);

	list_replace_init(&efrm_vi_manager->tx_flush_outstanding_list,
			  &flush_pending);

	list_for_each_safe(pos, temp,
			   &efrm_vi_manager->rx_flush_waiting_list) {
		virs = container_of(pos, struct vi_resource, rx_flush_link);

		list_del(&virs->rx_flush_link);
		if (virs->tx_flushing == 0)
			list_add_tail(&virs->tx_flush_link, &flush_pending);
	}

	list_for_each_safe(pos, temp,
			   &efrm_vi_manager->rx_flush_outstanding_list) {
		virs = container_of(pos, struct vi_resource, rx_flush_link);

		list_del(&virs->rx_flush_link);
		if (virs->tx_flushing == 0)
			list_add_tail(&virs->tx_flush_link, &flush_pending);
	}

	spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock, lock_flags);

	while (!list_empty(&flush_pending)) {
		virs =
		    list_entry(list_pop(&flush_pending), struct vi_resource,
			       tx_flush_link);
		EFRM_TRACE("%s: found PT endpoint " EFRM_RESOURCE_FMT
			   " with flush pending [Tx=0x%x, Rx=0x%x, RxO=0x%x]",
			   __func__,
			   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle),
			   virs->tx_flushing,
			   virs->rx_flushing,
			   virs->rx_flush_outstanding);
		efrm_vi_rm_free_flushed_resource(virs);
	}

fail_rm_ctor:

	/* Complete outstanding closes. */
	destroy_workqueue(efrm_vi_manager->workqueue);
fail_create_workqueue:
	EFRM_ASSERT(list_empty(&efrm_vi_manager->close_pending));
	kfifo_vfree(efrm_vi_manager->instances_with_interrupt);
fail_with_int_id_pool:

	kfifo_vfree(efrm_vi_manager->instances_with_timer);
fail_with_timer_id_pool:

	if (destroy)
		return 0;

	EFRM_DO_DEBUG(memset(efrm_vi_manager, 0, sizeof(*efrm_vi_manager)));
	kfree(efrm_vi_manager);
fail_alloc:

	*rm_in_out = NULL;
	EFRM_ERR("%s: failed rc=%d", __func__, rc);
	return rc;
}

int
efrm_create_vi_resource_manager(struct efrm_resource_manager **rm_out,
				const struct vi_resource_dimensions *dims)
{
	return efrm_create_or_destroy_vi_resource_manager(rm_out, dims, false);
}

static void efrm_vi_rm_dtor(struct efrm_resource_manager *rm)
{
	efrm_create_or_destroy_vi_resource_manager(&rm, NULL, true);
}
