/*****************************************************************************
 *
 * Name:	skgepnmi.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.78 $
 * Date:	$Date: 2000/09/12 10:44:58 $
 * Purpose:	Private Network Management Interface
 *
 ****************************************************************************/

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

/*****************************************************************************
 *
 * History:
 *
 *	$Log: skgepnmi.c,v $
 *	Revision 1.78  2000/09/12 10:44:58  cgoos
 *	Fixed SK_PNMI_STORE_U32 calls with typecasted argument.
 *	
 *	Revision 1.77  2000/09/07 08:10:19  rwahl
 *	- Modified algorithm for 64bit NDIS statistic counters;
 *	  returns 64bit or 32bit value depending on passed buffer
 *	  size. Indicate capability for 64bit NDIS counter, if passed
 *	  buffer size is zero. OID_GEN_XMIT_ERROR, OID_GEN_RCV_ERROR,
 *	  and OID_GEN_RCV_NO_BUFFER handled as 64bit counter, too.
 *	- corrected OID_SKGE_RLMT_PORT_PREFERRED.
 *	
 *	Revision 1.76  2000/08/03 15:23:39  rwahl
 *	- Correction for FrameTooLong counter has to be moved to OID handling
 *	  routines (instead of statistic counter routine).
 *	- Fix in XMAC Reset Event handling: Only offset counter for hardware
 *	  statistic registers are updated.
 *	
 *	Revision 1.75  2000/08/01 16:46:05  rwahl
 *	- Added StatRxLongFrames counter and correction of FrameTooLong counter.
 *	- Added directive to control width (default = 32bit) of NDIS statistic
 *	  counters (SK_NDIS_64BIT_CTR).
 *	
 *	Revision 1.74  2000/07/04 11:41:53  rwahl
 *	- Added volition connector type.
 *	
 *	Revision 1.73  2000/03/15 16:33:10  rwahl
 *	Fixed bug 10510; wrong reset of virtual port statistic counters.
 *	
 *	Revision 1.72  1999/12/06 16:15:53  rwahl
 *	Fixed problem of instance range for current and factory MAC address.
 *	
 *	Revision 1.71  1999/12/06 10:14:20  rwahl
 *	Fixed bug 10476; set operation for PHY_OPERATION_MODE.
 *	
 *	Revision 1.70  1999/11/22 13:33:34  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.69  1999/10/18 11:42:15  rwahl
 *	Added typecasts for checking event dependent param (debug only).
 *	
 *	Revision 1.68  1999/10/06 09:35:59  cgoos
 *	Added state check to PHY_READ call (hanged if called during startup).
 *	
 *	Revision 1.67  1999/09/22 09:53:20  rwahl
 *	- Read Broadcom register for updating fcs error counter (1000Base-T).
 *	
 *	Revision 1.66  1999/08/26 13:47:56  rwahl
 *	Added SK_DRIVER_SENDEVENT when queueing RLMT_CHANGE_THRES trap.
 *	
 *	Revision 1.65  1999/07/26 07:49:35  cgoos
 *	Added two typecasts to avoid compiler warnings.
 *	
 *	Revision 1.64  1999/05/20 09:24:12  cgoos
 *	Changes for 1000Base-T (sensors, Master/Slave).
 *	
 *	Revision 1.63  1999/04/13 15:11:58  mhaveman
 *	Moved include of rlmt.h to header skgepnmi.h because some macros
 *	are needed there.
 *	
 *	Revision 1.62  1999/04/13 15:08:07  mhaveman
 *	Replaced again SK_RLMT_CHECK_LINK with SK_PNMI_RLMT_MODE_CHK_LINK
 *	to grant unified interface by only using the PNMI header file.
 *	SK_PNMI_RLMT_MODE_CHK_LINK is defined the same as SK_RLMT_CHECK_LINK.
 *	
 *	Revision 1.61  1999/04/13 15:02:48  mhaveman
 *	Changes caused by review:
 *	-Changed some comments
 *	-Removed redundant check for OID_SKGE_PHYS_FAC_ADDR
 *	-Optimized PRESET check.
 *	-Meaning of error SK_ADDR_DUPLICATE_ADDRESS changed. Set of same
 *	 address will now not cause this error. Removed corresponding check.
 *	
 *	Revision 1.60  1999/03/23 10:41:23  mhaveman
 *	Added comments.
 *	
 *	Revision 1.59  1999/02/19 08:01:28  mhaveman
 *	Fixed bug 10372 that after counter reset all ports were displayed
 *	as inactive.
 *	
 *	Revision 1.58  1999/02/16 18:04:47  mhaveman
 *	Fixed problem of twisted OIDs SENSOR_WAR_TIME and SENSOR_ERR_TIME.
 *	
 *	Revision 1.56  1999/01/27 12:29:11  mhaveman
 *	SkTimerStart was called with time value in milli seconds but needs
 *	micro seconds.
 *	
 *	Revision 1.55  1999/01/25 15:00:38  mhaveman
 *	Added support to allow multiple ports to be active. If this feature in
 *	future will be used, the Management Data Base variables PORT_ACTIVE
 *	and PORT_PREFERED should be moved to the port specific part of RLMT.
 *	Currently they return the values of the first active physical port
 *	found. A set to the virtual port will actually change all active
 *	physical ports. A get returns the melted values of all active physical
 *	ports. If the port values differ a return value INDETERMINATED will
 *	be returned. This effects especially the CONF group.
 *	
 *	Revision 1.54  1999/01/19 10:10:22  mhaveman
 *	-Fixed bug 10354: Counter values of virtual port were wrong after port
 *	 switches
 *	-Added check if a switch to the same port is notified.
 *	
 *	Revision 1.53  1999/01/07 09:25:21  mhaveman
 *	Forgot to initialize a variable.
 *	
 *	Revision 1.52  1999/01/05 10:34:33  mhaveman
 *	Fixed little error in RlmtChangeEstimate calculation.
 *	
 *	Revision 1.51  1999/01/05 09:59:07  mhaveman
 *	-Moved timer start to init level 2
 *	-Redesigned port switch average calculation to avoid 64bit
 *	 arithmetic.
 *	
 *	Revision 1.50  1998/12/10 15:13:59  mhaveman
 *	-Fixed: PHYS_CUR_ADDR returned wrong addresses
 *	-Fixed: RLMT_PORT_PREFERED and RLMT_CHANGE_THRES preset returned
 *	        always BAD_VALUE.
 *	-Fixed: TRAP buffer seemed to sometimes suddenly empty
 *	
 *	Revision 1.49  1998/12/09 16:17:07  mhaveman
 *	Fixed: Couldnot delete VPD keys on UNIX.
 *	
 *	Revision 1.48  1998/12/09 14:11:10  mhaveman
 *	-Add: Debugmessage for XMAC_RESET supressed to minimize output.
 *	-Fixed: RlmtChangeThreshold will now be initialized.
 *	-Fixed: VPD_ENTRIES_LIST extended value with unnecessary space char.
 *	-Fixed: On VPD key creation an invalid key name could be created
 *	        (e.g. A5)
 *	-Some minor changes in comments and code.
 *	
 *	Revision 1.47  1998/12/08 16:00:31  mhaveman
 *	-Fixed: For RLMT_PORT_ACTIVE will now be returned a 0 if no port
 *		is active.
 *	-Fixed: For the RLMT statistics group only the last value was
 *		returned and the rest of the buffer was filled with 0xff
 *	-Fixed: Mysteriously the preset on RLMT_MODE still returned
 *		BAD_VALUE.
 *	Revision 1.46  1998/12/08 10:04:56  mhaveman
 *	-Fixed: Preset on RLMT_MODE returned always BAD_VALUE error.
 *	-Fixed: Alignment error in GetStruct
 *	-Fixed: If for Get/Preset/SetStruct the buffer size is equal or
 *	        larger than SK_PNMI_MIN_STRUCT_SIZE the return value is stored
 *		to the buffer. In this case the caller should always return
 *	        ok to its upper routines. Only if the buffer size is less
 *	        than SK_PNMI_MIN_STRUCT_SIZE and the return value is unequal
 *	        to 0, an error should be returned by the caller.
 *	-Fixed: Wrong number of instances with RLMT statistic.
 *	-Fixed: Return now SK_LMODE_STAT_UNKNOWN if the LinkModeStatus is 0.
 *	
 *	Revision 1.45  1998/12/03 17:17:24  mhaveman
 *	-Removed for VPD create action the buffer size limitation to 4 bytes.
 *	-Pass now physical/active physical port to ADDR for CUR_ADDR set
 *	
 *	Revision 1.44  1998/12/03 15:14:35  mhaveman
 *	Another change to Vpd instance evaluation.
 *
 *	Revision 1.43  1998/12/03 14:18:10  mhaveman
 *	-Fixed problem in PnmiSetStruct. It was impossible to set any value.
 *	-Removed VPD key evaluation for VPD_FREE_BYTES and VPD_ACTION.
 *	
 *	Revision 1.42  1998/12/03 11:31:47  mhaveman
 *	Inserted cast to satisfy lint.
 *	
 *	Revision 1.41  1998/12/03 11:28:16  mhaveman
 *	Removed SK_PNMI_CHECKPTR
 *	
 *	Revision 1.40  1998/12/03 11:19:07  mhaveman
 *	Fixed problems
 *	-A set to virtual port will now be ignored. A set with broadcast
 *	 address to any port will be ignored.
 *	-GetStruct function made VPD instance calculation wrong.
 *	-Prefered port returned -1 instead of 0.
 *	
 *	Revision 1.39  1998/11/26 15:30:29  mhaveman
 *	Added sense mode to link mode.
 *	
 *	Revision 1.38  1998/11/23 15:34:00  mhaveman
 *	-Fixed bug for RX counters. On an RX overflow interrupt the high
 *	 words of all RX counters were incremented.
 *	-SET operations on FLOWCTRL_MODE and LINK_MODE accept now the
 *	 value 0, which has no effect. It is usefull for multiple instance
 *	 SETs.
 *	
 *	Revision 1.37  1998/11/20 08:02:04  mhaveman
 *	-Fixed: Ports were compared with MAX_SENSORS
 *	-Fixed: Crash in GetTrapEntry with MEMSET macro
 *	-Fixed: Conversions between physical, logical port index and instance
 *	
 *	Revision 1.36  1998/11/16 07:48:53  mhaveman
 *	Casted SK_DRIVER_SENDEVENT with (void) to eleminate compiler warnings
 *	on Solaris.
 *	
 *	Revision 1.35  1998/11/16 07:45:34  mhaveman
 *	SkAddrOverride now returns value and will be checked.
 *	
 *	Revision 1.34  1998/11/10 13:40:37  mhaveman
 *	Needed to change interface, because NT driver needs a return value
 *	of needed buffer space on TOO_SHORT errors. Therefore all
 *	SkPnmiGet/Preset/Set functions now have a pointer to the length
 *	parameter, where the needed space on error is returned.
 *	
 *	Revision 1.33  1998/11/03 13:52:46  mhaveman
 *	Made file lint conform.
 *	
 *	Revision 1.32  1998/11/03 13:19:07  mhaveman
 *	The events SK_HWEV_SET_LMODE and SK_HWEV_SET_FLOWMODE pass now in
 *	Para32[0] the physical MAC index and in Para32[1] the new mode.
 *	
 *	Revision 1.31  1998/11/03 12:30:40  gklug
 *	fix: compiler warning memset
 *
 *	Revision 1.30  1998/11/03 12:04:46  mhaveman
 *	Fixed problem in SENSOR_VALUE, which wrote beyond the buffer end
 *	Fixed alignment problem with CHIPSET.
 *	
 *	Revision 1.29  1998/11/02 11:23:54  mhaveman
 *	Corrected SK_ERROR_LOG to SK_ERR_LOG. Sorry.
 *	
 *	Revision 1.28  1998/11/02 10:47:16  mhaveman
 *	Added syslog messages for internal errors.
 *	
 *	Revision 1.27  1998/10/30 15:48:06  mhaveman
 *	Fixed problems after simulation of SK_PNMI_EVT_CHG_EST_TIMER and
 *	RlmtChangeThreshold calculation.
 *	
 *	Revision 1.26  1998/10/29 15:36:55  mhaveman
 *	-Fixed bug in trap buffer handling.
 *	-OID_SKGE_DRIVER_DESCR, OID_SKGE_DRIVER_VERSION, OID_SKGE_HW_DESCR,
 *	 OID_SKGE_HW_VERSION, OID_SKGE_VPD_ENTRIES_LIST, OID_SKGE_VPD_KEY,
 *	 OID_SKGE_VPD_VALUE, and OID_SKGE_SENSOR_DESCR return values with
 *	 a leading octet before each string storing the string length.
 *	-Perform a RlmtUpdate during SK_PNMI_EVT_XMAC_RESET to minimize
 *	 RlmtUpdate calls in GetStatVal.
 *	-Inserted SK_PNMI_CHECKFLAGS macro increase readability.
 *	
 *	Revision 1.25  1998/10/29 08:50:36  mhaveman
 *	Fixed problems after second event simulation.
 *	
 *	Revision 1.24  1998/10/28 08:44:37  mhaveman
 *	-Fixed alignment problem
 *	-Fixed problems during event simulation
 *	-Fixed sequence of error return code (INSTANCE -> ACCESS -> SHORT)
 *	-Changed type of parameter Instance back to SK_U32 because of VPD
 *	-Updated new VPD function calls
 *	
 *	Revision 1.23  1998/10/23 10:16:37  mhaveman
 *	Fixed bugs after buffer test simulation.
 *	
 *	Revision 1.22  1998/10/21 13:23:52  mhaveman
 *	-Call syntax of SkOsGetTime() changed to SkOsGetTime(pAc).
 *	-Changed calculation of hundrets of seconds.
 *	
 *	Revision 1.20  1998/10/20 07:30:45  mhaveman
 *	Made type changes to unsigned integer where possible.
 *	
 *	Revision 1.19  1998/10/19 10:51:30  mhaveman
 *	-Made Bug fixes after simulation run
 *	-Renamed RlmtMAC... to RlmtPort...
 *	-Marked workarounds with Errata comments
 *	
 *	Revision 1.18  1998/10/14 07:50:08  mhaveman
 *	-For OID_SKGE_LINK_STATUS the link down detection has moved from RLMT
 *	 to HWACCESS.
 *	-Provided all MEMCPY/MEMSET macros with (char *) pointers, because
 *	 Solaris throwed warnings when mapping to bcopy/bset.
 *	
 *	Revision 1.17  1998/10/13 07:42:01  mhaveman
 *	-Added OIDs OID_SKGE_TRAP_NUMBER and OID_SKGE_ALL_DATA
 *	-Removed old cvs history entries
 *	-Renamed MacNumber to PortNumber
 *	
 *	Revision 1.16  1998/10/07 10:52:49  mhaveman
 *	-Inserted handling of some OID_GEN_ Ids for windows
 *	-Fixed problem with 803.2 statistic.
 *	
 *	Revision 1.15  1998/10/01 09:16:29  mhaveman
 *	Added Debug messages for function call and UpdateFlag tracing.
 *	
 *	Revision 1.14  1998/09/30 13:39:09  mhaveman
 *	-Reduced namings of 'MAC' by replacing them with 'PORT'.
 *	-Completed counting of OID_SKGE_RX_HW_ERROR_CTS,
 *       OID_SKGE_TX_HW_ERROR_CTS,
 *	 OID_SKGE_IN_ERRORS_CTS, and OID_SKGE_OUT_ERROR_CTS.
 *	-SET check for RlmtMode
 *	
 *	Revision 1.13  1998/09/28 13:13:08  mhaveman
 *	Hide strcmp, strlen, and strncpy behind macros SK_STRCMP, SK_STRLEN,
 *	and SK_STRNCPY. (Same reasons as for mem.. and MEM..)
 *	
 *	Revision 1.12  1998/09/16 08:18:36  cgoos
 *	Fix: XM_INxx and XM_OUTxx called with different parameter order:
 *      sometimes IoC,Mac,...  sometimes Mac,IoC,... Now always first variant.
 *	Fix: inserted "Pnmi." into some pAC->pDriverDescription / Version.
 *	Change: memset, memcpy to makros SK_MEMSET, SK_MEMCPY
 *
 *	Revision 1.11  1998/09/04 17:01:45  mhaveman
 *	Added SyncCounter as macro and OID_SKGE_.._NO_DESCR_CTS to
 *	OID_SKGE_RX_NO_BUF_CTS.
 *	
 *	Revision 1.10  1998/09/04 14:35:35  mhaveman
 *	Added macro counters, that are counted by driver.
 *	
 ****************************************************************************/


static const char SysKonnectFileId[] =
	"@(#) $Id: skgepnmi.c,v 1.78 2000/09/12 10:44:58 cgoos Exp $"
	" (C) SysKonnect.";

#include "h/skdrv1st.h"
#include "h/sktypes.h"
#include "h/xmac_ii.h"

#include "h/skdebug.h"
#include "h/skqueue.h"
#include "h/skgepnmi.h"
#include "h/skgesirq.h"
#include "h/skcsum.h"
#include "h/skvpd.h"
#include "h/skgehw.h"
#include "h/skgeinit.h"
#include "h/skdrv2nd.h"
#include "h/skgepnm2.h"


/*
 * Public Function prototypes
 */
int SkPnmiInit(SK_AC *pAC, SK_IOC IoC, int level);
int SkPnmiGetVar(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, void *pBuf,
	unsigned int *pLen, SK_U32 Instance);
int SkPnmiPreSetVar(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, void *pBuf,
	unsigned int *pLen, SK_U32 Instance);
int SkPnmiSetVar(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, void *pBuf,
	unsigned int *pLen, SK_U32 Instance);
int SkPnmiGetStruct(SK_AC *pAC, SK_IOC IoC, void *pBuf, unsigned int *pLen);
int SkPnmiPreSetStruct(SK_AC *pAC, SK_IOC IoC, void *pBuf, unsigned int *pLen);
int SkPnmiSetStruct(SK_AC *pAC, SK_IOC IoC, void *pBuf, unsigned int *pLen);
int SkPnmiEvent(SK_AC *pAC, SK_IOC IoC, SK_U32 Event, SK_EVPARA Param);


/*
 * Private Function prototypes
 */
static int Addr(SK_AC *pAC, SK_IOC IoC, int action,
	SK_U32 Id, char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static SK_U8 CalculateLinkModeStatus(SK_AC *pAC, SK_IOC IoC, unsigned int
	PhysPortIndex);
static SK_U8 CalculateLinkStatus(SK_AC *pAC, SK_IOC IoC, unsigned int
	PhysPortIndex);
static void CopyMac(char *pDst, SK_MAC_ADDR *pMac);
static void CopyTrapQueue(SK_AC *pAC, char *pDstBuf);
static int CsumStat(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int General(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static SK_U64 GetPhysStatVal(SK_AC *pAC, SK_IOC IoC,
	unsigned int PhysPortIndex, unsigned int StatIndex);
static SK_U64 GetStatVal(SK_AC *pAC, SK_IOC IoC, unsigned int LogPortIndex,
	unsigned int StatIndex);
static char* GetTrapEntry(SK_AC *pAC, SK_U32 TrapId, unsigned int Size);
static void GetTrapQueueLen(SK_AC *pAC, unsigned int *pLen,
	unsigned int *pEntries);
static int GetVpdKeyArr(SK_AC *pAC, SK_IOC IoC, char *pKeyArr,
	unsigned int KeyArrLen, unsigned int *pKeyNo);
static int LookupId(SK_U32 Id);
static int Mac8023Stat(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int MacPrivateConf(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int MacPrivateStat(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int MacUpdate(SK_AC *pAC, SK_IOC IoC, unsigned int FirstMac,
	unsigned int LastMac);
static int Monitor(SK_AC *pAC, SK_IOC IoC, int action,
	SK_U32 Id, char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int OidStruct(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int Perform(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int* pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int PnmiStruct(SK_AC *pAC, SK_IOC IoC, int Action, char *pBuf,
	unsigned int *pLen);
static int PnmiVar(SK_AC *pAC, SK_IOC IoC, int Action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance);
static void QueueRlmtNewMacTrap(SK_AC *pAC, unsigned int ActiveMac);
static void QueueRlmtPortTrap(SK_AC *pAC, SK_U32 TrapId,
	unsigned int PortIndex);
static void QueueSensorTrap(SK_AC *pAC, SK_U32 TrapId,
	unsigned int SensorIndex);
static void QueueSimpleTrap(SK_AC *pAC, SK_U32 TrapId);
static void ResetCounter(SK_AC *pAC, SK_IOC IoC);
static int Rlmt(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int RlmtStat(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int RlmtUpdate(SK_AC *pAC, SK_IOC IoC);
static int SensorStat(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);
static int SirqUpdate(SK_AC *pAC, SK_IOC IoC);
static void VirtualConf(SK_AC *pAC, SK_IOC IoC, SK_U32 Id, char *pBuf);
static int Vpd(SK_AC *pAC, SK_IOC IoC, int action, SK_U32 Id,
	char *pBuf, unsigned int *pLen, SK_U32 Instance,
	unsigned int TableIndex);


/******************************************************************************
 *
 * Global variables
 */

/*
 * Table to correlate OID with handler function and index to
 * hardware register stored in StatAddress if applicable.
 */
static const SK_PNMI_TAB_ENTRY IdTable[] = {
	{OID_GEN_XMIT_OK,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX},
	{OID_GEN_RCV_OK,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HRX},
	{OID_GEN_XMIT_ERROR,
		0,
		0,
		0,
		SK_PNMI_RO, General, 0},
	{OID_GEN_RCV_ERROR,
		0,
		0,
		0,
		SK_PNMI_RO, General, 0},
	{OID_GEN_RCV_NO_BUFFER,
		0,
		0,
		0,
		SK_PNMI_RO, General, 0},
	{OID_GEN_DIRECTED_FRAMES_XMIT,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_UNICAST},
	{OID_GEN_MULTICAST_FRAMES_XMIT,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_MULTICAST},
	{OID_GEN_BROADCAST_FRAMES_XMIT,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_BROADCAST},
	{OID_GEN_DIRECTED_FRAMES_RCV,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HRX_UNICAST},
	{OID_GEN_MULTICAST_FRAMES_RCV,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HRX_MULTICAST},
	{OID_GEN_BROADCAST_FRAMES_RCV,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HRX_BROADCAST},
	{OID_GEN_RCV_CRC_ERROR,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HRX_FCS},
	{OID_GEN_TRANSMIT_QUEUE_LENGTH,
		0,
		0,
		0,
		SK_PNMI_RO, General, 0},
	{OID_802_3_PERMANENT_ADDRESS,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, 0},
	{OID_802_3_CURRENT_ADDRESS,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, 0},
	{OID_802_3_RCV_ERROR_ALIGNMENT,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HRX_FRAMING},
	{OID_802_3_XMIT_ONE_COLLISION,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_SINGLE_COL},
	{OID_802_3_XMIT_MORE_COLLISIONS,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_MULTI_COL},
	{OID_802_3_XMIT_DEFERRED,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_DEFFERAL},
	{OID_802_3_XMIT_MAX_COLLISIONS,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_EXCESS_COL},
	{OID_802_3_RCV_OVERRUN,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HRX_OVERFLOW},
	{OID_802_3_XMIT_UNDERRUN,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_UNDERRUN},
	{OID_802_3_XMIT_TIMES_CRS_LOST,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_CARRIER},
	{OID_802_3_XMIT_LATE_COLLISIONS,
		0,
		0,
		0,
		SK_PNMI_RO, Mac8023Stat, SK_PNMI_HTX_LATE_COL},
	{OID_SKGE_MDB_VERSION,
		1,
		0,
		SK_PNMI_MAI_OFF(MgmtDBVersion),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_SUPPORTED_LIST,
		0,
		0,
		0,
		SK_PNMI_RO, General, 0},
	{OID_SKGE_ALL_DATA,
		0,
		0,
		0,
		SK_PNMI_RW, OidStruct, 0},
	{OID_SKGE_VPD_FREE_BYTES,
		1,
		0,
		SK_PNMI_MAI_OFF(VpdFreeBytes),
		SK_PNMI_RO, Vpd, 0},
	{OID_SKGE_VPD_ENTRIES_LIST,
		1,
		0,
		SK_PNMI_MAI_OFF(VpdEntriesList),
		SK_PNMI_RO, Vpd, 0},
	{OID_SKGE_VPD_ENTRIES_NUMBER,
		1,
		0,
		SK_PNMI_MAI_OFF(VpdEntriesNumber),
		SK_PNMI_RO, Vpd, 0},
	{OID_SKGE_VPD_KEY,
		SK_PNMI_VPD_ENTRIES,
		sizeof(SK_PNMI_VPD),
		SK_PNMI_OFF(Vpd) + SK_PNMI_VPD_OFF(VpdKey),
		SK_PNMI_RO, Vpd, 0},
	{OID_SKGE_VPD_VALUE,
		SK_PNMI_VPD_ENTRIES,
		sizeof(SK_PNMI_VPD),
		SK_PNMI_OFF(Vpd) + SK_PNMI_VPD_OFF(VpdValue),
		SK_PNMI_RO, Vpd, 0},
	{OID_SKGE_VPD_ACCESS,
		SK_PNMI_VPD_ENTRIES,
		sizeof(SK_PNMI_VPD),
		SK_PNMI_OFF(Vpd) + SK_PNMI_VPD_OFF(VpdAccess),
		SK_PNMI_RO, Vpd, 0},
	{OID_SKGE_VPD_ACTION,
		SK_PNMI_VPD_ENTRIES,
		sizeof(SK_PNMI_VPD),
		SK_PNMI_OFF(Vpd) + SK_PNMI_VPD_OFF(VpdAction),
		SK_PNMI_RW, Vpd, 0},
	{OID_SKGE_PORT_NUMBER,		
		1,
		0,
		SK_PNMI_MAI_OFF(PortNumber),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_DEVICE_TYPE,
		1,
		0,
		SK_PNMI_MAI_OFF(DeviceType),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_DRIVER_DESCR,
		1,
		0,
		SK_PNMI_MAI_OFF(DriverDescr),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_DRIVER_VERSION,
		1,
		0,
		SK_PNMI_MAI_OFF(DriverVersion),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_HW_DESCR,
		1,
		0,
		SK_PNMI_MAI_OFF(HwDescr),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_HW_VERSION,
		1,
		0,
		SK_PNMI_MAI_OFF(HwVersion),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_CHIPSET,
		1,
		0,
		SK_PNMI_MAI_OFF(Chipset),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_ACTION,
		1,
		0,
		SK_PNMI_MAI_OFF(Action),
		SK_PNMI_RW, Perform, 0},
	{OID_SKGE_RESULT,
		1,
		0,
		SK_PNMI_MAI_OFF(TestResult),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_BUS_TYPE,
		1,
		0,
		SK_PNMI_MAI_OFF(BusType),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_BUS_SPEED,
		1,
		0,
		SK_PNMI_MAI_OFF(BusSpeed),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_BUS_WIDTH,
		1,
		0,
		SK_PNMI_MAI_OFF(BusWidth),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TX_SW_QUEUE_LEN,
		1,
		0,
		SK_PNMI_MAI_OFF(TxSwQueueLen),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TX_SW_QUEUE_MAX,
		1,
		0,
		SK_PNMI_MAI_OFF(TxSwQueueMax),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TX_RETRY,
		1,
		0,
		SK_PNMI_MAI_OFF(TxRetryCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_RX_INTR_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(RxIntrCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TX_INTR_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(TxIntrCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_RX_NO_BUF_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(RxNoBufCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TX_NO_BUF_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(TxNoBufCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TX_USED_DESCR_NO,
		1,
		0,
		SK_PNMI_MAI_OFF(TxUsedDescrNo),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_RX_DELIVERED_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(RxDeliveredCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_RX_OCTETS_DELIV_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(RxOctetsDeliveredCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_RX_HW_ERROR_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(RxHwErrorsCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TX_HW_ERROR_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(TxHwErrorsCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_IN_ERRORS_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(InErrorsCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_OUT_ERROR_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(OutErrorsCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_ERR_RECOVERY_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(ErrRecoveryCts),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_SYSUPTIME,
		1,
		0,
		SK_PNMI_MAI_OFF(SysUpTime),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_SENSOR_NUMBER,
		1,
		0,
		SK_PNMI_MAI_OFF(SensorNumber),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_SENSOR_INDEX,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorIndex),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_DESCR,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorDescr),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_TYPE,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorType),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_VALUE,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorValue),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_WAR_THRES_LOW,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorWarningThresholdLow),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_WAR_THRES_UPP,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorWarningThresholdHigh),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_ERR_THRES_LOW,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorErrorThresholdLow),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_ERR_THRES_UPP,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorErrorThresholdHigh),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_STATUS,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorStatus),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_WAR_CTS,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorWarningCts),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_ERR_CTS,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorErrorCts),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_WAR_TIME,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorWarningTimestamp),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_SENSOR_ERR_TIME,
		SK_PNMI_SENSOR_ENTRIES,
		sizeof(SK_PNMI_SENSOR),
		SK_PNMI_OFF(Sensor) + SK_PNMI_SEN_OFF(SensorErrorTimestamp),
		SK_PNMI_RO, SensorStat, 0},
	{OID_SKGE_CHKSM_NUMBER,
		1,
		0,
		SK_PNMI_MAI_OFF(ChecksumNumber),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_CHKSM_RX_OK_CTS,
		SKCS_NUM_PROTOCOLS,
		sizeof(SK_PNMI_CHECKSUM),
		SK_PNMI_OFF(Checksum) + SK_PNMI_CHK_OFF(ChecksumRxOkCts),
		SK_PNMI_RO, CsumStat, 0},
	{OID_SKGE_CHKSM_RX_UNABLE_CTS,
		SKCS_NUM_PROTOCOLS,
		sizeof(SK_PNMI_CHECKSUM),
		SK_PNMI_OFF(Checksum) + SK_PNMI_CHK_OFF(ChecksumRxUnableCts),
		SK_PNMI_RO, CsumStat, 0},
	{OID_SKGE_CHKSM_RX_ERR_CTS,
		SKCS_NUM_PROTOCOLS,
		sizeof(SK_PNMI_CHECKSUM),
		SK_PNMI_OFF(Checksum) + SK_PNMI_CHK_OFF(ChecksumRxErrCts),
		SK_PNMI_RO, CsumStat, 0},
	{OID_SKGE_CHKSM_TX_OK_CTS,
		SKCS_NUM_PROTOCOLS,
		sizeof(SK_PNMI_CHECKSUM),
		SK_PNMI_OFF(Checksum) + SK_PNMI_CHK_OFF(ChecksumTxOkCts),
		SK_PNMI_RO, CsumStat, 0},
	{OID_SKGE_CHKSM_TX_UNABLE_CTS,
		SKCS_NUM_PROTOCOLS,
		sizeof(SK_PNMI_CHECKSUM),
		SK_PNMI_OFF(Checksum) + SK_PNMI_CHK_OFF(ChecksumTxUnableCts),
		SK_PNMI_RO, CsumStat, 0},
	{OID_SKGE_STAT_TX,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX},
	{OID_SKGE_STAT_TX_OCTETS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxOctetsOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_OCTET},
	{OID_SKGE_STAT_TX_BROADCAST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxBroadcastOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_BROADCAST},
	{OID_SKGE_STAT_TX_MULTICAST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxMulticastOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_MULTICAST},
	{OID_SKGE_STAT_TX_UNICAST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxUnicastOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_UNICAST},
	{OID_SKGE_STAT_TX_LONGFRAMES,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxLongFramesCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_LONGFRAMES},
	{OID_SKGE_STAT_TX_BURST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxBurstCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_BURST},
	{OID_SKGE_STAT_TX_PFLOWC,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxPauseMacCtrlCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_PMACC},
	{OID_SKGE_STAT_TX_FLOWC,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxMacCtrlCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_MACC},
	{OID_SKGE_STAT_TX_SINGLE_COL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxSingleCollisionCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_SINGLE_COL},
	{OID_SKGE_STAT_TX_MULTI_COL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxMultipleCollisionCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_MULTI_COL},
	{OID_SKGE_STAT_TX_EXCESS_COL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxExcessiveCollisionCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_EXCESS_COL},
	{OID_SKGE_STAT_TX_LATE_COL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxLateCollisionCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_LATE_COL},
	{OID_SKGE_STAT_TX_DEFFERAL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxDeferralCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_DEFFERAL},
	{OID_SKGE_STAT_TX_EXCESS_DEF,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxExcessiveDeferralCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_EXCESS_DEF},
	{OID_SKGE_STAT_TX_UNDERRUN,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxFifoUnderrunCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_UNDERRUN},
	{OID_SKGE_STAT_TX_CARRIER,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxCarrierCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_CARRIER},
/*	{OID_SKGE_STAT_TX_UTIL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxUtilization),
		SK_PNMI_RO, MacPrivateStat, (SK_U16)(-1)}, */
	{OID_SKGE_STAT_TX_64,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTx64Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_64},
	{OID_SKGE_STAT_TX_127,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTx127Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_127},
	{OID_SKGE_STAT_TX_255,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTx255Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_255},
	{OID_SKGE_STAT_TX_511,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTx511Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_511},
	{OID_SKGE_STAT_TX_1023,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTx1023Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_1023},
	{OID_SKGE_STAT_TX_MAX,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxMaxCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_MAX},
	{OID_SKGE_STAT_TX_SYNC,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxSyncCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_SYNC},
	{OID_SKGE_STAT_TX_SYNC_OCTETS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatTxSyncOctetsCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HTX_SYNC_OCTET},
	{OID_SKGE_STAT_RX,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX},
	{OID_SKGE_STAT_RX_OCTETS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxOctetsOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_OCTET},
	{OID_SKGE_STAT_RX_BROADCAST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxBroadcastOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_BROADCAST},
	{OID_SKGE_STAT_RX_MULTICAST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxMulticastOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_MULTICAST},
	{OID_SKGE_STAT_RX_UNICAST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxUnicastOkCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_UNICAST},
	{OID_SKGE_STAT_RX_LONGFRAMES,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxLongFramesCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_LONGFRAMES},
	{OID_SKGE_STAT_RX_PFLOWC,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxPauseMacCtrlCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_PMACC},
	{OID_SKGE_STAT_RX_FLOWC,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxMacCtrlCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_MACC},
	{OID_SKGE_STAT_RX_PFLOWC_ERR,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxPauseMacCtrlErrorCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_PMACC_ERR},
	{OID_SKGE_STAT_RX_FLOWC_UNKWN,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxMacCtrlUnknownCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_MACC_UNKWN},
	{OID_SKGE_STAT_RX_BURST,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxBurstCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_BURST},
	{OID_SKGE_STAT_RX_MISSED,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxMissedCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_MISSED},
	{OID_SKGE_STAT_RX_FRAMING,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxFramingCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_FRAMING},
	{OID_SKGE_STAT_RX_OVERFLOW,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxFifoOverflowCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_OVERFLOW},
	{OID_SKGE_STAT_RX_JABBER,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxJabberCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_JABBER},
	{OID_SKGE_STAT_RX_CARRIER,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxCarrierCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_CARRIER},
	{OID_SKGE_STAT_RX_IR_LENGTH,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxIRLengthCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_IRLENGTH},
	{OID_SKGE_STAT_RX_SYMBOL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxSymbolCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_SYMBOL},
	{OID_SKGE_STAT_RX_SHORTS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxShortsCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_SHORTS},
	{OID_SKGE_STAT_RX_RUNT,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxRuntCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_RUNT},
	{OID_SKGE_STAT_RX_CEXT,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxCextCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_CEXT},
	{OID_SKGE_STAT_RX_TOO_LONG,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxTooLongCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_TOO_LONG},
	{OID_SKGE_STAT_RX_FCS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxFcsCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_FCS},
/*	{OID_SKGE_STAT_RX_UTIL,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxUtilization),
		SK_PNMI_RO, MacPrivateStat, (SK_U16)(-1)}, */
	{OID_SKGE_STAT_RX_64,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRx64Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_64},
	{OID_SKGE_STAT_RX_127,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRx127Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_127},
	{OID_SKGE_STAT_RX_255,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRx255Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_255},
	{OID_SKGE_STAT_RX_511,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRx511Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_511},
	{OID_SKGE_STAT_RX_1023,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRx1023Cts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_1023},
	{OID_SKGE_STAT_RX_MAX,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_STAT),
		SK_PNMI_OFF(Stat) + SK_PNMI_STA_OFF(StatRxMaxCts),
		SK_PNMI_RO, MacPrivateStat, SK_PNMI_HRX_MAX},
	{OID_SKGE_PHYS_CUR_ADDR,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfMacCurrentAddr),
		SK_PNMI_RW, Addr, 0},
	{OID_SKGE_PHYS_FAC_ADDR,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfMacFactoryAddr),
		SK_PNMI_RO, Addr, 0},
	{OID_SKGE_PMD,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfPMD),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_CONNECTOR,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfConnector),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_LINK_CAP,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfLinkCapability),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_LINK_MODE,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfLinkMode),
		SK_PNMI_RW, MacPrivateConf, 0},
	{OID_SKGE_LINK_MODE_STATUS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfLinkModeStatus),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_LINK_STATUS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfLinkStatus),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_FLOWCTRL_CAP,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfFlowCtrlCapability),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_FLOWCTRL_MODE,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfFlowCtrlMode),
		SK_PNMI_RW, MacPrivateConf, 0},
	{OID_SKGE_FLOWCTRL_STATUS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfFlowCtrlStatus),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_PHY_OPERATION_CAP,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfPhyOperationCapability),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_PHY_OPERATION_MODE,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfPhyOperationMode),
		SK_PNMI_RW, MacPrivateConf, 0},
	{OID_SKGE_PHY_OPERATION_STATUS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_CONF),
		SK_PNMI_OFF(Conf) + SK_PNMI_CNF_OFF(ConfPhyOperationStatus),
		SK_PNMI_RO, MacPrivateConf, 0},
	{OID_SKGE_TRAP,
		1,
		0,
		SK_PNMI_MAI_OFF(Trap),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_TRAP_NUMBER,
		1,
		0,
		SK_PNMI_MAI_OFF(TrapNumber),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_RLMT_MODE,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtMode),
		SK_PNMI_RW, Rlmt, 0},
	{OID_SKGE_RLMT_PORT_NUMBER,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtPortNumber),
		SK_PNMI_RO, Rlmt, 0},
	{OID_SKGE_RLMT_PORT_ACTIVE,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtPortActive),
		SK_PNMI_RO, Rlmt, 0},
	{OID_SKGE_RLMT_PORT_PREFERRED,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtPortPreferred),
		SK_PNMI_RW, Rlmt, 0},
	{OID_SKGE_RLMT_CHANGE_CTS,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtChangeCts),
		SK_PNMI_RO, Rlmt, 0},
	{OID_SKGE_RLMT_CHANGE_TIME,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtChangeTime),
		SK_PNMI_RO, Rlmt, 0},
	{OID_SKGE_RLMT_CHANGE_ESTIM,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtChangeEstimate),
		SK_PNMI_RO, Rlmt, 0},
	{OID_SKGE_RLMT_CHANGE_THRES,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtChangeThreshold),
		SK_PNMI_RW, Rlmt, 0},
	{OID_SKGE_RLMT_PORT_INDEX,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_RLMT),
		SK_PNMI_OFF(Rlmt) + SK_PNMI_RLM_OFF(RlmtIndex),
		SK_PNMI_RO, RlmtStat, 0},
	{OID_SKGE_RLMT_STATUS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_RLMT),
		SK_PNMI_OFF(Rlmt) + SK_PNMI_RLM_OFF(RlmtStatus),
		SK_PNMI_RO, RlmtStat, 0},
	{OID_SKGE_RLMT_TX_HELLO_CTS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_RLMT),
		SK_PNMI_OFF(Rlmt) + SK_PNMI_RLM_OFF(RlmtTxHelloCts),
		SK_PNMI_RO, RlmtStat, 0},
	{OID_SKGE_RLMT_RX_HELLO_CTS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_RLMT),
		SK_PNMI_OFF(Rlmt) + SK_PNMI_RLM_OFF(RlmtRxHelloCts),
		SK_PNMI_RO, RlmtStat, 0},
	{OID_SKGE_RLMT_TX_SP_REQ_CTS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_RLMT),
		SK_PNMI_OFF(Rlmt) + SK_PNMI_RLM_OFF(RlmtTxSpHelloReqCts),
		SK_PNMI_RO, RlmtStat, 0},
	{OID_SKGE_RLMT_RX_SP_CTS,
		SK_PNMI_MAC_ENTRIES,
		sizeof(SK_PNMI_RLMT),
		SK_PNMI_OFF(Rlmt) + SK_PNMI_RLM_OFF(RlmtRxSpHelloCts),
		SK_PNMI_RO, RlmtStat, 0},
	{OID_SKGE_RLMT_MONITOR_NUMBER,
		1,
		0,
		SK_PNMI_MAI_OFF(RlmtMonitorNumber),
		SK_PNMI_RO, General, 0},
	{OID_SKGE_RLMT_MONITOR_INDEX,
		SK_PNMI_MONITOR_ENTRIES,
		sizeof(SK_PNMI_RLMT_MONITOR),
		SK_PNMI_OFF(RlmtMonitor) + SK_PNMI_MON_OFF(RlmtMonitorIndex),
		SK_PNMI_RO, Monitor, 0},
	{OID_SKGE_RLMT_MONITOR_ADDR,
		SK_PNMI_MONITOR_ENTRIES,
		sizeof(SK_PNMI_RLMT_MONITOR),
		SK_PNMI_OFF(RlmtMonitor) + SK_PNMI_MON_OFF(RlmtMonitorAddr),
		SK_PNMI_RO, Monitor, 0},
	{OID_SKGE_RLMT_MONITOR_ERRS,
		SK_PNMI_MONITOR_ENTRIES,
		sizeof(SK_PNMI_RLMT_MONITOR),
		SK_PNMI_OFF(RlmtMonitor) + SK_PNMI_MON_OFF(RlmtMonitorErrorCts),
		SK_PNMI_RO, Monitor, 0},
	{OID_SKGE_RLMT_MONITOR_TIMESTAMP,
		SK_PNMI_MONITOR_ENTRIES,
		sizeof(SK_PNMI_RLMT_MONITOR),
		SK_PNMI_OFF(RlmtMonitor) + SK_PNMI_MON_OFF(RlmtMonitorTimestamp),
		SK_PNMI_RO, Monitor, 0},
	{OID_SKGE_RLMT_MONITOR_ADMIN,
		SK_PNMI_MONITOR_ENTRIES,
		sizeof(SK_PNMI_RLMT_MONITOR),
		SK_PNMI_OFF(RlmtMonitor) + SK_PNMI_MON_OFF(RlmtMonitorAdmin),
		SK_PNMI_RW, Monitor, 0},
};

/*
 * Table for hardware register saving on resets and port switches
*/
static const SK_PNMI_STATADDR StatAddress[SK_PNMI_MAX_IDX] = {
	/*  0 */	{TRUE, XM_TXF_OK},
	/*  1 */	{TRUE, 0},
	/*  2 */	{FALSE, 0},
	/*  3 */	{TRUE, XM_TXF_BC_OK},
	/*  4 */	{TRUE, XM_TXF_MC_OK},
	/*  5 */	{TRUE, XM_TXF_UC_OK},
	/*  6 */	{TRUE, XM_TXF_LONG},
	/*  7 */	{TRUE, XM_TXE_BURST},
	/*  8 */	{TRUE, XM_TXF_MPAUSE},
	/*  9 */	{TRUE, XM_TXF_MCTRL},
	/* 10 */	{TRUE, XM_TXF_SNG_COL},
	/* 11 */	{TRUE, XM_TXF_MUL_COL},
	/* 12 */	{TRUE, XM_TXF_ABO_COL},
	/* 13 */	{TRUE, XM_TXF_LAT_COL},
	/* 14 */	{TRUE, XM_TXF_DEF},
	/* 15 */	{TRUE, XM_TXF_EX_DEF},
	/* 16 */	{TRUE, XM_TXE_FIFO_UR},
	/* 17 */	{TRUE, XM_TXE_CS_ERR},
	/* 18 */	{FALSE, 0},
	/* 19 */	{FALSE, 0},
	/* 20 */	{TRUE, XM_TXF_64B},
	/* 21 */	{TRUE, XM_TXF_127B},
	/* 22 */	{TRUE, XM_TXF_255B},
	/* 23 */	{TRUE, XM_TXF_511B},
	/* 24 */	{TRUE, XM_TXF_1023B},
	/* 25 */	{TRUE, XM_TXF_MAX_SZ},
	/* 26 */	{FALSE, 0},
	/* 27 */	{FALSE, 0},
	/* 28 */	{FALSE, 0},
	/* 29 */	{FALSE, 0},
	/* 30 */	{FALSE, 0},
	/* 31 */	{FALSE, 0},
	/* 32 */	{TRUE, XM_RXF_OK},
	/* 33 */	{TRUE, 0},
	/* 34 */	{FALSE, 0},
	/* 35 */	{TRUE, XM_RXF_BC_OK},
	/* 36 */	{TRUE, XM_RXF_MC_OK},
	/* 37 */	{TRUE, XM_RXF_UC_OK},
	/* 38 */	{TRUE, XM_RXF_MPAUSE},
	/* 39 */	{TRUE, XM_RXF_MCTRL},
	/* 40 */	{TRUE, XM_RXF_INV_MP},
	/* 41 */	{TRUE, XM_RXF_INV_MOC},
	/* 42 */	{TRUE, XM_RXE_BURST},
	/* 43 */	{TRUE, XM_RXE_FMISS},
	/* 44 */	{TRUE, XM_RXF_FRA_ERR},
	/* 45 */	{TRUE, XM_RXE_FIFO_OV},
	/* 46 */	{TRUE, XM_RXF_JAB_PKT},
	/* 47 */	{TRUE, XM_RXE_CAR_ERR},
	/* 48 */	{TRUE, XM_RXF_LEN_ERR},
	/* 49 */	{TRUE, XM_RXE_SYM_ERR},
	/* 50 */	{TRUE, XM_RXE_SHT_ERR},
	/* 51 */	{TRUE, XM_RXE_RUNT},
	/* 52 */	{TRUE, XM_RXF_LNG_ERR},
	/* 53 */	{TRUE, XM_RXF_FCS_ERR},
	/* 54 */	{FALSE, 0},
	/* 55 */	{TRUE, XM_RXF_CEX_ERR},
	/* 56 */	{FALSE, 0},
	/* 57 */	{FALSE, 0},
	/* 58 */	{TRUE, XM_RXF_64B},
	/* 59 */	{TRUE, XM_RXF_127B},
	/* 60 */	{TRUE, XM_RXF_255B},
	/* 61 */	{TRUE, XM_RXF_511B},
	/* 62 */	{TRUE, XM_RXF_1023B},
	/* 63 */	{TRUE, XM_RXF_MAX_SZ},
	/* 64 */	{FALSE, 0},
	/* 65 */	{FALSE, 0},
	/* 66 */	{TRUE, 0}
};


/*****************************************************************************
 *
 * Public functions
 *
 */

/*****************************************************************************
 *
 * SkPnmiInit - Init function of PNMI
 *
 * Description:
 *	SK_INIT_DATA: Initialises the data structures
 *	SK_INIT_IO:   Resets the XMAC statistics, determines the device and
 *	              connector type.
 *	SK_INIT_RUN:  Starts a timer event for port switch per hour
 *	              calculation.
 *
 * Returns:
 *	Always 0
 */

int SkPnmiInit(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Level)		/* Initialization level */
{
	unsigned int	PortMax;	/* Number of ports */
	unsigned int	PortIndex;	/* Current port index in loop */
	SK_U16		Val16;		/* Multiple purpose 16 bit variable */
	SK_U8		Val8;		/* Mulitple purpose 8 bit variable */
	SK_EVPARA	EventParam;	/* Event struct for timer event */


	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiInit: Called, level=%d\n", Level));

	switch (Level) {

	case SK_INIT_DATA:
		SK_MEMSET((char *)&pAC->Pnmi, 0, sizeof(pAC->Pnmi));
		pAC->Pnmi.TrapBufFree = SK_PNMI_TRAP_QUEUE_LEN;
		pAC->Pnmi.StartUpTime = SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));
		pAC->Pnmi.RlmtChangeThreshold = SK_PNMI_DEF_RLMT_CHG_THRES;
		for (PortIndex = 0; PortIndex < SK_MAX_MACS; PortIndex ++) {

			pAC->Pnmi.Port[PortIndex].ActiveFlag = SK_FALSE;
		}
		break;

	case SK_INIT_IO:
		/*
		 * Reset MAC counters
		 */
		PortMax = pAC->GIni.GIMacsFound;

		for (PortIndex = 0; PortIndex < PortMax; PortIndex ++) {

			Val16 = XM_SC_CLR_RXC | XM_SC_CLR_TXC;
			XM_OUT16(IoC, PortIndex, XM_STAT_CMD, Val16);
			/* Clear two times according to Errata #3 */
			XM_OUT16(IoC, PortIndex, XM_STAT_CMD, Val16);
		}

		/*
		 * Get pci bus speed
		 */
		SK_IN16(IoC, B0_CTST, &Val16);
		if ((Val16 & CS_BUS_CLOCK) == 0) {

			pAC->Pnmi.PciBusSpeed = 33;
		}
		else {
			pAC->Pnmi.PciBusSpeed = 66;
		}

		/*
		 * Get pci bus width
		 */
		SK_IN16(IoC, B0_CTST, &Val16);
		if ((Val16 & CS_BUS_SLOT_SZ) == 0) {

			pAC->Pnmi.PciBusWidth = 32;
		}
		else {
			pAC->Pnmi.PciBusWidth = 64;
		}

		/*
		 * Get PMD and DeviceType
		 */
		SK_IN8(IoC, B2_PMD_TYP, &Val8);
		switch (Val8) {
		case 'S':
			pAC->Pnmi.PMD = 3;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020002;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020001;
			}
			break;

		case 'L':
			pAC->Pnmi.PMD = 2;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020004;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020003;
			}
			break;

		case 'C':
			pAC->Pnmi.PMD = 4;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020006;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020005;
			}
			break;

		case 'T':
			pAC->Pnmi.PMD = 5;
			if (pAC->GIni.GIMacsFound > 1) {

				pAC->Pnmi.DeviceType = 0x00020008;
			}
			else {
				pAC->Pnmi.DeviceType = 0x00020007;
			}
			break;

		default :
			pAC->Pnmi.PMD = 1;
			pAC->Pnmi.DeviceType = 0;
			break;
		}

		/*
		 * Get connector
		 */
		SK_IN8(IoC, B2_CONN_TYP, &Val8);
		switch (Val8) {
		case 'C':
			pAC->Pnmi.Connector = 2;
			break;

		case 'D':
			pAC->Pnmi.Connector = 3;
			break;

		case 'F':
			pAC->Pnmi.Connector = 4;
			break;

		case 'J':
			pAC->Pnmi.Connector = 5;
			break;

		case 'V':
			pAC->Pnmi.Connector = 6;
			break;

		default:
			pAC->Pnmi.Connector = 1;
			break;
		}
		break;

	case SK_INIT_RUN:
		/*
		 * Start timer for RLMT change counter
		 */
		SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
		SkTimerStart(pAC, IoC, &pAC->Pnmi.RlmtChangeEstimate.EstTimer,
			28125000, SKGE_PNMI, SK_PNMI_EVT_CHG_EST_TIMER,
			EventParam);
		break;

	default:
		break; /* Nothing todo */
	}

	return (0);
}

/*****************************************************************************
 *
 * SkPnmiGetVar - Retrieves the value of a single OID
 *
 * Description:
 *	Calls a general sub-function for all this stuff. If the instance
 *	-1 is passed, the values of all instances are returned in an
 *	array of values.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to take
 *	                         the data.
 *	SK_PNMI_ERR_UNKNOWN_OID  The requested OID is unknown
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

int SkPnmiGetVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
void *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance)	/* Instance (1..n) that is to be queried or -1 */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiGetVar: Called, Id=0x%x, BufLen=%d\n", Id,
		*pLen));

	return (PnmiVar(pAC, IoC, SK_PNMI_GET, Id, (char *)pBuf, pLen,
		Instance));
}

/*****************************************************************************
 *
 * SkPnmiPreSetVar - Presets the value of a single OID
 *
 * Description:
 *	Calls a general sub-function for all this stuff. The preset does
 *	the same as a set, but returns just before finally setting the
 *	new value. This is usefull to check if a set might be successfull.
 *	If as instance a -1 is passed, an array of values is supposed and
 *	all instance of the OID will be set.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_OID  The requested OID is unknown.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

int SkPnmiPreSetVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
void *pBuf,		/* Buffer which stores the mgmt data to be set */
unsigned int *pLen,	/* Total length of mgmt data */
SK_U32 Instance)	/* Instance (1..n) that is to be set or -1 */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiPreSetVar: Called, Id=0x%x, BufLen=%d\n",
		Id, *pLen));

	return (PnmiVar(pAC, IoC, SK_PNMI_PRESET, Id, (char *)pBuf, pLen,
		Instance));
}

/*****************************************************************************
 *
 * SkPnmiSetVar - Sets the value of a single OID
 *
 * Description:
 *	Calls a general sub-function for all this stuff. The preset does
 *	the same as a set, but returns just before finally setting the
 *	new value. This is usefull to check if a set might be successfull.
 *	If as instance a -1 is passed, an array of values is supposed and
 *	all instance of the OID will be set.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_OID  The requested OID is unknown.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

int SkPnmiSetVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
void *pBuf,		/* Buffer which stores the mgmt data to be set */
unsigned int *pLen,	/* Total length of mgmt data */
SK_U32 Instance)	/* Instance (1..n) that is to be set or -1 */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiSetVar: Called, Id=0x%x, BufLen=%d\n", Id,
		*pLen));

	return (PnmiVar(pAC, IoC, SK_PNMI_SET, Id, (char *)pBuf, pLen,
		Instance));
}

/*****************************************************************************
 *
 * SkPnmiGetStruct - Retrieves the management database in SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	Runs through the IdTable, queries the single OIDs and stores the
 *	returned data into the management database structure
 *	SK_PNMI_STRUCT_DATA. The offset of the OID in the structure
 *	is stored in the IdTable. The return value of the function will also
 *	be stored in SK_PNMI_STRUCT_DATA if the passed buffer has the
 *	minimum size of SK_PNMI_MIN_STRUCT_SIZE.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to take
 *	                         the data.
 */

int SkPnmiGetStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
void *pBuf,		/* Buffer which will store the retrieved data */
unsigned int *pLen)	/* Length of buffer */
{
	int		Ret;
	unsigned int	TableIndex;
	unsigned int	DstOffset;
	unsigned int	InstanceNo;
	unsigned int	InstanceCnt;
	SK_U32		Instance;
	unsigned int	TmpLen;
	char		KeyArr[SK_PNMI_VPD_ARR_SIZE][SK_PNMI_VPD_STR_SIZE];

	
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiGetStruct: Called, BufLen=%d\n", *pLen));

	if (*pLen < SK_PNMI_STRUCT_SIZE) {

		if (*pLen >= SK_PNMI_MIN_STRUCT_SIZE) {

			SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_TOO_SHORT,
				(SK_U32)(-1));
		}

		*pLen = SK_PNMI_STRUCT_SIZE;
		return (SK_PNMI_ERR_TOO_SHORT);
	}

	/* Update statistic */
	SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On call");

	if ((Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1)) !=
		SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	if ((Ret = RlmtUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	if ((Ret = SirqUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	/*
	 * Increment semaphores to indicate that an update was
	 * already done
	 */
	pAC->Pnmi.MacUpdatedFlag ++;
	pAC->Pnmi.RlmtUpdatedFlag ++;
	pAC->Pnmi.SirqUpdatedFlag ++;

	/* Get vpd keys for instance calculation */
	Ret = GetVpdKeyArr(pAC, IoC, &KeyArr[0][0], sizeof(KeyArr), &TmpLen);
	if (Ret != SK_PNMI_ERR_OK) {

		pAC->Pnmi.MacUpdatedFlag --;
		pAC->Pnmi.RlmtUpdatedFlag --;
		pAC->Pnmi.SirqUpdatedFlag --;

		SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On return");
		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (SK_PNMI_ERR_GENERAL);
	}

	/* Retrieve values */
	SK_MEMSET((char *)pBuf, 0, SK_PNMI_STRUCT_SIZE);
	for (TableIndex = 0; TableIndex < sizeof(IdTable)/sizeof(IdTable[0]);
		TableIndex ++) {

		InstanceNo = IdTable[TableIndex].InstanceNo;

		for (InstanceCnt = 1; InstanceCnt <= InstanceNo;
			InstanceCnt ++) {

			DstOffset = IdTable[TableIndex].Offset +
				(InstanceCnt - 1) *
				IdTable[TableIndex].StructSize;

			/*
			 * For the VPD the instance is not an index number
			 * but the key itself. Determin with the instance
			 * counter the VPD key to be used.
			 */
			if (IdTable[TableIndex].Id == OID_SKGE_VPD_KEY ||
				IdTable[TableIndex].Id == OID_SKGE_VPD_VALUE ||
				IdTable[TableIndex].Id == OID_SKGE_VPD_ACCESS ||
				IdTable[TableIndex].Id == OID_SKGE_VPD_ACTION) {

				SK_PNMI_READ_U32(KeyArr[InstanceCnt - 1],
					Instance);
			}
			else {
				Instance = (SK_U32)InstanceCnt;
			}

			TmpLen = *pLen - DstOffset;
			Ret = IdTable[TableIndex].Func(pAC, IoC, SK_PNMI_GET,
				IdTable[TableIndex].Id, (char *)pBuf +
				DstOffset, &TmpLen, Instance, TableIndex);

			/*
			 * An unknown instance error means that we reached
			 * the last instance of that variable. Proceed with
			 * the next OID in the table and ignore the return
			 * code.
			 */
			if (Ret == SK_PNMI_ERR_UNKNOWN_INST) {

				break;
			}

			if (Ret != SK_PNMI_ERR_OK) {

				pAC->Pnmi.MacUpdatedFlag --;
				pAC->Pnmi.RlmtUpdatedFlag --;
				pAC->Pnmi.SirqUpdatedFlag --;

				SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On return");
				SK_PNMI_SET_STAT(pBuf, Ret, DstOffset);
				*pLen = SK_PNMI_MIN_STRUCT_SIZE;
				return (Ret);
			}
		}
	}

	pAC->Pnmi.MacUpdatedFlag --;
	pAC->Pnmi.RlmtUpdatedFlag --;
	pAC->Pnmi.SirqUpdatedFlag --;

	*pLen = SK_PNMI_STRUCT_SIZE;
	SK_PNMI_CHECKFLAGS("SkPnmiGetStruct: On return");
	SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_OK, (SK_U32)(-1));
	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * SkPnmiPreSetStruct - Presets the management database in SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	Calls a general sub-function for all this set stuff. The preset does
 *	the same as a set, but returns just before finally setting the
 *	new value. This is usefull to check if a set might be successfull.
 *	The sub-function runs through the IdTable, checks which OIDs are able
 *	to set, and calls the handler function of the OID to perform the
 *	preset. The return value of the function will also be stored in
 *	SK_PNMI_STRUCT_DATA if the passed buffer has the minimum size of
 *	SK_PNMI_MIN_STRUCT_SIZE.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 */

int SkPnmiPreSetStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
void *pBuf,		/* Buffer which contains the data to be set */
unsigned int *pLen)	/* Length of buffer */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiPreSetStruct: Called, BufLen=%d\n", *pLen));

	return (PnmiStruct(pAC, IoC, SK_PNMI_PRESET, (char *)pBuf, pLen));
}

/*****************************************************************************
 *
 * SkPnmiSetStruct - Sets the management database in SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	Calls a general sub-function for all this set stuff. The return value
 *	of the function will also be stored in SK_PNMI_STRUCT_DATA if the
 *	passed buffer has the minimum size of SK_PNMI_MIN_STRUCT_SIZE.
 *	The sub-function runs through the IdTable, checks which OIDs are able
 *	to set, and calls the handler function of the OID to perform the
 *	set. The return value of the function will also be stored in
 *	SK_PNMI_STRUCT_DATA if the passed buffer has the minimum size of
 *	SK_PNMI_MIN_STRUCT_SIZE.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 */

int SkPnmiSetStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
void *pBuf,		/* Buffer which contains the data to be set */
unsigned int *pLen)	/* Length of buffer */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
		("PNMI: SkPnmiSetStruct: Called, BufLen=%d\n", *pLen));

	return (PnmiStruct(pAC, IoC, SK_PNMI_SET, (char *)pBuf, pLen));
}

/*****************************************************************************
 *
 * SkPnmiEvent - Event handler
 *
 * Description:
 *	Handles the following events:
 *	SK_PNMI_EVT_SIRQ_OVERFLOW     When a hardware counter overflows an
 *	                              interrupt will be generated which is
 *	                              first handled by SIRQ which generates a
 *	                              this event. The event increments the
 *	                              upper 32 bit of the 64 bit counter.
 *	SK_PNMI_EVT_SEN_XXX           The event is generated by the I2C module
 *	                              when a sensor reports a warning or
 *	                              error. The event will store a trap
 *	                              message in the trap buffer.
 *	SK_PNMI_EVT_CHG_EST_TIMER     The timer event was initiated by this
 *	                              module and is used to calculate the
 *	                              port switches per hour.
 *	SK_PNMI_EVT_CLEAR_COUNTER     The event clears all counters and
 *	                              timestamps.
 *	SK_PNMI_EVT_XMAC_RESET        The event is generated by the driver
 *	                              before a hard reset of the XMAC is
 *	                              performed. All counters will be saved
 *	                              and added to the hardware counter
 *	                              values after reset to grant continuous
 *	                              counter values.
 *	SK_PNMI_EVT_RLMT_PORT_UP      Generated by RLMT to notify that a port
 *	                              went logically up. A trap message will
 *	                              be stored to the trap buffer.
 *	SK_PNMI_EVT_RLMT_PORT_DOWN    Generated by RLMT to notify that a port
 *	                              went logically down. A trap message will
 *	                              be stored to the trap buffer.
 *	SK_PNMI_EVT_RLMT_PORT_SWITCH  Generated by RLMT to notify that the
 *	                              active port switched. PNMI will split
 *	                              this into two message ACTIVE_DOWN and
 *	                              ACTIVE_UP to be future compatible with
 *	                              load balancing and card fail over.
 *	SK_PNMI_EVT_RLMT_SEGMENTATION Generated by RLMT to notify that two
 *	                              spanning tree root bridges were
 *	                              detected. A trap message will be stored
 *	                              to the trap buffer.
 *	SK_PNMI_EVT_RLMT_ACTIVE_DOWN  Notifies PNMI that an active port went
 *	                              down. PNMI will not further add the
 *	                              statistic values to the virtual port.
 *	SK_PNMI_EVT_RLMT_ACTIVE_UP    Notifies PNMI that a port went up and
 *	                              is now an active port. PNMI will now
 *	                              add the statistic data of this port to
 *	                              the virtual port.
 *
 * Returns:
 *	Always 0
 */

int SkPnmiEvent(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Event,		/* Event-Id */
SK_EVPARA Param)	/* Event dependent parameter */
{
	unsigned int	PhysPortIndex;
	int		CounterIndex;
	int		Ret;
	SK_U16		MacStatus;
	SK_U64		OverflowStatus;
	SK_U64		Mask;
	SK_U32		MacCntEvent;
	SK_U64		Value;
	SK_U16		Register;
	SK_EVPARA	EventParam;
	SK_U64		NewestValue;
	SK_U64		OldestValue;
	SK_U64		Delta;
	SK_PNMI_ESTIMATE *pEst;


#ifdef DEBUG
	if (Event != SK_PNMI_EVT_XMAC_RESET) {

		SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
			("PNMI: SkPnmiEvent: Called, Event=0x%x, Param=0x%x\n",
			(unsigned long)Event, (unsigned long)Param.Para64));
	}
#endif
	SK_PNMI_CHECKFLAGS("SkPnmiEvent: On call");

	switch (Event) {

	case SK_PNMI_EVT_SIRQ_OVERFLOW:
		PhysPortIndex = (int)Param.Para32[0];
		MacStatus = (SK_U16)Param.Para32[1];
#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SIRQ_OVERFLOW parameter wrong, PhysPortIndex=0x%x\n",
				PhysPortIndex));
			return (0);
		}
#endif
		OverflowStatus = 0;

		/*
		 * Check which source caused an overflow interrupt. The
		 * interrupt source is a self-clearing register. We only
		 * need to check the interrupt source once. Another check
		 * will be done by the SIRQ module to be sure that no
		 * interrupt get lost during process time.
		 */
		if ((MacStatus & XM_IS_RXC_OV) == XM_IS_RXC_OV) {

			XM_IN32(IoC, PhysPortIndex, XM_RX_CNT_EV,
				&MacCntEvent);
			OverflowStatus |= (SK_U64)MacCntEvent << 32;
		}
		if ((MacStatus & XM_IS_TXC_OV) == XM_IS_TXC_OV) {

			XM_IN32(IoC, PhysPortIndex, XM_TX_CNT_EV,
				&MacCntEvent);
			OverflowStatus |= (SK_U64)MacCntEvent;
		}
		if (OverflowStatus == 0) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}

		/*
		 * Check the overflow status register and increment
		 * the upper dword of corresponding counter.
		 */
		for (CounterIndex = 0; CounterIndex < sizeof(Mask) * 8;
			CounterIndex ++) {

			Mask = (SK_U64)1 << CounterIndex;
			if ((OverflowStatus & Mask) == 0) {

				continue;
			}

			switch (CounterIndex) {

			case SK_PNMI_HTX_UTILUNDER:
			case SK_PNMI_HTX_UTILOVER:
				XM_IN16(IoC, PhysPortIndex, XM_TX_CMD,
					&Register);
				Register |= XM_TX_SAM_LINE;
				XM_OUT16(IoC, PhysPortIndex, XM_TX_CMD,
					Register);
				break;

			case SK_PNMI_HRX_UTILUNDER:
			case SK_PNMI_HRX_UTILOVER:
				XM_IN16(IoC, PhysPortIndex, XM_RX_CMD,
					&Register);
				Register |= XM_RX_SAM_LINE;
				XM_OUT16(IoC, PhysPortIndex, XM_RX_CMD,
					Register);
				break;

			case SK_PNMI_HTX_OCTETHIGH:
			case SK_PNMI_HTX_OCTETLOW:
			case SK_PNMI_HTX_RESERVED26:
			case SK_PNMI_HTX_RESERVED27:
			case SK_PNMI_HTX_RESERVED28:
			case SK_PNMI_HTX_RESERVED29:
			case SK_PNMI_HTX_RESERVED30:
			case SK_PNMI_HTX_RESERVED31:
			case SK_PNMI_HRX_OCTETHIGH:
			case SK_PNMI_HRX_OCTETLOW:
			case SK_PNMI_HRX_IRLENGTH:
			case SK_PNMI_HRX_RESERVED22:
			
			/*
			 * the following counters aren't be handled (id > 63)
			 */
			case SK_PNMI_HTX_SYNC:
			case SK_PNMI_HTX_SYNC_OCTET:
			case SK_PNMI_HRX_LONGFRAMES:
				break;

			default:
				pAC->Pnmi.Port[PhysPortIndex].
					CounterHigh[CounterIndex] ++;
			}
		}
		break;

	case SK_PNMI_EVT_SEN_WAR_LOW:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SEN_WAR_LOW parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif
		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_WAR_LOW,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_SEN_WAR_UPP:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR:SkPnmiEvent: SK_PNMI_EVT_SEN_WAR_UPP parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif
		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_WAR_UPP,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_SEN_ERR_LOW:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SEN_ERR_LOW parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif
		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_ERR_LOW,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;
	
	case SK_PNMI_EVT_SEN_ERR_UPP:
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= (unsigned int)pAC->I2c.MaxSens) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_SEN_ERR_UPP parameter wrong, SensorIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif
		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSensorTrap(pAC, OID_SKGE_TRAP_SEN_ERR_UPP,
			(unsigned int)Param.Para64);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_CHG_EST_TIMER:
		/*
		 * Calculate port switch average on a per hour basis
		 *   Time interval for check       : 28125 ms
		 *   Number of values for average  : 8
		 *
		 * Be careful in changing these values, on change check
		 *   - typedef of SK_PNMI_ESTIMATE (Size of EstValue
		 *     array one less than value number)
		 *   - Timer initilization SkTimerStart() in SkPnmiInit
		 *   - Delta value below must be multiplicated with
		 *     power of 2
		 *
		 */
		pEst = &pAC->Pnmi.RlmtChangeEstimate;
		CounterIndex = pEst->EstValueIndex + 1;
		if (CounterIndex == 7) {

			CounterIndex = 0;
		}
		pEst->EstValueIndex = CounterIndex;

		NewestValue = pAC->Pnmi.RlmtChangeCts;
		OldestValue = pEst->EstValue[CounterIndex];
		pEst->EstValue[CounterIndex] = NewestValue;

		/*
		 * Calculate average. Delta stores the number of
		 * port switches per 28125 * 8 = 225000 ms
		 */
		if (NewestValue >= OldestValue) {

			Delta = NewestValue - OldestValue;
		}
		else {
			/* Overflow situation */
			Delta = (SK_U64)(0 - OldestValue) + NewestValue;
		}

		/*
		 * Extrapolate delta to port switches per hour.
		 *     Estimate = Delta * (3600000 / 225000)
		 *              = Delta * 16
		 *              = Delta << 4
		 */
		pAC->Pnmi.RlmtChangeEstimate.Estimate = Delta << 4;

		/*
		 * Check if threshold is exceeded. If the threshold is
		 * permanently exceeded every 28125 ms an event will be
		 * generated to remind the user of this condition.
		 */
		if ((pAC->Pnmi.RlmtChangeThreshold != 0) &&
			(pAC->Pnmi.RlmtChangeEstimate.Estimate >=
			pAC->Pnmi.RlmtChangeThreshold)) {

			QueueSimpleTrap(pAC, OID_SKGE_TRAP_RLMT_CHANGE_THRES);
			(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		}

		SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
		SkTimerStart(pAC, IoC, &pAC->Pnmi.RlmtChangeEstimate.EstTimer,
			28125000, SKGE_PNMI, SK_PNMI_EVT_CHG_EST_TIMER,
			EventParam);
		break;

	case SK_PNMI_EVT_CLEAR_COUNTER:
		/*
		 * Set all counters and timestamps to zero
		 */
		ResetCounter(pAC, IoC);
		break;

	case SK_PNMI_EVT_XMAC_RESET:
		/*
		 * To grant continuous counter values store the current
		 * XMAC statistic values to the entries 1..n of the
		 * CounterOffset array. XMAC Errata #2 
		 */
#ifdef DEBUG
		if ((unsigned int)Param.Para64 >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_XMAC_RESET parameter wrong, PhysPortIndex=%d\n",
				(unsigned int)Param.Para64));
			return (0);
		}
#endif
		PhysPortIndex = (unsigned int)Param.Para64;

		/*
		 * Update XMAC statistic to get fresh values
		 */
		Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1);
		if (Ret != SK_PNMI_ERR_OK) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}
		/*
		 * Increment semaphore to indicate that an update was
		 * already done
		 */
		pAC->Pnmi.MacUpdatedFlag ++;

		for (CounterIndex = 0; CounterIndex < SK_PNMI_SCNT_NOT;
			CounterIndex ++) {

			if (!StatAddress[CounterIndex].GetOffset) {

				continue;
			}

			pAC->Pnmi.Port[PhysPortIndex].
				CounterOffset[CounterIndex] = GetPhysStatVal(
				pAC, IoC, PhysPortIndex, CounterIndex);
			pAC->Pnmi.Port[PhysPortIndex].
				CounterHigh[CounterIndex] = 0;
		}

		pAC->Pnmi.MacUpdatedFlag --;
		break;

	case SK_PNMI_EVT_RLMT_PORT_UP:
#ifdef DEBUG
		if ((unsigned int)Param.Para32[0] >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_PORT_UP parameter wrong, PhysPortIndex=%d\n",
				(unsigned int)Param.Para32[0]));

			return (0);
		}
#endif
		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueRlmtPortTrap(pAC, OID_SKGE_TRAP_RLMT_PORT_UP,
			(unsigned int)Param.Para32[0]);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_RLMT_PORT_DOWN:
#ifdef DEBUG
		if ((unsigned int)Param.Para32[0] >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_PORT_DOWN parameter wrong, PhysPortIndex=%d\n",
				(unsigned int)Param.Para32[0]));

			return (0);
		}
#endif
		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueRlmtPortTrap(pAC, OID_SKGE_TRAP_RLMT_PORT_DOWN,
			(unsigned int)Param.Para32[0]);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	case SK_PNMI_EVT_RLMT_ACTIVE_DOWN:
		PhysPortIndex = (unsigned int)Param.Para32[0];
#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_ACTIVE_DOWN parameter too high, PhysPort=%d\n",
				PhysPortIndex));
		}
#endif
		/*
		 * Nothing to do if port is already inactive
		 */
		if (!pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

			return (0);
		}

		/*
		 * Update statistic counters to calculate new offset
		 * for the virtual port and increment semaphore to
		 * indicate that an update was already done.
		 */
		if (MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1) !=
			SK_PNMI_ERR_OK) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}
		pAC->Pnmi.MacUpdatedFlag ++;

		/*
		 * Calculate new counter offset for virtual port to
		 * grant continous counting on port switches. The virtual
		 * port consists of all currently active ports. The port
		 * down event indicates that a port is removed fromt the
		 * virtual port. Therefore add the counter value of the
		 * removed port to the CounterOffset for the virtual port
		 * to grant the same counter value.
		 */
		for (CounterIndex = 0; CounterIndex < SK_PNMI_MAX_IDX;
			CounterIndex ++) {

			if (!StatAddress[CounterIndex].GetOffset) {

				continue;
			}

			Value = GetPhysStatVal(pAC, IoC, PhysPortIndex,
				CounterIndex);

			pAC->Pnmi.VirtualCounterOffset[CounterIndex] += Value;
		}

		/*
		 * Set port to inactive
		 */
		pAC->Pnmi.Port[PhysPortIndex].ActiveFlag = SK_FALSE;

		pAC->Pnmi.MacUpdatedFlag --;
		break;

	case SK_PNMI_EVT_RLMT_ACTIVE_UP:
		PhysPortIndex = (unsigned int)Param.Para32[0];
#ifdef DEBUG
		if (PhysPortIndex >= SK_MAX_MACS) {

			SK_DBG_MSG(pAC, SK_DBGMOD_PNMI, SK_DBGCAT_CTRL,
				("PNMI: ERR: SkPnmiEvent: SK_PNMI_EVT_RLMT_ACTIVE_UP parameter too high, PhysPort=%d\n",
				PhysPortIndex));
		}
#endif
		/*
		 * Nothing to do if port is already active
		 */
		if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

			return (0);
		}

		/*
		 * Statistic maintanence
		 */
		pAC->Pnmi.RlmtChangeCts ++;
		pAC->Pnmi.RlmtChangeTime =
			SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));

		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueRlmtNewMacTrap(pAC, PhysPortIndex);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);

		/*
		 * Update statistic counters to calculate new offset
		 * for the virtual port and increment semaphore to indicate
		 * that an update was already done.
		 */
		if (MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1) !=
			SK_PNMI_ERR_OK) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}
		pAC->Pnmi.MacUpdatedFlag ++;

		/*
		 * Calculate new counter offset for virtual port to
		 * grant continous counting on port switches. A new port
		 * is added to the virtual port. Therefore substract the
		 * counter value of the new port from the CounterOffset
		 * for the virtual port to grant the same value.
		 */
		for (CounterIndex = 0; CounterIndex < SK_PNMI_MAX_IDX;
			CounterIndex ++) {

			if (!StatAddress[CounterIndex].GetOffset) {

				continue;
			}

			Value = GetPhysStatVal(pAC, IoC, PhysPortIndex,
				CounterIndex);

			pAC->Pnmi.VirtualCounterOffset[CounterIndex] -= Value;
		}

		/*
		 * Set port to active
		 */
		pAC->Pnmi.Port[PhysPortIndex].ActiveFlag = SK_TRUE;

		pAC->Pnmi.MacUpdatedFlag --;
		break;

	case SK_PNMI_EVT_RLMT_PORT_SWITCH:
		/*
		 * This event becomes obsolete if RLMT generates directly
		 * the events SK_PNMI_EVT_RLMT_ACTIVE_DOWN and
		 * SK_PNMI_EVT_RLMT_ACTIVE_UP. The events are here emulated.
		 * PNMI handles that multiple ports may become active. 
		 * Increment semaphore to indicate that an update was
		 * already done.
		 */
		if (MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1) !=
			SK_PNMI_ERR_OK) {

			SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
			return (0);
		}
		pAC->Pnmi.MacUpdatedFlag ++;

		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_RLMT_ACTIVE_DOWN, Param);
		Param.Para32[0] = Param.Para32[1];
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_RLMT_ACTIVE_UP, Param);

		pAC->Pnmi.MacUpdatedFlag --;
		break;

	case SK_PNMI_EVT_RLMT_SEGMENTATION:
		/*
		 * Store a trap message in the trap buffer and generate
		 * an event for user space applications with the
		 * SK_DRIVER_SENDEVENT macro.
		 */
		QueueSimpleTrap(pAC, OID_SKGE_TRAP_RLMT_SEGMENTATION);
		(void)SK_DRIVER_SENDEVENT(pAC, IoC);
		break;

	default:
		break;
	}

	SK_PNMI_CHECKFLAGS("SkPnmiEvent: On return");
	return (0);
}


/******************************************************************************
 *
 * Private functions
 *
 */

/*****************************************************************************
 *
 * PnmiVar - Gets, presets, and sets single OIDs
 *
 * Description:
 *	Looks up the requested OID, calls the corresponding handler
 *	function, and passes the parameters with the get, preset, or
 *	set command. The function is called by SkGePnmiGetVar,
 *	SkGePnmiPreSetVar, or SkGePnmiSetVar.
 *
 * Returns:
 *	SK_PNMI_ERR_XXX. For details have a look to the description of the
 *	calling functions.
 */

static int PnmiVar(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer which stores the mgmt data to be set */
unsigned int *pLen,	/* Total length of mgmt data */
SK_U32 Instance)	/* Instance (1..n) that is to be set or -1 */
{
	unsigned int	TableIndex;
	int		Ret;


	if ((TableIndex = LookupId(Id)) == (unsigned int)(-1)) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_OID);
	}

	SK_PNMI_CHECKFLAGS("PnmiVar: On call");

	Ret = IdTable[TableIndex].Func(pAC, IoC, Action, Id, pBuf, pLen,
		Instance, TableIndex);

	SK_PNMI_CHECKFLAGS("PnmiVar: On return");

	return (Ret);
}

/*****************************************************************************
 *
 * PnmiStruct - Presets and Sets data in structure SK_PNMI_STRUCT_DATA
 *
 * Description:
 *	The return value of the function will also be stored in
 *	SK_PNMI_STRUCT_DATA if the passed buffer has the minimum size of
 *	SK_PNMI_MIN_STRUCT_SIZE. The sub-function runs through the IdTable,
 *	checks which OIDs are able to set, and calls the handler function of
 *	the OID to perform the set. The return value of the function will
 *	also be stored in SK_PNMI_STRUCT_DATA if the passed buffer has the
 *	minimum size of SK_PNMI_MIN_STRUCT_SIZE. The function is called
 *	by SkGePnmiPreSetStruct and SkGePnmiSetStruct.
 *
 * Returns:
 *	SK_PNMI_ERR_XXX. The codes are described in the calling functions.
 */

static int PnmiStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int  Action,		/* Set action to be performed */
char *pBuf,		/* Buffer which contains the data to be set */
unsigned int *pLen)	/* Length of buffer */
{
	int		Ret;
	unsigned int	TableIndex;
	unsigned int	DstOffset;
	unsigned int	Len;
	unsigned int	InstanceNo;
	unsigned int	InstanceCnt;
	SK_U32		Instance;
	SK_U32		Id;


	/* Check if the passed buffer has the right size */
	if (*pLen < SK_PNMI_STRUCT_SIZE) {

		/* Check if we can return the error within the buffer */
		if (*pLen >= SK_PNMI_MIN_STRUCT_SIZE) {

			SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_TOO_SHORT,
				(SK_U32)(-1));
		}

		*pLen = SK_PNMI_STRUCT_SIZE;
		return (SK_PNMI_ERR_TOO_SHORT);
	}
	
	SK_PNMI_CHECKFLAGS("PnmiStruct: On call");

	/*
	 * Update the values of RLMT and SIRQ and increment semaphores to
	 * indicate that an update was already done.
	 */
	if ((Ret = RlmtUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	if ((Ret = SirqUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

		SK_PNMI_SET_STAT(pBuf, Ret, (SK_U32)(-1));
		*pLen = SK_PNMI_MIN_STRUCT_SIZE;
		return (Ret);
	}

	pAC->Pnmi.RlmtUpdatedFlag ++;
	pAC->Pnmi.SirqUpdatedFlag ++;

	/* Preset/Set values */
	for (TableIndex = 0; TableIndex < sizeof(IdTable)/sizeof(IdTable[0]);
		TableIndex ++) {

		if (IdTable[TableIndex].Access != SK_PNMI_RW) {

			continue;
		}

		InstanceNo = IdTable[TableIndex].InstanceNo;
		Id = IdTable[TableIndex].Id;

		for (InstanceCnt = 1; InstanceCnt <= InstanceNo;
			InstanceCnt ++) {

			DstOffset = IdTable[TableIndex].Offset +
				(InstanceCnt - 1) *
				IdTable[TableIndex].StructSize;

			/*
			 * Because VPD multiple instance variables are
			 * not setable we do not need to evaluate VPD
			 * instances. Have a look to VPD instance
			 * calculation in SkPnmiGetStruct().
			 */
			Instance = (SK_U32)InstanceCnt;

			/*
			 * Evaluate needed buffer length
			 */
			Len = 0;
			Ret = IdTable[TableIndex].Func(pAC, IoC,
				SK_PNMI_GET, IdTable[TableIndex].Id,
				NULL, &Len, Instance, TableIndex);

			if (Ret == SK_PNMI_ERR_UNKNOWN_INST) {

				break;
			}
			if (Ret != SK_PNMI_ERR_TOO_SHORT) {

				pAC->Pnmi.RlmtUpdatedFlag --;
				pAC->Pnmi.SirqUpdatedFlag --;

				SK_PNMI_CHECKFLAGS("PnmiStruct: On return");
				SK_PNMI_SET_STAT(pBuf,
					SK_PNMI_ERR_GENERAL, DstOffset);
				*pLen = SK_PNMI_MIN_STRUCT_SIZE;
				return (SK_PNMI_ERR_GENERAL);
			}
			if (Id == OID_SKGE_VPD_ACTION) {

				switch (*(pBuf + DstOffset)) {

				case SK_PNMI_VPD_CREATE:
					Len = 3 + *(pBuf + DstOffset + 3);
					break;

				case SK_PNMI_VPD_DELETE:
					Len = 3;
					break;

				default:
					Len = 1;
					break;
				}
			}

			/* Call the OID handler function */
			Ret = IdTable[TableIndex].Func(pAC, IoC, Action,
				IdTable[TableIndex].Id, pBuf + DstOffset,
				&Len, Instance, TableIndex);

			if (Ret != SK_PNMI_ERR_OK) {

				pAC->Pnmi.RlmtUpdatedFlag --;
				pAC->Pnmi.SirqUpdatedFlag --;

				SK_PNMI_CHECKFLAGS("PnmiStruct: On return");
				SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_BAD_VALUE,
					DstOffset);
				*pLen = SK_PNMI_MIN_STRUCT_SIZE;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
		}
	}

	pAC->Pnmi.RlmtUpdatedFlag --;
	pAC->Pnmi.SirqUpdatedFlag --;

	SK_PNMI_CHECKFLAGS("PnmiStruct: On return");
	SK_PNMI_SET_STAT(pBuf, SK_PNMI_ERR_OK, (SK_U32)(-1));
	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * LookupId - Lookup an OID in the IdTable
 *
 * Description:
 *	Scans the IdTable to find the table entry of an OID.
 *
 * Returns:
 *	The table index or -1 if not found.
 */

static int LookupId(
SK_U32 Id)		/* Object identifier to be searched */
{
	int i;
	int Len = sizeof(IdTable)/sizeof(IdTable[0]);

	for (i=0; i<Len; i++) {

		if (IdTable[i].Id == Id) {

			return i;
		}
	}

	return (-1);
}

/*****************************************************************************
 *
 * OidStruct - Handler of OID_SKGE_ALL_DATA
 *
 * Description:
 *	This OID performs a Get/Preset/SetStruct call and returns all data
 *	in a SK_PNMI_STRUCT_DATA structure.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int OidStruct(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	if (Id != OID_SKGE_ALL_DATA) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR003,
			SK_PNMI_ERR003MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);
	}

	/*
	 * Check instance. We only handle single instance variables
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	switch (Action) {

	case SK_PNMI_GET:
		return (SkPnmiGetStruct(pAC, IoC, pBuf, pLen));

	case SK_PNMI_PRESET:
		return (SkPnmiPreSetStruct(pAC, IoC, pBuf, pLen));

	case SK_PNMI_SET:
		return (SkPnmiSetStruct(pAC, IoC, pBuf, pLen));
	}

	SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR004, SK_PNMI_ERR004MSG);

	*pLen = 0;
	return (SK_PNMI_ERR_GENERAL);
}

/*****************************************************************************
 *
 * Perform - OID handler of OID_SKGE_ACTION
 *
 * Description:
 *	None.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int Perform(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	int	Ret;
	SK_U32	ActionOp;


	/*
	 * Check instance. We only handle single instance variables
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	if (*pLen < sizeof(SK_U32)) {

		*pLen = sizeof(SK_U32);
		return (SK_PNMI_ERR_TOO_SHORT);
	}

	/* Check if a get should be performed */
	if (Action == SK_PNMI_GET) {

		/* A get is easy. We always return the same value */
		ActionOp = (SK_U32)SK_PNMI_ACT_IDLE;
		SK_PNMI_STORE_U32(pBuf, ActionOp);
		*pLen = sizeof(SK_U32);

		return (SK_PNMI_ERR_OK);
	}

	/* Continue with PRESET/SET action */
	if (*pLen > sizeof(SK_U32)) {

		return (SK_PNMI_ERR_BAD_VALUE);
	}

	/* Check if the command is a known one */
	SK_PNMI_READ_U32(pBuf, ActionOp);
	if (*pLen > sizeof(SK_U32) ||
		(ActionOp != SK_PNMI_ACT_IDLE &&
		ActionOp != SK_PNMI_ACT_RESET &&
		ActionOp != SK_PNMI_ACT_SELFTEST &&
		ActionOp != SK_PNMI_ACT_RESETCNT)) {

		*pLen = 0;
		return (SK_PNMI_ERR_BAD_VALUE);
	}

	/* A preset ends here */
	if (Action == SK_PNMI_PRESET) {

		return (SK_PNMI_ERR_OK);
	}

	switch (ActionOp) {

	case SK_PNMI_ACT_IDLE:
		/* Nothing to do */
		break;

	case SK_PNMI_ACT_RESET:
		/*
		 * Perform a driver reset or something that comes near
		 * to this.
		 */
		Ret = SK_DRIVER_RESET(pAC, IoC);
		if (Ret != 0) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR005,
				SK_PNMI_ERR005MSG);

			return (SK_PNMI_ERR_GENERAL);
		}
		break;

	case SK_PNMI_ACT_SELFTEST:
		/*
		 * Perform a driver selftest or something similar to this.
		 * Currently this feature is not used and will probably
		 * implemented in another way.
		 */
		Ret = SK_DRIVER_SELFTEST(pAC, IoC);
		pAC->Pnmi.TestResult = Ret;
		break;

	case SK_PNMI_ACT_RESETCNT:
		/* Set all counters and timestamps to zero */
		ResetCounter(pAC, IoC);
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR006,
			SK_PNMI_ERR006MSG);

		return (SK_PNMI_ERR_GENERAL);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Mac8023Stat - OID handler of OID_GEN_XXX and OID_802_3_XXX
 *
 * Description:
 *	Retrieves the statistic values of the virtual port (logical
 *	index 0). Only special OIDs of NDIS are handled which consist
 *	of a 32 bit instead of a 64 bit value. The OIDs are public
 *	because perhaps some other platform can use them too.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int Mac8023Stat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	int    Ret;
	SK_U64 StatVal;
	SK_BOOL Is64BitReq = SK_FALSE;

	/*
	 * Only the active Mac is returned
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	/*
	 * Check action type
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Check length
	 */
	switch (Id) {

	case OID_802_3_PERMANENT_ADDRESS:
	case OID_802_3_CURRENT_ADDRESS:
		if (*pLen < sizeof(SK_MAC_ADDR)) {

			*pLen = sizeof(SK_MAC_ADDR);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
#ifndef SK_NDIS_64BIT_CTR
		if (*pLen < sizeof(SK_U32)) {
			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}

#else /* SK_NDIS_64BIT_CTR */
		
		/*
		 * for compatibility, at least 32bit are required for oid
		 */
		if (*pLen < sizeof(SK_U32)) {
			/*
			* but indicate handling for 64bit values,
			* if insufficient space is provided
			*/
			*pLen = sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		
		Is64BitReq = (*pLen < sizeof(SK_U64)) ? SK_FALSE : SK_TRUE;
#endif /* SK_NDIS_64BIT_CTR */
		break;
	}

	/*
	 * Update all statistics, because we retrieve virtual MAC, which
	 * consists of multiple physical statistics and increment semaphore
	 * to indicate that an update was already done.
	 */
	Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1);
	if ( Ret != SK_PNMI_ERR_OK) {

		*pLen = 0;
		return (Ret);
	}
	pAC->Pnmi.MacUpdatedFlag ++;

	/*
	 * Get value (MAC Index 0 identifies the virtual MAC)
	 */
	switch (Id) {

	case OID_802_3_PERMANENT_ADDRESS:
		CopyMac(pBuf, &pAC->Addr.PermanentMacAddress);
		*pLen = sizeof(SK_MAC_ADDR);
		break;

	case OID_802_3_CURRENT_ADDRESS:
		CopyMac(pBuf, &pAC->Addr.CurrentMacAddress);
		*pLen = sizeof(SK_MAC_ADDR);
		break;

	default:
		StatVal = GetStatVal(pAC, IoC, 0, IdTable[TableIndex].Param);

		/*
		 * by default 32bit values are evaluated
		 */
		if (!Is64BitReq) {
			SK_U32	StatVal32;
			StatVal32 = (SK_U32)StatVal;
			SK_PNMI_STORE_U32(pBuf, StatVal32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, StatVal);
			*pLen = sizeof(SK_U64);
		}
		break;
	}

	pAC->Pnmi.MacUpdatedFlag --;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * MacPrivateStat - OID handler function of OID_SKGE_STAT_XXX
 *
 * Description:
 *	Retrieves the XMAC statistic data.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int MacPrivateStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	unsigned int	LogPortMax;
	unsigned int	LogPortIndex;
	unsigned int	PhysPortMax;
	unsigned int	Limit;
	unsigned int	Offset;
	int		Ret;
	SK_U64		StatVal;


	/*
	 * Calculate instance if wished. MAC index 0 is the virtual
	 * MAC.
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;
	LogPortMax = SK_PNMI_PORT_PHYS2LOG(PhysPortMax);

	if ((Instance != (SK_U32)(-1))) {

		if ((Instance < 1) || (Instance > LogPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		LogPortIndex = SK_PNMI_PORT_INST2LOG(Instance);
		Limit = LogPortIndex + 1;
	}
	else {
		LogPortIndex = 0;
		Limit = LogPortMax;
	}

	/*
	 * Check action
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Check length
	 */
	if (*pLen < (Limit - LogPortIndex) * sizeof(SK_U64)) {

		*pLen = (Limit - LogPortIndex) * sizeof(SK_U64);
		return (SK_PNMI_ERR_TOO_SHORT);
	}

	/*
	 * Update XMAC statistic and increment semaphore to indicate that
	 * an update was already done.
	 */
	Ret = MacUpdate(pAC, IoC, 0, PhysPortMax - 1);
	if (Ret != SK_PNMI_ERR_OK) {

		*pLen = 0;
		return (Ret);
	}
	pAC->Pnmi.MacUpdatedFlag ++;

	/*
	 * Get value
	 */
	Offset = 0;
	for (; LogPortIndex < Limit; LogPortIndex ++) {

		switch (Id) {

/* XXX not yet implemented due to XMAC problems
		case OID_SKGE_STAT_TX_UTIL:
			return (SK_PNMI_ERR_GENERAL);
*/
/* XXX not yet implemented due to XMAC problems
		case OID_SKGE_STAT_RX_UTIL:
			return (SK_PNMI_ERR_GENERAL);
*/
		/*
		 * Frames longer than IEEE 802.3 frame max size are counted
		 * by XMAC in frame_too_long counter even reception of long
		 * frames was enabled and the frame was correct.
		 * So correct the value by subtracting RxLongFrame counter.
		 */
		case OID_SKGE_STAT_RX_TOO_LONG:
			StatVal = GetStatVal(pAC, IoC, LogPortIndex,
					     IdTable[TableIndex].Param) -
				GetStatVal(pAC, IoC, LogPortIndex,
					   SK_PNMI_HRX_LONGFRAMES);
			SK_PNMI_STORE_U64(pBuf + Offset, StatVal);
			break;

		default:
			StatVal = GetStatVal(pAC, IoC, LogPortIndex,
				IdTable[TableIndex].Param);
			SK_PNMI_STORE_U64(pBuf + Offset, StatVal);
			break;
		}

		Offset += sizeof(SK_U64);
	}
	*pLen = Offset;

	pAC->Pnmi.MacUpdatedFlag --;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Addr - OID handler function of OID_SKGE_PHYS_CUR_ADDR and _FAC_ADDR
 *
 * Description:
 *	Get/Presets/Sets the current and factory MAC address. The MAC
 *	address of the virtual port, which is reported to the OS, may
 *	not be changed, but the physical ones. A set to the virtual port
 *	will be ignored. No error should be reported because otherwise
 *	a multiple instance set (-1) would always fail.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int Addr(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	int		Ret;
	unsigned int	LogPortMax;
	unsigned int	PhysPortMax;
	unsigned int	LogPortIndex;
	unsigned int	PhysPortIndex;
	unsigned int	Limit;
	unsigned int	Offset = 0;


	/*
	 * Calculate instance if wished
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;
	LogPortMax = SK_PNMI_PORT_PHYS2LOG(PhysPortMax);

	if ((Instance != (SK_U32)(-1))) {
		
		if ((Instance < 1) || (Instance > LogPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		LogPortIndex = SK_PNMI_PORT_INST2LOG(Instance);
		Limit = LogPortIndex + 1;
	}
	else {
		LogPortIndex = 0;
		Limit = LogPortMax;
	}

	/*
	 * Perform Action
	 */
	if (Action == SK_PNMI_GET) {

		/*
		 * Check length
		*/
		if (*pLen < (Limit - LogPortIndex) * 6) {

			*pLen = (Limit - LogPortIndex) * 6;
			return (SK_PNMI_ERR_TOO_SHORT);
		}

		/*
		 * Get value
		 */
		for (; LogPortIndex < Limit; LogPortIndex ++) {

			switch (Id) {

			case OID_SKGE_PHYS_CUR_ADDR:
				if (LogPortIndex == 0) {

					CopyMac(pBuf + Offset, &pAC->Addr.
						CurrentMacAddress);
				}
				else {
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					CopyMac(pBuf + Offset, &pAC->Addr.
						Port[PhysPortIndex].
						CurrentMacAddress);
				}
				Offset += 6;
				break;

			case OID_SKGE_PHYS_FAC_ADDR:
				if (LogPortIndex == 0) {

					CopyMac(pBuf + Offset, &pAC->Addr.
						PermanentMacAddress);
				}
				else {
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					CopyMac(pBuf + Offset, &pAC->Addr.
						Port[PhysPortIndex].
						PermanentMacAddress);
				}
				Offset += 6;
				break;

			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR008,
					SK_PNMI_ERR008MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
		}

		*pLen = Offset;
	}
	else {
		/*
		 * The logical MAC address may not be changed only
		 * the physical ones
		 */
		if (Id == OID_SKGE_PHYS_FAC_ADDR) {

			*pLen = 0;
			return (SK_PNMI_ERR_READ_ONLY);
		}

		/*
		 * Only the current address may be changed
		 */
		if (Id != OID_SKGE_PHYS_CUR_ADDR) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR009,
				SK_PNMI_ERR009MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/*
		 * Check length
		*/
		if (*pLen < (Limit - LogPortIndex) * 6) {

			*pLen = (Limit - LogPortIndex) * 6;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		if (*pLen > (Limit - LogPortIndex) * 6) {

			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}

		/*
		 * Check Action
		 */
		if (Action == SK_PNMI_PRESET) {

			*pLen = 0;
			return (SK_PNMI_ERR_OK);
		}

		/*
		 * Set OID_SKGE_MAC_CUR_ADDR
		 */
		for (; LogPortIndex < Limit; LogPortIndex ++, Offset += 6) {

			/*
			 * A set to virtual port and set of broadcast
			 * address will be ignored
			 */
			if (LogPortIndex == 0 || SK_MEMCMP(pBuf + Offset,
				"\xff\xff\xff\xff\xff\xff", 6) == 0) {

				continue;
			}

			PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(pAC,
				LogPortIndex);

			Ret = SkAddrOverride(pAC, IoC, PhysPortIndex,
				(SK_MAC_ADDR *)(pBuf + Offset),
				(LogPortIndex == 0 ? SK_ADDR_VIRTUAL_ADDRESS :
				SK_ADDR_PHYSICAL_ADDRESS));
			if (Ret != SK_ADDR_OVERRIDE_SUCCESS) {

				return (SK_PNMI_ERR_GENERAL);
			}
		}
		*pLen = Offset;
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * CsumStat - OID handler function of OID_SKGE_CHKSM_XXX
 *
 * Description:
 *	Retrieves the statistic values of the CSUM module. The CSUM data
 *	structure must be available in the SK_AC even if the CSUM module
 *	is not included, because PNMI reads the statistic data from the
 *	CSUM part of SK_AC directly.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int CsumStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	unsigned int	Index;
	unsigned int	Limit;
	unsigned int	Offset = 0;
	SK_U64		StatVal;


	/*
	 * Calculate instance if wished
	 */
	if (Instance != (SK_U32)(-1)) {
		
		if ((Instance < 1) || (Instance > SKCS_NUM_PROTOCOLS)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		Index = (unsigned int)Instance - 1;
		Limit = (unsigned int)Instance;
	}
	else {
		Index = 0;
		Limit = SKCS_NUM_PROTOCOLS;
	}

	/*
	 * Check action
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Check length
	 */
	if (*pLen < (Limit - Index) * sizeof(SK_U64)) {

		*pLen = (Limit - Index) * sizeof(SK_U64);
		return (SK_PNMI_ERR_TOO_SHORT);
	}

	/*
	 * Get value
	 */
	for (; Index < Limit; Index ++) {

		switch (Id) {

		case OID_SKGE_CHKSM_RX_OK_CTS:
			StatVal = pAC->Csum.ProtoStats[Index].RxOkCts;
			break;

		case OID_SKGE_CHKSM_RX_UNABLE_CTS:
			StatVal = pAC->Csum.ProtoStats[Index].RxUnableCts;
			break;

		case OID_SKGE_CHKSM_RX_ERR_CTS:
			StatVal = pAC->Csum.ProtoStats[Index].RxErrCts;
			break;

		case OID_SKGE_CHKSM_TX_OK_CTS:
			StatVal = pAC->Csum.ProtoStats[Index].TxOkCts;
			break;

		case OID_SKGE_CHKSM_TX_UNABLE_CTS:
			StatVal = pAC->Csum.ProtoStats[Index].TxUnableCts;
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR010,
				SK_PNMI_ERR010MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		SK_PNMI_STORE_U64(pBuf + Offset, StatVal);
		Offset += sizeof(SK_U64);
	}

	/*
	 * Store used buffer space
	 */
	*pLen = Offset;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * SensorStat - OID handler function of OID_SKGE_SENSOR_XXX
 *
 * Description:
 *	Retrieves the statistic values of the I2C module, which handles
 *	the temperature and voltage sensors.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int SensorStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	unsigned int	i;
	unsigned int	Index;
	unsigned int	Limit;
	unsigned int	Offset;
	unsigned int	Len;
	SK_U32		Val32;
	SK_U64		Val64;


	/*
	 * Calculate instance if wished
	 */
	if ((Instance != (SK_U32)(-1))) {

		if ((Instance < 1) || (Instance > (SK_U32)pAC->I2c.MaxSens)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		Index = (unsigned int)Instance -1;
		Limit = (unsigned int)Instance;
	}
	else {
		Index = 0;
		Limit = (unsigned int) pAC->I2c.MaxSens;
	}

	/*
	 * Check action
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Check length
	 */
	switch (Id) {

	case OID_SKGE_SENSOR_VALUE:
	case OID_SKGE_SENSOR_WAR_THRES_LOW:
	case OID_SKGE_SENSOR_WAR_THRES_UPP:
	case OID_SKGE_SENSOR_ERR_THRES_LOW:
	case OID_SKGE_SENSOR_ERR_THRES_UPP:
		if (*pLen < (Limit - Index) * sizeof(SK_U32)) {

			*pLen = (Limit - Index) * sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_SENSOR_DESCR:
		for (Offset = 0, i = Index; i < Limit; i ++) {

			Len = (unsigned int)
				SK_STRLEN(pAC->I2c.SenTable[i].SenDesc) + 1;
			if (Len >= SK_PNMI_STRINGLEN2) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR011,
					SK_PNMI_ERR011MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			Offset += Len;
		}
		if (*pLen < Offset) {

			*pLen = Offset;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_SENSOR_INDEX:
	case OID_SKGE_SENSOR_TYPE:
	case OID_SKGE_SENSOR_STATUS:
		if (*pLen < Limit - Index) {

			*pLen = Limit - Index;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_SENSOR_WAR_CTS:
	case OID_SKGE_SENSOR_WAR_TIME:
	case OID_SKGE_SENSOR_ERR_CTS:
	case OID_SKGE_SENSOR_ERR_TIME:
		if (*pLen < (Limit - Index) * sizeof(SK_U64)) {

			*pLen = (Limit - Index) * sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR012,
			SK_PNMI_ERR012MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);

	}

	/*
	 * Get value
	 */
	for (Offset = 0; Index < Limit; Index ++) {

		switch (Id) {

		case OID_SKGE_SENSOR_INDEX:
			*(pBuf + Offset) = (char)Index;
			Offset += sizeof(char);
			break;

		case OID_SKGE_SENSOR_DESCR:
			Len = SK_STRLEN(pAC->I2c.SenTable[Index].SenDesc);
			SK_MEMCPY(pBuf + Offset + 1,
				pAC->I2c.SenTable[Index].SenDesc, Len);
			*(pBuf + Offset) = (char)Len;
			Offset += Len + 1;
			break;

		case OID_SKGE_SENSOR_TYPE:
			*(pBuf + Offset) =
				(char)pAC->I2c.SenTable[Index].SenType;
			Offset += sizeof(char);
			break;

		case OID_SKGE_SENSOR_VALUE:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].SenValue;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_WAR_THRES_LOW:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].
				SenThreWarnLow;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_WAR_THRES_UPP:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].
				SenThreWarnHigh;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_ERR_THRES_LOW:
			Val32 = (SK_U32)pAC->I2c.SenTable[Index].
				SenThreErrLow;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_ERR_THRES_UPP:
			Val32 = pAC->I2c.SenTable[Index].SenThreErrHigh;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_SENSOR_STATUS:
			*(pBuf + Offset) =
				(char)pAC->I2c.SenTable[Index].SenErrFlag;
			Offset += sizeof(char);
			break;

		case OID_SKGE_SENSOR_WAR_CTS:
			Val64 = pAC->I2c.SenTable[Index].SenWarnCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_SENSOR_ERR_CTS:
			Val64 = pAC->I2c.SenTable[Index].SenErrCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_SENSOR_WAR_TIME:
			Val64 = SK_PNMI_HUNDREDS_SEC(pAC->I2c.SenTable[Index].
				SenBegWarnTS);
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_SENSOR_ERR_TIME:
			Val64 = SK_PNMI_HUNDREDS_SEC(pAC->I2c.SenTable[Index].
				SenBegErrTS);
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR013,
				SK_PNMI_ERR013MSG);

			return (SK_PNMI_ERR_GENERAL);
		}
	}

	/*
	 * Store used buffer space
	 */
	*pLen = Offset;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Vpd - OID handler function of OID_SKGE_VPD_XXX
 *
 * Description:
 *	Get/preset/set of VPD data. As instance the name of a VPD key
 *	can be passed. The Instance parameter is a SK_U32 and can be
 *	used as a string buffer for the VPD key, because their maximum
 *	length is 4 byte.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int Vpd(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	SK_VPD_STATUS	*pVpdStatus;
	unsigned int	BufLen;
	char		Buf[256];
	char		KeyArr[SK_PNMI_VPD_ARR_SIZE][SK_PNMI_VPD_STR_SIZE];
	char		KeyStr[SK_PNMI_VPD_STR_SIZE];
	unsigned int	KeyNo;
	unsigned int	Offset;
	unsigned int	Index;
	unsigned int	FirstIndex;
	unsigned int	LastIndex;
	unsigned int	Len;
	int		Ret;
	SK_U32		Val32;

	/*
	 * Get array of all currently stored VPD keys
	 */
	Ret = GetVpdKeyArr(pAC, IoC, &KeyArr[0][0], sizeof(KeyArr),
		&KeyNo);
	if (Ret != SK_PNMI_ERR_OK) {

		*pLen = 0;
		return (Ret);
	}

	/*
	 * If instance is not -1, try to find the requested VPD key for
	 * the multiple instance variables. The other OIDs as for example
	 * OID VPD_ACTION are single instance variables and must be
	 * handled separatly.
	 */
	FirstIndex = 0;
	LastIndex = KeyNo;

	if ((Instance != (SK_U32)(-1))) {

		if (Id == OID_SKGE_VPD_KEY || Id == OID_SKGE_VPD_VALUE ||
			Id == OID_SKGE_VPD_ACCESS) {

			SK_STRNCPY(KeyStr, (char *)&Instance, 4);
			KeyStr[4] = 0;

			for (Index = 0; Index < KeyNo; Index ++) {

				if (SK_STRCMP(KeyStr, KeyArr[Index]) == 0) {

					FirstIndex = Index;
					LastIndex = Index+1;
					break;
				}
			}
			if (Index == KeyNo) {

				*pLen = 0;
				return (SK_PNMI_ERR_UNKNOWN_INST);
			}
		}
		else if (Instance != 1) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}
	}

	/*
	 * Get value, if a query should be performed
	 */
	if (Action == SK_PNMI_GET) {

		switch (Id) {

		case OID_SKGE_VPD_FREE_BYTES:
			/* Check length of buffer */
			if (*pLen < sizeof(SK_U32)) {

				*pLen = sizeof(SK_U32);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/* Get number of free bytes */
			pVpdStatus = VpdStat(pAC, IoC);
			if (pVpdStatus == NULL) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR017,
					SK_PNMI_ERR017MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			if ((pVpdStatus->vpd_status & VPD_VALID) == 0) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR018,
					SK_PNMI_ERR018MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			
			Val32 = (SK_U32)pVpdStatus->vpd_free_rw;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
			break;

		case OID_SKGE_VPD_ENTRIES_LIST:
			/* Check length */
			for (Len = 0, Index = 0; Index < KeyNo; Index ++) {

				Len += SK_STRLEN(KeyArr[Index]) + 1;
			}
			if (*pLen < Len) {

				*pLen = Len;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			/* Get value */
			*(pBuf) = (char)Len - 1;
			for (Offset = 1, Index = 0; Index < KeyNo; Index ++) {

				Len = SK_STRLEN(KeyArr[Index]);
				SK_MEMCPY(pBuf + Offset, KeyArr[Index], Len);

				Offset += Len;

				if (Index < KeyNo - 1) {

					*(pBuf + Offset) = ' ';
					Offset ++;
				}
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_ENTRIES_NUMBER:
			/* Check length */
			if (*pLen < sizeof(SK_U32)) {

				*pLen = sizeof(SK_U32);
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			Val32 = (SK_U32)KeyNo;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
			break;

		case OID_SKGE_VPD_KEY:
			/* Check buffer length, if it is large enough */
			for (Len = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				Len += SK_STRLEN(KeyArr[Index]) + 1;
			}
			if (*pLen < Len) {

				*pLen = Len;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			/*
			 * Get the key to an intermediate buffer, because
			 * we have to prepend a length byte.
			 */
			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				Len = SK_STRLEN(KeyArr[Index]);

				*(pBuf + Offset) = (char)Len;
				SK_MEMCPY(pBuf + Offset + 1, KeyArr[Index],
					Len);
				Offset += Len + 1;
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_VALUE:
			/* Check the buffer length if it is large enough */
			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				BufLen = 256;
				if (VpdRead(pAC, IoC, KeyArr[Index], Buf,
					(int *)&BufLen) > 0 ||
					BufLen >= SK_PNMI_VPD_DATALEN) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR021,
						SK_PNMI_ERR021MSG);

					return (SK_PNMI_ERR_GENERAL);
				}
				Offset += BufLen + 1;
			}
			if (*pLen < Offset) {

				*pLen = Offset;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			/*
			 * Get the value to an intermediate buffer, because
			 * we have to prepend a length byte.
			 */
			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				BufLen = 256;
				if (VpdRead(pAC, IoC, KeyArr[Index], Buf,
					(int *)&BufLen) > 0 ||
					BufLen >= SK_PNMI_VPD_DATALEN) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR022,
						SK_PNMI_ERR022MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}

				*(pBuf + Offset) = (char)BufLen;
				SK_MEMCPY(pBuf + Offset + 1, Buf, BufLen);
				Offset += BufLen + 1;
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_ACCESS:
			if (*pLen < LastIndex - FirstIndex) {

				*pLen = LastIndex - FirstIndex;
				return (SK_PNMI_ERR_TOO_SHORT);
			}

			for (Offset = 0, Index = FirstIndex;
				Index < LastIndex; Index ++) {

				if (VpdMayWrite(KeyArr[Index])) {

					*(pBuf + Offset) = SK_PNMI_VPD_RW;
				}
				else {
					*(pBuf + Offset) = SK_PNMI_VPD_RO;
				}
				Offset ++;
			}
			*pLen = Offset;
			break;

		case OID_SKGE_VPD_ACTION:
			Offset = LastIndex - FirstIndex;
			if (*pLen < Offset) {

				*pLen = Offset;
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			SK_MEMSET(pBuf, 0, Offset);
			*pLen = Offset;
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR023,
				SK_PNMI_ERR023MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
	} 
	else {
		/* The only OID which can be set is VPD_ACTION */
		if (Id != OID_SKGE_VPD_ACTION) {

			if (Id == OID_SKGE_VPD_FREE_BYTES ||
				Id == OID_SKGE_VPD_ENTRIES_LIST ||
				Id == OID_SKGE_VPD_ENTRIES_NUMBER ||
				Id == OID_SKGE_VPD_KEY ||
				Id == OID_SKGE_VPD_VALUE ||
				Id == OID_SKGE_VPD_ACCESS) {

				*pLen = 0;
				return (SK_PNMI_ERR_READ_ONLY);
			}

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR024,
				SK_PNMI_ERR024MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/*
		 * From this point we handle VPD_ACTION. Check the buffer
		 * length. It should at least have the size of one byte.
		 */
		if (*pLen < 1) {

			*pLen = 1;
			return (SK_PNMI_ERR_TOO_SHORT);
		}

		/*
		 * The first byte contains the VPD action type we should
		 * perform.
		 */
		switch (*pBuf) {

		case SK_PNMI_VPD_IGNORE:
			/* Nothing to do */
			break;

		case SK_PNMI_VPD_CREATE:
			/*
			 * We have to create a new VPD entry or we modify
			 * an existing one. Check first the buffer length.
			 */
			if (*pLen < 4) {

				*pLen = 4;
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			KeyStr[0] = pBuf[1];
			KeyStr[1] = pBuf[2];
			KeyStr[2] = 0;

			/*
			 * Is the entry writable or does it belong to the
			 * read-only area?
			 */
			if (!VpdMayWrite(KeyStr)) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			Offset = (int)pBuf[3] & 0xFF;

			SK_MEMCPY(Buf, pBuf + 4, Offset);
			Buf[Offset] = 0;

			/* A preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			/* Write the new entry or modify an existing one */
			Ret = VpdWrite(pAC, IoC, KeyStr, Buf);
			if (Ret == SK_PNMI_VPD_NOWRITE ) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			else if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR025,
					SK_PNMI_ERR025MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}

			/*
			 * Perform an update of the VPD data. This is
			 * not mandantory, but just to be sure.
			 */
			Ret = VpdUpdate(pAC, IoC);
			if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR026,
					SK_PNMI_ERR026MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		case SK_PNMI_VPD_DELETE:
			/* Check if the buffer size is plausible */
			if (*pLen < 3) {

				*pLen = 3;
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			if (*pLen > 3) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			KeyStr[0] = pBuf[1];
			KeyStr[1] = pBuf[2];
			KeyStr[2] = 0;

			/* Find the passed key in the array */
			for (Index = 0; Index < KeyNo; Index ++) {

				if (SK_STRCMP(KeyStr, KeyArr[Index]) == 0) {

					break;
				}
			}
			/*
			 * If we cannot find the key it is wrong, so we
			 * return an appropriate error value.
			 */
			if (Index == KeyNo) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			/* Ok, you wanted it and you will get it */
			Ret = VpdDelete(pAC, IoC, KeyStr);
			if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR027,
					SK_PNMI_ERR027MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}

			/*
			 * Perform an update of the VPD data. This is
			 * not mandantory, but just to be sure.
			 */
			Ret = VpdUpdate(pAC, IoC);
			if (Ret != SK_PNMI_VPD_OK) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR028,
					SK_PNMI_ERR028MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		default:
			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * General - OID handler function of various single instance OIDs
 *
 * Description:
 *	The code is simple. No description necessary.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int General(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	int		Ret;
	unsigned int	Index;
	unsigned int	Len;
	unsigned int	Offset;
	unsigned int	Val;
	SK_U8		Val8;
	SK_U16		Val16;
	SK_U32		Val32;
	SK_U64		Val64;
	SK_U64		Val64RxHwErrs = 0;
	SK_U64		Val64TxHwErrs = 0;
	SK_BOOL		Is64BitReq = SK_FALSE;
	char		Buf[256];


	/*
	 * Check instance. We only handle single instance variables
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	/*
	 * Check action. We only allow get requests.
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Check length for the various supported OIDs
	 */
	switch (Id) {

	case OID_GEN_XMIT_ERROR:
	case OID_GEN_RCV_ERROR:
	case OID_GEN_RCV_NO_BUFFER:
#ifndef SK_NDIS_64BIT_CTR
		if (*pLen < sizeof(SK_U32)) {
			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}

#else /* SK_NDIS_64BIT_CTR */
		
		/*
		 * for compatibility, at least 32bit are required for oid
		 */
		if (*pLen < sizeof(SK_U32)) {
			/*
			* but indicate handling for 64bit values,
			* if insufficient space is provided
			*/
			*pLen = sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		
		Is64BitReq = (*pLen < sizeof(SK_U64)) ? SK_FALSE : SK_TRUE;
#endif /* SK_NDIS_64BIT_CTR */
		break;

	case OID_SKGE_PORT_NUMBER:
	case OID_SKGE_DEVICE_TYPE:
	case OID_SKGE_RESULT:
	case OID_SKGE_RLMT_MONITOR_NUMBER:
	case OID_GEN_TRANSMIT_QUEUE_LENGTH:
	case OID_SKGE_TRAP_NUMBER:
	case OID_SKGE_MDB_VERSION:
		if (*pLen < sizeof(SK_U32)) {

			*pLen = sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_CHIPSET:
		if (*pLen < sizeof(SK_U16)) {

			*pLen = sizeof(SK_U16);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_BUS_TYPE:
	case OID_SKGE_BUS_SPEED:
	case OID_SKGE_BUS_WIDTH:
	case OID_SKGE_SENSOR_NUMBER:
	case OID_SKGE_CHKSM_NUMBER:
		if (*pLen < sizeof(SK_U8)) {

			*pLen = sizeof(SK_U8);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_TX_SW_QUEUE_LEN:
	case OID_SKGE_TX_SW_QUEUE_MAX:
	case OID_SKGE_TX_RETRY:
	case OID_SKGE_RX_INTR_CTS:
	case OID_SKGE_TX_INTR_CTS:
	case OID_SKGE_RX_NO_BUF_CTS:
	case OID_SKGE_TX_NO_BUF_CTS:
	case OID_SKGE_TX_USED_DESCR_NO:
	case OID_SKGE_RX_DELIVERED_CTS:
	case OID_SKGE_RX_OCTETS_DELIV_CTS:
	case OID_SKGE_RX_HW_ERROR_CTS:
	case OID_SKGE_TX_HW_ERROR_CTS:
	case OID_SKGE_IN_ERRORS_CTS:
	case OID_SKGE_OUT_ERROR_CTS:
	case OID_SKGE_ERR_RECOVERY_CTS:
	case OID_SKGE_SYSUPTIME:
		if (*pLen < sizeof(SK_U64)) {

			*pLen = sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
		/* Checked later */
		break;
	}

	/* Update statistic */
	if (Id == OID_SKGE_RX_HW_ERROR_CTS ||
		Id == OID_SKGE_TX_HW_ERROR_CTS ||
		Id == OID_SKGE_IN_ERRORS_CTS ||
		Id == OID_SKGE_OUT_ERROR_CTS ||
		Id == OID_GEN_XMIT_ERROR ||
		Id == OID_GEN_RCV_ERROR) {

		/* Force the XMAC to update its statistic counters and
		 * Increment semaphore to indicate that an update was
		 * already done.
		 */
		Ret = MacUpdate(pAC, IoC, 0, pAC->GIni.GIMacsFound - 1);
		if (Ret != SK_PNMI_ERR_OK) {

			*pLen = 0;
			return (Ret);
		}
		pAC->Pnmi.MacUpdatedFlag ++;

		/*
		 * Some OIDs consist of multiple hardware counters. Those
		 * values which are contained in all of them will be added
		 * now.
		 */
		switch (Id) {

		case OID_SKGE_RX_HW_ERROR_CTS:
		case OID_SKGE_IN_ERRORS_CTS:
		case OID_GEN_RCV_ERROR:
			Val64RxHwErrs =
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_MISSED) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_FRAMING) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_OVERFLOW)+
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_JABBER) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_CARRIER) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_IRLENGTH)+
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_SYMBOL) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_SHORTS) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_RUNT) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_TOO_LONG)-
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_LONGFRAMES)+
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_FCS) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HRX_CEXT);
			break;

		case OID_SKGE_TX_HW_ERROR_CTS:
		case OID_SKGE_OUT_ERROR_CTS:
		case OID_GEN_XMIT_ERROR:
			Val64TxHwErrs =
				GetStatVal(pAC, IoC, 0,
				SK_PNMI_HTX_EXCESS_COL) +
				GetStatVal(pAC, IoC, 0, SK_PNMI_HTX_LATE_COL)+
				GetStatVal(pAC, IoC, 0, SK_PNMI_HTX_UNDERRUN)+
				GetStatVal(pAC, IoC, 0, SK_PNMI_HTX_CARRIER)+
				GetStatVal(pAC, IoC, 0,
				SK_PNMI_HTX_EXCESS_COL);
			break;
		}
	}

	/*
	 * Retrieve value
	 */
	switch (Id) {

	case OID_SKGE_SUPPORTED_LIST:
		Len = sizeof(IdTable)/sizeof(IdTable[0]) * sizeof(SK_U32);
		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		for (Offset = 0, Index = 0; Offset < Len;
			Offset += sizeof(SK_U32), Index ++) {

			Val32 = (SK_U32)IdTable[Index].Id;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
		}
		*pLen = Len;
		break;

	case OID_SKGE_PORT_NUMBER:
		Val32 = (SK_U32)pAC->GIni.GIMacsFound;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_DEVICE_TYPE:
		Val32 = (SK_U32)pAC->Pnmi.DeviceType;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_DRIVER_DESCR:
		if (pAC->Pnmi.pDriverDescription == NULL) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR007,
				SK_PNMI_ERR007MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		Len = SK_STRLEN(pAC->Pnmi.pDriverDescription) + 1;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR029,
				SK_PNMI_ERR029MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, pAC->Pnmi.pDriverDescription, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_DRIVER_VERSION:
		if (pAC->Pnmi.pDriverVersion == NULL) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR030,
				SK_PNMI_ERR030MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		Len = SK_STRLEN(pAC->Pnmi.pDriverVersion) + 1;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR031,
				SK_PNMI_ERR031MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, pAC->Pnmi.pDriverVersion, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_HW_DESCR:
		/*
		 * The hardware description is located in the VPD. This
		 * query may move to the initialisation routine. But
		 * the VPD data is cached and therefore a call here
		 * will not make much difference.
		 */
		Len = 256;
		if (VpdRead(pAC, IoC, VPD_NAME, Buf, (int *)&Len) > 0) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR032,
				SK_PNMI_ERR032MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
		Len ++;
		if (Len > SK_PNMI_STRINGLEN1) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR033,
				SK_PNMI_ERR033MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		*pBuf = (char)(Len - 1);
		SK_MEMCPY(pBuf + 1, Buf, Len - 1);
		*pLen = Len;
		break;

	case OID_SKGE_HW_VERSION:
		/* Oh, I love to do some string manipulation */
		if (*pLen < 5) {

			*pLen = 5;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		Val8 = (SK_U8)pAC->GIni.GIPciHwRev;
		pBuf[0] = 4;
		pBuf[1] = 'v';
		pBuf[2] = (char)(0x30 | ((Val8 >> 4) & 0x0F));
		pBuf[3] = '.';
		pBuf[4] = (char)(0x30 | (Val8 & 0x0F));
		*pLen = 5;
		break;

	case OID_SKGE_CHIPSET:
		Val16 = SK_PNMI_CHIPSET;
		SK_PNMI_STORE_U16(pBuf, Val16);
		*pLen = sizeof(SK_U16);
		break;

	case OID_SKGE_BUS_TYPE:
		*pBuf = (char)SK_PNMI_BUS_PCI;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_BUS_SPEED:
		*pBuf = pAC->Pnmi.PciBusSpeed;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_BUS_WIDTH:
		*pBuf = pAC->Pnmi.PciBusWidth;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_RESULT:
		Val32 = pAC->Pnmi.TestResult;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_SENSOR_NUMBER:
		*pBuf = (char)pAC->I2c.MaxSens;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_CHKSM_NUMBER:
		*pBuf = SKCS_NUM_PROTOCOLS;
		*pLen = sizeof(char);
		break;

	case OID_SKGE_TRAP_NUMBER:
		GetTrapQueueLen(pAC, &Len, &Val);
		Val32 = (SK_U32)Val;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_TRAP:
		GetTrapQueueLen(pAC, &Len, &Val);
		if (*pLen < Len) {

			*pLen = Len;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		CopyTrapQueue(pAC, pBuf);
		*pLen = Len;
		break;

	case OID_SKGE_RLMT_MONITOR_NUMBER:
/* XXX Not yet implemented by RLMT therefore we return zero elements */
		Val32 = 0;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_SKGE_TX_SW_QUEUE_LEN:
		Val64 = pAC->Pnmi.TxSwQueueLen;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_SW_QUEUE_MAX:
		Val64 = pAC->Pnmi.TxSwQueueMax;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_RETRY:
		Val64 = pAC->Pnmi.TxRetryCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_INTR_CTS:
		Val64 = pAC->Pnmi.RxIntrCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_INTR_CTS:
		Val64 = pAC->Pnmi.TxIntrCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_NO_BUF_CTS:
		Val64 = pAC->Pnmi.RxNoBufCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_NO_BUF_CTS:
		Val64 = pAC->Pnmi.TxNoBufCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_USED_DESCR_NO:
		Val64 = pAC->Pnmi.TxUsedDescrNo;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_DELIVERED_CTS:
		Val64 = pAC->Pnmi.RxDeliveredCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_OCTETS_DELIV_CTS:
		Val64 = pAC->Pnmi.RxOctetsDeliveredCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_RX_HW_ERROR_CTS:
		SK_PNMI_STORE_U64(pBuf, Val64RxHwErrs);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_TX_HW_ERROR_CTS:
		SK_PNMI_STORE_U64(pBuf, Val64TxHwErrs);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_IN_ERRORS_CTS:
		Val64 = Val64RxHwErrs + pAC->Pnmi.RxNoBufCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_OUT_ERROR_CTS:
		Val64 = Val64TxHwErrs + pAC->Pnmi.TxNoBufCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_ERR_RECOVERY_CTS:
		Val64 = pAC->Pnmi.ErrRecoveryCts;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_SYSUPTIME:
		Val64 = SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));
		Val64 -= pAC->Pnmi.StartUpTime;
		SK_PNMI_STORE_U64(pBuf, Val64);
		*pLen = sizeof(SK_U64);
		break;

	case OID_SKGE_MDB_VERSION:
		Val32 = SK_PNMI_MDB_VERSION;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	case OID_GEN_RCV_ERROR:
		Val64 = Val64RxHwErrs + pAC->Pnmi.RxNoBufCts;

		/*
		 * by default 32bit values are evaluated
		 */
		if (!Is64BitReq) {
			SK_U32	Val32;
			Val32 = (SK_U32)Val64;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
		}
		break;

	case OID_GEN_XMIT_ERROR:
		Val64 = Val64TxHwErrs + pAC->Pnmi.TxNoBufCts;

		/*
		 * by default 32bit values are evaluated
		 */
		if (!Is64BitReq) {
			SK_U32	Val32;
			Val32 = (SK_U32)Val64;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
		}
		break;

	case OID_GEN_RCV_NO_BUFFER:
		Val64 = pAC->Pnmi.RxNoBufCts;

		/*
		 * by default 32bit values are evaluated
		 */
		if (!Is64BitReq) {
			SK_U32	Val32;
			Val32 = (SK_U32)Val64;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
		}
		else {
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
		}
		break;

	case OID_GEN_TRANSMIT_QUEUE_LENGTH:
		Val32 = (SK_U32)pAC->Pnmi.TxSwQueueLen;
		SK_PNMI_STORE_U32(pBuf, Val32);
		*pLen = sizeof(SK_U32);
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR034,
			SK_PNMI_ERR034MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);
	}

	if (Id == OID_SKGE_RX_HW_ERROR_CTS ||
		Id == OID_SKGE_TX_HW_ERROR_CTS ||
		Id == OID_SKGE_IN_ERRORS_CTS ||
		Id == OID_SKGE_OUT_ERROR_CTS ||
		Id == OID_GEN_XMIT_ERROR ||
		Id == OID_GEN_RCV_ERROR) {

		pAC->Pnmi.MacUpdatedFlag --;
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Rlmt - OID handler function of OID_SKGE_RLMT_XXX single instance.
 *
 * Description:
 *	Get/Presets/Sets the RLMT OIDs.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int Rlmt(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	int		Ret;
	unsigned int	PhysPortIndex;
	unsigned int	PhysPortMax;
	SK_EVPARA	EventParam;
	SK_U32		Val32;
	SK_U64		Val64;


	/*
	 * Check instance. Only single instance OIDs are allowed here.
	 */
	if (Instance != (SK_U32)(-1) && Instance != 1) {

		*pLen = 0;
		return (SK_PNMI_ERR_UNKNOWN_INST);
	}

	/*
	 * Perform the requested action
	 */
	if (Action == SK_PNMI_GET) {

		/*
		 * Check if the buffer length is large enough.
		 */

		switch (Id) {

		case OID_SKGE_RLMT_MODE:
		case OID_SKGE_RLMT_PORT_ACTIVE:
		case OID_SKGE_RLMT_PORT_PREFERRED:
			if (*pLen < sizeof(SK_U8)) {

				*pLen = sizeof(SK_U8);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		case OID_SKGE_RLMT_PORT_NUMBER:
			if (*pLen < sizeof(SK_U32)) {

				*pLen = sizeof(SK_U32);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		case OID_SKGE_RLMT_CHANGE_CTS:
		case OID_SKGE_RLMT_CHANGE_TIME:
		case OID_SKGE_RLMT_CHANGE_ESTIM:
		case OID_SKGE_RLMT_CHANGE_THRES:
			if (*pLen < sizeof(SK_U64)) {

				*pLen = sizeof(SK_U64);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR035,
				SK_PNMI_ERR035MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/*
		 * Update RLMT statistic and increment semaphores to indicate
		 * that an update was already done. Maybe RLMT will hold its
		 * statistic always up to date some time. Then we can
		 * remove this type of call.
		 */
		if ((Ret = RlmtUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

			*pLen = 0;
			return (Ret);
		}
		pAC->Pnmi.RlmtUpdatedFlag ++;

		/*
		 * Retrieve Value
		*/
		switch (Id) {

		case OID_SKGE_RLMT_MODE:
			*pBuf = (char)pAC->Rlmt.RlmtMode;
			*pLen = sizeof(char);
			break;

		case OID_SKGE_RLMT_PORT_NUMBER:
			Val32 = (SK_U32)pAC->GIni.GIMacsFound;
			SK_PNMI_STORE_U32(pBuf, Val32);
			*pLen = sizeof(SK_U32);
			break;

		case OID_SKGE_RLMT_PORT_ACTIVE:
			*pBuf = 0;
			/*
			 * If multiple ports may become active this OID
			 * doesn't make sense any more. A new variable in
			 * the port structure should be created. However,
			 * for this variable the first active port is
			 * returned.
			 */
			PhysPortMax = pAC->GIni.GIMacsFound;

			for (PhysPortIndex = 0; PhysPortIndex < PhysPortMax;
				PhysPortIndex ++) {

				if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

					*pBuf = (char)SK_PNMI_PORT_PHYS2LOG(
						PhysPortIndex);
					break;
				}
			}
			*pLen = sizeof(char);
			break;

		case OID_SKGE_RLMT_PORT_PREFERRED:
			*pBuf = (char)SK_PNMI_PORT_PHYS2LOG(
				pAC->Rlmt.MacPreferred);
			*pLen = sizeof(char);
			break;

		case OID_SKGE_RLMT_CHANGE_CTS:
			Val64 = pAC->Pnmi.RlmtChangeCts;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_CHANGE_TIME:
			Val64 = pAC->Pnmi.RlmtChangeTime;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_CHANGE_ESTIM:
			Val64 = pAC->Pnmi.RlmtChangeEstimate.Estimate;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_CHANGE_THRES:
			Val64 = pAC->Pnmi.RlmtChangeThreshold;
			SK_PNMI_STORE_U64(pBuf, Val64);
			*pLen = sizeof(SK_U64);
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR036,
				SK_PNMI_ERR036MSG);

			pAC->Pnmi.RlmtUpdatedFlag --;
			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		pAC->Pnmi.RlmtUpdatedFlag --;
	}
	else {
		/* Perform a preset or set */
		switch (Id) {

		case OID_SKGE_RLMT_MODE:
			/* Check if the buffer length is plausible */
			if (*pLen < sizeof(char)) {

				*pLen = sizeof(char);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/* Check if the value range is correct */
			if (*pLen != sizeof(char) ||
				(*pBuf & SK_PNMI_RLMT_MODE_CHK_LINK) == 0 ||
				*(SK_U8 *)pBuf > 15) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				*pLen = 0;
				return (SK_PNMI_ERR_OK);
			}
			/* Send an event to RLMT to change the mode */
			SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
			EventParam.Para32[0] |= (SK_U32)(*pBuf);
			if (SkRlmtEvent(pAC, IoC, SK_RLMT_MODE_CHANGE,
				EventParam) > 0) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR037,
					SK_PNMI_ERR037MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		case OID_SKGE_RLMT_PORT_PREFERRED:
			/* Check if the buffer length is plausible */
			if (*pLen < sizeof(char)) {

				*pLen = sizeof(char);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/* Check if the value range is correct */
			if (*pLen != sizeof(char) || *(SK_U8 *)pBuf >
				(SK_U8)pAC->GIni.GIMacsFound) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				*pLen = 0;
				return (SK_PNMI_ERR_OK);
			}

			/*
			 * Send an event to RLMT change the preferred port.
			 * A param of -1 means automatic mode. RLMT will
			 * make the decision which is the preferred port.
			 */
			SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
			EventParam.Para32[0] = (SK_U32)(*pBuf) - 1;
			if (SkRlmtEvent(pAC, IoC, SK_RLMT_PREFPORT_CHANGE,
				EventParam) > 0) {

				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR038,
					SK_PNMI_ERR038MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
			break;

		case OID_SKGE_RLMT_CHANGE_THRES:
			/* Check if the buffer length is plausible */
			if (*pLen < sizeof(SK_U64)) {

				*pLen = sizeof(SK_U64);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			/*
			 * There are not many restrictions to the
			 * value range.
			 */
			if (*pLen != sizeof(SK_U64)) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}
			/* A preset ends here */
			if (Action == SK_PNMI_PRESET) {

				*pLen = 0;
				return (SK_PNMI_ERR_OK);
			}
			/*
			 * Store the new threshold, which will be taken
			 * on the next timer event.
			 */
			SK_PNMI_READ_U64(pBuf, Val64);
			pAC->Pnmi.RlmtChangeThreshold = Val64;
			break;

		default:
			/* The other OIDs are not be able for set */
			*pLen = 0;
			return (SK_PNMI_ERR_READ_ONLY);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * RlmtStat - OID handler function of OID_SKGE_RLMT_XXX multiple instance.
 *
 * Description:
 *	Performs get requests on multiple instance variables.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int RlmtStat(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	unsigned int	PhysPortMax;
	unsigned int	PhysPortIndex;
	unsigned int	Limit;
	unsigned int	Offset;
	int		Ret;
	SK_U32		Val32;
	SK_U64		Val64;


	/*
	 * Calculate the port indexes from the instance
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;

	if ((Instance != (SK_U32)(-1))) {

		if ((Instance < 1) || (Instance > PhysPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		PhysPortIndex = Instance - 1;
		Limit = PhysPortIndex + 1;
	}
	else {
		PhysPortIndex = 0;
		Limit = PhysPortMax;
	}

	/*
	 * Currently only get requests are allowed.
	 */
	if (Action != SK_PNMI_GET) {

		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Check if the buffer length is large enough.
	 */
	switch (Id) {

	case OID_SKGE_RLMT_PORT_INDEX:
	case OID_SKGE_RLMT_STATUS:
		if (*pLen < (Limit - PhysPortIndex) * sizeof(SK_U32)) {

			*pLen = (Limit - PhysPortIndex) * sizeof(SK_U32);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	case OID_SKGE_RLMT_TX_HELLO_CTS:
	case OID_SKGE_RLMT_RX_HELLO_CTS:
	case OID_SKGE_RLMT_TX_SP_REQ_CTS:
	case OID_SKGE_RLMT_RX_SP_CTS:
		if (*pLen < (Limit - PhysPortIndex) * sizeof(SK_U64)) {

			*pLen = (Limit - PhysPortIndex) * sizeof(SK_U64);
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		break;

	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR039,
			SK_PNMI_ERR039MSG);

		*pLen = 0;
		return (SK_PNMI_ERR_GENERAL);

	}

	/*
	 * Update statistic and increment semaphores to indicate that
	 * an update was already done.
	 */
	if ((Ret = RlmtUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

		*pLen = 0;
		return (Ret);
	}
	pAC->Pnmi.RlmtUpdatedFlag ++;

	/*
	 * Get value
	 */
	Offset = 0;
	for (; PhysPortIndex < Limit; PhysPortIndex ++) {

		switch (Id) {

		case OID_SKGE_RLMT_PORT_INDEX:
			Val32 = PhysPortIndex;
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_RLMT_STATUS:
			if (pAC->Rlmt.Port[PhysPortIndex].PortState ==
				SK_RLMT_PS_INIT ||
				pAC->Rlmt.Port[PhysPortIndex].PortState ==
				SK_RLMT_PS_DOWN) {

				Val32 = SK_PNMI_RLMT_STATUS_ERROR;
			}
			else if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

				Val32 = SK_PNMI_RLMT_STATUS_ACTIVE;
			}
			else {
				Val32 = SK_PNMI_RLMT_STATUS_STANDBY;
			}
			SK_PNMI_STORE_U32(pBuf + Offset, Val32);
			Offset += sizeof(SK_U32);
			break;

		case OID_SKGE_RLMT_TX_HELLO_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].TxHelloCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_RX_HELLO_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].RxHelloCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_TX_SP_REQ_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].TxSpHelloReqCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		case OID_SKGE_RLMT_RX_SP_CTS:
			Val64 = pAC->Rlmt.Port[PhysPortIndex].RxSpHelloCts;
			SK_PNMI_STORE_U64(pBuf + Offset, Val64);
			Offset += sizeof(SK_U64);
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR040,
				SK_PNMI_ERR040MSG);

			pAC->Pnmi.RlmtUpdatedFlag --;
			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
	}
	*pLen = Offset;

	pAC->Pnmi.RlmtUpdatedFlag --;

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * MacPrivateConf - OID handler function of OIDs concerning the configuration
 *
 * Description:
 *	Get/Presets/Sets the OIDs concerning the configuration.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int MacPrivateConf(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	unsigned int	PhysPortMax;
	unsigned int	PhysPortIndex;
	unsigned int	LogPortMax;
	unsigned int	LogPortIndex;
	unsigned int	Limit;
	unsigned int	Offset;
	char		Val8;
	int		Ret;
	SK_EVPARA	EventParam;


	/*
	 * Calculate instance if wished. MAC index 0 is the virtual
	 * MAC.
	 */
	PhysPortMax = pAC->GIni.GIMacsFound;
	LogPortMax = SK_PNMI_PORT_PHYS2LOG(PhysPortMax);

	if ((Instance != (SK_U32)(-1))) {

		if ((Instance < 1) || (Instance > LogPortMax)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		LogPortIndex = SK_PNMI_PORT_INST2LOG(Instance);
		Limit = LogPortIndex + 1;
	}
	else {
		LogPortIndex = 0;
		Limit = LogPortMax;
	}

	/*
	 * Perform action
	 */
	if (Action == SK_PNMI_GET) {

		/*
		 * Check length
		 */
		switch (Id) {

		case OID_SKGE_PMD:
		case OID_SKGE_CONNECTOR:
		case OID_SKGE_LINK_CAP:
		case OID_SKGE_LINK_MODE:
		case OID_SKGE_LINK_MODE_STATUS:
		case OID_SKGE_LINK_STATUS:
		case OID_SKGE_FLOWCTRL_CAP:
		case OID_SKGE_FLOWCTRL_MODE:
		case OID_SKGE_FLOWCTRL_STATUS:
		case OID_SKGE_PHY_OPERATION_CAP:
		case OID_SKGE_PHY_OPERATION_MODE:
		case OID_SKGE_PHY_OPERATION_STATUS:
			if (*pLen < (Limit - LogPortIndex) * sizeof(SK_U8)) {

				*pLen = (Limit - LogPortIndex) *
					sizeof(SK_U8);
				return (SK_PNMI_ERR_TOO_SHORT);
			}
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR041,
				SK_PNMI_ERR041MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}

		/*
		 * Update statistic and increment semaphore to indicate
		 * that an update was already done.
		 */
		if ((Ret = SirqUpdate(pAC, IoC)) != SK_PNMI_ERR_OK) {

			*pLen = 0;
			return (Ret);
		}
		pAC->Pnmi.SirqUpdatedFlag ++;

		/*
		 * Get value
		 */
		Offset = 0;
		for (; LogPortIndex < Limit; LogPortIndex ++) {

			switch (Id) {

			case OID_SKGE_PMD:
				*(pBuf + Offset) = pAC->Pnmi.PMD;
				Offset += sizeof(char);
				break;

			case OID_SKGE_CONNECTOR:
				*(pBuf + Offset) = pAC->Pnmi.Connector;
				Offset += sizeof(char);
				break;

			case OID_SKGE_LINK_CAP:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical ports */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PLinkCap;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_LINK_MODE:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical ports */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PLinkModeConf;
				}

				Offset += sizeof(char);
				break;

			case OID_SKGE_LINK_MODE_STATUS:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical port */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) =
						CalculateLinkModeStatus(pAC,
							IoC, PhysPortIndex);
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_LINK_STATUS:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical ports */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) =
						CalculateLinkStatus(pAC,
							IoC, PhysPortIndex);
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_FLOWCTRL_CAP:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical ports */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PFlowCtrlCap;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_FLOWCTRL_MODE:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical port */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PFlowCtrlMode;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_FLOWCTRL_STATUS:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical port */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PFlowCtrlStatus;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_PHY_OPERATION_CAP:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical ports */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PMSCap;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_PHY_OPERATION_MODE:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical port */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PMSMode;
				}
				Offset += sizeof(char);
				break;

			case OID_SKGE_PHY_OPERATION_STATUS:
				if (LogPortIndex == 0) {

					/* Get value for virtual port */
					VirtualConf(pAC, IoC, Id, pBuf +
						Offset);
				}
				else {
					/* Get value for physical port */
					PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(
						pAC, LogPortIndex);

					*(pBuf + Offset) = pAC->GIni.GP[
						PhysPortIndex].PMSStatus;
				}
				Offset += sizeof(char);
				break;

			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR042,
					SK_PNMI_ERR042MSG);

				pAC->Pnmi.SirqUpdatedFlag --;
				return (SK_PNMI_ERR_GENERAL);
			}
		}
		*pLen = Offset;
		pAC->Pnmi.SirqUpdatedFlag --;

		return (SK_PNMI_ERR_OK);
	}

	/*
	 * From here SET or PRESET action. Check if the passed
	 * buffer length is plausible.
	 */
	switch (Id) {

	case OID_SKGE_LINK_MODE:
	case OID_SKGE_FLOWCTRL_MODE:
	case OID_SKGE_PHY_OPERATION_MODE:
		if (*pLen < Limit - LogPortIndex) {

			*pLen = Limit - LogPortIndex;
			return (SK_PNMI_ERR_TOO_SHORT);
		}
		if (*pLen != Limit - LogPortIndex) {

			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}
		break;

	default:
		*pLen = 0;
		return (SK_PNMI_ERR_READ_ONLY);
	}

	/*
	 * Perform preset or set
	 */
	Offset = 0;
	for (; LogPortIndex < Limit; LogPortIndex ++) {

		switch (Id) {

		case OID_SKGE_LINK_MODE:
			/* Check the value range */
			Val8 = *(pBuf + Offset);
			if (Val8 == 0) {

				Offset += sizeof(char);
				break;
			}
			if (Val8 < SK_LMODE_HALF ||
				Val8 > SK_LMODE_AUTOSENSE) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (LogPortIndex == 0) {

				/*
				 * The virtual port consists of all currently
				 * active ports. Find them and send an event
				 * with the new link mode to SIRQ.
				 */
				for (PhysPortIndex = 0;
					PhysPortIndex < PhysPortMax;
					PhysPortIndex ++) {

					if (!pAC->Pnmi.Port[PhysPortIndex].
						ActiveFlag) {

						continue;
					}

					EventParam.Para32[0] = PhysPortIndex;
					EventParam.Para32[1] = (SK_U32)Val8;
					if (SkGeSirqEvent(pAC, IoC,
						SK_HWEV_SET_LMODE,
						EventParam) > 0) {

						SK_ERR_LOG(pAC, SK_ERRCL_SW,
							SK_PNMI_ERR043,
							SK_PNMI_ERR043MSG);

						*pLen = 0;
						return (SK_PNMI_ERR_GENERAL);
					}
				}
			}
			else {
				/*
				 * Send an event with the new link mode to
				 * the SIRQ module.
				 */
				EventParam.Para32[0] = SK_PNMI_PORT_LOG2PHYS(
					pAC, LogPortIndex);
				EventParam.Para32[1] = (SK_U32)Val8;
				if (SkGeSirqEvent(pAC, IoC, SK_HWEV_SET_LMODE,
					EventParam) > 0) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR043,
						SK_PNMI_ERR043MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}
			}
			Offset += sizeof(char);
			break;

		case OID_SKGE_FLOWCTRL_MODE:
			/* Check the value range */
			Val8 = *(pBuf + Offset);
			if (Val8 == 0) {

				Offset += sizeof(char);
				break;
			}
			if (Val8 < SK_FLOW_MODE_NONE ||
				Val8 > SK_FLOW_MODE_SYM_OR_REM) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (LogPortIndex == 0) {

				/*
				 * The virtual port consists of all currently
				 * active ports. Find them and send an event
				 * with the new flow control mode to SIRQ.
				 */
				for (PhysPortIndex = 0;
					PhysPortIndex < PhysPortMax;
					PhysPortIndex ++) {

					if (!pAC->Pnmi.Port[PhysPortIndex].
						ActiveFlag) {

						continue;
					}

					EventParam.Para32[0] = PhysPortIndex;
					EventParam.Para32[1] = (SK_U32)Val8;
					if (SkGeSirqEvent(pAC, IoC,
						SK_HWEV_SET_FLOWMODE,
						EventParam) > 0) {

						SK_ERR_LOG(pAC, SK_ERRCL_SW,
							SK_PNMI_ERR044,
							SK_PNMI_ERR044MSG);

						*pLen = 0;
						return (SK_PNMI_ERR_GENERAL);
					}
				}
			}
			else {
				/*
				 * Send an event with the new flow control
				 * mode to the SIRQ module.
				 */
				EventParam.Para32[0] = SK_PNMI_PORT_LOG2PHYS(
					pAC, LogPortIndex);
				EventParam.Para32[1] = (SK_U32)Val8;
				if (SkGeSirqEvent(pAC, IoC,
					SK_HWEV_SET_FLOWMODE, EventParam)
					> 0) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR044,
						SK_PNMI_ERR044MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}
			}
			Offset += sizeof(char);
			break;

		case OID_SKGE_PHY_OPERATION_MODE :
			/* Check the value range */
			Val8 = *(pBuf + Offset);
			if (Val8 == 0) {
				/* mode of this port remains unchanged */
				Offset += sizeof(char);
				break;
			}
			if (Val8 < SK_MS_MODE_AUTO ||
				Val8 > SK_MS_MODE_SLAVE) {

				*pLen = 0;
				return (SK_PNMI_ERR_BAD_VALUE);
			}

			/* The preset ends here */
			if (Action == SK_PNMI_PRESET) {

				return (SK_PNMI_ERR_OK);
			}

			if (LogPortIndex == 0) {

				/*
				 * The virtual port consists of all currently
				 * active ports. Find them and send an event
				 * with new master/slave (role) mode to SIRQ.
				 */
				for (PhysPortIndex = 0;
					PhysPortIndex < PhysPortMax;
					PhysPortIndex ++) {

					if (!pAC->Pnmi.Port[PhysPortIndex].
						ActiveFlag) {

						continue;
					}

					EventParam.Para32[0] = PhysPortIndex;
					EventParam.Para32[1] = (SK_U32)Val8;
					if (SkGeSirqEvent(pAC, IoC,
						SK_HWEV_SET_ROLE,
						EventParam) > 0) {

						SK_ERR_LOG(pAC, SK_ERRCL_SW,
							SK_PNMI_ERR052,
							SK_PNMI_ERR052MSG);

						*pLen = 0;
						return (SK_PNMI_ERR_GENERAL);
					}
				}
			}
			else {
				/*
				 * Send an event with the new master/slave
				 * (role) mode to the SIRQ module.
				 */
				EventParam.Para32[0] = SK_PNMI_PORT_LOG2PHYS(
					pAC, LogPortIndex);
				EventParam.Para32[1] = (SK_U32)Val8;
				if (SkGeSirqEvent(pAC, IoC,
					SK_HWEV_SET_ROLE, EventParam) > 0) {

					SK_ERR_LOG(pAC, SK_ERRCL_SW,
						SK_PNMI_ERR052,
						SK_PNMI_ERR052MSG);

					*pLen = 0;
					return (SK_PNMI_ERR_GENERAL);
				}
			}
			
			Offset += sizeof(char);
			break;

		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR045,
				SK_PNMI_ERR045MSG);

			*pLen = 0;
			return (SK_PNMI_ERR_GENERAL);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * Monitor - OID handler function for RLMT_MONITOR_XXX
 *
 * Description:
 *	Because RLMT currently does not support the monitoring of
 *	remote adapter cards, we return always an empty table.
 *
 * Returns:
 *	SK_PNMI_ERR_OK           The request was successfully performed.
 *	SK_PNMI_ERR_GENERAL      A general severe internal error occured.
 *	SK_PNMI_ERR_TOO_SHORT    The passed buffer is too short to contain
 *	                         the correct data (e.g. a 32bit value is
 *	                         needed, but a 16 bit value was passed).
 *	SK_PNMI_ERR_BAD_VALUE    The passed value is not in the valid
 *	                         value range.
 *	SK_PNMI_ERR_READ_ONLY    The OID is read-only and cannot be set.
 *	SK_PNMI_ERR_UNKNOWN_INST The requested instance of the OID doesn't
 *                               exist (e.g. port instance 3 on a two port
 *	                         adapter.
 */

static int Monitor(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
int Action,		/* Get/PreSet/Set action */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf,		/* Buffer to which to mgmt data will be retrieved */
unsigned int *pLen,	/* On call: buffer length. On return: used buffer */
SK_U32 Instance,	/* Instance (1..n) that is to be queried or -1 */
unsigned int TableIndex) /* Index to the Id table */
{
	unsigned int	Index;
	unsigned int	Limit;
	unsigned int	Offset;
	unsigned int	Entries;

	
	/*
	 * Calculate instance if wished.
	 */
/* XXX Not yet implemented. Return always an empty table. */
	Entries = 0;

	if ((Instance != (SK_U32)(-1))) {

		if ((Instance < 1) || (Instance > Entries)) {

			*pLen = 0;
			return (SK_PNMI_ERR_UNKNOWN_INST);
		}

		Index = (unsigned int)Instance - 1;
		Limit = (unsigned int)Instance;
	}
	else {
		Index = 0;
		Limit = Entries;
	}

	/*
	 * Get/Set value
	*/
	if (Action == SK_PNMI_GET) {

		for (Offset=0; Index < Limit; Index ++) {

			switch (Id) {

			case OID_SKGE_RLMT_MONITOR_INDEX:
			case OID_SKGE_RLMT_MONITOR_ADDR:
			case OID_SKGE_RLMT_MONITOR_ERRS:
			case OID_SKGE_RLMT_MONITOR_TIMESTAMP:
			case OID_SKGE_RLMT_MONITOR_ADMIN:
				break;

			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR046,
					SK_PNMI_ERR046MSG);

				*pLen = 0;
				return (SK_PNMI_ERR_GENERAL);
			}
		}
		*pLen = Offset;
	}
	else {
		/* Only MONITOR_ADMIN can be set */
		if (Id != OID_SKGE_RLMT_MONITOR_ADMIN) {

			*pLen = 0;
			return (SK_PNMI_ERR_READ_ONLY);
		}

		/* Check if the length is plausible */
		if (*pLen < (Limit - Index)) {

			return (SK_PNMI_ERR_TOO_SHORT);
		}
		/* Okay, we have a wide value range */
		if (*pLen != (Limit - Index)) {

			*pLen = 0;
			return (SK_PNMI_ERR_BAD_VALUE);
		}
/*
		for (Offset=0; Index < Limit; Index ++) {
		}
*/
/*
 * XXX Not yet implemented. Return always BAD_VALUE, because the table
 * is empty.
 */
		*pLen = 0;
		return (SK_PNMI_ERR_BAD_VALUE);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * VirtualConf - Calculates the values of configuration OIDs for virtual port
 *
 * Description:
 *	We handle here the get of the configuration group OIDs, which are
 *	a little bit complicated. The virtual port consists of all currently
 *	active physical ports. If multiple ports are active and configured
 *	differently we get in some trouble to return a single value. So we
 *	get the value of the first active port and compare it with that of
 *	the other active ports. If they are not the same, we return a value
 *	that indicates that the state is indeterminated.
 *
 * Returns:
 *	Nothing
 */

static void VirtualConf(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
SK_U32 Id,		/* Object ID that is to be processed */
char *pBuf)		/* Buffer to which to mgmt data will be retrieved */
{
	unsigned int	PhysPortMax;
	unsigned int	PhysPortIndex;
	SK_U8		Val8;
	SK_BOOL		PortActiveFlag;


	*pBuf = 0;
	PortActiveFlag = SK_FALSE;
	PhysPortMax = pAC->GIni.GIMacsFound;

	for (PhysPortIndex = 0; PhysPortIndex < PhysPortMax;
		PhysPortIndex ++) {

		/* Check if the physical port is active */
		if (!pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

			continue;
		}

		PortActiveFlag = SK_TRUE;

		switch (Id) {

		case OID_SKGE_LINK_CAP:

			/*
			 * Different capabilities should not happen, but
			 * in the case of the cases OR them all together.
			 * From a curious point of view the virtual port
			 * is capable of all found capabilities.
			 */
			*pBuf |= pAC->GIni.GP[PhysPortIndex].PLinkCap;
			break;

		case OID_SKGE_LINK_MODE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pAC->GIni.GP[PhysPortIndex].
					PLinkModeConf;
				continue;
			}

			/*
			 * If we find an active port with a different link
			 * mode than the first one we return a value that
			 * indicates that the link mode is indeterminated.
			 */
			if (*pBuf != pAC->GIni.GP[PhysPortIndex].PLinkModeConf
				) {

				*pBuf = SK_LMODE_INDETERMINATED;
			}
			break;

		case OID_SKGE_LINK_MODE_STATUS:
			/* Get the link mode of the physical port */
			Val8 = CalculateLinkModeStatus(pAC, IoC,
				PhysPortIndex);

			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = Val8;
				continue;
			}

			/*
			 * If we find an active port with a different link
			 * mode status than the first one we return a value
			 * that indicates that the link mode status is
			 * indeterminated.
			 */
			if (*pBuf != Val8) {

				*pBuf = SK_LMODE_STAT_INDETERMINATED;
			}
			break;

		case OID_SKGE_LINK_STATUS:
			/* Get the link status of the physical port */
			Val8 = CalculateLinkStatus(pAC, IoC, PhysPortIndex);

			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = Val8;
				continue;
			}

			/*
			 * If we find an active port with a different link
			 * status than the first one, we return a value
			 * that indicates that the link status is
			 * indeterminated.
			 */
			if (*pBuf != Val8) {

				*pBuf = SK_PNMI_RLMT_LSTAT_INDETERMINATED;
			}
			break;

		case OID_SKGE_FLOWCTRL_CAP:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pAC->GIni.GP[PhysPortIndex].
					PFlowCtrlCap;
				continue;
			}

			/*
			 * From a curious point of view the virtual port
			 * is capable of all found capabilities.
			 */
			*pBuf |= pAC->GIni.GP[PhysPortIndex].PFlowCtrlCap;
			break;

		case OID_SKGE_FLOWCTRL_MODE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pAC->GIni.GP[PhysPortIndex].
					PFlowCtrlMode;
				continue;
			}

			/*
			 * If we find an active port with a different flow
			 * control mode than the first one, we return a value
			 * that indicates that the mode is indeterminated.
			 */
			if (*pBuf != pAC->GIni.GP[PhysPortIndex].
				PFlowCtrlMode) {

				*pBuf = SK_FLOW_MODE_INDETERMINATED;
			}
			break;

		case OID_SKGE_FLOWCTRL_STATUS:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pAC->GIni.GP[PhysPortIndex].
					PFlowCtrlStatus;
				continue;
			}

			/*
			 * If we find an active port with a different flow
			 * control status than the first one, we return a
			 * value that indicates that the status is
			 * indeterminated.
			 */
			if (*pBuf != pAC->GIni.GP[PhysPortIndex].
				PFlowCtrlStatus) {

				*pBuf = SK_FLOW_STAT_INDETERMINATED;
			}
			break;
		case OID_SKGE_PHY_OPERATION_CAP:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pAC->GIni.GP[PhysPortIndex].
					PMSCap;
				continue;
			}

			/*
			 * From a curious point of view the virtual port
			 * is capable of all found capabilities.
			 */
			*pBuf |= pAC->GIni.GP[PhysPortIndex].PMSCap;
			break;

		case OID_SKGE_PHY_OPERATION_MODE:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pAC->GIni.GP[PhysPortIndex].
					PMSMode;
				continue;
			}

			/*
			 * If we find an active port with a different master/
			 * slave mode than the first one, we return a value
			 * that indicates that the mode is indeterminated.
			 */
			if (*pBuf != pAC->GIni.GP[PhysPortIndex].
				PMSMode) {

				*pBuf = SK_MS_MODE_INDETERMINATED;
			}
			break;

		case OID_SKGE_PHY_OPERATION_STATUS:
			/* Check if it is the first active port */
			if (*pBuf == 0) {

				*pBuf = pAC->GIni.GP[PhysPortIndex].
					PMSStatus;
				continue;
			}

			/*
			 * If we find an active port with a different master/
			 * slave status than the first one, we return a
			 * value that indicates that the status is
			 * indeterminated.
			 */
			if (*pBuf != pAC->GIni.GP[PhysPortIndex].
				PMSStatus) {

				*pBuf = SK_MS_STAT_INDETERMINATED;
			}
			break;
		}
	}

	/*
	 * If no port is active return an indeterminated answer
	 */
	if (!PortActiveFlag) {

		switch (Id) {

		case OID_SKGE_LINK_CAP:
			*pBuf = SK_LMODE_CAP_INDETERMINATED;
			break;

		case OID_SKGE_LINK_MODE:
			*pBuf = SK_LMODE_INDETERMINATED;
			break;

		case OID_SKGE_LINK_MODE_STATUS:
			*pBuf = SK_LMODE_STAT_INDETERMINATED;
			break;

		case OID_SKGE_LINK_STATUS:
			*pBuf = SK_PNMI_RLMT_LSTAT_INDETERMINATED;
			break;

		case OID_SKGE_FLOWCTRL_CAP:
		case OID_SKGE_FLOWCTRL_MODE:
			*pBuf = SK_FLOW_MODE_INDETERMINATED;
			break;

		case OID_SKGE_FLOWCTRL_STATUS:
			*pBuf = SK_FLOW_STAT_INDETERMINATED;
			break;
			
		case OID_SKGE_PHY_OPERATION_CAP:
			*pBuf = SK_MS_CAP_INDETERMINATED;
			break;

		case OID_SKGE_PHY_OPERATION_MODE:
			*pBuf = SK_MS_MODE_INDETERMINATED;
			break;

		case OID_SKGE_PHY_OPERATION_STATUS:
			*pBuf = SK_MS_STAT_INDETERMINATED;
			break;
		}
	}
}

/*****************************************************************************
 *
 * CalculateLinkStatus - Determins the link status of a physical port
 *
 * Description:
 *	Determins the link status the following way:
 *	  LSTAT_PHY_DOWN:  Link is down
 *	  LSTAT_AUTONEG:   Auto-negotiation failed
 *	  LSTAT_LOG_DOWN:  Link is up but RLMT did not yet put the port
 *	                   logically up.
 *	  LSTAT_LOG_UP:    RLMT marked the port as up
 *
 * Returns:
 *	Link status of physical port
 */

static SK_U8 CalculateLinkStatus(
SK_AC *pAC,			/* Pointer to adapter context */
SK_IOC IoC,			/* IO context handle */
unsigned int PhysPortIndex)	/* Physical port index */
{
	SK_U8	Result;


	if (!pAC->GIni.GP[PhysPortIndex].PHWLinkUp) {

		Result = SK_PNMI_RLMT_LSTAT_PHY_DOWN;
	}
	else if (pAC->GIni.GP[PhysPortIndex].PAutoNegFail > 0) {

		Result = SK_PNMI_RLMT_LSTAT_AUTONEG;
				}
	else if (!pAC->Rlmt.Port[PhysPortIndex].PortDown) {

		Result = SK_PNMI_RLMT_LSTAT_LOG_UP;
	}
	else {
		Result = SK_PNMI_RLMT_LSTAT_LOG_DOWN;
	}

	return (Result);
}

/*****************************************************************************
 *
 * CalculateLinkModeStatus - Determins the link mode status of a phys. port
 *
 * Description:
 *	The COMMON module only tells us if the mode is half or full duplex.
 *	But in the decade of auto sensing it is usefull for the user to
 *	know if the mode was negotiated or forced. Therefore we have a
 *	look to the mode, which was last used by the negotiation process.
 *
 * Returns:
 *	The link mode status
 */

static SK_U8 CalculateLinkModeStatus(
SK_AC *pAC,			/* Pointer to adapter context */
SK_IOC IoC,			/* IO context handle */
unsigned int PhysPortIndex)	/* Physical port index */
{
	SK_U8	Result;


	/* Get the current mode, which can be full or half duplex */
	Result = pAC->GIni.GP[PhysPortIndex].PLinkModeStatus;

	/* Check if no valid mode could be found (link is down) */
	if (Result < SK_LMODE_STAT_HALF) {

		Result = SK_LMODE_STAT_UNKNOWN;
	} 
	else if (pAC->GIni.GP[PhysPortIndex].PLinkMode >= SK_LMODE_AUTOHALF) {

		/*
		 * Auto-negotiation was used to bring up the link. Change
		 * the already found duplex status that it indicates
		 * auto-negotiation was involved.
		 */
		if (Result == SK_LMODE_STAT_HALF) {

			Result = SK_LMODE_STAT_AUTOHALF;
		}
		else if (Result == SK_LMODE_STAT_FULL) {

			Result = SK_LMODE_STAT_AUTOFULL;
		}
	}

	return (Result);
}

/*****************************************************************************
 *
 * GetVpdKeyArr - Obtain an array of VPD keys
 *
 * Description:
 *	Read the VPD keys and build an array of VPD keys, which are
 *	easy to access.
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */

static int GetVpdKeyArr(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
char *pKeyArr,		/* Ptr KeyArray */
unsigned int KeyArrLen,	/* Length of array in bytes */
unsigned int *pKeyNo)	/* Number of keys */
{
	unsigned int		BufKeysLen = 128;
	char			BufKeys[128];
	unsigned int		StartOffset;
	unsigned int		Offset;
	int			Index;
	int			Ret;


	SK_MEMSET(pKeyArr, 0, KeyArrLen);

	/*
	 * Get VPD key list
	 */
	Ret = VpdKeys(pAC, IoC, (char *)&BufKeys, (int *)&BufKeysLen,
		(int *)pKeyNo);
	if (Ret > 0) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR014,
			SK_PNMI_ERR014MSG);

		return (SK_PNMI_ERR_GENERAL);
	}
	/* If no keys are available return now */
	if (*pKeyNo == 0 || BufKeysLen == 0) {

		return (SK_PNMI_ERR_OK);
	}
	/*
	 * If the key list is too long for us trunc it and give a
	 * errorlog notification. This case should not happen because
	 * the maximum number of keys is limited due to RAM limitations
	 */
	if (*pKeyNo > SK_PNMI_VPD_ARR_SIZE) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR015,
			SK_PNMI_ERR015MSG);

		*pKeyNo = SK_PNMI_VPD_ARR_SIZE;
	}

	/*
	 * Now build an array of fixed string length size and copy
	 * the keys together.
	 */
	for (Index = 0, StartOffset = 0, Offset = 0; Offset < BufKeysLen;
		Offset ++) {

		if (BufKeys[Offset] != 0) {

			continue;
		}

		if (Offset - StartOffset > SK_PNMI_VPD_STR_SIZE) {

			SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR016,
				SK_PNMI_ERR016MSG);
			return (SK_PNMI_ERR_GENERAL);
		}

		SK_STRNCPY(pKeyArr + Index * SK_PNMI_VPD_STR_SIZE,
			&BufKeys[StartOffset], SK_PNMI_VPD_STR_SIZE);

		Index ++;
		StartOffset = Offset + 1;
	}

	/* Last key not zero terminated? Get it anyway */
	if (StartOffset < Offset) {

		SK_STRNCPY(pKeyArr + Index * SK_PNMI_VPD_STR_SIZE,
			&BufKeys[StartOffset], SK_PNMI_VPD_STR_SIZE);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * SirqUpdate - Let the SIRQ update its internal values
 *
 * Description:
 *	Just to be sure that the SIRQ module holds its internal data
 *	structures up to date, we send an update event before we make
 *	any access.
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */

static int SirqUpdate(
SK_AC *pAC,	/* Pointer to adapter context */
SK_IOC IoC)	/* IO context handle */
{
	SK_EVPARA	EventParam;


	/* Was the module already updated during the current PNMI call? */
	if (pAC->Pnmi.SirqUpdatedFlag > 0) {

		return (SK_PNMI_ERR_OK);
	}

	/* Send an synchronuous update event to the module */
	SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
	if (SkGeSirqEvent(pAC, IoC, SK_HWEV_UPDATE_STAT, EventParam) > 0) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR047,
			SK_PNMI_ERR047MSG);

		return (SK_PNMI_ERR_GENERAL);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * RlmtUpdate - Let the RLMT update its internal values
 *
 * Description:
 *	Just to be sure that the RLMT module holds its internal data
 *	structures up to date, we send an update event before we make
 *	any access.
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */

static int RlmtUpdate(
SK_AC *pAC,	/* Pointer to adapter context */
SK_IOC IoC)	/* IO context handle */
{
	SK_EVPARA	EventParam;


	/* Was the module already updated during the current PNMI call? */
	if (pAC->Pnmi.RlmtUpdatedFlag > 0) {

		return (SK_PNMI_ERR_OK);
	}

	/* Send an synchronuous update event to the module */
	SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));
	if (SkRlmtEvent(pAC, IoC, SK_RLMT_STATS_UPDATE, EventParam) > 0) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SK_PNMI_ERR048,
			SK_PNMI_ERR048MSG);

		return (SK_PNMI_ERR_GENERAL);
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * MacUpdate - Force the XMAC to output the current statistic
 *
 * Description:
 *	The XMAC holds its statistic internally. To obtain the current
 *	values we must send a command so that the statistic data will
 *	be written to a predefined memory area on the adapter. 
 *
 * Returns:
 *	SK_PNMI_ERR_OK	     Task successfully performed.
 *	SK_PNMI_ERR_GENERAL  Something went wrong.
 */

static int MacUpdate(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC,		/* IO context handle */
unsigned int FirstMac,	/* Index of the first Mac to be updated */
unsigned int LastMac)	/* Index of the last Mac to be updated */
{
	unsigned int	MacIndex;
	SK_U16		StatReg;
	unsigned int	WaitIndex;


	/*
	 * Were the statistics already updated during the
	 * current PNMI call?
	 */
	if (pAC->Pnmi.MacUpdatedFlag > 0) {

		return (SK_PNMI_ERR_OK);
	}

	/* Send an update command to all XMACs specified */
	for (MacIndex = FirstMac; MacIndex <= LastMac; MacIndex ++) {

		StatReg = XM_SC_SNP_TXC | XM_SC_SNP_RXC;
		XM_OUT16(IoC, MacIndex, XM_STAT_CMD, StatReg);

		/*
		 * It is an auto-clearing register. If the command bits
		 * went to zero again, the statistics are transfered.
		 * Normally the command should be executed immediately.
		 * But just to be sure we execute a loop.
		 */
		for (WaitIndex = 0; WaitIndex < 10; WaitIndex ++) {

			XM_IN16(IoC, MacIndex, XM_STAT_CMD, &StatReg);
			if ((StatReg & (XM_SC_SNP_TXC | XM_SC_SNP_RXC)) ==
				0) {

				break;
			}
		}
		if (WaitIndex == 10 ) {

			SK_ERR_LOG(pAC, SK_ERRCL_HW, SK_PNMI_ERR050,
				SK_PNMI_ERR050MSG);

			return (SK_PNMI_ERR_GENERAL);
		}
	}

	return (SK_PNMI_ERR_OK);
}

/*****************************************************************************
 *
 * GetStatVal - Retrieve an XMAC statistic counter
 *
 * Description:
 *	Retrieves the statistic counter of a virtual or physical port. The
 *	virtual port is identified by the index 0. It consists of all
 *	currently active ports. To obtain the counter value for this port
 *	we must add the statistic counter of all active ports. To grant
 *	continuous counter values for the virtual port even when port
 *	switches occur we must additionally add a delta value, which was
 *	calculated during a SK_PNMI_EVT_RLMT_ACTIVE_UP event.
 *
 * Returns:
 *	Requested statistic value
 */

static SK_U64 GetStatVal(
SK_AC *pAC,			/* Pointer to adapter context */
SK_IOC IoC,			/* IO context handle */
unsigned int LogPortIndex,	/* Index of the logical Port to be processed */
unsigned int StatIndex)		/* Index to statistic value */
{
	unsigned int	PhysPortIndex;
	unsigned int	PhysPortMax;
	SK_U64		Val = 0;


	if (LogPortIndex == 0) {

		PhysPortMax = pAC->GIni.GIMacsFound;

		/* Add counter of all active ports */
		for (PhysPortIndex = 0; PhysPortIndex < PhysPortMax;
			PhysPortIndex ++) {

			if (pAC->Pnmi.Port[PhysPortIndex].ActiveFlag) {

				Val += GetPhysStatVal(pAC, IoC, PhysPortIndex,
					StatIndex);
			}
		}

		/* Correct value because of port switches */
		Val += pAC->Pnmi.VirtualCounterOffset[StatIndex];
	}
	else {
		/* Get counter value of physical port */
		PhysPortIndex = SK_PNMI_PORT_LOG2PHYS(pAC, LogPortIndex);
		Val = GetPhysStatVal(pAC, IoC, PhysPortIndex, StatIndex);
	}

	return (Val);
}

/*****************************************************************************
 *
 * GetPhysStatVal - Get counter value for physical port
 *
 * Description:
 *	Builds a 64bit counter value. Except for the octet counters
 *	the lower 32bit are counted in hardware and the upper 32bit
 *	in software by monitoring counter overflow interrupts in the
 *	event handler. To grant continous counter values during XMAC
 *	resets (caused by a workaround) we must add a delta value.
 *	The delta was calculated in the event handler when a
 *	SK_PNMI_EVT_XMAC_RESET was received.
 *
 * Returns:
 *	Counter value
 */

static SK_U64 GetPhysStatVal(
SK_AC *pAC,			/* Pointer to adapter context */
SK_IOC IoC,			/* IO context handle */
unsigned int PhysPortIndex,	/* Index of the logical Port to be processed */
unsigned int StatIndex)		/* Index to statistic value */
{
	SK_U64		Val = 0;
	SK_U32		LowVal;
	SK_U32		HighVal;


	switch (StatIndex) {

	case SK_PNMI_HTX_OCTET:
		XM_IN32(IoC, PhysPortIndex, XM_TXO_OK_LO, &LowVal);
		XM_IN32(IoC, PhysPortIndex, XM_TXO_OK_HI, &HighVal);
		break;

	case SK_PNMI_HRX_OCTET:
		XM_IN32(IoC, PhysPortIndex, XM_RXO_OK_LO, &LowVal);
		XM_IN32(IoC, PhysPortIndex, XM_RXO_OK_HI, &HighVal);
		break;

	case SK_PNMI_HTX_OCTETLOW:
	case SK_PNMI_HRX_OCTETLOW:
		return (Val);

	case SK_PNMI_HTX_SYNC:
		LowVal = (SK_U32)pAC->Pnmi.Port[PhysPortIndex].StatSyncCts;
		HighVal = (SK_U32)
			(pAC->Pnmi.Port[PhysPortIndex].StatSyncCts >> 32);
		break;

	case SK_PNMI_HTX_SYNC_OCTET:
		LowVal = (SK_U32)pAC->Pnmi.Port[PhysPortIndex].
			StatSyncOctetsCts;
		HighVal = (SK_U32)
			(pAC->Pnmi.Port[PhysPortIndex].StatSyncOctetsCts >>
			32);
		break;

	case SK_PNMI_HRX_LONGFRAMES:
		LowVal = (SK_U32)pAC->Pnmi.Port[PhysPortIndex].StatRxLongFrameCts;
		HighVal = (SK_U32)
			(pAC->Pnmi.Port[PhysPortIndex].StatRxLongFrameCts >> 32);
		break;

	case SK_PNMI_HRX_FCS:
		/* 
		 * Broadcom filters fcs errors and counts it in 
		 * Receive Error Counter register
		 */
		if (pAC->GIni.GP[PhysPortIndex].PhyType == SK_PHY_BCOM) {
			/* do not read while not initialized (PHY_READ hangs!)*/
			if (pAC->GIni.GP[PhysPortIndex].PState) {
				PHY_READ(IoC, &pAC->GIni.GP[PhysPortIndex],
					 PhysPortIndex, PHY_BCOM_RE_CTR,
					&LowVal);
			}
			else {
				LowVal = 0;
			}
			HighVal = pAC->Pnmi.Port[PhysPortIndex].CounterHigh[StatIndex];
		}
		else {
			XM_IN32(IoC, PhysPortIndex,
				StatAddress[StatIndex].Param, &LowVal);
			HighVal = pAC->Pnmi.Port[PhysPortIndex].CounterHigh[StatIndex];
		}
	default:
		XM_IN32(IoC, PhysPortIndex, StatAddress[StatIndex].Param,
			&LowVal);
		HighVal = pAC->Pnmi.Port[PhysPortIndex].CounterHigh[StatIndex];
		break;
	}

	Val = (((SK_U64)HighVal << 32) | (SK_U64)LowVal);

	/* Correct value because of possible XMAC reset. XMAC Errata #2 */
	Val += pAC->Pnmi.Port[PhysPortIndex].CounterOffset[StatIndex];

	return (Val);
}

/*****************************************************************************
 *
 * ResetCounter - Set all counters and timestamps to zero
 *
 * Description:
 *	Notifies other common modules which store statistic data to
 *	reset their counters and finally reset our own counters.
 *
 * Returns:
 *	Nothing
 */

static void ResetCounter(
SK_AC *pAC,		/* Pointer to adapter context */
SK_IOC IoC)		/* IO context handle */
{
	unsigned int	PhysPortIndex;
	SK_EVPARA	EventParam;


	SK_MEMSET((char *)&EventParam, 0, sizeof(EventParam));

	/* Notify sensor module */
	SkEventQueue(pAC, SKGE_I2C, SK_I2CEV_CLEAR, EventParam);

	/* Notify RLMT module */
	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STATS_CLEAR, EventParam);

	/* Notify SIRQ module */
	SkEventQueue(pAC, SKGE_HWAC, SK_HWEV_CLEAR_STAT, EventParam);

	/* Notify CSUM module */
#ifdef SK_USE_CSUM
	EventParam.Para64 = (SK_U64)(-1);
	SkEventQueue(pAC, SKGE_CSUM, SK_CSUM_EVENT_CLEAR_PROTO_STATS,
		EventParam);
#endif

	/* Clear XMAC statistic */
	for (PhysPortIndex = 0; PhysPortIndex <
		(unsigned int)pAC->GIni.GIMacsFound; PhysPortIndex ++) {

		XM_OUT16(IoC, PhysPortIndex, XM_STAT_CMD,
			XM_SC_CLR_RXC | XM_SC_CLR_TXC);
		/* Clear two times according to Errata #3 */
		XM_OUT16(IoC, PhysPortIndex, XM_STAT_CMD,
			XM_SC_CLR_RXC | XM_SC_CLR_TXC);

		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].CounterHigh,
			0, sizeof(pAC->Pnmi.Port[PhysPortIndex].CounterHigh));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
			CounterOffset, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].CounterOffset));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].StatSyncCts,
			0, sizeof(pAC->Pnmi.Port[PhysPortIndex].StatSyncCts));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
			StatSyncOctetsCts, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].StatSyncOctetsCts));
		SK_MEMSET((char *)&pAC->Pnmi.Port[PhysPortIndex].
			StatRxLongFrameCts, 0, sizeof(pAC->Pnmi.Port[
			PhysPortIndex].StatRxLongFrameCts));
	}

	/*
	 * Clear local statistics
	 */
	SK_MEMSET((char *)&pAC->Pnmi.VirtualCounterOffset, 0,
		  sizeof(pAC->Pnmi.VirtualCounterOffset));
	pAC->Pnmi.RlmtChangeCts = 0;
	pAC->Pnmi.RlmtChangeTime = 0;
	SK_MEMSET((char *)&pAC->Pnmi.RlmtChangeEstimate.EstValue[0], 0,
		sizeof(pAC->Pnmi.RlmtChangeEstimate.EstValue));
	pAC->Pnmi.RlmtChangeEstimate.EstValueIndex = 0;
	pAC->Pnmi.RlmtChangeEstimate.Estimate = 0;
	pAC->Pnmi.TxSwQueueMax = 0;
	pAC->Pnmi.TxRetryCts = 0;
	pAC->Pnmi.RxIntrCts = 0;
	pAC->Pnmi.TxIntrCts = 0;
	pAC->Pnmi.RxNoBufCts = 0;
	pAC->Pnmi.TxNoBufCts = 0;
	pAC->Pnmi.TxUsedDescrNo = 0;
	pAC->Pnmi.RxDeliveredCts = 0;
	pAC->Pnmi.RxOctetsDeliveredCts = 0;
	pAC->Pnmi.ErrRecoveryCts = 0;
}

/*****************************************************************************
 *
 * GetTrapEntry - Get an entry in the trap buffer
 *
 * Description:
 *	The trap buffer stores various events. A user application somehow
 *	gets notified that an event occured and retrieves the trap buffer
 *	contens (or simply polls the buffer). The buffer is organized as
 *	a ring which stores the newest traps at the beginning. The oldest
 *	traps are overwritten by the newest ones. Each trap entry has a
 *	unique number, so that applications may detect new trap entries.
 *
 * Returns:
 *	A pointer to the trap entry
 */

static char* GetTrapEntry(
SK_AC *pAC,		/* Pointer to adapter context */
SK_U32 TrapId,		/* SNMP ID of the trap */
unsigned int Size)	/* Space needed for trap entry */
{
	unsigned int		BufPad = pAC->Pnmi.TrapBufPad;
	unsigned int		BufFree = pAC->Pnmi.TrapBufFree;
	unsigned int		Beg = pAC->Pnmi.TrapQueueBeg;
	unsigned int		End = pAC->Pnmi.TrapQueueEnd;
	char			*pBuf = &pAC->Pnmi.TrapBuf[0];
	int			Wrap;
	unsigned int		NeededSpace;
	unsigned int		EntrySize;
	SK_U32			Val32;
	SK_U64			Val64;


	/* Last byte of entry will get a copy of the entry length */
	Size ++;

	/*
	 * Calculate needed buffer space */
	if (Beg >= Size) {

		NeededSpace = Size;
		Wrap = FALSE;
	}
	else {
		NeededSpace = Beg + Size;
		Wrap = TRUE;
	}

	/*
	 * Check if enough buffer space is provided. Otherwise
	 * free some entries. Leave one byte space between begin
	 * and end of buffer to make it possible to detect whether
	 * the buffer is full or empty
	 */
	while (BufFree < NeededSpace + 1) {

		if (End == 0) {

			End = SK_PNMI_TRAP_QUEUE_LEN;
		}

		EntrySize = (unsigned int)*((unsigned char *)pBuf + End - 1);
		BufFree += EntrySize;
		End -= EntrySize;
#ifdef DEBUG
		SK_MEMSET(pBuf + End, (char)(-1), EntrySize);
#endif
		if (End == BufPad) {
#ifdef DEBUG
			SK_MEMSET(pBuf, (char)(-1), End);
#endif
			BufFree += End;
			End = 0;
			BufPad = 0;
		}
	}

	/* 
	 * Insert new entry as first entry. Newest entries are
	 * stored at the beginning of the queue.
	 */
	if (Wrap) {

		BufPad = Beg;
		Beg = SK_PNMI_TRAP_QUEUE_LEN - Size;
	}
	else {
		Beg = Beg - Size;
	}
	BufFree -= NeededSpace;

	/* Save the current offsets */
	pAC->Pnmi.TrapQueueBeg = Beg;
	pAC->Pnmi.TrapQueueEnd = End;
	pAC->Pnmi.TrapBufPad = BufPad;
	pAC->Pnmi.TrapBufFree = BufFree;

	/* Initialize the trap entry */
	*(pBuf + Beg + Size - 1) = (char)Size;
	*(pBuf + Beg) = (char)Size;
	Val32 = (pAC->Pnmi.TrapUnique) ++;
	SK_PNMI_STORE_U32(pBuf + Beg + 1, Val32);
	SK_PNMI_STORE_U32(pBuf + Beg + 1 + sizeof(SK_U32), TrapId);
	Val64 = SK_PNMI_HUNDREDS_SEC(SkOsGetTime(pAC));
	SK_PNMI_STORE_U64(pBuf + Beg + 1 + 2 * sizeof(SK_U32), Val64);

	return (pBuf + Beg);
}

/*****************************************************************************
 *
 * CopyTrapQueue - Copies the trap buffer for the TRAP OID
 *
 * Description:
 *	On a query of the TRAP OID the trap buffer contents will be
 *	copied continuously to the request buffer, which must be large
 *	enough. No length check is performed.
 *
 * Returns:
 *	Nothing
 */

static void CopyTrapQueue(
SK_AC *pAC,		/* Pointer to adapter context */
char *pDstBuf)		/* Buffer to which the queued traps will be copied */
{
	unsigned int	BufPad = pAC->Pnmi.TrapBufPad;
	unsigned int	Trap = pAC->Pnmi.TrapQueueBeg;
	unsigned int	End = pAC->Pnmi.TrapQueueEnd;
	char		*pBuf = &pAC->Pnmi.TrapBuf[0];
	unsigned int	Len;
	unsigned int	DstOff = 0;


	while (Trap != End) {

		Len = (unsigned int)*(pBuf + Trap);

		/*
		 * Last byte containing a copy of the length will
		 * not be copied.
		 */
		*(pDstBuf + DstOff) = (char)(Len - 1);
		SK_MEMCPY(pDstBuf + DstOff + 1, pBuf + Trap + 1, Len - 2);
		DstOff += Len - 1;

		Trap += Len;
		if (Trap == SK_PNMI_TRAP_QUEUE_LEN) {

			Trap = BufPad;
		}
	}
}

/*****************************************************************************
 *
 * GetTrapQueueLen - Get the length of the trap buffer
 *
 * Description:
 *	Evaluates the number of currently stored traps and the needed
 *	buffer size to retrieve them.
 *
 * Returns:
 *	Nothing
 */

static void GetTrapQueueLen(
SK_AC *pAC,		/* Pointer to adapter context */
unsigned int *pLen,	/* Length in Bytes of all queued traps */
unsigned int *pEntries)	/* Returns number of trapes stored in queue */
{
	unsigned int	BufPad = pAC->Pnmi.TrapBufPad;
	unsigned int	Trap = pAC->Pnmi.TrapQueueBeg;
	unsigned int	End = pAC->Pnmi.TrapQueueEnd;
	char		*pBuf = &pAC->Pnmi.TrapBuf[0];
	unsigned int	Len;
	unsigned int	Entries = 0;
	unsigned int	TotalLen = 0;


	while (Trap != End) {

		Len = (unsigned int)*(pBuf + Trap);
		TotalLen += Len - 1;
		Entries ++;

		Trap += Len;
		if (Trap == SK_PNMI_TRAP_QUEUE_LEN) {

			Trap = BufPad;
		}
	}

	*pEntries = Entries;
	*pLen = TotalLen;
}

/*****************************************************************************
 *
 * QueueSimpleTrap - Store a simple trap to the trap buffer
 *
 * Description:
 *	A simple trap is a trap with now additional data. It consists
 *	simply of a trap code.
 *
 * Returns:
 *	Nothing
 */

static void QueueSimpleTrap(
SK_AC *pAC,		/* Pointer to adapter context */
SK_U32 TrapId)		/* Type of sensor trap */
{
	GetTrapEntry(pAC, TrapId, SK_PNMI_TRAP_SIMPLE_LEN);
}

/*****************************************************************************
 *
 * QueueSensorTrap - Stores a sensor trap in the trap buffer
 *
 * Description:
 *	Gets an entry in the trap buffer and fills it with sensor related
 *	data.
 *
 * Returns:
 *	Nothing
 */

static void QueueSensorTrap(
SK_AC *pAC,			/* Pointer to adapter context */
SK_U32 TrapId,			/* Type of sensor trap */
unsigned int SensorIndex)	/* Index of sensor which caused the trap */
{
	char		*pBuf;
	unsigned int	Offset;
	unsigned int	DescrLen;
	SK_U32		Val32;


	/* Get trap buffer entry */
	DescrLen = SK_STRLEN(pAC->I2c.SenTable[SensorIndex].SenDesc);
	pBuf = GetTrapEntry(pAC, TrapId,
		SK_PNMI_TRAP_SENSOR_LEN_BASE + DescrLen);
	Offset = SK_PNMI_TRAP_SIMPLE_LEN;

	/* Store additionally sensor trap related data */
	Val32 = OID_SKGE_SENSOR_INDEX;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = 4;
	Val32 = (SK_U32)SensorIndex;
	SK_PNMI_STORE_U32(pBuf + Offset + 5, Val32);
	Offset += 9;
	
	Val32 = (SK_U32)OID_SKGE_SENSOR_DESCR;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = (char)DescrLen;
	SK_MEMCPY(pBuf + Offset + 5, pAC->I2c.SenTable[SensorIndex].SenDesc,
		DescrLen);
	Offset += DescrLen + 5;

	Val32 = OID_SKGE_SENSOR_TYPE;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = 1;
	*(pBuf + Offset + 5) = (char)pAC->I2c.SenTable[SensorIndex].SenType;
	Offset += 6;

	Val32 = OID_SKGE_SENSOR_VALUE;
	SK_PNMI_STORE_U32(pBuf + Offset, Val32);
	*(pBuf + Offset + 4) = 4;
	Val32 = (SK_U32)pAC->I2c.SenTable[SensorIndex].SenValue;
	SK_PNMI_STORE_U32(pBuf + Offset + 5, Val32);
}

/*****************************************************************************
 *
 * QueueRlmtNewMacTrap - Store a port switch trap in the trap buffer
 *
 * Description:
 *	Nothing further to explain.
 *
 * Returns:
 *	Nothing
 */

static void QueueRlmtNewMacTrap(
SK_AC *pAC,		/* Pointer to adapter context */
unsigned int ActiveMac)	/* Index (0..n) of the currently active port */
{
	char	*pBuf;
	SK_U32	Val32;


	pBuf = GetTrapEntry(pAC, OID_SKGE_TRAP_RLMT_CHANGE_PORT,
		SK_PNMI_TRAP_RLMT_CHANGE_LEN);

	Val32 = OID_SKGE_RLMT_PORT_ACTIVE;
	SK_PNMI_STORE_U32(pBuf + SK_PNMI_TRAP_SIMPLE_LEN, Val32);
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 4) = 1;
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 5) = (char)ActiveMac;
}

/*****************************************************************************
 *
 * QueueRlmtPortTrap - Store port related RLMT trap to trap buffer
 *
 * Description:
 *	Nothing further to explain.
 *
 * Returns:
 *	Nothing
 */

static void QueueRlmtPortTrap(
SK_AC *pAC,		/* Pointer to adapter context */
SK_U32 TrapId,		/* Type of RLMT port trap */
unsigned int PortIndex)	/* Index of the port, which changed its state */
{
	char	*pBuf;
	SK_U32	Val32;


	pBuf = GetTrapEntry(pAC, TrapId, SK_PNMI_TRAP_RLMT_PORT_LEN);

	Val32 = OID_SKGE_RLMT_PORT_INDEX;
	SK_PNMI_STORE_U32(pBuf + SK_PNMI_TRAP_SIMPLE_LEN, Val32);
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 4) = 1;
	*(pBuf + SK_PNMI_TRAP_SIMPLE_LEN + 5) = (char)PortIndex;
}

/*****************************************************************************
 *
 * CopyMac - Copies a MAC address
 *
 * Description:
 *	Nothing further to explain.
 *
 * Returns:
 *	Nothing
 */

static void CopyMac(
char *pDst,		/* Pointer to destination buffer */
SK_MAC_ADDR *pMac)	/* Pointer of Source */
{
	int	i;


	for (i = 0; i < sizeof(SK_MAC_ADDR); i ++) {

		*(pDst + i) = pMac->a[i];
	}
}
