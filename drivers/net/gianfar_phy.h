/* 
 * drivers/net/gianfar_phy.h
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
#ifndef __GIANFAR_PHY_H
#define __GIANFAR_PHY_H

#define miim_end ((u32)-2)
#define miim_read ((u32)-1)

#define MIIMIND_BUSY            0x00000001
#define MIIMIND_NOTVALID        0x00000004

#define MIIM_CONTROL		0x00
#define MIIM_CONTROL_RESET	0x00008000
#define MIIM_CONTROL_INIT	0x00001140
#define MIIM_ANEN		0x00001000

#define MIIM_CR                 0x00
#define MIIM_CR_RST		0x00008000
#define MIIM_CR_INIT	        0x00001000

#define MIIM_STATUS		0x1
#define MIIM_STATUS_AN_DONE	0x00000020
#define MIIM_STATUS_LINK	0x0004

#define MIIM_PHYIR1		0x2
#define MIIM_PHYIR2		0x3

#define GFAR_AN_TIMEOUT         0x000fffff

#define MIIM_ANLPBPA	0x5
#define MIIM_ANLPBPA_HALF	0x00000040
#define MIIM_ANLPBPA_FULL	0x00000020

#define MIIM_ANEX		0x6
#define MIIM_ANEX_NP    	0x00000004
#define MIIM_ANEX_PRX   	0x00000002


/* Cicada Extended Control Register 1 */
#define MIIM_CIS8201_EXT_CON1           0x17
#define MIIM_CIS8201_EXTCON1_INIT       0x0000

/* Cicada Interrupt Mask Register */
#define MIIM_CIS8204_IMASK		0x19
#define MIIM_CIS8204_IMASK_IEN		0x8000
#define MIIM_CIS8204_IMASK_SPEED	0x4000
#define MIIM_CIS8204_IMASK_LINK		0x2000
#define MIIM_CIS8204_IMASK_DUPLEX	0x1000
#define MIIM_CIS8204_IMASK_MASK		0xf000

/* Cicada Interrupt Status Register */
#define MIIM_CIS8204_ISTAT		0x1a
#define MIIM_CIS8204_ISTAT_STATUS	0x8000
#define MIIM_CIS8204_ISTAT_SPEED	0x4000
#define MIIM_CIS8204_ISTAT_LINK		0x2000
#define MIIM_CIS8204_ISTAT_DUPLEX	0x1000

/* Cicada Auxiliary Control/Status Register */
#define MIIM_CIS8201_AUX_CONSTAT        0x1c
#define MIIM_CIS8201_AUXCONSTAT_INIT    0x0004
#define MIIM_CIS8201_AUXCONSTAT_DUPLEX  0x0020
#define MIIM_CIS8201_AUXCONSTAT_SPEED   0x0018
#define MIIM_CIS8201_AUXCONSTAT_GBIT    0x0010
#define MIIM_CIS8201_AUXCONSTAT_100     0x0008
                                                                                
/* 88E1011 PHY Status Register */
#define MIIM_88E1011_PHY_STATUS         0x11
#define MIIM_88E1011_PHYSTAT_SPEED      0xc000
#define MIIM_88E1011_PHYSTAT_GBIT       0x8000
#define MIIM_88E1011_PHYSTAT_100        0x4000
#define MIIM_88E1011_PHYSTAT_DUPLEX     0x2000
#define MIIM_88E1011_PHYSTAT_LINK	0x0400

#define MIIM_88E1011_IEVENT		0x13
#define MIIM_88E1011_IEVENT_CLEAR	0x0000

#define MIIM_88E1011_IMASK		0x12
#define MIIM_88E1011_IMASK_INIT		0x6400
#define MIIM_88E1011_IMASK_CLEAR	0x0000

/* DM9161 Control register values */
#define MIIM_DM9161_CR_STOP	0x0400
#define MIIM_DM9161_CR_RSTAN	0x1200

#define MIIM_DM9161_SCR		0x10
#define MIIM_DM9161_SCR_INIT	0x0610

/* DM9161 Specified Configuration and Status Register */
#define MIIM_DM9161_SCSR	0x11
#define MIIM_DM9161_SCSR_100F	0x8000
#define MIIM_DM9161_SCSR_100H	0x4000
#define MIIM_DM9161_SCSR_10F	0x2000
#define MIIM_DM9161_SCSR_10H	0x1000

/* DM9161 Interrupt Register */
#define MIIM_DM9161_INTR	0x15
#define MIIM_DM9161_INTR_PEND		0x8000
#define MIIM_DM9161_INTR_DPLX_MASK	0x0800
#define MIIM_DM9161_INTR_SPD_MASK	0x0400
#define MIIM_DM9161_INTR_LINK_MASK	0x0200
#define MIIM_DM9161_INTR_MASK		0x0100
#define MIIM_DM9161_INTR_DPLX_CHANGE	0x0010
#define MIIM_DM9161_INTR_SPD_CHANGE	0x0008
#define MIIM_DM9161_INTR_LINK_CHANGE	0x0004
#define MIIM_DM9161_INTR_INIT 		0x0000
#define MIIM_DM9161_INTR_STOP	\
(MIIM_DM9161_INTR_DPLX_MASK | MIIM_DM9161_INTR_SPD_MASK \
 | MIIM_DM9161_INTR_LINK_MASK | MIIM_DM9161_INTR_MASK)

/* DM9161 10BT Configuration/Status */
#define MIIM_DM9161_10BTCSR	0x12
#define MIIM_DM9161_10BTCSR_INIT	0x7800


#define MIIM_READ_COMMAND       0x00000001

/*
 * struct phy_cmd:  A command for reading or writing a PHY register
 *
 * mii_reg:  The register to read or write
 *
 * mii_data:  For writes, the value to put in the register.
 * 	A value of -1 indicates this is a read.
 *
 * funct: A function pointer which is invoked for each command.
 * 	For reads, this function will be passed the value read
 *	from the PHY, and process it.
 *	For writes, the result of this function will be written
 *	to the PHY register
 */
struct phy_cmd {
    u32 mii_reg;
    u32 mii_data;
    u16 (*funct) (u16 mii_reg, struct net_device * dev);
};

/* struct phy_info: a structure which defines attributes for a PHY
 *
 * id will contain a number which represents the PHY.  During
 * startup, the driver will poll the PHY to find out what its
 * UID--as defined by registers 2 and 3--is.  The 32-bit result
 * gotten from the PHY will be shifted right by "shift" bits to
 * discard any bits which may change based on revision numbers
 * unimportant to functionality
 *
 * The struct phy_cmd entries represent pointers to an arrays of
 * commands which tell the driver what to do to the PHY.
 */
struct phy_info {
    u32 id;
    char *name;
    unsigned int shift;
    /* Called to configure the PHY, and modify the controller
     * based on the results */
    const struct phy_cmd *config;

    /* Called when starting up the controller.  Usually sets
     * up the interrupt for state changes */
    const struct phy_cmd *startup;

    /* Called inside the interrupt handler to acknowledge
     * the interrupt */
    const struct phy_cmd *ack_int;

    /* Called in the bottom half to handle the interrupt */
    const struct phy_cmd *handle_int;

    /* Called when bringing down the controller.  Usually stops
     * the interrupts from being generated */
    const struct phy_cmd *shutdown;
};

struct phy_info *get_phy_info(struct net_device *dev);
void phy_run_commands(struct net_device *dev, const struct phy_cmd *cmd);

#endif /* GIANFAR_PHY_H */
