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
 *
 * 2002/06/10 - Tony Cureington <tony.cureington * hp_com>
 *     - corrected uninitialized pointer (ifr.ifr_data) in bond_check_dev_link;
 *       actually changed function to use MIIPHY, then MIIREG, and finally
 *       ETHTOOL to determine the link status
 *     - fixed bad ifr_data pointer assignments in bond_ioctl
 *     - corrected mode 1 being reported as active-backup in bond_get_info;
 *       also added text to distinguish type of load balancing (rr or xor)
 *     - change arp_ip_target module param from "1-12s" (array of 12 ptrs)
 *       to "s" (a single ptr)
 *
 * 2002/08/30 - Jay Vosburgh <fubar at us dot ibm dot com>
 *     - Removed acquisition of xmit_lock in set_multicast_list; caused
 *       deadlock on SMP (lock is held by caller).
 *     - Revamped SIOCGMIIPHY, SIOCGMIIREG portion of bond_check_dev_link().
 *
 * 2002/09/18 - Jay Vosburgh <fubar at us dot ibm dot com>
 *     - Fixed up bond_check_dev_link() (and callers): removed some magic
 *	 numbers, banished local MII_ defines, wrapped ioctl calls to
 *	 prevent EFAULT errors
 *
 * 2002/9/30 - Jay Vosburgh <fubar at us dot ibm dot com>
 *     - make sure the ip target matches the arp_target before saving the
 *	 hw address.
 *
 * 2002/9/30 - Dan Eisner <eisner at 2robots dot com>
 *     - make sure my_ip is set before taking down the link, since
 *	 not all switches respond if the source ip is not set.
 *
 * 2002/10/8 - Janice Girouard <girouard at us dot ibm dot com>
 *     - read in the local ip address when enslaving a device
 *     - add primary support
 *     - make sure 2*arp_interval has passed when a new device
 *       is brought on-line before taking it down.
 *
 * 2002/09/11 - Philippe De Muyter <phdm at macqel dot be>
 *     - Added bond_xmit_broadcast logic.
 *     - Added bond_mode() support function.
 *
 * 2002/10/26 - Laurent Deniel <laurent.deniel at free.fr>
 *     - allow to register multicast addresses only on active slave
 *       (useful in active-backup mode)
 *     - add multicast module parameter
 *     - fix deletion of multicast groups after unloading module
 *
 * 2002/11/06 - Kameshwara Rayaprolu <kameshwara.rao * wipro_com>
 *     - Changes to prevent panic from closing the device twice; if we close 
 *       the device in bond_release, we must set the original_flags to down 
 *       so it won't be closed again by the network layer.
 *
 * 2002/11/07 - Tony Cureington <tony.cureington * hp_com>
 *     - Fix arp_target_hw_addr memory leak
 *     - Created activebackup_arp_monitor function to handle arp monitoring 
 *       in active backup mode - the bond_arp_monitor had several problems... 
 *       such as allowing slaves to tx arps sequentially without any delay 
 *       for a response
 *     - Renamed bond_arp_monitor to loadbalance_arp_monitor and re-wrote
 *       this function to just handle arp monitoring in load-balancing mode;
 *       it is a lot more compact now
 *     - Changes to ensure one and only one slave transmits in active-backup 
 *       mode
 *     - Robustesize parameters; warn users about bad combinations of 
 *       parameters; also if miimon is specified and a network driver does 
 *       not support MII or ETHTOOL, inform the user of this
 *     - Changes to support link_failure_count when in arp monitoring mode
 *     - Fix up/down delay reported in /proc
 *     - Added version; log version; make version available from "modinfo -d"
 *     - Fixed problem in bond_check_dev_link - if the first IOCTL (SIOCGMIIPH)
 *	 failed, the ETHTOOL ioctl never got a chance
 *
 * 2002/11/16 - Laurent Deniel <laurent.deniel at free.fr>
 *     - fix multicast handling in activebackup_arp_monitor
 *     - remove one unnecessary and confusing current_slave == slave test 
 *	 in activebackup_arp_monitor
 *
 *  2002/11/17 - Laurent Deniel <laurent.deniel at free.fr>
 *     - fix bond_slave_info_query when slave_id = num_slaves
 *
 *  2002/11/19 - Janice Girouard <girouard at us dot ibm dot com>
 *     - correct ifr_data reference.  Update ifr_data reference
 *       to mii_ioctl_data struct values to avoid confusion.
 *
 *  2002/11/22 - Bert Barbe <bert.barbe at oracle dot com>
 *      - Add support for multiple arp_ip_target
 *
 *  2002/12/13 - Jay Vosburgh <fubar at us dot ibm dot com>
 *	- Changed to allow text strings for mode and multicast, e.g.,
 *	  insmod bonding mode=active-backup.  The numbers still work.
 *	  One change: an invalid choice will cause module load failure,
 *	  rather than the previous behavior of just picking one.
 *	- Minor cleanups; got rid of dup ctype stuff, atoi function
 * 
 * 2003/02/07 - Jay Vosburgh <fubar at us dot ibm dot com>
 *	- Added use_carrier module parameter that causes miimon to
 *	  use netif_carrier_ok() test instead of MII/ETHTOOL ioctls.
 *	- Minor cleanups; consolidated ioctl calls to one function.
 *
 * 2003/02/07 - Tony Cureington <tony.cureington * hp_com>
 *	- Fix bond_mii_monitor() logic error that could result in
 *	  bonding round-robin mode ignoring links after failover/recovery
 *
 * 2003/03/17 - Jay Vosburgh <fubar at us dot ibm dot com>
 *	- kmalloc fix (GFP_KERNEL to GFP_ATOMIC) reported by
 *	  Shmulik dot Hen at intel.com.
 *	- Based on discussion on mailing list, changed use of
 *	  update_slave_cnt(), created wrapper functions for adding/removing
 *	  slaves, changed bond_xmit_xor() to check slave_cnt instead of
 *	  checking slave and slave->dev (which only worked by accident).
 *	- Misc code cleanup: get arp_send() prototype from header file,
 *	  add max_bonds to bonding.txt.
 *
 * 2003/03/18 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Make sure only bond_attach_slave() and bond_detach_slave() can
 *	  manipulate the slave list, including slave_cnt, even when in
 *	  bond_release_all().
 *	- Fixed hang in bond_release() while traffic is running.
 *	  netdev_set_master() must not be called from within the bond lock.
 *
 * 2003/03/18 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Fixed hang in bond_enslave(): netdev_set_master() must not be
 *	  called from within the bond lock while traffic is running.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/socket.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/if_bonding.h>
#include <linux/smp.h>
#include <linux/if_ether.h>
#include <net/arp.h>
#include <linux/mii.h>
#include <linux/ethtool.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#define DRV_VERSION		"2.5.65-20030320"
#define DRV_RELDATE		"March 20, 2003"
#define DRV_NAME		"bonding"
#define DRV_DESCRIPTION		"Ethernet Channel Bonding Driver"

static const char *version =
DRV_NAME ".c:v" DRV_VERSION " (" DRV_RELDATE ")\n";

/* monitor all links that often (in milliseconds). <=0 disables monitoring */
#ifndef BOND_LINK_MON_INTERV
#define BOND_LINK_MON_INTERV	0
#endif

#ifndef BOND_LINK_ARP_INTERV
#define BOND_LINK_ARP_INTERV	0
#endif

#ifndef MAX_ARP_IP_TARGETS
#define MAX_ARP_IP_TARGETS 16
#endif

static int arp_interval = BOND_LINK_ARP_INTERV;
static char *arp_ip_target[MAX_ARP_IP_TARGETS] = { NULL, };
static unsigned long arp_target[MAX_ARP_IP_TARGETS] = { 0, } ;
static int arp_ip_count = 0;
static u32 my_ip = 0;
char *arp_target_hw_addr = NULL;

static char *primary= NULL;

static int max_bonds	= BOND_DEFAULT_MAX_BONDS;
static int miimon	= BOND_LINK_MON_INTERV;
static int use_carrier	= 1;
static int bond_mode	= BOND_MODE_ROUNDROBIN;
static int updelay	= 0;
static int downdelay	= 0;

static char *mode	= NULL;

static struct bond_parm_tbl bond_mode_tbl[] = {
{	"balance-rr",		BOND_MODE_ROUNDROBIN},
{	"active-backup",	BOND_MODE_ACTIVEBACKUP},
{	"balance-xor",		BOND_MODE_XOR},
{	"broadcast",		BOND_MODE_BROADCAST},
{	NULL,			-1},
};

static int multicast_mode	= BOND_MULTICAST_ALL;
static char *multicast		= NULL;

static struct bond_parm_tbl bond_mc_tbl[] = {
{	"disabled",		BOND_MULTICAST_DISABLED},
{	"active",		BOND_MULTICAST_ACTIVE},
{	"all",			BOND_MULTICAST_ALL},
{	NULL,			-1},
};

static int first_pass	= 1;
static struct bonding *these_bonds =  NULL;
static struct net_device *dev_bonds = NULL;

MODULE_PARM(max_bonds, "i");
MODULE_PARM_DESC(max_bonds, "Max number of bonded devices");
MODULE_PARM(miimon, "i");
MODULE_PARM_DESC(miimon, "Link check interval in milliseconds");
MODULE_PARM(use_carrier, "i");
MODULE_PARM_DESC(use_carrier, "Use netif_carrier_ok (vs MII ioctls) in miimon; 09 for off, 1 for on (default)");
MODULE_PARM(mode, "s");
MODULE_PARM_DESC(mode, "Mode of operation : 0 for round robin, 1 for active-backup, 2 for xor");
MODULE_PARM(arp_interval, "i");
MODULE_PARM_DESC(arp_interval, "arp interval in milliseconds");
MODULE_PARM(arp_ip_target, "1-" __MODULE_STRING(MAX_ARP_IP_TARGETS) "s");
MODULE_PARM_DESC(arp_ip_target, "arp targets in n.n.n.n form");
MODULE_PARM(updelay, "i");
MODULE_PARM_DESC(updelay, "Delay before considering link up, in milliseconds");
MODULE_PARM(downdelay, "i");
MODULE_PARM_DESC(downdelay, "Delay before considering link down, in milliseconds");
MODULE_PARM(primary, "s");
MODULE_PARM_DESC(primary, "Primary network device to use");
MODULE_PARM(multicast, "s");
MODULE_PARM_DESC(multicast, "Mode for multicast support : 0 for none, 1 for active slave, 2 for all slaves (default)");

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *dev);
static int bond_xmit_xor(struct sk_buff *skb, struct net_device *dev);
static int bond_xmit_activebackup(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *bond_get_stats(struct net_device *dev);
static void bond_mii_monitor(struct net_device *dev);
static void loadbalance_arp_monitor(struct net_device *dev);
static void activebackup_arp_monitor(struct net_device *dev);
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
static void bond_mc_update(bonding_t *bond, slave_t *new, slave_t *old);
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
  
static void arp_send_all(slave_t *slave)
{	
	int i; 

	for (i = 0; (i<MAX_ARP_IP_TARGETS) && arp_target[i]; i++) { 
		arp_send(ARPOP_REQUEST, ETH_P_ARP, arp_target[i], slave->dev, 
			 my_ip, arp_target_hw_addr, slave->dev->dev_addr,
			 arp_target_hw_addr); 
	} 
}
 

static const char *
bond_mode_name(void)
{
	switch (bond_mode) {
	case BOND_MODE_ROUNDROBIN :
		return "load balancing (round-robin)";
	case BOND_MODE_ACTIVEBACKUP :
		return "fault-tolerance (active-backup)";
	case BOND_MODE_XOR :
		return "load balancing (xor)";
	case BOND_MODE_BROADCAST :
		return "fault-tolerance (broadcast)";
	default :
		return "unknown";
	}
}

static const char *
multicast_mode_name(void)
{
	switch(multicast_mode) {
	case BOND_MULTICAST_DISABLED :
		return "disabled";
	case BOND_MULTICAST_ACTIVE :
		return "active slave only";
	case BOND_MULTICAST_ALL :
		return "all slaves";
	default :
		return "unknown";
	}
}

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
 * This function counts and verifies the the number of attached
 * slaves, checking the count against the expected value (given that incr
 * is either 1 or -1, for add or removal of a slave).  Only
 * bond_xmit_xor() uses the slave_cnt value, but this is still a good
 * consistency check.
 */
static inline void
update_slave_cnt(bonding_t *bond, int incr)
{
	slave_t *slave = NULL;
	int expect = bond->slave_cnt + incr;

	bond->slave_cnt = 0;
	for (slave = bond->prev; slave != (slave_t*)bond;
	     slave = slave->prev) {
		bond->slave_cnt++;
	}

	if (expect != bond->slave_cnt)
		BUG();
}

/* 
 * This function detaches the slave <slave> from the list <bond>.
 * WARNING: no check is made to verify if the slave effectively
 * belongs to <bond>. It returns <slave> in case it's needed.
 * Nothing is freed on return, structures are just unchained.
 * If the bond->current_slave pointer was pointing to <slave>,
 * it's replaced with slave->next, or <bond> if not applicable.
 *
 * bond->lock held by caller.
 */
static slave_t *
bond_detach_slave(bonding_t *bond, slave_t *slave)
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
	} else {
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

	update_slave_cnt(bond, -1);

	return slave;
}

static void
bond_attach_slave(struct bonding *bond, struct slave *new_slave)
{
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

	update_slave_cnt(bond, 1);
}


/*
 * Less bad way to call ioctl from within the kernel; this needs to be
 * done some other way to get the call out of interrupt context.
 * Needs "ioctl" variable to be supplied by calling context.
 */
#define IOCTL(dev, arg, cmd) ({		\
	int ret;			\
	mm_segment_t fs = get_fs();	\
	set_fs(get_ds());		\
	ret = ioctl(dev, arg, cmd);	\
	set_fs(fs);			\
	ret; })

/* 
 * if <dev> supports MII link status reporting, check its link status.
 *
 * We either do MII/ETHTOOL ioctls, or check netif_carrier_ok(),
 * depening upon the setting of the use_carrier parameter.
 *
 * Return either BMSR_LSTATUS, meaning that the link is up (or we
 * can't tell and just pretend it is), or 0, meaning that the link is
 * down.
 *
 * If reporting is non-zero, instead of faking link up, return -1 if
 * both ETHTOOL and MII ioctls fail (meaning the device does not
 * support them).  If use_carrier is set, return whatever it says.
 * It'd be nice if there was a good way to tell if a driver supports
 * netif_carrier, but there really isn't.
 */
static int
bond_check_dev_link(struct net_device *dev, int reporting)
{
	static int (* ioctl)(struct net_device *, struct ifreq *, int);
	struct ifreq ifr;
	struct mii_ioctl_data *mii;
	struct ethtool_value etool;

	if (use_carrier) {
		return netif_carrier_ok(dev) ? BMSR_LSTATUS : 0;
	}

	ioctl = dev->do_ioctl;
	if (ioctl) {
		/* TODO: set pointer to correct ioctl on a per team member */
		/*       bases to make this more efficient. that is, once  */
		/*       we determine the correct ioctl, we will always    */
		/*       call it and not the others for that team          */
		/*       member.                                           */

		/*
		 * We cannot assume that SIOCGMIIPHY will also read a
		 * register; not all network drivers (e.g., e100)
		 * support that.
		 */

		/* Yes, the mii is overlaid on the ifreq.ifr_ifru */
		mii = (struct mii_ioctl_data *)&ifr.ifr_data;
		if (IOCTL(dev, &ifr, SIOCGMIIPHY) == 0) {
			mii->reg_num = MII_BMSR;
			if (IOCTL(dev, &ifr, SIOCGMIIREG) == 0) {
				return mii->val_out & BMSR_LSTATUS;
			}
		}

		/* try SIOCETHTOOL ioctl, some drivers cache ETHTOOL_GLINK */
		/* for a period of time so we attempt to get link status   */
		/* from it last if the above MII ioctls fail...            */
	        etool.cmd = ETHTOOL_GLINK;
	        ifr.ifr_data = (char*)&etool;
		if (IOCTL(dev, &ifr, SIOCETHTOOL) == 0) {
			if (etool.data == 1) {
				return BMSR_LSTATUS;
			} else { 
#ifdef BONDING_DEBUG
				printk(KERN_INFO 
					":: SIOCETHTOOL shows link down \n");
#endif
				return 0;
			} 
		}

	}
 
	/*
	 * If reporting, report that either there's no dev->do_ioctl,
	 * or both SIOCGMIIREG and SIOCETHTOOL failed (meaning that we
	 * cannot report link status).  If not reporting, pretend
	 * we're ok.
	 */
	return reporting ? -1 : BMSR_LSTATUS;
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

	return (has_active_interface ? BMSR_LSTATUS : 0);
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
		if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
			arp_timer->function = (void *)&activebackup_arp_monitor;
		} else {
			arp_timer->function = (void *)&loadbalance_arp_monitor;
		}
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
                if (arp_target_hw_addr != NULL) {
			kfree(arp_target_hw_addr); 
			arp_target_hw_addr = NULL;
		}
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
	switch (multicast_mode) {
	case BOND_MULTICAST_ACTIVE :
		/* write lock already acquired */
		if (bond->current_slave != NULL)
			dev_mc_add(bond->current_slave->dev, addr, alen, 0);
		break;
	case BOND_MULTICAST_ALL :
		for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev)
			dev_mc_add(slave->dev, addr, alen, 0);
		break;
	case BOND_MULTICAST_DISABLED :
		break;
	}
} 

/*
 * Remove a multicast address from every slave in the bonding group
 */
static void bond_mc_delete(bonding_t *bond, void *addr, int alen)
{ 
	slave_t *slave; 
	switch (multicast_mode) {
	case BOND_MULTICAST_ACTIVE :
		/* write lock already acquired */
		if (bond->current_slave != NULL)
			dev_mc_delete(bond->current_slave->dev, addr, alen, 0);
		break;
	case BOND_MULTICAST_ALL :
		for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev)
			dev_mc_delete(slave->dev, addr, alen, 0);
		break;
	case BOND_MULTICAST_DISABLED :
		break;
	}
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
	switch (multicast_mode) {
	case BOND_MULTICAST_ACTIVE :
		/* write lock already acquired */
		if (bond->current_slave != NULL)
			dev_set_promiscuity(bond->current_slave->dev, inc);
		break;
	case BOND_MULTICAST_ALL :
		for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev)
			dev_set_promiscuity(slave->dev, inc);
		break;
	case BOND_MULTICAST_DISABLED :
		break;
	}
} 

/*
 * Push the allmulti flag down to all slaves
 */
static void bond_set_allmulti(bonding_t *bond, int inc)
{ 
	slave_t *slave; 
	switch (multicast_mode) {
	case BOND_MULTICAST_ACTIVE : 
		/* write lock already acquired */
		if (bond->current_slave != NULL)
			dev_set_allmulti(bond->current_slave->dev, inc);
		break;
	case BOND_MULTICAST_ALL :
		for (slave = bond->prev; slave != (slave_t*)bond; slave = slave->prev)
			dev_set_allmulti(slave->dev, inc);
		break;
	case BOND_MULTICAST_DISABLED :
		break;
	}
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

	if (multicast_mode == BOND_MULTICAST_DISABLED)
		return;
	/*
	 * Lock the private data for the master
	 */
	write_lock_irqsave(&bond->lock, flags);

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
	bond_mc_list_copy (master->mc_list, bond, GFP_ATOMIC);

	write_unlock_irqrestore(&bond->lock, flags);
}

/*
 * Update the mc list and multicast-related flags for the new and 
 * old active slaves (if any) according to the multicast mode
 */
static void bond_mc_update(bonding_t *bond, slave_t *new, slave_t *old)
{
	struct dev_mc_list *dmi;

	switch(multicast_mode) {
	case BOND_MULTICAST_ACTIVE :		
		if (bond->device->flags & IFF_PROMISC) {
			if (old != NULL && new != old)
				dev_set_promiscuity(old->dev, -1);
			dev_set_promiscuity(new->dev, 1);
		}
		if (bond->device->flags & IFF_ALLMULTI) {
			if (old != NULL && new != old)
				dev_set_allmulti(old->dev, -1);
			dev_set_allmulti(new->dev, 1);
		}
		/* first remove all mc addresses from old slave if any,
		   and _then_ add them to new active slave */
		if (old != NULL && new != old) {
			for (dmi = bond->device->mc_list; dmi != NULL; dmi = dmi->next)
				dev_mc_delete(old->dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		}
		for (dmi = bond->device->mc_list; dmi != NULL; dmi = dmi->next)
			dev_mc_add(new->dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		break;
	case BOND_MULTICAST_ALL :
		/* nothing to do: mc list is already up-to-date on all slaves */
		break;
	case BOND_MULTICAST_DISABLED :
		break;
	}
}

/* enslave device <slave> to bond device <master> */
static int bond_enslave(struct net_device *master_dev, 
                        struct net_device *slave_dev)
{
	bonding_t *bond = NULL;
	slave_t *new_slave = NULL;
	unsigned long flags = 0;
	unsigned long rflags = 0;
	int ndx = 0;
	int err = 0;
	struct dev_mc_list *dmi;
	struct in_ifaddr **ifap;
	struct in_ifaddr *ifa;
	int link_reporting;

	if (master_dev == NULL || slave_dev == NULL) {
		return -ENODEV;
	}
	bond = (struct bonding *) master_dev->priv;

	if (slave_dev->do_ioctl == NULL) {
		printk(KERN_DEBUG
			"Warning : no link monitoring support for %s\n",
			slave_dev->name);
	}

	/* not running. */
	if ((slave_dev->flags & IFF_UP) != IFF_UP) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error, slave_dev is not running\n");
#endif
		return -EINVAL;
	}

	/* already enslaved */
	if (master_dev->flags & IFF_SLAVE || slave_dev->flags & IFF_SLAVE) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error, Device was already enslaved\n");
#endif
		return -EBUSY;
	}
		   
	if ((new_slave = kmalloc(sizeof(slave_t), GFP_ATOMIC)) == NULL) {
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
		goto err_free;
	}

	new_slave->dev = slave_dev;

	if (multicast_mode == BOND_MULTICAST_ALL) {
		/* set promiscuity level to new slave */ 
		if (master_dev->flags & IFF_PROMISC)
			dev_set_promiscuity(slave_dev, 1); 
 
		/* set allmulti level to new slave */
		if (master_dev->flags & IFF_ALLMULTI) 
			dev_set_allmulti(slave_dev, 1); 
		
		/* upload master's mc_list to new slave */ 
		for (dmi = master_dev->mc_list; dmi != NULL; dmi = dmi->next) 
			dev_mc_add (slave_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
	}

	write_lock_irqsave(&bond->lock, flags);
	
	bond_attach_slave(bond, new_slave);
	new_slave->delay = 0;
	new_slave->link_failure_count = 0;

	if (miimon > 0 && !use_carrier) {
		link_reporting = bond_check_dev_link(slave_dev, 1);

		if ((link_reporting == -1) && (arp_interval == 0)) {
			/*
			 * miimon is set but a bonded network driver
			 * does not support ETHTOOL/MII and
			 * arp_interval is not set.  Note: if
			 * use_carrier is enabled, we will never go
			 * here (because netif_carrier is always
			 * supported); thus, we don't need to change
			 * the messages for netif_carrier.
			 */ 
			printk(KERN_ERR
				"bond_enslave(): MII and ETHTOOL support not "
				"available for interface %s, and "
				"arp_interval/arp_ip_target module parameters "
		       		"not specified, thus bonding will not detect "
				"link failures! see bonding.txt for details.\n",
		       		slave_dev->name);
		} else if (link_reporting == -1) {
			/* unable  get link status using mii/ethtool */
			printk(KERN_WARNING 
			       "bond_enslave: can't get link status from "
			       "interface %s; the network driver associated "
			       "with this interface does not support "
			       "MII or ETHTOOL link status reporting, thus "
			       "miimon has no effect on this interface.\n", 
			       slave_dev->name);
		}
	}

	/* check for initial state */
	if ((miimon <= 0) ||
	    (bond_check_dev_link(slave_dev, 0) == BMSR_LSTATUS)) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Initial state of slave_dev is BOND_LINK_UP\n");
#endif
		new_slave->link  = BOND_LINK_UP;
		new_slave->jiffies = jiffies;
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
	if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
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
			bond_mc_update(bond, new_slave, NULL);
		}
		else {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "This is just a backup slave\n");
#endif
			bond_set_slave_inactive_flags(new_slave);
		}
		read_lock_irqsave(&(((struct in_device *)slave_dev->ip_ptr)->lock), rflags);
		ifap= &(((struct in_device *)slave_dev->ip_ptr)->ifa_list);
		ifa = *ifap;
		my_ip = ifa->ifa_address;
		read_unlock_irqrestore(&(((struct in_device *)slave_dev->ip_ptr)->lock), rflags);

		/* if there is a primary slave, remember it */
		if (primary != NULL)
		  if( strcmp(primary, new_slave->dev->name) == 0) 
			bond->primary_slave = new_slave;
	} else {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "This slave is always active in trunk mode\n");
#endif
		/* always active in trunk mode */
		new_slave->state = BOND_STATE_ACTIVE;
		if (bond->current_slave == NULL) 
			bond->current_slave = new_slave;
	}

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

	/* enslave is successful */
	return 0;
err_free:
	kfree(new_slave);
	return err;
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
		bond_mc_update(bond, newactive, oldactive);
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
				(bond_mode == BOND_MODE_ACTIVEBACKUP) ? "backup":"other");
			write_lock(&bond->ptrlock);
			bond->current_slave = (slave_t *)NULL;
			write_unlock(&bond->ptrlock);
			return NULL; /* still no slave, return NULL */
		}
	} else if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
		/* make sure oldslave doesn't send arps - this could
		 * cause a ping-pong effect between interfaces since they
		 * would be able to tx arps - in active backup only one
		 * slave should be able to tx arps, and that should be
		 * the current_slave; the only exception is when all
		 * slaves have gone down, then only one non-current slave can
		 * send arps at a time; clearing oldslaves' mc list is handled
		 * later in this function.
		 */
		bond_set_slave_inactive_flags(oldslave);
	}

	mintime = updelay;

	/* first try the primary link; if arping, a link must tx/rx traffic 
	 * before it can be considered the current_slave - also, we would skip 
	 * slaves between the current_slave and primary_slave that may be up 
	 * and able to arp
	 */
	if ((bond->primary_slave != NULL) && (arp_interval == 0)) {
		if (IS_UP(bond->primary_slave->dev)) 
			newslave = bond->primary_slave;
	}

	do {
		if (IS_UP(newslave->dev)) {
			if (newslave->link == BOND_LINK_UP) {
				/* this one is immediately usable */
				if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
					bond_set_slave_active_flags(newslave);
					bond_mc_update(bond, newslave, oldslave);
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
		bestslave->jiffies = jiffies;
		bond_set_slave_active_flags(bestslave);
		bond_mc_update(bond, bestslave, oldslave);
		write_lock(&bond->ptrlock);
		bond->current_slave = bestslave;
		write_unlock(&bond->ptrlock);
		return bestslave;
	}

	if ((bond_mode == BOND_MODE_ACTIVEBACKUP) &&
	    (multicast_mode == BOND_MULTICAST_ACTIVE) &&
	    (oldslave != NULL)) {
		/* flush bonds (master's) mc_list from oldslave since it wasn't
		 * updated (and deleted) above
		 */ 
		bond_mc_list_flush(oldslave->dev, bond->device); 
		if (bond->device->flags & IFF_PROMISC) {
			dev_set_promiscuity(oldslave->dev, -1);
		}
		if (bond->device->flags & IFF_ALLMULTI) {
			dev_set_allmulti(oldslave->dev, -1);
		}
	}

	printk (" but could not find any %s interface.\n",
		(bond_mode == BOND_MODE_ACTIVEBACKUP) ? "backup":"other");
	
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

	/* master already enslaved, or slave not enslaved,
	   or no slave for this master */
	if ((master->flags & IFF_SLAVE) || !(slave->flags & IFF_SLAVE)) {
		printk (KERN_DEBUG "%s: cannot release %s.\n", master->name, slave->name);
		return -EINVAL;
	}

	write_lock_irqsave(&bond->lock, flags);
	bond->current_arp_slave = NULL;
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
			
			if (bond->current_slave == NULL) {
				printk(KERN_INFO
					"%s: now running without any active interface !\n",
					master->name);
			}

			if (bond->primary_slave == our_slave) {
				bond->primary_slave = NULL;
			}

			break;
		}

	}
	write_unlock_irqrestore(&bond->lock, flags);
	
	if (our_slave == (slave_t *)bond) {
		/* if we get here, it's because the device was not found */
		printk (KERN_INFO "%s: %s not enslaved\n", master->name, slave->name);
		return -EINVAL;
	}

	/* undo settings and restore original values */
	
	if (multicast_mode == BOND_MULTICAST_ALL) {
		/* flush master's mc_list from slave */ 
		bond_mc_list_flush (slave, master); 

		/* unset promiscuity level from slave */
		if (master->flags & IFF_PROMISC) 
			dev_set_promiscuity(slave, -1); 

		/* unset allmulti level from slave */ 
		if (master->flags & IFF_ALLMULTI)
			dev_set_allmulti(slave, -1); 
	}

	netdev_set_master(slave, NULL);

	/* only restore its RUNNING flag if monitoring set it down */
	if (slave->flags & IFF_UP) {
		slave->flags |= IFF_RUNNING;
	}

	if (slave->flags & IFF_NOARP || 
		bond->current_slave != NULL) {
			dev_close(slave);
			our_slave->original_flags &= ~IFF_UP;
	}

	bond_restore_slave_flags(our_slave);
	
	kfree(our_slave);

	return 0;  /* deletion OK */
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
	bond->current_arp_slave = NULL;
	bond->current_slave = NULL;
	bond->primary_slave = NULL;

	while ((our_slave = bond->prev) != (slave_t *)bond) {
		slave_dev = our_slave->dev;
		bond_detach_slave(bond, our_slave);

		if (multicast_mode == BOND_MULTICAST_ALL 
		    || (multicast_mode == BOND_MULTICAST_ACTIVE 
			&& bond->current_slave == our_slave)) {

			/* flush master's mc_list from slave */ 
			bond_mc_list_flush (slave_dev, master); 
			
			/* unset promiscuity level from slave */
			if (master->flags & IFF_PROMISC) 
				dev_set_promiscuity(slave_dev, -1); 
			
			/* unset allmulti level from slave */ 
			if (master->flags & IFF_ALLMULTI)
				dev_set_allmulti(slave_dev, -1); 
		}

		kfree(our_slave);

		/*
		 * Can be safely called from inside the bond lock
		 * since traffic and timers have already stopped
		 */
		netdev_set_master(slave_dev, NULL);

		/* only restore its RUNNING flag if monitoring set it down */
		if (slave_dev->flags & IFF_UP)
			slave_dev->flags |= IFF_RUNNING;

		if (slave_dev->flags & IFF_NOARP)
			dev_close(slave_dev);
	}

	printk (KERN_INFO "%s: released all slaves\n", master->name);

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
		int link_state;
		
		link_state = bond_check_dev_link(dev, 0);

		switch (slave->link) {
		case BOND_LINK_UP:	/* the link was up */
			if (link_state == BMSR_LSTATUS) {
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
						? ((bond_mode == BOND_MODE_ACTIVEBACKUP)
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
			if (link_state != BMSR_LSTATUS) {
				/* link stays down */
				if (slave->delay <= 0) {
					/* link down for too long time */
					slave->link = BOND_LINK_DOWN;
					/* in active/backup mode, we must
					   completely disable this interface */
					if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
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
			} else {
				/* link up again */
				slave->link  = BOND_LINK_UP;
				slave->jiffies = jiffies;
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
			if (link_state != BMSR_LSTATUS) {
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
			if (link_state != BMSR_LSTATUS) {
				/* link down again */
				slave->link  = BOND_LINK_DOWN;
				printk(KERN_INFO
					"%s: link status down again after %d ms "
					"for interface %s.\n",
					master->name,
					(updelay - slave->delay) * miimon,
					dev->name);
			} else {
				/* link stays up */
				if (slave->delay == 0) {
					/* now the link has been up for long time enough */
					slave->link = BOND_LINK_UP;
					slave->jiffies = jiffies;

					if (bond_mode != BOND_MODE_ACTIVEBACKUP) {
						/* make it immediately active */
						slave->state = BOND_STATE_ACTIVE;
					} else if (slave != bond->primary_slave) {
						/* prevent it from being the active one */
						slave->state = BOND_STATE_BACKUP;
					}

					printk(KERN_INFO
						"%s: link status definitely up "
						"for interface %s.\n",
						master->name,
						dev->name);
	
					if ( (bond->primary_slave != NULL)	 
					  && (slave == bond->primary_slave) )
						change_active_interface(bond); 
				}
				else
					slave->delay--;
				
				/* we'll also look for the mostly eligible slave */
				if (bond->primary_slave == NULL)  {
				    if (IS_UP(dev) && (slave->delay < mindelay)) {
					mindelay = slave->delay;
					bestslave = slave;
				    } 
				} else if ( (IS_UP(bond->primary_slave->dev))  || 
				          ( (!IS_UP(bond->primary_slave->dev))  && 
				          (IS_UP(dev) && (slave->delay < mindelay)) ) ) {
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

	/* no active interface at the moment or need to bring up the primary */
	if (oldcurrent == NULL)  { /* no active interface at the moment */
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
				bestslave->jiffies = jiffies;
			}

			if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
				bond_set_slave_active_flags(bestslave);
				bond_mc_update(bond, bestslave, NULL);
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
 * ensuring that traffic is being sent and received when arp monitoring
 * is used in load-balancing mode. if the adapter has been dormant, then an 
 * arp is transmitted to generate traffic. see activebackup_arp_monitor for 
 * arp monitoring in active backup mode. 
 */
static void loadbalance_arp_monitor(struct net_device *master)
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

	/* TODO: investigate why rtnl_shlock_nowait and rtnl_exlock_nowait
	 * are called below and add comment why they are required... 
	 */
	if ((!IS_UP(master)) || rtnl_shlock_nowait()) {
		mod_timer(&bond->arp_timer, next_timer);
		read_unlock_irqrestore(&bond->lock, flags);
		return;
	}

	if (rtnl_exlock_nowait()) {
		rtnl_shunlock();
		mod_timer(&bond->arp_timer, next_timer);
		read_unlock_irqrestore(&bond->lock, flags);
		return;
	}

	/* see if any of the previous devices are up now (i.e. they have
	 * xmt and rcv traffic). the current_slave does not come into
	 * the picture unless it is null. also, slave->jiffies is not needed
	 * here because we send an arp on each slave and give a slave as
	 * long as it needs to get the tx/rx within the delta.
	 * TODO: what about up/down delay in arp mode? it wasn't here before
	 *       so it can wait 
	 */
	slave = (slave_t *)bond;
	while ((slave = slave->prev) != (slave_t *)bond)  {

	  	if (slave->link != BOND_LINK_UP) {

	  		if (((jiffies - slave->dev->trans_start) <= 
						the_delta_in_ticks) &&  
			     ((jiffies - slave->dev->last_rx) <= 
						the_delta_in_ticks)) {

				slave->link  = BOND_LINK_UP;
				slave->state = BOND_STATE_ACTIVE;

				/* primary_slave has no meaning in round-robin
				 * mode. the window of a slave being up and 
				 * current_slave being null after enslaving
				 * is closed.
				 */
				read_lock(&bond->ptrlock);
				if (bond->current_slave == NULL) {
					read_unlock(&bond->ptrlock);
					printk(KERN_INFO
						"%s: link status definitely up "
						"for interface %s, ",
						master->name,
						slave->dev->name);
					change_active_interface(bond); 
				} else {
					read_unlock(&bond->ptrlock);
					printk(KERN_INFO
						"%s: interface %s is now up\n",
						master->name,
						slave->dev->name);
				}
			} 
		} else {
			/* slave->link == BOND_LINK_UP */

			/* not all switches will respond to an arp request
			 * when the source ip is 0, so don't take the link down
			 * if we don't know our ip yet
			 */
			if (((jiffies - slave->dev->trans_start) >= 
		              (2*the_delta_in_ticks)) ||
		             (((jiffies - slave->dev->last_rx) >= 
		               (2*the_delta_in_ticks)) && my_ip !=0)) {
				slave->link  = BOND_LINK_DOWN;
				slave->state = BOND_STATE_BACKUP;
				if (slave->link_failure_count < UINT_MAX) {
					slave->link_failure_count++;
				}
				printk(KERN_INFO
				       "%s: interface %s is now down.\n",
				       master->name,
				       slave->dev->name);

				read_lock(&bond->ptrlock);
				if (slave == bond->current_slave) {
					read_unlock(&bond->ptrlock);
					change_active_interface(bond);
				} else {
					read_unlock(&bond->ptrlock);
				}
			}
		} 

		/* note: if switch is in round-robin mode, all links 
		 * must tx arp to ensure all links rx an arp - otherwise
		 * links may oscillate or not come up at all; if switch is 
		 * in something like xor mode, there is nothing we can 
		 * do - all replies will be rx'ed on same link causing slaves 
		 * to be unstable during low/no traffic periods
		 */
		if (IS_UP(slave->dev)) {
			arp_send_all(slave);
		}
	}

	rtnl_exunlock();
	rtnl_shunlock();
	read_unlock_irqrestore(&bond->lock, flags);

	/* re-arm the timer */
	mod_timer(&bond->arp_timer, next_timer);
}

/* 
 * When using arp monitoring in active-backup mode, this function is
 * called to determine if any backup slaves have went down or a new
 * current slave needs to be found.
 * The backup slaves never generate traffic, they are considered up by merely 
 * receiving traffic. If the current slave goes down, each backup slave will 
 * be given the opportunity to tx/rx an arp before being taken down - this 
 * prevents all slaves from being taken down due to the current slave not 
 * sending any traffic for the backups to receive. The arps are not necessarily
 * necessary, any tx and rx traffic will keep the current slave up. While any 
 * rx traffic will keep the backup slaves up, the current slave is responsible 
 * for generating traffic to keep them up regardless of any other traffic they 
 * may have received.
 * see loadbalance_arp_monitor for arp monitoring in load balancing mode
 */
static void activebackup_arp_monitor(struct net_device *master)
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
	        read_unlock_irqrestore(&bond->lock, flags);
		return;
	}

	/* determine if any slave has come up or any backup slave has 
	 * gone down 
	 * TODO: what about up/down delay in arp mode? it wasn't here before
	 *       so it can wait 
	 */
	slave = (slave_t *)bond;
	while ((slave = slave->prev) != (slave_t *)bond)  {

	  	if (slave->link != BOND_LINK_UP) {
			if ((jiffies - slave->dev->last_rx) <=
			    the_delta_in_ticks) {

				slave->link = BOND_LINK_UP;
				write_lock(&bond->ptrlock);
				if ((bond->current_slave == NULL) &&
				    ((jiffies - slave->dev->trans_start) <=
				     the_delta_in_ticks)) {
					bond->current_slave = slave;
					bond_set_slave_active_flags(slave);
			                bond_mc_update(bond, slave, NULL);
					bond->current_arp_slave = NULL;
				} else if (bond->current_slave != slave) {
					/* this slave has just come up but we 
					 * already have a current slave; this
					 * can also happen if bond_enslave adds
					 * a new slave that is up while we are 
					 * searching for a new slave
					 */
					bond_set_slave_inactive_flags(slave);
					bond->current_arp_slave = NULL;
				}

				if (slave == bond->current_slave) {
					printk(KERN_INFO
						"%s: %s is up and now the "
						"active interface\n",
						master->name,
						slave->dev->name);
				} else {
					printk(KERN_INFO
						"%s: backup interface %s is "
						"now up\n",
						master->name,
						slave->dev->name);
				}

				write_unlock(&bond->ptrlock);
			}
		} else {
			read_lock(&bond->ptrlock);
			if ((slave != bond->current_slave) &&
			    (bond->current_arp_slave == NULL) &&
			    (((jiffies - slave->dev->last_rx) >=
			     3*the_delta_in_ticks) && (my_ip != 0))) {
				/* a backup slave has gone down; three times 
				 * the delta allows the current slave to be 
				 * taken out before the backup slave.
				 * note: a non-null current_arp_slave indicates
				 * the current_slave went down and we are 
				 * searching for a new one; under this 
				 * condition we only take the current_slave 
				 * down - this gives each slave a chance to 
				 * tx/rx traffic before being taken out
				 */
				read_unlock(&bond->ptrlock);
				slave->link  = BOND_LINK_DOWN;
				if (slave->link_failure_count < UINT_MAX) {
					slave->link_failure_count++;
				}
				bond_set_slave_inactive_flags(slave);
				printk(KERN_INFO
					"%s: backup interface %s is now down\n",
					master->name,
					slave->dev->name);
			} else {
				read_unlock(&bond->ptrlock);
			}
		}
	}

	read_lock(&bond->ptrlock);
	slave = bond->current_slave;
	read_unlock(&bond->ptrlock);

	if (slave != NULL) {

		/* if we have sent traffic in the past 2*arp_intervals but
		 * haven't xmit and rx traffic in that time interval, select 
		 * a different slave. slave->jiffies is only updated when
		 * a slave first becomes the current_slave - not necessarily
		 * after every arp; this ensures the slave has a full 2*delta 
		 * before being taken out. if a primary is being used, check 
		 * if it is up and needs to take over as the current_slave
		 */
		if ((((jiffies - slave->dev->trans_start) >= 
		       (2*the_delta_in_ticks)) ||
		     (((jiffies - slave->dev->last_rx) >= 
		       (2*the_delta_in_ticks)) && (my_ip != 0))) &&
		    ((jiffies - slave->jiffies) >= 2*the_delta_in_ticks)) {

			slave->link  = BOND_LINK_DOWN;
			if (slave->link_failure_count < UINT_MAX) {
				slave->link_failure_count++;
			}
			printk(KERN_INFO "%s: link status down for "
					 "active interface %s, disabling it",
			       master->name,
			       slave->dev->name);
			slave = change_active_interface(bond);
			bond->current_arp_slave = slave;
			if (slave != NULL) {
				slave->jiffies = jiffies;
			}

		} else if ((bond->primary_slave != NULL) && 
			   (bond->primary_slave != slave) && 
			   (bond->primary_slave->link == BOND_LINK_UP)) {
			/* at this point, slave is the current_slave */
			printk(KERN_INFO 
			       "%s: changing from interface %s to primary "
			       "interface %s\n",
			       master->name, 
			       slave->dev->name, 
			       bond->primary_slave->dev->name);
			       
			/* primary is up so switch to it */
			bond_set_slave_inactive_flags(slave);
			bond_mc_update(bond, bond->primary_slave, slave);
			write_lock(&bond->ptrlock);
			bond->current_slave = bond->primary_slave;
			write_unlock(&bond->ptrlock);
			slave = bond->primary_slave;
			bond_set_slave_active_flags(slave);
			slave->jiffies = jiffies;
		} else {
			bond->current_arp_slave = NULL;
		}

		/* the current slave must tx an arp to ensure backup slaves
		 * rx traffic
		 */
		if ((slave != NULL) &&
		    (((jiffies - slave->dev->last_rx) >= the_delta_in_ticks) &&
		     (my_ip != 0))) {
		  arp_send_all(slave);
		}
	}

	/* if we don't have a current_slave, search for the next available 
	 * backup slave from the current_arp_slave and make it the candidate 
	 * for becoming the current_slave
	 */
	if (slave == NULL) { 

		if ((bond->current_arp_slave == NULL) ||
		    (bond->current_arp_slave == (slave_t *)bond)) {
			bond->current_arp_slave = bond->prev;
		} 

		if (bond->current_arp_slave != (slave_t *)bond) {
			bond_set_slave_inactive_flags(bond->current_arp_slave);
			slave = bond->current_arp_slave->next;

			/* search for next candidate */
			do {
				if (IS_UP(slave->dev)) {
					slave->link = BOND_LINK_BACK;
					bond_set_slave_active_flags(slave);
					arp_send_all(slave);
					slave->jiffies = jiffies;
					bond->current_arp_slave = slave;
					break;
				}

				/* if the link state is up at this point, we 
				 * mark it down - this can happen if we have 
				 * simultaneous link failures and 
				 * change_active_interface doesn't make this 
				 * one the current slave so it is still marked 
				 * up when it is actually down
				 */
				if (slave->link == BOND_LINK_UP) {
					slave->link  = BOND_LINK_DOWN;
					if (slave->link_failure_count < 
							UINT_MAX) {
						slave->link_failure_count++;
					}

					bond_set_slave_inactive_flags(slave);
					printk(KERN_INFO
						"%s: backup interface "
						"%s is now down.\n",
						master->name,
						slave->dev->name);
				}
			} while ((slave = slave->next) != 
					bond->current_arp_slave->next);
		}
	}

	mod_timer(&bond->arp_timer, next_timer);
	read_unlock_irqrestore(&bond->lock, flags);
}

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

	info->bond_mode = bond_mode;
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

	if (slave != (slave_t *)bond) {
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
	struct mii_ioctl_data *mii = NULL;
	int ret = 0;

#ifdef BONDING_DEBUG
	printk(KERN_INFO "bond_ioctl: master=%s, cmd=%d\n", 
		master_dev->name, cmd);
#endif

	switch (cmd) {
	case SIOCGMIIPHY:
		mii = (struct mii_ioctl_data *)&ifr->ifr_data;
		if (mii == NULL) {
			return -EINVAL;
		}
		mii->phy_id = 0;
		/* Fall Through */
	case SIOCGMIIREG:
		/* 
		 * We do this again just in case we were called by SIOCGMIIREG
		 * instead of SIOCGMIIPHY.
		 */
		mii = (struct mii_ioctl_data *)&ifr->ifr_data;
		if (mii == NULL) {
			return -EINVAL;
		}
		if (mii->reg_num == 1) {
			mii->val_out = bond_check_mii_link(
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
			if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
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

/* 
 * in broadcast mode, we send everything to all usable interfaces.
 */
static int bond_xmit_broadcast(struct sk_buff *skb, struct net_device *dev)
{
	slave_t *slave, *start_at;
	struct bonding *bond = (struct bonding *) dev->priv;
	unsigned long flags;
	struct net_device *device_we_should_send_to = 0;

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
		read_unlock_irqrestore(&bond->lock, flags);
		dev_kfree_skb(skb);
		return 0;
	}

	do {
		if (IS_UP(slave->dev)
		    && (slave->link == BOND_LINK_UP)
		    && (slave->state == BOND_STATE_ACTIVE)) {
			if (device_we_should_send_to) {
				struct sk_buff *skb2;
				if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL) {
					printk(KERN_ERR "bond_xmit_broadcast: skb_clone() failed\n");
					continue;
				}

				skb2->dev = device_we_should_send_to;
				skb2->priority = 1;
				dev_queue_xmit(skb2);
			}
			device_we_should_send_to = slave->dev;
		}
	} while ((slave = slave->next) != start_at);

	if (device_we_should_send_to) {
		skb->dev = device_we_should_send_to;
		skb->priority = 1;
		dev_queue_xmit(skb);
	} else
		dev_kfree_skb(skb);

	/* frame sent to all suitable interfaces */
	read_unlock_irqrestore(&bond->lock, flags);
	return 0;
}

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
 * the source and destination hw addresses.  If this device is not 
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
	if (bond->slave_cnt == 0) {
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
	 * the target hw address, save it so we don't need 
	 * to use a broadcast address.
	 * don't do this if in active backup mode because the slaves must 
	 * receive packets to stay up, and the only ones they receive are 
	 * broadcasts. 
	 */
	if ( (bond_mode != BOND_MODE_ACTIVEBACKUP) && 
             (arp_ip_count == 1) &&
	     (arp_interval > 0) && (arp_target_hw_addr == NULL) &&
	     (skb->protocol == __constant_htons(ETH_P_IP) ) ) {
		struct ethhdr *eth_hdr = 
			(struct ethhdr *) (((char *)skb->data));
		struct iphdr *ip_hdr = (struct iphdr *)(eth_hdr + 1);

		if (arp_target[0] == ip_hdr->daddr) {
			arp_target_hw_addr = kmalloc(ETH_ALEN, GFP_KERNEL);
			if (arp_target_hw_addr != NULL)
				memcpy(arp_target_hw_addr, eth_hdr->h_dest, ETH_ALEN);
		}
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

		len += sprintf(buf + len, "Bonding Mode: %s\n",
			       bond_mode_name());

		if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
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
				link == BMSR_LSTATUS ? "up\n" : "down\n");
		len += sprintf(buf + len, "MII Polling Interval (ms): %d\n", 
				miimon);
		len += sprintf(buf + len, "Up Delay (ms): %d\n", 
				updelay * miimon);
		len += sprintf(buf + len, "Down Delay (ms): %d\n", 
				downdelay * miimon);
		len += sprintf(buf + len, "Multicast Mode: %s\n",
			       multicast_mode_name());

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
	.notifier_call = bond_event,
};

static int __init bond_init(struct net_device *dev)
{
	bonding_t *bond, *this_bond, *last_bond;
	int count;

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
	bond->current_arp_slave = NULL;
	bond->device = dev;
	dev->priv = bond;

	/* Initialize the device structure. */
	switch (bond_mode) {
	case BOND_MODE_ACTIVEBACKUP:
		dev->hard_start_xmit = bond_xmit_activebackup;
		break;
	case BOND_MODE_ROUNDROBIN:
		dev->hard_start_xmit = bond_xmit_roundrobin;
		break;
	case BOND_MODE_XOR:
		dev->hard_start_xmit = bond_xmit_xor;
		break;
	case BOND_MODE_BROADCAST:
		dev->hard_start_xmit = bond_xmit_broadcast;
		break;
	default:
		printk(KERN_ERR "Unknown bonding mode %d\n", bond_mode);
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
	printk(", in %s mode.\n", bond_mode_name());

	printk(KERN_INFO "%s registered with", dev->name);
	if (arp_interval > 0) {
		printk(" ARP monitoring set to %d ms with %d target(s):", 
			arp_interval, arp_ip_count);
		for (count=0 ; count<arp_ip_count ; count++)
                        printk (" %s", arp_ip_target[count]);
		printk("\n");
	} else {
		printk("out ARP monitoring\n");
	}

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

/*
 * Convert string input module parms.  Accept either the
 * number of the mode or its string name.
 */
static inline int
bond_parse_parm(char *mode_arg, struct bond_parm_tbl *tbl)
{
	int i;

	for (i = 0; tbl[i].modename != NULL; i++) {
		if ((isdigit(*mode_arg) &&
		    tbl[i].mode == simple_strtol(mode_arg, NULL, 0)) ||
		    (0 == strncmp(mode_arg, tbl[i].modename,
				  strlen(tbl[i].modename)))) {
			return tbl[i].mode;
		}
	}

	return -1;
}


static int __init bonding_init(void)
{
	int no;
	int err;

	/* Find a name for this unit */
	static struct net_device *dev_bond = NULL;

	printk(KERN_INFO "%s", version);

	/*
	 * Convert string parameters.
	 */
	if (mode) {
		bond_mode = bond_parse_parm(mode, bond_mode_tbl);
		if (bond_mode == -1) {
			printk(KERN_WARNING
			       "bonding_init(): Invalid bonding mode \"%s\"\n",
			       mode == NULL ? "NULL" : mode);
			return -EINVAL;
		}
	}

	if (multicast) {
		multicast_mode = bond_parse_parm(multicast, bond_mc_tbl);
		if (multicast_mode == -1) {
			printk(KERN_WARNING 
		       "bonding_init(): Invalid multicast mode \"%s\"\n",
			       multicast == NULL ? "NULL" : multicast);
			return -EINVAL;
		}
	}

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

	if (miimon < 0) {
		printk(KERN_WARNING 
		       "bonding_init(): miimon module parameter (%d), "
		       "not in range 0-%d, so it was reset to %d\n",
		       miimon, INT_MAX, BOND_LINK_MON_INTERV);
		miimon = BOND_LINK_MON_INTERV;
	}

	if (updelay < 0) {
		printk(KERN_WARNING 
		       "bonding_init(): updelay module parameter (%d), "
		       "not in range 0-%d, so it was reset to 0\n",
		       updelay, INT_MAX);
		updelay = 0;
	}

	if (downdelay < 0) {
		printk(KERN_WARNING 
		       "bonding_init(): downdelay module parameter (%d), "
		       "not in range 0-%d, so it was reset to 0\n",
		       downdelay, INT_MAX);
		downdelay = 0;
	}

	if (miimon == 0) {
		if ((updelay != 0) || (downdelay != 0)) {
			/* just warn the user the up/down delay will have
			 * no effect since miimon is zero...
			 */
			printk(KERN_WARNING 
		               "bonding_init(): miimon module parameter not "
			       "set and updelay (%d) or downdelay (%d) module "
			       "parameter is set; updelay and downdelay have "
			       "no effect unless miimon is set\n",
		               updelay, downdelay);
		}
	} else {
		/* don't allow arp monitoring */
		if (arp_interval != 0) {
			printk(KERN_WARNING 
		               "bonding_init(): miimon (%d) and arp_interval "
			       "(%d) can't be used simultaneously, "
			       "disabling ARP monitoring\n",
		               miimon, arp_interval);
			arp_interval = 0;
		}

		if ((updelay % miimon) != 0) {
			/* updelay will be rounded in bond_init() when it
			 * is divided by miimon, we just inform user here
			 */
			printk(KERN_WARNING 
		               "bonding_init(): updelay (%d) is not a multiple "
			       "of miimon (%d), updelay rounded to %d ms\n",
		               updelay, miimon, (updelay / miimon) * miimon);
		}

		if ((downdelay % miimon) != 0) {
			/* downdelay will be rounded in bond_init() when it
			 * is divided by miimon, we just inform user here
			 */
			printk(KERN_WARNING 
		               "bonding_init(): downdelay (%d) is not a "
			       "multiple of miimon (%d), downdelay rounded "
			       "to %d ms\n",
		               downdelay, miimon, 
			       (downdelay / miimon) * miimon);
		}
	}

	if (arp_interval < 0) {
		printk(KERN_WARNING 
		       "bonding_init(): arp_interval module parameter (%d), "
		       "not in range 0-%d, so it was reset to %d\n",
		       arp_interval, INT_MAX, BOND_LINK_ARP_INTERV);
		arp_interval = BOND_LINK_ARP_INTERV;
	}

        for (arp_ip_count=0 ;
             (arp_ip_count < MAX_ARP_IP_TARGETS) && arp_ip_target[arp_ip_count];
              arp_ip_count++ ) {
                /* TODO: check and log bad ip address */
                if (my_inet_aton(arp_ip_target[arp_ip_count],
                                 &arp_target[arp_ip_count]) == 0) {
                        printk(KERN_WARNING
                               "bonding_init(): bad arp_ip_target module "
                               "parameter (%s), ARP monitoring will not be "
                               "performed\n",
                               arp_ip_target[arp_ip_count]);
                        arp_interval = 0;
		}
        }


	if ( (arp_interval > 0) && (arp_ip_count==0)) {
		/* don't allow arping if no arp_ip_target given... */
		printk(KERN_WARNING 
		       "bonding_init(): arp_interval module parameter "
		       "(%d) specified without providing an arp_ip_target "
		       "parameter, arp_interval was reset to 0\n",
		       arp_interval);
		arp_interval = 0;
	}

	if ((miimon == 0) && (arp_interval == 0)) {
		/* miimon and arp_interval not set, we need one so things
		 * work as expected, see bonding.txt for details
		 */
		printk(KERN_ERR 
		       "bonding_init(): either miimon or "
		       "arp_interval and arp_ip_target module parameters "
		       "must be specified, otherwise bonding will not detect "
		       "link failures! see bonding.txt for details.\n");
	}

	if ((primary != NULL) && (bond_mode != BOND_MODE_ACTIVEBACKUP)){
		/* currently, using a primary only makes sence 
		 * in active backup mode 
		 */
		printk(KERN_WARNING 
		       "bonding_init(): %s primary device specified but has "
		       " no effect in %s mode\n",
		       primary, bond_mode_name());
		primary = NULL;
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
MODULE_DESCRIPTION(DRV_DESCRIPTION ", v" DRV_VERSION);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
