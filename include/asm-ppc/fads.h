
/*
 * A collection of structures, addresses, and values associated with
 * the Motorola 860T FADS board.  Copied from the MBX stuff.
 *
 * Copyright (c) 1998 Dan Malek (dmalek@jlc.net)
 */
#ifndef __MACH_FADS_DEFS
#define __MACH_FADS_DEFS

#ifndef __ASSEMBLY__
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
 * I tried to follow the FADS manual, although the startup PROM
 * dictates this and we simply have to move some of the physical
 * addresses for Linux.
 */
#define BCSR_ADDR		((uint)0xfe000000)
#define BCSR_SIZE		((uint)(64 * 1024))
#define	BCSR0			((uint)0xfe000000)
#define	BCSR1			((uint)0xfe000004)
#define	BCSR2			((uint)0xfe000008)
#define	BCSR3			((uint)0xfe00000c)
#define	BCSR4			((uint)0xfe000010)
#define IMAP_ADDR		((uint)0xf0000000)
#define IMAP_SIZE		((uint)(64 * 1024))
#define PCMCIA_MEM_ADDR		((uint)0x04000000)
#define PCMCIA_MEM_SIZE		((uint)(64 * 1024))
 
/* Bits of interest in the BCSRs.
 */
#define BCSR1_ETHEN		((uint)0x20000000)
#define BCSR1_RS232EN_1		((uint)0x01000000)
#define BCSR1_RS232EN_2		((uint)0x00040000)
#define BCSR4_ETHLOOP		((uint)0x80000000)	/* EEST Loopback */
#define BCSR4_EEFDX		((uint)0x40000000)	/* EEST FDX enable */
#define BCSR4_FETH_EN		((uint)0x08000000)	/* PHY enable */
#define BCSR4_FETHCFG0		((uint)0x04000000)	/* PHY autoneg mode */
#define BCSR4_FETHCFG1		((uint)0x00400000)	/* PHY autoneg mode */
#define BCSR4_FETHFDE		((uint)0x02000000)	/* PHY FDX advertise */
#define BCSR4_FETHRST		((uint)0x00200000)	/* PHY Reset */
#endif /* !__ASSEMBLY__ */
 
/* Interrupt level assignments.
 */
#define FEC_INTERRUPT	SIU_LEVEL1	/* FEC interrupt */
#define PHY_INTERRUPT	SIU_IRQ2	/* PHY link change interrupt */

/* We don't use the 8259.
 */
#define NR_8259_INTS	0
 
/* Machine type
 */
#define _MACH_8xx (_MACH_fads)

#endif
