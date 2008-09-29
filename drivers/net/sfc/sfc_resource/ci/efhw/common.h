/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides API of the efhw library which may be used both from
 * the kernel and from the user-space code.
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

#ifndef __CI_EFHW_COMMON_H__
#define __CI_EFHW_COMMON_H__

#include <ci/efhw/common_sysdep.h>

typedef uint32_t efhw_buffer_addr_t;
#define EFHW_BUFFER_ADDR_FMT	"[ba:%"PRIx32"]"

/*! Comment? */
typedef union {
	uint64_t u64;
	struct {
		uint32_t a;
		uint32_t b;
	} opaque;
} efhw_event_t;

/* Flags for TX/RX queues */
#define EFHW_VI_JUMBO_EN           0x01    /*! scatter RX over multiple desc */
#define EFHW_VI_ISCSI_RX_HDIG_EN   0x02    /*! iscsi rx header digest */
#define EFHW_VI_ISCSI_TX_HDIG_EN   0x04    /*! iscsi tx header digest */
#define EFHW_VI_ISCSI_RX_DDIG_EN   0x08    /*! iscsi rx data digest */
#define EFHW_VI_ISCSI_TX_DDIG_EN   0x10    /*! iscsi tx data digest */
#define EFHW_VI_TX_PHYS_ADDR_EN    0x20    /*! TX physical address mode */
#define EFHW_VI_RX_PHYS_ADDR_EN    0x40    /*! RX physical address mode */
#define EFHW_VI_RM_WITH_INTERRUPT  0x80    /*! VI with an interrupt */
#define EFHW_VI_TX_IP_CSUM_DIS     0x100   /*! enable ip checksum generation */
#define EFHW_VI_TX_TCPUDP_CSUM_DIS 0x200   /*! enable tcp/udp checksum
					       generation */
#define EFHW_VI_TX_TCPUDP_ONLY     0x400   /*! drop non-tcp/udp packets */

/* Types of hardware filter */
/* Each of these values implicitly selects scatter filters on B0 - or in
   EFHW_IP_FILTER_TYPE_NOSCAT_B0_MASK if a non-scatter filter is required */
#define EFHW_IP_FILTER_TYPE_UDP_WILDCARD  (0)	/* dest host only */
#define EFHW_IP_FILTER_TYPE_UDP_FULL      (1)	/* dest host and port */
#define EFHW_IP_FILTER_TYPE_TCP_WILDCARD  (2)	/* dest based filter */
#define EFHW_IP_FILTER_TYPE_TCP_FULL      (3)	/* src  filter */
/* Same again, but with RSS (for B0 only) */
#define EFHW_IP_FILTER_TYPE_UDP_WILDCARD_RSS_B0  (4)
#define EFHW_IP_FILTER_TYPE_UDP_FULL_RSS_B0      (5)
#define EFHW_IP_FILTER_TYPE_TCP_WILDCARD_RSS_B0  (6)
#define EFHW_IP_FILTER_TYPE_TCP_FULL_RSS_B0      (7)

#define EFHW_IP_FILTER_TYPE_FULL_MASK      (0x1) /* Mask for full / wildcard */
#define EFHW_IP_FILTER_TYPE_TCP_MASK       (0x2) /* Mask for TCP type */
#define EFHW_IP_FILTER_TYPE_RSS_B0_MASK    (0x4) /* Mask for B0 RSS enable */
#define EFHW_IP_FILTER_TYPE_NOSCAT_B0_MASK (0x8) /* Mask for B0 SCATTER dsbl */

#define EFHW_IP_FILTER_TYPE_MASK	(0xffff) /* Mask of types above */

#define EFHW_IP_FILTER_BROADCAST	(0x10000) /* driverlink filter
						     support */

#endif /* __CI_EFHW_COMMON_H__ */
