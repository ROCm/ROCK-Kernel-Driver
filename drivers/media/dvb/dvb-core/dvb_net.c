/* 
 * dvb_net.c
 *
 * Copyright (C) 2001 Convergence integrated media GmbH
 *                    Ralph Metzler <ralph@convergence.de>
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 
 */

#include <linux/dvb/net.h>
#include <asm/uaccess.h>

#include "dvb_demux.h"
#include "dvb_net.h"
#include "dvb_functions.h"


#if 1
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif


#define DVB_NET_MULTICAST_MAX 10

struct dvb_net_priv {
	int in_use;
        struct net_device_stats stats;
        char name[6];
	u16 pid;
        struct dmx_demux *demux;
	struct dmx_section_feed *secfeed;
	struct dmx_section_filter *secfilter;
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


static void dvb_net_sec(struct net_device *dev, u8 *pkt, int pkt_len)
{
        u8 *eth;
        struct sk_buff *skb;

	/* note: pkt_len includes a 32bit checksum */
	if (pkt_len < 16) {
		printk("%s: IP/MPE packet length = %d too small.\n",
			dev->name, pkt_len);
		((struct dvb_net_priv *) dev->priv)->stats.rx_errors++;
		((struct dvb_net_priv *) dev->priv)->stats.rx_length_errors++;
		return;
	}
	if ((pkt[5] & 0xfd) != 0xc1) {
		/* drop scrambled or broken packets */
		((struct dvb_net_priv *) dev->priv)->stats.rx_errors++;
		((struct dvb_net_priv *) dev->priv)->stats.rx_crc_errors++;
		return;
	}
	if (pkt[5] & 0x02) {
		//FIXME: handle LLC/SNAP
                ((struct dvb_net_priv *)dev->priv)->stats.rx_dropped++;
                return;
        }
	if (pkt[7]) {
		/* FIXME: assemble datagram from multiple sections */
		((struct dvb_net_priv *) dev->priv)->stats.rx_errors++;
		((struct dvb_net_priv *) dev->priv)->stats.rx_frame_errors++;
		return;
	}

	/* we have 14 byte ethernet header (ip header follows);
	 * 12 byte MPE header; 4 byte checksum; + 2 byte alignment
	 */
	if (!(skb = dev_alloc_skb(pkt_len - 4 - 12 + 14 + 2))) {
		//printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
		((struct dvb_net_priv *) dev->priv)->stats.rx_dropped++;
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
        
        ((struct dvb_net_priv *)dev->priv)->stats.rx_packets++;
        ((struct dvb_net_priv *)dev->priv)->stats.rx_bytes+=skb->len;
        netif_rx(skb);
}
 
static int dvb_net_callback(const u8 *buffer1, size_t buffer1_len,
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

static int dvb_net_filter_set(struct net_device *dev, 
		   struct dmx_section_filter **secfilter,
		   u8 *mac, u8 *mac_mask)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;
	int ret;

	*secfilter=0;
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

	priv->secfeed=0;
	priv->secfilter=0;

	dprintk("%s: alloc secfeed\n", __FUNCTION__);
	ret=demux->allocate_section_feed(demux, &priv->secfeed, 
					 dvb_net_callback);
	if (ret<0) {
		printk("%s: could not allocate section feed\n", dev->name);
		return ret;
	}

	ret = priv->secfeed->set(priv->secfeed, priv->pid, 32768, 0, 1);

	if (ret<0) {
		printk("%s: could not set section feed\n", dev->name);
		priv->demux->release_section_feed(priv->demux, priv->secfeed);
		priv->secfeed=0;
		return ret;
	}

	if (priv->rx_mode != RX_MODE_PROMISC) {
		dprintk("%s: set secfilter\n", __FUNCTION__);
		dvb_net_filter_set(dev, &priv->secfilter, mac, mask_normal);
	}

	switch (priv->rx_mode) {
	case RX_MODE_MULTI:
		for (i = 0; i < priv->multi_num; i++) {
			dprintk("%s: set multi_secfilter[%d]\n", __FUNCTION__, i);
			dvb_net_filter_set(dev, &priv->multi_secfilter[i],
					   priv->multi_macs[i], mask_normal);
		}
		break;
	case RX_MODE_ALL_MULTI:
		priv->multi_num=1;
		dprintk("%s: set multi_secfilter[0]\n", __FUNCTION__);
		dvb_net_filter_set(dev, &priv->multi_secfilter[0],
				   mac_allmulti, mask_allmulti);
		break;
	case RX_MODE_PROMISC:
		priv->multi_num=0;
		dprintk("%s: set secfilter\n", __FUNCTION__);
		dvb_net_filter_set(dev, &priv->secfilter, mac, mask_promisc);
		break;
	}
	
	dprintk("%s: start filtering\n", __FUNCTION__);
	priv->secfeed->start_filtering(priv->secfeed);
	return 0;
}

static void dvb_net_feed_stop(struct net_device *dev)
{
	struct dvb_net_priv *priv = (struct dvb_net_priv*) dev->priv;
	int i;

	dprintk("%s\n", __FUNCTION__);
        if (priv->secfeed) {
		if (priv->secfeed->is_filtering) {
			dprintk("%s: stop secfeed\n", __FUNCTION__);
		        priv->secfeed->stop_filtering(priv->secfeed);
		}

		if (priv->secfilter) {
			dprintk("%s: release secfilter\n", __FUNCTION__);
			priv->secfeed->release_filter(priv->secfeed,
					       priv->secfilter);
		priv->secfilter=0;
		}

		for (i=0; i<priv->multi_num; i++) {
			if (priv->multi_secfilter[i]) {
				dprintk("%s: release multi_filter[%d]\n", __FUNCTION__, i);
				priv->secfeed->release_filter(priv->secfeed,
						       priv->multi_secfilter[i]);
			priv->multi_secfilter[i]=0;
		}
		}

		priv->demux->release_section_feed(priv->demux, priv->secfeed);
		priv->secfeed=0;
	} else
		printk("%s: no feed to stop\n", dev->name);
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
        dvb_net_feed_stop(dev);
	return 0;
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


static int dvb_net_add_if(struct dvb_net *dvbnet, u16 pid)
{
        struct net_device *net;
	struct dvb_net_priv *priv;
	int result;
	int if_num;
 
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

	INIT_WORK(&priv->set_multicast_list_wq, wq_set_multicast_list, net);
	INIT_WORK(&priv->restart_net_feed_wq, wq_restart_net_feed, net);

        net->base_addr = pid;
                
	if ((result = register_netdev(net)) < 0) {
		kfree(net);
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
		result=dvb_net_add_if(dvbnet, dvbnetif->pid);
		if (result<0)
			return result;
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
			return -EFAULT;

		netdev=(struct net_device*)&dvbnet->device[dvbnetif->if_num];
		priv_data=(struct dvb_net_priv*)netdev->priv;
		dvbnetif->pid=priv_data->pid;
		break;
	}
	case NET_REMOVE_IF:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		return dvb_net_remove_if(dvbnet, (int) (long) parg);
	default:
		return -EINVAL;
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
        .read =	0,
	.write = 0,
	.ioctl = dvb_net_ioctl,
	.open =	dvb_generic_open,
	.release = dvb_generic_release,
	.poll =	0,
};

static struct dvb_device dvbdev_net = {
        .priv = 0,
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

