/*
 * Definitions for the SGI O2 Crime chip.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 */

#ifndef __ASM_CRIME_H__
#define __ASM_CRIME_H__

#include <asm/types.h>
#include <asm/addrspace.h>

/*
 * Address map
 */
#ifndef __ASSEMBLY__
#define CRIME_BASE	KSEG1ADDR(0x14000000)
#else
#define CRIME_BASE	0xffffffffb4000000
#endif

#ifndef __ASSEMBLY__
static inline u64 crime_read_64 (unsigned long __offset) {
        return *((volatile u64 *) (CRIME_BASE + __offset));
}
static inline void crime_write_64 (unsigned long __offset, u64 __val) {
        *((volatile u64 *) (CRIME_BASE + __offset)) = __val;
}
#endif

#undef BIT
#define BIT(x) (1UL << (x))

/* All CRIME registers are 64 bits */
#define CRIME_ID		0

#define CRIME_ID_MASK		0xff
#define CRIME_ID_IDBITS		0xf0
#define CRIME_ID_IDVALUE	0xa0
#define CRIME_ID_REV		0x0f

#define CRIME_REV_PETTY		0x00
#define CRIME_REV_11		0x11
#define CRIME_REV_13		0x13
#define CRIME_REV_14		0x14

#define CRIME_CONTROL		(0x00000008)
#define CRIME_CONTROL_MASK	0x3fff		/* 14-bit registers */

/* CRIME_CONTROL register bits */
#define CRIME_CONTROL_TRITON_SYSADC	0x2000
#define CRIME_CONTROL_CRIME_SYSADC	0x1000
#define CRIME_CONTROL_HARD_RESET	0x0800
#define CRIME_CONTROL_SOFT_RESET	0x0400
#define CRIME_CONTROL_DOG_ENA		0x0200
#define CRIME_CONTROL_ENDIANESS		0x0100

#define CRIME_CONTROL_ENDIAN_BIG	0x0100
#define CRIME_CONTROL_ENDIAN_LITTLE	0x0000

#define CRIME_CONTROL_CQUEUE_HWM	0x000f
#define CRIME_CONTROL_CQUEUE_SHFT	0
#define CRIME_CONTROL_WBUF_HWM		0x00f0
#define CRIME_CONTROL_WBUF_SHFT		8

#define CRIME_INT_STAT			(0x00000010)
#define CRIME_INT_MASK			(0x00000018)
#define CRIME_SOFT_INT			(0x00000020)
#define CRIME_HARD_INT			(0x00000028)

/* Bits in CRIME_INT_XXX and CRIME_HARD_INT */
#define MACE_VID_IN1_INT		BIT (0)
#define MACE_VID_IN2_INT		BIT (1)
#define MACE_VID_OUT_INT		BIT (2)
#define MACE_ETHERNET_INT		BIT (3)
#define MACE_SUPERIO_INT		BIT (4)
#define MACE_MISC_INT			BIT (5)
#define MACE_AUDIO_INT			BIT (6)
#define MACE_PCI_BRIDGE_INT		BIT (7)
#define MACEPCI_SCSI0_INT		BIT (8)
#define MACEPCI_SCSI1_INT		BIT (9)
#define MACEPCI_SLOT0_INT		BIT (10)
#define MACEPCI_SLOT1_INT		BIT (11)
#define MACEPCI_SLOT2_INT		BIT (12)
#define MACEPCI_SHARED0_INT		BIT (13)
#define MACEPCI_SHARED1_INT		BIT (14)
#define MACEPCI_SHARED2_INT		BIT (15)
#define CRIME_GBE0_INT			BIT (16)
#define CRIME_GBE1_INT			BIT (17)
#define CRIME_GBE2_INT			BIT (18)
#define CRIME_GBE3_INT			BIT (19)
#define CRIME_CPUERR_INT		BIT (20)
#define CRIME_MEMERR_INT		BIT (21)
#define CRIME_RE_EMPTY_E_INT		BIT (22)
#define CRIME_RE_FULL_E_INT		BIT (23)
#define CRIME_RE_IDLE_E_INT		BIT (24)
#define CRIME_RE_EMPTY_L_INT		BIT (25)
#define CRIME_RE_FULL_L_INT		BIT (26)
#define CRIME_RE_IDLE_L_INT    		BIT (27)
#define CRIME_SOFT0_INT			BIT (28)
#define CRIME_SOFT1_INT			BIT (29)
#define CRIME_SOFT2_INT			BIT (30)
#define CRIME_SYSCORERR_INT		CRIME_SOFT2_INT
#define CRIME_VICE_INT			BIT (31)

/* Masks for deciding who handles the interrupt */
#define CRIME_MACE_INT_MASK		0x8f
#define CRIME_MACEISA_INT_MASK		0x70
#define CRIME_MACEPCI_INT_MASK		0xff00
#define CRIME_CRIME_INT_MASK		0xffff0000

/*
 * XXX Todo
 */
#define CRIME_DOG			(0x00000030)
/* We are word-play compatible but not misspelling compatible */
#define MC_GRUFF			CRIME_DOG
#define CRIME_DOG_MASK			(0x001fffff)

/* CRIME_DOG register bits */
#define CRIME_DOG_POWER_ON_RESET	(0x00010000)
#define CRIME_DOG_WARM_RESET		(0x00080000)
#define CRIME_DOG_TIMEOUT		(CRIME_DOG_POWER_ON_RESET|CRIME_DOG_WARM_RESET)
#define CRIME_DOG_VALUE			(0x00007fff)	/* ??? */

#define CRIME_TIME			(0x00000038)
#define CRIME_TIME_MASK			(0x0000ffffffffffff)

#ifdef MASTER_FREQ
#undef MASTER_FREQ
#endif
#define CRIME_MASTER_FREQ		66666500	/* Crime upcounter frequency */
#define CRIME_NS_PER_TICK		15	/* for delay_calibrate */

#define CRIME_CPU_ERROR_ADDR		(0x00000040)
#define CRIME_CPU_ERROR_ADDR_MASK	(0x3ffffffff)

#define CRIME_CPU_ERROR_STAT		(0x00000048)
/* REV_PETTY only! */
#define CRIME_CPU_ERROR_ENA		(0x00000050)

/*
 * bit definitions for CRIME/VICE error status and enable registers
 */
#define CRIME_CPU_ERROR_MASK           0x7UL   /* cpu error stat is 3 bits */
#define CRIME_CPU_ERROR_CPU_ILL_ADDR   0x4
#define CRIME_CPU_ERROR_VICE_WRT_PRTY  0x2
#define CRIME_CPU_ERROR_CPU_WRT_PRTY   0x1

/*
 * these are the definitions for the error status/enable  register in
 * petty crime.  Note that the enable register does not exist in crime
 * rev 1 and above.
 */
#define CRIME_CPU_ERROR_MASK_REV0		0x3ff	/* cpu error stat is 9 bits */
#define CRIME_CPU_ERROR_CPU_INV_ADDR_RD		0x200
#define CRIME_CPU_ERROR_VICE_II			0x100
#define CRIME_CPU_ERROR_VICE_SYSAD		0x80
#define CRIME_CPU_ERROR_VICE_SYSCMD		0x40
#define CRIME_CPU_ERROR_VICE_INV_ADDR		0x20
#define CRIME_CPU_ERROR_CPU_II			0x10
#define CRIME_CPU_ERROR_CPU_SYSAD		0x8
#define CRIME_CPU_ERROR_CPU_SYSCMD		0x4
#define CRIME_CPU_ERROR_CPU_INV_ADDR_WR		0x2
#define CRIME_CPU_ERROR_CPU_INV_REG_ADDR	0x1

#define CRIME_VICE_ERROR_ADDR		(0x00000058)
#define CRIME_VICE_ERROR_ADDR_MASK	(0x3fffffff)

#define CRIME_MEM_CONTROL		(0x00000200)
#define CRIME_MEM_CONTROL_MASK		0x3	/* 25 cent register */
#define CRIME_MEM_CONTROL_ECC_ENA	0x1
#define CRIME_MEM_CONTROL_USE_ECC_REPL	0x2

/*
 * macros for CRIME memory bank control registers.
 */
#define CRIME_MEM_BANK_CONTROL(__bank)		(0x00000208 + ((__bank) << 3))
#define CRIME_MEM_BANK_CONTROL_MASK		0x11f /* 9 bits 7:5 reserved */
#define CRIME_MEM_BANK_CONTROL_ADDR		0x01f
#define CRIME_MEM_BANK_CONTROL_SDRAM_SIZE	0x100

#define CRIME_MEM_REFRESH_COUNTER	(0x00000248)
#define CRIME_MEM_REFRESH_COUNTER_MASK	0x7ff	/* 11-bit register */

#define CRIME_MAXBANKS                 8

/*
 * CRIME Memory error status register bit definitions
 */
#define CRIME_MEM_ERROR_STAT		(0x00000250)
#define CRIME_MEM_ERROR_STAT_MASK       0x0ff7ffff    /* 28-bit register */
#define CRIME_MEM_ERROR_MACE_ID		0x0000007f
#define CRIME_MEM_ERROR_MACE_ACCESS	0x00000080
#define CRIME_MEM_ERROR_RE_ID		0x00007f00
#define CRIME_MEM_ERROR_RE_ACCESS	0x00008000
#define CRIME_MEM_ERROR_GBE_ACCESS	0x00010000
#define CRIME_MEM_ERROR_VICE_ACCESS	0x00020000
#define CRIME_MEM_ERROR_CPU_ACCESS	0x00040000
#define CRIME_MEM_ERROR_RESERVED	0x00080000
#define CRIME_MEM_ERROR_SOFT_ERR	0x00100000
#define CRIME_MEM_ERROR_HARD_ERR	0x00200000
#define CRIME_MEM_ERROR_MULTIPLE	0x00400000
#define CRIME_MEM_ERROR_ECC		0x01800000
#define CRIME_MEM_ERROR_MEM_ECC_RD	0x00800000
#define CRIME_MEM_ERROR_MEM_ECC_RMW	0x01000000
#define CRIME_MEM_ERROR_INV		0x0e000000
#define CRIME_MEM_ERROR_INV_MEM_ADDR_RD	0x02000000
#define CRIME_MEM_ERROR_INV_MEM_ADDR_WR	0x04000000
#define CRIME_MEM_ERROR_INV_MEM_ADDR_RMW	0x08000000

#define CRIME_MEM_ERROR_ADDR		(0x00000258)
#define CRIME_MEM_ERROR_ADDR_MASK	0x3fffffff

#define CRIME_MEM_ERROR_ECC_SYN		(0x00000260)
#define CRIME_MEM_ERROR_ECC_SYN_MASK	0xffffffff

#define CRIME_MEM_ERROR_ECC_CHK		(0x00000268)
#define CRIME_MEM_ERROR_ECC_CHK_MASK    0xffffffff

#define CRIME_MEM_ERROR_ECC_REPL	(0x00000270)
#define CRIME_MEM_ERROR_ECC_REPL_MASK	0xffffffff

#endif /* __ASM_CRIME_H__ */
