/*
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: vlan@scry.wanfear.com
 *              VLAN Home Page: http://www.candelatech.com/~greear/vlan.html
 * 
 * Fixes:
 *              Fix for packet capture - Nick Eggleston <nick@dccinc.com>;
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/uaccess.h> /* for copy_from_user */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/datalink.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/init.h>
#include <net/p8022.h>
#include <net/arp.h>
#include <linux/rtnetlink.h>
#include <linux/brlock.h>
#include <linux/notifier.h>

#include <linux/if_vlan.h>
#include "vlan.h"
#include "vlanproc.h"

/* Global VLAN variables */

/* Our listing of VLAN group(s) */
struct vlan_group *p802_1Q_vlan_list;

static char vlan_fullname[] = "802.1Q VLAN Support";
static unsigned int vlan_version = 1;
static unsigned int vlan_release = 6;
static char vlan_copyright[] = " Ben Greear <greearb@candelatech.com>";

static int vlan_device_event(struct notifier_block *, unsigned long, void *);

struct notifier_block vlan_notifier_block = {
	notifier_call: vlan_device_event,
};

/* These may be changed at run-time through IOCTLs */

/* Determines interface naming scheme. */
unsigned short vlan_name_type = VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD;

/* Counter for how many NON-VLAN protos we've received on a VLAN. */
unsigned long vlan_bad_proto_recvd = 0;

/* DO reorder the header by default */
unsigned short vlan_default_dev_flags = 1;

static struct packet_type vlan_packet_type = {
	type: __constant_htons(ETH_P_8021Q),
	dev:  NULL,
	func: vlan_skb_recv, /* VLAN receive method */
	data: (void *)(-1),  /* Set here '(void *)1' when this code can SHARE SKBs */
	next: NULL
};

/* End of global variables definitions. */

/*
 * Function vlan_proto_init (pro)
 *
 *    Initialize VLAN protocol layer, 
 *
 */
static int __init vlan_proto_init(void)
{
	int err;

	printk(VLAN_INF "%s v%u.%u %s\n",
	       vlan_fullname, vlan_version, vlan_release, vlan_copyright);

	/* proc file system initialization */
	err = vlan_proc_init();
	if (err < 0) {
		printk(KERN_ERR __FUNCTION__
		       "%s: can't create entry in proc filesystem!\n",
		       VLAN_NAME);
		return 1;
	}

	dev_add_pack(&vlan_packet_type);

	/* Register us to receive netdevice events */
	register_netdevice_notifier(&vlan_notifier_block);

	vlan_ioctl_hook = vlan_ioctl_handler;

	printk(VLAN_INF "%s Initialization complete.\n", VLAN_NAME);
	return 0;
}

/*
 * Cleanup of groups before exit
 */

static void vlan_group_cleanup(void)
{
	struct vlan_group *grp = NULL;
	struct vlan_group *nextgroup;

	for (grp = p802_1Q_vlan_list; (grp != NULL);) {
		nextgroup = grp->next;
		kfree(grp);
		grp = nextgroup;
	}
	p802_1Q_vlan_list = NULL;
}

/*
 *     Module 'remove' entry point.
 *     o delete /proc/net/router directory and static entries.
 */ 
static void __exit vlan_cleanup_module(void)
{
	/* Un-register us from receiving netdevice events */
	unregister_netdevice_notifier(&vlan_notifier_block);

	dev_remove_pack(&vlan_packet_type);
	vlan_proc_cleanup();
	vlan_group_cleanup();
	vlan_ioctl_hook = NULL;
}

module_init(vlan_proto_init);
module_exit(vlan_cleanup_module);

/**  Will search linearly for now, based on device index.  Could
 * hash, or directly link, this some day. --Ben
 * TODO:  Potential performance issue here.  Linear search where N is
 *        the number of 'real' devices used by VLANs.
 */
struct vlan_group* vlan_find_group(int real_dev_ifindex)
{
	struct vlan_group *grp = NULL;

	br_read_lock_bh(BR_NETPROTO_LOCK);
	for (grp = p802_1Q_vlan_list;
	     ((grp != NULL) && (grp->real_dev_ifindex != real_dev_ifindex));
	     grp = grp->next) {
		/* nothing */ ;
	}
	br_read_unlock_bh(BR_NETPROTO_LOCK);

	return grp;
}

/*  Find the protocol handler.  Assumes VID < 0xFFF.
 */
struct net_device *find_802_1Q_vlan_dev(struct net_device *real_dev,
                                        unsigned short VID)
{
	struct vlan_group *grp = vlan_find_group(real_dev->ifindex);

	if (grp)
                return grp->vlan_devices[VID];

	return NULL;
}

/** This method will explicitly do a dev_put on the device if do_dev_put
 * is TRUE.  This gets around a difficulty with reference counting, and
 * the unregister-by-name (below).  If do_locks is true, it will grab
 * a lock before un-registering.  If do_locks is false, it is assumed that
 * the lock has already been grabbed externally...  --Ben
 */
int unregister_802_1Q_vlan_dev(int real_dev_ifindex, unsigned short vlan_id,
			       int do_dev_put, int do_locks)
{
	struct net_device *dev = NULL;
	struct vlan_group *grp;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": VID: %i\n", vlan_id);
#endif

	/* sanity check */
	if ((vlan_id >= 0xFFF) || (vlan_id <= 0))
		return -EINVAL;

	grp = vlan_find_group(real_dev_ifindex);
	if (grp) {
		dev = grp->vlan_devices[vlan_id];
		if (dev) {
			/* Remove proc entry */
			vlan_proc_rem_dev(dev);

			/* Take it out of our own structures */
			grp->vlan_devices[vlan_id] = NULL;

			/* Take it out of the global list of devices.
			 *  NOTE:  This deletes dev, don't access it again!!
			 */

			if (do_dev_put)
				dev_put(dev);

			/* TODO:  Please review this code. */
			if (do_locks) {
				rtnl_lock();
				unregister_netdevice(dev);
				rtnl_unlock();
			} else {
				unregister_netdevice(dev);
			}

			MOD_DEC_USE_COUNT;
		}
	}
        
        return 0;
}

int unregister_802_1Q_vlan_device(const char *vlan_IF_name)
{
	struct net_device *dev = NULL;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": unregister VLAN by name, name -:%s:-\n",
	       vlan_IF_name);
#endif

	dev = dev_get_by_name(vlan_IF_name);
	if (dev) {
		if (dev->priv_flags & IFF_802_1Q_VLAN) {
			return unregister_802_1Q_vlan_dev(
				VLAN_DEV_INFO(dev)->real_dev->ifindex,
				(unsigned short)(VLAN_DEV_INFO(dev)->vlan_id),
				1 /* do dev_put */, 1 /* do locking */);
		} else {
			printk(VLAN_ERR __FUNCTION__
			       ": ERROR:	Tried to remove a non-vlan device "
			       "with VLAN code, name: %s  priv_flags: %hX\n",
			       dev->name, dev->priv_flags);
			dev_put(dev);
			return -EPERM;
		}
	} else {
#ifdef VLAN_DEBUG
		printk(VLAN_DBG __FUNCTION__ ": WARNING: Could not find dev.\n");
#endif
		return -EINVAL;
	}
}

/*  Attach a VLAN device to a mac address (ie Ethernet Card).
 *  Returns the device that was created, or NULL if there was
 *  an error of some kind.
 */
struct net_device *register_802_1Q_vlan_device(const char* eth_IF_name,
					       unsigned short VLAN_ID)
{
	struct vlan_group *grp;
	struct net_device *new_dev;
	struct net_device *real_dev; /* the ethernet device */
	int malloc_size = 0;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": if_name -:%s:-	vid: %i\n",
	       eth_IF_name, VLAN_ID);
#endif

	if (VLAN_ID >= 0xfff)
		goto out_ret_null;

	/* find the device relating to eth_IF_name. */
	real_dev = dev_get_by_name(eth_IF_name);
	if (!real_dev)
		goto out_ret_null;

	/* TODO:  Make sure this device can really handle having a VLAN attached
	 * to it...
	 */
	if (find_802_1Q_vlan_dev(real_dev, VLAN_ID)) {
		/* was already registered. */
		printk(VLAN_DBG __FUNCTION__ ": ALREADY had VLAN registered\n");
		dev_put(real_dev);
		return NULL;
	}

	malloc_size = (sizeof(struct net_device));
	new_dev = (struct net_device *) kmalloc(malloc_size, GFP_KERNEL);
	VLAN_MEM_DBG("net_device malloc, addr: %p  size: %i\n",
		     new_dev, malloc_size);

	if (new_dev == NULL)
		goto out_put_dev;

	memset(new_dev, 0, malloc_size);

	/* set us up to not use a Qdisc, as the underlying Hardware device
	 * can do all the queueing we could want.
	 */
	/* new_dev->qdisc_sleeping = &noqueue_qdisc;   Not needed it seems. */
	new_dev->tx_queue_len = 0; /* This should effectively give us no queue. */

	/* Gotta set up the fields for the device. */
#ifdef VLAN_DEBUG
	printk(VLAN_DBG "About to allocate name, vlan_name_type: %i\n",
	       vlan_name_type);
#endif
	switch (vlan_name_type) {
	case VLAN_NAME_TYPE_RAW_PLUS_VID:
		/* name will look like:	 eth1.0005 */
		sprintf(new_dev->name, "%s.%.4i", real_dev->name, VLAN_ID);
		break;
	case VLAN_NAME_TYPE_PLUS_VID_NO_PAD:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 vlan5
		 */
		sprintf(new_dev->name, "vlan%i", VLAN_ID);
		break;
	case VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 eth0.5
		 */
		sprintf(new_dev->name, "%s.%i", real_dev->name, VLAN_ID);
		break;
	case VLAN_NAME_TYPE_PLUS_VID:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 vlan0005
		 */
	default:
		sprintf(new_dev->name, "vlan%.4i", VLAN_ID);
	};
		    
#ifdef VLAN_DEBUG
	printk(VLAN_DBG "Allocated new name -:%s:-\n", new_dev->name);
#endif
	/* set up method calls */
	new_dev->init = vlan_dev_init;
	new_dev->destructor = vlan_dev_destruct;
	new_dev->features |= NETIF_F_DYNALLOC ; 
	    
	/* new_dev->ifindex = 0;  it will be set when added to
	 * the global list.
	 * iflink is set as well.
	 */
	new_dev->get_stats = vlan_dev_get_stats;
	    
	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	new_dev->flags = real_dev->flags;
	new_dev->flags &= ~IFF_UP;

	/* Make this thing known as a VLAN device */
	new_dev->priv_flags |= IFF_802_1Q_VLAN;
				
	/* need 4 bytes for extra VLAN header info,
	 * hope the underlying device can handle it.
	 */
	new_dev->mtu = real_dev->mtu;
	new_dev->change_mtu = vlan_dev_change_mtu;

	/* TODO: maybe just assign it to be ETHERNET? */
	new_dev->type = real_dev->type;

	/* Regular ethernet + 4 bytes (18 total). */
	new_dev->hard_header_len = VLAN_HLEN + real_dev->hard_header_len;

	new_dev->priv = kmalloc(sizeof(struct vlan_dev_info),
				GFP_KERNEL);
	VLAN_MEM_DBG("new_dev->priv malloc, addr: %p  size: %i\n",
		     new_dev->priv,
		     sizeof(struct vlan_dev_info));
	    
	if (new_dev->priv == NULL) {
		kfree(new_dev);
		goto out_put_dev;
	}

	memset(new_dev->priv, 0, sizeof(struct vlan_dev_info));

	memcpy(new_dev->broadcast, real_dev->broadcast, real_dev->addr_len);
	memcpy(new_dev->dev_addr, real_dev->dev_addr, real_dev->addr_len);
	new_dev->addr_len = real_dev->addr_len;

	new_dev->open = vlan_dev_open;
	new_dev->stop = vlan_dev_stop;
	new_dev->hard_header = vlan_dev_hard_header;

	new_dev->hard_start_xmit = vlan_dev_hard_start_xmit;
	new_dev->rebuild_header = vlan_dev_rebuild_header;
	new_dev->hard_header_parse = real_dev->hard_header_parse;
	new_dev->set_mac_address = vlan_dev_set_mac_address;
	new_dev->set_multicast_list = vlan_dev_set_multicast_list;

	VLAN_DEV_INFO(new_dev)->vlan_id = VLAN_ID; /* 1 through 0xFFF */
	VLAN_DEV_INFO(new_dev)->real_dev = real_dev;
	VLAN_DEV_INFO(new_dev)->dent = NULL;
	VLAN_DEV_INFO(new_dev)->flags = vlan_default_dev_flags;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "About to go find the group for idx: %i\n",
	       real_dev->ifindex);
#endif
	    
	/* So, got the sucker initialized, now lets place
	 * it into our local structure.
	 */
	grp = vlan_find_group(real_dev->ifindex);
	if (!grp) { /* need to add a new group */
		grp = kmalloc(sizeof(struct vlan_group), GFP_KERNEL);
		VLAN_MEM_DBG("grp malloc, addr: %p  size: %i\n",
			     grp, sizeof(struct vlan_group));
		if (!grp) {
			kfree(new_dev->priv);
			VLAN_FMEM_DBG("new_dev->priv free, addr: %p\n",
				      new_dev->priv);
			kfree(new_dev);
			VLAN_FMEM_DBG("new_dev free, addr: %p\n", new_dev);

			goto out_put_dev;
		}
					
		printk(KERN_ALERT "VLAN REGISTER:  Allocated new group.\n");
		memset(grp, 0, sizeof(struct vlan_group));
		grp->real_dev_ifindex = real_dev->ifindex;

		br_write_lock_bh(BR_NETPROTO_LOCK);
		grp->next = p802_1Q_vlan_list;
		p802_1Q_vlan_list = grp;
		br_write_unlock_bh(BR_NETPROTO_LOCK);
	}
	    
	grp->vlan_devices[VLAN_ID] = new_dev;
	vlan_proc_add_dev(new_dev); /* create it's proc entry */

	/* TODO: Please check this: RTNL   --Ben */
	rtnl_lock();
	register_netdevice(new_dev);
	rtnl_unlock();
	    
	/* NOTE:  We have a reference to the real device,
	 * so hold on to the reference.
	 */
	MOD_INC_USE_COUNT; /* Add was a success!! */
#ifdef VLAN_DEBUG
	printk(VLAN_DBG "Allocated new device successfully, returning.\n");
#endif
	return new_dev;

out_put_dev:
	dev_put(real_dev);

out_ret_null:
	return NULL;
}

static int vlan_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct net_device *dev = (struct net_device *)(ptr);
	struct vlan_group *grp = NULL;
	int i = 0;
	struct net_device *vlandev = NULL;

	switch (event) {
	case NETDEV_CHANGEADDR:
		/* Ignore for now */
		break;

	case NETDEV_GOING_DOWN:
		/* Ignore for now */
		break;

	case NETDEV_DOWN:
		/* TODO:  Please review this code. */
		/* put all related VLANs in the down state too. */
		for (grp = p802_1Q_vlan_list; grp != NULL; grp = grp->next) {
			int flgs = 0;

			for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
				vlandev = grp->vlan_devices[i];
				if (!vlandev ||
				    (VLAN_DEV_INFO(vlandev)->real_dev != dev) ||
				    (!(vlandev->flags & IFF_UP)))
					continue;

				flgs = vlandev->flags;
				flgs &= ~IFF_UP;
				dev_change_flags(vlandev, flgs);
			}
		}
		break;

	case NETDEV_UP:
		/* TODO:  Please review this code. */
		/* put all related VLANs in the down state too. */
		for (grp = p802_1Q_vlan_list; grp != NULL; grp = grp->next) {
			int flgs;

			for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
				vlandev = grp->vlan_devices[i];
				if (!vlandev ||
				    (VLAN_DEV_INFO(vlandev)->real_dev != dev) ||
				    (vlandev->flags & IFF_UP))
					continue;
				
				flgs = vlandev->flags;
				flgs |= IFF_UP;
				dev_change_flags(vlandev, flgs);
			}
		}
		break;
		
	case NETDEV_UNREGISTER:
		/* TODO:  Please review this code. */
		/* delete all related VLANs. */
		for (grp = p802_1Q_vlan_list; grp != NULL; grp = grp->next) {
			for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
				vlandev = grp->vlan_devices[i];
				if (!vlandev ||
				    (VLAN_DEV_INFO(vlandev)->real_dev != dev))
					continue;

				unregister_802_1Q_vlan_dev(
					VLAN_DEV_INFO(vlandev)->real_dev->ifindex,
					VLAN_DEV_INFO(vlandev)->vlan_id,
					0, 0);
				vlandev = NULL;
			}
		}
		break;
	};

	return NOTIFY_DONE;
}

/*
 *	VLAN IOCTL handler.
 *	o execute requested action or pass command to the device driver
 *   arg is really a void* to a vlan_ioctl_args structure.
 */
int vlan_ioctl_handler(unsigned long arg)
{
	int err = 0;
	struct vlan_ioctl_args args;

	/* everything here needs root permissions, except aguably the
	 * hack ioctls for sending packets.  However, I know _I_ don't
	 * want users running that on my network! --BLG
	 */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&args, (void*)arg,
                           sizeof(struct vlan_ioctl_args)))
		return -EFAULT;

	/* Null terminate this sucker, just in case. */
	args.device1[23] = 0;
	args.u.device2[23] = 0;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": args.cmd: %x\n", args.cmd);
#endif

	switch (args.cmd) {
	case SET_VLAN_INGRESS_PRIORITY_CMD:
		err = vlan_dev_set_ingress_priority(args.device1,
						    args.u.skb_priority,
						    args.vlan_qos);
		break;

	case SET_VLAN_EGRESS_PRIORITY_CMD:
		err = vlan_dev_set_egress_priority(args.device1,
						   args.u.skb_priority,
						   args.vlan_qos);
		break;

	case SET_VLAN_FLAG_CMD:
		err = vlan_dev_set_vlan_flag(args.device1,
					     args.u.flag,
					     args.vlan_qos);
		break;

	case SET_VLAN_NAME_TYPE_CMD:
		if ((args.u.name_type >= 0) &&
		    (args.u.name_type < VLAN_NAME_TYPE_HIGHEST)) {
			vlan_name_type = args.u.name_type;
			err = 0;
		} else {
			err = -EINVAL;
		}
		break;

		/* TODO:  Figure out how to pass info back...
		   case GET_VLAN_INGRESS_PRIORITY_IOCTL:
		   err = vlan_dev_get_ingress_priority(args);
		   break;

		   case GET_VLAN_EGRESS_PRIORITY_IOCTL:
		   err = vlan_dev_get_egress_priority(args);
		   break;
		*/

	case ADD_VLAN_CMD:
		/* we have been given the name of the Ethernet Device we want to
		 * talk to:  args.dev1	 We also have the
		 * VLAN ID:  args.u.VID
		 */
		if (register_802_1Q_vlan_device(args.device1, args.u.VID)) {
			err = 0;
		} else {
			err = -EINVAL;
		}
		break;

	case DEL_VLAN_CMD:
		/* Here, the args.dev1 is the actual VLAN we want
		 * to get rid of.
		 */
		err = unregister_802_1Q_vlan_device(args.device1);
		break;

	default:
		/* pass on to underlying device instead?? */
		printk(VLAN_DBG __FUNCTION__ ": Unknown VLAN CMD: %x \n",
		       args.cmd);
		return -EINVAL;
	};

	return err;
}


