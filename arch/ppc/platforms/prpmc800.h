/*
 * include/asm-ppc/platforms/prpmc800.h
 *
 * Definitions for Motorola PrPMC800 board support
 *
 * Author: Dale Farnsworth <dale.farnsworth@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
 /*
 * From Processor to PCI:
 *   PCI Mem Space: 0x80000000 - 0xa0000000 -> 0x80000000 - 0xa0000000 (512 MB)
 *   PCI I/O Space: 0xfe400000 - 0xfeef0000 -> 0x00000000 - 0x00b00000 (11 MB)
 *      Note: Must skip 0xfe000000-0xfe400000 for CONFIG_HIGHMEM/PKMAP area
 *
 * From PCI to Processor:
 *   System Memory: 0x00000000 -> 0x00000000
 */


#ifndef __ASMPPC_PRPMC800_H
#define __ASMPPC_PRPMC800_H

#define PRPMC800_PCI_CONFIG_ADDR		0xfe000cf8
#define PRPMC800_PCI_CONFIG_DATA		0xfe000cfc

#define PRPMC800_PROC_PCI_IO_START		0xfe400000U
#define PRPMC800_PROC_PCI_IO_END		0xfeefffffU
#define PRPMC800_PCI_IO_START			0x00000000U
#define PRPMC800_PCI_IO_END			0x00afffffU

#define PRPMC800_PROC_PCI_MEM_START		0x80000000U
#define PRPMC800_PROC_PCI_MEM_END		0x9fffffffU
#define PRPMC800_PCI_MEM_START			0x80000000U
#define PRPMC800_PCI_MEM_END			0x9fffffffU

#define PRPMC800_PCI_DRAM_OFFSET		0x00000000U
#define PRPMC800_PCI_PHY_MEM_OFFSET		0x00000000U

#define PRPMC800_ISA_IO_BASE			PRPMC800_PROC_PCI_IO_START
#define PRPMC800_ISA_MEM_BASE			0x00000000U

#define PRPMC800_HARRIER_XCSR_BASE		0xfeff0000
#define PRPMC800_HARRIER_MPIC_BASE		0xff000000

#define PRPMC800_SERIAL_1			0xfeff00c0

#define PRPMC800_BASE_BAUD			1843200


#endif /* __ASMPPC_PRPMC800_H */
