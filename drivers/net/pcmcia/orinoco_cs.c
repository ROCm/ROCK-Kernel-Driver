/* orinoco_cs.c 0.04	- (formerly known as dldwd_cs.c)
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
 * terms of the GNU Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the above.
 * If you wish to allow the use of your version of this file only
 * under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
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
 * For the moment access to the device statistics from the interrupt
 * handler is unsafe - we just put up with any resulting errors in the
 * statisics. FIXME: This should probably be changed to store the
 * stats in atomic types.
 *
 * EXCEPT that we don't want the irq handler running when we actually
 * reset or shut down the card, because strange things might happen
 * (probably the worst would be one packet of garbage, but you can't
 * be too careful). For this we use __dldwd_stop_irqs() which will set
 * a flag to disable the interrupt handler, and wait for any
 * outstanding instances of the handler to complete. THIS WILL LOSE
 * INTERRUPTS! so it shouldn't be used except for resets, when we
 * don't care about that.*/

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
 * TODO - Jean II
 *	o inline functions (lot's of candidate, need to reorder code)
 *	o Separate Pcmcia specific code to help Airport/Mini PCI driver
 *	o Test PrismII/Symbol cards & firmware versions
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
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

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/bus_ops.h>

#include "hermes.h"

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
static char *version = "orinoco_cs.c 0.04 (David Gibson <hermes@gibson.dropbear.id.au>)";
MODULE_PARM(pc_debug, "i");
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
#define DEBUGMORE(n, args...) do { if (pc_debug>(n)) printk(args); } while (0)
#else
#define DEBUG(n, args...) do { } while (0)
#define DEBUGMORE(n, args...) do { } while (0)
#endif

#define TRACE_ENTER(devname) DEBUG(2, "%s: -> " __FUNCTION__ "()\n", devname);
#define TRACE_EXIT(devname)  DEBUG(2, "%s: <- " __FUNCTION__ "()\n", devname);

#define MAX(a, b) ( (a) > (b) ? (a) : (b) )
#define MIN(a, b) ( (a) < (b) ? (a) : (b) )

#define RUP_EVEN(a) ( (a) % 2 ? (a) + 1 : (a) )


#if (! defined (WIRELESS_EXT)) || (WIRELESS_EXT < 10)
#error "orinoco_cs requires Wireless extensions v10 or later."
#endif /* (! defined (WIRELESS_EXT)) || (WIRELESS_EXT < 10) */
#define WIRELESS_SPY		// enable iwspy support

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/* The old way: bit map of interrupts to choose from */
/* This means pick from 15, 14, 12, 11, 10, 9, 7, 5, 4, and 3 */
static uint irq_mask = 0xdeb8;
/* Newer, simpler way of listing specific interrupts */
static int irq_list[4] = { -1 };
/* Do a Pcmcia soft reset (may help some cards) */
static int reset_cor = 0;

MODULE_PARM(irq_mask, "i");
MODULE_PARM(irq_list, "1-4i");
MODULE_PARM(reset_cor, "i");

/*====================================================================*/

#define DLDWD_MIN_MTU		256
#define DLDWD_MAX_MTU		(HERMES_FRAME_LEN_MAX - ENCAPS_OVERHEAD)

#define LTV_BUF_SIZE		128
#define USER_BAP		0
#define IRQ_BAP			1
#define DLDWD_MACPORT		0
#define IRQ_LOOP_MAX		10
#define TX_NICBUF_SIZE		2048
#define TX_NICBUF_SIZE_BUG	1585		/* Bug in Intel firmware */
#define MAX_KEYS		4
#define MAX_KEY_SIZE		14
#define LARGE_KEY_SIZE		13
#define SMALL_KEY_SIZE		5
#define MAX_FRAME_SIZE		2304

const long channel_frequency[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};

#define NUM_CHANNELS ( sizeof(channel_frequency) / sizeof(channel_frequency[0]) )

/* This tables gives the actual meanings of the bitrate IDs returned by the firmware.
   It gives the rate in halfMb/s, negative indicates auto mode */
const int rate_list[] = { 0, 2, 4, -22, 11, 22, -4, -11, 0, 0, 0, 0};

#define NUM_RATES (sizeof(rate_list) / sizeof(rate_list[0]))
typedef struct dldwd_key {
	uint16_t len;
	char data[MAX_KEY_SIZE];
} __attribute__ ((packed)) dldwd_key_t;

typedef dldwd_key_t dldwd_keys_t[MAX_KEYS];

typedef struct dldwd_priv {
	dev_link_t link;
	dev_node_t node;
	int instance;

	spinlock_t lock;
	long state;
#define DLDWD_STATE_INIRQ 0
#define DLDWD_STATE_DOIRQ 1

	/* Net device stuff */
	struct net_device ndev;
	struct net_device_stats stats;
	struct iw_statistics wstats;


	/* Hardware control variables */
	hermes_t hw;
	uint16_t txfid;

	/* Capabilities of the hardware/firmware */
	hermes_identity_t firmware_info;
	int firmware_type;
#define FIRMWARE_TYPE_LUCENT 1
#define FIRMWARE_TYPE_PRISM2 2
#define FIRMWARE_TYPE_SYMBOL 3
	int has_ibss, has_port3, prefer_port3, has_ibss_any;
	int has_wep, has_big_wep;
	int has_mwo;
	int has_pm;
	int has_retry;
	int has_preamble;
	int broken_reset, broken_allocate;
	uint16_t channel_mask;

	/* Current configuration */
	uint32_t iw_mode;
	int port_type, allow_ibss;
	uint16_t wep_on, wep_restrict, tx_key;
	dldwd_keys_t keys;
 	char nick[IW_ESSID_MAX_SIZE+1];
	char desired_essid[IW_ESSID_MAX_SIZE+1];
	uint16_t frag_thresh, mwo_robust;
	uint16_t channel;
	uint16_t ap_density, rts_thresh;
	uint16_t tx_rate_ctrl;
	uint16_t pm_on, pm_mcast, pm_period, pm_timeout;
	uint16_t retry_short, retry_long, retry_time;
	uint16_t preamble;

	int promiscuous, allmulti, mc_count;

#ifdef WIRELESS_SPY
	int			spy_number;
	u_char			spy_address[IW_MAX_SPY][ETH_ALEN];
	struct iw_quality	spy_stat[IW_MAX_SPY];
#endif

	/* /proc based debugging stuff */
	struct proc_dir_entry *dir_dev;
	struct proc_dir_entry *dir_regs;
	struct proc_dir_entry *dir_recs;
} dldwd_priv_t;

struct p80211_hdr {
	uint16_t frame_ctl;
	uint16_t duration_id;
	uint8_t addr1[ETH_ALEN];
	uint8_t addr2[ETH_ALEN];
	uint8_t addr3[ETH_ALEN];
	uint16_t seq_ctl;
	uint8_t addr4[ETH_ALEN];
	uint16_t data_len;
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
	uint8_t dsap;
	uint8_t ssap;
	uint8_t ctrl;
	uint8_t oui[3];
} __attribute__ ((packed));

struct dldwd_frame_hdr {
	hermes_frame_desc_t desc;
	struct p80211_hdr p80211;
	struct ethhdr p8023;
	struct p8022_hdr p8022;
	uint16_t ethertype;
} __attribute__ ((packed));

#define P8023_OFFSET		(sizeof(hermes_frame_desc_t) + \
				sizeof(struct p80211_hdr))
#define ENCAPS_OVERHEAD		(sizeof(struct p8022_hdr) + 2)

/*
 * Function prototypes
 */

/* PCMCIA gumpf */

static void dldwd_config(dev_link_t * link);
static void dldwd_release(u_long arg);
static int dldwd_event(event_t event, int priority,
		       event_callback_args_t * args);

static dev_link_t *dldwd_attach(void);
static void dldwd_detach(dev_link_t *);

/* Hardware control routines */

static int __dldwd_hw_reset(dldwd_priv_t *priv);
static void dldwd_shutdown(dldwd_priv_t *dev);
static int dldwd_reset(dldwd_priv_t *dev);
static int __dldwd_hw_setup_wep(dldwd_priv_t *priv);
static int dldwd_hw_get_bssid(dldwd_priv_t *priv, char buf[ETH_ALEN]);
static int dldwd_hw_get_essid(dldwd_priv_t *priv, int *active, char buf[IW_ESSID_MAX_SIZE+1]);
static long dldwd_hw_get_freq(dldwd_priv_t *priv);
static int dldwd_hw_get_bitratelist(dldwd_priv_t *priv, int *numrates,
				    int32_t *rates, int max);

/* Interrupt handling routines */
void dldwd_interrupt(int irq, void * dev_id, struct pt_regs *regs);
static void __dldwd_ev_tick(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_wterr(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_infdrop(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_info(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_rx(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_txexc(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_tx(dldwd_priv_t *priv, hermes_t *hw);
static void __dldwd_ev_alloc(dldwd_priv_t *priv, hermes_t *hw);

/* struct net_device methods */
static int dldwd_init(struct net_device *dev);
static int dldwd_open(struct net_device *dev);
static int dldwd_stop(struct net_device *dev);

static int dldwd_xmit(struct sk_buff *skb, struct net_device *dev);
static void dldwd_tx_timeout(struct net_device *dev);

static struct net_device_stats *dldwd_get_stats(struct net_device *dev);
static struct iw_statistics *dldwd_get_wireless_stats(struct net_device *dev);
static void dldwd_stat_gather(struct net_device *dev,
			      struct sk_buff *skb,
			      struct dldwd_frame_hdr *hdr);

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
static int dldwd_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

static int dldwd_change_mtu(struct net_device *dev, int new_mtu);
static void __dldwd_set_multicast_list(struct net_device *dev);

/* /proc debugging stuff */
static int dldwd_proc_init(void);
static void dldwd_proc_cleanup(void);
static int dldwd_proc_dev_init(dldwd_priv_t *dev);
static void dldwd_proc_dev_cleanup(dldwd_priv_t *dev);

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
__dldwd_start_irqs(dldwd_priv_t *priv, uint16_t irqmask)
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
			priv->port_type = 1;
			priv->allow_ibss = 1;
		}
		break;
	default:
		printk(KERN_ERR "%s: Invalid priv->iw_mode in set_port_type()\n",
		       priv->ndev.name);
	}
}

static inline void
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

static void
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

static int
dldwd_reset(dldwd_priv_t *priv)
{
	struct net_device *dev = &priv->ndev;
	hermes_t *hw = &priv->hw;
	int err = 0;
	hermes_id_t idbuf;
	int frame_size;

	TRACE_ENTER(priv->ndev.name);

	dldwd_lock(priv);

	__dldwd_stop_irqs(priv);

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
		if (err)
			goto out;
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

	/* Set retry settings - will fail on lot's of firmwares */
	if (priv->has_retry) {
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_SHORT_RETRY_LIMIT,
					   priv->retry_short);
		if (err) {
			printk(KERN_WARNING "%s: Can't set retry limit!\n", dev->name);
			goto out;
		}
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_LONG_RETRY_LIMIT,
					   priv->retry_long);
		if (err)
			goto out;
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_MAX_TX_LIFETIME,
					   priv->retry_time);
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
	int	extra_wep_flag = 0;

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

	case FIRMWARE_TYPE_PRISM2: /* Prism II style WEP */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style WEP */
		if (priv->wep_on) {
			char keybuf[LARGE_KEY_SIZE+1];
			int keylen;
			int i;
			
			/* Write all 4 keys */
			for(i = 0; i < MAX_KEYS; i++) {
				keylen = priv->keys[i].len;
				keybuf[keylen] = '\0';
				memcpy(keybuf, priv->keys[i].data, keylen);
				err = hermes_write_ltv(hw, USER_BAP,
						       HERMES_RID_CNF_PRISM2_KEY0 + i,
						       HERMES_BYTES_TO_RECLEN(keylen + 1),
						       &keybuf);
				if (err)
					return err;
			}

			err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PRISM2_TX_KEY,
						   priv->tx_key);
			if (err)
				return err;

			/* Authentication is where Prism2 and Symbol
			 * firmware differ... */
			if (priv->firmware_type == FIRMWARE_TYPE_SYMBOL) {
				/* Symbol cards : set the authentication :
				 * 0 -> no encryption, 1 -> open,
				 * 2 -> shared key, 3 -> shared key 128bit */
				if(priv->wep_restrict) {
					if(priv->keys[priv->tx_key].len >
					   SMALL_KEY_SIZE)
						extra_wep_flag = 3;
					else
						extra_wep_flag = 2;
				} else
					extra_wep_flag = 1;
				err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_SYMBOL_AUTH_TYPE, priv->wep_restrict);
				if (err)
					return err;
			} else {
				/* Prism2 card : we need to modify master
				 * WEP setting */
				if(priv->wep_restrict)
					extra_wep_flag = 2;
				else
					extra_wep_flag = 0;
			}
		}
		
		/* Master WEP setting : on/off */
		err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNF_PRISM2_WEP_ON, (priv->wep_on | extra_wep_flag));
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
		uint16_t rid;

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
	uint16_t channel;
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
				    int32_t *rates, int max)
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
	num = MIN(num, max);

	for (i = 0; i < num; i++) {
		rates[i] = (p[i] & 0x7f) * 500000; /* convert to bps */
	}

	return 0;
}

#ifndef PCMCIA_DEBUG
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
	uint16_t evstat, events;
	static int old_time = 0, timecount = 0; /* Eugh, revolting hack for now */

	if (test_and_set_bit(DLDWD_STATE_INIRQ, &priv->state))
		BUG();

	if (! dldwd_irqs_allowed(priv)) {
		clear_bit(DLDWD_STATE_INIRQ, &priv->state);
		return;
	}

	DEBUG(3, "%s: dldwd_interrupt()  irq %d\n", priv->ndev.name, irq);

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
	uint16_t rxfid, status;
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
	err = hermes_bap_pread(hw, IRQ_BAP, &hdr, sizeof(hdr), rxfid, 0);
	if (err) {
		printk(KERN_ERR "%s: error %d reading frame header. "
		       "Frame dropped.\n", dev->name, err);
		stats->rx_errors++;
		goto drop;
	}

	status = le16_to_cpu(hdr.desc.status);
	
	if (status & HERMES_RXSTAT_ERR) {
		if ((status & HERMES_RXSTAT_ERR) == HERMES_RXSTAT_BADCRC) {
			stats->rx_crc_errors++;
			printk(KERN_WARNING "%s: Bad CRC on Rx. Frame dropped.\n",
			       dev->name);
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

	/* Handle decapsulation */
	switch (status & HERMES_RXSTAT_MSGTYPE) {
		/* These both indicate a SNAP within 802.2 LLC within
		   802.3 within 802.11 frame which we'll need to
		   de-encapsulate. IEEE and ISO OSI have a lot to
		   answer for.  */
	case HERMES_RXSTAT_1042:
	case HERMES_RXSTAT_TUNNEL:
		data_len = length - ENCAPS_OVERHEAD;
		data_off = sizeof(hdr);

		eh = (struct ethhdr *)skb_put(skb, ETH_HLEN);

		memcpy(eh, &hdr.p8023, sizeof(hdr.p8023));
		eh->h_proto = hdr.ethertype;

		break;

		/* Otherwise, we just throw the whole thing in, and hope
		   the protocol layer can deal with it as 802.3 */
	default:
		data_len = length;
		data_off = P8023_OFFSET;
		break;
	}

	p = skb_put(skb, data_len);
	if (hermes_bap_pread(hw, IRQ_BAP, p, RUP_EVEN(data_len),
			     rxfid, data_off) != 0) {
		printk(KERN_WARNING "%s: Error reading packet data\n",
		       dev->name);
		stats->rx_errors++;
		goto drop;
	}

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
	uint16_t allocfid;

	allocfid = hermes_read_regn(hw, ALLOCFID);
	DEBUG(3, "%s: Allocation complete FID=0x%04x\n", priv->ndev.name, allocfid);

	/* For some reason we don't seem to get transmit completed events properly */
	if (allocfid == priv->txfid)
		__dldwd_ev_tx(priv, hw);

/* 	hermes_write_regn(hw, ALLOCFID, 0); */
}

/*
 * struct net_device methods
 */

static int dldwd_init(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	hermes_id_t nickbuf;
	uint16_t reclen;
	int len;
	char *vendor_str;
	uint32_t firmver;

	TRACE_ENTER("dldwd");
	
	dldwd_lock(priv);

	err = hermes_reset(hw);
	if (err != 0) {
		printk(KERN_ERR "%s: failed to reset hardware\n", dev->name);
		goto out;
	}
	
	/* Get the firmware version */
	err = hermes_read_staidentity(hw, USER_BAP, &priv->firmware_info);
	if (err) {
		printk(KERN_WARNING "%s: Error %d reading firmware info. Wildly guessing capabilities...\n",
		       dev->name, err);
		memset(&priv->firmware_info, 0, sizeof(priv->firmware_info));
	}

	firmver = ((uint32_t)priv->firmware_info.major << 16) | priv->firmware_info.minor;
	DEBUG(2, "%s: firmver = 0x%X\n", dev->name, firmver);

	/* Determine capabilities from the firmware version */

	switch (priv->firmware_info.vendor) {
	case 0x1:
		/* Lucent Wavelan IEEE, Lucent Orinoco, Cabletron RoamAbout,
		 * ELSA, Melco, HP, IBM, Dell 1150 cards */
		vendor_str = "Lucent";
		/* Lucent MAC : 00:60:1D:* & 00:02:2D:* */

		priv->firmware_type = FIRMWARE_TYPE_LUCENT;
		priv->broken_reset = 0;
		priv->broken_allocate = 0;
		priv->has_port3 = 1;		/* Still works in 7.28 */
		priv->has_ibss = (firmver >= 0x60006);
		priv->has_ibss_any = (firmver >= 0x60010);
		priv->has_wep = (firmver >= 0x40020);
		priv->has_big_wep = 1; /* FIXME: this is wrong - how do we tell
					  Gold cards from the others? */
		priv->has_mwo = (firmver >= 0x60000);
		priv->has_pm = (firmver >= 0x40020);
		priv->has_retry = 0;
		priv->has_preamble = 0;
		/* Tested with Lucent firmware :
		 *	1.16 ; 4.08 ; 4.52 ; 6.04 ; 6.16 ; 7.28 => Jean II
		 * Tested CableTron firmware : 4.32 => Anton */
		break;
	case 0x2:
		vendor_str = "Generic Prism II";
		/* Some D-Link cards report vendor 0x02... */

		priv->firmware_type = FIRMWARE_TYPE_PRISM2;
		priv->broken_reset = 0;
		priv->broken_allocate = 0;
		priv->has_port3 = 1;
		priv->has_ibss = (firmver >= 0x00007); /* FIXME */
		priv->has_wep = (firmver >= 0x00007); /* FIXME */
		priv->has_big_wep = 0;
		priv->has_mwo = 0;
		priv->has_pm = (firmver >= 0x00007); /* FIXME */
		priv->has_retry = 0;
		priv->has_preamble = 0;

		/* Tim Hurley -> D-Link card, vendor 02, firmware 0.08 */

		/* Special case for Symbol cards */
		if(firmver == 0x10001) {
			/* Symbol , 3Com AirConnect, Intel, Ericsson WLAN */
			vendor_str = "Symbol";
			/* Intel MAC : 00:02:B3:* */
			/* 3Com MAC : 00:50:DA:* */

			/* FIXME : probably need to use SYMBOL_***ARY_VER
			 * to get proper firmware version */
			priv->firmware_type = FIRMWARE_TYPE_SYMBOL;
			priv->broken_reset = 0;
			priv->broken_allocate = 1;
			priv->has_port3 = 1;
			priv->has_ibss = 1; /* FIXME */
			priv->has_wep = 1; /* FIXME */
			priv->has_big_wep = 1;	/* RID_SYMBOL_KEY_LENGTH */
			priv->has_mwo = 0;
			priv->has_pm = 1; /* FIXME */
			priv->has_retry = 0;
			priv->has_preamble = 0; /* FIXME */
			/* Tested with Intel firmware : v15 => Jean II */
		}
		break;
	case 0x3:
		vendor_str = "Samsung";
		/* To check - Should cover Samsung & Compaq */

		priv->firmware_type = FIRMWARE_TYPE_PRISM2;
		priv->broken_reset = 0;
		priv->broken_allocate = 0;
		priv->has_port3 = 1;
		priv->has_ibss = 0; /* FIXME: available in later firmwares */
		priv->has_wep = (firmver >= 0x20000); /* FIXME */
		priv->has_big_wep = 0; /* FIXME */
		priv->has_mwo = 0;
		priv->has_pm = (firmver >= 0x20000); /* FIXME */
		priv->has_retry = 0;
		priv->has_preamble = 0;
		break;
	case 0x6:
		/* D-Link DWL 650, ... */
		vendor_str = "LinkSys/D-Link";
		/* D-Link MAC : 00:40:05:* */

		priv->firmware_type = FIRMWARE_TYPE_PRISM2;
		priv->broken_reset = 0;
		priv->broken_allocate = 0;
		priv->has_port3 = 1;
		priv->has_ibss = (firmver >= 0x00007); /* FIXME */
		priv->has_wep = (firmver >= 0x00007); /* FIXME */
		priv->has_big_wep = 0;
		priv->has_mwo = 0;
		priv->has_pm = (firmver >= 0x00007); /* FIXME */
		priv->has_retry = 0;
		priv->has_preamble = 0;
		/* Tested with D-Link firmware 0.07 => Jean II */
		/* Note : with 0.07, IBSS to a Lucent card seem flaky */
		break;
	default:
		vendor_str = "UNKNOWN";

		priv->firmware_type = 0;
		priv->broken_reset = 0;
		priv->broken_allocate = 0;
		priv->has_port3 = 0;
		priv->has_ibss = 0;
		priv->has_wep = 0;
		priv->has_big_wep = 0;
		priv->has_mwo = 0;
		priv->has_pm = 0;
		priv->has_retry = 0;
		priv->has_preamble = 0;
	}

	printk(KERN_INFO "%s: Firmware ID %02X vendor 0x%x (%s) version %d.%02d\n",
	       dev->name, priv->firmware_info.id, priv->firmware_info.vendor,
	       vendor_str, priv->firmware_info.major, priv->firmware_info.minor);
	
	if (priv->has_port3)
		printk(KERN_INFO "%s: Ad-hoc demo mode supported.\n", dev->name);
	if (priv->has_ibss)
		printk(KERN_INFO "%s: IEEE standard IBSS ad-hoc mode supported.\n",
		       dev->name);
	if (priv->has_wep) {
		printk(KERN_INFO "%s: WEP supported, ", dev->name);
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

	printk(KERN_INFO "%s: MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
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
	if ( nickbuf.len )
		len = MIN(IW_ESSID_MAX_SIZE, le16_to_cpu(nickbuf.len));
	else
		len = MIN(IW_ESSID_MAX_SIZE, 2 * reclen);
	memcpy(priv->nick, &nickbuf.val, len);
	priv->nick[len] = '\0';

	printk(KERN_INFO "%s: Station name \"%s\"\n", dev->name, priv->nick);

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

	/* Set initial bitrate control*/
	priv->tx_rate_ctrl = 3;

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

	/* Retry setup */
	if (priv->has_retry) {
		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_SHORT_RETRY_LIMIT, &priv->retry_short);
		if (err)
			goto out;

		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_LONG_RETRY_LIMIT, &priv->retry_long);
		if (err)
			goto out;

		err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_MAX_TX_LIFETIME, &priv->retry_time);
		if (err)
			goto out;
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

	printk(KERN_INFO "%s: ready\n", dev->name);

 out:
	dldwd_unlock(priv);

	TRACE_EXIT("dldwd");

	return err;
}

static int dldwd_open(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	dev_link_t *link = &priv->link;
	int err = 0;
	
	TRACE_ENTER(dev->name);

	link->open++;
	MOD_INC_USE_COUNT;
	netif_device_attach(dev);
	
	err = dldwd_reset(priv);
	if (err)
		dldwd_stop(dev);
	else
		netif_start_queue(dev);

	TRACE_EXIT(dev->name);

	return err;
}

static int dldwd_stop(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	dev_link_t *link = &priv->link;

	TRACE_ENTER(dev->name);

	netif_stop_queue(dev);

	dldwd_shutdown(priv);
	
	link->open--;

	if (link->state & DEV_STALE_CONFIG)
		mod_timer(&link->release, jiffies + HZ/20);
	
	MOD_DEC_USE_COUNT;

	TRACE_EXIT(dev->name);
	
	return 0;
}

static struct net_device_stats *dldwd_get_stats(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	
	return &priv->stats;
}

static struct iw_statistics *dldwd_get_wireless_stats(struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	hermes_t *hw = &priv->hw;
	struct iw_statistics *wstats = &priv->wstats;
	int err = 0;
	hermes_commsqual_t cq;

	dldwd_lock(priv);

	if (priv->port_type == 3) {
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
		err = hermes_read_commsqual(hw, USER_BAP, &cq);
		
		DEBUG(3, "%s: Global stats = %X-%X-%X\n", dev->name,
		      cq.qual, cq.signal, cq.noise);

		/* Why are we using MIN/MAX ? We don't really care
		 * if the value goes above max, because we export the
		 * raw dBm values anyway. The normalisation should be done
		 * in user space - Jean II */
		wstats->qual.qual = MAX(MIN(cq.qual, 0x8b-0x2f), 0);
		wstats->qual.level = MAX(MIN(cq.signal, 0x8a), 0x2f) - 0x95;
		wstats->qual.noise = MAX(MIN(cq.noise, 0x8a), 0x2f) - 0x95;
		wstats->qual.updated = 7;
	}

	dldwd_unlock(priv);

	if (err)
		return NULL;
		
	return wstats;
}

#ifdef WIRELESS_SPY
static inline void dldwd_spy_gather(struct net_device *dev,
				    u_char *mac,
				    hermes_commsqual_t *cq)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	int i;

	/* Gather wireless spy statistics: for each packet, compare the
	 * source address with out list, and if match, get the stats... */
	for (i = 0; i < priv->spy_number; i++)
		if (!memcmp(mac, priv->spy_address[i], ETH_ALEN)) {
			priv->spy_stat[i].qual = MAX(MIN(cq->qual, 0x8b-0x2f), 0);
			priv->spy_stat[i].level = MAX(MIN(cq->signal, 0x8a), 0x2f) - 0x95;
			priv->spy_stat[i].noise = MAX(MIN(cq->noise, 0x8a), 0x2f) - 0x95;
			priv->spy_stat[i].updated = 7;
		}
}
#endif /* WIRELESS_SPY */

static void dldwd_stat_gather(struct net_device *dev,
			      struct sk_buff *skb,
			      struct dldwd_frame_hdr *hdr)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	hermes_commsqual_t cq;

	/* Using spy support with lots of Rx packets, like in an
	 * infrastructure (AP), will really slow down everything, because
	 * the MAC address must be compared to each entry of the spy list.
	 * If the user really asks for it (set some address in the
	 * spy list), we do it, but he will pay the price.
	 * Note that to get here, you need both WIRELESS_SPY
	 * compiled in AND some addresses in the list !!!
	 */
#ifdef WIRELESS_EXT
	/* Note : gcc will optimise the whole section away if
	 * WIRELESS_SPY is not defined... - Jean II */
	if (
#ifdef WIRELESS_SPY
		(priv->spy_number > 0) ||
#endif
		0 )
	{
		u_char *stats = (u_char *) &(hdr->desc.q_info);
		/* This code may look strange. Everywhere we are using 16 bit
		 * ints except here. I've verified that these are are the
		 * correct values. Please check on PPC - Jean II */
		cq.signal = stats[1];	/* High order byte */
		cq.noise = stats[0];	/* Low order byte */
		cq.qual = stats[0] - stats[1];	/* Better than nothing */

		DEBUG(3, "%s: Packet stats = %X-%X-%X\n", dev->name,
		      cq.qual, cq.signal, cq.noise);

#ifdef WIRELESS_SPY
		dldwd_spy_gather(dev, skb->mac.raw + ETH_ALEN, &cq);  
#endif
	}
#endif /* WIRELESS_EXT */
}

struct p8022_hdr encaps_hdr = {
	0xaa, 0xaa, 0x03, {0x00, 0x00, 0xf8}
};

static int dldwd_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dldwd_priv_t *priv = (dldwd_priv_t *)dev->priv;
	struct net_device_stats *stats = &priv->stats;
	hermes_t *hw = &priv->hw;
	int err = 0;
	uint16_t txfid = priv->txfid;
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
	len = MAX(skb->len - ETH_HLEN, ETH_ZLEN);

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
		memcpy(&hdr.p8022, &encaps_hdr, sizeof(encaps_hdr));

		hdr.ethertype = eh->h_proto;
		err  = hermes_bap_pwrite(hw, USER_BAP, &hdr, sizeof(hdr),
					 txfid, 0);
		if (err) {
			printk(KERN_ERR
			       "%s: Error %d writing packet header to BAP\n",
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
		printk(KERN_ERR "%s: Error %d writing packet data to BAP\n",
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

static void dldwd_tx_timeout(struct net_device *dev)
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
	int ptype;
	struct iw_range range;
	int numrates;
	int i, k;

	TRACE_ENTER(dev->name);

	err = verify_area(VERIFY_WRITE, rrq->pointer, sizeof(range));
	if (err)
		return err;

	rrq->length = sizeof(range);

	dldwd_lock(priv);
	ptype = priv->port_type;
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

	if ((ptype == 3) && (priv->spy_number == 0)){
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
	uint16_t xlen = 0;
	int err = 0;
	char keybuf[MAX_KEY_SIZE];

	if (erq->pointer) {
		/* We actually have a key to set */
		
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
	uint16_t xlen = 0;
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

	memcpy(priv->desired_essid, essidbuf, IW_ESSID_MAX_SIZE+1);

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
	uint16_t val;
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
	uint16_t val;

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
	case FIRMWARE_TYPE_PRISM2: /* Prism II style rate */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style rate */
		switch(brate) {
		case 0:
			fixed = 0x0;
			upto = 0x15;
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
			upto = 0x15;
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
	uint16_t val;
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
	case FIRMWARE_TYPE_PRISM2: /* Prism II style rate */
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
	uint16_t enable, period, timeout, mcast;

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
static int dldwd_ioctl_setretry(struct net_device *dev, struct iw_param *rrq)
{
	dldwd_priv_t *priv = dev->priv;
	int err = 0;


	dldwd_lock(priv);

	if ((rrq->disabled) || (!priv->has_retry)){
		err = -EOPNOTSUPP;
		goto out;
	} else {
		if (rrq->flags & IW_RETRY_LIMIT) {
			if (rrq->flags & IW_RETRY_MAX)
				priv->retry_long = rrq->value;
			else if (rrq->flags & IW_RETRY_MIN)
				priv->retry_short = rrq->value;
			else {
				/* No modifier : set both */
				priv->retry_long = rrq->value;
				priv->retry_short = rrq->value;
			}
		}
		if (rrq->flags & IW_RETRY_LIFETIME) {
			priv->retry_time = rrq->value / 1000;
		}
		if ((rrq->flags & IW_RETRY_TYPE) == 0) {
			err = -EINVAL;
			goto out;
		}			
	}

 out:
	dldwd_unlock(priv);

	return err;
}

static int dldwd_ioctl_getretry(struct net_device *dev, struct iw_param *rrq)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	uint16_t short_limit, long_limit, lifetime;

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
		priv->prefer_port3 = 0;
			
		break;

	case 1: /* Try to do Lucent proprietary ad-hoc mode */
		if (! priv->has_port3) {
			err = -EINVAL;
			break;
		}
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

static int dldwd_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	dldwd_priv_t *priv = dev->priv;
	struct iwreq *wrq = (struct iwreq *)rq;
	int err = 0;
	int changed = 0;

	TRACE_ENTER(dev->name);

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
		err = dldwd_ioctl_setretry(dev, &wrq->u.retry);
		if (! err)
			changed = 1;
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
				  "get_preamble" }
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

	default:
		err = -EOPNOTSUPP;
	}
	
	if (! err && changed && netif_running(dev)) {
		err = dldwd_reset(priv);
		if (err)
			dldwd_stop(dev);
	}		

	TRACE_EXIT(dev->name);
		
	return err;
}

static int dldwd_change_mtu(struct net_device *dev, int new_mtu)
{
	TRACE_ENTER(dev->name);

	if ( (new_mtu < DLDWD_MIN_MTU) || (new_mtu > DLDWD_MAX_MTU) )
		return -EINVAL;

	dev->mtu = new_mtu;

	TRACE_EXIT(dev->name);

	return 0;
}

static void __dldwd_set_multicast_list(struct net_device *dev)
{
	dldwd_priv_t *priv = dev->priv;
	hermes_t *hw = &priv->hw;
	int err = 0;
	int promisc, allmulti, mc_count;

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
	uint16_t rid;
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
	RTCNFENTRY(PRISM2_TX_KEY, DISPLAY_WORDS),
	RTCNFENTRY(PRISM2_KEY0, DISPLAY_BYTES),
	RTCNFENTRY(PRISM2_KEY1, DISPLAY_BYTES),
	RTCNFENTRY(PRISM2_KEY2, DISPLAY_BYTES),
	RTCNFENTRY(PRISM2_KEY3, DISPLAY_BYTES),
	RTCNFENTRY(PRISM2_WEP_ON, DISPLAY_WORDS),
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
	uint16_t length;
	int err;

	buf = page;

	/* print out all the config RIDs */
	for (i = 0; i < NUM_RIDS; i++) {
		uint16_t rid = record_table[i].rid;
		int minlen = record_table[i].minlen;
		int maxlen = record_table[i].maxlen;
		int len;
		uint8_t *val8;
		uint16_t *val16;
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
		val16 = (uint16_t *)val8;

		buf += sprintf(buf, "%-15s (0x%04x): length=%d (%d bytes)\tvalue=", record_table[i].name,
			       rid, length, (length-1)*2);
		len = MIN( MAX(minlen, (length-1)*2), maxlen);

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
			len = MIN(len, le16_to_cpu(val16[0])+2);
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

static int
dldwd_proc_dev_init(dldwd_priv_t *dev)
{
	dev->dir_dev = NULL;
	/* create the directory for it to sit in */
	dev->dir_dev = create_proc_entry(dev->node.dev_name, S_IFDIR | S_IRUGO | S_IXUGO,
					 dir_base);
	if (dev->dir_dev == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/hermes/%s.\n",  dev->node.dev_name);
		goto fail;
	}

	dev->dir_regs = NULL;
	dev->dir_regs = create_proc_read_entry("regs", S_IFREG | S_IRUGO,
					       dev->dir_dev, dldwd_proc_get_hermes_regs, dev);
	if (dev->dir_regs == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/hermes/%s/regs.\n",  dev->node.dev_name);
		goto fail;
	}

	dev->dir_recs = NULL;
	dev->dir_recs = create_proc_read_entry("recs", S_IFREG | S_IRUGO,
					       dev->dir_dev, dldwd_proc_get_hermes_recs, dev);
	if (dev->dir_recs == NULL) {
		printk(KERN_ERR "Unable to initialise /proc/hermes/%s/recs.\n",  dev->node.dev_name);
		goto fail;
	}

	return 0;
 fail:
	dldwd_proc_dev_cleanup(dev);
	return -ENOMEM;
}

static void
dldwd_proc_dev_cleanup(dldwd_priv_t *priv)
{
	if (priv->dir_regs) {
		remove_proc_entry("regs", priv->dir_dev);
		priv->dir_regs = NULL;
	}		
	if (priv->dir_recs) {
		remove_proc_entry("recs", priv->dir_dev);
		priv->dir_recs = NULL;
	}		
	if (priv->dir_dev) {
		remove_proc_entry(priv->node.dev_name, dir_base);
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

/*====================================================================*/

/*
 * From here on in, it's all PCMCIA junk, taken almost directly from
 * dummy_cs.c
 */

/*
   The dev_info variable is the "key" that is used to match up this
   device driver with appropriate cards, through the card configuration
   database.
*/

static dev_info_t dev_info = "orinoco_cs";

/*
   A linked list of "instances" of the dummy device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one dev_link_t structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of dev_link_t pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

static dev_link_t *dev_list = NULL;
static int num_instances = 0;

/*====================================================================*/

static void cs_error(client_handle_t handle, int func, int ret)
{
	error_info_t err = { func, ret };
	CardServices(ReportError, handle, &err);
}

/*======================================================================
  dldwd_attach() creates an "instance" of the driver, allocating
  local data structures for one device.  The device is registered
  with Card Services.
  
  The dev_link structure is initialized, but we don't actually
  configure the card at this point -- we wait until we receive a
  card insertion event.
  ======================================================================*/

static dev_link_t *dldwd_attach(void)
{
	dldwd_priv_t *priv;
	dev_link_t *link;
	struct net_device *ndev;
	client_reg_t client_reg;
	int ret, i;

	TRACE_ENTER("dldwd");

	/* Allocate space for private device-specific data */
	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (! priv) {
		link = NULL;
		goto out;
	}

	memset(priv, 0, sizeof(*priv));
	priv->instance = num_instances++; /* FIXME: Racy? */
	spin_lock_init(&priv->lock);

	link = &priv->link;
	ndev = &priv->ndev;
	link->priv = ndev->priv = priv;

	/* Initialize the dev_link_t structure */
	link->release.function = &dldwd_release;
	link->release.data = (u_long) link;

	/* Interrupt setup */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;
	if (irq_list[0] == -1)
		link->irq.IRQInfo2 = irq_mask;
	else
		for (i = 0; i < 4; i++)
			link->irq.IRQInfo2 |= 1 << irq_list[i];
	link->irq.Handler = NULL;

	/*
	   General socket configuration defaults can go here.  In this
	   client, we assume very little, and rely on the CIS for almost
	   everything.  In most clients, many details (i.e., number, sizes,
	   and attributes of IO windows) are fixed by the nature of the
	   device, and can be hard-wired here.
	 */
	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY_AND_IO;

	/* Set up the net_device */
	ether_setup(ndev);
	ndev->init = dldwd_init;
	ndev->open = dldwd_open;
	ndev->stop = dldwd_stop;
	ndev->hard_start_xmit = dldwd_xmit;
	ndev->tx_timeout = dldwd_tx_timeout;
	ndev->watchdog_timeo = 4*HZ; /* 4 second timeout */

	ndev->get_stats = dldwd_get_stats;
	ndev->get_wireless_stats = dldwd_get_wireless_stats;

	ndev->do_ioctl = dldwd_ioctl;
	
	ndev->change_mtu = dldwd_change_mtu;
	ndev->set_multicast_list = dldwd_set_multicast_list;

	netif_stop_queue(ndev);

	/* Register with Card Services */
	link->next = dev_list;
	dev_list = link;
	client_reg.dev_info = &dev_info;
	client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
	client_reg.EventMask =
	    CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	    CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	    CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
	client_reg.event_handler = &dldwd_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;
	ret = CardServices(RegisterClient, &link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		dldwd_detach(link);
		link = NULL;
		goto out;
	}

 out:
	TRACE_EXIT("dldwd");
	return link;
}				/* dldwd_attach */

/*======================================================================
  This deletes a driver "instance".  The device is de-registered
  with Card Services.  If it has been released, all local data
  structures are freed.  Otherwise, the structures will be freed
  when the device is released.
  ======================================================================*/

static void dldwd_detach(dev_link_t * link)
{
	dev_link_t **linkp;
	dldwd_priv_t *priv = link->priv;

	TRACE_ENTER("dldwd");

	/* Locate device structure */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
		if (*linkp == link)
			break;
	if (*linkp == NULL)
		goto out;

	/*
	   If the device is currently configured and active, we won't
	   actually delete it yet.  Instead, it is marked so that when
	   the release() function is called, that will trigger a proper
	   detach().
	 */
	if (link->state & DEV_CONFIG) {
#ifdef PCMCIA_DEBUG
		printk(KERN_DEBUG "orinoco_cs: detach postponed, '%s' "
		       "still locked\n", link->dev->dev_name);
#endif
		link->state |= DEV_STALE_LINK;
		goto out;
	}

	/* Break the link with Card Services */
	if (link->handle)
		CardServices(DeregisterClient, link->handle);

	/* Unlink device structure, and free it */
	*linkp = link->next;
	DEBUG(0, "orinoco_cs: detach: link=%p link->dev=%p\n", link, link->dev);
	if (link->dev) {
		DEBUG(0, "orinoco_cs: About to unregister net device %p\n",
		      &priv->ndev);
		unregister_netdev(&priv->ndev);
	}
	kfree(priv);

	num_instances--; /* FIXME: Racy? */

 out:
	TRACE_EXIT("dldwd");
}				/* dldwd_detach */

/*
 * Do a soft reset of the Pcmcia card using the Configuration Option Register
 * Can't do any harm, and actually may do some good on some cards...
 */
static int dldwd_cor_reset(dev_link_t *link)
{
	conf_reg_t reg;
	u_long default_cor; 

	/* Save original COR value */
	reg.Function = 0;
	reg.Action = CS_READ;
	reg.Offset = CISREG_COR;
	reg.Value = 0;
	CardServices(AccessConfigurationRegister, link->handle, &reg);
	default_cor = reg.Value;

	DEBUG(2, "dldwd : dldwd_cor_reset() : cor=0x%lX\n", default_cor);

	/* Soft-Reset card */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = (default_cor | COR_SOFT_RESET);
	CardServices(AccessConfigurationRegister, link->handle, &reg);

	/* Wait until the card has acknowledged our reset */
	mdelay(1);

	/* Restore original COR configuration index */
	reg.Value = (default_cor & COR_CONFIG_MASK);
	CardServices(AccessConfigurationRegister, link->handle, &reg);

	/* Wait until the card has finished restarting */
	mdelay(1);

	return(0);
}

/*======================================================================
  dldwd_config() is scheduled to run after a CARD_INSERTION event
  is received, to configure the PCMCIA socket, and to make the
  device available to the system.
  ======================================================================*/

#define CS_CHECK(fn, args...) \
while ((last_ret=CardServices(last_fn=(fn),args))!=0) goto cs_failed

#define CFG_CHECK(fn, args...) \
if (CardServices(fn, args) != 0) goto next_entry

static void dldwd_config(dev_link_t * link)
{
	client_handle_t handle = link->handle;
	dldwd_priv_t *priv = link->priv;
	hermes_t *hw = &priv->hw;
	struct net_device *ndev = &priv->ndev;
	tuple_t tuple;
	cisparse_t parse;
	int last_fn, last_ret;
	u_char buf[64];
	config_info_t conf;
	cistpl_cftable_entry_t dflt = { 0 };
	cisinfo_t info;

	TRACE_ENTER("dldwd");

	CS_CHECK(ValidateCIS, handle, &info);

	/*
	   This reads the card's CONFIG tuple to find its configuration
	   registers.
	 */
	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	CS_CHECK(GetTupleData, handle, &tuple);
	CS_CHECK(ParseTuple, handle, &tuple, &parse);
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;

	/* Look up the current Vcc */
	CS_CHECK(GetConfigurationInfo, handle, &conf);
	link->conf.Vcc = conf.Vcc;

	DEBUG(0, "dldwd_config: ConfigBase = 0x%x link->conf.Vcc = %d\n", 
	      link->conf.ConfigBase, link->conf.Vcc);

	/*
	   In this loop, we scan the CIS for configuration table entries,
	   each of which describes a valid card configuration, including
	   voltage, IO window, memory window, and interrupt settings.

	   We make no assumptions about the card to be configured: we use
	   just the information available in the CIS.  In an ideal world,
	   this would work for any PCMCIA card, but it requires a complete
	   and accurate CIS.  In practice, a driver usually "knows" most of
	   these things without consulting the CIS, and most client drivers
	   will only use the CIS to fill in implementation-defined details.
	 */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	while (1) {
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
		CFG_CHECK(GetTupleData, handle, &tuple);
		CFG_CHECK(ParseTuple, handle, &tuple, &parse);

		DEBUG(0, "dldwd_config: index = 0x%x, flags = 0x%x\n",
		      cfg->index, cfg->flags);

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Does this card need audio output? */
		if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
			link->conf.Attributes |= CONF_ENABLE_SPKR;
			link->conf.Status = CCSR_AUDIO_ENA;
		}

		/* Use power settings for Vcc and Vpp if present */
		/*  Note that the CIS values need to be rescaled */
		if (cfg->vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc !=
			    cfg->vcc.param[CISTPL_POWER_VNOM] / 10000) {
				DEBUG(2, "dldwd_config: Vcc mismatch (conf.Vcc = %d, CIS = %d)\n",  conf.Vcc, cfg->vcc.param[CISTPL_POWER_VNOM] / 10000);
				goto next_entry;
			}
		} else if (dflt.vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc !=
			    dflt.vcc.param[CISTPL_POWER_VNOM] / 10000) {
				DEBUG(2, "dldwd_config: Vcc mismatch (conf.Vcc = %d, CIS = %d)\n",  conf.Vcc, dflt.vcc.param[CISTPL_POWER_VNOM] / 10000);
				goto next_entry;
			}
		}

		if (cfg->vpp1.present & (1 << CISTPL_POWER_VNOM))
			link->conf.Vpp1 = link->conf.Vpp2 =
			    cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
		else if (dflt.vpp1.present & (1 << CISTPL_POWER_VNOM))
			link->conf.Vpp1 = link->conf.Vpp2 =
			    dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;
		
		DEBUG(0, "dldwd_config: We seem to have configured Vcc and Vpp\n");

		/* Do we need to allocate an interrupt? */
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
			link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io =
			    (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 =
				    IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 =
				    IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines =
			    io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 =
				    link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}

			/* This reserves IO space but doesn't actually enable it */
			CFG_CHECK(RequestIO, link->handle, &link->io);
		}


		/* If we got this far, we're cool! */

		break;
		
	next_entry:
		if (link->io.NumPorts1)
			CardServices(ReleaseIO, link->handle, &link->io);
		CS_CHECK(GetNextTuple, handle, &tuple);
	}

	/*
	   Allocate an interrupt line.  Note that this does not assign a
	   handler to the interrupt, unless the 'Handler' member of the
	   irq structure is initialized.
	 */
	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		int i;

		link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
		link->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;
		if (irq_list[0] == -1)
			link->irq.IRQInfo2 = irq_mask;
		else
			for (i=0; i<4; i++)
				link->irq.IRQInfo2 |= 1 << irq_list[i];
		
  		link->irq.Handler = dldwd_interrupt; 
  		link->irq.Instance = priv; 
		
		CS_CHECK(RequestIRQ, link->handle, &link->irq);
	}

	/* We initialize the hermes structure before completing PCMCIA
	   configuration just in case the interrupt handler gets
	   called. */
	hermes_struct_init(hw, link->io.BasePort1);

	/*
	   This actually configures the PCMCIA socket -- setting up
	   the I/O windows and the interrupt mapping, and putting the
	   card and host interface into "Memory and IO" mode.
	 */
	CS_CHECK(RequestConfiguration, link->handle, &link->conf);

	ndev->base_addr = link->io.BasePort1;
	ndev->irq = link->irq.AssignedIRQ;

	/* Do a Pcmcia soft reset of the card (optional) */
	if(reset_cor)
		dldwd_cor_reset(link);

	/* register_netdev will give us an ethX name */
	ndev->name[0] = '\0';
	/* Tell the stack we exist */
	if (register_netdev(ndev) != 0) {
		printk(KERN_ERR "orinoco_cs: register_netdev() failed\n");
		goto failed;
	}
	strcpy(priv->node.dev_name, ndev->name);
	
	/* Finally, report what we've done */
	printk(KERN_INFO "%s: index 0x%02x: Vcc %d.%d",
	       priv->node.dev_name, link->conf.ConfigIndex,
	       link->conf.Vcc / 10, link->conf.Vcc % 10);
	if (link->conf.Vpp1)
		printk(", Vpp %d.%d", link->conf.Vpp1 / 10,
		       link->conf.Vpp1 % 10);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %d", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1 + link->io.NumPorts1 - 1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2 + link->io.NumPorts2 - 1);
	printk("\n");
	
	/* And give us the proc nodes for debugging */
	if (dldwd_proc_dev_init(priv) != 0) {
		printk(KERN_ERR "orinoco_cs: Failed to create /proc node for %s\n",
		       priv->node.dev_name);
		goto failed;
	}
	
	/*
	   At this point, the dev_node_t structure(s) need to be
	   initialized and arranged in a linked list at link->dev.
	 */
	priv->node.major = priv->node.minor = 0;
	link->dev = &priv->node;
	link->state &= ~DEV_CONFIG_PENDING;

	TRACE_EXIT("dldwd");

	return;

 cs_failed:
	cs_error(link->handle, last_fn, last_ret);
 failed:
	dldwd_release((u_long) link);

	TRACE_EXIT("dldwd");
}				/* dldwd_config */

/*======================================================================
  After a card is removed, dldwd_release() will unregister the
  device, and release the PCMCIA configuration.  If the device is
  still open, this will be postponed until it is closed.
  ======================================================================*/

static void dldwd_release(u_long arg)
{
	dev_link_t *link = (dev_link_t *) arg;
	dldwd_priv_t *priv = link->priv;

	TRACE_ENTER(link->dev->dev_name);

	/*
	   If the device is currently in use, we won't release until it
	   is actually closed, because until then, we can't be sure that
	   no one will try to access the device or its data structures.
	 */
	if (link->open) {
		DEBUG(0, "orinoco_cs: release postponed, '%s' still open\n",
		      link->dev->dev_name);
		link->state |= DEV_STALE_CONFIG;
		return;
	}

	/*
	   In a normal driver, additional code may be needed to release
	   other kernel data structures associated with this device. 
	 */

	dldwd_proc_dev_cleanup(priv);

	/* Don't bother checking to see if these succeed or not */
	CardServices(ReleaseConfiguration, link->handle);
	if (link->io.NumPorts1)
		CardServices(ReleaseIO, link->handle, &link->io);
	if (link->irq.AssignedIRQ)
		CardServices(ReleaseIRQ, link->handle, &link->irq);
	link->state &= ~DEV_CONFIG;

	TRACE_EXIT(link->dev->dev_name);
}				/* dldwd_release */

/*======================================================================
  The card status event handler.  Mostly, this schedules other
  stuff to run after an event is received.

  When a CARD_REMOVAL event is received, we immediately set a
  private flag to block future accesses to this device.  All the
  functions that actually access the device should check this flag
  to make sure the card is still present.
  ======================================================================*/

static int dldwd_event(event_t event, int priority,
		       event_callback_args_t * args)
{
	dev_link_t *link = args->client_data;
	dldwd_priv_t *priv = (dldwd_priv_t *)link->priv;
	struct net_device *dev = &priv->ndev;

	TRACE_ENTER("dldwd");

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		dldwd_shutdown(priv);
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			netif_stop_queue(dev);
			netif_device_detach(dev);
			mod_timer(&link->release, jiffies + HZ / 20);
		}
		break;
	case CS_EVENT_CARD_INSERTION:
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		dldwd_config(link);
		break;
	case CS_EVENT_PM_SUSPEND:

		link->state |= DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		dldwd_shutdown(priv);
		/* Mark the device as stopped, to block IO until later */

		if (link->state & DEV_CONFIG) {
			if (link->open) {
				netif_stop_queue(dev);
				netif_device_detach(dev);
			}
			CardServices(ReleaseConfiguration, link->handle);
		}
		break;
	case CS_EVENT_PM_RESUME:
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		if (link->state & DEV_CONFIG) {
			CardServices(RequestConfiguration, link->handle,
				     &link->conf);

			if (link->open) {
				if (dldwd_reset(priv) == 0) {
					netif_device_attach(dev);
					netif_start_queue(dev);
				} else {
					printk(KERN_ERR "%s: Error resetting device on PCMCIA event\n",
					       dev->name);
					dldwd_stop(dev);
				}
			}
		}
		/*
		   In a normal driver, additional code may go here to restore
		   the device state and restart IO. 
		 */
		break;
	}

	TRACE_EXIT("dldwd");

	return 0;
}				/* dldwd_event */

static int __init init_dldwd_cs(void)
{
	servinfo_t serv;
	int err;
	
	TRACE_ENTER("dldwd");

	printk(KERN_INFO "dldwd: David's Less Dodgy WaveLAN/IEEE Driver\n");

	DEBUG(0, "%s\n", version);
	CardServices(GetCardServicesInfo, &serv);
	if (serv.Revision != CS_RELEASE_CODE) {
		printk(KERN_NOTICE "orinoco_cs: Card Services release "
		       "does not match!\n");
		return -1;
	}
	register_pccard_driver(&dev_info, &dldwd_attach, &dldwd_detach);

	
	err = dldwd_proc_init();

	TRACE_EXIT("dldwd");
	return err;
}

static void __exit exit_dldwd_cs(void)
{
	TRACE_ENTER("dldwd");

	dldwd_proc_cleanup();

	unregister_pccard_driver(&dev_info);

	if (dev_list)
		DEBUG(0, "orinoco_cs: Removing leftover devices.\n");
	while (dev_list != NULL) {
		del_timer(&dev_list->release);
		if (dev_list->state & DEV_CONFIG)
			dldwd_release((u_long) dev_list);
		dldwd_detach(dev_list);
	}

	TRACE_EXIT("dldwd");
}

module_init(init_dldwd_cs);
module_exit(exit_dldwd_cs);
