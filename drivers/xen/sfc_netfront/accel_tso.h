/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
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

#ifndef NETFRONT_ACCEL_TSO_H
#define NETFRONT_ACCEL_TSO_H

#include "accel_bufs.h"

/* Track the buffers used in each output packet */
struct netfront_accel_tso_buffer {
	struct netfront_accel_tso_buffer *next;
	struct netfront_accel_pkt_desc *buf;
	unsigned length;
};

/* Track the output packets formed from each input packet */
struct netfront_accel_tso_output_packet {
	struct netfront_accel_tso_output_packet *next;
	struct netfront_accel_tso_buffer *tso_bufs;
	unsigned tso_bufs_len;
};


/*
 * Max available space in a buffer for data once meta-data has taken
 * its place 
 */
#define NETFRONT_ACCEL_TSO_BUF_LENGTH					\
	((PAGE_SIZE / NETFRONT_ACCEL_BUFS_PER_PAGE)			\
	 - sizeof(struct netfront_accel_tso_buffer)			\
	 - sizeof(struct netfront_accel_tso_output_packet))

int netfront_accel_enqueue_skb_tso(netfront_accel_vnic *vnic,
				   struct sk_buff *skb);

#endif /* NETFRONT_ACCEL_TSO_H */
