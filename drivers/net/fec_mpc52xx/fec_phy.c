/*
 * arch/ppc/52xx_io/fec_phy.c
 *
 * Driver for the MPC5200 Fast Ethernet Controller
 * Based heavily on the MII support for the MPC8xx by Dan Malek
 *
 * Author: Dale Farnsworth <dfarnsworth@mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <asm/io.h>
#include <asm/mpc52xx.h>

#ifdef CONFIG_PPC_MERGE
#include <platforms/52xx/bestcomm.h>
#else
#include <syslib/bestcomm/bestcomm.h>
#include <syslib/bestcomm/fec.h>
#endif

#include "fec_phy.h"
#include "fec.h"

static int mpc52xx_netdev_ethtool_ioctl(struct net_device *dev, void *useraddr);

/* MII processing.  We keep this as simple as possible.  Requests are
 * placed on the list (if there is room).  When the request is finished
 * by the MII, an optional function may be called.
 */
typedef struct mii_list {
	uint	mii_regval;
	void	(*mii_func)(uint val, struct net_device *dev, uint data);
	struct	mii_list *mii_next;
	uint	mii_data;
} mii_list_t;

#define		NMII	20
mii_list_t	mii_cmds[NMII];
mii_list_t	*mii_free;
mii_list_t	*mii_head;
mii_list_t	*mii_tail;

typedef struct mdio_read_data {
	__u16 regval;
	struct task_struct *sleeping_task;
} mdio_read_data_t;

static int mii_queue(struct net_device *dev, int request,
		void (*func)(uint, struct net_device *, uint), uint data);

/* Make MII read/write commands for the FEC.
 * */
#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | \
							(VAL & 0xffff))
#define mk_mii_end	0

/* Register definitions for the PHY.
*/

#define MII_REG_CR	 0	/* Control Register */
#define MII_REG_SR	 1	/* Status Register */
#define MII_REG_PHYIR1	 2	/* PHY Identification Register 1 */
#define MII_REG_PHYIR2	 3	/* PHY Identification Register 2 */
#define MII_REG_ANAR	 4	/* A-N Advertisement Register */
#define MII_REG_ANLPAR	 5	/* A-N Link Partner Ability Register */
#define MII_REG_ANER	 6	/* A-N Expansion Register */
#define MII_REG_ANNPTR	 7	/* A-N Next Page Transmit Register */
#define MII_REG_ANLPRNPR 8	/* A-N Link Partner Received Next Page Reg. */

/* values for phy_status */

#define PHY_CONF_ANE	0x0001	/* 1 auto-negotiation enabled */
#define PHY_CONF_LOOP	0x0002	/* 1 loopback mode enabled */
#define PHY_CONF_SPMASK	0x00f0	/* mask for speed */
#define PHY_CONF_10HDX	0x0010	/* 10 Mbit half duplex supported */
#define PHY_CONF_10FDX	0x0020	/* 10 Mbit full duplex supported */
#define PHY_CONF_100HDX	0x0040	/* 100 Mbit half duplex supported */
#define PHY_CONF_100FDX	0x0080	/* 100 Mbit full duplex supported */

#define PHY_STAT_LINK	0x0100	/* 1 up - 0 down */
#define PHY_STAT_FAULT	0x0200	/* 1 remote fault */
#define PHY_STAT_ANC	0x0400	/* 1 auto-negotiation complete	*/
#define PHY_STAT_SPMASK	0xf000	/* mask for speed */
#define PHY_STAT_10HDX	0x1000	/* 10 Mbit half duplex selected	*/
#define PHY_STAT_10FDX	0x2000	/* 10 Mbit full duplex selected	*/
#define PHY_STAT_100HDX	0x4000	/* 100 Mbit half duplex selected */
#define PHY_STAT_100FDX	0x8000	/* 100 Mbit full duplex selected */

void fec_mii(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;
	mii_list_t	*mip;
	uint		mii_reg;

	mii_reg = in_be32(&fec->mii_data);

	if ((mip = mii_head) == NULL) {
		printk(KERN_ERR "MII and no head!\n");
		return;
	}

	if (mip->mii_func != NULL)
		(*(mip->mii_func))(mii_reg, dev, mip->mii_data);

	mii_head = mip->mii_next;
	mip->mii_next = mii_free;
	mii_free = mip;

	if ((mip = mii_head) != NULL)
		out_be32(&fec->mii_data, mip->mii_regval);
}

static int mii_queue(struct net_device *dev, int regval,
				void (*func)(uint, struct net_device *, uint),
				uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	struct mpc52xx_fec *fec = priv->fec;
	mii_list_t	*mip;
	int		retval;

	/* Add PHY address to register command.
	*/
	regval |= priv->phy_addr << 23;

	retval = 0;

	if ((mip = mii_free) != NULL) {
		mii_free = mip->mii_next;
		mip->mii_regval = regval;
		mip->mii_func = func;
		mip->mii_next = NULL;
		mip->mii_data = data;
		if (mii_head) {
			mii_tail->mii_next = mip;
			mii_tail = mip;
		} else {
			mii_head = mii_tail = mip;
			out_be32(&fec->mii_data, regval);
		}
	} else
		retval = 1;

	return retval;
}

static void mii_do_cmd(struct net_device *dev, const phy_cmd_t *c)
{
	int k;

	if (!c)
		return;

	for (k = 0; (c+k)->mii_data != mk_mii_end; k++)
		mii_queue(dev, (c+k)->mii_data, (c+k)->funct, 0);
}

static void mii_parse_sr(uint mii_reg, struct net_device *dev, uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	uint s = priv->phy_status;

	s &= ~(PHY_STAT_LINK | PHY_STAT_FAULT | PHY_STAT_ANC);

	if (mii_reg & 0x0004)
		s |= PHY_STAT_LINK;
	if (mii_reg & 0x0010)
		s |= PHY_STAT_FAULT;
	if (mii_reg & 0x0020)
		s |= PHY_STAT_ANC;

	priv->phy_status = s;
}

static void mii_parse_cr(uint mii_reg, struct net_device *dev, uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	uint s = priv->phy_status;

	s &= ~(PHY_CONF_ANE | PHY_CONF_LOOP);

	if (mii_reg & 0x1000)
		s |= PHY_CONF_ANE;
	if (mii_reg & 0x4000)
		s |= PHY_CONF_LOOP;

	priv->phy_status = s;
}

static void mii_parse_anar(uint mii_reg, struct net_device *dev, uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	uint s = priv->phy_status;

	s &= ~(PHY_CONF_SPMASK);

	if (mii_reg & 0x0020)
		s |= PHY_CONF_10HDX;
	if (mii_reg & 0x0040)
		s |= PHY_CONF_10FDX;
	if (mii_reg & 0x0080)
		s |= PHY_CONF_100HDX;
	if (mii_reg & 0x0100)
		s |= PHY_CONF_100FDX;

	priv->phy_status = s;
}

/* ------------------------------------------------------------------------- */
/* Generic PHY support.  Should work for all PHYs, but does not support link
 * change interrupts.
 */
static phy_info_t phy_info_generic = {
	0x00000000, /* 0-->match any PHY */
	"GENERIC",

	(const phy_cmd_t []) {	/* config */
		/* advertise only half-duplex capabilities */
		{ mk_mii_write(MII_ADVERTISE, MII_ADVERTISE_HALF),
			mii_parse_anar },

		/* enable auto-negotiation */
		{ mk_mii_write(MII_BMCR, BMCR_ANENABLE), mii_parse_cr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {	/* startup */
		/* restart auto-negotiation */
		{ mk_mii_write(MII_BMCR, (BMCR_ANENABLE | BMCR_ANRESTART)),
			NULL },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) { /* ack_int */
		/* We don't actually use the ack_int table with a generic
		 * PHY, but putting a reference to mii_parse_sr here keeps
		 * us from getting a compiler warning about unused static
		 * functions in the case where we only compile in generic
		 * PHY support.
		 */
		{ mk_mii_read(MII_BMSR), mii_parse_sr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {	/* shutdown */
		{ mk_mii_end, }
	},
};
/* -------------------------------------------------------------------- */

/* register definitions for the 971 */

#define MII_LXT971_PCR	16	/* Port Control Register	*/
#define MII_LXT971_SR2	17	/* Status Register 2		*/
#define MII_LXT971_IER	18	/* Interrupt Enable Register	*/
#define MII_LXT971_ISR	19	/* Interrupt Status Register	*/
#define MII_LXT971_LCR	20	/* LED Control Register		*/
#define MII_LXT971_TCR	30	/* Transmit Control Register	*/

static void mii_parse_lxt971_sr2(uint mii_reg, struct net_device *dev, uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	uint s = priv->phy_status;

	s &= ~(PHY_STAT_SPMASK);

	if (mii_reg & 0x4000) {
		if (mii_reg & 0x0200)
			s |= PHY_STAT_100FDX;
		else
			s |= PHY_STAT_100HDX;
	} else {
		if (mii_reg & 0x0200)
			s |= PHY_STAT_10FDX;
		else
			s |= PHY_STAT_10HDX;
	}
	if (mii_reg & 0x0008)
		s |= PHY_STAT_FAULT;

	priv->phy_status = s;
}

static phy_info_t phy_info_lxt971 = {
	0x0001378e,
	"LXT971",

	(const phy_cmd_t []) {	/* config */
		{ mk_mii_write(MII_REG_ANAR, 0x0A1), NULL }, /* 10/100, HD */
		{ mk_mii_read(MII_REG_CR), mii_parse_cr },
		{ mk_mii_read(MII_REG_ANAR), mii_parse_anar },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {	/* startup - enable interrupts */
		{ mk_mii_write(MII_LXT971_IER, 0x00f2), NULL },
		{ mk_mii_write(MII_REG_CR, 0x1200), NULL }, /* autonegotiate */

		/* Somehow does the 971 tell me that the link is down
		 * the first read after power-up.
		 * read here to get a valid value in ack_int */

		{ mk_mii_read(MII_REG_SR), mii_parse_sr },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) { /* ack_int */
		/* find out the current status */

		{ mk_mii_read(MII_REG_SR), mii_parse_sr },
		{ mk_mii_read(MII_LXT971_SR2), mii_parse_lxt971_sr2 },

		/* we only need to read ISR to acknowledge */

		{ mk_mii_read(MII_LXT971_ISR), NULL },
		{ mk_mii_end, }
	},
	(const phy_cmd_t []) {	/* shutdown - disable interrupts */
		{ mk_mii_write(MII_LXT971_IER, 0x0000), NULL },
		{ mk_mii_end, }
	},
};

static phy_info_t *phy_info[] = {
	&phy_info_lxt971,
	/* Generic PHY support.  This must be the last PHY in the table.
	 * It will be used to support any PHY that doesn't match a previous
	 * entry in the table.
	 */
	&phy_info_generic,
	NULL
};

static void mii_display_config(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	uint s = priv->phy_status;

	printk(KERN_INFO "%s: config: auto-negotiation ", dev->name);

	if (s & PHY_CONF_ANE)
		printk("on");
	else
		printk("off");

	if (s & PHY_CONF_100FDX)
		printk(", 100FDX");
	if (s & PHY_CONF_100HDX)
		printk(", 100HDX");
	if (s & PHY_CONF_10FDX)
		printk(", 10FDX");
	if (s & PHY_CONF_10HDX)
		printk(", 10HDX");
	if (!(s & PHY_CONF_SPMASK))
		printk(", No speed/duplex selected?");

	if (s & PHY_CONF_LOOP)
		printk(", loopback enabled");

	printk(".\n");

	priv->sequence_done = 1;
}

static void mii_queue_config(uint mii_reg, struct net_device *dev, uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;

	priv->phy_task.func = (void *)mii_display_config;
	priv->phy_task.data = (unsigned long)dev;
	tasklet_schedule(&priv->phy_task);
}


phy_cmd_t phy_cmd_config[] =  { { mk_mii_read(MII_REG_CR), mii_queue_config },
				{ mk_mii_end, } };


/* Read remainder of PHY ID.
*/
static void mii_discover_phy3(uint mii_reg, struct net_device *dev, uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	int	i;

	priv->phy_id |= (mii_reg & 0xffff);

	for (i = 0; phy_info[i]; i++) {
		if (phy_info[i]->id == (priv->phy_id >> 4) || !phy_info[i]->id)
			break;
		if (phy_info[i]->id == 0)	/* check generic entry */
			break;
	}

	if (!phy_info[i])
		panic("%s: PHY id 0x%08x is not supported!\n",
			dev->name, priv->phy_id);

	priv->phy = phy_info[i];
	priv->phy_id_done = 1;

	printk(KERN_INFO "%s: Phy @ 0x%x, type %s (0x%08x)\n",
		dev->name, priv->phy_addr, priv->phy->name, priv->phy_id);
}

/* Scan all of the MII PHY addresses looking for someone to respond
 * with a valid ID.  This usually happens quickly.
 */
static void mii_discover_phy(uint mii_reg, struct net_device *dev, uint data)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	uint	phytype;

	if ((phytype = (mii_reg & 0xffff)) != 0xffff) {
		/* Got first part of ID, now get remainder.
		*/
		priv->phy_id = phytype << 16;
		mii_queue(dev, mk_mii_read(MII_REG_PHYIR2), mii_discover_phy3,
									0);
	} else {
		priv->phy_addr++;
		if (priv->phy_addr < 32)
			mii_queue(dev, mk_mii_read(MII_REG_PHYIR1),
							mii_discover_phy, 0);
		else
			printk(KERN_ERR "fec: No PHY device found.\n");
	}
}

static int mpc52xx_netdev_ethtool_ioctl(struct net_device *dev, void *useraddr)
{
	__u32 ethcmd;

	if (copy_from_user(&ethcmd, useraddr, sizeof ethcmd))
		return -EFAULT;

	switch (ethcmd) {

		/* Get driver info */
	case ETHTOOL_GDRVINFO:{
			struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
			strncpy(info.driver, "MPC5200 FEC",
				sizeof info.driver - 1);
			if (copy_to_user(useraddr, &info, sizeof info))
				return -EFAULT;
			return 0;
		}
		/* get message-level */
	case ETHTOOL_GMSGLVL:{
			struct ethtool_value edata = { ETHTOOL_GMSGLVL };
			edata.data = 0;	/* XXX */
			if (copy_to_user(useraddr, &edata, sizeof edata))
				return -EFAULT;
			return 0;
		}
		/* set message-level */
	case ETHTOOL_SMSGLVL:{
			struct ethtool_value edata;
			if (copy_from_user(&edata, useraddr, sizeof edata))
				return -EFAULT;
			return 0;
		}
	}
	return -EOPNOTSUPP;
}

int fec_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int retval;

	switch (cmd) {
	case SIOCETHTOOL:
		retval = mpc52xx_netdev_ethtool_ioctl(
					dev, (void *) rq->ifr_data);
		break;

	default:
		retval = -EOPNOTSUPP;
		break;
	}
	return retval;
}

void fec_mii_init(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;
	int i;

	for (i=0; i<NMII-1; i++)
		mii_cmds[i].mii_next = &mii_cmds[i+1];
	mii_free = mii_cmds;

	/* Queue up command to detect the PHY and initialize the
	 * remainder of the interface.
	 */
	priv->phy_id_done = 0;
	priv->phy_addr = 0;
	mii_queue(dev, mk_mii_read(MII_REG_PHYIR1), mii_discover_phy, 0);

	priv->old_status = 0;
}

int fec_mii_wait(struct net_device *dev)
{
	struct fec_priv *priv = (struct fec_priv *)dev->priv;

	if (!priv->sequence_done) {
		if (!priv->phy) {
			printk("KERN_ERR fec_open: PHY not configured\n");
			return -ENODEV;		/* No PHY we understand */
		}

		mii_do_cmd(dev, priv->phy->config);
		mii_do_cmd(dev, phy_cmd_config); /* display configuration */
		while(!priv->sequence_done)
			schedule();

		mii_do_cmd(dev, priv->phy->startup);
	}
	return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dale Farnsworth");
MODULE_DESCRIPTION("PHY driver for Motorola MPC52xx FEC");
