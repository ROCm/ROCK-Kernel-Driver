/*******************************************************************************

  
  Copyright(c) 1999 - 2004 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
*******************************************************************************/

/* Macros to make drivers compatible with 2.4 Linux kernels
 *
 * In order to make a single network driver work with all 2.4 kernels
 * these compatibility macros can be used.
 * They are backwards compatible implementations of the latest APIs.
 * The idea is that these macros will let you use the newest driver with old
 * kernels, but can be removed when working with the latest and greatest.
 */

#ifndef __E100_KCOMPAT_H__
#define __E100_KCOMPAT_H__

#include <linux/version.h>
#include <linux/types.h>

/******************************************************************
 *#################################################################
 *#
 *# General definitions, not related to a specific kernel version.
 *#
 *#################################################################
 ******************************************************************/
#ifndef __init
#define __init
#endif

#ifndef __devinit
#define __devinit
#endif

#ifndef __exit
#define __exit
#endif

#ifndef __devexit
#define __devexit
#endif

#ifndef __devinitdata
#define __devinitdata
#endif

#ifndef __devexit_p
#define __devexit_p(x) x
#endif

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(license)
#endif

#ifndef SET_NETDEV_DEV
#define SET_NETDEV_DEV(net, pdev)
#endif

#ifndef IRQ_HANDLED
#define irqreturn_t void
#define IRQ_HANDLED
#define IRQ_NONE
#endif

#ifndef HAVE_FREE_NETDEV
#define free_netdev(x)	kfree(x)
#endif

#ifndef MOD_INC_USE_COUNT
#define MOD_INC_USE_COUNT do {} while (0)
#endif

#ifndef MOD_DEC_USE_COUNT
#define MOD_DEC_USE_COUNT do {} while (0)
#endif

#ifdef HAVE_POLL_CONTROLLER
#define CONFIG_NET_POLL_CONTROLLER
#endif

#ifndef min_t
#define min_t(type,x,y) \
        ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#endif

#ifndef max_t
#define max_t(type,x,y) \
        ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })
#endif

#ifndef cpu_relax
#define cpu_relax() do {} while (0)
#endif

#ifndef ETHTOOL_GWOL
/* Wake-On-Lan options. */
#define WAKE_PHY                (1 << 0)
#define WAKE_UCAST              (1 << 1)
#define WAKE_ARP                (1 << 4)
#define WAKE_MAGIC              (1 << 5)
#endif

/* Installations with ethtool version < 1.6 */
#ifndef ETHTOOL_GLINK
#define ETHTOOL_GLINK		0x0000000a /* Get link status */
#endif
#ifndef ETH_TEST_FL_OFFLINE
#define ETH_TEST_FL_OFFLINE	(1 << 0)
#endif
#ifndef ETH_TEST_FL_FAILED
#define ETH_TEST_FL_FAILED	(1 << 1)
#endif
#ifndef ETHTOOL_TEST
#define ETHTOOL_TEST		0x0000001a /* execute NIC self-test, priv. */
#endif
#undef ethtool_test
#define ethtool_test _kc_ethtool_test
/* for requesting NIC test and getting results*/
struct _kc_ethtool_test {
	u32	cmd;		/* ETHTOOL_TEST */
	u32	flags;		/* ETH_TEST_FL_xxx */
	u32	reserved;
	u32	len;		/* result length, in number of u64 elements */
	u64	data[0];
};
#ifndef ETH_GSTRING_LEN
#define ETH_GSTRING_LEN         32
#endif
#ifndef ETHTOOL_GSTRINGS
#define ETHTOOL_GSTRINGS	0x0000001b /* get specified string set */
#endif
#undef ethtool_gstrings
#define ethtool_gstrings _kc_ethtool_gstrings
/* for passing string sets for data tagging */
struct _kc_ethtool_gstrings {
	u32	cmd;		/* ETHTOOL_GSTRINGS */
	u32	string_set;	/* string set id e.c. ETH_SS_TEST, etc*/
	u32	len;		/* number of strings in the string set */
	u8	data[0];
};
#ifndef ETH_SS_TEST
#define ETH_SS_TEST		0
#endif
#ifndef ETH_SS_STATS
#define ETH_SS_STATS		1
#endif
#ifndef ETHTOOL_GSTATS
#define ETHTOOL_GSTATS		0x0000001d /* get NIC-specific statistics */
#endif
#undef ethtool_stats
#define ethtool_stats _kc_ethtool_stats
/* for dumping NIC-specific statistics */
struct _kc_ethtool_stats {
	u32	cmd;		/* ETHTOOL_GSTATS */
	u32	n_stats;	/* number of u64's being returned */
	u64	data[0];
};
#ifndef ETHTOOL_BUSINFO_LEN
#define ETHTOOL_BUSINFO_LEN	32
#endif
#undef ethtool_drvinfo
#define ethtool_drvinfo k_ethtool_drvinfo
struct k_ethtool_drvinfo {
	u32	cmd;
	char	driver[32];	/* driver short name, "tulip", "eepro100" */
	char	version[32];	/* driver version string */
	char	fw_version[32];	/* firmware version string, if applicable */
	char	bus_info[ETHTOOL_BUSINFO_LEN];	/* Bus info for this IF. */
				/* For PCI devices, use pci_dev->slot_name. */
	char	reserved1[32];
	char	reserved2[16];
	u32	n_stats;	/* number of u64's from ETHTOOL_GSTATS */
	u32	testinfo_len;
	u32	eedump_len;	/* Size of data from ETHTOOL_GEEPROM (bytes) */
	u32	regdump_len;	/* Size of data from ETHTOOL_GREGS (bytes) */
};
#ifndef ETHTOOL_GEEPROM
#define ETHTOOL_GEEPROM 0xb
#define ETHTOOL_SEEPROM 0xc
#undef ETHTOOL_GREGS
struct ethtool_eeprom {
	u32	cmd;
	u32	magic;
	u32	offset;
	u32	len;
	u8	data[0];
};
#endif /* ETHTOOL_GEEPROM */

#ifndef ETHTOOL_PHYS_ID
#define ETHTOOL_PHYS_ID 0x1c
#undef ethtool_value
#define ethtool_value k_ethtool_value
struct k_ethtool_value {
	u32     cmd;
	u32     data;
};
#endif /* ETHTOOL_PHYS_ID */ 

/******************************************************************
 *#################################################################
 *#
 *# Kernels before 2.4.3
 *#
 *#################################################################
 ******************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,3)
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>

#ifndef pci_request_regions
#define pci_request_regions e100_pci_request_regions
extern int e100_pci_request_regions(struct pci_dev *pdev, char *res_name);
#endif

#ifndef pci_release_regions
#define pci_release_regions e100_pci_release_regions
extern void e100_pci_release_regions(struct pci_dev *pdev);
#endif

#ifndef is_valid_ether_addr
#define is_valid_ether_addr _kc_is_valid_ether_addr
extern int _kc_is_valid_ether_addr(u8 *addr);
#endif 

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,4,3) */

/******************************************************************
 *#################################################################
 *#
 *# Kernels before 2.4.4
 *#
 *#################################################################
 ******************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,4)

#define pci_disable_device(dev) do{} while(0)

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,4,4) */

/******************************************************************
 *#################################################################
 *#
 *# Kernels before 2.4.5
 *#
 *#################################################################
 ******************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,5)

#define skb_linearize(skb, gfp_mask) ({     \
    struct sk_buff *tmp_skb;                \
    tmp_skb = skb;                          \
    skb = skb_copy(tmp_skb, gfp_mask);      \
    dev_kfree_skb_any(tmp_skb); })

/* MII constants */

/* MDI register set*/
#ifndef MII_BMCR
#define MII_BMCR		0x00	/* MDI control register */
#endif
#ifndef MII_BMSR
#define MII_BMSR		0x01	/* MDI Status regiser */
#endif
#ifndef MII_PHYSID1
#define MII_PHYSID1		0x02	/* Phy indentification reg (word 1) */
#endif
#ifndef MII_PHYSID2
#define MII_PHYSID2		0x03	/* Phy indentification reg (word 2) */
#endif
#ifndef MII_ADVERTISE
#define MII_ADVERTISE		0x04	/* Auto-negotiation advertisement */
#endif
#ifndef MII_LPA
#define MII_LPA			0x05	/* Auto-negotiation link partner ability */
#endif
#ifndef MII_EXPANSION
#define MII_EXPANSION		0x06	/* Auto-negotiation expansion */
#endif
#ifndef MII_NCONFIG
#define MII_NCONFIG		0x1c	/* Network interface config   (MDI/MDIX) */
#endif

/* MDI Control register bit definitions*/
#ifndef BMCR_RESV
#define BMCR_RESV		0x007f	/* Unused...                   */
#endif
#ifndef BMCR_CTST
#define BMCR_CTST	        0x0080	/* Collision test              */
#endif
#ifndef BMCR_FULLDPLX
#define BMCR_FULLDPLX		0x0100	/* Full duplex                 */
#endif
#ifndef BMCR_ANRESTART
#define BMCR_ANRESTART		0x0200	/* Auto negotiation restart    */
#endif
#ifndef BMCR_ISOLATE
#define BMCR_ISOLATE		0x0400	/* Disconnect DP83840 from MII */
#endif
#ifndef BMCR_PDOWN
#define BMCR_PDOWN		0x0800	/* Powerdown the DP83840       */
#endif
#ifndef BMCR_ANENABLE
#define BMCR_ANENABLE		0x1000	/* Enable auto negotiation     */
#endif
#ifndef BMCR_SPEED100
#define BMCR_SPEED100		0x2000	/* Select 100Mbps              */
#endif
#ifndef BMCR_LOOPBACK
#define BMCR_LOOPBACK		0x4000	/* TXD loopback bits           */
#endif
#ifndef BMCR_RESET
#define BMCR_RESET		0x8000	/* Reset the DP83840           */
#endif

/* MDI Status register bit definitions*/
#ifndef BMSR_ERCAP
#define BMSR_ERCAP		0x0001	/* Ext-reg capability          */
#endif
#ifndef BMSR_JCD
#define BMSR_JCD		0x0002	/* Jabber detected             */
#endif
#ifndef BMSR_LSTATUS
#define BMSR_LSTATUS		0x0004	/* Link status                 */
#endif
#ifndef BMSR_ANEGCAPABLE
#define BMSR_ANEGCAPABLE	0x0008	/* Able to do auto-negotiation */
#endif
#ifndef BMSR_RFAULT
#define BMSR_RFAULT		0x0010	/* Remote fault detected       */
#endif
#ifndef BMSR_ANEGCOMPLETE
#define BMSR_ANEGCOMPLETE	0x0020	/* Auto-negotiation complete   */
#endif
#ifndef BMSR_RESV
#define BMSR_RESV		0x07c0	/* Unused...                   */
#endif
#ifndef BMSR_10HALF
#define BMSR_10HALF		0x0800	/* Can do 10mbps, half-duplex  */
#endif
#ifndef BMSR_10FULL
#define BMSR_10FULL		0x1000	/* Can do 10mbps, full-duplex  */
#endif
#ifndef BMSR_100HALF
#define BMSR_100HALF		0x2000	/* Can do 100mbps, half-duplex */
#endif
#ifndef BMSR_100FULL
#define BMSR_100FULL		0x4000	/* Can do 100mbps, full-duplex */
#endif
#ifndef BMSR_100BASE4
#define BMSR_100BASE4		0x8000	/* Can do 100mbps, 4k packets  */
#endif

/* Auto-Negotiation advertisement register bit definitions*/
#ifndef ADVERTISE_10HALF
#define ADVERTISE_10HALF	0x0020	/* Try for 10mbps half-duplex  */
#endif
#ifndef ADVERTISE_10FULL
#define ADVERTISE_10FULL	0x0040	/* Try for 10mbps full-duplex  */
#endif
#ifndef ADVERTISE_100HALF
#define ADVERTISE_100HALF	0x0080	/* Try for 100mbps half-duplex */
#endif
#ifndef ADVERTISE_100FULL
#define ADVERTISE_100FULL	0x0100	/* Try for 100mbps full-duplex */
#endif
#ifndef ADVERTISE_100BASE4
#define ADVERTISE_100BASE4	0x0200	/* Try for 100mbps 4k packets  */
#endif

/* Auto-Negotiation expansion register bit definitions*/
#ifndef EXPANSION_NWAY 
#define EXPANSION_NWAY		0x0001	/* Can do N-way auto-nego      */
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,4,5) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,7)

#ifdef SIOCGMIIPHY
#undef SIOCGMIIPHY
#endif
#define SIOCGMIIPHY     SIOCDEVPRIVATE

#ifdef SIOCGMIIREG
#undef SIOCGMIIREG
#endif
#define SIOCGMIIREG     (SIOCDEVPRIVATE+1)

#ifdef SIOCSMIIREG
#undef SIOCSMIIREG
#endif
#define SIOCSMIIREG     (SIOCDEVPRIVATE+2)

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,4,7) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,6)

#include <linux/types.h>
#include <linux/pci.h>

/* Power Management */
#define PMCSR		0xe0
#define PM_ENABLE_BIT	0x0100
#define PM_CLEAR_BIT	0x8000
#define PM_STATE_MASK	0xFFFC
#define PM_STATE_D1	0x0001

static inline int
pci_enable_wake(struct pci_dev *dev, u32 state, int enable)
{
	u16 p_state;

	pci_read_config_word(dev, PMCSR, &p_state);
	pci_write_config_word(dev, PMCSR, p_state | PM_CLEAR_BIT);

	if (enable == 0) {
		p_state &= ~PM_ENABLE_BIT;
	} else {
		p_state |= PM_ENABLE_BIT;
	}
	p_state &= PM_STATE_MASK;
	p_state |= state;

	pci_write_config_word(dev, PMCSR, p_state);

	return 0;
}

struct mii_ioctl_data {
	u16             phy_id;
	u16             reg_num;
	u16             val_in;
	u16             val_out;
};

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,4,6) */

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,6) )
#ifndef pci_set_power_state
#define pci_set_power_state _kc_pci_set_power_state
extern int _kc_pci_set_power_state(struct pci_dev *dev, int state);
#endif
#ifndef pci_save_state
#define pci_save_state _kc_pci_save_state
extern int _kc_pci_save_state(struct pci_dev *dev, u32 *buffer);
#endif
#ifndef pci_restore_state
#define pci_restore_state _kc_pci_restore_state
extern int _kc_pci_restore_state(struct pci_dev *pdev, u32 *buffer);
#endif
/* PCI PM entry point syntax changed, so don't support suspend/resume */
#undef CONFIG_PM
#endif

#ifndef pci_for_each_dev
#define pci_for_each_dev(dev) for(dev = pci_devices; dev; dev = dev->next)
#endif

/* 2.4.20 => 2.4.18 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,20) )

#ifndef ETHTOOL_GRINGPARAM
#define ETHTOOL_GRINGPARAM	0x00000010 /* Get ring parameters */
#endif
#ifndef ETHTOOL_SRINGPARAM
#define ETHTOOL_SRINGPARAM	0x00000011 /* Set ring parameters, priv. */
#endif
#ifndef ETHTOOL_GPAUSEPARAM
#define ETHTOOL_GPAUSEPARAM	0x00000012 /* Get pause parameters */
#endif
#ifndef ETHTOOL_SPAUSEPARAM
#define ETHTOOL_SPAUSEPARAM	0x00000013 /* Set pause parameters, priv. */
#endif
#ifndef ETHTOOL_GRXCSUM
#define ETHTOOL_GRXCSUM		0x00000014 /* Get RX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_SRXCSUM
#define ETHTOOL_SRXCSUM		0x00000015 /* Set RX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_GTXCSUM
#define ETHTOOL_GTXCSUM		0x00000016 /* Get TX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_STXCSUM
#define ETHTOOL_STXCSUM		0x00000017 /* Set TX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_GSG
#define ETHTOOL_GSG		0x00000018 /* Get scatter-gather enable
					    * (ethtool_value) */
#endif
#ifndef ETHTOOL_SSG
#define ETHTOOL_SSG		0x00000019 /* Set scatter-gather enable
					    * (ethtool_value), priv. */
#endif

#undef ethtool_ringparam
#define ethtool_ringparam _kc_ethtool_ringparam
struct _kc_ethtool_ringparam {
	u32	cmd;	/* ETHTOOL_{G,S}RINGPARAM */

	/* Read only attributes.  These indicate the maximum number
	 * of pending RX/TX ring entries the driver will allow the
	 * user to set.
	 */
	u32	rx_max_pending;
	u32	rx_mini_max_pending;
	u32	rx_jumbo_max_pending;
	u32	tx_max_pending;

	/* Values changeable by the user.  The valid values are
	 * in the range 1 to the "*_max_pending" counterpart above.
	 */
	u32	rx_pending;
	u32	rx_mini_pending;
	u32	rx_jumbo_pending;
	u32	tx_pending;
};

#undef ethtool_pauseparam
#define ethtool_pauseparam _kc_ethtool_pauseparam
/* for configuring link flow control parameters */
struct _kc_ethtool_pauseparam {
	u32	cmd;	/* ETHTOOL_{G,S}PAUSEPARAM */

	/* If the link is being auto-negotiated (via ethtool_cmd.autoneg
	 * being true) the user may set 'autonet' here non-zero to have the
	 * pause parameters be auto-negotiated too.  In such a case, the
	 * {rx,tx}_pause values below determine what capabilities are
	 * advertised.
	 *
	 * If 'autoneg' is zero or the link is not being auto-negotiated,
	 * then {rx,tx}_pause force the driver to use/not-use pause
	 * flow control.
	 */
	u32	autoneg;
	u32	rx_pause;
	u32	tx_pause;
};

#endif /* 2.4.20 => 2.4.18 */

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,22) )
#define pci_name(x)	((x)->slot_name)
#endif

#endif /* __E100_KCOMPAT_H__ */
