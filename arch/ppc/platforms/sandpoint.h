/*
 * arch/ppc/platforms/sandpoint.h
 * 
 * Definitions for Motorola SPS Sandpoint Test Platform
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * Sandpoint uses the CHRP map (Map B).
 */

#ifndef __PPC_PLATFORMS_SANDPOINT_H
#define __PPC_PLATFORMS_SANDPOINT_H

#ifdef CONFIG_SANDPOINT_X3
#define SANDPOINT_SIO_SLOT      0	/* Cascaded from EPIC IRQ 0 */
#if 0
/* The Sandpoint X3 allows the IDE interrupt to be directly connected
 * from the Windbond (PCI INTC or INTD) to the serial EPIC.  Someday
 * we should try this, but it was easier to use the existing 83c553
 * initialization than change it to route the different interrupts :-).
 *	-- Dan
 */
#define SANDPOINT_IDE_INT0	23	/* EPIC 7 */
#define SANDPOINT_IDE_INT1	24	/* EPIC 8 */
#else
#define SANDPOINT_IDE_INT0	14	/* 8259 Test */
#define SANDPOINT_IDE_INT1	15	/* 8259 Test */
#endif
#else
 /*
  * Define the PCI slot that the 8259 is sharing interrupts with.
  * Valid values are 1 (PCI slot 2) and 2 (PCI slot 3).
  */
#define SANDPOINT_SIO_SLOT      1

/* ...and for the IDE from the 8259....
*/
#define SANDPOINT_IDE_INT0	14
#define SANDPOINT_IDE_INT1	15
#endif

#define	SANDPOINT_SIO_IRQ	(SANDPOINT_SIO_SLOT + NUM_8259_INTERRUPTS)

/*
 * The sandpoint boards have processor modules that either have an 8240 or
 * an MPC107 host bridge on them.  These bridges have an IDSEL line that allows
 * them to respond to PCI transactions as if they were a normal PCI devices.
 * However, the processor on the processor side of the bridge can not reach
 * out onto the PCI bus and then select the bridge or bad things will happen
 * (documented in the 8240 and 107 manuals).
 * Because of this, we always skip the bridge PCI device when accessing the
 * PCI bus.  The PCI slot that the bridge occupies is defined by the macro
 * below.
 */
#define SANDPOINT_HOST_BRIDGE_IDSEL     12


void sandpoint_find_bridges(void);

#endif /* __PPC_PLATFORMS_SANDPOINT_H */
