/*
 * include/asm-ppc/platforms/mvme5100.h
 *
 * Definitions for Motorola MVME5100.
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_MVME5100_H__
#define __ASM_MVME5100_H__

#define MVME5100_HAWK_SMC_BASE		0xfef80000

#define	MVME5100_PCI_CONFIG_ADDR	0xfe000cf8
#define	MVME5100_PCI_CONFIG_DATA	0xfe000cfc

#define MVME5100_PCI_IO_BASE		0xfe000000
#define MVME5100_PCI_MEM_BASE		0x80000000

#define MVME5100_PCI_MEM_OFFSET		0x00000000

#define MVME5100_PCI_DRAM_OFFSET	0x00000000
#define MVME5100_ISA_MEM_BASE		0x00000000
#define MVME5100_ISA_IO_BASE		MVME5100_PCI_IO_BASE

#define MVME5100_PCI_LOWER_MEM		0x80000000
#define MVME5100_PCI_UPPER_MEM		0xf3f7ffff
#define MVME5100_PCI_LOWER_IO		0x00000000
#define MVME5100_PCI_UPPER_IO		0x0077ffff

/* MVME5100 board register addresses. */
#define	MVME5100_BOARD_STATUS_REG	0xfef88080
#define	MVME5100_BOARD_MODFAIL_REG	0xfef88090
#define	MVME5100_BOARD_MODRST_REG	0xfef880a0
#define	MVME5100_BOARD_TBEN_REG		0xfef880c0
#define MVME5100_BOARD_SW_READ_REG	0xfef880e0
#define	MVME5100_BOARD_GEO_ADDR_REG	0xfef880e8
#define	MVME5100_BOARD_EXT_FEATURE1_REG	0xfef880f0
#define	MVME5100_BOARD_EXT_FEATURE2_REG	0xfef88100

/* Define the NVRAM/RTC address strobe & data registers */
#define MVME5100_PHYS_NVRAM_AS0		0xfef880c8
#define MVME5100_PHYS_NVRAM_AS1		0xfef880d0
#define MVME5100_PHYS_NVRAM_DATA	0xfef880d8

#define MVME5100_NVRAM_AS0	(MVME5100_PHYS_NVRAM_AS0 - MVME5100_ISA_IO_BASE)
#define MVME5100_NVRAM_AS1	(MVME5100_PHYS_NVRAM_AS1 - MVME5100_ISA_IO_BASE)
#define MVME5100_NVRAM_DATA	(MVME5100_PHYS_NVRAM_DATA - MVME5100_ISA_IO_BASE)

/* UART clock, addresses, and irq */
#define MVME5100_BASE_BAUD		1843200
#define	MVME5100_SERIAL_1		0xfef88000
#define	MVME5100_SERIAL_2		0xfef88200
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
#define MVME5100_SERIAL_IRQ		17
#else
#define MVME5100_SERIAL_IRQ		1
#endif

#define MVME5100_WINBOND_DEVFN		0x58
#define MVME5100_WINBOND_VIDDID		0x056510ad

extern void mvme5100_setup_bridge(void);

#endif /* __ASM_MVME5100_H__ */
#endif /* __KERNEL__ */
