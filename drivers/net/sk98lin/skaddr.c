/******************************************************************************
 *
 * Name:	skaddr.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.36 $
 * Date:	$Date: 2000/08/07 11:10:39 $
 * Purpose:	Manage Addresses (Multicast and Unicast) and Promiscuous Mode.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2000 SysKonnect,
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
 *	$Log: skaddr.c,v $
 *	Revision 1.36  2000/08/07 11:10:39  rassmann
 *	Editorial changes.
 *	
 *	Revision 1.35  2000/05/04 09:38:41  rassmann
 *	Editorial changes.
 *	Corrected multicast address hashing.
 *	
 *	Revision 1.34  1999/11/22 13:23:44  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.33  1999/05/28 10:56:06  rassmann
 *	Editorial changes.
 *	
 *	Revision 1.32  1999/03/31 10:59:20  rassmann
 *	Returning Success instead of DupAddr if address shall be overridden
 *	with same value.
 *	
 *	Revision 1.31  1999/01/14 16:18:17  rassmann
 *	Corrected multicast initialization.
 *	
 *	Revision 1.30  1999/01/04 10:30:35  rassmann
 *	SkAddrOverride only possible after SK_INIT_IO phase.
 *	
 *	Revision 1.29  1998/12/29 13:13:10  rassmann
 *	An address override is now preserved in the SK_INIT_IO phase.
 *	All functions return an int now.
 *	Extended parameter checking.
 *	
 *	Revision 1.28  1998/12/01 11:45:53  rassmann
 *	Code cleanup.
 *	
 *	Revision 1.27  1998/12/01 09:22:49  rassmann
 *	SkAddrMcAdd and SkAddrMcUpdate returned SK_MC_FILTERING_INEXACT
 *	too often.
 *	
 *	Revision 1.26  1998/11/24 12:39:44  rassmann
 *	Reserved multicast entry for BPDU address.
 *	13 multicast entries left for protocol.
 *	
 *	Revision 1.25  1998/11/17 16:54:23  rassmann
 *	Using exact match for up to 14 multicast addresses.
 *	Still receiving all multicasts if more addresses are added.
 *	
 *	Revision 1.24  1998/11/13 17:24:31  rassmann
 *	Changed return value of SkAddrOverride to int.
 *	
 *	Revision 1.23  1998/11/13 16:56:18  rassmann
 *	Added macro SK_ADDR_COMPARE.
 *	Changed return type of SkAddrOverride to SK_BOOL.
 *	
 *	Revision 1.22  1998/11/04 17:06:17  rassmann
 *	Corrected McUpdate and PromiscuousChange functions.
 *	
 *	Revision 1.21  1998/10/29 14:34:04  rassmann
 *	Clearing SK_ADDR struct at startup.
 *	
 *	Revision 1.20  1998/10/28 18:16:34  rassmann
 *	Avoiding I/Os before SK_INIT_RUN level.
 *	Aligning InexactFilter.
 *	
 *	Revision 1.19  1998/10/28 11:29:28  rassmann
 *	Programming physical address in SkAddrMcUpdate.
 *	Corrected programming of exact match entries.
 *	
 *	Revision 1.18  1998/10/28 10:34:48  rassmann
 *	Corrected reading of physical addresses.
 *	
 *	Revision 1.17  1998/10/28 10:26:13  rassmann
 *	Getting ports' current MAC addresses from EPROM now.
 *	Added debug output.
 *	
 *	Revision 1.16  1998/10/27 16:20:12  rassmann
 *	Reading MAC address byte by byte.
 *	
 *	Revision 1.15  1998/10/22 11:39:09  rassmann
 *	Corrected signed/unsigned mismatches.
 *	
 *	Revision 1.14  1998/10/19 17:12:35  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.13  1998/10/19 17:02:19  rassmann
 *	Now reading permanent MAC addresses from CRF.
 *	
 *	Revision 1.12  1998/10/15 15:15:48  rassmann
 *	Changed Flags Parameters from SK_U8 to int.
 *	Checked with lint.
 *	
 *	Revision 1.11  1998/09/24 19:15:12  rassmann
 *	Code cleanup.
 *	
 *	Revision 1.10  1998/09/18 20:18:54  rassmann
 *	Added HW access.
 *	Implemented swapping.
 *	
 *	Revision 1.9  1998/09/16 11:32:00  rassmann
 *	Including skdrv1st.h again. :(
 *	
 *	Revision 1.8  1998/09/16 11:09:34  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.7  1998/09/14 17:06:34  rassmann
 *	Minor changes.
 *	
 *	Revision 1.6  1998/09/07 08:45:41  rassmann
 *	Syntax corrections.
 *	
 *	Revision 1.5  1998/09/04 19:40:19  rassmann
 *	Interface enhancements.
 *	
 *	Revision 1.4  1998/09/04 12:14:12  rassmann
 *	Interface cleanup.
 *	
 *	Revision 1.3  1998/09/02 16:56:40  rassmann
 *	Updated interface.
 *	
 *	Revision 1.2  1998/08/27 14:26:09  rassmann
 *	Updated interface.
 *	
 *	Revision 1.1  1998/08/21 08:30:22  rassmann
 *	First public version.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Description:
 *
 * This module is intended to manage multicast addresses, address override,
 * and promiscuous mode on GEnesis adapters.
 *
 * Address Layout:
 *	port address:		physical MAC address
 *	1st exact match:	logical MAC address
 *	2nd exact match:	RLMT multicast
 *	exact match 3-13:	OS-specific multicasts
 *
 * Include File Hierarchy:
 *
 *	"skdrv1st.h"
 *	"skdrv2nd.h"
 *
 ******************************************************************************/

#ifndef	lint
static const char SysKonnectFileId[] =
	"@(#) $Id: skaddr.c,v 1.36 2000/08/07 11:10:39 rassmann Exp $ (C) SysKonnect.";
#endif	/* !defined(lint) */

#define __SKADDR_C

#ifdef __cplusplus
#error C++ is not yet supported.
extern "C" {
#endif	/* cplusplus */

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

/* defines ********************************************************************/


#define CRC32_POLY	0xEDB88320UL	/* CRC32-Poly - XMAC: Little Endian */
#define HASH_BITS	6				/* #bits in hash */
#define	SK_MC_BIT	0x01

/* Error numbers and messages. */

#define SKERR_ADDR_E001		(SK_ERRBASE_ADDR + 0)
#define SKERR_ADDR_E001MSG	"Bad Flags."
#define SKERR_ADDR_E002		(SKERR_ADDR_E001 + 1)
#define SKERR_ADDR_E002MSG	"New Error."

/* typedefs *******************************************************************/

/* None. */

/* global variables ***********************************************************/

/* 64-bit hash values with all bits set. */

SK_U16	OnesHash[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

/* local variables ************************************************************/

#ifdef DEBUG
static int	Next0[SK_MAX_MACS] = {0, 0};
#endif	/* DEBUG */

/* functions ******************************************************************/

/******************************************************************************
 *
 *	SkAddrInit - initialize data, set state to init
 *
 * Description:
 *
 *	SK_INIT_DATA
 *	============
 *
 *	This routine clears the multicast tables and resets promiscuous mode.
 *	Some entries are reserved for the "logical MAC address", the
 *	SK-RLMT multicast address, and the BPDU multicast address.
 *
 *
 *	SK_INIT_IO
 *	==========
 *
 *	All permanent MAC addresses are read from EPROM.
 *	If the current MAC addresses are not already set in software,
 *	they are set to the values of the permanent addresses.
 *	The current addresses are written to the corresponding XMAC.
 *
 *
 *	SK_INIT_RUN
 *	===========
 *
 *	Nothing.
 *
 * Context:
 *	init, pageable
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 */
int	SkAddrInit(
SK_AC	*pAC,	/* the adapter context */
SK_IOC	IoC,	/* I/O context */
int		Level)	/* initialization level */
{
	int				j;
	SK_U32			i;
	SK_U8			*InAddr;
	SK_U16			*OutAddr;
	SK_ADDR_PORT	*pAPort;

	switch (Level) {
	case SK_INIT_DATA:
		SK_MEMSET((char *)&pAC->Addr, 0, sizeof(SK_ADDR));

		for (i = 0; i < SK_MAX_MACS; i++) {
			pAPort = &pAC->Addr.Port[i];
			pAPort->PromMode = SK_PROM_MODE_NONE;

			pAPort->FirstExactMatchRlmt	= SK_ADDR_FIRST_MATCH_RLMT;
			pAPort->FirstExactMatchDrv	= SK_ADDR_FIRST_MATCH_DRV;
			pAPort->NextExactMatchRlmt	= SK_ADDR_FIRST_MATCH_RLMT;
			pAPort->NextExactMatchDrv	= SK_ADDR_FIRST_MATCH_DRV;

#if 0
			/* Don't do this here ... */

			/* Reset Promiscuous mode. */
			(void)SkAddrPromiscuousChange(
				pAC,
				IoC,
				i,
				SK_PROM_MODE_NONE);
#endif	/* 0 */
		}
		
#ifdef DEBUG
		for (i = 0; i < SK_MAX_MACS; i++) {
			if (pAC->Addr.Port[i].NextExactMatchRlmt <
				SK_ADDR_FIRST_MATCH_RLMT) {
				Next0[i] |= 4;
			}
		}
#endif	/* DEBUG */

		/* pAC->Addr.InitDone = SK_INIT_DATA; */
		break;

	case SK_INIT_IO:
		pAC->Addr.ActivePort = pAC->Rlmt.MacActive;
		
#ifdef DEBUG
		for (i = 0; i < SK_MAX_MACS; i++) {
			if (pAC->Addr.Port[i].NextExactMatchRlmt <
				SK_ADDR_FIRST_MATCH_RLMT) {
				Next0[i] |= 8;
			}
		}
#endif	/* DEBUG */

		/* Read permanent logical MAC address from Control Register File. */
		for (j = 0; j < SK_MAC_ADDR_LEN; j++) {
			InAddr = (SK_U8 *)&pAC->Addr.PermanentMacAddress.a[j];
			SK_IN8(IoC, B2_MAC_1 + j, InAddr);
		}

		if (!pAC->Addr.CurrentMacAddressSet) {
			/*
			 * Set the current logical MAC address
			 * to the permanent one.
			 */
			pAC->Addr.CurrentMacAddress =
				pAC->Addr.PermanentMacAddress;
			pAC->Addr.CurrentMacAddressSet = SK_TRUE;
		}

		/* Set the current logical MAC address. */
		pAC->Addr.Port[pAC->Addr.ActivePort].Exact[0] =
			pAC->Addr.CurrentMacAddress;

#ifdef xDEBUG
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_ADDR,
			SK_DBGCAT_INIT,
			("Permanent MAC Address: %02X %02X %02X %02X %02X %02X\n",
				pAC->Addr.PermanentMacAddress.a[0],
				pAC->Addr.PermanentMacAddress.a[1],
				pAC->Addr.PermanentMacAddress.a[2],
				pAC->Addr.PermanentMacAddress.a[3],
				pAC->Addr.PermanentMacAddress.a[4],
				pAC->Addr.PermanentMacAddress.a[5]))
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_ADDR,
			SK_DBGCAT_INIT,
			("Logical MAC Address: %02X %02X %02X %02X %02X %02X\n",
				pAC->Addr.CurrentMacAddress.a[0],
				pAC->Addr.CurrentMacAddress.a[1],
				pAC->Addr.CurrentMacAddress.a[2],
				pAC->Addr.CurrentMacAddress.a[3],
				pAC->Addr.CurrentMacAddress.a[4],
				pAC->Addr.CurrentMacAddress.a[5]))
#endif	/* DEBUG */

#if 0
		/* Don't do this here ... */

		(void)SkAddrMcUpdate(pAC, IoC, pAC->Addr.ActivePort);
#endif	/* 0 */

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			pAPort = &pAC->Addr.Port[i];

			/*
			 * Read permanent port addresses from
			 * Control Register File.
			 */
			for (j = 0; j < SK_MAC_ADDR_LEN; j++) {
				InAddr = (SK_U8 *)&pAPort->PermanentMacAddress.a[j];
				SK_IN8(IoC, B2_MAC_2 + 8 * i + j, InAddr);
			}

			if (!pAPort->CurrentMacAddressSet) {
				/*
				 * Set the current and previous physical
				 * MAC address of this port to its permanent
				 * MAC address.
				 */
				pAPort->CurrentMacAddress = pAPort->PermanentMacAddress;
				pAPort->PreviousMacAddress = pAPort->PermanentMacAddress;
				pAPort->CurrentMacAddressSet = SK_TRUE;
			}

			/* Set port's current MAC addresses. */
			OutAddr = (SK_U16 *)&pAPort->CurrentMacAddress.a[0];
			XM_OUTADDR(IoC, i, XM_SA, OutAddr);

#ifdef xDEBUG
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_ADDR,
				SK_DBGCAT_INIT,
				("Permanent Physical MAC Address: %02X %02X %02X %02X %02X %02X\n",
					pAPort->PermanentMacAddress.a[0],
					pAPort->PermanentMacAddress.a[1],
					pAPort->PermanentMacAddress.a[2],
					pAPort->PermanentMacAddress.a[3],
					pAPort->PermanentMacAddress.a[4],
					pAPort->PermanentMacAddress.a[5]))
			SK_DBG_MSG(
				pAC,
				SK_DBGMOD_ADDR,
				SK_DBGCAT_INIT,
				("Phsical MAC Address: %02X %02X %02X %02X %02X %02X\n",
					pAPort->CurrentMacAddress.a[0],
					pAPort->CurrentMacAddress.a[1],
					pAPort->CurrentMacAddress.a[2],
					pAPort->CurrentMacAddress.a[3],
					pAPort->CurrentMacAddress.a[4],
					pAPort->CurrentMacAddress.a[5]))
#endif	/* DEBUG */
		}
		/* pAC->Addr.InitDone = SK_INIT_IO; */
		break;

	case SK_INIT_RUN:
#ifdef DEBUG
		for (i = 0; i < SK_MAX_MACS; i++) {
			if (pAC->Addr.Port[i].NextExactMatchRlmt <
				SK_ADDR_FIRST_MATCH_RLMT) {
				Next0[i] |= 16;
			}
		}
#endif	/* DEBUG */

		/* pAC->Addr.InitDone = SK_INIT_RUN; */
		break;

	default:	/* error */
		break;
	}

	return (SK_ADDR_SUCCESS);
}	/* SkAddrInit */


/******************************************************************************
 *
 *	SkAddrMcClear - clear the multicast table
 *
 * Description:
 *	This routine clears the multicast table
 *	(either entry 2 or entries 3-16 and InexactFilter) of the given port.
 *	If not suppressed by Flag SK_MC_SW_ONLY, the hardware is updated
 *	immediately.
 *
 * Context:
 *	runtime, pageable
 *	may be called starting with SK_INIT_DATA with flag SK_MC_SW_ONLY
 *	may be called after SK_INIT_IO without limitation
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrMcClear(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortIdx,	/* Index of affected port */
int		Flags)		/* permanent/non-perm, sw-only */
{
	int i;

	if (PortIdx >= (SK_U32)pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	if (Flags & SK_ADDR_PERMANENT) {

		/* Clear RLMT multicast addresses. */
		pAC->Addr.Port[PortIdx].NextExactMatchRlmt = SK_ADDR_FIRST_MATCH_RLMT;
	}
	else {	/* not permanent => DRV */

		/* Clear InexactFilter. */

		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortIdx].InexactFilter.Bytes[i] = 0;
		}

		/* Clear DRV multicast addresses. */

		pAC->Addr.Port[PortIdx].NextExactMatchDrv = SK_ADDR_FIRST_MATCH_DRV;
	}

	if (!(Flags & SK_MC_SW_ONLY)) {
		(void)SkAddrMcUpdate(pAC, IoC, PortIdx);
	}

	return (SK_ADDR_SUCCESS);
}	/* SkAddrMcClear */

#ifndef SK_ADDR_CHEAT

/******************************************************************************
 *
 *	SkCrc32McHash - hash multicast address
 *
 * Description:
 *	This routine computes the hash value for a multicast address.
 *
 * Notes:
 *	The code was adapted from the XaQti data sheet.
 *
 * Context:
 *	runtime, pageable
 *
 * Returns:
 *	Hash value of multicast address.
 */
unsigned SkCrc32McHash(
unsigned char *pMc)	/* Multicast address */
{
	unsigned Idx;
	unsigned Bit;
	unsigned Data;
	unsigned Crc;

	Crc = 0xFFFFFFFFUL;
	for (Idx = 0; Idx < SK_MAC_ADDR_LEN; Idx++) {
		Data = *pMc++;
		for (Bit = 0; Bit < 8; Bit++, Data >>= 1) {
			Crc = (Crc >> 1) ^ (((Crc ^ Data) & 1) ? CRC32_POLY : 0);
		}
	}

	return (Crc & ((1 << HASH_BITS) - 1));
}	/* SkCrc32McHash */

#endif	/* not SK_ADDR_CHEAT */

/******************************************************************************
 *
 *	SkAddrMcAdd - add a multicast address to a port
 *
 * Description:
 *	This routine enables reception for a given address on the given port.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 *	In the current version, only RLMT may add addresses to the non-active
 *	port.
 *
 *	The multicast bit is only checked if there are no free exact match
 *	entries.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_DATA
 *
 * Returns:
 *	SK_MC_FILTERING_EXACT
 *	SK_MC_FILTERING_INEXACT
 *	SK_MC_ILLEGAL_ADDRESS
 *	SK_MC_ILLEGAL_PORT
 *	SK_MC_RLMT_OVERFLOW
 */
int	SkAddrMcAdd(
SK_AC		*pAC,		/* adapter context */
SK_IOC		IoC,		/* I/O context */
SK_U32		PortIdx,	/* Port Index */
SK_MAC_ADDR	*pMc,		/* multicast address to be added */
int			Flags)		/* permanent/non-permanent */
{
	int	i;
	SK_U8	Inexact;
#ifndef SK_ADDR_CHEAT
	unsigned HashBit;
#endif	/* !defined(SK_ADDR_CHEAT) */

	if (PortIdx >= (SK_U32)pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	if (Flags & SK_ADDR_PERMANENT) {
#ifdef DEBUG
		if (pAC->Addr.Port[PortIdx].NextExactMatchRlmt <
			SK_ADDR_FIRST_MATCH_RLMT) {
			Next0[PortIdx] |= 1;
			return (SK_MC_RLMT_OVERFLOW);
		}
#endif	/* DEBUG */
		
		if (pAC->Addr.Port[PortIdx].NextExactMatchRlmt >
			SK_ADDR_LAST_MATCH_RLMT) {
			return (SK_MC_RLMT_OVERFLOW);
		}

		/* Set an RLMT multicast address. */

		pAC->Addr.Port[PortIdx].Exact[
			pAC->Addr.Port[PortIdx].NextExactMatchRlmt++] = *pMc;

		return (SK_MC_FILTERING_EXACT);
	}

	/* Not PERMANENT => DRV */
	if (PortIdx != pAC->Addr.ActivePort) {

		/* Only RLMT is allowed to do this. */
		return (SK_MC_ILLEGAL_PORT);
	}

#ifdef DEBUG
	if (pAC->Addr.Port[PortIdx].NextExactMatchDrv <
		SK_ADDR_FIRST_MATCH_DRV) {
			Next0[PortIdx] |= 2;
		return (SK_MC_RLMT_OVERFLOW);
	}
#endif	/* DEBUG */
	
	if (pAC->Addr.Port[PortIdx].NextExactMatchDrv <= SK_ADDR_LAST_MATCH_DRV) {

		/* Set exact match entry. */
		pAC->Addr.Port[PortIdx].Exact[
			pAC->Addr.Port[PortIdx].NextExactMatchDrv++] = *pMc;

		/* Clear InexactFilter. */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortIdx].InexactFilter.Bytes[i] = 0;
		}
	}
	else {
		if (!(pMc->a[0] & SK_MC_BIT)) {
			/*
			 * Hashing only possible with
			 * multicast addresses.
			 */
			return (SK_MC_ILLEGAL_ADDRESS);
		}
#ifndef SK_ADDR_CHEAT
		/* Compute hash value of address. */
		HashBit = 63 - SkCrc32McHash(&pMc->a[0]);

		/* Add bit to InexactFilter. */
		pAC->Addr.Port[PortIdx].InexactFilter.Bytes[HashBit / 8] |=
			1 << (HashBit % 8);
#else	/* SK_ADDR_CHEAT */
		/* Set all bits in InexactFilter. */
		for (i = 0; i < 8; i++) {
			pAC->Addr.Port[PortIdx].InexactFilter.Bytes[i] = 0xFF;
		}
#endif	/* SK_ADDR_CHEAT */
	}

	for (Inexact = 0, i = 0; i < 8; i++) {
		Inexact |= pAC->Addr.Port[PortIdx].InexactFilter.Bytes[i];
	}

	if (Inexact == 0 && pAC->Addr.Port[PortIdx].PromMode == 0) {
		return (SK_MC_FILTERING_EXACT);
	}
	else {
		return (SK_MC_FILTERING_INEXACT);
	}
}	/* SkAddrMcAdd */


/******************************************************************************
 *
 *	SkAddrMcUpdate - update the HW MC address table and set the MAC address
 *
 * Description:
 *	This routine enables reception of the addresses contained in a local
 *	table for a given port.
 *	It also programs the port's current physical MAC address.
 *
 * Notes:
 *	The return code is only valid for SK_PROM_MODE_NONE.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_MC_FILTERING_EXACT
 *	SK_MC_FILTERING_INEXACT
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrMcUpdate(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* I/O context */
SK_U32	PortIdx)	/* Port Index */
{
	SK_U32			i;
	SK_U8			Inexact;
	SK_U16			*OutAddr;
	SK_U16			LoMode;		/* Lower 16 bits of XMAC Mode Reg. */
	SK_ADDR_PORT	*pAPort;

	if (PortIdx >= (SK_U32)pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	SK_DBG_MSG(
		pAC,
		SK_DBGMOD_ADDR,
		SK_DBGCAT_CTRL,
		("SkAddrMcUpdate on Port %u.\n", PortIdx))
	
	pAPort = &pAC->Addr.Port[PortIdx];

#ifdef DEBUG
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_ADDR,
			SK_DBGCAT_CTRL,
			("Next0 on Port %d: %d\n", PortIdx, Next0[PortIdx]))
#endif	/* DEBUG */

	for (i = 0;		/* Also program the logical MAC address. */
		i < pAPort->NextExactMatchRlmt;
		i++) {

		/* Set exact match address i on HW. */
		OutAddr = (SK_U16 *)&pAPort->Exact[i].a[0];
		XM_OUTADDR(IoC, PortIdx, XM_EXM(i), OutAddr);
	}

	/* Clear other permanent exact match addresses on HW. */
	if (pAPort->NextExactMatchRlmt <= SK_ADDR_LAST_MATCH_RLMT) {
		SkXmClrExactAddr(
			pAC,
			IoC,
			PortIdx,
			pAPort->NextExactMatchRlmt,
			SK_ADDR_LAST_MATCH_RLMT);
	}

	for (i = pAPort->FirstExactMatchDrv; i < pAPort->NextExactMatchDrv; i++) {
		OutAddr = (SK_U16 *)&pAPort->Exact[i].a[0];
		XM_OUTADDR(IoC, PortIdx, XM_EXM(i), OutAddr);
	}

	/* Clear other non-permanent exact match addresses on HW. */
	if (pAPort->NextExactMatchDrv <= SK_ADDR_LAST_MATCH_DRV) {
		SkXmClrExactAddr(
			pAC,
			IoC,
			PortIdx,
			pAPort->NextExactMatchDrv,
			SK_ADDR_LAST_MATCH_DRV);
	}

	for (Inexact = 0, i = 0; i < 8; i++) {
		Inexact |= pAPort->InexactFilter.Bytes[i];
	}

	if (pAPort->PromMode & SK_PROM_MODE_ALL_MC) {
		/* Set all bits in 64-bit hash register. */
		XM_OUTHASH(IoC, PortIdx, XM_HSM, &OnesHash);

		/* Set bit 15 in mode register. */
		XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);
		LoMode |= XM_MD_ENA_HSH;
		XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
	}
	else if (Inexact != 0) {
		/* Set 64-bit hash register to InexactFilter. */
		XM_OUTHASH(IoC, PortIdx, XM_HSM, &pAPort->InexactFilter.Bytes[0]);

		/* Set bit 15 in mode register. */
		XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);
		LoMode |= XM_MD_ENA_HSH;
		XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
	}
	else {
		/* Clear bit 15 in mode register. */
		XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);
		LoMode &= ~XM_MD_ENA_HSH;
		XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
	}

	if (pAPort->PromMode != SK_PROM_MODE_NONE) {
		(void)SkAddrPromiscuousChange(pAC, IoC, PortIdx, pAPort->PromMode);
	}

	/* Set port's current MAC address. */
	OutAddr = (SK_U16 *)&pAPort->CurrentMacAddress.a[0];
	XM_OUTADDR(IoC, PortIdx, XM_SA, OutAddr);

#ifdef DEBUG
	for (i = 0;		/* Also program the logical MAC address. */
		i < pAPort->NextExactMatchRlmt;
		i++) {
		SK_U8		InAddr8[6];
		SK_U16		*InAddr;

		/* Get exact match address i from port PortIdx. */
		InAddr = (SK_U16 *)&InAddr8[0];
		XM_INADDR(IoC, PortIdx, XM_EXM(i), InAddr);
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_RLMT,
			SK_DBGCAT_CTRL,
			("MC address %d on Port %u: %02x %02x %02x %02x %02x %02x --  %02x %02x %02x %02x %02x %02x.\n",
				i,
				PortIdx,
				InAddr8[0],
				InAddr8[1],
				InAddr8[2],
				InAddr8[3],
				InAddr8[4],
				InAddr8[5],
				pAPort->Exact[i].a[0],
				pAPort->Exact[i].a[1],
				pAPort->Exact[i].a[2],
				pAPort->Exact[i].a[3],
				pAPort->Exact[i].a[4],
				pAPort->Exact[i].a[5]))
	}
#endif	/* DEBUG */		

	/* Determine return value. */
	if (Inexact == 0 && pAPort->PromMode == 0) {
		return (SK_MC_FILTERING_EXACT);
	}
	else {
		return (SK_MC_FILTERING_INEXACT);
	}
}	/* SkAddrMcUpdate */


/******************************************************************************
 *
 *	SkAddrOverride - override a port's MAC address
 *
 * Description:
 *	This routine overrides the MAC address of one port.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS if successful.
 *	SK_ADDR_DUPLICATE_ADDRESS if duplicate MAC address.
 *	SK_ADDR_MULTICAST_ADDRESS if multicast or broadcast address.
 *	SK_ADDR_TOO_EARLY if SK_INIT_IO was not executed before.
 */
int	SkAddrOverride(
SK_AC		*pAC,		/* adapter context */
SK_IOC		IoC,		/* I/O context */
SK_U32		PortIdx,	/* Port Index */
SK_MAC_ADDR	*pNewAddr,	/* new MAC address */
int			Flags)		/* logical/physical MAC address */
{
	SK_U32		i;
	SK_U16		*OutAddr;
	SK_EVPARA	Para;
#if 0
	SK_MAC_ADDR	NewAddr;	/* new MAC address */
	SK_U8		AddrBits;
#endif	/* 0 */

	if (PortIdx >= (SK_U32)pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	if (pNewAddr->a[0] & SK_MC_BIT) {
		return (SK_ADDR_MULTICAST_ADDRESS);
	}

#if 0
DANGEROUS!
	if (Flags & SK_ADDR_PHYSICAL_ADDRESS) {	/* Physical address. */
		if (!pAC->Addr.Port[PortIdx].CurrentMacAddressSet) {
			pAC->Addr.Port[PortIdx].PreviousMacAddress = *pNewAddr;
			pAC->Addr.Port[PortIdx].CurrentMacAddress = *pNewAddr;
			pAC->Addr.Port[PortIdx].CurrentMacAddressSet = SK_TRUE;
			return (SK_ADDR_SUCCESS);
		}
	}
	else {
		if (!pAC->Addr.CurrentMacAddressSet) {
			pAC->Addr.CurrentMacAddress = *pNewAddr;
			pAC->Addr.CurrentMacAddressSet = SK_TRUE;
			return (SK_ADDR_SUCCESS);
		}
	}
DANGEROUS!
#endif	/* 0 */

	if (!pAC->Addr.CurrentMacAddressSet) {
		return (SK_ADDR_TOO_EARLY);
	}

	if (Flags & SK_ADDR_PHYSICAL_ADDRESS) {	/* Physical MAC address. */
		if (SK_ADDR_EQUAL(pNewAddr->a, pAC->Addr.CurrentMacAddress.a)) {
			return (SK_ADDR_DUPLICATE_ADDRESS);
		}

		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Addr.Port[i].CurrentMacAddressSet) {
				return (SK_ADDR_TOO_EARLY);
			}

			if (SK_ADDR_EQUAL(
				pNewAddr->a,
				pAC->Addr.Port[i].CurrentMacAddress.a)) {
				if (i == PortIdx) {
					return (SK_ADDR_SUCCESS);
				}
				else {
					return (SK_ADDR_DUPLICATE_ADDRESS);
				}
			}
		}

		pAC->Addr.Port[PortIdx].PreviousMacAddress =
			pAC->Addr.Port[PortIdx].CurrentMacAddress;
		pAC->Addr.Port[PortIdx].CurrentMacAddress = *pNewAddr;

		/* Change port's address. */
		OutAddr = (SK_U16 *)pNewAddr;
		XM_OUTADDR(IoC, PortIdx, XM_SA, OutAddr);

		/* Report address change to RLMT. */
		Para.Para32[0] = PortIdx;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_PORT_ADDR, Para);
	}
	else {	/* Logical MAC address. */
		if (SK_ADDR_EQUAL(pNewAddr->a, pAC->Addr.CurrentMacAddress.a)) {
			return (SK_ADDR_SUCCESS);
		}
		
		for (i = 0; i < (SK_U32)pAC->GIni.GIMacsFound; i++) {
			if (!pAC->Addr.Port[i].CurrentMacAddressSet) {
				return (SK_ADDR_TOO_EARLY);
			}

			if (SK_ADDR_EQUAL(
				pNewAddr->a,
				pAC->Addr.Port[i].CurrentMacAddress.a)) {
				return (SK_ADDR_DUPLICATE_ADDRESS);
			}
		}

		pAC->Addr.CurrentMacAddress = *pNewAddr;
		pAC->Addr.Port[PortIdx].Exact[0] = *pNewAddr;

#ifdef DEBUG
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_ADDR,
			SK_DBGCAT_CTRL,
			("Permanent MAC Address: %02X %02X %02X %02X %02X %02X\n",
				pAC->Addr.PermanentMacAddress.a[0],
				pAC->Addr.PermanentMacAddress.a[1],
				pAC->Addr.PermanentMacAddress.a[2],
				pAC->Addr.PermanentMacAddress.a[3],
				pAC->Addr.PermanentMacAddress.a[4],
				pAC->Addr.PermanentMacAddress.a[5]))
		SK_DBG_MSG(
			pAC,
			SK_DBGMOD_ADDR,
			SK_DBGCAT_CTRL,
			("New logical MAC Address: %02X %02X %02X %02X %02X %02X\n",
				pAC->Addr.CurrentMacAddress.a[0],
				pAC->Addr.CurrentMacAddress.a[1],
				pAC->Addr.CurrentMacAddress.a[2],
				pAC->Addr.CurrentMacAddress.a[3],
				pAC->Addr.CurrentMacAddress.a[4],
				pAC->Addr.CurrentMacAddress.a[5]))
#endif	/* DEBUG */

		/* Write address to first exact match entry of active port. */
		(void)SkAddrMcUpdate(pAC, IoC, PortIdx);
	}

	return (SK_ADDR_SUCCESS);
}	/* SkAddrOverride */


/******************************************************************************
 *
 *	SkAddrPromiscuousChange - set promiscuous mode for given port
 *
 * Description:
 *	This routine manages promiscuous mode:
 *	- none
 *	- all LLC frames
 *	- all MC frames
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrPromiscuousChange(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* I/O context */
SK_U32	PortIdx,		/* port whose promiscuous mode changes */
int		NewPromMode)	/* new promiscuous mode */
{
	int			i;
	SK_BOOL		InexactModeBit;
	SK_U8		Inexact;
	SK_U8		HwInexact;
	SK_FILTER64	HwInexactFilter;
	SK_U16		LoMode;		/* Lower 16 bits of XMAC Mode Register. */
	int			CurPromMode = SK_PROM_MODE_NONE;

	if (PortIdx >= (SK_U32)pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	/* Read CurPromMode from Hardware. */
	XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);

	if (LoMode & XM_MD_ENA_PROM) {
		CurPromMode |= SK_PROM_MODE_LLC;
	}
	
	for (Inexact = 0xFF, i = 0; i < 8; i++) {
		Inexact &= pAC->Addr.Port[PortIdx].InexactFilter.Bytes[i];
	}
	if (Inexact == 0xFF) {
		CurPromMode |= (pAC->Addr.Port[PortIdx].PromMode & SK_PROM_MODE_ALL_MC);
	}
	else {
		/* Read InexactModeBit (bit 15 in mode register). */
		XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);
		
		InexactModeBit = (LoMode & XM_MD_ENA_HSH) != 0;

		/* Read 64-bit hash register from HW. */
		XM_INHASH(IoC, PortIdx, XM_HSM, &HwInexactFilter.Bytes[0]);

		for (HwInexact = 0xFF, i = 0; i < 8; i++) {
			HwInexact &= HwInexactFilter.Bytes[i];
		}

		if (InexactModeBit && (HwInexact == 0xFF)) {
			CurPromMode |= SK_PROM_MODE_ALL_MC;
		}
	}

	pAC->Addr.Port[PortIdx].PromMode = NewPromMode;

	if (NewPromMode == CurPromMode) {
		return (SK_ADDR_SUCCESS);
	}

	if ((NewPromMode & SK_PROM_MODE_ALL_MC) &&
		!(CurPromMode & SK_PROM_MODE_ALL_MC)) {	/* All MC. */
		/* Set all bits in 64-bit hash register. */
		XM_OUTHASH(IoC, PortIdx, XM_HSM, &OnesHash);

		/* Set bit 15 in mode register. */
		XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);
		LoMode |= XM_MD_ENA_HSH;
		XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
	}
	else if ((CurPromMode & SK_PROM_MODE_ALL_MC) &&
		!(NewPromMode & SK_PROM_MODE_ALL_MC)) {	/* Norm MC. */
		for (Inexact = 0, i = 0; i < 8; i++) {
			Inexact |= pAC->Addr.Port[PortIdx].InexactFilter.Bytes[i];
		}
		if (Inexact == 0) {
			/* Clear bit 15 in mode register. */
			XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);
			LoMode &= ~XM_MD_ENA_HSH;
			XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
		}
		else {
			/* Set 64-bit hash register to InexactFilter. */
			XM_OUTHASH(
				IoC,
				PortIdx,
				XM_HSM,
				&pAC->Addr.Port[PortIdx].InexactFilter.Bytes[0]);

			/* Set bit 15 in mode register. */
			XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);
			LoMode |= XM_MD_ENA_HSH;
			XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
		}
	}

	if ((NewPromMode & SK_PROM_MODE_LLC) &&
		!(CurPromMode & SK_PROM_MODE_LLC)) {	/* Prom. LLC */
		/* Set promiscuous bit in mode register. */
		XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);

#if 0
		/* Receive MAC frames. */
		LoMode |= XM_MD_RX_MCTRL;
#endif	/* 0 */

		LoMode |= XM_MD_ENA_PROM;
		XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
	}
	else if ((CurPromMode & SK_PROM_MODE_LLC) &&
		!(NewPromMode & SK_PROM_MODE_LLC)) {	/* Norm. LLC. */
		/* Clear promiscuous bit in mode register. */
		XM_IN16(IoC, PortIdx, XM_MODE, &LoMode);

#if 0
		/* Don't receive MAC frames. */
		LoMode &= ~XM_MD_RX_MCTRL;
#endif	/* 0 */
		
		LoMode &= ~XM_MD_ENA_PROM;
		XM_OUT16(IoC, PortIdx, XM_MODE, LoMode);
	}
	
	return (SK_ADDR_SUCCESS);
}	/* SkAddrPromiscuousChange */


/******************************************************************************
 *
 *	SkAddrSwap - swap address info
 *
 * Description:
 *	This routine swaps address info of two ports.
 *
 * Context:
 *	runtime, pageable
 *	may be called after SK_INIT_IO
 *
 * Returns:
 *	SK_ADDR_SUCCESS
 *	SK_ADDR_ILLEGAL_PORT
 */
int	SkAddrSwap(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* I/O context */
SK_U32	FromPortIdx,	/* Port1 Index */
SK_U32	ToPortIdx)		/* Port2 Index */
{
	int			i;
	SK_U8		Byte;
	SK_MAC_ADDR	MacAddr;
	SK_U32		DWord;

	if (FromPortIdx >= (SK_U32)pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	if (ToPortIdx >= (SK_U32)pAC->GIni.GIMacsFound) {
		return (SK_ADDR_ILLEGAL_PORT);
	}

	/*
	 * Swap
	 * - Exact Match Entries
	 * - FirstExactMatchRlmt;
	 * - NextExactMatchRlmt;
	 * - FirstExactMatchDrv;
	 * - NextExactMatchDrv;
	 * - 64-bit filter
	 * - Promiscuous Mode
	 * of ports.
	 */

	for (i = 0; i < SK_ADDR_EXACT_MATCHES; i++) {
		MacAddr = pAC->Addr.Port[FromPortIdx].Exact[i];
		pAC->Addr.Port[FromPortIdx].Exact[i] =
			pAC->Addr.Port[ToPortIdx].Exact[i];
		pAC->Addr.Port[ToPortIdx].Exact[i] = MacAddr;
	}

	for (i = 0; i < 8; i++) {
		Byte = pAC->Addr.Port[FromPortIdx].InexactFilter.Bytes[i];
		pAC->Addr.Port[FromPortIdx].InexactFilter.Bytes[i] =
			pAC->Addr.Port[ToPortIdx].InexactFilter.Bytes[i];
		pAC->Addr.Port[ToPortIdx].InexactFilter.Bytes[i] = Byte;
	}

	i = pAC->Addr.Port[FromPortIdx].PromMode;
	pAC->Addr.Port[FromPortIdx].PromMode = pAC->Addr.Port[ToPortIdx].PromMode;
	pAC->Addr.Port[ToPortIdx].PromMode = i;

	DWord = pAC->Addr.Port[FromPortIdx].FirstExactMatchRlmt;
	pAC->Addr.Port[FromPortIdx].FirstExactMatchRlmt =
		pAC->Addr.Port[ToPortIdx].FirstExactMatchRlmt;
	pAC->Addr.Port[ToPortIdx].FirstExactMatchRlmt = DWord;

	DWord = pAC->Addr.Port[FromPortIdx].NextExactMatchRlmt;
	pAC->Addr.Port[FromPortIdx].NextExactMatchRlmt =
		pAC->Addr.Port[ToPortIdx].NextExactMatchRlmt;
	pAC->Addr.Port[ToPortIdx].NextExactMatchRlmt = DWord;

	DWord = pAC->Addr.Port[FromPortIdx].FirstExactMatchDrv;
	pAC->Addr.Port[FromPortIdx].FirstExactMatchDrv =
		pAC->Addr.Port[ToPortIdx].FirstExactMatchDrv;
	pAC->Addr.Port[ToPortIdx].FirstExactMatchDrv = DWord;

	DWord = pAC->Addr.Port[FromPortIdx].NextExactMatchDrv;
	pAC->Addr.Port[FromPortIdx].NextExactMatchDrv =
		pAC->Addr.Port[ToPortIdx].NextExactMatchDrv;
	pAC->Addr.Port[ToPortIdx].NextExactMatchDrv = DWord;

	pAC->Addr.ActivePort = ToPortIdx;

	(void)SkAddrMcUpdate(pAC, IoC, FromPortIdx);
	(void)SkAddrMcUpdate(pAC, IoC, ToPortIdx);

	return (SK_ADDR_SUCCESS);
}	/* SkAddrSwap */

#ifdef __cplusplus
}
#endif	/* __cplusplus */

