/******************************************************************************
 *
 * Name:	skgeinit.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.46 $
 * Date:	$Date: 2000/08/10 11:28:00 $
 * Purpose:	Structures and prototypes for the GE Init Module
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
 *	$Log: skgeinit.h,v $
 *	Revision 1.46  2000/08/10 11:28:00  rassmann
 *	Editorial changes.
 *	Preserving 32-bit alignment in structs for the adapter context.
 *	
 *	Revision 1.45  1999/11/22 13:56:19  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.44  1999/10/26 07:34:15  malthoff
 *	The define SK_LNK_ON has been lost in v1.41.
 *	
 *	Revision 1.43  1999/10/06 09:30:16  cgoos
 *	Changed SK_XM_THR_JUMBO.
 *	
 *	Revision 1.42  1999/09/16 12:58:26  cgoos
 *	Changed SK_LED_STANDY macro to be independent of HW link sync.
 *	
 *	Revision 1.41  1999/07/30 06:56:14  malthoff
 *	Correct comment for SK_MS_STAT_UNSET.
 *	
 *	Revision 1.40  1999/05/27 13:38:46  cgoos
 *	Added SK_BMU_TX_WM.
 *	Made SK_BMU_TX_WM and SK_BMU_RX_WM user-definable.
 *	Changed XMAC Tx treshold to max. values.
 *	
 *	Revision 1.39  1999/05/20 14:35:26  malthoff
 *	Remove prototypes for SkGeLinkLED().
 *	
 *	Revision 1.38  1999/05/19 11:59:12  cgoos
 *	Added SK_MS_CAP_INDETERMINATED define.
 *	
 *	Revision 1.37  1999/05/19 07:32:33  cgoos
 *	Changes for 1000Base-T.
 *	LED-defines for HWAC_LINK_LED macro.
 *	
 *	Revision 1.36  1999/04/08 14:00:24  gklug
 *	add:Port struct field PLinkResCt
 *	
 *	Revision 1.35  1999/03/25 07:43:07  malthoff
 *	Add error string for SKERR_HWI_E018MSG.
 *	
 *	Revision 1.34  1999/03/12 16:25:57  malthoff
 *	Remove PPollRxD and PPollTxD.
 *	Add SKERR_HWI_E017MSG. and SK_DPOLL_MAX.
 *	
 *	Revision 1.33  1999/03/12 13:34:41  malthoff
 *	Add Autonegotiation error codes.
 *	Change defines for parameter Mode in SkXmSetRxCmd().
 *	Replace __STDC__ by SK_KR_PROTO.
 *	
 *	Revision 1.32  1999/01/25 14:40:20  mhaveman
 *	Added new return states for the virtual management port if multiple
 *	ports are active but differently configured.
 *	
 *	Revision 1.31  1998/12/11 15:17:02  gklug
 *	add: Link partnet autoneg states : Unknown Manual and Autonegotiation
 *	
 *	Revision 1.30  1998/12/07 12:17:04  gklug
 *	add: Link Partner autonegotiation flag
 *	
 *	Revision 1.29  1998/12/01 10:54:42  gklug
 *	add: variables for XMAC Errata
 *	
 *	Revision 1.28  1998/12/01 10:14:15  gklug
 *	add: PIsave saves the Interrupt status word
 *	
 *	Revision 1.27  1998/11/26 15:24:52  mhaveman
 *	Added link status states SK_LMODE_STAT_AUTOHALF and
 *	SK_LMODE_STAT_AUTOFULL which are used by PNMI.
 *	
 *	Revision 1.26  1998/11/26 14:53:01  gklug
 *	add:autoNeg Timeout variable
 *	
 *	Revision 1.25  1998/11/26 08:58:50  gklug
 *	add: Link Mode configuration (AUTO Sense mode)
 *	
 *	Revision 1.24  1998/11/24 13:30:27  gklug
 *	add: PCheckPar to port struct
 *	
 *	Revision 1.23  1998/11/18 13:23:26  malthoff
 *	Add SK_PKT_TO_MAX.
 *	
 *	Revision 1.22  1998/11/18 13:19:54  gklug
 *	add: PPrevShorts and PLinkBroken to port struct for WA XMAC Errata #C1
 *
 *	Revision 1.21  1998/10/26 08:02:57  malthoff
 *	Add GIRamOffs.
 *	
 *	Revision 1.20  1998/10/19 07:28:37  malthoff
 *	Add prototyp for SkGeInitRamIface().
 *	
 *	Revision 1.19  1998/10/14 14:47:48  malthoff
 *	SK_TIMER should not be defined for Diagnostics.
 *	Add SKERR_HWI_E015MSG and SKERR_HWI_E016MSG.
 *	
 *	Revision 1.18  1998/10/14 14:00:03  gklug
 *	add: timer to port struct for workaround of Errata #2
 *	
 *	Revision 1.17  1998/10/14 11:23:09  malthoff
 *	Add prototype for SkXmAutoNegDone().
 *	Fix SkXmSetRxCmd() prototype statement.
 *
 *	Revision 1.16  1998/10/14 05:42:29  gklug
 *	add: HWLinkUp flag to Port struct
 *	
 *	Revision 1.15  1998/10/09 08:26:33  malthoff
 *	Rename SK_RB_ULPP_B to SK_RB_LLPP_B.
 *	
 *	Revision 1.14  1998/10/09 07:11:13  malthoff
 *	bug fix: SK_FACT_53 is 85 not 117.
 *	Rework time out init values.
 *	Add GIPortUsage and corresponding defines.
 *	Add some error log messages.
 *	
 *	Revision 1.13  1998/10/06 14:13:14  malthoff
 *	Add prototyp for SkGeLoadLnkSyncCnt().
 *
 *	Revision 1.12  1998/10/05 11:29:53  malthoff
 *	bug fix: A comment was not closed.
 *
 *	Revision 1.11  1998/10/05 08:01:59  malthoff
 *	Add default Timeout- Threshold- and
 *	Watermark constants. Add QRam start and end
 *	variables. Also add vars to store the polling
 *	mode and receive command. Add new Error Log
 *	Messages and function prototypes.
 *
 *	Revision 1.10  1998/09/28 13:34:48  malthoff
 *	Add mode bits for LED functions.
 *	Move Autoneg and Flow Ctrl bits from shgesirq.h
 *	Add the required Error Log Entries
 *	and Function Prototypes.
 *
 *	Revision 1.9  1998/09/16 14:38:41  malthoff
 *	Rework the SK_LNK_xxx defines.
 *	Add error log message defines.
 *	Add prototypes for skxmac2.c
 *
 *	Revision 1.8  1998/09/11 05:29:18  gklug
 *	add: init state of a port
 *
 *	Revision 1.7  1998/09/08 08:35:52  gklug
 *	add: defines of the Init Levels
 *
 *	Revision 1.6  1998/09/03 13:48:42  gklug
 *	add: Link strati, capabilities to Port struct
 *
 *	Revision 1.5  1998/09/03 13:30:59  malthoff
 *	Add SK_LNK_BLINK and SK_LNK_PERM.
 *
 *	Revision 1.4  1998/09/03 09:55:31  malthoff
 *	Add constants for parameters Dir and RstMode
 *	when calling SkGeStopPort().
 *	Rework the prototyp section.
 *	Add Queue Address offsets PRxQOff, PXsQOff, and PXaQOff.
 *	Remove Ioc with IoC.
 *
 *	Revision 1.3  1998/08/19 09:11:54  gklug
 *	fix: struct are removed from c-source (see CCC)
 *	add: typedefs for all structs
 *
 *	Revision 1.2  1998/07/28 12:38:26  malthoff
 *	The prototypes got the parameter 'IoC'.
 *
 *	Revision 1.1  1998/07/23 09:50:24  malthoff
 *	Created.
 *
 *
 ******************************************************************************/

#ifndef __INC_SKGEINIT_H_
#define __INC_SKGEINIT_H_

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* defines ********************************************************************/

/*
 * defines for modifying Link LED behaviour (has been used with SkGeLinkLED())
 */
#define SK_LNK_OFF	LED_OFF
#define SK_LNK_ON	(LED_ON | LED_BLK_OFF| LED_SYNC_OFF)	
#define SK_LNK_BLINK	(LED_ON | LED_BLK_ON | LED_SYNC_ON)
#define SK_LNK_PERM	(LED_ON | LED_BLK_OFF| LED_SYNC_ON)
#define SK_LNK_TST	(LED_ON | LED_BLK_ON | LED_SYNC_OFF)

/*
 * defines for parameter 'Mode' when calling SK_HWAC_LINK_LED()
 */
#define SK_LED_OFF	LED_OFF
#define SK_LED_ACTIVE	(LED_ON | LED_BLK_OFF| LED_SYNC_OFF)
#define SK_LED_STANDBY	(LED_ON | LED_BLK_ON| LED_SYNC_OFF)

/*
 * defines for parameter 'Mode' when calling SkGeXmitLED()
 */
#define SK_LED_DIS	0
#define SK_LED_ENA	1
#define SK_LED_TST	2

/*
 * Counter and Timer constants, for a host clock of 62.5 MHz
 */
#define SK_XMIT_DUR	0x002faf08L		/*  50 ms */
#define SK_BLK_DUR	0x01dcd650L		/* 500 ms */

#define SK_DPOLL_DEF	0x00EE6B28L		/* 250 ms */
#define SK_DPOLL_MAX	0x00FFFFFFL		/* ca. 268ms */

#define SK_FACT_62	100			/* is given in percent */
#define SK_FACT_53	 85

/*
 * Timeout values
 */
#define SK_MAC_TO_53	72		/* MAC arbiter timeout */
#define SK_PKT_TO_53	0x2000		/* Packet arbiter timeout */
#define SK_PKT_TO_MAX	0xffff		/* Maximum value */
#define SK_RI_TO_53	36		/* RAM interface timeout */

/*
 * RAM Buffer High Pause Threshold values
 */
#define SK_RB_ULPP	( 8 * 1024)	/* Upper Level in kB/8 */
#define SK_RB_LLPP_S	(10 * 1024)	/* Lower Level for small Queues */
#define SK_RB_LLPP_B	(16 * 1024)	/* Lower Level for big Queues */

#ifndef SK_BMU_RX_WM
#define SK_BMU_RX_WM	0x600		/* BMU Rx Watermark */
#endif
#ifndef SK_BMU_TX_WM
#define SK_BMU_TX_WM	0x600		/* BMU Rx Watermark */
#endif

/* XMAC II Tx Threshold */
#define SK_XM_THR_REDL	0x01fb		/* .. for redundant link usage */
#define SK_XM_THR_SL	0x01fb		/* .. for single link adapters */
#define SK_XM_THR_MULL	0x01fb		/* .. for multiple link usage */
#define SK_XM_THR_JUMBO	0x03fc		/* .. for jumbo frame usage */

/* values for GIPortUsage */
#define SK_RED_LINK	1		/* redundant link usage */
#define SK_MUL_LINK	2		/* multiple link usage */
#define SK_JUMBO_LINK	3		/* driver uses jumbo frames */

/* Minimum RAM Buffer Receive Queue Size */
#define SK_MIN_RXQ_SIZE	16	/* 16 kB */
/*
 * defines for parameter 'Dir' when calling SkGeStopPort()
 */
#define	SK_STOP_TX	1	/* Stops the transmit path, resets the XMAC */
#define SK_STOP_RX	2	/* Stops the receive path */
#define SK_STOP_ALL	3	/* Stops rx and tx path, resets the XMAC */

/*
 * defines for parameter 'RstMode' when calling SkGeStopPort()
 */
#define SK_SOFT_RST	1	/* perform a software reset */
#define SK_HARD_RST	2	/* perform a hardware reset */

/*
 * Define Init Levels
 */
#define	SK_INIT_DATA	0	/* Init level 0: init data structures */
#define	SK_INIT_IO	1	/* Init level 1: init with IOs */
#define	SK_INIT_RUN	2	/* Init level 2: init for run time */

/*
 * Set Link Mode Parameter
 */
#define	SK_LMODE_HALF		1	/* Half Duplex Mode */
#define	SK_LMODE_FULL		2	/* Full Duplex Mode */
#define	SK_LMODE_AUTOHALF	3	/* AutoHalf Duplex Mode */
#define	SK_LMODE_AUTOFULL	4	/* AutoFull Duplex Mode */
#define	SK_LMODE_AUTOBOTH	5	/* AutoBoth Duplex Mode */
#define	SK_LMODE_AUTOSENSE	6	/* configured mode auto sensing */
#define SK_LMODE_INDETERMINATED	7	/* Return value for virtual port if
					 * multiple ports are differently
					 * configured.
					 */

/*
 * Autonegotiation timeout in 100ms granularity.
 */
#define	SK_AND_MAX_TO		6	/* Wait 600 msec before link comes up */

/*
 * Define Autonegotiation error codes here
 */
#define	SK_AND_OK		0	/* no error */
#define	SK_AND_OTHER		1	/* other error than below */
#define	SK_AND_DUP_CAP		2	/* Duplex capabilities error */

/*
 * Link Capability value
 */
#define	SK_LMODE_CAP_HALF	(1<<0)	/* Half Duplex Mode */
#define	SK_LMODE_CAP_FULL	(1<<1)	/* Full Duplex Mode */
#define	SK_LMODE_CAP_AUTOHALF	(1<<2)	/* AutoHalf Duplex Mode */
#define	SK_LMODE_CAP_AUTOFULL	(1<<3)	/* AutoFull Duplex Mode */
#define SK_LMODE_CAP_INDETERMINATED (1<<4) /* Return value for virtual port if
					 * multiple ports are differently
					 * configured.
					 */

/*
 * Link mode current state
 */
#define	SK_LMODE_STAT_UNKNOWN	1	/* Unknown Duplex Mode */
#define	SK_LMODE_STAT_HALF	2	/* Half Duplex Mode */
#define	SK_LMODE_STAT_FULL	3	/* Full Duplex Mode */
#define SK_LMODE_STAT_AUTOHALF	4	/* Half Duplex Mode obtained by AutoNeg */
#define SK_LMODE_STAT_AUTOFULL	5	/* Half Duplex Mode obtained by AutoNeg */
#define SK_LMODE_STAT_INDETERMINATED 6	/* Return value for virtual port if
					 * multiple ports are differently
					 * configured.
					 */
/*
 * Set Flow Control Mode Parameter (and capabilities)
 */
#define	SK_FLOW_MODE_NONE	1	/* No Flow Control */
#define	SK_FLOW_MODE_LOC_SEND	2	/* Local station sends PAUSE */
#define	SK_FLOW_MODE_SYMMETRIC	3	/* Both station may send PAUSE */
#define	SK_FLOW_MODE_SYM_OR_REM	4	/* Both station may send PAUSE or
					 * just the remote station may send
					 * PAUSE
					 */
#define SK_FLOW_MODE_INDETERMINATED 5	/* Return value for virtual port if
					 * multiple ports are differently
					 * configured.
					 */

/*
 * Flow Control Status Parameter
 */
#define	SK_FLOW_STAT_NONE	1	/* No Flow Control */
#define	SK_FLOW_STAT_REM_SEND	2	/* Remote Station sends PAUSE */
#define	SK_FLOW_STAT_LOC_SEND	3	/* Local station sends PAUSE */
#define	SK_FLOW_STAT_SYMMETRIC	4	/* Both station may send PAUSE */
#define SK_FLOW_STAT_INDETERMINATED 5	/* Return value for virtual port if
					 * multiple ports are differently
					 * configured.
					 */
/*
 * Master/Slave Mode capabilities
 */
#define	SK_MS_CAP_AUTO		(1<<0)	/* Automatic resolution */
#define	SK_MS_CAP_MASTER	(1<<1)	/* This station is master */
#define	SK_MS_CAP_SLAVE		(1<<2)	/* This station is slave */
#define	SK_MS_CAP_INDETERMINATED (1<<3)	/* Return value for virtual port if
					 * multiple ports are differently
					 * configured.
					 */

/*
 * Set Master/Slave Mode Parameter (and capabilities)
 */
#define	SK_MS_MODE_AUTO		1	/* Automatic resolution */
#define	SK_MS_MODE_MASTER	2	/* This station is master */
#define	SK_MS_MODE_SLAVE	3	/* This station is slave */
#define SK_MS_MODE_INDETERMINATED 4	/* Return value for virtual port if 
					 * multiple ports are differently
					 */

/*
 * Master/Slave Status Parameter
 */
#define	SK_MS_STAT_UNSET	1	/* The MS status is never been determ*/
#define	SK_MS_STAT_MASTER	2	/* This station is master */
#define	SK_MS_STAT_SLAVE	3	/* This station is slave */
#define	SK_MS_STAT_FAULT	4	/* MS resolution failed */
#define SK_MS_STAT_INDETERMINATED 5	/* Return value for virtual port if
					 * multiple ports are differently
					 */

/*
 * defines for parameter 'Mode' when calling SkXmSetRxCmd()
 */
#define SK_STRIP_FCS_ON		(1<<0)	/* Enable FCS stripping of rx frames */
#define SK_STRIP_FCS_OFF	(1<<1)	/* Disable FCS stripping of rx frames */
#define SK_STRIP_PAD_ON		(1<<2)	/* Enable pad byte stripping of rx f */
#define SK_STRIP_PAD_OFF	(1<<3)	/* Disable pad byte stripping of rx f */
#define SK_LENERR_OK_ON		(1<<4)	/* Don't chk fr for in range len error*/
#define SK_LENERR_OK_OFF	(1<<5)	/* Check frames for in range len error*/
#define SK_BIG_PK_OK_ON		(1<<6)	/* Don't set rcvError bit for big fr */
#define SK_BIG_PK_OK_OFF	(1<<7)	/* Set rcvError bit for big frames */	

/*
 * States of PState
 */
#define SK_PRT_RESET	0	/* the port is reset */
#define SK_PRT_STOP	1	/* the port is stopped (similar to sw reset) */
#define SK_PRT_INIT	2	/* the port is initialized */
#define SK_PRT_RUN	3	/* the port has an active link */

/*
 * Default receive frame limit for Workaround of XMAC Errata
 */
#define	SK_DEF_RX_WA_LIM	SK_CONSTU64(100)

/*
 * Define link partner Status
 */
#define	SK_LIPA_UNKNOWN	0	/* Link partner is in unknown state */
#define	SK_LIPA_MANUAL	1	/* Link partner is in detected manual state */
#define	SK_LIPA_AUTO	2	/* Link partner is in autonegotiation state */

/*
 * Define Maximum Restarts before restart is ignored (3com WA)
 */
#define	SK_MAX_LRESTART	3	/* Max. 3 times the link is restarted */

/* structures *****************************************************************/

/*
 * Port Structure
 */
typedef	struct s_GePort {
#ifndef SK_DIAG
	SK_TIMER	PWaTimer;	/* Workaround Timer */
#endif
	SK_U64	PPrevShorts;	/* Previous short Counter checking */
	SK_U64	PPrevRx;		/* Previous RxOk Counter checking */
	SK_U64	PPrevFcs;		/* Previous FCS Error Counter checking */
	SK_U64	PRxLim;			/* Previous RxOk Counter checking */
	int		PLinkResCt;		/* Link Restart Counter */
	int		PAutoNegTimeOut;/* AutoNegotiation timeout current value */
	int		PRxQSize;		/* Port Rx Queue Size in kB */
	int		PXSQSize;		/* Port Synchronous Transmit Queue Size in kB */
	int		PXAQSize;		/* Port Asynchronous Transmit Queue Size in kB*/
	SK_U32	PRxQRamStart;	/* Receive Queue RAM Buffer Start Address */
	SK_U32	PRxQRamEnd;		/* Receive Queue RAM Buffer End Address */
	SK_U32	PXsQRamStart;	/* Sync Tx Queue RAM Buffer Start Address */
	SK_U32	PXsQRamEnd;		/* Sync Tx Queue RAM Buffer End Address */
	SK_U32	PXaQRamStart;	/* Async Tx Queue RAM Buffer Start Address */
	SK_U32	PXaQRamEnd;		/* Async Tx Queue RAM Buffer End Address */
	int		PRxQOff;		/* Rx Queue Address Offset */
	int		PXsQOff;		/* Synchronous Tx Queue Address Offset */
	int		PXaQOff;		/* Asynchronous Tx Queue Address Offset */
	SK_U16	PRxCmd;			/* Port Receive Command Configuration Value */
	SK_U16	PIsave;			/* Saved Interrupt status word */
	SK_U16	PSsave;			/* Saved PHY status word */
	SK_BOOL	PHWLinkUp;		/* The hardware Link is up (wireing) */
	SK_BOOL	PState;			/* Is port initialized ? */
	SK_BOOL	PLinkBroken;	/* Is Link broken ? */
	SK_BOOL	PCheckPar;		/* Do we check for parity errors ? */
	SK_U8	PLinkCap;		/* Link Capabilities */
	SK_U8	PLinkModeConf;	/* Link Mode configured */
	SK_U8	PLinkMode;		/* Link Mode currently used */
	SK_U8	PLinkModeStatus;/* Link Mode Status */
	SK_U8	PFlowCtrlCap;	/* Flow Control Capabilities */
	SK_U8	PFlowCtrlMode;	/* Flow Control Mode */
	SK_U8	PFlowCtrlStatus;/* Flow Control Status */
	SK_U8	PMSCap;			/* Master/Slave Capabilities */
	SK_U8	PMSMode;		/* Master/Slave Mode */
	SK_U8	PMSStatus;		/* Master/Slave Status */
	SK_U8	PAutoNegFail;	/* Autonegotiation fail flag */
	SK_U8	PLipaAutoNeg;	/* Autonegotiation possible with Link Partner */
	SK_U16	PhyAddr;		/* MDIO/MDC PHY address */
	int	PhyType;	/* PHY used on this port */
} SK_GEPORT;

/*
 * Gigabit Ethernet Initalization Struct
 * (has to be included in the adapter context)
 */
typedef	struct s_GeInit {
	int			GIMacsFound;	/* Number of MACs found on this adapter */
	int			GIPciHwRev;		/* PCI HW Revision Number */
	SK_U32		GIRamOffs;		/* RAM Address Offset for addr calculation */
	int			GIRamSize;		/* The RAM size of the adapter in kB */
	int			GIHstClkFact;	/* Host Clock Factor (62.5 / HstClk * 100) */
	int			GIPortUsage;	/* driver port usage: SK_RED_LINK/SK_MUL_LINK */
	SK_U32		GIPollTimerVal;	/* Descriptor Poll Timer Init Val in clk ticks*/
	int			GILevel;		/* Initialization Level Completed */
	SK_GEPORT	GP[SK_MAX_MACS];/* Port Dependent Information */
	SK_BOOL		GIAnyPortAct;	/* Is True if one or more port is initialized */
	SK_U8		Align01;
	SK_U16		Align02;
} SK_GEINIT;

/*
 * Define the error numbers and messages for xmac_ii.c and skgeinit.c
 */
#define	SKERR_HWI_E001		(SK_ERRBASE_HWINIT)
#define	SKERR_HWI_E001MSG	"SkXmClrExactAddr() has got illegal parameters"
#define	SKERR_HWI_E002		(SKERR_HWI_E001+1)
#define	SKERR_HWI_E002MSG	"SkGeInit() Level 1 call missing"
#define	SKERR_HWI_E003		(SKERR_HWI_E002+1)
#define	SKERR_HWI_E003MSG	"SkGeInit() called with illegal init Level"
#define	SKERR_HWI_E004		(SKERR_HWI_E003+1)
#define	SKERR_HWI_E004MSG	"SkGeInitPort() Queue size illegal configured"
#define	SKERR_HWI_E005		(SKERR_HWI_E004+1)
#define	SKERR_HWI_E005MSG	"SkGeInitPort() cannot init running ports"
#define	SKERR_HWI_E006		(SKERR_HWI_E005+1)
#define	SKERR_HWI_E006MSG	"SkGeXmInit(): PState does not match HW state"
#define	SKERR_HWI_E007		(SKERR_HWI_E006+1)
#define	SKERR_HWI_E007MSG	"SkXmInitDupMd() called with invalid Dup Mode"
#define	SKERR_HWI_E008		(SKERR_HWI_E007+1)
#define	SKERR_HWI_E008MSG	"SkXmSetRxCmd() called with invalid Mode"
#define	SKERR_HWI_E009		(SKERR_HWI_E008+1)
#define	SKERR_HWI_E009MSG	"SkGeCfgSync() called although PXSQSize zero"
#define	SKERR_HWI_E010		(SKERR_HWI_E009+1)
#define	SKERR_HWI_E010MSG	"SkGeCfgSync() called with invalid parameters"
#define	SKERR_HWI_E011		(SKERR_HWI_E010+1)
#define	SKERR_HWI_E011MSG	"SkGeInitPort() Receive Queue Size to small"
#define	SKERR_HWI_E012		(SKERR_HWI_E011+1)
#define	SKERR_HWI_E012MSG	"SkGeInitPort() invalid Queue Size specified"
#define	SKERR_HWI_E013		(SKERR_HWI_E012+1)
#define	SKERR_HWI_E013MSG	"SkGeInitPort() cfg changed for running queue"
#define	SKERR_HWI_E014		(SKERR_HWI_E013+1)
#define	SKERR_HWI_E014MSG	"SkGeInitPort() unknown GIPortUsage specified"
#define	SKERR_HWI_E015		(SKERR_HWI_E014+1)
#define	SKERR_HWI_E015MSG	"Illegal Link mode parameter"
#define	SKERR_HWI_E016		(SKERR_HWI_E015+1)
#define	SKERR_HWI_E016MSG	"Illegal Flow control mode parameter"
#define	SKERR_HWI_E017		(SKERR_HWI_E016+1)
#define	SKERR_HWI_E017MSG	"Illegal value specified for GIPollTimerVal"
#define	SKERR_HWI_E018		(SKERR_HWI_E017+1)
#define	SKERR_HWI_E018MSG	"FATAL: SkGeStopPort() does not terminate"
#define	SKERR_HWI_E019		(SKERR_HWI_E018+1)
#define	SKERR_HWI_E019MSG	""

/* function prototypes ********************************************************/

#ifndef	SK_KR_PROTO

/*
 * public functions in skgeinit.c
 */
extern void	SkGePollRxD(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_BOOL		PollRxD);

extern void	SkGePollTxD(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_BOOL 	PollTxD);

extern void	SkGeYellowLED(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		State);

extern int	SkGeCfgSync(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_U32		IntTime,
	SK_U32		LimCount,
	int		SyncMode);

extern void	SkGeLoadLnkSyncCnt(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_U32		CntVal);

extern void	SkGeStopPort(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	int		Dir,
	int		RstMode);

extern int	SkGeInit(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Level);

extern void	SkGeDeInit(
	SK_AC		*pAC,
	SK_IOC		IoC);

extern int	SkGeInitPort(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkGeXmitLED(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Led,
	int		Mode);

extern void	SkGeInitRamIface(
	SK_AC		*pAC,
	SK_IOC		IoC);

/*
 * public functions in skxmac2.c
 */
extern void	SkXmSetRxCmd(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	int		Mode);

extern void	SkXmClrExactAddr(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	int		StartNum,
	int		StopNum);

extern void	SkXmFlushTxFifo(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkXmFlushRxFifo(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkXmSoftRst(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkXmHardRst(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkXmInitMac(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkXmInitDupMd(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkXmInitPauseMd(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern int	SkXmAutoNegDone(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port);

extern void	SkXmAutoNegLipaXmac(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_U16		IStatus);

extern void	SkXmAutoNegLipaBcom(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_U16		IStatus);

extern void	SkXmAutoNegLipaLone(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_U16		IStatus);

extern void	SkXmIrq(
	SK_AC		*pAC,
	SK_IOC		IoC,
	int		Port,
	SK_U16		IStatus);

#else	/* SK_KR_PROTO */

/*
 * public functions in skgeinit.c
 */
extern void	SkGePollRxD();
extern void	SkGePollTxD();
extern void	SkGeYellowLED();
extern int	SkGeCfgSync();
extern void	SkGeLoadLnkSyncCnt();
extern void	SkGeStopPort();
extern int	SkGeInit();
extern void	SkGeDeInit();
extern int	SkGeInitPort();
extern void	SkGeXmitLED();
extern void	SkGeInitRamIface();

/*
 * public functions in skxmac2.c
 */
extern void	SkXmSetRxCmd();
extern void	SkXmClrExactAddr();
extern void	SkXmFlushTxFifo();
extern void	SkXmFlushRxFifo();
extern void	SkXmSoftRst();
extern void	SkXmHardRst();
extern void	SkXmInitMac();
extern void	SkXmInitDupMd();
extern void	SkXmInitPauseMd();
extern int	SkXmAutoNegDone();
extern void	SkXmAutoNegLipa();
extern void	SkXmIrq();

#endif	/* SK_KR_PROTO */

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* __INC_SKGEINIT_H_ */
