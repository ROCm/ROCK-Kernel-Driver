
/*
 * A collection of structures, addresses, and values associated with
 * the RPCG RPX-Classic board.  Copied from the RPX-Lite stuff.
 *
 * Copyright (c) 1998 Dan Malek (dmalek@jlc.net)
 */
#ifdef __KERNEL__
#ifndef __MACH_RPX_DEFS
#define __MACH_RPX_DEFS

#include <linux/config.h>

/* A Board Information structure that is given to a program when
 * prom starts it up.
 */
typedef struct bd_info {
	unsigned int	bi_memstart;	/* Memory start address */
	unsigned int	bi_memsize;	/* Memory (end) size in bytes */
	unsigned int	bi_intfreq;	/* Internal Freq, in Hz */
	unsigned int	bi_busfreq;	/* Bus Freq, in Hz */
	unsigned char	bi_enetaddr[6];
	unsigned int	bi_baudrate;
} bd_t;

extern bd_t m8xx_board_info;

/* Memory map is configured by the PROM startup.
 * We just map a few things we need.  The CSR is actually 4 byte-wide
 * registers that can be accessed as 8-, 16-, or 32-bit values.
 */
#define PCI_ISA_IO_ADDR		((unsigned)0x80000000)
#define PCI_ISA_IO_SIZE		((uint)(512 * 1024 * 1024))
#define PCI_ISA_MEM_ADDR	((unsigned)0xc0000000)
#define PCI_ISA_MEM_SIZE	((uint)(512 * 1024 * 1024))
#define RPX_CSR_ADDR		((uint)0xfa400000)
#define RPX_CSR_SIZE		((uint)(4 * 1024))
#define IMAP_ADDR		((uint)0xfa200000)
#define IMAP_SIZE		((uint)(64 * 1024))
#define PCI_CSR_ADDR		((uint)0x80000000)
#define PCI_CSR_SIZE		((uint)(64 * 1024))
#define PCMCIA_MEM_ADDR		((uint)0xe0000000)
#define PCMCIA_MEM_SIZE		((uint)(64 * 1024))
#define PCMCIA_IO_ADDR		((uint)0xe4000000)
#define PCMCIA_IO_SIZE		((uint)(4 * 1024))
#define PCMCIA_ATTRB_ADDR	((uint)0xe8000000)
#define PCMCIA_ATTRB_SIZE	((uint)(4 * 1024))

/* Things of interest in the CSR.
*/
#define BCSR0_ETHEN		((uint)0x80000000)
#define BCSR0_ETHLPBK		((uint)0x40000000)
#define BCSR0_COLTESTDIS	((uint)0x20000000)
#define BCSR0_FULLDPLXDIS	((uint)0x10000000)
#define BCSR0_ENFLSHSEL		((uint)0x04000000)
#define BCSR0_FLASH_SEL		((uint)0x02000000)
#define BCSR0_ENMONXCVR		((uint)0x01000000)

#define BCSR0_PCMCIAVOLT	((uint)0x000f0000)	/* CLLF */
#define BCSR0_PCMCIA3VOLT	((uint)0x000a0000)	/* CLLF */
#define BCSR0_PCMCIA5VOLT	((uint)0x00060000)	/* CLLF */

#define BCSR2_EN232XCVR		((uint)0x00008000)
#define BCSR2_QSPACESEL		((uint)0x00004000)
#define BCSR2_FETHLEDMODE	((uint)0x00000800)	/* CLLF */

#if defined(CONFIG_RPXLCD) || defined(CONFIG_HTDMSOUND)
/* HIOX Expansion card.
*/
#include <asm/rpx_hiox.h>
#endif

/* Interrupt level assignments.
*/
#define FEC_INTERRUPT	SIU_LEVEL1	/* FEC interrupt */

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

/* Machine type
*/
#define _MACH_8xx (_MACH_classic)

#endif
#endif /* __KERNEL__ */
