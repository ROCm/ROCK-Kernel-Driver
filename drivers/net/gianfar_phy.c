/* 
 * drivers/net/gianfar_phy.c
 *
 * Gianfar Ethernet Driver -- PHY handling
 * Driver for FEC on MPC8540 and TSEC on MPC8540/MPC8560
 * Based on 8260_io/fcc_enet.c
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
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

#include "gianfar.h"
#include "gianfar_phy.h"

/* Write value to the PHY for this device to the register at regnum, */
/* waiting until the write is done before it returns.  All PHY */
/* configuration has to be done through the TSEC1 MIIM regs */
void write_phy_reg(struct net_device *dev, u16 regnum, u16 value)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	struct gfar *regbase = priv->phyregs;
	struct ocp_gfar_data *einfo = priv->einfo;

	/* Set the PHY address and the register address we want to write */
	gfar_write(&regbase->miimadd, ((einfo->phyid) << 8) | regnum);

	/* Write out the value we want */
	gfar_write(&regbase->miimcon, value);

	/* Wait for the transaction to finish */
	while (gfar_read(&regbase->miimind) & MIIMIND_BUSY)
		cpu_relax();
}

/* Reads from register regnum in the PHY for device dev, */
/* returning the value.  Clears miimcom first.  All PHY */
/* configuration has to be done through the TSEC1 MIIM regs */
u16 read_phy_reg(struct net_device *dev, u16 regnum)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	struct gfar *regbase = priv->phyregs;
	struct ocp_gfar_data *einfo = priv->einfo;
	u16 value;

	/* Set the PHY address and the register address we want to read */
	gfar_write(&regbase->miimadd, ((einfo->phyid) << 8) | regnum);

	/* Clear miimcom, and then initiate a read */
	gfar_write(&regbase->miimcom, 0);
	gfar_write(&regbase->miimcom, MIIM_READ_COMMAND);

	/* Wait for the transaction to finish */
	while (gfar_read(&regbase->miimind) & (MIIMIND_NOTVALID | MIIMIND_BUSY))
		cpu_relax();

	/* Grab the value of the register from miimstat */
	value = gfar_read(&regbase->miimstat);

	return value;
}

/* returns which value to write to the control register. */
/* For 10/100 the value is slightly different. */
u16 mii_cr_init(u16 mii_reg, struct net_device * dev)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	struct ocp_gfar_data *einfo = priv->einfo;

	if (einfo->flags & GFAR_HAS_GIGABIT)
		return MIIM_CONTROL_INIT;
	else
		return MIIM_CR_INIT;
}

#define BRIEF_GFAR_ERRORS
/* Wait for auto-negotiation to complete */
u16 mii_parse_sr(u16 mii_reg, struct net_device * dev)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;

	unsigned int timeout = GFAR_AN_TIMEOUT;

	if (mii_reg & MIIM_STATUS_LINK)
		priv->link = 1;
	else
		priv->link = 0;

	/* Only auto-negotiate if the link has just gone up */
	if (priv->link && !priv->oldlink) {
		while ((!(mii_reg & MIIM_STATUS_AN_DONE)) && timeout--)
			mii_reg = read_phy_reg(dev, MIIM_STATUS);

#if defined(BRIEF_GFAR_ERRORS)
		if (mii_reg & MIIM_STATUS_AN_DONE)
			printk(KERN_INFO "%s: Auto-negotiation done\n",
			       dev->name);
		else
			printk(KERN_INFO "%s: Auto-negotiation timed out\n",
			       dev->name);
#endif
	}

	return 0;
}

/* Determine the speed and duplex which was negotiated */
u16 mii_parse_88E1011_psr(u16 mii_reg, struct net_device * dev)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	unsigned int speed;

	if (priv->link) {
		if (mii_reg & MIIM_88E1011_PHYSTAT_DUPLEX)
			priv->duplexity = 1;
		else
			priv->duplexity = 0;

		speed = (mii_reg & MIIM_88E1011_PHYSTAT_SPEED);

		switch (speed) {
		case MIIM_88E1011_PHYSTAT_GBIT:
			priv->speed = 1000;
			break;
		case MIIM_88E1011_PHYSTAT_100:
			priv->speed = 100;
			break;
		default:
			priv->speed = 10;
			break;
		}
	} else {
		priv->speed = 0;
		priv->duplexity = 0;
	}

	return 0;
}

u16 mii_parse_cis8201(u16 mii_reg, struct net_device * dev)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	unsigned int speed;

	if (priv->link) {
		if (mii_reg & MIIM_CIS8201_AUXCONSTAT_DUPLEX)
			priv->duplexity = 1;
		else
			priv->duplexity = 0;

		speed = mii_reg & MIIM_CIS8201_AUXCONSTAT_SPEED;

		switch (speed) {
		case MIIM_CIS8201_AUXCONSTAT_GBIT:
			priv->speed = 1000;
			break;
		case MIIM_CIS8201_AUXCONSTAT_100:
			priv->speed = 100;
			break;
		default:
			priv->speed = 10;
			break;
		}
	} else {
		priv->speed = 0;
		priv->duplexity = 0;
	}

	return 0;
}

u16 mii_parse_dm9161_scsr(u16 mii_reg, struct net_device * dev)
{
	struct gfar_private *priv = (struct gfar_private *) dev->priv;

	if (mii_reg & (MIIM_DM9161_SCSR_100F | MIIM_DM9161_SCSR_100H))
		priv->speed = 100;
	else
		priv->speed = 10;

	if (mii_reg & (MIIM_DM9161_SCSR_100F | MIIM_DM9161_SCSR_10F))
		priv->duplexity = 1;
	else
		priv->duplexity = 0;

	return 0;
}

u16 dm9161_wait(u16 mii_reg, struct net_device *dev)
{
	int timeout = HZ;
	int secondary = 10;
	u16 temp;

	do {

		/* Davicom takes a bit to come up after a reset,
		 * so wait here for a bit */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(timeout);

		temp = read_phy_reg(dev, MIIM_STATUS);

		secondary--;
	} while ((!(temp & MIIM_STATUS_AN_DONE)) && secondary);

	return 0;
}

static struct phy_info phy_info_M88E1011S = {
	0x01410c6,
	"Marvell 88E1011S",
	4,
	(const struct phy_cmd[]) {	/* config */
		/* Reset and configure the PHY */
		{MIIM_CONTROL, MIIM_CONTROL_INIT, mii_cr_init},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* startup */
		/* Status is read once to clear old link state */
		{MIIM_STATUS, miim_read, NULL},
		/* Auto-negotiate */
		{MIIM_STATUS, miim_read, mii_parse_sr},
		/* Read the status */
		{MIIM_88E1011_PHY_STATUS, miim_read, mii_parse_88E1011_psr},
		/* Clear the IEVENT register */
		{MIIM_88E1011_IEVENT, miim_read, NULL},
		/* Set up the mask */
		{MIIM_88E1011_IMASK, MIIM_88E1011_IMASK_INIT, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* ack_int */
		/* Clear the interrupt */
		{MIIM_88E1011_IEVENT, miim_read, NULL},
		/* Disable interrupts */
		{MIIM_88E1011_IMASK, MIIM_88E1011_IMASK_CLEAR, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* handle_int */
		/* Read the Status (2x to make sure link is right) */
		{MIIM_STATUS, miim_read, NULL},
		/* Check the status */
		{MIIM_STATUS, miim_read, mii_parse_sr},
		{MIIM_88E1011_PHY_STATUS, miim_read, mii_parse_88E1011_psr},
			/* Enable Interrupts */
		{MIIM_88E1011_IMASK, MIIM_88E1011_IMASK_INIT, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* shutdown */
		{MIIM_88E1011_IEVENT, miim_read, NULL},
		{MIIM_88E1011_IMASK, MIIM_88E1011_IMASK_CLEAR, NULL},
		{miim_end,}
	},
};

/* Cicada 8204 */
static struct phy_info phy_info_cis8204 = {
	0x3f11,
	"Cicada Cis8204",
	6,
	(const struct phy_cmd[]) {	/* config */
		/* Override PHY config settings */
		{MIIM_CIS8201_AUX_CONSTAT, MIIM_CIS8201_AUXCONSTAT_INIT, NULL},
		/* Set up the interface mode */
		{MIIM_CIS8201_EXT_CON1, MIIM_CIS8201_EXTCON1_INIT, NULL},
		/* Configure some basic stuff */
		{MIIM_CONTROL, MIIM_CONTROL_INIT, mii_cr_init},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* startup */
		/* Read the Status (2x to make sure link is right) */
		{MIIM_STATUS, miim_read, NULL},
		/* Auto-negotiate */
		{MIIM_STATUS, miim_read, mii_parse_sr},
		/* Read the status */
		{MIIM_CIS8201_AUX_CONSTAT, miim_read, mii_parse_cis8201},
		/* Clear the status register */
		{MIIM_CIS8204_ISTAT, miim_read, NULL},
		/* Enable interrupts */
		{MIIM_CIS8204_IMASK, MIIM_CIS8204_IMASK_MASK, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* ack_int */
		/* Clear the status register */
		{MIIM_CIS8204_ISTAT, miim_read, NULL},
		/* Disable interrupts */
		{MIIM_CIS8204_IMASK, 0x0, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* handle_int */
		/* Read the Status (2x to make sure link is right) */
		{MIIM_STATUS, miim_read, NULL},
		/* Auto-negotiate */
		{MIIM_STATUS, miim_read, mii_parse_sr},
		/* Read the status */
		{MIIM_CIS8201_AUX_CONSTAT, miim_read, mii_parse_cis8201},
		/* Enable interrupts */
		{MIIM_CIS8204_IMASK, MIIM_CIS8204_IMASK_MASK, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* shutdown */
		/* Clear the status register */
		{MIIM_CIS8204_ISTAT, miim_read, NULL},
		/* Disable interrupts */
		{MIIM_CIS8204_IMASK, 0x0, NULL},
		{miim_end,}
	},
};

/* Cicada 8201 */
static struct phy_info phy_info_cis8201 = {
	0xfc41,
	"CIS8201",
	4,
	(const struct phy_cmd[]) {	/* config */
		/* Override PHY config settings */
		{MIIM_CIS8201_AUX_CONSTAT, MIIM_CIS8201_AUXCONSTAT_INIT, NULL},
		/* Set up the interface mode */
		{MIIM_CIS8201_EXT_CON1, MIIM_CIS8201_EXTCON1_INIT, NULL},
		/* Configure some basic stuff */
		{MIIM_CONTROL, MIIM_CONTROL_INIT, mii_cr_init},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* startup */
		/* Read the Status (2x to make sure link is right) */
		{MIIM_STATUS, miim_read, NULL},
		/* Auto-negotiate */
		{MIIM_STATUS, miim_read, mii_parse_sr},
		/* Read the status */
		{MIIM_CIS8201_AUX_CONSTAT, miim_read, mii_parse_cis8201},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* ack_int */
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* handle_int */
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* shutdown */
		{miim_end,}
	},
};

static struct phy_info phy_info_dm9161 = {
	0x0181b88,
	"Davicom DM9161E",
	4,
	(const struct phy_cmd[]) {	/* config */
		{MIIM_CONTROL, MIIM_DM9161_CR_STOP, NULL},
		/* Do not bypass the scrambler/descrambler */
		{MIIM_DM9161_SCR, MIIM_DM9161_SCR_INIT, NULL},
		/* Clear 10BTCSR to default */
		{MIIM_DM9161_10BTCSR, MIIM_DM9161_10BTCSR_INIT, NULL},
		/* Configure some basic stuff */
		{MIIM_CONTROL, MIIM_CR_INIT, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* startup */
		/* Restart Auto Negotiation */
		{MIIM_CONTROL, MIIM_DM9161_CR_RSTAN, NULL},
		/* Status is read once to clear old link state */
		{MIIM_STATUS, miim_read, dm9161_wait},
		/* Auto-negotiate */
		{MIIM_STATUS, miim_read, mii_parse_sr},
		/* Read the status */
		{MIIM_DM9161_SCSR, miim_read, mii_parse_dm9161_scsr},
		/* Clear any pending interrupts */
		{MIIM_DM9161_INTR, miim_read, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* ack_int */
		{MIIM_DM9161_INTR, miim_read, NULL},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* handle_int */
		{MIIM_STATUS, miim_read, NULL},
		{MIIM_STATUS, miim_read, mii_parse_sr},
		{MIIM_DM9161_SCSR, miim_read, mii_parse_dm9161_scsr},
		{miim_end,}
	},
	(const struct phy_cmd[]) {	/* shutdown */
		{MIIM_DM9161_INTR, miim_read, NULL},
		{miim_end,}
	},
};

static struct phy_info *phy_info[] = {
	&phy_info_cis8201,
	&phy_info_cis8204,
	&phy_info_M88E1011S,
	&phy_info_dm9161,
	NULL
};

/* Use the PHY ID registers to determine what type of PHY is attached
 * to device dev.  return a struct phy_info structure describing that PHY
 */
struct phy_info * get_phy_info(struct net_device *dev)
{
	u16 phy_reg;
	u32 phy_ID;
	int i;
	struct phy_info *theInfo = NULL;

	/* Grab the bits from PHYIR1, and put them in the upper half */
	phy_reg = read_phy_reg(dev, MIIM_PHYIR1);
	phy_ID = (phy_reg & 0xffff) << 16;

	/* Grab the bits from PHYIR2, and put them in the lower half */
	phy_reg = read_phy_reg(dev, MIIM_PHYIR2);
	phy_ID |= (phy_reg & 0xffff);

	/* loop through all the known PHY types, and find one that */
	/* matches the ID we read from the PHY. */
	for (i = 0; phy_info[i]; i++)
		if (phy_info[i]->id == (phy_ID >> phy_info[i]->shift))
			theInfo = phy_info[i];

	if (theInfo == NULL) {
		printk("%s: PHY id %x is not supported!\n", dev->name, phy_ID);
		return NULL;
	} else {
		printk("%s: PHY is %s (%x)\n", dev->name, theInfo->name,
		       phy_ID);
	}

	return theInfo;
}

/* Take a list of struct phy_cmd, and, depending on the values, either */
/* read or write, using a helper function if provided */
/* It is assumed that all lists of struct phy_cmd will be terminated by */
/* mii_end. */
void phy_run_commands(struct net_device *dev, const struct phy_cmd *cmd)
{
	int i;
	u16 result;
	struct gfar_private *priv = (struct gfar_private *) dev->priv;
	struct gfar *phyregs = priv->phyregs;

	/* Reset the management interface */
	gfar_write(&phyregs->miimcfg, MIIMCFG_RESET);

	/* Setup the MII Mgmt clock speed */
	gfar_write(&phyregs->miimcfg, MIIMCFG_INIT_VALUE);

	/* Wait until the bus is free */
	while (gfar_read(&phyregs->miimind) & MIIMIND_BUSY)
		cpu_relax();

	for (i = 0; cmd->mii_reg != miim_end; i++) {
		/* The command is a read if mii_data is miim_read */
		if (cmd->mii_data == miim_read) {
			/* Read the value of the PHY reg */
			result = read_phy_reg(dev, cmd->mii_reg);

			/* If a function was supplied, we need to let it process */
			/* the result. */
			if (cmd->funct != NULL)
				(*(cmd->funct)) (result, dev);
		} else {	/* Otherwise, it's a write */
			/* If a function was supplied, it will provide 
			 * the value to write */
			/* Otherwise, the value was supplied in cmd->mii_data */
			if (cmd->funct != NULL)
				result = (*(cmd->funct)) (0, dev);
			else
				result = cmd->mii_data;

			/* Write the appropriate value to the PHY reg */
			write_phy_reg(dev, cmd->mii_reg, result);
		}
		cmd++;
	}
}
