/* net_init.c: Initialization for network devices. */
/*
	Written 1993,1994,1995 by Donald Becker.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	This file contains the initialization for the "pl14+" style ethernet
	drivers.  It should eventually replace most of drivers/net/Space.c.
	It's primary advantage is that it's able to allocate low-memory buffers.
	A secondary advantage is that the dangerous NE*000 netcards can reserve
	their I/O port region before the SCSI probes start.

	Modifications/additions by Bjorn Ekwall <bj0rn@blox.se>:
		ethdev_index[MAX_ETH_CARDS]
		register_netdev() / unregister_netdev()
		
	Modifications by Wolfgang Walter
		Use dev_close cleanly so we always shut things down tidily.
		
	Changed 29/10/95, Alan Cox to pass sockaddr's around for mac addresses.
	
	14/06/96 - Paul Gortmaker:	Add generic eth_change_mtu() function. 
	24/09/96 - Paul Norton: Add token-ring variants of the netdev functions. 
	
	08/11/99 - Alan Cox: Got fed up of the mess in this file and cleaned it
			up. We now share common code and have regularised name
			allocation setups. Abolished the 16 card limits.
	03/19/2000 - jgarzik and Urban Widmark: init_etherdev 32-byte align
	03/21/2001 - jgarzik: alloc_etherdev and friends

*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/if_ether.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/fddidevice.h>
#include <linux/hippidevice.h>
#include <linux/trdevice.h>
#include <linux/fcdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ltalk.h>
#include <linux/rtnetlink.h>
#include <net/neighbour.h>

/* The network devices currently exist only in the socket namespace, so these
   entries are unused.  The only ones that make sense are
    open	start the ethercard
    close	stop  the ethercard
    ioctl	To get statistics, perhaps set the interface port (AUI, BNC, etc.)
   One can also imagine getting raw packets using
    read & write
   but this is probably better handled by a raw packet socket.

   Given that almost all of these functions are handled in the current
   socket-based scheme, putting ethercard devices in /dev/ seems pointless.
   
   [Removed all support for /dev network devices. When someone adds
    streams then by magic we get them, but otherwise they are un-needed
	and a space waste]
*/


struct net_device *alloc_netdev(int sizeof_priv, const char *mask,
				       void (*setup)(struct net_device *))
{
	void *p;
	struct net_device *dev;
	int alloc_size;

	/* ensure 32-byte alignment of both the device and private area */

	alloc_size = (sizeof(struct net_device) + NETDEV_ALIGN_CONST)
			& ~NETDEV_ALIGN_CONST;
	alloc_size += sizeof_priv + NETDEV_ALIGN_CONST;

	p = kmalloc (alloc_size, GFP_KERNEL);
	if (!p) {
		printk(KERN_ERR "alloc_dev: Unable to allocate device.\n");
		return NULL;
	}

	memset(p, 0, alloc_size);

	dev = (struct net_device *)(((long)p + NETDEV_ALIGN_CONST)
				& ~NETDEV_ALIGN_CONST);
	dev->padded = (char *)dev - (char *)p;

	if (sizeof_priv)
		dev->priv = netdev_priv(dev);

	setup(dev);
	strcpy(dev->name, mask);

	return dev;
}
EXPORT_SYMBOL(alloc_netdev);

int register_netdev(struct net_device *dev)
{
	int err;

	rtnl_lock();

	/*
	 *	If the name is a format string the caller wants us to
	 *	do a name allocation
	 */
	 
	if (strchr(dev->name, '%'))
	{
		err = dev_alloc_name(dev, dev->name);
		if (err < 0)
			goto out;
	}
	
	/*
	 *	Back compatibility hook. Kill this one in 2.5
	 */
	
	if (dev->name[0]==0 || dev->name[0]==' ')
	{
		err = dev_alloc_name(dev, "eth%d");
		if (err < 0)
			goto out;
	}

	err = register_netdevice(dev);

out:
	rtnl_unlock();
	return err;
}

void unregister_netdev(struct net_device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();
}

EXPORT_SYMBOL(register_netdev);
EXPORT_SYMBOL(unregister_netdev);
