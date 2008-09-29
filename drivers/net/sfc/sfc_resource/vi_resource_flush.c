/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains DMA queue flushing of VI resources.
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
#include <ci/efrm/sysdep.h>
#include <ci/efrm/buffer_table.h>
#include <ci/efrm/vi_resource_private.h>
#include "efrm_internal.h"


/* can fail as workitem can already be scheuled -- ignore failure */
#define EFRM_VI_RM_DELAYED_FREE(manager) \
	queue_work(manager->workqueue, &manager->work_item)

static const int flush_fifo_hwm = 8 /* TODO should be a HW specific const */ ;

static void
efrm_vi_resource_rx_flush_done(struct vi_resource *virs, bool *completed)
{
	/* We should only get a flush event if there is a flush
	 * outstanding. */
	EFRM_ASSERT(virs->rx_flush_outstanding);

	virs->rx_flush_outstanding = 0;
	virs->rx_flushing = 0;

	list_del(&virs->rx_flush_link);
	efrm_vi_manager->rx_flush_outstanding_count--;

	if (virs->tx_flushing == 0) {
		list_add_tail(&virs->rx_flush_link,
			      &efrm_vi_manager->close_pending);
		*completed = 1;
	}
}

static void
efrm_vi_resource_tx_flush_done(struct vi_resource *virs, bool *completed)
{
	/* We should only get a flush event if there is a flush
	 * outstanding. */
	EFRM_ASSERT(virs->tx_flushing);

	virs->tx_flushing = 0;

	list_del(&virs->tx_flush_link);

	if (virs->rx_flushing == 0) {
		list_add_tail(&virs->rx_flush_link,
			      &efrm_vi_manager->close_pending);
		*completed = 1;
	}
}

static void
efrm_vi_resource_issue_rx_flush(struct vi_resource *virs, bool *completed)
{
	struct efhw_nic *nic = virs->rs.rs_client->nic;
	int instance;
	int rc;

	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);

	list_add_tail(&virs->rx_flush_link,
		      &efrm_vi_manager->rx_flush_outstanding_list);
	virs->rx_flush_outstanding = virs->rx_flushing;
	efrm_vi_manager->rx_flush_outstanding_count++;

	EFRM_TRACE("%s: rx queue %d flush requested for nic %d",
		   __func__, instance, nic->index);
	rc = efhw_nic_flush_rx_dma_channel(nic, instance);
	if (rc == -EAGAIN)
		efrm_vi_resource_rx_flush_done(virs, completed);
}

static void
efrm_vi_resource_issue_tx_flush(struct vi_resource *virs, bool *completed)
{
	struct efhw_nic *nic = virs->rs.rs_client->nic;
	int instance;
	int rc;

	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);

	list_add_tail(&virs->tx_flush_link,
		      &efrm_vi_manager->tx_flush_outstanding_list);

	EFRM_TRACE("%s: tx queue %d flush requested for nic %d",
		   __func__, instance, nic->index);
	rc = efhw_nic_flush_tx_dma_channel(nic, instance);
	if (rc == -EAGAIN)
		efrm_vi_resource_tx_flush_done(virs, completed);
}

static void efrm_vi_resource_process_waiting_flushes(bool *completed)
{
	struct vi_resource *virs;

	while (efrm_vi_manager->rx_flush_outstanding_count < flush_fifo_hwm &&
	       !list_empty(&efrm_vi_manager->rx_flush_waiting_list)) {
		virs =
		    list_entry(list_pop
			       (&efrm_vi_manager->rx_flush_waiting_list),
			       struct vi_resource, rx_flush_link);
		efrm_vi_resource_issue_rx_flush(virs, completed);
	}
}

#if BUG7916_WORKAROUND || BUG5302_WORKAROUND
static void
efrm_vi_resource_flush_retry_vi(struct vi_resource *virs,
				int64_t time_now, bool *completed)
{
	struct efhw_nic *nic;
	int instance;

	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);

	virs->flush_count++;
	virs->flush_time = time_now;
	nic = virs->rs.rs_client->nic;

#if BUG7916_WORKAROUND
	if (virs->rx_flush_outstanding) {
		EFRM_TRACE("%s: Retrying RX flush on instance %d",
			   __func__, instance);

		list_del(&virs->rx_flush_link);
		efrm_vi_manager->rx_flush_outstanding_count--;
		efrm_vi_resource_issue_rx_flush(virs, completed);
		efrm_vi_resource_process_waiting_flushes(completed);
	}
#endif

#if BUG5302_WORKAROUND
	if (virs->tx_flushing) {
		if (virs->flush_count > 5) {
			EFRM_TRACE("%s: VI resource stuck flush pending "
				   "(instance=%d, count=%d)",
				   __func__, instance, virs->flush_count);
			falcon_clobber_tx_dma_ptrs(nic, instance);
		} else {
			EFRM_TRACE("%s: Retrying TX flush on instance %d",
				   __func__, instance);
		}

		list_del(&virs->tx_flush_link);
		efrm_vi_resource_issue_tx_flush(virs, completed);
	}
#endif
}
#endif

int efrm_vi_resource_flush_retry(struct vi_resource *virs)
{
#if BUG7916_WORKAROUND || BUG5302_WORKAROUND
	irq_flags_t lock_flags;
	bool completed = false;

	if (virs->rx_flushing == 0 && virs->tx_flushing == 0)
		return -EALREADY;

	spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);
	efrm_vi_resource_flush_retry_vi(virs, get_jiffies_64(), &completed);
	spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock, lock_flags);

	if (completed)
		EFRM_VI_RM_DELAYED_FREE(efrm_vi_manager);
#endif

	return 0;
}
EXPORT_SYMBOL(efrm_vi_resource_flush_retry);

#if BUG7916_WORKAROUND || BUG5302_WORKAROUND
/* resource manager lock should be taken before this call */
static void efrm_vi_handle_flush_loss(bool *completed)
{
	struct list_head *pos, *temp;
	struct vi_resource *virs;
	int64_t time_now, time_pending;

	/* It's possible we miss flushes - the list is sorted in order we
	 * generate flushes, see if any are very old. It's also possible
	 * that we decide an endpoint is flushed even though we've not
	 * received all the flush events. We *should * mark as
	 * completed, reclaim and loop again. ??
	 * THIS NEEDS BACKPORTING FROM THE FALCON branch
	 */
	time_now = get_jiffies_64();

#if BUG7916_WORKAROUND
	list_for_each_safe(pos, temp,
			   &efrm_vi_manager->rx_flush_outstanding_list) {
		virs = container_of(pos, struct vi_resource, rx_flush_link);

		time_pending = time_now - virs->flush_time;

		/* List entries are held in reverse chronological order.  Only
		 * process the old ones. */
		if (time_pending <= 0x100000000LL)
			break;

		efrm_vi_resource_flush_retry_vi(virs, time_now, completed);
	}
#endif

#if BUG5302_WORKAROUND
	list_for_each_safe(pos, temp,
			   &efrm_vi_manager->tx_flush_outstanding_list) {
		virs = container_of(pos, struct vi_resource, tx_flush_link);

		time_pending = time_now - virs->flush_time;

		/* List entries are held in reverse chronological order.
		 * Only process the old ones. */
		if (time_pending <= 0x100000000LL)
			break;

		efrm_vi_resource_flush_retry_vi(virs, time_now, completed);
	}
#endif
}
#endif

void
efrm_vi_register_flush_callback(struct vi_resource *virs,
				void (*handler)(void *), void *arg)
{
	if (handler == NULL) {
		virs->flush_callback_fn = handler;
		wmb();
		virs->flush_callback_arg = arg;
	} else {
		virs->flush_callback_arg = arg;
		wmb();
		virs->flush_callback_fn = handler;
	}
}
EXPORT_SYMBOL(efrm_vi_register_flush_callback);

int efrm_pt_flush(struct vi_resource *virs)
{
	int instance;
	irq_flags_t lock_flags;
	bool completed = false;

	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);

	EFRM_ASSERT(virs->rx_flushing == 0);
	EFRM_ASSERT(virs->rx_flush_outstanding == 0);
	EFRM_ASSERT(virs->tx_flushing == 0);

	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT " EVQ=%d TXQ=%d RXQ=%d",
		   __func__, EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle),
		   virs->evq_capacity,
		   virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX],
		   virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX]);

	spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);

	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX] != 0)
		virs->rx_flushing = 1;

	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX] != 0)
		virs->tx_flushing = 1;

	/* Clean up immediately if there are no flushes. */
	if (virs->rx_flushing == 0 && virs->tx_flushing == 0) {
		list_add_tail(&virs->rx_flush_link,
			      &efrm_vi_manager->close_pending);
		completed = true;
	}

	/* Issue the RX flush if possible or queue it for later. */
	if (virs->rx_flushing) {
#if BUG7916_WORKAROUND || BUG5302_WORKAROUND
		if (efrm_vi_manager->rx_flush_outstanding_count >=
		    flush_fifo_hwm)
			efrm_vi_handle_flush_loss(&completed);
#endif
		if (efrm_vi_manager->rx_flush_outstanding_count >=
		    flush_fifo_hwm) {
			list_add_tail(&virs->rx_flush_link,
				      &efrm_vi_manager->rx_flush_waiting_list);
		} else {
			efrm_vi_resource_issue_rx_flush(virs, &completed);
		}
	}

	/* Issue the TX flush.  There's no limit to the number of
	 * outstanding TX flushes. */
	if (virs->tx_flushing)
		efrm_vi_resource_issue_tx_flush(virs, &completed);

	virs->flush_time = get_jiffies_64();

	spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock, lock_flags);

	if (completed)
		EFRM_VI_RM_DELAYED_FREE(efrm_vi_manager);

	return 0;
}
EXPORT_SYMBOL(efrm_pt_flush);

static void
efrm_handle_rx_dmaq_flushed(struct efhw_nic *flush_nic, int instance,
			    bool *completed)
{
	struct list_head *pos, *temp;
	struct vi_resource *virs;

	list_for_each_safe(pos, temp,
			   &efrm_vi_manager->rx_flush_outstanding_list) {
		virs = container_of(pos, struct vi_resource, rx_flush_link);

		if (instance == EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle)) {
			efrm_vi_resource_rx_flush_done(virs, completed);
			efrm_vi_resource_process_waiting_flushes(completed);
			return;
		}
	}
	EFRM_TRACE("%s: Unhandled rx flush event, nic %d, instance %d",
		   __func__, flush_nic->index, instance);
}

static void
efrm_handle_tx_dmaq_flushed(struct efhw_nic *flush_nic, int instance,
			    bool *completed)
{
	struct list_head *pos, *temp;
	struct vi_resource *virs;

	list_for_each_safe(pos, temp,
			   &efrm_vi_manager->tx_flush_outstanding_list) {
		virs = container_of(pos, struct vi_resource, tx_flush_link);

		if (instance == EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle)) {
			efrm_vi_resource_tx_flush_done(virs, completed);
			return;
		}
	}
	EFRM_TRACE("%s: Unhandled tx flush event, nic %d, instance %d",
		   __func__, flush_nic->index, instance);
}

void
efrm_handle_dmaq_flushed(struct efhw_nic *flush_nic, unsigned instance,
			 int rx_flush)
{
	irq_flags_t lock_flags;
	bool completed = false;

	EFRM_TRACE("%s: nic_i=%d  instance=%d  rx_flush=%d", __func__,
		   flush_nic->index, instance, rx_flush);

	spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);

	if (rx_flush)
		efrm_handle_rx_dmaq_flushed(flush_nic, instance, &completed);
	else
		efrm_handle_tx_dmaq_flushed(flush_nic, instance, &completed);

#if BUG7916_WORKAROUND || BUG5302_WORKAROUND
	efrm_vi_handle_flush_loss(&completed);
#endif

	spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock, lock_flags);

	if (completed)
		EFRM_VI_RM_DELAYED_FREE(efrm_vi_manager);
}

static void
efrm_vi_rm_reinit_dmaqs(struct vi_resource *virs)
{
	struct efhw_nic *nic = virs->rs.rs_client->nic;

	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX] != 0)
		efrm_vi_rm_init_dmaq(virs, EFRM_VI_RM_DMA_QUEUE_TX, nic);
	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX])
		efrm_vi_rm_init_dmaq(virs, EFRM_VI_RM_DMA_QUEUE_RX, nic);
}

/* free any PT endpoints whose flush has now complete */
void efrm_vi_rm_delayed_free(struct work_struct *data)
{
	irq_flags_t lock_flags;
	struct list_head close_pending;
	struct vi_resource *virs;

	EFRM_RESOURCE_MANAGER_ASSERT_VALID(&efrm_vi_manager->rm);

	spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);
	list_replace_init(&efrm_vi_manager->close_pending, &close_pending);
	spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock, lock_flags);

	EFRM_TRACE("%s: %p", __func__, efrm_vi_manager);
	while (!list_empty(&close_pending)) {
		virs =
		    list_entry(list_pop(&close_pending), struct vi_resource,
			       rx_flush_link);
		EFRM_TRACE("%s: flushed VI instance=%d", __func__,
			   EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle));

		if (virs->flush_callback_fn != NULL) {
			efrm_vi_rm_reinit_dmaqs(virs);
			virs->flush_callback_fn(virs->flush_callback_arg);
		} else
			efrm_vi_rm_free_flushed_resource(virs);
	}
}

void efrm_vi_rm_salvage_flushed_vis(void)
{
#if BUG7916_WORKAROUND || BUG5302_WORKAROUND
	irq_flags_t lock_flags;
	bool completed;

	spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);
	efrm_vi_handle_flush_loss(&completed);
	spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock, lock_flags);
#endif

	efrm_vi_rm_delayed_free(&efrm_vi_manager->work_item);
}

void efrm_vi_resource_free(struct vi_resource *virs)
{
	efrm_vi_register_flush_callback(virs, NULL, NULL);
	efrm_pt_flush(virs);
}
EXPORT_SYMBOL(efrm_vi_resource_free);


void efrm_vi_resource_release(struct vi_resource *virs)
{
	if (__efrm_resource_release(&virs->rs))
		efrm_vi_resource_free(virs);
}
EXPORT_SYMBOL(efrm_vi_resource_release);

/*
 * vi: sw=8:ai:aw
 */
