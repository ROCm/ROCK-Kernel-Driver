/*
 * eth1394.h -- Ethernet driver for Linux IEEE-1394 Subsystem
 *
 * Copyright (C) 2000 Bonin Franck <boninf@free.fr>
 *           (C) 2001 Ben Collins <bcollins@debian.org>
 *
 * Mainly based on work by Emanuel Pirker and Andreas E. Bombe
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __ETH1394_H
#define __ETH1394_H

/* Register for incoming packets. This is 8192 bytes, which supports up to
 * 1600mbs. We'll need to change this if that ever becomes "small" :)  */
#define ETHER1394_REGION_ADDR_LEN	8192
#define ETHER1394_REGION_ADDR		0xfffff0200000ULL
#define ETHER1394_REGION_ADDR_END	(ETHER1394_REGION_ADDR + ETHER1394_REGION_ADDR_LEN)

/* Node set == 64 */
#define NODE_SET			(ALL_NODES + 1)

/* Private structure for our ethernet driver */
struct eth1394_priv {
	struct net_device_stats stats;	/* Device stats			 */
	struct hpsb_host *host;		/* The card for this dev	 */
	unsigned char max_rec[NODE_SET];/* Max payload per node		 */
	unsigned char sspd[NODE_SET];	/* Max speed per node		 */
	u16 fifo_hi[ALL_NODES];		/* 16bit hi fifo offset per node */
	u32 fifo_lo[ALL_NODES];		/* 32bit lo fifo offset per node */
	u64 eui[ALL_NODES];		/* EUI-64 per node		 */
	spinlock_t lock;		/* Private lock			 */
};

struct host_info {
	struct list_head list;
	struct hpsb_host *host;
	struct net_device *dev;
};

/* This is our task struct. It's used for the complete_tq callback.  */
struct packet_task {
	struct sk_buff *skb;	/* Socket buffer we are sending */
	nodeid_t dest_node;	/* Destination of the packet */
	u64 addr;		/* Address */
	struct tq_struct tq;	/* The task */
};

/* IP1394 headers */
#include <asm/byteorder.h>

/* Unfragmented */
#if defined __BIG_ENDIAN_BITFIELD
struct eth1394_uf_hdr {
	u8 lf:2;
	u16 res:14;
	u16 ether_type;		/* Ethernet packet type */
} __attribute__((packed));
#elif defined __LITTLE_ENDIAN_BITFIELD
struct eth1394_uf_hdr {
	u16 res:14;
	u8 lf:2;
	u16 ether_type;
} __attribute__((packed));
#else
#error Unknown bit field type
#endif

/* First fragment */
#if defined __BIG_ENDIAN_BITFIELD
struct eth1394_ff_hdr {
	u8 lf:2;
	u8 res1:2;
	u16 dg_size:12;		/* Datagram size */
	u16 ether_type;		/* Ethernet packet type */
	u16 dgl;		/* Datagram label */
	u16 res2;
} __attribute__((packed));
#elif defined __LITTLE_ENDIAN_BITFIELD
struct eth1394_ff_hdr {
	u16 dg_size:12;
	u8 res1:2;
	u8 lf:2;
	u16 ether_type;
	u16 dgl;
	u16 res2;
} __attribute__((packed));
#else
#error Unknown bit field type
#endif

/* XXX: Subsequent fragments, including last */
#if defined __BIG_ENDIAN_BITFIELD
struct eth1394_sf_hdr {
	u8 lf:2;
	u8 res1:2;
	u16 dg_size:12;		/* Datagram size */
	u8 res2:6;
	u16 fg_off:10;		/* Fragment offset */
	u16 dgl;		/* Datagram label */
	u16 res3;
} __attribute__((packed));
#elif defined __LITTLE_ENDIAN_BITFIELD
struct eth1394_sf_hdr {
	u16 dg_size:12;
	u8 res1:2;
	u8 lf:2;
	u16 fg_off:10;
	u8 res2:6;
	u16 dgl;
	u16 res3;
} __attribute__((packed));
#else
#error Unknown bit field type
#endif

#if defined __BIG_ENDIAN_BITFIELD
struct eth1394_common_hdr {
	u8 lf:2;
	u16 pad1:14;
} __attribute__((packed));
#elif defined __LITTLE_ENDIAN_BITFIELD
struct eth1394_common_hdr {
	u16 pad1:14;
	u8 lf:2;
} __attribute__((packed));
#else
#error Unknown bit field type
#endif

struct eth1394_hdr_words {
	u16 word1;
	u16 word2;
	u16 word3;
	u16 word4;
};

union eth1394_hdr {
	struct eth1394_common_hdr common;
	struct eth1394_uf_hdr uf;
	struct eth1394_ff_hdr ff;
	struct eth1394_sf_hdr sf;
	struct eth1394_hdr_words words;
};

/* End of IP1394 headers */

/* Fragment types */
#define ETH1394_HDR_LF_UF	0	/* unfragmented		*/
#define ETH1394_HDR_LF_FF	1	/* first fragment	*/
#define ETH1394_HDR_LF_LF	2	/* last fragment	*/
#define ETH1394_HDR_LF_IF	3	/* interior fragment	*/

#define IP1394_HW_ADDR_LEN	16	/* As per RFC		*/

/* Our arp packet (ARPHRD_IEEE1394) */
struct eth1394_arp {
	u16 hw_type;		/* 0x0018	*/
	u16 proto_type;		/* 0x0806	*/
	u8 hw_addr_len;		/* 16 		*/
	u8 ip_addr_len;		/* 4		*/
	u16 opcode;		/* ARP Opcode	*/
	/* Above is exactly the same format as struct arphdr */

	u64 s_uniq_id;		/* Sender's 64bit EUI			*/
	u8 max_rec;		/* Sender's max packet size		*/
	u8 sspd;		/* Sender's max speed			*/
	u16 fifo_hi;		/* hi 16bits of sender's FIFO addr	*/
	u32 fifo_lo;		/* lo 32bits of sender's FIFO addr	*/
	u32 sip;		/* Sender's IP Address			*/
	u32 tip;		/* IP Address of requested hw addr	*/
};

/* Network timeout */
#define ETHER1394_TIMEOUT	100000

#endif /* __ETH1394_H */
