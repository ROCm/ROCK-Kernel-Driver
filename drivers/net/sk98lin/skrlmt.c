/******************************************************************************
 *
 * Name:	skrlmt.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.49 $
 * Date:	$Date: 1999/11/22 13:38:02 $
 * Purpose:	Manage links on SK-NET Adapters, esp. redundant ones.
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
 *	$Log: skrlmt.c,v $
 *	Revision 1.49  1999/11/22 13:38:02  cgoos
 *	Changed license header to GPL.
 *	Added initialization to some variables to avoid compiler warnings.
 *	
 *	Revision 1.48  1999/10/04 14:01:17  rassmann
 *	Corrected reaction to reception of BPDU frames.
 *	Added parameter descriptions to "For Readme" section skrlmt.txt.
 *	Clarified usage of lookahead result *pForRlmt.
 *	Requested driver to present RLMT packets as soon as poosible.
 *	
 *	Revision 1.47  1999/07/20 12:53:36  rassmann
 *	Fixed documentation errors for lookahead macros.
 *	
 *	Revision 1.46  1999/05/28 13:29:16  rassmann
 *	Replaced C++-style comment.
 *	
 *	Revision 1.45  1999/05/28 13:28:08  rassmann
 *	Corrected syntax error (xxx).
 *	
 *	Revision 1.44  1999/05/28 11:15:54  rassmann
 *	Changed behaviour to reflect Design Spec v1.2.
 *	Controlling Link LED(s).
 *	Introduced RLMT Packet Version field in RLMT Packet.
 *	Newstyle lookahead macros (checking meta-information before looking at
 *	  the packet).
 *	
 *	Revision 1.43  1999/01/28 13:12:43  rassmann
 *	Corrected Lookahead (bug introduced in previous Rev.).
 *	
 *	Revision 1.42  1999/01/28 12:50:41  rassmann
 *	Not using broadcast time stamps in CheckLinkState mode.
 *	
 *	Revision 1.41  1999/01/27 14:13:02  rassmann
 *	Monitoring broadcast traffic.
 *	Switching more reliably and not too early if switch is
 *	 configured for spanning tree.
 *	
 *	Revision 1.40  1999/01/22 13:17:30  rassmann
 *	Informing PNMI of NET_UP.
 *	Clearing RLMT multicast addresses before first setting them.
 *	Reporting segmentation earlier, setting a "quiet time"
 *	 after a report.
 *	
 *	Revision 1.39  1998/12/10 15:29:53  rassmann
 *	Corrected SuspectStatus in SkRlmtBuildCheckChain().
 *	Corrected CHECK_SEG mode.
 *	
 *	Revision 1.38  1998/12/08 13:11:23  rassmann
 *	Stopping SegTimer at RlmtStop.
 *	
 *	Revision 1.37  1998/12/07 16:51:42  rassmann
 *	Corrected comments.
 *	
 *	Revision 1.36  1998/12/04 10:58:56  rassmann
 *	Setting next pointer to NULL when receiving.
 *	
 *	Revision 1.35  1998/12/03 16:12:42  rassmann
 *	Ignoring/correcting illegal PrefPort values.
 *	
 *	Revision 1.34  1998/12/01 11:45:35  rassmann
 *	Code cleanup.
 *	
 *	Revision 1.33  1998/12/01 10:29:32  rassmann
 *	Starting standby ports before getting the net up.
 *	Checking if a port is started when the link comes up.
 *	
 *	Revision 1.32  1998/11/30 16:19:50  rassmann
 *	New default for PortNoRx.
 *	
 *	Revision 1.31  1998/11/27 19:17:13  rassmann
 *	Corrected handling of LINK_DOWN coming shortly after LINK_UP.
 *	
 *	Revision 1.30  1998/11/24 12:37:31  rassmann
 *	Implemented segmentation check.
 *	
 *	Revision 1.29  1998/11/18 13:04:32  rassmann
 *	Secured PortUpTimer event.
 *	Waiting longer before starting standby port(s).
 *	
 *	Revision 1.28  1998/11/17 13:43:04  rassmann
 *	Handling (logical) tx failure.
 *	Sending packet on logical address after PORT_SWITCH.
 *	
 *	Revision 1.27  1998/11/13 17:09:50  rassmann
 *	Secured some events against being called in wrong state.
 *	
 *	Revision 1.26  1998/11/13 16:56:54  rassmann
 *	Added macro version of SkRlmtLookaheadPacket.
 *	
 *	Revision 1.25  1998/11/06 18:06:04  rassmann
 *	Corrected timing when RLMT checks fail.
 *	Clearing tx counter earlier in periodical checks.
 *	
 *	Revision 1.24  1998/11/05 10:37:27  rassmann
 *	Checking destination address in Lookahead.
 *	
 *	Revision 1.23  1998/11/03 13:53:49  rassmann
 *	RLMT should switch now (at least in mode 3).
 *	
 *	Revision 1.22  1998/10/29 14:34:49  rassmann
 *	Clearing SK_RLMT struct at startup.
 *	Initializing PortsUp during SK_RLMT_START.
 *	
 *	Revision 1.21  1998/10/28 11:30:17  rassmann
 *	Default mode is now SK_RLMT_CHECK_LOC_LINK.
 *	
 *	Revision 1.20  1998/10/26 16:02:03  rassmann
 *	Ignoring LINK_DOWN for links that are down.
 *	
 *	Revision 1.19  1998/10/22 15:54:01  rassmann
 *	Corrected EtherLen.
 *	Starting Link Check when second port comes up.
 *	
 *	Revision 1.18  1998/10/22 11:39:50  rassmann
 *	Corrected signed/unsigned mismatches.
 *	Corrected receive list handling and address recognition.
 *	
 *	Revision 1.17  1998/10/19 17:01:20  rassmann
 *	More detailed checking of received packets.
 *	
 *	Revision 1.16  1998/10/15 15:16:34  rassmann
 *	Finished Spanning Tree checking.
 *	Checked with lint.
 *	
 *	Revision 1.15  1998/09/24 19:16:07  rassmann
 *	Code cleanup.
 *	Introduced Timer for PORT_DOWN due to no RX.
 *	
 *	Revision 1.14  1998/09/18 20:27:14  rassmann
 *	Added address override.
 *	
 *	Revision 1.13  1998/09/16 11:31:48  rassmann
 *	Including skdrv1st.h again. :(
 *	
 *	Revision 1.12  1998/09/16 11:09:50  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.11  1998/09/15 12:32:03  rassmann
 *	Syntax correction.
 *	
 *	Revision 1.10  1998/09/15 11:28:49  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.9  1998/09/14 17:07:37  rassmann
 *	Added code for port checking via LAN.
 *	Changed Mbuf definition.
 *	
 *	Revision 1.8  1998/09/07 11:14:14  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.7  1998/09/07 09:06:07  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.6  1998/09/04 19:41:33  rassmann
 *	Syntax corrections.
 *	Started entering code for checking local links.
 *	
 *	Revision 1.5  1998/09/04 12:14:27  rassmann
 *	Interface cleanup.
 *	
 *	Revision 1.4  1998/09/02 16:55:28  rassmann
 *	Updated to reflect new DRV/HWAC/RLMT interface.
 *	
 *	Revision 1.3  1998/08/27 14:29:03  rassmann
 *	Code cleanup.
 *	
 *	Revision 1.2  1998/08/27 14:26:24  rassmann
 *	Updated interface.
 *	
 *	Revision 1.1  1998/08/21 08:26:49  rassmann
 *	First public version.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Description:
 *
 * This module contains code for Link ManagemenT (LMT) of SK-NET Adapters.
 * It is mainly intended for adapters with more than one link.
 * For such adapters, this module realizes Redundant Link ManagemenT (RLMT).
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#ifndef	lint
static const char SysKonnectFileId[] =
	"@(#) $Id: skrlmt.c,v 1.49 1999/11/22 13:38:02 cgoos Exp $ (C) SysKonnect.";
#endif	/* !defined(lint) */

#define __SKRLMT_C

#ifdef __cplusplus
xxxx	/* not supported yet - force error */
extern "C" {
#endif	/* cplusplus */

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

/* defines ********************************************************************/

#ifndef SK_HWAC_LINK_LED
#define SK_HWAC_LINK_LED(a,b,c,d)
#endif	/* !defined(SK_HWAC_LINK_LED) */

#ifndef DEBUG
#define RLMT_STATIC	static
#else	/* DEBUG */
#define RLMT_STATIC

#ifndef SK_LITTLE_ENDIAN
/* First 32 bits */
#define OFFS_LO32	1
/* Second 32 bits */
#define OFFS_HI32	0
#else	/* SK_LITTLE_ENDIAN */
/* First 32 bits */
#define OFFS_LO32	0
/* Second 32 bits */
#define OFFS_HI32	1
#endif	/* SK_LITTLE_ENDIAN */

#endif	/* DEBUG */

/* ----- Private timeout values ----- */

#define SK_RLMT_MIN_TO_VAL		   125000	/* 1/8 sec. */
#define SK_RLMT_DEF_TO_VAL		  1000000	/* 1 sec. */
#define SK_RLMT_PORTDOWN_TIM_VAL	   900000	/* another 0.9 sec. */
#define SK_RLMT_PORTSTART_TIM_VAL	   100000	/* 0.1 sec. */
#define SK_RLMT_PORTUP_TIM_VAL		  2500000	/* 2.5 sec. */
#define SK_RLMT_SEG_TO_VAL		900000000	/* 15 min. */

/*
 * Amount that a time stamp must be later to be recognized as "substantially
 * later". This is about 1/128 sec.
 */

#define SK_RLMT_BC_DELTA	((SK_TICKS_PER_SEC >> 7) + 1)

/* ----- Private RLMT defaults ----- */

#define SK_RLMT_DEF_PREF_PORT	0			/* "Lower" port. */
#define SK_RLMT_DEF_MODE 	SK_RLMT_CHECK_LINK	/* Default RLMT Mode. */

/* ----- Private RLMT checking states ----- */

#define SK_RLMT_RCS_SEG		1	/* RLMT Check State: check seg. */
#define SK_RLMT_RCS_START_SEG	2	/* RLMT Check State: start check seg. */
#define SK_RLMT_RCS_SEND_SEG	4	/* RLMT Check State: send BPDU packet */
#define SK_RLMT_RCS_REPORT_SEG	8	/* RLMT Check State: report seg. */

/* ----- Private PORT checking states ----- */

#define SK_RLMT_PCS_TX		1	/* Port Check State: check tx. */
#define SK_RLMT_PCS_RX		2	/* Port Check State: check rx. */

/* ----- Private PORT events ----- */

/* Note: Update simulation when changing these. */

#define SK_RLMT_PORTSTART_TIM	1100	/* Port start timeout. */
#define SK_RLMT_PORTUP_TIM	1101	/* Port can now go up. */
#define SK_RLMT_PORTDOWN_RX_TIM	1102	/* Port did not receive once ... */
#define SK_RLMT_PORTDOWN	1103	/* Port went down. */
#define SK_RLMT_PORTDOWN_TX_TIM	1104	/* Partner did not receive ... */

/* ----- Private RLMT events ----- */

/* Note: Update simulation when changing these. */

#define SK_RLMT_TIM		2100	/* RLMT timeout. */
#define SK_RLMT_SEG_TIM		2101	/* RLMT segmentation check timeout. */

#define TO_SHORTEN(tim)	((tim) / 2)

/* Error numbers and messages. */

#define SKERR_RLMT_E001		(SK_ERRBASE_RLMT + 0)
#define SKERR_RLMT_E001_MSG	"No Packet."
#define SKERR_RLMT_E002		(SKERR_RLMT_E001 + 1)
#define SKERR_RLMT_E002_MSG	"Short Packet."
#define SKERR_RLMT_E003		(SKERR_RLMT_E002 + 1)
#define SKERR_RLMT_E003_MSG	"Unknown RLMT event."
#define SKERR_RLMT_E004		(SKERR_RLMT_E003 + 1)
#define SKERR_RLMT_E004_MSG	"PortsUp incorrect."
#define SKERR_RLMT_E005		(SKERR_RLMT_E004 + 1)
#define SKERR_RLMT_E005_MSG	\
 "Net seems to be segmented (different root bridges are reported on the ports)."
#define SKERR_RLMT_E006		(SKERR_RLMT_E005 + 1)
#define SKERR_RLMT_E006_MSG	"Duplicate MAC Address detected."
#define SKERR_RLMT_E007		(SKERR_RLMT_E006 + 1)
#define SKERR_RLMT_E007_MSG	"LinksUp incorrect."
#define SKERR_RLMT_E008		(SKERR_RLMT_E007 + 1)
#define SKERR_RLMT_E008_MSG	"Port not started but link came up."
#define SKERR_RLMT_E009		(SKERR_RLMT_E008 + 1)
#define SKERR_RLMT_E009_MSG	"Corrected illegal setting of Preferred Port."
#define SKERR_RLMT_E010		(SKERR_RLMT_E009 + 1)
#define SKERR_RLMT_E010_MSG	"Ignored illegal Preferred Port."

/* LLC field values. */

#define LLC_COMMAND_RESPONSE_BIT	1
#define LLC_TEST_COMMAND		0xE3
#define LLC_UI				0x03

/* RLMT Packet fields. */

#define	SK_RLMT_DSAP			0
#define	SK_RLMT_SSAP			0
#define SK_RLMT_CTRL			(LLC_TEST_COMMAND)
#define SK_RLMT_INDICATOR0		0x53	/* S */
#define SK_RLMT_INDICATOR1		0x4B	/* K */
#define SK_RLMT_INDICATOR2		0x2D	/* - */
#define SK_RLMT_INDICATOR3		0x52	/* R */
#define SK_RLMT_INDICATOR4		0x4C	/* L */
#define SK_RLMT_INDICATOR5		0x4D	/* M */
#define SK_RLMT_INDICATOR6		0x54	/* T */
#define SK_RLMT_PACKET_VERSION	0

/* RLMT SPT Flag values. */

#define	SK_RLMT_SPT_FLAG_CHANGE		0x01
#define	SK_RLMT_SPT_FLAG_CHANGE_ACK	0x80

/* RLMT SPT Packet fields. */

#define	SK_RLMT_SPT_DSAP		0x42
#define	SK_RLMT_SPT_SSAP		0x42
#define SK_RLMT_SPT_CTRL		(LLC_UI)
#define	SK_RLMT_SPT_PROTOCOL_ID0	0x00
#define	SK_RLMT_SPT_PROTOCOL_ID1	0x00
#define	SK_RLMT_SPT_PROTOCOL_VERSION_ID	0x00
#define	SK_RLMT_SPT_BPDU_TYPE		0x00
#define	SK_RLMT_SPT_FLAGS		0x00	/* ?? */
#define	SK_RLMT_SPT_ROOT_ID0		0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_ROOT_ID1		0xFF	/* Lowest possible priority. */

/* Remaining 6 bytes will be the current port address. */

#define	SK_RLMT_SPT_ROOT_PATH_COST0	0x00
#define	SK_RLMT_SPT_ROOT_PATH_COST1	0x00
#define	SK_RLMT_SPT_ROOT_PATH_COST2	0x00
#define	SK_RLMT_SPT_ROOT_PATH_COST3	0x00
#define	SK_RLMT_SPT_BRIDGE_ID0		0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_BRIDGE_ID1		0xFF	/* Lowest possible priority. */

/* Remaining 6 bytes will be the current port address. */

#define	SK_RLMT_SPT_PORT_ID0		0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_PORT_ID1		0xFF	/* Lowest possible priority. */
#define	SK_RLMT_SPT_MSG_AGE0		0x00
#define	SK_RLMT_SPT_MSG_AGE1		0x00
#define	SK_RLMT_SPT_MAX_AGE0		0x00
#define	SK_RLMT_SPT_MAX_AGE1		0xFF
#define	SK_RLMT_SPT_HELLO_TIME0		0x00
#define	SK_RLMT_SPT_HELLO_TIME1		0xFF
#define	SK_RLMT_SPT_FWD_DELAY0		0x00
#define	SK_RLMT_SPT_FWD_DELAY1		0x40

/* Size defines. */

#define SK_RLMT_MIN_PACKET_SIZE	34
#define SK_RLMT_MAX_PACKET_SIZE	(SK_RLMT_MAX_TX_BUF_SIZE)
#define SK_PACKET_DATA_LEN	(SK_RLMT_MAX_PACKET_SIZE - \
				 SK_RLMT_MIN_PACKET_SIZE)

/* ----- RLMT packet types ----- */

#define SK_PACKET_ANNOUNCE	1	/* Port announcement. */
#define SK_PACKET_ALIVE		2	/* Alive packet to port. */
#define SK_PACKET_ADDR_CHANGED	3	/* Port address changed. */
#define SK_PACKET_CHECK_TX	4	/* Check your tx line. */

#ifdef SK_LITTLE_ENDIAN
#define SK_U16_TO_NETWORK_ORDER(Val,Addr) { \
	SK_U8	*_Addr = (SK_U8*)(Addr); \
	SK_U16	_Val = (SK_U16)(Val); \
	*_Addr++ = (SK_U8)(_Val >> 8); \
	*_Addr = (SK_U8)(_Val & 0xFF); \
}
#endif	/* SK_LITTLE_ENDIAN */

#ifdef SK_BIG_ENDIAN
#define SK_U16_TO_NETWORK_ORDER(Val,Addr) (*(SK_U16*)(Addr) = (SK_U16)(Val))
#endif	/* SK_BIG_ENDIAN */

#define AUTONEG_FAILED	SK_FALSE
#define AUTONEG_SUCCESS	SK_TRUE


/* typedefs *******************************************************************/

/* RLMT packet.  Length: SK_RLMT_MAX_PACKET_SIZE (60) bytes. */

typedef struct s_RlmtPacket {
	SK_U8	DstAddr[SK_MAC_ADDR_LEN];
	SK_U8	SrcAddr[SK_MAC_ADDR_LEN];
	SK_U8	TypeLen[2];
	SK_U8	DSap;
	SK_U8	SSap;
	SK_U8	Ctrl;
	SK_U8	Indicator[7];
	SK_U8	RlmtPacketType[2];
	SK_U8	Align1[2];
	SK_U8	Random[4];	/* Random value of requesting(!) station. */
	SK_U8	RlmtPacketVersion[2];	/* RLMT Packet version */
	SK_U8	Data[SK_PACKET_DATA_LEN];
} SK_RLMT_PACKET;

typedef struct s_SpTreeRlmtPacket {
	SK_U8	DstAddr[SK_MAC_ADDR_LEN];
	SK_U8	SrcAddr[SK_MAC_ADDR_LEN];
	SK_U8	TypeLen[2];
	SK_U8	DSap;
	SK_U8	SSap;
	SK_U8	Ctrl;
	SK_U8	ProtocolId[2];
	SK_U8	ProtocolVersionId;
	SK_U8	BpduType;
	SK_U8	Flags;
	SK_U8	RootId[8];
	SK_U8	RootPathCost[4];
	SK_U8	BridgeId[8];
	SK_U8	PortId[2];
	SK_U8	MessageAge[2];
	SK_U8	MaxAge[2];
	SK_U8	HelloTime[2];
	SK_U8	ForwardDelay[2];
} SK_SPTREE_PACKET;

/* global variables ***********************************************************/

SK_MAC_ADDR	SkRlmtMcAddr = {{0x01,  0x00,  0x5A,  0x52,  0x4C,  0x4D}};
SK_MAC_ADDR	BridgeMcAddr = {{0x01,  0x80,  0xC2,  0x00,  0x00,  0x00}};
SK_MAC_ADDR	BcAddr = {{0xFF,  0xFF,  0xFF,  0xFF,  0xFF,  0xFF}};

/* local variables ************************************************************/

/* None. */

/* functions ******************************************************************/

RLMT_STATIC void	SkRlmtCheckSwitch(
	SK_AC	*pAC,
	SK_IOC	IoC);
RLMT_STATIC void	SkRlmtCheckSeg(
	SK_AC	*pAC,
	SK_IOC	IoC);

/******************************************************************************
 *
 *	SkRlmtInit - initialize data, set state to init
 *
 * Description:
 *
 *	SK_INIT_DATA
 *	============
 *
 *	This routine initializes all RLMT-related variables to a known state.
 *	The initial state is SK_RLMT_RS_INIT.
 *	All ports are initialized to SK_RLMT_PS_INIT.
 *
 *
 *	SK_INIT_IO
 *	==========
 *
 *	Nothing.
 *
 *
 *	SK_INIT_RUN
 *	===========
 *
 *	Determine the adapter's random value.
 *	Set the hw registers, the "logical adapter address", the
 *	RLMT multicast address, and eventually the BPDU multicast address.
 *
 * Context:
 *	init, pageable
 *
 * Returns:
 *	Nothing.
 */
void	SkRlmtInit(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
int	Level)	/* initialization level */
{
	SK_U32		i, j;
	SK_U64		Random;
	SK_EVPARA	Para;

	SK_DBG_MSG(
		pAC,
		SK_DBGMOD_RLMT,
		SK_DBGCAT_INIT,
		("RLMT Init level %d.\n", Level))

	switch (Level) {
	case SK_INIT_DATA:	/* Initialize data structures. */
		SK_MEMSET((char *)&pAC->Rlmt, 0, sizeof(SK_RLMT));

		for (i = 0; i < SK_MAX_MACS; i++) {
			pAC->Rlmt.Port[i].PortState = SK_RLMT_PS_INIT;
			pAC->Rlmt.Port[i].LinkDown = SK_TRUE;
			pAC->Rlmt.Port[i].PortDown = SK_TRUE;
			pAC->Rlmt.Port[i].PortStarted = SK_FALSE;
			pAC->Rlmt.Port[i].PortNoRx = SK_FALSE;
			pAC->Rlmt.Port[i].RootIdSet = SK_FALSE;
		}

		pAC->Rlmt.RlmtState = SK_RLMT_RS_INIT;
		pAC->Rlmt.RootIdSet = SK_FALSE;
		pAC->Rlmt.MacPreferred = 0xFFFFFFFF;	  /* Automatic. */
		pAC->Rlmt.PrefPort = SK_RLMT_DEF_PREF_PORT;
		pAC->Rlmt.MacActive = pAC->Rlmt.PrefPort; /* Just assuming. */
		pAC->Rlmt.RlmtMode = SK_RLMT_DEF_MODE;
		pAC->Rlmt.TimeoutValue = SK_RLMT_DEF_TO_VAL;
		break;

	case SK_INIT_IO:	/* GIMacsFound first available here. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_INIT,
			("RLMT: %d MACs were detected.\n",
				pAC->GIni.GIMacsFound))

		/* Initialize HW registers? */

		if (pAC->GIni.GIMacsFound < 2) {
			Para.Para32[0] = SK_RLMT_CHECK_LINK;
			(void)SkRlmtEvent(pAC, IoC, SK_RLMT_MODE_CHANGE, Para);
		}
		break;

	case SK_INIT_RUN:
		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			Random = SkOsGetTime(pAC);
			*(SK_U32*)&pAC->Rlmt.Port[i].Random = *(SK_U32*)&Random;

			for (j = 0; j < 4; j++) {
				pAC->Rlmt.Port[i].Random[j] ^= pAC->Addr.Port[i
					].CurrentMacAddress.a[SK_MAC_ADDR_LEN -
					1 - j];
			}

			(void)SkAddrMcClear(
				pAC,
				IoC,
				i,
				SK_ADDR_PERMANENT | SK_MC_SW_ONLY);
			
			/* Add RLMT MC address. */

			(void)SkAddrMcAdd(
				pAC,
				IoC,
				i,
				&SkRlmtMcAddr,
				SK_ADDR_PERMANENT);

			if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) {
				/* Add BPDU MC address. */

				(void)SkAddrMcAdd(
					pAC,
					IoC,
					i,
					&BridgeMcAddr,
					SK_ADDR_PERMANENT);
			}

			(void)SkAddrMcUpdate(pAC, IoC, i);
		}
		break;

	default:	/* error */
		break;
	}

	return;

}	/* SkRlmtInit */


/******************************************************************************
 *
 *	SkRlmtBuildCheckChain - build the check chain
 *
 * Description:
 *	This routine builds the local check chain:
 *	- Each port that is up checks the next port.
 *	- The last port that is up checks the first port that is up.
 *
 * Notes:
 *	- Currently only local ports are considered when building the chain.
 *	- Currently the SuspectState is just reset;
 *	  it would be better to save it ...
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtBuildCheckChain(
SK_AC	*pAC)	/* adapter context */
{
	SK_U32	i;
	SK_U32	NumMacsUp;
	SK_U32	FirstMacUp=0;
	SK_U32	PrevMacUp=0;

	if (!(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LOC_LINK)) {
		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			pAC->Rlmt.Port[i].PortsChecked = 0;
		}
		return;	/* Nothing to build. */
	}

	SK_DBG_MSG(
		pAC,
		SK_DBGMOD_RLMT,
		SK_DBGCAT_CTRL,
		("SkRlmtBuildCheckChain.\n"))

	NumMacsUp = 0;

	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		pAC->Rlmt.Port[i].PortsChecked = 0;
		pAC->Rlmt.Port[i].PortsSuspect = 0;
		pAC->Rlmt.Port[i].CheckingState &=
			~(SK_RLMT_PCS_RX | SK_RLMT_PCS_TX);

		/*
		 * If more than two links are detected we should consider
		 * checking at least two other ports:
		 * 1. the next port that is not LinkDown and
		 * 2. the next port that is not PortDown.
		 */

		if (!pAC->Rlmt.Port[i].LinkDown) {
			if (NumMacsUp == 0) {
				FirstMacUp = i;
			}
			else {
				pAC->Rlmt.Port[PrevMacUp].PortCheck[
					pAC->Rlmt.Port[
					PrevMacUp].PortsChecked].CheckAddr =
					pAC->Addr.Port[i].CurrentMacAddress;
				pAC->Rlmt.Port[PrevMacUp].PortCheck[
					pAC->Rlmt.Port[
					PrevMacUp].PortsChecked].SuspectTx =
					SK_FALSE;
				pAC->Rlmt.Port[PrevMacUp].PortsChecked++;
			}
			PrevMacUp = i;
			NumMacsUp++;
		}
	}

	if (NumMacsUp > 1) {
		pAC->Rlmt.Port[PrevMacUp].PortCheck[pAC->Rlmt.Port[
			PrevMacUp].PortsChecked].CheckAddr =
			pAC->Addr.Port[FirstMacUp].CurrentMacAddress;
		pAC->Rlmt.Port[PrevMacUp].PortCheck[pAC->Rlmt.Port[
			PrevMacUp].PortsChecked].SuspectTx = SK_FALSE;
		pAC->Rlmt.Port[PrevMacUp].PortsChecked++;
	}

#ifdef DEBUG
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
                SK_DBG_MSG(
                        pAC,
                        SK_DBGMOD_RLMT,
                        SK_DBGCAT_CTRL,
                        ("Port %d checks %d other ports: %2X.\n",
				i,
				pAC->Rlmt.Port[i].PortsChecked,
				pAC->Rlmt.Port[i].PortCheck[0].CheckAddr.a[5]))
	}
#endif	/* DEBUG */

	return;       
}	/* SkRlmtBuildCheckChain */

#ifdef SK_RLMT_SLOW_LOOKAHEAD

/******************************************************************************
 *
 *	SkRlmtLookaheadPacket - examine received packet shortly, count s-th
 *
 * Description:
 *	This routine examines each received packet fast and short and
 *	increments some counters.
 *	The received packet has to be stored virtually contiguous.
 *	This function does the following:
 *	- Increment some counters.
 *	- Ensure length is sufficient.
 *	- Ensure that destination address is physical port address,
 *	  RLMT multicast, or BPDU multicast address.
 *
 * Notes:
 *	This function is fully reentrant while the fast path is not blocked.
 *
 * Context:
 *	isr/dpr, not pageable
 *
 * Returns:
 *	SK_FALSE packet for upper layers
 *	SK_TRUE  packet for RLMT
 */
SK_BOOL	SkRlmtLookaheadPacket(
SK_AC		*pAC,		/* adapter context */
SK_U32		PortIdx,	/* receiving port */
SK_U8		*pLaPacket,	/* received packet's data */
unsigned	PacketLength,	/* received packet's length */ /* Necessary? */
unsigned	LaLength)	/* lookahead length */
{
	int	i;
	SK_BOOL	IsBc;		/* Broadcast address? */
	int	RxDest;		/* Receive destination? */
	int	Offset;		/* Offset of data to present to LOOKAHEAD. */
	int	NumBytes;	/* #Bytes to present to LOOKAHEAD. */
	SK_RLMT_PACKET	*pRPacket;
	SK_ADDR_PORT	*pAPort;

#ifdef DEBUG
	PacketLength = PacketLength;
#endif	/* DEBUG */

	pRPacket = (SK_RLMT_PACKET*)pLaPacket;
	pAPort = &pAC->Addr.Port[PortIdx];

#ifdef DEBUG
	if (pLaPacket == NULL) {

		/* Create error log entry. */

		SK_ERR_LOG(
			pAC,
			SK_ERRCL_SW,
			SKERR_RLMT_E001,
			SKERR_RLMT_E001_MSG);

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_RX,
			("SkRlmtLookaheadPacket: NULL pointer.\n"))

		return (SK_FALSE);
	}
#endif	/* DEBUG */

	/* Drivers should get IsBc from the descriptor. */

	IsBc = SK_TRUE;
	for (i = 0; IsBc && i < SK_MAC_ADDR_LEN; i++) {
		IsBc = IsBc && (pLaPacket[i] == 0xFF);
	}

	SK_RLMT_PRE_LOOKAHEAD(
		pAC,
		PortIdx,
		PacketLength,
		IsBc,
		&Offset,
		&NumBytes)

	if (NumBytes == 0) {
		return (SK_FALSE);
	}

	SK_RLMT_LOOKAHEAD(
		pAC,
		PortIdx,
		&pLaPacket[Offset],
		IsBc,
		pLaPacket[0] & 1,	/* Drivers: Get info from descriptor. */
		&RxDest)

	if (RxDest & SK_RLMT_RX_RLMT) {
		return (SK_TRUE);
	}

	return (SK_FALSE);
}	/* SkRlmtLookaheadPacket */

#endif	/* SK_RLMT_SLOW_LOOKAHEAD */

/******************************************************************************
 *
 *	SkRlmtBuildPacket - build an RLMT packet
 *
 * Description:
 *	This routine sets up an RLMT packet.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	NULL or pointer to RLMT mbuf
 */
RLMT_STATIC SK_MBUF	*SkRlmtBuildPacket(
SK_AC		*pAC,		/* adapter context */
SK_IOC		IoC,		/* I/O context */
SK_U32		PortIdx,	/* sending port */
SK_U16		PacketType,	/* RLMT packet type */
SK_MAC_ADDR	*SrcAddr,	/* source address */
SK_MAC_ADDR	*DestAddr)	/* destination address */
{
	int		i;
	SK_U16		Length;
	SK_MBUF		*pMb;
	SK_RLMT_PACKET	*pPacket;

	if ((pMb = SkDrvAllocRlmtMbuf(pAC, IoC, SK_RLMT_MAX_PACKET_SIZE)) !=
		NULL) {
		pPacket = (SK_RLMT_PACKET*)pMb->pData;
		for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
			pPacket->DstAddr[i] = DestAddr->a[i];
			pPacket->SrcAddr[i] = SrcAddr->a[i];
		}
		pPacket->DSap = SK_RLMT_DSAP;
		pPacket->SSap = SK_RLMT_SSAP;
		pPacket->Ctrl = SK_RLMT_CTRL;
		pPacket->Indicator[0] = SK_RLMT_INDICATOR0;
		pPacket->Indicator[1] = SK_RLMT_INDICATOR1;
		pPacket->Indicator[2] = SK_RLMT_INDICATOR2;
		pPacket->Indicator[3] = SK_RLMT_INDICATOR3;
		pPacket->Indicator[4] = SK_RLMT_INDICATOR4;
		pPacket->Indicator[5] = SK_RLMT_INDICATOR5;
		pPacket->Indicator[6] = SK_RLMT_INDICATOR6;

		SK_U16_TO_NETWORK_ORDER(
			PacketType,
			&pPacket->RlmtPacketType[0]);

		for (i = 0; i < 4; i++) {
			pPacket->Random[i] = pAC->Rlmt.Port[PortIdx].Random[i];
		}
		
		SK_U16_TO_NETWORK_ORDER(
			SK_RLMT_PACKET_VERSION,
			&pPacket->RlmtPacketVersion[0]);

		for (i = 0; i < SK_PACKET_DATA_LEN; i++) {
			pPacket->Data[i] = 0x00;
		}

		Length = SK_RLMT_MAX_PACKET_SIZE;	/* Or smaller. */
		pMb->Length = Length;
		pMb->PortIdx = PortIdx;
		Length -= 14;
		SK_U16_TO_NETWORK_ORDER(Length, &pPacket->TypeLen[0]);

		if (PacketType == SK_PACKET_ALIVE) {
			pAC->Rlmt.Port[PortIdx].TxHelloCts++;
		}
	}

	return (pMb);       
}	/* SkRlmtBuildPacket */


/******************************************************************************
 *
 *	SkRlmtBuildSpanningTreePacket - build spanning tree check packet
 *
 * Description:
 *	This routine sets up a BPDU packet for spanning tree check.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	NULL or pointer to RLMT mbuf
 */
RLMT_STATIC SK_MBUF	*SkRlmtBuildSpanningTreePacket(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortIdx)	/* sending port */
{
	unsigned		i;
	SK_U16			Length;
	SK_MBUF			*pMb;
	SK_SPTREE_PACKET	*pSPacket;

	if ((pMb = SkDrvAllocRlmtMbuf(pAC, IoC, SK_RLMT_MAX_PACKET_SIZE)) !=
		NULL) {
		pSPacket = (SK_SPTREE_PACKET*)pMb->pData;
		for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
			pSPacket->DstAddr[i] = BridgeMcAddr.a[i];
			pSPacket->SrcAddr[i] =
				pAC->Addr.Port[PortIdx].CurrentMacAddress.a[i];
		}
		pSPacket->DSap = SK_RLMT_SPT_DSAP;
		pSPacket->SSap = SK_RLMT_SPT_SSAP;
		pSPacket->Ctrl = SK_RLMT_SPT_CTRL;

		pSPacket->ProtocolId[0] = SK_RLMT_SPT_PROTOCOL_ID0;
		pSPacket->ProtocolId[1] = SK_RLMT_SPT_PROTOCOL_ID1;
		pSPacket->ProtocolVersionId = SK_RLMT_SPT_PROTOCOL_VERSION_ID;
		pSPacket->BpduType = SK_RLMT_SPT_BPDU_TYPE;
		pSPacket->Flags = SK_RLMT_SPT_FLAGS;
		pSPacket->RootId[0] = SK_RLMT_SPT_ROOT_ID0;
		pSPacket->RootId[1] = SK_RLMT_SPT_ROOT_ID1;
		pSPacket->RootPathCost[0] = SK_RLMT_SPT_ROOT_PATH_COST0;
		pSPacket->RootPathCost[1] = SK_RLMT_SPT_ROOT_PATH_COST1;
		pSPacket->RootPathCost[2] = SK_RLMT_SPT_ROOT_PATH_COST2;
		pSPacket->RootPathCost[3] = SK_RLMT_SPT_ROOT_PATH_COST3;
		pSPacket->BridgeId[0] = SK_RLMT_SPT_BRIDGE_ID0;
		pSPacket->BridgeId[1] = SK_RLMT_SPT_BRIDGE_ID1;

		/*
		 * Use virtual address as bridge ID and filter these packets
		 * on receive.
		 */

		for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
			pSPacket->BridgeId[i + 2] = pSPacket->RootId[i + 2] =
				pAC->Addr.CurrentMacAddress.a[i];
		}
		pSPacket->PortId[0] = SK_RLMT_SPT_PORT_ID0;
		pSPacket->PortId[1] = SK_RLMT_SPT_PORT_ID1;
		pSPacket->MessageAge[0] = SK_RLMT_SPT_MSG_AGE0;
		pSPacket->MessageAge[1] = SK_RLMT_SPT_MSG_AGE1;
		pSPacket->MaxAge[0] = SK_RLMT_SPT_MAX_AGE0;
		pSPacket->MaxAge[1] = SK_RLMT_SPT_MAX_AGE1;
		pSPacket->HelloTime[0] = SK_RLMT_SPT_HELLO_TIME0;
		pSPacket->HelloTime[1] = SK_RLMT_SPT_HELLO_TIME1;
		pSPacket->ForwardDelay[0] = SK_RLMT_SPT_FWD_DELAY0;
		pSPacket->ForwardDelay[1] = SK_RLMT_SPT_FWD_DELAY1;

		Length = SK_RLMT_MAX_PACKET_SIZE;	/* Or smaller. */
		pMb->Length = Length;
		pMb->PortIdx = PortIdx;
		Length -= 14;
		SK_U16_TO_NETWORK_ORDER(Length, &pSPacket->TypeLen[0]);

		pAC->Rlmt.Port[PortIdx].TxSpHelloReqCts++;
	}

	return (pMb);       
}	/* SkRlmtBuildSpanningTreePacket */


/******************************************************************************
 *
 *	SkRlmtSend - build and send check packets
 *
 * Description:
 *	Depending on the RLMT state and the checking state, several packets
 *	are sent through the indicated port.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtSend(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
SK_U32	PortIdx)
{
	unsigned	j;
	SK_EVPARA	Para;
	SK_RLMT_PORT	*pRPort;

	pRPort = &pAC->Rlmt.Port[PortIdx];
	if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LOC_LINK) {
		if (pRPort->CheckingState &
			(SK_RLMT_PCS_TX | SK_RLMT_PCS_RX)) {

			/*
			 * Port is suspicious. Send the RLMT packet to the
			 * RLMT multicast address.
			 */

			if ((Para.pParaPtr = SkRlmtBuildPacket(
				pAC,
				IoC,
				PortIdx,
				SK_PACKET_ALIVE,
				&pAC->Addr.Port[PortIdx].CurrentMacAddress,
				&SkRlmtMcAddr)) != NULL) {
				SkEventQueue(
					pAC,
					SKGE_DRV,
					SK_DRV_RLMT_SEND,
					Para);
			}
		}
		else {
			/*
			 * Send a directed RLMT packet to all ports that are
			 * checked by the indicated port.
			 */

			for (j = 0; j < pRPort->PortsChecked; j++) {
				if ((Para.pParaPtr =
					SkRlmtBuildPacket(
						pAC,
						IoC,
						PortIdx,
						SK_PACKET_ALIVE,
						&pAC->Addr.Port[PortIdx].CurrentMacAddress,
						&pRPort->PortCheck[j].CheckAddr)
						) != NULL) {
					SkEventQueue(
						pAC,
						SKGE_DRV,
						SK_DRV_RLMT_SEND,
						Para);
				}
			}
		}
	}

	if ((pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) &&
		(pAC->Rlmt.CheckingState & SK_RLMT_RCS_SEND_SEG)) {

		/*
		 * Send a BPDU packet to make a connected switch tell us
		 * the correct root bridge.
		 */

		if ((Para.pParaPtr =
			SkRlmtBuildSpanningTreePacket(
				pAC,
				IoC,
				PortIdx)
			) != NULL) {
			
			pAC->Rlmt.CheckingState &= ~SK_RLMT_RCS_SEND_SEG;
			pRPort->RootIdSet = SK_FALSE;

			SkEventQueue(
				pAC,
				SKGE_DRV,
				SK_DRV_RLMT_SEND,
				Para);
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_TX,
				("SkRlmtSend: BPDU Packet on Port %u.\n",
					PortIdx))
		}
	}

	return;

}	/* SkRlmtSend */


/******************************************************************************
 *
 *	SkRlmtPortReceives - check if port is (going) down and bring it up
 *
 * Description:
 *	This routine checks if a port who received a non-BPDU packet
 *	needs to go up or needs to be stopped going down.
 *
* Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtPortReceives(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortIdx)	/* port to check */
{
	SK_RLMT_PORT	*pRPort;
	SK_EVPARA	Para;

	pRPort = &pAC->Rlmt.Port[PortIdx];
	pRPort->PortNoRx = SK_FALSE;

	if ((pRPort->PortState == SK_RLMT_PS_DOWN) &&
		!(pRPort->CheckingState & SK_RLMT_PCS_TX)) {

		/*
		 * Port is marked down (rx), but received a non-BPDU packet.
		 * Bring it up.
		 */

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Received on PortDown.\n"))

		pRPort->PortState = SK_RLMT_PS_GOING_UP;
		pRPort->GuTimeStamp = SkOsGetTime(pAC);
		Para.Para32[0] = PortIdx;
		SkTimerStart(
			pAC,
			IoC,
			&pRPort->UpTimer,
			SK_RLMT_PORTUP_TIM_VAL,
			SKGE_RLMT,
			SK_RLMT_PORTUP_TIM,
			Para);
		SkRlmtCheckSwitch(pAC, IoC);
	}
	else if (pRPort->CheckingState & SK_RLMT_PCS_RX) {
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Stop bringing port down.\n"))
		SkTimerStop(pAC, IoC, &pRPort->DownRxTimer);
		SkRlmtCheckSwitch(pAC, IoC);
	}

	pRPort->CheckingState &= ~SK_RLMT_PCS_RX;
	return;
}	/* SkRlmtPortReceives */


/******************************************************************************
 *
 *	SkRlmtPacketReceive - receive a packet for closer examination
 *
 * Description:
 *	This routine examines a packet more closely than SkRlmtLookahead*().
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtPacketReceive(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
SK_MBUF	*pMb)	/* received packet */
{
#ifdef xDEBUG
	extern	void DumpData(char *p, int size);
#endif	/* DEBUG */
	int			i;
	unsigned		j;
	SK_U16			PacketType;
	SK_U32			PortIdx;
	SK_ADDR_PORT		*pAPort;
	SK_RLMT_PORT		*pRPort;
	SK_RLMT_PACKET		*pRPacket;
	SK_SPTREE_PACKET	*pSPacket;
	SK_EVPARA		Para;

	PortIdx	= pMb->PortIdx;
	pAPort = &pAC->Addr.Port[PortIdx];
	pRPort = &pAC->Rlmt.Port[PortIdx];

	SK_DBG_MSG(
		pAC,
		SK_DBGMOD_RLMT,
		SK_DBGCAT_RX,
		("SkRlmtPacketReceive: PortIdx == %d.\n", PortIdx))

	pRPacket = (SK_RLMT_PACKET*)pMb->pData;
	pSPacket = (SK_SPTREE_PACKET*)pRPacket;

#ifdef xDEBUG
	DumpData((char *)pRPacket, 32);
#endif	/* DEBUG */

	if ((pRPort->PacketsPerTimeSlot - pRPort->BpduPacketsPerTimeSlot) != 0) {
		SkRlmtPortReceives(pAC, IoC, PortIdx);
	}
	
	/* Check destination address. */

	if (!SK_ADDR_EQUAL(pAPort->CurrentMacAddress.a, pRPacket->DstAddr) &&
		!SK_ADDR_EQUAL(SkRlmtMcAddr.a, pRPacket->DstAddr) &&
		!SK_ADDR_EQUAL(BridgeMcAddr.a, pRPacket->DstAddr)) {

		/*
		 * Not sent to current MAC or registered MC address
		 * => Trash it.
		 */

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Not for me.\n"))

		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
		return;
	}
	else if (SK_ADDR_EQUAL(pAPort->CurrentMacAddress.a, pRPacket->SrcAddr)) {

		/*
		 * Was sent by same port (may happen during port switching
		 * or in case of duplicate MAC addresses).
		 */

		/*
		 * Check for duplicate address here:
		 * If Packet.Random != My.Random => DupAddr.
		 */

		for (i = 3; i >= 0; i--) {
			if (pRPort->Random[i] !=
				pRPacket->Random[i]) {
				break;
			}
		}

		/*
		 * CAUTION: Do not check for duplicate MAC
		 * address in RLMT Alive Reply packets.
		 */

		if (i >= 0 && pRPacket->DSap == SK_RLMT_DSAP &&
			pRPacket->Ctrl == SK_RLMT_CTRL &&
			pRPacket->SSap == SK_RLMT_SSAP &&
			pRPacket->Indicator[0] == SK_RLMT_INDICATOR0 &&
			pRPacket->Indicator[1] == SK_RLMT_INDICATOR1 &&
			pRPacket->Indicator[2] == SK_RLMT_INDICATOR2 &&
			pRPacket->Indicator[3] == SK_RLMT_INDICATOR3 &&
			pRPacket->Indicator[4] == SK_RLMT_INDICATOR4 &&
			pRPacket->Indicator[5] == SK_RLMT_INDICATOR5 &&
			pRPacket->Indicator[6] == SK_RLMT_INDICATOR6) {
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Duplicate MAC Address.\n"))

			/* Error Log entry. */

			SK_ERR_LOG(
				pAC,
				SK_ERRCL_COMM,
				SKERR_RLMT_E006,
				SKERR_RLMT_E006_MSG);
		}
		else {
			/* Simply trash it. */

			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Sent by me.\n"))
		}

		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);

		return;
	}

	/* Check SuspectTx entries. */

	if (pRPort->PortsSuspect > 0) {
		for (j = 0; j < pRPort->PortsChecked; j++) {
			if (pRPort->PortCheck[j].SuspectTx &&
				SK_ADDR_EQUAL(
					pRPacket->SrcAddr,
					pRPort->PortCheck[j].CheckAddr.a)) {
				pRPort->PortCheck[j].SuspectTx = SK_FALSE;
				pRPort->PortsSuspect--;
				break;
			}
		}
	}

	/* Determine type of packet. */

	if (pRPacket->DSap == SK_RLMT_DSAP &&
		pRPacket->Ctrl == SK_RLMT_CTRL &&
		(pRPacket->SSap & ~LLC_COMMAND_RESPONSE_BIT) == SK_RLMT_SSAP &&
		pRPacket->Indicator[0] == SK_RLMT_INDICATOR0 &&
		pRPacket->Indicator[1] == SK_RLMT_INDICATOR1 &&
		pRPacket->Indicator[2] == SK_RLMT_INDICATOR2 &&
		pRPacket->Indicator[3] == SK_RLMT_INDICATOR3 &&
		pRPacket->Indicator[4] == SK_RLMT_INDICATOR4 &&
		pRPacket->Indicator[5] == SK_RLMT_INDICATOR5 &&
		pRPacket->Indicator[6] == SK_RLMT_INDICATOR6) {

		/* It's an RLMT packet. */

		PacketType = (SK_U16)((pRPacket->RlmtPacketType[0] << 8) |
			pRPacket->RlmtPacketType[1]);

		switch (PacketType) {
		case SK_PACKET_ANNOUNCE:	/* Not yet used. */
#if 0
			/* Build the check chain. */

			SkRlmtBuildCheckChain(pAC);
#endif	/* 0 */

			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Announce.\n"))

			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
			break;

		case SK_PACKET_ALIVE:
			if (pRPacket->SSap & LLC_COMMAND_RESPONSE_BIT) {
				SK_DBG_MSG(
					pAC,
					SK_DBGMOD_RLMT,
					SK_DBGCAT_RX,
					("SkRlmtPacketReceive: Alive Reply.\n"))

				/* Alive Reply Packet. */

#if 0
				pRPort->RlmtAcksPerTimeSlot++;
#endif	/* 0 */

				if (!(pAC->Addr.Port[PortIdx].PromMode &
					SK_PROM_MODE_LLC) ||
					SK_ADDR_EQUAL(
						pRPacket->DstAddr,
						pAPort->CurrentMacAddress.a)) {

					/* Obviously we could send something. */

					if (pRPort->CheckingState &
						SK_RLMT_PCS_TX) {
						pRPort->CheckingState &=
							~SK_RLMT_PCS_TX;
						SkTimerStop(
							pAC,
							IoC,
							&pRPort->DownTxTimer);
					}

					if ((pRPort->PortState ==
						SK_RLMT_PS_DOWN) &&
						!(pRPort->CheckingState &
						SK_RLMT_PCS_RX)) {
						pRPort->PortState =
							SK_RLMT_PS_GOING_UP;
						pRPort->GuTimeStamp =
							SkOsGetTime(pAC);

						SkTimerStop(
							pAC,
							IoC,
							&pRPort->DownTxTimer);

						Para.Para32[0] = PortIdx;
						SkTimerStart(
							pAC,
							IoC,
							&pRPort->UpTimer,
							SK_RLMT_PORTUP_TIM_VAL,
							SKGE_RLMT,
							SK_RLMT_PORTUP_TIM,
							Para);
					}
				}

				/* Mark sending port as alive? */

				SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
			}
			else {	/* Alive Request Packet. */
				SK_DBG_MSG(
					pAC,
					SK_DBGMOD_RLMT,
					SK_DBGCAT_RX,
					("SkRlmtPacketReceive: Alive Request.\n"))

				pRPort->RxHelloCts++;
#if 0
				pRPort->RlmtChksPerTimeSlot++;
#endif	/* 0 */

				/* Answer. */

				for (i = 0; i < SK_MAC_ADDR_LEN; i++) {
					pRPacket->DstAddr[i] =
						pRPacket->SrcAddr[i];
					pRPacket->SrcAddr[i] = pAC->Addr.Port[
						PortIdx].CurrentMacAddress.a[i];
				}
				pRPacket->SSap |= LLC_COMMAND_RESPONSE_BIT;

				Para.pParaPtr = pMb;
				SkEventQueue(
					pAC,
					SKGE_DRV,
					SK_DRV_RLMT_SEND,
					Para);
			}
			break;

		case SK_PACKET_CHECK_TX:
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Check your tx line.\n"))

			/*
			 * A port checking us requests us to check our tx line.
			 */

			pRPort->CheckingState |= SK_RLMT_PCS_TX;

			/* Start PortDownTx timer. */

			Para.Para32[0] = PortIdx;
			SkTimerStart(
				pAC,
				IoC,
				&pRPort->DownTxTimer,
				SK_RLMT_PORTDOWN_TIM_VAL,
				SKGE_RLMT,
				SK_RLMT_PORTDOWN_TX_TIM,
				Para);

			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);

			if ((Para.pParaPtr = SkRlmtBuildPacket(
				pAC,
				IoC,
				PortIdx,
				SK_PACKET_ALIVE,
				&pAC->Addr.Port[PortIdx].CurrentMacAddress,
				&SkRlmtMcAddr)) != NULL) {
				SkEventQueue(
					pAC,
					SKGE_DRV,
					SK_DRV_RLMT_SEND,
					Para);
			}
			break;

		case SK_PACKET_ADDR_CHANGED:
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Address Change.\n"))

			/* Build the check chain. */

			SkRlmtBuildCheckChain(pAC);
			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
			break;

		default:
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_RX,
				("SkRlmtPacketReceive: Unknown RLMT packet.\n"))

			/* RA;:;: ??? */

			SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
		}
	}
	else if (pSPacket->DSap == SK_RLMT_SPT_DSAP &&
		pSPacket->Ctrl == SK_RLMT_SPT_CTRL &&
		(pSPacket->SSap & ~LLC_COMMAND_RESPONSE_BIT) ==
		SK_RLMT_SPT_SSAP) {
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_RX,
			("SkRlmtPacketReceive: BPDU Packet.\n"))

		/* Spanning Tree packet. */

		pRPort->RxSpHelloCts++;

		if (!SK_ADDR_EQUAL(
			&pSPacket->RootId[2],
			&pAC->Addr.CurrentMacAddress.a[0])) {

			/*
			 * Check segmentation if a new root bridge is set and
			 * the segmentation check is not currently running.
			 */

			if (!SK_ADDR_EQUAL(
					&pSPacket->RootId[2],
					&pRPort->Root.Id[2]) &&
				(pAC->Rlmt.LinksUp > 1) &&
				(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) &&
				!(pAC->Rlmt.CheckingState & SK_RLMT_RCS_SEG)) {
				pAC->Rlmt.CheckingState |=
					SK_RLMT_RCS_START_SEG |
					SK_RLMT_RCS_SEND_SEG;
			}

			/* Store tree view of this port. */

			for (i = 0; i < 8; i++) {
				pRPort->Root.Id[i] = pSPacket->RootId[i];
			}
			pRPort->RootIdSet = SK_TRUE;
		}

		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);

		if (pAC->Rlmt.CheckingState & SK_RLMT_RCS_REPORT_SEG) {
			SkRlmtCheckSeg(pAC, IoC);
		}
	}
	else {
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_RX,
			("SkRlmtPacketReceive: Unknown Packet Type.\n"))

		/* Unknown packet. */

		SkDrvFreeRlmtMbuf(pAC, IoC, pMb);
	}
	return;
}	/* SkRlmtPacketReceive */


/******************************************************************************
 *
 *	SkRlmtCheckPort - check if a port works
 *
 * Description:
 *	This routine checks if a port whose link is up received something
 *	and if it seems to transmit successfully.
 *
 *	# PortState: PsInit, PsLinkDown, PsDown, PsGoingUp, PsUp
 *	# PortCheckingState (Bitfield): ChkTx, ChkRx, ChkSeg
 *	# RlmtCheckingState (Bitfield): ChkSeg, StartChkSeg, ReportSeg
 *
 *	if (Rx - RxBpdu == 0) {	# No rx.
 *		if (state == PsUp) {
 *			PortCheckingState |= ChkRx
 *		}
 *		if (ModeCheckSeg && (Timeout ==
 *			TO_SHORTEN(RLMT_DEFAULT_TIMEOUT))) {
 *			RlmtCheckingState |= ChkSeg)
 *			PortCheckingState |= ChkSeg
 *		}
 *		NewTimeout = TO_SHORTEN(Timeout)
 *		if (NewTimeout < RLMT_MIN_TIMEOUT) {
 *			NewTimeout = RLMT_MIN_TIMEOUT
 *			PortState = PsDown
 *			...
 *		}
 *	}
 *	else {	# something was received
 *		# Set counter to 0 at LinkDown?
 *		#   No - rx may be reported after LinkDown ???
 *		PortCheckingState &= ~ChkRx
 *		NewTimeout = RLMT_DEFAULT_TIMEOUT
 *		if (RxAck == 0) {
 *			possible reasons:
 *			is my tx line bad? --
 *				send RLMT multicast and report
 *				back internally? (only possible
 *				between ports on same adapter)
 *		}
 *		if (RxChk == 0) {
 *			possible reasons:
 *			- tx line of port set to check me
 *			  maybe bad
 *			- no other port/adapter available or set
 *			  to check me
 *			- adapter checking me has a longer
 *			  timeout
 *			??? anything that can be done here?
 *		}
 *	}
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	New timeout value.
 */
RLMT_STATIC SK_U32	SkRlmtCheckPort(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortIdx)	/* port to check */
{
	unsigned	i;
	SK_U32		NewTimeout;
	SK_RLMT_PORT	*pRPort;
	SK_EVPARA	Para;

	pRPort = &pAC->Rlmt.Port[PortIdx];

	if ((pRPort->PacketsPerTimeSlot - pRPort->BpduPacketsPerTimeSlot) == 0) {
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SkRlmtCheckPort %d: No (%d) receives in last time slot.\n",
				PortIdx,
				pRPort->PacketsPerTimeSlot))

		/*
		 * Check segmentation if there was no receive at least twice
		 * in a row (PortNoRx is already set) and the segmentation
		 * check is not currently running.
		 */

		if (pRPort->PortNoRx && (pAC->Rlmt.LinksUp > 1) &&
			(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) &&
			!(pAC->Rlmt.CheckingState & SK_RLMT_RCS_SEG)) {
			pAC->Rlmt.CheckingState |=
				SK_RLMT_RCS_START_SEG | SK_RLMT_RCS_SEND_SEG;
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SkRlmtCheckPort: PortsSuspect %d, PcsRx %d.\n",
				pRPort->PortsSuspect,
				pRPort->CheckingState & SK_RLMT_PCS_RX))

		if (pRPort->PortState != SK_RLMT_PS_DOWN) {
			NewTimeout = TO_SHORTEN(pAC->Rlmt.TimeoutValue);
			if (NewTimeout < SK_RLMT_MIN_TO_VAL) {
				NewTimeout = SK_RLMT_MIN_TO_VAL;
			}

			if (!(pRPort->CheckingState & SK_RLMT_PCS_RX)) {
				Para.Para32[0] = PortIdx;
				pRPort->CheckingState |= SK_RLMT_PCS_RX;

				/*
				 * What shall we do if the port checked
				 * by this one receives our request
				 * frames?  What's bad - our rx line
				 * or his tx line?
				 */

				SkTimerStart(
					pAC,
					IoC,
					&pRPort->DownRxTimer,
					SK_RLMT_PORTDOWN_TIM_VAL,
					SKGE_RLMT,
					SK_RLMT_PORTDOWN_RX_TIM,
					Para);

				for (i = 0; i < pRPort->PortsChecked; i++) {
					if (pRPort->PortCheck[i].SuspectTx) {
						continue;
					}
					pRPort->PortCheck[i].SuspectTx = SK_TRUE;
					pRPort->PortsSuspect++;
					if ((Para.pParaPtr =
						SkRlmtBuildPacket(
							pAC,
							IoC,
							PortIdx,
							SK_PACKET_CHECK_TX,
							&pAC->Addr.Port[PortIdx].CurrentMacAddress,
							&pRPort->PortCheck[i].CheckAddr)
							) != NULL) {
						SkEventQueue(
							pAC,
							SKGE_DRV,
							SK_DRV_RLMT_SEND,
							Para);
					}
				}
			}
		}
		else {	/* PortDown -- or all partners suspect. */
			NewTimeout = SK_RLMT_DEF_TO_VAL;
		}
		pRPort->PortNoRx = SK_TRUE;
	}
	else {	/* A non-BPDU packet was received. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SkRlmtCheckPort %d: %d (%d) receives in last time slot.\n",
				PortIdx,
                                pRPort->PacketsPerTimeSlot -
					pRPort->BpduPacketsPerTimeSlot,
				pRPort->PacketsPerTimeSlot))
		
		SkRlmtPortReceives(pAC, IoC, PortIdx);
		NewTimeout = SK_RLMT_DEF_TO_VAL;              
	}

	return (NewTimeout);       
}	/* SkRlmtCheckPort */


/******************************************************************************
 *
 *	SkRlmtSelectBcRx - select new active port, criteria 1 (CLP)
 *
 * Description:
 *	This routine selects the port that received a broadcast frame
 *	substantially later than all other ports.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectBcRx(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
SK_U32	A,	/* active port */
SK_U32	P,	/* preferred port */
SK_U32	*N)	/* new active port */
{
	SK_U64		BcTimeStamp;
	SK_U32		i;
	SK_BOOL		PortFound;

        BcTimeStamp = 0;	/* Not totally necessary, but feeling better. */
	PortFound = SK_FALSE;
	
	/* Select port with the latest TimeStamp. */

	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("TimeStamp Port %d: %.08x%.08x.\n",
			i,
			*((SK_U32*)(&pAC->Rlmt.Port[i].BcTimeStamp) + OFFS_HI32),
			*((SK_U32*)(&pAC->Rlmt.Port[i].BcTimeStamp) + OFFS_LO32)))
		if (!pAC->Rlmt.Port[i].PortDown &&
			!pAC->Rlmt.Port[i].PortNoRx) {
			if (!PortFound ||
				pAC->Rlmt.Port[i].BcTimeStamp > BcTimeStamp) {
				BcTimeStamp = pAC->Rlmt.Port[i].BcTimeStamp;
				*N = i;
				PortFound = SK_TRUE;
			}
		}
	}

	if (PortFound) {
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("Port %d received the last broadcast.\n", *N))

		/* Look if another port's time stamp is similar. */

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			if (i == *N) {
				continue;
			}
			if (!pAC->Rlmt.Port[i].PortDown &&
				!pAC->Rlmt.Port[i].PortNoRx &&
				(pAC->Rlmt.Port[i].BcTimeStamp >
				BcTimeStamp - SK_RLMT_BC_DELTA ||
				pAC->Rlmt.Port[i].BcTimeStamp
				+ SK_RLMT_BC_DELTA > BcTimeStamp)) {
				PortFound = SK_FALSE;
				SK_DBG_MSG(
					pAC,
					SK_DBGMOD_RLMT,
					SK_DBGCAT_CTRL,
					("Port %d received a broadcast %s.\n",
					i,
					"at a similar time"))
				break;
			}
		}
	}

#ifdef DEBUG
	if (PortFound) {
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_CHECK_SWITCH found Port %d receiving %s.\n",
				*N,
				"the substantially latest broadcast"))
	}
#endif	/* DEBUG */

	return (PortFound);
}	/* SkRlmtSelectBcRx */


/******************************************************************************
 *
 *	SkRlmtSelectNotSuspect - select new active port, criteria 2 (CLP)
 *
 * Description:
 *	This routine selects a good port (it is PortUp && !SuspectRx).
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectNotSuspect(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
SK_U32	A,	/* active port */
SK_U32	P,	/* preferred port */
SK_U32	*N)	/* new active port */
{
	SK_U32		i;
	SK_BOOL		PortFound;

	PortFound = SK_FALSE;

	/* Select first port that is PortUp && !SuspectRx. */

	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (!pAC->Rlmt.Port[i].PortDown &&
			!(pAC->Rlmt.Port[i].CheckingState & SK_RLMT_PCS_RX)) {
			*N = i;
			if (!pAC->Rlmt.Port[A].PortDown &&
				!(pAC->Rlmt.Port[A].CheckingState &
				SK_RLMT_PCS_RX)) {
				*N = A;
			}
			if (!pAC->Rlmt.Port[P].PortDown &&
				!(pAC->Rlmt.Port[P].CheckingState &
				SK_RLMT_PCS_RX)) {
				*N = P;
			}
			PortFound = SK_TRUE;
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_CHECK_SWITCH found Port %d up and not check RX.\n",
					*N))
			break;
		}
	}

	return (PortFound);
}	/* SkRlmtSelectNotSuspect */


/******************************************************************************
 *
 *	SkRlmtSelectUp - select new active port, criteria 3, 4 (CLP)
 *
 * Description:
 *	This routine selects a port that is up.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectUp(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
SK_U32	A,	/* active port */
SK_U32	P,	/* preferred port */
SK_U32	*N,	/* new active port */
SK_BOOL	AutoNegDone)	/* successfully auto-negotiated? */
{
	SK_U32		i;
	SK_BOOL		PortFound;

	PortFound = SK_FALSE;

	/* Select first port that is PortUp. */

	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_UP &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {
			*N = i;
			if (pAC->Rlmt.Port[A].PortState == SK_RLMT_PS_UP &&
				pAC->GIni.GP[A].PAutoNegFail != AutoNegDone) {
				*N = A;
			}
			if (pAC->Rlmt.Port[P].PortState == SK_RLMT_PS_UP &&
				pAC->GIni.GP[P].PAutoNegFail != AutoNegDone) {
				*N = P;
			}
			PortFound = SK_TRUE;
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_CHECK_SWITCH found Port %d up.\n",
					*N))
			break;
		}
	}

	return (PortFound);
}	/* SkRlmtSelectUp */


/******************************************************************************
 *
 *	SkRlmtSelectGoingUp - select new active port, criteria 5, 6 (CLP)
 *
 * Description:
 *	This routine selects the port that is going up for the longest time.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectGoingUp(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
SK_U32	A,	/* active port */
SK_U32	P,	/* preferred port */
SK_U32	*N,	/* new active port */
SK_BOOL	AutoNegDone)	/* successfully auto-negotiated? */
{
	SK_U64		GuTimeStamp=0;
	SK_U32		i;
	SK_BOOL		PortFound;

	PortFound = SK_FALSE;

	/* Select port that is PortGoingUp for the longest time. */

	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_GOING_UP &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {
                        GuTimeStamp = pAC->Rlmt.Port[i].GuTimeStamp;
			*N = i;
			PortFound = SK_TRUE;
			break;
		}
	}

	if (!PortFound) {
		return (SK_FALSE);
	}

	for (i = *N + 1; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_GOING_UP &&
                        pAC->Rlmt.Port[i].GuTimeStamp < GuTimeStamp &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {

                        GuTimeStamp = pAC->Rlmt.Port[i].GuTimeStamp;
			*N = i;
		}
	}

	SK_DBG_MSG(
		pAC,
		SK_DBGMOD_RLMT,
		SK_DBGCAT_CTRL,
		("SK_RLMT_CHECK_SWITCH found Port %d going up.\n", *N))

	return (SK_TRUE);
}	/* SkRlmtSelectGoingUp */


/******************************************************************************
 *
 *	SkRlmtSelectDown - select new active port, criteria 7, 8 (CLP)
 *
 * Description:
 *	This routine selects a port that is down.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	SK_BOOL
 */
RLMT_STATIC SK_BOOL	SkRlmtSelectDown(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* I/O context */
SK_U32	A,	/* active port */
SK_U32	P,	/* preferred port */
SK_U32	*N,	/* new active port */
SK_BOOL	AutoNegDone)	/* successfully auto-negotiated? */
{
	SK_U32		i;
	SK_BOOL		PortFound;

	PortFound = SK_FALSE;

	/* Select first port that is PortDown. */

	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].PortState == SK_RLMT_PS_DOWN &&
			pAC->GIni.GP[i].PAutoNegFail != AutoNegDone) {
			*N = i;
			if (pAC->Rlmt.Port[A].PortState == SK_RLMT_PS_DOWN &&
				pAC->GIni.GP[A].PAutoNegFail != AutoNegDone) {
				*N = A;
			}
			if (pAC->Rlmt.Port[P].PortState == SK_RLMT_PS_DOWN &&
				pAC->GIni.GP[P].PAutoNegFail != AutoNegDone) {
				*N = P;
			}
			PortFound = SK_TRUE;
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_CHECK_SWITCH found Port %d down.\n",
					*N))
			break;
		}
	}

	return (PortFound);
}	/* SkRlmtSelectDown */


/******************************************************************************
 *
 *	SkRlmtCheckSwitch - select new active port and switch to it
 *
 * Description:
 *	This routine decides which port should be the active one and queues
 *	port switching if necessary.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtCheckSwitch(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC)	/* I/O context */
{
	SK_EVPARA	Para;
	SK_U32		A;
	SK_U32		P;
	SK_U32		i;
	SK_BOOL		PortFound;

	if (pAC->Rlmt.RlmtState == SK_RLMT_RS_INIT) {
		return;
	}

	A = pAC->Rlmt.MacActive;	/* Index of active port. */
	P = pAC->Rlmt.PrefPort;		/* Index of preferred port. */
	PortFound = SK_FALSE;

	if (pAC->Rlmt.LinksUp == 0) {

		/*
		 * Last link went down - shut down the net.
		 */

		pAC->Rlmt.RlmtState = SK_RLMT_RS_NET_DOWN;
		Para.Para32[0] = SK_RLMT_NET_DOWN_TEMP;
		SkEventQueue(
			pAC,
			SKGE_DRV,
			SK_DRV_NET_DOWN,
			Para);

		return;
	}
	else if (pAC->Rlmt.LinksUp == 1 &&
		pAC->Rlmt.RlmtState == SK_RLMT_RS_NET_DOWN) {

		/* First link came up - get the net up. */

		pAC->Rlmt.RlmtState = SK_RLMT_RS_NET_UP;

		/*
		 * If pAC->Rlmt.MacActive != Para.Para32[0],
		 * the DRV switches to the port that came up.
		 */

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Rlmt.Port[i].LinkDown) {
				if (!pAC->Rlmt.Port[A].LinkDown) {
					i = A;
				}
				if (!pAC->Rlmt.Port[P].LinkDown) {
					i = P;
				}
				PortFound = SK_TRUE;
				break;
			}
		}

		if (PortFound) {
			Para.Para32[0] = pAC->Rlmt.MacActive;
			Para.Para32[1] = i;
			SkEventQueue(
				pAC,
				SKGE_PNMI,
				SK_PNMI_EVT_RLMT_PORT_SWITCH,
				Para);

			pAC->Rlmt.MacActive = i;
			Para.Para32[0] = i;
			SkEventQueue(pAC, SKGE_DRV, SK_DRV_NET_UP, Para);

			if ((Para.pParaPtr = SkRlmtBuildPacket(
					pAC,
					IoC,
					i,
					SK_PACKET_ANNOUNCE,
					&pAC->Addr.CurrentMacAddress,
					&SkRlmtMcAddr)
				) != NULL) {

				/*
				 * Send packet to RLMT multicast address
				 * to force switches to learn the new
				 * location of the address.
				 */

				SkEventQueue(
					pAC,
					SKGE_DRV,
					SK_DRV_RLMT_SEND,
					Para);
			}
		}
		else {
			SK_ERR_LOG(
				pAC,
				SK_ERRCL_SW,
				SKERR_RLMT_E007,
				SKERR_RLMT_E007_MSG);
		}

		return;
	}
	else {
		Para.Para32[0] = A;

		/*
		 * Preselection:
		 *   If RLMT Mode != CheckLinkState
		 *     select port that received a broadcast frame
		 *     substantially later than all other ports
		 *   else select first port that is not SuspectRx
		 *   else select first port that is PortUp
		 *   else select port that is PortGoingUp for the longest time
		 *   else select first port that is PortDown
		 *   else stop.
		 *
		 * For the preselected port:
		 *   If ActivePort is equal in quality, select ActivePort.
		 *
		 *   If PrefPort is equal in quality, select PrefPort.
		 *
		 *   If MacActive != SelectedPort,
		 *     If old ActivePort is LinkDown,
		 *       SwitchHard
		 *     else
		 *       SwitchSoft
		 */

		if (pAC->Rlmt.RlmtMode != SK_RLMT_CHECK_LINK) {
			if (!PortFound) {
				PortFound = SkRlmtSelectBcRx(pAC, IoC,
					A, P, &Para.Para32[1]);
			}

			if (!PortFound) {
				PortFound = SkRlmtSelectNotSuspect(pAC, IoC,
					A, P, &Para.Para32[1]);
			}
		}

		if (!PortFound) {
			PortFound = SkRlmtSelectUp(pAC, IoC,
				A, P, &Para.Para32[1], AUTONEG_SUCCESS);
		}

		if (!PortFound) {
			PortFound = SkRlmtSelectUp(pAC, IoC,
				A, P, &Para.Para32[1], AUTONEG_FAILED);
		}

		if (!PortFound) {
			PortFound = SkRlmtSelectGoingUp(pAC, IoC,
				A, P, &Para.Para32[1], AUTONEG_SUCCESS);
		}

		if (!PortFound) {
			PortFound = SkRlmtSelectGoingUp(pAC, IoC,
				A, P,&Para.Para32[1], AUTONEG_FAILED);
		}

		if (pAC->Rlmt.RlmtMode != SK_RLMT_CHECK_LINK) {
			if (!PortFound) {
				PortFound = SkRlmtSelectDown(pAC, IoC,
					A, P,&Para.Para32[1], AUTONEG_SUCCESS);
			}

			if (!PortFound) {
				PortFound = SkRlmtSelectDown(pAC, IoC,
					A, P,&Para.Para32[1], AUTONEG_FAILED);
			}
		}

		if (PortFound) {
			if (Para.Para32[1] != A) {
				pAC->Rlmt.MacActive = Para.Para32[1];
				SK_HWAC_LINK_LED(
					pAC,
					IoC,
					Para.Para32[1],
					SK_LED_ACTIVE);
				if (pAC->Rlmt.Port[A].LinkDown) {
					SkEventQueue(
						pAC,
						SKGE_DRV,
						SK_DRV_SWITCH_HARD,
						Para);
				}
				else {
					SK_HWAC_LINK_LED(
						pAC,
						IoC,
						Para.Para32[0],
						SK_LED_STANDBY);
					SkEventQueue(
						pAC,
						SKGE_DRV,
						SK_DRV_SWITCH_SOFT,
						Para);
				}
				SkEventQueue(
					pAC,
					SKGE_PNMI,
					SK_PNMI_EVT_RLMT_PORT_SWITCH,
					Para);
				if ((Para.pParaPtr = SkRlmtBuildPacket(
						pAC,
						IoC,
						Para.Para32[1],
						SK_PACKET_ANNOUNCE,
						&pAC->Addr.CurrentMacAddress,
						&SkRlmtMcAddr)
					) != NULL) {

					/*
					 * Send "new" packet to RLMT multicast
					 * address to force switches to learn
					 * the new location of the MAC address.
					 */

					SkEventQueue(
						pAC,
						SKGE_DRV,
						SK_DRV_RLMT_SEND,
						Para);
				}
			}
		}
		else {
			SK_ERR_LOG(
				pAC,
				SK_ERRCL_SW,
				SKERR_RLMT_E004,
				SKERR_RLMT_E004_MSG);
		}
	}

	return;
}	/* SkRlmtCheckSwitch */


/******************************************************************************
 *
 *	SkRlmtCheckSeg - Report if segmentation is detected
 *
 * Description:
 *	This routine checks if the ports see different root bridges and reports
 *	segmentation in such a case.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing.
 */
RLMT_STATIC void	SkRlmtCheckSeg(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC)	/* I/O context */
{
	SK_BOOL	Equal;
	SK_U32	i, j;

	pAC->Rlmt.RootIdSet = SK_FALSE;
	Equal = SK_TRUE;
	for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
		if (pAC->Rlmt.Port[i].LinkDown ||
			!pAC->Rlmt.Port[i].RootIdSet) {
			continue;
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("Root ID %d: %02x %02x %02x %02x %02x %02x %02x %02x.\n",
				i,
				pAC->Rlmt.Port[i].Root.Id[0],
				pAC->Rlmt.Port[i].Root.Id[1],
				pAC->Rlmt.Port[i].Root.Id[2],
				pAC->Rlmt.Port[i].Root.Id[3],
				pAC->Rlmt.Port[i].Root.Id[4],
				pAC->Rlmt.Port[i].Root.Id[5],
				pAC->Rlmt.Port[i].Root.Id[6],
				pAC->Rlmt.Port[i].Root.Id[7]))

		if (!pAC->Rlmt.RootIdSet) {
			pAC->Rlmt.Root = pAC->Rlmt.Port[i].Root;
			pAC->Rlmt.RootIdSet = SK_TRUE;
			continue;
		}

		for (j = 0; j < 8; j ++) {
			Equal &= pAC->Rlmt.Port[i].Root.Id[j] ==
				pAC->Rlmt.Root.Id[j];
			if (!Equal) {
				break;
			}
		}
		
		if (!Equal) {
			SK_ERR_LOG(
				pAC,
				SK_ERRCL_COMM,
				SKERR_RLMT_E005,
				SKERR_RLMT_E005_MSG);
#ifdef SK_PNMI_EVT_SEGMENTATION
			SkEventQueue(
				pAC,
				SKGE_PNMI,
				SK_PNMI_EVT_SEGMENTATION,
				Para);
#endif	/* SK_PNMI_EVT_SEGMENTATION */
			pAC->Rlmt.CheckingState &= ~SK_RLMT_RCS_REPORT_SEG;
			break;
		}
	}
}	/* SkRlmtCheckSeg */


/******************************************************************************
 *
 *	SkRlmtPortStart - initialize port variables and start port
 *
 * Description:
 *	This routine initializes a port's variables and issues a PORT_START
 *	to the HWAC module.  This handles retries if the start fails or the
 *	link eventually goes down.
 *
 * Context:
 *	runtime, pageable?
 *
 * Returns:
 *	Nothing
 */
RLMT_STATIC void	SkRlmtPortStart(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortIdx)	/* event code */
{
	SK_EVPARA	Para;

	pAC->Rlmt.Port[PortIdx].PortState = SK_RLMT_PS_LINK_DOWN;
	pAC->Rlmt.Port[PortIdx].PortStarted = SK_TRUE;
	pAC->Rlmt.Port[PortIdx].LinkDown = SK_TRUE;
	pAC->Rlmt.Port[PortIdx].PortDown = SK_TRUE;
	pAC->Rlmt.Port[PortIdx].CheckingState = 0;
	pAC->Rlmt.Port[PortIdx].RootIdSet = SK_FALSE;
	Para.Para32[0] = PortIdx;
	SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_START, Para);
}	/* SkRlmtPortStart */


/******************************************************************************
 *
 *	SkRlmtEvent - a PORT- or an RLMT-specific event happened
 *
 * Description:
 *	This routine handles PORT- and RLMT-specific events.
 *
 * Context:
 *	runtime, pageable?
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	0
 */
int	SkRlmtEvent(
SK_AC		*pAC,	/* adapter context */
SK_IOC		IoC,	/* I/O context */
SK_U32		Event,	/* event code */
SK_EVPARA	Para)	/* event-specific parameter */
{
	SK_U32		i, j;
	SK_RLMT_PORT	*pRPort;
	SK_EVPARA	Para2;
	SK_MBUF		*pMb;
	SK_MBUF		*pNextMb;
	SK_MAC_ADDR	*pOldMacAddr;
	SK_MAC_ADDR	*pNewMacAddr;
	SK_U32		Timeout;
	SK_U32		NewTimeout;
	SK_U32		PrevRlmtMode;

	switch (Event) {
	case SK_RLMT_PORTSTART_TIM:	/* From RLMT via TIME. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORTSTART_TIMEOUT Port %d Event (%d) BEGIN.\n", Para.Para32[0], Event))

		/*
		 * Used to start non-preferred ports if the preferred one
		 * does not come up.
		 * This timeout needs only be set when starting the first
		 * (preferred) port.
		 */

		if (pAC->Rlmt.Port[Para.Para32[0]].LinkDown) {

			/* PORT_START failed. */

			for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
				if (!pAC->Rlmt.Port[i].PortStarted) {
					SkRlmtPortStart(pAC, IoC, i);
				}
			}
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORTSTART_TIMEOUT Event (%d) END.\n", Event))
		break;

	case SK_RLMT_LINK_UP:		/* From SIRQ. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_LINK_UP Port %d Event (%d) BEGIN.\n",
				Para.Para32[0], Event))

		pRPort = &pAC->Rlmt.Port[Para.Para32[0]];

		if (!pRPort->PortStarted) {
			SK_ERR_LOG(
				pAC,
				SK_ERRCL_SW,
				SKERR_RLMT_E008,
				SKERR_RLMT_E008_MSG);

			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_LINK_UP Event (%d) EMPTY.\n", Event))
			break;
		}

		if (!pRPort->LinkDown) {
			/* RA;:;: Any better solution? */

			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_LINK_UP Event (%d) EMPTY.\n", Event))
			break;
		}

		SkTimerStop(pAC, IoC, &pRPort->UpTimer);
		SkTimerStop(pAC, IoC, &pRPort->DownRxTimer);
		SkTimerStop(pAC, IoC, &pRPort->DownTxTimer);

		/* Do something if timer already fired? */

		pRPort->LinkDown = SK_FALSE;
		pRPort->PortState = SK_RLMT_PS_GOING_UP;
		pRPort->GuTimeStamp = SkOsGetTime(pAC);
		pRPort->BcTimeStamp = 0;
		if (pAC->Rlmt.LinksUp == 0) {
			SK_HWAC_LINK_LED(
				pAC,
				IoC,
				Para.Para32[0],
				SK_LED_ACTIVE);
		}
		else {
			SK_HWAC_LINK_LED(
				pAC,
				IoC,
				Para.Para32[0],
				SK_LED_STANDBY);
		}
		pAC->Rlmt.LinksUp++;

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Rlmt.Port[i].PortStarted) {
				SkRlmtPortStart(pAC, IoC, i);
			}
		}

		SkRlmtCheckSwitch(pAC, IoC);

		if (pAC->Rlmt.LinksUp >= 2) {
			if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LOC_LINK) {

				/* Build the check chain. */

				SkRlmtBuildCheckChain(pAC);
			}
		}

		/*
		 * If the first link comes up, start the periodical
		 * RLMT timeout.
		 */

		if (pAC->GIni.GIMacsFound > 1 && pAC->Rlmt.LinksUp == 1 &&
			(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_OTHERS)) {
			SkTimerStart(
				pAC,
				IoC,
				&pAC->Rlmt.LocTimer,
				pAC->Rlmt.TimeoutValue,
				SKGE_RLMT,
				SK_RLMT_TIM,
				Para);
		}

		SkTimerStart(
			pAC,
			IoC,
			&pRPort->UpTimer,
			SK_RLMT_PORTUP_TIM_VAL,
			SKGE_RLMT,
			SK_RLMT_PORTUP_TIM,
			Para);

		/*
		 * Later:
		 * if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LOC_LINK) &&
		 */

		if ((pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LINK) &&
			(Para2.pParaPtr = SkRlmtBuildPacket(
				pAC,
				IoC,
				Para.Para32[0],
				SK_PACKET_ANNOUNCE,
				&pAC->Addr.Port[Para.Para32[0]].CurrentMacAddress,
				&SkRlmtMcAddr)
			) != NULL) {

			/* Send "new" packet to RLMT multicast address. */

			SkEventQueue(pAC, SKGE_DRV, SK_DRV_RLMT_SEND, Para2);
		}

		if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) {
			if ((Para2.pParaPtr =
				SkRlmtBuildSpanningTreePacket(
					pAC,
					IoC,
					Para.Para32[0])
				) != NULL) {
				
				pAC->Rlmt.Port[Para.Para32[0]].RootIdSet =
					SK_FALSE;
				pAC->Rlmt.CheckingState |=
					SK_RLMT_RCS_SEG |
					SK_RLMT_RCS_REPORT_SEG;
				
				SkEventQueue(
					pAC,
					SKGE_DRV,
					SK_DRV_RLMT_SEND,
					Para2);

				SkTimerStart(
					pAC,
					IoC,
					&pAC->Rlmt.SegTimer,
					SK_RLMT_SEG_TO_VAL,
					SKGE_RLMT,
					SK_RLMT_SEG_TIM,
					Para);
			}
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_LINK_UP Event (%d) END.\n", Event))
		break;

	case SK_RLMT_PORTUP_TIM:	/* From RLMT via TIME. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORTUP_TIM Port %d Event (%d) BEGIN.\n", Para.Para32[0], Event))

		pRPort = &pAC->Rlmt.Port[Para.Para32[0]];

		if (pRPort->LinkDown || (pRPort->PortState == SK_RLMT_PS_UP)) {
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_PORTUP_TIM Port %d Event (%d) EMPTY.\n",
					Para.Para32[0],
					Event))
			break;
		}

		pRPort->PortDown = SK_FALSE;
                pRPort->PortState = SK_RLMT_PS_UP;
		pAC->Rlmt.PortsUp++;

		if (pAC->Rlmt.RlmtState != SK_RLMT_RS_INIT) {
			SkRlmtCheckSwitch(pAC, IoC);

			SkEventQueue(
				pAC,
				SKGE_PNMI,
				SK_PNMI_EVT_RLMT_PORT_UP,
				Para);
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORTUP_TIM Event (%d) END.\n", Event))
		break;

	case SK_RLMT_PORTDOWN:		/* From RLMT. */
	case SK_RLMT_PORTDOWN_RX_TIM:	/* From RLMT via TIME. */
	case SK_RLMT_PORTDOWN_TX_TIM:	/* From RLMT via TIME. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORTDOWN* Port %d Event (%d) BEGIN.\n", Para.Para32[0], Event))

		pRPort = &pAC->Rlmt.Port[Para.Para32[0]];

		if (!pRPort->PortStarted ||
			(Event == SK_RLMT_PORTDOWN_TX_TIM &&
			!(pRPort->CheckingState & SK_RLMT_PCS_TX))) {
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_PORTDOWN* Event (%d) EMPTY.\n", Event))
			break;
		}
		/* Stop port's timers. */

		SkTimerStop(pAC, IoC, &pRPort->UpTimer);
		SkTimerStop(pAC, IoC, &pRPort->DownRxTimer);
		SkTimerStop(pAC, IoC, &pRPort->DownTxTimer);

		if (pRPort->PortState != SK_RLMT_PS_LINK_DOWN) {
			pRPort->PortState = SK_RLMT_PS_DOWN;
		}

		if (!pRPort->PortDown) {
			pAC->Rlmt.PortsUp--;
			pRPort->PortDown = SK_TRUE;

			SkEventQueue(
				pAC,
				SKGE_PNMI,
				SK_PNMI_EVT_RLMT_PORT_DOWN,
				Para);
		}

		pRPort->PacketsPerTimeSlot = 0;
		pRPort->DataPacketsPerTimeSlot = 0;
		pRPort->BpduPacketsPerTimeSlot = 0;
#if 0
		pRPort->RlmtChksPerTimeSlot = 0;
		pRPort->RlmtAcksPerTimeSlot = 0;
#endif	/* 0 */

		/*
		 * RA;:;: To be checked:
		 * - actions at RLMT_STOP: We should not switch anymore.
		 */

		if (pAC->Rlmt.RlmtState != SK_RLMT_RS_INIT) {
			if (Para.Para32[0] == pAC->Rlmt.MacActive) {

				/* Active Port went down. */

				SkRlmtCheckSwitch(pAC, IoC);
			}
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORTDOWN* Event (%d) END.\n", Event))
		break;

	case SK_RLMT_LINK_DOWN:		/* From SIRQ. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_LINK_DOWN Port %d Event (%d) BEGIN.\n",
				Para.Para32[0], Event))

		if (!pAC->Rlmt.Port[Para.Para32[0]].LinkDown) {
			pAC->Rlmt.LinksUp--;
			pAC->Rlmt.Port[Para.Para32[0]].LinkDown = SK_TRUE;
			pAC->Rlmt.Port[Para.Para32[0]].PortState =
					SK_RLMT_PS_LINK_DOWN;
			SK_HWAC_LINK_LED(
				pAC,
				IoC,
				Para.Para32[0],
				SK_LED_OFF);

			if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LOC_LINK) {

				/* Build the check chain. */

				SkRlmtBuildCheckChain(pAC);
			}

			/* Ensure that port is marked down. */

			(void)SkRlmtEvent(
				pAC,
				IoC,
				SK_RLMT_PORTDOWN,
				Para);
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_LINK_DOWN Event (%d) END.\n", Event))
		break;

	case SK_RLMT_PORT_ADDR:		/* From ADDR. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORT_ADDR Port %d Event (%d) BEGIN.\n", Para.Para32[0], Event))

		/* Port's physical MAC address changed. */

		pOldMacAddr =
			&pAC->Addr.Port[Para.Para32[0]].PreviousMacAddress;
		pNewMacAddr =
			&pAC->Addr.Port[Para.Para32[0]].CurrentMacAddress;

		/*
		 * NOTE: This is not scalable for solutions where ports are
		 *	 checked remotely.  There, we need to send an RLMT
		 *	 address change packet - and how do we ensure delivery?
		 */

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			pRPort = &pAC->Rlmt.Port[i];
			for (j = 0; j < pRPort->PortsChecked; j++) {
				if (SK_ADDR_EQUAL(
					pRPort->PortCheck[j].CheckAddr.a,
					pOldMacAddr->a)) {
					pRPort->PortCheck[j].CheckAddr =
						*pNewMacAddr;
				}
			}
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PORT_ADDR Event (%d) END.\n", Event))
		break;

	/* ----- RLMT events ----- */

	case SK_RLMT_START:		/* From DRV. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_START Event (%d) BEGIN.\n", Event))

		if (pAC->Rlmt.RlmtState != SK_RLMT_RS_INIT) {
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_START Event (%d) EMPTY.\n", Event))
			break;
		}

		if (pAC->Rlmt.PrefPort >= (SK_U32)pAC->GIni.GIMacsFound) {
			SK_ERR_LOG(
				pAC,
				SK_ERRCL_SW,
				SKERR_RLMT_E009,
				SKERR_RLMT_E009_MSG);

			/* Change PrefPort to internal default. */

			Para.Para32[0] = 0xFFFFFFFF;
			(void)SkRlmtEvent(
				pAC,
				IoC,
				SK_RLMT_PREFPORT_CHANGE,
				Para);
		}
#if 0
		if (pAC->GIni.GIMacsFound == 1 &&
			pAC->Rlmt.RlmtMode != SK_RLMT_CHECK_LINK) {
			Para.Para32[0] = SK_RLMT_CHECK_LINK;
			(void)SkRlmtEvent(
				pAC,
				IoC,
				SK_RLMT_MODE_CHANGE,
				Para);
		}
#endif	/* 0 */
		pAC->Rlmt.LinksUp = 0;
		pAC->Rlmt.PortsUp = 0;
		pAC->Rlmt.CheckingState = 0;
		pAC->Rlmt.RlmtState = SK_RLMT_RS_NET_DOWN;

		SkRlmtPortStart(pAC, IoC, pAC->Rlmt.PrefPort);

		/* Start Timer (for first port only). */

		Para2.Para32[0] = pAC->Rlmt.PrefPort;
		SkTimerStart(
			pAC,
			IoC,
			&pAC->Rlmt.Port[pAC->Rlmt.PrefPort].UpTimer,
			SK_RLMT_PORTSTART_TIM_VAL,
			SKGE_RLMT,
			SK_RLMT_PORTSTART_TIM,
			Para2);

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_START Event (%d) END.\n", Event))
		break;

	case SK_RLMT_STOP:		/* From DRV. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_STOP Event (%d) BEGIN.\n", Event))

		if (pAC->Rlmt.RlmtState == SK_RLMT_RS_INIT) {
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_STOP Event (%d) EMPTY.\n", Event))
			break;
		}

		/* Stop RLMT timers. */

		SkTimerStop(pAC, IoC, &pAC->Rlmt.LocTimer); 
		SkTimerStop(pAC, IoC, &pAC->Rlmt.SegTimer);

		/* Stop Net. */

		pAC->Rlmt.RlmtState = SK_RLMT_RS_INIT;
		pAC->Rlmt.RootIdSet = SK_FALSE;
		Para2.Para32[0] = SK_RLMT_NET_DOWN_FINAL;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_NET_DOWN, Para2);

		/* Stop ports. */

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			if (pAC->Rlmt.Port[i].PortState != SK_RLMT_PS_INIT) {
				SkTimerStop(
					pAC,
					IoC,
					&pAC->Rlmt.Port[i].UpTimer);
				SkTimerStop(
					pAC,
					IoC,
					&pAC->Rlmt.Port[i].DownRxTimer);
				SkTimerStop(
					pAC,
					IoC,
					&pAC->Rlmt.Port[i].DownTxTimer);

				pAC->Rlmt.Port[i].PortState = SK_RLMT_PS_INIT;
				pAC->Rlmt.Port[i].RootIdSet = SK_FALSE;
				pAC->Rlmt.Port[i].PortStarted = SK_FALSE;
				Para2.Para32[0] = i;
				SkEventQueue(
					pAC,
					SKGE_HWAC,
					SK_HWEV_PORT_STOP,
					Para2);
			}
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_STOP Event (%d) END.\n", Event))
		break;

	case SK_RLMT_TIM:		/* From RLMT via TIME. */
#if 0
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_TIM Event (%d) BEGIN.\n", Event))
#endif	/* 0 */

		if (!(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_OTHERS) ||
			pAC->Rlmt.LinksUp == 0) {

			/*
			 * Mode changed or all links down:
			 * No more link checking.
			 */

			break;
		}

#if 0
                pAC->Rlmt.SwitchCheckCounter--;
		if (pAC->Rlmt.SwitchCheckCounter == 0) {
			pAC->Rlmt.SwitchCheckCounter;
		}
#endif	/* 0 */

		NewTimeout = SK_RLMT_DEF_TO_VAL;
		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			pRPort = &pAC->Rlmt.Port[i];
			if (!pRPort->LinkDown) {
				Timeout = SkRlmtCheckPort(pAC, IoC, i);
				if (Timeout < NewTimeout) {
					NewTimeout = Timeout;
				}

				/*
				 * This counter should be set to 0 for all
				 * ports before the first frame is sent in the
				 * next loop.
				 */

				pRPort->PacketsPerTimeSlot = 0;
				pRPort->DataPacketsPerTimeSlot = 0;
				pRPort->BpduPacketsPerTimeSlot = 0;
#if 0
				pRPort->RlmtChksPerTimeSlot = 0;
				pRPort->RlmtAcksPerTimeSlot = 0;
#endif	/* 0 */
			}
		}
		pAC->Rlmt.TimeoutValue = NewTimeout;

		if (pAC->Rlmt.LinksUp > 1) {
			/*
			 * If checking remote ports, also send packets if
			 *   (LinksUp == 1) &&
			 *   this port checks at least one (remote) port.
			 */

			/*
			 * Must be new loop, as SkRlmtCheckPort can request to
			 * check segmentation when e.g. checking the last port.
			 */

			for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
				pRPort = &pAC->Rlmt.Port[i];
				if (!pRPort->LinkDown) {	/* !PortDown? */
					SkRlmtSend(pAC, IoC, i);
				}
			}
		}

		SkTimerStart(
			pAC,
			IoC,
			&pAC->Rlmt.LocTimer,
			pAC->Rlmt.TimeoutValue,
			SKGE_RLMT,
			SK_RLMT_TIM,
			Para);

		if (pAC->Rlmt.LinksUp > 1 &&
			(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) &&
			(pAC->Rlmt.CheckingState & SK_RLMT_RCS_START_SEG)) {
			SkTimerStart(
				pAC,
				IoC,
				&pAC->Rlmt.SegTimer,
				SK_RLMT_SEG_TO_VAL,
				SKGE_RLMT,
				SK_RLMT_SEG_TIM,
				Para);
			pAC->Rlmt.CheckingState &=
				~SK_RLMT_RCS_START_SEG;
			pAC->Rlmt.CheckingState |=
				SK_RLMT_RCS_SEG | SK_RLMT_RCS_REPORT_SEG;
		}

#if 0
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_TIM Event (%d) END.\n", Event))
#endif	/* 0 */
		break;

	case SK_RLMT_SEG_TIM:
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_SEG_TIM Event (%d) BEGIN.\n", Event))

#ifdef DEBUG
		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			SK_U8		InAddr8[6];
			SK_U16		*InAddr;
			SK_ADDR_PORT	*pAPort;

			InAddr = (SK_U16 *)&InAddr8[0];
			pAPort = &pAC->Addr.Port[i];
			for (j = 0;
				j < pAPort->NextExactMatchRlmt;
				j++) {

				/* Get exact match address j from port i. */

				XM_INADDR(IoC, i, XM_EXM(j), InAddr);
				SK_DBG_MSG(
					pAC,
					SK_DBGMOD_RLMT,
					SK_DBGCAT_CTRL,
					("MC address %d on Port %u: %02x %02x %02x %02x %02x %02x --  %02x %02x %02x %02x %02x %02x.\n",
						j,
       						i,
       						InAddr8[0],
       						InAddr8[1],
       						InAddr8[2],
       						InAddr8[3],
       						InAddr8[4],
       						InAddr8[5],
						pAPort->Exact[j].a[0],
						pAPort->Exact[j].a[1],
						pAPort->Exact[j].a[2],
						pAPort->Exact[j].a[3],
						pAPort->Exact[j].a[4],
						pAPort->Exact[j].a[5]))
			}
		}
#endif	/* DEBUG */
				   
		pAC->Rlmt.CheckingState &= ~SK_RLMT_RCS_SEG;

		SkRlmtCheckSeg(pAC, IoC);

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_SEG_TIM Event (%d) END.\n", Event))
		break;

	case SK_RLMT_PACKET_RECEIVED:	/* From DRV. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PACKET_RECEIVED Event (%d) BEGIN.\n", Event))

		/* Should we ignore frames during port switching? */

#ifdef DEBUG
		pMb = Para.pParaPtr;
		if (pMb == NULL) {
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("No mbuf.\n"))
		}
		else if (pMb->pNext != NULL) {
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("More than one mbuf or pMb->pNext not set.\n"))
		}
#endif	/* DEBUG */

		for (pMb = Para.pParaPtr; pMb != NULL; pMb = pNextMb) {
			pNextMb = pMb->pNext;
			pMb->pNext = NULL;
			SkRlmtPacketReceive(pAC, IoC, pMb);
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PACKET_RECEIVED Event (%d) END.\n", Event))
		break;

	case SK_RLMT_STATS_CLEAR:	/* From PNMI. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_CLEAR Event (%d) BEGIN.\n", Event))

		/* Clear statistics for virtual and physical ports. */

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			pAC->Rlmt.Port[i].TxHelloCts = 0;
			pAC->Rlmt.Port[i].RxHelloCts = 0;
			pAC->Rlmt.Port[i].TxSpHelloReqCts = 0;
			pAC->Rlmt.Port[i].RxSpHelloCts = 0;
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_CLEAR Event (%d) END.\n", Event))
		break;

	case SK_RLMT_STATS_UPDATE:	/* From PNMI. */
#if 0
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_UPDATE Event (%d) BEGIN.\n", Event))

		/* Update statistics. */

		/* Currently always up-to-date. */

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_STATS_UPDATE Event (%d) END.\n", Event))
#endif	/* 0 */
		break;

	case SK_RLMT_PREFPORT_CHANGE:	/* From PNMI. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PREFPORT_CHANGE to Port %d Event (%d) BEGIN.\n", Para.Para32[0], Event))

		/* 0xFFFFFFFF == auto-mode. */

		if (Para.Para32[0] == 0xFFFFFFFF) {
			pAC->Rlmt.PrefPort = SK_RLMT_DEF_PREF_PORT;
		}
		else {
			if (Para.Para32[0] >= (SK_U32)pAC->GIni.GIMacsFound) {
				SK_ERR_LOG(
					pAC,
					SK_ERRCL_SW,
					SKERR_RLMT_E010,
					SKERR_RLMT_E010_MSG);

				SK_DBG_MSG(
					pAC,
					SK_DBGMOD_RLMT,
					SK_DBGCAT_CTRL,
					("SK_RLMT_PREFPORT_CHANGE Event (%d) EMPTY.\n", Event))
				break;
			}

			pAC->Rlmt.PrefPort = Para.Para32[0];
		}

		pAC->Rlmt.MacPreferred = Para.Para32[0];

		SkRlmtCheckSwitch(pAC, IoC);

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_PREFPORT_CHANGE Event (%d) END.\n", Event))
		break;

	case SK_RLMT_MODE_CHANGE:	/* From PNMI. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_MODE_CHANGE Event (%d) BEGIN.\n", Event))

		if (pAC->GIni.GIMacsFound < 2) {
			pAC->Rlmt.RlmtMode = SK_RLMT_CHECK_LINK;
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("Forced RLMT mode to CLS on single link adapter.\n"))
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_RLMT,
				SK_DBGCAT_CTRL,
				("SK_RLMT_MODE_CHANGE Event (%d) EMPTY.\n",
					Event))
			break;
		}

		/* Update RLMT mode. */

		PrevRlmtMode = pAC->Rlmt.RlmtMode;
		pAC->Rlmt.RlmtMode = Para.Para32[0] | SK_RLMT_CHECK_LINK;

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("RLMT: Changed Mode to %X.\n", pAC->Rlmt.RlmtMode))

		if ((PrevRlmtMode & SK_RLMT_CHECK_LOC_LINK) !=
			(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_LOC_LINK)) {
			if (!(PrevRlmtMode & SK_RLMT_CHECK_OTHERS) &&
				pAC->GIni.GIMacsFound > 1 &&
				pAC->Rlmt.PortsUp == 1) {
				SkTimerStart(
					pAC,
					IoC,
					&pAC->Rlmt.LocTimer,
					pAC->Rlmt.TimeoutValue,
					SKGE_RLMT,
					SK_RLMT_TIM,
					Para);
			}
		}

		if ((PrevRlmtMode & SK_RLMT_CHECK_SEG) !=
			(pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG)) {

			for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
				(void)SkAddrMcClear(
					pAC,
					IoC,
					i,
					SK_ADDR_PERMANENT | SK_MC_SW_ONLY);

				/* Add RLMT MC address. */

				(void)SkAddrMcAdd(
					pAC,
					IoC,
					i,
					&SkRlmtMcAddr,
					SK_ADDR_PERMANENT);

				if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) {
					/* Add BPDU MC address. */

					(void)SkAddrMcAdd(
						pAC,
						IoC,
						i,
						&BridgeMcAddr,
						SK_ADDR_PERMANENT);

					if (pAC->Rlmt.RlmtState !=
						SK_RLMT_RS_INIT) {
						if (!pAC->Rlmt.Port[i].LinkDown &&
							(Para2.pParaPtr =
							SkRlmtBuildSpanningTreePacket(
								pAC,
								IoC,
								i)
							) != NULL) {
							
							pAC->Rlmt.Port[i
								].RootIdSet =
								SK_FALSE;

							SkEventQueue(
								pAC,
								SKGE_DRV,
								SK_DRV_RLMT_SEND,
								Para2);
						}
					}
				}

				(void)SkAddrMcUpdate(pAC, IoC, i);
			}

			if (pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) {
				SkTimerStart(
					pAC,
					IoC,
					&pAC->Rlmt.SegTimer,
					SK_RLMT_SEG_TO_VAL,
					SKGE_RLMT,
					SK_RLMT_SEG_TIM,
					Para);
			}
		}

		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("SK_RLMT_MODE_CHANGE Event (%d) END.\n", Event))
		break;

	default:	/* Create error log entry. */
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("Unknown RLMT Event %d.\n", Event))

		SK_ERR_LOG(
			pAC,
			SK_ERRCL_SW,
			SKERR_RLMT_E003,
			SKERR_RLMT_E003_MSG);
		break;
	}

	return (0);       
}	/* SkRlmtEvent */

#ifdef __cplusplus
}
#endif	/* __cplusplus */
