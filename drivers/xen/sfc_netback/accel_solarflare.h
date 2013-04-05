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

#ifndef NETBACK_ACCEL_SOLARFLARE_H
#define NETBACK_ACCEL_SOLARFLARE_H

#include "accel.h"
#include "accel_msg_iface.h"

#include "driverlink_api.h"

#define MAX_NICS 5
#define MAX_PORTS 2


extern int netback_accel_sf_init(void);
extern void netback_accel_sf_shutdown(void);
extern int netback_accel_sf_hwtype(struct netback_accel *bend);

extern int netback_accel_sf_char_init(void);
extern void netback_accel_sf_char_shutdown(void);

extern int netback_accel_setup_vnic_hw(struct netback_accel *bend);
extern void netback_accel_shutdown_vnic_hw(struct netback_accel *bend);

extern int netback_accel_add_buffers(struct netback_accel *bend, int pages, 
				     int log2_pages, u32 *grants,
				     u32 *buf_addr_out);
extern int netback_accel_remove_buffers(struct netback_accel *bend);


/* Add a filter for the specified IP/port to the backend */
extern int
netback_accel_filter_check_add(struct netback_accel *bend, 
			       struct netback_accel_filter_spec *filt);
/* Remove a filter entry for the specific device and IP/port */
extern
void netback_accel_filter_remove_index(struct netback_accel *bend, 
				       int filter_index);
extern
void netback_accel_filter_remove_spec(struct netback_accel *bend, 
				      struct netback_accel_filter_spec *filt);

/* This is designed to look a bit like a skb */
struct netback_pkt_buf {
	union {
		unsigned char *raw;
	} mac;
	union {
		struct iphdr  *iph;
		struct arphdr *arph;
		unsigned char *raw;
	} nh;
	int protocol;
};

/*! \brief Handle a received packet: insert fast path filters as necessary
 * \param skb The packet buffer
 */
extern void netback_accel_rx_packet(struct netback_pkt_buf *skb, void *fwd_priv);

/*! \brief Handle a transmitted packet: update fast path filters as necessary
 * \param skb The packet buffer
 */
extern void netback_accel_tx_packet(struct sk_buff *skb, void *fwd_priv);

#endif /* NETBACK_ACCEL_SOLARFLARE_H */
