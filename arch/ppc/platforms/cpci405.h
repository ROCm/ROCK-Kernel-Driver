/*
 * CPCI-405 board specific definitions
 *
 * Copyright (c) 2001 Stefan Roese (stefan.roese@esd-electronics.com)
 */

#ifndef __ASM_CPCI405_H__
#define __ASM_CPCI405_H__

#include <linux/config.h>

/* We have a 405GP core */
#include <platforms/ibm405gp.h>

#include <asm/ppcboot.h>

/* Some 4xx parts use a different timebase frequency from the internal clock.
*/
#define bi_tbfreq bi_intfreq

/* Early initialization address mapping for block_io.
 * Standard 405GP map.
 */
#define PPC4xx_PCI_IO_PADDR	((uint)PPC405_PCI_PHY_IO_BASE)
#define PPC4xx_PCI_IO_VADDR	PPC4xx_PCI_IO_PADDR
#define PPC4xx_PCI_IO_SIZE	((uint)64*1024)
#define PPC4xx_PCI_CFG_PADDR	((uint)PPC405_PCI_CONFIG_ADDR)
#define PPC4xx_PCI_CFG_VADDR	PPC4xx_PCI_CFG_PADDR
#define PPC4xx_PCI_CFG_SIZE	((uint)4*1024)
#define PPC4xx_PCI_LCFG_PADDR	((uint)0xef400000)
#define PPC4xx_PCI_LCFG_VADDR	PPC4xx_PCI_LCFG_PADDR
#define PPC4xx_PCI_LCFG_SIZE	((uint)4*1024)
#define PPC4xx_ONB_IO_PADDR	((uint)0xef600000)
#define PPC4xx_ONB_IO_VADDR	PPC4xx_ONB_IO_PADDR
#define PPC4xx_ONB_IO_SIZE	((uint)4*1024)

#ifdef CONFIG_PPC405GP_INTERNAL_CLOCK
#define BASE_BAUD		201600
#else
#define BASE_BAUD		691200
#endif

#define PPC4xx_MACHINE_NAME "esd CPCI-405"

#endif	/* __ASM_CPCI405_H__ */
