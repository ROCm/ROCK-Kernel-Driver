/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_PCI_PCI_DEFS_H
#define _ASM_SN_PCI_PCI_DEFS_H

#include <linux/config.h>

/* defines for the PCI bus architecture */

/* Bit layout of address fields for Type-1
 * Configuration Space cycles.
 */
#define	PCI_TYPE0_SLOT_MASK	0xFFFFF800
#define	PCI_TYPE0_FUNC_MASK	0x00000700
#define	PCI_TYPE0_REG_MASK	0x000000FF

#define	PCI_TYPE0_SLOT_SHFT	11
#define	PCI_TYPE0_FUNC_SHFT	8
#define	PCI_TYPE0_REG_SHFT	0

#define	PCI_TYPE0_FUNC(a)	(((a) & PCI_TYPE0_FUNC_MASK) >> PCI_TYPE0_FUNC_SHFT)
#define	PCI_TYPE0_REG(a)	(((a) & PCI_TYPE0_REG_MASK) >> PCI_TYPE0_REG_SHFT)

#define	PCI_TYPE0(s,f,r)	((((1<<(s)) << PCI_TYPE0_SLOT_SHFT) & PCI_TYPE0_SLOT_MASK) |\
				 (((f) << PCI_TYPE0_FUNC_SHFT) & PCI_TYPE0_FUNC_MASK) |\
				 (((r) << PCI_TYPE0_REG_SHFT) & PCI_TYPE0_REG_MASK))

/* Bit layout of address fields for Type-1
 * Configuration Space cycles.
 * NOTE: I'm including the byte offset within
 * the 32-bit word as part of the register
 * number as an extension of the layout in
 * the PCI spec.
 */
#define	PCI_TYPE1_BUS_MASK	0x00FF0000
#define	PCI_TYPE1_SLOT_MASK	0x0000F100
#define	PCI_TYPE1_FUNC_MASK	0x00000700
#define	PCI_TYPE1_REG_MASK	0x000000FF

#define	PCI_TYPE1_BUS_SHFT	16
#define	PCI_TYPE1_SLOT_SHFT	11
#define	PCI_TYPE1_FUNC_SHFT	8
#define	PCI_TYPE1_REG_SHFT	0

#define	PCI_TYPE1_BUS(a)	(((a) & PCI_TYPE1_BUS_MASK) >> PCI_TYPE1_BUS_SHFT)
#define	PCI_TYPE1_SLOT(a)	(((a) & PCI_TYPE1_SLOT_MASK) >> PCI_TYPE1_SLOT_SHFT)
#define	PCI_TYPE1_FUNC(a)	(((a) & PCI_TYPE1_FUNC_MASK) >> PCI_TYPE1_FUNC_SHFT)
#define	PCI_TYPE1_REG(a)	(((a) & PCI_TYPE1_REG_MASK) >> PCI_TYPE1_REG_SHFT)

#define	PCI_TYPE1(b,s,f,r)	((((b) << PCI_TYPE1_BUS_SHFT) & PCI_TYPE1_BUS_MASK) |\
				 (((s) << PCI_TYPE1_SLOT_SHFT) & PCI_TYPE1_SLOT_MASK) |\
				 (((f) << PCI_TYPE1_FUNC_SHFT) & PCI_TYPE1_FUNC_MASK) |\
				 (((r) << PCI_TYPE1_REG_SHFT) & PCI_TYPE1_REG_MASK))

/* Byte offsets of registers in CFG space
 */
#define	PCI_CFG_VENDOR_ID	0x00		/* Vendor ID (2 bytes) */
#define	PCI_CFG_DEVICE_ID	0x02		/* Device ID (2 bytes) */

#define	PCI_CFG_COMMAND		0x04		/* Command (2 bytes) */
#define	PCI_CFG_STATUS		0x06		/* Status (2 bytes) */

/* NOTE: if you are using a C "switch" statement to
 * differentiate between the Config space registers, be
 * aware that PCI_CFG_CLASS_CODE and PCI_CFG_BASE_CLASS
 * are the same offset.
 */
#define	PCI_CFG_REV_ID		0x08		/* Revision Id (1 byte) */
#define	PCI_CFG_CLASS_CODE	0x09		/* Class Code (3 bytes) */
#define	PCI_CFG_BASE_CLASS	0x09		/* Base Class (1 byte) */
#define	PCI_CFG_SUB_CLASS	0x0A		/* Sub Class (1 byte) */
#define	PCI_CFG_PROG_IF		0x0B		/* Prog Interface (1 byte) */

#define	PCI_CFG_CACHE_LINE	0x0C		/* Cache line size (1 byte) */
#define	PCI_CFG_LATENCY_TIMER	0x0D		/* Latency Timer (1 byte) */
#define	PCI_CFG_HEADER_TYPE	0x0E		/* Header Type (1 byte) */
#define	PCI_CFG_BIST		0x0F		/* Built In Self Test */

#define	PCI_CFG_BASE_ADDR_0	0x10		/* Base Address (4 bytes) */
#define	PCI_CFG_BASE_ADDR_1	0x14		/* Base Address (4 bytes) */
#define	PCI_CFG_BASE_ADDR_2	0x18		/* Base Address (4 bytes) */
#define	PCI_CFG_BASE_ADDR_3	0x1C		/* Base Address (4 bytes) */
#define	PCI_CFG_BASE_ADDR_4	0x20		/* Base Address (4 bytes) */
#define	PCI_CFG_BASE_ADDR_5	0x24		/* Base Address (4 bytes) */

#define	PCI_CFG_BASE_ADDR_OFF	0x04		/* Base Address Offset (1..5)*/
#define	PCI_CFG_BASE_ADDR(n)	(PCI_CFG_BASE_ADDR_0 + (n)*PCI_CFG_BASE_ADDR_OFF)
#define	PCI_CFG_BASE_ADDRS	6		/* up to this many BASE regs */

#define	PCI_CFG_CARDBUS_CIS	0x28		/* Cardbus CIS Pointer (4B) */

#define	PCI_CFG_SUBSYS_VEND_ID	0x2C		/* Subsystem Vendor ID (2B) */
#define	PCI_CFG_SUBSYS_ID	0x2E		/* Subsystem ID */

#define	PCI_EXPANSION_ROM	0x30		/* Expansion Rom Base (4B) */

#define	PCI_INTR_LINE		0x3C		/* Interrupt Line (1B) */
#define	PCI_INTR_PIN		0x3D		/* Interrupt Pin (1B) */
#define	PCI_MIN_GNT		0x3E		/* Minimum Grant (1B) */
#define	PCI_MAX_LAT		0x3F		/* Maximum Latency (1B) */

#define PCI_CFG_VEND_SPECIFIC	0x40		/* first vendor specific reg */

/* layout for Type 0x01 headers */

#define	PCI_CFG_PPB_BUS_PRI		0x18	/* immediate upstream bus # */
#define	PCI_CFG_PPB_BUS_SEC		0x19	/* immediate downstream bus # */
#define	PCI_CFG_PPB_BUS_SUB		0x1A	/* last downstream bus # */
#define	PCI_CFG_PPB_SEC_LAT		0x1B	/* latency timer for SEC bus */
#define PCI_CFG_PPB_IOBASE		0x1C	/* IO Base Addr bits 12..15 */
#define PCI_CFG_PPB_IOLIM		0x1D	/* IO Limit Addr bits 12..15 */
#define	PCI_CFG_PPB_SEC_STAT		0x1E	/* Secondary Status */
#define PCI_CFG_PPB_MEMBASE		0x20	/* MEM Base Addr bits 16..31 */
#define PCI_CFG_PPB_MEMLIM		0x22	/* MEM Limit Addr bits 16..31 */
#define PCI_CFG_PPB_MEMPFBASE		0x24	/* PfMEM Base Addr bits 16..31 */
#define PCI_CFG_PPB_MEMPFLIM		0x26	/* PfMEM Limit Addr bits 16..31 */
#define PCI_CFG_PPB_MEMPFBASEHI		0x28	/* PfMEM Base Addr bits 32..63 */
#define PCI_CFG_PPB_MEMPFLIMHI		0x2C	/* PfMEM Limit Addr bits 32..63 */
#define PCI_CFG_PPB_IOBASEHI		0x30	/* IO Base Addr bits 16..31 */
#define PCI_CFG_PPB_IOLIMHI		0x32	/* IO Limit Addr bits 16..31 */
#define	PCI_CFG_PPB_SUB_VENDOR		0x34	/* Subsystem Vendor ID */
#define	PCI_CFG_PPB_SUB_DEVICE		0x36	/* Subsystem Device ID */
#define	PCI_CFG_PPB_INT_PIN		0x3D	/* Interrupt Pin */
#define	PCI_CFG_PPB_BRIDGE_CTRL		0x3E	/* Bridge Control */
     /* XXX- these might be DEC 21152 specific */
#define	PCI_CFG_PPB_CHIP_CTRL		0x40
#define	PCI_CFG_PPB_DIAG_CTRL		0x41
#define	PCI_CFG_PPB_ARB_CTRL		0x42
#define	PCI_CFG_PPB_SERR_DISABLE	0x64
#define	PCI_CFG_PPB_CLK2_CTRL		0x68
#define	PCI_CFG_PPB_SERR_STATUS		0x6A

/* Command Register layout (0x04) */
#define	PCI_CMD_IO_SPACE	0x001		/* I/O Space device */
#define	PCI_CMD_MEM_SPACE	0x002		/* Memory Space */
#define	PCI_CMD_BUS_MASTER	0x004		/* Bus Master */
#define	PCI_CMD_SPEC_CYCLES	0x008		/* Special Cycles */
#define	PCI_CMD_MEMW_INV_ENAB	0x010		/* Memory Write Inv Enable */
#define	PCI_CMD_VGA_PALETTE_SNP	0x020		/* VGA Palette Snoop */
#define	PCI_CMD_PAR_ERR_RESP	0x040		/* Parity Error Response */
#define	PCI_CMD_WAIT_CYCLE_CTL	0x080		/* Wait Cycle Control */
#define	PCI_CMD_SERR_ENABLE	0x100		/* SERR# Enable */
#define	PCI_CMD_F_BK_BK_ENABLE	0x200		/* Fast Back-to-Back Enable */

/* Status Register Layout (0x06) */
#define	PCI_STAT_PAR_ERR_DET	0x8000		/* Detected Parity Error */
#define	PCI_STAT_SYS_ERR	0x4000		/* Signaled System Error */
#define	PCI_STAT_RCVD_MSTR_ABT	0x2000		/* Received Master Abort */
#define	PCI_STAT_RCVD_TGT_ABT	0x1000		/* Received Target Abort */
#define	PCI_STAT_SGNL_TGT_ABT	0x0800		/* Signaled Target Abort */

#define	PCI_STAT_DEVSEL_TIMING	0x0600		/* DEVSEL Timing Mask */
#define	DEVSEL_TIMING(_x)	(((_x) >> 9) & 3)	/* devsel tim macro */
#define	DEVSEL_FAST		0		/* Fast timing */
#define	DEVSEL_MEDIUM		1		/* Medium timing */
#define	DEVSEL_SLOW		2		/* Slow timing */

#define	PCI_STAT_DATA_PAR_ERR	0x0100		/* Data Parity Err Detected */
#define	PCI_STAT_F_BK_BK_CAP	0x0080		/* Fast Back-to-Back Capable */
#define	PCI_STAT_UDF_SUPP	0x0040		/* UDF Supported */
#define	PCI_STAT_66MHZ_CAP	0x0020		/* 66 MHz Capable */

/* BIST Register Layout (0x0F) */
#define	PCI_BIST_BIST_CAP	0x80		/* BIST Capable */
#define	PCI_BIST_START_BIST	0x40		/* Start BIST */
#define	PCI_BIST_CMPLTION_MASK	0x0F		/* COMPLETION MASK */
#define	PCI_BIST_CMPL_OK	0x00		/* 0 value is completion OK */

/* Base Address Register 0x10 */
#define	PCI_BA_IO_SPACE		0x1		/* I/O Space Marker */
#define	PCI_BA_MEM_LOCATION	0x6		/* 2 bits for location avail */
#define	PCI_BA_MEM_32BIT	0x0		/* Anywhere in 32bit space */
#define	PCI_BA_MEM_1MEG		0x2		/* Locate below 1 Meg */
#define	PCI_BA_MEM_64BIT	0x4		/* Anywhere in 64bit space */
#define	PCI_BA_PREFETCH		0x8		/* Prefetchable, no side effect */

/* PIO interface macros */

#ifndef IOC3_EMULATION

#define PCI_INB(x)          (*((volatile char*)x))
#define PCI_INH(x)          (*((volatile short*)x))
#define PCI_INW(x)          (*((volatile int*)x))
#define PCI_OUTB(x,y)       (*((volatile char*)x) = y)
#define PCI_OUTH(x,y)       (*((volatile short*)x) = y)
#define PCI_OUTW(x,y)       (*((volatile int*)x) = y)

#else

extern uint pci_read(void * address, int type);
extern void pci_write(void * address, int data, int type);

#define BYTE   1
#define HALF   2
#define WORD   4

#define PCI_INB(x)          pci_read((void *)(x),BYTE)
#define PCI_INH(x)          pci_read((void *)(x),HALF)
#define PCI_INW(x)          pci_read((void *)(x),WORD)
#define PCI_OUTB(x,y)       pci_write((void *)(x),(y),BYTE)
#define PCI_OUTH(x,y)       pci_write((void *)(x),(y),HALF)
#define PCI_OUTW(x,y)       pci_write((void *)(x),(y),WORD)

#endif /* !IOC3_EMULATION */
						/* effects on reads, merges */

#ifdef CONFIG_SGI_IP22
#define BYTECOUNT_W_GIO	    0xbf400000
#endif

/*
 * Definition of address layouts for PCI Config mechanism #1
 * XXX- These largely duplicate PCI_TYPE1 constants at the top
 * of the file; the two groups should probably be combined.
 */

#define CFG1_ADDR_REGISTER_MASK		0x000000fc
#define CFG1_ADDR_FUNCTION_MASK		0x00000700
#define CFG1_ADDR_DEVICE_MASK		0x0000f800
#define CFG1_ADDR_BUS_MASK		0x00ff0000

#define CFG1_REGISTER_SHIFT		2
#define CFG1_FUNCTION_SHIFT		8
#define CFG1_DEVICE_SHIFT		11
#define CFG1_BUS_SHIFT			16

#ifdef CONFIG_SGI_IP32
 /* Definitions related to IP32 PCI Bridge policy
  * XXX- should probaly be moved to a mace-specific header
  */
#define PCI_CONFIG_BITS			0xfe0085ff
#define	PCI_CONTROL_MRMRA_ENABLE	0x00000800
#define PCI_FIRST_IO_ADDR		0x1000
#define PCI_IO_MAP_INCR			0x1000
#endif /* CONFIG_SGI_IP32 */

#endif /* _ASM_SN_PCI_PCI_DEFS_H */
