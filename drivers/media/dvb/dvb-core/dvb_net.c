/* 
 * dvb_net.c
 *
 * Copyright (C) 2001 Convergence integrated media GmbH
 *                    Ralph Metzler <ralph@convergence.de>
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * ULE Decapsulation code:
 * Copyright (C) 2003 gcs - Global Communication & Services GmbH.
 *                and Institute for Computer Sciences
 *                    Salzburg University.
 *                    Hilmar Linder <hlinder@cosy.sbg.ac.at>
 *                and Wolfram Stering <wstering@cosy.sbg.ac.at>
 *
 * ULE Decaps according to draft-fair-ipdvb-ule-01.txt.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dvb/net.h>
#include <linux/uio.h>
#include <asm/uaccess.h>
#include <linux/crc32.h>

#include "dvb_demux.h"
#include "dvb_net.h"
#include "dvb_functions.h"


static inline __u32 iov_crc32( __u32 c, struct kvec *iov, unsigned int cnt )
{
	unsigned int j;
	for (j = 0; j < cnt; j++)
		c = crc32_be( c, iov[j].iov_base, iov[j].iov_len );
	return c;
}


#if 1
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif


#define DVB_NET_MULTICAST_MAX 10

#define isprint(c)	((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))

static void hexdump( const unsigned char *buf, unsigned short len )
{
	char str[80], octet[10];
	int ofs, i, l;

	for (ofs = 0; ofs < len; ofs += 16) {
		sprintf( str, "%03d: ", ofs );

		for (i = 0; i < 16; i++) {
			if ((i + ofs) < len)
				sprintf( octet, "%02x ", buf[ofs + i] );
			else
				strcpy( octet, "   " );

			strcat( str, octet );
		}
		strcat( str, "  " );
		l = strlen( str );

		for (i = 0; (i < 16) && ((i + ofs) < len); i++)
			str[l++] = isprint( buf[ofs + i] ) ? buf[ofs + i] : '.';

		str[l] = '\0';
		printk( KERN_WARNING "%s\n", str );
	}
}


struct dvb_net_priv {
	int in_use;
        struct net_device_stats stats;
        char name[6];
	u16 pid;
	struct dvb_net *host;
        struct dmx_demux *demux;
	struct dmx_section_feed *secfeed;
	struct dmx_section_filter *secfilter;
	struct dmx_ts_feed *tsfeed;
	int multi_num;
	struct dmx_section_filter *multi_secfilter[DVB_NET_MULTICAST_MAX];
	unsigned char multi_macs[DVB_NET_MULTICAST_MAX][6];
	int rx_mode;
#define RX_MODE_UNI 0
#define RX_MODE_MULTI 1
#define RX_MODE_ALL_MULTI 2
#define RX_MODE_PROMISC 3
	struct work_struct set_multicast_list_wq;
	struct work_struct restart_net_feed_wq;
	unsigned char feedtype;
	int need_pusi;
	unsigned char tscc;			/* TS continuity counter after sync. */
	struct sk_buff *ule_skb;
	unsigned short ule_sndu_len;
	unsigned short ule_sndu_type;
	unsigned char ule_sndu_type_1;
	unsigned char ule_dbit;			/* whether the DestMAC address present
						 * bit is set or not. */
	unsigned char ule_ethhdr_complete;	/* whether we have completed the Ethernet
						 * header for the current ULE SNDU. */
	int ule_sndu_remain;
};


/**
 *	Determine the packet's protocol ID. The rule here is that we 
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 *
 *  stolen from eth.c out of the linux kernel, hacked for dvb-device
 *  by Michael Holzt <kju@debian.org>
 */
static unsigned short dvb_net_eth_type_trans(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;
	
	skb->mac.raw=skb->data;
	skb_pull(skb,dev->hard_header_len);
	eth= skb->mac.ethernet;
	
	if (*eth->h_dest & 1) {
		if(memcmp(eth->h_dest,dev->broadcast, ETH_ALEN)==0)
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}
	
	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;
		
	rawp = skb->data;
	
	/**
	 *	This is a magic hack to spot IPX packets. Older Novell breaks
	 *	the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *	layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *	won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);
		
	/**
	 *	Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

#define TS_SZ	188
#define TS_SYNC	0x47
#define TS_TEI	0x80
#define TS_PUSI	0x40
#define TS_AF_A	0x20
#define TS_AF_D	0x10

#define ULE_TEST	0
#define ULE_BRIDGED	1
#define ULE_LLC		2

static inline void reset_ule( struct dvb_net_priv *p )
{
	p->ule_skb = NULL;
	p->ule_sndu_len = 0;
	p->ule_sndu_type = 0;
	p->ule_sndu_type_1 = 0;
	p->ule_sndu_remain = 0;
	p->ule_dbit = 0xFF;
	p->ule_ethhdr_complete = 0;
}

static const char eth_dest_addr[] = { 0x0b, 0x0a, 0x09, 0x08, 0x04, 0x03 };

static void dvb_net_ule( struct net_device *dev, const u8 *buf, size_t buf_len )
{
	struct dvb_net_priv *priv = (struct dvb_net_priv *)dev->priv;
	unsigned long skipped = 0L, skblen = 0L;
	u8 *ts, *ts_end, *from_where = NULL, ts_remain = 0, how_much = 0, new_ts = 1;
	struct ethhdr *ethh = NULL;
	unsigned int emergency_count = 0;

	if (dev == NULL) {
		printk( KERN_ERR "NO netdev struct!\n" );
		return;
	}

	for (ts = (char *)buf, ts_end = (char *)buf + buf_len; ts < ts_end; ) {

		if (emergency_count++ > 200) {
			/* Huh?? */
			hexdump(ts, TS_SZ);
			printk(KERN_WARNING "*** LOOP ALERT! ts %p ts_remain %u "
				"how_much %u, ule_skb %p, ule_len %u, ule_remain %u\n",
				ts, ts_remain, how_much, priv->ule_skb,
				priv->ule_sndu_len, priv->ule_sndu_remain);
			break;
		}

		if (new_ts) {
			if ((ts[0] != TS_SYNC) || (ts[1] & TS_TEI)) {
				printk(KERN_WARNING "Invalid TS cell: SYNC %#x, TEI %u.\n",
				       ts[0], ts[1] & TS_TEI >> 7);
				continue;
			}
			ts_remain = 184;
			from_where = ts + 4;
		}
		/* Synchronize on PUSI, if required. */
		if (priv->need_pusi) {
			if (ts[1] & TS_PUSI) {
				/* Find beginning of first ULE SNDU in current TS cell.
				 * priv->need_pusi = 0; */
				priv->tscc = ts[3] & 0x0F;
				/* There is a pointer field here. */
				if (ts[4] > ts_remain) {
					printk(KERN_ERR "Invalid ULE packet "
					       "(pointer field %d)\n", ts[4]);
					continue;
				}
				from_where = &ts[5] + ts[4];
				ts_remain -= 1 + ts[4];
				skipped = 0;
			} else {
				skipped++;
				continue;
			}
		}

		/* Check continuity counter. */
		if (new_ts) {
			if ((ts[3] & 0x0F) == priv->tscc)
				priv->tscc = (priv->tscc + 1) & 0x0F;
			else {
				/* TS discontinuity handling: */
				if (priv->ule_skb) {
					dev_kfree_skb( priv->ule_skb );
					/* Prepare for next SNDU. */
					reset_ule(priv);
					((struct dvb_net_priv *) dev->priv)->stats.rx_errors++;
					((struct dvb_net_priv *) dev->priv)->stats.rx_frame_errors++;
				}
				/* skip to next PUSI. */
				printk(KERN_WARNING "TS discontinuity: got %#x, "
				       "exptected %#x.\n", ts[3] & 0x0F, priv->tscc);
				priv->need_pusi = 1;
				continue;
			}
			/* If we still have an incomplete payload, but PUSI is
			 * set, some TS cells are missing.
			 * This is only possible here, if we missed exactly 16 TS
			 * cells (continuity counter). */
			if (ts[1] & TS_PUSI) {
				if (! priv->need_pusi) {
					/* printk(KERN_WARNING "Skipping pointer field %u.\n", *from_where); */
					if (*from_where > 181) {
						printk(KERN_WARNING "*** Invalid pointer "
						       "field: %u.  Current TS cell "
						       "follows:\n", *from_where);
						hexdump( ts, TS_SZ );
						printk(KERN_WARNING "-------------------\n");
					}
					/* Skip pointer field (we're processing a
					 * packed payload). */
					from_where += 1;
					ts_remain -= 1;
				} else
					priv->need_pusi = 0;

				if (priv->ule_sndu_remain > 183) {
					((struct dvb_net_priv *) dev->priv)->stats.rx_errors++;
					((struct dvb_net_priv *) dev->priv)->stats.rx_length_errors++;
					printk(KERN_WARNING "Expected %d more SNDU bytes, but "
					       "got PUSI.  Flushing incomplete payload.\n",
					       priv->ule_sndu_remain);
					dev_kfree_skb(priv->ule_skb);
					/* Prepare for next SNDU. */
					reset_ule(priv);
				}
			}
		}

		/* Check if new payload needs to be started. */
		if (priv->ule_skb == NULL) {
			/* Start a new payload w/ skb.
			 * Find ULE header.  It is only guaranteed that the
			 * length field (2 bytes) is contained in the current
			 * TS.
			 * Check ts_remain has to be >= 2 here. */
			if (ts_remain < 2) {
				printk(KERN_WARNING "Invalid payload packing: only %d "
				       "bytes left in TS.  Resyncing.\n", ts_remain);
				priv->ule_sndu_len = 0;
				priv->need_pusi = 1;
				continue;
			}

			if (! priv->ule_sndu_len) {
				priv->ule_sndu_len = from_where[0] << 8 | from_where[1];
				if (priv->ule_sndu_len & 0x8000) {
					/* D-Bit is set: no dest mac present. */
					priv->ule_sndu_len &= 0x7FFF;
					priv->ule_dbit = 1;
				} else
					priv->ule_dbit = 0;

				/* printk(KERN_WARNING "ULE D-Bit: %d, SNDU len %u.\n",
				          priv->ule_dbit, priv->ule_sndu_len); */

				if (priv->ule_sndu_len > 32763) {
					printk(KERN_WARNING "Invalid ULE SNDU length %u. "
					       "Resyncing.\n", priv->ule_sndu_len);
					hexdump(ts, TS_SZ);
					priv->ule_sndu_len = 0;
					priv->need_pusi = 1;
					new_ts = 1;
					ts += TS_SZ;
					continue;
				}
				ts_remain -= 2;	/* consume the 2 bytes SNDU length. */
				from_where += 2;
			}

			/*
			 * State of current TS:
			 *   ts_remain (remaining bytes in the current TS cell)
			 *   0	ule_type is not available now, we need the next TS cell
			 *   1	the first byte of the ule_type is present
			 * >=2	full ULE header present, maybe some payload data as well.
			 */
			switch (ts_remain) {
				case 1:
					priv->ule_sndu_type = from_where[0] << 8;
					priv->ule_sndu_type_1 = 1; /* first byte of ule_type is set. */
					/* ts_remain -= 1; from_where += 1;
					 *   here not necessary, because we continue. */
				case 0:
					new_ts = 1;
					ts += TS_SZ;
					continue;

				default: /* complete ULE header is present in current TS. */
					/* Extract ULE type field. */
					if (priv->ule_sndu_type_1) {
						priv->ule_sndu_type |= from_where[0];
						from_where += 1; /* points to payload start. */
						ts_remain -= 1;
					} else {
						/* Complete type is present in new TS. */
						priv->ule_sndu_type = from_where[0] << 8 | from_where[1];
						from_where += 2; /* points to payload start. */
						ts_remain -= 2;
					}
					break;
			}

			if (priv->ule_sndu_type == ULE_TEST) {
				/* Test SNDU, discarded by the receiver. */
				printk(KERN_WARNING "Discarding ULE Test SNDU (%d bytes). "
				       "Resyncing.\n", priv->ule_sndu_len);
				priv->ule_sndu_len = 0;
				priv->need_pusi = 1;
				continue;
			}

			skblen = priv->ule_sndu_len;	/* Including CRC32 */
			if (priv->ule_sndu_type != ULE_BRIDGED) {
				skblen += ETH_HLEN;
#if 1
				if (! priv->ule_dbit)
					skblen -= ETH_ALEN;
#endif
			}
			priv->ule_skb = dev_alloc_skb(skblen);
			if (priv->ule_skb == NULL) {
				printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
				       dev->name);
				((struct dvb_net_priv *)dev->priv)->stats.rx_dropped++;
				return;
			}

#if 0
			if (priv->ule_sndu_type != ULE_BRIDGED) {
				// skb_reserve(priv->ule_skb, 2);    /* longword align L3 header */
				// Create Ethernet header.
				ethh = (struct ethhdr *)skb_put( priv->ule_skb, ETH_HLEN );
				memset( ethh->h_source, 0x00, ETH_ALEN );
				if (priv->ule_dbit) {
					// Dest MAC address not present --> generate our own.
					memcpy( ethh->h_dest, eth_dest_addr, ETH_ALEN );
				} else {
					// Dest MAC address could be split across two TS cells.
					// FIXME: implement.

					printk( KERN_WARNING "%s: got destination MAC "
						"address.\n", dev->name );
					memcpy( ethh->h_dest, eth_dest_addr, ETH_ALEN );
				}
				ethh->h_proto = htons(priv->ule_sndu_type == ULE_LLC ?
						      priv->ule_sndu_len : priv->ule_sndu_type);
			}
#endif
			/* this includes the CRC32 _and_ dest mac, if !dbit! */
			priv->ule_sndu_remain = priv->ule_sndu_len;
			priv->ule_skb->dev = dev;
		}

		/* Copy data into our current skb. */
		how_much = min(priv->ule_sndu_remain, (int)ts_remain);
		if ((priv->ule_ethhdr_complete < ETH_ALEN) &&
		    (priv->ule_sndu_type != ULE_BRIDGED)) {
			ethh = (struct ethhdr *)priv->ule_skb->data;
			if (! priv->ule_dbit) {
				if (how_much >= (ETH_ALEN - priv->ule_ethhdr_complete)) {
					/* copy dest mac address. */
					memcpy(skb_put(priv->ule_skb,
						       (ETH_ALEN - priv->ule_ethhdr_complete)),
					       from_where,
					       (ETH_ALEN - priv->ule_ethhdr_complete));
					memset(ethh->h_source, 0x00, ETH_ALEN);
					ethh->h_proto = htons(priv->ule_sndu_type == ULE_LLC ?
							      priv->ule_sndu_len :
							      priv->ule_sndu_type);
					skb_put(priv->ule_skb, ETH_ALEN + 2);

					how_much -= (ETH_ALEN - priv->ule_ethhdr_complete);
					priv->ule_sndu_remain -= (ETH_ALEN -
								  priv->ule_ethhdr_complete);
					ts_remain -= (ETH_ALEN - priv->ule_ethhdr_complete);
					from_where += (ETH_ALEN - priv->ule_ethhdr_complete);
					priv->ule_ethhdr_complete = ETH_ALEN;
				}
			} else {
				/* Generate whole Ethernet header. */
				memcpy(ethh->h_dest, eth_dest_addr, ETH_ALEN);
				memset(ethh->h_source, 0x00, ETH_ALEN);
				ethh->h_proto = htons(priv->ule_sndu_type == ULE_LLC ?
						      priv->ule_sndu_len : priv->ule_sndu_type);
				skb_put(priv->ule_skb, ETH_HLEN);
				priv->ule_ethhdr_complete = ETH_ALEN;
			}
		}
		/* printk(KERN_WARNING "Copying %u bytes, ule_sndu_remain = %u, "
		          "ule_sndu_len = %u.\n", how_much, priv->ule_sndu_remain,
			  priv->ule_sndu_len); */
		memcpy(skb_put(priv->ule_skb, how_much), from_where, how_much);
		priv->ule_sndu_remain -= how_much;
		ts_remain -= how_much;
		from_where += how_much;

		if ((priv->ule_ethhdr_complete < ETH_ALEN) &&
		    (priv->ule_sndu_type != ULE_BRIDGED)) {
			priv->ule_ethhdr_complete += how_much;
		}

		/* Check for complete payload. */
		if (priv->ule_sndu_remain <= 0) {
			/* Check CRC32, we've got it in our skb already. */
			unsigned short ulen = htons(priv->ule_sndu_len);
			unsigned short utype = htons(priv->ule_sndu_type);
			struct kvec iov[4] = {
				{ &ulen, sizeof ulen },
				{ &utype, sizeof utype },
				{ NULL, 0 },
				{ priv->ule_skb->data + ETH_HLEN,
					priv->ule_skb->len - ETH_HLEN - 4 }
			};
			unsigned long ule_crc = ~0L, expected_crc;
			if (priv->ule_dbit) {
				/* Set D-bit for CRC32 verification,
				 * if it was set originally. */
				ulen |= 0x0080;
			} else {
				iov[2].iov_base = priv->ule_skb->data;
				iov[2].iov_len = ETH_ALEN;
			}
			ule_crc = iov_crc32(ule_crc, iov, 4);
			expected_crc = *((u8 *)priv->ule_skb->tail - 4) << 24 |
				*((u8 *)priv->ule_skb->tail - 3) << 16 |
				*((u8 *)priv->ule_skb->tail - 2) << 8 |
				*((u8 *)priv->ule_skb->tail - 1);
			if (ule_crc != expected_crc) {
				printk(KERN_WARNING "CRC32 check %s: %#lx / %#lx.\n",
				       ule_crc != expected_crc ? "FAILED" : "OK",
				       ule_crc, expected_crc);
				hexdump(priv->ule_skb->data + ETH_HLEN,
					priv->ule_skb->len - ETH_HLEN);

				((struct dvb_net_priv *) dev->priv)->stats.rx_errors++;
				((struct dvb_net_priv *) dev->priv)->stats.rx_crc_errors++;
				dev_kfree_skb(priv->ule_skb);
			} else {
				/* CRC32 was OK. Remove it from skb. */
				priv->ule_skb->tail -= 4;
				priv->ule_skb->len -= 4;
				/* Stuff into kernel's protocol stack. */
				priv->ule_skb->protocol = dvb_net_eth_type_trans(priv->ule_skb, dev);
				/* If D-bit is set (i.e. destination MAC address not present),
				 * receive the packet anyhw. */
				/* if (priv->ule_dbit && skb->pkt_type == PACKET_OTHERHOST) */
					priv->ule_skb->pkt_type = PACKET_HOST;
				((struct dvb_net_priv *) dev->priv)->stats.rx_packets++;
				((struct dvb_net_priv *) dev->priv)->stats.rx_bytes += priv->ule_skb->len;
				netif_rx(priv->ule_skb);
			}
			/* Prepare for next SNDU. */
			reset_ule(priv);
		}

		/* More data in current TS (look at the bytes following the CRC32)? */
		if (ts_remain >= 2 && *((unsigned short *)from_where) != 0xFFFF) {
			/* Next ULE SNDU starts right there. */
			new_ts = 0;
			priv->ule_skb = NULL;
			priv->ule_sndu_type_1 = 0;
			priv->ule_sndu_len = 0;
			// printk(KERN_WARNING "More data in current TS: [%#x %#x %#x %#x]\n",
			//	*(from_where + 0), *(from_where + 1),
			//	*(from_where + 2), *(from_where + 3));
			// printk(KERN_WARNING "ts @ %p, stopped @ %p:\n", ts, from_where + 0);
			// hexdump(ts, 188);
		} else {
			new_ts = 1;
			ts += TS_SZ;
			if (priv->ule_skb == NULL) {
				priv->need_pusi = 1;
				priv->ule_sndu_type_1 = 0;
				priv->ule_sndu_len = 0;
			}
		}
	}	/* for all available TS cells */
}

static int dvb_net_ts_callback(const u8 *buffer1, size_t buffer1_len,
			       const u8 *buffer2, size_t buffer2_len,
			       struct dmx_ts_feed *feed, enum dmx_success success)
{
	struct net_device *dev = (struct net_device *)feed->priv;

	if (buffer2 != 0)
		printk(KERN_WARNING "buffer2 not 0: %p.\n", buffer2);
	if (buffer1_len > 32768)
		printk(KERN_WARNING "length > 32k: %u.\n", buffer1_len);
	/* printk("TS callback: %u bytes, %u TS cells @ %p.\n",
	          buffer1_len, buffer1_len / TS_SZ, buffer1); */
	dvb_net_ule(dev, buffer1, buffer1_len);
	return 0;
}


static void dvb_net_sec(struct net_device *dev, u8 *pkt, int pkt_len)
{
        u8 *eth;
        struct sk_buff *skb;
	struct net_device_stats *stats = &(((struct dvb_net_priv *) dev->priv)->stats);

	/* note: pkt_len includes a 32bit checksum */
	if (pkt_len < 16) {
		printk("%s: IP/MPE packet length = %d too small.\n",
			dev->name, pkt_len);
		stats->rx_errors++;
		stats->rx_length_errors++;
		return;
	}
/* it seems some ISPs manage to screw up here, so we have to
 * relax the error checks... */
#if 0
	if ((pkt[5] & 0xfd) != 0xc1) {
		/* drop scrambled or broken packets */
#else
	if ((pkt[5] & 0x3c) != 0x00) {
		/* drop scrambled */
#endif
		stats->rx_errors++;
		stats->rx_crc_errors++;
		return;
	}
	if (pkt[5] & 0x02) {
		//FIXME: handle LLC/SNAP
                stats->rx_dropped++;
                return;
        }
	if (pkt[7]) {
		/* FIXME: assemble datagram from multiple sections */
		stats->rx_errors++;
		stats->rx_frame_errors++;
		return;
	}

	/* we have 14 byte ethernet header (ip header follows);
	 * 12 byte MPE header; 4 byte checksum; + 2 byte alignment
	 */
	if (!(skb = dev_alloc_skb(pkt_len - 4 - 12 + 14 + 2))) {
		//printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
		stats->rx_dropped++;
		return;
	}
	skb_reserve(skb, 2);    /* longword align L3 header */
	skb->dev = dev;

	/* copy L3 payload */
	eth = (u8 *) skb_put(skb, pkt_len - 12 - 4 + 14);
	memcpy(eth + 14, pkt + 12, pkt_len - 12 - 4);

	/* create ethernet header: */
        eth[0]=pkt[0x0b];
        eth[1]=pkt[0x0a];
        eth[2]=pkt[0x09];
        eth[3]=pkt[0x08];
        eth[4]=pkt[0x04];
        eth[5]=pkt[0x03];

        eth[6]=eth[7]=eth[8]=eth[9]=eth[10]=eth[11]=0;

	eth[12] = 0x08;	/* ETH_P_IP */
	eth[13] = 0x00;

	skb->protocol = dvb_net_eth_type_trans(skb, dev);
        
	stats->rx_packets++;
	stats->rx_bytes+=skb->len;
        netif_rx(skb);
}
 
static int dvb_net_sec_callback(const u8 *buffer1, size_t buffer1_len,
		 const u8 *buffer2, size_t buffer2_len,
		 struct dmx_section_filter *filter,
		 enum dmx_success success)
{
        struct net_device *dev=(struct net_device *) filter->priv;

	/**
	 * we rely on the DVB API definition where exactly one complete
	 * section is delivered in buffer1
	 */
	dvb_net_sec (dev, (u8*) buffer1, buffer1_len);
	return 0;
}

static int dvb_net_tx(struct sk_buff *skb, struct net_device *dev)
{
	return 0;
}

static u8 mask_normal[6]={0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static u8 mask_allmulti[6]={0xff, 0xff, 0xff, 0x00, 0x00, 0x00};
static u8 mac_allmulti[6]={0x01, 0x00, 0x5e, 0x00, 0x00, 0x00};
static u8 mask_promisc[6]={0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static int dvb_net_filter_sec_set(struct net_device *dev,
		   struct dmx_section_filter **secfilter,
		   u8 *mac, u8 *mac_mask)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;
	int ret;

	*secfilter=NULL;
	ret = priv->secfeed->allocate_filter(priv->secfeed, secfilter);
	if (ret<0) {
		printk("%s: could not get filter\n", dev->name);
		return ret;
	}

	(*secfilter)->priv=(void *) dev;

	memset((*secfilter)->filter_value, 0x00, DMX_MAX_FILTER_SIZE);
	memset((*secfilter)->filter_mask,  0x00, DMX_MAX_FILTER_SIZE);
	memset((*secfilter)->filter_mode,  0xff, DMX_MAX_FILTER_SIZE);

	(*secfilter)->filter_value[0]=0x3e;
	(*secfilter)->filter_value[3]=mac[5];
	(*secfilter)->filter_value[4]=mac[4];
	(*secfilter)->filter_value[8]=mac[3];
	(*secfilter)->filter_value[9]=mac[2];
	(*secfilter)->filter_value[10]=mac[1];
	(*secfilter)->filter_value[11]=mac[0];

	(*secfilter)->filter_mask[0] = 0xff;
	(*secfilter)->filter_mask[3] = mac_mask[5];
	(*secfilter)->filter_mask[4] = mac_mask[4];
	(*secfilter)->filter_mask[8] = mac_mask[3];
	(*secfilter)->filter_mask[9] = mac_mask[2];
	(*secfilter)->filter_mask[10] = mac_mask[1];
	(*secfilter)->filter_mask[11]=mac_mask[0];

	dprintk("%s: filter mac=%02x %02x %02x %02x %02x %02x\n",
	       dev->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	dprintk("%s: filter mask=%02x %02x %02x %02x %02x %02x\n",
	       dev->name, mac_mask[0], mac_mask[1], mac_mask[2],
	       mac_mask[3], mac_mask[4], mac_mask[5]);

	return 0;
}

static int dvb_net_feed_start(struct net_device *dev)
{
	int ret, i;
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;
        struct dmx_demux *demux = priv->demux;
        unsigned char *mac = (unsigned char *) dev->dev_addr;
		
	dprintk("%s: rx_mode %i\n", __FUNCTION__, priv->rx_mode);
	if (priv->secfeed || priv->secfilter || priv->multi_secfilter[0])
		printk("%s: BUG %d\n", __FUNCTION__, __LINE__);

	priv->secfeed=NULL;
	priv->secfilter=NULL;
	priv->tsfeed = NULL;

	if (priv->feedtype == DVB_NET_FEEDTYPE_MPE) {
	dprintk("%s: alloc secfeed\n", __FUNCTION__);
	ret=demux->allocate_section_feed(demux, &priv->secfeed, 
					 dvb_net_sec_callback);
	if (ret<0) {
		printk("%s: could not allocate section feed\n", dev->name);
		return ret;
	}

	ret = priv->secfeed->set(priv->secfeed, priv->pid, 32768, 0, 1);

	if (ret<0) {
		printk("%s: could not set section feed\n", dev->name);
		priv->demux->release_section_feed(priv->demux, priv->secfeed);
		priv->secfeed=NULL;
		return ret;
	}

	if (priv->rx_mode != RX_MODE_PROMISC) {
		dprintk("%s: set secfilter\n", __FUNCTION__);
			dvb_net_filter_sec_set(dev, &priv->secfilter, mac, mask_normal);
	}

	switch (priv->rx_mode) {
	case RX_MODE_MULTI:
		for (i = 0; i < priv->multi_num; i++) {
			dprintk("%s: set multi_secfilter[%d]\n", __FUNCTION__, i);
				dvb_net_filter_sec_set(dev, &priv->multi_secfilter[i],
					   priv->multi_macs[i], mask_normal);
		}
		break;
	case RX_MODE_ALL_MULTI:
		priv->multi_num=1;
		dprintk("%s: set multi_secfilter[0]\n", __FUNCTION__);
			dvb_net_filter_sec_set(dev, &priv->multi_secfilter[0],
				   mac_allmulti, mask_allmulti);
		break;
	case RX_MODE_PROMISC:
		priv->multi_num=0;
		dprintk("%s: set secfilter\n", __FUNCTION__);
			dvb_net_filter_sec_set(dev, &priv->secfilter, mac, mask_promisc);
		break;
	}
	
	dprintk("%s: start filtering\n", __FUNCTION__);
	priv->secfeed->start_filtering(priv->secfeed);
	} else if (priv->feedtype == DVB_NET_FEEDTYPE_ULE) {
		struct timespec timeout = { 0, 30000000 }; // 30 msec

		/* we have payloads encapsulated in TS */
		dprintk("%s: alloc tsfeed\n", __FUNCTION__);
		ret = demux->allocate_ts_feed(demux, &priv->tsfeed, dvb_net_ts_callback);
		if (ret < 0) {
			printk("%s: could not allocate ts feed\n", dev->name);
			return ret;
		}

		/* Set netdevice pointer for ts decaps callback. */
		priv->tsfeed->priv = (void *)dev;
		ret = priv->tsfeed->set(priv->tsfeed, priv->pid,
					TS_PACKET, DMX_TS_PES_OTHER,
					188 * 100, /* nr. of bytes delivered per callback */
					32768,     /* circular buffer size */
					0,         /* descramble */
					timeout);

		if (ret < 0) {
			printk("%s: could not set ts feed\n", dev->name);
			priv->demux->release_ts_feed(priv->demux, priv->tsfeed);
			priv->tsfeed = NULL;
			return ret;
		}

		dprintk("%s: start filtering\n", __FUNCTION__);
		priv->tsfeed->start_filtering(priv->tsfeed);
	} else
		return -EINVAL;

	return 0;
}

static int dvb_net_feed_stop(struct net_device *dev)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;
	int i;

	dprintk("%s\n", __FUNCTION__);
	if (priv->feedtype == DVB_NET_FEEDTYPE_MPE) {
        if (priv->secfeed) {
		if (priv->secfeed->is_filtering) {
			dprintk("%s: stop secfeed\n", __FUNCTION__);
		        priv->secfeed->stop_filtering(priv->secfeed);
		}

		if (priv->secfilter) {
			dprintk("%s: release secfilter\n", __FUNCTION__);
			priv->secfeed->release_filter(priv->secfeed,
					       priv->secfilter);
		priv->secfilter=NULL;
		}

		for (i=0; i<priv->multi_num; i++) {
			if (priv->multi_secfilter[i]) {
					dprintk("%s: release multi_filter[%d]\n",
						__FUNCTION__, i);
				priv->secfeed->release_filter(priv->secfeed,
						       priv->multi_secfilter[i]);
			priv->multi_secfilter[i]=NULL;
		}
		}

		priv->demux->release_section_feed(priv->demux, priv->secfeed);
		priv->secfeed=NULL;
	} else
		printk("%s: no feed to stop\n", dev->name);
	} else if (priv->feedtype == DVB_NET_FEEDTYPE_ULE) {
		if (priv->tsfeed) {
			if (priv->tsfeed->is_filtering) {
				dprintk("%s: stop tsfeed\n", __FUNCTION__);
				priv->tsfeed->stop_filtering(priv->tsfeed);
			}
			priv->demux->release_ts_feed(priv->demux, priv->tsfeed);
			priv->tsfeed = NULL;
		}
		else
			printk("%s: no ts feed to stop\n", dev->name);
	} else
		return -EINVAL;
	return 0;
}


static int dvb_set_mc_filter (struct net_device *dev, struct dev_mc_list *mc)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;

	if (priv->multi_num == DVB_NET_MULTICAST_MAX)
		return -ENOMEM;

	memcpy(priv->multi_macs[priv->multi_num], mc->dmi_addr, 6);

	priv->multi_num++;
	return 0;
}


static void wq_set_multicast_list (void *data)
{
	struct net_device *dev = data;
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;

	dvb_net_feed_stop(dev);

	priv->rx_mode = RX_MODE_UNI;
	
	if(dev->flags & IFF_PROMISC) {
		dprintk("%s: promiscuous mode\n", dev->name);
		priv->rx_mode = RX_MODE_PROMISC;
	} else if ((dev->flags & IFF_ALLMULTI)) {
		dprintk("%s: allmulti mode\n", dev->name);
		priv->rx_mode = RX_MODE_ALL_MULTI;
	} else if (dev->mc_count) {
		int mci;
		struct dev_mc_list *mc;

		dprintk("%s: set_mc_list, %d entries\n",
			dev->name, dev->mc_count);

		priv->rx_mode = RX_MODE_MULTI;
		priv->multi_num = 0;

		for (mci = 0, mc=dev->mc_list; 
		     mci < dev->mc_count;
		     mc = mc->next, mci++) {
			dvb_set_mc_filter(dev, mc);
		}
	}

		dvb_net_feed_start(dev);
	}


static void dvb_net_set_multicast_list (struct net_device *dev)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;
	schedule_work(&priv->set_multicast_list_wq);
}


static int dvb_net_set_config(struct net_device *dev, struct ifmap *map)
{
	if (netif_running(dev))
		return -EBUSY;
	return 0;
}


static void wq_restart_net_feed (void *data)
{
	struct net_device *dev = data;

	if (netif_running(dev)) {
		dvb_net_feed_stop(dev);
		dvb_net_feed_start(dev);
	}
}


static int dvb_net_set_mac (struct net_device *dev, void *p)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;
	struct sockaddr *addr=p;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	if (netif_running(dev))
		schedule_work(&priv->restart_net_feed_wq);

	return 0;
}


static int dvb_net_open(struct net_device *dev)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;

	priv->in_use++;
	dvb_net_feed_start(dev);
	return 0;
}


static int dvb_net_stop(struct net_device *dev)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;

	priv->in_use--;
        return dvb_net_feed_stop(dev);
}

static struct net_device_stats * dvb_net_get_stats(struct net_device *dev)
{
        return &((struct dvb_net_priv*) dev->priv)->stats;
}


static void dvb_net_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->open		= dvb_net_open;
	dev->stop		= dvb_net_stop;
	dev->hard_start_xmit	= dvb_net_tx;
	dev->get_stats		= dvb_net_get_stats;
	dev->set_multicast_list = dvb_net_set_multicast_list;
	dev->set_config         = dvb_net_set_config;
	dev->set_mac_address    = dvb_net_set_mac;
	dev->mtu		= 4096;
	dev->mc_count           = 0;
	dev->hard_header_cache  = NULL;

	dev->flags |= IFF_NOARP;
}

static int get_if(struct dvb_net *dvbnet)
{
	int i;

	for (i=0; i<DVB_NET_DEVICES_MAX; i++)
		if (!dvbnet->state[i])
			break;

	if (i == DVB_NET_DEVICES_MAX)
		return -1;

	dvbnet->state[i]=1;
	return i;
}

static int dvb_net_add_if(struct dvb_net *dvbnet, u16 pid, u8 feedtype)
{
        struct net_device *net;
	struct dvb_net_priv *priv;
	int result;
	int if_num;
 
	if (feedtype != DVB_NET_FEEDTYPE_MPE && feedtype != DVB_NET_FEEDTYPE_ULE)
		return -EINVAL;
	if ((if_num = get_if(dvbnet)) < 0)
		return -EINVAL;

	net = alloc_netdev(sizeof(struct dvb_net_priv), "dvb",
			   dvb_net_setup);
	if (!net)
		return -ENOMEM;
	
	sprintf(net->name, "dvb%d_%d", dvbnet->dvbdev->adapter->num, if_num);

	net->addr_len  		= 6;
	memcpy(net->dev_addr, dvbnet->dvbdev->adapter->proposed_mac, 6);

	dvbnet->device[if_num] = net;
	
	priv = net->priv;
        priv->demux = dvbnet->demux;
        priv->pid = pid;
	priv->rx_mode = RX_MODE_UNI;
	priv->need_pusi = 1;
	priv->tscc = 0;
	priv->feedtype = feedtype;
	reset_ule(priv);

	INIT_WORK(&priv->set_multicast_list_wq, wq_set_multicast_list, net);
	INIT_WORK(&priv->restart_net_feed_wq, wq_restart_net_feed, net);

        net->base_addr = pid;
                
	if ((result = register_netdev(net)) < 0) {
		dvbnet->device[if_num] = NULL;
		free_netdev(net);
		return result;
	}

        return if_num;
}


static int dvb_net_remove_if(struct dvb_net *dvbnet, int num)
{
	struct net_device *net = dvbnet->device[num];
	struct dvb_net_priv *priv = net->priv;

	if (!dvbnet->state[num])
		return -EINVAL;
	if (priv->in_use)
		return -EBUSY;

	dvb_net_stop(net);
	flush_scheduled_work();
        unregister_netdev(net);
	dvbnet->state[num]=0;
	dvbnet->device[num] = NULL;
	free_netdev(net);

	return 0;
}


static int dvb_net_do_ioctl(struct inode *inode, struct file *file,
		  unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct dvb_net *dvbnet = (struct dvb_net *) dvbdev->priv;

	if (((file->f_flags&O_ACCMODE)==O_RDONLY))
		return -EPERM;
	
	switch (cmd) {
	case NET_ADD_IF:
	{
		struct dvb_net_if *dvbnetif=(struct dvb_net_if *)parg;
		int result;
		
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!try_module_get(dvbdev->adapter->module))
			return -EPERM;

		result=dvb_net_add_if(dvbnet, dvbnetif->pid, dvbnetif->feedtype);
		if (result<0) {
			module_put(dvbdev->adapter->module);
			return result;
		}
		dvbnetif->if_num=result;
		break;
	}
	case NET_GET_IF:
	{
		struct net_device *netdev;
		struct dvb_net_priv *priv_data;
		struct dvb_net_if *dvbnetif=(struct dvb_net_if *)parg;

		if (dvbnetif->if_num >= DVB_NET_DEVICES_MAX ||
		    !dvbnet->state[dvbnetif->if_num])
			return -EINVAL;

		netdev = dvbnet->device[dvbnetif->if_num];
		priv_data=(struct dvb_net_priv*)netdev->priv;
		dvbnetif->pid=priv_data->pid;
		dvbnetif->feedtype=priv_data->feedtype;
		break;
	}
	case NET_REMOVE_IF:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		module_put(dvbdev->adapter->module);
		return dvb_net_remove_if(dvbnet, (int) (long) parg);

	/* binary compatiblity cruft */
	case __NET_ADD_IF_OLD:
	{
		struct __dvb_net_if_old *dvbnetif=(struct __dvb_net_if_old *)parg;
		int result;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!try_module_get(dvbdev->adapter->module))
			return -EPERM;

		result=dvb_net_add_if(dvbnet, dvbnetif->pid, DVB_NET_FEEDTYPE_MPE);
		if (result<0) {
			module_put(dvbdev->adapter->module);
			return result;
		}
		dvbnetif->if_num=result;
		break;
	}
	case __NET_GET_IF_OLD:
	{
		struct net_device *netdev;
		struct dvb_net_priv *priv_data;
		struct __dvb_net_if_old *dvbnetif=(struct __dvb_net_if_old *)parg;

		if (dvbnetif->if_num >= DVB_NET_DEVICES_MAX ||
		    !dvbnet->state[dvbnetif->if_num])
		return -EINVAL;

		netdev = dvbnet->device[dvbnetif->if_num];
		priv_data=(struct dvb_net_priv*)netdev->priv;
		dvbnetif->pid=priv_data->pid;
		break;
	}
	default:
		return -ENOTTY;
	}
	return 0;
}

static int dvb_net_ioctl(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(inode, file, cmd, arg, dvb_net_do_ioctl);
}

static struct file_operations dvb_net_fops = {
	.owner = THIS_MODULE,
	.ioctl = dvb_net_ioctl,
	.open =	dvb_generic_open,
	.release = dvb_generic_release,
};

static struct dvb_device dvbdev_net = {
        .priv = NULL,
        .users = 1,
        .writers = 1,
        .fops = &dvb_net_fops,
};


void dvb_net_release (struct dvb_net *dvbnet)
{
	int i;

	dvb_unregister_device(dvbnet->dvbdev);

	for (i=0; i<DVB_NET_DEVICES_MAX; i++) {
		if (!dvbnet->state[i])
			continue;
		dvb_net_remove_if(dvbnet, i);
	}
}


int dvb_net_init (struct dvb_adapter *adap, struct dvb_net *dvbnet,
		  struct dmx_demux *dmx)
{
	int i;
		
	dvbnet->demux = dmx;

	for (i=0; i<DVB_NET_DEVICES_MAX; i++)
		dvbnet->state[i] = 0;

	dvb_register_device (adap, &dvbnet->dvbdev, &dvbdev_net,
			     dvbnet, DVB_DEVICE_NET);

	return 0;
}

