#ifndef _ASM_IA64_MMZONE_SN1_H
#define _ASM_IA64_MMZONE_SN1_H

/*
 * Copyright, 2000, Silicon Graphics, sprasad@engr.sgi.com
 */

#include <linux/config.h>

/* Maximum configuration supported by SNIA hardware. There are other
 * restrictions that may limit us to a smaller max configuration.
 */
#define MAXNODES                128
#define MAXNASIDS		128

#define CHUNKSZ                (64*1024*1024)
#define CHUNKSHIFT              26      /* 2 ^^ CHUNKSHIFT == CHUNKSZ */

extern int 	cnodeid_map[] ;
extern int	nasid_map[] ;

#define CNODEID_TO_NASID(n)	(cnodeid_map[(n)])
#define NASID_TO_CNODEID(n)     (nasid_map[(n)])

#define MAX_CHUNKS_PER_NODE     128


/*
 * These are a bunch of sn1 hw specific defines. For now, keep it 
 * in this file. If it gets too diverse we may want to create a 
 * mmhwdefs_sn1.h
 */

/*
 * Structure of the mem config of the node as a SN1 MI reg
 * Medusa supports this reg config.
 */

typedef struct node_memmap_s
{
        unsigned int    b0      :1,     /* 0 bank 0 present */
                        b1      :1,     /* 1 bank 1 present */
                        r01     :2,     /* 2-3 reserved */
                        b01size :4,     /* 4-7 Size of bank 0 and 1 */
                        b2      :1,     /* 8 bank 2 present */
                        b3      :1,     /* 9 bank 3 present */
                        r23     :2,     /* 10-11 reserved */
                        b23size :4,     /* 12-15 Size of bank 2 and 3 */
                        b4      :1,     /* 16 bank 4 present */
                        b5      :1,     /* 17 bank 5 present */
                        r45     :2,     /* 18-19 reserved */
                        b45size :4,     /* 20-23 Size of bank 4 and 5 */
                        b6      :1,     /* 24 bank 6 present */
                        b7      :1,     /* 25 bank 7 present */
                        r67     :2,     /* 26-27 reserved */
                        b67size :4;     /* 28-31 Size of bank 6 and 7 */
} node_memmap_t ;

#define GBSHIFT                 30
#define MBSHIFT                 20

/*
 * SN1 Arch defined values
 */
#define SN1_MAX_BANK_PER_NODE   8
#define SN1_BANK_PER_NODE_SHIFT 3       /* derived from SN1_MAX_BANK_PER_NODE */
#define SN1_NODE_ADDR_SHIFT     (GBSHIFT+3)             /* 8GB */
#define SN1_BANK_ADDR_SHIFT     (SN1_NODE_ADDR_SHIFT-SN1_BANK_PER_NODE_SHIFT)

#define SN1_BANK_SIZE_SHIFT     (MBSHIFT+6)     /* 64 MB */
#define SN1_MIN_BANK_SIZE_SHIFT SN1_BANK_SIZE_SHIFT

/*
 * BankSize nibble to bank size mapping
 *
 *      1 - 64 MB
 *      2 - 128 MB
 *      3 - 256 MB
 *      4 - 512 MB
 *      5 - 1024 MB (1GB)
 */

/* fixme - this macro breaks for bsize 6-8 and 0 */

#ifdef CONFIG_IA64_SGI_SN1_SIM
/* Support the medusa hack for 8M/16M/32M nodes */
#define BankSizeBytes(bsize)            ((bsize<6) ? (1<<((bsize-1)+SN1_BANK_SIZE_SHIFT)) :\
					 (1<<((bsize-9)+MBSHIFT)))
#else
#define BankSizeBytes(bsize)            (1<<((bsize-1)+SN1_BANK_SIZE_SHIFT))
#endif

#define BankSizeToEFIPages(bsize)       ((BankSizeBytes(bsize)) >> 12)

#define GetPhysAddr(n,b)                (((u64)n<<SN1_NODE_ADDR_SHIFT) | \
                                                ((u64)b<<SN1_BANK_ADDR_SHIFT))

#define GetNasId(paddr)			((u64)(paddr) >> SN1_NODE_ADDR_SHIFT)

#define GetBankId(paddr)						\
				(((u64)(paddr) >> SN1_BANK_ADDR_SHIFT) & 7)

#define SN1_MAX_BANK_SIZE		((u64)BankSizeBytes(5))
#define SN1_BANK_SIZE_MASK		(~(SN1_MAX_BANK_SIZE-1))

#endif /* _ASM_IA64_MMZONE_SN1_H */
