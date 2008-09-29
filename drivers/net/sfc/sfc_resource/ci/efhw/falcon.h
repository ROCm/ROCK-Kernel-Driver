/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains API provided by efhw/falcon.c file.  This file is not
 * designed for use outside of the SFC resource driver.
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

#ifndef __CI_EFHW_FALCON_H__
#define __CI_EFHW_FALCON_H__

#include <ci/efhw/efhw_types.h>
#include <ci/efhw/common.h>

/*----------------------------------------------------------------------------
 *
 * Locks - unfortunately required
 *
 *---------------------------------------------------------------------------*/

#define FALCON_LOCK_DECL        irq_flags_t lock_state
#define FALCON_LOCK_LOCK(nic) \
	spin_lock_irqsave((nic)->reg_lock, lock_state)
#define FALCON_LOCK_UNLOCK(nic) \
	spin_unlock_irqrestore((nic)->reg_lock, lock_state)

extern struct efhw_func_ops falcon_char_functional_units;

/*! specify a pace value for a TX DMA Queue */
extern void falcon_nic_pace(struct efhw_nic *nic, uint dmaq, uint pace);

/*! configure the pace engine */
extern void falcon_nic_pace_cfg(struct efhw_nic *nic, int fb_base,
				int bin_thresh);

/*! confirm buffer table updates - should be used for items where
   loss of data would be unacceptable. E.g for the buffers that back
   an event or DMA queue */
extern void falcon_nic_buffer_table_confirm(struct efhw_nic *nic);

/*! Reset the all the TX DMA queue pointers. */
extern void falcon_clobber_tx_dma_ptrs(struct efhw_nic *nic, uint dmaq);

extern int
falcon_handle_char_event(struct efhw_nic *nic,
			 struct efhw_ev_handler *h, efhw_event_t *evp);

/*! Acknowledge to HW that processing is complete on a given event queue */
extern void falcon_nic_evq_ack(struct efhw_nic *nic, uint evq,	/* evq id */
			       uint rptr,	/* new read pointer update */
			       bool wakeup	/* request a wakeup event if
						   ptr's != */
    );

extern void
falcon_nic_buffer_table_set_n(struct efhw_nic *nic, int buffer_id,
			      dma_addr_t dma_addr, uint bufsz, uint region,
			      int n_pages, int own_id);

extern int falcon_nic_filter_ctor(struct efhw_nic *nic);

extern void falcon_nic_filter_dtor(struct efhw_nic *nic);

#endif /* __CI_EFHW_FALCON_H__ */
