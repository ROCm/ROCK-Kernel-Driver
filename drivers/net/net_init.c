/* net_init.c: Initialization for network devices. */
/*
	Written 1993,1994,1995 by Donald Becker.

	The author may be reached as becker@cesdis.gsfc.nasa.gov or
	C/O Center of Excellence in Space Data and Information Sciences
		Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

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

*/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/malloc.h>
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


static struct net_device *init_alloc_dev(int sizeof_priv)
{
	struct net_device *dev;
	int alloc_size;

	/* ensure 32-byte alignment of the private area */
	alloc_size = sizeof (*dev) + sizeof_priv + 31;

	dev = (struct net_device *) kmalloc (alloc_size, GFP_KERNEL);
	if (dev == NULL)
	{
		printk(KERN_ERR "alloc_dev: Unable to allocate device memory.\n");
		return NULL;
	}

	memset(dev, 0, alloc_size);

	if (sizeof_priv)
		dev->priv = (void *) (((long)(dev + 1) + 31) & ~31);

	return dev;
}

/* 
 *	Create and name a device from a prototype, then perform any needed
 *	setup.
 */

static struct net_device *init_netdev(struct net_device *dev, int sizeof_priv,
				      char *mask, void (*setup)(struct net_device *))
{
	int new_device = 0;

	/*
	 *	Allocate a device if one is not provided.
	 */
	 
	if (dev == NULL) {
		dev=init_alloc_dev(sizeof_priv);
		if(dev==NULL)
			return NULL;
		new_device = 1;
	}

	/*
	 *	Allocate a name
	 */
	 
	if (dev->name[0] == '\0' || dev->name[0] == ' ') {
		strcpy(dev->name, mask);
		if (dev_alloc_name(dev, mask)<0) {
			if (new_device)
				kfree(dev);
			return NULL;
		}
	}

	netdev_boot_setup_check(dev);
	
	/*
	 *	Configure via the caller provided setup function then
	 *	register if needed.
	 */
	
	setup(dev);
	
	if (new_device) {
		rtnl_lock();
		register_netdevice(dev);
		rtnl_unlock();
	}
	return dev;
}

/**
 * init_etherdev - Register ethernet device
 * @dev: An ethernet device structure to be filled in, or %NULL if a new
 *	struct should be allocated.
 * @sizeof_priv: Size of additional driver-private structure to be allocated
 *	for this ethernet device
 *
 * Fill in the fields of the device structure with ethernet-generic values.
 *
 * If no device structure is passed, a new one is constructed, complete with
 * a private data area of size @sizeof_priv.  A 32-byte (not bit)
 * alignment is enforced for this private data area.
 *
 * If an empty string area is passed as dev->name, or a new structure is made,
 * a new name string is constructed.
 */

struct net_device *init_etherdev(struct net_device *dev, int sizeof_priv)
{
	return init_netdev(dev, sizeof_priv, "eth%d", ether_setup);
}


static int eth_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr=p;
	if (netif_running(dev))
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);
	return 0;
}

static int eth_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > 1500))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

#ifdef CONFIG_FDDI

struct net_device *init_fddidev(struct net_device *dev, int sizeof_priv)
{
	return init_netdev(dev, sizeof_priv, "fddi%d", fddi_setup);
}

static int fddi_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < FDDI_K_SNAP_HLEN) || (new_mtu > FDDI_K_SNAP_DLEN))
		return(-EINVAL);
	dev->mtu = new_mtu;
	return(0);
}

#endif /* CONFIG_FDDI */

#ifdef CONFIG_HIPPI

static int hippi_change_mtu(struct net_device *dev, int new_mtu)
{
	/*
	 * HIPPI's got these nice large MTUs.
	 */
	if ((new_mtu < 68) || (new_mtu > 65280))
		return -EINVAL;
	dev->mtu = new_mtu;
	return(0);
}


/*
 * For HIPPI we will actually use the lower 4 bytes of the hardware
 * address as the I-FIELD rather than the actual hardware address.
 */
static int hippi_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	if (netif_running(dev))
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}


struct net_device *init_hippi_dev(struct net_device *dev, int sizeof_priv)
{
	return init_netdev(dev, sizeof_priv, "hip%d", hippi_setup);
}


void unregister_hipdev(struct net_device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();
}


static int hippi_neigh_setup_dev(struct net_device *dev, struct neigh_parms *p)
{
	/* Never send broadcast/multicast ARP messages */
	p->mcast_probes = 0;
 
	/* In IPv6 unicast probes are valid even on NBMA,
	* because they are encapsulated in normal IPv6 protocol.
	* Should be a generic flag. 
	*/
	if (p->tbl->family != AF_INET6)
		p->ucast_probes = 0;
	return 0;
}

#endif /* CONFIG_HIPPI */

void ether_setup(struct net_device *dev)
{
	/* Fill in the fields of the device structure with ethernet-generic values.
	   This should be in a common file instead of per-driver.  */
	
	dev->change_mtu		= eth_change_mtu;
	dev->hard_header	= eth_header;
	dev->rebuild_header 	= eth_rebuild_header;
	dev->set_mac_address 	= eth_mac_addr;
	dev->hard_header_cache	= eth_header_cache;
	dev->header_cache_update= eth_header_cache_update;
	dev->hard_header_parse	= eth_header_parse;

	dev->type		= ARPHRD_ETHER;
	dev->hard_header_len 	= ETH_HLEN;
	dev->mtu		= 1500; /* eth_mtu */
	dev->addr_len		= ETH_ALEN;
	dev->tx_queue_len	= 100;	/* Ethernet wants good queues */	
	
	memset(dev->broadcast,0xFF, ETH_ALEN);

	/* New-style flags. */
	dev->flags		= IFF_BROADCAST|IFF_MULTICAST;

	dev_init_buffers(dev);
}

#ifdef CONFIG_FDDI

void fddi_setup(struct net_device *dev)
{
	/*
	 * Fill in the fields of the device structure with FDDI-generic values.
	 * This should be in a common file instead of per-driver.
	 */
	
	dev->change_mtu			= fddi_change_mtu;
	dev->hard_header		= fddi_header;
	dev->rebuild_header		= fddi_rebuild_header;

	dev->type				= ARPHRD_FDDI;
	dev->hard_header_len	= FDDI_K_SNAP_HLEN+3;	/* Assume 802.2 SNAP hdr len + 3 pad bytes */
	dev->mtu				= FDDI_K_SNAP_DLEN;		/* Assume max payload of 802.2 SNAP frame */
	dev->addr_len			= FDDI_K_ALEN;
	dev->tx_queue_len		= 100;	/* Long queues on FDDI */
	
	memset(dev->broadcast, 0xFF, FDDI_K_ALEN);

	/* New-style flags */
	dev->flags		= IFF_BROADCAST | IFF_MULTICAST;

	dev_init_buffers(dev);
	
	return;
}

#endif /* CONFIG_FDDI */

#ifdef CONFIG_HIPPI
void hippi_setup(struct net_device *dev)
{
	dev->set_multicast_list	= NULL;
	dev->change_mtu			= hippi_change_mtu;
	dev->hard_header		= hippi_header;
	dev->rebuild_header 		= hippi_rebuild_header;
	dev->set_mac_address 		= hippi_mac_addr;
	dev->hard_header_parse		= NULL;
	dev->hard_header_cache		= NULL;
	dev->header_cache_update	= NULL;
	dev->neigh_setup 		= hippi_neigh_setup_dev; 

	/*
	 * We don't support HIPPI `ARP' for the time being, and probably
	 * never will unless someone else implements it. However we
	 * still need a fake ARPHRD to make ifconfig and friends play ball.
	 */
	dev->type		= ARPHRD_HIPPI;
	dev->hard_header_len 	= HIPPI_HLEN;
	dev->mtu		= 65280;
	dev->addr_len		= HIPPI_ALEN;
	dev->tx_queue_len	= 25 /* 5 */;
	memset(dev->broadcast, 0xFF, HIPPI_ALEN);


	/*
	 * HIPPI doesn't support broadcast+multicast and we only use
	 * static ARP tables. ARP is disabled by hippi_neigh_setup_dev. 
	 */
	dev->flags = 0; 

	dev_init_buffers(dev);
}
#endif /* CONFIG_HIPPI */

#if defined(CONFIG_ATALK) || defined(CONFIG_ATALK_MODULE)

static int ltalk_change_mtu(struct net_device *dev, int mtu)
{
	return -EINVAL;
}

static int ltalk_mac_addr(struct net_device *dev, void *addr)
{	
	return -EINVAL;
}


void ltalk_setup(struct net_device *dev)
{
	/* Fill in the fields of the device structure with localtalk-generic values. */
	
	dev->change_mtu		= ltalk_change_mtu;
	dev->hard_header	= NULL;
	dev->rebuild_header 	= NULL;
	dev->set_mac_address 	= ltalk_mac_addr;
	dev->hard_header_cache	= NULL;
	dev->header_cache_update= NULL;

	dev->type		= ARPHRD_LOCALTLK;
	dev->hard_header_len 	= LTALK_HLEN;
	dev->mtu		= LTALK_MTU;
	dev->addr_len		= LTALK_ALEN;
	dev->tx_queue_len	= 10;	
	
	dev->broadcast[0]	= 0xFF;

	dev->flags		= IFF_BROADCAST|IFF_MULTICAST|IFF_NOARP;

	dev_init_buffers(dev);
}

#endif /* CONFIG_ATALK || CONFIG_ATALK_MODULE */

int ether_config(struct net_device *dev, struct ifmap *map)
{
	if (map->mem_start != (u_long)(-1))
		dev->mem_start = map->mem_start;
	if (map->mem_end != (u_long)(-1))
		dev->mem_end = map->mem_end;
	if (map->base_addr != (u_short)(-1))
		dev->base_addr = map->base_addr;
	if (map->irq != (u_char)(-1))
		dev->irq = map->irq;
	if (map->dma != (u_char)(-1))
		dev->dma = map->dma;
	if (map->port != (u_char)(-1))
		dev->if_port = map->port;
	return 0;
}

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
		err = -EBUSY;
		if(dev_alloc_name(dev, dev->name)<0)
			goto out;
	}
	
	/*
	 *	Back compatibility hook. Kill this one in 2.5
	 */
	
	if (dev->name[0]==0 || dev->name[0]==' ')
	{
		err = -EBUSY;
		if(dev_alloc_name(dev, "eth%d")<0)
			goto out;
	}
		
		
	err = -EIO;
	if (register_netdevice(dev))
		goto out;

	err = 0;

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


#ifdef CONFIG_TR

static void tr_configure(struct net_device *dev)
{
	/*
	 *	Configure and register
	 */
	
	dev->hard_header	= tr_header;
	dev->rebuild_header	= tr_rebuild_header;

	dev->type		= ARPHRD_IEEE802_TR;
	dev->hard_header_len	= TR_HLEN;
	dev->mtu		= 2000;
	dev->addr_len		= TR_ALEN;
	dev->tx_queue_len	= 100;	/* Long queues on tr */
	
	memset(dev->broadcast,0xFF, TR_ALEN);

	/* New-style flags. */
	dev->flags		= IFF_BROADCAST | IFF_MULTICAST ;
}

struct net_device *init_trdev(struct net_device *dev, int sizeof_priv)
{
	return init_netdev(dev, sizeof_priv, "tr%d", tr_configure);
}

void tr_setup(struct net_device *dev)
{
}

int register_trdev(struct net_device *dev)
{
	dev_init_buffers(dev);
	
	if (dev->init && dev->init(dev) != 0) {
		unregister_trdev(dev);
		return -EIO;
	}
	return 0;
}

void unregister_trdev(struct net_device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();
}
#endif /* CONFIG_TR */


#ifdef CONFIG_NET_FC

void fc_setup(struct net_device *dev)
{
	dev->hard_header        =        fc_header;
        dev->rebuild_header  	=        fc_rebuild_header;
                
        dev->type               =        ARPHRD_IEEE802;
	dev->hard_header_len    =        FC_HLEN;
        dev->mtu                =        2024;
        dev->addr_len           =        FC_ALEN;
        dev->tx_queue_len       =        100; /* Long queues on fc */

        memset(dev->broadcast,0xFF, FC_ALEN);

        /* New-style flags. */
        dev->flags              =        IFF_BROADCAST;
	dev_init_buffers(dev);
        return;
}


struct net_device *init_fcdev(struct net_device *dev, int sizeof_priv)
{
	return init_netdev(dev, sizeof_priv, "fc%d", fc_setup);
}

int register_fcdev(struct net_device *dev)
{
        dev_init_buffers(dev);
        if (dev->init && dev->init(dev) != 0) {
                unregister_fcdev(dev);
                return -EIO;
        }
        return 0;
}                                               
        
void unregister_fcdev(struct net_device *dev)
{
        rtnl_lock();
	unregister_netdevice(dev);
        rtnl_unlock();
}

#endif /* CONFIG_NET_FC */

