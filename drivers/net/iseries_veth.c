/* File veth.c created by Kyle A. Lucke on Mon Aug  7 2000. */
/*
 * IBM eServer iSeries Virtual Ethernet Device Driver
 * Copyright (C) 2001 Kyle A. Lucke (klucke@us.ibm.com), IBM Corp.
 * Substantially cleaned up by:
 * Copyright (C) 2003 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *                                                                      
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 *
 * This module implements the virtual ethernet device for iSeries LPAR
 * Linux.  It uses hypervisor message passing to enable an
 * ethernet-like network device communicating between partitions on
 * the iSeries.
 *
 * The iSeries LPAR hypervisor currently allows for up to 16 different
 * virtual ethernets.  These are all dynamically configurable on
 * OS/400 partitions, but dynamic configuration is not supported under
 * Linux yet.  An ethXX network device will be created for each
 * virtual ethernet this partition is connected to.
 *
 * - This driver is responsible for routing packets to and from other
 *   partitions.  The MAC addresses used by the virtual ethernets
 *   contains meaning and must not be modified.
 *
 * - Having 2 virtual ethernets to the same remote partition DOES NOT
 *   double the available bandwidth.  The 2 devices will share the
 *   available hypervisor bandwidth.
 *
 * - If you send a packet to your own mac address, it will just be
 *   dropped, you won't get it on the receive side.
 *
 * - Multicast is implemented by sending the frame frame to every
 *   other partition.  It is the responsibility of the receiving
 *   partition to filter the addresses desired.
 *
 * Tunable parameters:
 *
 * VETH_NUMBUFFERS: This compile time option defaults to 120.  It
 * controls how much memory Linux will allocate per remote partition
 * it is communicating with.  It can be thought of as the maximum
 * number of packets outstanding to a remote partition at a time.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/ethtool.h>
#include <asm/iSeries/mf.h>
#include <asm/uaccess.h>

#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iommu.h>

#include "iseries_veth.h"

extern struct pci_dev *iSeries_veth_dev;

#define veth_printk(prio, fmt, args...) \
	printk(prio "%s: " fmt, __FILE__, ## args)

#define veth_error(fmt, args...) \
	printk(KERN_ERR "(%s:%3.3d) ERROR: " fmt, __FILE__, __LINE__ , ## args)

#define VETH_NUMBUFFERS		120

#if VETH_NUMBUFFERS < 10
#define ACK_THRESHOLD 1
#elif VETH_NUMBUFFERS < 20
#define ACK_THRESHOLD 4
#elif VETH_NUMBUFFERS < 40
#define ACK_THRESHOLD 10
#else
#define ACK_THRESHOLD 20
#endif

static int veth_open(struct net_device *dev);
static int veth_close(struct net_device *dev);
static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int veth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static void veth_handle_event(struct HvLpEvent *, struct pt_regs *);
static void veth_handle_ack(struct VethLpEvent *);
static void veth_handle_int(struct VethLpEvent *);
static void veth_init_connection(struct veth_lpar_connection *cnx, u8 rlp);
static void veth_open_connection(u8);
static void veth_finish_open_connection(void *parm);
static void veth_close_connection(u8);
static void veth_set_multicast_list(struct net_device *dev);

static void veth_take_cap(struct veth_lpar_connection *, struct VethLpEvent *);
static void veth_take_cap_ack(struct veth_lpar_connection *, struct VethLpEvent *);
static void veth_take_monitor_ack(struct veth_lpar_connection *,
				  struct VethLpEvent *);
static void veth_recycle_msg(struct veth_lpar_connection *, struct veth_msg *);
static void veth_monitor_ack_task(void *);
static void veth_receive(struct veth_lpar_connection *, struct VethLpEvent *);
static struct net_device_stats *veth_get_stats(struct net_device *dev);
static int veth_change_mtu(struct net_device *dev, int new_mtu);
static void veth_timed_ack(unsigned long connectionPtr);
static void veth_failMe(struct veth_lpar_connection *cnx);

static struct VethFabricMgr *mFabricMgr; /* = NULL */
static struct net_device *veth_devices[HVMAXARCHITECTEDVIRTUALLANS];
static int veth_num_devices; /* = 0 */

#define VETH_MAX_MTU		9000

MODULE_AUTHOR("Kyle Lucke <klucke@us.ibm.com>");
MODULE_DESCRIPTION("iSeries Virtual ethernet driver");
MODULE_LICENSE("GPL");

static int VethModuleReopen = 1;

static inline u64 veth_dma_addr(void *p)
{
	return 0x8000000000000000LL | virt_to_absolute((unsigned long) p);
}

static inline HvLpEvent_Rc 
veth_signalevent(struct veth_lpar_connection *cnx, u16 subtype, 
		 HvLpEvent_AckInd ackind, HvLpEvent_AckType acktype,
		 u64 token,
		 u64 data1, u64 data2, u64 data3, u64 data4, u64 data5)
{
	return HvCallEvent_signalLpEventFast(cnx->remote_lp,
					     HvLpEvent_Type_VirtualLan,
					     subtype, ackind, acktype,
					     cnx->src_inst,
					     cnx->dst_inst,
					     token, data1, data2, data3,
					     data4, data5);
}

static inline HvLpEvent_Rc veth_signaldata(struct veth_lpar_connection *cnx,
					   u16 subtype, u64 token, void *data)
{
	u64 *p = (u64 *) data;

	return veth_signalevent(cnx, subtype, HvLpEvent_AckInd_NoAck,
				HvLpEvent_AckType_ImmediateAck,
				token, p[0], p[1], p[2], p[3], p[4]);
}

struct veth_allocation {
	struct completion c;
	int num;
};

static void veth_complete_allocation(void *parm, int number)
{
	struct veth_allocation *vc = (struct veth_allocation *)parm;

	vc->num = number;
	complete(&vc->c);
}

static int veth_allocate_events(HvLpIndex rlp, int number)
{
	struct veth_allocation vc = { COMPLETION_INITIALIZER(vc.c), 0 };

	mf_allocateLpEvents(rlp, HvLpEvent_Type_VirtualLan,
			    sizeof(struct VethLpEvent), number,
			    &veth_complete_allocation, &vc);
	wait_for_completion(&vc.c);

	return vc.num;
}

struct net_device * __init veth_probe_one(int vlan)
{
	struct net_device *dev;
	struct veth_port *port;
	int i, rc;

	dev = alloc_etherdev(sizeof (struct veth_port));
	if (! dev) {
		veth_error("Unable to allocate net_device structure!\n");
		return NULL;
	}

	port = (struct veth_port *) dev->priv;

	spin_lock_init(&port->pending_gate);
	rwlock_init(&port->mcast_gate);

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		HvLpVirtualLanIndexMap map;

		if (i == mFabricMgr->this_lp)
			continue;
		map = HvLpConfig_getVirtualLanIndexMapForLp(i);
		if (map & (0x8000 >> vlan))
			port->lpar_map |= (1 << i);
	}

	dev->dev_addr[0] = 0x02;
	dev->dev_addr[1] = 0x01;
	dev->dev_addr[2] = 0xff;
	dev->dev_addr[3] = vlan;
	dev->dev_addr[4] = 0xff;
	dev->dev_addr[5] = HvLpConfig_getLpIndex_outline();

	dev->mtu = VETH_MAX_MTU;

	memcpy(&port->mac_addr, dev->dev_addr, 6);

	dev->open = veth_open;
	dev->hard_start_xmit = veth_start_xmit;
	dev->stop = veth_close;
	dev->get_stats = veth_get_stats;
	dev->change_mtu = veth_change_mtu;
	dev->set_mac_address = NULL;
	dev->set_multicast_list = veth_set_multicast_list;
	dev->do_ioctl = veth_ioctl;

	rc = register_netdev(dev);
	if (rc != 0) {
		veth_printk(KERN_ERR,
			    "Failed to register an ethernet device for vlan %d\n",
			    vlan);
		free_netdev(dev);
		return NULL;
	}

	veth_printk(KERN_DEBUG, "%s attached to iSeries vlan %d (lpar_map=0x%04x)\n",
		    dev->name, vlan, port->lpar_map);

	return dev;
}

int __init veth_probe(void)
{
	HvLpIndexMap vlan_map = HvLpConfig_getVirtualLanIndexMap();
	int i;

	memset(veth_devices, 0, sizeof(veth_devices));

	for (i = 0; vlan_map != 0; vlan_map <<= 1, i++) {
		struct net_device *dev = NULL;

		if (! (vlan_map & 0x8000))
			continue;

		dev = veth_probe_one(i);

		if (dev) {
			mFabricMgr->netdev[i] = dev;
			veth_devices[veth_num_devices] = dev;
			veth_num_devices++;
		}
	}

	if (veth_num_devices == 0)
		return -ENODEV;

	return 0;
}

void __exit veth_module_cleanup(void)
{
	int i;
	struct VethFabricMgr *fm = mFabricMgr;

	if (! mFabricMgr)
		return;

	VethModuleReopen = 0;
	
	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct veth_lpar_connection *cnx = &mFabricMgr->connection[i];
		unsigned long flags;

		spin_lock_irqsave(&cnx->status_gate, flags);
		veth_close_connection(i);
		spin_unlock_irqrestore(&cnx->status_gate, flags);
	}
	
	flush_scheduled_work();
	
	HvLpEvent_unregisterHandler(HvLpEvent_Type_VirtualLan);
	
	mb();
	mFabricMgr = NULL;
	mb();
	
	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct veth_lpar_connection *cnx = &fm->connection[i];

		if (cnx->mNumberAllocated + cnx->mNumberRcvMsgs > 0) {
			mf_deallocateLpEvents(cnx->remote_lp,
					      HvLpEvent_Type_VirtualLan,
					      cnx->mNumberAllocated
					      + cnx->mNumberRcvMsgs,
					      NULL, NULL);
		}
		
		if (cnx->msgs)
			kfree(cnx->msgs);
	}
	
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALLANS; ++i) {
		struct net_device *dev;

		if (! fm->netdev[i])
			continue;

		dev = fm->netdev[i];
		fm->netdev[i] = NULL;

		mb();
			
		if (dev) {
			unregister_netdev(dev);
			free_netdev(dev);
		}
	}
	
	kfree(fm);
}

module_exit(veth_module_cleanup);

int __init veth_module_init(void)
{
	int i;
	int this_lp;
	int rc;

	mFabricMgr = kmalloc(sizeof (struct VethFabricMgr), GFP_KERNEL);
	if (! mFabricMgr) {
		veth_error("Unable to allocate fabric manager\n");
		return -ENOMEM;
	}

	memset(mFabricMgr, 0, sizeof (*mFabricMgr));

	this_lp = HvLpConfig_getLpIndex_outline();
	mFabricMgr->this_lp = this_lp;

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct veth_lpar_connection *cnx = &mFabricMgr->connection[i];

		veth_init_connection(cnx, i);
	}

	rc = veth_probe();
	if (rc != 0) {
		veth_module_cleanup();
		return rc;
	}

	HvLpEvent_registerHandler(HvLpEvent_Type_VirtualLan, &veth_handle_event);

	/* Run through the active lps and open connections to the ones
	 * we need to */
	/* FIXME: is there any reason to do this backwards? */
	for (i = HVMAXARCHITECTEDLPS - 1; i >= 0; --i) {
		struct veth_lpar_connection *cnx = &mFabricMgr->connection[i];

		if ( (i == this_lp) 
		     || ! HvLpConfig_doLpsCommunicateOnVirtualLan(this_lp, i) )
			continue;

		spin_lock_irq(&cnx->status_gate);
		veth_open_connection(i);
		spin_unlock_irq(&cnx->status_gate);
	}

	return 0;
}

module_init(veth_module_init);

static int veth_open(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;

	memset(&port->stats, 0, sizeof (port->stats));

	netif_start_queue(dev);

	return 0;
}

static int veth_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}

static struct net_device_stats *veth_get_stats(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;

	return &port->stats;
}

static int veth_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > VETH_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static int veth_transmit_to_one(struct sk_buff *skb, HvLpIndex rlp,
				struct net_device *dev)
{
	struct veth_lpar_connection *cnx = mFabricMgr->connection + rlp;
	HvLpEvent_Rc rc;
	u32 dma_address, dma_length;
	struct veth_msg *msg = NULL;

	if (! cnx->status.ready)
		return 2;

	if ((skb->len - 14) > VETH_MAX_MTU)
		return 2;

	VETHSTACKPOP(&cnx->msg_stack, msg);

	if (! msg)
		return 1;

	dma_length = skb->len;
	dma_address = pci_map_single(iSeries_veth_dev, skb->data,
				     dma_length, PCI_DMA_TODEVICE);
	
	/* Is it really necessary to check the length and address
	 * fields of the first entry here? */
	if (dma_address != NO_TCE) {
		msg->skb = skb;
		msg->data.addr[0] = dma_address;
		msg->data.len[0] = dma_length;
		msg->data.eof = 1;
		set_bit(0, &(msg->in_use));
		rc = veth_signaldata(cnx, VethEventTypeFrames,
				     msg->token, &msg->data);
	} else {
		struct veth_port *port = (struct veth_port *) dev->priv;
		rc = -1;	/* Bad return code */
		port->stats.tx_errors++;
	}
	
	if (rc != HvLpEvent_Rc_Good) {
		msg->skb = NULL;
		/* need to set in use to make veth_recycle_msg in case
		 * this was a mapping failure */
		set_bit(0, &msg->in_use);
		veth_recycle_msg(cnx, msg);
		return 2;
	}

	return 0;
}

static HvLpIndexMap veth_transmit_to_many(struct sk_buff *skb,
					  HvLpIndexMap lpmask,
					  struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;
	int i;
	int rc;

	for (i = 0; i < HVMAXARCHITECTEDLPS; i++) {
		struct sk_buff *clone;

		if (! lpmask & (1<<i))
			continue;
		
		clone = skb_clone(skb, GFP_ATOMIC);

		if (! clone) {
			veth_error("%s: skb_clone failed %p\n",
				   dev->name, skb);
			continue;
		}
		/* the ack handles deleting the skb */
		rc = veth_transmit_to_one(clone, i, dev);
		
		/* tx failed, we need to free the skb */
		if (rc != 0)
			dev_kfree_skb(clone);
			
		/* if we didn't fail from lack of buffers, the tx as a
		 * whole is successful */
		if (rc != 1)
			lpmask &= ~(1<<i);
	}

	if (! lpmask) {
		port->stats.tx_packets++;
		port->stats.tx_bytes += skb->len;
	}

	return lpmask;
}

static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned char *frame = skb->data;
	struct veth_port *port = (struct veth_port *) dev->priv;
	unsigned long flags;
	HvLpIndexMap lpmask;

	BUG_ON(! mFabricMgr);

	if (! (frame[0] & 0x01)) {
		/* unicast packet */
		HvLpIndex rlp = frame[5];

		if ( ! ((1 << rlp) & port->lpar_map) ) {
			dev_kfree_skb(skb);
			return 0;
		}

		lpmask = 1 << rlp;
	} else {
		lpmask = port->lpar_map;
	}

	lpmask = veth_transmit_to_many(skb, lpmask, dev);

	if (lpmask) {
		spin_lock_irqsave(&port->pending_gate, flags);
		if (port->pending_skb) {
			veth_error("%s: Tx while skb was pending!\n", dev->name);
			dev_kfree_skb(skb);
			return 1;
		}

		port->pending_skb = skb;
		port->pending_lpmask = lpmask;
		netif_stop_queue(dev);

		spin_unlock_irqrestore(&port->pending_gate, flags);
	}

	dev_kfree_skb(skb);
	return 0;
}

static int veth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
#ifdef SIOCETHTOOL
	struct ethtool_cmd ecmd;

	if (cmd != SIOCETHTOOL)
		return -EOPNOTSUPP;
	if (copy_from_user(&ecmd, ifr->ifr_data, sizeof (ecmd)))
		return -EFAULT;
	switch (ecmd.cmd) {
	case ETHTOOL_GSET:
		ecmd.supported = (SUPPORTED_1000baseT_Full
				  | SUPPORTED_Autoneg | SUPPORTED_FIBRE);
		ecmd.advertising = (SUPPORTED_1000baseT_Full
				    | SUPPORTED_Autoneg | SUPPORTED_FIBRE);

		ecmd.port = PORT_FIBRE;
		ecmd.transceiver = XCVR_INTERNAL;
		ecmd.phy_address = 0;
		ecmd.speed = SPEED_1000;
		ecmd.duplex = DUPLEX_FULL;
		ecmd.autoneg = AUTONEG_ENABLE;
		ecmd.maxtxpkt = 120;
		ecmd.maxrxpkt = 120;
		if (copy_to_user(ifr->ifr_data, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		return 0;

	case ETHTOOL_GDRVINFO:{
			struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
			strncpy(info.driver, "veth", sizeof(info.driver) - 1);
			info.driver[sizeof(info.driver) - 1] = '\0';
			strncpy(info.version, "1.0", sizeof(info.version) - 1);
			if (copy_to_user(ifr->ifr_data, &info, sizeof(info)))
				return -EFAULT;
			return 0;
		}
		/* get link status */
	case ETHTOOL_GLINK:{
			struct ethtool_value edata = { ETHTOOL_GLINK };
			edata.data = 1;
			if (copy_to_user(ifr->ifr_data, &edata, sizeof(edata)))
				return -EFAULT;
			return 0;
		}

	default:
		break;
	}

#endif
	return -EOPNOTSUPP;
}

static void veth_set_multicast_list(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;
	unsigned long flags;

	write_lock_irqsave(&port->mcast_gate, flags);

	if (dev->flags & IFF_PROMISC) {	/* set promiscuous mode */
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n",
		       dev->name);
		port->promiscuous = 1;
	} else if ( (dev->flags & IFF_ALLMULTI)
		    || (dev->mc_count > VETH_MAX_MCAST) ) {
		port->all_mcast = 1;
	} else {
		struct dev_mc_list *dmi = dev->mc_list;
		int i;

		/* Update table */
		port->num_mcast = 0;
		
		for (i = 0; i < dev->mc_count; i++) {
			u8 *addr = dmi->dmi_addr;
			u64 xaddr = 0;

			if (addr[0] & 0x01) {/* multicast address? */
				memcpy(&xaddr, addr, ETH_ALEN);
				port->mcast_addr[port->num_mcast] = xaddr;
				port->num_mcast++;
			}
			dmi = dmi->next;
		}
	}

	write_unlock_irqrestore(&port->mcast_gate, flags);
}

static void veth_handle_event(struct HvLpEvent *event, struct pt_regs *regs)
{
	struct VethLpEvent *veth_event = (struct VethLpEvent *)event;

	if (event->xFlags.xFunction == HvLpEvent_Function_Ack)
		veth_handle_ack(veth_event);
	else if (event->xFlags.xFunction == HvLpEvent_Function_Int)
		veth_handle_int(veth_event);
}

static void veth_handle_ack(struct VethLpEvent *event)
{
	HvLpIndex rlp = event->base_event.xTargetLp;
	struct veth_lpar_connection *cnx = &mFabricMgr->connection[rlp];

	switch (event->base_event.xSubtype) {
	case VethEventTypeCap:
		veth_take_cap_ack(cnx, event);
		break;
	case VethEventTypeMonitor:
		veth_take_monitor_ack(cnx, event);
		break;
	default:
		veth_error("Unknown ack type %d from lpar %d\n",
			   event->base_event.xSubtype, rlp);
	};
}

static void veth_handle_int(struct VethLpEvent *event)
{
	HvLpIndex rlp = event->base_event.xSourceLp;
	struct veth_lpar_connection *cnx = &mFabricMgr->connection[rlp];
	int i;

	switch (event->base_event.xSubtype) {
	case VethEventTypeCap:
		veth_take_cap(cnx, event);
		break;
	case VethEventTypeMonitor:
		/* do nothing... this'll hang out here til we're dead,
		 * and the hypervisor will return it for us. */
		break;
	case VethEventTypeFramesAck:
		for (i = 0; i < VETH_MAX_ACKS_PER_MSG; ++i) {
			u16 msgnum = event->u.frames_ack_data.token[i];

			if (msgnum < VETH_NUMBUFFERS)
				veth_recycle_msg(cnx, cnx->msgs + msgnum);
		}
		break;
	case VethEventTypeFrames:
		veth_receive(cnx, event);
		break;
	default:
		veth_error("Unknown interrupt type %d from lpar %d\n",
			   event->base_event.xSubtype, rlp);
	};
}

static void veth_failMe(struct veth_lpar_connection *cnx)
{
	cnx->status.ready = 0;
}

static void veth_init_connection(struct veth_lpar_connection *cnx, u8 rlp)
{
	struct veth_msg *msgs;
	HvLpIndex this_lp = mFabricMgr->this_lp;
	int i;

	veth_failMe(cnx);

	cnx->remote_lp = rlp;

	spin_lock_init(&cnx->ack_gate);
	spin_lock_init(&cnx->status_gate);

	cnx->status.got_cap = 0;
	cnx->status.got_cap_ack = 0;

	INIT_WORK(&cnx->finish_open_wq, veth_finish_open_connection, cnx);
	INIT_WORK(&cnx->monitor_ack_wq, veth_monitor_ack_task, cnx);

	init_timer(&cnx->ack_timer);
	cnx->ack_timer.function = veth_timed_ack;
	cnx->ack_timer.data = (unsigned long) cnx;

	if ( (rlp == this_lp) 
	     || ! HvLpConfig_doLpsCommunicateOnVirtualLan(this_lp, rlp) )
		return;

	msgs = kmalloc(VETH_NUMBUFFERS * sizeof(struct veth_msg), GFP_KERNEL);
	if (! msgs)
		return;

	cnx->msgs = msgs;
	memset(msgs, 0, VETH_NUMBUFFERS * sizeof(struct veth_msg));
	spin_lock_init(&cnx->msg_stack.lock);

	for (i = 0; i < VETH_NUMBUFFERS; i++) {
		msgs[i].token = i;
		VETHSTACKPUSH(&cnx->msg_stack, msgs + i);
	}

	cnx->mNumberAllocated = veth_allocate_events(rlp, 2);

	if (cnx->mNumberAllocated < 2) {
		veth_error("Couldn't allocate base msgs for lpar %d, only got %d\n",
			   cnx->remote_lp, cnx->mNumberAllocated);
		veth_failMe(cnx);
		return;
	}

	cnx->mNumberRcvMsgs = veth_allocate_events(cnx->remote_lp,
						   VETH_NUMBUFFERS);

	cnx->local_caps.num_buffers = VETH_NUMBUFFERS;
	cnx->local_caps.ack_threshold = ACK_THRESHOLD;
	cnx->local_caps.ack_timeout = VETH_ACKTIMEOUT;
}

static void veth_open_connection(u8 rlp)
{
	struct veth_lpar_connection *cnx = &mFabricMgr->connection[rlp];
	HvLpEvent_Rc rc;
	u64 *rawcap = (u64 *) &cnx->local_caps;

	if (! cnx->msgs || (cnx->mNumberAllocated < 2)
	    || ! cnx->mNumberRcvMsgs) {
		veth_failMe(cnx);
		return;
	}

	spin_lock_irq(&cnx->ack_gate);

	memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));
	cnx->num_pending_acks = 0;

	HvCallEvent_openLpEventPath(rlp, HvLpEvent_Type_VirtualLan);

	cnx->status.open = 1;

	cnx->src_inst = 
		HvCallEvent_getSourceLpInstanceId(rlp,
						  HvLpEvent_Type_VirtualLan);
	cnx->dst_inst =
		HvCallEvent_getTargetLpInstanceId(rlp,
						  HvLpEvent_Type_VirtualLan);

	spin_unlock_irq(&cnx->ack_gate);

	rc = veth_signalevent(cnx, VethEventTypeCap,
			      HvLpEvent_AckInd_DoAck,
			      HvLpEvent_AckType_ImmediateAck,
			      0, rawcap[0], rawcap[1], rawcap[2], rawcap[3],
			      rawcap[4]);

	if ( (rc == HvLpEvent_Rc_PartitionDead)
	     || (rc == HvLpEvent_Rc_PathClosed)) {
		/* Never mind we'll resend out caps when we get the
		 * caps from the other end comes up and sends
		 * theirs */
		return;
	} else if (rc != HvLpEvent_Rc_Good) {
		veth_error("Couldn't send capabilities to lpar %d, rc=%x\n",
				  cnx->remote_lp, (int) rc);
		veth_failMe(cnx);
		return;
	}

	cnx->status.sent_caps = 1;
}

static void veth_finish_open_connection(void *parm)
{
	struct veth_lpar_connection *cnx = (struct veth_lpar_connection *)parm;
	struct VethCapData *remote_caps = &cnx->remote_caps;
	u64 num_acks_needed = 0;
	HvLpEvent_Rc rc;

	spin_lock_irq(&cnx->status_gate);

	memcpy(remote_caps, &cnx->cap_event.u.caps_data,
	       sizeof(*remote_caps));

	/* Convert timer to jiffies */
	if (cnx->local_caps.ack_timeout)
		cnx->ack_timeout = remote_caps->ack_timeout * HZ / 1000000;
	else
		cnx->ack_timeout = VETH_ACKTIMEOUT * HZ / 1000000;

	if ( (remote_caps->num_buffers == 0)
	     || (remote_caps->ack_threshold > VETH_MAX_ACKS_PER_MSG)
	     || (remote_caps->ack_threshold == 0) 
	     || (cnx->ack_timeout == 0) ) {
		veth_error("Received incompatible capabilities from lpar %d\n",
			   cnx->remote_lp);
		cnx->cap_event.base_event.xRc = HvLpEvent_Rc_InvalidSubtypeData;
		HvCallEvent_ackLpEvent((struct HvLpEvent *)&cnx->cap_event);

		veth_failMe(cnx);
		goto out;
	}

	num_acks_needed = (remote_caps->num_buffers / remote_caps->ack_threshold) + 1;

	if (cnx->mNumberLpAcksAlloced < num_acks_needed) {
		int num;
		
		num_acks_needed = num_acks_needed - cnx->mNumberLpAcksAlloced;
		
		spin_unlock_irq(&cnx->status_gate);
		
		num = veth_allocate_events(cnx->remote_lp, num_acks_needed);
		
		if (num > 0)
			cnx->mNumberLpAcksAlloced += num;
		spin_lock_irq(&cnx->status_gate);
	}
	
	if (cnx->mNumberLpAcksAlloced < num_acks_needed) {
		veth_error("Couldn't allocate all the frames ack events for lpar %d\n",
			   cnx->remote_lp);

		cnx->cap_event.base_event.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *)&cnx->cap_event);

		veth_failMe(cnx);
		goto out;
	}

	rc = HvCallEvent_ackLpEvent((struct HvLpEvent *)&cnx->cap_event);
	if (rc != HvLpEvent_Rc_Good) {
		veth_error("Failed to ack remote cap for lpar %d with rc %x\n",
			   cnx->remote_lp, (int) rc);
		veth_failMe(cnx);
		goto out;
	}

	if (cnx->cap_ack_event.base_event.xRc != HvLpEvent_Rc_Good) {
		veth_printk(KERN_ERR, "Bad rc(%d) from lpar %d on capabilities\n",
			    cnx->cap_ack_event.base_event.xRc, cnx->remote_lp);
		veth_failMe(cnx);
		goto out;
	}

	/* Send the monitor */
	rc = veth_signalevent(cnx, VethEventTypeMonitor,
			      HvLpEvent_AckInd_DoAck,
			      HvLpEvent_AckType_DeferredAck,
			      0, 0, 0, 0, 0, 0);
	
	if (rc != HvLpEvent_Rc_Good) {
		veth_error("Monitor send to lpar %d failed with rc %x\n",
				  cnx->remote_lp, (int) rc);
		veth_failMe(cnx);
		goto out;
	}

	cnx->status.ready = 1;
	
	/* Start the ACK timer */
	cnx->ack_timer.expires = jiffies + cnx->ack_timeout;
	add_timer(&cnx->ack_timer);

 out:
	spin_unlock_irq(&cnx->status_gate);
}

static void veth_close_connection(u8 rlp)
{
	struct veth_lpar_connection *cnx = &mFabricMgr->connection[rlp];
	unsigned long flags;

	del_timer_sync(&cnx->ack_timer);

	cnx->status.sent_caps = 0;
	cnx->status.got_cap = 0;
	cnx->status.got_cap_ack = 0;

	if (cnx->status.open) {
		int i;		

		HvCallEvent_closeLpEventPath(rlp, HvLpEvent_Type_VirtualLan);
		cnx->status.open = 0;
		veth_failMe(cnx);

		/* reset ack data */
		spin_lock_irqsave(&cnx->ack_gate, flags);

		memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));
		cnx->num_pending_acks = 0;

		spin_unlock_irqrestore(&cnx->ack_gate, flags);

		/* Clean up any leftover messages */
		for (i = 0; i < VETH_NUMBUFFERS; ++i)
			veth_recycle_msg(cnx, cnx->msgs + i);
	}

}

static void veth_take_cap(struct veth_lpar_connection *cnx, struct VethLpEvent *event)
{
	unsigned long flags;
	HvLpEvent_Rc rc;

	spin_lock_irqsave(&cnx->status_gate, flags);

	if (cnx->status.got_cap) {
		veth_error("Received a second capabilities from lpar %d\n",
			   cnx->remote_lp);
		event->base_event.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
		goto out;
	}

	memcpy(&cnx->cap_event, event, sizeof (cnx->cap_event));
	/* If we failed to send caps out before (presumably because
	 * the target lpar was down), send them now. */
	if (! cnx->status.sent_caps) {
		u64 *rawcap = (u64 *) &cnx->local_caps;

		cnx->dst_inst =
			HvCallEvent_getTargetLpInstanceId(cnx->remote_lp,
							  HvLpEvent_Type_VirtualLan);

		rc = veth_signalevent(cnx, VethEventTypeCap,
				      HvLpEvent_AckInd_DoAck,
				      HvLpEvent_AckType_ImmediateAck,
				      0, rawcap[0], rawcap[1], rawcap[2], rawcap[3],
				      rawcap[4]);
		if ( (rc == HvLpEvent_Rc_PartitionDead)
		     || (rc == HvLpEvent_Rc_PathClosed)) {
			veth_error("Partition down when resending capabilities!!\n");
			goto out;
		} else if (rc != HvLpEvent_Rc_Good) {
			veth_error("Couldn't send cap to lpar %d, rc %x\n",
				   cnx->remote_lp, (int) rc);
			veth_failMe(cnx);
			goto out;
		}
		cnx->status.sent_caps = 1;
	}

	cnx->status.got_cap = 1;
	if (cnx->status.got_cap_ack)
		schedule_work(&cnx->finish_open_wq);

 out:
	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void veth_take_cap_ack(struct veth_lpar_connection *cnx,
			      struct VethLpEvent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->status_gate, flags);

	if (cnx->status.got_cap_ack) {
		veth_error("Received a second capabilities ack from lpar %d\n",
			   cnx->remote_lp);
		goto out;
	}

	memcpy(&cnx->cap_ack_event, event, sizeof(&cnx->cap_ack_event));
	cnx->status.got_cap_ack = 1;

	if (cnx->status.got_cap)
		schedule_work(&cnx->finish_open_wq);

 out:
	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void veth_take_monitor_ack(struct veth_lpar_connection *cnx,
				  struct VethLpEvent *event)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->status_gate, flags);

	veth_printk(KERN_DEBUG, "Monitor ack returned for lpar %d\n", cnx->remote_lp);

	if (cnx->status.monitor_ack_pending) {
		veth_error("Received a monitor ack from lpar %d while already processing one\n",
			   cnx->remote_lp);
		goto out;
	}

	schedule_work(&cnx->monitor_ack_wq);

 out:
	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void veth_recycle_msg(struct veth_lpar_connection *cnx,
			     struct veth_msg *myMsg)
{
	u32 dma_address, dma_length;
	int i;

	if (test_and_clear_bit(0, &myMsg->in_use)) {
		dma_address = myMsg->data.addr[0];
		dma_length = myMsg->data.len[0];

		pci_unmap_single(iSeries_veth_dev, dma_address, dma_length,
				 PCI_DMA_TODEVICE);

		if (myMsg->skb) {
			dev_kfree_skb_any(myMsg->skb);
			myMsg->skb = NULL;
		}

		memset(&myMsg->data, 0, sizeof(myMsg->data));
		VETHSTACKPUSH(&cnx->msg_stack, myMsg);
	} else {
		if (cnx->status.open)
			veth_error("Bogus frames ack from lpar %d (index=%d)\n",
				   cnx->remote_lp, myMsg->token);
	}

	for (i = 0; i < veth_num_devices; i++) {
		struct net_device *dev = veth_devices[i];
		struct veth_port *port = (struct veth_port *)dev->priv;
		unsigned long flags;

		if (! (port->lpar_map & (1<<cnx->remote_lp)))
			continue;

		spin_lock_irqsave(&port->pending_gate, flags);
		if (port->pending_skb) {
			port->pending_lpmask =
				veth_transmit_to_many(port->pending_skb,
						      port->pending_lpmask,
						      dev);
			if (! port->pending_lpmask) {
				dev_kfree_skb_any(port->pending_skb);
				port->pending_skb = NULL;
				netif_start_queue(dev);
			}
		}
		spin_unlock_irqrestore(&port->pending_gate, flags);
	}
}

static void veth_monitor_ack_task(void *parm)
{
	struct veth_lpar_connection *cnx = (struct veth_lpar_connection *) parm;

	spin_lock_irq(&cnx->status_gate);

	veth_failMe(cnx);

	if (cnx->status.open) {
		veth_close_connection(cnx->remote_lp);

		udelay(100);
	}

	if (VethModuleReopen)
		veth_open_connection(cnx->remote_lp);

	cnx->status.monitor_ack_pending = 0;

	spin_unlock_irq(&cnx->status_gate);
}

static inline int veth_frame_wanted(struct veth_port *port, u64 mac_addr)
{
	int wanted = 0;
	int i;
	unsigned long flags;

	if ( (mac_addr == port->mac_addr)
	     || (mac_addr == 0xffffffffffff0000)
	     || port->promiscuous )
		return 1;
	
	if (! (((char *) &mac_addr)[0] & 0x01))
		return 0;

	read_lock_irqsave(&port->mcast_gate, flags);

	if (port->all_mcast) {
		wanted = 1;
		goto out;
	}

	for (i = 0; i < port->num_mcast; ++i) {
		if (port->mcast_addr[i] == mac_addr) {
			wanted = 1;
			break;
		}
	}

 out:
	read_unlock_irqrestore(&port->mcast_gate, flags);

	return wanted;
}

struct dma_chunk {
	u64 addr;
	u64 size;
};

#define VETH_MAX_PAGES_PER_FRAME ( (VETH_MAX_MTU+PAGE_SIZE-2)/PAGE_SIZE + 1 )

static inline void veth_build_dma_list(struct dma_chunk *list, unsigned char *p,
				      unsigned long length)
{
	unsigned long done;
	int i = 1;

	/* FIXME: skbs are continguous in real addresses.  Do we
	 * really need to break it into PAGE_SIZE chunks, or can we do
	 * it just at the granularity of iSeries real->absolute
	 * mapping? */
	list[0].addr = veth_dma_addr(p);
	list[0].size = min(length,
			   PAGE_SIZE - ((unsigned long)p & ~PAGE_MASK));

	done = list[0].size;
	while (done < length) {
		list[i].addr = veth_dma_addr(p + done);
		list[i].size = min(done, PAGE_SIZE);
		done += list[i].size;
		i++;
	}
}

static void veth_receive(struct veth_lpar_connection *cnx, struct VethLpEvent *event)
{
	struct VethFramesData *senddata = &event->u.frames_data;
	int startchunk = 0;
	int nchunks;
	unsigned long flags;
	HvLpDma_Rc rc;

	do {
		u16 length = 0;
		struct sk_buff *skb;
		struct dma_chunk local_list[VETH_MAX_PAGES_PER_FRAME];
		struct dma_chunk remote_list[VETH_MAX_FRAMES_PER_MSG];
		u64 dest;
		HvLpVirtualLanIndex vlan;
		struct net_device *dev;
		struct veth_port *port;

		/* FIXME: do we need this? */
		memset(local_list, 0, sizeof(local_list));
		memset(remote_list, 0, sizeof(VETH_MAX_FRAMES_PER_MSG));

		nchunks = 0;

		/* a 0 address marks the end of the valid entries */
		if (senddata->addr[startchunk] == 0)
			break;

		/* make sure that we have at least 1 EOF entry in the
		 * remaining entries */
		if (! (senddata->eof >> startchunk)) {
			veth_error("missing EOF frag in event: 0x%x startchunk=%d\n",
				   (unsigned) senddata->eof, startchunk);
			break;
		}

		/* build list of chunks in this frame */
		do {
			remote_list[nchunks].addr =
				(u64) senddata->addr[startchunk + nchunks] << 32;
			remote_list[nchunks].size =
				senddata->len[startchunk + nchunks];
			length += remote_list[nchunks].size;
		} while (! (senddata->eof & (1 << (startchunk + nchunks++))));

		/* length == total length of all chunks */
		/* nchunks == # of chunks in this frame */

		if ((length - ETH_HLEN) > VETH_MAX_MTU)
			continue;

		skb = alloc_skb(length, GFP_ATOMIC);
		if (!skb)
			continue;

		veth_build_dma_list(local_list, skb->data, length);

		rc = HvCallEvent_dmaBufList(HvLpEvent_Type_VirtualLan,
					    event->base_event.xSourceLp,
					    HvLpDma_Direction_RemoteToLocal,
					    cnx->src_inst,
					    cnx->dst_inst,
					    HvLpDma_AddressType_RealAddress,
					    HvLpDma_AddressType_TceIndex,
					    veth_dma_addr(&local_list),
					    veth_dma_addr(&remote_list),
					    length);
		if (rc != HvLpDma_Rc_Good) {
			dev_kfree_skb_irq(skb);
			continue;
		}
		
		vlan = skb->data[9];
		dev = mFabricMgr->netdev[vlan];
		port = (struct veth_port *)dev->priv;
		dest = *((u64 *) skb->data) & 0xFFFFFFFFFFFF0000;

		if ((vlan > HVMAXARCHITECTEDVIRTUALLANS) || !port) {
			dev_kfree_skb_irq(skb);
			continue;
		}
		if (! veth_frame_wanted(port, dest)) {
			dev_kfree_skb_irq(skb);
			continue;
		}
			
		skb_put(skb, length);
		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);
		skb->ip_summed = CHECKSUM_NONE;
		netif_rx(skb);	/* send it up */
		port->stats.rx_packets++;
		port->stats.rx_bytes += length;
	} while (startchunk += nchunks, startchunk < VETH_MAX_FRAMES_PER_MSG);

	/* Ack it */
	spin_lock_irqsave(&cnx->ack_gate, flags);
	
	if (cnx->num_pending_acks < VETH_MAX_ACKS_PER_MSG) {
		cnx->pending_acks[cnx->num_pending_acks] =
			event->base_event.xCorrelationToken;
		++cnx->num_pending_acks;
		
		if (cnx->num_pending_acks == cnx->remote_caps.ack_threshold) {
			rc = veth_signaldata(cnx, VethEventTypeFramesAck,
					     0, &cnx->pending_acks);
			
			if (rc != HvLpEvent_Rc_Good)
				veth_error("Error 0x%x acking frames from lpar %d!\n",
					   (unsigned)rc, cnx->remote_lp);
			
			cnx->num_pending_acks = 0;
			memset(&cnx->pending_acks, 0xff, sizeof(cnx->pending_acks));
		}
		
	}
	
	spin_unlock_irqrestore(&cnx->ack_gate, flags);
}

static void veth_timed_ack(unsigned long ptr)
{
	unsigned long flags;
	HvLpEvent_Rc rc;
	struct veth_lpar_connection *cnx = (struct veth_lpar_connection *) ptr;

	/* Ack all the events */
	spin_lock_irqsave(&cnx->ack_gate, flags);

	if (cnx->num_pending_acks > 0) {
		rc = veth_signaldata(cnx, VethEventTypeFramesAck,
				     0, &cnx->pending_acks);
		if (rc != HvLpEvent_Rc_Good)
			veth_error("Error 0x%x acking frames from lpar %d!\n", 
				   (unsigned) rc, cnx->remote_lp);

		cnx->num_pending_acks = 0;
		memset(&cnx->pending_acks, 0xff, sizeof(cnx->pending_acks));
	}

	spin_unlock_irqrestore(&cnx->ack_gate, flags);

	/* Reschedule the timer */
	cnx->ack_timer.expires = jiffies + cnx->ack_timeout;
	add_timer(&cnx->ack_timer);
}
