/* orinoco.c 0.08a	- (formerly known as dldwd_cs.c and orinoco_cs.c)
 *
 * A driver for "Hermes" chipset based PCMCIA wireless adaptors, such
 * as the Lucent WavelanIEEE/Orinoco cards and their OEM (Cabletron/
 * EnteraSys RoamAbout 802.11, ELSA Airlancer, Melco Buffalo and others).
 * It should also be usable on various Prism II based cards such as the
 * Linksys, D-Link and Farallon Skyline. It should also work on Symbol
 * cards such as the 3Com AirConnect and Ericsson WLAN.
 *
 * Copyright (C) 2000 David Gibson, Linuxcare Australia <hermes@gibson.dropbear.id.au>
 *	With some help from :
 * Copyright (C) 2001 Jean Tourrilhes, HP Labs <jt@hpl.hp.com>
 * Copyright (C) 2001 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 * Based on dummy_cs.c 1.27 2000/06/12 21:27:25
 *
 * Portions based on wvlan_cs.c 1.0.6, Copyright Andreas Neuhaus <andy@fasta.fh-dortmund.de>
 *      http://www.fasta.fh-dortmund.de/users/andy/wvlan/
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David
 * A. Hinds are Copyright (C) 1999 David A. Hinds.  All Rights
 * Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

/*
 * Tentative changelog...
 *
 * v0.01 -> v0.02 - 21/3/2001 - Jean II
 *	o Allow to use regular ethX device name instead of dldwdX
 *	o Warning on IBSS with ESSID=any for firmware 6.06
 *	o Put proper range.throughput values (optimistic)
 *	o IWSPY support (IOCTL and stat gather in Rx path)
 *	o Allow setting frequency in Ad-Hoc mode
 *	o Disable WEP setting if !has_wep to work on old firmware
 *	o Fix txpower range
 *	o Start adding support for Samsung/Compaq firmware
 *
 * v0.02 -> v0.03 - 23/3/2001 - Jean II
 *	o Start adding Symbol support - need to check all that
 *	o Fix Prism2/Symbol WEP to accept 128 bits keys
 *	o Add Symbol WEP (add authentication type)
 *	o Add Prism2/Symbol rate
 *	o Add PM timeout (holdover duration)
 *	o Enable "iwconfig eth0 key off" and friends (toggle flags)
 *	o Enable "iwconfig eth0 power unicast/all" (toggle flags)
 *	o Try with an intel card. It report firmware 1.01, behave like
 *	  an antiquated firmware, however on windows it says 2.00. Yuck !
 *	o Workaround firmware bug in allocate buffer (Intel 1.01)
 *	o Finish external renaming to orinoco...
 *	o Testing with various Wavelan firmwares
 *
 * v0.03 -> v0.04 - 30/3/2001 - Jean II
 *	o Update to Wireless 11 -> add retry limit/lifetime support
 *	o Tested with a D-Link DWL 650 card, fill in firmware support
 *	o Warning on Vcc mismatch (D-Link 3.3v card in Lucent 5v only slot)
 *	o Fixed the Prims2 WEP bugs that I introduced in v0.03 :-(
 *	  It work on D-Link *only* after a tcpdump. Weird...
 *	  And still doesn't work on Intel card. Grrrr...
 *	o Update the mode after a setport3
 *	o Add preamble setting for Symbol cards (not yet enabled)
 *	o Don't complain as much about Symbol cards...
 *
 * v0.04 -> v0.04b - 22/4/2001 - David Gibson
 *      o Removed the 'eth' parameter - always use ethXX as the
 *        interface name instead of dldwdXX.  The other was racy
 *        anyway.
 *	o Clean up RID definitions in hermes.h, other cleanups
 *
 * v0.04b -> v0.04c - 24/4/2001 - Jean II
 *	o Tim Hurley <timster@seiki.bliztech.com> reported a D-Link card
 *	  with vendor 02 and firmware 0.08. Added in the capabilities...
 *	o Tested Lucent firmware 7.28, everything works...
 *
 * v0.04c -> v0.05 - 3/5/2001 - Benjamin Herrenschmidt
 *	o Spin-off Pcmcia code. This file is renamed orinoco.c,
 *	  and orinoco_cs.c now contains only the Pcmcia specific stuff
 *	o Add Airport driver support on top of orinoco.c (see airport.c)
 *
 * v0.05 -> v0.05a - 4/5/2001 - Jean II
 *	o Revert to old Pcmcia code to fix breakage of Ben's changes...
 *
 * v0.05a -> v0.05b - 4/5/2001 - Jean II
 *	o add module parameter 'ignore_cis_vcc' for D-Link @ 5V
 *	o D-Link firmware doesn't support multicast. We just print a few
 *	  error messages, but otherwise everything works...
 *	o For David : set/getport3 works fine, just upgrade iwpriv...
 *
 * v0.05b -> v0.05c - 5/5/2001 - Benjamin Herrenschmidt
 *	o Adapt airport.c to latest changes in orinoco.c
 *	o Remove deferred power enabling code
 *
 * v0.05c -> v0.05d - 5/5/2001 - Jean II
 *	o Workaround to SNAP decapsulate frame from LinkSys AP
 *	  original patch from : Dong Liu <dliu@research.bell-labs.com>
 *	  (note : the memcmp bug was mine - fixed)
 *	o Remove set_retry stuff, no firmware support it (bloat--).
 *
 * v0.05d -> v0.06 - 25/5/2001 - Jean II
 *		Original patch from "Hong Lin" <alin@redhat.com>,
 *		"Ian Kinner" <ikinner@redhat.com>
 *		and "David Smith" <dsmith@redhat.com>
 *	o Init of priv->tx_rate_ctrl in firmware specific section.
 *	o Prism2/Symbol rate, upto should be 0xF and not 0x15. Doh !
 *	o Spectrum card always need cor_reset (for every reset)
 *	o Fix cor_reset to not loose bit 7 in the register
 *	o flush_stale_links to remove zombie Pcmcia instances
 *	o Ack previous hermes event before reset
 *		Me (with my little hands)
 *	o Allow orinoco.c to call cor_reset via priv->card_reset_handler
 *	o Add priv->need_card_reset to toggle this feature
 *	o Fix various buglets when setting WEP in Symbol firmware
 *	  Now, encryption is fully functional on Symbol cards. Youpi !
 *
 * v0.06 -> v0.06b - 25/5/2001 - Jean II
 *	o IBSS on Symbol use port_mode = 4. Please don't ask...
 *
 * v0.06b -> v0.06c - 29/5/2001 - Jean II
 *	o Show first spy address in /proc/net/wireless for IBSS mode as well
 *
 * v0.06c -> v0.06d - 6/7/2001 - David Gibson
 *      o Change a bunch of KERN_INFO messages to KERN_DEBUG, as per Linus'
 *        wishes to reduce the number of unecessary messages.
 *	o Removed bogus message on CRC error.
 *	o Merged fixeds for v0.08 Prism 2 firmware from William Waghorn
 *	  <willwaghorn@yahoo.co.uk>
 *	o Slight cleanup/re-arrangement of firmware detection code.
 *
 * v0.06d -> v0.06e - 1/8/2001 - David Gibson
 *	o Removed some redundant global initializers (orinoco_cs.c).
 *	o Added some module metadataa
 *
 * v0.06e -> v0.06f - 14/8/2001 - David Gibson
 *	o Wording fix to license
 *	o Added a 'use_alternate_encaps' module parameter for APs which need an
 *	  oui of 00:00:00.  We really need a better way of handling this, but
 *	  the module flag is better than nothing for now.
 *
 * v0.06f -> v0.07 - 20/8/2001 - David Gibson
 *	o Removed BAP error retries from hermes_bap_seek().  For Tx we now
 *	  let the upper layers handle the retry, we retry explicitly in the
 *	  Rx path, but don't make as much noise about it.
 *	o Firmware detection cleanups.
 *
 * v0.07 -> v0.07a - 1/10/3001 - Jean II
 *	o Add code to read Symbol firmware revision, inspired by latest code
 *	  in Spectrum24 by Lee John Keyser-Allen - Thanks Lee !
 *	o Thanks to Jared Valentine <hidden@xmission.com> for "providing" me
 *	  a 3Com card with a recent firmware, fill out Symbol firmware
 *	  capabilities of latest rev (2.20), as well as older Symbol cards.
 *	o Disable Power Management in newer Symbol firmware, the API 
 *	  has changed (documentation needed).
 *
 * v0.07a -> v0.08 - 3/10/2001 - David Gibson
 *	o Fixed a possible buffer overrun found by the Stanford checker (in
 *	  dldwd_ioctl_setiwencode()).  Can only be called by root anyway, so not
 *	  a big problem.
 *	o Turned has_big_wep on for Intersil cards.  That's not true for all of
 *	  them but we should at least let the capable ones try.
 *	o Wait for BUSY to clear at the beginning of hermes_bap_seek().  I
 *	  realised that my assumption that the driver's serialization
 *	  would prevent the BAP being busy on entry was possibly false, because
 *	  things other than seeks may make the BAP busy.
 *	o Use "alternate" (oui 00:00:00) encapsulation by default.
 *	  Setting use_old_encaps will mimic the old behaviour, but I think we
 *	  will be able to eliminate this.
 *	o Don't try to make __initdata const (the version string).  This can't
 *	  work because of the way the __initdata sectioning works.
 *	o Added MODULE_LICENSE tags.
 *	o Support for PLX (transparent PCMCIA->PCI brdge) cards.
 *	o Changed to using the new type-facist min/max.
 *
 * v0.08 -> v0.08a - 9/10/2001 - David Gibson
 *	o Inserted some missing acknowledgements/info into the Changelog.
 *	o Fixed some bugs in the normalisation of signel level reporting.
 *	o Fixed bad bug in WEP key handling on Intersil and Symbol firmware,
 *	  which led to an instant crash on big-endian machines.
 *
 * TODO - Jean II
 *	o inline functions (lots of candidate, need to reorder code)
 *	o Test PrismII/Symbol cards & firmware versions
 *	o Mini-PCI support (some people have reported success - JII)
 *	o Find and kill remaining Tx timeout problems
 */
/* Notes on locking:
 *
 * The basic principle of operation is that everything except the
 * interrupt handler is serialized through a single spinlock in the
 * dldwd_priv_t structure, using dldwd_lock() and
 * dldwd_unlock() (which in turn use spin_lock_bh() and spin_unlock_bh()).
 *
 * The kernel's IRQ handling stuff ensures that the interrupt handler
 * does not re-enter itself. The interrupt handler is written such
 * that everything it does is safe without a lock: chiefly this means
 * that the Rx path uses one of the Hermes chipset's BAPs while
 * everything else uses the other.
 *
 * Actually, the current updating of the statistics from the interrupt
 * handler is unsafe.  However all it can do is perturb the
 * packet/byte counts slightly, so we just put up with it.  We could
 * fix this to use atomic types, but it's probably not worth it.
 *
 * The big exception is that that we don't want the irq handler
 * running when we actually reset or shut down the card, because
 * strange things might happen (probably the worst would be one packet
 * of garbage, but you can't be too careful). For this we use
 * __dldwd_stop_irqs() which will set a flag to disable the interrupt
 * handler, and wait for any outstanding instances of the handler to
 * complete. THIS WILL LOSE INTERRUPTS! so it shouldn't be used except
 * for resets, where losing a few interrupts is acceptable. */

#include <linux/config.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/list.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/bus_ops.h>

#include "hermes.h"
#include "orinoco.h"

static char version[] __initdata = "orinoco.c 0.08a (David Gibson <hermes@gibson.dropbear.id.au> and others)";
MODULE_AUTHOR("David Gibson <hermes@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("Driver for Lucent Orinoco, Prism II based and similar wireless cards");
MODULE_LICENSE("Dual MPL/GPL");

/* Level of debugging. Used in the macros in orinoco.h */
#ifdef ORINOCO_DEBUG
int dldwd_debug = ORINOCO_DEBUG;
MODULE_PARM(dldwd_debug, "i");
#endif

int use_old_encaps = 0;
MODULE_PARM(use_old_encaps, "i");

#define SYMBOL_MAX_VER_LEN	(14)

const long channel_frequency[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};

#define NUM_CHANNELS ( sizeof(channel_frequency) / sizeof(channel_frequency[0]) )

/* This tables gives the actual meanings of the bitrate IDs returned by the firmware.
   It gives the rate in halfMb/s, negative indicates auto mode */
const int rate_list[] = { 0, 2, 4, -22, 11, 22, -4, -11, 0, 0, 0, 0};

#define NUM_RATES (sizeof(rate_list) / sizeof(rate_list[0]))

struct p80211_hdr {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u16 data_len;
} __attribute__ ((packed));

/* Frame control field constants */
#define DLDWD_FCTL_VERS			0x0002
#define DLDWD_FCTL_FTYPE		0x000c
#define DLDWD_FCTL_STYPE		0x00f0
#define DLDWD_FCTL_TODS			0x0100
#define DLDWD_FCTL_FROMDS		0x0200
#define DLDWD_FCTL_MOREFRAGS		0x0400
#define DLDWD_FCTL_RETRY		0x0800
#define DLDWD_FCTL_PM			0x1000
#define DLDWD_FCTL_MOREDATA		0x2000
#define DLDWD_FCTL_WEP			0x4000
#define DLDWD_FCTL_ORDER		0x8000

#define DLDWD_FTYPE_MGMT		0x0000
#define DLDWD_FTYPE_CTL			0x0004
#define DLDWD_FTYPE_DATA		0x0008

struct p8022_hdr {
	u8 dsap;
	u8 ssap;
	u8 ctrl;
	u8 oui[3];
} __attribute__ ((packed));

struct dldwd_frame_hdr {
	hermes_frame_desc_t desc;
	struct p80211_hdr p80211;
	struct ethhdr p8023;
	struct p8022_hdr p8022;
	u16 ethertype;
} __attribute__ ((packed));

#define P8023_OFFSET		(sizeof(hermes_frame_desc_t) + \
				sizeof(struct p80211_hdr))
#define ENCAPS_OVERHEAD		(sizeof(struct p8022_hdr) + 2)

/* 802.2 LLL header SNAP used for SNAP encapsulation over 802.11 */
struct p8022_hdr encaps_hdr = {
	0xaa, 0xaa, 0x03, {0x00, 0x00, 0x00}
};

struct p8022_hdr old_encaps_hdr = {
	0xaa, 0xaa, 0x03, {0x00, 0x00, 0xf8}
};

/* How many times to retry if we get an EIO reading the BAP in the Rx path */
#define RX_EIO_RETRY		10

typedef struct dldwd_commsqual {
	u16 qual, signal, noise;
} __attribute__ ((packed)) dldwd_commsqual_t;

/*
 * Function prototypes
 */

static void dldwd_stat_gather(struct net_device *dev,
			      struct sk_buff *skb,
			      struct dldwd_frame_hdr *hdr);

static struct net_device_stats *dldwd_get_stats(struct net_device *dev);
static struct iw_statistics *dldwd_get_wireless_stats(struct net_device *dev);

/* Hardware control routines */

static int __dldwd_hw_reset(dldwd_priv_t *priv);
static int __dldwd_hw_setup_wep(dldwd_priv_t *priv);
static int dldwd_hw_get_bssid(dldwd_priv_t *priv, char buf[ETH_ALEN]);
static int dldwd_hw_get_essid(dldwd_priv_t *priv, int *active, char buf[IW_ESSID_MAX_SIZE+1]);
static long dldwd_hw_get_freq(dldwd_priv_t *priv);
static int dldwd_hw_get_bitratelist(dldwd_priv_t *priv, int *numrates,
				    s32 *rates, int max);

/* Interrupt handling routines */
static void __dldwd_ev_tick(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_wterr(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_infdrop(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_info(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_rx(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_txexc(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_tx(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_alloc(dldwd_priv_t *priv, hermes_t *hw);

static int dldwd_ioctl_getiwrange(struct net_device *dev, struct iw_point *rrq);
static int dldwd_ioctl_setiwencode(struct net_device *dev, struct iw_point *erq);
static int dldwd_ioctl_getiwencode(struct net_device *dev, struct iw_point *erq);
static int dldwd_ioctl_setessid(struct net_device *dev, struct iw_point *erq);
static int dldwd_ioctl_getessid(struct net_device *dev, struct iw_point *erq);
static int dldwd_ioctl_setnick(struct net_device *dev, struct iw_point *nrq);
static int dldwd_ioctl_getnick(struct net_device *dev, struct iw_point *nrq);
static int dldwd_ioctl_setfreq(struct net_device *dev, struct iw_freq *frq);
static int dldwd_ioctl_getsens(struct net_device *dev, struct iw_param *srq);
static int dldwd_ioctl_setsens(struct net_device *dev, struct iw_param *srq);
static int dldwd_ioctl_setrts(struct net_device *dev, struct iw_param *rrq);
static int dldwd_ioctl_setfrag(struct net_device *dev, struct iw_param *frq);
static int dldwd_ioctl_getfrag(struct net_device *dev, struct iw_param *frq);
static int dldwd_ioctl_setrate(struct net_device *dev, struct iw_param *frq);
static int dldwd_ioctl_getrate(struct net_device *dev, struct iw_param *frq);
static int dldwd_ioctl_setpower(struct net_device *dev, struct iw_param *prq);
static int dldwd_ioctl_getpower(struct net_device *dev, struct iw_param *prq);
static int dldwd_ioctl_setport3(struct net_device *dev, struct iwreq *wrq);
static int dldwd_ioctl_getport3(struct net_device *dev, struct iwreq *wrq);
static void __dldwd_set_multicast_list(struct net_device *dev);

/* /proc debugging stuff */
static int dldwd_proc_init(void);
static void dldwd_proc_cleanup(void);

/*
 * Inline functions
 */
static inline void
dldwd_lock(dldwd_priv_t *priv)
{
	spin_lock_bh(&priv->lock);
}

static inline void
dldwd_unlock(dldwd_priv_t *priv)
{
	spin_unlock_bh(&priv->lock);
}

static inline int
dldwd_irqs_allowed(dldwd_priv_t *priv)
{
	return test_bit(DLDWD_STATE_DOIRQ, &priv->state);
}

static inline void
__dldwd_stop_irqs(dldwd_priv_t *priv)
{
	hermes_t *hw = &priv->hw;

	hermes_set_irqmask(hw, 0);
	clear_bit(DLDWD_STATE_DOIRQ, &priv->state);
	while (test_bit(DLDWD_STATE_INIRQ, &priv->state))
		;
}

static inline void
__dldwd_start_irqs(dldwd_priv_t *priv, u16 irqmask)
{
	hermes_t *hw = &priv->hw;

	TRACE_ENTER(priv->ndev.name);

	__cli();
	set_bit(DLDWD_STATE_DOIRQ, &priv->state);
	hermes_set_irqmask(hw, irqmask);
	__sti();

	TRACE_EXIT(priv->ndev.name);
}

static inline void
set_port_type(dldwd_priv_t *priv)
{
	switch (priv->iw_mode) {
	case IW_MODE_INFRA:
		priv->port_type = 1;
		priv->allow_ibss = 0;
		break;
	case IW_MODE_ADHOC:
		if (priv->prefer_port3) {
			priv->port_type = 3;
			priv->allow_ibss = 0;
		} else {
			priv->port_type = priv->ibss_port;
			priv->allow_ibss = 1;
		}
		break;
	default:
		printk(KERN_ERR "%s: Invalid priv->iw_mode in set_port_type()\n",
		       priv->ndev.name);
	}
}

extern void
dldwd_set_multicast_list(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;

	dldwd_lock(priv);
	__dldwd_set_multicast_list(dev);
	dldwd_unlock(priv);
}

/*
 * Hardware control routines
 */

static int
__dldwd_hw_reset(dldwd_priv_t *priv)
{
	hermes_t *hw = &priv->hw;
	int err;

	if (! priv->broken_reset)
		return hermes_reset(hw);
	else {
		hw->inten = 0;
		hermes_write_regn(hw, INTEN, 0);
		err = hermes_disable_port(hw, 0);
		hermes_write_regn(hw, EVACK, 0xffff);
		return err;
	}
}

void
dldwd_shutdown(dldwd_priv_t *priv)
{
/* 	hermes_t *hw = &priv->hw; */
	int err = 0;

	TRACE_ENTER(priv->ndev.name);

	dldwd_lock(priv);
	__dldwd_stop_irqs(priv);

	err = __dldwd_hw_reset(priv);
	if (err && err != -ENODEV) /* If the card is gone, we don't care about shutting it down */
		printk(KERN_ERR "%s: Error %d shutting down Hermes chipset\n", priv->ndev.name, err);

	dldwd_unlock(priv);

	TRACE_EXIT(priv->ndev.name);
}

int
dldwd_reset(dldwd_priv_t *priv)
{
	struct net_device *dev = &priv->ndev;
	hermes_t *hw = &priv->hw;
	int err = 0;
	hermes_id_t idbuf;
	int frame_size;

	TRACE_ENTER(priv->ndev.name);

	/* Stop other people bothering us */
	dldwd_lock(priv);
	__dldwd_stop_irqs(priv);

	/* Check if we need a card reset */
	if((priv->need_card_reset) && (priv->card_reset_handler != NULL))
		priv->card_reset_handler(priv);

	/* Do standard firmware reset if we can */
	err = __dldwd_hw_reset(priv);
	if (err)
		goto out;

	frame_size = TX_NICBUF_SIZE;
	/* This stupid bug is present in Intel firmware 1.10, and
	 * may be fixed in later firmwares - Jean II */
	if(priv->broken_allocate)
		frame_size = TX_NICBUF_SIZE_BUG;
	err = hermes_allocate(hw, frame_size, &priv->txfid);
	if (err)
		goto out;

	/* Now set up all the parameters on the card */
	
	/* Set up the link mode */
	
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PORTTYPE, priv->port_type);
	if (err)
		goto out;
	if (priv->has_ibss) {
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_CREATEIBSS,
					   priv->allow_ibss);
		if (err)
			goto out;
		if((strlen(priv->desired_essid) == 0) && (priv->allow_ibss)
		   && (!priv->has_ibss_any)) {
			printk(KERN_WARNING "%s: This firmware requires an \
ESSID in IBSS-Ad-Hoc mode.\n", dev->name);
			/* With wvlan_cs, in this case, we would crash.
			 * hopefully, this driver will behave better...
			 * Jean II */
		}
	}

	/* Set up encryption */
	if (priv->has_wep) {
		err = __dldwd_hw_setup_wep(priv);
		if (err) {
			printk(KERN_ERR "%s: Error %d activating WEP.\n",
			       dev->name, err);
			goto out;
		}
	}

	/* Set the desired ESSID */
	idbuf.len = cpu_to_le16(strlen(priv->desired_essid));
	memcpy(&idbuf.val, priv->desired_essid, sizeof(idbuf.val));
	err = hermes_write_ltv(hw, USER_BAP, (priv->port_type == 3) ?
			       HERMES_RID_CNF_OWN_SSID : HERMES_RID_CNF_DESIRED_SSID,
			       HERMES_BYTES_TO_RECLEN(strlen(priv->desired_essid)+2),
			       &idbuf);
	if (err)
		goto out;

	/* Set the station name */
	idbuf.len = cpu_to_le16(strlen(priv->nick));
	memcpy(&idbuf.val, priv->nick, sizeof(idbuf.val));
	err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNF_NICKNAME,
			       HERMES_BYTES_TO_RECLEN(strlen(priv->nick)+2),
			       &idbuf);
	if (err)
		goto out;

	/* Set the channel/frequency */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_CHANNEL, priv->channel);
	if (err)
		goto out;

	/* Set AP density */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_SYSTEM_SCALE, priv->ap_density);
	if (err)
		goto out;

	/* Set RTS threshold */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_RTS_THRESH, priv->rts_thresh);
	if (err)
		goto out;

	/* Set fragmentation threshold or MWO robustness */
	if (priv->has_mwo)
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNF_MWO_ROBUST, priv->mwo_robust);
	else
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNF_FRAG_THRESH, priv->frag_thresh);
	if (err)
		goto out;

	/* Set bitrate */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_TX_RATE_CTRL,
				   priv->tx_rate_ctrl);
	if (err)
		goto out;

	/* Set power management */
	if (priv->has_pm) {
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_ENABLE,
					   priv->pm_on);
		if (err)
			goto out;
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_MCAST_RX,
					   priv->pm_mcast);
		if (err)
			goto out;
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_PERIOD,
					   priv->pm_period);
		if (err)
			goto out;
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_HOLDOVER,
					   priv->pm_timeout);
		if (err)
			goto out;
	}

	/* Set preamble - only for Symbol so far... */
	if (priv->has_preamble) {
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_SYMBOL_PREAMBLE,
					   priv->preamble);
		if (err) {
			printk(KERN_WARNING "%s: Can't set preamble!\n", dev->name);
			goto out;
		}
	}

	/* Set promiscuity / multicast*/
	priv->promiscuous = 0;
	priv->allmulti = 0;
	priv->mc_count = 0;
	__dldwd_set_multicast_list(dev);
	
	err = hermes_enable_port(hw, DLDWD_MACPORT);
	if (err)
		goto out;
	
	__dldwd_start_irqs(priv, HERMES_EV_RX | HERMES_EV_ALLOC |
			   HERMES_EV_TX | HERMES_EV_TXEXC |
			   HERMES_EV_WTERR | HERMES_EV_INFO |
			   HERMES_EV_INFDROP);

 out:
	dldwd_unlock(priv);

	TRACE_EXIT(priv->ndev.name);

	return err;
}

static int __dldwd_hw_setup_wep(dldwd_priv_t *priv)
{
	hermes_t *hw = &priv->hw;
	int err = 0;
	int	master_wep_flag;
	int	auth_flag;

	TRACE_ENTER(priv->ndev.name);

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_LUCENT: /* Lucent style WEP */
		if (priv->wep_on) {
			err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_TX_KEY, priv->tx_key);
			if (err)
				return err;
			
			err = HERMES_WRITE_RECORD(hw, USER_BAP, HERMES_RID_CNF_KEYS, &priv->keys);
			if (err)
				return err;
		}
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_WEP_ON, priv->wep_on);
		if (err)
			return err;
		break;

	case FIRMWARE_TYPE_INTERSIL: /* Intersil style WEP */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style WEP */
		master_wep_flag = 0;		/* Off */
		if (priv->wep_on) {
/*  			int keylen; */
			int i;

			/* Fudge around firmware weirdness */
/*  			keylen = priv->keys[priv->tx_key].len; */

			/* Write all 4 keys */
			for(i = 0; i < MAX_KEYS; i++) {
				int keylen = le16_to_cpu(priv->keys[i].len);

				if (keylen > LARGE_KEY_SIZE) {
					printk(KERN_ERR "%s: BUG: Key %d has oversize length %d.\n",
					       priv->ndev.name, i, keylen);
					return -E2BIG;
				}

				printk("About to write key %d, keylen=%d\n",
				       i, keylen);				     
				err = hermes_write_ltv(hw, USER_BAP,
						       HERMES_RID_CNF_INTERSIL_KEY0 + i,
						       HERMES_BYTES_TO_RECLEN(keylen),
						       priv->keys[i].data);
				if (err)
					return err;
			}

			/* Write the index of the key used in transmission */
			err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_INTERSIL_TX_KEY,
						   priv->tx_key);
			if (err)
				return err;

			/* Authentication is where Intersil and Symbol
			 * firmware differ... */
			if (priv->firmware_type == FIRMWARE_TYPE_SYMBOL) {
				/* Symbol cards : set the authentication :
				 * 0 -> no encryption, 1 -> open,
				 * 2 -> shared key
				 * 3 -> shared key 128 -> AP only */
				if(priv->wep_restrict)
					auth_flag = 2;
				else
					auth_flag = 1;
				err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_SYMBOL_AUTH_TYPE, auth_flag);
				if (err)
					return err;
				/* Master WEP setting is always 3 */
				master_wep_flag = 3;
			} else {
				/* Prism2 card : we need to modify master
				 * WEP setting */
				if(priv->wep_restrict)
					master_wep_flag = 3;
				else
					master_wep_flag = 1;
			}
		}
		
		/* Master WEP setting : on/off */
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_INTERSIL_WEP_ON, master_wep_flag);
		if (err)
			return err;	

		break;

	default:
		if (priv->wep_on) {
			printk(KERN_ERR "%s: WEP enabled, although not supported!\n",
			       priv->ndev.name);
			return -EINVAL;
		}
	}

	TRACE_EXIT(priv->ndev.name);

	return 0;
}

static int dldwd_hw_get_bssid(dldwd_priv_t *priv, char buf[ETH_ALEN])
{
	hermes_t *hw = &priv->hw;
	int err = 0;

	dldwd_lock(priv);

	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENT_BSSID,
			      ETH_ALEN, NULL, buf);

	dldwd_unlock(priv);

	return err;
}

static int dldwd_hw_get_essid(dldwd_priv_t *priv, int *active,
			      char buf[IW_ESSID_MAX_SIZE+1])
{
	hermes_t *hw = &priv->hw;
	int err = 0;
	hermes_id_t essidbuf;
	char *p = (char *)(&essidbuf.val);
	int len;

	TRACE_ENTER(priv->ndev.name);

	dldwd_lock(priv);

	if (strlen(priv->desired_essid) > 0) {
		/* We read the desired SSID from the hardware rather
		   than from priv->desired_essid, just in case the
		   firmware is allowed to change it on us. I'm not
		   sure about this */
		/* My guess is that the OWN_SSID should always be whatever
		 * we set to the card, whereas CURRENT_SSID is the one that
		 * may change... - Jean II */
		u16 rid;

		*active = 1;

		rid = (priv->port_type == 3) ? HERMES_RID_CNF_OWN_SSID :
			HERMES_RID_CNF_DESIRED_SSID;
		
		err = hermes_read_ltv(hw, USER_BAP, rid, sizeof(essidbuf),
				      NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	} else {
		*active = 0;

		err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CURRENT_SSID,
				      sizeof(essidbuf), NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	}

	len = le16_to_cpu(essidbuf.len);

	memset(buf, 0, sizeof(buf));
	memcpy(buf, p, len);
	buf[len] = '\0';

 fail_unlock:
	dldwd_unlock(priv);

	TRACE_EXIT(priv->ndev.name);

	return err;       
}

static long dldwd_hw_get_freq(dldwd_priv_t *priv)
{
	
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 channel;
	long freq = 0;

	dldwd_lock(priv);
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CURRENT_CHANNEL, &channel);
	if (err)
		goto out;

	if ( (channel < 1) || (channel > NUM_CHANNELS) ) {
		struct net_device *dev = &priv->ndev;

		printk(KERN_WARNING "%s: Channel out of range (%d)!\n", dev->name, channel);
		err = -EBUSY;
		goto out;

	}
	freq = channel_frequency[channel-1] * 100000;

 out:
	dldwd_unlock(priv);

	if (err > 0)
		err = -EBUSY;
	return err ? err : freq;
}

static int dldwd_hw_get_bitratelist(dldwd_priv_t *priv, int *numrates,
				    s32 *rates, int max)
{
	hermes_t *hw = &priv->hw;
	hermes_id_t list;
	unsigned char *p = (unsigned char *)&list.val;
	int err = 0;
	int num;
	int i;

	dldwd_lock(priv);
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_DATARATES, sizeof(list),
			      NULL, &list);
	dldwd_unlock(priv);

	if (err)
		return err;
	
	num = le16_to_cpu(list.len);
	*numrates = num;
	num = min(num, max);

	for (i = 0; i < num; i++) {
		rates[i] = (p[i] & 0x7f) * 500000; /* convert to bps */
	}

	return 0;
}

#ifndef ORINOCO_DEBUG
static inline void show_rx_frame(struct dldwd_frame_hdr *frame) {}
#else
static void show_rx_frame(struct dldwd_frame_hdr *frame)
{
	printk(KERN_DEBUG "RX descriptor:\n");
	printk(KERN_DEBUG "  status      = 0x%04x\n", frame->desc.status);
	printk(KERN_DEBUG "  res1        = 0x%04x\n", frame->desc.res1);
	printk(KERN_DEBUG "  res2        = 0x%04x\n", frame->desc.res2);
	printk(KERN_DEBUG "  q_info      = 0x%04x\n", frame->desc.q_info);
	printk(KERN_DEBUG "  res3        = 0x%04x\n", frame->desc.res3);
	printk(KERN_DEBUG "  res4        = 0x%04x\n", frame->desc.res4);
	printk(KERN_DEBUG "  tx_ctl      = 0x%04x\n", frame->desc.tx_ctl);

	printk(KERN_DEBUG "IEEE 802.11 header:\n");
	printk(KERN_DEBUG "  frame_ctl   = 0x%04x\n",
	       frame->p80211.frame_ctl);
	printk(KERN_DEBUG "  duration_id = 0x%04x\n",
	       frame->p80211.duration_id);
	printk(KERN_DEBUG "  addr1       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr1[0], frame->p80211.addr1[1],
	       frame->p80211.addr1[2], frame->p80211.addr1[3],
	       frame->p80211.addr1[4], frame->p80211.addr1[5]);
	printk(KERN_DEBUG "  addr2       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr2[0], frame->p80211.addr2[1],
	       frame->p80211.addr2[2], frame->p80211.addr2[3],
	       frame->p80211.addr2[4], frame->p80211.addr2[5]);
	printk(KERN_DEBUG "  addr3       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr3[0], frame->p80211.addr3[1],
	       frame->p80211.addr3[2], frame->p80211.addr3[3],
	       frame->p80211.addr3[4], frame->p80211.addr3[5]);
	printk(KERN_DEBUG "  seq_ctl     = 0x%04x\n",
	       frame->p80211.seq_ctl);
	printk(KERN_DEBUG "  addr4       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p80211.addr4[0], frame->p80211.addr4[1],
	       frame->p80211.addr4[2], frame->p80211.addr4[3],
	       frame->p80211.addr4[4], frame->p80211.addr4[5]);
	printk(KERN_DEBUG "  data_len    = 0x%04x\n",
	       frame->p80211.data_len);

	printk(KERN_DEBUG "IEEE 802.3 header:\n");
	printk(KERN_DEBUG "  dest        = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p8023.h_dest[0], frame->p8023.h_dest[1],
	       frame->p8023.h_dest[2], frame->p8023.h_dest[3],
	       frame->p8023.h_dest[4], frame->p8023.h_dest[5]);
	printk(KERN_DEBUG "  src         = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       frame->p8023.h_source[0], frame->p8023.h_source[1],
	       frame->p8023.h_source[2], frame->p8023.h_source[3],
	       frame->p8023.h_source[4], frame->p8023.h_source[5]);
	printk(KERN_DEBUG "  len         = 0x%04x\n", frame->p8023.h_proto);

	printk(KERN_DEBUG "IEEE 802.2 LLC/SNAP header:\n");
	printk(KERN_DEBUG "  DSAP        = 0x%02x\n", frame->p8022.dsap);
	printk(KERN_DEBUG "  SSAP        = 0x%02x\n", frame->p8022.ssap);
	printk(KERN_DEBUG "  ctrl        = 0x%02x\n", frame->p8022.ctrl);
	printk(KERN_DEBUG "  OUI         = %02x:%02x:%02x\n",
	       frame->p8022.oui[0], frame->p8022.oui[1], frame->p8022.oui[2]);
	printk(KERN_DEBUG "  ethertype  = 0x%04x\n", frame->ethertype);
}
#endif

/*
 * Interrupt handler
 */
void dldwd_interrupt(int irq, void * dev_id, struct pt_regs *regs)
{
	dldwd_priv_t *priv = (dldwd_priv_t *) dev_id;
	hermes_t *hw = &priv->hw;
	struct net_device *dev = &priv->ndev;
	int count = IRQ_LOOP_MAX;
	u16 evstat, events;
	static int old_time = 0, timecount = 0; /* Eugh, revolting hack for now */

	if (test_and_set_bit(DLDWD_STATE_INIRQ, &priv->state))
		BUG();

	if (! dldwd_irqs_allowed(priv)) {
		clear_bit(DLDWD_STATE_INIRQ, &priv->state);
		return;
	}

	DEBUG(3, "%s: dldwd_interrupt()\n", priv->ndev.name);

	while (1) {
		if (jiffies != old_time)
			timecount = 0;
		if ( (++timecount > 50) || (! count--) ) {
			printk(KERN_CRIT "%s: IRQ handler is looping too \
much! Shutting down.\n",
			       dev->name);
			/* Perform an emergency shutdown */
			clear_bit(DLDWD_STATE_DOIRQ, &priv->state);
			hermes_set_irqmask(hw, 0);
			break;
		}

		evstat = hermes_read_regn(hw, EVSTAT);
		DEBUG(3, "__dldwd_interrupt(): count=%d EVSTAT=0x%04x inten=0x%04x\n",
		      count, evstat, hw->inten);

		events = evstat & hw->inten;

		if (! events) {
			if (netif_queue_stopped(dev)) {
				/* There seems to be a firmware bug which
				   sometimes causes the card to give an
				   interrupt with no event set, when there
				   sould be a Tx completed event. */
				DEBUG(3, "%s: Interrupt with no event (ALLOCFID=0x%04x)\n",
				      dev->name, (int)hermes_read_regn(hw, ALLOCFID));
				events = HERMES_EV_TX | HERMES_EV_ALLOC;
			} else /* Nothing's happening, we're done */
				break;
		}

		/* Check the card hasn't been removed */
		if (! hermes_present(hw)) {
			DEBUG(0, "dldwd_interrupt(): card removed\n");
			break;
		}

		if (events & HERMES_EV_TICK)
			__dldwd_ev_tick(priv, hw);
		if (events & HERMES_EV_WTERR)
			__dldwd_ev_wterr(priv, hw);
		if (events & HERMES_EV_INFDROP)
			__dldwd_ev_infdrop(priv, hw);
		if (events & HERMES_EV_INFO)
			__dldwd_ev_info(priv, hw);
		if (events & HERMES_EV_RX)
			__dldwd_ev_rx(priv, hw);
		if (events & HERMES_EV_TXEXC)
			__dldwd_ev_txexc(priv, hw);
		if (events & HERMES_EV_TX)
			__dldwd_ev_tx(priv, hw);
		if (events & HERMES_EV_ALLOC)
			__dldwd_ev_alloc(priv, hw);
		
		hermes_write_regn(hw, EVACK, events);
	}

	clear_bit(DLDWD_STATE_INIRQ, &priv->state);
}

static void __dldwd_ev_tick(dldwd_priv_t *priv, hermes_t *hw)
{
	printk(KERN_DEBUG "%s: TICK\n", priv->ndev.name);
}

static void __dldwd_ev_wterr(dldwd_priv_t *priv, hermes_t *hw)
{
	/* This seems to happen a fair bit under load, but ignoring it
	   seems to work fine...*/
	DEBUG(1, "%s: MAC controller error (WTERR). Ignoring.\n",
	      priv->ndev.name);
}

static void __dldwd_ev_infdrop(dldwd_priv_t *priv, hermes_t *hw)
{
	printk(KERN_WARNING "%s: Information frame lost.\n", priv->ndev.name);
}

static void __dldwd_ev_info(dldwd_priv_t *priv, hermes_t *hw)
{
	DEBUG(3, "%s: Information frame received.\n", priv->ndev.name);
	/* We don't actually do anything about it - we assume the MAC
	   controller can deal with it */
}

static void __dldwd_ev_rx(dldwd_priv_t *priv, hermes_t *hw)
{
	struct net_device *dev = &priv->ndev;
	struct net_device_stats *stats = &priv->stats;
	struct iw_statistics *wstats = &priv->wstats;
	struct sk_buff *skb = NULL;
	int l = RX_EIO_RETRY;
	u16 rxfid, status;
	int length, data_len, data_off;
	char *p;
	struct dldwd_frame_hdr hdr;
	struct ethhdr *eh;
	int err;

	rxfid = hermes_read_regn(hw, RXFID);
	DEBUG(3, "__dldwd_ev_rx(): RXFID=0x%04x\n", rxfid);

	/* We read in the entire frame header here. This isn't really
	   necessary, since we ignore most of it, but it's
	   conceptually simpler. We can tune this later if
	   necessary. */
	do {
		err = hermes_bap_pread(hw, IRQ_BAP, &hdr, sizeof(hdr),
				       rxfid, 0);
	} while ( (err == -EIO) && (--l) );
	if (err) {
		if (err == -EIO)
			DEBUG(1, "%s: EIO reading frame header.\n", dev->name);
		else
			printk(KERN_ERR "%s: error %d reading frame header. "
			       "Frame dropped.\n", dev->name, err);
		stats->rx_errors++;
		goto drop;
	}
	DEBUG(2, "%s: BAP read suceeded: l=%d\n", dev->name, l);

	status = le16_to_cpu(hdr.desc.status);
	
	if (status & HERMES_RXSTAT_ERR) {
		if ((status & HERMES_RXSTAT_ERR) == HERMES_RXSTAT_BADCRC) {
			stats->rx_crc_errors++;
			DEBUG(1, "%s: Bad CRC on Rx. Frame dropped.\n", dev->name);
			show_rx_frame(&hdr);
		} else if ((status & HERMES_RXSTAT_ERR)
			   == HERMES_RXSTAT_UNDECRYPTABLE) {
			wstats->discard.code++;
			printk(KERN_WARNING "%s: Undecryptable frame on Rx. Frame dropped.\n",
			       dev->name);
		} else {
			wstats->discard.misc++;
			printk("%s: Unknown Rx error (0x%x). Frame dropped.\n",
			       dev->name, status & HERMES_RXSTAT_ERR);
		}
		stats->rx_errors++;
		goto drop;
	}

	length = le16_to_cpu(hdr.p80211.data_len);
	/* Yes, you heard right, that's le16. 802.2 and 802.3 are
	   big-endian, but 802.11 is little-endian believe it or
	   not. */
	/* Correct. 802.3 is big-endian byte order and little endian bit
	 * order, whereas 802.11 is little endian for both byte and bit
	 * order. That's specified in the 802.11 spec. - Jean II */
	
	/* Sanity check */
	if (length > MAX_FRAME_SIZE) {
		printk(KERN_WARNING "%s: Oversized frame received (%d bytes)\n",
		       dev->name, length);
		stats->rx_length_errors++;
		stats->rx_errors++;
		goto drop;
	}

	/* We need space for the packet data itself, plus an ethernet
	   header, plus 2 bytes so we can align the IP header on a
	   32bit boundary, plus 1 byte so we can read in odd length
	   packets from the card, which has an IO granularity of 16
	   bits */  
	skb = dev_alloc_skb(length+ETH_HLEN+2+1);
	if (!skb) {
		printk(KERN_WARNING "%s: Can't allocate skb for Rx\n",
		       dev->name);
		stats->rx_dropped++;
		goto drop;
	}

	skb_reserve(skb, 2); /* This way the IP header is aligned */

	/* Handle decapsulation
	 * In most cases, the firmware tell us about SNAP frames.
	 * For some reason, the SNAP frames sent by LinkSys APs
	 * are not properly recognised by most firmwares.
	 * So, check ourselves (note : only 3 bytes out of 6).
	 */
	if(((status & HERMES_RXSTAT_MSGTYPE) == HERMES_RXSTAT_1042) ||
	   ((status & HERMES_RXSTAT_MSGTYPE) == HERMES_RXSTAT_TUNNEL) ||
	   (!memcmp(&hdr.p8022, &encaps_hdr, 3))) {
		/* These indicate a SNAP within 802.2 LLC within
		   802.11 frame which we'll need to de-encapsulate to
		   the original EthernetII frame. */

		/* Remove SNAP header, reconstruct EthernetII frame */
		data_len = length - ENCAPS_OVERHEAD;
		data_off = sizeof(hdr);

		eh = (struct ethhdr *)skb_put(skb, ETH_HLEN);

		memcpy(eh, &hdr.p8023, sizeof(hdr.p8023));
		eh->h_proto = hdr.ethertype;
	} else {
		/* All other cases indicate a genuine 802.3 frame.
		 * No decapsulation needed */

		/* Otherwise, we just throw the whole thing in,
		 * and hope the protocol layer can deal with it
		 * as 802.3 */
		data_len = length;
		data_off = P8023_OFFSET;
	}

	p = skb_put(skb, data_len);
	do {
		err = hermes_bap_pread(hw, IRQ_BAP, p, RUP_EVEN(data_len),
				       rxfid, data_off);
	} while ( (err == -EIO) && (--l) );
	if (err) {
		if (err == -EIO)
			DEBUG(1, "%s: EIO reading frame header.\n", dev->name);
		else
			printk(KERN_ERR "%s: error %d reading frame header. "
			       "Frame dropped.\n", dev->name, err);
		stats->rx_errors++;
		goto drop;
	}
	DEBUG(2, "%s: BAP read suceeded: l=%d\n", dev->name, l);

	dev->last_rx = jiffies;
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;
	
	/* Process the wireless stats if needed */
	dldwd_stat_gather(dev, skb, &hdr);

	/* Pass the packet to the networking stack */
	netif_rx(skb);
	stats->rx_packets++;
	stats->rx_bytes += length;

	return;

 drop:	
	if (skb)
		dev_kfree_skb_irq(skb);
	return;
}

static void __dldwd_ev_txexc(dldwd_priv_t *priv, hermes_t *hw)
{
	struct net_device *dev = &priv->ndev;
	struct net_device_stats *stats = &priv->stats;

	printk(KERN_WARNING "%s: Tx error!\n", dev->name);

	netif_wake_queue(dev);
	stats->tx_errors++;
}

static void __dldwd_ev_tx(dldwd_priv_t *priv, hermes_t *hw)
{
	struct net_device *dev = &priv->ndev;
	struct net_device_stats *stats = &priv->stats;

	DEBUG(3, "%s: Transmit completed\n", dev->name);

	stats->tx_packets++;
	netif_wake_queue(dev);
}

static void __dldwd_ev_alloc(dldwd_priv_t *priv, hermes_t *hw)
{
	u16 allocfid;

	allocfid = hermes_read_regn(hw, ALLOCFID);
	DEBUG(3, "%s: Allocation complete FID=0x%04x\n", priv->ndev.name, allocfid);

	/* For some reason we don't seem to get transmit completed events properly */
	if (allocfid == priv->txfid)
		__dldwd_ev_tx(priv, hw);

/* 	hermes_write_regn(hw, ALLOCFID, 0); */
}

static void determine_firmware(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err;
	struct sta_id {
		u16 id, vendor, major, minor;
	} __attribute__ ((packed)) sta_id;
	u32 firmver;

	/* Get the firmware version */
	err = HERMES_READ_RECORD(hw, USER_BAP,
				 HERMES_RID_STAIDENTITY, &sta_id);
	if (err) {
		printk(KERN_WARNING "%s: Error %d reading firmware info. Wildly guessing capabilities...\n",
		       dev->name, err);
		memset(&sta_id, 0, sizeof(sta_id));
	}
	le16_to_cpus(&sta_id.id);
	le16_to_cpus(&sta_id.vendor);
	le16_to_cpus(&sta_id.major);
	le16_to_cpus(&sta_id.minor);

	firmver = ((u32)sta_id.major << 16) | sta_id.minor;

	printk(KERN_DEBUG "%s: Station identity %04x:%04x:%04x:%04x\n",
	       dev->name, sta_id.id, sta_id.vendor,
	       sta_id.major, sta_id.minor);

	/* Determine capabilities from the firmware version */

	if (sta_id.vendor == 1) {
		/* Lucent Wavelan IEEE, Lucent Orinoco, Cabletron RoamAbout,
		   ELSA, Melco, HP, IBM, Dell 1150, Compaq 110/210 */
		printk(KERN_DEBUG "%s: Looks like a Lucent/Agere firmware "
		       "version %d.%02d\n", dev->name,
		       sta_id.major, sta_id.minor);

		priv->firmware_type = FIRMWARE_TYPE_LUCENT;
		priv->tx_rate_ctrl = 0x3;	/* 11 Mb/s auto */
		priv->need_card_reset = 0;
		priv->broken_reset = 0;
		priv->broken_allocate = 0;
		priv->has_port3 = 1;		/* Still works in 7.28 */
		priv->has_ibss = (firmver >= 0x60006);
		priv->has_ibss_any = (firmver >= 0x60010);
		priv->has_wep = (firmver >= 0x40020);
		priv->has_big_wep = 1; /* FIXME: this is wrong - how do we tell
					  Gold cards from the others? */
		priv->has_mwo = (firmver >= 0x60000);
		priv->has_pm = (firmver >= 0x40020); /* Don't work in 7.52 ? */
		priv->has_preamble = 0;
		priv->ibss_port = 1;
		/* Tested with Lucent firmware :
		 *	1.16 ; 4.08 ; 4.52 ; 6.04 ; 6.16 ; 7.28 => Jean II
		 * Tested CableTron firmware : 4.32 => Anton */
	} else if ((sta_id.vendor == 2) &&
		   ((firmver == 0x10001) || (firmver == 0x20001))) {
		/* Symbol , 3Com AirConnect, Intel, Ericsson WLAN */
		/* Intel MAC : 00:02:B3:* */
		/* 3Com MAC : 00:50:DA:* */
		char tmp[SYMBOL_MAX_VER_LEN+1];

		memset(tmp, 0, sizeof(tmp));
		/* Get the Symbol firmware version */
		err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_SYMBOL_SECONDARY_VER,
				      SYMBOL_MAX_VER_LEN, NULL, &tmp);
		if (err) {
			printk(KERN_WARNING
			       "%s: Error %d reading Symbol firmware info. Wildly guessing capabilities...\n",
			       dev->name, err);
			firmver = 0;
			tmp[0] = '\0';
		} else {
			/* The firmware revision is a string, the format is
			 * something like : "V2.20-01".
			 * Quick and dirty parsing... - Jean II
			 */
			firmver = ((tmp[1] - '0') << 16) | ((tmp[3] - '0') << 12)
				| ((tmp[4] - '0') << 8) | ((tmp[6] - '0') << 4)
				| (tmp[7] - '0');

			tmp[SYMBOL_MAX_VER_LEN] = '\0';
		}

		printk(KERN_DEBUG "%s: Looks like a Symbol firmware "
		       "version [%s] (parsing to %X)\n", dev->name,
		       tmp, firmver);

		priv->firmware_type = FIRMWARE_TYPE_SYMBOL;
		priv->tx_rate_ctrl = 0xF;	/* 11 Mb/s auto */
		priv->need_card_reset = 1;
		priv->broken_reset = 0;
		priv->broken_allocate = 1;
		priv->has_port3 = 1;
		priv->has_ibss = (firmver >= 0x20000);
		priv->has_wep = (firmver >= 0x15012);
		priv->has_big_wep = (firmver >= 0x20000);
		priv->has_mwo = 0;
		priv->has_pm = (firmver >= 0x20000) && (firmver < 0x22000);
		priv->has_preamble = (firmver >= 0x20000);
		priv->ibss_port = 4;
		/* Tested with Intel firmware : 0x20015 => Jean II */
		/* Tested with 3Com firmware : 0x15012 & 0x22001 => Jean II */
	} else {
		/* D-Link, Linksys, Adtron, ZoomAir, and many others...
		 * Samsung, Compaq 100/200 and Proxim are slightly
		 * different and less well tested */
		/* D-Link MAC : 00:40:05:* */
		/* Addtron MAC : 00:90:D1:* */
		printk(KERN_DEBUG "%s: Looks like an Intersil firmware "
		       "version %d.%02d\n", dev->name,
		       sta_id.major, sta_id.minor);

		priv->firmware_type = FIRMWARE_TYPE_INTERSIL;
		priv->tx_rate_ctrl = 0xF;	/* 11 Mb/s auto */
		priv->need_card_reset = 0;
		priv->broken_reset = 0;
		priv->broken_allocate = 0;
		priv->has_port3 = 1;
		priv->has_ibss = (firmver >= 0x00007); /* FIXME */
		priv->has_wep = (firmver >= 0x00008);
		priv->has_big_wep = priv->has_wep;
		priv->has_mwo = 0;
		priv->has_pm = (firmver >= 0x00007);
		priv->has_preamble = 0;

		if (firmver >= 0x00008)
			priv->ibss_port = 0;
		else {
			printk(KERN_NOTICE "%s: Intersil firmware earlier "
			       "than v0.08 - several features not supported.",
			       dev->name);
			priv->ibss_port = 1;
		}
	}
}

/*
 * struct net_device methods
 */

int
dldwd_init(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	hermes_id_t nickbuf;
	u16 reclen;
	int len;

	TRACE_ENTER("dldwd");

	dldwd_lock(priv);

	/* Do standard firmware reset */
	err = hermes_reset(hw);
	if (err != 0) {
		printk(KERN_ERR "%s: failed to reset hardware (err = %d)\n",
		       dev->name, err);
		goto out;
	}

	determine_firmware(dev);

	if (priv->has_port3)
		printk(KERN_DEBUG "%s: Ad-hoc demo mode supported.\n", dev->name);
	if (priv->has_ibss)
		printk(KERN_DEBUG "%s: IEEE standard IBSS ad-hoc mode supported.\n",
		       dev->name);
	if (priv->has_wep) {
		printk(KERN_DEBUG "%s: WEP supported, ", dev->name);
		if (priv->has_big_wep)
			printk("\"128\"-bit key.\n");
		else
			printk("40-bit key.\n");
	}

	/* Get the MAC address */
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CNF_MACADDR,
			      ETH_ALEN, NULL, dev->dev_addr);
	if (err) {
		printk(KERN_WARNING "%s: failed to read MAC address!\n",
		       dev->name);
		goto out;
	}

	printk(KERN_DEBUG "%s: MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
	       dev->name, dev->dev_addr[0], dev->dev_addr[1],
	       dev->dev_addr[2], dev->dev_addr[3], dev->dev_addr[4],
	       dev->dev_addr[5]);

	/* Get the station name */
	err = hermes_read_ltv(hw, USER_BAP, HERMES_RID_CNF_NICKNAME,
			      sizeof(nickbuf), &reclen, &nickbuf);
	if (err) {
		printk(KERN_ERR "%s: failed to read station name!n",
		       dev->name);
		goto out;
	}
	if (nickbuf.len)
		len = min_t(u16, IW_ESSID_MAX_SIZE, le16_to_cpu(nickbuf.len));
	else
		len = min(IW_ESSID_MAX_SIZE, 2 * reclen);
	memcpy(priv->nick, &nickbuf.val, len);
	priv->nick[len] = '\0';

	printk(KERN_DEBUG "%s: Station name \"%s\"\n", dev->name, priv->nick);

	/* Get allowed channels */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CHANNEL_LIST, &priv->channel_mask);
	if (err) {
		printk(KERN_ERR "%s: failed to read channel list!\n",
		       dev->name);
		goto out;
	}

	/* Get initial AP density */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_SYSTEM_SCALE, &priv->ap_density);
	if (err) {
		printk(KERN_ERR "%s: failed to read AP density!\n", dev->name);
		goto out;
	}

	/* Get initial RTS threshold */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_RTS_THRESH, &priv->rts_thresh);
	if (err) {
		printk(KERN_ERR "%s: failed to read RTS threshold!\n", dev->name);
		goto out;
	}

	/* Get initial fragmentation settings */
	if (priv->has_mwo)
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_MWO_ROBUST,
					  &priv->mwo_robust);
	else
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_FRAG_THRESH,
					  &priv->frag_thresh);
	if (err) {
		printk(KERN_ERR "%s: failed to read fragmentation settings!\n", dev->name);
		goto out;
	}

	/* Power management setup */
	if (priv->has_pm) {
		priv->pm_on = 0;
		priv->pm_mcast = 1;
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_PERIOD,
					  &priv->pm_period);
		if (err) {
			printk(KERN_ERR "%s: failed to read power management period!\n",
			       dev->name);
			goto out;
		}
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_HOLDOVER,
					  &priv->pm_timeout);
		if (err) {
			printk(KERN_ERR "%s: failed to read power management timeout!\n",
			       dev->name);
			goto out;
		}
	}

	/* Preamble setup */
	if (priv->has_preamble) {
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_SYMBOL_PREAMBLE, &priv->preamble);
		if (err)
			goto out;
	}
		
	/* Set up the default configuration */
	priv->iw_mode = IW_MODE_INFRA;
	/* By default use IEEE/IBSS ad-hoc mode if we have it */
	priv->prefer_port3 = priv->has_port3 && (! priv->has_ibss);
	set_port_type(priv);

	priv->promiscuous = 0;
	priv->allmulti = 0;
	priv->wep_on = 0;
	priv->tx_key = 0;

	printk(KERN_DEBUG "%s: ready\n", dev->name);

 out:
	dldwd_unlock(priv);

	TRACE_EXIT("dldwd");

	return err;
}

struct net_device_stats *
dldwd_get_stats(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	
	return &priv->stats;
}

struct iw_statistics *
dldwd_get_wireless_stats(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	hermes_t *hw = &priv->hw;
	struct iw_statistics *wstats = &priv->wstats;
	int err = 0;

	if (!priv->hw_ready)
		return NULL;

	dldwd_lock(priv);

	if (priv->iw_mode == IW_MODE_ADHOC) {
		memset(&wstats->qual, 0, sizeof(wstats->qual));
#ifdef WIRELESS_SPY
		/* If a spy address is defined, we report stats of the
		 * first spy address - Jean II */
		if (priv->spy_number > 0) {
			wstats->qual.qual = priv->spy_stat[0].qual;
			wstats->qual.level = priv->spy_stat[0].level;
			wstats->qual.noise = priv->spy_stat[0].noise;
			wstats->qual.updated = priv->spy_stat[0].updated;
		}
#endif /* WIRELESS_SPY */
	} else {
		dldwd_commsqual_t cq;

		err = HERMES_READ_RECORD(hw, USER_BAP,
					 HERMES_RID_COMMSQUALITY, &cq);
		
		DEBUG(3, "%s: Global stats = %X-%X-%X\n", dev->name,
		      cq.qual, cq.signal, cq.noise);

		wstats->qual.qual = (int)le16_to_cpu(cq.qual);
		wstats->qual.level = (int)le16_to_cpu(cq.signal) - 0x95;
		wstats->qual.noise = (int)le16_to_cpu(cq.noise) - 0x95;
		wstats->qual.updated = 7;
	}

	dldwd_unlock(priv);

	if (err)
		return NULL;
		
	return wstats;
}

#ifdef WIRELESS_SPY
static inline void dldwd_spy_gather(struct net_device *dev, u_char *mac,
				    int level, int noise)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	int i;

	/* Gather wireless spy statistics: for each packet, compare the
	 * source address with out list, and if match, get the stats... */
	for (i = 0; i < priv->spy_number; i++)
		if (!memcmp(mac, priv->spy_address[i], ETH_ALEN)) {
			priv->spy_stat[i].level = level - 0x95;
			priv->spy_stat[i].noise = noise - 0x95;
			priv->spy_stat[i].qual = level - noise;
			priv->spy_stat[i].updated = 7;
		}
}
#endif /* WIRELESS_SPY */

void
dldwd_stat_gather( struct net_device *dev,
		   struct sk_buff *skb,
		   struct dldwd_frame_hdr *hdr)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;

	/* Using spy support with lots of Rx packets, like in an
	 * infrastructure (AP), will really slow down everything, because
	 * the MAC address must be compared to each entry of the spy list.
	 * If the user really asks for it (set some address in the
	 * spy list), we do it, but he will pay the price.
	 * Note that to get here, you need both WIRELESS_SPY
	 * compiled in AND some addresses in the list !!!
	 */
#ifdef WIRELESS_SPY
	/* Note : gcc will optimise the whole section away if
	 * WIRELESS_SPY is not defined... - Jean II */
	if (priv->spy_number > 0) {
		u8 *stats = (u8 *) &(hdr->desc.q_info);
		/* This code may look strange. Everywhere we are using 16 bit
		 * ints except here. I've verified that these are are the
		 * correct values. Please check on PPC - Jean II */

		dldwd_spy_gather(dev, skb->mac.raw + ETH_ALEN, (int)stats[1], (int)stats[0]);
	}
#endif /* WIRELESS_SPY */
}

int
dldwd_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	struct net_device_stats *stats = &priv->stats;
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 txfid = priv->txfid;
	char *p;
	struct ethhdr *eh;
	int len, data_len, data_off;
	struct dldwd_frame_hdr hdr;
	hermes_response_t resp;

	if (! netif_running(dev)) {
		printk(KERN_ERR "%s: Tx on stopped device!\n",
		       dev->name);
		return 1;

	}
	
	if (netif_queue_stopped(dev)) {
		printk(KERN_ERR "%s: Tx while transmitter busy!\n", 
		       dev->name);
		return 1;
	}
	
	dldwd_lock(priv);

	/* Length of the packet body */
	len = max_t(int,skb->len - ETH_HLEN, ETH_ZLEN);

	eh = (struct ethhdr *)skb->data;

	/* Build the IEEE 802.11 header */
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.p80211.addr1, eh->h_dest, ETH_ALEN);
	memcpy(hdr.p80211.addr2, eh->h_source, ETH_ALEN);
	hdr.p80211.frame_ctl = DLDWD_FTYPE_DATA;

	/* Encapsulate Ethernet-II frames */
	if (ntohs(eh->h_proto) > 1500) { /* Ethernet-II frame */
		data_len = len;
		data_off = sizeof(hdr);
		p = skb->data + ETH_HLEN;

		/* 802.11 header */
		hdr.p80211.data_len = cpu_to_le16(data_len + ENCAPS_OVERHEAD);

		/* 802.3 header */
		memcpy(hdr.p8023.h_dest, eh->h_dest, ETH_ALEN);
		memcpy(hdr.p8023.h_source, eh->h_source, ETH_ALEN);
		hdr.p8023.h_proto = htons(data_len + ENCAPS_OVERHEAD);
		
		/* 802.2 header */
		if (! use_old_encaps) 
			memcpy(&hdr.p8022, &encaps_hdr,
			       sizeof(encaps_hdr));
		else
			memcpy(&hdr.p8022, &encaps_hdr,
			       sizeof(old_encaps_hdr));
			
		hdr.ethertype = eh->h_proto;
		err  = hermes_bap_pwrite(hw, USER_BAP, &hdr, sizeof(hdr),
					 txfid, 0);
		if (err) {
			if (err == -EIO)
				/* We get these errors reported by the
				   firmware every so often apparently at
				   random.  Let the upper layers
				   handle the retry */
				DEBUG(1, "%s: DEBUG: EIO writing packet header to BAP\n", dev->name);
			else
				printk(KERN_ERR "%s: Error %d writing packet header to BAP\n",
				       dev->name, err);
			stats->tx_errors++;
			goto fail;
		}
	} else { /* IEEE 802.3 frame */
		data_len = len + ETH_HLEN;
		data_off = P8023_OFFSET;
		p = skb->data;
		
		/* 802.11 header */
		hdr.p80211.data_len = cpu_to_le16(len);
		err = hermes_bap_pwrite(hw, USER_BAP, &hdr, P8023_OFFSET,
					txfid, 0);
		if (err) {
			printk(KERN_ERR
			       "%s: Error %d writing packet header to BAP\n",
			       dev->name, err);
			stats->tx_errors++;
			goto fail;
		}
	}

	/* Round up for odd length packets */
	err = hermes_bap_pwrite(hw, USER_BAP, p, RUP_EVEN(data_len), txfid, data_off);
	if (err) {
		if (err == -EIO)
			DEBUG(1, "%s: DEBUG: EIO writing packet header to BAP\n", dev->name);
		else
			printk(KERN_ERR "%s: Error %d writing packet header to BAP",
			       dev->name, err);
		stats->tx_errors++;
		goto fail;
	}

	/* Finally, we actually initiate the send */
	err = hermes_docmd_wait(hw, HERMES_CMD_TX | HERMES_CMD_RECL, txfid, &resp);
	if (err) {
		printk(KERN_ERR "%s: Error %d transmitting packet\n", dev->name, err);
		stats->tx_errors++;
		goto fail;
	}

	dev->trans_start = jiffies;
	stats->tx_bytes += data_off + data_len;

	netif_stop_queue(dev);

	dldwd_unlock(priv);

	dev_kfree_skb(skb);

	return 0;
 fail:

	dldwd_unlock(priv);
	return err;
}

void
dldwd_tx_timeout(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	struct net_device_stats *stats = &priv->stats;
	int err = 0;

	printk(KERN_WARNING "%s: Tx timeout! Resetting card.\n", dev->name);

	stats->tx_errors++;

	err = dldwd_reset(priv);
	if (err)
		printk(KERN_ERR "%s: Error %d resetting card on Tx timeout!\n",
		       dev->name, err);
	else {
		dev->trans_start = jiffies;
		netif_wake_queue(dev);
	}
}

static int dldwd_ioctl_getiwrange(struct net_device *dev, struct iw_point *rrq)
{
	dldwd_priv_t *priv = dev->priv;
	int err = 0;
	int mode;
	struct iw_range range;
	int numrates;
	int i, k;

	TRACE_ENTER(dev->name);

	err = verify_area(VERIFY_WRITE, rrq->pointer, sizeof(range));
	if (err)
		return err;

	rrq->length = sizeof(range);

	dldwd_lock(priv);
	mode = priv->iw_mode;
	dldwd_unlock(priv);

	memset(&range, 0, sizeof(range));

	/* Much of this shamelessly taken from wvlan_cs.c. No idea
	 * what it all means -dgibson */
#if WIRELESS_EXT > 10
	range.we_version_compiled = WIRELESS_EXT;
	range.we_version_source = 11;
#endif /* WIRELESS_EXT > 10 */

	range.min_nwid = range.max_nwid = 0; /* We don't use nwids */

	/* Set available channels/frequencies */
	range.num_channels = NUM_CHANNELS;
	k = 0;
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (priv->channel_mask & (1 << i)) {
			range.freq[k].i = i + 1;
			range.freq[k].m = channel_frequency[i] * 100000;
			range.freq[k].e = 1;
			k++;
		}
		
		if (k >= IW_MAX_FREQUENCIES)
			break;
	}
	range.num_frequency = k;

	range.sensitivity = 3;

	if ((mode == IW_MODE_ADHOC) && (priv->spy_number == 0)){
		/* Quality stats meaningless in ad-hoc mode */
		range.max_qual.qual = 0;
		range.max_qual.level = 0;
		range.max_qual.noise = 0;
	} else {
		range.max_qual.qual = 0x8b - 0x2f;
		range.max_qual.level = 0x2f - 0x95 - 1;
		range.max_qual.noise = 0x2f - 0x95 - 1;
	}

	err = dldwd_hw_get_bitratelist(priv, &numrates,
				       range.bitrate, IW_MAX_BITRATES);
	if (err)
		return err;
	range.num_bitrates = numrates;
	
	/* Set an indication of the max TCP throughput in bit/s that we can
	 * expect using this interface. May be use for QoS stuff...
	 * Jean II */
	if(numrates > 2)
		range.throughput = 5 * 1000 * 1000;	/* ~5 Mb/s */
	else
		range.throughput = 1.5 * 1000 * 1000;	/* ~1.5 Mb/s */

	range.min_rts = 0;
	range.max_rts = 2347;
	range.min_frag = 256;
	range.max_frag = 2346;

	dldwd_lock(priv);
	if (priv->has_wep) {
		range.max_encoding_tokens = MAX_KEYS;

		range.encoding_size[0] = SMALL_KEY_SIZE;
		range.num_encoding_sizes = 1;

		if (priv->has_big_wep) {
			range.encoding_size[1] = LARGE_KEY_SIZE;
			range.num_encoding_sizes = 2;
		}
	} else {
		range.num_encoding_sizes = 0;
		range.max_encoding_tokens = 0;
	}
	dldwd_unlock(priv);
		
	range.min_pmp = 0;
	range.max_pmp = 65535000;
	range.min_pmt = 0;
	range.max_pmt = 65535 * 1000;	/* ??? */
	range.pmp_flags = IW_POWER_PERIOD;
	range.pmt_flags = IW_POWER_TIMEOUT;
	range.pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_UNICAST_R;

	range.num_txpower = 1;
	range.txpower[0] = 15; /* 15dBm */
	range.txpower_capa = IW_TXPOW_DBM;

#if WIRELESS_EXT > 10
	range.retry_capa = IW_RETRY_LIMIT | IW_RETRY_LIFETIME;
	range.retry_flags = IW_RETRY_LIMIT;
	range.r_time_flags = IW_RETRY_LIFETIME;
	range.min_retry = 0;
	range.max_retry = 65535;	/* ??? */
	range.min_r_time = 0;
	range.max_r_time = 65535 * 1000;	/* ??? */
#endif /* WIRELESS_EXT > 10 */

	if (copy_to_user(rrq->pointer, &range, sizeof(range)))
		return -EFAULT;

	TRACE_EXIT(dev->name);

	return 0;
}

static int dldwd_ioctl_setiwencode(struct net_device *dev, struct iw_point *erq)
{
	dldwd_priv_t *priv = dev->priv;
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	int setindex = priv->tx_key;
	int enable = priv->wep_on;
	int restricted = priv->wep_restrict;
	u16 xlen = 0;
	int err = 0;
	char keybuf[MAX_KEY_SIZE];
	
	if (erq->pointer) {
		/* We actually have a key to set */
		if ( (erq->length < SMALL_KEY_SIZE) || (erq->length > MAX_KEY_SIZE) )
			return -EINVAL;
		
		if (copy_from_user(keybuf, erq->pointer, erq->length))
			return -EFAULT;
	}
	
	dldwd_lock(priv);
	
	if (erq->pointer) {
		if (erq->length > MAX_KEY_SIZE) {
			err = -E2BIG;
			goto out;
		}
		
		if ( (erq->length > LARGE_KEY_SIZE)
		     || ( ! priv->has_big_wep && (erq->length > SMALL_KEY_SIZE))  ) {
			err = -EINVAL;
			goto out;
		}
		
		if ((index < 0) || (index >= MAX_KEYS))
			index = priv->tx_key;
		
		if (erq->length > SMALL_KEY_SIZE) {
			xlen = LARGE_KEY_SIZE;
		} else if (erq->length > 0) {
			xlen = SMALL_KEY_SIZE;
		} else
			xlen = 0;
		
		/* Switch on WEP if off */
		if ((!enable) && (xlen > 0)) {
			setindex = index;
			enable = 1;
		}
	} else {
		/* Important note : if the user do "iwconfig eth0 enc off",
		 * we will arrive there with an index of -1. This is valid
		 * but need to be taken care off... Jean II */
		if ((index < 0) || (index >= MAX_KEYS)) {
			if((index != -1) || (erq->flags == 0)) {
				err = -EINVAL;
				goto out;
			}
		} else {
			/* Set the index : Check that the key is valid */
			if(priv->keys[index].len == 0) {
				err = -EINVAL;
				goto out;
			}
			setindex = index;
		}
	}
	
	if (erq->flags & IW_ENCODE_DISABLED)
		enable = 0;
	/* Only for Prism2 & Symbol cards (so far) - Jean II */
	if (erq->flags & IW_ENCODE_OPEN)
		restricted = 0;
	if (erq->flags & IW_ENCODE_RESTRICTED)
		restricted = 1;

	if (erq->pointer) {
		priv->keys[index].len = cpu_to_le16(xlen);
		memset(priv->keys[index].data, 0, sizeof(priv->keys[index].data));
		memcpy(priv->keys[index].data, keybuf, erq->length);
	}
	priv->tx_key = setindex;
	priv->wep_on = enable;
	priv->wep_restrict = restricted;
	
 out:
	dldwd_unlock(priv);

	return 0;
}

static int dldwd_ioctl_getiwencode(struct net_device *dev, struct iw_point *erq)
{
	dldwd_priv_t *priv = dev->priv;
	int index = (erq->flags & IW_ENCODE_INDEX) - 1;
	u16 xlen = 0;
	char keybuf[MAX_KEY_SIZE];

	
	dldwd_lock(priv);

	if ((index < 0) || (index >= MAX_KEYS))
		index = priv->tx_key;

	erq->flags = 0;
	if (! priv->wep_on)
		erq->flags |= IW_ENCODE_DISABLED;
	erq->flags |= index + 1;
	
	/* Only for symbol cards - Jean II */
	if (priv->firmware_type != FIRMWARE_TYPE_LUCENT) {
		if(priv->wep_restrict)
			erq->flags |= IW_ENCODE_RESTRICTED;
		else
			erq->flags |= IW_ENCODE_OPEN;
	}

	xlen = le16_to_cpu(priv->keys[index].len);

	erq->length = xlen;

	if (erq->pointer) {
		memcpy(keybuf, priv->keys[index].data, MAX_KEY_SIZE);
	}
	
	dldwd_unlock(priv);

	if (erq->pointer) {
		if (copy_to_user(erq->pointer, keybuf, xlen))
			return -EFAULT;
	}

	return 0;
}

static int dldwd_ioctl_setessid(struct net_device *dev, struct iw_point *erq)
{
	dldwd_priv_t *priv = dev->priv;
	char essidbuf[IW_ESSID_MAX_SIZE+1];

	/* Note : ESSID is ignored in Ad-Hoc demo mode, but we can set it
	 * anyway... - Jean II */

	memset(&essidbuf, 0, sizeof(essidbuf));

	if (erq->flags) {
		if (erq->length > IW_ESSID_MAX_SIZE)
			return -E2BIG;
		
		if (copy_from_user(&essidbuf, erq->pointer, erq->length))
			return -EFAULT;

		essidbuf[erq->length] = '\0';
	}

	dldwd_lock(priv);

	memcpy(priv->desired_essid, essidbuf, sizeof(priv->desired_essid));

	dldwd_unlock(priv);

	return 0;
}

static int dldwd_ioctl_getessid(struct net_device *dev, struct iw_point *erq)
{
	dldwd_priv_t *priv = dev->priv;
	char essidbuf[IW_ESSID_MAX_SIZE+1];
	int active;
	int err = 0;

	TRACE_ENTER(dev->name);

	err = dldwd_hw_get_essid(priv, &active, essidbuf);
	if (err)
		return err;

	erq->flags = 1;
	erq->length = strlen(essidbuf) + 1;
	if (erq->pointer)
		if ( copy_to_user(erq->pointer, essidbuf, erq->length) )
			return -EFAULT;

	TRACE_EXIT(dev->name);
	
	return 0;
}

static int dldwd_ioctl_setnick(struct net_device *dev, struct iw_point *nrq)
{
	dldwd_priv_t *priv = dev->priv;
	char nickbuf[IW_ESSID_MAX_SIZE+1];

	if (nrq->length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	memset(nickbuf, 0, sizeof(nickbuf));

	if (copy_from_user(nickbuf, nrq->pointer, nrq->length))
		return -EFAULT;

	nickbuf[nrq->length] = '\0';
	
	dldwd_lock(priv);

	memcpy(priv->nick, nickbuf, sizeof(priv->nick));

	dldwd_unlock(priv);

	return 0;
}

static int dldwd_ioctl_getnick(struct net_device *dev, struct iw_point *nrq)
{
	dldwd_priv_t *priv = dev->priv;
	char nickbuf[IW_ESSID_MAX_SIZE+1];

	dldwd_lock(priv);
	memcpy(nickbuf, priv->nick, IW_ESSID_MAX_SIZE+1);
	dldwd_unlock(priv);

	nrq->length = strlen(nickbuf)+1;

	if (copy_to_user(nrq->pointer, nickbuf, sizeof(nickbuf)))
		return -EFAULT;

	return 0;
}

static int dldwd_ioctl_setfreq(struct net_device *dev, struct iw_freq *frq)
{
	dldwd_priv_t *priv = dev->priv;
	int chan = -1;

	/* We can only use this in Ad-Hoc demo mode to set the operating
	 * frequency, or in IBSS mode to set the frequency where the IBSS
	 * will be created - Jean II */
	if (priv->iw_mode != IW_MODE_ADHOC)
		return -EOPNOTSUPP;

	if ( (frq->e == 0) && (frq->m <= 1000) ) {
		/* Setting by channel number */
		chan = frq->m;
	} else {
		/* Setting by frequency - search the table */
		int mult = 1;
		int i;

		for (i = 0; i < (6 - frq->e); i++)
			mult *= 10;

		for (i = 0; i < NUM_CHANNELS; i++)
			if (frq->m == (channel_frequency[i] * mult))
				chan = i+1;
	}

	if ( (chan < 1) || (chan > NUM_CHANNELS) ||
	     ! (priv->channel_mask & (1 << (chan-1)) ) )
		return -EINVAL;

	dldwd_lock(priv);
	priv->channel = chan;
	dldwd_unlock(priv);

	return 0;
}

static int dldwd_ioctl_getsens(struct net_device *dev, struct iw_param *srq)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	u16 val;
	int err;

	dldwd_lock(priv);
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_SYSTEM_SCALE, &val);
	dldwd_unlock(priv);

	if (err)
		return err;

	srq->value = val;
	srq->fixed = 0; /* auto */

	return 0;
}

static int dldwd_ioctl_setsens(struct net_device *dev, struct iw_param *srq)
{
	dldwd_priv_t *priv = dev->priv;
	int val = srq->value;

	if ((val < 1) || (val > 3))
		return -EINVAL;
	
	dldwd_lock(priv);
	priv->ap_density = val;
	dldwd_unlock(priv);

	return 0;
}

static int dldwd_ioctl_setrts(struct net_device *dev, struct iw_param *rrq)
{
	dldwd_priv_t *priv = dev->priv;
	int val = rrq->value;

	if (rrq->disabled)
		val = 2347;

	if ( (val < 0) || (val > 2347) )
		return -EINVAL;

	dldwd_lock(priv);
	priv->rts_thresh = val;
	dldwd_unlock(priv);

	return 0;
}

static int dldwd_ioctl_setfrag(struct net_device *dev, struct iw_param *frq)
{
	dldwd_priv_t *priv = dev->priv;
	int err = 0;

	dldwd_lock(priv);

	if (priv->has_mwo) {
		if (frq->disabled)
			priv->mwo_robust = 0;
		else {
			if (frq->fixed)
				printk(KERN_WARNING "%s: Fixed fragmentation not \
supported on this firmware. Using MWO robust instead.\n", dev->name);
			priv->mwo_robust = 1;
		}
	} else {
		if (frq->disabled)
			priv->frag_thresh = 2346;
		else {
			if ( (frq->value < 256) || (frq->value > 2346) )
				err = -EINVAL;
			else
				priv->frag_thresh = frq->value & ~0x1; /* must be even */
		}
	}

	dldwd_unlock(priv);

	return err;
}

static int dldwd_ioctl_getfrag(struct net_device *dev, struct iw_param *frq)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 val;

	dldwd_lock(priv);
	
	if (priv->has_mwo) {
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_MWO_ROBUST, &val);
		if (err)
			val = 0;

		frq->value = val ? 2347 : 0;
		frq->disabled = ! val;
		frq->fixed = 0;
	} else {
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_FRAG_THRESH, &val);
		if (err)
			val = 0;

		frq->value = val;
		frq->disabled = (val >= 2346);
		frq->fixed = 1;
	}

	dldwd_unlock(priv);
	
	return err;
}

static int dldwd_ioctl_setrate(struct net_device *dev, struct iw_param *rrq)
{
	dldwd_priv_t *priv = dev->priv;
	int err = 0;
	int rate_ctrl = -1;
	int fixed, upto;
	int brate;
	int i;

	dldwd_lock(priv);

	/* Normalise value */
	brate = rrq->value / 500000;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_LUCENT: /* Lucent style rate */
		if (! rrq->fixed) {
			if (brate > 0)
				brate = -brate;
			else
				brate = -22;
		}
	
		for (i = 0; i < NUM_RATES; i++)
			if (rate_list[i] == brate) {
				rate_ctrl = i;
				break;
			}
	
		if ( (rate_ctrl < 1) || (rate_ctrl >= NUM_RATES) )
			err = -EINVAL;
		else
			priv->tx_rate_ctrl = rate_ctrl;
		break;
	case FIRMWARE_TYPE_INTERSIL: /* Intersil style rate */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style rate */
		switch(brate) {
		case 0:
			fixed = 0x0;
			upto = 0xF;
			break;
		case 2:
			fixed = 0x1;
			upto = 0x1;
			break;
		case 4:
			fixed = 0x2;
			upto = 0x3;
			break;
		case 11:
			fixed = 0x4;
			upto = 0x7;
			break;
		case 22:
			fixed = 0x8;
			upto = 0xF;
			break;
		default:
			fixed = 0x0;
			upto = 0x0;
		}
		if (rrq->fixed)
			rate_ctrl = fixed;
		else
			rate_ctrl = upto;
		if (rate_ctrl == 0)
			err = -EINVAL;
		else
			priv->tx_rate_ctrl = rate_ctrl;
		break;
	}

	dldwd_unlock(priv);

	return err;
}

static int dldwd_ioctl_getrate(struct net_device *dev, struct iw_param *rrq)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 val;
	int brate = 0;

	dldwd_lock(priv);
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_TX_RATE_CTRL, &val);
	if (err)
		goto out;
	
	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_LUCENT: /* Lucent style rate */
		brate = rate_list[val];
	
		if (brate < 0) {
			rrq->fixed = 0;

			err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CURRENT_TX_RATE, &val);
			if (err)
				goto out;

			if (val == 6)
				brate = 11;
			else
				brate = 2*val;
		} else
			rrq->fixed = 1;
		break;
	case FIRMWARE_TYPE_INTERSIL: /* Intersil style rate */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style rate */
		/* Check if auto or fixed (crude approximation) */
		if((val & 0x1) && (val > 1)) {
			rrq->fixed = 0;

			err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CURRENT_TX_RATE, &val);
			if (err)
				goto out;
		} else
			rrq->fixed = 1;

		if(val >= 8)
			brate = 22;
		else if(val >= 4)
			brate = 11;
		else if(val >= 2)
			brate = 4;
		else
			brate = 2;
		break;
	}

	rrq->value = brate * 500000;
	rrq->disabled = 0;

 out:
	dldwd_unlock(priv);

	return err;
}

static int dldwd_ioctl_setpower(struct net_device *dev, struct iw_param *prq)
{
	dldwd_priv_t *priv = dev->priv;
	int err = 0;


	dldwd_lock(priv);

	if (prq->disabled) {
		priv->pm_on = 0;
	} else {
		switch (prq->flags & IW_POWER_MODE) {
		case IW_POWER_UNICAST_R:
			priv->pm_mcast = 0;
			priv->pm_on = 1;
			break;
		case IW_POWER_ALL_R:
			priv->pm_mcast = 1;
			priv->pm_on = 1;
			break;
		case IW_POWER_ON:
			/* No flags : but we may have a value - Jean II */
			break;
		default:
			err = -EINVAL;
		}
		if (err)
			goto out;
		
		if (prq->flags & IW_POWER_TIMEOUT) {
			priv->pm_on = 1;
			priv->pm_timeout = prq->value / 1000;
		}
		if (prq->flags & IW_POWER_PERIOD) {
			priv->pm_on = 1;
			priv->pm_period = prq->value / 1000;
		}
		/* It's valid to not have a value if we are just toggling
		 * the flags... Jean II */
		if(!priv->pm_on) {
			err = -EINVAL;
			goto out;
		}			
	}

 out:
	dldwd_unlock(priv);

	return err;
}

static int dldwd_ioctl_getpower(struct net_device *dev, struct iw_param *prq)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 enable, period, timeout, mcast;

	dldwd_lock(priv);
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_ENABLE, &enable);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_PERIOD, &period);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_HOLDOVER, &timeout);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNF_PM_MCAST_RX, &mcast);
	if (err)
		goto out;

	prq->disabled = !enable;
	/* Note : by default, display the period */
	if ((prq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		prq->flags = IW_POWER_TIMEOUT;
		prq->value = timeout * 1000;
	} else {
		prq->flags = IW_POWER_PERIOD;
		prq->value = period * 1000;
	}
	if (mcast)
		prq->flags |= IW_POWER_ALL_R;
	else
		prq->flags |= IW_POWER_UNICAST_R;

 out:
	dldwd_unlock(priv);

	return err;
}

#if WIRELESS_EXT > 10
static int dldwd_ioctl_getretry(struct net_device *dev, struct iw_param *rrq)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	u16 short_limit, long_limit, lifetime;

	dldwd_lock(priv);
	
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_SHORT_RETRY_LIMIT, &short_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_LONG_RETRY_LIMIT, &long_limit);
	if (err)
		goto out;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_MAX_TX_LIFETIME, &lifetime);
	if (err)
		goto out;

	rrq->disabled = 0;		/* Can't be disabled */

	/* Note : by default, display the retry number */
	if ((rrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		rrq->flags = IW_RETRY_LIFETIME;
		rrq->value = lifetime * 1000;	/* ??? */
	} else {
		/* By default, display the min number */
		if ((rrq->flags & IW_RETRY_MAX)) {
			rrq->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
			rrq->value = long_limit;
		} else {
			rrq->flags = IW_RETRY_LIMIT;
			rrq->value = short_limit;
			if(short_limit != long_limit)
				rrq->flags |= IW_RETRY_MIN;
		}
	}

 out:
	dldwd_unlock(priv);

	return err;
}
#endif /* WIRELESS_EXT > 10 */

static int dldwd_ioctl_setibssport(struct net_device *dev, struct iwreq *wrq)
{
	dldwd_priv_t *priv = dev->priv;
	int val = *( (int *) wrq->u.name );

	dldwd_lock(priv);
	priv->ibss_port = val ;

	/* Actually update the mode we are using */
	set_port_type(priv);

	dldwd_unlock(priv);
	return 0;
}

static int dldwd_ioctl_getibssport(struct net_device *dev, struct iwreq *wrq)
{
	dldwd_priv_t *priv = dev->priv;
	int *val = (int *)wrq->u.name;

	dldwd_lock(priv);
	*val = priv->ibss_port;
	dldwd_unlock(priv);

	return 0;
}

static int dldwd_ioctl_setport3(struct net_device *dev, struct iwreq *wrq)
{
	dldwd_priv_t *priv = dev->priv;
	int val = *( (int *) wrq->u.name );
	int err = 0;

	dldwd_lock(priv);
	switch (val) {
	case 0: /* Try to do IEEE ad-hoc mode */
		if (! priv->has_ibss) {
			err = -EINVAL;
			break;
		}
		DEBUG(2, "%s: Prefer IBSS Ad-Hoc mode\n", dev->name);
		priv->prefer_port3 = 0;
			
		break;

	case 1: /* Try to do Lucent proprietary ad-hoc mode */
		if (! priv->has_port3) {
			err = -EINVAL;
			break;
		}
		DEBUG(2, "%s: Prefer Ad-Hoc demo mode\n", dev->name);
		priv->prefer_port3 = 1;
		break;

	default:
		err = -EINVAL;
	}

	if (! err)
		/* Actually update the mode we are using */
		set_port_type(priv);

	dldwd_unlock(priv);

	return err;
}

static int dldwd_ioctl_getport3(struct net_device *dev, struct iwreq *wrq)
{
	dldwd_priv_t *priv = dev->priv;
	int *val = (int *)wrq->u.name;

	dldwd_lock(priv);
	*val = priv->prefer_port3;
	dldwd_unlock(priv);

	return 0;
}

/* Spy is used for link quality/strength measurements in Ad-Hoc mode
 * Jean II */
static int dldwd_ioctl_setspy(struct net_device *dev, struct iw_point *srq)
{
	dldwd_priv_t *priv = dev->priv;
	struct sockaddr address[IW_MAX_SPY];
	int number = srq->length;
	int i;
	int err = 0;

	/* Check the number of addresses */
	if (number > IW_MAX_SPY)
		return -E2BIG;

	/* Get the data in the driver */
	if (srq->pointer) {
		if (copy_from_user(address, srq->pointer,
				   sizeof(struct sockaddr) * number))
			return -EFAULT;
	}

	/* Make sure nobody mess with the structure while we do */
	dldwd_lock(priv);

	/* dldwd_lock() doesn't disable interrupts, so make sure the
	 * interrupt rx path don't get confused while we copy */
	priv->spy_number = 0;

	if (number > 0) {
		/* Extract the addresses */
		for (i = 0; i < number; i++)
			memcpy(priv->spy_address[i], address[i].sa_data,
			       ETH_ALEN);
		/* Reset stats */
		memset(priv->spy_stat, 0,
		       sizeof(struct iw_quality) * IW_MAX_SPY);
		/* Set number of addresses */
		priv->spy_number = number;
	}

	/* Time to show what we have done... */
	DEBUG(0, "%s: New spy list:\n", dev->name);
	for (i = 0; i < number; i++) {
		DEBUG(0, "%s: %d - %02x:%02x:%02x:%02x:%02x:%02x\n",
		      dev->name, i+1,
		      priv->spy_address[i][0], priv->spy_address[i][1],
		      priv->spy_address[i][2], priv->spy_address[i][3],
		      priv->spy_address[i][4], priv->spy_address[i][5]);
	}

	/* Now, let the others play */
	dldwd_unlock(priv);

	return err;
}

static int dldwd_ioctl_getspy(struct net_device *dev, struct iw_point *srq)
{
	dldwd_priv_t *priv = dev->priv;
	struct sockaddr address[IW_MAX_SPY];
	struct iw_quality spy_stat[IW_MAX_SPY];
	int number;
	int i;

	dldwd_lock(priv);

	number = priv->spy_number;
	if ((number > 0) && (srq->pointer)) {
		/* Create address struct */
		for (i = 0; i < number; i++) {
			memcpy(address[i].sa_data, priv->spy_address[i],
			       ETH_ALEN);
			address[i].sa_family = AF_UNIX;
		}
		/* Copy stats */
		/* In theory, we should disable irqs while copying the stats
		 * because the rx path migh update it in the middle...
		 * Bah, who care ? - Jean II */
		memcpy(&spy_stat, priv->spy_stat,
		       sizeof(struct iw_quality) * IW_MAX_SPY);
		for (i=0; i < number; i++)
			priv->spy_stat[i].updated = 0;
	}

	dldwd_unlock(priv);

	/* Push stuff to user space */
	srq->length = number;
	if(copy_to_user(srq->pointer, address,
			 sizeof(struct sockaddr) * number))
		return -EFAULT;
	if(copy_to_user(srq->pointer + (sizeof(struct sockaddr)*number),
			&spy_stat, sizeof(struct iw_quality) * number))
		return -EFAULT;

	return 0;
}

int
dldwd_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	dldwd_priv_t *priv = dev->priv;
	struct iwreq *wrq = (struct iwreq *)rq;
	int err = 0;
	int changed = 0;

	TRACE_ENTER(dev->name);

	/* In theory, we could allow most of the the SET stuff to be done
	 * In practice, the laps of time at startup when the card is not
	 * ready is very short, so why bother...
	 * Note that hw_ready is different from up/down (ifconfig), when
	 * the device is not yet up, it is usually already ready...
	 * Jean II */
	if (!priv->hw_ready)
		return -ENODEV;

	switch (cmd) {
	case SIOCGIWNAME:
		DEBUG(1, "%s: SIOCGIWNAME\n", dev->name);
		strcpy(wrq->u.name, "IEEE 802.11-DS");
		break;
		
	case SIOCGIWAP:
		DEBUG(1, "%s: SIOCGIWAP\n", dev->name);
		wrq->u.ap_addr.sa_family = ARPHRD_ETHER;
		err = dldwd_hw_get_bssid(priv, wrq->u.ap_addr.sa_data);
		break;

	case SIOCGIWRANGE:
		DEBUG(1, "%s: SIOCGIWRANGE\n", dev->name);
		err = dldwd_ioctl_getiwrange(dev, &wrq->u.data);
		break;

	case SIOCSIWMODE:
		DEBUG(1, "%s: SIOCSIWMODE\n", dev->name);
		dldwd_lock(priv);
		switch (wrq->u.mode) {
		case IW_MODE_ADHOC:
			if (! (priv->has_ibss || priv->has_port3) )
				err = -EINVAL;
			else {
				priv->iw_mode = IW_MODE_ADHOC;
				changed = 1;
			}
			break;

		case IW_MODE_INFRA:
			priv->iw_mode = IW_MODE_INFRA;
			changed = 1;
			break;

		default:
			err = -EINVAL;
			break;
		}
		set_port_type(priv);
		dldwd_unlock(priv);
		break;

	case SIOCGIWMODE:
		DEBUG(1, "%s: SIOCGIWMODE\n", dev->name);
		dldwd_lock(priv);
		wrq->u.mode = priv->iw_mode;
		dldwd_unlock(priv);
		break;

	case SIOCSIWENCODE:
		DEBUG(1, "%s: SIOCSIWENCODE\n", dev->name);
		if (! priv->has_wep) {
			err = -EOPNOTSUPP;
			break;
		}

		err = dldwd_ioctl_setiwencode(dev, &wrq->u.encoding);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWENCODE:
		DEBUG(1, "%s: SIOCGIWENCODE\n", dev->name);
		if (! priv->has_wep) {
			err = -EOPNOTSUPP;
			break;
		}

		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		err = dldwd_ioctl_getiwencode(dev, &wrq->u.encoding);
		break;

	case SIOCSIWESSID:
		DEBUG(1, "%s: SIOCSIWESSID\n", dev->name);
		err = dldwd_ioctl_setessid(dev, &wrq->u.essid);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWESSID:
		DEBUG(1, "%s: SIOCGIWESSID\n", dev->name);
		err = dldwd_ioctl_getessid(dev, &wrq->u.essid);
		break;

	case SIOCSIWNICKN:
		DEBUG(1, "%s: SIOCSIWNICKN\n", dev->name);
		err = dldwd_ioctl_setnick(dev, &wrq->u.data);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWNICKN:
		DEBUG(1, "%s: SIOCGIWNICKN\n", dev->name);
		err = dldwd_ioctl_getnick(dev, &wrq->u.data);
		break;

	case SIOCGIWFREQ:
		DEBUG(1, "%s: SIOCGIWFREQ\n", dev->name);
		wrq->u.freq.m = dldwd_hw_get_freq(priv);
		wrq->u.freq.e = 1;
		break;

	case SIOCSIWFREQ:
		DEBUG(1, "%s: SIOCSIWFREQ\n", dev->name);
		err = dldwd_ioctl_setfreq(dev, &wrq->u.freq);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWSENS:
		DEBUG(1, "%s: SIOCGIWSENS\n", dev->name);
		err = dldwd_ioctl_getsens(dev, &wrq->u.sens);
		break;

	case SIOCSIWSENS:
		DEBUG(1, "%s: SIOCSIWSENS\n", dev->name);
		err = dldwd_ioctl_setsens(dev, &wrq->u.sens);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWRTS:
		DEBUG(1, "%s: SIOCGIWRTS\n", dev->name);
		wrq->u.rts.value = priv->rts_thresh;
		wrq->u.rts.disabled = (wrq->u.rts.value == 2347);
		wrq->u.rts.fixed = 1;
		break;

	case SIOCSIWRTS:
		DEBUG(1, "%s: SIOCSIWRTS\n", dev->name);
		err = dldwd_ioctl_setrts(dev, &wrq->u.rts);
		if (! err)
			changed = 1;
		break;

	case SIOCSIWFRAG:
		DEBUG(1, "%s: SIOCSIWFRAG\n", dev->name);
		err = dldwd_ioctl_setfrag(dev, &wrq->u.frag);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWFRAG:
		DEBUG(1, "%s: SIOCGIWFRAG\n", dev->name);
		err = dldwd_ioctl_getfrag(dev, &wrq->u.frag);
		break;

	case SIOCSIWRATE:
		DEBUG(1, "%s: SIOCSIWRATE\n", dev->name);
		err = dldwd_ioctl_setrate(dev, &wrq->u.bitrate);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWRATE:
		DEBUG(1, "%s: SIOCGIWRATE\n", dev->name);
		err = dldwd_ioctl_getrate(dev, &wrq->u.bitrate);
		break;

	case SIOCSIWPOWER:
		DEBUG(1, "%s: SIOCSIWPOWER\n", dev->name);
		err = dldwd_ioctl_setpower(dev, &wrq->u.power);
		if (! err)
			changed = 1;
		break;

	case SIOCGIWPOWER:
		DEBUG(1, "%s: SIOCGIWPOWER\n", dev->name);
		err = dldwd_ioctl_getpower(dev, &wrq->u.power);
		break;

	case SIOCGIWTXPOW:
		DEBUG(1, "%s: SIOCGIWTXPOW\n", dev->name);
		/* The card only supports one tx power, so this is easy */
		wrq->u.txpower.value = 15; /* dBm */
		wrq->u.txpower.fixed = 1;
		wrq->u.txpower.disabled = 0;
		wrq->u.txpower.flags = IW_TXPOW_DBM;
		break;

#if WIRELESS_EXT > 10
	case SIOCSIWRETRY:
		DEBUG(1, "%s: SIOCSIWRETRY\n", dev->name);
		err = -EOPNOTSUPP;
		break;

	case SIOCGIWRETRY:
		DEBUG(1, "%s: SIOCGIWRETRY\n", dev->name);
		err = dldwd_ioctl_getretry(dev, &wrq->u.retry);
		break;
#endif /* WIRELESS_EXT > 10 */

	case SIOCSIWSPY:
		DEBUG(1, "%s: SIOCSIWSPY\n", dev->name);

		err = dldwd_ioctl_setspy(dev, &wrq->u.data);
		break;

	case SIOCGIWSPY:
		DEBUG(1, "%s: SIOCGIWSPY\n", dev->name);

		err = dldwd_ioctl_getspy(dev, &wrq->u.data);
		break;

	case SIOCGIWPRIV:
		DEBUG(1, "%s: SIOCGIWPRIV\n", dev->name);
		if (wrq->u.data.pointer) {
			struct iw_priv_args privtab[] = {
				{ SIOCDEVPRIVATE + 0x0, 0, 0, "force_reset" },
				{ SIOCDEVPRIVATE + 0x1, 0, 0, "card_reset" },
				{ SIOCDEVPRIVATE + 0x2,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  0, "set_port3" },
				{ SIOCDEVPRIVATE + 0x3, 0,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  "get_port3" },
				{ SIOCDEVPRIVATE + 0x4,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  0, "set_preamble" },
				{ SIOCDEVPRIVATE + 0x5, 0,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  "get_preamble" },
				{ SIOCDEVPRIVATE + 0x6,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  0, "set_ibssport" },
				{ SIOCDEVPRIVATE + 0x7, 0,
				  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
				  "get_ibssport" }
			};

			err = verify_area(VERIFY_WRITE, wrq->u.data.pointer, sizeof(privtab));
			if (err)
				break;
			
			wrq->u.data.length = sizeof(privtab) / sizeof(privtab[0]);
			if (copy_to_user(wrq->u.data.pointer, privtab, sizeof(privtab)))
				err = -EFAULT;
		}
		break;
	       
	case SIOCDEVPRIVATE + 0x0: /* force_reset */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x0 (force_reset)\n",
		      dev->name);
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}
		
		printk(KERN_DEBUG "%s: Forcing reset!\n", dev->name);
		dldwd_reset(priv);
		break;

	case SIOCDEVPRIVATE + 0x1: /* card_reset */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x1 (card_reset)\n",
		      dev->name);
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}
		
		printk(KERN_DEBUG "%s: Forcing card reset!\n", dev->name);
		if(priv->card_reset_handler != NULL)
			priv->card_reset_handler(priv);
		dldwd_reset(priv);
		break;

	case SIOCDEVPRIVATE + 0x2: /* set_port3 */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x2 (set_port3)\n",
		      dev->name);
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		err = dldwd_ioctl_setport3(dev, wrq);
		if (! err)
			changed = 1;
		break;

	case SIOCDEVPRIVATE + 0x3: /* get_port3 */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x3 (get_port3)\n",
		      dev->name);
		err = dldwd_ioctl_getport3(dev, wrq);
		break;

	case SIOCDEVPRIVATE + 0x4: /* set_preamble */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x4 (set_preamble)\n",
		      dev->name);
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		/* 802.11b has recently defined some short preamble.
		 * Basically, the Phy header has been reduced in size.
		 * This increase performance, especially at high rates
		 * (the preamble is transmitted at 1Mb/s), unfortunately
		 * this give compatibility troubles... - Jean II */
		if(priv->has_preamble) {
			int val = *( (int *) wrq->u.name );

			dldwd_lock(priv);
			if(val)
				priv->preamble = 1;
			else
				priv->preamble = 0;
			dldwd_unlock(priv);
			changed = 1;
		} else
			err = -EOPNOTSUPP;
		break;

	case SIOCDEVPRIVATE + 0x5: /* get_preamble */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x5 (get_preamble)\n",
		      dev->name);
		if(priv->has_preamble) {
			int *val = (int *)wrq->u.name;

			dldwd_lock(priv);
			*val = priv->preamble;
			dldwd_unlock(priv);
		} else
			err = -EOPNOTSUPP;
		break;
	case SIOCDEVPRIVATE + 0x6: /* set_ibssport */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x6 (set_ibssport)\n",
		      dev->name);
		if (! capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}

		err = dldwd_ioctl_setibssport(dev, wrq);
		if (! err)
			changed = 1;
		break;

	case SIOCDEVPRIVATE + 0x7: /* get_ibssport */
		DEBUG(1, "%s: SIOCDEVPRIVATE + 0x7 (get_ibssport)\n",
		      dev->name);
		err = dldwd_ioctl_getibssport(dev, wrq);
		break;


	default:
		err = -EOPNOTSUPP;
	}
	
	if (! err && changed && netif_running(dev)) {
		err = dldwd_reset(priv);
		if (err) {
			/* Ouch ! What are we supposed to do ? */
			printk(KERN_ERR "orinoco_cs: Failed to set parameters on %s\n",
			       dev->name);
			netif_stop_queue(dev);
			dldwd_shutdown(priv);
			priv->hw_ready = 0;
		}
	}		

	TRACE_EXIT(dev->name);
		
	return err;
}

int
dldwd_change_mtu(struct net_device *dev, int new_mtu)
{
	TRACE_ENTER(dev->name);

	if ( (new_mtu < DLDWD_MIN_MTU) || (new_mtu > DLDWD_MAX_MTU) )
		return -EINVAL;

	dev->mtu = new_mtu;

	TRACE_EXIT(dev->name);

	return 0;
}

static void
__dldwd_set_multicast_list(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	int promisc, allmulti, mc_count;

	/* We'll wait until it's ready. Anyway, the network doesn't call us
	 * here until we are open - Jean II */
	if (!priv->hw_ready)
		return;


	TRACE_ENTER(dev->name);

	DEBUG(3, "dev->flags=0x%x, priv->promiscuous=%d, dev->mc_count=%d priv->mc_count=%d\n",
	      dev->flags, priv->promiscuous, dev->mc_count, priv->mc_count);

	/* The Hermes doesn't seem to have an allmulti mode, so we go
	 * into promiscuous mode and let the upper levels deal. */
	if ( (dev->flags & IFF_PROMISC) ) {
		promisc = 1;
		allmulti = 0;
		mc_count = 0;
	} else if ( (dev->flags & IFF_ALLMULTI) ||
		    (dev->mc_count > HERMES_MAX_MULTICAST) ) {
		promisc = 0;
		allmulti = 1;
		mc_count = HERMES_MAX_MULTICAST;
	} else {
		promisc = 0;
		allmulti = 0;
		mc_count = dev->mc_count;
	}

	DEBUG(3, "promisc=%d mc_count=%d\n",
	      promisc, mc_count);

	if (promisc != priv->promiscuous) { /* Don't touch the hardware if we don't have to */
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PROMISCUOUS,
					   promisc);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting promiscuity to %d.\n",
			       dev->name, err, promisc);
		} else 
			priv->promiscuous = promisc;
	}

	if (allmulti) {
		/* FIXME: This method of doing allmulticast reception
		   comes from the NetBSD driver. Haven't actually
		   tested whether it works or not. */
		hermes_multicast_t mclist;

		memset(&mclist, 0, sizeof(mclist));
		err = HERMES_WRITE_RECORD(hw, USER_BAP, HERMES_RID_CNF_MULTICAST_LIST, &mclist);
		if (err)
			printk(KERN_ERR "%s: Error %d setting multicast list.\n",
			       dev->name, err);
		else
			priv->allmulti = 1;
		       
	} else if (mc_count || (! mc_count && priv->mc_count) ) {
		struct dev_mc_list *p = dev->mc_list;
		hermes_multicast_t mclist;
		int i;

		for (i = 0; i < mc_count; i++) {
			/* First some paranoid checks */
			if (! p) {
				printk(KERN_ERR "%s: Multicast list shorter than mc_count.\n",
				       dev->name);
				break;
			}
			if (p->dmi_addrlen != ETH_ALEN) {

				printk(KERN_ERR "%s: Bad address size (%d) in multicast list.\n",
				       dev->name, p->dmi_addrlen);
				break;
			}

			memcpy(mclist.addr[i], p->dmi_addr, ETH_ALEN);
			p = p->next;
		}

		/* More paranoia */
		if (p)
			printk(KERN_ERR "%s: Multicast list longer than mc_count.\n",
			       dev->name);

		priv->mc_count = i;			

		DEBUG(3, "priv->mc_count = %d\n", priv->mc_count);

		err = hermes_write_ltv(hw, USER_BAP, HERMES_RID_CNF_MULTICAST_LIST,
				       HERMES_BYTES_TO_RECLEN(priv->mc_count * ETH_ALEN),
				       &mclist);
		if (err)
			printk(KERN_ERR "%s: Error %d setting multicast list.\n",
			       dev->name, err);
		else
			priv->allmulti = 0;
	}

	/* Since we can set the promiscuous flag when it wasn't asked
	   for, make sure the net_device knows about it. */
	if (priv->promiscuous)
		dev->flags |= IFF_PROMISC;
	else
		dev->flags &= ~IFF_PROMISC;

	if (priv->allmulti)
		dev->flags |= IFF_ALLMULTI;
	else
		dev->flags &= ~IFF_ALLMULTI;

	TRACE_EXIT(dev->name);
}

/*
 * procfs stuff
 */

static struct proc_dir_entry *dir_base = NULL;

/*
 * This function updates the total amount of data printed so far. It then
 * determines if the amount of data printed into a buffer  has reached the
 * offset requested. If it hasn't, then the buffer is shifted over so that
 * the next bit of data can be printed over the old bit. If the total
 * amount printed so far exceeds the total amount requested, then this
 * function returns 1, otherwise 0.
 */
static int 

shift_buffer(char *buffer, int requested_offset, int requested_len,
	     int *total, int *slop, char **buf)
{
	int printed;
	
	printed = *buf - buffer;
	if (*total + printed <= requested_offset) {
		*total += printed;
		*buf = buffer;
	}
	else {
		if (*total < requested_offset) {
			*slop = requested_offset - *total;
		}
		*total = requested_offset + printed - *slop;
	}
	if (*total > requested_offset + requested_len) {
		return 1;
	}
	else {
		return 0;
	}
}

/*
 * This function calculates the actual start of the requested data
 * in the buffer. It also calculates actual length of data returned,
 * which could be less that the amount of data requested.
 */
#define PROC_BUFFER_SIZE 4096
#define PROC_SAFE_SIZE 3072

static int
calc_start_len(char *buffer, char **start, int requested_offset,
	       int requested_len, int total, char *buf)
{
	int return_len, buffer_len;
	
	buffer_len = buf - buffer;
	if (buffer_len >= PROC_BUFFER_SIZE - 1) {
		printk(KERN_ERR "calc_start_len: exceeded /proc buffer size\n");
	}
	
	/*
	 * There may be bytes before and after the
	 * chunk that was actually requested.
	 */
	return_len = total - requested_offset;
	if (return_len < 0) {
		return_len = 0;
	}
	*start = buf - return_len;
	if (return_len > requested_len) {
		return_len = requested_len;
	}
	return return_len;
}

static int
dldwd_proc_get_hermes_regs(char *page, char **start, off_t requested_offset,
			   int requested_len, int *eof, void *data)
{
	dldwd_priv_t *dev = (dldwd_priv_t *)data;
	hermes_t *hw = &dev->hw;
	char *buf;
	int total = 0, slop = 0;

	/* Hum, in this case hardware register are probably not readable... */
	if (!dev->hw_ready)
		return -ENODEV;

	buf = page;

#define DHERMESREG(name) buf += sprintf(buf, "%-16s: %04x\n", #name, hermes_read_regn(hw, name))

	DHERMESREG(CMD);
	DHERMESREG(PARAM0);
	DHERMESREG(PARAM1);
	DHERMESREG(PARAM2);
	DHERMESREG(STATUS);
	DHERMESREG(RESP0);
	DHERMESREG(RESP1);
	DHERMESREG(RESP2);
	DHERMESREG(INFOFID);
	DHERMESREG(RXFID);
	DHERMESREG(ALLOCFID);
	DHERMESREG(TXCOMPLFID);
	DHERMESREG(SELECT0);
	DHERMESREG(OFFSET0);
	DHERMESREG(SELECT1);
	DHERMESREG(OFFSET1);
	DHERMESREG(EVSTAT);
	DHERMESREG(INTEN);
	DHERMESREG(EVACK);
	DHERMESREG(CONTROL);
	DHERMESREG(SWSUPPORT0);
	DHERMESREG(SWSUPPORT1);
	DHERMESREG(SWSUPPORT2);
	DHERMESREG(AUXPAGE);
	DHERMESREG(AUXOFFSET);
	DHERMESREG(AUXDATA);
#undef DHERMESREG

	shift_buffer(page, requested_offset, requested_len, &total,
		     &slop, &buf);
	return calc_start_len(page, start, requested_offset, requested_len,
			      total, buf);
}

struct {
	u16 rid;
	char *name;
	int minlen, maxlen;
	int displaytype;
#define DISPLAY_WORDS	0
#define DISPLAY_BYTES	1
#define DISPLAY_STRING	2
} record_table[] = {
#define RTCNFENTRY(name, type) { HERMES_RID_CNF_##name, #name, 0, LTV_BUF_SIZE, type }
	RTCNFENTRY(PORTTYPE, DISPLAY_WORDS),
	RTCNFENTRY(MACADDR, DISPLAY_BYTES),
	RTCNFENTRY(DESIRED_SSID, DISPLAY_STRING),
	RTCNFENTRY(CHANNEL, DISPLAY_WORDS),
	RTCNFENTRY(OWN_SSID, DISPLAY_STRING),
	RTCNFENTRY(SYSTEM_SCALE, DISPLAY_WORDS),
	RTCNFENTRY(MAX_DATA_LEN, DISPLAY_WORDS),
	RTCNFENTRY(PM_ENABLE, DISPLAY_WORDS),
	RTCNFENTRY(PM_MCAST_RX, DISPLAY_WORDS),
	RTCNFENTRY(PM_PERIOD, DISPLAY_WORDS),
	RTCNFENTRY(NICKNAME, DISPLAY_STRING),
	RTCNFENTRY(WEP_ON, DISPLAY_WORDS),
	RTCNFENTRY(MWO_ROBUST, DISPLAY_WORDS),
	RTCNFENTRY(MULTICAST_LIST, DISPLAY_BYTES),
	RTCNFENTRY(CREATEIBSS, DISPLAY_WORDS),
	RTCNFENTRY(FRAG_THRESH, DISPLAY_WORDS),
	RTCNFENTRY(RTS_THRESH, DISPLAY_WORDS),
	RTCNFENTRY(TX_RATE_CTRL, DISPLAY_WORDS),
	RTCNFENTRY(PROMISCUOUS, DISPLAY_WORDS),
	RTCNFENTRY(KEYS, DISPLAY_BYTES),
	RTCNFENTRY(TX_KEY, DISPLAY_WORDS),
	RTCNFENTRY(TICKTIME, DISPLAY_WORDS),
	RTCNFENTRY(INTERSIL_TX_KEY, DISPLAY_WORDS),
	RTCNFENTRY(INTERSIL_KEY0, DISPLAY_BYTES),
	RTCNFENTRY(INTERSIL_KEY1, DISPLAY_BYTES),
	RTCNFENTRY(INTERSIL_KEY2, DISPLAY_BYTES),
	RTCNFENTRY(INTERSIL_KEY3, DISPLAY_BYTES),
	RTCNFENTRY(INTERSIL_WEP_ON, DISPLAY_WORDS),
#undef RTCNFENTRY
#define RTINFENTRY(name,type) { HERMES_RID_##name, #name, 0, LTV_BUF_SIZE, type }
	RTINFENTRY(CHANNEL_LIST, DISPLAY_WORDS),
	RTINFENTRY(STAIDENTITY, DISPLAY_WORDS),
	RTINFENTRY(CURRENT_SSID, DISPLAY_STRING),
	RTINFENTRY(CURRENT_BSSID, DISPLAY_BYTES),
	RTINFENTRY(COMMSQUALITY, DISPLAY_WORDS),
	RTINFENTRY(CURRENT_TX_RATE, DISPLAY_WORDS),
	RTINFENTRY(WEP_AVAIL, DISPLAY_WORDS),
	RTINFENTRY(CURRENT_CHANNEL, DISPLAY_WORDS),
	RTINFENTRY(DATARATES, DISPLAY_BYTES),
#undef RTINFENTRY
};
#define NUM_RIDS ( sizeof(record_table) / sizeof(record_table[0]) )

static int
dldwd_proc_get_hermes_recs(char *page, char **start, off_t requested_offset,
			   int requested_len, int *eof, void *data)
{
	dldwd_priv_t *dev = (dldwd_priv_t *)data;
	hermes_t *hw = &dev->hw;
	char *buf;
	int total = 0, slop = 0;
	int i;
	u16 length;
	int err;

	/* Hum, in this case hardware register are probably not readable... */
	if (!dev->hw_ready)
		return -ENODEV;
		
	buf = page;

	/* print out all the config RIDs */
	for (i = 0; i < NUM_RIDS; i++) {
		u16 rid = record_table[i].rid;
		int minlen = record_table[i].minlen;
		int maxlen = record_table[i].maxlen;
		int len;
		u8 *val8;
		u16 *val16;
		int j;

		val8 = kmalloc(maxlen + 2, GFP_KERNEL);
		if (! val8)
			return -ENOMEM;

		err = hermes_read_ltv(hw, USER_BAP, rid, maxlen,
				      &length, val8);
		if (err) {
			DEBUG(0, "Error %d reading RID 0x%04x\n", err, rid);
			continue;
		}
		val16 = (u16 *)val8;

		buf += sprintf(buf, "%-15s (0x%04x): length=%d (%d bytes)\tvalue=", record_table[i].name,
			       rid, length, (length-1)*2);
		len = min( (int)max(minlen, ((int)length-1)*2), maxlen);

		switch (record_table[i].displaytype) {
		case DISPLAY_WORDS:
			for (j = 0; j < len / 2; j++) {
				buf += sprintf(buf, "%04X-", le16_to_cpu(val16[j]));
			}
			buf--;
			break;

		case DISPLAY_BYTES:
		default:
			for (j = 0; j < len; j++) {
				buf += sprintf(buf, "%02X:", val8[j]);
			}
			buf--;
			break;

		case DISPLAY_STRING:
			len = min(len, le16_to_cpu(val16[0])+2);
			val8[len] = '\0';
			buf += sprintf(buf, "\"%s\"", (char *)&val16[1]);
			break;
		}

		buf += sprintf(buf, "\n");

		kfree(val8);

		if (shift_buffer(page, requested_offset, requested_len,
				 &total, &slop, &buf))
			break;

		if ( (buf - page) > PROC_SAFE_SIZE )
			break;
	}

	return calc_start_len(page, start, requested_offset, requested_len,
			      total, buf);
}

/* initialise the /proc subsystem for the hermes driver, creating the
 * separate entries */
static int
dldwd_proc_init(void)
{
	int err = 0;

	TRACE_ENTER("dldwd");

	/* create the directory for it to sit in */
	dir_base = create_proc_entry("hermes", S_IFDIR, &proc_root);
	if (dir_base == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/hermes.\n");
		dldwd_proc_cleanup();
		err = -ENOMEM;
	}

	TRACE_EXIT("dldwd");

	return err;
}

int
dldwd_proc_dev_init(dldwd_priv_t *dev)
{
	struct net_device *ndev = &dev->ndev;

	dev->dir_dev = NULL;
	/* create the directory for it to sit in */
	dev->dir_dev = create_proc_entry(ndev->name, S_IFDIR | S_IRUGO | S_IXUGO,
					 dir_base);
	if (dev->dir_dev == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/hermes/%s.\n",  ndev->name);
		goto fail;
	}

	dev->dir_regs = NULL;
	dev->dir_regs = create_proc_read_entry("regs", S_IFREG | S_IRUGO,
					       dev->dir_dev, dldwd_proc_get_hermes_regs, dev);
	if (dev->dir_regs == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/hermes/%s/regs.\n",  ndev->name);
		goto fail;
	}

	dev->dir_recs = NULL;
	dev->dir_recs = create_proc_read_entry("recs", S_IFREG | S_IRUGO,
					       dev->dir_dev, dldwd_proc_get_hermes_recs, dev);
	if (dev->dir_recs == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/hermes/%s/recs.\n",  ndev->name);
		goto fail;
	}

	return 0;
 fail:
	dldwd_proc_dev_cleanup(dev);
	return -ENOMEM;
}

void
dldwd_proc_dev_cleanup(dldwd_priv_t *priv)
{
	struct net_device *ndev = &priv->ndev;

	if (priv->dir_regs) {
		remove_proc_entry("regs", priv->dir_dev);
		priv->dir_regs = NULL;
	}		
	if (priv->dir_recs) {
		remove_proc_entry("recs", priv->dir_dev);
		priv->dir_recs = NULL;
	}		
	if (priv->dir_dev) {
		remove_proc_entry(ndev->name, dir_base);
		priv->dir_dev = NULL;
	}
}

static void
dldwd_proc_cleanup(void)
{
	TRACE_ENTER("dldwd");

	if (dir_base) {
		remove_proc_entry("hermes", &proc_root);
		dir_base = NULL;
	}
	
	TRACE_EXIT("dldwd");
}

int
dldwd_setup(dldwd_priv_t* priv)
{
	struct net_device *dev = &priv->ndev;;

	spin_lock_init(&priv->lock);

	/* Set up the net_device */
	ether_setup(dev);
	dev->priv = priv;

	/* Setup up default routines */
	priv->card_reset_handler = NULL;	/* Caller may override */
	dev->init = dldwd_init;
	dev->open = NULL;		/* Caller *must* override */
	dev->stop = NULL;
	dev->hard_start_xmit = dldwd_xmit;
	dev->tx_timeout = dldwd_tx_timeout;
	dev->watchdog_timeo = HZ; /* 4 second timeout */

	dev->get_stats = dldwd_get_stats;
	dev->get_wireless_stats = dldwd_get_wireless_stats;

	dev->do_ioctl = dldwd_ioctl;

	dev->change_mtu = dldwd_change_mtu;
	dev->set_multicast_list = dldwd_set_multicast_list;

	netif_stop_queue(dev);

	return 0;
}

#ifdef ORINOCO_DEBUG
EXPORT_SYMBOL(dldwd_debug);
#endif
EXPORT_SYMBOL(dldwd_init);
EXPORT_SYMBOL(dldwd_xmit);
EXPORT_SYMBOL(dldwd_tx_timeout);
EXPORT_SYMBOL(dldwd_ioctl);
EXPORT_SYMBOL(dldwd_change_mtu);
EXPORT_SYMBOL(dldwd_set_multicast_list);
EXPORT_SYMBOL(dldwd_shutdown);
EXPORT_SYMBOL(dldwd_reset);
EXPORT_SYMBOL(dldwd_setup);
EXPORT_SYMBOL(dldwd_proc_dev_init);
EXPORT_SYMBOL(dldwd_proc_dev_cleanup);
EXPORT_SYMBOL(dldwd_interrupt);

static int __init init_dldwd(void)
{
	int err;

	err = dldwd_proc_init();

	printk(KERN_DEBUG "%s\n", version);

	return 0;
}

static void __exit exit_dldwd(void)
{
	dldwd_proc_cleanup();
}

module_init(init_dldwd);
module_exit(exit_dldwd);
