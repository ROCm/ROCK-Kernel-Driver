/*
 * arch/ppc/platforms/4xx/ibmnp4gs.h
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBMNP4GS_H__
#define __ASM_IBMNP4GS_H__

#include <linux/config.h>
#include <asm/ibm_ocp.h>

/* ibm405.h at bottom of this file */

/* PCI
 * PCI Bridge config reg definitions
 * see 17-19 of manual
 */

#define PPC405_PCI_CONFIG_ADDR	0xeec00000
#define PPC405_PCI_CONFIG_DATA	0xeec00004

#define PPC405_PCI_PHY_MEM_BASE	0x80000000	/* hose_a->pci_mem_offset */
						/* setbat */
#define PPC405_PCI_MEM_BASE	PPC405_PCI_PHY_MEM_BASE	/* setbat */
#define PPC405_PCI_PHY_IO_BASE	0xe8000000	/* setbat */
#define PPC405_PCI_IO_BASE	PPC405_PCI_PHY_IO_BASE	/* setbat */

#define PPC405_PCI_LOWER_MEM	0x80000000	/* hose_a->mem_space.start */
#define PPC405_PCI_UPPER_MEM	0xBfffffff	/* hose_a->mem_space.end */
#define PPC405_PCI_LOWER_IO	0x00000000	/* hose_a->io_space.start */
#define PPC405_PCI_UPPER_IO	0x0000ffff	/* hose_a->io_space.end */

#define PPC405_ISA_IO_BASE	PPC405_PCI_IO_BASE

#define PPC4xx_PCI_IO_PADDR	((uint)PPC405_PCI_PHY_IO_BASE)
#define PPC4xx_PCI_IO_VADDR	PPC4xx_PCI_IO_PADDR
#define PPC4xx_PCI_IO_SIZE	((uint)64*1024)
#define PPC4xx_PCI_CFG_PADDR	((uint)PPC405_PCI_CONFIG_ADDR)
#define PPC4xx_PCI_CFG_VADDR	PPC4xx_PCI_CFG_PADDR
#define PPC4xx_PCI_CFG_SIZE	((uint)4*1024)
#define PPC4xx_PCI_LCFG_PADDR	((uint)0xef400000)
#define PPC4xx_PCI_LCFG_VADDR	PPC4xx_PCI_LCFG_PADDR
#define PPC4xx_PCI_LCFG_SIZE	((uint)4*1024)
#define PPC4xx_ONB_IO_PADDR	((uint)0xef600000)
#define PPC4xx_ONB_IO_VADDR	PPC4xx_ONB_IO_PADDR
#define PPC4xx_ONB_IO_SIZE	((uint)4*1024)

#define PCI_CONFIG_ADDR_MASK		0x7F000000
#define PCI_CONFIG_CYCLE_ENABLE		0x80000000
#define PCI_BASE_ADDRESS_2		0x18	/* 32 bits [htype 0 only] */
#define PCI_BASE_ADDRESS_MEM_PREFETCH	0x08
#define PCI_CONFIG_ADDR_MASK		0x7F000000
#define PCI_CONFIG_CYCLE_ENABLE		0x80000000
#define PCI_BASE_ADDRESS_MEM_CARD1	0x80000000
#define PCI_BASE_ADDRESS_MEM_CARD2	0x90000000
#define PPC405_UART0_INT		1
#define PPC_405RAINIER2_IO_PAGE		((void*)0xe8001000)
#define PPC_405RAINIER1_IO_PAGE		((void*)0xe8002000)
#define PPC405_UART0_IO_BASE		0x300	/* mostly use (rainier_io_page+0x300) */
#define RAINIER_IO_PAGE_INTERPOSER_PADDR 0xe8000000
#define RAINIER_IO_PAGE_INTERPOSER_VADDR RAINIER_IO_PAGE_INTERPOSER_PADDR
#define RAINIER_IO_PAGE_PCI_PADDR	0xeec00000
#define RAINIER_IO_PAGE_PCI_VADDR	RAINIER_IO_PAGE_PCI_PADDR

/* serial port defines */
#define RS_TABLE_SIZE	1

#define UART0_INT	1

#define PCIL0_BASE	0xEF400000
#define UART0_IO_BASE	PPC_405RAINIER1_IO_PAGE + PPC405_UART0_IO_BASE

#define UART_NUMS	1
#define BD_EMAC_ADDR(e,i) bi_enetaddr[i]

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (u8 *)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)
#endif

/* DCR defines */
#define DCRN_CHCR_BASE		0x0B1
#define DCRN_CHPSR_BASE		0x0B4
#define DCRN_CPMSR_BASE		0x0B8
#define DCRN_CPMFR_BASE		0x0BA

#define  PSR_PLL_FWD_MASK        0xC0000000
#define  PSR_PLL_FDBACK_MASK     0x30000000
#define  PSR_PLL_TUNING_MASK     0x0E000000
#define  PSR_PLB_CPU_MASK        0x01800000
#define  PSR_OPB_PLB_MASK        0x00600000
#define  PSR_PCI_PLB_MASK        0x00180000
#define  PSR_EB_PLB_MASK         0x00060000
#define  PSR_ROM_WIDTH_MASK      0x00018000
#define  PSR_ROM_LOC             0x00004000
#define  PSR_PCI_ASYNC_EN        0x00001000
#define  PSR_PCI_ARBIT_EN        0x00000400

#define IBM_CPM_IIC0		0x80000000	/* IIC interface */
#define IBM_CPM_PCI		0x40000000	/* PCI bridge */
#define IBM_CPM_CPU		0x20000000	/* processor core */
#define IBM_CPM_DMA		0x10000000	/* DMA controller */
#define IBM_CPM_BRG		0x08000000	/* PLB to OPB bridge */
#define IBM_CPM_DCP		0x04000000	/* CodePack */
#define IBM_CPM_EBC		0x02000000	/* ROM/SRAM peripheral controller */
#define IBM_CPM_SDRAM0		0x01000000	/* SDRAM memory controller */
#define IBM_CPM_PLB		0x00800000	/* PLB bus arbiter */
#define IBM_CPM_GPIO0		0x00400000	/* General Purpose IO (??) */
#define IBM_CPM_UART0		0x00200000	/* serial port 0 */
#define IBM_CPM_UART1		0x00100000	/* serial port 1 */
#define IBM_CPM_UIC		0x00080000	/* Universal Interrupt Controller */
#define IBM_CPM_TMRCLK		0x00040000	/* CPU timers */

#define DFLT_IBM4xx_PM		0

#define DCRN_DMA0_BASE		0x100
#define DCRN_DMA1_BASE		0x108
#define DCRN_DMA2_BASE		0x110
#define DCRN_DMA3_BASE		0x118
#define DCRNCAP_DMA_SG		1	/* have DMA scatter/gather capability */
#define DCRN_DMASR_BASE		0x120
#define DCRN_EBC_BASE		0x012
#define DCRN_DCP0_BASE		0x014
#define DCRN_MAL_BASE		0x180
#define DCRN_OCM0_BASE		0x018
#define DCRN_PLB0_BASE		0x084
#define DCRN_PLLMR_BASE		0x0B0
#define DCRN_POB0_BASE		0x0A0
#define DCRN_SDRAM0_BASE	0x010
#define DCRN_UIC0_BASE		0x0C0
#define UIC0 DCRN_UIC0_BASE

#include <asm/ibm405.h>

#endif				/* __ASM_IBMNP4GS_H__ */
#endif				/* __KERNEL__ */
