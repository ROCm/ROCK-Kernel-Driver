/*
 *    Copyright 2000 MontaVista Software Inc.
 *    	http://www.mvista.com
 *	<mlocke@mvista.com>
 *
 *         Embedded Planet 405GP board
 *         http://www.embeddedplanet.com
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_EP405_H__
#define __ASM_EP405_H__

/* We have a 405GP core */
#include <platforms/ibm405gp.h>

#ifndef __ASSEMBLY__
typedef struct board_info {
	unsigned int	 bi_memsize;		/* DRAM installed, in bytes */
	unsigned char	 bi_enetaddr[6];	/* Local Ethernet MAC address */
	unsigned int	 bi_intfreq;		/* Processor speed, in Hz */
	unsigned int	 bi_busfreq;		/* PLB Bus speed, in Hz */
	unsigned int	 bi_pci_busfreq;	/* PCI Bus speed, in Hz */
} bd_t;

/* Some 4xx parts use a different timebase frequency from the internal clock.
*/
#define bi_tbfreq bi_intfreq

extern void *ep405_bcsr;
extern void *ep405_nvram;

/* Map for the BCSR and NVRAM space */
#define EP405_BCSR_PADDR	((uint)0xf4000000)
#define EP405_BCSR_SIZE		((uint)16)
#define EP405_NVRAM_PADDR	((uint)0xf4200000)
/* FIXME: what if the board has something other than 512k NVRAM */
#define EP405_NVRAM_SIZE	((uint)512*1024)

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

/* serial defines */
#define BASE_BAUD		399193

#define PPC4xx_MACHINE_NAME "Embedded Planet 405GP"

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_EP405_H__ */
#endif /* __KERNEL__ */
