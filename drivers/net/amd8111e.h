/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 
 * USA

Module Name:

    amd8111e.h

Abstract:
	
 	 AMD8111 based 10/100 Ethernet Controller driver definitions. 

Environment:
    
	Kernel Mode

Revision History:

*/

#ifndef _AMD811E_H
#define _AMD811E_H

/* Hardware definitions */

#define B31_MASK	0x80000000
#define B30_MASK	0X40000000
#define B29_MASK	0x20000000
#define B28_MASK	0x10000000
#define B27_MASK	0x08000000
#define B26_MASK	0x04000000
#define B25_MASK	0x02000000
#define B24_MASK	0x01000000
#define B23_MASK	0x00800000
#define B22_MASK	0x00400000
#define B21_MASK	0x00200000
#define B20_MASK	0x00100000
#define B19_MASK	0x00080000
#define B18_MASK	0x00040000
#define B17_MASK	0x00020000
#define B16_MASK	0x00010000

#define B15_MASK	0x8000
#define B14_MASK	0x4000
#define B13_MASK	0x2000
#define B12_MASK	0x1000
#define B11_MASK	0x0800
#define B10_MASK	0x0400
#define B9_MASK		0x0200
#define B8_MASK		0x0100
#define B7_MASK		0x0080
#define B6_MASK		0x0040
#define B5_MASK		0x0020
#define B4_MASK		0x0010
#define B3_MASK		0x0008
#define B2_MASK		0x0004
#define B1_MASK		0x0002
#define B0_MASK		0x0001

/* PCI register offset */
#define PCI_ID_REG		0x00
#define PCI_COMMAND_REG		0x04
/* #define MEMEN_BIT		B1_MASK */
/* #define IOEN_BIT		B0_MASK */
#define PCI_REV_ID_REG		0x08
#define PCI_MEM_BASE_REG	0x10
/* #define MEMBASE_MASK		0xFFFFF000 */
/* #define MEMBASE_SIZE		4096 */
#define PCI_INTR_REG		0x3C
#define PCI_STATUS_REG		0x06
#define PCI_CAP_ID_REG_OFFSET	0x34
#define PCI_PMC_REG_OFFSET	0x36
#define PCI_PMCSR_REG_OFFSET	0x38

/* #define NEW_CAP		0x0010  */
#define PME_EN			0x0100

#define PARTID_MASK		0xFFFFF000
#define PARTID_START_BIT	12

/* #define LANCE_DWIO_RESET_PORT	0x18
#define LANCE_WIO_RESET_PORT	0x14 */
#define MIB_OFFSET		0x28

/* Command style register access

Registers CMD0, CMD2, CMD3,CMD7 and INTEN0 uses a write access technique called command style access. It allows the write to selected bits of this register without altering the bits that are not selected. Command style registers are divided into 4 bytes that can be written independently. Higher order bit of each byte is the  value bit that specifies the value that will be written into the selected bits of register. 

eg., if the value 10011010b is written into the least significant byte of a command style register, bits 1,3 and 4 of the register will be set to 1, and the other bits will not be altered. If the value 00011010b is written into the same byte, bits 1,3 and 4 will be cleared to 0 and the other bits will not be altered.

*/

/*  Offset for Memory Mapped Registers. */
/* 32 bit registers */

#define  ASF_STAT		0x00	/* ASF status register */
#define CHIPID			0x04	/* Chip ID regsiter */
#define	MIB_DATA		0x10	/* MIB data register */
#define MIB_ADDR		0x14	/* MIB address register */
#define STAT0			0x30	/* Status0 register */
#define INT0			0x38	/* Interrupt0 register */
#define INTEN0			0x40	/* Interrupt0  enable register*/
#define CMD0			0x48	/* Command0 register */
#define CMD2			0x50	/* Command2 register */
#define CMD3			0x54	/* Command3 resiter */
#define CMD7			0x64	/* Command7 register */

#define CTRL1 			0x6C	/* Control1 register */
#define CTRL2 			0x70	/* Control2 register */

#define XMT_RING_LIMIT		0x7C	/* Transmit ring limit register */

#define AUTOPOLL0		0x88	/* Auto-poll0 register */
#define AUTOPOLL1		0x8A	/* Auto-poll1 register */
#define AUTOPOLL2		0x8C	/* Auto-poll2 register */
#define AUTOPOLL3		0x8E	/* Auto-poll3 register */
#define AUTOPOLL4		0x90	/* Auto-poll4 register */
#define	AUTOPOLL5		0x92	/* Auto-poll5 register */

#define AP_VALUE		0x98	/* Auto-poll value register */
#define DLY_INT_A		0xA8	/* Group A delayed interrupt register */
#define DLY_INT_B		0xAC	/* Group B delayed interrupt register */

#define FLOW_CONTROL		0xC8	/* Flow control register */
#define PHY_ACCESS		0xD0	/* PHY access register */

#define STVAL			0xD8	/* Software timer value register */

#define XMT_RING_BASE_ADDR0	0x100	/* Transmit ring0 base addr register */
#define XMT_RING_BASE_ADDR1	0x108	/* Transmit ring1 base addr register */
#define XMT_RING_BASE_ADDR2	0x110	/* Transmit ring2 base addr register */
#define XMT_RING_BASE_ADDR3	0x118	/* Transmit ring2 base addr register */

#define RCV_RING_BASE_ADDR0	0x120	/* Transmit ring0 base addr register */

#define PMAT0			0x190	/* OnNow pattern register0 */
#define PMAT1			0x194	/* OnNow pattern register1 */

/* 16bit registers */

#define XMT_RING_LEN0		0x140	/* Transmit Ring0 length register */
#define XMT_RING_LEN1		0x144	/* Transmit Ring1 length register */
#define XMT_RING_LEN2		0x148 	/* Transmit Ring2 length register */
#define XMT_RING_LEN3		0x14C	/* Transmit Ring3 length register */

#define RCV_RING_LEN0		0x150	/* Transmit Ring0 length register */

#define SRAM_SIZE		0x178	/* SRAM size register */
#define SRAM_BOUNDARY		0x17A	/* SRAM boundary register */

/* 48bit register */

#define PADR			0x160	/* Physical address register */

/* 64bit register */

#define LADRF			0x168	/* Logical address filter register */

/* 8bit regsisters */

#define IFS1			0x18C	/* Inter-frame spacing Part1 register */
#define IFS			0x18D	/* Inter-frame spacing register */

/* Register Bit Definitions */

/* STAT_ASF			0x00, 32bit register */
#define ASF_INIT_DONE		B1_MASK
#define ASF_INIT_PRESENT	B0_MASK

/* MIB_ADDR			0x14, 16bit register */
#define	MIB_CMD_ACTIVE		B15_MASK
#define	MIB_RD_CMD		B13_MASK
#define	MIB_CLEAR		B12_MASK
#define	MIB_ADDRESS		0x0000003F	/* 5:0 */

/* QOS_ADDR			0x1C, 16bit register */
#define QOS_CMD_ACTIVE		B15_MASK
#define QOS_WR_CMD		B14_MASK
#define QOS_RD_CMD		B13_MASK
#define QOS_ADDRESS		0x0000001F	/* 4:0 */

/* STAT0			0x30, 32bit register */
#define PAUSE_PEND		B14_MASK
#define PAUSING			B13_MASK
#define PMAT_DET		B12_MASK
#define MP_DET			B11_MASK
#define LC_DET			B10_MASK
#define SPEED_MASK		0x0380	/* 9:7 */
#define FULL_DPLX		B6_MASK
#define LINK_STATS		B5_MASK
#define AUTONEG_COMPLETE	B4_MASK
#define MIIPD			B3_MASK
#define RX_SUSPENDED		B2_MASK
#define TX_SUSPENDED		B1_MASK
#define RUNNING			B0_MASK
#define PHY_SPEED_10		0x2
#define PHY_SPEED_100		0x3

/* INT0				0x38, 32bit register */
#define INTR			B31_MASK
#define PCSINT			B28_MASK
#define LCINT			B27_MASK
#define APINT5			B26_MASK
#define APINT4			B25_MASK
#define APINT3			B24_MASK
#define TINT_SUM		B23_MASK
#define APINT2			B22_MASK
#define APINT1			B21_MASK
#define APINT0			B20_MASK
#define MIIPDTINT		B19_MASK
#define MCCIINT			B18_MASK
#define MCCINT			B17_MASK
#define MREINT			B16_MASK
#define RINT_SUM		B15_MASK
#define SPNDINT			B14_MASK
#define MPINT			B13_MASK
#define SINT			B12_MASK
#define TINT3			B11_MASK
#define TINT2			B10_MASK
#define TINT1			B9_MASK
#define TINT0			B8_MASK
#define UINT			B7_MASK
#define STINT			B4_MASK
#define RINT3			B3_MASK
#define RINT2			B2_MASK
#define RINT1			B1_MASK
#define RINT0			B0_MASK

/* INTEN0			0x40, 32bit register */
#define VAL3			B31_MASK   /* VAL bit for byte 3 */
#define VAL2			B23_MASK   /* VAL bit for byte 2 */
#define VAL1			B15_MASK   /* VAL bit for byte 1 */
#define VAL0			B7_MASK    /* VAL bit for byte 0 */
/* VAL3 */
#define PSCINTEN		B28_MASK
#define LCINTEN			B27_MASK
#define APINT5EN		B26_MASK
#define APINT4EN		B25_MASK
#define APINT3EN		B24_MASK
/* VAL2 */
#define APINT2EN		B22_MASK
#define APINT1EN		B21_MASK
#define APINT0EN		B20_MASK
#define MIIPDTINTEN		B19_MASK
#define MCCIINTEN		B18_MASK
#define MCCINTEN		B17_MASK
#define MREINTEN		B16_MASK
/* VAL1 */
#define SPNDINTEN		B14_MASK
#define MPINTEN			B13_MASK
#define SINTEN			B12_MASK
#define TINTEN3			B11_MASK
#define TINTEN2			B10_MASK
#define TINTEN1			B9_MASK
#define TINTEN0			B8_MASK
/* VAL0 */
#define STINTEN			B4_MASK
#define RINTEN3			B3_MASK
#define RINTEN2			B2_MASK
#define RINTEN1			B1_MASK
#define RINTEN0			B0_MASK

#define INTEN0_CLEAR 		0x1F7F7F1F /* Command style register */		

/* CMD0				0x48, 32bit register */
/* VAL2 */
#define RDMD3			B19_MASK
#define RDMD2			B18_MASK
#define RDMD1			B17_MASK
#define RDMD0			B16_MASK
/* VAL1 */
#define TDMD3			B11_MASK
#define TDMD2			B10_MASK
#define TDMD1			B9_MASK
#define TDMD0			B8_MASK
/* VAL0 */
#define UINTCMD			B6_MASK
#define RX_FAST_SPND		B5_MASK
#define TX_FAST_SPND		B4_MASK
#define RX_SPND			B3_MASK
#define TX_SPND			B2_MASK
#define INTREN			B1_MASK
#define RUN			B0_MASK

#define CMD0_CLEAR 		0x000F0F7F   /* Command style register */	

/* CMD2 			0x50, 32bit register */
/* VAL3 */
#define CONDUIT_MODE		B29_MASK
/* VAL2 */
#define RPA			B19_MASK
#define DRCVPA			B18_MASK
#define DRCVBC			B17_MASK
#define PROM			B16_MASK
/* VAL1 */
#define ASTRP_RCV		B13_MASK
#define FCOLL			B12_MASK
#define EMBA			B11_MASK
#define DXMT2PD			B10_MASK
#define LTINTEN			B9_MASK
#define DXMTFCS			B8_MASK
/* VAL0 */
#define APAD_XMT		B6_MASK
#define DRTY			B5_MASK
#define INLOOP			B4_MASK
#define EXLOOP			B3_MASK
#define REX_RTRY		B2_MASK
#define REX_UFLO		B1_MASK
#define REX_LCOL		B0_MASK

#define CMD2_CLEAR 		0x3F7F3F7F   /* Command style register */

/* CMD3				0x54, 32bit register */
/* VAL3 */
#define ASF_INIT_DONE_ALIAS	B29_MASK
/* VAL2 */
#define JUMBO			B21_MASK
#define VSIZE			B20_MASK
#define VLONLY			B19_MASK
#define VL_TAG_DEL		B18_MASK
/* VAL1 */
#define EN_PMGR			B14_MASK
#define INTLEVEL		B13_MASK
#define FORCE_FULL_DUPLEX	B12_MASK
#define FORCE_LINK_STATUS	B11_MASK
#define APEP			B10_MASK
#define MPPLBA			B9_MASK
/* VAL0 */
#define RESET_PHY_PULSE		B2_MASK
#define RESET_PHY		B1_MASK
#define PHY_RST_POL		B0_MASK
/* CMD7				0x64, 32bit register */
/* VAL0 */
#define PMAT_SAVE_MATCH		B4_MASK
#define PMAT_MODE		B3_MASK
#define MPEN_SW			B1_MASK
#define LCMODE_SW		B0_MASK

#define CMD7_CLEAR  		0x0000001B		/* Command style register */
/* CTRL0			0x68, 32bit register */
#define PHY_SEL			0x03000000	/* 25:24 */
#define RESET_PHY_WIDTH		0x00FF0000	/* 23:16 */
#define BSWP_REGS		B10_MASK
#define BSWP_DESC		B9_MASK
#define BSWP_DATA		B8_MASK
#define CACHE_ALIGN		B4_MASK
#define BURST_LIMIT		0x0000000F	/* 3:0 */

/* CTRL1			0x6C, 32bit register */
#define SLOTMOD_MASK		0x03000000	/* 25:24 */
#define XMTSP_MASK		0x300	/* 17:16 */
#define XMTSP_128		0x200
#define XMTSP_64		0x100
#define CRTL1_DEFAULT		0x00000017

/* CTRL2			0x70, 32bit register */
#define FS_MASK			0x00070000	/* 18:16 */
#define FMDC_MASK		0x00000300	/* 9:8 */
#define XPHYRST			B7_MASK
#define XPHYANE			B6_MASK
#define XPHYFD			B5_MASK
#define XPHYSP			B3_MASK	/* 4:3 */
#define APDW_MASK		0x00000007	/* 2:0 */

/* RCV_RING_CFG			0x78, 16bit register */
#define RCV_DROP3		B11_MASK
#define RCV_DROP2		B10_MASK
#define RCV_DROP1		B9_MASK
#define RCV_DROP0		B8_MASK
#define RCV_RING_DEFAULT	0x0030	/* 5:4 */
#define RCV_RING3_EN		B3_MASK
#define RCV_RING2_EN		B2_MASK
#define RCV_RING1_EN		B1_MASK
#define RCV_RING0_EN		B0_MASK

/* XMT_RING_LIMIT		0x7C, 32bit register */
#define XMT_RING2_LIMIT		0x00FF0000	/* 23:16 */
#define XMT_RING1_LIMIT		0x0000FF00	/* 15:8 */
#define XMT_RING0_LIMIT		0x000000FF	/* 7:0 */

/* AUTOPOLL0			0x88, 16bit register */
#define AP_REG0_EN		B15_MASK
#define AP_REG0_ADDR_MASK	0x1F00	/* 12:8 */
#define AP_PHY0_ADDR_MASK	0x001F	/* 4:0 */

/* AUTOPOLL1			0x8A, 16bit register */
#define AP_REG1_EN		B15_MASK
#define AP_REG1_ADDR_MASK	0x1F00	/* 12:8 */
#define AP_PRE_SUP1		B6_MASK
#define AP_PHY1_DFLT		B5_MASK
#define AP_PHY1_ADDR_MASK	0x001F	/* 4:0 */

/* AUTOPOLL2			0x8C, 16bit register */
#define AP_REG2_EN		B15_MASK
#define AP_REG2_ADDR_MASK	0x1F00	/* 12:8 */
#define AP_PRE_SUP2		B6_MASK
#define AP_PHY2_DFLT		B5_MASK
#define AP_PHY2_ADDR_MASK	0x001F	/* 4:0 */

/* AUTOPOLL3			0x8E, 16bit register */
#define AP_REG3_EN		B15_MASK
#define AP_REG3_ADDR_MASK	0x1F00	/* 12:8 */
#define AP_PRE_SUP3		B6_MASK
#define AP_PHY3_DFLT		B5_MASK
#define AP_PHY3_ADDR_MASK	0x001F	/* 4:0 */

/* AUTOPOLL4			0x90, 16bit register */
#define AP_REG4_EN		B15_MASK
#define AP_REG4_ADDR_MASK	0x1F00	/* 12:8 */
#define AP_PRE_SUP4		B6_MASK
#define AP_PHY4_DFLT		B5_MASK
#define AP_PHY4_ADDR_MASK	0x001F	/* 4:0 */

/* AUTOPOLL5			0x92, 16bit register */
#define AP_REG5_EN		B15_MASK
#define AP_REG5_ADDR_MASK	0x1F00	/* 12:8 */
#define AP_PRE_SUP5		B6_MASK
#define AP_PHY5_DFLT		B5_MASK
#define AP_PHY5_ADDR_MASK	0x001F	/* 4:0 */

/* AP_VALUE 			0x98, 32bit ragister */
#define AP_VAL_ACTIVE		B31_MASK
#define AP_VAL_RD_CMD		B29_MASK
#define AP_ADDR			0x00070000	/* 18:16 */
#define AP_VAL			0x0000FFFF	/* 15:0 */

/* PCS_ANEG			0x9C, 32bit register */
#define SYNC_LOST		B10_MASK
#define IMATCH			B9_MASK
#define CMATCH			B8_MASK
#define PCS_AN_IDLE		B1_MASK
#define PCS_AN_CFG		B0_MASK

/* DLY_INT_A			0xA8, 32bit register */
#define DLY_INT_A_R3		B31_MASK
#define DLY_INT_A_R2		B30_MASK
#define DLY_INT_A_R1		B29_MASK
#define DLY_INT_A_R0		B28_MASK
#define DLY_INT_A_T3		B27_MASK
#define DLY_INT_A_T2		B26_MASK
#define DLY_INT_A_T1		B25_MASK
#define DLY_INT_A_T0		B24_MASK
#define EVENT_COUNT_A		0x00FF0000	/* 20:16 */
#define MAX_DELAY_TIME_A	0x000007FF	/* 10:0 */

/* DLY_INT_B			0xAC, 32bit register */
#define DLY_INT_B_R3		B31_MASK
#define DLY_INT_B_R2		B30_MASK
#define DLY_INT_B_R1		B29_MASK
#define DLY_INT_B_R0		B28_MASK
#define DLY_INT_B_T3		B27_MASK
#define DLY_INT_B_T2		B26_MASK
#define DLY_INT_B_T1		B25_MASK
#define DLY_INT_B_T0		B24_MASK
#define EVENT_COUNT_B		0x00FF0000	/* 20:16 */
#define MAX_DELAY_TIME_B	0x000007FF	/* 10:0 */

/* DFC_THRESH2			0xC0, 16bit register */
#define DFC_THRESH2_HIGH	0xFF00	/* 15:8 */
#define DFC_THRESH2_LOW		0x00FF	/* 7:0 */

/* DFC_THRESH3			0xC2, 16bit register */
#define DFC_THRESH3_HIGH	0xFF00	/* 15:8 */
#define DFC_THRESH3_LOW		0x00FF	/* 7:0 */

/* DFC_THRESH0			0xC4, 16bit register */
#define DFC_THRESH0_HIGH	0xFF00	/* 15:8 */
#define DFC_THRESH0_LOW		0x00FF	/* 7:0 */

/* DFC_THRESH1			0xC6, 16bit register */
#define DFC_THRESH1_HIGH	0xFF00	/* 15:8 */
#define DFC_THRESH1_LOW		0x00FF	/* 7:0 */

/* FLOW_CONTROL 		0xC8, 32bit register */
#define PAUSE_LEN_CHG		B30_MASK
#define	FFC_EN			B28_MASK
#define DFC_RING3_EN		B27_MASK
#define DFC_RING2_EN		B26_MASK
#define DFC_RING1_EN		B25_MASK
#define DFC_RING0_EN		B24_MASK
#define FIXP_CONGEST		B21_MASK
#define FPA			B20_MASK
#define NPA			B19_MASK
#define FIXP			B18_MASK
#define FCPEN			B17_MASK
#define FCCMD			B16_MASK
#define PAUSE_LEN		0x0000FFFF	/* 15:0 */

/* FFC THRESH			0xCC, 32bit register */
#define FFC_HIGH		0xFFFF0000	/* 31:16 */
#define FFC_LOW			0x0000FFFF	/* 15:0 */

/* PHY_ ACCESS			0xD0, 32bit register */
#define	PHY_CMD_ACTIVE		B31_MASK
#define PHY_WR_CMD		B30_MASK
#define PHY_RD_CMD		B29_MASK
#define PHY_RD_ERR		B28_MASK
#define PHY_PRE_SUP		B27_MASK
#define PHY_ADDR		0x03E00000	/* 25:21 */
#define PHY_REG_ADDR		0x001F0000	/* 20:16 */
#define PHY_DATA		0x0000FFFF	/* 15:0 */

/* LED0..3			0xE0..0xE6, 16bit register */
#define LEDOUT			B15_MASK
#define LEDPOL			B14_MASK
#define LEDDIS			B13_MASK
#define LEDSTRETCH		B12_MASK
#define LED1000			B8_MASK
#define LED100			B7_MASK
#define LEDMP			B6_MASK
#define LEDFD			B5_MASK
#define LEDLINK			B4_MASK
#define LEDRCVMAT		B3_MASK
#define LEDXMT			B2_MASK
#define LEDRCV			B1_MASK
#define LEDCOLOUT		B0_MASK

/* EEPROM_ACC			0x17C, 16bit register */
#define PVALID			B15_MASK
#define PREAD			B14_MASK
#define EEDET			B13_MASK
#define	EEN			B4_MASK
#define ECS			B2_MASK
#define EESK			B1_MASK
#define edi_edo			b0_MASK

/* PMAT0			0x190,	 32bit register */
#define PMR_ACTIVE		B31_MASK
#define PMR_WR_CMD		B30_MASK
#define PMR_RD_CMD		B29_MASK
#define PMR_BANK		B28_MASK
#define PMR_ADDR		0x007F0000	/* 22:16 */
#define PMR_B4			0x000000FF	/* 15:0 */

/* PMAT1			0x194,	 32bit register */
#define PMR_B3			0xFF000000	/* 31:24 */
#define PMR_B2			0x00FF0000	/* 23:16 */
#define PMR_B1			0x0000FF00	/* 15:8 */
#define PMR_B0			0x000000FF	/* 7:0 */

/************************************************************************/
/*                                                                      */
/*                      MIB counter definitions                         */
/*                                                                      */
/************************************************************************/

#define rcv_miss_pkts				0x00
#define rcv_octets				0x01
#define rcv_broadcast_pkts			0x02
#define rcv_multicast_pkts			0x03
#define rcv_undersize_pkts			0x04
#define rcv_oversize_pkts			0x05
#define rcv_fragments				0x06
#define rcv_jabbers				0x07
#define rcv_unicast_pkts			0x08
#define rcv_alignment_errors			0x09
#define rcv_fcs_errors				0x0A
#define rcv_good_octets				0x0B
#define rcv_mac_ctrl				0x0C
#define rcv_flow_ctrl				0x0D
#define rcv_pkts_64_octets			0x0E
#define rcv_pkts_65to127_octets			0x0F
#define rcv_pkts_128to255_octets		0x10
#define rcv_pkts_256to511_octets		0x11
#define rcv_pkts_512to1023_octets		0x12
#define rcv_pkts_1024to1518_octets		0x13
#define rcv_unsupported_opcode			0x14
#define rcv_symbol_errors			0x15
#define rcv_drop_pkts_ring1			0x16
#define rcv_drop_pkts_ring2			0x17
#define rcv_drop_pkts_ring3			0x18
#define rcv_drop_pkts_ring4			0x19
#define rcv_jumbo_pkts				0x1A

#define xmt_underrun_pkts			0x20
#define xmt_octets				0x21
#define xmt_packets				0x22
#define xmt_broadcast_pkts			0x23
#define xmt_multicast_pkts			0x24
#define xmt_collisions				0x25
#define xmt_unicast_pkts			0x26
#define xmt_one_collision			0x27
#define xmt_multiple_collision			0x28
#define xmt_deferred_transmit			0x29
#define xmt_late_collision			0x2A
#define xmt_excessive_defer			0x2B
#define xmt_loss_carrier			0x2C
#define xmt_excessive_collision			0x2D
#define xmt_back_pressure			0x2E
#define xmt_flow_ctrl				0x2F
#define xmt_pkts_64_octets			0x30
#define xmt_pkts_65to127_octets			0x31
#define xmt_pkts_128to255_octets		0x32
#define xmt_pkts_256to511_octets		0x33
#define xmt_pkts_512to1023_octets		0x34
#define xmt_pkts_1024to1518_octet		0x35
#define xmt_oversize_pkts			0x36
#define xmt_jumbo_pkts				0x37


/* Driver definitions */

#define	 PCI_VENDOR_ID_AMD		0x1022
#define  PCI_DEVICE_ID_AMD8111E_7462	0x7462

#define MAX_UNITS			16 /* Maximum number of devices possible */

#define NUM_TX_BUFFERS			32 /* Number of transmit buffers */
#define NUM_RX_BUFFERS			32 /* Number of receive buffers */	

#define TX_BUFF_MOD_MASK         	31 /* (NUM_TX_BUFFERS -1) */
#define RX_BUFF_MOD_MASK         	31 /* (NUM_RX_BUFFERS -1) */

#define NUM_TX_RING_DR			32  
#define NUM_RX_RING_DR			32 

#define TX_RING_DR_MOD_MASK         	31 /* (NUM_TX_RING_DR -1) */
#define RX_RING_DR_MOD_MASK         	31 /* (NUM_RX_RING_DR -1) */

#define MAX_FILTER_SIZE			64 /* Maximum multicast address */ 
#define AMD8111E_MIN_MTU	 	60 	
#define AMD8111E_MAX_MTU		9000			

#define PKT_BUFF_SZ			1536
#define MIN_PKT_LEN			60
#define ETH_ADDR_LEN			6

#define OPTION_VLAN_ENABLE		0x0001
#define OPTION_JUMBO_ENABLE		0x0002
#define OPTION_MULTICAST_ENABLE		0x0004
#define OPTION_WOL_ENABLE		0x0008
#define OPTION_WAKE_MAGIC_ENABLE	0x0010
#define OPTION_WAKE_PHY_ENABLE		0x0020

#define PHY_REG_ADDR_MASK		0x1f

/* Assume contoller gets data 10 times the maximum processing time */
#define  REPEAT_CNT			10; 
     
/* amd8111e decriptor flag definitions */

#define OWN_BIT			B15_MASK
#define ADD_FCS_BIT		B13_MASK
#define LTINT_BIT		B12_MASK
#define STP_BIT			B9_MASK
#define ENP_BIT			B8_MASK
#define KILL_BIT		B6_MASK
#define TCC_MASK		0x0003
#define TCC_VLAN_INSERT		B1_MASK
#define TCC_VLAN_REPLACE	0x0003
#define RESET_RX_FLAGS		0x0000

#define ERR_BIT 		B14_MASK
#define FRAM_BIT		B13_MASK
#define OFLO_BIT		B12_MASK
#define CRC_BIT			B11_MASK
#define PAM_BIT			B6_MASK
#define LAFM_BIT		B5_MASK
#define BAM_BIT			B4_MASK
#define TT_MASK			0x000c
#define TT_VLAN_TAGGED		0x000c
#define TT_PRTY_TAGGED		0x0008

/* driver ioctl parameters */
#define PHY_ID 			0x01	/* currently it is fixed */
#define AMD8111E_REG_DUMP_LEN	4096	/* Memory mapped register length */

/* amd8111e desriptor format */

struct amd8111e_tx_dr{

	u16 buff_count; /* Size of the buffer pointed by this descriptor */

	u16 tx_dr_offset2;

	u16 tag_ctrl_info;

	u16 tag_ctrl_cmd;

	u32 buff_phy_addr;

	u32 reserved;
}; 

struct amd8111e_rx_dr{
	
	u32 reserved;

	u16 msg_count; /* Received message len */

	u16 tag_ctrl_info; 

	u16 buff_count;  /* Len of the buffer pointed by descriptor. */

	u16 rx_dr_offset10;

	u32 buff_phy_addr;

};
struct amd8111e_link_config{

#define SPEED_INVALID		0xffff
#define DUPLEX_INVALID		0xff
#define AUTONEG_INVALID		0xff
	
	unsigned long			orig_phy_option;
	u16				speed;
	u8				duplex;
	u8				autoneg;
	u16 				orig_speed;
	u8				orig_duplex;
	u8				reserved;  /* 32bit alignment */
};
struct amd8111e_priv{
	
	struct amd8111e_tx_dr*  tx_ring;
	struct amd8111e_rx_dr* rx_ring;
	dma_addr_t tx_ring_dma_addr;	/* tx descriptor ring base address */
	dma_addr_t rx_ring_dma_addr;	/* rx descriptor ring base address */
	const char *name;
	struct pci_dev *pci_dev;	/* Ptr to the associated pci_dev */
	struct net_device* amd8111e_net_dev; 	/* ptr to associated net_device */
	/* Transmit and recive skbs */
	struct sk_buff *tx_skbuff[NUM_TX_BUFFERS];
	struct sk_buff *rx_skbuff[NUM_RX_BUFFERS];
	/* Transmit and receive dma mapped addr */
	dma_addr_t tx_dma_addr[NUM_TX_BUFFERS];
	dma_addr_t rx_dma_addr[NUM_RX_BUFFERS];
	/* Reg memory mapped address */
	void *  mmio;
	
	spinlock_t lock;	/* Guard lock */
	unsigned long  rx_idx, tx_idx;	/* The next free ring entry */
	unsigned long  tx_complete_idx;
	unsigned long tx_ring_complete_idx;
	unsigned long tx_ring_idx;
	int rx_buff_len;	/* Buffer length of rx buffers */
	int options;		/* Options enabled/disabled for the device */
	unsigned long ext_phy_option;
	struct amd8111e_link_config link_config;
	int pm_cap;

	struct net_device *next;
#if AMD8111E_VLAN_TAG_USED
	struct vlan_group		*vlgrp;
#endif	
	char opened;
	struct net_device_stats stats;
	struct net_device_stats prev_stats;
	struct dev_mc_list* mc_list;
	
};
#define AMD8111E_READ_REG64(_memMapBase, _offset, _pUlData)	\
			*(u32*)(_pUlData) = readl(_memMapBase + (_offset));	\
			*((u32*)(_pUlData))+1) = readl(_memMapBase + ((_offset)+4))

#define AMD8111E_WRITE_REG64(_memMapBase, _offset, _pUlData)	\
			writel(*(u32*)(_pUlData), _memMapBase + (_offset));	\
			writel(*(u32*)((u8*)(_pUlData)+4), _memMapBase + ((_offset)+4))	\

/* maps the external speed options to internal value */
static unsigned char speed_duplex_mapping[] = {

	XPHYANE,		/* Auto-negotiation, speed_duplex option 0 */
	0,			/* 10M Half,  speed_duplex option 1 */
	XPHYFD,			/* 10M Full,  speed_duplex option 2 */
	XPHYSP,			/* 100M Half, speed_duplex option 3 */
	XPHYFD | XPHYSP		/* 100M Full, speed_duplex option 4 */
};
static int card_idx;
static int speed_duplex[MAX_UNITS] = { 0, };

#endif /* _AMD8111E_H */

