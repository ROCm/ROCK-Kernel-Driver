/*
 * include/asm-ppc/platforms/ibm405lp.h  405LP-specific definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * Bishop Brock
 * IBM Research, Austin Center for Low-Power Computing
 * bcbrock@us.ibm.com
 * March, 2002
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM405LP_H__
#define __ASM_IBM405LP_H__

#include <linux/config.h>
#include <asm/ibm4xx.h>

/* Machine-specific register naming for the 4xx processors is a mess. It seems
   that everyone had a different idea on how to prefix/abbreviate/configure the
   DCR numbers and MMIO addresses.  I'm no different! For the 405LP we have
   defined all of the DCRs and MMIO address consistently with their names as
   documented in the official IBM hardware manual for the processor.

   DCRs are all given a DCRN_ prefix, which seems to be the most
   common consistent naming scheme in old code (although the official IBM DCR
   names are so unique that there's really little need for the DCRN_).

   At the end of the DCR defines several synonyms are defined for backwards
   compatibility, but all new code specific to the 405LP uses the consistent
   names.

   Version 07/24/02 1.1 - Armin
        added default pm define
*/

/*****************************************************************************
 * Directly accessed DCRs
 *****************************************************************************/

/* DCRs used for Indirect Access */

#define DCRN_SDRAM0_CFGADDR 0x010	/* Memory Ctlr. DCR Address Register */
#define DCRN_SDRAM0_CFGDATA 0x011	/* Memory Ctlr. DCR Data Register */
#define DCRN_EBC0_CFGADDR   0x012	/* Peripheral Ctlr. DCR Address Register */
#define DCRN_EBC0_CFGDATA   0x013	/* Peripheral Ctlr. DCR Data Register */
#define DCRN_SLA0_CFGADDR   0x0e0	/* Speech Label Accel. DCR Address Reg. */
#define DCRN_SLA0_CFGDATA   0x0e1	/* Speech Label Accel. DCR Data Reg. */
#define DCRN_LCD0_CFGADDR   0x3c8	/* LCD Ctlr. DCR Address Reg. */
#define DCRN_LCD0_CFGDATA   0x3c9	/* LCD Ctlr. DCR Data Reg. */

/* On-chip Buses */

#define DCRN_PLB0_BESR      0x084	/* PLB Bus Error Status Register */
#define DCRN_PLB0_BEAR      0x086	/* PLB Bus Error Address Register */
#define DCRN_PLB0_ACR       0x087	/* PLB Arbiter Control Register */
#define DCRN_POB0_BESR0     0x0a0	/* PLB to OPB Bus Error Status Register 0 */
#define DCRN_POB0_BEAR      0x0a2	/* PLB to OPB Bus Error Address Register */
#define DCRN_POB0_BESR1     0x0a4	/* PLB to OPB Bus Error Status Register 1 */

/* Clocking and Chip Control */

#define DCRN_CPC0_PLLMR     0x0b0	/* PLL Mode Register */
#define DCRN_CPC0_CGCR0     0x0b1	/* Clock Generation Control Register 0 */
#define DCRN_CPC0_CGCR1     0x0b2	/* Clock Generation Control Register 1 */
#define DCRN_CPC0_CR0       0x0b5	/* Chip Control Register 0 */
#define DCRN_CHCR0 DCRN_CPC0_CR0
#define DCRN_CPC0_CR1       0x0b4	/* Chip Control Register 1 */
#define DCRN_CPC0_PLBAPR    0x0b6	/* PLB Arbiter Priority Register */
#define DCRN_CPC0_JTAGID    0x0b7	/* JTAG ID Register */

/* Clock and Power Management */

#define DCRN_CPMSR_BASE     0x0b8	/* CPM Status Register */
#define DCRN_CPMFR_BASE     0x0ba	/* CPM Force Register */

/* Universal Interrupt Controller */

#define DCRN_UIC0_SR        0x0c0	/* UIC Status Register */
#define DCRN_UIC0_ER        0x0c2	/* UIC Enable Register */
#define DCRN_UIC0_CR        0x0c3	/* UIC Critical Register */
#define DCRN_UIC0_PR        0x0c4	/* UIC Polarity Register */
#define DCRN_UIC0_TR        0x0c5	/* UIC Triggering Register */
#define DCRN_UIC0_MSR       0x0c6	/* UIC Masked Status Register */
#define DCRN_UIC0_VR        0x0c7	/* UIC Vector Register */
#define DCRN_UIC0_VCR       0x0c8	/* UIC Vector Configuration Register */

/* Real-time Clock */

#define DCRN_RTC0_SEC       0x140	/* RTC Seconds Register */
#define DCRN_RTC0_SECAL     0x141	/* RTC Seconds Alarm Register */
#define DCRN_RTC0_MIN       0x142	/* RTC Minutes Register */
#define DCRN_RTC0_MINAL     0x143	/* RTC Minutes Alarm Register */
#define DCRN_RTC0_HR        0x144	/* RTC Hours Register */
#define DCRN_RTC0_HRAL      0x145	/* RTC Hours Alarm Register */
#define DCRN_RTC0_DOW       0x146	/* RTC Day of Week Register */
#define DCRN_RTC0_DOM       0x147	/* RTC Date of Month Register */
#define DCRN_RTC0_MONTH     0x148	/* RTC Month Register */
#define DCRN_RTC0_YEAR      0x149	/* RTC Year Register */
#define DCRN_RTC0_CR0       0x14a	/* RTC "A" Register */
#define DCRN_RTC0_CR1       0x14b	/* RTC "B" Register */
#define DCRN_RTC0_CR2       0x14c	/* RTC "C" Register */
#define DCRN_RTC0_CR3       0x14d	/* RTC "D" Register */
#define DCRN_RTC0_CEN       0x14e	/* RTC Century Register */
#define DCRN_RTC0_WRAP      0x150	/* RTC Wrapper */

/* Advanced Power Management Controller */

#define DCRN_APM0_ISR 	     0x160	/* APM Interrupt Status Register */
#define DCRN_APM0_IER 	     0x162	/* APM Interrupt Enable Register */
#define DCRN_APM0_IPR 	     0x163	/* APM Interrupt Polarity Register */
#define DCRN_APM0_ITR 	     0x164	/* APM Interrupt Trigger Register */
#define DCRN_APM0_CFG  	     0x165	/* APM Configuration Register */
#define DCRN_APM0_SR  	     0x166	/* APM Status Register */
#define DCRN_APM0_ID  	     0x167	/* APM Revision ID Register */

/* Triple DES Controller */

#define DCRN_TDES0_ADDR      0x180	/* TDES OPB Slave Base Address  */
#define DCRN_TDES0_CFG       0x181	/* TDES OPB Slave Configuration */
#define DCRN_TDES0_STAT      0x182	/* TDES Status  */
#define DCRN_TDES0_ID        0x183	/* TDES Core ID */

/* LCD Controller */

#define DCRN_LCD0_CFG        0x3c0	/* LCD Configuration Register */
#define DCRN_LCD0_ICR        0x3c1	/* LCD Interrupt Control Register */
#define DCRN_LCD0_ISR        0x3c2	/* LCD Interrupt Status Register */
#define DCRN_LCD0_IMR        0x3c3	/* LCD Interrupt Mask Register */

/*****************************************************************************
 * Indirectly accessed DCRs. Note that unlike direct-access DCRs whose numbers
 * must be hard-coded into the instruction, indirect-access DCR numbers can be
 * computed.
 *****************************************************************************/

/* Offsets for SDRAM Controler Registers */

#define DCRN_SDRAM0_BESR0  0x00	/* Bus Error Syndrome Register 0 */
#define DCRN_SDRAM0_BESR1  0x08	/* Bus Error Syndrome Register 1 */
#define DCRN_SDRAM0_BEAR   0x10	/* Bus Error Address Register */
#define DCRN_SDRAM0_CFG    0x20	/* Memory Controller Options 1 */
#define DCRN_SDRAM0_STATUS 0x24	/* SDRAM controller status */
#define DCRN_SDRAM0_RTR    0x30	/* Refresh Timer Register */
#define DCRN_SDRAM0_PMIT   0x34	/* Power Management Idle Timer */
#define DCRN_SDRAM0_B0CR   0x40	/* Memory Bank 0 Configuration */
#define DCRN_SDRAM0_B1CR   0x44	/* Memory Bank 1 Configuration */
#define DCRN_SDRAM0_B2CR   0x48	/* Memory Bank 2 Configuration */
#define DCRN_SDRAM0_B3CR   0x4c	/* Memory Bank 3 Configuration */
#define DCRN_SDRAM0_TR     0x80	/* Sdram Timing Register 1 */
#define DCRN_SDRAM0_ECCCFG 0x94	/* ECC Configuration */
#define DCRN_SDRAM0_ECCESR 0x98	/* ECC Error Status Register */

#define SDRAM0_BANKS           4
#define DCRN_SDRAM0_BnCR(bank) (0x40 + (4 * (bank)))

/* Offsets for External Bus Controller Registers */

#define DCRN_EBC0_B0CR  0x00	/* Peripheral Bank 0 Configuration Register */
#define DCRN_EBC0_B1CR  0x01	/* Peripheral Bank 1 Configuration Register */
#define DCRN_EBC0_B2CR  0x02	/* Peripheral Bank 2 Configuration Register */
#define DCRN_EBC0_B3CR  0x03	/* Peripheral Bank 3 Configuration Register */
#define DCRN_EBC0_B4CR  0x04	/* Peripheral Bank 4 Configuration Register */
#define DCRN_EBC0_B5CR  0x05	/* Peripheral Bank 5 Configuration Register */
#define DCRN_EBC0_B6CR  0x06	/* Peripheral Bank 6 Configuration Register */
#define DCRN_EBC0_B7CR  0x07	/* Peripheral Bank 7 Configuration Register */
#define DCRN_EBC0_B0AP  0x10	/* Peripheral Bank 0 Access Parameters */
#define DCRN_EBC0_B1AP  0x11	/* Peripheral Bank 1 Access Parameters */
#define DCRN_EBC0_B2AP  0x12	/* Peripheral Bank 2 Access Parameters */
#define DCRN_EBC0_B3AP  0x13	/* Peripheral Bank 3 Access Parameters */
#define DCRN_EBC0_B4AP  0x14	/* Peripheral Bank 4 Access Parameters */
#define DCRN_EBC0_B5AP  0x15	/* Peripheral Bank 5 Access Parameters */
#define DCRN_EBC0_B6AP  0x16	/* Peripheral Bank 6 Access Parameters */
#define DCRN_EBC0_B7AP  0x17	/* Peripheral Bank 7 Access Parameters */
#define DCRN_EBC0_BEAR  0x20	/* Periperal Bus Error Address Register */
#define DCRN_EBC0_BESR0 0x21	/* Peripheral Bus Error Status Register 0 */
#define DCRN_EBC0_BESR1 0x22	/* Peripheral Bus Error Status Register 0 */
#define DCRN_EBC0_CFG   0x23	/* External Peripheral Control Register */

#define EBC0_BANKS           8
#define DCRN_EBC0_BnCR(bank) (bank)
#define DCRN_EBC0_BnAP(bank) (0x10 + (bank))

/* Offsets for LCD Controller DCRs */

#define DCRN_LCD0_DER    0x80010000	/* Display Enable Regsiter */
#define DCRN_LCD0_DCFG   0x80010010	/* Display Configuration Register */
#define DCRN_LCD0_DSR    0x80010040	/* Display Status Register */
#define DCRN_LCD0_FRDR   0x80010080	/* Dither and Frame Rate Modulation Reg. */
#define DCRN_LCD0_SDR    0x800100c0	/* Signal Delay Register */
#define DCRN_LCD0_ADSR   0x80010100	/* Active Display Size Register */
#define DCRN_LCD0_TDSR   0x80010104	/* Total Display Size Register */
#define DCRN_LCD0_FPLCR  0x80010140	/* FPLINE Control Register */
#define DCRN_LCD0_FPLOR  0x80010144	/* FPLINE Offset Register */
#define DCRN_LCD0_FPFCR  0x80010148	/* FPFRAME Control Register */
#define DCRN_LCD0_FPFOR  0x8001014c	/* FPFRAME Control Register */
#define DCRN_LCD0_FPSCR  0x80010150	/* FPSHIFT Control Register */
#define DCRN_LCD0_FPDRR  0x80010158	/* FPDRDY Control Register */
#define DCRN_LCD0_FPDCR  0x80010160	/* FPDATA Control Register */
#define DCRN_LCD0_PFBFR  0x80010800	/* Pixel and Frame Buffer Format Reg. */
#define DCRN_LCD0_PFR    0x80011000	/* Pixel Format Register */
#define DCRN_LCD0_FBBAR  0x80011008	/* Frame Buffer Base Address Register */
#define DCRN_LCD0_STRIDE 0x8001100c	/* Stride Register */
#define DCRN_LCD0_PAR    0x80011800	/* Palette Access Registers Base */
#define DCRN_LCD0_CER    0x80012000	/* Cursor Enable Register */
#define DCRN_LCD0_CBAR   0x80012008	/* Cursor Base Address Register */
#define DCRN_LCD0_CLR    0x8001200c	/* Cursor Location Register */
#define DCRN_LCD0_CC0R   0x80012010	/* Cursor Color 0 */
#define DCRN_LCD0_CC1R   0x80012014	/* Cursor Color 1 */

#define LCD0_PAR_REGS     256
#define DCRN_LCD0_PARn(n) (DCRN_LCD0_PAR + (4 * (n)))

/* Offsets for Decompression Controller DCRs */

#define DCRN_DCP0_ITOR0   0x00	/* Index Table Origin Register 0 */
#define DCRN_DCP0_ITOR1   0x01	/* Index Table Origin Register 1 */
#define DCRN_DCP0_ITOR2   0x02	/* Index Table Origin Register 2 */
#define DCRN_DCP0_ITOR3   0x03	/* Index Table Origin Register 3 */
#define DCRN_DCP0_ADDR0   0x04	/* Address Decode Definition Register 0 */
#define DCRN_DCP0_ADDR1   0x05	/* Address Decode Definition Register 1 */
#define DCRN_DCP0_CFG     0x40	/* Decompression Controller Cfg. Register */
#define DCRN_DCP0_ID      0x41	/* Decompression Controller ID Register */
#define DCRN_DCP0_VER     0x42	/* Decompression Controller Version Register */
#define DCRN_DCP0_PLBBEAR 0x50	/* Bus Error Address Register (PLB) */
#define DCRN_DCP0_MEMBEAR 0x51	/* Bus Error Address Register (EBC/SDRAM) */
#define DCRN_DCP0_ESR     0x52	/* Bus Error Status Register 0 (Masters 0-3) */

#define DCRN_DCP0_RAMn(n) (0x400 + (n))	/* Decompression Decode Table Entries
					   0x400-0x5FF Low 16-bit decode table
					   0x600-0x7FF High 16-bit decode table
					 */

/* Offsets for Speech Label Accelerator DCRs */

#define DCRN_SLA0_CR    0x00	/* SLA Control Register */
#define DCRN_SLA0_SR    0x01	/* SLA Status Register */
#define DCRN_SLA0_BESR  0x02	/* SLA Bus Error Status Register */
#define DCRN_SLA0_BEAR  0x03	/* SLA Bus Error Address Register */
#define DCRN_SLA0_UADDR 0x04	/* SLA PLB Upper Address Register */
#define DCRN_SLA0_GMBA  0x05	/* SLA General Indirect Memory Base Address */
#define DCRN_SLA0_GMLL  0x06	/* SLA General Indirect Memory Link List */
#define DCRN_SLA0_AMBA  0x07	/* SLA Atom Memory Base Address Register */
#define DCRN_SLA0_ACBA  0x08	/* SLA Accumulator Base Address Register */
#define DCRN_SLA0_DIBA  0x09	/* SLA Done Indication Base Address Register */
#define DCRN_SLA0_GPOFF 0x0A	/* SLA General Indirect Pass Offset Register */
#define DCRN_SLA0_SLPMD 0x0B	/* SLA Sleep Mode Control Register */
#define DCRN_SLA0_ID    0x0C	/* SLA ID Register */
#define DCRN_SLA0_GMLLR 0x0D	/* SLA General Indirect Memory Link List Reset */

#define DCRN_DMA0_BASE	0x100
#define DCRN_DMA1_BASE	0x108
#define DCRN_DMA2_BASE	0x110
#define DCRN_DMA3_BASE	0x118
#define DCRNCAP_DMA_SG	1	/* have DMA scatter/gather capability */
#define DCRN_DMASR_BASE	0x120
#define DCRN_EBC_BASE	0x012
#define DCRN_DCP0_BASE	0x014
#define DCRN_UIC0_BASE	0x0C0

#define UIC0	DCRN_UIC0_BASE

#undef NR_UICS
#define NR_UICS	1

/* More memory-mapped I/O bases, etc., esp. for OCP, that should be moved
   elsewhere. */

#define IIC0_BASE	0xEF600500
#define OPB0_BASE	0xEF600600
#define GPIO0_BASE	0xEF600700

#define IIC0_IRQ	2

/****************************************************************************
 * MMIO Addresses
 ***************************************************************************/

/* Touch Panel/PWM Controller */

#define TPC0_IO_BASE   0xef600a00

#define TPC_CR   0x00		/* TPC Command Register */
#define TPC_PCRX 0x04		/* TPC Precharge Count Register X1 */
#define TPC_DCRX 0x08		/* TPC Discharge Count Register X1 */
#define TPC_PCRY 0x0c		/* TPC Precharge Count Register Y1 */
#define TPC_DCRY 0x10		/* TPC Discharge Count Register Y1 */
#define TPC_RRX  0x14		/* TPC Read Register X1 */
#define TPC_RRY  0x18		/* TPC Read Register Y1 */
#define TPC_SRX  0x1c		/* TPC Status Register X1 */
#define TPC_SRY  0x20		/* TPC Status Register Y1 */

/* Triple-DES Controller */

#define TDES0_IO_BASE  0xef600b00

/*****************************************************************************
 * CPM bits for the 405LP.
 *****************************************************************************/

#define CPM_BITMASK(i) (((unsigned)0x80000000) >> i)

#define IBM_CPM_IIC0   CPM_BITMASK(0)	/* IIC Interface */
#define IBM_CPM_CPU    CPM_BITMASK(1)	/* Processor Core */
#define IBM_CPM_DMA    CPM_BITMASK(3)	/* DMA Controller */
#define IBM_CPM_OPB    CPM_BITMASK(4)	/* PLB to OPB Bridge */
#define IBM_CPM_DCP    CPM_BITMASK(5)	/* CodePack */
#define IBM_CPM_EBC    CPM_BITMASK(6)	/* ROM/SRAM Peripheral Controller */
#define IBM_CPM_SDRAM0 CPM_BITMASK(7)	/* SDRAM memory controller */
#define IBM_CPM_PLB    CPM_BITMASK(8)	/* PLB bus arbiter */
#define IBM_CPM_GPIO0  CPM_BITMASK(9)	/* General Purpose IO (??) */
#define IBM_CPM_UART0  CPM_BITMASK(10)	/* Serial Port 0 */
#define IBM_CPM_UART1  CPM_BITMASK(11)	/* Serial Port 1 */
#define IBM_CPM_UIC    CPM_BITMASK(12)	/* Universal Interrupt Controller */
#define IBM_CPM_TMRCLK CPM_BITMASK(13)	/* CPU Timers */
#define IBM_CPM_SLA    CPM_BITMASK(14)	/* Speech Label Accelerator */
#define IBM_CPM_CSI    CPM_BITMASK(15)	/* CODEC Serial Interface */
#define IBM_CPM_TPC    CPM_BITMASK(16)	/* Touch Panel Controller */
#define IBM_CPM_TDES   CPM_BITMASK(18)	/* Triple DES */

#define DFLT_IBM4xx_PM 0 /* for now until we get a better hable on this one - armin */

/*****************************************************************************
 * UIC IRQ ordinals for the 405LP.  IRQ bit names are as documented in the
 * 405LP manual (except for reserved fields).  Backwards-compatible synonyms
 * appear at the end.
 *****************************************************************************/

#define UIC_IRQ_U0    0		/* UART0 */
#define UIC_IRQ_U1    1		/* UART1 */
#define UIC_IRQ_IIC   2		/* IIC */
#define UIC_IRQ_EM    3		/* EBC ??? */
#define UIC_IRQ_IRQ4  4		/* Reserved */
#define UIC_IRQ_D0    5		/* DMA Channel 0 */
#define UIC_IRQ_D1    6		/* DMA Channel 1 */
#define UIC_IRQ_D2    7		/* DMA Channel 2 */
#define UIC_IRQ_D3    8		/* DMA Channel 3 */
#define UIC_IRQ_IRQ9  9		/* Reserved */
#define UIC_IRQ_IRQ10 10	/* Reserved */
#define UIC_IRQ_IRQ11 11	/* Reserved */
#define UIC_IRQ_IRQ12 12	/* Reserved */
#define UIC_IRQ_IRQ13 13	/* Reserved */
#define UIC_IRQ_IRQ14 14	/* Reserved */
#define UIC_IRQ_IRQ15 15	/* Reserved */
#define UIC_IRQ_IRQ16 16	/* Reserved */
#define UIC_IRQ_EC    17	/* ECC Correctable Error ??? */
#define UIC_IRQ_TPX   18	/* Touch Panel X */
#define UIC_IRQ_TPY   19	/* Touch Panel Y */
#define UIC_IRQ_SLA   20	/* SLA Interrupt */
#define UIC_IRQ_CSI   21	/* CSI Interrupt */
#define UIC_IRQ_LCD   22	/* LCD Interrupt */
#define UIC_IRQ_RTC   23	/* RTC Interrupt */
#define UIC_IRQ_APM   24	/* APM Interrupt */
#define UIC_IRQ_EIR0  25	/* External IRQ 0 */
#define UIC_IRQ_EIR1  26	/* External IRQ 1 */
#define UIC_IRQ_EIR2  27	/* External IRQ 2 */
#define UIC_IRQ_EIR3  28	/* External IRQ 3 */
#define UIC_IRQ_EIR4  29	/* External IRQ 4 */
#define UIC_IRQ_EIR5  30	/* External IRQ 5 */
#define UIC_IRQ_EIR6  31	/* External IRQ 6 */

/*****************************************************************************
 * Serial port definitions
 *****************************************************************************/

#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE	64
#else
#define RS_TABLE_SIZE	4
#endif

#define UART0_INT	UIC_IRQ_U0
#define UART1_INT	UIC_IRQ_U1
#define UART0_IO_BASE	0xEF600300
#define UART1_IO_BASE	0xEF600400

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base:(u8 *) UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_DEBUG_IO_BASE    UART0_IO_BASE
#define SERIAL_PORT_DFNS        \
        STD_UART_OP(0)          \
        STD_UART_OP(1)
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_DEBUG_IO_BASE    UART1_IO_BASE
#define SERIAL_PORT_DFNS        \
        STD_UART_OP(1)          \
        STD_UART_OP(0)
#endif

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/system.h>

/****************************************************************************
 * DCR type structures and field definitions for DCRs manipulated by the 405LP
 * Linux port
 ****************************************************************************/

/* APM0_CFG - APM Configuration Register */

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd:17;
		unsigned int isp:1;	/* Initiate Sleep */
		unsigned int ewt:1;	/* Enable Watchdog Timer */
		unsigned int sm:2;	/* Sleep Mode */
		unsigned int iica:3;	/* I2C Address (low-order 3 bits) */
		unsigned int psc:1;	/* Power Select Control */
		unsigned int cdiv:6;	/* IIC Clock Divider */
		unsigned int v:1;	/* Valid bit */
	} fields;
} apm0_cfg_t;

#define APM0_CFG_MASK 0xffff8000	/* AND to clear all non-reserved fields */

/* APM0_SR - APM Status Register */

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd:17;
		unsigned int cdet:1;	/* Clock Detect */
		unsigned int en:1;	/* APM Enable Indicator */
		unsigned int rset:1;	/* Processor Reset by APM? */
		unsigned int pfr:1;	/* Power Fail Reset? */
		unsigned int rsrt:1;	/* Restart Successful? */
		unsigned int sdwn:1;	/* Shutdown Complete */
		unsigned int errc:8;	/* Error Code */
		unsigned int v:1;	/* Valid Bit */
	} fields;
} apm0_sr_t;

#define APM0_SR_MASK 0xffff8000	/* AND to clear all non-reserved fields */

/* APM0_IER -- APM Interrupt Enable Register
   APM0_IPR -- APM Interrupt Polarity Register
   APM0_ISR -- APM Interrupt Status Register
   APM0_ITR -- APM Interrupt Trigger Register

   The interrupts are also accessed via standard interrupt numbers:

   59 : Wake-up Input 0
   60 : Wake-up Input 1
   61 : Wake-up Input 2
   62 : Real-Time Clock Interrupt
*/

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd:27;
		unsigned int wi0e:1;
		unsigned int wi1e:1;
		unsigned int wi2e:1;
		unsigned int cie:1;
		unsigned int v:1;
	} fields;
} apm0_ier_t;

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd:27;
		unsigned int wi0p:1;
		unsigned int wi1p:1;
		unsigned int wi2p:1;
		unsigned int cip:1;
		unsigned int v:1;
	} fields;
} apm0_ipr_t;

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd:27;
		unsigned int wi0s:1;
		unsigned int wi1s:1;
		unsigned int wi2s:1;
		unsigned int cis:1;
		unsigned int v:1;
	} fields;
} apm0_isr_t;

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd:27;
		unsigned int wi0t:1;
		unsigned int wi1t:1;
		unsigned int wi2t:1;
		unsigned int cit:1;
		unsigned int v:1;
	} fields;
} apm0_itr_t;

#define APM0_IER_MASK 0xffffffe0	/* AND to clear all non-reserved fields */
#define APM0_IPR_MASK 0xffffffe0	/* AND to clear all non-reserved fields */
#define APM0_ISR_MASK 0xffffffe0	/* AND to clear all non-reserved fields */
#define APM0_ITR_MASK 0xffffffe0	/* AND to clear all non-reserved fields */

/* CPC0_PLLMR - PLL Mode Register */

typedef union {
	u32 reg;
	struct {
		unsigned int pmul:5;	/* PLL Multiplier */
		unsigned int pdiv:5;	/* PLL Divider */
		unsigned int tun:10;	/* PLL Tuning Control */
		unsigned int db2:1;	/* Divide VCO by 2 Select */
		unsigned int csel:2;	/* PLL Clock Output Select */
		unsigned int rsvd:8;	/* Reserved */
		unsigned int v:1;	/* Valid bit */
	} fields;
} cpc0_pllmr_t;

#define CPC0_PLLMR_MASK 0x000001fe	/* AND to clear all non-reserved fields */
#define CPC0_PLLMR_RTVFS_MASK CPC0_PLLMR_MASK	/* All bits controlled by RTVFS */

/* The PLL multiplier/divider are always multiples of 4. */

#define CPC0_PLLMR_MULDIV_ENCODE(n) ((((unsigned)(n)) / 4) - 1)
#define CPC0_PLLMR_MULDIV_DECODE(n) (((n) + 1) * 4)
#define CPC0_PLLMR_MULDIV_MAX 128

#define CPC0_PLLMR_TUN_HIGH 0x200	/* High-band tuning */
#define CPC0_PLLMR_TUN_LOW  0x000	/* Low-band tuning */

#define CPC0_PLLMR_CSEL_REFCLK  0	/* System Reference Clock */
#define CPC0_PLLMR_CSEL_PLLVCO  1	/* PLL VCO */
#define CPC0_PLLMR_CSEL_RTC     2	/* RTC */
#define CPC0_PLLMR_CSEL_EBCPLB5 3	/* EBC-PLB divisor is 5 ??? */

/* CPC0_CGCR0 - Clock Generation and Control Register 0 */

typedef union {
	u32 reg;
	struct {
		unsigned int pcp:5;	/* Proc. Core/PLB Clock Divisor */
		unsigned int pcsc:5;	/* Proc. Core/SysClkOut Divisor */
		unsigned int pcu:5;	/* Proc. Core/UARTSerClk Clock Div. */
		unsigned int u0cs:1;	/* UART0 Clock Select */
		unsigned int u1cs:1;	/* UART1 Clock Select */
		unsigned int scsel:2;	/* SysClkOut Select */
		unsigned int rsvd:13;	/* Reserved */
	} fields;
} cpc0_cgcr0_t;

#define CPC0_CGCR0_MASK 0x00001fff	/* AND to clear all non-reserved fields */
#define CPC0_CGCR0_RTVFS_MASK 0x0001ffff	/* AND to clear all rtvfs-modified
						   fields */

#define CPC0_CGCR0_SCSEL_OFF  0	/* SysClkOut driven low (low power) */
#define CPC0_CGCR0_SCSEL_CPU  1	/* Select CPU clock as SysClkOut */
#define CPC0_CGCR0_SCSEL_PLB  2	/* SysClkOut is PLB Sample Cycle */
#define CPC0_CGCR0_SCSEL_OPB  3	/* SysClkOut is OPB Sample Cycle */

/* CPC0_CGCR1 - Clock Generation and Control Register 1 */

typedef union {
	u32 reg;
	struct {
		unsigned int po:5;	/* PLB/OPB Clock Divisor */
		unsigned int pext:5;	/* PLB/External Clock Divisor */
		unsigned int ppxl:5;	/* PLB/LCD Pixel Clock Divisor */
		unsigned int csel:2;	/* PerClk Select */
		unsigned int rsvd:15;	/* Reserved */
	} fields;
} cpc0_cgcr1_t;

#define CPC0_CGCR1_MASK 0x00007fff	/* AND to clear all non-reserved fields */
#define CPC0_CGCR1_RTVFS_MASK 0x0001ffff	/* AND to clear all rtvfs-modified
						   fields */

/* 5-bit clock dividers are directly encoded, except that an encoding of 0
   indicates divide-by-32. */

#define CPC0_DIV_MAX      32
#define CPC0_DIV_VALID(n) (((n) > 0) && ((n) <= CPC0_DIV_MAX))
#define CPC0_DIV_ENCODE(n) (((unsigned)(n) >= CPC0_DIV_MAX) ? 0 : (unsigned)(n))
#define CPC0_DIV_DECODE(n) (((n) == 0) ? CPC0_DIV_MAX : (n))

#define CPC0_CGCR1_CSEL_OFF    0	/* PerClk driven low (low power) */
#define CPC0_CGCR1_CSEL_PERCLK 1	/* Select PerClk */
#define CPC0_CGCR1_CSEL_PLBCLK 2	/* Select PLB clock */
#define CPC0_CGCR1_CSEL_OPBCLK 3	/* Select OPB clock */

/* CPC0_CR0 - Chip Control Register 0 */

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd0:7;	/* Reserved */
		unsigned int ssr:1;	/* SDRAM Self-Refresh on Sleep Req. */
		unsigned int gpms:2;	/* GPIO Pin Muxing Select */
		unsigned int u0pms:2;	/* UART0 Pin Muxing Select */
		unsigned int u1pms:2;	/* UART1 Pin Muxing Select */
		unsigned int ipms:2;	/* IIC Pin Muxing Select */
		unsigned int cpms:2;	/* CSI Pin Muxing Select */
		unsigned int tpms:2;	/* TPC Pin Muxing Select */
		unsigned int irpms:2;	/* IRQ Pin Muxing Select */
		unsigned int pcmd:1;	/* PCMCIA Mode Disable */
		unsigned int u0dte:1;	/* UART0 DMA Transmit Channel Enable */
		unsigned int u0rde:1;	/* UART0 DMA Receive Channel Enable */
		unsigned int u0dce:1;	/* UART0 DMA CLear on Enable */
		unsigned int rsvd1:6;	/* Reserved */
	} fields;
} cpc0_cr0_t;

#define CPC0_CR0_MASK 0xfe00003f	/* AND to clear all non-reserved fields */

/* CPC0_CR1 - Chip Control Register 1 */

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd0:28;	/* Reserved */
		unsigned int tbsed:1;	/* TB. Src. in Edge Detect Mode */
		unsigned int edmd:1;	/* TB. Src. Edge Detect Mode Disable */
		unsigned int rsvd1:2;	/* Reserved */
	} fields;
} cpc0_cr1_t;

#define CPC0_CR1_MASK 0xfffffff3	/* AND to clear all non-reserved fields */

/* DCP0_CFG - DCP Configuration Register */

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd0:18;
		unsigned int sldy:10;	/* Sleep Delay */
		unsigned int slen:1;	/* Sleep Enable */
		unsigned int cdb:1;	/* Clear Decompression Buffer */
		unsigned int rsvd1:1;
		unsigned int ikb:1;	/* Enable Decompression */
	} fields;
} dcp0_cfg_t;

#define DCP0_CFG_MASK 0xffffc002	/* AND to clear all non-reserved fields */

/* DMA0_SLP - DMA Sleep Mode Register */

typedef union {
	u32 reg;
	struct {
		unsigned idu:5;	/* Idle Timer Upper */
		unsigned rsvd0:5;
		unsigned sme:1;	/* Sleep Mode Enable */
		unsigned rsvd1:21;
	} fields;
} dma0_slp_t;

#define DMA0_SLP_MASK 0x07dfffff	/* AND to clear all non-reserved fields */

/* EBC0_BnAP - EBC Bank Access Parameters */

typedef union {
	u32 reg;
	struct {
		unsigned bme:1;	/* Burst Mode Enable */
		unsigned twt:8;	/* Transfer Wait (non-burst) */
		unsigned rsvd0:3;
		unsigned csn:2;	/* Chip Select On Timing */
		unsigned oen:2;	/* Output Enable On Timing */
		unsigned wbn:2;	/* Write Byte Enable On Timing */
		unsigned wbf:2;	/* Write Byte Enable Off Timing */
		unsigned th:3;	/* Transfer Hold */
		unsigned re:1;	/* Ready Enable */
		unsigned sor:1;	/* Sample On Ready */
		unsigned bem:1;	/* Byte Enable Mode */
		unsigned pen:1;	/* Parity Enable */
		unsigned rsvd1:5;
	} fields;
} ebc0_bnap_t;

#define EBC0_BnAP_MASK 0x0070001f	/* AND to clear all non-reserved fields */

/* EBC0_BnCR - EBC Bank Configuration Registers */

typedef union {
	u32 reg;
	struct {
		unsigned bas:12;	/* Base Address */
		unsigned bs:3;	/* Bank Size */
		unsigned bu:2;	/* Bank Usage */
		unsigned bw:2;	/* Bank Width */
		unsigned rsvd:13;
	} fields;
} ebc0_bncr_t;

#define EBC0_BnCR_MASK 0x00001fff	/* AND to clear all non-reserved fields */

#define EBC0_BnCR_BS_1MB   0
#define EBC0_BnCR_BS_2MB   1
#define EBC0_BnCR_BS_4MB   2
#define EBC0_BnCR_BS_8MB   3
#define EBC0_BnCR_BS_16MB  4
#define EBC0_BnCR_BS_32MB  5
#define EBC0_BnCR_BS_64MB  6
#define EBC0_BnCR_BS_128MB 7

#define EBC0_BnCR_BU_R  1
#define EBC0_BnCR_BU_W  2
#define EBC0_BnCR_BU_RW 3

#define EBC0_BnCR_BW_8  0
#define EBC0_BnCR_BW_16 1
#define EBC0_BnCR_BW_32 2

/* EBC0_CFG -EBC Configuration Register */

typedef union {
	u32 reg;
	struct {
		unsigned ebtc:1;	/* External Bus Three State Control */
		unsigned ptd:1;	/* Device-paced Time-out Disable */
		unsigned rtc:3;	/* Ready Timeout Count */
		unsigned rsvd0:4;
		unsigned cstc:1;	/* Chip Select Three State Control */
		unsigned bpf:2;	/* Burst Prefetch */
		unsigned rsvd1:2;
		unsigned pme:1;	/* Power Management Enable */
		unsigned pmt:5;	/* Power Management Timer */
		unsigned rsvd2:12;
	} fields;
} ebc0_cfg_t;

#define EBC0_CFG_MASK 0x078c0fff	/* AND to clear all non-reserved fields */

#define EBC0_CFG_RTC_16   0
#define EBC0_CFG_RTC_32   1
#define EBC0_CFG_RTC_64   2
#define EBC0_CFG_RTC_128  3
#define EBC0_CFG_RTC_256  4
#define EBC0_CFG_RTC_512  5
#define EBC0_CFG_RTC_1024 6
#define EBC0_CFG_RTC_2048 7

/* SDRAM0_CFG - SDRAM Controller Configuration Register */

typedef union {
	u32 reg;
	struct {
		unsigned int dce:1;	/* SDRAM Controller Enable */
		unsigned int sre:1;	/* Self-Refresh Enable */
		unsigned int pme:1;	/* Power Management Enable */
		unsigned int rsvd0:1;
		unsigned int regen:1;	/* Registered Memory Enable */
		unsigned int drw:2;	/* SDRAM Width */
		unsigned int brpf:2;	/* Burst Read Prefetch Granularity */
		unsigned int rsvd1:1;
		unsigned int emdulr:1;	/* Enable Memory Data Unless Read */
		unsigned int rsvd2:21;
	} fields;
} sdram0_cfg_t;

#define SDRAM0_CFG_MASK 0x106fffff	/* AND to clear all non-reserved fields */

#define SDRAM0_CFG_BRPF_16 1
#define SDRAM0_CFG_BRPF_32 2

/* SDRAM0_PMIT - SDRAM Power Management Idle Timer */

typedef union {
	u32 reg;
	struct {
		unsigned int cnt:5;	/* Cycle Count Before Sleep Request */
		unsigned int rsvd:27;
	} fields;
} sdram0_pmit_t;

#define SDRAM0_PMIT_MASK 0x07ffffff	/* AND to clear all non-reserved fields */

/* SDRAM0_RTR - Refresh timer register */

typedef union {
	u32 reg;
	struct {
		unsigned rsvd0:2;
		unsigned iv:11;
		unsigned rsvd1:19;
	} fields;
} sdram0_rtr_t;

#define SDRAM0_RTR_MASK 0xc007ffff	/* AND to clear non-reserved fields */
#define SDRAM0_RTR_RTVFS_MASK SDRAM0_RTR_MASK

#define SDRAM0_RTR_IV(n) (((n) & 0x3ff8) >> 2)

/* SDRAM0_TR - SDRAM Timing Register */

typedef union {
	u32 reg;
	struct {
		unsigned int rsvd0:7;
		unsigned int casl:2;	/* CAS Latency */
		unsigned int rsvd1:3;
		unsigned int pta:2;	/* Precharge-to-activate */
		unsigned int ctp:2;	/* Read/Write to Precharge */
		unsigned int ldf:2;	/* Command Leadoff */
		unsigned int rsvd2:9;
		unsigned int rfta:3;	/* Refresh-to-Activate */
		unsigned int rcd:2;	/* RAS-CAS Delay */
	} fields;
} sdram0_tr_t;

#define SDRAM0_TR_MASK 0xfe703fe0	/* AND to clear non-reserved fields */
#define SDRAM0_TR_RTVFS_MASK SDRAM0_TR_MASK

#define SDRAM0_TR_ENCODE(n) ((n) - 1)
#define SDRAM0_TR_ENCODE_RFTA(n) ((n) - 4)

/* SLA0_SLPMD - SLA Sleep Mode Control Register */

typedef union {
	u32 reg;
	struct {
		unsigned slcr:5;	/* Sleep Counter */
		unsigned rsvd0:5;
		unsigned slen:1;	/* Sleep Mode Enable */
		unsigned rsvd1:21;
	} fields;
} sla0_slpmd_t;

#define SLA0_SLPMD_MASK 0x07dfffff	/* AND to clear all non-reserved fields */

/* Several direct-write DCRs on the 405LP have an interlock requirement,
   implemented by a "valid" bit in the low-order bit.  This routine handles the
   handshaking for these registers, by

   1) Rewriting the current value with the valid bit clear;
   2) Rewriting the new value with the valid bit clear;
   3) Rewriting the new value with the valid bit set.

   The mask is a mask with 1s in every reserved bit position.

   NB: This routine always writes the register with the valid bit set,
       regardless of the valid bit setting in the 'new' parameter.

   Unfortunately this must be a macro to work (due to mtdcr()).

   Note that for APM registers, it takes multiple RTC clock cycles for the DCR
   writes to take effect.  Any time delays after writes to APM are the
   resonsibility of the caller.
*/

#define mtdcr_interlock(dcrn, new, mask)                                    \
do {                                                                        \
	u32 __old, __new;                                                   \
	                                                                    \
	__old = mfdcr(dcrn);                                                \
	mtdcr(dcrn, __old & 0xfffffffe);                                    \
	__new = ((__old & (mask)) | ((new) & ~(mask))) & 0xfffffffe;        \
	mtdcr(dcrn, __new);                                                 \
	mtdcr(dcrn, __new | 1);                                             \
} while (0)

/****************************************************************************
 * Power Managament Routines
 ****************************************************************************/

int ibm405lp_set_pixclk(unsigned pixclk_min, unsigned pixclk_max);

void ibm405lp_reset_sdram(u32 new_rtr, u32 new_tr);

extern int (*set_pixclk_hook) (unsigned pixclk_min, unsigned pixclk_max);
extern unsigned last_pixclk_min;
extern unsigned last_pixclk_max;

#endif				/* __ASSEMBLY__ */

#include <asm/ibm405.h>

#endif				/* __ASM_IBM405LP_H__ */
#endif				/* __KERNEL__ */
