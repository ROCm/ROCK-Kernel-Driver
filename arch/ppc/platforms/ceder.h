/*
 *
 *
 *    Copyright 2000 MontaVista Software Inc.
 * 	Author: MontaVista Software, Inc.
 *         	akuster@mvista.com or source@mvista.com
 *
 *    Module name: ceder.h
 *
 *    Description:
 *      Macros, definitions, and data structures specific to the IBM PowerPC
 *      Ceder eval board.
 *
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_CEDER_H__
#define __ASM_CEDER_H__
#include <platforms/ibmnp405l.h>

#ifndef __ASSEMBLY__
/*
 * Data structure defining board information maintained by the boot
 * ROM on IBM's "Ceder" evaluation board. An effort has been made to
 * keep the field names consistent with the 8xx 'bd_t' board info
 * structures.
 */

typedef struct board_info {
	unsigned char	 bi_s_version[4];	/* Version of this structure */
	unsigned char	 bi_r_version[30];	/* Version of the IBM ROM */
	unsigned int	 bi_memsize;		/* DRAM installed, in bytes */
	unsigned char	 bi_enetaddr[EMAC_NUMS][6];	/* Local Ethernet MAC address */
	unsigned char	 bi_pci_mac[6];
	unsigned int	 bi_intfreq;		/* Processor speed, in Hz */
	unsigned int	 bi_busfreq;		/* PLB Bus speed, in Hz */
	unsigned int	 bi_pci_busfreq;	/* PCI speed in Hz */
} bd_t;

/* Some 4xx parts use a different timebase frequency from the internal clock.
*/
#define bi_tbfreq bi_intfreq

/* Memory map for the IBM "Ceder" NP405 evaluation board.
 */

extern  void *ceder_rtc_base;
#define CEDER_RTC_PADDR		((uint)0xf0000000)
#define CEDER_RTC_VADDR		CEDER_RTC_PADDR
#define CEDER_RTC_SIZE		((uint)8*1024)


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

#define NR_BOARD_IRQS 32

#ifdef CONFIG_PPC405GP_INTERNAL_CLOCK
#define BASE_BAUD		201600
#else
#define BASE_BAUD		691200
#endif

#define PPC4xx_MACHINE_NAME "IBM NP405L Ceder"

extern char pci_irq_table[][4];


#endif /* !__ASSEMBLY__ */
#endif /* __ASM_CEDER_H__ */
#endif /* __KERNEL__ */
