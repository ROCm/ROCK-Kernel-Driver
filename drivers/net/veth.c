/* File veth.c created by Kyle A. Lucke on Mon Aug  7 2000. */

/**************************************************************************/
/*                                                                        */
/* IBM eServer iSeries Virtual Ethernet Device Driver                     */
/* Copyright (C) 2001 Kyle A. Lucke (klucke@us.ibm.com), IBM Corp.        */
/*                                                                        */
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
/* The virtual ethernet a given ethXX virtual ethernet device talks on    */
/* can be determined either by dumping /proc/iSeries/veth/vethX, where    */
/* X is the virtual ethernet number, and the netdevice name will be       */
/* printed out.  The virtual ethernet a given ethX device communicates on */
/* is also printed to the printk() buffer at module load time.            */
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
/* VethBuffersToAllocate: This compile time option defaults to 120. It can*/
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

#include "veth.h"
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/veth-proc.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/iSeries_proc.h>
#include <asm/iSeries/iSeries_dma.h>
#include <asm/semaphore.h>
#include <linux/proc_fs.h>

#define veth_printk(fmt, args...) \
printk(KERN_INFO "%s: " fmt, __FILE__, ## args)

#define veth_error(fmt, args...) \
printk(KERN_ERR "(%s:%3.3d) ERROR: " fmt, __FILE__, __LINE__ , ## args)

static char *version __initdata =
	"v1.06 05/04/2003  Kyle Lucke, klucke@us.ibm.com\n";

#define VethBuffersToAllocate 120

static struct VethFabricMgr *mFabricMgr = NULL;
static struct proc_dir_entry *veth_proc_root = NULL;

DECLARE_MUTEX_LOCKED(VethProcSemaphore);

static int veth_open(struct net_device *dev);
static int veth_close(struct net_device *dev);
static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int veth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static void veth_handleEvent(struct HvLpEvent *, struct pt_regs *);
static void veth_handleAck(struct HvLpEvent *);
static void veth_handleInt(struct HvLpEvent *);
static void veth_open_connections(void);
static void veth_openConnection(u8);
static void veth_closeConnection(u8);
static void veth_intFinishOpeningConnections(void *, int number);
static void veth_finishOpeningConnections(void *);
static void veth_finishOpeningConnectionsLocked(struct VethLpConnection *);
static int veth_multicast_wanted(struct veth_port *port, u64 dest);
static void veth_set_multicast_list(struct net_device *dev);

static void veth_sendCap(struct VethLpConnection *);
static void veth_sendMonitor(struct VethLpConnection *);
static void veth_takeCap(struct VethLpConnection *, struct VethLpEvent *);
static void veth_takeCapAck(struct VethLpConnection *, struct VethLpEvent *);
static void veth_takeMonitorAck(struct VethLpConnection *,
				struct VethLpEvent *);
static void veth_msgsInit(struct VethLpConnection *cnx);
static void veth_recycleMsgByNum(struct VethLpConnection *, u16);
static void veth_recycleMsg(struct VethLpConnection *, struct VethMsg *);
static void veth_capTask(void *);
static void veth_capAckTask(void *);
static void veth_monitorAckTask(void *);
static void veth_takeFrames(struct VethLpConnection *, struct VethLpEvent *);
static int veth_pTransmit(struct sk_buff *skb, HvLpIndex remoteLp,
			  struct net_device *dev);
static struct net_device_stats *veth_get_stats(struct net_device *dev);
static void veth_intFinishMsgsInit(void *, int);
static void veth_finishMsgsInit(struct VethLpConnection *cnx);
static void veth_intFinishCapTask(void *, int);
static void veth_finishCapTask(struct VethLpConnection *cnx);
static void veth_finishCapTaskLocked(struct VethLpConnection *cnx);
static void veth_finishSendCap(struct VethLpConnection *cnx);
static void veth_timedAck(unsigned long connectionPtr);
static void veth_startQueues(void);
static void veth_failMe(struct VethLpConnection *cnx);

static int proc_veth_dump_connection(char *page, char **start, off_t off,
				     int count, int *eof, void *data);
static int proc_veth_dump_port(char *page, char **start, off_t off, int count,
			       int *eof, void *data);


extern struct pci_dev *iSeries_veth_dev;
static struct net_device *veth_devices[16];
static int veth_dev_queue_stopped[16];
static int veth_num_devices; /* = 0 */

#define VETH_MAX_MTU		9000

MODULE_AUTHOR("Kyle Lucke <klucke@us.ibm.com>");
MODULE_DESCRIPTION("iSeries Virtual ethernet driver");
MODULE_LICENSE("GPL");

int VethModuleReopen = 1;

static void
show_packet(struct sk_buff *skb, char c)
{
	int i;

	for (i = 0; i < 60; i++) {
		if (i >= skb->len)
			break;

		printk("%02x%c", (unsigned)skb->data[i], c);
	}
	printk("\n");
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

struct net_device * __init
veth_probe_one(int idx)
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
		veth_printk
			("Failed to register an ethernet device (veth=%d)\n",
			 idx);
		kfree(dev);
		return NULL;
	}

	veth_printk("Found an ethernet device %s (veth=%d) (addr=%p)\n",
		    dev->name, idx, dev);

	return dev;
}

int __init
veth_probe(void)
{
	int vlans_found = 0;
	u16 vlan_map = HvLpConfig_getVirtualLanIndexMap();
	int i;

	memset(veth_devices, 0, sizeof (struct net_device *) * 16);
	memset(veth_dev_queue_stopped, 0, sizeof (int) * 16);

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

void
veth_proc_delete(struct proc_dir_entry *iSeries_proc)
{
	int i = 0;
	HvLpIndex thisLp = HvLpConfig_getLpIndex_outline();
	u16 vlanMap = HvLpConfig_getVirtualLanIndexMap();
	int vlanIndex = 0;

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		if (i == thisLp)
			continue;

		if (HvLpConfig_doLpsCommunicateOnVirtualLan(thisLp, i)) {
			char name[10];
			sprintf(name, "lpar%d", i);
			remove_proc_entry(name, veth_proc_root);
		}
	}

	while (vlanMap != 0) {
		if (vlanMap & 0x8000) {
			char name[10];
			sprintf(name, "veth%d", vlanIndex);
			remove_proc_entry(name, veth_proc_root);
		}

		++vlanIndex;
		vlanMap = vlanMap << 1;
	}

	remove_proc_entry("veth", iSeries_proc);

	up(&VethProcSemaphore);
}

void __exit
veth_module_cleanup(void)
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
	
	down(&VethProcSemaphore);
	
	iSeries_proc_callback(&veth_proc_delete);
	
	down(&VethProcSemaphore);
	
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
			veth_printk("Unregistering %s (veth=%d)\n",
				    dev->name, i);
			unregister_netdev(dev);
			kfree(dev);
		}
	}
	
	kfree(myFm);
}

module_exit(veth_module_cleanup);

void
veth_proc_init(struct proc_dir_entry *iSeries_proc)
{
	long i = 0;
	HvLpIndex thisLp = HvLpConfig_getLpIndex_outline();
	u16 vlanMap = HvLpConfig_getVirtualLanIndexMap();
	long vlanIndex = 0;

	veth_proc_root = proc_mkdir("veth", iSeries_proc);
	if (! veth_proc_root)
		return;

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		if (i == thisLp)
			continue;

		if (HvLpConfig_doLpsCommunicateOnVirtualLan(thisLp, i)) {
			struct proc_dir_entry *ent;
			char name[10];

			sprintf(name, "lpar%d", (int) i);
			ent = create_proc_entry(name, S_IFREG | S_IRUSR,
						veth_proc_root);
			if (! ent)
				return;
			ent->nlink = 1;
			ent->data = (void *) i;
			ent->read_proc = proc_veth_dump_connection;
			ent->write_proc = NULL;
		}
	}

	while (vlanMap != 0) {
		if (vlanMap & 0x8000) {
			struct proc_dir_entry *ent;
			char name[10];
			sprintf(name, "veth%d", (int) vlanIndex);
			ent = create_proc_entry(name, S_IFREG | S_IRUSR,
						veth_proc_root);
			if (! ent)
				return;
			ent->nlink = 1;
			ent->data = (void *) vlanIndex;
			ent->read_proc = proc_veth_dump_port;
			ent->write_proc = NULL;
		}

		++vlanIndex;
		vlanMap = vlanMap << 1;
	}

	up(&VethProcSemaphore);
}

int __init
veth_module_init(void)
{
	int i;
	int rc;

	mFabricMgr = kmalloc(sizeof (struct VethFabricMgr), GFP_KERNEL);
	if (! mFabricMgr) {
		veth_error("Unable to allocate fabric manager\n");
		return -ENOMEM;
	}

	memset(mFabricMgr, 0, sizeof (*mFabricMgr));
	veth_printk("Initializing veth module, fabric mgr (address=%p)\n",
		    mFabricMgr);

	mFabricMgr->mEyecatcher = 0x56455448464D4752ULL;
	mFabricMgr->mThisLp = HvLpConfig_getLpIndex_outline();

	for (i = 0; i < HVMAXARCHITECTEDLPS; ++i) {
		struct VethLpConnection *cnx =
			&mFabricMgr->mConnection[i];

		cnx->mEyecatcher = 0x564554484C50434EULL;
		veth_failMe(cnx);
		spin_lock_init(&cnx->ack_gate);
		spin_lock_init(&cnx->status_gate);
	}

	rc = veth_probe();
	if (rc != 0)
		return rc;

	veth_printk("%s", version);
	veth_open_connections();
	iSeries_proc_callback(&veth_proc_init);

	return 0;
}

module_init(veth_module_init);

static void
veth_failMe(struct VethLpConnection *cnx)
{
	cnx->status.mSentCap = 0;
	cnx->status.mCapAcked = 0;
	cnx->status.mGotCap = 0;
	cnx->status.mGotCapAcked = 0;
	cnx->status.mSentMonitor = 0;
	cnx->status.mFailed = 1;
}

static int
veth_open(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;

	memset(&port->stats, 0, sizeof (port->stats));

	netif_start_queue(dev);

	return 0;
}

static int
veth_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}

static struct net_device_stats *
veth_get_stats(struct net_device *dev)
{
	struct veth_port *port = (struct veth_port *) dev->priv;

	return (&port->stats);
}

static int
veth_start_xmit(struct sk_buff *skb, struct net_device *dev)
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
		HvLpIndex remoteLp = frame[5];

		if ((remoteLp != mFabricMgr->mThisLp)
		    &&
		    (HvLpConfig_doLpsCommunicateOnVirtualLan
		     (mFabricMgr->mThisLp, remoteLp))) {
			rc = veth_pTransmit(skb, remoteLp, dev);
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

			int number = dev->dev_addr[3];
			++veth_dev_queue_stopped[number];
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
veth_pTransmit(struct sk_buff *skb, HvLpIndex remoteLp, struct net_device *dev)
{
	struct VethLpConnection *cnx =
		mFabricMgr->mConnection + remoteLp;
	HvLpEvent_Rc rc;
	u32 dma_address, dma_length;
	struct VethMsg *msg = NULL;

/* 	printk("Tx (%d):", remoteLp); */
/* 	show_packet(skb, ':'); */

	if (cnx->status.mFailed)
		return 2;

	VETHSTACKPOP(&(cnx->mMsgStack), msg);

	if (! msg)
		return 1;

	if ((skb->len - 14) > VETH_MAX_MTU) {
		veth_recycleMsg(cnx, msg);
		return 2;
	}

	dma_length = skb->len;
	dma_address = pci_map_single(iSeries_veth_dev, skb->data,
				     dma_length, PCI_DMA_TODEVICE);
	
	/* Is it really necessary to check the length and address
	 * fields of the first entry here? */
	if (dma_address != NO_TCE) {
		msg->skb = skb;
		msg->mEvent.mSendData.mAddress[0] = dma_address;
		msg->mEvent.mSendData.mLength[0] = dma_length;
		msg->mEvent.mSendData.mEofMask = 1;
		set_bit(0, &(msg->mInUse));
		rc = veth_signalevent(cnx,
				      VethEventTypeFrames,
				      HvLpEvent_AckInd_NoAck,
				      HvLpEvent_AckType_ImmediateAck,
				      msg->mIndex,
				      msg->mEvent.raw[0],
				      msg->mEvent.raw[1],
				      msg->mEvent.raw[2],
				      msg->mEvent.raw[3],
				      msg->mEvent.raw[4]);
	} else {
		struct veth_port *port = (struct veth_port *) dev->priv;
		rc = -1;	/* Bad return code */
		port->stats.tx_errors++;
	}
	
	if (rc != HvLpEvent_Rc_Good) {
		msg->skb = NULL;
		/* need to set in use to make veth_recycleMsg in case
		 * this was a mapping failure */
		set_bit(0, &(msg->mInUse));
		veth_recycleMsg(cnx, msg);
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
		ecmd.supported =
		    (SUPPORTED_1000baseT_Full |
		     SUPPORTED_Autoneg | SUPPORTED_FIBRE);
		ecmd.advertising =
		    (SUPPORTED_1000baseT_Full |
		     SUPPORTED_Autoneg | SUPPORTED_FIBRE);

		ecmd.port = PORT_FIBRE;
		ecmd.transceiver = XCVR_INTERNAL;
		ecmd.phy_address = 0;
		ecmd.speed = SPEED_1000;
		ecmd.duplex = DUPLEX_FULL;
		ecmd.autoneg = AUTONEG_ENABLE;
		ecmd.maxtxpkt = 120;
		ecmd.maxrxpkt = 120;
		if (copy_to_user(ifr->ifr_data, &ecmd, sizeof (ecmd)))
			return -EFAULT;
		return 0;

	case ETHTOOL_GDRVINFO:{
			struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
			strncpy(info.driver, "veth", sizeof (info.driver) - 1);
			info.driver[sizeof (info.driver) - 1] = '\0';
			strncpy(info.version, "1.0", sizeof (info.version) - 1);
			if (copy_to_user(ifr->ifr_data, &info, sizeof (info)))
				return -EFAULT;
			return 0;
		}
		/* get link status */
	case ETHTOOL_GLINK:{
			struct ethtool_value edata = { ETHTOOL_GLINK };
			edata.data = 1;
			if (copy_to_user(ifr->ifr_data, &edata, sizeof (edata)))
				return -EFAULT;
			return 0;
		}

	default:
		break;
	}

#endif
	return -EOPNOTSUPP;
}

static void
veth_set_multicast_list(struct net_device *dev)
{
	char *addrs;
	struct veth_port *port = (struct veth_port *) dev->priv;
	u64 newAddress = 0;
	unsigned long flags;

	write_lock_irqsave(&port->mcast_gate, flags);

	if (dev->flags & IFF_PROMISC) {	/* set promiscuous mode */
		port->mPromiscuous = 1;
	} else {
		struct dev_mc_list *dmi = dev->mc_list;

		if ((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 12)) {
			port->all_mcast = 1;
		} else {
			int i;
			/* Update table */
			port->mNumAddrs = 0;

			for (i = 0; ((i < dev->mc_count) && (i < 12)); i++) {	/* for each address in the list */
				addrs = dmi->dmi_addr;
				dmi = dmi->next;
				if (addrs[0] & 0x01) {/* multicast address? */
					memcpy(&newAddress, addrs, 6);
					port->mMcasts[port->mNumAddrs] =
					    newAddress;
					mb();
					port->mNumAddrs = port->mNumAddrs + 1;
				}
			}
		}
	}

	write_unlock_irqrestore(&port->mcast_gate, flags);
}

static void
veth_handleEvent(struct HvLpEvent *event, struct pt_regs *regs)
{
	if (event->xFlags.xFunction == HvLpEvent_Function_Ack)
		veth_handleAck(event);
	else if (event->xFlags.xFunction == HvLpEvent_Function_Int)
		veth_handleInt(event);
}

static void
veth_handleAck(struct HvLpEvent *event)
{
	struct VethLpConnection *cnx =
	    &(mFabricMgr->mConnection[event->xTargetLp]);
	struct VethLpEvent *vethEvent = (struct VethLpEvent *) event;

	switch (event->xSubtype) {
	case VethEventTypeCap:
		veth_takeCapAck(cnx, vethEvent);
		break;
	case VethEventTypeMonitor:
		veth_takeMonitorAck(cnx, vethEvent);
		break;
	default:
		veth_error("Unknown ack type %d from lpar %d\n",
				  event->xSubtype,
				  cnx->remote_lp);

	};
}

static void
veth_handleInt(struct HvLpEvent *event)
{
	int i = 0;
	struct VethLpConnection *cnx =
	    &(mFabricMgr->mConnection[event->xSourceLp]);
	struct VethLpEvent *vethEvent = (struct VethLpEvent *) event;

	switch (event->xSubtype) {
	case VethEventTypeCap:
		veth_takeCap(cnx, vethEvent);
		break;
	case VethEventTypeMonitor:
		/* do nothing... this'll hang out here til we're dead,
		 * and the hypervisor will return it for us. */
		break;
	case VethEventTypeFramesAck:
		for (i = 0; i < VethMaxFramesMsgsAcked; ++i) {
			u16 msg =
				vethEvent->mDerivedData.mFramesAckData.mToken[i];
			veth_recycleMsgByNum(cnx, msg);
		}
		break;
	case VethEventTypeFrames:
		veth_takeFrames(cnx, vethEvent);
		break;
	default:
		veth_error("Unknown interrupt type %d from lpar %d\n",
				  event->xSubtype, cnx->remote_lp);
	};
}

static void
veth_open_connections(void)
{
	int i = 0;
	int this_lp = mFabricMgr->mThisLp;

	HvLpEvent_registerHandler(HvLpEvent_Type_VirtualLan,
				  &veth_handleEvent);

	/* Run through the active lps and open connections to the ones
	 * we need to */
	/* FIXME: is there any reason to do this backwards? */
	for (i = HVMAXARCHITECTEDLPS - 1; i >= 0; --i) {
		struct VethLpConnection *cnx = &mFabricMgr->mConnection[i];
		unsigned long flags;

		if (i == this_lp)
			continue;

		init_timer(&cnx->ack_timer);
		
		if (HvLpConfig_doLpsCommunicateOnVirtualLan(this_lp, i)) {
			spin_lock_irqsave(&cnx->status_gate, flags);
			veth_openConnection(i);
			spin_unlock_irqrestore(&cnx->status_gate, flags);
		}
	}
}

static void
veth_intFinishOpeningConnections(void *parm, int number)
{
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;
	cnx->mAllocTaskTq.data = parm;
	cnx->mNumberAllocated = number;
	schedule_work(&cnx->mAllocTaskTq);
}

static void
veth_finishOpeningConnections(void *parm)
{
	unsigned long flags;
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;
	spin_lock_irqsave(&cnx->status_gate, flags);
	veth_finishOpeningConnectionsLocked(cnx);
	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void
veth_finishOpeningConnectionsLocked(struct VethLpConnection *cnx)
{
	if (cnx->mNumberAllocated >= 2) {
		cnx->status.mCapMonAlloced = 1;
		veth_sendCap(cnx);
	} else {
		veth_error("Couldn't allocate base msgs for lpar %d, only got %d\n",
			   cnx->remote_lp, cnx->mNumberAllocated);
		veth_failMe(cnx);
	}
}

static void
veth_openConnection(u8 remoteLp)
{
	unsigned long flags;
	u64 i = 0;
	struct VethLpConnection *cnx = &mFabricMgr->mConnection[remoteLp];

	INIT_WORK(&cnx->mCapTaskTq, veth_capTask, NULL);
	INIT_WORK(&cnx->mCapAckTaskTq, veth_capAckTask, NULL);
	INIT_WORK(&cnx->mMonitorAckTaskTq, veth_monitorAckTask, NULL);
	INIT_WORK(&cnx->mAllocTaskTq, veth_finishOpeningConnections, NULL);

	cnx->remote_lp = remoteLp;

	spin_lock_irqsave(&cnx->ack_gate, flags);

	memset(&cnx->mEventData, 0xff, sizeof (cnx->mEventData));
	cnx->mNumAcks = 0;

	HvCallEvent_openLpEventPath(remoteLp, HvLpEvent_Type_VirtualLan);

	/* clean up non-acked msgs */
	for (i = 0; i < cnx->mNumMsgs; ++i)
		veth_recycleMsgByNum(cnx, i);

	cnx->status.mOpen = 1;

	cnx->src_inst = 
		HvCallEvent_getSourceLpInstanceId(remoteLp,
						  HvLpEvent_Type_VirtualLan);
	cnx->dst_inst =
		HvCallEvent_getTargetLpInstanceId(remoteLp,
						  HvLpEvent_Type_VirtualLan);

	if (! cnx->status.mCapMonAlloced) {
		cnx->mAllocTaskTq.func =
			(void *) veth_finishOpeningConnections;
		mf_allocateLpEvents(remoteLp, HvLpEvent_Type_VirtualLan,
				    sizeof (struct VethLpEvent), 2,
				    &veth_intFinishOpeningConnections,
				    cnx);
	} else {
		veth_finishOpeningConnectionsLocked(cnx);
	}

	spin_unlock_irqrestore(&cnx->ack_gate, flags);
}

static void
veth_closeConnection(u8 remoteLp)
{
	struct VethLpConnection *cnx =
	    &(mFabricMgr->mConnection[remoteLp]);
	unsigned long flags;

	del_timer_sync(&cnx->ack_timer);

	if (cnx->status.mOpen == 1) {
		HvCallEvent_closeLpEventPath(remoteLp,
					     HvLpEvent_Type_VirtualLan);
		cnx->status.mOpen = 0;
		veth_failMe(cnx);

		/* reset ack data */
		spin_lock_irqsave(&cnx->ack_gate, flags);

		memset(&cnx->mEventData, 0xff, sizeof (cnx->mEventData));
		cnx->mNumAcks = 0;

		spin_unlock_irqrestore(&cnx->ack_gate, flags);
	}

}

static void
veth_msgsInit(struct VethLpConnection *cnx)
{
	cnx->mAllocTaskTq.func = (void *) veth_finishMsgsInit;
	mf_allocateLpEvents(cnx->remote_lp,
			    HvLpEvent_Type_VirtualLan,
			    sizeof (struct VethLpEvent),
			    cnx->mMyCap.mNumberBuffers,
			    &veth_intFinishMsgsInit, cnx);
}

static void
veth_intFinishMsgsInit(void *parm, int number)
{
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;
	cnx->mAllocTaskTq.data = parm;
	cnx->mNumberRcvMsgs = number;
	schedule_work(&cnx->mAllocTaskTq);
}

static void
veth_intFinishCapTask(void *parm, int number)
{
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;
	cnx->mAllocTaskTq.data = parm;
	if (number > 0)
		cnx->mNumberLpAcksAlloced += number;
	schedule_work(&cnx->mAllocTaskTq);
}

static void
veth_finishMsgsInit(struct VethLpConnection *cnx)
{
	int i = 0;
	unsigned int numberGotten = 0;
	u64 amountOfHeapToGet =
		cnx->mMyCap.mNumberBuffers *
		sizeof (struct VethMsg);
	unsigned long flags;
	spin_lock_irqsave(&cnx->status_gate, flags);

	if (cnx->mNumberRcvMsgs >=
	    cnx->mMyCap.mNumberBuffers) {
		struct VethMsg *msgs;

		msgs = kmalloc(amountOfHeapToGet, GFP_ATOMIC);

		cnx->mMsgs = msgs;

		if (msgs) {
			memset(msgs, 0, amountOfHeapToGet);

			for (i = 0;
			     i < cnx->mMyCap.mNumberBuffers;
			     ++i) {
				msgs[i].mIndex = i;
				++numberGotten;
				VETHSTACKPUSH(&(cnx->mMsgStack),
					      (cnx->mMsgs + i));
			}
			if (numberGotten > 0) {
				cnx->mNumMsgs = numberGotten;
			}
		}
	}

	cnx->mMyCap.mNumberBuffers =
	    cnx->mNumMsgs;

	if (cnx->mNumMsgs < 10)
		cnx->mMyCap.mThreshold = 1;
	else if (cnx->mNumMsgs < 20)
		cnx->mMyCap.mThreshold = 4;
	else if (cnx->mNumMsgs < 40)
		cnx->mMyCap.mThreshold = 10;
	else
		cnx->mMyCap.mThreshold = 20;

	cnx->mMyCap.mTimer = VethAckTimeoutUsec;

	veth_finishSendCap(cnx);

	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void
veth_sendCap(struct VethLpConnection *cnx)
{
	if (cnx->mMsgs == NULL) {
		cnx->mMyCap.mNumberBuffers =
		    VethBuffersToAllocate;
		veth_msgsInit(cnx);
	} else {
		veth_finishSendCap(cnx);
	}
}

static void
veth_finishSendCap(struct VethLpConnection *cnx)
{
	HvLpEvent_Rc rc;
	u64 *rawcap = (u64 *) &cnx->mMyCap;

	rc = veth_signalevent(cnx, VethEventTypeCap,
			      HvLpEvent_AckInd_DoAck,
			      HvLpEvent_AckType_ImmediateAck,
			      0, rawcap[0], rawcap[1], rawcap[2], rawcap[3],
			      rawcap[4]);

	if ( (rc == HvLpEvent_Rc_PartitionDead)
	     || (rc == HvLpEvent_Rc_PathClosed)) {
		cnx->status.mSentCap = 0;
	} else if (rc != HvLpEvent_Rc_Good) {
		veth_error("Couldn't send cap to lpar %d, rc %x\n",
				  cnx->remote_lp, (int) rc);
		veth_failMe(cnx);
	} else {
		cnx->status.mSentCap = 1;
	}
}

static void
veth_takeCap(struct VethLpConnection *cnx, struct VethLpEvent *event)
{
	if (!test_and_set_bit(0, &(cnx->mCapTaskPending))) {
		cnx->mCapTaskTq.data = cnx;
		memcpy(&cnx->mCapEvent, event,
		       sizeof (cnx->mCapEvent));
		schedule_work(&cnx->mCapTaskTq);
	} else {
		veth_error("Received a capabilities from lpar %d while already processing one\n",
			   cnx->remote_lp);
		event->mBaseEvent.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
	}
}

static void
veth_takeCapAck(struct VethLpConnection *cnx, struct VethLpEvent *event)
{
	if (!test_and_set_bit(0, &(cnx->mCapAckTaskPending))) {
		cnx->mCapAckTaskTq.data = cnx;
		memcpy(&cnx->mCapAckEvent, event,
		       sizeof (cnx->mCapAckEvent));
		schedule_work(&cnx->mCapAckTaskTq);
	} else {
		veth_error("Received a capabilities ack from lpar %d while already processing one\n",
			   cnx->remote_lp);
	}
}

static void
veth_takeMonitorAck(struct VethLpConnection *cnx,
		    struct VethLpEvent *event)
{
	if (!test_and_set_bit(0, &(cnx->mMonitorAckTaskPending))) {
		cnx->mMonitorAckTaskTq.data = cnx;
		memcpy(&cnx->mMonitorAckEvent, event,
		       sizeof (cnx->mMonitorAckEvent));
		schedule_work(&cnx->mMonitorAckTaskTq);
	} else {
		veth_error("Received a monitor ack from lpar %d while already processing one\n",
			   cnx->remote_lp);
	}
}

static void
veth_recycleMsgByNum(struct VethLpConnection *cnx, u16 msg)
{
	if (msg < cnx->mNumMsgs) {
		struct VethMsg *myMsg = cnx->mMsgs + msg;
		veth_recycleMsg(cnx, myMsg);
	}
}

static void
veth_recycleMsg(struct VethLpConnection *cnx, struct VethMsg *myMsg)
{
	u32 dma_address, dma_length;

	if (test_and_clear_bit(0, &(myMsg->mInUse))) {
		dma_address = myMsg->mEvent.mSendData.mAddress[0];
		dma_length = myMsg->mEvent.mSendData.mLength[0];

		pci_unmap_single(iSeries_veth_dev, dma_address, dma_length,
				 PCI_DMA_TODEVICE);

		if (myMsg->skb != NULL) {
			dev_kfree_skb_any(myMsg->skb);
			myMsg->skb = NULL;
		}

		memset(&(myMsg->mEvent.mSendData), 0,
		       sizeof (struct VethFramesData));
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

static void
veth_capTask(void *parm)
{
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;
	struct VethLpEvent *event = &cnx->mCapEvent;
	unsigned long flags;
	struct VethCapData *remoteCap = &(cnx->mRemoteCap);
	u64 numAcks = 0;
	spin_lock_irqsave(&cnx->status_gate, flags);
	cnx->status.mGotCap = 1;

	memcpy(remoteCap, &(event->mDerivedData.mCapabilitiesData),
	       sizeof (cnx->mRemoteCap));

	if ((remoteCap->mNumberBuffers <= VethMaxFramesMsgs)
	    && (remoteCap->mNumberBuffers != 0)
	    && (remoteCap->mThreshold <=
		VethMaxFramesMsgsAcked)
	    && (remoteCap->mThreshold != 0)) {
		numAcks =
		    (remoteCap->mNumberBuffers /
		     remoteCap->mThreshold) + 1;

		if (cnx->mNumberLpAcksAlloced < numAcks) {
			numAcks = numAcks - cnx->mNumberLpAcksAlloced;
			cnx->mAllocTaskTq.func =
			    (void *) (void *) veth_finishCapTask;
			mf_allocateLpEvents(cnx->remote_lp,
					    HvLpEvent_Type_VirtualLan,
					    sizeof (struct VethLpEvent),
					    numAcks, &veth_intFinishCapTask,
					    cnx);
		} else
			veth_finishCapTaskLocked(cnx);
	} else {
		veth_error("Received incompatible capabilities from lpar %d\n",
			   cnx->remote_lp);
		event->mBaseEvent.xRc = HvLpEvent_Rc_InvalidSubtypeData;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
	}

	clear_bit(0, &(cnx->mCapTaskPending));
	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void
veth_capAckTask(void *parm)
{
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;
	struct VethLpEvent *event = &cnx->mCapAckEvent;
	unsigned long flags;

	spin_lock_irqsave(&cnx->status_gate, flags);

	if (event->mBaseEvent.xRc == HvLpEvent_Rc_Good) {
		cnx->status.mCapAcked = 1;

		if ((cnx->status.mGotCap == 1)
		    && (cnx->status.mGotCapAcked == 1)) {
			if (cnx->status.mSentMonitor != 1)
				veth_sendMonitor(cnx);
		}
	} else {
		veth_printk("Bad rc(%d) from lpar %d on capabilities\n",
			    event->mBaseEvent.xRc, cnx->remote_lp);
		veth_failMe(cnx);
	}

	clear_bit(0, &(cnx->mCapAckTaskPending));
	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void
veth_monitorAckTask(void *parm)
{
	struct VethLpConnection *cnx = (struct VethLpConnection *) parm;
	unsigned long flags;

	spin_lock_irqsave(&cnx->status_gate, flags);

	veth_failMe(cnx);

	veth_printk("Monitor ack returned for lpar %d\n",
		    cnx->remote_lp);

	if (cnx->status.mOpen) {
		veth_closeConnection(cnx->remote_lp);

		udelay(100);

		schedule_work(&cnx->mMonitorAckTaskTq);
	} else {
		if (VethModuleReopen) {
			veth_openConnection(cnx->remote_lp);
		} else {
			int i = 0;

			for (i = 0; i < cnx->mNumMsgs; ++i)
				veth_recycleMsgByNum(cnx, i);
		}
		clear_bit(0, &(cnx->mMonitorAckTaskPending));
	}

	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

#define number_of_pages(v, l) ((((unsigned long)(v) & ((1 << 12) - 1)) + (l) + 4096 - 1) / 4096)
#define page_offset(v) ((unsigned long)(v) & ((1 << 12) - 1))

static void
veth_takeFrames(struct VethLpConnection *cnx, struct VethLpEvent *event)
{
	int i = 0;
	struct veth_port *port = NULL;
	struct BufList {
		union {
			struct {
				u32 token2;
				u32 garbage;
			} token1;
			u64 address;
		} addr;
		u64 size;
	};
	struct BufList myBufList[4];	/* max pages per frame */
	struct BufList remoteList[VethMaxFramesPerMsg];	/* max frags per frame */
	unsigned long flags;

	memset(myBufList, 0, sizeof(myBufList));
	memset(remoteList, 0, sizeof(VethMaxFramesPerMsg));

	do {
		int nfrags = 0;
		u16 length = 0;

		/* a 0 address marks the end of the valid entries */
		if (event->mDerivedData.mSendData.mAddress[i] == 0)
			break;

		/* make sure that we have at least 1 EOF entry in the
		 * remaining entries */
		if (!(event->mDerivedData.mSendData.mEofMask >> i)) {
			veth_printk
			    ("bad lp event: missing EOF frag in event mEofMask 0x%x i %d\n",
			     event->mDerivedData.mSendData.mEofMask, i);
			break;
		}

		/* add up length of non-EOF frags */
		do {
			remoteList[nfrags].addr.token1.token2 =
			    event->mDerivedData.mSendData.mAddress[i + nfrags];
			remoteList[nfrags].addr.token1.garbage = 0;
			length += remoteList[nfrags].size =
			    event->mDerivedData.mSendData.mLength[i + nfrags];
		} while (! (event->mDerivedData.mSendData.mEofMask
			    & (1 << (i + nfrags++))));

		/* length == total length of all framgents */
		/* nfrags == # of fragments in this frame */

		if ((length - 14) <= VETH_MAX_MTU) {
			struct sk_buff *skb = alloc_skb(length, GFP_ATOMIC);

			if (skb != NULL) {
				HvLpDma_Rc rc = HvLpDma_Rc_Good;

				/* build the buffer list for the dma operation */
				int numPages = number_of_pages((skb->data), length);	/* number of pages in this fragment of the complete buffer */
				myBufList[0].addr.address =
				    (0x8000000000000000LL |
				     (virt_to_absolute
				      ((unsigned long) skb->data)));
				myBufList[0].size =
				    (numPages > 1) 
					? (4096 - page_offset(skb->data))
					: length;
				if (numPages > 1) {
					myBufList[1].addr.address =
					    (0x8000000000000000LL |
					     (virt_to_absolute
					      ((unsigned long) skb->data +
					       myBufList[0].size)));
					myBufList[1].size =
					    (numPages > 2) 
						? 4096 
						: length - myBufList[0].size;
					if (numPages > 2) {
						myBufList[2].addr.address =
						    (0x8000000000000000LL |
						     (virt_to_absolute
						      ((unsigned long) skb->
						       data +
						       myBufList[0].size +
						       myBufList[1].size)));
						myBufList[2].size =
						    (numPages >
						     3) ? 4096 : length -
						    myBufList[0].size -
						    myBufList[1].size;
						if (numPages > 3) {
							myBufList[3].addr.
							    address =
							    0x8000000000000000LL
							    |
							    (virt_to_absolute
							     ((unsigned long)
							      skb->data +
							      myBufList[0].
							      size +
							      myBufList[1].
							      size +
							      myBufList[2].
							      size));
							myBufList[3].size =
							    length -
							    myBufList[0].size -
							    myBufList[1].size -
							    myBufList[2].size;
						}
					}
				}
				rc = HvCallEvent_dmaBufList(
					HvLpEvent_Type_VirtualLan,
					event->mBaseEvent.xSourceLp,
					HvLpDma_Direction_RemoteToLocal,
					cnx->src_inst,
					cnx->dst_inst,
					HvLpDma_AddressType_RealAddress,
					HvLpDma_AddressType_TceIndex,
					0x8000000000000000LL |
					(virt_to_absolute
					 ((unsigned long) &myBufList)),
					0x8000000000000000LL |
					(virt_to_absolute
					 ((unsigned long) &remoteList)), length);

				if (rc == HvLpDma_Rc_Good) {
					HvLpVirtualLanIndex vlan = skb->data[9];
					u64 dest =
					    *((u64 *) skb->
					      data) & 0xFFFFFFFFFFFF0000;

					if (((vlan < HvMaxArchitectedVirtualLans) && ((port = mFabricMgr->mPorts[vlan]) != NULL)) && ((dest == port->mMyAddress) ||	/* it's for me */
																      (dest == 0xFFFFFFFFFFFF0000) ||	/* it's a broadcast */
																      (veth_multicast_wanted(port, dest)) ||	/* it's one of my multicasts */
																      (port->mPromiscuous == 1))) {	/* I'm promiscuous */
						skb_put(skb, length);
						skb->dev = port->mDev;
						skb->protocol =
						    eth_type_trans(skb,
								   port->mDev);
						skb->ip_summed = CHECKSUM_NONE;
						netif_rx(skb);	/* send it up */
						port->stats.rx_packets++;
						port->stats.rx_bytes += length;

					} else {
						dev_kfree_skb_irq(skb);
					}
				} else {
					dev_kfree_skb_irq(skb);
				}
			}
		} else {
			break;
		}
		i += nfrags;
	} while (i < VethMaxFramesPerMsg);

	/* Ack it */
	spin_lock_irqsave(&cnx->ack_gate, flags);
	
	if (cnx->mNumAcks < VethMaxFramesMsgsAcked) {
		cnx->mEventData.mAckData.mToken[cnx->mNumAcks] =
			event->mBaseEvent.xCorrelationToken;
		++cnx->mNumAcks;
		
		if (cnx->mNumAcks == cnx->mRemoteCap.mThreshold) {
			HvLpEvent_Rc rc;

			rc = veth_signalevent(cnx,
					      VethEventTypeFramesAck,
					      HvLpEvent_AckInd_NoAck,
					      HvLpEvent_AckType_ImmediateAck,
					      0,
					      cnx->mEventData.raw[0],
					      cnx->mEventData.raw[1],
					      cnx->mEventData.raw[2],
					      cnx->mEventData.raw[3],
					      cnx->mEventData.raw[4]);
			
			if (rc != HvLpEvent_Rc_Good) {
				veth_error("Bad lp event return code(%x) acking frames from lpar %d\n",
					   (int) rc, cnx->remote_lp);
			}
			
			cnx->mNumAcks = 0;
			
			memset(&cnx->mEventData, 0xff, sizeof(cnx->mEventData));
		}
		
	}
	
	spin_unlock_irqrestore(&cnx->ack_gate, flags);
}

#undef number_of_pages
#undef page_offset

static void
veth_timedAck(unsigned long connectionPtr)
{
	unsigned long flags;
	HvLpEvent_Rc rc;
	struct VethLpConnection *cnx =
	    (struct VethLpConnection *) connectionPtr;
	/* Ack all the events */
	spin_lock_irqsave(&cnx->ack_gate, flags);

	if (cnx->mNumAcks > 0) {
		rc = veth_signalevent(cnx, VethEventTypeFramesAck,
				      HvLpEvent_AckInd_NoAck,
				      HvLpEvent_AckType_ImmediateAck,
				      0,
				      cnx->mEventData.raw[0],
				      cnx->mEventData.raw[1],
				      cnx->mEventData.raw[2],
				      cnx->mEventData.raw[3],
				      cnx->mEventData.raw[4]);

		if (rc != HvLpEvent_Rc_Good) {
			veth_error("Bad lp event return code(%x) acking frames from lpar %d!\n",
				   (int) rc, cnx->remote_lp);
		}

		cnx->mNumAcks = 0;

		memset(&cnx->mEventData, 0xff, sizeof(cnx->mEventData));
	}

	spin_unlock_irqrestore(&cnx->ack_gate, flags);

	veth_startQueues();

	/* Reschedule the timer */
	cnx->ack_timer.expires = jiffies + cnx->mTimeout;
	add_timer(&cnx->ack_timer);
}

static int
veth_multicast_wanted(struct veth_port *port, u64 mac_addr)
{
	int wanted = 0;
	int i;
	unsigned long flags;

	if (! (((char *) &mac_addr)[0] & 0x01))
		return 0;

	read_lock_irqsave(&port->mcast_gate, flags);

	if (port->all_mcast) {
		wanted = 1;
		goto out;
	}

	for (i = 0; i < port->mNumAddrs; ++i) {
		if (port->mMcasts[i] == mac_addr) {
			wanted = 1;
			break;
		}
	}

 out:
	read_unlock_irqrestore(&port->mcast_gate, flags);

	return wanted;
}

static void
veth_sendMonitor(struct VethLpConnection *cnx)
{
	HvLpEvent_Rc rc;

	rc = veth_signalevent(cnx, VethEventTypeMonitor,
			      HvLpEvent_AckInd_DoAck,
			      HvLpEvent_AckType_DeferredAck,
			      0, 0, 0, 0, 0, 0);

	if (rc == HvLpEvent_Rc_Good) {
		cnx->status.mSentMonitor = 1;
		cnx->status.mFailed = 0;

		/* Start the ACK timer */
		init_timer(&cnx->ack_timer);
		cnx->ack_timer.function = veth_timedAck;
		cnx->ack_timer.data = (unsigned long) cnx;
		cnx->ack_timer.expires = jiffies + cnx->mTimeout;
		add_timer(&cnx->ack_timer);

	} else {
		veth_error("Monitor send to lpar %d failed with rc %x\n",
				  cnx->remote_lp, (int) rc);
		veth_failMe(cnx);
	}
}

static void
veth_finishCapTask(struct VethLpConnection *cnx)
{
	unsigned long flags;

	spin_lock_irqsave(&cnx->status_gate, flags);
	veth_finishCapTaskLocked(cnx);
	spin_unlock_irqrestore(&cnx->status_gate, flags);
}

static void
veth_finishCapTaskLocked(struct VethLpConnection *cnx)
{
	struct VethLpEvent *event = &cnx->mCapEvent;
	struct VethCapData *remoteCap = &(cnx->mRemoteCap);
	int numAcks =
	    (remoteCap->mNumberBuffers /
	     remoteCap->mThreshold) + 1;

	/* Convert timer to jiffies */
	if (cnx->mMyCap.mTimer)
		cnx->mTimeout =
		    remoteCap->mTimer * HZ / 1000000;
	else
		cnx->mTimeout = VethAckTimeoutUsec * HZ / 1000000;

	if (cnx->mNumberLpAcksAlloced >= numAcks) {
		HvLpEvent_Rc rc =
		    HvCallEvent_ackLpEvent((struct HvLpEvent *) event);

		if (rc == HvLpEvent_Rc_Good) {
			cnx->status.mGotCapAcked = 1;

			if (cnx->status.mSentCap != 1) {
				cnx->dst_inst =
				    HvCallEvent_getTargetLpInstanceId
				    (cnx->remote_lp,
				     HvLpEvent_Type_VirtualLan);

				veth_sendCap(cnx);
			} else if (cnx->status.mCapAcked == 1) {
				if (cnx->status.
				    mSentMonitor != 1)
					veth_sendMonitor(cnx);
			}
		} else {
			veth_error("Failed to ack remote cap for lpar %d with rc %x\n",
				   cnx->remote_lp, (int) rc);
			veth_failMe(cnx);
		}
	} else {
		veth_error("Couldn't allocate all the frames ack events for lpar %d\n",
			   cnx->remote_lp);
		event->mBaseEvent.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
	}
}

static int
proc_veth_dump_connection(char *page, char **start, off_t off, int count,
			  int *eof, void *data)
{
	char *out = page;
	long cnx_num = (long) data;
	int len = 0;
	struct VethLpConnection *cnx = NULL;

	if ((cnx_num < 0) || (cnx_num > HVMAXARCHITECTEDLPS)
	    || !mFabricMgr) {
		veth_error("Got bad data from /proc file system\n");
		len = sprintf(page, "ERROR\n");
	} else {
		cnx = &mFabricMgr->mConnection[cnx_num];

		out += sprintf(out, "Remote Lp:\t%d\n", cnx->remote_lp);
		out += sprintf(out, "Source Inst:\t%04X\n",
			       cnx->src_inst);
		out += sprintf(out, "Target Inst:\t%04X\n",
			       cnx->dst_inst);
		out += sprintf(out, "Num Msgs:\t%d\n", cnx->mNumMsgs);
		out += sprintf(out, "Num Lp Acks:\t%d\n",
			       cnx->mNumberLpAcksAlloced);
		out += sprintf(out, "Num Acks:\t%d\n", cnx->mNumAcks);

		out += sprintf(out, "<");

		if (cnx->status.mOpen)
			out += sprintf(out, "Open/");

		if (cnx->status.mCapMonAlloced)
			out += sprintf(out, "CapMonAlloced/");

		if (cnx->status.mBaseMsgsAlloced)
			out += sprintf(out, "BaseMsgsAlloced/");

		if (cnx->status.mSentCap)
			out += sprintf(out, "SentCap/");

		if (cnx->status.mCapAcked)
			out += sprintf(out, "CapAcked/");

		if (cnx->status.mGotCap)
			out += sprintf(out, "GotCap/");

		if (cnx->status.mGotCapAcked)
			out += sprintf(out, "GotCapAcked/");

		if (cnx->status.mSentMonitor)
			out += sprintf(out, "SentMonitor/");

		if (cnx->status.mPopulatedRings)
			out += sprintf(out, "PopulatedRings/");

		if (cnx->status.mFailed)
			out += sprintf(out, "Failed/");

		if (*(out - 1) == '<') {
			out--;
		} else {
			BUG_ON(*(out-1) != '/');
			out += sprintf(out, ">");
		}

		out += sprintf(out, "\n");

		out += sprintf(out,
			       "Capabilities (System:<Version/Buffers/Threshold/Timeout>):\n");
		out += sprintf(out, "\tLocal:<");
		out += sprintf(out, "%d/%d/%d/%d>\n", cnx->mMyCap.mVersion,
			       cnx->mMyCap.mNumberBuffers,
			       cnx->mMyCap.mThreshold, cnx->mMyCap.mTimer);
		out += sprintf(out, "\tRemote:<");
		out += sprintf(out, "%d/%d/%d/%d>\n", cnx->mRemoteCap.mVersion,
			       cnx->mRemoteCap.mNumberBuffers,
			       cnx->mRemoteCap.mThreshold, cnx->mRemoteCap.mTimer);
		len = out - page;
	}
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	*start = page + off;
	return len;
}

static int
proc_veth_dump_port(char *page, char **start, off_t off, int count, int *eof,
		    void *data)
{
	char *out = page;
	long whichPort = (long) data;
	int len = 0;
	struct veth_port *port = NULL;

	if ((whichPort < 0) || (whichPort > HvMaxArchitectedVirtualLans)
	    || (mFabricMgr == NULL)) {
		len = sprintf(page, "Virtual ethernet is not configured.\n");
	} else {
		int i, j;
		port = mFabricMgr->mPorts[whichPort];

		if (port != NULL) {
			u64 tmp;

			out += sprintf(out, "Net device:\t%p\n", port->mDev);
			out += sprintf(out, "Net device name:\t%s\n", port->mDev->name);
			out += sprintf(out, "Address:\t");
			tmp = port->mMyAddress;
			for (j = 0; j < ETH_ALEN; j++) {
				out += sprintf(out, "%02X",
					       (unsigned)(tmp >> 56));
				tmp <<= 8;
			}
			printk("\n");
			out += sprintf(out, "Promiscuous:\t%d\n", port->mPromiscuous);
			out += sprintf(out, "All multicast:\t%d\n", port->all_mcast);
			out += sprintf(out, "Number multicast:\t%d\n", port->mNumAddrs);

			for (i = 0; i < port->mNumAddrs; ++i) {
				tmp = port->mMcasts[i];
				for (j = 0; j < ETH_ALEN; j++) {
					out += sprintf(out, "%02X",
						       (unsigned)(tmp >> 56));
					tmp <<= 8;
				}
			}
		} else {
			out += sprintf(page, "veth%ld is not configured.\n",
				       whichPort);
		}

		len = out - page;
	}
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	*start = page + off;
	return len;
}
