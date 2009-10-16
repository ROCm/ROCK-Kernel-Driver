/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains event handling for VI resource.
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
#include <ci/efhw/eventq.h>
#include <ci/efrm/private.h>
#include <ci/efrm/vi_resource_private.h>
#include <ci/efrm/efrm_nic.h>
#include "efrm_internal.h"


static inline int
efrm_eventq_bytes(struct vi_resource *virs)
{
	return efrm_vi_rm_evq_bytes(virs);
}


static inline efhw_event_t *
efrm_eventq_base(struct vi_resource *virs)
{
	struct eventq_resource_hardware *hw;
	hw = &(virs->nic_info.evq_pages);
	return (efhw_event_t *) (efhw_iopages_ptr(&(hw->iobuff)) +
				 hw->iobuff_off);
}


void
efrm_eventq_request_wakeup(struct vi_resource *virs, unsigned current_ptr)
{
	struct efhw_nic *nic = virs->rs.rs_client->nic;
	int next_i;
	next_i = ((current_ptr / sizeof(efhw_event_t)) &
		  (virs->evq_capacity - 1));

	efhw_nic_wakeup_request(nic, efrm_eventq_dma_addr(virs), next_i,
				EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle));
}
EXPORT_SYMBOL(efrm_eventq_request_wakeup);

void efrm_eventq_reset(struct vi_resource *virs)
{
	struct efhw_nic *nic = virs->rs.rs_client->nic;
	int instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);

	EFRM_ASSERT(virs->evq_capacity != 0);

	/* FIXME: Protect against concurrent resets. */

	efhw_nic_event_queue_disable(nic, instance, 0);

	memset(efrm_eventq_base(virs), EFHW_CLEAR_EVENT_VALUE,
	       efrm_eventq_bytes(virs));
	efhw_nic_event_queue_enable(nic, instance, virs->evq_capacity,
				    efrm_eventq_dma_addr(virs),
				    virs->nic_info.evq_pages.
				      buf_tbl_alloc.base,
				    instance < 64);
	EFRM_TRACE("%s: " EFRM_RESOURCE_FMT, __func__,
		   EFRM_RESOURCE_PRI_ARG(virs->rs.rs_handle));
}
EXPORT_SYMBOL(efrm_eventq_reset);

int
efrm_eventq_register_callback(struct vi_resource *virs,
			      void (*handler) (void *, int,
					       struct efhw_nic *nic),
			      void *arg)
{
	struct efrm_nic_per_vi *cb_info;
	int instance;
	int bit;

	EFRM_RESOURCE_ASSERT_VALID(&virs->rs, 0);
	EFRM_ASSERT(virs->evq_capacity != 0);
	EFRM_ASSERT(handler != NULL);

	/* ?? TODO: Get rid of this test when client is compulsory. */
	if (virs->rs.rs_client == NULL) {
		EFRM_ERR("%s: no client", __func__);
		return -EINVAL;
	}

	virs->evq_callback_arg = arg;
	virs->evq_callback_fn = handler;
	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
	cb_info = &efrm_nic(virs->rs.rs_client->nic)->vis[instance];

	/* The handler can be set only once. */
	bit = test_and_set_bit(VI_RESOURCE_EVQ_STATE_CALLBACK_REGISTERED,
			       &cb_info->state);
	if (bit)
		return -EBUSY;
	cb_info->vi = virs;

	return 0;
}
EXPORT_SYMBOL(efrm_eventq_register_callback);

void efrm_eventq_kill_callback(struct vi_resource *virs)
{
	struct efrm_nic_per_vi *cb_info;
	int32_t evq_state;
	int instance;
	int bit;

	EFRM_RESOURCE_ASSERT_VALID(&virs->rs, 0);
	EFRM_ASSERT(virs->evq_capacity != 0);
	EFRM_ASSERT(virs->rs.rs_client != NULL);

	instance = EFRM_RESOURCE_INSTANCE(virs->rs.rs_handle);
	cb_info = &efrm_nic(virs->rs.rs_client->nic)->vis[instance];
	cb_info->vi = NULL;

	/* Disable the timer. */
	efhw_nic_event_queue_disable(virs->rs.rs_client->nic,
				     instance, /*timer_only */ 1);

	/* Disable the callback. */
	bit = test_and_clear_bit(VI_RESOURCE_EVQ_STATE_CALLBACK_REGISTERED,
				 &cb_info->state);
	EFRM_ASSERT(bit);	/* do not call me twice! */

	/* Spin until the callback is complete. */
	do {
		rmb();

		udelay(1);
		evq_state = cb_info->state;
	} while ((evq_state & VI_RESOURCE_EVQ_STATE(BUSY)));

	virs->evq_callback_fn = NULL;
}
EXPORT_SYMBOL(efrm_eventq_kill_callback);

static void
efrm_eventq_do_callback(struct efhw_nic *nic, unsigned instance,
			bool is_timeout)
{
	struct efrm_nic *rnic = efrm_nic(nic);
	void (*handler) (void *, int is_timeout, struct efhw_nic *nic);
	void *arg;
	struct efrm_nic_per_vi *cb_info;
	int32_t evq_state;
	int32_t new_evq_state;
	struct vi_resource *virs;
	int bit;

	EFRM_ASSERT(efrm_vi_manager);

	cb_info = &rnic->vis[instance];

	/* Set the BUSY bit and clear WAKEUP_PENDING.  Do this
	 * before waking up the sleeper to avoid races. */
	while (1) {
		evq_state = cb_info->state;
		new_evq_state = evq_state;

		if ((evq_state & VI_RESOURCE_EVQ_STATE(BUSY)) != 0) {
			EFRM_ERR("%s:%d: evq_state[%d] corrupted!",
				 __func__, __LINE__, instance);
			return;
		}

		if (!is_timeout)
			new_evq_state &= ~VI_RESOURCE_EVQ_STATE(WAKEUP_PENDING);

		if (evq_state & VI_RESOURCE_EVQ_STATE(CALLBACK_REGISTERED)) {
			new_evq_state |= VI_RESOURCE_EVQ_STATE(BUSY);
			virs = cb_info->vi;
			if (cmpxchg(&cb_info->state, evq_state,
				    new_evq_state) == evq_state)
				break;
		} else {
			/* Just update the state if necessary. */
			if (new_evq_state == evq_state ||
			    cmpxchg(&cb_info->state, evq_state,
				    new_evq_state) == evq_state)
				return;
		}
	}

	if (virs) {
		handler = virs->evq_callback_fn;
		arg = virs->evq_callback_arg;
		EFRM_ASSERT(handler != NULL);
		handler(arg, is_timeout, nic);
	}

	/* Clear the BUSY bit. */
	bit =
	    test_and_clear_bit(VI_RESOURCE_EVQ_STATE_BUSY,
			       &cb_info->state);
	if (!bit) {
		EFRM_ERR("%s:%d: evq_state corrupted!",
			 __func__, __LINE__);
	}
}

void efrm_handle_wakeup_event(struct efhw_nic *nic, unsigned instance)
{
	efrm_eventq_do_callback(nic, instance, false);
}

void efrm_handle_timeout_event(struct efhw_nic *nic, unsigned instance)
{
	efrm_eventq_do_callback(nic, instance, true);
}

void efrm_handle_sram_event(struct efhw_nic *nic)
{
  if (nic->buf_commit_outstanding > 0)
    nic->buf_commit_outstanding--;
}
