/******************************************************************************
 *
 * Name:	skrlmt.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.27 $
 * Date:	$Date: 1999/11/22 13:59:56 $
 * Purpose:	Header file for Redundant Link ManagemenT.
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
 *	$Log: skrlmt.h,v $
 *	Revision 1.27  1999/11/22 13:59:56  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.26  1999/10/04 14:01:19  rassmann
 *	Corrected reaction to reception of BPDU frames.
 *	Added parameter descriptions to "For Readme" section skrlmt.txt.
 *	Clarified usage of lookahead result *pForRlmt.
 *	Requested driver to present RLMT packets as soon as poosible.
 *	
 *	Revision 1.25  1999/07/20 12:53:39  rassmann
 *	Fixed documentation errors for lookahead macros.
 *	
 *	Revision 1.24  1999/05/28 11:15:56  rassmann
 *	Changed behaviour to reflect Design Spec v1.2.
 *	Controlling Link LED(s).
 *	Introduced RLMT Packet Version field in RLMT Packet.
 *	Newstyle lookahead macros (checking meta-information before looking at
 *	  the packet).
 *	
 *	Revision 1.23  1999/01/28 12:50:42  rassmann
 *	Not using broadcast time stamps in CheckLinkState mode.
 *	
 *	Revision 1.22  1999/01/27 14:13:04  rassmann
 *	Monitoring broadcast traffic.
 *	Switching more reliably and not too early if switch is
 *	 configured for spanning tree.
 *	
 *	Revision 1.21  1998/12/08 13:11:25  rassmann
 *	Stopping SegTimer at RlmtStop.
 *	
 *	Revision 1.20  1998/11/24 12:37:33  rassmann
 *	Implemented segmentation check.
 *	
 *	Revision 1.19  1998/11/17 13:43:06  rassmann
 *	Handling (logical) tx failure.
 *	Sending packet on logical address after PORT_SWITCH.
 *	
 *	Revision 1.18  1998/11/13 16:56:56  rassmann
 *	Added macro version of SkRlmtLookaheadPacket.
 *	
 *	Revision 1.17  1998/11/06 18:06:05  rassmann
 *	Corrected timing when RLMT checks fail.
 *	Clearing tx counter earlier in periodical checks.
 *	
 *	Revision 1.16  1998/11/03 13:53:50  rassmann
 *	RLMT should switch now (at least in mode 3).
 *	
 *	Revision 1.15  1998/10/22 11:39:52  rassmann
 *	Corrected signed/unsigned mismatches.
 *	Corrected receive list handling and address recognition.
 *	
 *	Revision 1.14  1998/10/15 15:16:36  rassmann
 *	Finished Spanning Tree checking.
 *	Checked with lint.
 *	
 *	Revision 1.13  1998/09/24 19:16:08  rassmann
 *	Code cleanup.
 *	Introduced Timer for PORT_DOWN due to no RX.
 *	
 *	Revision 1.12  1998/09/16 11:09:52  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.11  1998/09/15 11:28:50  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.10  1998/09/14 17:07:38  rassmann
 *	Added code for port checking via LAN.
 *	Changed Mbuf definition.
 *	
 *	Revision 1.9  1998/09/07 11:14:15  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.8  1998/09/07 09:06:08  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.7  1998/09/04 19:41:34  rassmann
 *	Syntax corrections.
 *	Started entering code for checking local links.
 *	
 *	Revision 1.6  1998/09/04 12:14:28  rassmann
 *	Interface cleanup.
 *	
 *	Revision 1.5  1998/09/02 16:55:29  rassmann
 *	Updated to reflect new DRV/HWAC/RLMT interface.
 *	
 *	Revision 1.4  1998/09/02 07:26:02  afischer
 *	typedef for SK_RLMT_PORT
 *
 *	Revision 1.3  1998/08/27 14:29:03  rassmann
 *	Code cleanup.
 *	
 *	Revision 1.2  1998/08/27 14:26:25  rassmann
 *	Updated interface.
 *	
 *	Revision 1.1  1998/08/21 08:29:10  rassmann
 *	First public version.
 *	
 ******************************************************************************/

/******************************************************************************
 *
 * Description:
 *
 * This is the header file for Redundant Link ManagemenT.
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	...
 *	"sktypes.h"
 *	"skqueue.h"
 *	"skaddr.h"
 *	"skrlmt.h"
 *	...
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#ifndef __INC_SKRLMT_H
#define __INC_SKRLMT_H

#ifdef __cplusplus
xxxx	/* not supported yet - force error */
extern "C" {
#endif	/* cplusplus */

/* defines ********************************************************************/

#define	SK_RLMT_NET_DOWN_TEMP	1	/* NET_DOWN due to last port down. */
#define	SK_RLMT_NET_DOWN_FINAL	2	/* NET_DOWN due to RLMT_STOP. */

/* ----- Default queue sizes - must be multiples of 8 KB ----- */

/* Less than 8 KB free in RX queue => pause frames. */
#define SK_RLMT_STANDBY_QRXSIZE	128	/* Size of rx standby queue in KB. */
#define SK_RLMT_STANDBY_QXASIZE	32	/* Size of async standby queue in KB. */
#define SK_RLMT_STANDBY_QXSSIZE	0	/* Size of sync standby queue in KB. */

#define SK_RLMT_MAX_TX_BUF_SIZE	60	/* Maximum RLMT transmit size. */

/* ----- PORT states ----- */

#define SK_RLMT_PS_INIT		0	/* Port state: Init. */
#define SK_RLMT_PS_LINK_DOWN	1	/* Port state: Link down. */
#define SK_RLMT_PS_DOWN		2	/* Port state: Port down. */
#define SK_RLMT_PS_GOING_UP	3	/* Port state: Going up. */
#define SK_RLMT_PS_UP		4	/* Port state: Up. */

/* ----- RLMT states ----- */

#define SK_RLMT_RS_INIT		0	/* RLMT state: Init. */
#define SK_RLMT_RS_NET_DOWN	1	/* RLMT state: Net down. */
#define SK_RLMT_RS_NET_UP	2	/* RLMT state: Net up. */

/* ----- PORT events ----- */

#define SK_RLMT_LINK_UP		1001	/* Link came up. */
#define SK_RLMT_LINK_DOWN	1002	/* Link went down. */
#define SK_RLMT_PORT_ADDR	1003	/* Port address changed. */

/* ----- RLMT events ----- */

#define SK_RLMT_START		2001	/* Start RLMT. */
#define SK_RLMT_STOP		2002	/* Stop RLMT. */
#define SK_RLMT_PACKET_RECEIVED	2003	/* Packet was received for RLMT. */
#define SK_RLMT_STATS_CLEAR	2004	/* Clear statistics. */
#define SK_RLMT_STATS_UPDATE	2005	/* Update statistics. */
#define SK_RLMT_PREFPORT_CHANGE	2006	/* Change preferred port. */
#define SK_RLMT_MODE_CHANGE	2007	/* New RlmtMode. */

/* ----- RLMT mode bits ----- */

#define SK_RLMT_CHECK_LINK	1	/* Check Link. */
#define SK_RLMT_CHECK_LOC_LINK	2	/* Check other link on same adapter. */
#define SK_RLMT_CHECK_SEG	4	/* Check segmentation. */

#ifndef RLMT_CHECK_REMOTE
#define SK_RLMT_CHECK_OTHERS	SK_RLMT_CHECK_LOC_LINK
#else	/* RLMT_CHECK_REMOTE */
#define SK_RLMT_CHECK_REM_LINK	8	/* Check link(s) on other adapter(s). */
#define SK_RLMT_MAX_REMOTE_PORTS_CHECKED	3
#define SK_RLMT_CHECK_OTHERS	(SK_RLMT_CHECK_LOC_LINK | \
				 SK_RLMT_CHECK_REM_LINK)
#endif	/* RLMT_CHECK_REMOTE */

/* ----- RLMT modes ----- */

/* Check Link State. */
#define SK_RLMT_MODE_CLS	(SK_RLMT_CHECK_LINK)

/* Check Local Ports: check other links on the same adapter. */
#define SK_RLMT_MODE_CLP	(SK_RLMT_CHECK_LINK | \
				 SK_RLMT_CHECK_LOC_LINK)

/* Check Local Ports and Segmentation Status. */
#define SK_RLMT_MODE_CLPSS	(SK_RLMT_CHECK_LINK | \
				 SK_RLMT_CHECK_LOC_LINK | \
				 SK_RLMT_CHECK_SEG)

#ifdef RLMT_CHECK_REMOTE
/* Check Local and Remote Ports: check links (local or remote). */
	Name of define TBD!
#define SK_RLMT_MODE_CRP	(SK_RLMT_CHECK_LINK | \
				 SK_RLMT_CHECK_LOC_LINK | \
				 SK_RLMT_CHECK_REM_LINK)

/* Check Local and Remote Ports and Segmentation Status. */
	Name of define TBD!
#define SK_RLMT_MODE_CRPSS	(SK_RLMT_CHECK_LINK | \
				 SK_RLMT_CHECK_LOC_LINK | \
				 SK_RLMT_CHECK_REM_LINK | \
				 SK_RLMT_CHECK_SEG)
#endif	/* RLMT_CHECK_REMOTE */

/* ----- RLMT lookahead result bits ----- */

#define SK_RLMT_RX_RLMT		1	/* Give packet to RLMT. */
#define SK_RLMT_RX_PROTOCOL	2	/* Give packet to protocol. */

/* Macros */

#if 0
SK_AC		*pAC		/* adapter context */
SK_U32		PortIdx		/* receiving port */
unsigned	PacketLength	/* received packet's length */
SK_BOOL		IsBc		/* Flag: broadcast received */
unsigned	*pOffset	/* Result: offset of bytes to present
				   to SK_RLMT_LOOKAHEAD */
unsigned	*pNumBytes	/* Result: #Bytes to present
				   to SK_RLMT_LOOKAHEAD */
#endif	/* 0 */


#define SK_RLMT_PRE_LOOKAHEAD(pAC,PortIdx,PacketLength,IsBc,pOffset,pNumBytes) { \
	SK_AC	*_pAC; \
	SK_U32	_PortIdx; \
	_pAC = (pAC); \
	_PortIdx = (SK_U32)(PortIdx); \
	_pAC->Rlmt.Port[_PortIdx].PacketsRx++; \
	_pAC->Rlmt.Port[_PortIdx].PacketsPerTimeSlot++; \
	if ((IsBc) && _pAC->Rlmt.RlmtMode != SK_RLMT_MODE_CLS) { \
		*(pOffset) = 6; \
		*(pNumBytes) = 6; \
	} \
	else { \
		*(pOffset) = 0; \
		if ((PacketLength) > SK_RLMT_MAX_TX_BUF_SIZE) { \
			_pAC->Rlmt.Port[_PortIdx].DataPacketsPerTimeSlot++; \
			*(pNumBytes) = 0; \
		} \
		else { \
			*(pNumBytes) = 6; \
		} \
	} \
}

#if 0
SK_AC		*pAC		/* adapter context */
SK_U32		PortIdx		/* receiving port */
SK_U8		*pLaPacket,	/* received packet's data (points to pOffset) */
SK_BOOL		IsBc		/* Flag: broadcast received */
SK_BOOL		IsMc		/* Flag: multicast received */
unsigned	*pForRlmt	/* Result: bits SK_RLMT_RX_RLMT,
				   SK_RLMT_RX_PROTOCOL */
SK_RLMT_LOOKAHEAD() expects *pNumBytes from
packet offset *pOffset (s.a.) at *pLaPacket.

If you use SK_RLMT_LOOKAHEAD in a path where you already know if the packet is
BC, MC, or UC, you should use constants for IsBc and IsMc, so that your compiler
can trash unneeded parts of the if construction.
#endif	/* 0 */
#define SK_RLMT_LOOKAHEAD(pAC,PortIdx,pLaPacket,IsBc,IsMc,pForRlmt) { \
	SK_AC	*_pAC; \
	SK_U32	_PortIdx; \
	SK_U8	*_pLaPacket; \
	_pAC = (pAC); \
	_PortIdx = (SK_U32)(PortIdx); \
	_pLaPacket = (SK_U8 *)(pLaPacket); \
	if (IsBc) {\
		if (!SK_ADDR_EQUAL( \
			_pLaPacket, \
			_pAC->Addr.CurrentMacAddress.a)) { \
			_pAC->Rlmt.Port[_PortIdx].BcTimeStamp = \
				SkOsGetTime(_pAC); \
		} \
		_pAC->Rlmt.Port[_PortIdx].DataPacketsPerTimeSlot++; \
		*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
	} \
	else if (IsMc) { \
		if (SK_ADDR_EQUAL(_pLaPacket, BridgeMcAddr.a)) { \
			_pAC->Rlmt.Port[_PortIdx].BpduPacketsPerTimeSlot++; \
			if (_pAC->Rlmt.RlmtMode & SK_RLMT_CHECK_SEG) { \
				*(pForRlmt) = SK_RLMT_RX_RLMT | \
					SK_RLMT_RX_PROTOCOL; \
			} \
			else { \
				*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
			} \
		} \
		else if (SK_ADDR_EQUAL(_pLaPacket, SkRlmtMcAddr.a)) { \
			*(pForRlmt) = SK_RLMT_RX_RLMT; \
		} \
		else { \
			_pAC->Rlmt.Port[_PortIdx].DataPacketsPerTimeSlot++; \
			*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
		} \
	} \
	else { \
		if (SK_ADDR_EQUAL( \
			_pLaPacket, \
			_pAC->Addr.Port[_PortIdx].CurrentMacAddress.a)) { \
			*(pForRlmt) = SK_RLMT_RX_RLMT; \
		} \
		else { \
			_pAC->Rlmt.Port[_PortIdx].DataPacketsPerTimeSlot++; \
			*(pForRlmt) = SK_RLMT_RX_PROTOCOL; \
		} \
	} \
}

#ifdef SK_RLMT_FAST_LOOKAHEAD
Error: SK_RLMT_FAST_LOOKAHEAD no longer used. Use new macros for lookahead.
#endif	/* SK_RLMT_FAST_LOOKAHEAD */

/* typedefs *******************************************************************/

typedef struct s_RootId {
	SK_U8		Id[8];		/* Root Bridge Id. */
} SK_RLMT_ROOT_ID;

typedef struct s_port {
	SK_MAC_ADDR	CheckAddr;
	SK_BOOL		SuspectTx;
} SK_PORT_CHECK;

typedef struct s_RlmtPort {

/* ----- Public part (read-only) ----- */

	SK_U8		PortState;	/* Current state of this port. */

	/* For PNMI */

	SK_BOOL		LinkDown;
	SK_BOOL		PortDown;

	SK_U64		TxHelloCts;
	SK_U64		RxHelloCts;
	SK_U64		TxSpHelloReqCts;
	SK_U64		RxSpHelloCts;

/* ----- Private part ----- */

	SK_BOOL		PortStarted;		/* Port is started. */
	SK_BOOL		PortNoRx;		/* NoRx for >= 1 time slot. */
	SK_U32		CheckingState;		/* Checking State. */

	SK_U64		PacketsRx;		/* Total packets received. */
	SK_U32		PacketsPerTimeSlot;	/* Packets rxed between TOs. */
	SK_U32		DataPacketsPerTimeSlot;	/* Data packets ... */
#if 0
	SK_U32		RlmtAcksPerTimeSlot;	/* RLMT Acks rxed in TS. */
	SK_U32		RlmtChksPerTimeSlot;	/* RLMT Chks rxed in TS. */
#endif	/* 0 */
	SK_U32		BpduPacketsPerTimeSlot;	/* BPDU packets rxed in TS. */
	SK_U64		BcTimeStamp;		/* Time of last BC receive. */
	SK_U64		GuTimeStamp;		/* Time of entering GOING_UP. */

	SK_TIMER	UpTimer;		/* Timer struct Link/Port up. */
	SK_TIMER	DownRxTimer;		/* Timer struct down rx. */
	SK_TIMER	DownTxTimer;		/* Timer struct down tx. */

	SK_U8		Random[4];		/* Random value. */
	unsigned	PortsChecked;		/* #ports checked. */
	unsigned	PortsSuspect;		/* #ports checked that are s. */
	SK_PORT_CHECK	PortCheck[1];
/*	SK_PORT_CHECK	PortCheck[SK_MAX_MACS - 1]; */

	SK_BOOL		RootIdSet;
	SK_RLMT_ROOT_ID	Root;			/* Root Bridge Id. */
} SK_RLMT_PORT;

#ifdef SK_RLMT_MBUF_PRIVATE
typedef struct s_RlmtMbuf {
	some content
} SK_RLMT_MBUF;
#endif	/* SK_RLMT_MBUF_PRIVATE */

#ifdef SK_LA_INFO
typedef struct s_Rlmt_PacketInfo {
	unsigned	PacketLength;	/* Length of packet. */
	unsigned	PacketType;	/* Directed/Multicast/Broadcast. */
} SK_RLMT_PINFO;
#endif	/* SK_LA_INFO */

typedef struct s_Rlmt {

/* ----- Public part (read-only) ----- */

	SK_U8		RlmtState;		/* Current RLMT state. */
	SK_RLMT_PORT	Port[SK_MAX_MACS];	/* Array of available ports. */
	SK_U32		PrefPort;		/* Preferred port. */

	/* For PNMI */

	SK_U32		RlmtMode;		/* Check ... */
	SK_U32		MacActive;		/* Active port. */
	SK_U32		MacPreferred;		/* 0xFFFFFFFF: Automatic. */

/* ----- Private part ----- */

	int		LinksUp;		/* #Links up. */
	int		PortsUp;		/* #Ports up. */
	SK_U32		TimeoutValue;		/* RLMT timeout value. */
	SK_TIMER	LocTimer;		/* Timer struct. */

	SK_U32		CheckingState;		/* Checking State. */
	SK_BOOL		RootIdSet;
	SK_RLMT_ROOT_ID	Root;			/* Root Bridge Id. */
	SK_TIMER	SegTimer;		/* Timer struct. */
} SK_RLMT;

extern	SK_MAC_ADDR	BridgeMcAddr;
extern	SK_MAC_ADDR     SkRlmtMcAddr;

/* function prototypes ********************************************************/


#ifndef SK_KR_PROTO

/* Functions provided by SkRlmt */

/* ANSI/C++ compliant function prototypes */

extern	void	SkRlmtInit(
	SK_AC	*pAC,
	SK_IOC	IoC,
	int	Level);

#ifdef SK_RLMT_SLOW_LOOKAHEAD
extern	SK_BOOL	SkRlmtLookaheadPacket(
	SK_AC		*pAC,
	SK_U32		PortIdx,
	SK_U8		*pLaPacket,
	unsigned	PacketLength,
	unsigned	LaLength);
#endif	/* SK_RLMT_SLOW_LOOKAHEAD */

extern	int	SkRlmtEvent(
	SK_AC		*pAC,
	SK_IOC		IoC,
	SK_U32		Event,
	SK_EVPARA	Para);

#else	/* defined(SK_KR_PROTO)) */

/* Non-ANSI/C++ compliant function prototypes */

xxxx	/* not supported yet - force error */

#endif	/* defined(SK_KR_PROTO)) */


#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* __INC_SKRLMT_H */
