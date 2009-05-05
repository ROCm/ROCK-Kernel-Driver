/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains private API for VI resource.  The API is not designed
 * to be used outside of the SFC resource driver.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
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

#ifndef __CI_EFRM_VI_RESOURCE_PRIVATE_H__
#define __CI_EFRM_VI_RESOURCE_PRIVATE_H__

#include <ci/efhw/common.h>
#include <ci/efrm/vi_resource_manager.h>

extern struct vi_resource_manager *efrm_vi_manager;

/*************************************************************************/

extern void efrm_vi_rm_delayed_free(struct work_struct *data);

extern void efrm_vi_rm_salvage_flushed_vis(void);

void efrm_vi_rm_free_flushed_resource(struct vi_resource *virs);

void efrm_vi_rm_init_dmaq(struct vi_resource *virs, int queue_index,
			  struct efhw_nic *nic);

/*! Wakeup handler */
extern void efrm_handle_wakeup_event(struct efhw_nic *nic, unsigned id);

/*! Timeout handler */
extern void efrm_handle_timeout_event(struct efhw_nic *nic, unsigned id);

/*! DMA flush handler */
extern void efrm_handle_dmaq_flushed(struct efhw_nic *nic, unsigned id,
				   int rx_flush);

/*! SRAM update handler */
extern void efrm_handle_sram_event(struct efhw_nic *nic);

#endif /* __CI_EFRM_VI_RESOURCE_PRIVATE_H__ */
