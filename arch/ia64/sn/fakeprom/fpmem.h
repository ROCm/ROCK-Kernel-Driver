/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc.  All rights reserved.
 */

#include <linux/config.h>

/*
 * Structure of the mem config of the node as a SN1 MI reg
 * Medusa supports this reg config.
 *
 * BankSize nibble to bank size mapping
 *
 *      1 - 64 MB
 *      2 - 128 MB
 *      3 - 256 MB
 *      4 - 512 MB
 *      5 - 1024 MB (1GB)
 */

#define MBSHIFT				20

#ifdef CONFIG_IA64_SGI_SN1
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

/* Support the medusa hack for 8M/16M/32M nodes */
#define SN1_BANK_SIZE_SHIFT		(MBSHIFT+6)     /* 64 MB */
#define BankSizeBytes(bsize)            ((bsize<6) ? (1<<((bsize-1)+SN1_BANK_SIZE_SHIFT)) :\
                                         (1<<((bsize-9)+MBSHIFT)))
#else
typedef struct node_memmap_s
{
        unsigned int    b0size  :3,     /* 0-2   bank 0 size */
                        b0dou   :1,     /* 3     bank 0 is 2-sided */
                        ena0    :1,     /* 4     bank 0 enabled */
                        r0      :3,     /* 5-7   reserved */
        		b1size  :3,     /* 8-10  bank 1 size */
                        b1dou   :1,     /* 11    bank 1 is 2-sided */
                        ena1    :1,     /* 12    bank 1 enabled */
                        r1      :3,     /* 13-15 reserved */
        		b2size  :3,     /* 16-18 bank 2 size */
                        b2dou   :1,     /* 19    bank 1 is 2-sided */
                        ena2    :1,     /* 20    bank 2 enabled */
                        r2      :3,     /* 21-23 reserved */
        		b3size  :3,     /* 24-26 bank 3 size */
                        b3dou   :1,     /* 27    bank 3 is 2-sided */
                        ena3    :1,     /* 28    bank 3 enabled */
                        r3      :3;     /* 29-31 reserved */
} node_memmap_t ;

#define SN2_BANK_SIZE_SHIFT		(MBSHIFT+6)     /* 64 MB */
#define BankPresent(bsize)		(bsize<6)
#define BankSizeBytes(bsize)            (BankPresent(bsize) ? 1UL<<((bsize)+SN2_BANK_SIZE_SHIFT) : 0)
#endif

typedef struct sn_memmap_s
{
	short		nasid ;
	short		cpuconfig;
	node_memmap_t 	node_memmap ;
} sn_memmap_t ;

typedef struct sn_config_s
{
	int		cpus;
	int		nodes;
	sn_memmap_t	memmap[1];		/* start of array */
} sn_config_t;



extern void build_init(unsigned long);
extern int build_efi_memmap(void *, int);
extern int GetNumNodes(void);
extern int GetNumCpus(void);
extern int IsCpuPresent(int, int);
extern int GetNasid(int);
