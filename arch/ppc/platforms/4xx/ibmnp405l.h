/*
 * arch/ppc/platforms/4xx/ibmnp405l.h
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBMNP405L_H__
#define __ASM_IBMNP405L_H__

#include <linux/config.h>

/* serial port defines */
#define RS_TABLE_SIZE	2

#define UART0_INT	0
#define UART1_INT	1

#define UART0_IO_BASE	0xEF600300
#define UART1_IO_BASE	0xEF600400
#define IIC0_BASE	0xEF600500
#define OPB0_BASE	0xEF600600
#define GPIO0_BASE	0xEF600700
#define EMAC0_BASE	0xEF600800
#define EMAC1_BASE	0xEF600900
#define ZMII0_BASE	0xEF600C10
#define BL_MAC_WOL	41	/* WOL */
#define BL_MAL_SERR	45	/* MAL SERR */
#define BL_MAL_TXDE	46	/* MAL TXDE */
#define BL_MAL_RXDE	47	/* MAL RXDE */
#define BL_MAL_TXEOB	17	/* MAL TX EOB */
#define BL_MAL_RXEOB	18	/* MAL RX EOB */
#define BL_MAC_ETH0	37	/* MAC */
#define BL_MAC_ETH1	38	/* MAC */

#define EMAC_NUMS	2

#define IIC0_IRQ	2

#undef NR_UICS
#define NR_UICS	2
#define UIC_CASCADE_MASK 0x0003	/* bits 30 & 31 */

#define BD_EMAC_ADDR(e,i) bi_enetaddr[e][i]

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base:(u8 *) UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_DEBUG_IO_BASE	UART0_IO_BASE
#define SERIAL_PORT_DFNS        \
        STD_UART_OP(0)          \
        STD_UART_OP(1)
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_DEBUG_IO_BASE	UART1_IO_BASE
#define SERIAL_PORT_DFNS        \
        STD_UART_OP(1)          \
        STD_UART_OP(0)
#endif

/* DCR defines */
/* ------------------------------------------------------------------------- */

#define DCRN_CHCR_BASE	0x0F1
#define DCRN_CHPSR_BASE	0x0B4
#define DCRN_CPMSR_BASE	0x0BA
#define DCRN_CPMFR_BASE	0x0B9
#define DCRN_CPMER_BASE	0x0B8

#define IBM_CPM_EMAC0	0x00800000	/* on-chip ethernet MM unit */
#define IBM_CPM_EMAC1	0x00100000	/* EMAC 1 MII */
#define IBM_CPM_UIC1	0x00020000	/* Universal Interrupt Controller */
#define IBM_CPM_UIC0	0x00010000	/* Universal Interrupt Controller */
#define IBM_CPM_CPU	0x00008000	/* processor core */
#define IBM_CPM_EBC	0x00004000	/* ROM/SRAM peripheral controller */
#define IBM_CPM_SDRAM0	0x00002000	/* SDRAM memory controller */
#define IBM_CPM_GPIO0	0x00001000	/* General Purpose IO (??) */
#define IBM_CPM_HDLC	0x00000800	/* HDCL */
#define IBM_CPM_TMRCLK	0x00000400	/* CPU timers */
#define IBM_CPM_PLB	0x00000100	/* PLB bus arbiter */
#define IBM_CPM_OPB	0x00000080	/* PLB to OPB bridge */
#define IBM_CPM_DMA	0x00000040	/* DMA controller */
#define IBM_CPM_IIC0	0x00000010	/* IIC interface */
#define IBM_CPM_UART0	0x00000002	/* serial port 0 */
#define IBM_CPM_UART1	0x00000001	/* serial port 1 */
#define DFLT_IBM4xx_PM	~(IBM_CPM_UIC0 | IBM_CPM_UIC1 | IBM_CPM_CPU	\
			| IBM_CPM_EBC | IBM_CPM_SDRAM0 | IBM_CPM_PLB	\
			| IBM_CPM_OPB | IBM_CPM_TMRCLK | IBM_CPM_DMA	\
			| IBM_CPM_EMAC0 | IBM_CPM_EMAC1)

#define DCRN_DMA0_BASE	0x100
#define DCRN_DMA1_BASE	0x108
#define DCRN_DMA2_BASE	0x110
#define DCRN_DMA3_BASE	0x118
#define DCRNCAP_DMA_SG	1	/* have DMA scatter/gather capability */
#define DCRN_DMASR_BASE	0x120
#define DCRN_EBC_BASE	0x012
#define DCRN_DCP0_BASE	0x014
#define DCRN_MAL_BASE	0x180
#define DCRN_MAL1_BASE	0x200
#define DCRN_OCM0_BASE	0x018
#define DCRN_PLB0_BASE	0x084
#define DCRN_PLLMR_BASE	0x0F0
#define DCRN_POB0_BASE	0x0A0
#define DCRN_SDRAM0_BASE 0x010
#define DCRN_UIC0_BASE	0x0C0
#define DCRN_UIC1_BASE	0x0D0
#define DCRN_CPC0_EPRCSR 0x0F3

#define UIC0_UIC1NC      30	/* UIC1 non-critical interrupt */
#define UIC0_UIC1CR      31	/* UIC1 critical interrupt */

#define CHR1_CETE	0x00000004	/* CPU external timer enable */
#define UIC0	DCRN_UIC0_BASE
#define UIC1	DCRN_UIC1_BASE

#define SDRAM_CFG	0x20
#define SDRAM0_ECCCFG	0x94
#define SDRAM_NO_ECC	0x10000000
#include <asm/ibm405.h>

#endif				/* __ASM_IBMNP405L_H__ */
#endif				/* __KERNEL__ */
