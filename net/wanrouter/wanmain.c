/*****************************************************************************
* wanmain.c	WAN Multiprotocol Router Module. Main code.
*
*		This module is completely hardware-independent and provides
*		the following common services for the WAN Link Drivers:
*		 o WAN device managenment (registering, unregistering)
*		 o Network interface management
*		 o Physical connection management (dial-up, incomming calls)
*		 o Logical connection management (switched virtual circuits)
*		 o Protocol encapsulation/decapsulation
*
* Author:	Gideon Hack	
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Oct 01, 1999  Gideon Hack     Update for s514 PCI card
* Dec 27, 1996	Gene Kozin	Initial version (based on Sangoma's WANPIPE)
* Jan 16, 1997	Gene Kozin	router_devlist made public
* Jan 31, 1997  Alan Cox	Hacked it about a bit for 2.1
* Jun 27, 1997  Alan Cox	realigned with vendor code
* Oct 15, 1997  Farhan Thawar   changed wan_encapsulate to add a pad byte of 0
* Apr 20, 1998	Alan Cox	Fixed 2.1 symbols
* May 17, 1998  K. Baranowski	Fixed SNAP encapsulation in wan_encapsulate
* Dec 15, 1998  Arnaldo Melo    support for firmwares of up to 128000 bytes
*                               check wandev->setup return value
* Dec 22, 1998  Arnaldo Melo    vmalloc/vfree used in device_setup to allocate
*                               kernel memory and copy configuration data to
*                               kernel space (for big firmwares)
* May 19, 1999  Arnaldo Melo    __init in wanrouter_init
* Jun 02, 1999  Gideon Hack	Updates for Linux 2.0.X and 2.2.X kernels.	
*****************************************************************************/

#include <linux/version.h>
#include <linux/config.h>
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/kernel.h>
#include <linux/module.h>	/* support for loadable modules */
#include <linux/malloc.h>	/* kmalloc(), kfree() */
#include <linux/mm.h>		/* verify_area(), etc. */
#include <linux/string.h>	/* inline mem*, str* functions */
#include <linux/vmalloc.h>	/* vmalloc, vfree */
#include <asm/segment.h>	/* kernel <-> user copy */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>	/* copy_to/from_user */
#include <linux/wanrouter.h>	/* WAN router API definitions */
#include <linux/init.h>		/* __init et al. */



/*
 * 	Defines and Macros 
 */

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

/*
 * 	Function Prototypes 
 */

/* 
 * 	Kernel loadable module interface.
 */
#ifdef MODULE
int init_module (void);
void cleanup_module (void);
#endif

/* 
 *	WAN device IOCTL handlers 
 */

static int device_setup(wan_device_t *wandev, wandev_conf_t *u_conf);
static int device_stat(wan_device_t *wandev, wandev_stat_t *u_stat);
static int device_shutdown(wan_device_t *wandev);
static int device_new_if(wan_device_t *wandev, wanif_conf_t *u_conf);
static int device_del_if(wan_device_t *wandev, char *u_name);
 
/* 
 *	Miscellaneous 
 */

static wan_device_t *find_device (char *name);
static int delete_interface (wan_device_t *wandev, char *name, int force);

/*
 *	Global Data
 */

static char fullname[]		= "WAN Router";
static char copyright[]		= "(c) 1995-1999 Sangoma Technologies Inc.";
static char modname[]		= ROUTER_NAME;	/* short module name */
wan_device_t* router_devlist 	= NULL;	/* list of registered devices */
static int devcnt 		= 0;

/* 
 *	Organize Unique Identifiers for encapsulation/decapsulation 
 */

static unsigned char oui_ether[] = { 0x00, 0x00, 0x00 };
#if 0
static unsigned char oui_802_2[] = { 0x00, 0x80, 0xC2 };
#endif

#ifndef MODULE
int __init wanrouter_init(void)
{
	int err;
	extern int wanpipe_init(void),
		   cyclomx_init(void);

	printk(KERN_INFO "%s v%u.%u %s\n",
		fullname, ROUTER_VERSION, ROUTER_RELEASE, copyright);
	err = wanrouter_proc_init();
	if (err)
		printk(KERN_ERR "%s: can't create entry in proc filesystem!\n",	modname);

	/*
	 *	Initialise compiled in boards
	 */		
	 
#ifdef CONFIG_VENDOR_SANGOMA
	wanpipe_init();
#endif	
#ifdef CONFIG_CYCLADES_SYNC
	cyclomx_init();
#endif
	return err;
}

#else

/*
 *	Kernel Loadable Module Entry Points
 */

/*
 * 	Module 'insert' entry point.
 * 	o print announcement
 * 	o initialize static data
 * 	o create /proc/net/router directory and static entries
 *
 * 	Return:	0	Ok
 *		< 0	error.
 * 	Context:	process
 */

int init_module	(void)
{
	int err;

	printk(KERN_INFO "%s v%u.%u %s\n",
		fullname, ROUTER_VERSION, ROUTER_RELEASE, copyright);
	err = wanrouter_proc_init();
	if (err) printk(KERN_ERR
		"%s: can't create entry in proc filesystem!\n", modname);
	return err;
}

/*
 * 	Module 'remove' entry point.
 * 	o delete /proc/net/router directory and static entries.
 */

void cleanup_module (void)
{
	wanrouter_proc_cleanup();
}

#endif

/*
 * 	Kernel APIs
 */

/*
 * 	Register WAN device.
 * 	o verify device credentials
 * 	o create an entry for the device in the /proc/net/router directory
 * 	o initialize internally maintained fields of the wan_device structure
 * 	o link device data space to a singly-linked list
 * 	o if it's the first device, then start kernel 'thread'
 * 	o increment module use count
 *
 * 	Return:
 *	0	Ok
 *	< 0	error.
 *
 * 	Context:	process
 */


int register_wan_device(wan_device_t *wandev)
{
	int err, namelen;

	if ((wandev == NULL) || (wandev->magic != ROUTER_MAGIC) ||
	    (wandev->name == NULL))
		return -EINVAL;

	namelen = strlen(wandev->name);
	if (!namelen || (namelen > WAN_DRVNAME_SZ))
		return -EINVAL;
		
	if (find_device(wandev->name) != NULL)
		return -EEXIST;

#ifdef WANDEBUG		
	printk(KERN_INFO "%s: registering WAN device %s\n",
		modname, wandev->name);
#endif
	/*
	 *	Register /proc directory entry 
	 */
	err = wanrouter_proc_add(wandev);
	if (err) {
		printk(KERN_ERR
			"%s: can't create /proc/net/router/%s entry!\n",
			modname, wandev->name);
		return err;
	}

	/*
	 *	Initialize fields of the wan_device structure maintained by the
	 *	router and update local data.
	 */
	 
	wandev->ndev = 0;
	wandev->dev  = NULL;
	wandev->next = router_devlist;
	router_devlist = wandev;
	++devcnt;
        MOD_INC_USE_COUNT;	/* prevent module from unloading */
	return 0;
}

/*
 *	Unregister WAN device.
 *	o shut down device
 *	o unlink device data space from the linked list
 *	o delete device entry in the /proc/net/router directory
 *	o decrement module use count
 *
 *	Return:		0	Ok
 *			<0	error.
 *	Context:	process
 */


int unregister_wan_device(char *name)
{
	wan_device_t *wandev, *prev;

	if (name == NULL)
		return -EINVAL;

	for (wandev = router_devlist, prev = NULL;
		wandev && strcmp(wandev->name, name);
		prev = wandev, wandev = wandev->next)
		;
	if (wandev == NULL)
		return -ENODEV;

#ifdef WANDEBUG		
	printk(KERN_INFO "%s: unregistering WAN device %s\n", modname, name);
#endif
	
	if (wandev->state != WAN_UNCONFIGURED) {
		while(wandev->dev)
			delete_interface(wandev, wandev->dev->name, 1);
		if (wandev->shutdown)	
			wandev->shutdown(wandev);
	}
	if (prev)
		prev->next = wandev->next;
	else
		router_devlist = wandev->next;
	--devcnt;
	wanrouter_proc_delete(wandev);
        MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Encapsulate packet.
 *
 *	Return:	encapsulation header size
 *		< 0	- unsupported Ethertype
 *
 *	Notes:
 *	1. This function may be called on interrupt context.
 */


int wanrouter_encapsulate (struct sk_buff* skb, struct net_device* dev)
{
	int hdr_len = 0;

	switch (skb->protocol)
	{
	case ETH_P_IP:		/* IP datagram encapsulation */
		hdr_len += 1;
		skb_push(skb, 1);
		skb->data[0] = NLPID_IP;
		break;

	case ETH_P_IPX:		/* SNAP encapsulation */
	case ETH_P_ARP:
		hdr_len += 7;
		skb_push(skb, 7);
		skb->data[0] = 0;
		skb->data[1] = NLPID_SNAP;
		memcpy(&skb->data[2], oui_ether, sizeof(oui_ether));
		*((unsigned short*)&skb->data[5]) = htons(skb->protocol);
		break;

	default:		/* Unknown packet type */
		printk(KERN_INFO
			"%s: unsupported Ethertype 0x%04X on interface %s!\n",
			modname, skb->protocol, dev->name);
		hdr_len = -EINVAL;
	}
	return hdr_len;
}

/*
 *	Decapsulate packet.
 *
 *	Return:	Ethertype (in network order)
 *			0	unknown encapsulation
 *
 *	Notes:
 *	1. This function may be called on interrupt context.
 */


unsigned short wanrouter_type_trans (struct sk_buff* skb, struct net_device* dev)
{
	int cnt = skb->data[0] ? 0 : 1;	/* there may be a pad present */
	unsigned short ethertype;

	switch (skb->data[cnt])
	{
	case NLPID_IP:		/* IP datagramm */
		ethertype = htons(ETH_P_IP);
		cnt += 1;
		break;

        case NLPID_SNAP:	/* SNAP encapsulation */
		if (memcmp(&skb->data[cnt + 1], oui_ether, sizeof(oui_ether)))
		{
          		printk(KERN_INFO
				"%s: unsupported SNAP OUI %02X-%02X-%02X "
				"on interface %s!\n", modname,
				skb->data[cnt+1], skb->data[cnt+2],
				skb->data[cnt+3], dev->name);
			return 0;
		}	
		ethertype = *((unsigned short*)&skb->data[cnt+4]);
		cnt += 6;
		break;

	/* add other protocols, e.g. CLNP, ESIS, ISIS, if needed */

	default:
		printk(KERN_INFO
			"%s: unsupported NLPID 0x%02X on interface %s!\n",
			modname, skb->data[cnt], dev->name);
		return 0;
	}
	skb->protocol = ethertype;
	skb->pkt_type = PACKET_HOST;	/*	Physically point to point */
	skb->mac.raw  = skb->data;
	skb_pull(skb, cnt);
	return ethertype;
}


/*
 *	WAN device IOCTL.
 *	o find WAN device associated with this node
 *	o execute requested action or pass command to the device driver
 */

int wanrouter_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct proc_dir_entry *dent;
	wan_device_t *wandev;

	if (!capable(CAP_NET_ADMIN)){
		return -EPERM;
	}
		
	if ((cmd >> 8) != ROUTER_IOCTL)
		return -EINVAL;
		
	dent = inode->u.generic_ip;
	if ((dent == NULL) || (dent->data == NULL))
		return -EINVAL;
		
	wandev = dent->data;
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;
		
	switch (cmd) {
	case ROUTER_SETUP:
		err = device_setup(wandev, (void*)arg);
		break;

	case ROUTER_DOWN:
		err = device_shutdown(wandev);
		break;

	case ROUTER_STAT:
		err = device_stat(wandev, (void*)arg);
		break;

	case ROUTER_IFNEW:
		err = device_new_if(wandev, (void*)arg);
		break;

	case ROUTER_IFDEL:
		err = device_del_if(wandev, (void*)arg);
		break;

	case ROUTER_IFSTAT:
		break;

	default:
		if ((cmd >= ROUTER_USER) &&
		    (cmd <= ROUTER_USER_MAX) &&
		    wandev->ioctl)
			err = wandev->ioctl(wandev, cmd, arg);
		else err = -EINVAL;
	}
	return err;
}

/*
 *	WAN Driver IOCTL Handlers
 */

/*
 *	Setup WAN link device.
 *	o verify user address space
 *	o allocate kernel memory and copy configuration data to kernel space
 *	o if configuration data includes extension, copy it to kernel space too
 *	o call driver's setup() entry point
 */

static int device_setup (wan_device_t *wandev, wandev_conf_t *u_conf)
{
	void *data = NULL;
	wandev_conf_t *conf;
	int err = -EINVAL;

	if (wandev->setup == NULL)	/* Nothing to do ? */
		return 0;

	conf = kmalloc(sizeof(wandev_conf_t), GFP_KERNEL);
	if (conf == NULL)
		return -ENOBUFS;

	if(copy_from_user(conf, u_conf, sizeof(wandev_conf_t))) {
		kfree(conf);
		return -EFAULT;
	}
	
	if (conf->magic != ROUTER_MAGIC) {
		kfree(conf);
	        return -EINVAL; 
	}

	if (conf->data_size && conf->data) {
		if(conf->data_size > 128000 || conf->data_size < 0) {
			kfree(conf);
		        return -EINVAL;;
		}

		data = vmalloc(conf->data_size);
		if (data) {
			if(!copy_from_user(data, conf->data, conf->data_size)){
				conf->data=data;
				err = wandev->setup(wandev,conf);
			}
			else 
				err = -EFAULT;
		}
		else 
			err = -ENOBUFS;

		if (data)
			vfree(data);

	}

	kfree(conf);
	return err;
}

/*
 *	Shutdown WAN device.
 *	o delete all not opened logical channels for this device
 *	o call driver's shutdown() entry point
 */
 
static int device_shutdown (wan_device_t* wandev)
{
	struct net_device* dev;

	if (wandev->state == WAN_UNCONFIGURED)
		return 0;
		
	for (dev = wandev->dev; dev;)
	{
		if (delete_interface(wandev, dev->name, 0))
		{
			struct net_device **slave = dev->priv;
			dev = *slave;
		}
	}
	if (wandev->ndev)
		return -EBUSY;	/* there are opened interfaces  */
		
	if (wandev->shutdown)
		return wandev->shutdown(wandev);
	return 0;
}

/*
 *	Get WAN device status & statistics.
 */

static int device_stat (wan_device_t *wandev, wandev_stat_t *u_stat)
{
	wandev_stat_t stat;

	memset(&stat, 0, sizeof(stat));

	/* Ask device driver to update device statistics */
	if ((wandev->state != WAN_UNCONFIGURED) && wandev->update)
		wandev->update(wandev);

	/* Fill out structure */
	stat.ndev  = wandev->ndev;
	stat.state = wandev->state;

	if(copy_to_user(u_stat, &stat, sizeof(stat)))
		return -EFAULT;

	return 0;
}

/*
 *	Create new WAN interface.
 *	o verify user address space
 *	o copy configuration data to kernel address space
 *	o allocate network interface data space
 *	o call driver's new_if() entry point
 *	o make sure there is no interface name conflict
 *	o register network interface
 */

static int device_new_if (wan_device_t* wandev, wanif_conf_t* u_conf)
{
	wanif_conf_t conf;
	struct net_device *dev;
	int err;

	if ((wandev->state == WAN_UNCONFIGURED) || (wandev->new_if == NULL))
		return -ENODEV;
		
	if(copy_from_user(&conf, u_conf, sizeof(wanif_conf_t)))
		return -EFAULT;
		
	if (conf.magic != ROUTER_MAGIC)
		return -EINVAL;
		
	dev = kmalloc(sizeof(struct net_device), GFP_KERNEL);
	if (dev == NULL)
		return -ENOBUFS;
		
	memset(dev, 0, sizeof(struct net_device));
	err = wandev->new_if(wandev, dev, &conf);
	if (!err) {
		/* Register network interface. This will invoke init()
		 * function supplied by the driver.  If device registered
		 * successfully, add it to the interface list.
		 */
		if (dev->name == NULL)
			err = -EINVAL;
			
		else if (dev_get(dev->name))
			err = -EEXIST;	/* name already exists */
		else {
#ifdef WANDEBUG		
			printk(KERN_INFO "%s: registering interface %s...\n",
				modname, dev->name);
#endif				
			err = register_netdev(dev);
			if (!err) {
				struct net_device **slave = dev->priv;

				cli();	/***** critical section start *****/
				*slave = wandev->dev;
				wandev->dev = dev;
				++wandev->ndev;
				sti();	/****** critical section end ******/
				return 0;	/* done !!! */
			}
		}
		if (wandev->del_if)
			wandev->del_if(wandev, dev);
	}
	kfree(dev);
	return err;
}


/*
 *	Delete WAN logical channel.
 *	 o verify user address space
 *	 o copy configuration data to kernel address space
 */

static int device_del_if (wan_device_t *wandev, char *u_name)
{
	char name[WAN_IFNAME_SZ + 1];

	if (wandev->state == WAN_UNCONFIGURED)
		return -ENODEV;
	
	memset(name, 0, sizeof(name));

	if(copy_from_user(name, u_name, WAN_IFNAME_SZ))
		return -EFAULT;
	return delete_interface(wandev, name, 0);
}


/*
 *	Miscellaneous Functions
 */

/*
 *	Find WAN device by name.
 *	Return pointer to the WAN device data space or NULL if device not found.
 */

static wan_device_t *find_device(char *name)
{
	wan_device_t *wandev;

	for (wandev = router_devlist;wandev && strcmp(wandev->name, name);
		wandev = wandev->next);
	return wandev;
}

/*
 *	Delete WAN logical channel identified by its name.
 *	o find logical channel by its name
 *	o call driver's del_if() entry point
 *	o unregister network interface
 *	o unlink channel data space from linked list of channels
 *	o release channel data space
 *
 *	Return:	0		success
 *		-ENODEV		channel not found.
 *		-EBUSY		interface is open
 *
 *	Note: If (force != 0), then device will be destroyed even if interface
 *	associated with it is open. It's caller's responsibility to make
 *	sure that opened interfaces are not removed!
 */

static int delete_interface (wan_device_t *wandev, char *name, int force)
{
	struct net_device *dev, *prev;

	dev = wandev->dev;
	prev = NULL;
	while (dev && strcmp(name, dev->name)) {
		struct net_device **slave = dev->priv;

		prev = dev;
		dev = *slave;
	}

	if (dev == NULL)
		return -ENODEV;	/* interface not found */

	if (netif_running(dev)) {
		if (force) {
			printk(KERN_WARNING
				"%s: deleting opened interface %s!\n",
				modname, name);
		}
		else
			return -EBUSY;	/* interface in use */
	}

	if (wandev->del_if)
		wandev->del_if(wandev, dev);

	cli();			/***** critical section start *****/
	if (prev) {
		struct net_device **prev_slave = prev->priv;
		struct net_device **slave = dev->priv;

		*prev_slave = *slave;
	} else {
		struct net_device **slave = dev->priv;

		wandev->dev = *slave;
	}
	--wandev->ndev;
	sti();			/****** critical section end ******/

	printk("Unregistering '%s'\n", dev->name); 
	unregister_netdev(dev);
	kfree(dev);
	return 0;
}

EXPORT_SYMBOL(register_wan_device);
EXPORT_SYMBOL(unregister_wan_device);
EXPORT_SYMBOL(wanrouter_encapsulate);
EXPORT_SYMBOL(wanrouter_type_trans);

/*
 *	End
 */
