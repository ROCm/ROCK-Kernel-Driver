/* 
 * dvb_net.c
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

#include <linux/dvb/net.h>

#include <asm/uaccess.h>
#include "demux.h"
#include "dvb_net.h"

/*
 *	Determine the packet's protocol ID. The rule here is that we 
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 *
 *  stolen from eth.c out of the linux kernel, hacked for dvb-device
 *  by Michael Holzt <kju@debian.org>
 */
 
unsigned short my_eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;
	
	skb->mac.raw=skb->data;
	skb_pull(skb,dev->hard_header_len);
	eth= skb->mac.ethernet;
	
	if(*eth->h_dest&1)
	{
		if(memcmp(eth->h_dest,dev->broadcast, ETH_ALEN)==0)
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}
	
	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;
		
	rawp = skb->data;
	
	/*
	 *	This is a magic hack to spot IPX packets. Older Novell breaks
	 *	the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *	layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *	won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);
		
	/*
	 *	Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

static void 
dvb_net_sec(struct net_device *dev, u8 *pkt, int pkt_len)
{
        u8 *eth;
        struct sk_buff *skb;

        if (pkt_len<13) {
                printk("%s: IP/MPE packet length = %d too small.\n", dev->name, pkt_len);
		return;
	}
        skb = dev_alloc_skb(pkt_len+2);
        if (skb == NULL) {
                printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
                       dev->name);
                ((dvb_net_priv_t *)dev->priv)->stats.rx_dropped++;
                return;
        }
        eth=(u8 *) skb_put(skb, pkt_len+2);
        memcpy(eth+14, (void*)pkt+12, pkt_len-12);

        eth[0]=pkt[0x0b];
        eth[1]=pkt[0x0a];
        eth[2]=pkt[0x09];
        eth[3]=pkt[0x08];
        eth[4]=pkt[0x04];
        eth[5]=pkt[0x03];
        eth[6]=eth[7]=eth[8]=eth[9]=eth[10]=eth[11]=0;
        eth[12]=0x08; eth[13]=0x00;

	skb->protocol=my_eth_type_trans(skb,dev);
        skb->dev=dev;
        
        ((dvb_net_priv_t *)dev->priv)->stats.rx_packets++;
        ((dvb_net_priv_t *)dev->priv)->stats.rx_bytes+=skb->len;
        //sti();
        netif_rx(skb);
}
 
static int 
dvb_net_callback(u8 *buffer1, size_t buffer1_len,
		 u8 *buffer2, size_t buffer2_len,
		 dmx_section_filter_t *filter,
		 dmx_success_t success)
{
        struct net_device *dev=(struct net_device *) filter->priv;

	/* FIXME: this only works if exactly one complete section is
	          delivered in buffer1 only */
	dvb_net_sec(dev, buffer1, buffer1_len);
	return 0;
}

static int
dvb_net_tx(struct sk_buff *skb, struct net_device *dev)
{
	return 0;
}

#define MASK 0x00;

static int 
dvb_net_filter_set(struct net_device *dev, 
		   dmx_section_filter_t **secfilter,
		   unsigned char *mac)
{
	dvb_net_priv_t *priv=(dvb_net_priv_t *)dev->priv;
	int ret;

	*secfilter=0;
	ret=priv->secfeed->allocate_filter(priv->secfeed, secfilter);
	if (ret<0) {
		printk("%s: could not get filter\n", dev->name);
		return ret;
	}

	(*secfilter)->priv=(void *) dev;

	memset((*secfilter)->filter_value, 0, DMX_MAX_FILTER_SIZE);
	memset((*secfilter)->filter_mask , 0, DMX_MAX_FILTER_SIZE);

	(*secfilter)->filter_value[0]=0x3e;
	(*secfilter)->filter_mask[0]=MASK;

	(*secfilter)->filter_value[3]=mac[5];
	(*secfilter)->filter_mask[3]=MASK;
	(*secfilter)->filter_value[4]=mac[4];
	(*secfilter)->filter_mask[4]=MASK;
	(*secfilter)->filter_value[8]=mac[3];
	(*secfilter)->filter_mask[8]=MASK;
	(*secfilter)->filter_value[9]=mac[2];
	(*secfilter)->filter_mask[9]=MASK;

	(*secfilter)->filter_value[10]=mac[1];
	(*secfilter)->filter_mask[10]=MASK;
	(*secfilter)->filter_value[11]=mac[0];
	(*secfilter)->filter_mask[11]=MASK;

	printk("%s: filter mac=%02x %02x %02x %02x %02x %02x\n", 
	       dev->name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return 0;
}

static int
dvb_net_feed_start(struct net_device *dev)
{
	int ret, i;
	dvb_net_priv_t *priv=(dvb_net_priv_t *)dev->priv;
        dmx_demux_t *demux=priv->demux;
        unsigned char *mac=(unsigned char *) dev->dev_addr;
		
	priv->secfeed=0;
	priv->secfilter=0;

	ret=demux->allocate_section_feed(demux, &priv->secfeed, 
					 dvb_net_callback);
	if (ret<0) {
		printk("%s: could not get section feed\n", dev->name);
		return ret;
	}

	ret=priv->secfeed->set(priv->secfeed, priv->pid, 32768, 0, 0);
	if (ret<0) {
		printk("%s: could not set section feed\n", dev->name);
		priv->demux->
		        release_section_feed(priv->demux, priv->secfeed);
		priv->secfeed=0;
		return ret;
	}
	MOD_INC_USE_COUNT;

	dvb_net_filter_set(dev, &priv->secfilter, mac);
	for (i=0; i<priv->multi_num; i++) 
		dvb_net_filter_set(dev, &priv->secfilter,
				   priv->multi_macs[i]);

	priv->secfeed->start_filtering(priv->secfeed);
	printk("%s: feed_started\n", dev->name);
	return 0;
}

static void
dvb_net_feed_stop(struct net_device *dev)
{
	dvb_net_priv_t *priv=(dvb_net_priv_t *)dev->priv;
	int i;

        if (priv->secfeed) {
	        if (priv->secfeed->is_filtering)
		        priv->secfeed->stop_filtering(priv->secfeed);
		printk("%s: feed_stopped\n", dev->name);
	        if (priv->secfilter)
		        priv->secfeed->
			        release_filter(priv->secfeed, 
					       priv->secfilter);
		priv->secfilter=0;

		for (i=0; i<priv->multi_num; i++) {
			if (priv->multi_secfilter[i])
				priv->secfeed->
					release_filter(priv->secfeed, 
						       priv->multi_secfilter[i]);
			priv->multi_secfilter[i]=0;
		}
		priv->demux->
		        release_section_feed(priv->demux, priv->secfeed);
		priv->secfeed=0;
		MOD_DEC_USE_COUNT;
	} else
		printk("%s: no feed to stop\n", dev->name);
}

static int
dvb_set_mc_filter(struct net_device *dev, struct dev_mc_list *mc)
{
	dvb_net_priv_t *priv=(dvb_net_priv_t *)dev->priv;

	if (priv->multi_num==DVB_NET_MULTICAST_MAX)
		return -ENOMEM;

	printk("%s: set_mc_filter %d: %02x %02x %02x %02x %02x %02x\n", 
	       dev->name, 
	       priv->multi_num,
	       mc->dmi_addr[0],
	       mc->dmi_addr[1],
	       mc->dmi_addr[2],
	       mc->dmi_addr[3],
	       mc->dmi_addr[4],
	       mc->dmi_addr[5]);
	memcpy(priv->multi_macs[priv->multi_num], mc->dmi_addr, 6);
	
	priv->multi_num++;
	return 0;
}

static void
dvb_net_set_multi(struct net_device *dev)
{
	dvb_net_priv_t *priv=(dvb_net_priv_t *)dev->priv;

	printk("%s: set_multi()\n", dev->name);
	dvb_net_feed_stop(dev);

	if (dev->flags&IFF_PROMISC) {
		/* Enable promiscuous mode */
		printk("%s: promiscuous mode\n", dev->name);
	} else if((dev->flags&IFF_ALLMULTI)) {
		/* Disable promiscuous mode, use normal mode. */
		printk("%s: normal mode\n", dev->name);
	} else if(dev->mc_count) {
                int mci;
                struct dev_mc_list *mc;
		
		printk("%s: set_mc_list, %d entries\n", 
		       dev->name, dev->mc_count);
		priv->multi_num=0;
                for (mci=0, mc=dev->mc_list; 
		     mci<dev->mc_count;
		     mc=mc->next, mci++) {
			dvb_set_mc_filter(dev, mc);
                } 
	}
	dvb_net_feed_start(dev);
}

static int
dvb_net_set_config(struct net_device *dev, struct ifmap *map)
{
	if (netif_running(dev))
		return -EBUSY;
	return 0;
}

static int
dvb_net_set_mac(struct net_device *dev, void *p)
{
	struct sockaddr *addr=p;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	if (netif_running(dev)) {
	        dvb_net_feed_stop(dev);
		dvb_net_feed_start(dev);
	}
        return 0;
}


static int
dvb_net_open(struct net_device *dev)
{
	dvb_net_feed_start(dev);
	return 0;
}

static int
dvb_net_stop(struct net_device *dev)
{
        dvb_net_feed_stop(dev);
	return 0;
}

static struct net_device_stats *
dvb_net_get_stats(struct net_device *dev)
{
        return &((dvb_net_priv_t *)dev->priv)->stats;
}


static int
dvb_net_init_dev(struct net_device *dev)
{
	printk("dvb_net: dvb_net_init_dev()\n");

	ether_setup(dev);

	dev->open		= dvb_net_open;
	dev->stop		= dvb_net_stop;
	dev->hard_start_xmit	= dvb_net_tx;
	dev->get_stats		= dvb_net_get_stats;
	dev->set_multicast_list = dvb_net_set_multi;
	dev->set_config         = dvb_net_set_config;
	dev->set_mac_address    = dvb_net_set_mac;
	dev->mtu		= 4096;

	dev->flags             |= IFF_NOARP;
	dev->hard_header_cache  = NULL;

	//SET_MODULE_OWNER(dev);
	
	return 0;
}

static int 
get_if(dvb_net_t *dvbnet)
{
	int i;

	for (i=0; i<dvbnet->dev_num; i++) 
		if (!dvbnet->state[i])
			break;
	if (i==dvbnet->dev_num)
		return -1;
	dvbnet->state[i]=1;
	return i;
}


int 
dvb_net_add_if(dvb_net_t *dvbnet, u16 pid)
{
        struct net_device *net;
	dmx_demux_t *demux;
	int result;
	int if_num;
 
	if_num=get_if(dvbnet);
	if (if_num<0)
		return -EINVAL;

	net=&dvbnet->device[if_num];
	demux=dvbnet->demux;
	
	net->base_addr = 0;
	net->irq       = 0;
	net->dma       = 0;
	net->mem_start = 0;
        memcpy(net->name, "dvb0_0", 7);
        net->name[3]=dvbnet->card_num+0x30;
        net->name[5]=if_num+0x30;
        net->next      = NULL;
        net->init      = dvb_net_init_dev;
        net->priv      = kmalloc(sizeof(dvb_net_priv_t), GFP_KERNEL);
	if (net->priv == NULL)
			return -ENOMEM;
	memset(net->priv, 0, sizeof(dvb_net_priv_t));

        ((dvb_net_priv_t *)net->priv)->demux=demux;
        ((dvb_net_priv_t *)net->priv)->pid=pid;

        net->base_addr=pid;
                
	if ((result = register_netdev(net)) < 0) {
		return result;
	}
	MOD_INC_USE_COUNT;
        return if_num;
}

int 
dvb_net_remove_if(dvb_net_t *dvbnet, int num)
{
	if (!dvbnet->state[num])
		return -EINVAL;
	dvb_net_stop(&dvbnet->device[num]);
        kfree(dvbnet->device[num].priv);
        unregister_netdev(&dvbnet->device[num]);
	dvbnet->state[num]=0;
	MOD_DEC_USE_COUNT;
	return 0;
}

int dvb_net_ioctl(struct inode *inode, struct file *file, 
		  unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev=(struct dvb_device *) file->private_data;
	dvb_net_t *dvbnet=(dvb_net_t *) dvbdev->priv;

	if (((file->f_flags&O_ACCMODE)==O_RDONLY))
		return -EPERM;
	
	switch (cmd) {
	case NET_ADD_IF:
	{
		struct dvb_net_if *dvbnetif=(struct dvb_net_if *)parg;
		int result;
		
		result=dvb_net_add_if(dvbnet, dvbnetif->pid);
		if (result<0)
			return result;
		dvbnetif->if_num=result;
		break;
	}
	case NET_REMOVE_IF:
		return dvb_net_remove_if(dvbnet, (int) parg);
	default:
		return -EINVAL;
	}
	return 0;
}

static struct file_operations dvb_net_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_generic_open,
	.release	= dvb_generic_release,
};

static struct dvb_device dvbdev_net = {
	.priv		= 0,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_net_fops,
	.kernel_ioctl	= dvb_net_ioctl,
};

void
dvb_net_release(dvb_net_t *dvbnet)
{
	int i;

	dvb_unregister_device(dvbnet->dvbdev);
	for (i=0; i<dvbnet->dev_num; i++) {
		if (!dvbnet->state[i])
			continue;
		dvb_net_remove_if(dvbnet, i);
	}
}

int
dvb_net_init(struct dvb_adapter *adap, dvb_net_t *dvbnet, dmx_demux_t *demux)
{
	int i;
		
	dvbnet->demux=demux;
	dvbnet->dev_num=DVB_NET_DEVICES_MAX;
	for (i=0; i<dvbnet->dev_num; i++) 
		dvbnet->state[i]=0;
	dvb_register_device(adap, &dvbnet->dvbdev, &dvbdev_net, dvbnet, DVB_DEVICE_NET);
	return 0;
}

