/* File veth.c created by Kyle A. Lucke on Mon Aug  7 2000. */

/*
 * IBM eServer iSeries Virtual Ethernet Device Driver
 * Copyright (C) 2001 Kyle A. Lucke (klucke@us.ibm.com), IBM Corp.
 * Substantially cleaned up by:
 * Copyright (C) 2003 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 */

/*  This program is free software; you can redistribute it and/or modify  */
/*  it under the terms of the GNU General Public License as published by  */
/*  the Free Software Foundation; either version 2 of the License, or     */
/*  (at your option) any later version.                                   */
/*                                                                        */
/*  This program is distributed in the hope that it will be useful,       */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*  GNU General Public License for more details.                          */
/*                                                                        */
/*  You should have received a copy of the GNU General Public License     */
/*  along with this program; if not, write to the Free Software           */
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  */
/*                                                                   USA  */
/*                                                                        */
/* This module contains the implementation of a virtual ethernet device   */
/* for use with iSeries LPAR Linux.  It utilizes low-level message passing*/
/* provided by the hypervisor to enable an ethernet-like network device   */
/* that can be used to enable inter-partition communications on the same  */
/* physical iSeries.                                                      */
/*                                                                        */
/* The iSeries LPAR hypervisor has currently defined the ability for a    */
/* partition to communicate on up to 16 different virtual ethernets, all  */
/* dynamically configurable, at least for an OS/400 partition.  The       */
/* dynamic nature is not supported for Linux yet.                         */
/*                                                                        */
/* Each virtual ethernet a given Linux partition participates in will     */
/* cause a network device with the form ethXX to be created,              */
/*                                                                        */
/* This driver (and others like it on other partitions) is responsible for*/
/* routing packets to and from other partitions.  The MAC addresses used  */
/* by the virtual ethernets contain meaning, and should not be modified.  */
/* Doing so could disable the ability of your Linux partition to          */
/* communicate with the other OS/400 partitions on your physical iSeries. */
/* Similarly, setting the MAC address to something other than the         */
/* "virtual burned-in" address is not allowed, for the same reason.       */
/*                                                                        */
/* Notes:                                                                 */
/*                                                                        */
/* 1. Although there is the capability to talk on multiple shared         */
/*    ethernets to communicate to the same partition, each shared         */
/*    ethernet to a given partition X will use a finite, shared amount    */
/*    of hypervisor messages to do the communication.  So having 2 shared */
/*    ethernets to the same remote partition DOES NOT double the          */
/*    available bandwidth.  Each of the 2 shared ethernets will share the */
/*    same bandwidth available to another.                                */
/*                                                                        */
/* 2. It is allowed to have a virtual ethernet that does not communicate  */
/*    with any other partition.  It won't do anything, but it's allowed.  */
/*                                                                        */
/* 3. There is no "loopback" mode for a virtual ethernet device.  If you  */
/*    send a packet to your own mac address, it will just be dropped, you */
/*    won't get it on the receive side.  Such a thing could be done,      */
/*    but my default driver DOES NOT do so.                               */
/*                                                                        */
/* 4. Multicast addressing is implemented via broadcasting the multicast  */
/*    frames to other partitions.  It is the responsibility of the        */
/*    receiving partition to filter the addresses desired.                */
/*                                                                        */
/* 5. This module utilizes several different bottom half handlers for     */
/*    non-high-use path function (setup, error handling, etc.).  Multiple */
/*    bottom halves were used because only one would not keep up to the   */
/*    much faster iSeries device drivers this Linux driver is talking to. */
/*    All hi-priority work (receiving frames, handling frame acks) is done*/
/*    in the interrupt handler for maximum performance.                   */
/*                                                                        */
/* Tunable parameters:                                                    */
/*                                                                        */
/* VETH_NUMBUFFERS: This compile time option defaults to 120. It can      */
/* be safely changed to something greater or less than the default.  It   */
/* controls how much memory Linux will allocate per remote partition it is*/
/* communicating with.  The user can play with this to see how it affects */
/* performance, packets dropped, etc.  Without trying to understand the   */
/* complete driver, it can be thought of as the maximum number of packets */
/* outstanding to a remote partition at a time.                           */
/*                                                                        */
/**************************************************************************/

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
#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif
#include <asm/iSeries/mf.h>
#include <asm/uaccess.h>

#include "iseries_veth.h"
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/iSeries_dma.h>
#include <asm/semaphore.h>

#define veth_printk(prio, fmt, args...) \
	printk(prio "%s: " fmt, __FILE__, ## args)

#define veth_error(fmt, args...) \
	printk(KERN_ERR "(%s:%3.3d) ERROR: " fmt, __FILE__, __LINE__ , ## args)

#define VETH_NUMBUFFERS		120

static struct VethFabricMgr *mFabricMgr = NULL;

static int veth_open(struct net_device *dev);
static int veth_close(struct net_device *dev);
static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int veth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static void veth_handle_event(struct HvLpEvent *, struct pt_regs *);
static void veth_handle_ack(struct VethLpEvent *);
static void veth_handle_int(struct VethLpEvent *);
static void veth_init_connection(struct VethLpConnection *cnx, u8 rlp);
static void veth_open_connection(u8);
static void veth_finish_open_connection(void *parm);
static void veth_closeConnection(u8);
static void veth_set_multicast_list(struct net_device *dev);

static void veth_take_cap(struct VethLpConnection *, struct VethLpEvent *);
static void veth_take_cap_ack(struct VethLpConnection *, struct VethLpEvent *);
static void veth_take_monitor_ack(struct VethLpConnection *,
				  struct VethLpEvent *);
static void veth_recycle_msg(struct VethLpConnection *, struct VethMsg *);
static void veth_monitor_ack_task(void *);
static void veth_receive(struct VethLpConnection *, struct VethLpEvent *);
static int veth_pTransmit(struct sk_buff *skb, HvLpIndex rlp,
			  struct net_device *dev);
static struct net_device_stats *veth_get_stats(struct net_device *dev);
static void veth_timed_ack(unsigned long connectionPtr);
static void veth_startQueues(void);
static void veth_failMe(struct VethLpConnection *cnx);

extern struct pci_dev *iSeries_veth_dev;
static struct net_device *veth_devices[HVMAXARCHITECTEDVIRTUALLANS];
static int veth_num_devices; /* = 0 */

#define VETH_MAX_MTU		9000

MODULE_AUTHOR("Kyle Lucke <klucke@us.ibm.com>");
MODULE_DESCRIPTION("iSeries Virtual ethernet driver");
MODULE_LICENSE("GPL");

int VethModuleReopen = 1;

static inline u64 veth_dma_addr(void *p)
{
	return 0x8000000000000000LL | virt_to_absolute((unsigned long) p);
}

static inline HvLpEvent_Rc 
veth_signalevent(struct VethLpConnection *cnx, u16 subtype, 
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

static inline HvLpEvent_Rc
veth_signaldata(struct VethLpConnection *cnx, u16 subtype,
		u64 token, void *data)
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

struct net_device * __init veth_probe_one(int idx)
{
	struct net_device *dev;
	struct veth_port *port;
	int rc;

	dev = alloc_etherdev(sizeof (struct veth_port));
	if (! dev) {
		veth_error("Unable to allocate net_device structure!\n");
		return NULL;
	}

	port = (struct veth_port *) dev->priv;

	memset(port, 0, sizeof(*port));
	
	port->mDev = dev;
	rwlock_init(&port->mcast_gate);

	dev->dev_addr[0] = 0x02;
	dev->dev_addr[1] = 0x01;
	dev->dev_addr[2] = 0xff;
	dev->dev_addr[3] = idx;
	dev->dev_addr[4] = 0xff;
	dev->dev_addr[5] = HvLpConfig_getLpIndex_outline();

	dev->mtu = VETH_MAX_MTU;

	memcpy(&port->mMyAddress, dev->dev_addr, 6);

	dev->open = &veth_open;
	dev->hard_start_xmit = &veth_start_xmit;
	dev->stop = &veth_close;
	dev->get_stats = veth_get_stats;
	dev->set_multicast_list = &veth_set_multicast_list;
	dev->do_ioctl = &veth_ioctl;

	rc = register_netdev(dev);
	if (rc != 0) {
		veth_printk(KERN_ERR,
			    "Failed to register an ethernet device (veth=%d)\n",
			    idx);
		kfree(dev);
		return NULL;
	}

	veth_printk(KERN_DEBUG, "Found an ethernet device %s (veth=%d) (addr=%p)\n",
		    dev->name, idx, dev);

	return dev;
}

int __init veth_probe(void)
{
	int vlans_found = 0;
	u16 vlan_map = HvLpConfig_getVirtualLanIndexMap();
	int i;

	memset(veth_devices, 0, sizeof(veth_devices));

	for (i = 0; vlan_map != 0; vlan_map <<= 1, i++) {
		struct net_device *dev = NULL;

		if (! (vlan_map & 0x8000))
			continue;

		vlans_found++;
		dev = veth_probe_one(i);

		if (dev) {
			mFabricMgr->mPorts[i] = (struct veth_port *)dev->priv;
			veth_devices[veth_num_devices] = dev;
			veth_num_devices++;
		}
	}

	if (vlans_found == 0)
		return -ENODEV;

	return 0;
}

void __exit veth_module_cleanup(void)
{
	int i;
	struct VethFabricMgr *myFm = mFabricMgr;

	if (! mFabricMgr)
		return;

	VethModuleReopen = 0;
	
	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct VethLpConnection *cnx = &(mFabricMgr->mConnection[i]);
		unsigned long flags;

		spin_lock_irqsave(&cnx->status_gate, flags);
		veth_closeConnection(i);
		spin_unlock_irqrestore(&cnx->status_gate, flags);
	}
	
	flush_scheduled_work();
	
	HvLpEvent_unregisterHandler(HvLpEvent_Type_VirtualLan);
	
	mb();
	mFabricMgr = NULL;
	mb();
	
	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct VethLpConnection *cnx = &myFm->mConnection[i];

		if (cnx->mNumberAllocated + cnx->mNumberRcvMsgs > 0) {
			mf_deallocateLpEvents(cnx->remote_lp,
					      HvLpEvent_Type_VirtualLan,
					      cnx->mNumberAllocated
					      + cnx->mNumberRcvMsgs,
					      NULL, NULL);
		}
		
		if (cnx->mMsgs)
			kfree(cnx->mMsgs);
	}
	
	for (i = 0; i < HvMaxArchitectedVirtualLans; ++i) {
		struct net_device *dev;

		if (! myFm->mPorts[i])
			continue;

		dev = myFm->mPorts[i]->mDev;
		myFm->mPorts[i] = NULL;

		mb();
			
		if (dev) {
			unregister_netdev(dev);
			kfree(dev);
		}
	}
	
	kfree(myFm);
}

module_exit(veth_module_cleanup);

int __init veth_module_init(void)
{
	int i;
	int this_lp = mFabricMgr->mThisLp;
	int rc;

	mFabricMgr = kmalloc(sizeof (struct VethFabricMgr), GFP_KERNEL);
	if (! mFabricMgr) {
		veth_error("Unable to allocate fabric manager\n");
		return -ENOMEM;
	}

	memset(mFabricMgr, 0, sizeof (*mFabricMgr));

	mFabricMgr->mEyecatcher = 0x56455448464D4752ULL;
	this_lp = HvLpConfig_getLpIndex_outline();
	mFabricMgr->mThisLp = this_lp;

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct VethLpConnection *cnx = &mFabricMgr->mConnection[i];

		veth_init_connection(cnx, i);
	}

	rc = veth_probe();
	if (rc != 0)
		return rc;

	HvLpEvent_registerHandler(HvLpEvent_Type_VirtualLan, &veth_handle_event);

	/* Run through the active lps and open connections to the ones
	 * we need to */
	/* FIXME: is there any reason to do this backwards? */
	for (i = HVMAXARCHITECTEDLPS - 1; i >= 0; --i) {
		struct VethLpConnection *cnx = &mFabricMgr->mConnection[i];

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

static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned char *frame = skb->data;
	struct veth_port *port = (struct veth_port *) dev->priv;
	int i;
	int rc = 1;
	int individual_rc;
	int skb_len = skb->len;

	if (! mFabricMgr) {
		veth_error("NULL fabric manager with active ports!\n");
		netif_stop_queue(dev);
		BUG();
		return 1;
	}

	if (! (frame[0] & 0x01)) {
		/* unicast packet */
		HvLpIndex rlp = frame[5];

		if ((rlp != mFabricMgr->mThisLp)
		    &&
		    (HvLpConfig_doLpsCommunicateOnVirtualLan
		     (mFabricMgr->mThisLp, rlp))) {
			rc = veth_pTransmit(skb, rlp, dev);
		} else {
			dev_kfree_skb(skb);
			rc = 0;
		}
	} else {
		/* broadcast or multicast */
		for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
			if (i == mFabricMgr->mThisLp)
				continue;

			if (HvLpConfig_doLpsCommunicateOnVirtualLan
			    (mFabricMgr->mThisLp, i)) {
				struct sk_buff *clone =
					skb_clone(skb, GFP_ATOMIC);
				
				if (! clone) {
					veth_error("skb_clone failed %p\n",
						   skb);
					rc = 0;
					break;
				}
				
				/* the ack handles deleting the skb */
				individual_rc = veth_pTransmit(clone, i, dev);
				
				/* tx failed, we need to free the sbk */
				if (individual_rc != 0)
					dev_kfree_skb(clone);
				
				/* if we didn't fail from lack of
				 * buffers, the tx as a whole is
				 * successful */
				if (individual_rc != 1)
					rc = 0;
			}
		}

		/* broadcast/multicast - If every connection is out of
		   buffers (highly unlikely) then we leave rc set to 1
		   and stop the queue. If any connection fails for any
		   reason other than out of buffers, then we say the
		   tx succeeded.
		 */
		if (rc == 0)
			dev_kfree_skb(skb);
	}

	if (rc != 0) {
		if (rc == 1) {
			/* reasons for stopping the queue:
			   - a non broadcast/multicast packet was destined for a connection that is out of buffers
			   - a broadcast/multicast packet and every connection was out of buffers
			 */

			netif_stop_queue(dev);
		} else {
			/* reasons for not stopping the queue:
			   - a non broadcast/multicast packet was destined for a failed connection
			   - a broadcast/multicast packet and at least one connection had available buffers
			 */
			dev_kfree_skb(skb);
			rc = 0;
		}
	} else {
		port->stats.tx_packets++;
		port->stats.tx_bytes += skb_len;
	}

	return rc;
}

static int
veth_pTransmit(struct sk_buff *skb, HvLpIndex rlp, struct net_device *dev)
{
	struct VethLpConnection *cnx = mFabricMgr->mConnection + rlp;
	HvLpEvent_Rc rc;
	u32 dma_address, dma_length;
	struct VethMsg *msg = NULL;

	if (! cnx->status.ready)
		return 2;

	if ((skb->len - 14) > VETH_MAX_MTU)
		return 2;

	VETHSTACKPOP(&cnx->mMsgStack, msg);

	if (! msg)
		return 1;

	dma_length = skb->len;
	dma_address = pci_map_single(iSeries_veth_dev, skb->data,
				     dma_length, PCI_DMA_TODEVICE);
	
	/* Is it really necessary to check the length and address
	 * fields of the first entry here? */
	if (dma_address != NO_TCE) {
		msg->skb = skb;
		msg->mSendData.addr[0] = dma_address;
		msg->mSendData.len[0] = dma_length;
		msg->mSendData.eof = 1;
		set_bit(0, &(msg->mInUse));
		rc = veth_signaldata(cnx, VethEventTypeFrames,
				     msg->mIndex, &msg->mSendData);
	} else {
		struct veth_port *port = (struct veth_port *) dev->priv;
		rc = -1;	/* Bad return code */
		port->stats.tx_errors++;
	}
	
	if (rc != HvLpEvent_Rc_Good) {
		msg->skb = NULL;
		/* need to set in use to make veth_recycle_msg in case
		 * this was a mapping failure */
		set_bit(0, &msg->mInUse);
		veth_recycle_msg(cnx, msg);
		return 2;
	}

	return 0;
}

static int
veth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
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
				memcpy(&xaddr, addr, 6);
				port->mcast_addr[port->num_mcast] = xaddr;
				port->num_mcast++;
			}
			dmi = dmi->next;
		}
	}

	write_unlock_irqrestore(&port->mcast_gate, flags);
}

static void
veth_handle_event(struct HvLpEvent *event, struct pt_regs *regs)
{
	struct VethLpEvent *veth_event = (struct VethLpEvent *)event;

	if (event->xFlags.xFunction == HvLpEvent_Function_Ack)
		veth_handle_ack(veth_event);
	else if (event->xFlags.xFunction == HvLpEvent_Function_Int)
		veth_handle_int(veth_event);
}

static void
veth_handle_ack(struct VethLpEvent *event)
{
	HvLpIndex rlp = event->mBaseEvent.xTargetLp;
	struct VethLpConnection *cnx = &mFabricMgr->mConnection[rlp];

	switch (event->mBaseEvent.xSubtype) {
	case VethEventTypeCap:
		veth_take_cap_ack(cnx, event);
		break;
	case VethEventTypeMonitor:
		veth_take_monitor_ack(cnx, event);
		break;
	default:
		veth_error("Unknown ack type %d from lpar %d\n",
			   event->mBaseEvent.xSubtype, rlp);
	};
}

static void
veth_handle_int(struct VethLpEvent *event)
{
	HvLpIndex rlp = event->mBaseEvent.xSourceLp;
	struct VethLpConnection *cnx = &mFabricMgr->mConnection[rlp];
	int i;

	switch (event->mBaseEvent.xSubtype) {
	case VethEventTypeCap:
		veth_take_cap(cnx, event);
		break;
	case VethEventTypeMonitor:
		/* do nothing... this'll hang out here til we're dead,
		 * and the hypervisor will return it for us. */
		break;
	case VethEventTypeFramesAck:
		for (i = 0; i < VETH_MAX_ACKS_PER_MSG; ++i) {
			u16 msgnum = event->u.mFramesAckData.mToken[i];

			if (msgnum < cnx->mNumMsgs)
				veth_recycle_msg(cnx, cnx->mMsgs + msgnum);
		}
		break;
	case VethEventTypeFrames:
		veth_receive(cnx, event);
		break;
	default:
		veth_error("Unknown interrupt type %d from lpar %d\n",
			   event->mBaseEvent.xSubtype, rlp);
	};
}

static void veth_failMe(struct VethLpConnection *cnx)
{
	cnx->status.ready = 0;
}

static void veth_init_connection(struct VethLpConnection *cnx, u8 rlp)
{
	struct VethMsg *msgs;
	HvLpIndex this_lp = mFabricMgr->mThisLp;
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

	msgs = kmalloc(VETH_NUMBUFFERS * sizeof(struct VethMsg), GFP_KERNEL);
	if (! msgs)
		return;

	cnx->mMsgs = msgs;
	memset(msgs, 0, VETH_NUMBUFFERS * sizeof(struct VethMsg));
	spin_lock_init(&cnx->mMsgStack.lock);

	for (i = 0; i < VETH_NUMBUFFERS; i++) {
		msgs[i].mIndex = i;
		VETHSTACKPUSH(&cnx->mMsgStack, msgs + i);
	}

	cnx->mNumMsgs = VETH_NUMBUFFERS;

	cnx->mNumberAllocated = veth_allocate_events(rlp, 2);

	if (cnx->mNumberAllocated < 2) {
		veth_error("Couldn't allocate base msgs for lpar %d, only got %d\n",
			   cnx->remote_lp, cnx->mNumberAllocated);
		veth_failMe(cnx);
		return;
	}

	cnx->mNumberRcvMsgs = veth_allocate_events(cnx->remote_lp,
						   VETH_NUMBUFFERS);
}

static void veth_open_connection(u8 rlp)
{
	struct VethLpConnection *cnx = &mFabricMgr->mConnection[rlp];
	HvLpEvent_Rc rc;
	u64 *rawcap = (u64 *) &cnx->mMyCap;

	if (! cnx->mMsgs || (cnx->mNumberAllocated < 2)
	    || ! cnx->mNumberRcvMsgs) {
		veth_failMe(cnx);
		return;
	}

	spin_lock_irq(&cnx->ack_gate);

	memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));
	cnx->num_pending_acks = 0;

	HvCallEvent_openLpEventPath(rlp, HvLpEvent_Type_VirtualLan);

	cnx->status.mOpen = 1;

	cnx->src_inst = 
		HvCallEvent_getSourceLpInstanceId(rlp,
						  HvLpEvent_Type_VirtualLan);
	cnx->dst_inst =
		HvCallEvent_getTargetLpInstanceId(rlp,
						  HvLpEvent_Type_VirtualLan);

	spin_unlock_irq(&cnx->ack_gate);

	cnx->mMyCap.mNumberBuffers = cnx->mNumMsgs;

	if (cnx->mNumMsgs < 10)
		cnx->mMyCap.mThreshold = 1;
	else if (cnx->mNumMsgs < 20)
		cnx->mMyCap.mThreshold = 4;
	else if (cnx->mNumMsgs < 40)
		cnx->mMyCap.mThreshold = 10;
	else
		cnx->mMyCap.mThreshold = 20;

	cnx->mMyCap.mTimer = VETH_ACKTIMEOUT;

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
	struct VethLpConnection *cnx = (struct VethLpConnection *)parm;
	struct VethCapData *remoteCap = &cnx->mRemoteCap;
	u64 numAcks = 0;
	HvLpEvent_Rc rc;

	spin_lock_irq(&cnx->status_gate);

	memcpy(remoteCap, &cnx->cap_event.u.mCapabilitiesData,
	       sizeof(*remoteCap));

	if ( (remoteCap->mNumberBuffers == 0)
	     || (remoteCap->mThreshold > VETH_MAX_ACKS_PER_MSG)
	     || (remoteCap->mThreshold == 0) ) {
		veth_error("Received incompatible capabilities from lpar %d\n",
			   cnx->remote_lp);
		cnx->cap_event.mBaseEvent.xRc = HvLpEvent_Rc_InvalidSubtypeData;
		HvCallEvent_ackLpEvent((struct HvLpEvent *)&cnx->cap_event);

		veth_failMe(cnx);
		goto out;
	}

	numAcks = (remoteCap->mNumberBuffers / remoteCap->mThreshold) + 1;

	if (cnx->mNumberLpAcksAlloced < numAcks) {
		int num;
		
		numAcks = numAcks - cnx->mNumberLpAcksAlloced;
		
		spin_unlock_irq(&cnx->status_gate);
		
		num = veth_allocate_events(cnx->remote_lp, numAcks);
		
		if (num > 0)
			cnx->mNumberLpAcksAlloced += num;
		spin_lock_irq(&cnx->status_gate);
		
	}
	
	/* Convert timer to jiffies */
	if (cnx->mMyCap.mTimer)
		cnx->ack_timeout = remoteCap->mTimer * HZ / 1000000;
	else
		cnx->ack_timeout = VETH_ACKTIMEOUT * HZ / 1000000;

	if (cnx->mNumberLpAcksAlloced < numAcks) {
		veth_error("Couldn't allocate all the frames ack events for lpar %d\n",
			   cnx->remote_lp);

		cnx->cap_event.mBaseEvent.xRc = HvLpEvent_Rc_BufferNotAvailable;
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

	if (cnx->cap_ack_event.mBaseEvent.xRc != HvLpEvent_Rc_Good) {
		veth_printk(KERN_ERR, "Bad rc(%d) from lpar %d on capabilities\n",
			    cnx->cap_ack_event.mBaseEvent.xRc, cnx->remote_lp);
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

static void
veth_closeConnection(u8 rlp)
{
	struct VethLpConnection *cnx = &mFabricMgr->mConnection[rlp];
	unsigned long flags;

	del_timer_sync(&cnx->ack_timer);

	cnx->status.sent_caps = 0;
	cnx->status.got_cap = 0;
	cnx->status.got_cap_ack = 0;

	if (cnx->status.mOpen) {
		int i;		

		HvCallEvent_closeLpEventPath(rlp, HvLpEvent_Type_VirtualLan);
		cnx->status.mOpen = 0;
		veth_failMe(cnx);

		/* reset ack data */
		spin_lock_irqsave(&cnx->ack_gate, flags);

		memset(&cnx->pending_acks, 0xff, sizeof (cnx->pending_acks));
		cnx->num_pending_acks = 0;

		spin_unlock_irqrestore(&cnx->ack_gate, flags);

		/* Clean up any leftover messages */
		for (i = 0; i < cnx->mNumMsgs; ++i)
			veth_recycle_msg(cnx, cnx->mMsgs + i);
	}

}

static void veth_take_cap(struct VethLpConnection *cnx, struct VethLpEvent *event)
{
	unsigned long flags;
	HvLpEvent_Rc rc;

	spin_lock_irqsave(&cnx->status_gate, flags);

	if (cnx->status.got_cap) {
		veth_error("Received a second capabilities from lpar %d\n",
			   cnx->remote_lp);
		event->mBaseEvent.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
		goto out;
	}

	memcpy(&cnx->cap_event, event, sizeof (cnx->cap_event));
	/* If we failed to send caps out before (presumably because
	 * the target lpar was down), send them now. */
	if (! cnx->status.sent_caps) {
		u64 *rawcap = (u64 *) &cnx->mMyCap;

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

static void
veth_take_cap_ack(struct VethLpConnection *cnx, struct VethLpEvent *event)
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

static void
veth_take_monitor_ack(struct VethLpConnection *cnx,  struct VethLpEvent *event)
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

static void
veth_recycle_msg(struct VethLpConnection *cnx, struct VethMsg *myMsg)
{
	u32 dma_address, dma_length;

	if (test_and_clear_bit(0, &myMsg->mInUse)) {
		dma_address = myMsg->mSendData.addr[0];
		dma_length = myMsg->mSendData.len[0];

		pci_unmap_single(iSeries_veth_dev, dma_address, dma_length,
				 PCI_DMA_TODEVICE);

		if (myMsg->skb) {
			dev_kfree_skb_any(myMsg->skb);
			myMsg->skb = NULL;
		}

		memset(&myMsg->mSendData, 0, sizeof(myMsg->mSendData));
		VETHSTACKPUSH(&cnx->mMsgStack, myMsg);
	} else {
		if (cnx->status.mOpen) {
			veth_error("Received a frames ack for msg %d from lpar %d while not outstanding\n",
				   myMsg->mIndex, cnx->remote_lp);
		}
	}

	veth_startQueues();
}

static void
veth_startQueues(void)
{
	int i;

	for (i = 0; i < veth_num_devices; ++i)
		netif_wake_queue(veth_devices[i]);
}

static void veth_monitor_ack_task(void *parm)
{
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;

	spin_lock_irq(&cnx->status_gate);

	veth_failMe(cnx);

	if (cnx->status.mOpen) {
		veth_closeConnection(cnx->remote_lp);

		udelay(100);
	}

	if (VethModuleReopen) {
		veth_open_connection(cnx->remote_lp);
	}
	cnx->status.monitor_ack_pending = 0;

	spin_unlock_irq(&cnx->status_gate);
}

static inline int veth_frame_wanted(struct veth_port *port, u64 mac_addr)
{
	int wanted = 0;
	int i;
	unsigned long flags;

	if ( (mac_addr == port->mMyAddress)
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

static void veth_receive(struct VethLpConnection *cnx, struct VethLpEvent *event)
{
	struct VethFramesData *senddata = &event->u.mSendData;
	int startchunk = 0;
	int nchunks;
	unsigned long flags;
	HvLpDma_Rc rc;

	do {
		u16 length = 0;
		struct sk_buff *skb;
		struct dma_chunk local_list[VETH_MAX_PAGES_PER_FRAME];
		struct dma_chunk remote_list[VETH_MAXFRAMESPERMSG];
		u64 dest;
		HvLpVirtualLanIndex vlan;
		struct veth_port *port;

		/* FIXME: do we need this? */
		memset(local_list, 0, sizeof(local_list));
		memset(remote_list, 0, sizeof(VETH_MAXFRAMESPERMSG));

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
					    event->mBaseEvent.xSourceLp,
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
		port = mFabricMgr->mPorts[vlan];
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
		skb->dev = port->mDev;
		skb->protocol = eth_type_trans(skb, port->mDev);
		skb->ip_summed = CHECKSUM_NONE;
		netif_rx(skb);	/* send it up */
		port->stats.rx_packets++;
		port->stats.rx_bytes += length;
	} while (startchunk += nchunks, startchunk < VETH_MAXFRAMESPERMSG);

	/* Ack it */
	spin_lock_irqsave(&cnx->ack_gate, flags);
	
	if (cnx->num_pending_acks < VETH_MAX_ACKS_PER_MSG) {
		cnx->pending_acks[cnx->num_pending_acks] =
			event->mBaseEvent.xCorrelationToken;
		++cnx->num_pending_acks;
		
		if (cnx->num_pending_acks == cnx->mRemoteCap.mThreshold) {
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
	struct VethLpConnection *cnx = (struct VethLpConnection *) ptr;

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

	veth_startQueues();

	/* Reschedule the timer */
	cnx->ack_timer.expires = jiffies + cnx->ack_timeout;
	add_timer(&cnx->ack_timer);
}
