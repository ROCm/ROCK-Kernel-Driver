/* $Id: sgimc.h,v 1.1.1.1 1997/06/01 03:17:13 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * sgimc.h: Definitions for memory controller hardware found on
 *          SGI IP20, IP22, IP26, and IP28 machines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_SGI_SGIMC_H
#define _ASM_SGI_SGIMC_H

struct sgimc_misc_ctrl {
	u32 _unused1;
	volatile u32 cpuctrl0;     /* CPU control register 0, readwrite */
#define SGIMC_CCTRL0_REFS         0x0000000f /* REFS mask */
#define SGIMC_CCTRL0_EREFRESH     0x00000010 /* Memory refresh enable */
#define SGIMC_CCTRL0_EPERRGIO     0x00000020 /* GIO parity error enable */
#define SGIMC_CCTRL0_EPERRMEM     0x00000040 /* Main mem parity error enable */
#define SGIMC_CCTRL0_EPERRCPU     0x00000080 /* CPU bus parity error enable */
#define SGIMC_CCTRL0_WDOG         0x00000100 /* Watchdog timer enable */
#define SGIMC_CCTRL0_SYSINIT      0x00000200 /* System init bit */
#define SGIMC_CCTRL0_GFXRESET     0x00000400 /* Graphics interface reset */
#define SGIMC_CCTRL0_EISALOCK     0x00000800 /* Lock CPU from memory for EISA */
#define SGIMC_CCTRL0_EPERRSCMD    0x00001000 /* SysCMD bus parity error enable */
#define SGIMC_CCTRL0_IENAB        0x00002000 /* Allow interrupts from MC */
#define SGIMC_CCTRL0_ESNOOP       0x00004000 /* Snooping I/O enable */
#define SGIMC_CCTRL0_EPROMWR      0x00008000 /* Prom writes from cpu enable */
#define SGIMC_CCTRL0_WRESETPMEM   0x00010000 /* Perform warm reset, preserves mem */
#define SGIMC_CCTRL0_LENDIAN      0x00020000 /* Put MC in little-endian mode */
#define SGIMC_CCTRL0_WRESETDMEM   0x00040000 /* Warm reset, destroys mem contents */
#define SGIMC_CCTRL0_CMEMBADPAR   0x02000000 /* Generate bad perr from cpu to mem */
#define SGIMC_CCTRL0_R4KNOCHKPARR 0x04000000 /* Don't chk parity on mem data reads */
#define SGIMC_CCTRL0_GIOBTOB      0x08000000 /* Allow GIO back to back writes */

	u32 _unused2;
	volatile u32 cpuctrl1;     /* CPU control register 1, readwrite */
#define SGIMC_CCTRL1_EGIOTIMEO    0x00000010 /* GIO bus timeout enable */
#define SGIMC_CCTRL1_FIXEDEHPC    0x00001000 /* Fixed HPC endianness */
#define SGIMC_CCTRL1_LITTLEHPC    0x00002000 /* Little endian HPC */
#define SGIMC_CCTRL1_FIXEDEEXP0   0x00004000 /* Fixed EXP0 endianness */
#define SGIMC_CCTRL1_LITTLEEXP0   0x00008000 /* Little endian EXP0 */
#define SGIMC_CCTRL1_FIXEDEEXP1   0x00010000 /* Fixed EXP1 endianness */
#define SGIMC_CCTRL1_LITTLEEXP1   0x00020000 /* Little endian EXP1 */

	u32 _unused3;
	volatile u32 watchdogt;    /* Watchdog reg rdonly, write clears */

	u32 _unused4;
	volatile u32 systemid;     /* MC system ID register, readonly */
#define SGIMC_SYSID_MASKREV       0x0000000f /* Revision of MC controller */
#define SGIMC_SYSID_EPRESENT      0x00000010 /* Indicates presence of EISA bus */

	u32 _unused5[3];
	volatile u32 divider;      /* Divider reg for RPSS */

	u32 _unused6;
	volatile unsigned char eeprom;       /* EEPROM byte reg for r4k */
#define SGIMC_EEPROM_PRE          0x00000001 /* eeprom chip PRE pin assertion */
#define SGIMC_EEPROM_CSEL         0x00000002 /* Active high, eeprom chip select */
#define SGIMC_EEPROM_SECLOCK      0x00000004 /* EEPROM serial clock */
#define SGIMC_EEPROM_SDATAO       0x00000008 /* Serial EEPROM data-out */
#define SGIMC_EEPROM_SDATAI       0x00000010 /* Serial EEPROM data-in */

	unsigned char _unused7[3];
	u32 _unused8[3];
	volatile unsigned short rcntpre;     /* Preload refresh counter */

	unsigned short _unused9;
	u32 _unused9a;
	volatile unsigned short rcounter;    /* Readonly refresh counter */

	unsigned short _unused10;
	u32 _unused11[13];
	volatile u32 gioparm;      /* Parameter word for GIO64 */
#define SGIMC_GIOPARM_HPC64       0x00000001 /* HPC talks to GIO using 64-bits */
#define SGIMC_GIOPARM_GFX64       0x00000002 /* GFX talks to GIO using 64-bits */
#define SGIMC_GIOPARM_EXP064      0x00000004 /* EXP(slot0) talks using 64-bits */
#define SGIMC_GIOPARM_EXP164      0x00000008 /* EXP(slot1) talks using 64-bits */
#define SGIMC_GIOPARM_EISA64      0x00000010 /* EISA bus talks 64-bits to GIO */
#define SGIMC_GIOPARM_HPC264      0x00000020 /* 2nd HPX talks 64-bits to GIO */
#define SGIMC_GIOPARM_RTIMEGFX    0x00000040 /* GFX device has realtime attr */
#define SGIMC_GIOPARM_RTIMEEXP0   0x00000080 /* EXP(slot0) has realtime attr */
#define SGIMC_GIOPARM_RTIMEEXP1   0x00000100 /* EXP(slot1) has realtime attr */
#define SGIMC_GIOPARM_MASTEREISA  0x00000200 /* EISA bus can act as bus master */
#define SGIMC_GIOPARM_ONEBUS      0x00000400 /* Exists one GIO64 pipelined bus */
#define SGIMC_GIOPARM_MASTERGFX   0x00000800 /* GFX can act as a bus master */
#define SGIMC_GIOPARM_MASTEREXP0  0x00001000 /* EXP(slot0) can bus master */
#define SGIMC_GIOPARM_MASTEREXP1  0x00002000 /* EXP(slot1) can bus master */
#define SGIMC_GIOPARM_PLINEEXP0   0x00004000 /* EXP(slot0) has pipeline attr */
#define SGIMC_GIOPARM_PLINEEXP1   0x00008000 /* EXP(slot1) has pipeline attr */

	u32 _unused13;
	volatile unsigned short cputp;       /* CPU bus arb time period */

	unsigned short _unused14;
	u32 _unused15[3];
	volatile unsigned short lbursttp;    /* Time period for long bursts */

	unsigned short _unused16;
	u32 _unused17[9];
	volatile u32 mconfig0;     /* Memory config register zero */
	u32 _unused18;
	volatile u32 mconfig1;     /* Memory config register one */

        /* These defines apply to both mconfig registers above. */
#define SGIMC_MCONFIG_FOURMB     0x00000000  /* Physical ram = 4megs */
#define SGIMC_MCONFIG_EIGHTMB    0x00000100  /* Physical ram = 8megs */
#define SGIMC_MCONFIG_SXTEENMB   0x00000300  /* Physical ram = 16megs */
#define SGIMC_MCONFIG_TTWOMB     0x00000700  /* Physical ram = 32megs */
#define SGIMC_MCONFIG_SFOURMB    0x00000f00  /* Physical ram = 64megs */
#define SGIMC_MCONFIG_OTEIGHTMB  0x00001f00  /* Physical ram = 128megs */
#define SGIMC_MCONFIG_RMASK      0x00001f00  /* Ram config bitmask */

	u32 _unused19;
	volatile u32 cmacc;        /* Mem access config for CPU */
	u32 _unused20;
	volatile u32 gmacc;        /* Mem access config for GIO */

	/* This define applies to both cmacc and gmacc registers above. */
#define SGIMC_MACC_ALIASBIG       0x20000000 /* 512MB home for alias */

	/* Error address/status regs from GIO and CPU perspectives. */
	u32 _unused21;
	volatile u32 cerr;         /* Error address reg for CPU */
	u32 _unused22;
	volatile u32 cstat;        /* Status reg for CPU */
	u32 _unused23;
	volatile u32 gerr;         /* Error address reg for GIO */
	u32 _unused24;
	volatile u32 gstat;        /* Status reg for GIO */

	/* Special hard bus locking registers. */
	u32 _unused25;
	volatile unsigned char syssembit;    /* Uni-bit system semaphore */
	unsigned char _unused26[3];
	u32 _unused27;
	volatile unsigned char mlock;        /* Global GIO memory access lock */
	unsigned char _unused28[3];
	u32 _unused29;
	volatile unsigned char elock;        /* Locks EISA from GIO accesses */

	/* GIO dma control registers. */
	unsigned char _unused30[3];
	u32 _unused31[14];
	volatile u32 gio_dma_trans;/* DMA mask to translation GIO addrs */
	u32 _unused32;
	volatile u32 gio_dma_sbits;/* DMA GIO addr substitution bits */
	u32 _unused33;
	volatile u32 dma_intr_cause; /* DMA IRQ cause indicator bits */
	u32 _unused34;
	volatile u32 dma_ctrl;     /* Main DMA control reg */

	/* DMA TLB entry 0 */
	u32 _unused35;
	volatile u32 dtlb_hi0;
	u32 _unused36;
	volatile u32 dtlb_lo0;

	/* DMA TLB entry 1 */
	u32 _unused37;
	volatile u32 dtlb_hi1;
	u32 _unused38;
	volatile u32 dtlb_lo1;

	/* DMA TLB entry 2 */
	u32 _unused39;
	volatile u32 dtlb_hi2;
	u32 _unused40;
	volatile u32 dtlb_lo2;

	/* DMA TLB entry 3 */
	u32 _unused41;
	volatile u32 dtlb_hi3;
	u32 _unused42;
	volatile u32 dtlb_lo3;
};

/* MC misc control registers live at physical 0x1fa00000. */
extern struct sgimc_misc_ctrl *mcmisc_regs;
extern u32 *rpsscounter;          /* Chirps at 100ns */

struct sgimc_dma_ctrl {
	u32 _unused1;
	volatile u32 maddronly;   /* Address DMA goes at */
	u32 _unused2;
	volatile u32 maddrpdeflts; /* Same as above, plus set defaults */
	u32 _unused3;
	volatile u32 dmasz;       /* DMA count */
	u32 _unused4;
	volatile u32 ssize;       /* DMA stride size */
	u32 _unused5;
	volatile u32 gmaddronly;  /* Set GIO DMA but do not start trans */
	u32 _unused6;
	volatile u32 dmaddnpgo;   /* Set GIO DMA addr + start transfer */
	u32 _unused7;
	volatile u32 dmamode;     /* DMA mode config bit settings */
	u32 _unused8;
	volatile u32 dmacount;    /* Zoom and byte count for DMA */
	u32 _unused9;
	volatile u32 dmastart;    /* Pedal to the metal. */
	u32 _unused10;
	volatile u32 dmarunning;  /* DMA op is in progress */
	u32 _unused11;

	/* Set dma addr, defaults, and kick it */
	volatile u32 maddr_defl_go; /* go go go! -lm */
};

/* MC controller dma regs live at physical 0x1fa02000. */
extern struct sgimc_dma_ctrl *dmactrlregs;

/* Base location of the two ram banks found in IP2[0268] machines. */
#define SGIMC_SEG0_BADDR     0x08000000
#define SGIMC_SEG1_BADDR     0x20000000

/* Maximum size of the above banks are per machine. */
extern u32 sgimc_seg0_size, sgimc_seg1_size;
#define SGIMC_SEG0_SIZE_ALL         0x10000000 /* 256MB */
#define SGIMC_SEG1_SIZE_IP20_IP22   0x08000000 /* 128MB */
#define SGIMC_SEG1_SIZE_IP26_IP28   0x20000000 /* 512MB */

extern void sgimc_init(void);

#endif /* _ASM_SGI_SGIMC_H */
