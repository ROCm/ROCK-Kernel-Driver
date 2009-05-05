/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains public API for VI resource.
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

#ifndef __CI_EFRM_VI_RESOURCE_H__
#define __CI_EFRM_VI_RESOURCE_H__

#include <ci/efhw/efhw_types.h>
#include <ci/efrm/resource.h>
#include <ci/efrm/debug.h>

struct vi_resource;

/* Make these inline instead of macros for type checking */
static inline struct vi_resource *
efrm_to_vi_resource(struct efrm_resource *rs)
{
	EFRM_ASSERT(EFRM_RESOURCE_TYPE(rs->rs_handle) == EFRM_RESOURCE_VI);
	return (struct vi_resource *) rs;
}
static inline struct
efrm_resource *efrm_from_vi_resource(struct vi_resource *rs)
{
	return (struct efrm_resource *)rs;
}

#define EFAB_VI_RESOURCE_INSTANCE(virs) \
    EFRM_RESOURCE_INSTANCE(efrm_from_vi_resource(virs)->rs_handle)

#define EFAB_VI_RESOURCE_PRI_ARG(virs) \
    EFRM_RESOURCE_PRI_ARG(efrm_from_vi_resource(virs)->rs_handle)

extern int
efrm_vi_resource_alloc(struct efrm_client *client,
		       struct vi_resource *evq_virs,
		       uint16_t vi_flags, int32_t evq_capacity,
		       int32_t txq_capacity, int32_t rxq_capacity,
		       uint8_t tx_q_tag, uint8_t rx_q_tag,
		       struct vi_resource **virs_in_out,
		       uint32_t *out_io_mmap_bytes,
		       uint32_t *out_mem_mmap_bytes,
		       uint32_t *out_txq_capacity,
		       uint32_t *out_rxq_capacity);

extern void efrm_vi_resource_free(struct vi_resource *);
extern void efrm_vi_resource_release(struct vi_resource *);


/*--------------------------------------------------------------------
 *
 * eventq handling
 *
 *--------------------------------------------------------------------*/

/*! Reset an event queue and clear any associated timers */
extern void efrm_eventq_reset(struct vi_resource *virs);

/*! Register a kernel-level handler for the event queue.  This function is
 * called whenever a timer expires, or whenever the event queue is woken
 * but no thread is blocked on it.
 *
 * This function returns -EBUSY if a callback is already installed.
 *
 * \param rs      Event-queue resource
 * \param handler Callback-handler
 * \param arg     Argument to pass to callback-handler
 * \return        Status code
 */
extern int
efrm_eventq_register_callback(struct vi_resource *rs,
			      void (*handler)(void *arg, int is_timeout,
					      struct efhw_nic *nic),
			      void *arg);

/*! Kill the kernel-level callback.
 *
 * This function stops the timer from running and unregisters the callback
 * function.  It waits for any running timeout handlers to complete before
 * returning.
 *
 * \param rs      Event-queue resource
 * \return        Nothing
 */
extern void efrm_eventq_kill_callback(struct vi_resource *rs);

/*! Ask the NIC to generate a wakeup when an event is next delivered. */
extern void efrm_eventq_request_wakeup(struct vi_resource *rs,
				       unsigned current_ptr);

/*! Register a kernel-level handler for flush completions.
 * \TODO Currently, it is unsafe to install a callback more than once.
 *
 * \param rs      VI resource being flushed.
 * \param handler Callback handler function.
 * \param arg     Argument to be passed to handler.
 */
extern void
efrm_vi_register_flush_callback(struct vi_resource *rs,
				void (*handler)(void *),
				void *arg);

int efrm_vi_resource_flush_retry(struct vi_resource *virs);

/*! Comment? */
extern int efrm_pt_flush(struct vi_resource *);

/*! Comment? */
extern int efrm_pt_pace(struct vi_resource *, unsigned int val);

uint32_t efrm_vi_rm_txq_bytes(struct vi_resource *virs
			      /*,struct efhw_nic *nic */);
uint32_t efrm_vi_rm_rxq_bytes(struct vi_resource *virs
			      /*,struct efhw_nic *nic */);
uint32_t efrm_vi_rm_evq_bytes(struct vi_resource *virs
			      /*,struct efhw_nic *nic */);


/* Fill [out_vi_data] with information required to allow a VI to be init'd.
 * [out_vi_data] must ref at least VI_MAPPINGS_SIZE bytes.
 */
extern void efrm_vi_resource_mappings(struct vi_resource *, void *out_vi_data);


#endif /* __CI_EFRM_VI_RESOURCE_H__ */
