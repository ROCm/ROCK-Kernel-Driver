/*
 * arch/ppc/platforms/4xx/redwood.h
 *
 * Macros, definitions, and data structures specific to the IBM PowerPC
 * STB03xxx "Redwood" evaluation board.
 *
 * Author: Frank Rowand <frank_rowand@mvista.com>, or source@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_REDWOOD_H__
#define __ASM_REDWOOD_H__

/* Redwoods have an STB03xxx or STB04xxx core */
#include <platforms/4xx/ibmstb3.h>

#ifndef __ASSEMBLY__
typedef struct board_info {
	unsigned char	bi_s_version[4];	/* Version of this structure */
	unsigned char	bi_r_version[30];	/* Version of the IBM ROM */
	unsigned int	bi_memsize;		/* DRAM installed, in bytes */
	unsigned int	bi_dummy;		/* field shouldn't exist */
	unsigned char	bi_enetaddr[6];		/* Ethernet MAC address */
	unsigned int	bi_intfreq;		/* Processor speed, in Hz */
	unsigned int	bi_busfreq;		/* Bus speed, in Hz */
} bd_t;
#define bi_tbfreq bi_intfreq

#endif /* !__ASSEMBLY__ */

#define bi_tbfreq bi_intfreq
#define OAKNET_IO_PADDR		((uint)0xf2000000)
#define OAKNET_IO_VADDR		OAKNET_IO_PADDR
#define OAKNET_IO_BASE		OAKNET_IO_VADDR

/* ftr revisit- io size was 0xffff in old-line, is 0x40 in oak.h */
#define OAKNET_IO_SIZE		0xffff
#define OAKNET_INT		26	/* EXTINT1 */

#define IDE_XLINUX_MUX_BASE        0xf2040000
#define IDE_DMA_ADDR	0xfce00000

#define _IO_BASE	0
#define _ISA_MEM_BASE	0
#define PCI_DRAM_OFFSET	0

#define BASE_BAUD		(378000000 / 18 / 16)

#define PPC4xx_MACHINE_NAME	"IBM Redwood"

#endif /* __ASM_REDWOOD_H__ */
#endif /* __KERNEL__ */
