
/*
 * A collection of structures, addresses, and values associated with
 * the Motorola 860T FADS board.  Copied from the MBX stuff.
 *
 * Copyright (c) 1998 Dan Malek (dmalek@jlc.net)
 */
#ifndef __MACH_FADS_DEFS
#define __MACH_FADS_DEFS

/* A Board Information structure that is given to a program when
 * prom starts it up.
 */
typedef struct bd_info {
	unsigned int	bi_memstart;	/* Memory start address */
	unsigned int	bi_memsize;	/* Memory (end) size in bytes */
	unsigned int	bi_intfreq;	/* Internal Freq, in Hz */
	unsigned int	bi_busfreq;	/* Bus Freq, in Hz */
} bd_t;

extern bd_t m8xx_board_info;

/* Memory map is configured by the PROM startup.
 * I tried to follow the FADS manual, although the startup PROM
 * dictates this.
 */
#define BCSR_ADDR		((uint)0x02100000)
#define BCSR_SIZE		((uint)(64 * 1024))
#define	BCSR0			((uint)0x02100000)
#define	BCSR1			((uint)0x02100004)
#define	BCSR2			((uint)0x02100008)
#define	BCSR3			((uint)0x0210000c)
#define	BCSR4			((uint)0x02100010)
#define IMAP_ADDR		((uint)0x02200000)
#define IMAP_SIZE		((uint)(64 * 1024))
#define PCMCIA_MEM_ADDR		((uint)0x04000000)
#define PCMCIA_MEM_SIZE		((uint)(64 * 1024))

#endif
