/*
 * arch/ppc/platforms/4xx/ibmstb3.h
 *
 * Authors: Armin Kuster <akuster@mvista.com>, Tom Rini <trini@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBMSTBX_H__
#define __ASM_IBMSTBX_H__

#include <linux/config.h>
#include <platforms/4xx/ibm_ocp.h>

/* ibm405.h at bottom of this file */

/*
 * Memory map for the IBM "Redwood-4" STB03xxx evaluation board.
 *
 * The  STB03xxx internal i/o addresses don't work for us 1:1,
 * so we need to map them at a well know virtual address.
 *
 * 4000 000x   uart1           -> 0xe000 000x
 * 4001 00xx   ppu
 * 4002 00xx   smart card
 * 4003 000x   iic
 * 4004 000x   uart0
 * 4005 0xxx   timer
 * 4006 00xx   gpio
 * 4007 00xx   smart card
 * 400b 000x   iic
 * 400c 000x   scp
 * 400d 000x   modem
 */

#define STB03xxx_IO_BASE	((uint)0xe0000000)
#define PPC4xx_ONB_IO_PADDR	((uint)0x40000000)
#define PPC4xx_ONB_IO_VADDR	STB03xxx_IO_BASE
#define PPC4xx_ONB_IO_SIZE	((uint)14*64*1024)

/* Since we're into address mapping hacks, at least try to hide
 * it under a macro.....
 */
#define STB03xxx_MAP_IO_ADDR(a)	(((uint)(a) & 0x000fffff) + PPC4xx_ONB_IO_VADDR)

#define RS_TABLE_SIZE		1
#define UART0_INT		20
#ifdef __BOOTER__
#define UART0_IO_BASE		0x40040000
#else
#define UART0_IO_BASE		0xe0040000
#endif

 /* UART 0 is duped here so when the SICC is the default console
  * then ttys1 is configured properly - armin
  */

#define UART1_INT		20
#ifdef __BOOTER__
#define UART1_IO_BASE		0x40040000
#else
#define UART1_IO_BASE		0xe0040000
#endif

/* need to make this work in scheme - armin */

#define SICC0_INTRX		21
#define SICC0_INTTX		22
#define SICC0_IO_BASE		((uint* )0x40000000)

#define IDE0_BASE		0xf2100000
#define REDWOOD_IDE_CTRL	0xf4100000

#define IIC0_BASE	0x40030000
#define IIC1_BASE	0x400b0000
#define OPB0_BASE	0x40010000
#define GPIO0_BASE	0x40060000
#define IIC0_IRQ	9
#define IIC1_IRQ	10
#define IIC_OWN		0x55
#define IIC_CLOCK	50
#define IDE0_IRQ 	25

#define BD_EMAC_ADDR(e,i) bi_enetaddr[i]

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (u8 *)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_DEBUG_IO_BASE	UART0_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_DEBUG_IO_BASE	UART1_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(1)
#endif

/* ------------------------------------------------------------------------- */

#define DCRN_DCRX_BASE		0x020
#define DCRN_CIC_BASE 		0x030
#define DCRN_UIC0_BASE		0x040
#define DCRN_PLB0_BASE		0x054
#define DCRN_PLB1_BASE		0x064
#define DCRN_EBIMC_BASE		0x070
#define DCRN_POB0_BASE		0x0B0

#define DCRN_BE_BASE		0x090
#define DCRN_DMA0_BASE		0x0C0
#define DCRN_DMA1_BASE		0x0C8
#define DCRN_DMA2_BASE		0x0D0
#define DCRN_DMA3_BASE		0x0D8
#define DCRNCAP_DMA_CC		1	/* have DMA chained count capability */
#define DCRN_DMASR_BASE		0x0E0

#define DCRN_CPMFR_BASE		0x102
#define DCRN_SCCR_BASE		0x120
#define UIC0 DCRN_UIC0_BASE

#define IBM_CPM_IIC0	0x80000000	/* IIC 0 interface */
#define IBM_CPM_I1284	0x40000000	/* IEEE-1284 */
#define IBM_CPM_IIC1	0x20000000	/* IIC 1 interface */
#define IBM_CPM_CPU	0x10000000	/* PPC405B3 clock control */
#define IBM_CPM_AUD	0x08000000	/* Audio Decoder */
#define IBM_CPM_EBIU	0x04000000	/* External Bus Interface Unit */
#define IBM_CPM_SDRAM1	0x02000000	/* SDRAM 1 memory controller */
#define IBM_CPM_DMA	0x01000000	/* DMA controller */
#define IBM_CPM_UART1	0x00100000	/* Serial 1 / Infrared */
#define IBM_CPM_UART0	0x00080000	/* Serial 0 / 16550 */
#define IBM_CPM_DCRX	0x00040000	/* DCR Extension */
#define IBM_CPM_SC0	0x00020000	/* Smart Card 0 */
#define IBM_CPM_SC1	0x00008000	/* Smart Card 1 */
#define IBM_CPM_SDRAM0	0x00004000	/* SDRAM 0 memory controller */
#define IBM_CPM_XPT54	0x00002000	/* Transport - 54 Mhz */
#define IBM_CPM_CBS	0x00001000	/* Cross Bar Switch */
#define IBM_CPM_GPT	0x00000800	/* GPTPWM */
#define IBM_CPM_GPIO0	0x00000400	/* General Purpose IO 0 */
#define IBM_CPM_DENC	0x00000200	/* Digital video Encoder */
#define IBM_CPM_TMRCLK	0x00000100	/* CPU timers */
#define IBM_CPM_XPT27	0x00000080	/* Transport - 27 Mhz */
#define IBM_CPM_UIC	0x00000040	/* Universal Interrupt Controller */
#define IBM_CPM_MSI	0x00000010	/* Modem Serial Interface (SSP) */
#define IBM_CPM_UART2	0x00000008	/* Serial Control Port */
#define IBM_CPM_DSCR	0x00000004	/* Descrambler */
#define IBM_CPM_VID2	0x00000002	/* Video Decoder clock domain 2 */

#define DFLT_IBM4xx_PM	~(IBM_CPM_CPU | IBM_CPM_EBIU | IBM_CPM_SDRAM1 \
			| IBM_CPM_DMA | IBM_CPM_CBS | IBM_CPM_SDRAM0 \
			| IBM_CPM_XPT54 | IBM_CPM_TMRCLK | IBM_CPM_XPT27 \
			| IBM_CPM_UIC)

#ifdef DCRN_CIC_BASE
#define DCRN_CICCR	(DCRN_CIC_BASE + 0x0)	/* CIC Control Register */
#define DCRN_DMAS1	(DCRN_CIC_BASE + 0x1)	/* DMA Select1 Register */
#define DCRN_DMAS2	(DCRN_CIC_BASE + 0x2)	/* DMA Select2 Register */
#define DCRN_CICVCR	(DCRN_CIC_BASE + 0x3)	/* CIC Video COntro Register */
#define DCRN_CICSEL3	(DCRN_CIC_BASE + 0x5)	/* CIC Select 3 Register */
#define DCRN_SGPO	(DCRN_CIC_BASE + 0x6)	/* CIC GPIO Output Register */
#define DCRN_SGPOD	(DCRN_CIC_BASE + 0x7)	/* CIC GPIO OD Register */
#define DCRN_SGPTC	(DCRN_CIC_BASE + 0x8)	/* CIC GPIO Tristate Ctrl Reg */
#define DCRN_SGPI	(DCRN_CIC_BASE + 0x9)	/* CIC GPIO Input Reg */
#endif

#ifdef DCRN_DCRX_BASE
#define DCRN_DCRXICR	(DCRN_DCRX_BASE + 0x0)	/* Internal Control Register */
#define DCRN_DCRXISR	(DCRN_DCRX_BASE + 0x1)	/* Internal Status Register */
#define DCRN_DCRXECR	(DCRN_DCRX_BASE + 0x2)	/* External Control Register */
#define DCRN_DCRXESR	(DCRN_DCRX_BASE + 0x3)	/* External Status Register */
#define DCRN_DCRXTAR	(DCRN_DCRX_BASE + 0x4)	/* Target Address Register */
#define DCRN_DCRXTDR	(DCRN_DCRX_BASE + 0x5)	/* Target Data Register */
#define DCRN_DCRXIGR	(DCRN_DCRX_BASE + 0x6)	/* Interrupt Generation Register */
#define DCRN_DCRXBCR	(DCRN_DCRX_BASE + 0x7)	/* Line Buffer Control Register */
#endif

#ifdef DCRN_EBC_BASE
#define DCRN_EBCCFGADR	(DCRN_EBC_BASE + 0x0)	/* Peripheral Controller Address */
#define DCRN_EBCCFGDATA	(DCRN_EBC_BASE + 0x1)	/* Peripheral Controller Data */
#endif

#ifdef DCRN_EBIMC_BASE
#define DCRN_BRCRH0	(DCRN_EBIMC_BASE + 0x0)	/* Bus Region Config High 0 */
#define DCRN_BRCRH1	(DCRN_EBIMC_BASE + 0x1)	/* Bus Region Config High 1 */
#define DCRN_BRCRH2	(DCRN_EBIMC_BASE + 0x2)	/* Bus Region Config High 2 */
#define DCRN_BRCRH3	(DCRN_EBIMC_BASE + 0x3)	/* Bus Region Config High 3 */
#define DCRN_BRCRH4	(DCRN_EBIMC_BASE + 0x4)	/* Bus Region Config High 4 */
#define DCRN_BRCRH5	(DCRN_EBIMC_BASE + 0x5)	/* Bus Region Config High 5 */
#define DCRN_BRCRH6	(DCRN_EBIMC_BASE + 0x6)	/* Bus Region Config High 6 */
#define DCRN_BRCRH7	(DCRN_EBIMC_BASE + 0x7)	/* Bus Region Config High 7 */
#define DCRN_BRCR0	(DCRN_EBIMC_BASE + 0x10)	/* BRC 0 */
#define DCRN_BRCR1	(DCRN_EBIMC_BASE + 0x11)	/* BRC 1 */
#define DCRN_BRCR2	(DCRN_EBIMC_BASE + 0x12)	/* BRC 2 */
#define DCRN_BRCR3	(DCRN_EBIMC_BASE + 0x13)	/* BRC 3 */
#define DCRN_BRCR4	(DCRN_EBIMC_BASE + 0x14)	/* BRC 4 */
#define DCRN_BRCR5	(DCRN_EBIMC_BASE + 0x15)	/* BRC 5 */
#define DCRN_BRCR6	(DCRN_EBIMC_BASE + 0x16)	/* BRC 6 */
#define DCRN_BRCR7	(DCRN_EBIMC_BASE + 0x17)	/* BRC 7 */
#define DCRN_BEAR0	(DCRN_EBIMC_BASE + 0x20)	/* Bus Error Address Register */
#define DCRN_BESR0	(DCRN_EBIMC_BASE + 0x21)	/* Bus Error Status Register */
#define DCRN_BIUCR	(DCRN_EBIMC_BASE + 0x2A)	/* Bus Interfac Unit Ctrl Reg */
#endif

#ifdef DCRN_SCCR_BASE
#define DCRN_SCCR	(DCRN_SCCR_BASE + 0x0)
#endif

#ifdef DCRN_SDRAM0_BASE
#define DCRN_SDRAM0_CFGADDR	(DCRN_SDRAM0_BASE + 0x0)	/* Memory Controller Address */
#define DCRN_SDRAM0_CFGDATA	(DCRN_SDRAM0_BASE + 0x1)	/* Memory Controller Data */
#endif

#ifdef DCRN_OCM0_BASE
#define DCRN_OCMISARC	(DCRN_OCM0_BASE + 0x0)	/* OCM Instr Side Addr Range Compare */
#define DCRN_OCMISCR	(DCRN_OCM0_BASE + 0x1)	/* OCM Instr Side Control */
#define DCRN_OCMDSARC	(DCRN_OCM0_BASE + 0x2)	/* OCM Data Side Addr Range Compare */
#define DCRN_OCMDSCR	(DCRN_OCM0_BASE + 0x3)	/* OCM Data Side Control */
#endif

#include <asm/ibm405.h>

#endif				/* __ASM_IBMSTBX_H__ */
#endif				/* __KERNEL__ */
