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
 *	- Fixed hang in bond_release() with traffic running:
 *	  netdev_set_master() must not be called from within the bond lock.
 *
 * 2003/03/18 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Fixed hang in bond_enslave() with traffic running:
 *	  netdev_set_master() must not be called from within the bond lock.
 *
 * 2003/03/18 - Amir Noam <amir.noam at intel dot com>
 *	- Added support for getting slave's speed and duplex via ethtool.
 *	  Needed for 802.3ad and other future modes.
 *
 * 2003/03/18 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Enable support of modes that need to use the unique mac address of
 *	  each slave.
 *	  * bond_enslave(): Moved setting the slave's mac address, and
 *	    openning it, from the application to the driver. This breaks
 *	    backward comaptibility with old versions of ifenslave that open
 *	     the slave before enalsving it !!!.
 *	  * bond_release(): The driver also takes care of closing the slave
 *	    and restoring its original mac address.
 *	- Removed the code that restores all base driver's flags.
 *	  Flags are automatically restored once all undo stages are done
 *	  properly.
 *	- Block possibility of enslaving before the master is up. This
 *	  prevents putting the system in an unstable state.
 *
 * 2003/03/18 - Amir Noam <amir.noam at intel dot com>,
 *		Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Added support for IEEE 802.3ad Dynamic link aggregation mode.
 *
 * 2003/05/01 - Amir Noam <amir.noam at intel dot com>
 *	- Added ABI version control to restore compatibility between
 *	  new/old ifenslave and new/old bonding.
 *
 * 2003/05/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Fixed bug in bond_release_all(): save old value of current_slave
 *	  before setting it to NULL.
 *	- Changed driver versioning scheme to include version number instead
 *	  of release date (that is already in another field). There are 3
 *	  fields X.Y.Z where:
 *		X - Major version - big behavior changes
 *		Y - Minor version - addition of features
 *		Z - Extra version - minor changes and bug fixes
 *	  The current version is 1.0.0 as a base line.
 *
 * 2003/05/01 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Amir Noam <amir.noam at intel dot com>
 *	- Added support for lacp_rate module param.
 *	- Code beautification and style changes (mainly in comments).
 *	  new version - 1.0.1
 *
 * 2003/05/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Based on discussion on mailing list, changed locking scheme
 *	  to use lock/unlock or lock_bh/unlock_bh appropriately instead
 *	  of lock_irqsave/unlock_irqrestore. The new scheme helps exposing
 *	  hidden bugs and solves system hangs that occurred due to the fact
 *	  that holding lock_irqsave doesn't prevent softirqs from running.
 *	  This also increases total throughput since interrupts are not
 *	  blocked on each transmitted packets or monitor timeout.
 *	  new version - 2.0.0
 *
 * 2003/05/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Added support for Transmit load balancing mode.
 *	- Concentrate all assignments of current_slave to a single point
 *	  so specific modes can take actions when the primary adapter is
 *	  changed.
 *	- Take the updelay parameter into consideration during bond_enslave
 *	  since some adapters loose their link during setting the device.
 *	- Renamed bond_3ad_link_status_changed() to
 *	  bond_3ad_handle_link_change() for compatibility with TLB.
 *	  new version - 2.1.0
 *
 * 2003/05/01 - Tsippy Mendelson <tsippy.mendelson at intel dot com>
 *	- Added support for Adaptive load balancing mode which is
 *	  equivalent to Transmit load balancing + Receive load balancing.
 *	  new version - 2.2.0
 *
 * 2003/05/15 - Jay Vosburgh <fubar at us dot ibm dot com>
 *	- Applied fix to activebackup_arp_monitor posted to bonding-devel
 *	  by Tony Cureington <tony.cureington * hp_com>.  Fixes ARP
 *	  monitor endless failover bug.  Version to 2.2.10
 *
 * 2003/05/20 - Amir Noam <amir.noam at intel dot com>
 *	- Fixed bug in ABI version control - Don't commit to a specific
 *	  ABI version if receiving unsupported ioctl commands.
 *
 * 2003/05/22 - Jay Vosburgh <fubar at us dot ibm dot com>
 *	- Fix ifenslave -c causing bond to loose existing routes;
 *	  added bond_set_mac_address() that doesn't require the
 *	  bond to be down.
 *	- In conjunction with fix for ifenslave -c, in
 *	  bond_change_active(), changing to the already active slave
 *	  is no longer an error (it successfully does nothing).
 *
 * 2003/06/30 - Amir Noam <amir.noam at intel dot com>
 * 	- Fixed bond_change_active() for ALB/TLB modes.
 *	  Version to 2.2.14.
 *
 * 2003/07/29 - Amir Noam <amir.noam at intel dot com>
 * 	- Fixed ARP monitoring bug.
 *	  Version to 2.2.15.
 *
 * 2003/07/31 - Willy Tarreau <willy at ods dot org>
 * 	- Fixed kernel panic when using ARP monitoring without
 *	  setting bond's IP address.
 *	  Version to 2.2.16.
 *
 * 2003/08/06 - Amir Noam <amir.noam at intel dot com>
 * 	- Back port from 2.6: use alloc_netdev(); fix /proc handling;
 *	  made stats a part of bond struct so no need to allocate
 *	  and free it separately; use standard list operations instead
 *	  of pre-allocated array of bonds.
 *	  Version to 2.3.0.
 *
 * 2003/08/07 - Jay Vosburgh <fubar at us dot ibm dot com>,
 *	       Amir Noam <amir.noam at intel dot com> and
 *	       Shmulik Hen <shmulik.hen at intel dot com>
 *	- Propagating master's settings: Distinguish between modes that
 *	  use a primary slave from those that don't, and propagate settings
 *	  accordingly; Consolidate change_active opeartions and add
 *	  reselect_active and find_best opeartions; Decouple promiscuous
 *	  handling from the multicast mode setting; Add support for changing
 *	  HW address and MTU with proper unwind; Consolidate procfs code,
 *	  add CHANGENAME handler; Enhance netdev notification handling.
 *	  Version to 2.4.0.
 *
 * 2003/09/15 - Stephen Hemminger <shemminger at osdl dot org>,
 *	       Amir Noam <amir.noam at intel dot com>
 *	- Convert /proc to seq_file interface.
 *	  Change /proc/net/bondX/info to /proc/net/bonding/bondX.
 *	  Set version to 2.4.1.
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
#include <linux/ip.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/socket.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/if_bonding.h>
#include <linux/smp.h>
#include <linux/if_ether.h>
#include <net/arp.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include "bonding.h"
#include "bond_3ad.h"
#include "bond_alb.h"

#define DRV_VERSION	"2.4.1"
#define DRV_RELDATE	"September 15, 2003"
#define DRV_NAME	"bonding"
#define DRV_DESCRIPTION	"Ethernet Channel Bonding Driver"

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

#define USES_PRIMARY(mode) \
		(((mode) == BOND_MODE_ACTIVEBACKUP) || \
		 ((mode) == BOND_MODE_TLB) || \
		 ((mode) == BOND_MODE_ALB))

struct bond_parm_tbl {
	char *modename;
	int mode;
};

static int arp_interval = BOND_LINK_ARP_INTERV;
static char *arp_ip_target[MAX_ARP_IP_TARGETS] = { NULL, };
static u32 arp_target[MAX_ARP_IP_TARGETS] = { 0, } ;
static int arp_ip_count = 0;
static u32 my_ip = 0;
char *arp_target_hw_addr = NULL;

static char *primary= NULL;

static int app_abi_ver = 0;
static int orig_app_abi_ver = -1; /* This is used to save the first ABI version
				   * we receive from the application. Once set,
				   * it won't be changed, and the module will
				   * refuse to enslave/release interfaces if the
				   * command comes from an application using
				   * another ABI version.
				   */

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
{	"802.3ad",		BOND_MODE_8023AD},
{	"balance-tlb",		BOND_MODE_TLB},
{	"balance-alb",		BOND_MODE_ALB},
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

static int lacp_fast		= 0;
static char *lacp_rate		= NULL;

static struct bond_parm_tbl bond_lacp_tbl[] = {
{	"slow",		AD_LACP_SLOW},
{	"fast",		AD_LACP_FAST},
{	NULL,		-1},
};

static LIST_HEAD(bond_dev_list);
#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *bond_proc_dir = NULL;
#endif

MODULE_PARM(max_bonds, "i");
MODULE_PARM_DESC(max_bonds, "Max number of bonded devices");
MODULE_PARM(miimon, "i");
MODULE_PARM_DESC(miimon, "Link check interval in milliseconds");
MODULE_PARM(use_carrier, "i");
MODULE_PARM_DESC(use_carrier, "Use netif_carrier_ok (vs MII ioctls) in miimon; 0 for off, 1 for on (default)");
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
MODULE_PARM(lacp_rate, "s");
MODULE_PARM_DESC(lacp_rate, "LACPDU tx rate to request from 802.3ad partner (slow/fast)");

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *dev);
static int bond_xmit_xor(struct sk_buff *skb, struct net_device *dev);
static int bond_xmit_activebackup(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *bond_get_stats(struct net_device *dev);
static void bond_mii_monitor(struct net_device *dev);
static void loadbalance_arp_monitor(struct net_device *dev);
static void activebackup_arp_monitor(struct net_device *dev);
static void bond_mc_list_destroy(struct bonding *bond);
static void bond_mc_add(bonding_t *bond, void *addr, int alen);
static void bond_mc_delete(bonding_t *bond, void *addr, int alen);
static int bond_mc_list_copy (struct dev_mc_list *src, struct bonding *dst, int gpf_flag);
static inline int dmi_same(struct dev_mc_list *dmi1, struct dev_mc_list *dmi2);
static void bond_set_promiscuity(bonding_t *bond, int inc);
static void bond_set_allmulti(bonding_t *bond, int inc);
static struct dev_mc_list* bond_mc_list_find_dmi(struct dev_mc_list *dmi, struct dev_mc_list *mc_list);
static void bond_mc_update(bonding_t *bond, slave_t *new, slave_t *old);
static int bond_enslave(struct net_device *master, struct net_device *slave);
static int bond_release(struct net_device *master, struct net_device *slave);
static int bond_release_all(struct net_device *master);
static int bond_sethwaddr(struct net_device *master, struct net_device *slave);
static void change_active_interface(struct bonding *bond, struct slave *new);
static void reselect_active_interface(struct bonding *bond);
static struct slave *find_best_interface(struct bonding *bond);


/* #define BONDING_DEBUG 1 */
#ifdef BONDING_DEBUG
#define dprintk(x...) printk(x...)
#else /* BONDING_DEBUG */
#define dprintk(x...) do {} while (0)
#endif /* BONDING_DEBUG */

/* several macros */

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
	case BOND_MODE_8023AD:
		return "IEEE 802.3ad Dynamic link aggregation";
	case BOND_MODE_TLB:
		return "transmit load balancing";
	case BOND_MODE_ALB:
		return "adaptive load balancing";
	default:
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

void bond_set_slave_inactive_flags(slave_t *slave)
{
	slave->state = BOND_STATE_BACKUP;
	slave->dev->flags |= IFF_NOARP;
}

void bond_set_slave_active_flags(slave_t *slave)
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
 * it should be changed by the calling function.
 *
 * bond->lock held for writing by caller.
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
			bond->prev = bond->next = (slave_t *)bond;
		} else { /* not alone */
			bond->next        = slave->next;
			slave->next->prev = (slave_t *)bond;
			bond->prev->next  = slave->next;
		}
	} else {
		slave->prev->next = slave->next;
		if (bond->prev == slave) {  /* is this slave the last one ? */
			bond->prev = slave->prev;
		} else {
			slave->next->prev = slave->prev;
		}
	}

	update_slave_cnt(bond, -1);

	return slave;
}

/*
 * This function attaches the slave <slave> to the list <bond>.
 *
 * bond->lock held for writing by caller.
 */
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
 * Get link speed and duplex from the slave's base driver
 * using ethtool. If for some reason the call fails or the
 * values are invalid, fake speed and duplex to 100/Full
 * and return error.
 */
static int bond_update_speed_duplex(struct slave *slave)
{
	struct net_device *dev = slave->dev;
	static int (* ioctl)(struct net_device *, struct ifreq *, int);
	struct ifreq ifr;
	struct ethtool_cmd etool;

	ioctl = dev->do_ioctl;
	if (ioctl) {
		etool.cmd = ETHTOOL_GSET;
		ifr.ifr_data = (char*)&etool;
		if (IOCTL(dev, &ifr, SIOCETHTOOL) == 0) {
			slave->speed = etool.speed;
			slave->duplex = etool.duplex;
		} else {
			goto err_out;
		}
	} else {
		goto err_out;
	}

	switch (slave->speed) {
		case SPEED_10:
		case SPEED_100:
		case SPEED_1000:
			break;
		default:
			goto err_out;
	}

	switch (slave->duplex) {
		case DUPLEX_FULL:
		case DUPLEX_HALF:
			break;
		default:
			goto err_out;
	}

	return 0;

err_out:
	/* Fake speed and duplex */
	slave->speed = SPEED_100;
	slave->duplex = DUPLEX_FULL;
	return -1;
}

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

	read_lock_bh(&bond->lock);
	read_lock(&bond->ptrlock);
	has_active_interface = (bond->current_slave != NULL);
	read_unlock(&bond->ptrlock);
	read_unlock_bh(&bond->lock);

	return (has_active_interface ? BMSR_LSTATUS : 0);
}

/* register to receive lacpdus on a bond */
static void bond_register_lacpdu(struct bonding *bond)
{
	struct packet_type* pk_type = &(BOND_AD_INFO(bond).ad_pkt_type);

	/* initialize packet type */
	pk_type->type = PKT_TYPE_LACPDU;
	pk_type->dev = bond->device;
	pk_type->func = bond_3ad_lacpdu_recv;

	dev_add_pack(pk_type);
}

/* unregister to receive lacpdus on a bond */
static void bond_unregister_lacpdu(struct bonding *bond)
{
	dev_remove_pack(&(BOND_AD_INFO(bond).ad_pkt_type));
}

static int bond_open(struct net_device *dev)
{
	struct bonding *bond = (struct bonding *)(dev->priv);
	struct timer_list *timer = &((struct bonding *)(dev->priv))->mii_timer;
	struct timer_list *arp_timer = &((struct bonding *)(dev->priv))->arp_timer;

	if ((bond_mode == BOND_MODE_TLB) ||
	    (bond_mode == BOND_MODE_ALB)) {
		struct timer_list *alb_timer = &(BOND_ALB_INFO(bond).alb_timer);

		/* bond_alb_initialize must be called before the timer
		 * is started.
		 */
		if (bond_alb_initialize(bond, (bond_mode == BOND_MODE_ALB))) {
			/* something went wrong - fail the open operation */
			return -1;
		}

		init_timer(alb_timer);
		alb_timer->expires  = jiffies + 1;
		alb_timer->data     = (unsigned long)bond;
		alb_timer->function = (void *)&bond_alb_monitor;
		add_timer(alb_timer);
	}

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

	if (bond_mode == BOND_MODE_8023AD) {
		struct timer_list *ad_timer = &(BOND_AD_INFO(bond).ad_timer);
		init_timer(ad_timer);
		ad_timer->expires  = jiffies + (AD_TIMER_INTERVAL * HZ / 1000);
		ad_timer->data     = (unsigned long)bond;
		ad_timer->function = (void *)&bond_3ad_state_machine_handler;
		add_timer(ad_timer);

		/* register to receive LACPDUs */
		bond_register_lacpdu(bond);
	}

	return 0;
}

static int bond_close(struct net_device *master)
{
	bonding_t *bond = (struct bonding *) master->priv;

	write_lock_bh(&bond->lock);

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

	if (bond_mode == BOND_MODE_8023AD) {
		del_timer_sync(&(BOND_AD_INFO(bond).ad_timer));

		/* Unregister the receive of LACPDUs */
		bond_unregister_lacpdu(bond);
	}

	bond_mc_list_destroy (bond);

	write_unlock_bh(&bond->lock);

	/* Release the bonded slaves */
	bond_release_all(master);

	if ((bond_mode == BOND_MODE_TLB) ||
	    (bond_mode == BOND_MODE_ALB)) {
		del_timer_sync(&(BOND_ALB_INFO(bond).alb_timer));

		bond_alb_deinitialize(bond);
	}

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

	if (bond_mode == BOND_MODE_8023AD) {
		/* del lacpdu mc addr from mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_delete(dev, lacpdu_multicast, ETH_ALEN, 0);
	}
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
 * Push the promiscuity flag down to appropriate slaves
 */
static void bond_set_promiscuity(bonding_t *bond, int inc)
{ 
	slave_t *slave; 

	if (USES_PRIMARY(bond_mode)) {
		if (bond->current_slave) {
			dev_set_promiscuity(bond->current_slave->dev, inc);
		}

	} else { 
		for (slave = bond->prev; slave != (slave_t*)bond;
		     slave = slave->prev) {
			dev_set_promiscuity(slave->dev, inc);
		}
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

	write_lock_bh(&bond->lock);

	/*
	 * Do promisc before checking multicast_mode
	 */
	if ( (master->flags & IFF_PROMISC) && !(bond->flags & IFF_PROMISC) )
		bond_set_promiscuity(bond, 1); 

	if ( !(master->flags & IFF_PROMISC) && (bond->flags & IFF_PROMISC) ) 
		bond_set_promiscuity(bond, -1); 

	if (multicast_mode == BOND_MULTICAST_DISABLED) {
		bond->flags = master->flags;
		write_unlock_bh(&bond->lock);
		return;
	}

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

	write_unlock_bh(&bond->lock);
}

/*
 * Update the mc list and multicast-related flags for the new and 
 * old active slaves (if any) according to the multicast mode, and
 * promiscuous flags unconditionally.
 */
static void bond_mc_update(bonding_t *bond, slave_t *new, slave_t *old)
{
	struct dev_mc_list *dmi;

	if (USES_PRIMARY(bond_mode)) {
		if (bond->device->flags & IFF_PROMISC) {
			if (old)
				dev_set_promiscuity(old->dev, -1);
			if (new)
				dev_set_promiscuity(new->dev, 1);
		}
	}

	switch(multicast_mode) {
	case BOND_MULTICAST_ACTIVE :		
		if (bond->device->flags & IFF_ALLMULTI) {
			if (old)
				dev_set_allmulti(old->dev, -1);
			if (new)
				dev_set_allmulti(new->dev, 1);
		}
		/* first remove all mc addresses from old slave if any,
		   and _then_ add them to new active slave */
		if (old) {
			for (dmi = bond->device->mc_list; dmi != NULL; dmi = dmi->next)
				dev_mc_delete(old->dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		}
		if (new) {
			for (dmi = bond->device->mc_list; dmi != NULL; dmi = dmi->next)
				dev_mc_add(new->dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		}
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
	unsigned long rflags = 0;
	int err = 0;
	struct dev_mc_list *dmi;
	struct in_ifaddr **ifap;
	struct in_ifaddr *ifa;
	int link_reporting;
	struct sockaddr addr;

	if (master_dev == NULL || slave_dev == NULL) {
		return -ENODEV;
	}
	bond = (struct bonding *) master_dev->priv;

	if (slave_dev->do_ioctl == NULL) {
		printk(KERN_DEBUG
			"Warning : no link monitoring support for %s\n",
			slave_dev->name);
	}


	/* bond must be initialized by bond_open() before enslaving */
	if (!(master_dev->flags & IFF_UP)) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error, master_dev is not up\n");
#endif
		return -EPERM;
	}

	/* already enslaved */
	if (master_dev->flags & IFF_SLAVE || slave_dev->flags & IFF_SLAVE) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error, Device was already enslaved\n");
#endif
		return -EBUSY;
	}

	if (app_abi_ver >= 1) {
		/* The application is using an ABI, which requires the
		 * slave interface to be closed.
		 */
		if ((slave_dev->flags & IFF_UP)) {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "Error, slave_dev is up\n");
#endif
			return -EPERM;
		}

		if (slave_dev->set_mac_address == NULL) {
			printk(KERN_CRIT
			       "The slave device you specified does not support"
			       " setting the MAC address.\n");
			printk(KERN_CRIT
			       "Your kernel likely does not support slave"
			       " devices.\n");

			return -EOPNOTSUPP;
		}
	} else {
		/* The application is not using an ABI, which requires the
		 * slave interface to be open.
		 */
		if (!(slave_dev->flags & IFF_UP)) {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "Error, slave_dev is not running\n");
#endif
			return -EINVAL;
		}

		if ((bond_mode == BOND_MODE_8023AD) ||
		    (bond_mode == BOND_MODE_TLB) ||
		    (bond_mode == BOND_MODE_ALB)) {
			printk(KERN_ERR
			       "bonding: Error: to use %s mode, you must "
			       "upgrade ifenslave.\n", bond_mode_name());
			return -EOPNOTSUPP;
		}
	}

	if ((new_slave = kmalloc(sizeof(slave_t), GFP_KERNEL)) == NULL) {
		return -ENOMEM;
	}
	memset(new_slave, 0, sizeof(slave_t));

	/* save slave's original flags before calling
	 * netdev_set_master and dev_open
	 */
	new_slave->original_flags = slave_dev->flags;

	if (app_abi_ver >= 1) {
		/* save slave's original ("permanent") mac address for
		 * modes that needs it, and for restoring it upon release,
		 * and then set it to the master's address
		 */
		memcpy(new_slave->perm_hwaddr, slave_dev->dev_addr, ETH_ALEN);

		if (bond->slave_cnt > 0) {
			/* set slave to master's mac address
			 * The application already set the master's
			 * mac address to that of the first slave
			 */
			memcpy(addr.sa_data, master_dev->dev_addr, master_dev->addr_len);
			addr.sa_family = slave_dev->type;
			err = slave_dev->set_mac_address(slave_dev, &addr);
			if (err) {
#ifdef BONDING_DEBUG
				printk(KERN_CRIT "Error %d calling set_mac_address\n", err);
#endif
				goto err_free;
			}
		}

		/* open the slave since the application closed it */
		err = dev_open(slave_dev);
		if (err) {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "Openning slave %s failed\n", slave_dev->name);
#endif
			goto err_restore_mac;
		}
	}

	err = netdev_set_master(slave_dev, master_dev);
	if (err) {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Error %d calling netdev_set_master\n", err);
#endif
		if (app_abi_ver < 1) {
			goto err_free;
		} else {
			goto err_close;
		}
	}

	new_slave->dev = slave_dev;

	if ((bond_mode == BOND_MODE_TLB) ||
	    (bond_mode == BOND_MODE_ALB)) {
		/* bond_alb_init_slave() must be called before all other stages since
		 * it might fail and we do not want to have to undo everything
		 */
		err = bond_alb_init_slave(bond, new_slave);
		if (err) {
			goto err_unset_master;
		}
	}

	/* set promiscuity level to new slave */ 
	if (master_dev->flags & IFF_PROMISC) {
		/* If the mode USES_PRIMARY, then the new slave gets the
		 * master's promisc (and mc) settings only if it becomes the
		 * current_slave, and that is taken care of later when calling
		 * bond_change_active()
		 */
		if (!USES_PRIMARY(bond_mode)) {
			dev_set_promiscuity(slave_dev, 1); 
		}
	}
 
	if (multicast_mode == BOND_MULTICAST_ALL) {
		/* set allmulti level to new slave */
		if (master_dev->flags & IFF_ALLMULTI) 
			dev_set_allmulti(slave_dev, 1); 
		
		/* upload master's mc_list to new slave */ 
		for (dmi = master_dev->mc_list; dmi != NULL; dmi = dmi->next) 
			dev_mc_add (slave_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
	}

	if (bond_mode == BOND_MODE_8023AD) {
		/* add lacpdu mc addr to mc list */
		u8 lacpdu_multicast[ETH_ALEN] = MULTICAST_LACPDU_ADDR;

		dev_mc_add(slave_dev, lacpdu_multicast, ETH_ALEN, 0);
	}

	write_lock_bh(&bond->lock);
	
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
		if (updelay) {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "Initial state of slave_dev is "
			       "BOND_LINK_BACK\n");
#endif
			new_slave->link  = BOND_LINK_BACK;
			new_slave->delay = updelay;
		}
		else {
#ifdef BONDING_DEBUG
			printk(KERN_DEBUG "Initial state of slave_dev is "
				"BOND_LINK_UP\n");
#endif
			new_slave->link  = BOND_LINK_UP;
		}
		new_slave->jiffies = jiffies;
	}
	else {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "Initial state of slave_dev is "
			"BOND_LINK_DOWN\n");
#endif
		new_slave->link  = BOND_LINK_DOWN;
	}

	if (bond_update_speed_duplex(new_slave) &&
	    (new_slave->link != BOND_LINK_DOWN)) {

		printk(KERN_WARNING
		       "bond_enslave(): failed to get speed/duplex from %s, "
		       "speed forced to 100Mbps, duplex forced to Full.\n",
		       new_slave->dev->name);
		if (bond_mode == BOND_MODE_8023AD) {
			printk(KERN_WARNING
			       "Operation of 802.3ad mode requires ETHTOOL support "
			       "in base driver for proper aggregator selection.\n");
		}
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
			&& (new_slave->link != BOND_LINK_DOWN)) {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "This is the first active slave\n");
#endif
			/* first slave or no active slave yet, and this link
			   is OK, so make this interface the active one */
			change_active_interface(bond, new_slave);
		}
		else {
#ifdef BONDING_DEBUG
			printk(KERN_CRIT "This is just a backup slave\n");
#endif
			bond_set_slave_inactive_flags(new_slave);
		}
		if (((struct in_device *)slave_dev->ip_ptr) != NULL) {
			read_lock_irqsave(&(((struct in_device *)slave_dev->ip_ptr)->lock), rflags);
			ifap= &(((struct in_device *)slave_dev->ip_ptr)->ifa_list);
			ifa = *ifap;
			if (ifa != NULL)
				my_ip = ifa->ifa_address;
			read_unlock_irqrestore(&(((struct in_device *)slave_dev->ip_ptr)->lock), rflags);
		}

		/* if there is a primary slave, remember it */
		if (primary != NULL) {
			if (strcmp(primary, new_slave->dev->name) == 0) {
				bond->primary_slave = new_slave;
			}
		}
	} else if (bond_mode == BOND_MODE_8023AD) {
		/* in 802.3ad mode, the internal mechanism
		 * will activate the slaves in the selected
		 * aggregator
		 */
		bond_set_slave_inactive_flags(new_slave);
		/* if this is the first slave */
		if (new_slave == bond->next) {
			SLAVE_AD_INFO(new_slave).id = 1;
			/* Initialize AD with the number of times that the AD timer is called in 1 second
			 * can be called only after the mac address of the bond is set
			 */
			bond_3ad_initialize(bond, 1000/AD_TIMER_INTERVAL,
					    lacp_fast);
		} else {
			SLAVE_AD_INFO(new_slave).id =
			SLAVE_AD_INFO(new_slave->prev).id + 1;
		}

		bond_3ad_bind_slave(new_slave);
	} else if ((bond_mode == BOND_MODE_TLB) ||
		   (bond_mode == BOND_MODE_ALB)) {
		new_slave->state = BOND_STATE_ACTIVE;
		if ((bond->current_slave == NULL) && (new_slave->link != BOND_LINK_DOWN)) {
			/* first slave or no active slave yet, and this link
			 * is OK, so make this interface the active one
			 */
			change_active_interface(bond, new_slave);
		}

		/* if there is a primary slave, remember it */
		if (primary != NULL) {
			if (strcmp(primary, new_slave->dev->name) == 0) {
				bond->primary_slave = new_slave;
			}
		}
	} else {
#ifdef BONDING_DEBUG
		printk(KERN_CRIT "This slave is always active in trunk mode\n");
#endif
		/* always active in trunk mode */
		new_slave->state = BOND_STATE_ACTIVE;

		/* In trunking mode there is little meaning to current_slave
		 * anyway (it holds no special properties of the bond device),
		 * so we can change it without calling change_active_interface()
		 */
		if (bond->current_slave == NULL) 
			bond->current_slave = new_slave;
	}

	write_unlock_bh(&bond->lock);

	if (app_abi_ver < 1) {
		/*
		 * !!! This is to support old versions of ifenslave.
		 * We can remove this in 2.5 because our ifenslave takes
		 * care of this for us.
		 * We check to see if the master has a mac address yet.
		 * If not, we'll give it the mac address of our slave device.
		 */
		int ndx = 0;

		for (ndx = 0; ndx < slave_dev->addr_len; ndx++) {
#ifdef BONDING_DEBUG
			printk(KERN_DEBUG
			       "Checking ndx=%d of master_dev->dev_addr\n", ndx);
#endif
			if (master_dev->dev_addr[ndx] != 0) {
#ifdef BONDING_DEBUG
				printk(KERN_DEBUG
				       "Found non-zero byte at ndx=%d\n", ndx);
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
			printk(KERN_DEBUG "%s doesn't have a MAC address yet.  ",
			       master_dev->name);
			printk(KERN_DEBUG "Going to give assign it from %s.\n",
			       slave_dev->name);
#endif
			bond_sethwaddr(master_dev, slave_dev);
		}
	}

	printk (KERN_INFO "%s: enslaving %s as a%s interface with a%s link.\n",
		master_dev->name, slave_dev->name,
		new_slave->state == BOND_STATE_ACTIVE ? "n active" : " backup",
		new_slave->link != BOND_LINK_DOWN ? "n up" : " down");

	/* enslave is successful */
	return 0;

/* Undo stages on error */
err_unset_master:
	netdev_set_master(slave_dev, NULL);

err_close:
	dev_close(slave_dev);

err_restore_mac:
	memcpy(addr.sa_data, new_slave->perm_hwaddr, ETH_ALEN);
	addr.sa_family = slave_dev->type;
	slave_dev->set_mac_address(slave_dev, &addr);

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
	int ret = 0;

	if (master_dev == NULL || slave_dev == NULL) {
		return -ENODEV;
	}

	/* Verify that master_dev is indeed the master of slave_dev */
	if (!(slave_dev->flags & IFF_SLAVE) ||
	    (slave_dev->master != master_dev)) {

		return -EINVAL;
	}

	bond = (struct bonding *) master_dev->priv;
	write_lock_bh(&bond->lock);
	slave = (slave_t *)bond;
	oldactive = bond->current_slave;

	while ((slave = slave->prev) != (slave_t *)bond) {
		if(slave_dev == slave->dev) {
			newactive = slave;
			break;
		}
	}

	/*
	 * Changing to the current active: do nothing; return success.
	 */
	if (newactive && (newactive == oldactive)) {
		write_unlock_bh(&bond->lock);
		return 0;
	}

	if ((newactive != NULL)&&
	    (oldactive != NULL)&&
	    (newactive->link == BOND_LINK_UP)&&
	    IS_UP(newactive->dev)) {
		change_active_interface(bond, newactive);
	} else {
		ret = -EINVAL;
	}
	write_unlock_bh(&bond->lock);
	return ret;
}

/**
 * find_best_interface - select the best available slave to be the active one
 * @bond: our bonding struct
 *
 * Warning: Caller must hold ptrlock for writing.
 */
static struct slave *find_best_interface(struct bonding *bond)
{
	struct slave *newslave, *oldslave;
	struct slave *bestslave = NULL;
	int mintime;

	newslave = oldslave = bond->current_slave;

	if (newslave == NULL) { /* there were no active slaves left */
		if (bond->next != (slave_t *)bond) {  /* found one slave */
			newslave = bond->next;
		} else {
			return NULL; /* still no slave, return NULL */
		}
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

	/* remember where to stop iterating over the slaves */
	oldslave = newslave;

	do {
		if (IS_UP(newslave->dev)) {
			if (newslave->link == BOND_LINK_UP) {
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

	return bestslave;
}

/**
 * change_active_interface - change the active slave into the specified one
 * @bond: our bonding struct
 * @new: the new slave to make the active one
 * 
 * Set the new slave to the bond's settings and unset them on the old
 * current_slave.
 * Setting include flags, mc-list, promiscuity, allmulti, etc.
 *
 * If @new's link state is %BOND_LINK_BACK we'll set it to %BOND_LINK_UP,
 * because it is apparently the best available slave we have, even though its
 * updelay hasn't timed out yet.
 *
 * Warning: Caller must hold ptrlock for writing.
 */
static void change_active_interface(struct bonding *bond, struct slave *new)
{
	struct slave *old = bond->current_slave;

	if (old == new) {
		return;
	}

	if (new) {
		if (new->link == BOND_LINK_BACK) {
			if (USES_PRIMARY(bond_mode)) {
				printk (KERN_INFO
					"%s: making interface %s the new "
					"active one %d ms earlier.\n",
					bond->device->name, new->dev->name,
					(updelay - new->delay) * miimon);
			}

			new->delay = 0;
			new->link = BOND_LINK_UP;
			new->jiffies = jiffies;

			if (bond_mode == BOND_MODE_8023AD) {
				bond_3ad_handle_link_change(new, BOND_LINK_UP);
			}

			if ((bond_mode == BOND_MODE_TLB) ||
			    (bond_mode == BOND_MODE_ALB)) {
				bond_alb_handle_link_change(bond, new, BOND_LINK_UP);
			}
		} else {
			if (USES_PRIMARY(bond_mode)) {
				printk (KERN_INFO
					"%s: making interface %s the new active one.\n",
					bond->device->name, new->dev->name);
			}
		}
	}

	if (bond_mode == BOND_MODE_ACTIVEBACKUP) {
		if (old) {
			bond_set_slave_inactive_flags(old);
		}

		if (new) {
			bond_set_slave_active_flags(new);
		}
	}

	if (USES_PRIMARY(bond_mode)) {
		bond_mc_update(bond, new, old);
	}

	if ((bond_mode == BOND_MODE_TLB) ||
	    (bond_mode == BOND_MODE_ALB)) {
		bond_alb_assign_current_slave(bond, new);
	} else {
		bond->current_slave = new;
	}
}

/**
 * reselect_active_interface - select a new active slave, if needed
 * @bond: our bonding struct
 *
 * This functions shoud be called when one of the following occurs:
 * - The old current_slave has been released or lost its link.
 * - The primary_slave has got its link back.
 * - A slave has got its link back and there's no old current_slave.
 *
 * Warning: Caller must hold ptrlock for writing.
 */
static void reselect_active_interface(struct bonding *bond)
{
	struct slave *best_slave;

	best_slave = find_best_interface(bond);

	if (best_slave != bond->current_slave) {
		change_active_interface(bond, best_slave);
	}
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
	struct sockaddr addr;
	
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

	write_lock_bh(&bond->lock);
	bond->current_arp_slave = NULL;
	our_slave = (slave_t *)bond;
	old_current = bond->current_slave;
	while ((our_slave = our_slave->prev) != (slave_t *)bond) {
		if (our_slave->dev == slave) {
			int mac_addr_differ = memcmp(bond->device->dev_addr,
						 our_slave->perm_hwaddr,
						 ETH_ALEN);
			if (!mac_addr_differ && (bond->slave_cnt > 1)) {
				printk(KERN_WARNING "WARNING: the permanent HWaddr of %s "
				"- %02X:%02X:%02X:%02X:%02X:%02X - "
				"is still in use by %s. Set the HWaddr "
				"of %s to a different address "
				"to avoid conflicts.\n",
				       slave->name,
				       our_slave->perm_hwaddr[0],
				       our_slave->perm_hwaddr[1],
				       our_slave->perm_hwaddr[2],
				       our_slave->perm_hwaddr[3],
				       our_slave->perm_hwaddr[4],
				       our_slave->perm_hwaddr[5],
				       bond->device->name,
				       slave->name);
			}

			/* Inform AD package of unbinding of slave. */
			if (bond_mode == BOND_MODE_8023AD) {
				/* must be called before the slave is
				 * detached from the list
				 */
				bond_3ad_unbind_slave(our_slave);
			}

			printk (KERN_INFO "%s: releasing %s interface %s\n",
				master->name,
				(our_slave->state == BOND_STATE_ACTIVE) ? "active" : "backup",
				slave->name);

			/* release the slave from its bond */
			bond_detach_slave(bond, our_slave);

			if (bond->primary_slave == our_slave) {
				bond->primary_slave = NULL;
			}

			if (bond->current_slave == our_slave) {
				change_active_interface(bond, NULL);
				reselect_active_interface(bond);
			}

			if (bond->current_slave == NULL) {
				printk(KERN_INFO
					"%s: now running without any active interface !\n",
					master->name);
			}

			if ((bond_mode == BOND_MODE_TLB) ||
			    (bond_mode == BOND_MODE_ALB)) {
				/* must be called only after the slave has been
				 * detached from the list and the current_slave
				 * has been replaced (if our_slave == old_current)
				 */
				bond_alb_deinit_slave(bond, our_slave);
			}

			break;
		}

	}
	write_unlock_bh(&bond->lock);
	
	if (our_slave == (slave_t *)bond) {
		/* if we get here, it's because the device was not found */
		printk (KERN_INFO "%s: %s not enslaved\n", master->name, slave->name);
		return -EINVAL;
	}

	/* unset promiscuity level from slave */
	if (master->flags & IFF_PROMISC) {
		/* If the mode USES_PRIMARY, then we should only remove its
		 * promisc settings if it was the current_slave, but that was
		 * already taken care of above when we detached the slave
		 */
		if (!USES_PRIMARY(bond_mode)) {
			dev_set_promiscuity(slave, -1); 
		}
	}

	/* undo settings and restore original values */
	if (multicast_mode == BOND_MULTICAST_ALL) {
		/* flush master's mc_list from slave */ 
		bond_mc_list_flush (slave, master); 

		/* unset allmulti level from slave */ 
		if (master->flags & IFF_ALLMULTI)
			dev_set_allmulti(slave, -1); 
	}

	netdev_set_master(slave, NULL);

	/* close slave before restoring its mac address */
	dev_close(slave);

	if (app_abi_ver >= 1) {
		/* restore original ("permanent") mac address */
		memcpy(addr.sa_data, our_slave->perm_hwaddr, ETH_ALEN);
		addr.sa_family = slave->type;
		slave->set_mac_address(slave, &addr);
	}

	/* restore the original state of the
	 * IFF_NOARP flag that might have been
	 * set by bond_set_slave_inactive_flags()
	 */
	if ((our_slave->original_flags & IFF_NOARP) == 0) {
		slave->flags &= ~IFF_NOARP;
	}

	kfree(our_slave);

	/* if the last slave was removed, zero the mac address
	 * of the master so it will be set by the application
	 * to the mac address of the first slave
	 */
	if (bond->next == (slave_t*)bond) {
		memset(master->dev_addr, 0, master->addr_len);
	}

	return 0;  /* deletion OK */
}

/* 
 * This function releases all slaves.
 */
static int bond_release_all(struct net_device *master)
{
	bonding_t *bond;
	slave_t *our_slave, *old_current;
	struct net_device *slave_dev;
	struct sockaddr addr;
	int err = 0;

	if (master == NULL)  {
		return -ENODEV;
	}

	if (master->flags & IFF_SLAVE) {
		return -EINVAL;
	}

	bond = (struct bonding *) master->priv;

	write_lock_bh(&bond->lock);
	if (bond->next == (struct slave *) bond) {
		err = -EINVAL;
		goto out;
	}

	old_current = bond->current_slave;
	change_active_interface(bond, NULL);
	bond->current_arp_slave = NULL;
	bond->primary_slave = NULL;

	while ((our_slave = bond->prev) != (slave_t *)bond) {
		/* Inform AD package of unbinding of slave
		 * before slave is detached from the list.
		 */
		if (bond_mode == BOND_MODE_8023AD) {
			bond_3ad_unbind_slave(our_slave);
		}

		slave_dev = our_slave->dev;
		bond_detach_slave(bond, our_slave);

		if ((bond_mode == BOND_MODE_TLB) ||
		    (bond_mode == BOND_MODE_ALB)) {
			/* must be called only after the slave
			 * has been detached from the list
			 */
			bond_alb_deinit_slave(bond, our_slave);
		}

		/* now that the slave is detached, unlock and perform
		 * all the undo steps that should not be called from
		 * within a lock.
		 */
		write_unlock_bh(&bond->lock);

		/* unset promiscuity level from slave */
		if (master->flags & IFF_PROMISC) {
			if (!USES_PRIMARY(bond_mode)) {
				dev_set_promiscuity(slave_dev, -1); 
			}
		}

		if (multicast_mode == BOND_MULTICAST_ALL) {
			/* flush master's mc_list from slave */ 
			bond_mc_list_flush (slave_dev, master); 

			/* unset allmulti level from slave */ 
			if (master->flags & IFF_ALLMULTI)
				dev_set_allmulti(slave_dev, -1); 
		}

		netdev_set_master(slave_dev, NULL);

		/* close slave before restoring its mac address */
		dev_close(slave_dev);

		if (app_abi_ver >= 1) {
			/* restore original ("permanent") mac address*/
			memcpy(addr.sa_data, our_slave->perm_hwaddr, ETH_ALEN);
			addr.sa_family = slave_dev->type;
			slave_dev->set_mac_address(slave_dev, &addr);
		}

		/* restore the original state of the IFF_NOARP flag that might have
		 * been set by bond_set_slave_inactive_flags()
		 */
		if ((our_slave->original_flags & IFF_NOARP) == 0) {
			slave_dev->flags &= ~IFF_NOARP;
		}

		kfree(our_slave);

		/* re-acquire the lock before getting the next slave */
		write_lock_bh(&bond->lock);
	}

	/* zero the mac address of the master so it will be
	 * set by the application to the mac address of the
	 * first slave
	 */
	memset(master->dev_addr, 0, master->addr_len);

	printk (KERN_INFO "%s: released all slaves\n", master->name);

out:
	write_unlock_bh(&bond->lock);

	return err;
}

/* this function is called regularly to monitor each slave's link. */
static void bond_mii_monitor(struct net_device *master)
{
	bonding_t *bond = (struct bonding *) master->priv;
	slave_t *slave, *oldcurrent;
	int slave_died = 0;
	int do_failover = 0;

	read_lock(&bond->lock);

	/* we will try to read the link status of each of our slaves, and
	 * set their IFF_RUNNING flag appropriately. For each slave not
	 * supporting MII status, we won't do anything so that a user-space
	 * program could monitor the link itself if needed.
	 */

	slave = (slave_t *)bond;

	read_lock(&bond->ptrlock);
	oldcurrent = bond->current_slave;
	read_unlock(&bond->ptrlock);

	while ((slave = slave->prev) != (slave_t *)bond) {
		struct net_device *dev = slave->dev;
		int link_state;
		u16 old_speed = slave->speed;
		u8 old_duplex = slave->duplex;
		
		link_state = bond_check_dev_link(dev, 0);

		switch (slave->link) {
		case BOND_LINK_UP:	/* the link was up */
			if (link_state == BMSR_LSTATUS) {
				/* link stays up, nothing more to do */
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
					 * completely disable this interface
					 */
					if ((bond_mode == BOND_MODE_ACTIVEBACKUP) ||
					    (bond_mode == BOND_MODE_8023AD)) {
						bond_set_slave_inactive_flags(slave);
					}

					printk(KERN_INFO
						"%s: link status definitely down "
						"for interface %s, disabling it",
						master->name,
						dev->name);

					/* notify ad that the link status has changed */
					if (bond_mode == BOND_MODE_8023AD) {
						bond_3ad_handle_link_change(slave, BOND_LINK_DOWN);
					}

					if ((bond_mode == BOND_MODE_TLB) ||
					    (bond_mode == BOND_MODE_ALB)) {
						bond_alb_handle_link_change(bond, slave, BOND_LINK_DOWN);
					}

					if (slave == oldcurrent) {
						do_failover = 1;
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

					if (bond_mode == BOND_MODE_8023AD) {
						/* prevent it from being the active one */
						slave->state = BOND_STATE_BACKUP;
					}
					else if (bond_mode != BOND_MODE_ACTIVEBACKUP) {
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
	
					/* notify ad that the link status has changed */
					if (bond_mode == BOND_MODE_8023AD) {
						bond_3ad_handle_link_change(slave, BOND_LINK_UP);
					}

					if ((bond_mode == BOND_MODE_TLB) ||
					    (bond_mode == BOND_MODE_ALB)) {
						bond_alb_handle_link_change(bond, slave, BOND_LINK_UP);
					}

					if ((oldcurrent == NULL) ||
					    (slave == bond->primary_slave)) {
						do_failover = 1;
					}
				} else {
					slave->delay--;
				}
			}
			break;
		} /* end of switch */

		bond_update_speed_duplex(slave);

		if (bond_mode == BOND_MODE_8023AD) {
			if (old_speed != slave->speed) {
				bond_3ad_adapter_speed_changed(slave);
			}
			if (old_duplex != slave->duplex) {
				bond_3ad_adapter_duplex_changed(slave);
			}
		}

	} /* end of while */

	if (do_failover) {
		write_lock(&bond->ptrlock);

		reselect_active_interface(bond);
		if (oldcurrent && !bond->current_slave) {
			printk(KERN_INFO
				"%s: now running without any active interface !\n",
				master->name);
		}

		write_unlock(&bond->ptrlock);
	}

	read_unlock(&bond->lock);
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
	slave_t *slave, *oldcurrent;
	int the_delta_in_ticks =  arp_interval * HZ / 1000;
	int next_timer = jiffies + (arp_interval * HZ / 1000);
	int do_failover = 0;

	bond = (struct bonding *) master->priv; 
	if (master->priv == NULL) {
		mod_timer(&bond->arp_timer, next_timer);
		return;
	}

	/* TODO: investigate why rtnl_shlock_nowait and rtnl_exlock_nowait
	 * are called below and add comment why they are required... 
	 */
	if ((!IS_UP(master)) || rtnl_shlock_nowait()) {
		mod_timer(&bond->arp_timer, next_timer);
		return;
	}

	if (rtnl_exlock_nowait()) {
		rtnl_shunlock();
		mod_timer(&bond->arp_timer, next_timer);
		return;
	}

	read_lock(&bond->lock);

	read_lock(&bond->ptrlock);
	oldcurrent = bond->current_slave;
	read_unlock(&bond->ptrlock);

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
				if (oldcurrent == NULL) {
					printk(KERN_INFO
						"%s: link status definitely up "
						"for interface %s, ",
						master->name,
						slave->dev->name);
					do_failover = 1;
				} else {
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

				if (slave == oldcurrent) {
					do_failover = 1;
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

	if (do_failover) {
		write_lock(&bond->ptrlock);

		reselect_active_interface(bond);
		if (oldcurrent && !bond->current_slave) {
			printk(KERN_INFO
				"%s: now running without any active interface !\n",
				master->name);
		}

		write_unlock(&bond->ptrlock);
	}

	read_unlock(&bond->lock);
	rtnl_exunlock();
	rtnl_shunlock();

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
	slave_t *slave;
	int the_delta_in_ticks =  arp_interval * HZ / 1000;
	int next_timer = jiffies + (arp_interval * HZ / 1000);

	bond = (struct bonding *) master->priv; 
	if (master->priv == NULL) {
		mod_timer(&bond->arp_timer, next_timer);
		return;
	}

	if (!IS_UP(master)) {
		mod_timer(&bond->arp_timer, next_timer);
		return;
	}

	read_lock(&bond->lock);

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
					change_active_interface(bond, slave);
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
			write_lock(&bond->ptrlock);
			reselect_active_interface(bond);
			slave = bond->current_slave;
			write_unlock(&bond->ptrlock);
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
			write_lock(&bond->ptrlock);
			change_active_interface(bond, bond->primary_slave);
			write_unlock(&bond->ptrlock);
			slave = bond->primary_slave;
			slave->jiffies = jiffies;
		} else {
			bond->current_arp_slave = NULL;
		}

		/* the current slave must tx an arp to ensure backup slaves
		 * rx traffic
		 */
		if ((slave != NULL) && (my_ip != 0)) {
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
				 * reselect_active_interface doesn't make this 
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

	read_unlock(&bond->lock);
	mod_timer(&bond->arp_timer, next_timer);
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

	info->bond_mode = bond_mode;
	info->num_slaves = 0;
	info->miimon = miimon;

	read_lock_bh(&bond->lock);
	for (slave = bond->prev; slave != (slave_t *)bond; slave = slave->prev) {
		info->num_slaves++;
	}
	read_unlock_bh(&bond->lock);

	return 0;
}

static int bond_slave_info_query(struct net_device *master, 
					struct ifslave *info)
{
	bonding_t *bond = (struct bonding *) master->priv;
	slave_t *slave;
	int cur_ndx = 0;

	if (info->slave_id < 0) {
		return -ENODEV;
	}

	read_lock_bh(&bond->lock);
	for (slave = bond->prev; 
		 slave != (slave_t *)bond && cur_ndx < info->slave_id; 
		 slave = slave->prev) {
		cur_ndx++;
	}
	read_unlock_bh(&bond->lock);

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

static int bond_ethtool_ioctl(struct net_device *master_dev, struct ifreq *ifr)
{
	void *addr = ifr->ifr_data;
	uint32_t cmd;

	if (get_user(cmd, (uint32_t *) addr))
		return -EFAULT;

	switch (cmd) {

	case ETHTOOL_GDRVINFO:
		{
			struct ethtool_drvinfo info;
			char *endptr;

			if (copy_from_user(&info, addr, sizeof(info)))
				return -EFAULT;

			if (strcmp(info.driver, "ifenslave") == 0) {
				int new_abi_ver;

				new_abi_ver = simple_strtoul(info.fw_version,
						             &endptr, 0);
				if (*endptr) {
					printk(KERN_ERR
					       "bonding: Error: got invalid ABI"
					       " version from application\n");

					return -EINVAL;
				}

				if (orig_app_abi_ver == -1) {
					orig_app_abi_ver  = new_abi_ver;
				}

				app_abi_ver = new_abi_ver;
			}

			strncpy(info.driver,  DRV_NAME, 32);
			strncpy(info.version, DRV_VERSION, 32);
			snprintf(info.fw_version, 32, "%d", BOND_ABI_VERSION);

			if (copy_to_user(addr, &info, sizeof(info)))
				return -EFAULT;

			return 0;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}
}

static int bond_ioctl(struct net_device *master_dev, struct ifreq *ifr, int cmd)
{
	struct net_device *slave_dev = NULL;
	struct ifbond *u_binfo = NULL, k_binfo;
	struct ifslave *u_sinfo = NULL, k_sinfo;
	struct mii_ioctl_data *mii = NULL;
	int prev_abi_ver = orig_app_abi_ver;
	int ret = 0;

#ifdef BONDING_DEBUG
	printk(KERN_INFO "bond_ioctl: master=%s, cmd=%d\n", 
		master_dev->name, cmd);
#endif

	switch (cmd) {
	case SIOCETHTOOL:
		return bond_ethtool_ioctl(master_dev, ifr);

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

	if (orig_app_abi_ver == -1) {
		/* no orig_app_abi_ver was provided yet, so we'll use the
		 * current one from now on, even if it's 0
		 */
		orig_app_abi_ver = app_abi_ver;

	} else if (orig_app_abi_ver != app_abi_ver) {
		printk(KERN_ERR
		       "bonding: Error: already using ifenslave ABI "
		       "version %d; to upgrade ifenslave to version %d, "
		       "you must first reload bonding.\n",
		       orig_app_abi_ver, app_abi_ver);
		return -EINVAL;
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
			if (USES_PRIMARY(bond_mode)) {
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

	if (ret < 0) {
		/* The ioctl failed, so there's no point in changing the
		 * orig_app_abi_ver. We'll restore it's value just in case
		 * we've changed it earlier in this function.
		 */
		orig_app_abi_ver = prev_abi_ver;
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
	struct net_device *device_we_should_send_to = 0;

	if (!IS_UP(dev)) { /* bond down */
		dev_kfree_skb(skb);
		return 0;
	}

	read_lock(&bond->lock);

	read_lock(&bond->ptrlock);
	slave = start_at = bond->current_slave;
	read_unlock(&bond->ptrlock);

	if (slave == NULL) { /* we're at the root, get the first slave */
		/* no suitable interface, frame not sent */
		read_unlock(&bond->lock);
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
	read_unlock(&bond->lock);
	return 0;
}

static int bond_xmit_roundrobin(struct sk_buff *skb, struct net_device *dev)
{
	slave_t *slave, *start_at;
	struct bonding *bond = (struct bonding *) dev->priv;

	if (!IS_UP(dev)) { /* bond down */
		dev_kfree_skb(skb);
		return 0;
	}

	read_lock(&bond->lock);

	read_lock(&bond->ptrlock);
	slave = start_at = bond->current_slave;
	read_unlock(&bond->ptrlock);

	if (slave == NULL) { /* we're at the root, get the first slave */
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
		read_unlock(&bond->lock);
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

			read_unlock(&bond->lock);
			return 0;
		}
	} while ((slave = slave->next) != start_at);

	/* no suitable interface, frame not sent */
	dev_kfree_skb(skb);
	read_unlock(&bond->lock);
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
	struct ethhdr *data = (struct ethhdr *)skb->data;
	int slave_no;

	if (!IS_UP(dev)) { /* bond down */
		dev_kfree_skb(skb);
		return 0;
	}

	read_lock(&bond->lock);
	slave = bond->prev;

	/* we're at the root, get the first slave */
	if (bond->slave_cnt == 0) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
		read_unlock(&bond->lock);
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

			read_unlock(&bond->lock);
			return 0;
		}
	} while ((slave = slave->next) != start_at);

	/* no suitable interface, frame not sent */
	dev_kfree_skb(skb);
	read_unlock(&bond->lock);
	return 0;
}

/* 
 * in active-backup mode, we know that bond->current_slave is always valid if
 * the bond has a usable interface.
 */
static int bond_xmit_activebackup(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = (struct bonding *) dev->priv;
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

	read_lock(&bond->lock);

	read_lock(&bond->ptrlock);
	if (bond->current_slave != NULL) { /* one usable interface */
		skb->dev = bond->current_slave->dev;
		read_unlock(&bond->ptrlock);
		skb->priority = 1;
		ret = dev_queue_xmit(skb);
		read_unlock(&bond->lock);
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
	read_unlock(&bond->lock);
	return 0;
}

static struct net_device_stats *bond_get_stats(struct net_device *dev)
{
	bonding_t *bond = dev->priv;
	struct net_device_stats *stats = &(bond->stats), *sstats;
	slave_t *slave;

	memset(stats, 0, sizeof(struct net_device_stats));

	read_lock_bh(&bond->lock);

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

	read_unlock_bh(&bond->lock);
	return stats;
}

#ifdef CONFIG_PROC_FS

#define SEQ_START_TOKEN ((void *)1)

static void *bond_info_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bonding *bond = seq->private;
	loff_t off = 0;
	struct slave *slave;

	/* make sure the bond won't be taken away */
	read_lock(&dev_base_lock);
	read_lock_bh(&bond->lock);

	if (*pos == 0) {
		return SEQ_START_TOKEN;
	}

	for (slave = bond->prev; slave != (slave_t *)bond;
	     slave = slave->prev) {

		if (++off == *pos) {
			return slave;
		}
	}

	return NULL;
}

static void *bond_info_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bonding *bond = seq->private;
	struct slave *slave = v;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		slave = bond->prev;
	} else {
		slave = slave->prev;
	}

	return (slave == (struct slave *) bond) ? NULL : slave;
}

static void bond_info_seq_stop(struct seq_file *seq, void *v)
{
	struct bonding *bond = seq->private;

	read_unlock_bh(&bond->lock);
	read_unlock(&dev_base_lock);
}

static void bond_info_show_master(struct seq_file *seq, struct bonding *bond)
{
	struct slave *curr;

	read_lock(&bond->ptrlock);
	curr = bond->current_slave;
	read_unlock(&bond->ptrlock);

	seq_printf(seq, "Bonding Mode: %s\n", bond_mode_name());

	if (USES_PRIMARY(bond_mode)) {
		if (curr) {
			seq_printf(seq,
				   "Currently Active Slave: %s\n",
				   curr->dev->name);
		}
	}

	seq_printf(seq, "MII Status: %s\n", (curr) ? "up" : "down");
	seq_printf(seq, "MII Polling Interval (ms): %d\n", miimon);
	seq_printf(seq, "Up Delay (ms): %d\n", updelay * miimon);
	seq_printf(seq, "Down Delay (ms): %d\n", downdelay * miimon);
	seq_printf(seq, "Multicast Mode: %s\n", multicast_mode_name());

	if (bond_mode == BOND_MODE_8023AD) {
		struct ad_info ad_info;

		seq_puts(seq, "\n802.3ad info\n");

		if (bond_3ad_get_active_agg_info(bond, &ad_info)) {
			seq_printf(seq, "bond %s has no active aggregator\n",
				   bond->device->name);
		} else {
			seq_printf(seq, "Active Aggregator Info:\n");

			seq_printf(seq, "\tAggregator ID: %d\n",
				   ad_info.aggregator_id);
			seq_printf(seq, "\tNumber of ports: %d\n",
				   ad_info.ports);
			seq_printf(seq, "\tActor Key: %d\n",
				   ad_info.actor_key);
			seq_printf(seq, "\tPartner Key: %d\n",
				   ad_info.partner_key);
			seq_printf(seq, "\tPartner Mac Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
				   ad_info.partner_system[0],
				   ad_info.partner_system[1],
				   ad_info.partner_system[2],
				   ad_info.partner_system[3],
				   ad_info.partner_system[4],
				   ad_info.partner_system[5]);
		}
	}
}

static void bond_info_show_slave(struct seq_file *seq, const struct slave *slave)
{
	seq_printf(seq, "\nSlave Interface: %s\n", slave->dev->name);
	seq_printf(seq, "MII Status: %s\n",
		   (slave->link == BOND_LINK_UP) ?  "up" : "down");
	seq_printf(seq, "Link Failure Count: %d\n",
		   slave->link_failure_count);

	if (app_abi_ver >= 1) {
		seq_printf(seq,
			   "Permanent HW addr: %02x:%02x:%02x:%02x:%02x:%02x\n",
			   slave->perm_hwaddr[0],
			   slave->perm_hwaddr[1],
			   slave->perm_hwaddr[2],
			   slave->perm_hwaddr[3],
			   slave->perm_hwaddr[4],
			   slave->perm_hwaddr[5]);
	}

	if (bond_mode == BOND_MODE_8023AD) {
		const struct aggregator *agg
			= SLAVE_AD_INFO(slave).port.aggregator;

		if (agg) {
			seq_printf(seq, "Aggregator ID: %d\n",
				   agg->aggregator_identifier);
		} else {
			seq_puts(seq, "Aggregator ID: N/A\n");
		}
	}
}

static int bond_info_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%s\n", version);
		bond_info_show_master(seq, seq->private);
	} else {
		bond_info_show_slave(seq, v);
	}

	return 0;
}

static struct seq_operations bond_info_seq_ops = {
	.start = bond_info_seq_start,
	.next  = bond_info_seq_next,
	.stop  = bond_info_seq_stop,
	.show  = bond_info_seq_show,
};

static int bond_info_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct proc_dir_entry *proc;
	int rc;

	rc = seq_open(file, &bond_info_seq_ops);
	if (!rc) {
		/* recover the pointer buried in proc_dir_entry data */
		seq = file->private_data;
		proc = PDE(inode);
		seq->private = proc->data;
	}
	return rc;
}

static struct file_operations bond_info_fops = {
	.owner	 = THIS_MODULE,
	.open    = bond_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static int bond_create_proc_info(struct bonding *bond)
{
	struct net_device *dev = bond->device;

	if (bond_proc_dir) {
		bond->bond_proc_file = create_proc_entry(dev->name,
							 S_IRUGO, 
							 bond_proc_dir);
		if (bond->bond_proc_file == NULL) {
			printk(KERN_WARNING
			       "%s: Cannot create /proc/net/bonding/%s\n", 
			       dev->name, dev->name);
		} else {
			bond->bond_proc_file->data = bond;
			bond->bond_proc_file->proc_fops = &bond_info_fops;
			bond->bond_proc_file->owner = THIS_MODULE;
			memcpy(bond->procdir_name, dev->name, IFNAMSIZ);
		}
	}

	return 0;
}

static void bond_destroy_proc_info(struct bonding *bond)
{
	if (bond_proc_dir && bond->bond_proc_file) {
		remove_proc_entry(bond->procdir_name, bond_proc_dir);
		memset(bond->procdir_name, 0, IFNAMSIZ);
		bond->bond_proc_file = NULL;
	}
}

/* Create the bonding directory under /proc/net, if doesn't exist yet.
 * Caller must hold rtnl_lock.
 */
static void bond_create_proc_dir(void)
{
	int len = strlen(DRV_NAME);

	for (bond_proc_dir = proc_net->subdir; bond_proc_dir;
	     bond_proc_dir = bond_proc_dir->next) {
		if ((bond_proc_dir->namelen == len) &&
		    !memcmp(bond_proc_dir->name, DRV_NAME, len)) {
			break;
		}
	}

	if (!bond_proc_dir) {
		bond_proc_dir = proc_mkdir(DRV_NAME, proc_net);
		if (bond_proc_dir) {
			bond_proc_dir->owner = THIS_MODULE;
		} else {
			printk(KERN_WARNING DRV_NAME
				": Warning: cannot create /proc/net/%s\n",
				DRV_NAME);
		}
	}
}

/* Destroy the bonding directory under /proc/net, if empty.
 * Caller must hold rtnl_lock.
 */
static void bond_destroy_proc_dir(void)
{
	struct proc_dir_entry *de;

	if (!bond_proc_dir) {
		return;
	}

	/* verify that the /proc dir is empty */
	for (de = bond_proc_dir->subdir; de; de = de->next) {
		/* ignore . and .. */
		if (*(de->name) != '.') {
			break;
		}
	}

	if (de) {
		if (bond_proc_dir->owner == THIS_MODULE) {
			bond_proc_dir->owner = NULL;
		}
	} else {
		remove_proc_entry(DRV_NAME, proc_net);
		bond_proc_dir = NULL;
	}
}
#endif /* CONFIG_PROC_FS */

/*
 * Change HW address
 *
 * Note that many devices must be down to change the HW address, and
 * downing the master releases all slaves.  We can make bonds full of
 * bonding devices to test this, however.
 */
static inline int
bond_set_mac_address(struct net_device *dev, void *addr)
{
	struct bonding *bond = dev->priv;
	struct sockaddr *sa = addr, tmp_sa;
	struct slave *slave;
	int error;

	dprintk(KERN_INFO "bond_set_mac_address %p %s\n", dev,
	       dev->name);

	if (!is_valid_ether_addr(sa->sa_data)) {
		return -EADDRNOTAVAIL;
	}

	for (slave = bond->prev; slave != (struct slave *)bond;
	     slave = slave->prev) {
		dprintk(KERN_INFO "bond_set_mac: slave %p %s\n", slave,
			slave->dev->name);
		if (slave->dev->set_mac_address == NULL) {
			error = -EOPNOTSUPP;
			dprintk(KERN_INFO "bond_set_mac EOPNOTSUPP %s\n",
				slave->dev->name);
			goto unwind;
		}

		error = slave->dev->set_mac_address(slave->dev, addr);
		if (error) {
			/* TODO: consider downing the slave 
			 * and retry ?
			 * User should expect communications
			 * breakage anyway until ARP finish
			 * updating, so...
			 */
			dprintk(KERN_INFO "bond_set_mac err %d %s\n",
				error, slave->dev->name);
			goto unwind;
		}
	}

	/* success */
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);
	return 0;

unwind:
	memcpy(tmp_sa.sa_data, dev->dev_addr, dev->addr_len);
	tmp_sa.sa_family = dev->type;

	for (slave = slave->next; slave != bond->next;
	     slave = slave->next) {
		int tmp_error;

		tmp_error = slave->dev->set_mac_address(slave->dev, &tmp_sa);
		if (tmp_error) {
			dprintk(KERN_INFO "bond_set_mac_address: "
				"unwind err %d dev %s\n",
				tmp_error, slave->dev->name);
		}
	}

	return error;
}

/*
 * Change the MTU of all of a master's slaves to match the master
 */
static inline int
bond_change_mtu(struct net_device *dev, int newmtu)
{
	bonding_t *bond = dev->priv;
	slave_t *slave;
	int error;

	dprintk(KERN_INFO "CM: b %p nm %d\n", bond, newmtu);
	for (slave = bond->prev; slave != (slave_t *)bond;
	     slave = slave->prev) {
		dprintk(KERN_INFO "CM: s %p s->p %p c_m %p\n", slave,
			slave->prev, slave->dev->change_mtu);
		if (slave->dev->change_mtu) {
			error = slave->dev->change_mtu(slave->dev, newmtu);
		} else {
			slave->dev->mtu = newmtu;
			error = 0;
		}

		if (error) {
			/* If we failed to set the slave's mtu to the new value
			 * we must abort the operation even in ACTIVE_BACKUP
			 * mode, because if we allow the backup slaves to have
			 * different mtu values than the active slave we'll
			 * need to change their mtu when doing a failover. That
			 * means changing their mtu from timer context, which
			 * is probably not a good idea.
			 */
			dprintk(KERN_INFO "bond_change_mtu err %d %s\n",
			       error, slave->dev->name);
			goto unwind;
		}
	}

	dev->mtu = newmtu;
	return 0;


unwind:
	for (slave = slave->next; slave != bond->next;
	     slave = slave->next) {

		if (slave->dev->change_mtu) {
			slave->dev->change_mtu(slave->dev, dev->mtu);
		} else {
			slave->dev->mtu = dev->mtu;
		}
	}

	return error;
}

/*
 * Change device name
 */
static inline int bond_event_changename(struct bonding *bond)
{
#ifdef CONFIG_PROC_FS
	bond_destroy_proc_info(bond);
	bond_create_proc_info(bond);
#endif

	return NOTIFY_DONE;
}

static int bond_master_netdev_event(unsigned long event, struct net_device *event_dev)
{
	struct bonding *bond, *event_bond = NULL;

	list_for_each_entry(bond, &bond_dev_list, bond_list) {
		if (bond == event_dev->priv) {
			event_bond = bond;
			break;
		}
	}

	if (event_bond == NULL) {
		return NOTIFY_DONE;
	}

	switch (event) {
	case NETDEV_CHANGENAME:
		return bond_event_changename(event_bond);
	case NETDEV_UNREGISTER:
		/*
		 * TODO: remove a bond from the list?
		 */
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int bond_slave_netdev_event(unsigned long event, struct net_device *event_dev)
{
	struct net_device *master = event_dev->master;

	switch (event) {
	case NETDEV_UNREGISTER:
		if (master != NULL) {
			bond_release(master, event_dev);
		}
		break;
	case NETDEV_CHANGE:
		/*
		 * TODO: is this what we get if somebody
		 * sets up a hierarchical bond, then rmmod's
		 * one of the slave bonding devices?
		 */
		break;
	case NETDEV_DOWN:
		/*
		 * ... Or is it this?
		 */
		break;
	case NETDEV_CHANGEMTU:
		/*
		 * TODO: Should slaves be allowed to
		 * independently alter their MTU?  For
		 * an active-backup bond, slaves need
		 * not be the same type of device, so
		 * MTUs may vary.  For other modes,
		 * slaves arguably should have the
		 * same MTUs. To do this, we'd need to
		 * take over the slave's change_mtu
		 * function for the duration of their
		 * servitude.
		 */
		break;
	case NETDEV_CHANGENAME:
		/*
		 * TODO: handle changing the primary's name
		 */
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

/*
 * bond_netdev_event: handle netdev notifier chain events.
 *
 * This function receives events for the netdev chain.  The caller (an
 * ioctl handler calling notifier_call_chain) holds the necessary
 * locks for us to safely manipulate the slave devices (RTNL lock,
 * dev_probe_lock).
 */
static int bond_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *event_dev = (struct net_device *)ptr;
	unsigned short flags;
	int res = NOTIFY_DONE;

	dprintk(KERN_INFO "bond_netdev_event n_b %p ev %lx ptr %p\n",
		this, event, ptr);

	flags = event_dev->flags & (IFF_MASTER | IFF_SLAVE);
	switch (flags) {
	case IFF_MASTER:
		res = bond_master_netdev_event(event, event_dev);
		break;
	case IFF_SLAVE:
		res = bond_slave_netdev_event(event, event_dev);
		break;
	default:
		/* A master that is also a slave ? */
		break;
	}

	return res;
}

static struct notifier_block bond_netdev_notifier = {
	.notifier_call = bond_netdev_event,
};

/* De-initialize device specific data.
 * Caller must hold rtnl_lock.
 */
static inline void bond_deinit(struct net_device *dev)
{
	struct bonding *bond = dev->priv;

	list_del(&bond->bond_list);

#ifdef CONFIG_PROC_FS
	bond_destroy_proc_info(bond);
#endif
}

/* Unregister and free all bond devices.
 * Caller must hold rtnl_lock.
 */
static void bond_free_all(void)
{
	struct bonding *bond, *nxt;

	list_for_each_entry_safe(bond, nxt, &bond_dev_list, bond_list) {
		struct net_device *dev = bond->device;

		unregister_netdevice(dev);
		bond_deinit(dev);
		free_netdev(dev);
	}

#ifdef CONFIG_PROC_FS
	bond_destroy_proc_dir();
#endif
}

/*
 * Does not allocate but creates a /proc entry.
 * Allowed to fail.
 */
static int __init bond_init(struct net_device *dev)
{
	struct bonding *bond;
	int count;

#ifdef BONDING_DEBUG
	printk (KERN_INFO "Begin bond_init for %s\n", dev->name);
#endif
	bond = dev->priv;

	/* initialize rwlocks */
	rwlock_init(&bond->lock);
	rwlock_init(&bond->ptrlock);

	/* Initialize pointers */
	bond->next = bond->prev = (slave_t *)bond;
	bond->current_slave = NULL;
	bond->current_arp_slave = NULL;
	bond->device = dev;

	/* Initialize the device structure. */
	dev->set_mac_address = bond_set_mac_address;

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
	case BOND_MODE_8023AD:
		dev->hard_start_xmit = bond_3ad_xmit_xor;
		break;
	case BOND_MODE_TLB:
	case BOND_MODE_ALB:
		dev->hard_start_xmit = bond_alb_xmit;
		dev->set_mac_address = bond_alb_set_mac_address;
		break;
	default:
		printk(KERN_ERR "Unknown bonding mode %d\n", bond_mode);
		return -EINVAL;
	}

	dev->get_stats = bond_get_stats;
	dev->open = bond_open;
	dev->stop = bond_close;
	dev->set_multicast_list = set_multicast_list;
	dev->do_ioctl = bond_ioctl;
	dev->change_mtu = bond_change_mtu;
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
	bond_create_proc_info(bond);
#endif

	list_add_tail(&bond->bond_list, &bond_dev_list);

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

	if (USES_PRIMARY(bond_mode)) {
		multicast_mode = BOND_MULTICAST_ACTIVE;
	} else {
		multicast_mode = BOND_MULTICAST_ALL;
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

	if (lacp_rate) {
		if (bond_mode != BOND_MODE_8023AD) {
			printk(KERN_WARNING
			       "lacp_rate param is irrelevant in mode %s\n",
			       bond_mode_name());
		} else {
			lacp_fast = bond_parse_parm(lacp_rate, bond_lacp_tbl);
			if (lacp_fast == -1) {
				printk(KERN_WARNING
			       	       "bonding_init(): Invalid lacp rate "
				       "\"%s\"\n",
				       lacp_rate == NULL ? "NULL" : lacp_rate);

				return -EINVAL;
			}
		}
	}

	if (max_bonds < 1 || max_bonds > INT_MAX) {
		printk(KERN_WARNING 
		       "bonding_init(): max_bonds (%d) not in range %d-%d, "
		       "so it was reset to BOND_DEFAULT_MAX_BONDS (%d)",
		       max_bonds, 1, INT_MAX, BOND_DEFAULT_MAX_BONDS);
		max_bonds = BOND_DEFAULT_MAX_BONDS;
	}

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

	/* reset values for 802.3ad */
	if (bond_mode == BOND_MODE_8023AD) {
		if (arp_interval != 0) {
			printk(KERN_WARNING "bonding_init(): ARP monitoring"
			       "can't be used simultaneously with 802.3ad, "
			       "disabling ARP monitoring\n");
			arp_interval = 0;
		}

		if (miimon == 0) {
			printk(KERN_ERR
			       "bonding_init(): miimon must be specified, "
			       "otherwise bonding will not detect link failure, "
			       "speed and duplex which are essential "
			       "for 802.3ad operation\n");
			printk(KERN_ERR "Forcing miimon to 100msec\n");
			miimon = 100;
		}

		if (multicast_mode != BOND_MULTICAST_ALL) {
			printk(KERN_ERR
			       "bonding_init(): Multicast mode must "
			       "be set to ALL for 802.3ad\n");
			printk(KERN_ERR "Forcing Multicast mode to ALL\n");
			multicast_mode = BOND_MULTICAST_ALL;
		}
	}

	/* reset values for TLB/ALB */
	if ((bond_mode == BOND_MODE_TLB) ||
	    (bond_mode == BOND_MODE_ALB)) {
		if (miimon == 0) {
			printk(KERN_ERR
			       "bonding_init(): miimon must be specified, "
			       "otherwise bonding will not detect link failure "
			       "and link speed which are essential "
			       "for TLB/ALB load balancing\n");
			printk(KERN_ERR "Forcing miimon to 100msec\n");
			miimon = 100;
		}

		if (multicast_mode != BOND_MULTICAST_ACTIVE) {
			printk(KERN_ERR
			       "bonding_init(): Multicast mode must "
			       "be set to ACTIVE for TLB/ALB\n");
			printk(KERN_ERR "Forcing Multicast mode to ACTIVE\n");
			multicast_mode = BOND_MULTICAST_ACTIVE;
		}
	}

	if (bond_mode == BOND_MODE_ALB) {
		printk(KERN_INFO
		       "In ALB mode you might experience client disconnections"
		       " upon reconnection of a link if the bonding module"
		       " updelay parameter (%d msec) is incompatible with the"
		       " forwarding delay time of the switch\n", updelay);
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
		/* not complete check, but should be good enough to
		   catch mistakes */
		if (!isdigit(arp_ip_target[arp_ip_count][0])) { 
                        printk(KERN_WARNING
                               "bonding_init(): bad arp_ip_target module "
                               "parameter (%s), ARP monitoring will not be "
                               "performed\n",
                               arp_ip_target[arp_ip_count]);
                        arp_interval = 0;
		} else { 
			u32 ip = in_aton(arp_ip_target[arp_ip_count]); 
			arp_target[arp_ip_count] = ip;
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

	if ((primary != NULL) && !USES_PRIMARY(bond_mode)) {
		/* currently, using a primary only makes sense
		 * in active backup, TLB or ALB modes
		 */
		printk(KERN_WARNING 
		       "bonding_init(): %s primary device specified but has "
		       "no effect in %s mode\n",
		       primary, bond_mode_name());
		primary = NULL;
	}

	rtnl_lock();

#ifdef CONFIG_PROC_FS
	bond_create_proc_dir();
#endif

	err = 0;
	for (no = 0; no < max_bonds; no++) {
		struct net_device *dev;

		dev = alloc_netdev(sizeof(struct bonding), "", ether_setup);
		if (!dev) {
			err = -ENOMEM;
			goto out_err;
		}

		err = dev_alloc_name(dev, "bond%d");
		if (err < 0) {
			free_netdev(dev);
			goto out_err;
		}

		/* bond_init() must be called after dev_alloc_name() (for the
		 * /proc files), but before register_netdevice(), because we
		 * need to set function pointers.
		 */
		err = bond_init(dev);
		if (err < 0) {
			free_netdev(dev);
			goto out_err;
		}

		SET_MODULE_OWNER(dev);

		err = register_netdevice(dev);
		if (err < 0) {
			bond_deinit(dev);
			free_netdev(dev);
			goto out_err;
		}
	}

	rtnl_unlock();
	register_netdevice_notifier(&bond_netdev_notifier);

	return 0;

out_err:
	/* free and unregister all bonds that were successfully added */
	bond_free_all();

	rtnl_unlock();

	return err;
}

static void __exit bonding_exit(void)
{
	unregister_netdevice_notifier(&bond_netdev_notifier);

	rtnl_lock();
	bond_free_all();
	rtnl_unlock();
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
