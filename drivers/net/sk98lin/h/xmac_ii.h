/******************************************************************************
 *
 * Name:	xmac_ii.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.27 $
 * Date:	$Date: 2000/05/17 11:00:46 $
 * Purpose:	Defines and Macros for XaQti's Gigabit Ethernet Controller
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * History:
 *
 *	$Log: xmac_ii.h,v $
 *	Revision 1.27  2000/05/17 11:00:46  malthoff
 *	Add bit for enable/disable power management in BCOM chip.
 *	
 *	Revision 1.26  1999/11/22 14:03:00  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.25  1999/08/12 19:19:38  malthoff
 *	Add PHY_B_AC_TX_TST bit according to BCOM A1 errata sheet.
 *	
 *	Revision 1.24  1999/07/30 11:27:21  cgoos
 *	Fixed a missing end-of-comment.
 *	
 *	Revision 1.23  1999/07/30 07:03:31  malthoff
 *	Cut some long comments.
 *	Correct the XMAC PHY ID definitions.
 *	
 *	Revision 1.22  1999/05/19 07:33:18  cgoos
 *	Changes for 1000Base-T.
 *	
 *	Revision 1.21  1999/03/25 07:46:11  malthoff
 *	Add XM_HW_CFG, XM_TS_READ, and XM_TS_LOAD registers.
 *	
 *	Revision 1.20  1999/03/12 13:36:09  malthoff
 *	Remove __STDC__.
 *
 *	Revision 1.19  1998/12/10 12:22:54  gklug
 *	fix: RX_PAGE must be in interrupt mask
 *
 *	Revision 1.18  1998/12/10 10:36:36  gklug
 *	fix: swap of pause bits
 *
 *	Revision 1.17  1998/11/18 13:21:45  gklug
 *	fix: Default interrupt mask
 *
 *	Revision 1.16  1998/10/29 15:53:21  gklug
 *	fix: Default mask uses ASS (GP0) signal
 *
 *	Revision 1.15  1998/10/28 13:52:52  malthoff
 *	Add new bits in RX_CMD register.
 *
 *	Revision 1.14  1998/10/19 15:34:53  gklug
 *	fix: typos
 *
 *	Revision 1.13  1998/10/14 07:19:03  malthoff
 *	bug fix: Every define which describes bit 31
 *	must be declared as unsigned long 'UL'.
 *	fix bit definitions of PHY_AN_RFB and PHY_AN_PAUSE.
 *	Remove ANP defines. Rework the RFB defines.
 *
 *	Revision 1.12  1998/10/14 06:22:44  cgoos
 *	Changed shifted constant to ULONG.
 *
 *	Revision 1.11  1998/10/14 05:43:26  gklug
 *	add: shift pause codeing
 *	fix: PAUSE bits definition
 *
 *	Revision 1.10  1998/10/13 09:19:21  malthoff
 *	Again change XMR_FS_ANY_ERR because of new info from XaQti.
 *
 *	Revision 1.9  1998/10/09 07:58:30  malthoff
 *	Add XMR_FS_FCS_ERR to XMR_FS_ANY_ERR.
 *
 *	Revision 1.8  1998/10/09 07:18:17  malthoff
 *	bug fix of a bug fix: XM_PAUSE_MODE and XM_DEF_MODE
 *	are not inverted! Bug XM_DEF_MSK is inverted.
 *
 *	Revision 1.7  1998/10/05 08:04:32  malthoff
 *	bug fix: XM_PAUSE_MODE and XM_DEF_MODE
 *	must be inverted declarations.
 *
 *	Revision 1.6  1998/09/28 13:38:18  malthoff
 *	Add default modes and masks XM_DEF_MSK,
 *	XM_PAUSE_MODE and XM_DEF_MODE
 *
 *	Revision 1.5  1998/09/16 14:42:04  malthoff
 *	Bug Fix: XM_GP_PORT is a 32 bit (not a 16 bit) register.
 *
 *	Revision 1.4  1998/08/20 14:59:47  malthoff
 *	Rework this file after reading the XaQti data sheet
 *	"Differences between Rev. B2 & Rev. C XMAC II".
 *	This file is now 100% XMAC II Rev. C complained.
 *
 *	Revision 1.3  1998/06/29 12:18:23  malthoff
 *	Correct XMR_FS_ANY_ERR definition.
 *
 *	Revision 1.2  1998/06/29 12:10:56  malthoff
 *	Add define XMR_FS_ANY_ERR.
 *
 *	Revision 1.1  1998/06/19 13:37:17  malthoff
 *	created.
 *
 *
 ******************************************************************************/

#ifndef __INC_XMAC_H
#define __INC_XMAC_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* defines ********************************************************************/

/*
 * XMAC II registers
 *
 * The XMAC registers are 16 or 32 bits wide. The XMACs host processor
 * interface is set to 16 bit mode, therefore ALL registers will be
 * addressed with 16 bit accesses.
 *
 * The following macros are provided to access the XMAC registers
 * XM_IN16(), XM_OUT16, XM_IN32(), MX_OUT32(), XM_INADR(), XM_OUTADR(),
 * XM_INHASH(), and XM_OUTHASH().
 * The macros are defined in SkGeHw.h.
 *
 * Note:	NA reg	= Network Address e.g DA, SA etc.
 *
 */
#define XM_MMU_CMD	0x0000	/* 16 bit r/w	MMU Command Register */
	/* 0x0004:		reserved */
#define XM_POFF		0x0008	/* 32 bit r/w	Packet Offset Register */
#define XM_BURST	0x000c	/* 32 bit r/w	Burst Register for half duplex*/
#define XM_1L_VLAN_TAG	0x0010	/* 16 bit r/w	One Level VLAN Tag ID */
#define XM_2L_VLAN_TAG	0x0014	/* 16 bit r/w	Two Level VLAN Tag ID */
	/* 0x0018 - 0x001e:	reserved */
#define XM_TX_CMD	0x0020	/* 16 bit r/w	Transmit Command Register */
#define	XM_TX_RT_LIM	0x0024	/* 16 bit r/w	Transmit Retry Limit Register */
#define XM_TX_STIME	0x0028	/* 16 bit r/w	Transmit Slottime Register */
#define XM_TX_IPG	0x002c	/* 16 bit r/w	Transmit Inter Packet Gap */
#define XM_RX_CMD	0x0030	/* 16 bit r/w	Receive Command Register */
#define XM_PHY_ADDR	0x0034	/* 16 bit r/w	PHY Address Register */
#define	XM_PHY_DATA	0x0038	/* 16 bit r/w	PHY Data Register */
	/* 0x003c: 		reserved */
#define XM_GP_PORT	0x0040	/* 32 bit r/w	General Purpose Port Register */
#define XM_IMSK		0x0044	/* 16 bit r/w	Interrupt Mask Register */
#define XM_ISRC		0x0048	/* 16 bit ro	Interrupt Status Register */
#define XM_HW_CFG	0x004c	/* 16 bit r/w	Hardware Config Register */
	/* 0x0050 - 0x005e:	reserved */
#define XM_TX_LO_WM	0x0060	/* 16 bit r/w	Tx FIFO Low Water Mark */
#define XM_TX_HI_WM	0x0062	/* 16 bit r/w	Tx FIFO High Water Mark */
#define XM_TX_THR	0x0064	/* 16 bit r/w	Tx Request Threshold */
#define	XM_HT_THR	0x0066	/* 16 bit r/w	Host Request Threshold */
#define	XM_PAUSE_DA	0x0068	/* NA reg r/w	Pause Destination Address */
	/* 0x006e: 		reserved */
#define XM_CTL_PARA	0x0070	/* 32 bit r/w	Control Parameter Register */
#define XM_MAC_OPCODE	0x0074	/* 16 bit r/w	Opcode for MAC control frames */
#define XM_MAC_PTIME	0x0076	/* 16 bit r/w	Pause time for MAC ctrl frames*/
#define XM_TX_STAT	0x0078	/* 32 bit ro	Tx Status LIFO Register */

	/* 0x0080 - 0x00fc:	16 NA reg r/w	Exakt Match Address Registers */
	/* 				use the XM_EMX() macro to address */
#define XM_EXM_START	0x0080		/* r/w	Start Address of the EXM Regs */

	/*
	 * XM_EXM(Reg)
	 *
	 * returns the XMAC address offset off specified Exakt Match Addr Reg
	 *
	 * para:	Reg	EXM register to addr	(0 .. 15)
	 *
	 * usage:	XM_INADDR(XMAC_1,pAC,XM_EXM(i),&val[i]) ;
	 */
#define XM_EXM(Reg)	(XM_EXM_START + ((Reg) << 3))

#define XM_SRC_CHK	0x0100	/* NA reg r/w	Source Check Address Register */
#define XM_SA		0x0108	/* NA reg r/w	Station Address Register */
#define XM_HSM		0x0110	/* 64 bit r/w	Hash Match Address Registers */
#define XM_RX_LO_WM	0x0118	/* 16 bit r/w	Receive Low Water Mark */
#define XM_RX_HI_WM	0x011a	/* 16 bit r/w	Receive High Water Mark */
#define XM_RX_THR	0x011c	/* 32 bit r/w	Receive Request Threshold */
#define XM_DEV_ID	0x0120	/* 32 bit ro	Device ID Register */
#define XM_MODE		0x0124	/* 32 bit r/w	Mode Register */
#define XM_LSA		0x0128	/* NA reg ro	Last Source Register */
	/* 0x012e:		reserved */
#define XM_TS_READ	0x0130	/* 32 bit ro	TimeStamp Read Regeister */
#define XM_TS_LOAD	0x0134	/* 32 bit ro	TimeStamp Load Value */
	/* 0x0138 - 0x01fe:	reserved */
#define XM_STAT_CMD	0x0200	/* 16 bit r/w	Statistics Command Register */
#define	XM_RX_CNT_EV	0x0204	/* 32 bit ro	Rx Counter Event Register */
#define XM_TX_CNT_EV	0x0208	/* 32 bit ro	Tx Counter Event Register */
#define XM_RX_EV_MSK	0x020c	/* 32 bit r/w	Rx Counter Event Mask */
#define XM_TX_EV_MSK	0x0210	/* 32 bit r/w	Tx Counter Event Mask */
	/* 0x0204 - 0x027e:	reserved */
#define XM_TXF_OK	0x0280	/* 32 bit ro	Frames Transmitted OK Conuter */
#define XM_TXO_OK_HI	0x0284	/* 32 bit ro	Octets Transmitted OK High Cnt*/
#define XM_TXO_OK_LO	0x0288	/* 32 bit ro	Octets Transmitted OK Low Cnt */
#define XM_TXF_BC_OK	0x028c	/* 32 bit ro	Broadcast Frames Xmitted OK */
#define XM_TXF_MC_OK	0x0290	/* 32 bit ro	Multicast Frames Xmitted OK */
#define XM_TXF_UC_OK	0x0294	/* 32 bit ro	Unicast Frames Xmitted OK */
#define XM_TXF_LONG	0x0298	/* 32 bit ro	Tx Long Frame Counter */
#define XM_TXE_BURST	0x029c	/* 32 bit ro	Tx Burst Event Counter */
#define XM_TXF_MPAUSE	0x02a0	/* 32 bit ro	Tx Pause MAC Ctrl Frame Cnt */
#define XM_TXF_MCTRL	0x02a4	/* 32 bit ro	Tx MAC Ctrl Frame Counter */
#define XM_TXF_SNG_COL	0x02a8	/* 32 bit ro	Tx Single Colliosion Counter */
#define XM_TXF_MUL_COL	0x02ac	/* 32 bit ro	Tx Multiple Collision Counter */
#define XM_TXF_ABO_COL	0x02b0	/* 32 bit ro	Tx aborted due to Exessive Col*/
#define XM_TXF_LAT_COL	0x02b4	/* 32 bit ro	Tx Late Collision Counter */
#define XM_TXF_DEF	0x02b8	/* 32 bit ro	Tx Deferred Frame Counter */
#define XM_TXF_EX_DEF	0x02bc	/* 32 bit ro	Tx Excessive Deferall Counter */
#define XM_TXE_FIFO_UR	0x02c0	/* 32 bit ro	Tx FIFO Underrun Event Cnt */
#define XM_TXE_CS_ERR	0x02c4	/* 32 bit ro	Tx Carrier Sence Error Cnt */
#define XM_TXP_UTIL	0x02c8	/* 32 bit ro	Tx Utilization in % */
	/* 0x02cc - 0x02ce:	reserved */
#define XM_TXF_64B	0x02d0	/* 32 bit ro	64 Byte Tx Frame Counter */
#define XM_TXF_127B	0x02d4	/* 32 bit ro	65-127 Byte Tx Frame Counter */
#define XM_TXF_255B	0x02d8	/* 32 bit ro	128-255 Byte Tx Frame Counter */
#define XM_TXF_511B	0x02dc	/* 32 bit ro	256-511 Byte Tx Frame Counter */
#define XM_TXF_1023B	0x02e0	/* 32 bit ro	512-1023 Byte Tx Frame Counter*/
#define XM_TXF_MAX_SZ	0x02e4	/* 32 bit ro	1024-MaxSize Byte Tx Frame Cnt*/
	/* 0x02e8 - 0x02fe:	reserved */
#define XM_RXF_OK	0x0300	/* 32 bit ro	Frames Received OK */
#define XM_RXO_OK_HI	0x0304	/* 32 bit ro	Octets Received OK High Cnt */
#define XM_RXO_OK_LO	0x0308	/* 32 bit ro	Octets Received OK Low Counter*/
#define XM_RXF_BC_OK	0x030c	/* 32 bit ro	Broadcast Frames Received OK */
#define XM_RXF_MC_OK	0x0310	/* 32 bit ro	Multicast Frames Received OK */
#define XM_RXF_UC_OK	0x0314	/* 32 bit ro	Unicast Frames Received OK */
#define XM_RXF_MPAUSE	0x0318	/* 32 bit ro	Rx Pause MAC Ctrl Frame Cnt */
#define XM_RXF_MCTRL	0x031c	/* 32 bit ro	Rx MAC Ctrl Frame Counter */
#define XM_RXF_INV_MP	0x0320	/* 32 bit ro	Rx invalid Pause Frame Cnt */
#define XM_RXF_INV_MOC	0x0324	/* 32 bit ro	Rx Frames with inv. MAC Opcode*/
#define XM_RXE_BURST	0x0328	/* 32 bit ro	Rx Burst Event Counter */
#define XM_RXE_FMISS	0x032c	/* 32 bit ro	Rx Missed Frames Event Cnt */
#define XM_RXF_FRA_ERR	0x0330	/* 32 bit ro	Rx Framing Error Counter */
#define XM_RXE_FIFO_OV	0x0334	/* 32 bit ro	Rx FIFO overflow Event Cnt */
#define XM_RXF_JAB_PKT	0x0338	/* 32 bit ro	Rx Jabber Packet Frame Cnt */
#define XM_RXE_CAR_ERR	0x033c	/* 32 bit ro	Rx Carrier Event Error Cnt */
#define XM_RXF_LEN_ERR	0x0340	/* 32 bit ro	Rx in Range Length Error */
#define XM_RXE_SYM_ERR	0x0344	/* 32 bit ro	Rx Symbol Error Counter */
#define XM_RXE_SHT_ERR	0x0348	/* 32 bit ro	Rx Short Event Error Cnt */
#define XM_RXE_RUNT	0x034c	/* 32 bit ro	Rx Runt Event Counter */
#define XM_RXF_LNG_ERR	0x0350	/* 32 bit ro	Rx Frame too Long Error Cnt */
#define XM_RXF_FCS_ERR	0x0354	/* 32 bit ro	Rx Frame Check Seq. Error Cnt */
	/* 0x0358 - 0x035a:	reserved */
#define XM_RXF_CEX_ERR	0x035c	/* 32 bit ro	Rx Carrier Ext Error Frame Cnt*/
#define XM_RXP_UTIL	0x0360	/* 32 bit ro	Rx Utilization in % */
	/* 0x0364 - 0x0366:	reserved */
#define XM_RXF_64B	0x0368	/* 32 bit ro	64 Byte Rx Frame Counter */
#define XM_RXF_127B	0x036c	/* 32 bit ro	65-127 Byte Rx Frame Counter */
#define XM_RXF_255B	0x0370	/* 32 bit ro	128-255 Byte Rx Frame Counter */
#define XM_RXF_511B	0x0374	/* 32 bit ro	256-511 Byte Rx Frame Counter */
#define XM_RXF_1023B	0x0378	/* 32 bit ro	512-1023 Byte Rx Frame Counter*/
#define XM_RXF_MAX_SZ	0x037c	/* 32 bit ro	1024-MaxSize Byte Rx Frame Cnt*/
	/* 0x02e8 - 0x02fe:	reserved */


/*----------------------------------------------------------------------------*/
/*
 * XMAC Bit Definitions
 *
 * If the bit access behaviour differs from the register access behaviour
 * (r/w, ro) this is docomented after the bit number. The following bit
 * access behaviours are used:
 *	(sc)	self clearing
 *	(ro)	read only
 */

/*	XM_MMU_CMD	16 bit r/w	MMU Comamnd Register */
				/* Bit 15..13:	reserved */
#define XM_MMU_PHY_RDY	(1<<12)	/* Bit 12:	PHY Read Ready */
#define XM_MMU_PHY_BUSY	(1<<11)	/* Bit 11:	PHY Busy */
#define XM_MMU_IGN_PF	(1<<10)	/* Bit 10:	Ignore Pause Frame */
#define XM_MMU_MAC_LB	(1<<9)	/* Bit	9:	Enable MAC Loopback */
				/* Bit	8:	reserved */
#define XM_MMU_FRC_COL	(1<<7)	/* Bit	7:	Force Collision */
#define XM_MMU_SIM_COL	(1<<6)	/* Bit	6:	Simulate Collision */
#define XM_MMU_NO_PRE	(1<<5)	/* Bit	5:	No MDIO Preamble */
#define XM_MMU_GMII_FD	(1<<4)	/* Bit	4:	GMII uses Full Duplex */
#define XM_MMU_RAT_CTRL	(1<<3)	/* Bit	3:	Enable Rate Control */
#define XM_MMU_GMII_LOOP (1<<2)	/* Bit	2:	PHY is in Lookback Mode */
#define XM_MMU_ENA_RX	(1<<1)	/* Bit	1:	Enable Receiver */
#define XM_MMU_ENA_TX	(1<<0)	/* Bit	0:	Enable Transmitter */


/*	XM_TX_CMD	16 bit r/w	Transmit Command Register */
				/* Bit 15..7:	reserved */
#define XM_TX_BK2BK	(1<<6)	/* Bit	6:	Ignor Carrier Sense (tx Bk2Bk)*/
#define XM_TX_ENC_BYP	(1<<5)	/* Bit	5:	Set Encoder in Bypass Mode */
#define XM_TX_SAM_LINE	(1<<4)	/* Bit	4: (sc)	Start utilization calculation */
#define XM_TX_NO_GIG_MD	(1<<3)	/* Bit	3:	Disable Carrier Extension */
#define XM_TX_NO_PRE	(1<<2)	/* Bit	2:	Disable Preamble Generation */
#define XM_TX_NO_CRC	(1<<1)	/* Bit	1:	Disable CRC Generation */
#define XM_TX_AUTO_PAD	(1<<0)	/* Bit	0:	Enable Automatic Padding */


/*	XM_TX_RT_LIM	16 bit r/w	Transmit Retry Limit Register */
				/* Bit 15..5:	reserved */
#define	XM_RT_LIM_MSK	0x1f	/* Bit	4..0:	Tx Retry Limit */


/*	XM_TX_STIME	16 bit r/w	Transmit Slottime Register */
				/* Bit 15..7:	reserved */
#define XM_STIME_MSK	0x7f	/* Bit	6..0:	Tx Slottime bits */


/*	XM_TX_IPG	16 bit r/w	Transmit Inter Packet Gap */
				/* Bit 15..8:	reserved */
#define XM_IPG_MSK	0xff	/* Bit	7..0:	IPG value bits */


/*	XM_RX_CMD	16 bit r/w	Receive Command Register */
				/* Bit 15..9:	reserved */
#define XM_RX_LENERR_OK (1<<8)	/* Bit	8	don't set Rx Err bit for */
				/*		inrange error packets */
#define XM_RX_BIG_PK_OK	(1<<7)	/* Bit	7	don't set Rx Err bit for */
				/*		jumbo packets */
#define XM_RX_IPG_CAP	(1<<6)	/* Bit	6	repl. type field with IPG */
#define XM_RX_TP_MD	(1<<5)	/* Bit	5:	Enable transparent Mode */
#define XM_RX_STRIP_FCS	(1<<4)	/* Bit	4:	Enable FCS Stripping */
#define XM_RX_SELF_RX	(1<<3)	/* Bit	3: 	Enable Rx of own packets */
#define XM_RX_SAM_LINE	(1<<2)	/* Bit	2: (sc)	Start utilization calculation */
#define XM_RX_STRIP_PAD	(1<<1)	/* Bit	1:	Strip pad bytes of rx frames */
#define XM_RX_DIS_CEXT	(1<<0)	/* Bit	0:	Disable carrier ext. check */


/*	XM_PHY_ADDR	16 bit r/w	PHY Address Register */
				/* Bit 15..5:	reserved */
#define	XM_PHY_ADDR_SZ	0x1f	/* Bit	4..0:	PHY Address bits */


/*	XM_GP_PORT	32 bit r/w	General Purpose Port Register */
				/* Bit 31..7:	reserved */
#define	XM_GP_ANIP	(1L<<6)	/* Bit	6: (ro)	Auto Negotiation in Progress */
#define XM_GP_FRC_INT	(1L<<5)	/* Bit	5: (sc)	Force Interrupt */
				/* Bit	4:	reserved */
#define XM_GP_RES_MAC	(1L<<3)	/* Bit	3: (sc)	Reset MAC and FIFOs */
#define XM_GP_RES_STAT	(1L<<2)	/* Bit	2: (sc)	Reset the statistics module */
				/* Bit	1:	reserved */
#define XM_GP_INP_ASS	(1L<<0)	/* Bit	0: (ro) GP Input Pin asserted */


/*	XM_IMSK		16 bit r/w	Interrupt Mask Register */
/*	XM_ISRC		16 bit ro	Interrupt Status Register */
				/* Bit 15:	reserved */
#define XM_IS_LNK_AE	(1<<14) /* Bit 14:	Link Asynchronous Event */
#define XM_IS_TX_ABORT	(1<<13) /* Bit 13:	Transmit Abort, late Col. etc */
#define XM_IS_FRC_INT	(1<<12) /* Bit 12:	Force INT bit set in GP */
#define XM_IS_INP_ASS	(1<<11)	/* Bit 11:	Input Asserted, GP bit 0 set */
#define XM_IS_LIPA_RC	(1<<10)	/* Bit 10:	Link Partner requests config */
#define XM_IS_RX_PAGE	(1<<9)	/* Bit	9:	Page Received */
#define XM_IS_TX_PAGE	(1<<8)	/* Bit	8:	Next Page Loaded for Transmit */
#define XM_IS_AND	(1<<7)	/* Bit	7:	Auto Negotiation Done */
#define XM_IS_TSC_OV	(1<<6)	/* Bit	6:	Time Stamp Counter Overflow */
#define XM_IS_RXC_OV	(1<<5)	/* Bit	5:	Rx Counter Event Overflow */
#define XM_IS_TXC_OV	(1<<4)	/* Bit	4:	Tx Counter Event Overflow */
#define XM_IS_RXF_OV	(1<<3)	/* Bit	3:	Receive FIFO Overflow */
#define XM_IS_TXF_UR	(1<<2)	/* Bit	2:	Transmit FIFO Underrun */
#define XM_IS_TX_COMP	(1<<1)	/* Bit	1:	Frame Tx Complete */
#define XM_IS_RX_COMP	(1<<0)	/* Bit	0:	Frame Rx Complete */

#define XM_DEF_MSK	(~(XM_IS_INP_ASS | XM_IS_LIPA_RC | XM_IS_RX_PAGE |\
			XM_IS_AND | XM_IS_RXC_OV | XM_IS_TXC_OV |\
			XM_IS_TXF_UR))


/*	XM_HW_CFG	16 bit r/w	Hardware Config Register */
				/* Bit 15.. 4:	reserved */
#define	XM_HW_GEN_EOP	(1<<3)	/* Bit	3:	generate End of Packet pulse */
#define XM_HW_COM4SIG	(1<<2)	/* Bit	2:	use Comma Detect for Sig. Det.*/
				/* Bit	1:	reserved */
#define XM_HW_GMII_MD	(1<<0)	/* Bit	0:	GMII Interface selected */


/*	XM_TX_LO_WM	16 bit r/w	Tx FIFO Low Water Mark */
/*	XM_TX_HI_WM	16 bit r/w	Tx FIFO High Water Mark */
				/* Bit 15..10	reserved */
#define	XM_TX_WM_MSK	0x01ff	/* Bit	9.. 0	Tx FIFO Watermark bits */

/*	XM_TX_THR	16 bit r/w	Tx Request Threshold */
/*	XM_HT_THR	16 bit r/w	Host Request Threshold */
/*	XM_RX_THR	16 bit r/w	Receive Request Threshold */
				/* Bit 15..11	reserved */
#define	XM_THR_MSK	0x03ff	/* Bit 10.. 0	Tx FIFO Watermark bits */


/*	XM_TX_STAT	32 bit ro	Tx Status LIFO Register */
#define XM_ST_VALID	(1UL<<31)	/* Bit 31:	Status Valid */
#define XM_ST_BYTE_CNT	(0x3fffL<<17)	/* Bit 30..17:	Tx frame Length */
#define XM_ST_RETRY_CNT	(0x1fL<<12)	/* Bit 16..12:	Retry Count */
#define XM_ST_EX_COL	(1L<<11)	/* Bit 11:	Excessive Collisions */
#define XM_ST_EX_DEF	(1L<<10)	/* Bit 10:	Excessive Deferral */
#define XM_ST_BURST	(1L<<9)		/* Bit	9:	p. xmitted in burst md*/
#define XM_ST_DEFER	(1L<<8)		/* Bit	8:	packet was defered */
#define XM_ST_BC	(1L<<7)		/* Bit	7:	Broadcast packet */
#define XM_ST_MC	(1L<<6)		/* Bit	6:	Multicast packet */
#define XM_ST_UC	(1L<<5)		/* Bit	5:	Unicast packet */
#define XM_ST_TX_UR	(1L<<4)		/* Bit	4:	FIFO Underrun occured */
#define XM_ST_CS_ERR	(1L<<3)		/* Bit	3:	Carrier Sense Error */
#define XM_ST_LAT_COL	(1L<<2)		/* Bit	2:	Late Collision Error */
#define XM_ST_MUL_COL	(1L<<1)		/* Bit	1:	Multiple Collisions */
#define XM_ST_SGN_COL	(1L<<0)		/* Bit	0:	Single Collision */

/*	XM_RX_LO_WM	16 bit r/w	Receive Low Water Mark */
/*	XM_RX_HI_WM	16 bit r/w	Receive High Water Mark */
				/* Bit 15..11:	reserved */
#define	XM_RX_WM_MSK	0x03ff	/* Bit 11.. 0:	Rx FIFO Watermark bits */


/*	XM_DEV_ID	32 bit ro	Device ID Register */
#define	XM_DEV_OUI	(0x00ffffffUL<<8)	/* Bit 31..8:	Device OUI */
#define XM_DEV_REV	(0x07L << 5)		/* Bit	7..5:	Chip Rev Num */


/*	XM_MODE		32 bit r/w	Mode Register */
					/* Bit 31..27:	reserved */
#define XM_MD_ENA_REJ	(1L<<26)	/* Bit 26:	Enable Frame Reject */
#define XM_MD_SPOE_E	(1L<<25)	/* Bit 25:	Send Pause on Edge */
					/* 		extern generated */
#define XM_MD_TX_REP	(1L<<24)	/* Bit 24:	Transmit Repeater Mode*/
#define XM_MD_SPOFF_I	(1L<<23)	/* Bit 23:	Send Pause on FIFOfull*/
					/*		intern generated */
#define XM_MD_LE_STW	(1L<<22)	/* Bit 22:	Rx Stat Word in Lit En*/
#define XM_MD_TX_CONT	(1L<<21)	/* Bit 21:	Send Continuous */
#define XM_MD_TX_PAUSE	(1L<<20)	/* Bit 20: (sc)	Send Pause Frame */
#define XM_MD_ATS	(1L<<19)	/* Bit 19:	Append Time Stamp */
#define XM_MD_SPOL_I	(1L<<18)	/* Bit 18:	Send Pause on Low */
					/*		intern generated */
#define XM_MD_SPOH_I	(1L<<17)	/* Bit 17:	Send Pause on High */
					/*		intern generated */
#define XM_MD_CAP	(1L<<16)	/* Bit 16:	Check Address Pair */
#define XM_MD_ENA_HSH	(1L<<15)	/* Bit 15:	Enable Hashing */
#define XM_MD_CSA	(1L<<14)	/* Bit 14:	Check Station Address */
#define XM_MD_CAA	(1L<<13)	/* Bit 13:	Check Address Array */
#define XM_MD_RX_MCTRL	(1L<<12)	/* Bit 12:	Rx MAC Control Frames */
#define XM_MD_RX_RUNT	(1L<<11)	/* Bit 11:	Rx Runt Frames */
#define XM_MD_RX_IRLE	(1L<<10)	/* Bit 10:	Rx in Range Len Err F */
#define XM_MD_RX_LONG	(1L<<9)		/* Bit	9:	Rx Long Frames */
#define XM_MD_RX_CRCE	(1L<<8)		/* Bit	8:	Rx CRC Error Frames */
#define XM_MD_RX_ERR	(1L<<7)		/* Bit	7:	Rx Error Frames */
#define XM_MD_DIS_UC	(1L<<6)		/* Bit	6:	Disable Rx Unicast */
#define XM_MD_DIS_MC	(1L<<5)		/* Bit	5:	Disable Rx Multicast */
#define XM_MD_DIS_BC	(1L<<4)		/* Bit	4:	Disable Rx Boradcast */
#define XM_MD_ENA_PROM	(1L<<3)		/* Bit	3:	Enable Promiscuous */
#define XM_MD_ENA_BE	(1L<<2)		/* Bit	2:	Enable Big Endian */
#define XM_MD_FTF	(1L<<1)		/* Bit	1: (sc)	Flush Tx FIFO */
#define XM_MD_FRF	(1L<<0)		/* Bit	0: (sc)	Flush Rx FIFO */

#define	XM_PAUSE_MODE	(XM_MD_SPOE_E | XM_MD_SPOL_I | XM_MD_SPOH_I)
#define XM_DEF_MODE	(XM_MD_RX_RUNT | XM_MD_RX_IRLE | XM_MD_RX_LONG |\
			 XM_MD_RX_CRCE | XM_MD_RX_ERR | XM_MD_CSA |\
			 XM_MD_CAA)

/*	XM_STAT_CMD	16 bit r/w	Statistics Command Register */
				/* Bit 16..6:	reserved */
#define XM_SC_SNP_RXC	(1<<5)	/* Bit	5: (sc)	Snap Rx Counters */
#define XM_SC_SNP_TXC	(1<<4)	/* Bit	4: (sc)	Snap Tx Counters */
#define XM_SC_CP_RXC	(1<<3)	/* Bit	3: 	Copy Rx Counters Continuously */
#define XM_SC_CP_TXC	(1<<2)	/* Bit	2:	Copy Tx Counters Continuously */
#define XM_SC_CLR_RXC	(1<<1)	/* Bit	1: (sc)	Clear Rx Counters */
#define XM_SC_CLR_TXC	(1<<0)	/* Bit	0: (sc) Clear Tx Counters */


/*	XM_RX_CNT_EV	32 bit ro	Rx Counter Event Register */
/*	XM_RX_EV_MSK	32 bit r/w	Rx Counter Event Mask */
#define	XMR_MAX_SZ_OV	(1UL<<31)	/* Bit 31:	1024-MaxSize Rx Cnt Ov*/
#define XMR_1023B_OV	(1L<<30)	/* Bit 30:	512-1023Byte Rx Cnt Ov*/
#define XMR_511B_OV	(1L<<29)	/* Bit 29:	256-511 Byte Rx Cnt Ov*/
#define XMR_255B_OV	(1L<<28)	/* Bit 28:	128-255 Byte Rx Cnt Ov*/
#define XMR_127B_OV	(1L<<27)	/* Bit 27:	65-127 Byte Rx Cnt Ov */
#define XMR_64B_OV	(1L<<26)	/* Bit 26:	64 Byte Rx Cnt Ov */
#define XMR_UTIL_OV	(1L<<25)	/* Bit 25:	Rx Util Cnt Overflow */
#define XMR_UTIL_UR	(1L<<24)	/* Bit 24:	Rx Util Cnt Underrun */
#define XMR_CEX_ERR_OV	(1L<<23)	/* Bit 23:	CEXT Err Cnt Ov */
					/* Bit 22:	reserved */
#define XMR_FCS_ERR_OV	(1L<<21)	/* Bit 21:	Rx FCS Error Cnt Ov */
#define XMR_LNG_ERR_OV	(1L<<20)	/* Bit 20:	Rx too Long Err Cnt Ov*/
#define XMR_RUNT_OV	(1L<<19)	/* Bit 19:	Runt Event Cnt Ov */
#define XMR_SHT_ERR_OV	(1L<<18)	/* Bit 18:	Rx Short Ev Err Cnt Ov*/
#define XMR_SYM_ERR_OV	(1L<<17)	/* Bit 17:	Rx Sym Err Cnt Ov */
					/* Bit 16:	reserved */
#define XMR_CAR_ERR_OV	(1L<<15)	/* Bit 15:	Rx Carr Ev Err Cnt Ov */
#define XMR_JAB_PKT_OV	(1L<<14)	/* Bit 14:	Rx Jabb Packet Cnt Ov */
#define XMR_FIFO_OV	(1L<<13)	/* Bit 13:	Rx FIFO Ov Ev Cnt Ov */
#define XMR_FRA_ERR_OV	(1L<<12)	/* Bit 12:	Rx Framing Err Cnt Ov */
#define XMR_FMISS_OV	(1L<<11)	/* Bit 11:	Rx Missed Ev Cnt Ov */
#define XMR_BURST	(1L<<10)	/* Bit 10:	Rx Burst Event Cnt Ov */
#define XMR_INV_MOC	(1L<<9)		/* Bit	9:	Rx with inv. MAC OC Ov*/
#define XMR_INV_MP	(1L<<8)		/* Bit	8:	Rx inv Pause Frame Ov */
#define XMR_MCTRL_OV	(1L<<7)		/* Bit	7:	Rx MAC Ctrl-F Cnt Ov */
#define XMR_MPAUSE_OV	(1L<<6)		/* Bit	6:	Rx Pause MAC Ctrl-F Ov*/
#define XMR_UC_OK_OV	(1L<<5)		/* Bit	5:	Rx Unicast Frame CntOv*/
#define XMR_MC_OK_OV	(1L<<4)		/* Bit	4:	Rx Multicast Cnt Ov */
#define XMR_BC_OK_OV	(1L<<3)		/* Bit	3:	Rx Broadcast Cnt Ov */
#define XMR_OK_LO_OV	(1L<<2)		/* Bit	2:	Octets Rx OK Low CntOv*/
#define XMR_OK_HI_OV	(1L<<1)		/* Bit	1:	Octets Rx OK Hi Cnt Ov*/
#define XMR_OK_OV	(1L<<0)		/* Bit	0:	Frames Received Ok Ov */

#define XMR_DEF_MSK	0x00000006L	/* all bits excepting 1 and 2 */

/*	XM_TX_CNT_EV	32 bit ro	Tx Counter Event Register */
/*	XM_TX_EV_MSK	32 bit r/w	Tx Counter Event Mask */
					/* Bit 31..26:	reserved */
#define XMT_MAX_SZ_OV	(1L<<25)	/* Bit 25:	1024-MaxSize Tx Cnt Ov*/
#define XMT_1023B_OV	(1L<<24)	/* Bit 24:	512-1023Byte Tx Cnt Ov*/
#define XMT_511B_OV	(1L<<23)	/* Bit 23:	256-511 Byte Tx Cnt Ov*/
#define XMT_255B_OV	(1L<<22)	/* Bit 22:	128-255 Byte Tx Cnt Ov*/
#define XMT_127B_OV	(1L<<21)	/* Bit 21:	65-127 Byte Tx Cnt Ov */
#define XMT_64B_OV	(1L<<20)	/* Bit 20:	64 Byte Tx Cnt Ov */
#define XMT_UTIL_OV	(1L<<19)	/* Bit 19:	Tx Util Cnt Overflow */
#define XMT_UTIL_UR	(1L<<18)	/* Bit 18:	Tx Util Cnt Underrun */
#define XMT_CS_ERR_OV	(1L<<17)	/* Bit 17:	Tx Carr Sen Err Cnt Ov*/
#define XMT_FIFO_UR_OV	(1L<<16)	/* Bit 16:	Tx FIFO Ur Ev Cnt Ov */
#define XMT_EX_DEF_OV	(1L<<15)	/* Bit 15:	Tx Ex Deferall Cnt Ov */
#define XMT_DEF		(1L<<14)	/* Bit 14:	Tx Deferred Cnt Ov */
#define XMT_LAT_COL_OV	(1L<<13)	/* Bit 13:	Tx Late Col Cnt Ov */
#define XMT_ABO_COL_OV	(1L<<12)	/* Bit 12:	Tx abo dueto Ex Col Ov*/
#define XMT_MUL_COL_OV	(1L<<11)	/* Bit 11:	Tx Mult Col Cnt Ov */
#define XMT_SNG_COL	(1L<<10)	/* Bit 10:	Tx Single Col Cnt Ov */
#define XMT_MCTRL_OV	(1L<<9)		/* Bit	9:	Tx MAC Ctrl Counter Ov*/
#define XMT_MPAUSE	(1L<<8)		/* Bit	8:	Tx Pause MAC Ctrl-F Ov*/
#define XMT_BURST	(1L<<7)		/* Bit	7:	Tx Burst Event Cnt Ov */
#define XMT_LONG	(1L<<6)		/* Bit	6:	Tx Long Frame Cnt Ov */
#define XMT_UC_OK_OV	(1L<<5)		/* Bit	5:	Tx Unicast Cnt Ov */
#define XMT_MC_OK_OV	(1L<<4)		/* Bit	4:	Tx Multicast Cnt Ov */
#define XMT_BC_OK_OV	(1L<<3)		/* Bit	3:	Tx Broadcast Cnt Ov */
#define XMT_OK_LO_OV	(1L<<2)		/* Bit	2:	Octets Tx OK Low CntOv*/
#define XMT_OK_HI_OV	(1L<<1)		/* Bit	1:	Octets Tx OK Hi Cnt Ov*/
#define XMT_OK_OV	(1L<<0)		/* Bit	0:	Frames Tx Ok Ov */

#define XMT_DEF_MSK	0x00000006L	/* all bits excepting 1 and 2 */

/*
 * Receive Frame Status Encoding
 */
#define XMR_FS_LEN	(0x3fffUL<<18)	/* Bit 31..18:	Rx Frame Length */
#define XMR_FS_2L_VLAN	(1L<<17)	/* Bit 17:	tagged wh 2Lev VLAN ID*/
#define XMR_FS_1L_VLAN	(1L<<16)	/* Bit 16:	tagged wh 1Lev VLAN ID*/
#define XMR_FS_BC	(1L<<15)	/* Bit 15:	Broadcast Frame */
#define XMR_FS_MC	(1L<<14)	/* Bit 14:	Multicast Frame */
#define XMR_FS_UC	(1L<<13)	/* Bit 13:	Unicast Frame */
					/* Bit 12:	reserved */
#define XMR_FS_BURST	(1L<<11)	/* Bit 11:	Burst Mode */
#define XMR_FS_CEX_ERR	(1L<<10)	/* Bit 10:	Carrier Ext. Error */
#define XMR_FS_802_3	(1L<<9)		/* Bit	9:	802.3 Frame */
#define XMR_FS_COL_ERR	(1L<<8)		/* Bit	8:	Collision Error */
#define XMR_FS_CAR_ERR	(1L<<7)		/* Bit	7:	Carrier Event Error */
#define XMR_FS_LEN_ERR	(1L<<6)		/* Bit	6:	In-Range Length Error */
#define XMR_FS_FRA_ERR	(1L<<5)		/* Bit	5:	Framing Error */
#define XMR_FS_RUNT	(1L<<4)		/* Bit	4:	Runt Error */
#define XMR_FS_LNG_ERR	(1L<<3)		/* Bit	3:	Gaint Error */
#define XMR_FS_FCS_ERR	(1L<<2)		/* Bit	2:	Frame Check Sequ Err */
#define XMR_FS_ERR	(1L<<1)		/* Bit	1:	Frame Error */
#define XMR_FS_MCTRL	(1L<<0)		/* Bit	0:	MAC Control Packet */

/*
 * XMR_FS_ERR will be set if
 *	XMR_FS_FCS_ERR, XMR_FS_LNG_ERR, XMR_FS_RUNT,
 *	XMR_FS_FRA_ERR, XMR_FS_LEN_ERR, or XMR_FS_CEX_ERR
 * is set. XMR_FS_LNG_ERR and XMR_FS_LEN_ERR will issue
 * XMR_FS_ERR unless the corresponding bit in the Receive Command
 * Register is set.
 */
#define XMR_FS_ANY_ERR	XMR_FS_ERR

/*----------------------------------------------------------------------------*/
/*
 * XMAC-PHY Registers, indirect addressed over the XMAC
 */
#define	PHY_XMAC_CTRL		0x00	/* 16 bit r/w	PHY Control Register */
#define	PHY_XMAC_STAT		0x01	/* 16 bit r/w	PHY Status Register */
#define	PHY_XMAC_ID0		0x02	/* 16 bit ro	PHY ID0 Register */
#define	PHY_XMAC_ID1		0x03	/* 16 bit ro	PHY ID1 Register */
#define	PHY_XMAC_AUNE_ADV	0x04	/* 16 bit r/w	Autoneg Advertisement */
#define	PHY_XMAC_AUNE_LP	0x05	/* 16 bit ro	Link Partner Abi Reg */
#define	PHY_XMAC_AUNE_EXP	0x06	/* 16 bit ro	Autoneg Expansion Reg */
#define	PHY_XMAC_NEPG		0x07	/* 16 bit r/w	Next Page Register */
#define	PHY_XMAC_NEPG_LP	0x08	/* 16 bit ro	Next Page Link P Reg */
	/* 0x09 - 0x0e:		reserved */
#define	PHY_XMAC_EXT_STAT	0x0f	/* 16 bit ro	Ext Status Register */
#define	PHY_XMAC_RES_ABI	0x10	/* 16 bit ro	PHY Resolved Ability */

/*----------------------------------------------------------------------------*/
/*
 * Broadcom-PHY Registers, indirect addressed over XMAC
 */
#define	PHY_BCOM_CTRL		0x00	/* 16 bit r/w	PHY Control Register */
#define	PHY_BCOM_STAT		0x01	/* 16 bit ro	PHY Status Register */
#define	PHY_BCOM_ID0		0x02	/* 16 bit ro	PHY ID0 Register */
#define	PHY_BCOM_ID1		0x03	/* 16 bit ro	PHY ID1 Register */
#define	PHY_BCOM_AUNE_ADV	0x04	/* 16 bit r/w	Autoneg Advertisement */
#define	PHY_BCOM_AUNE_LP	0x05	/* 16 bit ro	Link Part Ability Reg */
#define	PHY_BCOM_AUNE_EXP	0x06	/* 16 bit ro	Autoneg Expansion Reg */
#define	PHY_BCOM_NEPG		0x07	/* 16 bit r/w	Next Page Register */
#define	PHY_BCOM_NEPG_LP	0x08	/* 16 bit ro	Next Page Link P Reg */
	/* Broadcom-specific registers */
#define	PHY_BCOM_1000T_CTRL	0x09	/* 16 bit r/w	1000Base-T Ctrl Reg */
#define	PHY_BCOM_1000T_STAT	0x0a	/* 16 bit ro	1000Base-T Status Reg */
	/* 0x0b - 0x0e:		reserved */
#define	PHY_BCOM_EXT_STAT	0x0f	/* 16 bit ro	Extended Status Reg */
#define	PHY_BCOM_P_EXT_CTRL	0x10	/* 16 bit r/w	PHY Extended Ctrl Reg */
#define	PHY_BCOM_P_EXT_STAT	0x11	/* 16 bit ro	PHY Extended Stat Reg */
#define	PHY_BCOM_RE_CTR		0x12	/* 16 bit r/w	Receive Error Counter */
#define	PHY_BCOM_FC_CTR		0x13	/* 16 bit r/w	False Carr Sense Cnt */
#define	PHY_BCOM_RNO_CTR	0x14	/* 16 bit r/w	Receiver NOT_OK Cnt */
	/* 0x15 - 0x17:		reserved */
#define	PHY_BCOM_AUX_CTRL	0x18	/* 16 bit r/w	Auxiliary Control Reg */
#define	PHY_BCOM_AUX_STAT	0x19	/* 16 bit ro	Auxiliary Stat Summary*/
#define	PHY_BCOM_INT_STAT	0x1a	/* 16 bit ro	Interrupt Status Reg */
#define	PHY_BCOM_INT_MASK	0x1b	/* 16 bit r/w	Interrupt Mask Reg */
	/* 0x1c:		reserved */
	/* 0x1d - 0x1f:		test registers */

/*----------------------------------------------------------------------------*/
/*
 * Level One-PHY Registers, indirect addressed over XMAC
 */
#define	PHY_LONE_CTRL		0x00	/* 16 bit r/w	PHY Control Register */
#define	PHY_LONE_STAT		0x01	/* 16 bit ro	PHY Status Register */
#define	PHY_LONE_ID0		0x02	/* 16 bit ro	PHY ID0 Register */
#define	PHY_LONE_ID1		0x03	/* 16 bit ro	PHY ID1 Register */
#define	PHY_LONE_AUNE_ADV	0x04	/* 16 bit r/w	Autoneg Advertisement */
#define	PHY_LONE_AUNE_LP	0x05	/* 16 bit ro	Link Part Ability Reg */
#define	PHY_LONE_AUNE_EXP	0x06	/* 16 bit ro	Autoneg Expansion Reg */
#define	PHY_LONE_NEPG		0x07	/* 16 bit r/w	Next Page Register */
#define	PHY_LONE_NEPG_LP	0x08	/* 16 bit ro	Next Page Link Partner*/
	/* Level One-specific registers */
#define	PHY_LONE_1000T_CTRL	0x09	/* 16 bit r/w	1000Base-T Control Reg*/
#define	PHY_LONE_1000T_STAT	0x0a	/* 16 bit ro	1000Base-T Status Reg */
	/* 0x0b -0x0e:		reserved */
#define	PHY_LONE_EXT_STAT	0x0f	/* 16 bit ro	Extended Status Reg */
#define	PHY_LONE_PORT_CFG	0x10	/* 16 bit r/w	Port Configuration Reg*/
#define	PHY_LONE_Q_STAT		0x11	/* 16 bit ro	Quick Status Reg */
#define	PHY_LONE_INT_ENAB	0x12	/* 16 bit r/w	Interrupt Enable Reg */
#define	PHY_LONE_INT_STAT	0x13	/* 16 bit ro	Interrupt Status Reg */
#define	PHY_LONE_LED_CFG	0x14	/* 16 bit r/w	LED Configuration Reg */
#define	PHY_LONE_PORT_CTRL	0x15	/* 16 bit r/w	Port Control Reg */
#define	PHY_LONE_CIM		0x16	/* 16 bit ro	CIM Reg */
	/* 0x17 -0x1c:		reserved */

/*----------------------------------------------------------------------------*/
/*
 * National-PHY Registers, indirect addressed over XMAC
 */
#define	PHY_NAT_CTRL		0x00	/* 16 bit r/w	PHY Control Register */
#define	PHY_NAT_STAT		0x01	/* 16 bit r/w	PHY Status Register */
#define	PHY_NAT_ID0		0x02	/* 16 bit ro	PHY ID0 Register */
#define	PHY_NAT_ID1		0x03	/* 16 bit ro	PHY ID1 Register */
#define	PHY_NAT_AUNE_ADV	0x04	/* 16 bit r/w	Autonegotiation Advertisement */
#define	PHY_NAT_AUNE_LP		0x05	/* 16 bit ro	Link Partner Ability Reg */
#define	PHY_NAT_AUNE_EXP	0x06	/* 16 bit ro	Autonegotiation Expansion Reg */
#define	PHY_NAT_NEPG		0x07	/* 16 bit r/w	Next Page Register */
#define	PHY_NAT_NEPG_LP		0x08	/* 16 bit ro	Next Page Link Partner Reg */
	/* National-specific registers */
#define	PHY_NAT_1000T_CTRL	0x09	/* 16 bit r/w	1000Base-T Control Reg */
#define	PHY_NAT_1000T_STAT	0x0a	/* 16 bit ro	1000Base-T Status Reg */
	/* 0x0b -0x0e:		reserved */
#define	PHY_NAT_EXT_STAT	0x0f	/* 16 bit ro	Extended Status Register */
#define	PHY_NAT_EXT_CTRL1	0x10	/* 16 bit ro	Extended Control Reg1 */
#define	PHY_NAT_Q_STAT1		0x11	/* 16 bit ro	Quick Status Reg1 */
#define	PHY_NAT_10B_OP		0x12	/* 16 bit ro	10Base-T Operations Reg */
#define	PHY_NAT_EXT_CTRL2	0x13	/* 16 bit ro	Extended Control Reg1 */
#define	PHY_NAT_Q_STAT2		0x14	/* 16 bit ro	Quick Status Reg2 */
	/* 0x15 -0x18:		reserved */
#define	PHY_NAT_PHY_ADDR	0x19	/* 16 bit ro	PHY Address Register */


/*----------------------------------------------------------------------------*/

/*
 * PHY bit definitions
 * Bits defined as PHY_X_..., PHY_B_..., PHY_L_... or PHY_N_... are
 * Xmac/Broadcom/LevelOne/National-specific.
 * All other are general.
 */

/*****	PHY_XMAC_CTRL	16 bit r/w	PHY Control Register *****/
/*****	PHY_BCOM_CTRL	16 bit r/w	PHY Control Register *****/
/*****	PHY_LONE_CTRL	16 bit r/w	PHY Control Register *****/
#define	PHY_CT_RESET	(1<<15)	/* Bit 15: (sc)	clear all PHY releated regs */
#define PHY_CT_LOOP	(1<<14)	/* Bit 14:	enable Loopback over PHY */
#define PHY_CT_SPS_LSB	(1<<13) /* Bit 13: (BC,L1) Speed select, lower bit */
#define PHY_CT_ANE	(1<<12)	/* Bit 12:	Autonegotiation Enabled */
#define PHY_CT_PDOWN	(1<<11)	/* Bit 11: (BC,L1) Power Down Mode */
#define PHY_CT_ISOL	(1<<10)	/* Bit 10: (BC,L1) Isolate Mode */
#define PHY_CT_RE_CFG	(1<<9)	/* Bit	9: (sc) Restart Autonegotiation */
#define PHY_CT_DUP_MD	(1<<8)	/* Bit	8:	Duplex Mode */
#define PHY_CT_COL_TST	(1<<7)	/* Bit	7: (BC,L1) Collsion Test enabled */
#define PHY_CT_SPS_MSB	(1<<6)	/* Bit	6: (BC,L1) Speed select, upper bit */
				/* Bit	5..0:	reserved */

#define PHY_B_CT_SP1000	(1<<6)	/* Bit  6:	enable speed of 1000 MBit/s */
#define PHY_B_CT_SP100	(1<<13)	/* Bit 13:	enable speed of  100 MBit/s */
#define PHY_B_CT_SP10	(0)	/* Bit 6/13 not set,	speed of  10 MBit/s */

#define PHY_L_CT_SP1000	(1<<6)	/* Bit  6:      enable speed of 1000 MBit/s */
#define PHY_L_CT_SP100	(1<<13)	/* Bit 13:      enable speed of  100 MBit/s */
#define PHY_L_CT_SP10	(0)	/* Bit 6/13 not set,    speed of  10 MBit/s */


/*****	PHY_XMAC_STAT	16 bit r/w	PHY Status Register *****/
/*****	PHY_BCOM_STAT	16 bit r/w	PHY Status Register *****/
/*****	PHY_LONE_STAT	16 bit r/w	PHY Status Register *****/
				/* Bit 15..9:	reserved */
				/*	(BC/L1) 100/10 MBit/s cap bits ignored*/
#define PHY_ST_EXT_ST	(1<<8)	/* Bit	8:	Extended Status Present */
				/* Bit	7:	reserved */
#define PHY_ST_PRE_SUB	(1<<6)	/* Bit	6: (BC/L1) preamble suppression */
#define PHY_ST_AN_OVER	(1<<5)	/* Bit	5:	Autonegotiation Over */
#define PHY_ST_REM_FLT	(1<<4)	/* Bit	4:	Remode Fault Condition Occured*/
#define PHY_ST_AN_CAP	(1<<3)	/* Bit	3:	Autonegotiation Capability */
#define PHY_ST_LSYNC	(1<<2)	/* Bit	2:	Link Synchronized */
#define PHY_ST_JAP_DET	(1<<1)	/* Bit	1: (BC/L1) Japper Detected */
#define PHY_ST_EXT_REG	(1<<0)	/* Bit	0:	Extended Register available */


/*	PHY_XMAC_ID1		16 bit ro	PHY ID1 Register */
/*	PHY_BCOM_ID1		16 bit ro	PHY ID1 Register */
/*	PHY_LONE_ID1		16 bit ro	PHY ID1 Register */
#define	PHY_I1_OUI	(0x3f<<10)	/* Bit 15..10:	Organiz. Unique ID */
#define PHY_I1_MOD_NUM	(0x3f<<4)	/* Bit	9.. 4:	Model Number */
#define PHY_I1_REV	(0x0f<<0)	/* Bit	3.. 0:	Revision Number */


/*****	PHY_XMAC_AUNE_ADV	16 bit r/w	Autoneg Advertisement *****/
/*****	PHY_XMAC_AUNE_LP	16 bit ro	Link Partner Ability Reg *****/
#define PHY_AN_NXT_PG	(1<<15)	/* Bit 15:	Request Next Page */
#define PHY_X_AN_ACK	(1<<14)	/* Bit 14: (ro)	Acknowledge Received */
#define PHY_X_AN_RFB	(3<<12)	/* Bit 13..12:	Remode Fault Bits */
				/* Bit 11.. 9:	reserved */
#define PHY_X_AN_PAUSE	(3<<7)	/* Bit	8.. 7:	Pause Bits */
#define PHY_X_AN_HD	(1<<6)	/* Bit	6:	Half Duplex */
#define PHY_X_AN_FD	(1<<5)	/* Bit	5:	Full Duplex */
				/* Bit	4.. 0:	reserved */

/*****	PHY_BCOM_AUNE_ADV	16 bit r/w	Autoneg Advertisement *****/
/*****	PHY_BCOM_AUNE_LP	16 bit ro	Link Partner Ability Reg *****/
/*	PHY_AN_NXT_PG	(see XMAC) Bit 15:	Request Next Page */
				/* Bit 14:	reserved */
#define PHY_B_AN_RF	(1<<13)	/* Bit 13:	Remote Fault */
				/* Bit 12:	reserved */
#define PHY_B_AN_ASP	(1<<11)	/* Bit 11:	Asymetric Pause */
#define PHY_B_AN_PC	(1<<10)	/* Bit 10:	Pause Capable */
				/* Bit	9..5:	100/10 BT cap bits ingnored */
#define PHY_B_AN_SEL	(0x1f<<0)/* Bit 4..0:	Selector Field, 00001=Ethernet*/

/*****	PHY_LONE_AUNE_ADV	16 bit r/w	Autoneg Advertisement *****/
/*****	PHY_LONE_AUNE_LP	16 bit ro	Link Partner Ability Reg *****/
/*	PHY_AN_NXT_PG	(see XMAC) Bit 15:	Request Next Page */
				/* Bit 14:	reserved */
#define PHY_L_AN_RF	(1<<13)	/* Bit 13:	Remote Fault */
				/* Bit 12:	reserved */
#define PHY_L_AN_ASP	(1<<11)	/* Bit 11:	Asymetric Pause */
#define PHY_L_AN_PC	(1<<10)	/* Bit 10:	Pause Capable */
				/* Bit	9..5:	100/10 BT cap bits ingnored */
#define PHY_L_AN_SEL	(0x1f<<0)/* Bit 4..0:	Selector Field, 00001=Ethernet*/

/*****	PHY_NAT_AUNE_ADV	16 bit r/w	Autoneg Advertisement *****/
/*****	PHY_NAT_AUNE_LP	16 bit ro	Link Partner Ability Reg *****/
/*	PHY_AN_NXT_PG	(see XMAC) Bit 15:	Request Next Page */
				/* Bit 14:	reserved */
#define PHY_N_AN_RF	(1<<13)	/* Bit 13:	Remote Fault */
				/* Bit 12:	reserved */
#define PHY_N_AN_100F	(1<<11)	/* Bit 11:	100Base-T2 FD Support */
#define PHY_N_AN_100H	(1<<10)	/* Bit 10:	100Base-T2 HD Support */
				/* Bit	9..5:	100/10 BT cap bits ingnored */
#define PHY_N_AN_SEL	(0x1f<<0)/* Bit 4..0:	Selector Field, 00001=Ethernet*/

/* field type definition for PHY_x_AN_SEL */
#define PHY_SEL_TYPE	0x01	/* 00001 = Ethernet */

/*****	PHY_XMAC_AUNE_EXP	16 bit ro	Autoneg Expansion Reg *****/
				/* Bit 15..4:	reserved */
#define PHY_AN_LP_NP	(1<<3)	/* Bit	3:	Link Partner can Next Page */
#define PHY_AN_LOC_NP	(1<<2)	/* Bit	2:	Local PHY can Next Page */
#define PHY_AN_RX_PG	(1<<1)	/* Bit	1:	Page Received */
				/* Bit	0:	reserved */

/*****	PHY_BCOM_AUNE_EXP	16 bit ro	Autoneg Expansion Reg *****/
				/* Bit 15..5:	reserved */
#define PHY_B_AN_PDF	(1<<4)	/* Bit	4:	Parallel Detection Fault */
/*	PHY_AN_LP_NP	(see XMAC) Bit	3:	Link Partner can Next Page */
/*	PHY_AN_LOC_NP	(see XMAC) Bit	2:	Local PHY can Next Page */
/*	PHY_AN_RX_PG	(see XMAC) Bit	1:	Page Received */
#define PHY_B_AN_LP_CAP	(1<<0)	/* Bit	0:	Link Partner Autoneg Cap. */ 	

/*****	PHY_LONE_AUNE_EXP	16 bit ro	Autoneg Expansion Reg *****/
#define PHY_L_AN_BP	(1<<5)	/* Bit	5:	Base Page Indication */
#define PHY_L_AN_PDF	(1<<4)	/* Bit	4:	Parallel Detection Fault */
/*	PHY_AN_LP_NP	(see XMAC) Bit	3:	Link Partner can Next Page */
/*	PHY_AN_LOC_NP	(see XMAC) Bit	2:	Local PHY can Next Page */
/*	PHY_AN_RX_PG	(see XMAC) Bit	1:	Page Received */
#define PHY_B_AN_LP_CAP	(1<<0)	/* Bit	0:	Link Partner Autoneg Cap. */ 	


/*****	PHY_XMAC_NEPG		16 bit r/w	Next Page Register *****/
/*****	PHY_BCOM_NEPG		16 bit r/w	Next Page Register *****/
/*****	PHY_LONE_NEPG		16 bit r/w	Next Page Register *****/
/*****	PHY_XMAC_NEPG_LP	16 bit ro	Next Page Link Partner *****/
/*****	PHY_BCOM_NEPG_LP	16 bit ro	Next Page Link Partner *****/
/*****	PHY_LONE_NEPG_LP	16 bit ro	Next Page Link Partner *****/
#define PHY_NP_MORE	(1<<15)	/* Bit 15:	More, Next Pages to follow */
#define PHY_NP_ACK1	(1<<14)	/* Bit 14: (ro)	Ack 1, for receiving a message*/
#define PHY_NP_MSG_VAL	(1<<13)	/* Bit 13:	Message Page valid */
#define PHY_NP_ACK2	(1<<12)	/* Bit 12:	Ack 2, comply with msg content*/
#define PHY_NP_TOG	(1<<11)	/* Bit 11:	Toggle Bit, ensure sync */
#define PHY_NP_MSG	0x07ff	/* Bit 10..0:	Message from/to Link Partner */

/*
 * XMAC-Specific
 */
/*****	PHY_XMAC_EXT_STAT	16 bit r/w	Extended Status Register *****/
#define PHY_X_EX_FD	(1<<15)	/* Bit 15:	Device Supports Full Duplex */
#define PHY_X_EX_HD	(1<<14)	/* Bit 14:	Device Supports Half Duplex */
				/* Bit 13..0:	reserved */

/*****	PHY_XMAC_RES_ABI	16 bit ro	PHY Resolved Ability *****/
				/* Bit 15..9:	reserved */
#define PHY_X_RS_PAUSE	(3<<7)	/* Bit	8..7:	selected Pause Mode */
#define PHY_X_RS_HD	(1<<6)	/* Bit	6:	Half Duplex Mode selected */
#define PHY_X_RS_FD	(1<<5)	/* Bit	5:	Full Duplex Mode selected */
#define PHY_X_RS_ABLMIS (1<<4)	/* Bit	4:	duplex or pause cap mismatch */
#define PHY_X_RS_PAUMIS (1<<3)	/* Bit	3:	pause capability missmatch */
				/* Bit	2..0:	reserved */
/*
 * Remote Fault Bits (PHY_X_AN_RFB) encoding
 */
#define X_RFB_OK	(0<<12)	/* Bit 12..13	No errors, Link OK */
#define X_RFB_LF	(1<<12)	/* Bit 12..13	Link Failure */
#define X_RFB_OFF	(2<<12)	/* Bit 12..13	Offline */
#define X_RFB_AN_ERR	(3<<12)	/* Bit 12..13	Autonegotiation Error */

/*
 * Pause Bits (PHY_X_AN_PAUSE and PHY_X_RS_PAUSE) encoding
 */
#define PHY_X_P_NO_PAUSE	(0<<7)	/* Bit	8..7:	no Pause Mode */
#define PHY_X_P_SYM_MD		(1<<7)	/* Bit	8..7:	symmetric Pause Mode */
#define PHY_X_P_ASYM_MD		(2<<7)	/* Bit	8..7:	asymmetric Pause Mode */
#define PHY_X_P_BOTH_MD		(3<<7)	/* Bit	8..7:	both Pause Mode */


/*
 * Broadcom-Specific
 */
/***** PHY_BCOM_1000T_CTRL	16 bit r/w	1000Base-T Control Reg *****/
#define PHY_B_1000C_TEST	(7<<13)	/* Bit	15..13:	Test Modes */
#define PHY_B_1000C_MSE		(1<<12)	/* Bit	12:	Master/Slave Enable */
#define PHY_B_1000C_MSC		(1<<11)	/* Bit	11:	M/S Configuration */
#define PHY_B_1000C_RD		(1<<10)	/* Bit	10:	Repeater/DTE */
#define PHY_B_1000C_AFD		(1<<9)	/* Bit	9:	Advertise Full Duplex */
#define PHY_B_1000C_AHD		(1<<8)	/* Bit	8:	Advertise Half Duplex */
					/* Bit	7..0:	reserved */

/***** PHY_BCOM_1000T_STAT	16 bit ro	1000Base-T Status Reg *****/
#define PHY_B_1000S_MSF		(1<<15)	/* Bit	15:	Master/Slave Fault */
#define PHY_B_1000S_MSR		(1<<14)	/* Bit	14:	Master/Slave Result */
#define PHY_B_1000S_LRS		(1<<13)	/* Bit	13:	Local Receiver Status */
#define PHY_B_1000S_RRS		(1<<12)	/* Bit	12:	Remote Receiver Status */
#define PHY_B_1000S_LP_FD	(1<<11)	/* Bit	11:	Link Partner can FD */
#define PHY_B_1000S_LP_HD	(1<<10)	/* Bit	10:	Link Partner can HD */
					/* Bit	9..8:	reserved */
#define PHY_B_1000S_IEC		(255<<0)/* Bit	7..0:	Idle Error Count */

/*****	PHY_BCOM_EXT_STAT	16 bit ro	Extended Status Register *****/
#define PHY_B_ES_X_FD_CAP	(1<<15)	/* Bit 15:	1000Base-X FD capable */
#define PHY_B_ES_X_HD_CAP	(1<<14)	/* Bit 14:	1000Base-X HD capable */
#define PHY_B_ES_T_FD_CAP	(1<<13)	/* Bit 13:	1000Base-T FD capable */
#define PHY_B_ES_T_HD_CAP	(1<<12)	/* Bit 12:	1000Base-T HD capable */
					/* Bit 11..0:	reserved */

/*****	PHY_BCOM_P_EXT_CTRL	16 bit r/w	PHY Extended Control Reg *****/
#define PHY_B_PEC_MAC_PHY	(1<<15)	/* Bit 15:	10BIT/GMI-Interface */
#define PHY_B_PEC_DIS_CROSS	(1<<14)	/* Bit 14:	Disable MDI Crossover */
#define PHY_B_PEC_TX_DIS	(1<<13)	/* Bit 13:	Tx output Disabled */
#define PHY_B_PEC_INT_DIS	(1<<12)	/* Bit 12:	Interrupts Disabled */
#define PHY_B_PEC_F_INT		(1<<11)	/* Bit 11:	Force Interrupt */
#define PHY_B_PEC_BY_45		(1<<10)	/* Bit 10:	Bypass 4B5B-Decoder */
#define PHY_B_PEC_BY_SCR	(1<<9)	/* Bit 9:	Bypass Scrambler */
#define PHY_B_PEC_BY_MLT3	(1<<8)	/* Bit 8:	Bypass MLT3 Encoder */
#define PHY_B_PEC_BY_RXA	(1<<7)	/* Bit 7:	Bypass Rx Alignm. */
#define PHY_B_PEC_RES_SCR	(1<<6)	/* Bit 6:	Reset Scrambler */
#define PHY_B_PEC_EN_LTR	(1<<5)	/* Bit 5:	Ena LED Traffic Mode */
#define PHY_B_PEC_LED_ON	(1<<4)	/* Bit 4:	Force LED's on */
#define PHY_B_PEC_LED_OFF	(1<<3)	/* Bit 3:	Force LED's off */
#define PHY_B_PEC_EX_IPG	(1<<2)	/* Bit 2:	Extend Tx IPG Mode */
#define PHY_B_PEC_3_LED		(1<<1)	/* Bit 1:	Three Link LED mode */
#define PHY_B_PEC_HIGH_LA	(1<<0)	/* Bit 0:	GMII Fifo Elasticy */

/*****	PHY_BCOM_P_EXT_STAT	16 bit ro	PHY Extended Status Reg *****/
					/* Bit 15..14:	reserved */
#define PHY_B_PES_CROSS_STAT	(1<<13)	/* Bit 13:	MDI Crossover Status */
#define PHY_B_PES_INT_STAT	(1<<12)	/* Bit 12:	Interrupt Status */
#define PHY_B_PES_RRS		(1<<11)	/* Bit 11:	Remote Receiver Stat. */
#define PHY_B_PES_LRS		(1<<10)	/* Bit 10:	Local Receiver Stat. */
#define PHY_B_PES_LOCKED	(1<<9)	/* Bit 9:	Locked */
#define PHY_B_PES_LS		(1<<8)	/* Bit 8:	Link Status */
#define PHY_B_PES_RF		(1<<7)	/* Bit 7:	Remote Fault */
#define PHY_B_PES_CE_ER		(1<<6)	/* Bit 6:	Carrier Ext Error */
#define PHY_B_PES_BAD_SSD	(1<<5)	/* Bit 5:	Bad SSD */
#define PHY_B_PES_BAD_ESD	(1<<4)	/* Bit 4:	Bad ESD */
#define PHY_B_PES_RX_ER		(1<<3)	/* Bit 3:	Receive Error */
#define PHY_B_PES_TX_ER		(1<<2)	/* Bit 2:	Transmit Error */
#define PHY_B_PES_LOCK_ER	(1<<1)	/* Bit 1:	Lock Error */
#define PHY_B_PES_MLT3_ER	(1<<0)	/* Bit 0:	MLT3 code Error */

/*****	PHY_BCOM_FC_CTR		16 bit r/w	False Carrier Counter *****/
					/* Bit 15..8:	reserved */
#define PHY_B_FC_CTR		(255<<0)/* Bit 7..0:	False Carrier Counter */

/*****	PHY_BCOM_RNO_CTR	16 bit r/w	Receive NOT_OK Counter *****/
#define PHY_B_RC_LOC		(255<<8)/* Bit 15..8:	Local Rx NOT_OK cnt */
#define PHY_B_RC_REM		(255<<0)/* Bit  7..0:	Remote Rx NOT_OK cnt */

/*****	PHY_BCOM_AUX_CTRL	16 bit r/w	Auxiliary Control Reg *****/
#define PHY_B_AC_L_SQE		(1<<15)	/* Bit 15:	Low Squelch */
#define PHY_B_AC_LONG_PACK	(1<<14)	/* Bit 14:	Rx Long Packets */
#define PHY_B_AC_ER_CTRL	(3<<12)	/* Bit 13..12:	Edgerate Control */
					/* Bit 11:	reserved */
#define PHY_B_AC_TX_TST		(1<<10) /* Bit 10:	tx test bit, always 1 */
					/* Bit  9.. 8:	reserved */
#define PHY_B_AC_DIS_PRF	(1<<7)	/* Bit  7:	dis part resp filter */
					/* Bit  6:	reserved */
#define PHY_B_AC_DIS_PM		(1<<5)	/* Bit  5:	dis power management */
					/* Bit  4:	reserved */
#define PHY_B_AC_DIAG		(1<<3)	/* Bit  3:	Diagnostic Mode */
					/* Bit  2.. 0:	reserved */

/*****	PHY_BCOM_AUX_STAT	16 bit ro	Auxiliary Status Reg *****/
#define PHY_B_AS_AN_C		(1<<15)	/* Bit 15:	AutoNeg complete */
#define PHY_B_AS_AN_CA		(1<<14)	/* Bit 14:	AN Complete Ack */
#define PHY_B_AS_ANACK_D	(1<<13)	/* Bit 13:	AN Ack Detect */
#define PHY_B_AS_ANAB_D		(1<<12)	/* Bit 12:	AN Ability Detect */
#define PHY_B_AS_NPW		(1<<11)	/* Bit 11:	AN Next Page Wait */
#define PHY_B_AS_AN_RES		(7<<8)	/* Bit 10..8:	AN HDC */
#define PHY_B_AS_PDF		(1<<7)	/* Bit 7:	Parallel Detect. Fault*/
#define PHY_B_AS_RF		(1<<6)	/* Bit 6:	Remote Fault */
#define PHY_B_AS_ANP_R		(1<<5)	/* Bit 5:	AN Page Received */
#define PHY_B_AS_LP_ANAB	(1<<4)	/* Bit 4:	LP AN Ability */
#define PHY_B_AS_LP_NPAB	(1<<3)	/* Bit 3:	LP Next Page Ability */
#define PHY_B_AS_LS		(1<<2)	/* Bit 2:	Link Status */
#define PHY_B_AS_PRR		(1<<1)	/* Bit 1:	Pause Resolution-Rx */
#define PHY_B_AS_PRT		(1<<0)	/* Bit 0:	Pause Resolution-Tx */

/*****	PHY_BCOM_INT_STAT	16 bit ro	Interrupt Status Reg *****/
/*****	PHY_BCOM_INT_MASK	16 bit r/w	Interrupt Mask Reg *****/
					/* Bit 15:	reserved */
#define PHY_B_IS_PSE		(1<<14)	/* Bit 14:	Pair Swap Error */
#define PHY_B_IS_MDXI_SC	(1<<13)	/* Bit 13:	MDIX Status Change */
#define PHY_B_IS_HCT		(1<<12)	/* Bit 12:	counter above 32k */
#define PHY_B_IS_LCT		(1<<11)	/* Bit 11:	all counter below 128 */
#define PHY_B_IS_AN_PR		(1<<10)	/* Bit 10:	Page Received */
#define PHY_B_IS_NO_HDCL	(1<<9)	/* Bit 9:	No HCD Link */
#define PHY_B_IS_NO_HDC		(1<<8)	/* Bit 8:	No HCD */
#define PHY_B_IS_NEG_USHDC	(1<<7)	/* Bit 7:	Negotiated Unsup. HCD */
#define PHY_B_IS_SCR_S_ER	(1<<6)	/* Bit 6:	Scrambler Sync Error */
#define PHY_B_IS_RRS_CHANGE	(1<<5)	/* Bit 5:	Remote Rx Stat Change */
#define PHY_B_IS_LRS_CHANGE	(1<<4)	/* Bit 4:	Local Rx Stat Change */
#define PHY_B_IS_DUP_CHANGE	(1<<3)	/* Bit 3:	Duplex Mode Change */
#define PHY_B_IS_LSP_CHANGE	(1<<2)	/* Bit 2:	Link Speed Change */
#define PHY_B_IS_LST_CHANGE	(1<<1)	/* Bit 1:	Link Status Changed */
#define PHY_B_IS_CRC_ER		(1<<0)	/* Bit 0:	CRC Error */

#define PHY_B_DEF_MSK	(~(PHY_B_IS_AN_PR | PHY_B_IS_LST_CHANGE))


/*
 * Pause Bits (PHY_B_AN_ASP and PHY_B_AN_PC) encoding
 */
#define PHY_B_P_NO_PAUSE	(0<<10)	/* Bit	11..10:	no Pause Mode */
#define PHY_B_P_SYM_MD		(1<<10)	/* Bit	11..10:	symmetric Pause Mode */
#define PHY_B_P_ASYM_MD		(2<<10)	/* Bit	11..10:	asymmetric Pause Mode */
#define PHY_B_P_BOTH_MD		(3<<10)	/* Bit	11..10:	both Pause Mode */

/*
 * Resolved Duplex mode and Capabilities (Aux Status Summary Reg)
 */
#define	PHY_B_RES_1000FD	(7<<8)	/* Bit	10..8:	1000Base-T Full Dup. */
#define	PHY_B_RES_1000HD	(6<<8)	/* Bit	10..8:	1000Base-T Half Dup. */
/* others: 100/10: invalid for us */

/*
 * Level One-Specific
 */
/*****	PHY_LONE_1000T_CTRL	16 bit r/w	1000Base-T Control Reg *****/
#define PHY_L_1000C_TEST	(7<<13)	/* Bit	15..13:	Test Modes */
#define PHY_L_1000C_MSE		(1<<12)	/* Bit	12:	Master/Slave Enable */
#define PHY_L_1000C_MSC		(1<<11)	/* Bit	11:	M/S Configuration */
#define PHY_L_1000C_RD		(1<<10)	/* Bit	10:	Repeater/DTE */
#define PHY_L_1000C_AFD		(1<<9)	/* Bit	9:	Advertise Full Duplex */
#define PHY_L_1000C_AHD		(1<<8)	/* Bit	8:	Advertise Half Duplex */
					/* Bit	7..0:	reserved */

/*****	PHY_LONE_1000T_STAT	16 bit ro	1000Base-T Status Reg *****/
#define PHY_L_1000S_MSF		(1<<15)	/* Bit	15:	Master/Slave Fault */
#define PHY_L_1000S_MSR		(1<<14)	/* Bit	14:	Master/Slave Result */
#define PHY_L_1000S_LRS		(1<<13)	/* Bit	13:	Local Receiver Status */
#define PHY_L_1000S_RRS		(1<<12)	/* Bit	12:	Remote Receiver Status*/
#define PHY_L_1000S_LP_FD	(1<<11)	/* Bit	11:	Link Partner can FD */
#define PHY_L_1000S_LP_HD	(1<<10)	/* Bit	10:	Link Partner can HD */
					/* Bit	9..8:	reserved */
#define PHY_B_1000S_IEC		(255<<0)/* Bit	7..0:	Idle Error Count */

/*****	PHY_LONE_EXT_STAT	16 bit ro	Extended Status Register *****/
#define PHY_L_ES_X_FD_CAP	(1<<15)	/* Bit 15:	1000Base-X FD capable */
#define PHY_L_ES_X_HD_CAP	(1<<14)	/* Bit 14:	1000Base-X HD capable */
#define PHY_L_ES_T_FD_CAP	(1<<13)	/* Bit 13:	1000Base-T FD capable */
#define PHY_L_ES_T_HD_CAP	(1<<12)	/* Bit 12:	1000Base-T HD capable */
					/* Bit 11..0:	reserved */

/*****	PHY_LONE_PORT_CFG	16 bit r/w	Port Configuration Reg *****/
#define PHY_L_PC_REP_MODE	(1<<15)	/* Bit 15:	Repeater Mode */
					/* Bit 14:	reserved */
#define PHY_L_PC_TX_DIS		(1<<13)	/* Bit 13:	Tx output Disabled */
#define PHY_L_PC_BY_SCR		(1<<12)	/* Bit 12:	Bypass Scrambler */
#define PHY_L_PC_BY_45		(1<<11)	/* Bit 11:	Bypass 4B5B-Decoder */
#define PHY_L_PC_JAB_DIS	(1<<10)	/* Bit 10:	Jabber Disabled */
#define PHY_L_PC_SQE		(1<<9)	/* Bit 9:	Enable Heartbeat */
#define PHY_L_PC_TP_LOOP	(1<<8)	/* Bit 8:	TP Loopback */
#define PHY_L_PC_SSS		(1<<7)	/* Bit 7:	Smart Speed Selection */
#define PHY_L_PC_FIFO_SIZE	(1<<6)	/* Bit 6:	FIFO Size */
#define PHY_L_PC_PRE_EN		(1<<5)	/* Bit 5:	Preamble Enable */
#define PHY_L_PC_CIM		(1<<4)	/* Bit 4:	Carrier Integrity Mon */
#define PHY_L_PC_10_SER		(1<<3)	/* Bit 3:	Use Serial Output */
#define PHY_L_PC_ANISOL		(1<<2)	/* Bit 2:	Unisolate Port */
#define PHY_L_PC_TEN_BIT	(1<<1)	/* Bit 1:	10bit iface mode on */
#define PHY_L_PC_ALTCLOCK	(1<<0)	/* Bit 0: (ro)	ALTCLOCK Mode on */

/*****	PHY_LONE_Q_STAT		16 bit ro	Quick Status Reg *****/
#define PHY_L_QS_D_RATE		(3<<14)	/* Bit 15..14:	Data Rate */
#define PHY_L_QS_TX_STAT	(1<<13)	/* Bit 13:	Transmitting */
#define PHY_L_QS_RX_STAT	(1<<12)	/* Bit 12:	Receiving */
#define PHY_L_QS_COL_STAT	(1<<11)	/* Bit 11:	Collision */
#define PHY_L_QS_L_STAT		(1<<10)	/* Bit 10:	Link is up */
#define PHY_L_QS_DUP_MOD	(1<<9)	/* Bit 9:	Full/Half Duplex */
#define PHY_L_QS_AN		(1<<8)	/* Bit 8:	AutoNeg is On */
#define PHY_L_QS_AN_C		(1<<7)	/* Bit 7:	AN is Complete */
#define PHY_L_QS_LLE		(7<<4)	/* Bit 6:	Line Length Estim. */
#define PHY_L_QS_PAUSE		(1<<3)	/* Bit 3:	LP advertised Pause */
#define PHY_L_QS_AS_PAUSE	(1<<2)	/* Bit 2:	LP adv. asym. Pause */
#define PHY_L_QS_ISOLATE	(1<<1)	/* Bit 1:	CIM Isolated */
#define PHY_L_QS_EVENT		(1<<0)	/* Bit 0:	Event has occurred */

/*****	PHY_LONE_INT_ENAB	16 bit r/w	Interrupt Enable Reg *****/
/*****	PHY_LONE_INT_STAT	16 bit ro	Interrupt Status Reg *****/
					/* Bit 15..14:	reserved */
#define PHY_L_IS_AN_F		(1<<13)	/* Bit 13:	Autoneg fault */
					/* Bit 12:	not described */
#define PHY_L_IS_CROSS		(1<<11)	/* Bit 11:	Crossover used */
#define PHY_L_IS_POL		(1<<10)	/* Bit 10:	Polarity correct. used*/
#define PHY_L_IS_SS		(1<<9)	/* Bit 9:	Smart Speed Downgrade*/
#define PHY_L_IS_CFULL		(1<<8)	/* Bit 8:	Counter Full */
#define PHY_L_IS_AN_C		(1<<7)	/* Bit 7:	AutoNeg Complete */
#define PHY_L_IS_SPEED		(1<<6)	/* Bit 6:	Speed Changed */
#define PHY_L_IS_DUP		(1<<5)	/* Bit 5:	Duplex Changed */
#define PHY_L_IS_LS		(1<<4)	/* Bit 4:	Link Status Changed */
#define PHY_L_IS_ISOL		(1<<3)	/* Bit 3:	Isolate Occured */
#define PHY_L_IS_MDINT		(1<<2)	/* Bit 2: (ro)	STAT: MII Int Pending */
#define PHY_L_IS_INTEN		(1<<1)	/* Bit 1:	ENAB: Enable IRQs */
#define PHY_L_IS_FORCE		(1<<0)	/* Bit 0:	ENAB: Force Interrupt */

#define PHY_L_DEF_MSK		(PHY_L_IS_LS | PHY_L_IS_ISOL | \
				PHY_L_IS_INTEN)	/* int. mask */

/*****	PHY_LONE_LED_CFG	16 bit r/w	LED Configuration Reg *****/
#define PHY_L_LC_LEDC		(3<<14)	/* Bit 15..14:	Col/Blink/On/Off */
#define PHY_L_LC_LEDR		(3<<12)	/* Bit 13..12:	Rx/Blink/On/Off */
#define PHY_L_LC_LEDT		(3<<10)	/* Bit 11..10:	Tx/Blink/On/Off */
#define PHY_L_LC_LEDG		(3<<8)	/* Bit 9..8:	Giga/Blink/On/Off */
#define PHY_L_LC_LEDS		(3<<6)	/* Bit 7..6:	10-100/Blink/On/Off */
#define PHY_L_LC_LEDL		(3<<4)	/* Bit 5..4:	Link/Blink/On/Off */
#define PHY_L_LC_LEDF		(3<<2)	/* Bit 3..2:	Duplex/Blink/On/Off */
#define PHY_L_LC_PSTRECH	(1<<1)	/* Bit 1:	Strech LED Pulses */
#define PHY_L_LC_FREQ		(1<<0)	/* Bit 0:	30/100 ms */

/*****	PHY_LONE_PORT_CTRL	16 bit r/w	Port Control Reg *****/
#define PHY_L_PC_TX_TCLK	(1<<15)	/* Bit 15:	Enable TX_TCLK */
					/* Bit 14:	reserved */
#define PHY_L_PC_ALT_NP		(1<<13)	/* Bit 14:	Alternate Next Page */
#define PHY_L_PC_GMII_ALT	(1<<12)	/* Bit 13:	Alternate GMII driver */
					/* Bit 11:	reserved */
#define PHY_L_PC_TEN_CRS	(1<<10)	/* Bit 10:	Extend CRS*/
					/* Bit 9..0:	not described */

/*****	PHY_LONE_CIM		16 bit ro	CIM Reg *****/
#define PHY_L_CIM_ISOL		(255<<8)/* Bit 15..8:	Isolate Count */
#define PHY_L_CIM_FALSE_CAR	(255<<0)/* Bit  7..0:	False Carrier Count */


/*
 * Pause Bits (PHY_L_AN_ASP and PHY_L_AN_PC) encoding
 */
#define PHY_L_P_NO_PAUSE	(0<<10)	/* Bit	11..10:	no Pause Mode */
#define PHY_L_P_SYM_MD		(1<<10)	/* Bit	11..10:	symmetric Pause Mode */
#define PHY_L_P_ASYM_MD		(2<<10)	/* Bit	11..10:	asymmetric Pause Mode */
#define PHY_L_P_BOTH_MD		(3<<10)	/* Bit	11..10:	both Pause Mode */


/*
 * National-Specific
 */
/***** PHY_NAT_1000T_CTRL	16 bit r/w	1000Base-T Control Reg *****/
#define PHY_N_1000C_TEST	(7<<13)	/* Bit	15..13:	Test Modes */
#define PHY_N_1000C_MSE		(1<<12)	/* Bit	12:	Master/Slave Enable */
#define PHY_N_1000C_MSC		(1<<11)	/* Bit	11:	M/S Configuration */
#define PHY_N_1000C_RD		(1<<10)	/* Bit	10:	Repeater/DTE */
#define PHY_N_1000C_AFD		(1<<9)	/* Bit	9:	Advertise Full Duplex */
#define PHY_N_1000C_AHD		(1<<8)	/* Bit	8:	Advertise Half Duplex */
#define PHY_N_1000C_APC		(1<<7)	/* Bit	7:	Asymetric Pause Cap. */
					/* Bit	6..0:	reserved */

/***** PHY_NAT_1000T_STAT	16 bit ro	1000Base-T Status Reg *****/
#define PHY_N_1000S_MSF		(1<<15)	/* Bit	15:	Master/Slave Fault */
#define PHY_N_1000S_MSR		(1<<14)	/* Bit	14:	Master/Slave Result */
#define PHY_N_1000S_LRS		(1<<13)	/* Bit	13:	Local Receiver Status */
#define PHY_N_1000S_RRS		(1<<12)	/* Bit	12:	Remote Receiver Status*/
#define PHY_N_1000S_LP_FD	(1<<11)	/* Bit	11:	Link Partner can FD */
#define PHY_N_1000S_LP_HD	(1<<10)	/* Bit	10:	Link Partner can HD */
#define PHY_N_1000C_LP_APC	(1<<9)	/* Bit	9:	LP Asym. Pause Cap. */
					/* Bit	8:	reserved */
#define PHY_N_1000S_IEC		(255<<0)/* Bit	7..0:	Idle Error Count */

/*****	PHY_NAT_EXT_STAT	16 bit ro	Extended Status Register *****/
#define PHY_N_ES_X_FD_CAP	(1<<15)	/* Bit 15:	1000Base-X FD capable */
#define PHY_N_ES_X_HD_CAP	(1<<14)	/* Bit 14:	1000Base-X HD capable */
#define PHY_N_ES_T_FD_CAP	(1<<13)	/* Bit 13:	1000Base-T FD capable */
#define PHY_N_ES_T_HD_CAP	(1<<12)	/* Bit 12:	1000Base-T HD capable */
					/* Bit 11..0:	reserved */

/* todo: those are still missing */
/*****	PHY_NAT_EXT_CTRL1	16 bit ro	Extended Control Reg1 *****/
/*****	PHY_NAT_Q_STAT1		16 bit ro	Quick Status Reg1 *****/
/*****	PHY_NAT_10B_OP		16 bit ro	10Base-T Operations Reg *****/
/*****	PHY_NAT_EXT_CTRL2	16 bit ro	Extended Control Reg1 *****/
/*****	PHY_NAT_Q_STAT2		16 bit ro	Quick Status Reg2 *****/
/*****	PHY_NAT_PHY_ADDR	16 bit ro	PHY Address Register *****/

/* typedefs *******************************************************************/


/* function prototypes ********************************************************/

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* __INC_XMAC_H */
