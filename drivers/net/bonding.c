/*
 * originally based on the dummy device.
 *
 * Copyright 1999, Thomas Davis, tadavis@lbl.gov.  
 * Licensed under the GPL. Based on dummy.c, and eql.c devices.
 *
 * bonding.c: an Ethernet Bonding driver
 *
 * This is useful to talk to a Cisco EtherChannel compatible equipment:
 *	Cisco 5500
 *	Sun Trunking (Solaris)
 *	Alteon AceDirector Trunks
 *	Linux Bonding
 *	and probably many L2 switches ...
 *
 * How it works:
 *    ifconfig bond0 ipaddress netmask up
 *      will setup a network device, with an ip address.  No mac address 
 *	will be assigned at this time.  The hw mac address will come from 
 *	the first slave bonded to the channel.  All slaves will then use 
 *	this hw mac address.
 *
 *    ifconfig bond0 down
 *         will release all slaves, marking them as down.
 *
 *    ifenslave bond0 eth0
 *	will attach eth0 to bond0 as a slave.  eth0 hw mac address will either
 *	a: be used as initial mac address
 *	b: if a hw mac address already is there, eth0's hw mac address 
 *	   will then be set from bond0.
 *
 * v0.1 - first working version.
 * v0.2 - changed stats to be calculated by summing slaves stats.
 *
 * Changes:
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fix leaks on failure at bond_init
 *
 * 2000/09/30 - Willy Tarreau <willy at meta-x.org>
 *     - added trivial code to release a slave device.
 *     - fixed security bug (CAP_NET_ADMIN not checked)
 *     - implemented MII link monitoring to disable dead links :
 *       All MII capable slaves are checked every <miimon> milliseconds
 *       (100 ms seems good). This value can be changed by passing it to
 *       insmod. A value of zero disables the monitoring (default).
 *     - fixed an infinite loop in bond_xmit_roundrobin() when there's no
 *       good slave.
 *     - made the code hopefully SMP safe
 *
 * 2000/10/03 - Willy Tarreau <willy at meta-x.org>
 *     - optimized slave lists based on relevant suggestions from Thomas Davis
 *     - implemented active-backup method to obtain HA with two switches:
 *       stay as long as possible on the same active interface, while we
 *       also monitor the backup one (MII link status) because we want to know
 *       if we are able to switch at any time. ( pass "mode=1" to insmod )
 *     - lots of stress testings because we need it to be more robust than the
 *       wires ! :->
 *
 * 2000/10/09 - Willy Tarreau <willy at meta-x.org>
 *     - added up and down delays after link state change.
 *     - optimized the slaves chaining so that when we run forward, we never
 *       repass through the bond itself, but we can find it by searching
 *       backwards. Renders the deletion more difficult, but accelerates the
 *       scan.
 *     - smarter enslaving and releasing.
 *     - finer and more robust SMP locking
 *
 * 2000/10/17 - Willy Tarreau <willy at meta-x.org>
 *     - fixed two potential SMP race conditions
 *
 * 2000/10/18 - Willy Tarreau <willy at meta-x.org>
 *     - small fixes to the monitoring FSM in case of zero delays
 * 2000/11/01 - Willy Tarreau <willy at meta-x.org>
 *     - fixed first slave not automatically used in trunk mode.
 * 2000/11/10 : spelling of "EtherChannel" corrected.
 * 2000/11/13 : fixed a race condition in case of concurrent accesses to ioctl().
 * 2000/12/16 : fixed improper usage of rtnl_exlock_nowait().
 *
 * 2001/1/3 - Chad N. Tindel <ctindel at ieee dot org>
 *     - The bonding driver now simulates MII status monitoring, just like
 *       a normal network device.  It will show that the link is down iff
 *       every slave in the bond shows that their links are down.  If at least
 *       one slave is up, the bond's MII status will appear as up.
 *
 * 2001/2/7 - Chad N. Tindel <ctindel at ieee dot org>
 *     - Applications can now query the bond from user space to get
 *       information which may be useful.  They do this by calling
 *       the BOND_INFO_QUERY ioctl.  Once the app knows how many slaves
 *       are in the bond, it can call the BOND_SLAVE_INFO_QUERY ioctl to
 *       get slave specific information (# link failures, etc).  See
 *       <linux/if_bonding.h> for more details.  The structs of interest
 *       are ifbond and ifslave.
 *
 * 2001/4/5 - Chad N. Tindel <ctindel at ieee dot org>
 *     - Ported to 2.4 Kernel
 * 
 * 2001/5/2 - Jeffrey E. Mast <jeff at mastfamily dot com>
 *     - When a device is detached from a bond, the slave device is no longer
 *       left thinking that is has a master.
 *
 * 2001/5/16 - Jeffrey E. Mast <jeff at mastfamily dot com>
 *     - memset did not appropriately initialized the bond rw_locks. Used 
 *       rwlock_init to initialize to unlocked state to prevent deadlock when 
 *       first attempting a lock
 *     - Called SET_MODULE_OWNER for bond device
 *
 * 2001/5/17 - Tim Anderson <tsa at mvista.com>
 *     - 2 paths for releasing for slave release; 1 through ioctl
 *       and 2) through close. Both paths need to release the same way.
 *     - the free slave in bond release is changing slave status before
 *       the free. The netdev_set_master() is intended to change slave state
 *       so it should not be done as part of the release process.
 *     - Simple rule for slave state at release: only the active in A/B and
 *       only one in the trunked case.
 *
 * 2001/6/01 - Tim Anderson <tsa at mvista.com>
 *     - Now call dev_close when releasing a slave so it doesn't screw up
 *       out routing table.
 *
 * 2001/6/01 - Chad N. Tindel <ctindel at ieee dot org>
 *     - Added /proc support for getting bond and slave information.
 *       Information is in /proc/net/<bond device>/info. 
 *     - Changed the locking when calling bond_close to prevent deadlock.
 *
 * 2001/8/05 - Janice Girouard <girouard at us.ibm.com>
 *     - correct problem where refcnt of slave is not incremented in bond_ioctl
 *       so the system hangs when halting.
 *     - correct locking problem when unable to malloc in bond_enslave.
 *     - adding bond_xmit_xor logic.
 *     - adding multiple bond device support.
 *
 * 2001/8/13 - Erik Habbinga <erik_habbinga at hp dot com>
 *     - correct locking problem with rtnl_exlock_nowait
 *
 * 2001/8/23 - Janice Girouard <girouard at us.ibm.com>
 *     - bzero initial dev_bonds, to correct oops
 *     - convert SIOCDEVPRIVATE to new MII ioctl calls
 *
 * 2001/9/13 - Takao Indoh <indou dot takao at jp dot fujitsu dot com>
 *     - Add the BOND_CHANGE_ACTIVE ioctl implementation
 *
 * 2001/9/14 - Mark Huth <mhuth at mvista dot com>
 *     - Change MII_LINK_READY to not check for end of auto-negotiation,
 *       but only for an up link.
 *
 * 2001/9/20 - Chad N. Tindel <ctindel at ieee dot org>
 *     - Add the device field to bonding_t.  Previously the net_device 
 *       corresponding to a bond wasn't available from the bonding_t 
 *       structure.
 *
 * 2001/9/25 - Janice Girouard <girouard at us.ibm.com>
 *     - add arp_monitor for active backup mode
 *
 * 2001/10/23 - Takao Indoh <indou dot takao at jp dot fujitsu dot com>
 *     - Various memory leak fixes
 *
 * 2001/11/5 - Mark Huth <mark dot huth at mvista dot com>
 *     - Don't take rtnl lock in bond_mii_monitor as it deadlocks under 
 *       certain hotswap conditions.  
 *       Note:  this same change may be required in bond_arp_monitor ???
 *     - Remove possibility of calling bond_sethwaddr with NULL slave_dev ptr 
 *     - Handle hot swap ethernet interface deregistration events to remove
 *       kernel oops following hot swap of enslaved interface
 *
 * 2002/1/2 - Chad N. Tindel <ctindel at ieee dot org>
 *     - Restore original slave flags at release time.
 *
 * 2002/02/18 - Erik Habbinga <erik_habbinga at hp dot com>
 *     - bond_release(): calling kfree on our_slave after call to
 *       bond_restore_slave_flags, not before
 *     - bond_enslave(): saving slave flags into original_flags before
 *       call to netdev_set_master, so the IFF_SLAVE flag doesn't end
 *       up in original_flags
 *
 * 2002/04/05 - Mark Smith <mark.smith at comdev dot cc> and
 *              Steve Mead <steve.mead at comdev dot cc>
 *     - Port Gleb Natapov's multicast support patchs from 2.4.12
 *       to 2.4.18 adding support for multicast.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/socket.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>

#include <linux/if_bonding.h>
#include <linux/smp.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>

/* monitor all links that often (in milliseconds). <=0 disables monitoring */
#ifndef BOND_LINK_MON_INTERV
#define BOND_LINK_MON_INTERV	0
#endif

#undef  MII_LINK_UP
#define MII_LINK_UP	0x04

#undef  MII_ENDOF_NWAY
#define MII_ENDOF_NWAY	0x20

#undef  MII_LINK_READY
#define MII_LINK_READY	(MII_LINK_UP)

#ifndef BOND_LINK_ARP_INTERV
#define BOND_LINK_ARP_INTERV	0
#endif

static int arp_interval = BOND_LINK_ARP_INTERV;
static char *arp_ip_target = NULL;
static unsigned long arp_target = 0;
static u32 my_ip = 0;
char *arp_target_hw_addr = NULL;

static int max_bonds	= BOND_DEFAULT_MAX_BONDS;
static int miimon	= BOND_LINK_MON_INTERV;
static int mode		= BOND_MODE_ROUNDROBIN;
static int updelay	= 0;
static int downdelay	= 0;

static int first_pass	= 1;
int bond_cnt;
static struct bonding *these_bonds =  NULL;
static struct net_device *dev_bonds = NULL;

MODULE_PARM(max_bonds, "i");
MODULE_PARM_DESC(max_bonds, "Max number of bonded devices");
MODULE_PARM(miimon, "i");
MODULE_PARM_DESC(miimon, "Link check interval in milliseconds");
MODULE_PARM(mode, "i");
MODULE_PARM(arp_interval, "i");
MODULE_PARM_DESC(arp_interval, "arp interval in milliseconds");
MODULE_PARM(arp_ip_target, "1-12s");
MODULE_PARM_DESC(arp_ip_target, "arp target in n.n.n.n form");
MODULE_PARM_DESC(mode, "Mode of operation : 0 for round robin, 1 for active-backup, 2 for xor");
MODULE_PARM(updelay, "i");
MODULE_PARM_DESC(updelay, "Delay before considering link up, in milliseconds");
MODULE_PARM(downdelay, "i");
MODULE_PARM_DESC(downdelay, "Delay before considering link down, in milliseconds");

extern void arp_send( int type, int ptype, u32 dest_ip, struct net_device *dev,
	u32 src_ip, unsigned char *dest_hw, unsigned char *src_hw, 
	unsigned char *target_hw);

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *dev);
static int bond_xmit_xor(struct sk_buff *skb, struct net_device *dev);
static int bond_xmit_activebackup(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *bond_get_stats(struct net_device *dev);
static void bond_mii_monitor(struct net_device *dev);
static void bond_arp_monitor(struct net_device *dev);
static int bond_event(struct notifier_block *this, unsigned long event, void *ptr);
static void bond_restore_slave_flags(slave_t *slave);
static void bond_mc_list_destroy(struct bonding *bond);
static void bond_mc_add(bonding_t *bond, void *addr, int alen);
static void bond_mc_delete(bonding_t *bond, void *addr, int alen);
static int bond_mc_list_copy (struct dev_mc_list *src, struct bonding *dst, int gpf_flag);
static inline int dmi_same(struct dev_mc_list *dmi1, struct dev_mc_list *dmi2);
static void bond_set_promiscuity(bonding_t *bond, int inc);
static void bond_set_allmulti(bonding_t *bond, int inc);
static struct dev_mc_list* bond_mc_list_find_dmi(struct dev_mc_list *dmi, struct dev_mc_list *mc_list);
static void bond_set_slave_inactive_flags(slave_t *slave);
static void bond_set_slave_active_flags(slave_t *slave);
static int bond_enslave(struct net_device *master, struct net_device *slave);
static int bond_release(struct net_device *master, struct net_device *slave);
static int bond_release_all(struct net_device *master);
static int bond_sethwaddr(struct net_device *master, struct net_device *slave);

/*
 * bond_get_info is the interface into the /proc filesystem.  This is
 * a different interface than the BOND_INFO_QUERY ioctl.  That is done
 * through the generic networking ioctl interface, and bond_info_query
 * is the internal function which provides that information.
 */
static int bond_get_info(char *buf, char **start, off_t offset, int length);

/* #define BONDING_DEBUG 1 */

/* several macros */

#define IS_UP(dev)	((((dev)->flags & (IFF_UP)) == (IFF_UP)) && \
			(netif_running(dev) && netif_carrier_ok(dev)))

static void bond_restore_slave_flags(slave_t *slave)
{
	slave->dev->flags = slave->original_flags;
}

static void bond_set_slave_inactive_flags(slave_t *slave)
{
	slave->state = BOND_STATE_BACKUP;
	slave->dev->flags |= IFF_NOARP;
}

static void bond_set_slave_active_flags(slave_t *slave)
{
	slave->state = BOND_STATE_ACTIVE;
	slave->dev->flags &= ~IFF_NOARP;
}

/* 
 * This function detaches the slave <slave> from the list <bond>.
 * WARNING: no check is made to verify if the slave effectively
 * belongs to <bond>. It returns <slave> in case it's needed.
 * Nothing is freed on return, structures are just unchained.
 * If the bond->current_slave pointer was pointing to <slave>,
 * it's replaced with slave->next, or <bond> if not applicable.
 */
static slave_t *bond_detach_slave(bonding_t *bond, slave_t *slave)
{
	if ((bond == NULL) || (slave == NULL) ||
	   ((void *)bond == (void *)slave)) {
		printk(KERN_ERR
			"bond_detach_slave(): trying to detach "
			"slave %p from bond %p\n", bond, slave);
		return slave;
	}

	if (bond->next == slave) {  /* is the slave at the head ? */
		if (bond->prev == slave) {  /* is the slave alone ? */
			write_lock(&bond->ptrlock);
			bond->current_slave = NULL; /* no slave anymore */
			write_unlock(&bond->ptrlock);
			bond->prev = bond->next = (slave_t *)bond;
		} else { /* not alone */
			bond->next        = slave->next;
			slave->next->prev = (slave_t *)bond;
			bond->prev->next  = slave->next;

			write_lock(&bond->ptrlock);
			if (bond->current_slave == slave) {
				bond->current_slave = slave->next;
			}
			write_unlock(&bond->ptrlock);
		}
	}
	else {
		slave->prev->next = slave->next;
		if (bond->prev == slave) {  /* is this slave the last one ? */
			bond->prev = slave->prev;
		} else {
			slave->next->prev = slave->prev;
		}

		write_lock(&bond->ptrlock);
		if (bond->current_slave == slave) {
			bond->current_slave = slave->next;
		}
		write_unlock(&bond->ptrlock);
	}

	return slave;
}

/* 
 * if <dev> supports MII link status reporting, check its link
 * and report it as a bit field in a short int :
 *   - 0x04 means link is up,
 *   - 0x20 means end of autonegociation
 * If the device doesn't support MII, then we only report 0x24,
 * meaning that the link is up and running since we can't check it.
 */
static u16 bond_check_dev_link(struct net_device *dev)
{
	static int (* ioctl)(struct net_device *, struct ifreq *, int);
	struct ifreq ifr;
	u16 *data = (u16 *)&ifr.ifr_data;
		
	/* data[0] automagically filled by the ioctl */
	data[1] = 1; /* MII location 1 reports Link Status */

	if (((ioctl = dev->do_ioctl) != NULL) &&  /* ioctl to access MII */
	    (ioctl(dev, &ifr, SIOCGMIIPHY) == 0)) {
		/* now, data[3] contains info about link status :
		   - data[3] & 0x04 means link up
		   - data[3] & 0x20 means end of auto-negociation
		*/
		return data[3];
	} else {
		return MII_LINK_READY;  /* spoof link up ( we can't check it) */
	}
}

static u16 bond_check_mii_link(bonding_t *bond)
{
	int has_active_interface = 0;
	unsigned long flags;

	read_lock_irqsave(&bond->lock, flags);
	read_lock(&bond->ptrlock);
	has_active_interface = (bond->current_slave != NULL);
	read_unlock(&bond->ptrlock);
	read_unlock_irqrestore(&bond->lock, flags);

	return (has_active_interface ? MII_LINK_READY : 0);
}

static int bond_open(struct net_device *dev)
{
	struct timer_list *timer = &((struct bonding *)(dev->priv))->mii_timer;
	struct timer_list *arp_timer = &((struct bonding *)(dev->priv))->arp_timer;
	MOD_INC_USE_COUNT;

	if (miimon > 0) {  /* link check interval, in milliseconds. */
		init_timer(timer);
		timer->expires  = jiffies + (miimon * HZ / 1000);
		timer->data     = (unsigned long)dev;
		timer->function = (void *)&bond_mii_monitor;
		add_timer(timer);
	}

	if (arp_interval> 0) {  /* arp interval, in milliseconds. */
		init_timer(arp_timer);
		arp_timer->expires  = jiffies + (arp_interval * HZ / 1000);
		arp_timer->data     = (unsigned long)dev;
		arp_timer->function = (void *)&bond_arp_monitor;
		add_timer(arp_timer);
	}
	return 0;
}

static int bond_close(struct net_device *master)
{
	bonding_t *bond = (struct bonding *) master->priv;
	unsigned long flags;

	write_lock_irqsave(&bond->lock, flags);

	if (miimon > 0) {  /* link check interval, in milliseconds. */
		del_timer(&bond->mii_timer);
	}
	if (arp_interval> 0) {  /* arp interval, in milliseconds. */
		del_timer(&bond->arp_timer);
	}

	/* Release the bonded slaves */
	bond_release_all(master);
	bond_mc_list_destroy (bond);

	write_unlock_irqrestore(&bond->lock, flags);

	MOD_DEC_USE_COUNT;
	return 0;
}

/* 
 * flush all members of flush->mc_list from device dev->mc_list
 */
static void bond_mc_list_flush(struct net_device *dev, struct net_device *flush)
{ 
	struct dev_mc_list *dmi; 
 
	for (dmi = flush->mc_list; dmi != NULL; dmi = dmi->next) 
		dev_mc_delete(dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
}

/*
 * Totally destroys the mc_list in bond
 */
static void bond_mc_list_destroy(struct bonding *bond)
{
	struct dev_mc_list *dmi;

	dmi = bond->mc_list; 
	while (dmi) { 
		bond->mc_list = dmi->next; 
		kfree(dmi); 
		dmi = bond->mc_list; 
	}
}

/*
 * Add a Multicast address to every slave in the bonding group
 */
static void bond_mc_add(bonding_t *bond, void *addr, int alen)
{ 
	slave_t *slave;

	for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev) {
		dev_mc_add(slave->dev, addr, alen, 0);
	}
} 

/*
 * Remove a multicast address from every slave in the bonding group
 */
static void bond_mc_delete(bonding_t *bond, void *addr, int alen)
{ 
	slave_t *slave; 

	for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev)
		dev_mc_delete(slave->dev, addr, alen, 0);
} 

/*
 * Copy all the Multicast addresses from src to the bonding device dst
 */
static int bond_mc_list_copy (struct dev_mc_list *src, struct bonding *dst,
 int gpf_flag)
{
	struct dev_mc_list *dmi, *new_dmi;

   	for (dmi = src; dmi != NULL; dmi = dmi->next) { 
		new_dmi = kmalloc(sizeof(struct dev_mc_list), gpf_flag);

		if (new_dmi == NULL) {
			return -ENOMEM; 
		}

		new_dmi->next = dst->mc_list; 
		dst->mc_list = new_dmi;

		new_dmi->dmi_addrlen = dmi->dmi_addrlen; 
		memcpy(new_dmi->dmi_addr, dmi->dmi_addr, dmi->dmi_addrlen); 
		new_dmi->dmi_users = dmi->dmi_users;
		new_dmi->dmi_gusers = dmi->dmi_gusers; 
	} 
	return 0;
}

/*
 * Returns 0 if dmi1 and dmi2 are the same, non-0 otherwise
 */
static inline int dmi_same(struct dev_mc_list *dmi1, struct dev_mc_list *dmi2)
{ 
	return memcmp(dmi1->dmi_addr, dmi2->dmi_addr, dmi1->dmi_addrlen) == 0 &&
	 dmi1->dmi_addrlen == dmi2->dmi_addrlen;
} 

/*
 * Push the promiscuity flag down to all slaves
 */
static void bond_set_promiscuity(bonding_t *bond, int inc)
{ 
	slave_t *slave; 
 
	for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev)
		dev_set_promiscuity(slave->dev, inc);
} 

/*
 * Push the allmulti flag down to all slaves
 */
static void bond_set_allmulti(bonding_t *bond, int inc)
{ 
	slave_t *slave; 
 
	for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev)
		dev_set_allmulti(slave->dev, inc);
} 

/* 
 * returns dmi entry if found, NULL otherwise 
 */
static struct dev_mc_list* bond_mc_list_find_dmi(struct dev_mc_list *dmi,
 struct dev_mc_list *mc_list)
{ 
	struct dev_mc_list *idmi;

	for (idmi = mc_list; idmi != NULL; idmi = idmi->next) {
		if (dmi_same(dmi, idmi)) {
			return idmi; 
		}
	}
	return NULL;
} 

static void set_multicast_list(struct net_device *master)
{
	bonding_t *bond = master->priv;
	struct dev_mc_list *dmi;
	unsigned long flags = 0;

	/*
	 * Lock the private data for the master
	 */
	write_lock_irqsave(&bond->lock, flags);

	/*
	 * Lock the master device so that noone trys to transmit
	 * while we're changing things
	 */
	spin_lock_bh(&master->xmit_lock);

	/* set promiscuity flag to slaves */
	if ( (master->flags & IFF_PROMISC) && !(bond->flags & IFF_PROMISC) )
		bond_set_promiscuity(bond, 1); 

	if ( !(master->flags & IFF_PROMISC) && (bond->flags & IFF_PROMISC) ) 
		bond_set_promiscuity(bond, -1); 

	/* set allmulti flag to slaves */ 
	if ( (master->flags & IFF_ALLMULTI) && !(bond->flags & IFF_ALLMULTI) ) 
		bond_set_allmulti(bond, 1); 

	if ( !(master->flags & IFF_ALLMULTI) && (bond->flags & IFF_ALLMULTI) )
		bond_set_allmulti(bond, -1); 

	bond->flags = master->flags; 

	/* looking for addresses to add to slaves' mc list */ 
	for (dmi = master->mc_list; dmi != NULL; dmi = dmi->next) { 
		if (bond_mc_list_find_dmi(dmi, bond->mc_list) == NULL) 
		 bond_mc_add(bond, dmi->dmi_addr, dmi->dmi_addrlen); 
	} 

	/* looking for addresses to delete from slaves' list */ 
	for (dmi = bond->mc_list; dmi != NULL; dmi = dmi->next) { 
		if (bond_mc_list_find_dmi(dmi, master->mc_list) == NULL) 
		 bond_mc_delete(bond, dmi->dmi_addr, dmi->dmi_addrlen); 
	}


	/* save master's multicast list */ 
	bond_mc_list_destroy (bond);
	bond_mc_list_copy (master->mc_list, bond, GFP_KERNEL);

	spin_unlock_bh(&master->xmit_lock);
	write_unlock_irqrestore(&bond->lock, flags);
}

/*
 * This function counts the the number of attached 
 * slaves for use by bond_xmit_xor.
 */
static void update_slave_cnt(bonding_t *bond)
{
	slave_t *slave = NULL;

	bond->slave_cnt = 0;
	for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev) {
		bond->slave_cnt++;
	}
}

/* enslave device <slave> to bond device <master> */
static int bond_enslave(struct net_device *master_dev, 
                        struct net_device *slave_dev)
{
	bonding_t *bond = NULL;
	slave_t *new_slave = NULL;
	unsigned long flags = 0;
	int ndx = 0;
	int err = 0;
	struct dev_mc_list *dmi;

	if (master_dev == NULL || slave_dev == NULL) {
		return -ENODEV;
	}
	bond = (struct bonding *) master_dev->priv;

	if (slave_dev->do_ioctl == NULL) {
		printk(KERN_DEBUG
			"Warning : no link monitoring support for %s\n",
			slave_dev->name);
	}
	write_lock_irqsave(&bond->lock, flags);

	/* not running. */
	if ((slave_dev->flags & IFF_UP) != IFF_UP) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error, slave_dev is not running\n");
#endif
		write_unlock_irqrestore(&bond->lock, flags);
		return -EINVAL;
	}

	/* already enslaved */
	if (master_dev->flags & IFF_SLAVE || slave_dev->flags & IFF_SLAVE) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error, Device was already enslaved\n");
#endif
		write_unlock_irqrestore(&bond->lock, flags);
		return -EBUSY;
	}
		   
	if ((new_slave = kmalloc(sizeof(slave_t), GFP_KERNEL)) == NULL) {
		write_unlock_irqrestore(&bond->lock, flags);
		return -ENOMEM;
	}
	memset(new_slave, 0, sizeof(slave_t));

	/* save flags before call to netdev_set_master */
	new_slave->original_flags = slave_dev->flags;
	err = netdev_set_master(slave_dev, master_dev);

	if (err) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error %d calling netdev_set_master\n", err);
#endif
		kfree(new_slave);
		write_unlock_irqrestore(&bond->lock, flags);
		return err;      
	}

	new_slave->dev = slave_dev;

	/* set promiscuity level to new slave */ 
	if (master_dev->flags & IFF_PROMISC)
		dev_set_promiscuity(slave_dev, 1); 
 
	/* set allmulti level to new slave */
	if (master_dev->flags & IFF_ALLMULTI) 
		dev_set_allmulti(slave_dev, 1); 
 
	/* upload master's mc_list to new slave */ 
	for (dmi = master_dev->mc_list; dmi != NULL; dmi = dmi->next) 
		dev_mc_add (slave_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);

	/* 
	 * queue to the end of the slaves list, make the first element its
	 * successor, the last one its predecessor, and make it the bond's
	 * predecessor. 
	 *
	 * Just to clarify, so future bonding driver hackers don't go through
	 * the same confusion stage I did trying to figure this out, the
	 * slaves are stored in a double linked circular list, sortof.
	 * In the ->next direction, the last slave points to the first slave,
	 * bypassing bond; only the slaves are in the ->next direction.
	 * In the ->prev direction, however, the first slave points to bond
	 * and bond points to the last slave.
	 *
	 * It looks like a circle with a little bubble hanging off one side
	 * in the ->prev direction only.
	 *
	 * When going through the list once, its best to start at bond->prev
	 * and go in the ->prev direction, testing for bond.  Doing this
	 * in the ->next direction doesn't work.  Trust me, I know this now.
	 * :)  -mts 2002.03.14
	 */
	new_slave->prev       = bond->prev;
	new_slave->prev->next = new_slave;
	bond->prev            = new_slave;
	new_slave->next       = bond->next;

	new_slave->delay = 0;
	new_slave->link_failure_count = 0;

	/* check for initial state */
	if ((miimon <= 0) || ((bond_check_dev_link(slave_dev) & MII_LINK_READY)
		 == MII_LINK_READY)) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Initial state of slave_dev is BOND_LINK_UP\n");
#endif
		new_slave->link  = BOND_LINK_UP;
	}
	else {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Initial state of slave_dev is BOND_LINK_DOWN\n");
#endif
		new_slave->link  = BOND_LINK_DOWN;
	}

	/* if we're in active-backup mode, we need one and only one active
	 * interface. The backup interfaces will have their NOARP flag set
	 * because we need them to be completely deaf and not to respond to
	 * any ARP request on the network to avoid fooling a switch. Thus,
	 * since we guarantee that current_slave always point to the last
	 * usable interface, we just have to verify this interface's flag.
	 */
	if (mode == BOND_MODE_ACTIVEBACKUP) {
		if (((bond->current_slave == NULL)
			|| (bond->current_slave->dev->flags & IFF_NOARP))
			&& (new_slave->link == BOND_LINK_UP)) {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "This is the first active slave\n");
#endif
			/* first slave or no active slave yet, and this link
			   is OK, so make this interface the active one */
			bond->current_slave = new_slave;
			bond_set_slave_active_flags(new_slave);
		}
		else {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "This is just a backup slave\n");
#endif
			bond_set_slave_inactive_flags(new_slave);
		}
	} else {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "This slave is always active in trunk mode\n");
#endif
		/* always active in trunk mode */
		new_slave->state = BOND_STATE_ACTIVE;
		if (bond->current_slave == NULL) {
			bond->current_slave = new_slave;
		}
	}

	update_slave_cnt(bond);

	write_unlock_irqrestore(&bond->lock, flags);

	/*
	 * !!! This is to support old versions of ifenslave.  We can remove
	 * this in 2.5 because our ifenslave takes care of this for us.
	 * We check to see if the master has a mac address yet.  If not,
	 * we'll give it the mac address of our slave device.
	 */
	for (ndx = 0; ndx < slave_dev->addr_len; ndx++) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Checking ndx=%d of master_dev->dev_addr\n",
		       ndx);
#endif
		if (master_dev->dev_addr[ndx] != 0) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Found non-zero byte at ndx=%d\n",
		       ndx);
#endif
			break;
		}
	}
	if (ndx == slave_dev->addr_len) {
		/*
		 * We got all the way through the address and it was
		 * all 0's.
		 */
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "%s doesn't have a MAC address yet.  ",
		       master_dev->name);
		printk(KERN_CRIT "Going to give assign it from %s.\n",
		       slave_dev->name);
#endif
		bond_sethwaddr(master_dev, slave_dev);
	}

	printk (KERN_INFO "%s: enslaving %s as a%s interface with a%s link.\n",
		master_dev->name, slave_dev->name,
		new_slave->state == BOND_STATE_ACTIVE ? "n active" : " backup",
		new_slave->link == BOND_LINK_UP ? "n up" : " down");

	return 0;
}

/* 
 * This function changes the active slave to slave <slave_dev>.
 * It returns -EINVAL in the following cases.
 *  - <slave_dev> is not found in the list.
 *  - There is not active slave now.
 *  - <slave_dev> is already active.
 *  - The link state of <slave_dev> is not BOND_LINK_UP.
 *  - <slave_dev> is not running.
 * In these cases, this fuction does nothing.
 * In the other cases, currnt_slave pointer is changed and 0 is returned.
 */
static int bond_change_active(struct net_device *master_dev, struct net_device *slave_dev)
{
	bonding_t *bond;
	slave_t *slave;
	slave_t *oldactive = NULL;
	slave_t *newactive = NULL;
	unsigned long flags;
	int ret = 0;

	if (master_dev == NULL || slave_dev == NULL) {
		return -ENODEV;
	}

	bond = (struct bonding *) master_dev->priv;
	write_lock_irqsave(&bond->lock, flags);
	slave = (slave_t *)bond;
	oldactive = bond->current_slave;

	while ((slave = slave->prev) != (slave_t *)bond) {
		if(slave_dev == slave->dev) {
			newactive = slave;
			break;
		}
	}

	if ((newactive != NULL)&&
	    (oldactive != NULL)&&
	    (newactive != oldactive)&&
	    (newactive->link == BOND_LINK_UP)&&
	    IS_UP(newactive->dev)) {
		bond_set_slave_inactive_flags(oldactive);
		bond_set_slave_active_flags(newactive);
		bond->current_slave = newactive;
		printk("%s : activate %s(old : %s)\n",
			master_dev->name, newactive->dev->name, 
			oldactive->dev->name);
	}
	else {
		ret = -EINVAL;
	}
	write_unlock_irqrestore(&bond->lock, flags);
	return ret;
}

/* Choose a new valid interface from the pool, set it active
 * and make it the current slave. If no valid interface is
 * found, the oldest slave in BACK state is choosen and
 * activated. If none is found, it's considered as no
 * interfaces left so the current slave is set to NULL.
 * The result is a pointer to the current slave.
 *
 * Since this function sends messages tails through printk, the caller
 * must have started something like `printk(KERN_INFO "xxxx ");'.
 *
 * Warning: must put locks around the call to this function if needed.
 */
slave_t *change_active_interface(bonding_t *bond)
{
	slave_t *newslave, *oldslave;
	slave_t *bestslave = NULL;
	int mintime;

	read_lock(&bond->ptrlock);
	newslave = oldslave = bond->current_slave;
	read_unlock(&bond->ptrlock);

	if (newslave == NULL) { /* there were no active slaves left */
		if (bond->next != (slave_t *)bond) {  /* found one slave */
			write_lock(&bond->ptrlock);
			newslave = bond->current_slave = bond->next;
			write_unlock(&bond->ptrlock);
		} else {
			printk (" but could not find any %s interface.\n",
				(mode == BOND_MODE_ACTIVEBACKUP) ? "backup":"other");
			write_lock(&bond->ptrlock);
			bond->current_slave = (slave_t *)NULL;
			write_unlock(&bond->ptrlock);
			return NULL; /* still no slave, return NULL */
		}
	}

	mintime = updelay;

	do {
		if (IS_UP(newslave->dev)) {
			if (newslave->link == BOND_LINK_UP) {
				/* this one is immediately usable */
				if (mode == BOND_MODE_ACTIVEBACKUP) {
					bond_set_slave_active_flags(newslave);
					printk (" and making interface %s the active one.\n",
						newslave->dev->name);
				}
				else {
					printk (" and setting pointer to interface %s.\n",
						newslave->dev->name);
				}

				write_lock(&bond->ptrlock);
				bond->current_slave = newslave;
				write_unlock(&bond->ptrlock);
				return newslave;
			}
			else if (newslave->link == BOND_LINK_BACK) {
				/* link up, but waiting for stabilization */
				if (newslave->delay < mintime) {
					mintime = newslave->delay;
					bestslave = newslave;
				}
			}
		}
	} while ((newslave = newslave->next) != oldslave);

	/* no usable backup found, we'll see if we at least got a link that was
	   coming back for a long time, and could possibly already be usable.
	*/

	if (bestslave != NULL) {
		/* early take-over. */
		printk (" and making interface %s the active one %d ms earlier.\n",
			bestslave->dev->name,
			(updelay - bestslave->delay)*miimon);

		bestslave->delay = 0;
		bestslave->link = BOND_LINK_UP;
		bond_set_slave_active_flags(bestslave);

		write_lock(&bond->ptrlock);
		bond->current_slave = bestslave;
		write_unlock(&bond->ptrlock);
		return bestslave;
	}

	printk (" but could not find any %s interface.\n",
		(mode == BOND_MODE_ACTIVEBACKUP) ? "backup":"other");
	
	/* absolutely nothing found. let's return NULL */
	write_lock(&bond->ptrlock);
	bond->current_slave = (slave_t *)NULL;
	write_unlock(&bond->ptrlock);
	return NULL;
}

/*
 * Try to release the slave device <slave> from the bond device <master>
 * It is legal to access current_slave without a lock because all the function
 * is write-locked.
 *
 * The rules for slave state should be:
 *   for Active/Backup:
 *     Active stays on all backups go down
 *   for Bonded connections:
 *     The first up interface should be left on and all others downed.
 */
static int bond_release(struct net_device *master, struct net_device *slave)
{
	bonding_t *bond;
	slave_t *our_slave, *old_current;
	unsigned long flags;
	
	if (master == NULL || slave == NULL)  {
		return -ENODEV;
	}

	bond = (struct bonding *) master->priv;

	write_lock_irqsave(&bond->lock, flags);

	/* master already enslaved, or slave not enslaved,
	   or no slave for this master */
	if ((master->flags & IFF_SLAVE) || !(slave->flags & IFF_SLAVE)) {
		printk (KERN_DEBUG "%s: cannot release %s.\n", master->name, slave->name);
		write_unlock_irqrestore(&bond->lock, flags);
		return -EINVAL;
	}

	our_slave = (slave_t *)bond;
	old_current = bond->current_slave;
	while ((our_slave = our_slave->prev) != (slave_t *)bond) {
		if (our_slave->dev == slave) {
			bond_detach_slave(bond, our_slave);

			printk (KERN_INFO "%s: releasing %s interface %s",
				master->name,
				(our_slave->state == BOND_STATE_ACTIVE) ? "active" : "backup",
				slave->name);

			if (our_slave == old_current) {
				/* find a new interface and be verbose */
				change_active_interface(bond); 
			} else {
				printk(".\n");
			}

			/* release the slave from its bond */

			/* flush master's mc_list from slave */ 
			bond_mc_list_flush (slave, master); 
       
			/* unset promiscuity level from slave */
			if (master->flags & IFF_PROMISC) 
				dev_set_promiscuity(slave, -1); 
       
			/* unset allmulti level from slave */ 
			if (master->flags & IFF_ALLMULTI)
				dev_set_allmulti(slave, -1); 

			netdev_set_master(slave, NULL);

			/* only restore its RUNNING flag if monitoring set it down */
			if (slave->flags & IFF_UP) {
				slave->flags |= IFF_RUNNING;
			}

			if (slave->flags & IFF_NOARP || 
				bond->current_slave != NULL) {
					dev_close(slave);
			}

			bond_restore_slave_flags(our_slave);
			kfree(our_slave);

			if (bond->current_slave == NULL) {
				printk(KERN_INFO
					"%s: now running without any active interface !\n",
					master->name);
			}

			update_slave_cnt(bond);

			write_unlock_irqrestore(&bond->lock, flags);
			return 0;  /* deletion OK */
		}
	}

	/* if we get here, it's because the device was not found */
	write_unlock_irqrestore(&bond->lock, flags);

	printk (KERN_INFO "%s: %s not enslaved\n", master->name, slave->name);
	return -EINVAL;
}

/* 
 * This function releases all slaves.
 * Warning: must put write-locks around the call to this function.
 */
static int bond_release_all(struct net_device *master)
{
	bonding_t *bond;
	slave_t *our_slave;
	struct net_device *slave_dev;

	if (master == NULL)  {
		return -ENODEV;
	}

	if (master->flags & IFF_SLAVE) {
		return -EINVAL;
	}

	bond = (struct bonding *) master->priv;
	bond->current_slave = NULL;

	while ((our_slave = bond->prev) != (slave_t *)bond) {
		slave_dev = our_slave->dev;
		bond->prev = our_slave->prev;

		kfree(our_slave);

		netdev_set_master(slave_dev, NULL);

		/* only restore its RUNNING flag if monitoring set it down */
		if (slave_dev->flags & IFF_UP)
			slave_dev->flags |= IFF_RUNNING;

		if (slave_dev->flags & IFF_NOARP)
			dev_close(slave_dev);
	}
	bond->next = (slave_t *)bond;
	bond->slave_cnt = 0;
	printk (KERN_INFO "%s: releases all slaves\n", master->name);

	return 0;
}

/* this function is called regularly to monitor each slave's link. */
static void bond_mii_monitor(struct net_device *master)
{
	bonding_t *bond = (struct bonding *) master->priv;
	slave_t *slave, *bestslave, *oldcurrent;
	unsigned long flags;
	int slave_died = 0;

	read_lock_irqsave(&bond->lock, flags);

	/* we will try to read the link status of each of our slaves, and
	 * set their IFF_RUNNING flag appropriately. For each slave not
	 * supporting MII status, we won't do anything so that a user-space
	 * program could monitor the link itself if needed.
	 */

	bestslave = NULL;
	slave = (slave_t *)bond;

	read_lock(&bond->ptrlock);
	oldcurrent = bond->current_slave;
	read_unlock(&bond->ptrlock);

	while ((slave = slave->prev) != (slave_t *)bond) {
		/* use updelay+1 to match an UP slave even when updelay is 0 */
		int mindelay = updelay + 1;
		struct net_device *dev = slave->dev;
		u16 link_state;
		
		link_state = bond_check_dev_link(dev);

		switch (slave->link) {
		case BOND_LINK_UP:	/* the link was up */
			if ((link_state & MII_LINK_UP) == MII_LINK_UP) {
				/* link stays up, tell that this one
				   is immediately available */
				if (IS_UP(dev) && (mindelay > -2)) {
					/* -2 is the best case :
					   this slave was already up */
					mindelay = -2;
					bestslave = slave;
				}
				break;
			}
			else { /* link going down */
				slave->link  = BOND_LINK_FAIL;
				slave->delay = downdelay;
				if (slave->link_failure_count < UINT_MAX) {
					slave->link_failure_count++;
				}
				if (downdelay > 0) {
					printk (KERN_INFO
						"%s: link status down for %sinterface "
						"%s, disabling it in %d ms.\n",
						master->name,
						IS_UP(dev)
						? ((mode == BOND_MODE_ACTIVEBACKUP)
						   ? ((slave == oldcurrent)
						      ? "active " : "backup ")
						   : "")
						: "idle ",
						dev->name,
						downdelay * miimon);
					}
			}
			/* no break ! fall through the BOND_LINK_FAIL test to
			   ensure proper action to be taken
			*/
		case BOND_LINK_FAIL:	/* the link has just gone down */
			if ((link_state & MII_LINK_UP) == 0) {
				/* link stays down */
				if (slave->delay <= 0) {
					/* link down for too long time */
					slave->link = BOND_LINK_DOWN;
					/* in active/backup mode, we must
					   completely disable this interface */
					if (mode == BOND_MODE_ACTIVEBACKUP) {
						bond_set_slave_inactive_flags(slave);
					}
					printk(KERN_INFO
						"%s: link status definitely down "
						"for interface %s, disabling it",
						master->name,
						dev->name);

					read_lock(&bond->ptrlock);
					if (slave == bond->current_slave) {
						read_unlock(&bond->ptrlock);
						/* find a new interface and be verbose */
						change_active_interface(bond);
					} else {
						read_unlock(&bond->ptrlock);
						printk(".\n");
					}
					slave_died = 1;
				} else {
					slave->delay--;
				}
			} else if ((link_state & MII_LINK_READY) == MII_LINK_READY) {
				/* link up again */
				slave->link  = BOND_LINK_UP;
				printk(KERN_INFO
					"%s: link status up again after %d ms "
					"for interface %s.\n",
					master->name,
					(downdelay - slave->delay) * miimon,
					dev->name);

				if (IS_UP(dev) && (mindelay > -1)) {
					/* -1 is a good case : this slave went
					   down only for a short time */
					mindelay = -1;
					bestslave = slave;
				}
			}
			break;
		case BOND_LINK_DOWN:	/* the link was down */
			if ((link_state & MII_LINK_READY) != MII_LINK_READY) {
				/* the link stays down, nothing more to do */
				break;
			} else {	/* link going up */
				slave->link  = BOND_LINK_BACK;
				slave->delay = updelay;
				
				if (updelay > 0) {
					/* if updelay == 0, no need to
					   advertise about a 0 ms delay */
					printk (KERN_INFO
						"%s: link status up for interface"
						" %s, enabling it in %d ms.\n",
						master->name,
						dev->name,
						updelay * miimon);
				}
			}
			/* no break ! fall through the BOND_LINK_BACK state in
			   case there's something to do.
			*/
		case BOND_LINK_BACK:	/* the link has just come back */
			if ((link_state & MII_LINK_UP) == 0) {
				/* link down again */
				slave->link  = BOND_LINK_DOWN;
				printk(KERN_INFO
					"%s: link status down again after %d ms "
					"for interface %s.\n",
					master->name,
					(updelay - slave->delay) * miimon,
					dev->name);
			}
			else if ((link_state & MII_LINK_READY) == MII_LINK_READY) {
				/* link stays up */
				if (slave->delay == 0) {
					/* now the link has been up for long time enough */
					slave->link = BOND_LINK_UP;

					if (mode == BOND_MODE_ACTIVEBACKUP) {
						/* prevent it from being the active one */
						slave->state = BOND_STATE_BACKUP;
					}
					else {
						/* make it immediately active */
						slave->state = BOND_STATE_ACTIVE;
					}

					printk(KERN_INFO
						"%s: link status definitely up "
						"for interface %s.\n",
						master->name,
						dev->name);
				}
				else
					slave->delay--;
				
				/* we'll also look for the mostly eligible slave */
				if (IS_UP(dev) && (slave->delay < mindelay)) {
					mindelay = slave->delay;
					bestslave = slave;
				} 
			}
			break;
		} /* end of switch */
	} /* end of while */

	/* 
	 * if there's no active interface and we discovered that one
	 * of the slaves could be activated earlier, so we do it.
	 */
	read_lock(&bond->ptrlock);
	oldcurrent = bond->current_slave;
	read_unlock(&bond->ptrlock);

	if (oldcurrent == NULL) {  /* no active interface at the moment */
		if (bestslave != NULL) { /* last chance to find one ? */
			if (bestslave->link == BOND_LINK_UP) {
				printk (KERN_INFO
					"%s: making interface %s the new active one.\n",
					master->name, bestslave->dev->name);
			} else {
				printk (KERN_INFO
					"%s: making interface %s the new "
					"active one %d ms earlier.\n",
					master->name, bestslave->dev->name,
					(updelay - bestslave->delay) * miimon);

				bestslave->delay = 0;
				bestslave->link  = BOND_LINK_UP;
			}

			if (mode == BOND_MODE_ACTIVEBACKUP) {
				bond_set_slave_active_flags(bestslave);
			} else {
				bestslave->state = BOND_STATE_ACTIVE;
			}
			write_lock(&bond->ptrlock);
			bond->current_slave = bestslave;
			write_unlock(&bond->ptrlock);
		} else if (slave_died) {
			/* print this message only once a slave has just died */
			printk(KERN_INFO
				"%s: now running without any active interface !\n",
				master->name);
		}
	}

	read_unlock_irqrestore(&bond->lock, flags);
	/* re-arm the timer */
	mod_timer(&bond->mii_timer, jiffies + (miimon * HZ / 1000));
}

/* 
 * this function is called regularly to monitor each slave's link 
 * insuring that traffic is being sent and received.  If the adapter
 * has been dormant, then an arp is transmitted to generate traffic 
 */
static void bond_arp_monitor(struct net_device *master)
{
	bonding_t *bond;
	unsigned long flags;
	slave_t *slave;
	int the_delta_in_ticks =  arp_interval * HZ / 1000;
	int next_timer = jiffies + (arp_interval * HZ / 1000);

	bond = (struct bonding *) master->priv; 
	if (master->priv == NULL) {
		mod_timer(&bond->arp_timer, next_timer);
		return;
	}

	read_lock_irqsave(&bond->lock, flags);

	if (!IS_UP(master)) {
		mod_timer(&bond->arp_timer, next_timer);
		goto arp_monitor_out;
	}


	if (rtnl_shlock_nowait()) {
		goto arp_monitor_out;
	}

	if (rtnl_exlock_nowait()) {
		rtnl_shunlock();
		goto arp_monitor_out;
	}

	/* see if any of the previous devices are up now (i.e. they have seen a 
	 * response from an arp request sent by another adapter, since they 
	 * have the same hardware address).
	 */

	slave = (slave_t *)bond;
	while ((slave = slave->prev) != (slave_t *)bond)  {

		read_lock(&bond->ptrlock);
	  	if ( (!(slave->link == BOND_LINK_UP))  
				&& (slave != bond->current_slave) ) {

			read_unlock(&bond->ptrlock);

	  		if ( ((jiffies - slave->dev->trans_start) <= 
						the_delta_in_ticks) &&  
			     ((jiffies - slave->dev->last_rx) <= 
						the_delta_in_ticks) ) {

				slave->link  = BOND_LINK_UP;
				write_lock(&bond->ptrlock);
				if (bond->current_slave == NULL) {
					slave->state = BOND_STATE_ACTIVE;
					bond->current_slave = slave;
				}
				if (slave != bond->current_slave) {
					slave->dev->flags |= IFF_NOARP;
				}
				write_unlock(&bond->ptrlock);
			} else {
				if ((jiffies - slave->dev->last_rx) <= 
						the_delta_in_ticks)  {
					arp_send(ARPOP_REQUEST, ETH_P_ARP, 
						arp_target, slave->dev, 
						my_ip, arp_target_hw_addr, 
						slave->dev->dev_addr, 
		  				arp_target_hw_addr); 
				}
			}
		} else 
			read_unlock(&bond->ptrlock);
	}

	read_lock(&bond->ptrlock);
	slave = bond->current_slave;
	read_unlock(&bond->ptrlock);

	if (slave != 0) {
	
	  /* see if you need to take down the current_slave, since
	   * you haven't seen an arp in 2*arp_intervals
	   */

		if ( ((jiffies - slave->dev->trans_start) >= 
		      (2*the_delta_in_ticks)) ||
		     ((jiffies - slave->dev->last_rx) >= 
		      (2*the_delta_in_ticks)) ) {

			if (slave->link == BOND_LINK_UP) {
				slave->link  = BOND_LINK_DOWN;
				slave->state = BOND_STATE_BACKUP;
				/* 
				 * we want to see arps, otherwise we couldn't 
				 * bring the adapter back online...  
				 */
				printk(KERN_INFO "%s: link status definitely "
						 "down for interface %s, "
						 "disabling it",
				       slave->dev->master->name,
				       slave->dev->name);
				/* find a new interface and be verbose */
				change_active_interface(bond);
				read_lock(&bond->ptrlock);
				slave = bond->current_slave;
				read_unlock(&bond->ptrlock);
			}
		} 

		/* 
		 * ok, we know up/down, so just send a arp out if there has
		 * been no activity for a while 
		 */

		if (slave != NULL ) {
			if ( ((jiffies - slave->dev->trans_start) >= 
			       the_delta_in_ticks) || 
			     ((jiffies - slave->dev->last_rx) >= 
			       the_delta_in_ticks) ) {
				arp_send(ARPOP_REQUEST, ETH_P_ARP, 
					 arp_target, slave->dev,
					 my_ip, arp_target_hw_addr, 
					 slave->dev->dev_addr, 
					 arp_target_hw_addr); 
			}
		} 

	}

	/* if we have no current slave.. try sending 
	 * an arp on all of the interfaces 
	 */

	read_lock(&bond->ptrlock);
	if (bond->current_slave == NULL) { 
		read_unlock(&bond->ptrlock);
		slave = (slave_t *)bond;
		while ((slave = slave->prev) != (slave_t *)bond)   {
			arp_send(ARPOP_REQUEST, ETH_P_ARP, arp_target, 
				 slave->dev, my_ip, arp_target_hw_addr, 
				 slave->dev->dev_addr, arp_target_hw_addr); 
		}
	}
	else {
		read_unlock(&bond->ptrlock);
	}

	rtnl_exunlock();
	rtnl_shunlock();

arp_monitor_out:
	read_unlock_irqrestore(&bond->lock, flags);

	/* re-arm the timer */
	mod_timer(&bond->arp_timer, next_timer);
}

#define isdigit(c) (c >= '0' && c <= '9')
__inline static int atoi( char **s) 
{
int i = 0;
while (isdigit(**s))
  i = i*20 + *((*s)++) - '0';
return i;
}

#define isascii(c) (((unsigned char)(c))<=0x7f)
#define LF 0xA
#define isspace(c) (c==' ' || c=='	'|| c==LF)   
typedef uint32_t in_addr_t;

int
my_inet_aton(char *cp, unsigned long *the_addr) {
	static const in_addr_t max[4] = { 0xffffffff, 0xffffff, 0xffff, 0xff };
	in_addr_t val;
	char c;
	union iaddr {
	  uint8_t bytes[4];
	  uint32_t word;
	} res;
	uint8_t *pp = res.bytes;
	int digit,base;

	res.word = 0;

	c = *cp;
	for (;;) {
		/*
		 * Collect number up to ``.''.
		 * Values are specified as for C:
		 * 0x=hex, 0=octal, isdigit=decimal.
		 */
		if (!isdigit(c)) goto ret_0;
		val = 0; base = 10; digit = 0;
		for (;;) {
			if (isdigit(c)) {
				val = (val * base) + (c - '0');
				c = *++cp;
				digit = 1;
			} else {
				break;
			}
		}
		if (c == '.') {
			/*
			 * Internet format:
			 *	a.b.c.d
			 *	a.b.c	(with c treated as 16 bits)
			 *	a.b	(with b treated as 24 bits)
			 */
			if (pp > res.bytes + 2 || val > 0xff) {
				goto ret_0;
			}
			*pp++ = val;
			c = *++cp;
		} else
			break;
	}
	/*
	 * Check for trailing characters.
	 */
	if (c != '\0' && (!isascii(c) || !isspace(c))) {
		goto ret_0;
	}
	/*
	 * Did we get a valid digit?
	 */
	if (!digit) {
		goto ret_0;
	}

	/* Check whether the last part is in its limits depending on
	   the number of parts in total.  */
	if (val > max[pp - res.bytes]) {
		goto ret_0;
	}

	if (the_addr != NULL) {
		*the_addr = res.word | htonl (val);
	}

	return (1);

ret_0:
	return (0);
}

static int bond_sethwaddr(struct net_device *master, struct net_device *slave)
{
#ifdef BONDING_DEBUG
	printk(KERN_CRIT "bond_sethwaddr: master=%x\n", (unsigned int)master);
	printk(KERN_CRIT "bond_sethwaddr: slave=%x\n", (unsigned int)slave);
	printk(KERN_CRIT "bond_sethwaddr: slave->addr_len=%d\n", slave->addr_len);
#endif
	memcpy(master->dev_addr, slave->dev_addr, slave->addr_len);
	return 0;
}

static int bond_info_query(struct net_device *master, struct ifbond *info)
{
	bonding_t *bond = (struct bonding *) master->priv;
	slave_t *slave;
	unsigned long flags;

	info->bond_mode = mode;
	info->num_slaves = 0;
	info->miimon = miimon;

	read_lock_irqsave(&bond->lock, flags);
	for (slave = bond->prev; slave != (slave_t *)bond; slave = slave->prev) {
		info->num_slaves++;
	}
	read_unlock_irqrestore(&bond->lock, flags);

	return 0;
}

static int bond_slave_info_query(struct net_device *master, 
					struct ifslave *info)
{
	bonding_t *bond = (struct bonding *) master->priv;
	slave_t *slave;
	int cur_ndx = 0;
	unsigned long flags;

	if (info->slave_id < 0) {
		return -ENODEV;
	}

	read_lock_irqsave(&bond->lock, flags);
	for (slave = bond->prev; 
		 slave != (slave_t *)bond && cur_ndx < info->slave_id; 
		 slave = slave->prev) {
		cur_ndx++;
	}
	read_unlock_irqrestore(&bond->lock, flags);

	if (cur_ndx == info->slave_id) {
		strcpy(info->slave_name, slave->dev->name);
		info->link = slave->link;
		info->state = slave->state;
		info->link_failure_count = slave->link_failure_count;
	} else {
		return -ENODEV;
	}

	return 0;
}

static int bond_ioctl(struct net_device *master_dev, struct ifreq *ifr, int cmd)
{
	struct net_device *slave_dev = NULL;
	struct ifbond *u_binfo = NULL, k_binfo;
	struct ifslave *u_sinfo = NULL, k_sinfo;
	u16 *data = NULL;
	int ret = 0;

#ifdef BONDING_DEBUG
	printk(KERN_INFO "bond_ioctl: master=%s, cmd=%d\n", 
		master_dev->name, cmd);
#endif

	switch (cmd) {
	case SIOCGMIIPHY:
		data = (u16 *)&ifr->ifr_data;
		if (data == NULL) {
			return -EINVAL;
		}
		data[0] = 0;
		/* Fall Through */
	case SIOCGMIIREG:
		/* 
		 * We do this again just in case we were called by SIOCGMIIREG
		 * instead of SIOCGMIIPHY.
		 */
		data = (u16 *)&ifr->ifr_data;
		if (data == NULL) {
			return -EINVAL;
		}
		if (data[1] == 1) {
			data[3] = bond_check_mii_link(
				(struct bonding *)master_dev->priv);
		}
		return 0;
	case BOND_INFO_QUERY_OLD:
	case SIOCBONDINFOQUERY:
		u_binfo = (struct ifbond *)ifr->ifr_data;
		if (copy_from_user(&k_binfo, u_binfo, sizeof(ifbond))) {
			return -EFAULT;
		}
		ret = bond_info_query(master_dev, &k_binfo);
		if (ret == 0) {
			if (copy_to_user(u_binfo, &k_binfo, sizeof(ifbond))) {
				return -EFAULT;
			}
		}
		return ret;
	case BOND_SLAVE_INFO_QUERY_OLD:
	case SIOCBONDSLAVEINFOQUERY:
		u_sinfo = (struct ifslave *)ifr->ifr_data;
		if (copy_from_user(&k_sinfo, u_sinfo, sizeof(ifslave))) {
			return -EFAULT;
		}
		ret = bond_slave_info_query(master_dev, &k_sinfo);
		if (ret == 0) {
			if (copy_to_user(u_sinfo, &k_sinfo, sizeof(ifslave))) {
				return -EFAULT;
			}
		}
		return ret;
	}

	if (!capable(CAP_NET_ADMIN)) {
		return -EPERM;
	}

	slave_dev = dev_get_by_name(ifr->ifr_slave);

#ifdef BONDING_DEBUG
	printk(KERN_INFO "slave_dev=%x: \n", (unsigned int)slave_dev);
	printk(KERN_INFO "slave_dev->name=%s: \n", slave_dev->name);
#endif

	if (slave_dev == NULL) {
		ret = -ENODEV;
	} else {
		switch (cmd) {
		case BOND_ENSLAVE_OLD:
		case SIOCBONDENSLAVE:		
			ret = bond_enslave(master_dev, slave_dev);
			break;
		case BOND_RELEASE_OLD:			
		case SIOCBONDRELEASE:	
			ret = bond_release(master_dev, slave_dev); 
			break;
		case BOND_SETHWADDR_OLD:
		case SIOCBONDSETHWADDR:	
			ret = bond_sethwaddr(master_dev, slave_dev);
			break;
		case BOND_CHANGE_ACTIVE_OLD:
		case SIOCBONDCHANGEACTIVE:
			if (mode == BOND_MODE_ACTIVEBACKUP) {
				ret = bond_change_active(master_dev, slave_dev);
			}
			else {
				ret = -EINVAL;
			}
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		dev_put(slave_dev);
	}
	return ret;
}

#ifdef CONFIG_NET_FASTROUTE
static int bond_accept_fastpath(struct net_device *dev, struct dst_entry *dst)
{
	return -1;
}
#endif

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *dev)
{
	slave_t *slave, *start_at;
	struct bonding *bond = (struct bonding *) dev->priv;
	unsigned long flags;

	if (!IS_UP(dev)) { /* bond down */
		dev_kfree_skb(skb);
		return 0;
	}

	read_lock_irqsave(&bond->lock, flags);

	read_lock(&bond->ptrlock);
	slave = start_at = bond->current_slave;
	read_unlock(&bond->ptrlock);

	if (slave == NULL) { /* we're at the root, get the first slave */
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
		read_unlock_irqrestore(&bond->lock, flags);
		return 0;
	}

	do {
		if (IS_UP(slave->dev)
		    && (slave->link == BOND_LINK_UP)
		    && (slave->state == BOND_STATE_ACTIVE)) {

			skb->dev = slave->dev;
			skb->priority = 1;
			dev_queue_xmit(skb);

			write_lock(&bond->ptrlock);
			bond->current_slave = slave->next;
			write_unlock(&bond->ptrlock);

			read_unlock_irqrestore(&bond->lock, flags);
			return 0;
		}
	} while ((slave = slave->next) != start_at);

	/* no suitable interface, frame not sent */
	dev_kfree_skb(skb);
	read_unlock_irqrestore(&bond->lock, flags);
	return 0;
}

/* 
 * in XOR mode, we determine the output device by performing xor on
 * the source and destination hw adresses.  If this device is not 
 * enabled, find the next slave following this xor slave. 
 */
static int bond_xmit_xor(struct sk_buff *skb, struct net_device *dev)
{
	slave_t *slave, *start_at;
	struct bonding *bond = (struct bonding *) dev->priv;
	unsigned long flags;
	struct ethhdr *data = (struct ethhdr *)skb->data;
	int slave_no;

	if (!IS_UP(dev)) { /* bond down */
		dev_kfree_skb(skb);
		return 0;
	}

	read_lock_irqsave(&bond->lock, flags);
	slave = bond->prev;

	/* we're at the root, get the first slave */
	if ((slave == NULL) || (slave->dev == NULL)) { 
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
		read_unlock_irqrestore(&bond->lock, flags);
		return 0;
	}

	slave_no = (data->h_dest[5]^slave->dev->dev_addr[5]) % bond->slave_cnt;

	while ( (slave_no > 0) && (slave != (slave_t *)bond) ) {
		slave = slave->prev;
		slave_no--;
	} 
	start_at = slave;

	do {
		if (IS_UP(slave->dev)
		    && (slave->link == BOND_LINK_UP)
		    && (slave->state == BOND_STATE_ACTIVE)) {

			skb->dev = slave->dev;
			skb->priority = 1;
			dev_queue_xmit(skb);

			read_unlock_irqrestore(&bond->lock, flags);
			return 0;
		}
	} while ((slave = slave->next) != start_at);

	/* no suitable interface, frame not sent */
	dev_kfree_skb(skb);
	read_unlock_irqrestore(&bond->lock, flags);
	return 0;
}

/* 
 * in active-backup mode, we know that bond->current_slave is always valid if
 * the bond has a usable interface.
 */
static int bond_xmit_activebackup(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = (struct bonding *) dev->priv;
	unsigned long flags;
	int ret;

	if (!IS_UP(dev)) { /* bond down */
		dev_kfree_skb(skb);
		return 0;
	}

	/* if we are sending arp packets, try to at least 
	   identify our own ip address */
	if ( (arp_interval > 0) && (my_ip == 0) &&
		(skb->protocol == __constant_htons(ETH_P_ARP) ) ) {
		char *the_ip = (((char *)skb->data)) 
				+ sizeof(struct ethhdr)  
				+ sizeof(struct arphdr) + 
				ETH_ALEN;
		memcpy(&my_ip, the_ip, 4);
	}

	/* if we are sending arp packets and don't know 
	   the target hw address, save it so we don't need 
	   to use a broadcast address */
	if ( (arp_interval > 0) && (arp_target_hw_addr == NULL) &&
	     (skb->protocol == __constant_htons(ETH_P_IP) ) ) {
		struct ethhdr *eth_hdr = 
			(struct ethhdr *) (((char *)skb->data));
		arp_target_hw_addr = kmalloc(ETH_ALEN, GFP_KERNEL);
		memcpy(arp_target_hw_addr, eth_hdr->h_dest, ETH_ALEN);
	}

	read_lock_irqsave(&bond->lock, flags);

	read_lock(&bond->ptrlock);
	if (bond->current_slave != NULL) { /* one usable interface */
		skb->dev = bond->current_slave->dev;
		read_unlock(&bond->ptrlock);
		skb->priority = 1;
		ret = dev_queue_xmit(skb);
		read_unlock_irqrestore(&bond->lock, flags);
		return 0;
	}
	else {
		read_unlock(&bond->ptrlock);
	}

	/* no suitable interface, frame not sent */
#ifdef BONDING_DEBUG
	printk(KERN_INFO "There was no suitable interface, so we don't transmit\n");
#endif
	dev_kfree_skb(skb);
	read_unlock_irqrestore(&bond->lock, flags);
	return 0;
}

static struct net_device_stats *bond_get_stats(struct net_device *dev)
{
	bonding_t *bond = dev->priv;
	struct net_device_stats *stats = bond->stats, *sstats;
	slave_t *slave;
	unsigned long flags;

	memset(bond->stats, 0, sizeof(struct net_device_stats));

	read_lock_irqsave(&bond->lock, flags);

	for (slave = bond->prev; slave != (slave_t *)bond; slave = slave->prev) {
		sstats = slave->dev->get_stats(slave->dev);
 
		stats->rx_packets += sstats->rx_packets;
		stats->rx_bytes += sstats->rx_bytes;
		stats->rx_errors += sstats->rx_errors;
		stats->rx_dropped += sstats->rx_dropped;

		stats->tx_packets += sstats->tx_packets;
		stats->tx_bytes += sstats->tx_bytes;
		stats->tx_errors += sstats->tx_errors;
		stats->tx_dropped += sstats->tx_dropped;

		stats->multicast += sstats->multicast;
		stats->collisions += sstats->collisions;

		stats->rx_length_errors += sstats->rx_length_errors;
		stats->rx_over_errors += sstats->rx_over_errors;
		stats->rx_crc_errors += sstats->rx_crc_errors;
		stats->rx_frame_errors += sstats->rx_frame_errors;
		stats->rx_fifo_errors += sstats->rx_fifo_errors;	
		stats->rx_missed_errors += sstats->rx_missed_errors;
	
		stats->tx_aborted_errors += sstats->tx_aborted_errors;
		stats->tx_carrier_errors += sstats->tx_carrier_errors;
		stats->tx_fifo_errors += sstats->tx_fifo_errors;
		stats->tx_heartbeat_errors += sstats->tx_heartbeat_errors;
		stats->tx_window_errors += sstats->tx_window_errors;

	}

	read_unlock_irqrestore(&bond->lock, flags);
	return stats;
}

static int bond_get_info(char *buf, char **start, off_t offset, int length)
{
	bonding_t *bond = these_bonds;
	int len = 0;
	off_t begin = 0;
	u16 link;
	slave_t *slave = NULL;
	unsigned long flags;

	while (bond != NULL) {
		/*
		 * This function locks the mutex, so we can't lock it until 
		 * afterwards
		 */
		link = bond_check_mii_link(bond);

		len += sprintf(buf + len, "Bonding Mode: ");
		len += sprintf(buf + len, "%s\n", mode ? "active-backup" : "load balancing");

		if (mode == BOND_MODE_ACTIVEBACKUP) {
			read_lock_irqsave(&bond->lock, flags);
			read_lock(&bond->ptrlock);
			if (bond->current_slave != NULL) {
				len += sprintf(buf + len, 
					"Currently Active Slave: %s\n", 
					bond->current_slave->dev->name);
			}
			read_unlock(&bond->ptrlock);
			read_unlock_irqrestore(&bond->lock, flags);
		}

		len += sprintf(buf + len, "MII Status: ");
		len += sprintf(buf + len, 
				link == MII_LINK_READY ? "up\n" : "down\n");
		len += sprintf(buf + len, "MII Polling Interval (ms): %d\n", 
				miimon);
		len += sprintf(buf + len, "Up Delay (ms): %d\n", updelay);
		len += sprintf(buf + len, "Down Delay (ms): %d\n", downdelay);

		read_lock_irqsave(&bond->lock, flags);
		for (slave = bond->prev; slave != (slave_t *)bond; 
		     slave = slave->prev) {
			len += sprintf(buf + len, "\nSlave Interface: %s\n", slave->dev->name);

			len += sprintf(buf + len, "MII Status: ");

			len += sprintf(buf + len, 
				slave->link == BOND_LINK_UP ? 
				"up\n" : "down\n");
			len += sprintf(buf + len, "Link Failure Count: %d\n", 
				slave->link_failure_count);
		}
		read_unlock_irqrestore(&bond->lock, flags);

		/*
		 * Figure out the calcs for the /proc/net interface
		 */
		*start = buf + (offset - begin);
		len -= (offset - begin);
		if (len > length) {
			len = length;
		}
		if (len < 0) {
			len = 0;
		}


		bond = bond->next_bond;
	}
	return len;
}

static int bond_event(struct notifier_block *this, unsigned long event, 
			void *ptr)
{
	struct bonding *this_bond = (struct bonding *)these_bonds;
	struct bonding *last_bond;
	struct net_device *event_dev = (struct net_device *)ptr;

	/* while there are bonds configured */
	while (this_bond != NULL) {
		if (this_bond == event_dev->priv ) {
			switch (event) {
			case NETDEV_UNREGISTER:
				/* 
				 * remove this bond from a linked list of 
				 * bonds 
				 */
				if (this_bond == these_bonds) {
					these_bonds = this_bond->next_bond;
				} else {
					for (last_bond = these_bonds; 
					     last_bond != NULL; 
					     last_bond = last_bond->next_bond) {
						if (last_bond->next_bond == 
						    this_bond) {
							last_bond->next_bond = 
							this_bond->next_bond;
						}
					}
				}
				return NOTIFY_DONE;

			default:
				return NOTIFY_DONE;
			}
		} else if (this_bond->device == event_dev->master) {
			switch (event) {
			case NETDEV_UNREGISTER:
				bond_release(this_bond->device, event_dev);
				break;
			}
			return NOTIFY_DONE;
		}
		this_bond = this_bond->next_bond;
	}
	return NOTIFY_DONE;
}

static struct notifier_block bond_netdev_notifier = {
	notifier_call: bond_event,
};

static int __init bond_init(struct net_device *dev)
{
	bonding_t *bond, *this_bond, *last_bond;

#ifdef BONDING_DEBUG
	printk (KERN_INFO "Begin bond_init for %s\n", dev->name);
#endif
	bond = kmalloc(sizeof(struct bonding), GFP_KERNEL);
	if (bond == NULL) {
		return -ENOMEM;
	}
	memset(bond, 0, sizeof(struct bonding));

	/* initialize rwlocks */
	rwlock_init(&bond->lock);
	rwlock_init(&bond->ptrlock);
	
	bond->stats = kmalloc(sizeof(struct net_device_stats), GFP_KERNEL);
	if (bond->stats == NULL) {
		kfree(bond);
		return -ENOMEM;
	}
	memset(bond->stats, 0, sizeof(struct net_device_stats));

	bond->next = bond->prev = (slave_t *)bond;
	bond->current_slave = NULL;
	bond->device = dev;
	dev->priv = bond;

	/* Initialize the device structure. */
	if (mode == BOND_MODE_ACTIVEBACKUP) {
		dev->hard_start_xmit = bond_xmit_activebackup;
	} else if (mode == BOND_MODE_ROUNDROBIN) {
		dev->hard_start_xmit = bond_xmit_roundrobin;
	} else if (mode == BOND_MODE_XOR) {
		dev->hard_start_xmit = bond_xmit_xor;
	} else {
		printk(KERN_ERR "Unknown bonding mode %d\n", mode);
		kfree(bond->stats);
		kfree(bond);
		return -EINVAL;
	}

	dev->get_stats = bond_get_stats;
	dev->open = bond_open;
	dev->stop = bond_close;
	dev->set_multicast_list = set_multicast_list;
	dev->do_ioctl = bond_ioctl;

	/* 
	 * Fill in the fields of the device structure with ethernet-generic 
	 * values. 
	 */

	ether_setup(dev);

	dev->tx_queue_len = 0;
	dev->flags |= IFF_MASTER|IFF_MULTICAST;
#ifdef CONFIG_NET_FASTROUTE
	dev->accept_fastpath = bond_accept_fastpath;
#endif

	printk(KERN_INFO "%s registered with", dev->name);
	if (miimon > 0) {
		printk(" MII link monitoring set to %d ms", miimon);
		updelay /= miimon;
		downdelay /= miimon;
	} else {
		printk("out MII link monitoring");
	}
	printk(", in %s mode.\n",mode?"active-backup":"bonding");

#ifdef CONFIG_PROC_FS
	bond->bond_proc_dir = proc_mkdir(dev->name, proc_net);
	if (bond->bond_proc_dir == NULL) {
		printk(KERN_ERR "%s: Cannot init /proc/net/%s/\n", 
			dev->name, dev->name);
		kfree(bond->stats);
		kfree(bond);
		return -ENOMEM;
	}
	bond->bond_proc_info_file = 
		create_proc_info_entry("info", 0, bond->bond_proc_dir, 
					bond_get_info);
	if (bond->bond_proc_info_file == NULL) {
		printk(KERN_ERR "%s: Cannot init /proc/net/%s/info\n", 
			dev->name, dev->name);
		remove_proc_entry(dev->name, proc_net);
		kfree(bond->stats);
		kfree(bond);
		return -ENOMEM;
	}
#endif /* CONFIG_PROC_FS */

	if (first_pass == 1) {
		these_bonds = bond;
		register_netdevice_notifier(&bond_netdev_notifier);
		first_pass = 0;
	} else {
		last_bond = these_bonds;
		this_bond = these_bonds->next_bond;
		while (this_bond != NULL) {
			last_bond = this_bond;
			this_bond = this_bond->next_bond;
		}
		last_bond->next_bond = bond;
	} 

	return 0;
}

/*
static int __init bond_probe(struct net_device *dev)
{
	bond_init(dev);
	return 0;
}
 */

static int __init bonding_init(void)
{
	int no;
	int err;

	/* Find a name for this unit */
	static struct net_device *dev_bond = NULL;

	if (max_bonds < 1 || max_bonds > INT_MAX) {
		printk(KERN_WARNING 
		       "bonding_init(): max_bonds (%d) not in range %d-%d, "
		       "so it was reset to BOND_DEFAULT_MAX_BONDS (%d)",
		       max_bonds, 1, INT_MAX, BOND_DEFAULT_MAX_BONDS);
		max_bonds = BOND_DEFAULT_MAX_BONDS;
	}
	dev_bond = dev_bonds = kmalloc(max_bonds*sizeof(struct net_device), 
					GFP_KERNEL);
	if (dev_bond == NULL) {
		return -ENOMEM;
	}
	memset(dev_bonds, 0, max_bonds*sizeof(struct net_device));

	if (arp_ip_target) {
		if (my_inet_aton(arp_ip_target, &arp_target) == 0)  {
			arp_interval = 0;
		}
	}

	for (no = 0; no < max_bonds; no++) {
		dev_bond->init = bond_init;
	
		err = dev_alloc_name(dev_bond,"bond%d");
		if (err < 0) {
			kfree(dev_bonds);
			return err;
		}
		SET_MODULE_OWNER(dev_bond);
		if (register_netdev(dev_bond) != 0) {
			kfree(dev_bonds);
			return -EIO;
		}	
		dev_bond++;
	}
	return 0;
}

static void __exit bonding_exit(void)
{
	struct net_device *dev_bond = dev_bonds;
	struct bonding *bond;
	int no;

	unregister_netdevice_notifier(&bond_netdev_notifier);
		 
	for (no = 0; no < max_bonds; no++) {

#ifdef CONFIG_PROC_FS
		bond = (struct bonding *) dev_bond->priv;
		remove_proc_entry("info", bond->bond_proc_dir);
		remove_proc_entry(dev_bond->name, proc_net);
#endif
		unregister_netdev(dev_bond);
		kfree(bond->stats);
		kfree(dev_bond->priv);
		
		dev_bond->priv = NULL;
		dev_bond++;
	}
	kfree(dev_bonds);
}

module_init(bonding_init);
module_exit(bonding_exit);
MODULE_LICENSE("GPL");

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
