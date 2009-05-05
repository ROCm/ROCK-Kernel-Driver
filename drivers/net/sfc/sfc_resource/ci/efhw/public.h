/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides public API of efhw library exported from the SFC
 * resource driver.
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

#ifndef __CI_EFHW_PUBLIC_H__
#define __CI_EFHW_PUBLIC_H__

#include <ci/efhw/common.h>
#include <ci/efhw/efhw_types.h>

/*! Returns true if we have some EtherFabric functional units -
  whether configured or not */
static inline int efhw_nic_have_functional_units(struct efhw_nic *nic)
{
	return nic->efhw_func != 0;
}

/*! Returns true if the EtherFabric functional units have been configured  */
static inline int efhw_nic_have_hw(struct efhw_nic *nic)
{
	return efhw_nic_have_functional_units(nic) && (EFHW_KVA(nic) != 0);
}

/*! Helper function to allocate the iobuffer needed by an eventq
 *   - it ensures the eventq has the correct alignment for the NIC
 *
 * \param rm        Event-queue resource manager
 * \param instance  Event-queue instance (index)
 * \param buf_bytes Requested size of eventq
 * \return          < 0 if iobuffer allocation fails
 */
int efhw_nic_event_queue_alloc_iobuffer(struct efhw_nic *nic,
					struct eventq_resource_hardware *h,
					int evq_instance, unsigned buf_bytes);

extern void falcon_nic_set_rx_usr_buf_size(struct efhw_nic *,
					   int rx_usr_buf_size);

/*! Get RX filter search limits from RX_FILTER_CTL_REG.
 *  use_raw_values = 0 to get actual depth of search, or 1 to get raw values
 *  from register.
 */
extern void
falcon_nic_get_rx_filter_search_limits(struct efhw_nic *nic,
				       struct efhw_filter_search_limits *lim,
				       int use_raw_values);

/*! Set RX filter search limits in RX_FILTER_CTL_REG.
 *  use_raw_values = 0 if specifying actual depth of search, or 1 if specifying
 *  raw values to write to the register.
 */
extern void
falcon_nic_set_rx_filter_search_limits(struct efhw_nic *nic,
				       struct efhw_filter_search_limits *lim,
				       int use_raw_values);


/*! Legacy RX IP filter search depth control interface */
extern void
falcon_nic_rx_filter_ctl_set(struct efhw_nic *nic, uint32_t tcp_full,
			     uint32_t tcp_wild,
			     uint32_t udp_full, uint32_t udp_wild);

/*! Legacy RX IP filter search depth control interface */
extern void
falcon_nic_rx_filter_ctl_get(struct efhw_nic *nic, uint32_t *tcp_full,
			     uint32_t *tcp_wild,
			     uint32_t *udp_full, uint32_t *udp_wild);

#endif /* __CI_EFHW_PUBLIC_H__ */
