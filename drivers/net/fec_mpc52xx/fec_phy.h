/*
 * arch/ppc/52xx_io/fec_phy.h
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

#ifdef CONFIG_USE_MDIO
#define MII_ADVERTISE_HALF	(ADVERTISE_100HALF | ADVERTISE_10HALF | \
				 ADVERTISE_CSMA)

#define MII_ADVERTISE_ALL	(ADVERTISE_100FULL | ADVERTISE_10FULL | \
				 MII_ADVERTISE_HALF)
#ifdef PHY_INTERRUPT
#define MII_ADVERTISE_DEFAULT	MII_ADVERTISE_ALL
#else
#define MII_ADVERTISE_DEFAULT	MII_ADVERTISE_HALF
#endif

#define MII_RCNTL_MODE		FEC_RCNTRL_MII_MODE
#define set_phy_speed(fec, s)	out_be32(&fec->mii_speed, s)
#define FEC_IMASK_ENABLE	0xf0fe0000

typedef struct {
	uint mii_data;
	void (*funct)(uint mii_reg, struct net_device *dev, uint data);
} phy_cmd_t;

typedef struct {
	uint id;
	char *name;

	const phy_cmd_t *config;
	const phy_cmd_t *startup;
	const phy_cmd_t *ack_int;
	const phy_cmd_t *shutdown;
} phy_info_t;

#else
#define MII_RCNTL_MODE		0
#define set_phy_speed(fec, s)
#define FEC_IMASK_ENABLE	0xf07e0000
#define fec_mii_wait(dev)	0
#define fec_mii(dev)	printk(KERN_WARNING "unexpected FEC_IEVENT_MII\n")
#define fec_mii_init(dev)
#endif	/* CONFIG_USE_MDIO */

/* MII-related definitions */
#define FEC_MII_DATA_ST		0x40000000	/* Start frame */
#define FEC_MII_DATA_OP_RD	0x20000000	/* Perform read */
#define FEC_MII_DATA_OP_WR	0x10000000	/* Perform write */
#define FEC_MII_DATA_PA_MSK	0x0f800000	/* PHY Address mask */
#define FEC_MII_DATA_RA_MSK	0x007c0000	/* PHY Register mask */
#define FEC_MII_DATA_TA		0x00020000	/* Turnaround */
#define FEC_MII_DATA_DATAMSK	0x00000fff	/* PHY data mask */

#define FEC_MII_DATA_RA_SHIFT	0x12		/* MII reg addr bits */
#define FEC_MII_DATA_PA_SHIFT	0x17		/* MII PHY addr bits */

#define FEC_MII_SPEED		(5 * 2)

extern void fec_mii_init(struct net_device *dev);
extern int fec_mii_wait(struct net_device *dev);
extern void fec_mii(struct net_device *dev);

extern int fec_ioctl(struct net_device *, struct ifreq *rq, int cmd);
