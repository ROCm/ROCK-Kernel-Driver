/* 
 * dvb_net.h
 *
 * Copyright (C) 2001 Convergence integrated media GmbH
 *                    Ralph Metzler <ralph@convergence.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVB_NET_H_
#define _DVB_NET_H_

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "dvbdev.h"

#define DVB_NET_DEVICES_MAX 10
#define DVB_NET_MULTICAST_MAX 10

typedef struct dvb_net_priv_s {
        struct net_device_stats stats;
        char name[6];
	u16 pid;
        dmx_demux_t *demux;
	dmx_section_feed_t *secfeed;
	dmx_section_filter_t *secfilter;
	int multi_num;
	dmx_section_filter_t *multi_secfilter[DVB_NET_MULTICAST_MAX];
	unsigned char multi_macs[DVB_NET_MULTICAST_MAX][6];
} dvb_net_priv_t;

typedef struct dvb_net_s {
	struct dvb_device *dvbdev;

	int card_num;
	int dev_num;
	struct net_device device[DVB_NET_DEVICES_MAX];
	int state[DVB_NET_DEVICES_MAX];
	dmx_demux_t *demux;
} dvb_net_t;


void dvb_net_release(dvb_net_t *);
int  dvb_net_init(struct dvb_adapter *, dvb_net_t *, dmx_demux_t *);

#endif
