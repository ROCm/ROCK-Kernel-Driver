/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains allocation of VI resources.
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
#include <ci/efhw/iopage.h>
#include <ci/driver/efab/hardware.h>
#include <ci/efhw/public.h>
#include <ci/efhw/falcon.h>
#include <ci/efrm/private.h>
#include <ci/efrm/buffer_table.h>
#include <ci/efrm/vi_resource_private.h>
#include <ci/efrm/efrm_client.h>
#include "efrm_internal.h"


/*** Data definitions ****************************************************/

static const char *dmaq_names[] = { "TX", "RX" };

struct vi_resource_manager *efrm_vi_manager;

/*** Forward references **************************************************/

static int
efrm_vi_resource_alloc_or_free(struct efrm_client *client,
			       int alloc, struct vi_resource *evq_virs,
			       uint16_t vi_flags, int32_t evq_capacity,
			       int32_t txq_capacity, int32_t rxq_capacity,
			       uint8_t tx_q_tag, uint8_t rx_q_tag,
			       struct vi_resource **virs_in_out);

/*** Reference count handling ********************************************/

static inline void efrm_vi_rm_get_ref(struct vi_resource *virs)
{
	atomic_inc(&virs->evq_refs);
}

static inline void efrm_vi_rm_drop_ref(struct vi_resource *virs)
{
	EFRM_ASSERT(atomic_read(&virs->evq_refs) != 0);
	if (atomic_dec_and_test(&virs->evq_refs))
		efrm_vi_resource_alloc_or_free(virs->rs.rs_client, false, NULL,
					       0, 0, 0, 0, 0, 0, &virs);
}

/*** Instance numbers ****************************************************/

static inline int efrm_vi_rm_alloc_id(uint16_t vi_flags, int32_t evq_capacity)
{
	irq_flags_t lock_flags;
	int instance;
	int rc;

	if (efrm_nic_tablep->a_nic == NULL)	/* ?? FIXME: surely not right */
		return -ENODEV;

	spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);

	/* Falcon A1 RX phys addr wierdness. */
	if (efrm_nic_tablep->a_nic->devtype.variant == 'A' &&
	    (vi_flags & EFHW_VI_RX_PHYS_ADDR_EN)) {
		if (vi_flags & EFHW_VI_JUMBO_EN) {
			/* Falcon-A cannot do phys + scatter. */
			EFRM_WARN
			    ("%s: falcon-A does not support phys+scatter mode",
			     __func__);
			instance = -1;
		} else if (efrm_vi_manager->iscsi_dmaq_instance_is_free
			   && evq_capacity == 0) {
			/* Falcon-A has a single RXQ that gives the correct
			 * semantics for physical addressing.  However, it
			 * happens to have the same instance number as the
			 * 'char' event queue, so we cannot also hand out
			 * the event queue. */
			efrm_vi_manager->iscsi_dmaq_instance_is_free = false;
			instance = FALCON_A1_ISCSI_DMAQ;
		} else {
			EFRM_WARN("%s: iSCSI receive queue not free",
				  __func__);
			instance = -1;
		}
		goto unlock_out;
	}

	if (vi_flags & EFHW_VI_RM_WITH_INTERRUPT) {
		rc = __kfifo_get(efrm_vi_manager->instances_with_interrupt,
				 (unsigned char *)&instance, sizeof(instance));
		if (rc != sizeof(instance)) {
			EFRM_ASSERT(rc == 0);
			instance = -1;
		}
		goto unlock_out;
	}

	/* Otherwise a normal run-of-the-mill VI. */
	rc = __kfifo_get(efrm_vi_manager->instances_with_timer,
			 (unsigned char *)&instance, sizeof(instance));
	if (rc != sizeof(instance)) {
		EFRM_ASSERT(rc == 0);
		instance = -1;
	}

unlock_out:
	spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock, lock_flags);
	return instance;
}

static void efrm_vi_rm_free_id(int instance)
{
	irq_flags_t lock_flags;
	struct kfifo *instances;

	if (efrm_nic_tablep->a_nic == NULL)	/* ?? FIXME: surely not right */
		return;

	if (efrm_nic_tablep->a_nic->devtype.variant == 'A' &&
	    instance == FALCON_A1_ISCSI_DMAQ) {
		EFRM_ASSERT(efrm_vi_manager->iscsi_dmaq_instance_is_free ==
			    false);
		spin_lock_irqsave(&efrm_vi_manager->rm.rm_lock, lock_flags);
		efrm_vi_manager->iscsi_dmaq_instance_is_free = true;
		spin_unlock_irqrestore(&efrm_vi_manager->rm.rm_lock,
				       lock_flags);
	} else {
		if (instance >= efrm_vi_manager->with_timer_base &&
		    instance < efrm_vi_manager->with_timer_limit) {
			instances = efrm_vi_manager->instances_with_timer;
		} else {
			EFRM_ASSERT(instance >=
				    efrm_vi_manager->with_interrupt_base);
			EFRM_ASSERT(instance <
				    efrm_vi_manager->with_interrupt_limit);
			instances = efrm_vi_manager->instances_with_interrupt;
		}

		EFRM_VERIFY_EQ(kfifo_put(instances, (unsigned char *)&instance,
					 sizeof(instance)), sizeof(instance));
	}
}

/*** Queue sizes *********************************************************/

/* NB. This should really take a nic as an argument, but that makes
 * the buffer table allocation difficult. */
uint32_t efrm_vi_rm_evq_bytes(struct vi_resource *virs
			      /*,struct efhw_nic *nic */)
{
	return virs->evq_capacity * sizeof(efhw_event_t);
}
EXPORT_SYMBOL(efrm_vi_rm_evq_bytes);

/* NB. This should really take a nic as an argument, but that makes
 * the buffer table allocation difficult. */
uint32_t efrm_vi_rm_txq_bytes(struct vi_resource *virs
			      /*,struct efhw_nic *nic */)
{
	return virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX] *
	    FALCON_DMA_TX_DESC_BYTES;
}
EXPORT_SYMBOL(efrm_vi_rm_txq_bytes);

/* NB. This should really take a nic as an argument, but that makes
 * the buffer table allocation difficult. */
uint32_t efrm_vi_rm_rxq_bytes(struct vi_resource *virs
			      /*,struct efhw_nic *nic */)
{
	uint32_t bytes_per_desc = ((virs->flags & EFHW_VI_RX_PHYS_ADDR_EN)
				   ? FALCON_DMA_RX_PHYS_DESC_BYTES
				   : FALCON_DMA_RX_BUF_DESC_BYTES);
	return virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX] * bytes_per_desc;
}
EXPORT_SYMBOL(efrm_vi_rm_rxq_bytes);

static int choose_size(int size_rq, unsigned sizes)
{
	int size;

	/* size_rq < 0 means default, but we interpret this as 'minimum'. */

	for (size = 256;; size <<= 1)
		if ((size & sizes) && size >= size_rq)
			return size;
		else if ((sizes & ~((size - 1) | size)) == 0)
			return -1;
}

static int
efrm_vi_rm_adjust_alloc_request(struct vi_resource *virs, struct efhw_nic *nic)
{
	int capacity;

	EFRM_ASSERT(nic->efhw_func);

	if (virs->evq_capacity) {
		capacity = choose_size(virs->evq_capacity, nic->evq_sizes);
		if (capacity < 0) {
			EFRM_ERR("vi_resource: bad evq size %d (supported=%x)",
				 virs->evq_capacity, nic->evq_sizes);
			return -E2BIG;
		}
		virs->evq_capacity = capacity;
	}
	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX]) {
		capacity =
		    choose_size(virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX],
				nic->txq_sizes);
		if (capacity < 0) {
			EFRM_ERR("vi_resource: bad txq size %d (supported=%x)",
				 virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX],
				 nic->txq_sizes);
			return -E2BIG;
		}
		virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX] = capacity;
	}
	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX]) {
		capacity =
		    choose_size(virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX],
				nic->rxq_sizes);
		if (capacity < 0) {
			EFRM_ERR("vi_resource: bad rxq size %d (supported=%x)",
				 virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX],
				 nic->rxq_sizes);
			return -E2BIG;
		}
		virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX] = capacity;
	}

	return 0;
}

/* remove the reference to the event queue in this VI resource and decrement
   the event queue's use count */
static inline void efrm_vi_rm_detach_evq(struct vi_resource *virs)
{
	struct vi_resource *evq_virs;

	EFRM_ASSERT(virs != NULL);

	evq_virs = virs->evq_virs;

	if (evq_virs != NULL) {
		virs->evq_virs = NULL;
		if (evq_virs == virs) {
			EFRM_TRACE("%s: " EFRM_RESOURCE_FMT
				   " had internal event queue ", __func__,
				   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle));
		} else {
			efrm_vi_rm_drop_ref(evq_virs);
			EFRM_TRACE("%s: " EFRM_RESOURCE_FMT " had event queue "
				   EFRM_RESOURCE_FMT, __func__,
				   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle),
				   EFRM_RESOURCE_PRI_ARG(evq_virs->rs.
							 rs_handle));
		}
	} else {
		EFRM_TRACE("%s: " EFRM_RESOURCE_FMT
			   " had no event queue (nothing to do)",
			   __func__,
			   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle));
	}
}

/*** Buffer Table allocations ********************************************/

static int
efrm_vi_rm_alloc_or_free_buffer_table(struct vi_resource *virs, bool is_alloc)
{
	uint32_t bytes;
	int page_order;
	int rc;

	if (!is_alloc)
		goto destroy;

	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX]) {
		bytes = efrm_vi_rm_txq_bytes(virs);
		page_order = get_order(bytes);
		rc = efrm_buffer_table_alloc(page_order,
					     (virs->dmaq_buf_tbl_alloc +
					      EFRM_VI_RM_DMA_QUEUE_TX));
		if (rc != 0) {
			EFRM_TRACE
			    ("%s: Error %d allocating TX buffer table entry",
			     __func__, rc);
			goto fail_txq_alloc;
		}
	}

	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX]) {
		bytes = efrm_vi_rm_rxq_bytes(virs);
		page_order = get_order(bytes);
		rc = efrm_buffer_table_alloc(page_order,
					     (virs->dmaq_buf_tbl_alloc +
					      EFRM_VI_RM_DMA_QUEUE_RX));
		if (rc != 0) {
			EFRM_TRACE
			    ("%s: Error %d allocating RX buffer table entry",
			     __func__, rc);
			goto fail_rxq_alloc;
		}
	}
	return 0;

destroy:
	rc = 0;

	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX]) {
		efrm_buffer_table_free(&virs->
				       dmaq_buf_tbl_alloc
				       [EFRM_VI_RM_DMA_QUEUE_RX]);
	}
fail_rxq_alloc:

	if (virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX]) {
		efrm_buffer_table_free(&virs->
				       dmaq_buf_tbl_alloc
				       [EFRM_VI_RM_DMA_QUEUE_TX]);
	}
fail_txq_alloc:

	return rc;
}

/*** Per-NIC allocations *************************************************/

static inline int
efrm_vi_rm_init_evq(struct vi_resource *virs, struct efhw_nic *nic)
{
	int instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
	struct eventq_resource_hardware *evq_hw =
	    &virs->nic_info.evq_pages;
	uint32_t buf_bytes = efrm_vi_rm_evq_bytes(virs);
	int rc;

	if (virs->evq_capacity == 0)
		return 0;
	evq_hw->capacity = virs->evq_capacity;

	/* Allocate buffer table entries to map onto the iobuffer.  This
	 * currently allocates its own buffer table entries on Falcon which is
	 * a bit wasteful on a multi-NIC system. */
	evq_hw->buf_tbl_alloc.base = (unsigned)-1;
	rc = efrm_buffer_table_alloc(get_order(buf_bytes),
				     &evq_hw->buf_tbl_alloc);
	if (rc < 0) {
		EFHW_WARN("%s: failed (%d) to alloc %d buffer table entries",
			  __func__, rc, get_order(buf_bytes));
		return rc;
	}

	/* Allocate the event queue memory. */
	rc = efhw_nic_event_queue_alloc_iobuffer(nic, evq_hw, instance,
						 buf_bytes);
	if (rc != 0) {
		EFRM_ERR("%s: Error allocating iobuffer: %d", __func__, rc);
		efrm_buffer_table_free(&evq_hw->buf_tbl_alloc);
		return rc;
	}

	/* Initialise the event queue hardware */
	efhw_nic_event_queue_enable(nic, instance, virs->evq_capacity,
				    efhw_iopages_dma_addr(&evq_hw->iobuff) +
				    evq_hw->iobuff_off,
				    evq_hw->buf_tbl_alloc.base,
				    instance < 64);

	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT " capacity=%u", __func__,
		   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle),
		   virs->evq_capacity);

#if defined(__ia64__)
	/* Page size may be large, so for now just increase the
	 * size of the requested evq up to a round number of
	 * pages
	 */
	buf_bytes = CI_ROUNDUP(buf_bytes, PAGE_SIZE);
#endif
	EFRM_ASSERT(buf_bytes % PAGE_SIZE == 0);

	virs->mem_mmap_bytes += buf_bytes;

	return 0;
}

static inline void
efrm_vi_rm_fini_evq(struct vi_resource *virs, struct efhw_nic *nic)
{
	int instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
	struct vi_resource_nic_info *nic_info = &virs->nic_info;

	if (virs->evq_capacity == 0)
		return;

	/* Zero the timer-value for this queue.
	   And Tell NIC to stop using this event queue. */
	efhw_nic_event_queue_disable(nic, instance, 0);

	if (nic_info->evq_pages.buf_tbl_alloc.base != (unsigned)-1)
		efrm_buffer_table_free(&nic_info->evq_pages.buf_tbl_alloc);

	efhw_iopages_free(nic, &nic_info->evq_pages.iobuff);
}

/*! FIXME: we should make sure this number is never zero (=> unprotected) */
/*! FIXME: put this definition in a relevant header (e.g. as (evqid)+1) */
#define EFAB_EVQ_OWNER_ID(evqid) ((evqid))

void
efrm_vi_rm_init_dmaq(struct vi_resource *virs, int queue_type,
		     struct efhw_nic *nic)
{
	int instance;
	int evq_instance;
	efhw_buffer_addr_t buf_addr;

	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
	evq_instance = EFRM_RESOURCE_INSTANCE(virs->evq_virs->rs.rs_handle);

	buf_addr = virs->dmaq_buf_tbl_alloc[queue_type].base;

	if (queue_type == EFRM_VI_RM_DMA_QUEUE_TX) {
		efhw_nic_dmaq_tx_q_init(nic,
			instance,	/* dmaq */
			evq_instance,	/* evq */
			EFAB_EVQ_OWNER_ID(evq_instance),	/* owner */
			virs->dmaq_tag[queue_type],	/* tag */
			virs->dmaq_capacity[queue_type], /* size of queue */
			buf_addr,	/* buffer index */
			virs->flags);	/* user specified Q attrs */
	} else {
		efhw_nic_dmaq_rx_q_init(nic,
			instance,	/* dmaq */
			evq_instance,	/* evq */
			EFAB_EVQ_OWNER_ID(evq_instance),	/* owner */
			virs->dmaq_tag[queue_type],	/* tag */
			virs->dmaq_capacity[queue_type], /* size of queue */
			buf_addr,	/* buffer index */
			virs->flags);	/* user specified Q attrs */
	}
}

static int
efrm_vi_rm_init_or_fini_dmaq(struct vi_resource *virs,
			     int queue_type, int init,
			     struct efhw_nic *nic)
{
	int rc;
	int instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
	uint32_t buf_bytes;
	struct vi_resource_nic_info *nic_info = &virs->nic_info;
	int page_order;
	uint32_t num_pages;
	struct efhw_iopages *iobuff;

	if (!init)
		goto destroy;

	/* Ignore disabled queues. */
	if (virs->dmaq_capacity[queue_type] == 0) {
		if (queue_type == EFRM_VI_RM_DMA_QUEUE_TX)
			efhw_nic_dmaq_tx_q_disable(nic, instance);
		else
			efhw_nic_dmaq_rx_q_disable(nic, instance);
		return 0;
	}

	buf_bytes = (queue_type == EFRM_VI_RM_DMA_QUEUE_TX
		     ? efrm_vi_rm_txq_bytes(virs)
		     : efrm_vi_rm_rxq_bytes(virs));

	page_order = get_order(buf_bytes);

	rc = efhw_iopages_alloc(nic, &nic_info->dmaq_pages[queue_type],
			      page_order);
	if (rc != 0) {
		EFRM_ERR("%s: Failed to allocate %s DMA buffer.", __func__,
			 dmaq_names[queue_type]);
		goto fail_iopages;
	}

	num_pages = 1 << page_order;
	iobuff = &nic_info->dmaq_pages[queue_type];
	efhw_nic_buffer_table_set_n(nic,
				    virs->dmaq_buf_tbl_alloc[queue_type].base,
				    efhw_iopages_dma_addr(iobuff),
				    EFHW_NIC_PAGE_SIZE, 0, num_pages, 0);

	falcon_nic_buffer_table_confirm(nic);

	virs->mem_mmap_bytes += roundup(buf_bytes, PAGE_SIZE);

	/* Make sure there is an event queue. */
	if (virs->evq_virs->evq_capacity <= 0) {
		EFRM_ERR("%s: Cannot use empty event queue for %s DMA",
			 __func__, dmaq_names[queue_type]);
		rc = -EINVAL;
		goto fail_evq;
	}

	efrm_vi_rm_init_dmaq(virs, queue_type, nic);

	return 0;

destroy:
	rc = 0;

	/* Ignore disabled queues. */
	if (virs->dmaq_capacity[queue_type] == 0)
		return 0;

	/* Ensure TX pacing turned off -- queue flush doesn't reset this. */
	if (queue_type == EFRM_VI_RM_DMA_QUEUE_TX)
		falcon_nic_pace(nic, instance, 0);

	/* No need to disable the queue here.  Nobody is using it anyway. */

fail_evq:
	efhw_iopages_free(nic, &nic_info->dmaq_pages[queue_type]);
fail_iopages:

	return rc;
}

static int
efrm_vi_rm_init_or_fini_nic(struct vi_resource *virs, int init,
			    struct efhw_nic *nic)
{
	int rc;
#ifndef NDEBUG
	int instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
#endif

	if (!init)
		goto destroy;

	rc = efrm_vi_rm_init_evq(virs, nic);
	if (rc != 0)
		goto fail_evq;

	rc = efrm_vi_rm_init_or_fini_dmaq(virs, EFRM_VI_RM_DMA_QUEUE_TX,
					  init, nic);
	if (rc != 0)
		goto fail_txq;

	rc = efrm_vi_rm_init_or_fini_dmaq(virs, EFRM_VI_RM_DMA_QUEUE_RX,
					  init, nic);
	if (rc != 0)
		goto fail_rxq;

	/* Allocate space for the control page. */
	EFRM_ASSERT(falcon_tx_dma_page_offset(instance) < PAGE_SIZE);
	EFRM_ASSERT(falcon_rx_dma_page_offset(instance) < PAGE_SIZE);
	EFRM_ASSERT(falcon_timer_page_offset(instance) < PAGE_SIZE);
	virs->bar_mmap_bytes += PAGE_SIZE;

	return 0;

destroy:
	rc = 0;

	efrm_vi_rm_init_or_fini_dmaq(virs, EFRM_VI_RM_DMA_QUEUE_RX,
				     false, nic);
fail_rxq:

	efrm_vi_rm_init_or_fini_dmaq(virs, EFRM_VI_RM_DMA_QUEUE_TX,
				     false, nic);
fail_txq:

	efrm_vi_rm_fini_evq(virs, nic);
fail_evq:

	EFRM_ASSERT(rc != 0 || !init);
	return rc;
}

static int
efrm_vi_resource_alloc_or_free(struct efrm_client *client,
			       int alloc, struct vi_resource *evq_virs,
			       uint16_t vi_flags, int32_t evq_capacity,
			       int32_t txq_capacity, int32_t rxq_capacity,
			       uint8_t tx_q_tag, uint8_t rx_q_tag,
			       struct vi_resource **virs_in_out)
{
	struct efhw_nic *nic = client->nic;
	struct vi_resource *virs;
	int rc;
	int instance;

	EFRM_ASSERT(virs_in_out);
	EFRM_ASSERT(efrm_vi_manager);
	EFRM_RESOURCE_MANAGER_ASSERT_VALID(&efrm_vi_manager->rm);

	if (!alloc)
		goto destroy;

	rx_q_tag &= (1 << TX_DESCQ_LABEL_WIDTH) - 1;
	tx_q_tag &= (1 << RX_DESCQ_LABEL_WIDTH) - 1;

	virs = kmalloc(sizeof(*virs), GFP_KERNEL);
	if (virs == NULL) {
		EFRM_ERR("%s: Error allocating VI resource object",
			 __func__);
		rc = -ENOMEM;
		goto fail_alloc;
	}
	memset(virs, 0, sizeof(*virs));

	/* Some macros make the assumption that the struct efrm_resource is
	 * the first member of a struct vi_resource. */
	EFRM_ASSERT(&virs->rs == (struct efrm_resource *) (virs));

	instance = efrm_vi_rm_alloc_id(vi_flags, evq_capacity);
	if (instance < 0) {
		/* Clear out the close list... */
		efrm_vi_rm_salvage_flushed_vis();
		instance = efrm_vi_rm_alloc_id(vi_flags, evq_capacity);
		if (instance >= 0)
			EFRM_TRACE("%s: Salvaged a closed VI.", __func__);
	}

	if (instance < 0) {
		/* Could flush resources and try again here. */
		EFRM_ERR("%s: Out of appropriate VI resources", __func__);
		rc = -EBUSY;
		goto fail_alloc_id;
	}

	EFRM_TRACE("%s: new VI ID %d", __func__, instance);
	efrm_resource_init(&virs->rs, EFRM_RESOURCE_VI, instance);

	/* Start with one reference.  Any external VIs using the EVQ of this
	 * resource will increment this reference rather than the resource
	 * reference to avoid DMAQ flushes from waiting for other DMAQ
	 * flushes to complete.  When the resource reference goes to zero,
	 * the DMAQ flush happens.  When the flush completes, this reference
	 * is decremented.  When this reference reaches zero, the instance
	 * is freed. */
	atomic_set(&virs->evq_refs, 1);

	virs->bar_mmap_bytes = 0;
	virs->mem_mmap_bytes = 0;
	virs->evq_capacity = evq_capacity;
	virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX] = txq_capacity;
	virs->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX] = rxq_capacity;
	virs->dmaq_tag[EFRM_VI_RM_DMA_QUEUE_TX] = tx_q_tag;
	virs->dmaq_tag[EFRM_VI_RM_DMA_QUEUE_RX] = rx_q_tag;
	virs->flags = vi_flags;
	INIT_LIST_HEAD(&virs->tx_flush_link);
	INIT_LIST_HEAD(&virs->rx_flush_link);
	virs->tx_flushing = 0;
	virs->rx_flushing = 0;

	/* Adjust the queue sizes. */
	rc = efrm_vi_rm_adjust_alloc_request(virs, nic);
	if (rc != 0)
		goto fail_adjust_request;

	/* Attach the EVQ early so that we can ensure that the NIC sets
	 * match. */
	if (evq_virs == NULL) {
		evq_virs = virs;
		EFRM_TRACE("%s: " EFRM_RESOURCE_FMT
			   " has no external event queue", __func__,
			   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle));
	} else {
		/* Make sure the resource managers are the same. */
		if (EFRM_RESOURCE_TYPE(evq_virs->rs.rs_handle) !=
		    EFRM_RESOURCE_VI) {
			EFRM_ERR("%s: Mismatched owner for event queue VI "
				 EFRM_RESOURCE_FMT, __func__,
				 EFRM_RESOURCE_PRI_ARG(evq_virs->rs.rs_handle));
			return -EINVAL;
		}
		EFRM_ASSERT(atomic_read(&evq_virs->evq_refs) != 0);
		efrm_vi_rm_get_ref(evq_virs);
		EFRM_TRACE("%s: " EFRM_RESOURCE_FMT " uses event queue "
			   EFRM_RESOURCE_FMT,
			   __func__,
			   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle),
			   EFRM_RESOURCE_PRI_ARG(evq_virs->rs.rs_handle));
	}
	virs->evq_virs = evq_virs;

	rc = efrm_vi_rm_alloc_or_free_buffer_table(virs, true);
	if (rc != 0)
		goto fail_buffer_table;

	rc = efrm_vi_rm_init_or_fini_nic(virs, true, nic);
	if (rc != 0)
		goto fail_init_nic;

	efrm_client_add_resource(client, &virs->rs);
	*virs_in_out = virs;
	EFRM_TRACE("%s: Allocated " EFRM_RESOURCE_FMT, __func__,
		   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle));
	return 0;

destroy:
	virs = *virs_in_out;
	EFRM_RESOURCE_ASSERT_VALID(&virs->rs, 1);
	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);

	EFRM_TRACE("%s: Freeing %d", __func__,
		   EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle));

	/* Destroying the VI.  The reference count must be zero. */
	EFRM_ASSERT(atomic_read(&virs->evq_refs) == 0);

	/* The EVQ should have gone (and DMA disabled) so that this
	 * function can't be re-entered to destroy the EVQ VI. */
	EFRM_ASSERT(virs->evq_virs == NULL);
	rc = 0;

fail_init_nic:
	efrm_vi_rm_init_or_fini_nic(virs, false, nic);

	efrm_vi_rm_alloc_or_free_buffer_table(virs, false);
fail_buffer_table:

	efrm_vi_rm_detach_evq(virs);

fail_adjust_request:

	EFRM_ASSERT(virs->evq_callback_fn == NULL);
	EFRM_TRACE("%s: delete VI ID %d", __func__, instance);
	efrm_vi_rm_free_id(instance);
fail_alloc_id:
	if (!alloc)
		efrm_client_put(virs->rs.rs_client);
	EFRM_DO_DEBUG(memset(virs, 0, sizeof(*virs)));
	kfree(virs);
fail_alloc:
	*virs_in_out = NULL;

	return rc;
}

/*** Resource object  ****************************************************/

int
efrm_vi_resource_alloc(struct efrm_client *client,
		       struct vi_resource *evq_virs,
		       uint16_t vi_flags, int32_t evq_capacity,
		       int32_t txq_capacity, int32_t rxq_capacity,
		       uint8_t tx_q_tag, uint8_t rx_q_tag,
		       struct vi_resource **virs_out,
		       uint32_t *out_io_mmap_bytes,
		       uint32_t *out_mem_mmap_bytes,
		       uint32_t *out_txq_capacity, uint32_t *out_rxq_capacity)
{
	int rc;
	EFRM_ASSERT(client != NULL);
	rc = efrm_vi_resource_alloc_or_free(client, true, evq_virs, vi_flags,
					    evq_capacity, txq_capacity,
					    rxq_capacity, tx_q_tag, rx_q_tag,
					    virs_out);
	if (rc == 0) {
		if (out_io_mmap_bytes != NULL)
			*out_io_mmap_bytes = (*virs_out)->bar_mmap_bytes;
		if (out_mem_mmap_bytes != NULL)
			*out_mem_mmap_bytes = (*virs_out)->mem_mmap_bytes;
		if (out_txq_capacity != NULL)
			*out_txq_capacity =
			    (*virs_out)->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_TX];
		if (out_rxq_capacity != NULL)
			*out_rxq_capacity =
			    (*virs_out)->dmaq_capacity[EFRM_VI_RM_DMA_QUEUE_RX];
	}

	return rc;
}
EXPORT_SYMBOL(efrm_vi_resource_alloc);

void efrm_vi_rm_free_flushed_resource(struct vi_resource *virs)
{
	EFRM_ASSERT(virs != NULL);
	EFRM_ASSERT(virs->rs.rs_ref_count == 0);

	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT, __func__,
		   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle));
	/* release the associated event queue then drop our own reference
	 * count */
	efrm_vi_rm_detach_evq(virs);
	efrm_vi_rm_drop_ref(virs);
}
