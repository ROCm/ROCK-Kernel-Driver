/*
 * drivers/net/gianfar_ethtool.c
 *
 * Gianfar Ethernet Driver
 * Ethtool support for Gianfar Enet
 * Based on e1000 ethtool support
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This software may be used and distributed according to 
 * the terms of the GNU Public License, Version 2, incorporated herein 
 * by reference.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/crc32.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <linux/ethtool.h>

#include "gianfar.h"

#define is_power_of_2(x)        ((x) != 0 && (((x) & ((x) - 1)) == 0))

extern int startup_gfar(struct net_device *dev);
extern void stop_gfar(struct net_device *dev);
extern void gfar_receive(int irq, void *dev_id, struct pt_regs *regs);

void gfar_fill_stats(struct net_device *dev, struct ethtool_stats *dummy,
		     u64 * buf);
void gfar_gstrings(struct net_device *dev, u32 stringset, u8 * buf);
int gfar_gcoalesce(struct net_device *dev, struct ethtool_coalesce *cvals);
int gfar_scoalesce(struct net_device *dev, struct ethtool_coalesce *cvals);
void gfar_gringparam(struct net_device *dev, struct ethtool_ringparam *rvals);
int gfar_sringparam(struct net_device *dev, struct ethtool_ringparam *rvals);
void gfar_gdrvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo);

static char stat_gstrings[][ETH_GSTRING_LEN] = {
	"RX Dropped by Kernel",
	"RX Large Frame Errors",
	"RX Short Frame Errors",
	"RX Non-Octet Errors",
	"RX CRC Errors",
	"RX Overrun Errors",
	"RX Busy Errors",
	"RX Babbling Errors",
	"RX Truncated Frames",
	"Ethernet Bus Error",
	"TX Babbling Errors",
	"TX Underrun Errors",
	"RX SKB Missing Errors",
	"TX Timeout Errors",
	"tx&rx 64B frames",
	"tx&rx 65-127B frames",
	"tx&rx 128-255B frames",
	"tx&rx 256-511B frames",
	"tx&rx 512-1023B frames",
	"tx&rx 1024-1518B frames",
	"tx&rx 1519-1522B Good VLAN",
	"RX bytes",
	"RX Packets",
	"RX FCS Errors",
	"Receive Multicast Packet",
	"Receive Broadcast Packet",
	"RX Control Frame Packets",
	"RX Pause Frame Packets",
	"RX Unknown OP Code",
	"RX Alignment Error",
	"RX Frame Length Error",
	"RX Code Error",
	"RX Carrier Sense Error",
	"RX Undersize Packets",
	"RX Oversize Packets",
	"RX Fragmented Frames",
	"RX Jabber Frames",
	"RX Dropped Frames",
	"TX Byte Counter",
	"TX Packets",
	"TX Multicast Packets",
	"TX Broadcast Packets",
	"TX Pause Control Frames",
	"TX Deferral Packets",
	"TX Excessive Deferral Packets",
	"TX Single Collision Packets",
	"TX Multiple Collision Packets",
	"TX Late Collision Packets",
	"TX Excessive Collision Packets",
	"TX Total Collision",
	"RESERVED",
	"TX Dropped Frames",
	"TX Jabber Frames",
	"TX FCS Errors",
	"TX Control Frames",
	"TX Oversize Frames",
	"TX Undersize Frames",
	"TX Fragmented Frames",
};

/* Fill in an array of 64-bit statistics from various sources.
 * This array will be appended to the end of the ethtool_stats
 * structure, and returned to user space
 */
void gfar_fill_stats(struct net_device *dev, struct ethtool_stats *dummy, u64 * buf)
{
	int i;
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	u32 *rmon = (u32 *) & priv->regs->rmon;
	u64 *extra = (u64 *) & priv->extra_stats;
	struct gfar_stats *stats = (struct gfar_stats *) buf;

	for (i = 0; i < GFAR_RMON_LEN; i++) {
		stats->rmon[i] = (u64) (rmon[i]);
	}

	for (i = 0; i < GFAR_EXTRA_STATS_LEN; i++) {
		stats->extra[i] = extra[i];
	}
}

/* Returns the number of stats (and their corresponding strings) */
int gfar_stats_count(struct net_device *dev)
{
	return GFAR_STATS_LEN;
}

void gfar_gstrings_normon(struct net_device *dev, u32 stringset, u8 * buf)
{
	memcpy(buf, stat_gstrings, GFAR_EXTRA_STATS_LEN * ETH_GSTRING_LEN);
}

void gfar_fill_stats_normon(struct net_device *dev, 
		struct ethtool_stats *dummy, u64 * buf)
{
	int i;
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	u64 *extra = (u64 *) & priv->extra_stats;

	for (i = 0; i < GFAR_EXTRA_STATS_LEN; i++) {
		buf[i] = extra[i];
	}
}


int gfar_stats_count_normon(struct net_device *dev)
{
	return GFAR_EXTRA_STATS_LEN;
}
/* Fills in the drvinfo structure with some basic info */
void gfar_gdrvinfo(struct net_device *dev, struct
	      ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver, gfar_driver_name, GFAR_INFOSTR_LEN);
	strncpy(drvinfo->version, gfar_driver_version, GFAR_INFOSTR_LEN);
	strncpy(drvinfo->fw_version, "N/A", GFAR_INFOSTR_LEN);
	strncpy(drvinfo->bus_info, "N/A", GFAR_INFOSTR_LEN);
	drvinfo->n_stats = GFAR_STATS_LEN;
	drvinfo->testinfo_len = 0;
	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

/* Return the current settings in the ethtool_cmd structure */
int gfar_gsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	uint gigabit_support = 
		priv->einfo->flags & GFAR_HAS_GIGABIT ? SUPPORTED_1000baseT_Full : 0;
	uint gigabit_advert = 
		priv->einfo->flags & GFAR_HAS_GIGABIT ? ADVERTISED_1000baseT_Full: 0;

	cmd->supported = (SUPPORTED_10baseT_Half
			  | SUPPORTED_100baseT_Half
			  | SUPPORTED_100baseT_Full
			  | gigabit_support | SUPPORTED_Autoneg);

	/* For now, we always advertise everything */
	cmd->advertising = (ADVERTISED_10baseT_Half
			    | ADVERTISED_100baseT_Half
			    | ADVERTISED_100baseT_Full
			    | gigabit_advert | ADVERTISED_Autoneg);

	cmd->speed = priv->speed;
	cmd->duplex = priv->duplexity;
	cmd->port = PORT_MII;
	cmd->phy_address = priv->einfo->phyid;
	cmd->transceiver = XCVR_EXTERNAL;
	cmd->autoneg = AUTONEG_ENABLE;
	cmd->maxtxpkt = priv->txcount;
	cmd->maxrxpkt = priv->rxcount;

	return 0;
}

/* Return the length of the register structure */
int gfar_reglen(struct net_device *dev)
{
	return sizeof (struct gfar);
}

/* Return a dump of the GFAR register space */
void gfar_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *regbuf)
{
	int i;
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	u32 *theregs = (u32 *) priv->regs;
	u32 *buf = (u32 *) regbuf;

	for (i = 0; i < sizeof (struct gfar) / sizeof (u32); i++)
		buf[i] = theregs[i];
}

/* Return the link state 1 is up, 0 is down */
u32 gfar_get_link(struct net_device *dev)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	return (u32) priv->link;
}

/* Fill in a buffer with the strings which correspond to the
 * stats */
void gfar_gstrings(struct net_device *dev, u32 stringset, u8 * buf)
{
	memcpy(buf, stat_gstrings, GFAR_STATS_LEN * ETH_GSTRING_LEN);
}

/* Convert microseconds to ethernet clock ticks, which changes
 * depending on what speed the controller is running at */
static unsigned int gfar_usecs2ticks(struct gfar_private *priv, unsigned int usecs)
{
	unsigned int count;

	/* The timer is different, depending on the interface speed */
	switch (priv->speed) {
	case 1000:
		count = GFAR_GBIT_TIME;
		break;
	case 100:
		count = GFAR_100_TIME;
		break;
	case 10:
	default:
		count = GFAR_10_TIME;
		break;
	}

	/* Make sure we return a number greater than 0
	 * if usecs > 0 */
	return ((usecs * 1000 + count - 1) / count);
}

/* Convert ethernet clock ticks to microseconds */
static unsigned int gfar_ticks2usecs(struct gfar_private *priv, unsigned int ticks)
{
	unsigned int count;

	/* The timer is different, depending on the interface speed */
	switch (priv->speed) {
	case 1000:
		count = GFAR_GBIT_TIME;
		break;
	case 100:
		count = GFAR_100_TIME;
		break;
	case 10:
	default:
		count = GFAR_10_TIME;
		break;
	}

	/* Make sure we return a number greater than 0 */
	/* if ticks is > 0 */
	return ((ticks * count) / 1000);
}

/* Get the coalescing parameters, and put them in the cvals
 * structure.  */
int gfar_gcoalesce(struct net_device *dev, struct ethtool_coalesce *cvals)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;

	cvals->rx_coalesce_usecs = gfar_ticks2usecs(priv, priv->rxtime);
	cvals->rx_max_coalesced_frames = priv->rxcount;

	cvals->tx_coalesce_usecs = gfar_ticks2usecs(priv, priv->txtime);
	cvals->tx_max_coalesced_frames = priv->txcount;

	cvals->use_adaptive_rx_coalesce = 0;
	cvals->use_adaptive_tx_coalesce = 0;

	cvals->pkt_rate_low = 0;
	cvals->rx_coalesce_usecs_low = 0;
	cvals->rx_max_coalesced_frames_low = 0;
	cvals->tx_coalesce_usecs_low = 0;
	cvals->tx_max_coalesced_frames_low = 0;

	/* When the packet rate is below pkt_rate_high but above
	 * pkt_rate_low (both measured in packets per second) the
	 * normal {rx,tx}_* coalescing parameters are used.
	 */

	/* When the packet rate is (measured in packets per second)
	 * is above pkt_rate_high, the {rx,tx}_*_high parameters are
	 * used.
	 */
	cvals->pkt_rate_high = 0;
	cvals->rx_coalesce_usecs_high = 0;
	cvals->rx_max_coalesced_frames_high = 0;
	cvals->tx_coalesce_usecs_high = 0;
	cvals->tx_max_coalesced_frames_high = 0;

	/* How often to do adaptive coalescing packet rate sampling,
	 * measured in seconds.  Must not be zero.
	 */
	cvals->rate_sample_interval = 0;

	return 0;
}

/* Change the coalescing values.
 * Both cvals->*_usecs and cvals->*_frames have to be > 0
 * in order for coalescing to be active
 */
int gfar_scoalesce(struct net_device *dev, struct ethtool_coalesce *cvals)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;

	/* Set up rx coalescing */
	if ((cvals->rx_coalesce_usecs == 0) ||
	    (cvals->rx_max_coalesced_frames == 0))
		priv->rxcoalescing = 0;
	else
		priv->rxcoalescing = 1;

	priv->rxtime = gfar_usecs2ticks(priv, cvals->rx_coalesce_usecs);
	priv->rxcount = cvals->rx_max_coalesced_frames;

	/* Set up tx coalescing */
	if ((cvals->tx_coalesce_usecs == 0) ||
	    (cvals->tx_max_coalesced_frames == 0))
		priv->txcoalescing = 0;
	else
		priv->txcoalescing = 1;

	priv->txtime = gfar_usecs2ticks(priv, cvals->tx_coalesce_usecs);
	priv->txcount = cvals->tx_max_coalesced_frames;

	if (priv->rxcoalescing)
		gfar_write(&priv->regs->rxic,
			   mk_ic_value(priv->rxcount, priv->rxtime));
	else
		gfar_write(&priv->regs->rxic, 0);

	if (priv->txcoalescing)
		gfar_write(&priv->regs->txic,
			   mk_ic_value(priv->txcount, priv->txtime));
	else
		gfar_write(&priv->regs->txic, 0);

	return 0;
}

/* Fills in rvals with the current ring parameters.  Currently,
 * rx, rx_mini, and rx_jumbo rings are the same size, as mini and
 * jumbo are ignored by the driver */
void gfar_gringparam(struct net_device *dev, struct ethtool_ringparam *rvals)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;

	rvals->rx_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->rx_mini_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->rx_jumbo_max_pending = GFAR_RX_MAX_RING_SIZE;
	rvals->tx_max_pending = GFAR_TX_MAX_RING_SIZE;

	/* Values changeable by the user.  The valid values are
	 * in the range 1 to the "*_max_pending" counterpart above.
	 */
	rvals->rx_pending = priv->rx_ring_size;
	rvals->rx_mini_pending = priv->rx_ring_size;
	rvals->rx_jumbo_pending = priv->rx_ring_size;
	rvals->tx_pending = priv->tx_ring_size;
}

/* Change the current ring parameters, stopping the controller if
 * necessary so that we don't mess things up while we're in
 * motion.  We wait for the ring to be clean before reallocating
 * the rings. */
int gfar_sringparam(struct net_device *dev, struct ethtool_ringparam *rvals)
{
	u32 tempval;
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	int err = 0;

	if (rvals->rx_pending > GFAR_RX_MAX_RING_SIZE)
		return -EINVAL;

	if (!is_power_of_2(rvals->rx_pending)) {
		printk("%s: Ring sizes must be a power of 2\n",
				dev->name);
		return -EINVAL;
	}

	if (rvals->tx_pending > GFAR_TX_MAX_RING_SIZE)
		return -EINVAL;

	if (!is_power_of_2(rvals->tx_pending)) {
		printk("%s: Ring sizes must be a power of 2\n",
				dev->name);
		return -EINVAL;
	}

	/* Stop the controller so we don't rx any more frames */
	/* But first, make sure we clear the bits */
	tempval = gfar_read(&priv->regs->dmactrl);
	tempval &= ~(DMACTRL_GRS | DMACTRL_GTS);
	gfar_write(&priv->regs->dmactrl, tempval);

	tempval = gfar_read(&priv->regs->dmactrl);
	tempval |= (DMACTRL_GRS | DMACTRL_GTS);
	gfar_write(&priv->regs->dmactrl, tempval);

	while (!(gfar_read(&priv->regs->ievent) & (IEVENT_GRSC | IEVENT_GTSC)))
		cpu_relax();

	/* Note that rx is not clean right now */
	priv->rxclean = 0;

	if (dev->flags & IFF_UP) {
		/* Tell the driver to process the rest of the frames */
		gfar_receive(0, (void *) dev, NULL);

		/* Now wait for it to be done */
		wait_event_interruptible(priv->rxcleanupq, priv->rxclean);

		/* Ok, all packets have been handled.  Now we bring it down,
		 * change the ring size, and bring it up */

		stop_gfar(dev);
	}

	priv->rx_ring_size = rvals->rx_pending;
	priv->tx_ring_size = rvals->tx_pending;

	if (dev->flags & IFF_UP)
		err = startup_gfar(dev);

	return err;
}

struct ethtool_ops gfar_ethtool_ops = {
	.get_settings = gfar_gsettings,
	.get_drvinfo = gfar_gdrvinfo,
	.get_regs_len = gfar_reglen,
	.get_regs = gfar_get_regs,
	.get_link = gfar_get_link,
	.get_coalesce = gfar_gcoalesce,
	.set_coalesce = gfar_scoalesce,
	.get_ringparam = gfar_gringparam,
	.set_ringparam = gfar_sringparam,
	.get_strings = gfar_gstrings,
	.get_stats_count = gfar_stats_count,
	.get_ethtool_stats = gfar_fill_stats,
};
