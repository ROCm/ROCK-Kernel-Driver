/*
 * include/asm-ppc/platforms/spruce.h
 *
 * Definitions for IBM Spruce reference board support
 *
 * Authors: Matt Porter and Johnnie Peters
 *          mporter@mvista.com
 *          jpeters@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_SPRUCE_H__
#define __ASM_SPRUCE_H__

#define SPRUCE_PCI_CONFIG_ADDR	0xfec00000
#define SPRUCE_PCI_CONFIG_DATA	0xfec00004

#define SPRUCE_PCI_PHY_IO_BASE	0xf8000000
#define SPRUCE_PCI_IO_BASE	SPRUCE_PCI_PHY_IO_BASE

#define SPRUCE_PCI_SYS_MEM_BASE	0x00000000

#define SPRUCE_PCI_LOWER_MEM	0x80000000
#define SPRUCE_PCI_UPPER_MEM	0x9fffffff
#define SPRUCE_PCI_LOWER_IO	0x00000000
#define SPRUCE_PCI_UPPER_IO	0x03ffffff

#define	SPRUCE_ISA_IO_BASE	SPRUCE_PCI_IO_BASE

#define SPRUCE_MEM_SIZE		0x04000000
#define SPRUCE_BUS_SPEED	66666667

#define SPRUCE_SERIAL_1_ADDR	0xff600300
#define SPRUCE_SERIAL_2_ADDR	0xff600400

#define SPRUCE_NVRAM_BASE_ADDR	0xff800000
#define SPRUCE_RTC_BASE_ADDR	SPRUCE_NVRAM_BASE_ADDR

#endif /* __ASM_SPRUCE_H__ */
#endif /* __KERNEL__ */
