/******************************************************************************
 *
 * Name:	skgesirq.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.55 $
 * Date:	$Date: 2000/06/19 08:36:25 $
 * Purpose:	Special IRQ module
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
 *	$Log: skgesirq.c,v $
 *	Revision 1.55  2000/06/19 08:36:25  cgoos
 *	Changed comment.
 *	
 *	Revision 1.54  2000/05/22 08:45:57  malthoff
 *	Fix: #10523 is valid for all BCom PHYs.
 *	
 *	Revision 1.53  2000/05/19 10:20:30  cgoos
 *	Removed Solaris debug output code.
 *	
 *	Revision 1.52  2000/05/19 10:19:37  cgoos
 *	Added PHY state check in HWLinkDown.
 *	Move PHY interrupt code to IS_EXT_REG case in SkGeSirqIsr.
 *	
 *	Revision 1.51  2000/05/18 05:56:20  cgoos
 *	Fixed typo.
 *	
 *	Revision 1.50  2000/05/17 12:49:49  malthoff
 *	Fixes BCom link bugs (#10523).
 *	
 *	Revision 1.49  1999/12/17 11:02:50  gklug
 *	fix: read PHY_STAT of Broadcom chip more often to assure good status
 *	
 *	Revision 1.48  1999/12/06 10:01:17  cgoos
 *	Added SET function for Role.
 *	
 *	Revision 1.47  1999/11/22 13:34:24  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.46  1999/09/16 10:30:07  cgoos
 *	Removed debugging output statement from Linux.
 *	
 *	Revision 1.45  1999/09/16 07:32:55  cgoos
 *	Fixed dual-port copperfield bug (PHY_READ from resetted port).
 *	Removed some unused variables.
 *	
 *	Revision 1.44  1999/08/03 15:25:04  cgoos
 *	Removed workaround for disabled interrupts in half duplex mode.
 *	
 *	Revision 1.43  1999/08/03 14:27:58  cgoos
 *	Removed SENSE mode code from SkGePortCheckUpBcom.
 *	
 *	Revision 1.42  1999/07/26 09:16:54  cgoos
 *	Added some typecasts to avoid compiler warnings.
 *	
 *	Revision 1.41  1999/05/19 07:28:59  cgoos
 *	Changes for 1000Base-T.
 *	
 *	Revision 1.40  1999/04/08 13:59:39  gklug
 *	fix: problem with 3Com switches endless RESTARTs
 *	
 *	Revision 1.39  1999/03/08 10:10:52  gklug
 *	fix: AutoSensing did switch to next mode even if LiPa indicated offline
 *	
 *	Revision 1.38  1999/03/08 09:49:03  gklug
 *	fix: Bug using pAC instead of IoC, causing AIX problems
 *	fix: change compare for Linux compiler bug workaround
 *	
 *	Revision 1.37  1999/01/28 14:51:33  gklug
 *	fix: monitor for autosensing and extra RESETS the RX on wire counters
 *	
 *	Revision 1.36  1999/01/22 09:19:55  gklug
 *	fix: Init DupMode and InitPauseMd are now called in RxTxEnable
 *	
 *	Revision 1.35  1998/12/11 15:22:59  gklug
 *	chg: autosensing: check for receive if manual mode was guessed
 *	chg: simplified workaround for XMAC errata
 *	chg: wait additional 100 ms before link goes up.
 *	chg: autoneg timeout to 600 ms
 *	chg: restart autoneg even if configured to autonegotiation
 *	
 *	Revision 1.34  1998/12/10 10:33:14  gklug
 *	add: more debug messages
 *	fix: do a new InitPhy if link went down (AutoSensing problem)
 *	chg: Check for zero shorts if link is NOT up
 *	chg: reset Port if link goes down
 *	chg: wait additional 100 ms when link comes up to check shorts
 *	fix: dummy read extended autoneg status to prevent link going down immediately
 *	
 *	Revision 1.33  1998/12/07 12:18:29  gklug
 *	add: refinement of autosense mode: take into account the autoneg cap of LiPa
 *	
 *	Revision 1.32  1998/12/07 07:11:21  gklug
 *	fix: compiler warning
 *	
 *	Revision 1.31  1998/12/02 09:29:05  gklug
 *	fix: WA XMAC Errata: FCSCt check was not correct.
 *	fix: WA XMAC Errata: Prec Counter were NOT updated in case of short checks.
 *	fix: Clear Stat : now clears the Prev counters of all known Ports
 *	
 *	Revision 1.30  1998/12/01 10:54:15  gklug
 *	dd: workaround for XMAC errata changed. Check RX count and CRC err Count, too.
 *	
 *	Revision 1.29  1998/12/01 10:01:53  gklug
 *	fix: if MAC IRQ occurs during port down, this will be handled correctly
 *	
 *	Revision 1.28  1998/11/26 16:22:11  gklug
 *	fix: bug in autosense if manual modes are used
 *	
 *	Revision 1.27  1998/11/26 15:50:06  gklug
 *	fix: PNMI needs to set PLinkModeConf
 *	
 *	Revision 1.26  1998/11/26 14:51:58  gklug
 *	add: AutoSensing functionalty
 *	
 *	Revision 1.25  1998/11/26 07:34:37  gklug
 *	fix: Init PrevShorts when restarting port due to Link connection
 *	
 *	Revision 1.24  1998/11/25 10:57:32  gklug
 *	fix: remove unreferenced local vars
 *	
 *	Revision 1.23  1998/11/25 08:26:40  gklug
 *	fix: don't do a RESET on a starting or stopping port
 *	
 *	Revision 1.22  1998/11/24 13:29:44  gklug
 *	add: Workaround for MAC parity errata
 *	
 *	Revision 1.21  1998/11/18 15:31:06  gklug
 *	fix: lint bugs
 *	
 *	Revision 1.20  1998/11/18 12:58:54  gklug
 *	fix: use PNMI query instead of hardware access
 *	
 *	Revision 1.19  1998/11/18 12:54:55  gklug
 *	chg: add new workaround for XMAC Errata
 *	add: short event counter monitoring on active link too
 *	
 *	Revision 1.18  1998/11/13 14:27:41  malthoff
 *	Bug Fix: Packet Arbiter Timeout was not cleared correctly
 *	for timeout on TX1 and TX2.
 *	
 *	Revision 1.17  1998/11/04 07:01:59  cgoos
 *	Moved HW link poll sequence.
 *	Added call to SkXmRxTxEnable.
 *	
 *	Revision 1.16  1998/11/03 13:46:03  gklug
 *	add: functionality of SET_LMODE and SET_FLOW_MODE
 *	fix: send RLMT LinkDown event when Port stop is given with LinkUp
 *	
 *	Revision 1.15  1998/11/03 12:56:47  gklug
 *	fix: Needs more events
 *	
 *	Revision 1.14  1998/10/30 07:36:35  gklug
 *	rmv: unnecessary code
 *	
 *	Revision 1.13  1998/10/29 15:21:57  gklug
 *	add: Poll link feature for activating HW link
 *	fix: Deactivate HWLink when Port STOP is given
 *	
 *	Revision 1.12  1998/10/28 07:38:57  cgoos
 *	Checking link status at begin of SkHWLinkUp.
 *	
 *	Revision 1.11  1998/10/22 09:46:50  gklug
 *	fix SysKonnectFileId typo
 *	
 *	Revision 1.10  1998/10/14 13:57:47  gklug
 *	add: Port start/stop event
 *	
 *	Revision 1.9  1998/10/14 05:48:29  cgoos
 *	Added definition for Para.
 *	
 *	Revision 1.8  1998/10/14 05:40:09  gklug
 *	add: Hardware Linkup signal used
 *	
 *	Revision 1.7  1998/10/09 06:50:20  malthoff
 *	Remove ID_sccs by SysKonnectFileId.
 *
 *	Revision 1.6  1998/10/08 09:11:49  gklug
 *	add: clear IRQ commands
 *	
 *	Revision 1.5  1998/10/02 14:27:35  cgoos
 *	Fixed some typos and wrong event names.
 *	
 *	Revision 1.4  1998/10/02 06:24:17  gklug
 *	add: HW error function
 *	fix: OUT macros
 *	
 *	Revision 1.3  1998/10/01 07:03:00  gklug
 *	add: ISR for the usual interrupt source register
 *	
 *	Revision 1.2  1998/09/03 13:50:33  gklug
 *	add: function prototypes
 *	
 *	Revision 1.1  1998/08/27 11:50:21  gklug
 *	initial revision
 *	
 *
 *
 ******************************************************************************/


/*
	Special Interrupt handler

	The following abstract should show how this module is included
	in the driver path:

	In the ISR of the driver the bits for frame transmission complete and
	for receive complete are checked and handled by the driver itself.
	The bits of the slow path mask are checked after this and then the
	entry into the so-called "slow path" is prepared. It is an implemetors
	decision whether this is executed directly or just scheduled by
	disabling the mask. In the interrupt service routine events may be
	generated, so it would be a good idea to call the EventDispatcher
	right after this ISR.

	The Interrupt service register of the adapter is NOT read by this
	module. SO if the drivers implemetor needs a while loop around the
	slow data paths Interrupt bits, he needs to call the SkGeIsr() for
	each loop entered.

	However, the XMAC Interrupt status registers are read in a while loop.

*/
static const char SysKonnectFileId[] =
	"$Id: skgesirq.c,v 1.55 2000/06/19 08:36:25 cgoos Exp $" ;

#include "h/skdrv1st.h"		/* Driver Specific Definitions */
#include "h/skgepnmi.h"		/* PNMI Definitions */
#include "h/skrlmt.h"		/* RLMT Definitions */
#include "h/skdrv2nd.h"		/* Adapter Control- and Driver specific Def. */


/* local function prototypes */
static int	SkGePortCheckUpXmac(SK_AC*, SK_IOC, int);
static int	SkGePortCheckUpBcom(SK_AC*, SK_IOC, int);
static int	SkGePortCheckUpLone(SK_AC*, SK_IOC, int);
static int	SkGePortCheckUpNat(SK_AC*, SK_IOC, int);
static void	SkPhyIsrBcom(SK_AC*, SK_IOC, int, SK_U16);
static void	SkPhyIsrLone(SK_AC*, SK_IOC, int, SK_U16);


#ifdef __C2MAN__
/*
	Special IRQ function

	General Description:

 */
intro()
{}
#endif

/*
 * Define return codes of SkGePortCheckUp and CheckShort
 */
#define	SK_HW_PS_NONE		0	/* No action needed */
#define	SK_HW_PS_RESTART	1	/* Restart needed */
#define	SK_HW_PS_LINK		2	/* Link Up actions needed */

/******************************************************************************
 *
 *	SkHWInitDefSense() - Default Autosensing mode initialization
 *
 * Description:
 *	This function handles the Hardware link down signal
 *
 * Note:
 *
 */
void	SkHWInitDefSense(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int	Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port] ;

	pPrt->PAutoNegTimeOut = 0;

	if (pPrt->PLinkModeConf != SK_LMODE_AUTOSENSE) {
		pPrt->PLinkMode = pPrt->PLinkModeConf;
		return;
	}

	SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
		("AutoSensing: First mode %d on Port %d\n",
		(int) SK_LMODE_AUTOFULL,
		 Port));

	pPrt->PLinkMode = SK_LMODE_AUTOFULL;

	return;
}

/******************************************************************************
 *
 *	SkHWSenseGetNext() - GetNextAutosensing Mode
 *
 * Description:
 *	This function handles the AutoSensing
 *
 * Note:
 *
 */
SK_U8	SkHWSenseGetNext(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int	Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port] ;

	pPrt->PAutoNegTimeOut = 0;

	if (pPrt->PLinkModeConf != SK_LMODE_AUTOSENSE) {
		/* Leave all as configured */
		return(pPrt->PLinkModeConf);
	}

	if (pPrt->PLinkMode == SK_LMODE_AUTOFULL) {
		/* Return next mode AUTOBOTH */
		return(SK_LMODE_AUTOBOTH);
	}

	/* Return default autofull */
	return(SK_LMODE_AUTOFULL);
}

/******************************************************************************
 *
 *	SkHWSenseSetNext() - Autosensing Set next mode
 *
 * Description:
 *	This function sets the appropriate next mode.
 *
 * Note:
 *
 */
void	SkHWSenseSetNext(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int	Port,		/* Port Index (MAC_1 + n) */
SK_U8	NewMode)	/* New Mode to be written in sense mode */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port] ;

	pPrt->PAutoNegTimeOut = 0;

	if (pPrt->PLinkModeConf != SK_LMODE_AUTOSENSE) {
		return;
	}

	SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
		("AutoSensing: next mode %d on Port %d\n", (int) NewMode,
		 Port));
	pPrt->PLinkMode = NewMode;

	return;
}

/******************************************************************************
 *
 *	SkHWLinkDown() - Link Down handling
 *
 * Description:
 *	This function handles the Hardware link down signal
 *
 * Note:
 *
 */
void	SkHWLinkDown(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int	Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U16		Word;

	pPrt = &pAC->GIni.GP[Port] ;

	/* Disable all XMAC interrupts */
	XM_OUT16(IoC, Port, XM_IMSK, 0xffff);

	/* Disable Receive and Transmitter */
	XM_IN16(IoC, Port, XM_MMU_CMD, &Word);
	XM_OUT16(IoC, Port, XM_MMU_CMD, Word & ~(XM_MMU_ENA_RX|XM_MMU_ENA_TX));
	
	/* disable all PHY interrupts */
	switch (pAC->GIni.GP[Port].PhyType) {
		case SK_PHY_BCOM:
			/* make sure that PHY is initialized */
			if (pAC->GIni.GP[Port].PState) {
				/* Workaround BCOM Errata (#10523) all BCom */
				/* Disable Power Management if link is down */
				PHY_READ(IoC, pPrt, Port, PHY_BCOM_AUX_CTRL,
					&Word);
				PHY_WRITE(IoC, pPrt, Port, PHY_BCOM_AUX_CTRL,	
					Word | PHY_B_AC_DIS_PM);
			}
			PHY_WRITE(IoC, pPrt, Port, PHY_BCOM_INT_MASK, 
				0xffff);
			break;
		case SK_PHY_LONE:
			PHY_WRITE(IoC, pPrt, Port, PHY_LONE_INT_ENAB, 
				0x0);
			break;
		case SK_PHY_NAT:
			/* todo: National
			PHY_WRITE(IoC, pPrt, Port, PHY_NAT_INT_MASK, 
				0xffff); */
			break;
	}

	/* Init default sense mode */
	SkHWInitDefSense(pAC, IoC, Port);

	if (!pPrt->PHWLinkUp) {
		return;
	} 

	SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
		("Link down Port %d\n", Port));

	/* Set Link to DOWN */
	pPrt->PHWLinkUp = SK_FALSE;

	/* Reset Port stati */
	pPrt->PLinkModeStatus = SK_LMODE_STAT_UNKNOWN;
	pPrt->PFlowCtrlStatus = SK_FLOW_STAT_NONE ;

	/*
	 * Reinit Phy especially when the AutoSense default is set now
	 */
	SkXmInitPhy(pAC, IoC, Port, SK_FALSE);

	/*
	 * GP0: used for workaround of Rev. C
	 * Errata 2
	 */

	/* Do NOT signal to RLMT */

	/* Do NOT start the timer here */
}

/******************************************************************************
 *
 *	SkHWLinkUp() - Link Up handling
 *
 * Description:
 *	This function handles the Hardware link up signal
 *
 * Note:
 *
 */
void	SkHWLinkUp(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int	Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port] ;

	if (pPrt->PHWLinkUp) {
		/* We do NOT need to proceed on active link */
		return;
	} 

	pPrt->PHWLinkUp = SK_TRUE ;
	pPrt->PAutoNegFail = SK_FALSE ;
	pPrt->PLinkModeStatus = SK_LMODE_STAT_UNKNOWN;

	if (pPrt->PLinkMode != SK_LMODE_AUTOHALF &&
	    pPrt->PLinkMode != SK_LMODE_AUTOFULL &&
	    pPrt->PLinkMode != SK_LMODE_AUTOBOTH) {
		/* Link is up and no Autonegotiation should be done */

		/* Configure Port */

		/* Set Link Mode */
		if (pPrt->PLinkMode == SK_LMODE_FULL) {
			pPrt->PLinkModeStatus = SK_LMODE_STAT_FULL;
		} else {
			pPrt->PLinkModeStatus = SK_LMODE_STAT_HALF;
		}

		/* No flow control without autonegotiation */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_NONE ;

		/* RX/TX enable */
		SkXmRxTxEnable(pAC, IoC, Port);
	}
}

/******************************************************************************
 *
 * SkMacParity	- does everything to handle MAC parity errors correctly
 *
 */
static	void	SkMacParity(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int	Port)		/* Port Index of the port failed */
{
	SK_EVPARA	Para;
	SK_GEPORT	*pPrt;		/* GIni Port struct pointer */
	SK_U64		TxMax;		/* TxMax Counter */
	unsigned int	Len;

	pPrt = &pAC->GIni.GP[Port];

	/* Clear IRQ */
	SK_OUT16(IoC, MR_ADDR(Port,TX_MFF_CTRL1), MFF_CLR_PERR) ;

	if (pPrt->PCheckPar) {
		if (Port == MAC_1) {
			SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E016,
				SKERR_SIRQ_E016MSG) ;
		} else {
			SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E017,
				SKERR_SIRQ_E017MSG) ;
		}
		Para.Para64 = Port;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = Port;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);

		return;
	}


	/* Check whether frames with a size of 1k were sent */
	Len = sizeof(SK_U64);
	SkPnmiGetVar(pAC, IoC, OID_SKGE_STAT_TX_MAX, (char *) &TxMax,
		&Len, (SK_U32) SK_PNMI_PORT_PHYS2INST(Port));

	if (TxMax > 0) {
		/* From now on check the parity */
		pPrt->PCheckPar = SK_TRUE;
	}
}

/******************************************************************************
 *
 *	Hardware Error service routine
 *
 * Description:
 *
 * Notes:
 */
static	void	SkGeHwErr(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
SK_U32	HwStatus)	/* Interrupt status word */
{
	SK_EVPARA	Para;

	if (HwStatus & IS_IRQ_STAT) {
		SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E013,
			SKERR_SIRQ_E013MSG) ;
		Para.Para64 = 0;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_ADAP_FAIL, Para);
	}

	if (HwStatus & IS_IRQ_MST_ERR) {
		SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E012,
			SKERR_SIRQ_E012MSG) ;
		Para.Para64 = 0;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_ADAP_FAIL, Para);
	}

	if (HwStatus & IS_NO_STAT_M1) {
		/* Ignore it */
		/* This situation is also indicated in the descriptor */
		SK_OUT16(IoC, MR_ADDR(MAC_1,RX_MFF_CTRL1), MFF_CLR_INSTAT) ;
	}

	if (HwStatus & IS_NO_STAT_M2) {
		/* Ignore it */
		/* This situation is also indicated in the descriptor */
		SK_OUT16(IoC, MR_ADDR(MAC_2,RX_MFF_CTRL1), MFF_CLR_INSTAT) ;
	}

	if (HwStatus & IS_NO_TIST_M1) {
		/* Ignore it */
		/* This situation is also indicated in the descriptor */
		SK_OUT16(IoC, MR_ADDR(MAC_1,RX_MFF_CTRL1), MFF_CLR_INTIST) ;
	}

	if (HwStatus & IS_NO_TIST_M2) {
		/* Ignore it */
		/* This situation is also indicated in the descriptor */
		SK_OUT16(IoC, MR_ADDR(MAC_2,RX_MFF_CTRL1), MFF_CLR_INTIST) ;
	}

	if (HwStatus & IS_RAM_RD_PAR) {
		SK_OUT16(IoC, B3_RI_CTRL, RI_CLR_RD_PERR) ;
		SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E014,
			SKERR_SIRQ_E014MSG) ;
		Para.Para64 = 0;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_ADAP_FAIL, Para);
	}

	if (HwStatus & IS_RAM_WR_PAR) {
		SK_OUT16(IoC, B3_RI_CTRL, RI_CLR_WR_PERR) ;
		SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E015,
			SKERR_SIRQ_E015MSG) ;
		Para.Para64 = 0;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_ADAP_FAIL, Para);
	}

	if (HwStatus & IS_M1_PAR_ERR) {
		SkMacParity(pAC, IoC, MAC_1) ;
	}

	if (HwStatus & IS_M2_PAR_ERR) {
		SkMacParity(pAC, IoC, MAC_2) ;
	}

	if (HwStatus & IS_R1_PAR_ERR) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_R1_CSR, CSR_IRQ_CL_P) ;

		SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E018,
			SKERR_SIRQ_E018MSG) ;
		Para.Para64 = MAC_1;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

	if (HwStatus & IS_R2_PAR_ERR) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_R2_CSR, CSR_IRQ_CL_P) ;

		SK_ERR_LOG(pAC, SK_ERRCL_HW , SKERR_SIRQ_E019,
			SKERR_SIRQ_E019MSG) ;
		Para.Para64 = MAC_2;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_2;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

}

/******************************************************************************
 *
 *	Interrupt service routine
 *
 * Description:
 *
 * Notes:
 */
void	SkGeSirqIsr(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
SK_U32	Istatus)	/* Interrupt status word */
{
	SK_U32		RegVal32;	/* Read register Value */
	SK_EVPARA	Para;
	SK_U16		XmIsr;

	if (Istatus & IS_HW_ERR) {
		SK_IN32(IoC, B0_HWE_ISRC, &RegVal32) ;
		SkGeHwErr(pAC, IoC, RegVal32) ;
	}

	/*
	 * Packet Timeout interrupts
	 */
	/* Check whether XMACs are correctly initialized */
	if ((Istatus & (IS_PA_TO_RX1 | IS_PA_TO_TX1)) &&
	    !pAC->GIni.GP[MAC_1].PState) {
		/* XMAC was not initialized but Packet timeout occured */
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E004,
			SKERR_SIRQ_E004MSG) ;
	}

	if ((Istatus & (IS_PA_TO_RX2 | IS_PA_TO_TX2)) &&
	    !pAC->GIni.GP[MAC_2].PState) {
		/* XMAC was not initialized but Packet timeout occured */
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E005,
			SKERR_SIRQ_E005MSG) ;
	}

	if (Istatus & IS_PA_TO_RX1) {
		/* Means network is filling us up */
		SK_ERR_LOG(pAC, SK_ERRCL_HW | SK_ERRCL_INIT, SKERR_SIRQ_E002,
			SKERR_SIRQ_E002MSG) ;
		SK_OUT16(IoC, B3_PA_CTRL, PA_CLR_TO_RX1) ;
	}

	if (Istatus & IS_PA_TO_RX2) {
		/* Means network is filling us up */
		SK_ERR_LOG(pAC, SK_ERRCL_HW | SK_ERRCL_INIT, SKERR_SIRQ_E003,
			SKERR_SIRQ_E003MSG) ;
		SK_OUT16(IoC, B3_PA_CTRL, PA_CLR_TO_RX2) ;
	}

	if (Istatus & IS_PA_TO_TX1) {
		/* May be a normal situation in a server with a slow network */
		SK_OUT16(IoC, B3_PA_CTRL, PA_CLR_TO_TX1) ;
	}

	if (Istatus & IS_PA_TO_TX2) {
		/* May be a normal situation in a server with a slow network */
		SK_OUT16(IoC, B3_PA_CTRL, PA_CLR_TO_TX2) ;
	}

	/*
	 * Check interrupts of the particular queues.
	 */
	if (Istatus & IS_R1_C) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_R1_CSR, CSR_IRQ_CL_C) ;
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E006,
			SKERR_SIRQ_E006MSG) ;
		Para.Para64 = MAC_1;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

	if (Istatus & IS_R2_C) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_R2_CSR, CSR_IRQ_CL_C) ;
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E007,
			SKERR_SIRQ_E007MSG) ;
		Para.Para64 = MAC_2;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_2;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

	if (Istatus & IS_XS1_C) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_XS1_CSR, CSR_IRQ_CL_C) ;
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E008,
			SKERR_SIRQ_E008MSG) ;
		Para.Para64 = MAC_1;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

	if (Istatus & IS_XA1_C) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_XA1_CSR, CSR_IRQ_CL_C) ;
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E009,
			SKERR_SIRQ_E009MSG) ;
		Para.Para64 = MAC_1;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

	if (Istatus & IS_XS2_C) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_XS2_CSR, CSR_IRQ_CL_C) ;
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E010,
			SKERR_SIRQ_E010MSG) ;
		Para.Para64 = MAC_2;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_2;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

	if (Istatus & IS_XA2_C) {
		/* Clear IRQ */
		SK_OUT32(IoC, B0_XA2_CSR, CSR_IRQ_CL_C) ;
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E011,
			SKERR_SIRQ_E011MSG) ;
		Para.Para64 = MAC_2;
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_FAIL, Para);
		Para.Para32[0] = MAC_2;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);
	}

	/*
	 * external reg interrupt
	 */
	if (Istatus & IS_EXT_REG) {
		SK_U16 	PhyInt;
		SK_U16 	PhyIMsk;
		int	i;
		/* test IRQs from PHY */
		for (i=0; i<pAC->GIni.GIMacsFound; i++) {
			switch (pAC->GIni.GP[i].PhyType) {
			case SK_PHY_XMAC:
				break;
			case SK_PHY_BCOM:
	    			if(pAC->GIni.GP[i].PState) {
					PHY_READ(IoC, &pAC->GIni.GP[i], i,
						PHY_BCOM_INT_STAT, &PhyInt);
					PHY_READ(IoC, &pAC->GIni.GP[i], i,
						PHY_BCOM_INT_MASK, &PhyIMsk);
					
					if (PhyInt & (~PhyIMsk)) {
						SK_DBG_MSG(pAC,SK_DBGMOD_HWM,
							SK_DBGCAT_IRQ,
							("Port %d Bcom Int: %x "
							" Mask: %x\n",
							i, PhyInt, PhyIMsk));
						SkPhyIsrBcom(pAC, IoC, i,
							(SK_U16) 
							(PhyInt & (~PhyIMsk)));
					}
				}
				else {
				}
				break;
			case SK_PHY_LONE:
				PHY_READ(IoC, &pAC->GIni.GP[i], i,
					PHY_LONE_INT_STAT, &PhyInt);
				PHY_READ(IoC, &pAC->GIni.GP[i], i,
					PHY_LONE_INT_ENAB, &PhyIMsk);
				
				if (PhyInt & PhyIMsk) {
					SK_DBG_MSG(pAC,SK_DBGMOD_HWM,
						SK_DBGCAT_IRQ,
						("Port %d  Lone Int: %x "
						" Mask: %x\n",
						i, PhyInt, PhyIMsk));
					SkPhyIsrLone(pAC, IoC, i,
						(SK_U16) (PhyInt & PhyIMsk));
				}
				break;
			case SK_PHY_NAT:
				/* todo: National */
				break;
			}
		}
	}

	/*
	 * I2C Ready interrupt
	 */
	if (Istatus & IS_I2C_READY) {
		SkI2cIsr(pAC, IoC);
	}

	if (Istatus & IS_LNK_SYNC_M1) {
		/*
		 * We do NOT need the Link Sync interrupt, because it shows
		 * us only a link going down.
		 */
		/* clear interrupt */
		SK_OUT8(IoC, MR_ADDR(MAC_1,LNK_SYNC_CTRL), LED_CLR_IRQ);
	}

	/* Check MAC after link sync counter */
	if (Istatus & IS_MAC1) {
		XM_IN16(IoC, MAC_1, XM_ISRC, &XmIsr) ;
		SkXmIrq(pAC, IoC, MAC_1, XmIsr);
	}

	if (Istatus & IS_LNK_SYNC_M2) {
		/*
		 * We do NOT need the Link Sync interrupt, because it shows
		 * us only a link going down.
		 */
		/* clear interrupt */
		SK_OUT8(IoC, MR_ADDR(MAC_2,LNK_SYNC_CTRL), LED_CLR_IRQ);
	}

	/* Check MAC after link sync counter */
	if (Istatus & IS_MAC2) {
		XM_IN16(IoC, MAC_2, XM_ISRC, &XmIsr) ;
		SkXmIrq(pAC, IoC, MAC_2, XmIsr);
	}

	/*
	 * Timer interrupt
	 *  To be served last
	 */
	if (Istatus & IS_TIMINT) {
		SkHwtIsr(pAC, IoC);
	}
}

/*
 * Define an array of RX counter which are checked
 * in AutoSense mode to check whether a link is not able to autonegotiate.
 */
static const SK_U32 SkGeRxOids[]= {
	OID_SKGE_STAT_RX_64,
	OID_SKGE_STAT_RX_127,
	OID_SKGE_STAT_RX_255,
	OID_SKGE_STAT_RX_511,
	OID_SKGE_STAT_RX_1023,
	OID_SKGE_STAT_RX_MAX,
} ;
/******************************************************************************
 *
 * SkGePortCheckShorts - Implementing of the Workaround Errata # 2
 *
 * return:
 *	0	o.k. nothing needed
 *	1	Restart needed on this port
 */
int	SkGePortCheckShorts(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	IoC,		/* IO Context */
int	Port)		/* Which port should be checked */
{
	SK_U64		Shorts;		/* Short Event Counter */
	SK_U64		CheckShorts;	/* Check value for Short Event Counter */
	SK_U64		RxCts;		/* RX Counter (packets on network) */
	SK_U64		RxTmp;		/* RX temp. Counter */
	SK_U64		FcsErrCts;	/* FCS Error Counter */
	SK_GEPORT	*pPrt;		/* GIni Port struct pointer */
	unsigned int	Len;
	int		Rtv;		/* Return value */
	int		i;

	pPrt = &pAC->GIni.GP[Port];

	/* Default: no action */
	Rtv = SK_HW_PS_NONE;

	/*
	 * Extra precaution: check for short Event counter
	 */
	Len = sizeof(SK_U64);
	SkPnmiGetVar(pAC, IoC, OID_SKGE_STAT_RX_SHORTS, (char *) &Shorts,
		&Len, (SK_U32) SK_PNMI_PORT_PHYS2INST(Port));

	/*
	 * Read RX counter (packets seen on the network and not neccesarily
	 * really received.
	 */
	Len = sizeof(SK_U64);
	RxCts = 0;

	for (i = 0; i < sizeof(SkGeRxOids)/sizeof(SK_U32) ; i++) {
		SkPnmiGetVar(pAC, IoC, SkGeRxOids[i], (char *) &RxTmp,
			&Len, (SK_U32) SK_PNMI_PORT_PHYS2INST(Port));
		RxCts += RxTmp;
	}

	/* On default: check shorts against zero */
	CheckShorts = 0;

	/*
	 * Extra extra precaution on active links:
	 */
	if (pPrt->PHWLinkUp) {
		/*
		 * Reset Link Restart counter
		 */
		pPrt->PLinkResCt = 0;

		/* If link is up check for 2 */
		CheckShorts = 2;

		Len = sizeof(SK_U64);
		SkPnmiGetVar(pAC, IoC, OID_SKGE_STAT_RX_FCS,
			(char *) &FcsErrCts, &Len,
			(SK_U32) SK_PNMI_PORT_PHYS2INST(Port));
		
		if (pPrt->PLinkModeConf == SK_LMODE_AUTOSENSE &&
		    pPrt->PLipaAutoNeg == SK_LIPA_UNKNOWN &&
		    (pPrt->PLinkMode == SK_LMODE_HALF ||
		     pPrt->PLinkMode == SK_LMODE_FULL)) {
			/*
			 * This is autosensing and we are in the fallback
			 * manual full/half duplex mode.
			 */
			if (RxCts == pPrt->PPrevRx) {
				/*
				 * Nothing received
				 * restart link
				 */
				pPrt->PPrevFcs = FcsErrCts;
				pPrt->PPrevShorts = Shorts;
				return(SK_HW_PS_RESTART);
			} else {
				pPrt->PLipaAutoNeg = SK_LIPA_MANUAL;
			}
		}

		if (((RxCts - pPrt->PPrevRx) > pPrt->PRxLim) ||
		    (!(FcsErrCts - pPrt->PPrevFcs))) {
			/*
			 * Note: The compare with zero above has to be done
			 * the way shown, otherwise the Linux driver will
			 * have a problem.
			 */
			/*
			 * we received a bunch of frames or no
			 * CRC error occured on the network ->
			 * ok.
			 */
			pPrt->PPrevRx = RxCts;
			pPrt->PPrevFcs = FcsErrCts;
			pPrt->PPrevShorts = Shorts;

			return(SK_HW_PS_NONE) ;
		}

		pPrt->PPrevFcs = FcsErrCts;
	}


	if ((Shorts - pPrt->PPrevShorts) > CheckShorts) {
		SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
			("Short Event Count Restart Port %d \n", Port));
		Rtv = SK_HW_PS_RESTART;
	}

	pPrt->PPrevShorts = Shorts;
	pPrt->PPrevRx = RxCts;

	return(Rtv);
}


/******************************************************************************
 *
 * SkGePortCheckUp - Implementing of the Workaround Errata # 2
 *
 * return:
 *	0	o.k. nothing needed
 *	1	Restart needed on this port
 *	2	Link came up
 */
int	SkGePortCheckUp(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	IoC,		/* IO Context */
int	Port)		/* Which port should be checked */
{
	SK_GEPORT	*pPrt;		/* GIni Port struct pointer */

	pPrt = &pAC->GIni.GP[Port];

	switch (pPrt->PhyType) {
	case SK_PHY_XMAC:
		return (SkGePortCheckUpXmac(pAC, IoC, Port));
	case SK_PHY_BCOM:
		return (SkGePortCheckUpBcom(pAC, IoC, Port));
	case SK_PHY_LONE:
		return (SkGePortCheckUpLone(pAC, IoC, Port));
	case SK_PHY_NAT:
		return (SkGePortCheckUpNat(pAC, IoC, Port));
	}
	
	return(SK_HW_PS_NONE) ;
}


/******************************************************************************
 *
 * SkGePortCheckUpXmac - Implementing of the Workaround Errata # 2
 *
 * return:
 *	0	o.k. nothing needed
 *	1	Restart needed on this port
 *	2	Link came up
 */
static int	SkGePortCheckUpXmac(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	IoC,		/* IO Context */
int	Port)		/* Which port should be checked */
{
	SK_GEPORT	*pPrt;		/* GIni Port struct pointer */
	SK_BOOL		AutoNeg;	/* Is Autonegotiation used ? */
	SK_U16		Isrc;		/* Interrupt source register */
	SK_U32		GpReg;		/* General Purpose register value */
	SK_U16		IsrcSum;	/* Interrupt source register sum */
	SK_U16		LpAb;		/* Link Partner Ability */
	SK_U16		ResAb;		/* Resolved Ability */
	SK_U64		Shorts;		/* Short Event Counter */
	unsigned int	Len;
	SK_U8		NextMode;	/* Next AutoSensing Mode */
	SK_U16		ExtStat;	/* Extended Status Register */
	int		Done;

	pPrt = &pAC->GIni.GP[Port];

	if (pPrt->PHWLinkUp) {
		if (pPrt->PhyType != SK_PHY_XMAC) {
			return(SK_HW_PS_NONE) ;
		}
		else {
			return(SkGePortCheckShorts(pAC, IoC, Port)) ;
		}
	}

	IsrcSum = pPrt->PIsave;
	pPrt->PIsave = 0;

	/* Now wait for each ports link */
	if (pPrt->PLinkMode == SK_LMODE_HALF ||
	    pPrt->PLinkMode == SK_LMODE_FULL) {
		AutoNeg = SK_FALSE;
	} else {
		AutoNeg = SK_TRUE;
	}

	if (pPrt->PLinkBroken) {
		/* Link was broken */
		XM_IN32(IoC,Port,XM_GP_PORT, &GpReg) ;

		if ((GpReg & XM_GP_INP_ASS) == 0) {
			/* The Link is in sync */
			XM_IN16(IoC,Port,XM_ISRC, &Isrc) ;
			IsrcSum |= Isrc;
			SkXmAutoNegLipaXmac(pAC, IoC, Port, IsrcSum);
			if ((Isrc & XM_IS_INP_ASS) == 0) {
				/* It has been in sync since last Time */
				/* Restart the PORT */
	
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
					("Link in sync Restart Port %d\n",
					 Port));

				/*
				 * We now need to reinitialize the PrevSHorts
				 * counter.
				 */
				Len = sizeof(SK_U64);
				SkPnmiGetVar(pAC, IoC,
					OID_SKGE_STAT_RX_SHORTS,
					(char *) &Shorts,
					&Len,
					(SK_U32) SK_PNMI_PORT_PHYS2INST(Port));
				pPrt->PPrevShorts = Shorts;

				pAC->GIni.GP[Port].PLinkBroken = SK_FALSE ;

				/*
				 * Link Restart Workaround:
				 *  it may be possible that the other Link side
				 *  restarts its link as well an we detect
				 *  another LinkBroken. To prevent this
				 *  happening we check for a maximum number
				 *  of consecutive restart. If those happens,
				 *  we do NOT restart the active link and
				 *  check whether the lionk is now o.k.
				 */
				pAC->GIni.GP[Port].PLinkResCt ++;
				pPrt->PAutoNegTimeOut = 0;

				if (pAC->GIni.GP[Port].PLinkResCt <
					SK_MAX_LRESTART) {
					return(SK_HW_PS_RESTART) ;
				}

				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("Do NOT restart on Port %d %x %x\n",
					Port, Isrc, IsrcSum));
				pAC->GIni.GP[Port].PLinkResCt = 0;
			} else {
				pPrt->PIsave = (SK_U16) (IsrcSum & (XM_IS_AND));
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("Save Sync/nosync Port %d %x %x\n",
					Port, Isrc, IsrcSum));
				/* Do nothing more if link is broken */
				return(SK_HW_PS_NONE) ;
			}
		} else {
			/* Do nothing more if link is broken */
			return(SK_HW_PS_NONE) ;
		}

	} else {
		/* Link was not broken, check if it is */
		XM_IN16(IoC,Port,XM_ISRC, &Isrc) ;
		IsrcSum |= Isrc;
		if ((Isrc & XM_IS_INP_ASS) == XM_IS_INP_ASS) {
			XM_IN16(IoC,Port,XM_ISRC, &Isrc) ;
			IsrcSum |= Isrc;
			if ((Isrc & XM_IS_INP_ASS) == XM_IS_INP_ASS) {
				XM_IN16(IoC,Port,XM_ISRC, &Isrc) ;
				IsrcSum |= Isrc;
				if ((Isrc & XM_IS_INP_ASS) == XM_IS_INP_ASS) {
					pPrt->PLinkBroken = SK_TRUE ;
					/*
					 * Re-Init Link partner Autoneg flag
					 */
					pPrt->PLipaAutoNeg = SK_LIPA_UNKNOWN;
					SK_DBG_MSG(pAC,SK_DBGMOD_HWM,
					   SK_DBGCAT_IRQ,
					   ("Link broken Port %d\n",
						 Port));

					/* cable removed-> reinit Sensemode */
					/* Init default sense mode */
					SkHWInitDefSense(pAC, IoC, Port);

					return(SK_HW_PS_RESTART) ;
				}
			}
		} else {
			SkXmAutoNegLipaXmac(pAC, IoC, Port, Isrc);
			if (SkGePortCheckShorts(pAC, IoC, Port) ==
				SK_HW_PS_RESTART) {
				return(SK_HW_PS_RESTART) ;
			}
		}
	}

	/*
	 * here we usually can check whether the link is in sync and
	 * autonegotiation is done.
	 */
	XM_IN32(IoC,Port,XM_GP_PORT, &GpReg) ;
	XM_IN16(IoC,Port,XM_ISRC, &Isrc) ;
	IsrcSum |= Isrc;

	SkXmAutoNegLipaXmac(pAC, IoC, Port, IsrcSum);
	if ((GpReg & XM_GP_INP_ASS) != 0 || (IsrcSum & XM_IS_INP_ASS) != 0) {
		if ((GpReg & XM_GP_INP_ASS) == 0) {
			/*
			 * Save Autonegotiation Done interrupt only if link 
			 * is in sync
			 */
			pPrt->PIsave = (SK_U16) (IsrcSum & (XM_IS_AND));
		}
#ifdef	DEBUG
		if (pPrt->PIsave & (XM_IS_AND)) {
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
				("AutoNeg done rescheduled Port %d\n", Port));
		}
#endif
		return(SK_HW_PS_NONE) ;
	}

	if (AutoNeg) {
		if (IsrcSum & XM_IS_AND) {
			SkHWLinkUp(pAC, IoC, Port) ;
			Done = SkXmAutoNegDone(pAC,IoC,Port);
			if (Done != SK_AND_OK) {
				/* Get PHY parameters, for debuging only */
				PHY_READ(IoC, pPrt, Port, PHY_XMAC_AUNE_LP,
					&LpAb);
				PHY_READ(IoC, pPrt, Port, PHY_XMAC_RES_ABI,
					&ResAb);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("AutoNeg FAIL Port %d (LpAb %x, ResAb %x)\n",
					 Port, LpAb, ResAb));
					
				/* Try next possible mode */
				NextMode = SkHWSenseGetNext(pAC, IoC, Port);
				SkHWLinkDown(pAC, IoC, Port) ;
				if (Done == SK_AND_DUP_CAP) {
					/* GoTo next mode */
					SkHWSenseSetNext(pAC, IoC, Port,
						NextMode);
				}

				return(SK_HW_PS_RESTART) ;

			} else {
				/*
				 * Dummy Read extended status to prevent
				 * extra link down/ups
				 * (clear Page Received bit if set)
				 */
				PHY_READ(IoC, pPrt, Port, PHY_XMAC_AUNE_EXP, &ExtStat);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("AutoNeg done Port %d\n", Port));
				return(SK_HW_PS_LINK) ;
			}
		} 
		
		/*
		 * AutoNeg not done, but HW link is up. Check for timeouts
		 */
		pPrt->PAutoNegTimeOut ++;
		if (pPrt->PAutoNegTimeOut >= SK_AND_MAX_TO) {
			/*
			 * Timeout occured.
			 * What do we need now?
			 */
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,
				SK_DBGCAT_IRQ,
				("AutoNeg timeout Port %d\n",
				 Port));
			if (pPrt->PLinkModeConf == SK_LMODE_AUTOSENSE &&
				pPrt->PLipaAutoNeg != SK_LIPA_AUTO) {
				/*
				 * Timeout occured
				 * Set Link manually up.
				 */
				SkHWSenseSetNext(pAC, IoC, Port,
					SK_LMODE_FULL);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,
					SK_DBGCAT_IRQ,
					("Set manual full duplex Port %d\n",
					 Port));
			}

			/*
			 * Do the restart
			 */
			return(SK_HW_PS_RESTART) ;
		}
	} else {
		/*
		 * Link is up and we don't need more.
		 */
#ifdef	DEBUG
		if (pPrt->PLipaAutoNeg == SK_LIPA_AUTO) {
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
				("ERROR: Lipa auto detected on port %d\n",
				Port));
		}
#endif

		SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
			("Link sync(GP), Port %d\n", Port));
		SkHWLinkUp(pAC, IoC, Port) ;
		return(SK_HW_PS_LINK) ;
	}

	return(SK_HW_PS_NONE) ;
}


/******************************************************************************
 *
 * SkGePortCheckUpBcom - Check, if the link is up
 *
 * return:
 *	0	o.k. nothing needed
 *	1	Restart needed on this port
 *	2	Link came up
 */
static int	SkGePortCheckUpBcom(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	IoC,		/* IO Context */
int	Port)		/* Which port should be checked */
{
	SK_GEPORT	*pPrt;		/* GIni Port struct pointer */
	SK_BOOL		AutoNeg;	/* Is Autonegotiation used ? */
	SK_U16		Isrc;		/* Interrupt source register */
	SK_U16		LpAb;		/* Link Partner Ability */
	SK_U16		ExtStat;	/* Extended Status Register */
	SK_U16		PhyStat;	/* Phy Status Register */
	int		Done;
	SK_U16		ResAb;
	SK_U16		SWord;

	pPrt = &pAC->GIni.GP[Port];

	/* Check for No HCD Link events (#10523) */
	PHY_READ(IoC, pPrt, Port, PHY_BCOM_INT_STAT, &Isrc);
	if ((Isrc & PHY_B_IS_NO_HDCL) == PHY_B_IS_NO_HDCL) {

		/* Workaround BCOM Errata */
		/* enable and disable Loopback mode if NO HCD occurs */
		PHY_READ(IoC, pPrt, Port, PHY_BCOM_CTRL, &SWord);
		PHY_WRITE(IoC, pPrt, Port, PHY_BCOM_CTRL, SWord | PHY_CT_LOOP);
		PHY_WRITE(IoC, pPrt, Port, PHY_BCOM_CTRL, SWord & ~PHY_CT_LOOP);
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("No HCD Link event, Port %d\n", Port));
	}

	PHY_READ(IoC, pPrt, Port, PHY_BCOM_STAT, &PhyStat);

	if (pPrt->PHWLinkUp) {
		return(SK_HW_PS_NONE) ;
	}

	pPrt->PIsave = 0;

	/* Now wait for each port's link */
	if (pPrt->PLinkMode == SK_LMODE_HALF ||
	    pPrt->PLinkMode == SK_LMODE_FULL) {
		AutoNeg = SK_FALSE;
	} else {
		AutoNeg = SK_TRUE;
	}

	/*
	 * here we usually can check whether the link is in sync and
	 * autonegotiation is done.
	 */
	XM_IN16(IoC, Port, XM_ISRC, &Isrc) ;

	PHY_READ(IoC, pPrt, Port, PHY_BCOM_STAT, &PhyStat);

	SkXmAutoNegLipaBcom(pAC, IoC, Port, PhyStat);
	
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("AutoNeg:%d, PhyStat: %Xh.\n", AutoNeg, PhyStat));

	PHY_READ(IoC, pPrt, Port, PHY_BCOM_1000T_STAT, &ResAb);

	if ((PhyStat & PHY_ST_LSYNC) == 0) {
		if (ResAb & (PHY_B_1000S_MSF)) {
			/* Error */
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
				("Master/Slave Fault port %d\n", Port));
			pPrt->PAutoNegFail = SK_TRUE;
			pPrt->PMSStatus = SK_MS_STAT_FAULT;
			return (SK_AND_OTHER);
		}
		return (SK_HW_PS_NONE);
	}
	
	if (ResAb & (PHY_B_1000S_MSF)) {
		/* Error */
		SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
			("Master/Slave Fault port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		pPrt->PMSStatus = SK_MS_STAT_FAULT;
		return (SK_AND_OTHER);
	} else if (ResAb & PHY_B_1000S_MSR) {
		pPrt->PMSStatus = SK_MS_STAT_MASTER;
	} else {
		pPrt->PMSStatus = SK_MS_STAT_SLAVE;
	}
	
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("AutoNeg:%d, PhyStat: %Xh.\n", AutoNeg, PhyStat));

	if (AutoNeg) {
		if (PhyStat & PHY_ST_AN_OVER) {
			SkHWLinkUp(pAC, IoC, Port);
			Done = SkXmAutoNegDone(pAC,IoC,Port);
			if (Done != SK_AND_OK) {
				/* Get PHY parameters, for debuging only */
				PHY_READ(IoC, pPrt, Port,
					PHY_BCOM_AUNE_LP,
					&LpAb);
				PHY_READ(IoC, pPrt, Port,
					PHY_BCOM_1000T_STAT,
					&ExtStat);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("AutoNeg FAIL Port %d (LpAb %x, "
					"1000TStat %x)\n",
					 Port, LpAb, ExtStat));
				return(SK_HW_PS_RESTART) ;

			} else {
				/*
				 * Dummy Read interrupt status to prevent
				 * extra link down/ups
				 */
				PHY_READ(IoC, pPrt, Port, PHY_BCOM_INT_STAT,
					&ExtStat);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("AutoNeg done Port %d\n", Port));
				return(SK_HW_PS_LINK) ;
			}
		} 
	} else {
		/*
		 * Link is up and we don't need more.
		 */
#ifdef	DEBUG
		if (pPrt->PLipaAutoNeg == SK_LIPA_AUTO) {
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
				("ERROR: Lipa auto detected on port %d\n",
				Port));
		}
#endif

#if 0
		PHY_READ(IoC, pPrt, Port, PHY_BCOM_1000T_STAT, &ResAb);
		if (ResAb & (PHY_B_1000S_MSF)) {
			/* Error */
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
				("Master/Slave Fault port %d\n", Port));
			pPrt->PAutoNegFail = SK_TRUE;
			pPrt->PMSStatus = SK_MS_STAT_FAULT;
			return (SK_AND_OTHER);
		} else if (ResAb & PHY_B_1000S_MSR) {
			pPrt->PMSStatus = SK_MS_STAT_MASTER ;
		} else {
			pPrt->PMSStatus = SK_MS_STAT_SLAVE ;
		}
#endif	/* 0 */


		/*
		 * Dummy Read interrupt status to prevent
		 * extra link down/ups
		 */
		PHY_READ(IoC, pPrt, Port, PHY_BCOM_INT_STAT, &ExtStat);
		
		SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
			("Link sync(GP), Port %d\n", Port));
		SkHWLinkUp(pAC, IoC, Port) ;
		return(SK_HW_PS_LINK) ;
	}

	return(SK_HW_PS_NONE) ;
}

/******************************************************************************
 *
 * SkGePortCheckUpLone - Check if the link is up
 *
 * return:
 *	0	o.k. nothing needed
 *	1	Restart needed on this port
 *	2	Link came up
 */
static int	SkGePortCheckUpLone(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	IoC,		/* IO Context */
int	Port)		/* Which port should be checked */
{
	SK_GEPORT	*pPrt;		/* GIni Port struct pointer */
	SK_BOOL		AutoNeg;	/* Is Autonegotiation used ? */
	SK_U16		Isrc;		/* Interrupt source register */
	SK_U16		LpAb;		/* Link Partner Ability */
	SK_U8		NextMode;	/* Next AutoSensing Mode */
	SK_U16		ExtStat;	/* Extended Status Register */
	SK_U16		PhyStat;	/* Phy Status Register */
	SK_U16		StatSum;
	int		Done;

	pPrt = &pAC->GIni.GP[Port];

	if (pPrt->PHWLinkUp) {
		return(SK_HW_PS_NONE) ;
	}

	StatSum = pPrt->PIsave;
	pPrt->PIsave = 0;

	/* Now wait for each ports link */
	if (pPrt->PLinkMode == SK_LMODE_HALF ||
	    pPrt->PLinkMode == SK_LMODE_FULL) {
		AutoNeg = SK_FALSE;
	} else {
		AutoNeg = SK_TRUE;
	}

	/*
	 * here we usually can check whether the link is in sync and
	 * autonegotiation is done.
	 */
	XM_IN16(IoC, Port, XM_ISRC, &Isrc) ;
	PHY_READ(IoC, pPrt, Port, PHY_LONE_STAT, &PhyStat);
	StatSum |= PhyStat;

	SkXmAutoNegLipaLone(pAC, IoC, Port, PhyStat);
	if ((PhyStat & PHY_ST_LSYNC) == 0){
		/*
		 * Save Autonegotiation Done bit
		 */
		pPrt->PIsave = (SK_U16) (StatSum & PHY_ST_AN_OVER);
#ifdef DEBUG
		if (pPrt->PIsave & PHY_ST_AN_OVER) {
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
				("AutoNeg done rescheduled Port %d\n", Port));
		}
#endif
		return(SK_HW_PS_NONE) ;
	}

	if (AutoNeg) {
		if (StatSum & PHY_ST_AN_OVER) {
			SkHWLinkUp(pAC, IoC, Port) ;
			Done = SkXmAutoNegDone(pAC,IoC,Port);
			if (Done != SK_AND_OK) {
				/* Get PHY parameters, for debuging only */
				PHY_READ(IoC, pPrt, Port,
					PHY_LONE_AUNE_LP,
					&LpAb);
				PHY_READ(IoC, pPrt, Port,
					PHY_LONE_1000T_STAT,
					&ExtStat);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("AutoNeg FAIL Port %d (LpAb %x, 1000TStat %x)\n",
					 Port, LpAb, ExtStat));
					
				/* Try next possible mode */
				NextMode = SkHWSenseGetNext(pAC, IoC, Port);
				SkHWLinkDown(pAC, IoC, Port) ;
				if (Done == SK_AND_DUP_CAP) {
					/* GoTo next mode */
					SkHWSenseSetNext(pAC, IoC, Port,
						NextMode);
				}

				return(SK_HW_PS_RESTART) ;

			} else {
				/*
				 * Dummy Read interrupt status to prevent
				 * extra link down/ups
				 */
				PHY_READ(IoC, pPrt, Port, PHY_LONE_INT_STAT,
					&ExtStat);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
					("AutoNeg done Port %d\n", Port));
				return(SK_HW_PS_LINK) ;
			}
		} 
		
		/*
		 * AutoNeg not done, but HW link is up. Check for timeouts
		 */
		pPrt->PAutoNegTimeOut ++;
		if (pPrt->PAutoNegTimeOut >= SK_AND_MAX_TO) {
			/*
			 * Timeout occured.
			 * What do we need now?
			 */
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,
				SK_DBGCAT_IRQ,
				("AutoNeg timeout Port %d\n",
				 Port));
			if (pPrt->PLinkModeConf == SK_LMODE_AUTOSENSE &&
				pPrt->PLipaAutoNeg != SK_LIPA_AUTO) {
				/*
				 * Timeout occured
				 * Set Link manually up.
				 */
				SkHWSenseSetNext(pAC, IoC, Port,
					SK_LMODE_FULL);
				SK_DBG_MSG(pAC,SK_DBGMOD_HWM,
					SK_DBGCAT_IRQ,
					("Set manual full duplex Port %d\n",
					 Port));
			}

			/*
			 * Do the restart
			 */
			return(SK_HW_PS_RESTART) ;
		}
	} else {
		/*
		 * Link is up and we don't need more.
		 */
#ifdef	DEBUG
		if (pPrt->PLipaAutoNeg == SK_LIPA_AUTO) {
			SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_CTRL,
				("ERROR: Lipa auto detected on port %d\n",
				Port));
		}
#endif

		/*
		 * Dummy Read interrupt status to prevent
		 * extra link down/ups
		 */
		PHY_READ(IoC, pPrt, Port, PHY_LONE_INT_STAT, &ExtStat);
		
		SK_DBG_MSG(pAC,SK_DBGMOD_HWM,SK_DBGCAT_IRQ,
			("Link sync(GP), Port %d\n", Port));
		SkHWLinkUp(pAC, IoC, Port) ;
		return(SK_HW_PS_LINK) ;
	}

	return(SK_HW_PS_NONE) ;
}


/******************************************************************************
 *
 * SkGePortCheckUpNat - Check if the link is up
 *
 * return:
 *	0	o.k. nothing needed
 *	1	Restart needed on this port
 *	2	Link came up
 */
static int	SkGePortCheckUpNat(
SK_AC	*pAC,		/* Adapters context */
SK_IOC	IoC,		/* IO Context */
int	Port)		/* Which port should be checked */
{
	/* todo: National */
	return(SK_HW_PS_NONE) ;
}


/******************************************************************************
 *
 *	Event service routine
 *
 * Description:
 *
 * Notes:
 */
int	SkGeSirqEvent(
SK_AC		*pAC,		/* Adapters context */
SK_IOC		IoC,		/* Io Context */
SK_U32		Event,		/* Module specific Event */
SK_EVPARA	Para)		/* Event specific Parameter */
{
	SK_U32	Port;
	SK_U32	Time;
	SK_U8	Val8 ;
	int	PortStat;

	Port = Para.Para32[0];

	switch (Event) {
	case SK_HWEV_WATIM:
		/* Check whether port came up */
		PortStat = SkGePortCheckUp(pAC, IoC, Port);

		switch (PortStat) {
		case SK_HW_PS_RESTART:
			if (pAC->GIni.GP[Port].PHWLinkUp) {
				/*
				 * Set Link to down.
				 */
				SkHWLinkDown(pAC, IoC, Port);

				/*
				 * Signal directly to RLMT to ensure correct
				 * sequence of SWITCH and RESET event.
				 */
				Para.Para32[0] = (SK_U32) Port;
				SkRlmtEvent(pAC, IoC, SK_RLMT_LINK_DOWN, Para);

				/* Start workaround Errata #2 timer */
				SkTimerStart(pAC, IoC,
					&pAC->GIni.GP[Port].PWaTimer,
					SK_WA_INA_TIME,
					SKGE_HWAC,
					SK_HWEV_WATIM,
					Para);
			}

			/* Restart needed */
			SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_RESET, Para);
			break;

		case SK_HW_PS_LINK:
			/* Signal to RLMT */
			SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_UP, Para);
			break;

		}

		/* Start again the check Timer */
		if (pAC->GIni.GP[Port].PHWLinkUp) {
			Time = SK_WA_ACT_TIME;
		} else {
			Time = SK_WA_INA_TIME;
		}

		/* todo: still needed for non-Xmac-PHYs ??? */
		/* Start workaround Errata #2 timer */
		SkTimerStart(pAC, IoC, &pAC->GIni.GP[Port].PWaTimer, Time,
			SKGE_HWAC, SK_HWEV_WATIM, Para);

		break;

	case SK_HWEV_PORT_START:
		if (pAC->GIni.GP[Port].PHWLinkUp) {
			/*
			 * Signal directly to RLMT to ensure correct
			 * sequence of SWITCH and RESET event.
			 */
			Para.Para32[0] = (SK_U32) Port;
			SkRlmtEvent(pAC, IoC, SK_RLMT_LINK_DOWN, Para);
		}

		SkHWLinkDown(pAC, IoC, Port) ;

		/* Schedule Port RESET */
		SkEventQueue(pAC, SKGE_DRV, SK_DRV_PORT_RESET, Para);

		/* Start workaround Errata #2 timer */
		SkTimerStart(pAC, IoC, &pAC->GIni.GP[Port].PWaTimer,
			SK_WA_INA_TIME,SKGE_HWAC,SK_HWEV_WATIM,Para);
		break;

	case SK_HWEV_PORT_STOP:
		if (pAC->GIni.GP[Port].PHWLinkUp) {
			/*
			 * Signal directly to RLMT to ensure correct
			 * sequence of SWITCH and RESET event.
			 */
			Para.Para32[0] = (SK_U32) Port;
			SkRlmtEvent(pAC, IoC, SK_RLMT_LINK_DOWN, Para);
		}
		/* Stop Workaround Timer */
		SkTimerStop(pAC, IoC, &pAC->GIni.GP[Port].PWaTimer) ;

		SkHWLinkDown(pAC, IoC, Port) ;
		break;

	case SK_HWEV_UPDATE_STAT:
		/* We do NOT need to update any statistics */
		break;

	case SK_HWEV_CLEAR_STAT:
		/* We do NOT need to clear any statistics */
		for (Port = 0; Port < (SK_U32) pAC->GIni.GIMacsFound; Port++) {
			pAC->GIni.GP[Port].PPrevRx = 0;
			pAC->GIni.GP[Port].PPrevFcs = 0;
			pAC->GIni.GP[Port].PPrevShorts = 0;
		}
		break;

	case SK_HWEV_SET_LMODE:
		Val8 = (SK_U8) Para.Para32[1];
		if (pAC->GIni.GP[Port].PLinkModeConf != Val8) {
			/* Set New link mode */
			pAC->GIni.GP[Port].PLinkModeConf = Val8;

			/* Restart Port */
			SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_STOP, Para);
			SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_START, Para);
		}
		break;

	case SK_HWEV_SET_FLOWMODE:
		Val8 = (SK_U8) Para.Para32[1];
		if (pAC->GIni.GP[Port].PFlowCtrlMode != Val8) {
			/* Set New Flow Control mode */
			pAC->GIni.GP[Port].PFlowCtrlMode = Val8;

			/* Restart Port */
			SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_STOP, Para);
			SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_START, Para);
		}
		break;

	case SK_HWEV_SET_ROLE:
		Val8 = (SK_U8) Para.Para32[1];
		if (pAC->GIni.GP[Port].PMSMode != Val8) {
			/* Set New link mode */
			pAC->GIni.GP[Port].PMSMode = Val8;

			/* Restart Port */
			SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_STOP, Para);
			SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_PORT_START, Para);
		}
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_SIRQ_E001,
			SKERR_SIRQ_E001MSG);
		break;
	}

	return(0) ;
}


/******************************************************************************
 *
 *	SkPhyIsrBcom - PHY interrupt service routine
 *
 * Description: handle all interrupts from BCOM PHY
 *
 * Returns: N/A
 */
static void SkPhyIsrBcom(
SK_AC		*pAC,		/* Adapters context */
SK_IOC		IoC,		/* Io Context */
int		Port,		/* Port Num = PHY Num */
SK_U16		IStatus)	/* Interrupts masked with PHY-Mask */
{
	SK_EVPARA	Para;

	if (IStatus & PHY_B_IS_PSE) {
		/* incorrectable pair swap error */
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_SIRQ_E022,
			SKERR_SIRQ_E022MSG) ;
	}
	
	if (IStatus & PHY_B_IS_MDXI_SC) {
		/* not used */
	}
	
	if (IStatus & PHY_B_IS_HCT) {
		/* not used */
	}
	
	if (IStatus & PHY_B_IS_LCT) {
		/* not used */
	}
	
	if (IStatus & (PHY_B_IS_AN_PR | PHY_B_IS_LST_CHANGE)) {
		SkHWLinkDown(pAC, IoC, Port);

		/* Signal to RLMT */
		Para.Para32[0] = (SK_U32) Port;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);

		/* Start workaround Errata #2 timer */
		SkTimerStart(pAC, IoC, &pAC->GIni.GP[Port].PWaTimer,
			SK_WA_INA_TIME,SKGE_HWAC,SK_HWEV_WATIM,Para);
	}

	if (IStatus & PHY_B_IS_NO_HDCL) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_NO_HDC) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_NEG_USHDC) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_SCR_S_ER) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_RRS_CHANGE) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_LRS_CHANGE) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_DUP_CHANGE) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_LSP_CHANGE) {
		/* not used */
	}

	if (IStatus & PHY_B_IS_CRC_ER) {
		/* not used */
	}

}


/******************************************************************************
 *
 *	SkPhyIsrLone - PHY interrupt service routine
 *
 * Description: handle all interrupts from LONE PHY
 *
 * Returns: N/A
 */
static void SkPhyIsrLone(
SK_AC		*pAC,		/* Adapters context */
SK_IOC		IoC,		/* Io Context */
int		Port,		/* Port Num = PHY Num */
SK_U16		IStatus)	/* Interrupts masked with PHY-Mask */
{
	SK_EVPARA	Para;

	if (IStatus & PHY_L_IS_CROSS) {
		/* not used */
	}
	
	if (IStatus & PHY_L_IS_POL) {
		/* not used */
	}
	
	if (IStatus & PHY_L_IS_SS) {
		/* not used */
	}
	
	if (IStatus & PHY_L_IS_CFULL) {
		/* not used */
	}
	
	if (IStatus & PHY_L_IS_AN_C) {
		/* not used */
	}
	
	if (IStatus & PHY_L_IS_SPEED) {
		/* not used */
	}
	
	if (IStatus & PHY_L_IS_CFULL) {
		/* not used */
	}
	
	if (IStatus & (PHY_L_IS_DUP | PHY_L_IS_ISOL)) {
		SkHWLinkDown(pAC, IoC, Port);

		/* Signal to RLMT */
		Para.Para32[0] = (SK_U32) Port;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);

		/* Start workaround Errata #2 timer */
		SkTimerStart(pAC, IoC, &pAC->GIni.GP[Port].PWaTimer,
			SK_WA_INA_TIME,SKGE_HWAC,SK_HWEV_WATIM,Para);
	}

	if (IStatus & PHY_L_IS_MDINT) {
		/* not used */
	}

}


/* End of File */
