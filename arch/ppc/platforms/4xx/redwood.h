/*
 *    Copyright 2001 MontaVista Software Inc.
 *	PPC405 modifications
 * 	Author: MontaVista Software, Inc.
 *         	frank_rowand@mvista.com or source@mvista.com
 *
 *    Module name: redwood.h
 *
 *    Description:
 *      Macros, definitions, and data structures specific to the IBM PowerPC
 *      STB03xxx "Redwood" evaluation board.
 */

#ifdef __KERNEL__
#ifndef __ASM_REDWOOD_H__
#define __ASM_REDWOOD_H__

/* Redwoods have an STB03xxx or STB04xxx core */
#include <platforms/ibmstb3.h>

#ifndef __ASSEMBLY__
typedef struct board_info {
	unsigned char	bi_s_version[4];	/* Version of this structure */
	unsigned char	bi_r_version[30];	/* Version of the IBM ROM */
	unsigned int	bi_memsize;		/* DRAM installed, in bytes */
	unsigned int	bi_dummy;		/* field shouldn't exist */
	unsigned char	bi_enetaddr[6];		/* Ethernet MAC address */
	unsigned int	bi_intfreq;		/* Processor speed, in Hz */
	unsigned int	bi_busfreq;		/* Bus speed, in Hz */
	unsigned int	bi_tbfreq;		/* Software timebase freq */
} bd_t;
#endif /* !__ASSEMBLY__ */

#define OAKNET_IO_PADDR		((uint)0xf2000000)
#define OAKNET_IO_VADDR		OAKNET_IO_PADDR
#define OAKNET_IO_BASE		OAKNET_IO_VADDR

/* ftr revisit- io size was 0xffff in old-line, is 0x40 in oak.h */
#define OAKNET_IO_SIZE		0xffff
#define OAKNET_INT		26	/* EXTINT1 */

#define _IO_BASE	0
#define _ISA_MEM_BASE	0
#define PCI_DRAM_OFFSET	0

#define BASE_BAUD		1312500

#define PPC4xx_MACHINE_NAME	"IBM Redwood"

#endif /* __ASM_REDWOOD_H__ */
#endif /* __KERNEL__ */
