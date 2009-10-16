/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides EtherFabric NIC hardware interface.
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

#ifndef __CI_DRIVER_EFAB_HARDWARE_H__
#define __CI_DRIVER_EFAB_HARDWARE_H__

#include "ci/driver/efab/hardware/workarounds.h"
#include <ci/efhw/hardware_sysdep.h>


/*----------------------------------------------------------------------------
 *
 * Common EtherFabric definitions
 *
 *---------------------------------------------------------------------------*/

#include <ci/efhw/debug.h>
#include <ci/efhw/common.h>
#include <ci/driver/efab/hardware/common.h>

/*----------------------------------------------------------------------------
 *
 * EtherFabric varients
 *
 *---------------------------------------------------------------------------*/

#include <ci/driver/efab/hardware/falcon.h>

/*----------------------------------------------------------------------------
 *
 * EtherFabric Portable Hardware Layer defines
 *
 *---------------------------------------------------------------------------*/

  /*-------------- Initialisation ------------ */
#define efhw_nic_close_hardware(nic) \
	((nic)->efhw_func->close_hardware(nic))

#define efhw_nic_init_hardware(nic, ev_handlers, mac_addr, non_irq_evq) \
	((nic)->efhw_func->init_hardware((nic), (ev_handlers), (mac_addr), \
					 (non_irq_evq)))

/*-------------- Interrupt support  ------------ */
/** Handle interrupt.  Return 0 if not handled, 1 if handled. */
#define efhw_nic_interrupt(nic) \
	((nic)->efhw_func->interrupt(nic))

#define efhw_nic_interrupt_enable(nic) \
	((nic)->efhw_func->interrupt_enable(nic))

#define efhw_nic_interrupt_disable(nic) \
	((nic)->efhw_func->interrupt_disable(nic))

#define efhw_nic_set_interrupt_moderation(nic, evq, val)                 \
	((nic)->efhw_func->set_interrupt_moderation(nic, evq, val))

/*-------------- Event support  ------------ */

#define efhw_nic_event_queue_enable(nic, evq, size, q_base, buf_base,   \
				    interrupting)                       \
	((nic)->efhw_func->event_queue_enable((nic), (evq), (size), (q_base), \
					      (buf_base), (interrupting)))

#define efhw_nic_event_queue_disable(nic, evq, timer_only) \
	((nic)->efhw_func->event_queue_disable(nic, evq, timer_only))

#define efhw_nic_wakeup_request(nic, q_base, index, evq) \
	((nic)->efhw_func->wakeup_request(nic, q_base, index, evq))

#define efhw_nic_sw_event(nic, data, ev) \
	((nic)->efhw_func->sw_event(nic, data, ev))

/*-------------- Filter support  ------------ */
#define efhw_nic_ipfilter_set(nic, type, index, dmaq,		\
			      saddr, sport, daddr, dport)	\
	((nic)->efhw_func->ipfilter_set(nic, type, index, dmaq,	\
					saddr, sport, daddr, dport))

#define efhw_nic_ipfilter_clear(nic, index) \
	((nic)->efhw_func->ipfilter_clear(nic, index))

/*-------------- DMA support  ------------ */
#define efhw_nic_dmaq_tx_q_init(nic, dmaq, evq, owner, tag,		\
				dmaq_size, index, flags)		\
	((nic)->efhw_func->dmaq_tx_q_init(nic, dmaq, evq, owner, tag,	\
					  dmaq_size, index, flags))

#define efhw_nic_dmaq_rx_q_init(nic, dmaq, evq, owner, tag,		\
				dmaq_size, index, flags) \
	((nic)->efhw_func->dmaq_rx_q_init(nic, dmaq, evq, owner, tag,	\
					  dmaq_size, index, flags))

#define efhw_nic_dmaq_tx_q_disable(nic, dmaq) \
	((nic)->efhw_func->dmaq_tx_q_disable(nic, dmaq))

#define efhw_nic_dmaq_rx_q_disable(nic, dmaq) \
	((nic)->efhw_func->dmaq_rx_q_disable(nic, dmaq))

#define efhw_nic_flush_tx_dma_channel(nic, dmaq) \
	((nic)->efhw_func->flush_tx_dma_channel(nic, dmaq))

#define efhw_nic_flush_rx_dma_channel(nic, dmaq) \
	((nic)->efhw_func->flush_rx_dma_channel(nic, dmaq))

/*-------------- MAC Low level interface ---- */
#define efhw_gmac_get_mac_addr(nic) \
	((nic)->gmac->get_mac_addr((nic)->gmac))

/*-------------- Buffer table -------------- */
#define efhw_nic_buffer_table_set(nic, addr, bufsz, region,		\
				  own_id, buf_id)			\
	((nic)->efhw_func->buffer_table_set(nic, addr, bufsz, region,	\
					    own_id, buf_id))

#define efhw_nic_buffer_table_set_n(nic, buf_id, addr, bufsz,		\
				    region, n_pages, own_id) \
	((nic)->efhw_func->buffer_table_set_n(nic, buf_id, addr, bufsz,	\
					      region, n_pages, own_id))

#define efhw_nic_buffer_table_clear(nic, id, num) \
	((nic)->efhw_func->buffer_table_clear(nic, id, num))

#define efhw_nic_buffer_table_commit(nic) \
	((nic)->efhw_func->buffer_table_commit(nic))

/*-------------- New filter API ------------ */
#define efhw_nic_filter_set(nic, spec, index_out) \
	((nic)->efhw_func->filter_set(nic, spec, index_out))

#define efhw_nic_filter_clear(nic, type, index_out) \
	((nic)->efhw_func->filter_clear(nic, type, index_out))


/* --- DMA --- */
#define EFHW_DMA_ADDRMASK		(0xffffffffffffffffULL)

/* --- Buffers --- */
#define EFHW_BUFFER_ADDR		FALCON_BUFFER_4K_ADDR
#define EFHW_BUFFER_PAGE		FALCON_BUFFER_4K_PAGE
#define EFHW_BUFFER_OFF			FALCON_BUFFER_4K_OFF

/* --- Filters --- */
#define EFHW_IP_FILTER_NUM		FALCON_FILTER_TBL_NUM

#define EFHW_MAX_PAGE_SIZE		FALCON_MAX_PAGE_SIZE

#if PAGE_SIZE <= EFHW_MAX_PAGE_SIZE
#define EFHW_NIC_PAGE_SIZE PAGE_SIZE
#else
#define EFHW_NIC_PAGE_SIZE EFHW_MAX_PAGE_SIZE
#endif
#define EFHW_NIC_PAGE_MASK (~(EFHW_NIC_PAGE_SIZE-1))

#endif /* __CI_DRIVER_EFAB_HARDWARE_H__ */
