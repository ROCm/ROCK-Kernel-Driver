/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#include "elx_os.h"
#include "elx_util.h"
#include "elx_clock.h"
#include "elx_hw.h"
#include "elx_mem.h"
#include "elx_sli.h"
#include "elx_sched.h"
#include "elx.h"
#include "elx_logmsg.h"
#include "elx_disc.h"
#include "elx_scsi.h"
#include "elx_crtn.h"
#include "prod_crtn.h"

/* ELS Log Message Preamble Strings - 100 */
char elx_msgPreambleELi[] = "ELi:";	/* ELS Information */
char elx_msgPreambleELw[] = "ELw:";	/* ELS Warning */
char elx_msgPreambleELe[] = "ELe:";	/* ELS Error */
char elx_msgPreambleELp[] = "ELp:";	/* ELS Panic */

/* DISCOVERY Log Message Preamble Strings - 200 */
char elx_msgPreambleDIi[] = "DIi:";	/* Discovery Information */
char elx_msgPreambleDIw[] = "DIw:";	/* Discovery Warning */
char elx_msgPreambleDIe[] = "DIe:";	/* Discovery Error */
char elx_msgPreambleDIp[] = "DIp:";	/* Discovery Panic */

/* MAIBOX Log Message Preamble Strings - 300 */
/* SLI Log Message Preamble Strings    - 300 */
char elx_msgPreambleMBi[] = "MBi:";	/* Mailbox Information */
char elx_msgPreambleMBw[] = "MBw:";	/* Mailbox Warning */
char elx_msgPreambleMBe[] = "MBe:";	/* Mailbox Error */
char elx_msgPreambleMBp[] = "MBp:";	/* Mailbox Panic */
char elx_msgPreambleSLw[] = "SLw:";	/* SLI Warning */
char elx_msgPreambleSLe[] = "SLe:";	/* SLI Error */
char elx_msgPreambleSLi[] = "SLi:";	/* SLI Information */

/* INIT Log Message Preamble Strings - 400, 500 */
char elx_msgPreambleINi[] = "INi:";	/* INIT Information */
char elx_msgPreambleINw[] = "INw:";	/* INIT Warning */
char elx_msgPreambleINc[] = "INc:";	/* INIT Error Config */
char elx_msgPreambleINe[] = "INe:";	/* INIT Error */
char elx_msgPreambleINp[] = "INp:";	/* INIT Panic */

/* IP Log Message Preamble Strings - 600 */
char elx_msgPreambleIPi[] = "IPi:";	/* IP Information */
char elx_msgPreambleIPw[] = "IPw:";	/* IP Warning */
char elx_msgPreambleIPe[] = "IPe:";	/* IP Error */
char elx_msgPreambleIPp[] = "IPp:";	/* IP Panic */

/* FCP Log Message Preamble Strings - 700, 800 */
char elx_msgPreambleFPi[] = "FPi:";	/* FP Information */
char elx_msgPreambleFPw[] = "FPw:";	/* FP Warning */
char elx_msgPreambleFPe[] = "FPe:";	/* FP Error */
char elx_msgPreambleFPp[] = "FPp:";	/* FP Panic */

/* NODE Log Message Preamble Strings - 900 */
char elx_msgPreambleNDi[] = "NDi:";	/* Node Information */
char elx_msgPreambleNDe[] = "NDe:";	/* Node Error */
char elx_msgPreambleNDp[] = "NDp:";	/* Node Panic */

/* MISC Log Message Preamble Strings - 1200 */
char elx_msgPreambleMIi[] = "MIi:";	/* MISC Information */
char elx_msgPreambleMIw[] = "MIw:";	/* MISC Warning */
char elx_msgPreambleMIc[] = "MIc:";	/* MISC Error Config */
char elx_msgPreambleMIe[] = "MIe:";	/* MISC Error */
char elx_msgPreambleMIp[] = "MIp:";	/* MISC Panic */

/* Link Log Message Preamble Strings - 1300 */
char elx_msgPreambleLKi[] = "LKi:";	/* Link Information */
char elx_msgPreambleLKw[] = "LKw:";	/* Link Warning */
char elx_msgPreambleLKe[] = "LKe:";	/* Link Error */
char elx_msgPreambleLKp[] = "Lkp:";	/* Link Panic */

/* CHECK CONDITION Log Message Preamble Strings - 1500 */
char elx_msgPreambleCKi[] = "CKi:";	/* Check Condition Information */
char elx_msgPreambleCKe[] = "CKe:";	/* Check Condition Error */
char elx_msgPreambleCKp[] = "CKp:";	/* Check Condition Panic */

/* IOCtl Log Message Preamble Strings - 1600 */
char elx_msgPreambleIOi[] = "IOi:";	/* IOCtl Information */
char elx_msgPreambleIOw[] = "IOw:";	/* IOCtl Warning */
char elx_msgPreambleIOe[] = "IOe:";	/* IOCtl Error */
char elx_msgPreambleIOp[] = "IOp:";	/* IOCtl Panic */

/* 
 * The format of all code below this point must meet rules specified by 
 * the ultility MKLOGRPT.
 */

/*
 *  Begin ELS LOG message structures
 */

/*
msgName: elx_mes0100
message:  FLOGI failure
descript: An ELS FLOGI command that was sent to the fabric failed.
data:     (1) ulpStatus (2) ulpWord[4]
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0100[] = "%sFLOGI failure Data: x%x x%x";
msgLogDef elx_msgBlk0100 = {
	ELX_LOG_MSG_EL_0100,
	elx_mes0100,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0101
message:  FLOGI completes successfully
descript: An ELS FLOGI command that was sent to the fabric succeeded.
data:     (1) ulpWord[4] (2) e_d_tov (3) r_a_tov (4) edtovResolution
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0101[] = "%sFLOGI completes sucessfully Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0101 = {
	ELX_LOG_MSG_EL_0101,
	elx_mes0101,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0102
message:  PLOGI completes to NPort <nlp_DID>
descript: The HBA performed a PLOGI into a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) disc (4) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0102[] = "%sPLOGI completes to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0102 = {
	ELX_LOG_MSG_EL_0102,
	elx_mes0102,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0103
message:  PRLI completes to NPort <nlp_DID>
descript: The HBA performed a PRLI into a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0103[] = "%sPRLI completes to NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0103 = {
	ELX_LOG_MSG_EL_0103,
	elx_mes0103,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0104
message:  ADISC completes to NPort <nlp_DID>
descript: The HBA performed a ADISC into a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) disc (4) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0104[] = "%sADISC completes to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0104 = {
	ELX_LOG_MSG_EL_0104,
	elx_mes0104,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0105
message:  LOGO completes to NPort <nlp_DID>
descript: The HBA performed a LOGO to a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0105[] = "%sLOGO completes to NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0105 = {
	ELX_LOG_MSG_EL_0105,
	elx_mes0105,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0106
message:  ELS cmd tag <ulpIoTag> completes
descript: The specific ELS command was completed by the firmware.
data:     (1) ulpStatus (2) ulpWord[4]
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0106[] = "%sELS cmd tag x%x completes Data: x%x x%x";
msgLogDef elx_msgBlk0106 = {
	ELX_LOG_MSG_EL_0106,
	elx_mes0106,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0107
message:  Retry ELS command <elsCmd> to remote NPORT <did>
descript: The driver is retrying the specific ELS command.
data:     (1) retry (2) delay
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0107[] =
    "%sRetry ELS command x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef elx_msgBlk0107 = {
	ELX_LOG_MSG_EL_0107,
	elx_mes0107,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0108
message:  No retry ELS command <elsCmd> to remote NPORT <did>
descript: The driver decided not to retry the specific ELS command that failed.
data:     (1) retry (2) nlp_flag
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0108[] =
    "%sNo retry ELS command x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef elx_msgBlk0108 = {
	ELX_LOG_MSG_EL_0108,
	elx_mes0108,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0109
message:  ACC to LOGO completes to NPort <nlp_DID>
descript: The driver received a LOGO from a remote NPort and successfully issued an ACC response.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0109[] = "%sACC to LOGO completes to NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0109 = {
	ELX_LOG_MSG_EL_0109,
	elx_mes0109,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0110
message:  ELS response tag <ulpIoTag> completes
descript: The specific ELS response was completed by the firmware.
data:     (1) ulpStatus (2) ulpWord[4] (3) nlp_DID (4) nlp_flag (5) nlp_state (6) nle.nlp_rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0110[] =
    "%sELS response tag x%x completes Data: x%x x%x x%x x%x x%x x%x";
msgLogDef elx_msgBlk0110 = {
	ELX_LOG_MSG_EL_0110,
	elx_mes0110,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0111
message:  Dropping received ELS cmd
descript: The driver decided to drop an ELS Response ring entry
data:     (1) ulpStatus (2) ulpWord[4]
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char elx_mes0111[] = "%sDropping received ELS cmd Data: x%x x%x";
msgLogDef elx_msgBlk0111 = {
	ELX_LOG_MSG_EL_0111,
	elx_mes0111,
	elx_msgPreambleELe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0112
message:  ELS command <elsCmd> received from NPORT <did> 
descript: Received the specific ELS command from a remote NPort.
data:     (1) fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0112[] = "%sELS command x%x received from NPORT x%x Data: x%x";
msgLogDef elx_msgBlk0112 = {
	ELX_LOG_MSG_EL_0112,
	elx_mes0112,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0113
message:  An FLOGI ELS command <elsCmd> was received from DID <did> in Loop Mode
descript: While in Loop Mode an unknown or unsupported ELS commnad 
          was received.
data:     None
severity: Error
log:      Always
action:   Check device DID
*/
char elx_mes0113[] =
    "%sAn FLOGI ELS command x%x was received from DID x%x in Loop Mode";
msgLogDef elx_msgBlk0113 = {
	ELX_LOG_MSG_EL_0113,
	elx_mes0113,
	elx_msgPreambleELe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0114
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0114[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0114 = {
	ELX_LOG_MSG_EL_0114,
	elx_mes0114,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0115
message:  Unknown ELS command <elsCmd> received from NPORT <did> 
descript: Received an unsupported ELS command from a remote NPORT.
data:     None
severity: Error
log:      Always
action:   Check remote NPORT for potential problem.
*/
char elx_mes0115[] = "%sUnknown ELS command x%x received from NPORT x%x";
msgLogDef elx_msgBlk0115 = {
	ELX_LOG_MSG_EL_0115,
	elx_mes0115,
	elx_msgPreambleELe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0116
message:  Xmit ELS command <elsCmd> to remote NPORT <did>
descript: Xmit ELS command to remote NPORT 
data:     (1) icmd->ulpIoTag (2) binfo->fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0116[] = "%sXmit ELS command x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef elx_msgBlk0116 = {
	ELX_LOG_MSG_EL_0116,
	elx_mes0116,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0117
message:  Xmit ELS response <elsCmd> to remote NPORT <did>
descript: Xmit ELS response to remote NPORT 
data:     (1) icmd->ulpIoTag (2) size
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0117[] =
    "%sXmit ELS response x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef elx_msgBlk0117 = {
	ELX_LOG_MSG_EL_0117,
	elx_mes0117,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0118
message:  Xmit CT response on exchange <xid>
descript: Xmit a CT response on the appropriate exchange.
data:     (1) ulpIoTag (2) fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0118[] = "%sXmit CT response on exchange x%x Data: x%x x%x";
msgLogDef elx_msgBlk0118 = {
	ELX_LOG_MSG_EL_0118,
	elx_mes0118,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0119
message:  Issue GEN REQ IOCB for NPORT <did>
descript: Issue a GEN REQ IOCB for remote NPORT.  These are typically
          used for CT request. 
data:     (1) ulpIoTag (2) fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0119[] = "%sIssue GEN REQ IOCB for NPORT x%x Data: x%x x%x";
msgLogDef elx_msgBlk0119 = {
	ELX_LOG_MSG_EL_0119,
	elx_mes0119,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0120
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0120[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0120 = {
	ELX_LOG_MSG_EL_0120,
	elx_mes0120,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0121
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0121[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0121 = {
	ELX_LOG_MSG_EL_0121,
	elx_mes0121,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0122
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0122[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0122 = {
	ELX_LOG_MSG_EL_0122,
	elx_mes0122,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0123
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0123[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0123 = {
	ELX_LOG_MSG_EL_0123,
	elx_mes0123,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0124
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0124[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0124 = {
	ELX_LOG_MSG_EL_0124,
	elx_mes0124,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0125
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0125[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0125 = {
	ELX_LOG_MSG_EL_0125,
	elx_mes0125,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0126
message:  PLOGI chkparm OK
descript: Recieved a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char elx_mes0126[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0126 = {
	ELX_LOG_MSG_EL_0126,
	elx_mes0126,
	elx_msgPreambleELi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0127
message:  ELS timeout
descript: An ELS IOCB command was posted to a ring and did not complete
          within ULP timeout seconds.
data:     (1) elscmd (2) did (3) ulpcommand (4) iotag
severity: Error
log:      Always
action:   If no ELS command is going through the adapter, reboot the system;
          If problem persists, contact Technical Support.
*/
char elx_mes0127[] = "%sELS timeout Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0127 = {
	ELX_LOG_MSG_EL_0127,
	elx_mes0127,
	elx_msgPreambleELe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_TIMEOUT
};

/*
 *  Begin DSCOVERY LOG Message Structures
 */

/*
msgName: elx_mes0200
message:  CONFIG_LINK bad hba state <hba_state>
descript: A CONFIG_LINK mbox command completed and the driver was not in the right state.
data:     none
severity: Error
log:      Always
action:   Software driver error.
          If this problem persists, report these errors to Technical Support.
*/
char elx_mes0200[] = "%sCONFIG_LINK bad hba state x%x";
msgLogDef elx_msgBlk0200 = {
	ELX_LOG_MSG_DI_0200,
	elx_mes0200,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0201
message:  Abort outstanding I/O on NPort <nlp_DID>
descript: All outstanding I/Os are cleaned up on the specified remote NPort.
data:     (1) nlp_flag (2) nlp_state (3) nle.nlp_rpi
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0201[] = "%sAbort outstanding I/O on NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0201 = {
	ELX_LOG_MSG_DI_0201,
	elx_mes0201,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0202
message:  Start Discovery hba state <hba_state>
descript: Device discovery / rediscovery after FLOGI, FAN or RSCN has started.
data:     (1) tmo (2) fc_plogi_cnt (3) fc_adisc_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0202[] = "%sStart Discovery hba state x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0202 = {
	ELX_LOG_MSG_DI_0202,
	elx_mes0202,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0203
message:  Nodev timeout on NPort <nlp_DID>
descript: A remote NPort the was discovered by the driver disappeared for more than ELX_NODEV_TMO seconds.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Error
log:      Always
action:   Check connections to Fabric / HUB or remote device.
*/
char elx_mes0203[] = "%sNodev timeout on NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0203 = {
	ELX_LOG_MSG_DI_0203,
	elx_mes0203,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0204
message:  Create SCSI Target <tgt>
descript: A mapped FCP target was discovered and the driver has allocated resources for it.
data:     none
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0204[] = "%sCreate SCSI Target %d";
msgLogDef elx_msgBlk0204 = {
	ELX_LOG_MSG_DI_0204,
	elx_mes0204,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0205
message:  Create SCSI LUN <lun> on Target <tgt>
descript: A LUN on a mapped FCP target was discovered and the driver has allocated resources for it.
data:     none
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0205[] = "%sCreate SCSI LUN %d on Target %d";
msgLogDef elx_msgBlk0205 = {
	ELX_LOG_MSG_DI_0205,
	elx_mes0205,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0206
message:  Report Lun completes on NPort <nlp_DID>
descript: The driver issued a REPORT_LUN SCSI command to a FCP target and it completed.
data:     (1) ulpStatus (2) rspStatus2 (3) rspStatus3 (4) nlp_failMask
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0206[] =
    "%sReport Lun completes on NPort x%x status: x%x status2: x%x status3: x%x failMask: x%x";
msgLogDef elx_msgBlk0206 = {
	ELX_LOG_MSG_DI_0206,
	elx_mes0206,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0207
message:  Issue Report LUN on NPort <nlp_DID>
descript: The driver issued a REPORT_LUN SCSI command to a FCP target.
data:     (1) nlp_failMask (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0207[] = "%sIssue Report LUN on NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0207 = {
	ELX_LOG_MSG_DI_0207,
	elx_mes0207,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0208
message:  Failmask change on NPort <nlp_DID>
descript: An event was processed that indicates the driver may not be able to communicate with
          the remote NPort.
data:     (1) nlp_failMask (2) bitmask (3) flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0208[] = "%sFailmask change on NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0208 = {
	ELX_LOG_MSG_DI_0208,
	elx_mes0208,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0209
message:  RFT request completes ulpStatus <ulpStatus> CmdRsp <CmdRsp>
descript: A RFT request that was sent to the fabric completed.
data:     (1) nlp_failMask (2) bitmask (3) flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0209[] = "%sRFT request completes ulpStatus x%x CmdRsp x%x";
msgLogDef elx_msgBlk0209 = {
	ELX_LOG_MSG_DI_0209,
	elx_mes0209,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0210
message:  Continue discovery with <num_disc_nodes> ADISCs to go
descript: Device discovery is in progress
data:     (1) fc_adisc_cnt (2) fc_flag (3) phba->hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0210[] =
    "%sContinue discovery with %d ADISCs to go Data: x%x x%x x%x";
msgLogDef elx_msgBlk0210 = {
	ELX_LOG_MSG_DI_0210,
	elx_mes0210,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0211
message:  DSM in event <evt> on NPort <nlp_DID> in state <cur_state>
descript: The driver Discovery State Machine is processing an event.
data:     (1) nlp_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0211[] = "%sDSM in event x%x on NPort x%x in state %d Data: x%x";
msgLogDef elx_msgBlk0211 = {
	ELX_LOG_MSG_DI_0211,
	elx_mes0211,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0212
message:  DSM out state <rc> on NPort <nlp_DID>
descript: The driver Discovery State Machine completed processing an event.
data:     (1) nlp_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0212[] = "%sDSM out state %d on NPort x%x Data: x%x";
msgLogDef elx_msgBlk0212 = {
	ELX_LOG_MSG_DI_0212,
	elx_mes0212,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0213
message:  Reassign scsi id <sid> to NPort <nlp_DID>
descript: A previously bound FCP Target has been rediscovered and reassigned a scsi id.
data:     (1) nlp_bind_type (2) nlp_flag (3) nlp_state (4) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0213[] =
    "%sReassign scsi id x%x to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0213 = {
	ELX_LOG_MSG_DI_0213,
	elx_mes0213,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0214
message:  RSCN received
descript: A RSCN ELS command was received from a fabric.
data:     (1) fc_flag (2) i (3) *lp (4) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0214[] = "%sRSCN received Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0214 = {
	ELX_LOG_MSG_DI_0214,
	elx_mes0214,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0215
message:  RSCN processed
descript: A RSCN ELS command was received from a fabric and processed.
data:     (1) fc_flag (2) cnt (3) fc_rscn_id_cnt (4) fc_ffstate
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0215[] = "%sRSCN processed Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0215 = {
	ELX_LOG_MSG_DI_0215,
	elx_mes0215,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0216
message:  Assign scandown scsi id <sid> to NPort <nlp_DID>
descript: A scsi id is assigned due to BIND_ALPA.
data:     (1) nlp_bind_type (2) nlp_flag (3) nlp_state (4) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0216[] =
    "%sAssign scandown scsi id x%x to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0216 = {
	ELX_LOG_MSG_DI_0216,
	elx_mes0216,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0217
message:  Unknown Identifier in RSCN payload
descript: Typically the identifier in the RSCN payload specifies 
          a domain, area or a specific NportID. If neither of 
          these are specified, a warning will be recorded. 
data:     (1) didp->un.word
detail:   (1) Illegal identifier
severity: Error
log:      Always
action:   Potential problem with Fabric. Check with Fabric vendor.
*/
char elx_mes0217[] = "%sUnknown Identifier in RSCN payload Data: x%x";
msgLogDef elx_msgBlk0217 = {
	ELX_LOG_MSG_DI_0217,
	elx_mes0217,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0218
message:  FDMI Request
descript: The driver is sending an FDMI request to the fabric.
data:     (1) fc_flag (2) hba_state (3) cmdcode
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0218[] = "%sFDMI Request Data: x%x x%x x%x";
msgLogDef elx_msgBlk0218 = {
	ELX_LOG_MSG_DI_0218,
	elx_mes0218,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0219
message:  Issue FDMI request failed
descript: Cannot issue FDMI request to HBA.
data:     (1) cmdcode
severity: Information
log:      LOG_Discovery verbose
action:   No action needed, informational
*/
char elx_mes0219[] = "%sIssue FDMI request failed Data: x%x";
msgLogDef elx_msgBlk0219 = {
	ELX_LOG_MSG_DI_0219,
	elx_mes0219,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0220
message:  FDMI rsp failed
descript: An error response was received to FDMI request
data:     (1) SWAP_DATA16(fdmi_cmd)
severity: Information
log:      LOG_DISCOVERY verbose
action:   The fabric does not support FDMI, check fabric configuration.
*/
char elx_mes0220[] = "%sFDMI rsp failed Data: x%x";
msgLogDef elx_msgBlk0220 = {
	ELX_LOG_MSG_DI_0220,
	elx_mes0220,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0221
message:  FAN timeout
descript: A link up event was received without the login bit set, 
          so the driver waits E_D_TOV for the Fabric to send a FAN. 
          If no FAN if received, a FLOGI will be sent after the timeout. 
data:     None
severity: Warning
log:      LOG_DISCOVERY verbose
action:   None required. The driver recovers from this condition by 
          issuing a FLOGI to the Fabric.
*/
char elx_mes0221[] = "%sFAN timeout";
msgLogDef elx_msgBlk0221 = {
	ELX_LOG_MSG_DI_0221,
	elx_mes0221,
	elx_msgPreambleDIw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0222
message:  Initial FLOGI timeout
descript: The driver sent the initial FLOGI to fabric and never got a response back.
data:     None
severity: Error
log:      Always
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char elx_mes0222[] = "%sInitial FLOGI timeout";
msgLogDef elx_msgBlk0222 = {
	ELX_LOG_MSG_DI_0222,
	elx_mes0222,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0223
message:  Timeout while waiting for NameServer login 
descript: Our login request to the NameServer was not acknowledged 
          within RATOV.
data:     None
severity: Error
log:      Always
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char elx_mes0223[] = "%sTimeout while waiting for NameServer login";
msgLogDef elx_msgBlk0223 = {
	ELX_LOG_MSG_DI_0223,
	elx_mes0223,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0224
message:  NameServer Query timeout
descript: Node authentication timeout, node Discovery timeout. A NameServer 
          Query to the Fabric or discovery of reported remote NPorts is not 
          acknowledged within R_A_TOV. 
data:     (1) fc_ns_retry (2) fc_max_ns_retry
severity: Error
log:      Always
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char elx_mes0224[] = "%sNameServer Query timeout Data: x%x x%x";
msgLogDef elx_msgBlk0224 = {
	ELX_LOG_MSG_DI_0224,
	elx_mes0224,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0225
message:  Device Discovery completes
descript: This indicates successful completion of device 
          (re)discovery after a link up. 
data:     None
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0225[] = "%sDevice Discovery completes";
msgLogDef elx_msgBlk0225 = {
	ELX_LOG_MSG_DI_0225,
	elx_mes0225,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0226
message:  Device Discovery completion error
descript: This indicates an uncorrectable error was encountered 
          during device (re)discovery after a link up. Fibre 
          Channel devices will not be accessible if this message 
          is displayed.
data:     None
severity: Error
log:      Always
action:   Reboot system. If problem persists, contact Technical 
          Support. Run with verbose mode on for more details.
*/
char elx_mes0226[] = "%sDevice Discovery completion error";
msgLogDef elx_msgBlk0226 = {
	ELX_LOG_MSG_DI_0226,
	elx_mes0226,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0227
message:  Node Authentication timeout
descript: The driver has lost track of what NPORTs are being authenticated.
data:     None
severity: Error
log:      Always
action:   None required. Driver should recover from this event.
*/
char elx_mes0227[] = "%sNode Authentication timeout";
msgLogDef elx_msgBlk0227 = {
	ELX_LOG_MSG_DI_0227,
	elx_mes0227,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0228
message:  CLEAR LA timeout
descript: The driver issued a CLEAR_LA that never completed
data:     None
severity: Error
log:      Always
action:   None required. Driver should recover from this event.
*/
char elx_mes0228[] = "%sCLEAR LA timeout";
msgLogDef elx_msgBlk0228 = {
	ELX_LOG_MSG_DI_0228,
	elx_mes0228,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0229
message:  Assign scsi ID <sid> to NPort <nlp_DID>
descript: The driver assigned a scsi id to a discovered mapped FCP target.
data:     (1) nlp_bind_type (2) nlp_flag (3) nlp_state (4) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0229[] = "%sAssign scsi ID x%x to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0229 = {
	ELX_LOG_MSG_DI_0229,
	elx_mes0229,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0230
message:  Cannot assign scsi ID on NPort <nlp_DID>
descript: The driver cannot assign a scsi id to a discovered mapped FCP target.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   Check persistent binding information
*/
char elx_mes0230[] = "%sCannot assign scsi ID on NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0230 = {
	ELX_LOG_MSG_DI_0230,
	elx_mes0230,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0231
message:  RSCN timeout
descript: The driver has lost track of what NPORTs have RSCNs pending.
data:     (1) fc_ns_retry (2) fc_max_ns_retry
severity: Error
log:      Always
action:   None required. Driver should recover from this event.
*/
char elx_mes0231[] = "%sRSCN timeout Data: x%x x%x";
msgLogDef elx_msgBlk0231 = {
	ELX_LOG_MSG_DI_0231,
	elx_mes0231,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0232
message:  Continue discovery with <num_disc_nodes> PLOGIs to go
descript: Device discovery is in progress
data:     (1) fc_plogi_cnt (2) fc_flag (3) phba->hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0232[] =
    "%sContinue discovery with %d PLOGIs to go Data: x%x x%x x%x";
msgLogDef elx_msgBlk0232 = {
	ELX_LOG_MSG_DI_0232,
	elx_mes0232,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0234
message:  ReDiscovery RSCN
descript: The number / type of RSCNs has forced the driver to go to 
          the nameserver and re-discover all NPORTs.
data:     (1) fc_defer_rscn.q_cnt (2) fc_flag (3) hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0234[] = "%sReDiscovery RSCN Data: x%x x%x x%x";
msgLogDef elx_msgBlk0234 = {
	ELX_LOG_MSG_DI_0234,
	elx_mes0234,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0235
message:  Deferred RSCN
descript: The driver has received multiple RSCNs and has deferred the 
          processing of the most recent RSCN.
data:     (1) fc_defer_rscn.q_cnt (2) fc_flag (3) hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0235[] = "%sDeferred RSCN Data: x%x x%x x%x";
msgLogDef elx_msgBlk0235 = {
	ELX_LOG_MSG_DI_0235,
	elx_mes0235,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0236
message:  NameServer Req
descript: The driver is issuing a nameserver request to the fabric.
data:     (1) cmdcode (2) fc_flag (3) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0236[] = "%sNameServer Req Data: x%x x%x x%x";
msgLogDef elx_msgBlk0236 = {
	ELX_LOG_MSG_DI_0236,
	elx_mes0236,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0237
message:  Pending Link Event during Discovery
descript: Received link event during discovery. Causes discovery restart.
data:     (1) hba_state (2) ulpIoTag (3) ulpStatus (4) ulpWord[4]
severity: Warning
log:      LOG_DISCOVERY verbose
action:   None required unless problem persist. If persistent check cabling.
*/
char elx_mes0237[] =
    "%sPending Link Event during Discovery Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0237 = {
	ELX_LOG_MSG_DI_0237,
	elx_mes0237,
	elx_msgPreambleDIw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0238
message:  NameServer Rsp
descript: The driver received a nameserver response.
data:     (1) Did (2) nlp_flag (3) fc_flag (4) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0238[] = "%sNameServer Rsp Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0238 = {
	ELX_LOG_MSG_DI_0238,
	elx_mes0238,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0239
message:  NameServer Rsp
descript: The driver received a nameserver response.
data:     (1) Did (2) ndlp (3) fc_flag (4) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0239[] = "%sNameServer Rsp Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0239 = {
	ELX_LOG_MSG_DI_0239,
	elx_mes0239,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0240
message:  NameServer Rsp Error
descript: The driver received a nameserver response containig a status error.
data:     (1) CommandResponse.bits.CmdRsp (2) ReasonCode (3) Explanation 
          (4) fc_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char elx_mes0240[] = "%sNameServer Rsp Error Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0240 = {
	ELX_LOG_MSG_DI_0240,
	elx_mes0240,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0241
message:  NameServer Rsp Error
descript: The driver received a nameserver response containig a status error.
data:     (1) CommandResponse.bits.CmdRsp (2) ReasonCode (3) Explanation 
          (4) fc_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char elx_mes0241[] = "%sNameServer Rsp Error Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0241 = {
	ELX_LOG_MSG_DI_0241,
	elx_mes0241,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0243
message:  Issue FDMI request failed
descript: Cannot issue FDMI request to HBA.
data:     (1) cmdcode
severity: Information
log:      LOG_Discovery verbose
action:   No action needed, informational
*/
char elx_mes0243[] = "%sIssue FDMI request failed Data: x%x";
msgLogDef elx_msgBlk0243 = {
	ELX_LOG_MSG_DI_0243,
	elx_mes0243,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0244
message:  Issue FDMI request failed
descript: Cannot issue FDMI request to HBA.
data:     (1) cmdcode
severity: Information
log:      LOG_Discovery verbose
action:   No action needed, informational
*/
char elx_mes0244[] = "%sIssue FDMI request failed Data: x%x";
msgLogDef elx_msgBlk0244 = {
	ELX_LOG_MSG_DI_0244,
	elx_mes0244,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0245
message:  ALPA based bind method used on an HBA which is in a nonloop topology
descript: ALPA based bind method used on an HBA which is not
          in a loop topology.
data:     (1) topology
severity: Warning
log:      LOG_DISCOVERY verbose
action:   Change the bind method configuration parameter of the HBA to
          1(WWNN) or 2(WWPN) or 3(DID)
*/
char elx_mes0245[] =
    "%sALPA based bind method used on an HBA which is in a nonloop topology Data: x%x";
msgLogDef elx_msgBlk0245 = {
	ELX_LOG_MSG_DI_0245,
	elx_mes0245,
	elx_msgPreambleDIw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0246
message:  RegLogin failed 
descript: Firmware returned failure for the specified RegLogin 
data:     Did, mbxStatus, hbaState 
severity: Error
log:      Always 
action:   This message indicates that the firmware could not do
          RegLogin for the specified Did. It could be because
	  there is a limitation on how many nodes an HBA can see. 
*/
char elx_mes0246[] = "%sRegLogin failed Data: x%x x%x x%x";
msgLogDef elx_msgBlk0246 = {
	ELX_LOG_MSG_DI_0246,
	elx_mes0246,
	elx_msgPreambleDIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0247
message:  Start Discovery Timer state <hba_state>
descript: Start device discovery / RSCN rescue timer
data:     (1) tmo (2) disctmo (3) fc_plogi_cnt (4) fc_adisc_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0247[] = "%sStart Discovery Timer state x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0247 = {
	ELX_LOG_MSG_DI_0247,
	elx_mes0247,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0248
message:  Cancel Discovery Timer state <hba_state>
descript: Cancel device discovery / RSCN rescue timer
data:     (1) fc_flag (2) rc (3) fc_plogi_cnt (4) fc_adisc_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char elx_mes0248[] = "%sCancel Discovery Timer state x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0248 = {
	ELX_LOG_MSG_DI_0248,
	elx_mes0248,
	elx_msgPreambleDIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin MAILBOX LOG Message Structures
 */

/*
msgName: elx_mes0300
message:  READ_LA: no buffers
descript: The driver attempted to issue READ_LA mailbox command to the HBA
          but there were no buffer available.
data:     None
severity: Warning
log:      LOG_MBOX verbose
action:   This message indicates (1) a possible lack of memory resources. Try 
          increasing the lpfc 'num_bufs' configuration parameter to allocate 
          more buffers. (2) A possible driver buffer management problem. If 
          this problem persists, report these errors to Technical Support.
*/
char elx_mes0300[] = "%sREAD_LA: no buffers";
msgLogDef elx_msgBlk0300 = {
	ELX_LOG_MSG_MB_0300,
	elx_mes0300,
	elx_msgPreambleMBw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0301
message:  READ_SPARAM: no buffers
descript: The driver attempted to issue READ_SPARAM mailbox command to the 
          HBA but there were no buffer available.
data:     None
severity: Warning
log:      LOG_MBOX verbose
action:   This message indicates (1) a possible lack of memory resources. Try 
          increasing the lpfc 'num_bufs' configuration parameter to allocate 
          more buffers. (2) A possible driver buffer management problem. If 
          this problem persists, report these errors to Technical Support.
*/
char elx_mes0301[] = "%sREAD_SPARAM: no buffers";
msgLogDef elx_msgBlk0301 = {
	ELX_LOG_MSG_MB_0301,
	elx_mes0301,
	elx_msgPreambleMBw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0302
message:  REG_LOGIN: no buffers
descript: The driver attempted to issue REG_LOGIN mailbox command to the HBA
          but there were no buffer available.
data:     None
severity: Warning
log:      LOG_MBOX verbose
action:   This message indicates (1) a possible lack of memory resources. Try 
          increasing the lpfc 'num_bufs' configuration parameter to allocate 
          more buffers. (2) A possible driver buffer management problem. If 
          this problem persists, report these errors to Technical Support.
*/
char elx_mes0302[] = "%sREG_LOGIN: no buffers Data x%x x%x";
msgLogDef elx_msgBlk0302 = {
	ELX_LOG_MSG_MB_0302,
	elx_mes0302,
	elx_msgPreambleMBw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0304
message:  Stray Mailbox Interrupt, mbxCommand <cmd> mbxStatus <status>.
descript: Received a mailbox completion interrupt and there are no 
          outstanding mailbox commands.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0304[] = "%sStray Mailbox Interrupt mbxCommand x%x mbxStatus x%x";
msgLogDef elx_msgBlk0304 = {
	ELX_LOG_MSG_MB_0304,
	elx_mes0304,
	elx_msgPreambleMBe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0305
message:  Mbox cmd cmpl error - RETRYing
descript: A mailbox command completed with an error status that causes the 
          driver to reissue the mailbox command.
data:     (1) mbxCommand (2) mbxStatus (3) word1 (4) hba_state
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char elx_mes0305[] = "%sMbox cmd cmpl error - RETRYing Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0305 = {
	ELX_LOG_MSG_MB_0305,
	elx_mes0305,
	elx_msgPreambleMBi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0306
message:  CONFIG_LINK mbxStatus error <mbxStatus> HBA state <hba_state>
descript: The driver issued a CONFIG_LINK mbox command to the HBA that failed.
data:     none
severity: Error
log:      Always
action:   This error could indicate a firmware or hardware
          problem. Report these errors to Technical Support.
*/
char elx_mes0306[] = "%sCONFIG_LINK mbxStatus error x%x HBA state x%x";
msgLogDef elx_msgBlk0306 = {
	ELX_LOG_MSG_MB_0306,
	elx_mes0306,
	elx_msgPreambleMBe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0307
message:  Mailbox Cmpl, wd0 <pmbox> wd1 <varWord> wd2 <varWord> cmpl <mbox_cmpl)
descript: A mailbox command completed.. 
data:     none
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char elx_mes0307[] = "%sMailbox Cmpl, wd0 x%x wd1 x%x wd2 x%x cmpl x%lx";
msgLogDef elx_msgBlk0307 = {
	ELX_LOG_MSG_MB_0307,
	elx_mes0307,
	elx_msgPreambleMBi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0308
message:  Mbox cmd issue - BUSY
descript: The driver attempted to issue a mailbox command while the mailbox 
          was busy processing the previous command. The processing of the 
          new command will be deferred until the mailbox becomes available.
data:     (1) mbxCommand (2) hba_state (3) sli_flag (4) flag
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char elx_mes0308[] = "%sMbox cmd issue - BUSY Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0308 = {
	ELX_LOG_MSG_MB_0308,
	elx_mes0308,
	elx_msgPreambleMBi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0309
message:  Mailbox cmd <cmd> issue
descript: The driver is in the process of issuing a mailbox command.
data:     (1) hba_state (2) sli_flag (3) flag
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char elx_mes0309[] = "%sMailbox cmd x%x issue Data: x%x x%x x%x";
msgLogDef elx_msgBlk0309 = {
	ELX_LOG_MSG_MB_0309,
	elx_mes0309,
	elx_msgPreambleMBi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0310
message:  Mailbox command <cmd> timeout
descript: A Mailbox command was posted to the adapter and did 
          not complete within 30 seconds.
data:     (1) hba_state (2) sli_flag (3) mbox_active
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If no I/O is going through the adapter, reboot 
          the system. If these problems persist, report these 
          errors to Technical Support.
*/
char elx_mes0310[] = "%sMailbox command x%x timeout Data: x%x x%x x%x";
msgLogDef elx_msgBlk0310 = {
	ELX_LOG_MSG_MB_0310,
	elx_mes0310,
	elx_msgPreambleMBe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_TIMEOUT
};

/*
msgName: elx_mes0311
message:  Mailbox command <cmd> cannot issue
descript: Driver is in the wrong state to issue the specified command
data:     (1) hba_state (2) sli_flag (3) flag
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char elx_mes0311[] = "%sMailbox command x%x cannot issue Data: x%x x%x x%x";
msgLogDef elx_msgBlk0311 = {
	ELX_LOG_MSG_MB_0311,
	elx_mes0311,
	elx_msgPreambleMBi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0312
message:  Ring <ringno> handler: portRspPut <portRspPut> is bigger then rsp ring <portRspMax> 
descript: Port rsp ring put index is > size of rsp ring
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver, firmware or hardware
          problem. Report these errors to Technical Support.
*/
char elx_mes0312[] =
    "%sRing %d handler: portRspPut %d is bigger then rsp ring %d";
msgLogDef elx_msgBlk0312 = {
	ELX_LOG_MSG_MB_0312,
	elx_mes0312,
	elx_msgPreambleSLe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0313
message:  Ring <ringno> handler: unexpected Rctl <Rctl> Type <Type> received 
descript: The Rctl/Type of a received frame did not match any for the configured masks
          for the specified ring.           
data:     None
severity: Warning
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char elx_mes0313[] =
    "%sRing %d handler: unexpected Rctl x%x Type x%x received ";
msgLogDef elx_msgBlk0313 = {
	ELX_LOG_MSG_MB_0313,
	elx_mes0313,
	elx_msgPreambleSLw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0314
message:  Ring <ringno> issue: portCmdGet <portCmdGet> is bigger then cmd ring <portCmdMax> 
descript: Port cmd ring get index is > size of cmd ring
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver, firmware or hardware
          problem. Report these errors to Technical Support.
*/
char elx_mes0314[] =
    "%sRing %d issue: portCmdGet %d is bigger then cmd ring %d";
msgLogDef elx_msgBlk0314 = {
	ELX_LOG_MSG_MB_0314,
	elx_mes0314,
	elx_msgPreambleSLe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0315
message:  Ring <ringno> issue: portCmdGet <portCmdGet> is bigger then cmd ring <portCmdMax> 
descript: Port cmd ring get index is > size of cmd ring
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char elx_mes0315[] =
    "%sRing %d issue: portCmdGet %d is bigger then cmd ring %d";
msgLogDef elx_msgBlk0315 = {
	ELX_LOG_MSG_MB_0315,
	elx_mes0315,
	elx_msgPreambleSLe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0316
message:  Cmd ring <ringno> put: iotag <iotag> greater then configured max <fast_iotag> wd0 <icmd>
descript: The assigned I/O iotag is > the max allowed
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver
          problem. If problems persist report these errors to 
          Technical Support.
*/
char elx_mes0316[] =
    "%sCmd ring %d put: iotag x%x greater then configured max x%x wd0 x%x";
msgLogDef elx_msgBlk0316 = {
	ELX_LOG_MSG_MB_0316,
	elx_mes0316,
	elx_msgPreambleSLe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0317
message:  Rsp ring <ringno> get: iotag <iotag> greater then configured max <fast_iotag> wd0 <irsp>
descript: The assigned I/O iotag is > the max allowed
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver
          problem. If problems persist report these errors to 
          Technical Support.
*/
char elx_mes0317[] =
    "%sRsp ring %d get: iotag x%x greater then configured max x%x wd0 x%x";
msgLogDef elx_msgBlk0317 = {
	ELX_LOG_MSG_MB_0317,
	elx_mes0317,
	elx_msgPreambleSLe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0318
message:  Outstanding I/O count for ring <ringno> is at max <fast_iotag>
descript: We cannot assign an I/O tag because none are available. Max allowed I/Os
          are currently outstanding.
data:     None
severity: Information
log:      LOG_SLI verbose
action:   This message indicates the adapter hba I/O queue is full. 
          Typically this happens if you are running heavy I/O on a
	  low-end (3 digit) adapter. Suggest you upgrade to our high-end
	  adapter.
*/
char elx_mes0318[] = "%sOutstanding I/O count for ring %d is at max x%x";
msgLogDef elx_msgBlk0318 = {
	ELX_LOG_MSG_MB_0318,
	elx_mes0318,
	elx_msgPreambleSLi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0319
descript: The driver issued a READ_SPARAM mbox command to the HBA that failed.
data:     none
severity: Error
log:      Always
action:   This error could indicate a firmware or hardware
          problem. Report these errors to Technical Support.
*/
char elx_mes0319[] = "%sREAD_SPARAM mbxStatus error x%x hba state x%x>";
msgLogDef elx_msgBlk0319 = {
	ELX_LOG_MSG_MB_0319,
	elx_mes0319,
	elx_msgPreambleMBe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0320
message:  CLEAR_LA mbxStatus error <mbxStatus> hba state <hba_state>
descript: The driver issued a CLEAR_LA mbox command to the HBA that failed.
data:     none
severity: Error
log:      Always
action:   This error could indicate a firmware or hardware
          problem. Report these errors to Technical Support.
*/
char elx_mes0320[] = "%sCLEAR_LA mbxStatus error x%x hba state x%x";
msgLogDef elx_msgBlk0320 = {
	ELX_LOG_MSG_MB_0320,
	elx_mes0320,
	elx_msgPreambleMBe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0321
message:  Unknown IOCB command
descript: Received an unknown IOCB command completion.
data:     (1) ulpCommand (2) ulpStatus (3) ulpIoTag (4) ulpContext)
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If these problems persist, report these errors 
          to Technical Support.
*/
char elx_mes0321[] = "%sUnknown IOCB command Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0321 = {
	ELX_LOG_MSG_MB_0321,
	elx_mes0321,
	elx_msgPreambleSLe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0322
message:  Ring <ringno> handler: unexpected completion IoTag <IoTag>
descript: The driver could not find a matching command for the completion
          received on the specified ring.           
data:     (1) ulpStatus (2) ulpWord[4] (3) ulpCommand (4) ulpContext
severity: Warning
log:      LOG_SLI verbose
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char elx_mes0322[] =
    "%sRing %d handler: unexpected completion IoTag x%x Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0322 = {
	ELX_LOG_MSG_MB_0322,
	elx_mes0322,
	elx_msgPreambleSLw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0323
message:  Unknown Mailbox command <cmd> Cmpl 
descript: A unknown mailbox command completed.. 
data:     (1) Mailbox Command
severity: Error
log:      Always
action:   This error could indicate a software driver, firmware or hardware
          problem. Report these errors to Technical Support.
*/
char elx_mes0323[] = "%sUnknown Mailbox command %x Cmpl";
msgLogDef elx_msgBlk0323 = {
	ELX_LOG_MSG_MB_0323,
	elx_mes0323,
	elx_msgPreambleMBe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0324
message:  Adapter initialization error, mbxCmd <cmd> READ_NVPARM, mbxStatus <status>
descript: A read nvparams mailbox command failed during config port.
data:     (1) Mailbox Command (2) Mailbox Command Status
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0324[] =
    "%sConfig Port initialization error, mbxCmd x%x READ_NVPARM, mbxStatus x%x";
msgLogDef elx_msgBlk0324 = {
	ELX_LOG_MSG_MB_0324,
	elx_mes0324,
	elx_msgPreambleMBe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin INIT LOG Message Structures
 */

/*
msgName: elx_mes0405
message:  Service Level Interface (SLI) 2 selected
descript: A CONFIG_PORT (SLI2) mailbox command was issued. 
data:     None
severity: Information
log:      LOG_INIT verbose
action:   No action needed, informational
*/
char elx_mes0405[] = "%sService Level Interface (SLI) 2 selected";
msgLogDef elx_msgBlk0405 = {
	ELX_LOG_MSG_IN_0405,
	elx_mes0405,
	elx_msgPreambleINi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_INIT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0406
message:  Memory Buffer Pool is below low water mark
descript: A driver memory buffer pool is low on buffers. 
data:     (1) seg (2) fc_lowmem (3) low
severity: Warning
log:      LOG_INIT verbose
action:   None required. Driver will recover as buffers are returned to pool.
*/
char elx_mes0406[] =
    "%sMemory Buffer Pool is below low water mark Data x%x x%x x%x";
msgLogDef elx_msgBlk0406 = {
	ELX_LOG_MSG_IN_0406,
	elx_mes0406,
	elx_msgPreambleINw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_INIT,
	ERRID_LOG_NO_RESOURCE
};

/*
msgName: elx_mes0407
message:  Memory Buffer Pool is at upper limit.
descript: A memory buffer pool cannot add more buffers because
          it is at its himem value. 
data:     (1) seg (2) q_cnt (3) himem
severity: Error
log:      Always
action:   None required. Driver will recover as buffers are returned to pool.
*/
char elx_mes0407[] =
    "%sMemory Buffer Pool is at its high water mark Data x%x x%x x%x";
msgLogDef elx_msgBlk0407 = {
	ELX_LOG_MSG_IN_0407,
	elx_mes0407,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_NO_RESOURCE
};

/*
msgName: elx_mes0409
message:  Memory Buffer Pool is out of buffers
descript: A driver memory buffer pool is exhausted.
data:     (1) seg (2) fc_free (3) fc_mbox.q_cnt (4) fc_memhi
severity: Error
log:      Always
action:   Configure more resources for that buffer pool. If 
          problems persist report these errors to Technical 
          Support.
*/
char elx_mes0409[] = "%sMemory Buffer Pool is out of buffers Data x%x x%x x%x";
msgLogDef elx_msgBlk0409 = {
	ELX_LOG_MSG_IN_0409,
	elx_mes0409,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_NO_RESOURCE
};

/*
msgName: elx_mes0410
message:  Cannot find virtual addr for mapped buf on ring <num>
descript: The driver cannot find the specified buffer in its 
          mapping table. Thus it cannot find the virtual address 
          needed to access the data.
data:     (1) first (2) q_first (3) q_last (4) q_cnt
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char elx_mes0410[] =
    "%sCannot find virtual addr for mapped buf on ring %d Data x%x x%x x%x x%x";
msgLogDef elx_msgBlk0410 = {
	ELX_LOG_MSG_IN_0410,
	elx_mes0410,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_NO_RESOURCE
};

/*
msgName: elx_mes0411
message:  fcp_bind_method is 4 with Persistent binding - ignoring fcp_bind_method
descript: The configuration parameter for fcp_bind_method conflicts with 
          Persistent binding parameter.
data:     (1) a_current (2) fcp_mapping
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char elx_mes0411[] =
    "%sfcp_bind_method is 4 with Persistent binding - ignoring fcp_bind_method Data: x%x x%x";
msgLogDef elx_msgBlk0411 = {
	ELX_LOG_MSG_IN_0411,
	elx_mes0411,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0412
message:  Scan-down is out of range - ignoring scan-down
descript: The configuration parameter for Scan-down is out of range.
data:     (1) clp[CFG_SCAN_DOWN].a_current (2) fcp_mapping
severity: Error
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char elx_mes0412[] =
    "%sScan-down is out of range - ignoring scan-down Data: x%x x%x";
msgLogDef elx_msgBlk0412 = {
	ELX_LOG_MSG_IN_0412,
	elx_mes0412,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0413
message:  Configuration parameter out of range, resetting to default value
descript: User is attempting to set a configuration parameter to a value not 
          supported by the driver. Resetting the configuration parameter to the
          default value.
data:     (1) a_string (2) a_low (3) a_hi (4) a_default
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char elx_mes0413[] =
    "%sConfiguration parameter lpfc_%s out of range [%d,%d]. Using default value %d";
msgLogDef elx_msgBlk0413 = {
	ELX_LOG_MSG_IN_0413,
	elx_mes0413,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0430
message:  WWPN binding entry <num>: Syntax error code <code>
descript: A syntax error occured while parsing WWPN binding 
          configuration information.
data:     None
detail:   Binding syntax error codes
          0  FC_SYNTAX_OK
          1  FC_SYNTAX_OK_BUT_NOT_THIS_BRD
          2  FC_SYNTAX_ERR_ASC_CONVERT
          3  FC_SYNTAX_ERR_EXP_COLON
          4  FC_SYNTAX_ERR_EXP_LPFC
          5  FC_SYNTAX_ERR_INV_LPFC_NUM
          6  FC_SYNTAX_ERR_EXP_T
          7  FC_SYNTAX_ERR_INV_TARGET_NUM
          8  FC_SYNTAX_ERR_EXP_D
          9  FC_SYNTAX_ERR_INV_DEVICE_NUM
          10 FC_SYNTAX_ERR_INV_RRATIO_NUM
          11 FC_SYNTAX_ERR_EXP_NULL_TERM
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char elx_mes0430[] = "%sWWPN binding entry %d: Syntax error code %d";
msgLogDef elx_msgBlk0430 = {
	ELX_LOG_MSG_IN_0430,
	elx_mes0430,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0431
message:  WWNN binding entry <num>: Syntax error code <code>
descript: A syntax error occured while parsing WWNN binding 
          configuration information.
data:     None
detail:   Binding syntax error codes
          0  FC_SYNTAX_OK
          1  FC_SYNTAX_OK_BUT_NOT_THIS_BRD
          2  FC_SYNTAX_ERR_ASC_CONVERT
          3  FC_SYNTAX_ERR_EXP_COLON
          4  FC_SYNTAX_ERR_EXP_LPFC
          5  FC_SYNTAX_ERR_INV_LPFC_NUM
          6  FC_SYNTAX_ERR_EXP_T
          7  FC_SYNTAX_ERR_INV_TARGET_NUM
          8  FC_SYNTAX_ERR_EXP_D
          9  FC_SYNTAX_ERR_INV_DEVICE_NUM
          10 FC_SYNTAX_ERR_INV_RRATIO_NUM
          11 FC_SYNTAX_ERR_EXP_NULL_TERM
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char elx_mes0431[] = "%sWWNN binding entry %d: Syntax error code %d";
msgLogDef elx_msgBlk0431 = {
	ELX_LOG_MSG_IN_0431,
	elx_mes0431,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0432
message:  WWPN binding entry: node table full
descript: More bindings entries were configured than the driver can handle. 
data:     None
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file such that 
          fewer bindings are configured.
*/
char elx_mes0432[] = "%sWWPN binding entry: node table full";
msgLogDef elx_msgBlk0432 = {
	ELX_LOG_MSG_IN_0432,
	elx_mes0432,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0433
message:  WWNN binding entry: node table full
descript: More bindings entries were configured than the driver can handle. 
data:     None
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file such that 
          fewer bindings are configured.
*/
char elx_mes0433[] = "%sWWNN binding entry: node table full";
msgLogDef elx_msgBlk0433 = {
	ELX_LOG_MSG_IN_0433,
	elx_mes0433,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0434
message:  DID binding entry <num>: Syntax error code <code>
descript: A syntax error occured while parsing DID binding 
          configuration information.
data:     None
detail:   Binding syntax error codes
          0  FC_SYNTAX_OK
          1  FC_SYNTAX_OK_BUT_NOT_THIS_BRD
          2  FC_SYNTAX_ERR_ASC_CONVERT
          3  FC_SYNTAX_ERR_EXP_COLON
          4  FC_SYNTAX_ERR_EXP_LPFC
          5  FC_SYNTAX_ERR_INV_LPFC_NUM
          6  FC_SYNTAX_ERR_EXP_T
          7  FC_SYNTAX_ERR_INV_TARGET_NUM
          8  FC_SYNTAX_ERR_EXP_D
          9  FC_SYNTAX_ERR_INV_DEVICE_NUM
          10 FC_SYNTAX_ERR_INV_RRATIO_NUM
          11 FC_SYNTAX_ERR_EXP_NULL_TERM
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char elx_mes0434[] = "%sDID binding entry %d: Syntax error code %d";
msgLogDef elx_msgBlk0434 = {
	ELX_LOG_MSG_IN_0434,
	elx_mes0434,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0435
message:  DID binding entry: node table full
descript: More bindings entries were configured than the driver can handle. 
data:     None
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file such that 
          fewer bindings are configured.
*/
char elx_mes0435[] = "%sDID binding entry: node table full";
msgLogDef elx_msgBlk0435 = {
	ELX_LOG_MSG_IN_0435,
	elx_mes0435,
	elx_msgPreambleINc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0436
message:  Adapter failed to init, timeout, status reg <status>
descript: The adapter failed during powerup diagnostics after it was reset.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0436[] = "%sAdapter failed to init, timeout, status reg x%x";
msgLogDef elx_msgBlk0436 = {
	ELX_LOG_MSG_IN_0436,
	elx_mes0436,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0437
message:  Adapter failed to init, chipset, status reg <status>
descript: The adapter failed during powerup diagnostics after it was reset.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0437[] = "%sAdapter failed to init, chipset, status reg x%x";
msgLogDef elx_msgBlk0437 = {
	ELX_LOG_MSG_IN_0437,
	elx_mes0437,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0438
message:  Adapter failed to init, chipset, status reg <status>
descript: The adapter failed during powerup diagnostics after it was reset.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0438[] = "%sAdapter failed to init, chipset, status reg x%x";
msgLogDef elx_msgBlk0438 = {
	ELX_LOG_MSG_IN_0438,
	elx_mes0438,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0439
message:  Adapter failed to init, mbxCmd <cmd> READ_REV, mbxStatus <status>
descript: Adapter initialization failed when issuing READ_REV mailbox command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0439[] =
    "%sAdapter failed to init, mbxCmd x%x READ_REV, mbxStatus x%x";
msgLogDef elx_msgBlk0439 = {
	ELX_LOG_MSG_IN_0439,
	elx_mes0439,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0440
message:  Adapter failed to init, mbxCmd <cmd> READ_REV detected outdated firmware
descript: Outdated firmware was detected during initialization. 
data:     (1) read_rev_reset
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. Update 
          firmware. If problems persist report these errors to Technical 
          Support.
*/
char elx_mes0440[] =
    "%sAdapter failed to init, mbxCmd x%x READ_REV detected outdated firmware Data: x%x";
msgLogDef elx_msgBlk0440 = {
	ELX_LOG_MSG_IN_0440,
	elx_mes0440,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0441
message:  VPD not present on adapter, mbxCmd <cmd> DUMP VPD, mbxStatus <status>
descript: DUMP_VPD mailbox command failed.
data:     None
severity: Information
log:      LOG_INIT verbose
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these to Technical Support.
*/
char elx_mes0441[] =
    "%sVPD not present on adapter, mbxCmd x%x DUMP VPD, mbxStatus x%x";
msgLogDef elx_msgBlk0441 = {
	ELX_LOG_MSG_IN_0441,
	elx_mes0441,
	elx_msgPreambleINi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0442
message:  Adapter failed to init, mbxCmd <cmd> CONFIG_PORT, mbxStatus <status>
descript: Adapter initialization failed when issuing CONFIG_PORT mailbox 
          command.
data:     (1) hbainit
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0442[] =
    "%sAdapter failed to init, mbxCmd x%x CONFIG_PORT, mbxStatus x%x Data: x%x";
msgLogDef elx_msgBlk0442 = {
	ELX_LOG_MSG_IN_0442,
	elx_mes0442,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0446
message:  Adapter failed to init, mbxCmd <cmd> CFG_RING, mbxStatus <status>, ring <num>
descript: Adapter initialization failed when issuing CFG_RING mailbox command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0446[] =
    "%sAdapter failed to init, mbxCmd x%x CFG_RING, mbxStatus x%x, ring %d";
msgLogDef elx_msgBlk0446 = {
	ELX_LOG_MSG_IN_0446,
	elx_mes0446,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0447
message:  Adapter failed init, mbxCmd <cmd> CONFIG_LINK mbxStatus <status>
descript: Adapter initialization failed when issuing CONFIG_LINK mailbox 
          command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0447[] =
    "%sAdapter failed init, mbxCmd x%x CONFIG_LINK mbxStatus x%x";
msgLogDef elx_msgBlk0447 = {
	ELX_LOG_MSG_IN_0447,
	elx_mes0447,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0448
message:  Adapter failed to init, mbxCmd <cmd> READ_SPARM mbxStatus <status>
descript: Adapter initialization failed when issuing READ_SPARM mailbox 
          command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0448[] =
    "%sAdapter failed init, mbxCmd x%x READ_SPARM mbxStatus x%x";
msgLogDef elx_msgBlk0448 = {
	ELX_LOG_MSG_IN_0448,
	elx_mes0448,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0449
message:  WorldWide PortName Type <type> doesn't conform to IP Profile
descript: In order to run IP, the WorldWide PortName must be of type 
          IEEE (NAA = 1). This message displays if the adapter WWPN 
          doesn't conform with the standard.
data:     None
severity: Error
log:      Always
action:   Turn off the network-on configuration parameter or configure 
          a different WWPN.
*/
char elx_mes0449[] =
    "%sWorldWide PortName Type x%x doesn't conform to IP Profile";
msgLogDef elx_msgBlk0449 = {
	ELX_LOG_MSG_IN_0449,
	elx_mes0449,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0450
message:  Adapter failed to init, mbxCmd <cmd> FARP, mbxStatus <status> 
descript: Adapter initialization failed when issuing FARP mailbox command.
data:     None
severity: Warning
log:      LOG_INIT verbose
action:   None required
*/
char elx_mes0450[] = "%sAdapter failed to init, mbxCmd x%x FARP, mbxStatus x%x";
msgLogDef elx_msgBlk0450 = {
	ELX_LOG_MSG_IN_0450,
	elx_mes0450,
	elx_msgPreambleINw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0451
message:  Enable interrupt handler failed
descript: The driver attempted to register the HBA interrupt service 
          routine with the host operating system but failed.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or driver problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0451[] = "%sEnable interrupt handler failed";
msgLogDef elx_msgBlk0451 = {
	ELX_LOG_MSG_IN_0451,
	elx_mes0451,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0453
message:  Adapter failed to init, mbxCmd <cmd> READ_CONFIG, mbxStatus <status>
descript: Adapter initialization failed when issuing READ_CONFIG mailbox 
          command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0453[] =
    "%sAdapter failed to init, mbxCmd x%x READ_CONFIG, mbxStatus x%x";
msgLogDef elx_msgBlk0453 = {
	ELX_LOG_MSG_IN_0453,
	elx_mes0453,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0454
message:  Adapter failed to init, mbxCmd <cmd> INIT_LINK, mbxStatus <status>
descript: Adapter initialization failed when issuing INIT_LINK mailbox command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0454[] =
    "%sAdapter failed to init, mbxCmd x%x INIT_LINK, mbxStatus x%x";
msgLogDef elx_msgBlk0454 = {
	ELX_LOG_MSG_IN_0454,
	elx_mes0454,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0455
message:  Vital Product
descript: Vital Product Data (VPD) contained in HBA flash.
data:     (1) vpd[0] (2) vpd[1] (3) vpd[2] (4) vpd[3]
severity: Information
log:      LOG_INIT verbose
action:   No action needed, informational
*/
char elx_mes0455[] = "%sVital Product Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0455 = {
	ELX_LOG_MSG_IN_0455,
	elx_mes0455,
	elx_msgPreambleINi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0457
message:  Adapter Hardware Error
descript: The driver received an interrupt indicting a possible hardware 
          problem.
data:     (1) status (2) status1 (3) status2
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char elx_mes0457[] = "%sAdapter Hardware Error Data: x%x x%x x%x";
msgLogDef elx_msgBlk0457 = {
	ELX_LOG_MSG_IN_0457,
	elx_mes0457,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: elx_mes0458
message:  Bring Adapter online
descript: The FC driver has received a request to bring the adapter 
          online. This may occur when running lputil.
data:     None
severity: Warning
log:      LOG_INIT verbose
action:   None required
*/
char elx_mes0458[] = "%sBring Adapter online";
msgLogDef elx_msgBlk0458 = {
	ELX_LOG_MSG_IN_0458,
	elx_mes0458,
	elx_msgPreambleINw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_INIT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0460
message:  Bring Adapter offline
descript: The FC driver has received a request to bring the adapter 
          offline. This may occur when running lputil.
data:     None
severity: Warning
log:      LOG_INIT verbose
action:   None required
*/
char elx_mes0460[] = "%sBring Adapter offline";
msgLogDef elx_msgBlk0460 = {
	ELX_LOG_MSG_IN_0460,
	elx_mes0460,
	elx_msgPreambleINw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_INIT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0462
message:  Too many cmd / rsp ring entries in SLI2 SLIM
descript: The configuration parameter for Scan-down is out of range.
data:     (1) totiocb (2) MAX_SLI2_IOCB
severity: Error
log:      Always
action:   Software driver error.
          If this problem persists, report these errors to Technical Support.
*/
char elx_mes0462[] =
    "%sToo many cmd / rsp ring entries in SLI2 SLIM Data: x%x x%x";
msgLogDef elx_msgBlk0462 = {
	ELX_LOG_MSG_IN_0462,
	elx_mes0462,
	elx_msgPreambleINe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
 *  Begin IP LOG Message Structures
 */

/*
msgName: elx_mes0600
message:  FARP-RSP received from DID <did>.
descript: A FARP ELS command response was received.
data:     None
severity: Information
log:      LOG_IP verbose
action:   No action needed, informational
*/
char elx_mes0600[] = "%sFARP-RSP received from DID x%x";
msgLogDef elx_msgBlk0600 = {
	ELX_LOG_MSG_IP_0600,
	elx_mes0600,
	elx_msgPreambleIPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0601
message:  FARP-REQ received from DID <did>
descript: A FARP ELS command request was received.
data:     None
severity: Information
log:      LOG_IP verbose
action:   No action needed, informational
*/
char elx_mes0601[] = "%sFARP-REQ received from DID x%x";
msgLogDef elx_msgBlk0601 = {
	ELX_LOG_MSG_IP_0601,
	elx_mes0601,
	elx_msgPreambleIPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0602
message:  IP Response Ring <num> out of posted buffers
descript: The IP ring returned all posted buffers to the driver 
          and is waiting for the driver to post new buffers. This 
          could mean the host system is out of TCP/IP buffers. 
data:     (1) fc_missbufcnt (2) NoRcvBuf
severity: Warning
log:      LOG_IP verbose
action:   Try allocating more IP buffers (STREAMS buffers or mbufs) 
          of size 4096 and/or increasing the post-ip-buf lpfc 
          configuration parameter. Reboot the system.
*/
char elx_mes0602[] =
    "%sIP Response Ring %d out of posted buffers Data: x%x x%x";
msgLogDef elx_msgBlk0602 = {
	ELX_LOG_MSG_IP_0602,
	elx_mes0602,
	elx_msgPreambleIPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_IP,
	ERRID_LOG_NO_RESOURCE
};

/*
msgName: elx_mes0603
message:  Xmit Sequence completion error
descript: A XMIT_SEQUENCE command completed with a status error 
          in the IOCB.
data:     (1) ulpStatus (2) ulpIoTag (3) ulpWord[4] (4) did
severity: Warning
log:      LOG_IP verbose
action:   If there are many errors to one device, check physical 
          connections to Fibre Channel network and the state of 
          the remote PortID.  The driver attempts to recover by 
          creating a new exchange to the remote device.
*/
char elx_mes0603[] = "%sXmit Sequence completion error Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0603 = {
	ELX_LOG_MSG_IP_0603,
	elx_mes0603,
	elx_msgPreambleIPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0604
message:  Post buffer for IP ring <num> failed
descript: The driver cannot allocate a buffer to post to the IP ring. 
          This usually means the host system is out of TCP/IP buffers. 
data:     (1) missbufcnt
severity: Error
log:      Always
action:   Try allocating more IP buffers (STREAMS buffers or mbufs) 
          of size 4096. Reboot the system.
*/
char elx_mes0604[] = "%sPost buffer for IP ring %d failed Data: x%x";
msgLogDef elx_msgBlk0604 = {
	ELX_LOG_MSG_IP_0604,
	elx_mes0604,
	elx_msgPreambleIPe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_IP,
	ERRID_LOG_NO_RESOURCE
};

/*
msgName: elx_mes0605
message:  No room on IP xmit queue
descript: The system is generating IOCB commands to be processed 
          faster than the adapter can process them. 
data:     (1) xmitnoroom
severity: Warning
log:      LOG_IP verbose
action:   Check the state of the link. If the link is up and running, 
          reconfigure the xmit queue size to be larger. Note, a larger 
          queue size may require more system IP buffers. If the link 
          is down, check physical connections to Fibre Channel network.
*/
char elx_mes0605[] = "%sNo room on IP xmit queue Data: x%x";
msgLogDef elx_msgBlk0605 = {
	ELX_LOG_MSG_IP_0605,
	elx_mes0605,
	elx_msgPreambleIPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0606
message:  XRI Create for IP traffic to DID <did>.
descript: The lpfc driver is missing an exchange resource identifier
          (XRI) for this node and needs to create one prior to 
          the transmit operation.
data:     None
severity: Information
log:      LOG_IP verbose
action:   No action needed, informational
*/
char elx_mes0606[] = "%sXRI Create for IP traffic to DID x%x";
msgLogDef elx_msgBlk0606 = {
	ELX_LOG_MSG_IP_0606,
	elx_mes0606,
	elx_msgPreambleIPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0607
message:  XRI response from DID with XRI <xri> and status <ulpStatus>
descript: The driver received an XRI response from SLI with the resulting
          exchange resource id and status.
data:     None
severity: Information
log:      LOG_IP verbose
action:   No action needed, informational
*/
char elx_mes0607[] = "%sXRI response from DID x%x with XRI x%x and status x%x";
msgLogDef elx_msgBlk0607 = {
	ELX_LOG_MSG_IP_0607,
	elx_mes0607,
	elx_msgPreambleIPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0608
message:  IP packet timed out 
descript: An IP IOCB command was posted to a ring and did not complete
          within timeout seconds.
data:     (1) Did
severity: Warning
log:      LOG_IP verbose
action:   If no IP packet is going through the adapter, reboot the system;
          If problem persists, contact Technical Support.
*/
char elx_mes0608[] = "%sIP packet timed out Data: x%x";
msgLogDef elx_msgBlk0608 = {
	ELX_LOG_MSG_IP_0608,
	elx_mes0608,
	elx_msgPreambleIPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0609
message:  Network buffer and DMA address mismatch
descript: An IP buffer free operation found a mismatch between an
          IP buffer and its dma address.
data:     (1) pib (2) ip buff found (3) ip buf actual (4) dma address
severity: Error
log:      Always
action:   Stop traffic and reboot the system.
*/
char elx_mes0609[] =
    "%sIP buffer-DMA address mismatch Data: x%llx x%llx x%llx x%llx";
msgLogDef elx_msgBlk0609 = {
	ELX_LOG_MSG_IP_0609,
	elx_mes0609,
	elx_msgPreambleIPe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0610
message:  FARP Request sent to remote DID
descript: A send to a remote IP address has no node in the driver's nodelists.
          Send a FARP request to obtain the node's HW address.
data:     (1) IEEE[0] (2) IEEE[1] (3) IEEE[2] (4) IEEE[3] (5) IEEE[4] (6) IEEE[5] 
severity: Information
log:      LOG_IP verbose
action:   Issue FARP and wait for PLOGI from remote node.
*/
char elx_mes0610[] =
    "%sFARP Request sent to remote HW Address %02x-%02x-%02x-%02x-%02x-%02x";
msgLogDef elx_msgBlk0610 = {
	ELX_LOG_MSG_IP_0610,
	elx_mes0610,
	elx_msgPreambleIPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin FCP LOG Message Structures
 */

/*
msgName: elx_mes0700
message:  Start nodev timer
descript: A target disappeared from the Fibre Channel network. If the 
          target does not return within nodev-tmo timeout all I/O to 
          the target will fail.
data:     (1) nlp_DID (2) nlp_flag (3) nlp_state (4) nlp
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0700[] = "%sStart nodev timer Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0700 = {
	ELX_LOG_MSG_FP_0700,
	elx_mes0700,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0701
message:  Issue Abort Task Set to TGT <num> LUN <num>
descript: The SCSI layer detected that it needs to abort all I/O 
          to a specific device. This results in an FCP Task 
          Management command to abort the I/O in progress. 
data:     (1) rpi (2) flags
severity: Information
log:      LOG_FCP verbose
action:   Check state of device in question. 
*/
char elx_mes0701[] = "%sIssue Abort Task Set to TGT %d LUN %d Data: x%x x%x";
msgLogDef elx_msgBlk0701 = {
	ELX_LOG_MSG_FP_0701,
	elx_mes0701,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0702
message:  Issue Target Reset to TGT <num>
descript: The SCSI layer detected that it needs to abort all I/O 
          to a specific target. This results in an FCP Task 
          Management command to abort the I/O in progress. 
data:     (1) rpi (2) flags
severity: Information
log:      LOG_FCP verbose
action:   Check state of target in question. 
*/
char elx_mes0702[] = "%sIssue Target Reset to TGT %d Data: x%x x%x";
msgLogDef elx_msgBlk0702 = {
	ELX_LOG_MSG_FP_0702,
	elx_mes0702,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0703
message:  Issue LUN Reset to TGT <num> LUN <num>
descript: The SCSI layer detected that it needs to abort all I/O 
          to a specific device. This results in an FCP Task 
          Management command to abort the I/O in progress. 
data:     (1) rpi (2) flags
severity: Information
log:      LOG_FCP verbose
action:   Check state of device in question. 
*/
char elx_mes0703[] = "%sIssue LUN Reset to TGT %d LUN %d Data: x%x x%x";
msgLogDef elx_msgBlk0703 = {
	ELX_LOG_MSG_FP_0703,
	elx_mes0703,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0706
message:  Start nodev timer
descript: A target disappeared from the Fibre Channel network. If the 
          target does not return within nodev-tmo timeout all I/O to 
          the target will fail.
data:     (1) nlp_DID (2) nlp_flag (3) nlp_state (4) nlp
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0706[] = "%sStart nodev timer Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0706 = {
	ELX_LOG_MSG_FP_0706,
	elx_mes0706,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0710
message:  Iodone <target>/<lun> error <result> SNS <lp> <lp3>
descript: This error indicates the FC driver is returning SCSI 
          command to the SCSI layer in error or with sense data.
data:     (1) retry (2) resid
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0710[] = "%sIodone <%d/%d> error x%x SNS x%x x%x Data: x%x x%x";
msgLogDef elx_msgBlk0710 = {
	ELX_LOG_MSG_FP_0710,
	elx_mes0710,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0712
message:  SCSI layer issued abort device
descript: The SCSI layer is requesting the driver to abort 
          I/O to a specific device.
data:     (1) target (2) lun (3)
severity: Error
log:      Always
action:   Check state of device in question.
*/
char elx_mes0712[] = "%sSCSI layer issued abort device Data: x%x x%x";
msgLogDef elx_msgBlk0712 = {
	ELX_LOG_MSG_FP_0712,
	elx_mes0712,
	elx_msgPreambleFPe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0713
message:  SCSI layer issued Target Reset
descript: The SCSI layer is requesting the driver to abort 
          I/O to a specific target.
data:     (1) target (2) lun 
severity: Error
log:      Always
action:   Check state of target in question.
*/
char elx_mes0713[] = "%sSCSI layer issued Target Reset Data: x%x x%x";
msgLogDef elx_msgBlk0713 = {
	ELX_LOG_MSG_FP_0713,
	elx_mes0713,
	elx_msgPreambleFPe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0714
message:  SCSI layer issued Bus Reset
descript: The SCSI layer is requesting the driver to abort 
          all I/Os to all targets on this HBA.
data:     (1) tgt (2) lun (3) rc - success / failure
severity: Error
log:      Always
action:   Check state of targets in question.
*/
char elx_mes0714[] = "%sSCSI layer issued Bus Reset Data: x%x x%x x%x";
msgLogDef elx_msgBlk0714 = {
	ELX_LOG_MSG_FP_0714,
	elx_mes0714,
	elx_msgPreambleFPe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0716
message:  FCP Read Underrun, expected <len>, residual <resid>
descript: FCP device provided less data than was requested.
data:     (1) fcpi_parm (2) cmnd[0] (3) underflow 
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0716[] =
    "%sFCP Read Underrun, expected %d, residual %d Data: x%x x%x x%x";
msgLogDef elx_msgBlk0716 = {
	ELX_LOG_MSG_FP_0716,
	elx_mes0716,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0717
message:  FCP command <cmd> residual underrun converted to error
descript: The driver convert this underrun condition to an error based 
          on the underflow field in the SCSI cmnd.
data:     (1) len (2) resid (3) underflow
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0717[] =
    "%sFCP command x%x residual underrun converted to error Data: x%x x%x x%x";
msgLogDef elx_msgBlk0717 = {
	ELX_LOG_MSG_FP_0717,
	elx_mes0717,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0729
message:  FCP cmd <cmnd> failed <target>/<lun>
descript: The specifed device failed an FCP command. 
data:     (1) status (2) result (3) xri (4) iotag
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char elx_mes0729[] =
    "%sFCP cmd x%x failed <%d/%d> status: x%x result: x%x Data: x%x x%x";
msgLogDef elx_msgBlk0729 = {
	ELX_LOG_MSG_FP_0729,
	elx_mes0729,
	elx_msgPreambleFPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0730
message:  FCP command failed: RSP
descript: The FCP command failed with a response error.
data:     (1) Status2 (2) Status3 (3) ResId (4) SnsLen (5) RspLen (6) Info3
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char elx_mes0730[] = "%sFCP command failed: RSP Data: x%x x%x x%x x%x x%x x%x";
msgLogDef elx_msgBlk0730 = {
	ELX_LOG_MSG_FP_0730,
	elx_mes0730,
	elx_msgPreambleFPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0732
message:  Retry FCP command due to 29,00 check condition
descript: The issued FCP command got a 29,00 check condition and will 
          be retried by the driver.
data:     (1) *lp (2) *lp+1 (3) *lp+2 (4) *lp+3
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0732[] =
    "%sRetry FCP command due to 29,00 check condition Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0732 = {
	ELX_LOG_MSG_FP_0732,
	elx_mes0732,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0734
message:  FCP Read Check Error
descript: The issued FCP command returned a Read Check Error
data:     (1) fcpDl (2) rspResId (3) fcpi_parm (4) cdb[0]
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char elx_mes0734[] = "%sFCP Read Check Error Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0734 = {
	ELX_LOG_MSG_FP_0734,
	elx_mes0734,
	elx_msgPreambleFPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_FCP,
	ERRID_LOG_HDW_ERR
};

/*
msgName: elx_mes0735
message:  FCP Read Check Error with Check Condition
descript: The issued FCP command returned a Read Check Error and a 
          Check condition.
data:     (1) fcpDl (2) rspResId (3) fcpi_parm (4) cdb[0]
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char elx_mes0735[] =
    "%sFCP Read Check Error with Check Condition Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0735 = {
	ELX_LOG_MSG_FP_0735,
	elx_mes0735,
	elx_msgPreambleFPw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_FCP | LOG_CHK_COND,
	ERRID_LOG_HDW_ERR
};

/*
msgName: elx_mes0736
message:  Recieved Queue Full status from FCP device <tgt> <lun>.
descript: Recieved a Queue Full error status from specified FCP device.
data:     (1) qfull_retry_count (2) qfull_retries (3) currentOutstanding (4) maxOutstanding
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0736[] =
    "%sRecieved Queue Full status from FCP device %d %d Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0736 = {
	ELX_LOG_MSG_FP_0736,
	elx_mes0736,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0737
message:  <ASC ASCQ> Check condition received
descript: The issued FCP command resulted in a Check Condition.
data:     (1) CFG_CHK_COND_ERR (2) CFG_DELAY_RSP_ERR (3) *lp
severity: Information
log:      LOG_FCP | LOG_CHK_COND verbose
action:   No action needed, informational
*/
char elx_mes0737[] = "%sx%x Check condition received Data: x%x x%x x%x";
msgLogDef elx_msgBlk0737 = {
	ELX_LOG_MSG_FP_0737,
	elx_mes0737,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP | LOG_CHK_COND,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0747
message:  Cmpl Target Reset
descript: Target Reset completed.
data:     (1) scsi_id (2) lun_id (3) Error (4) statLocalError (5) *cmd + WD7
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0747[] = "%sCmpl Target Reset Data: x%x x%x x%x x%x x%x";
msgLogDef elx_msgBlk0747 = {
	ELX_LOG_MSG_FP_0747,
	elx_mes0747,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0748
message:  Cmpl LUN Reset
descript: LUN Reset completed.
data:     (1) scsi_id (2) lun_id (3) Error (4) statLocalError (5) *cmd + WD7
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0748[] = "%sCmpl LUN Reset Data: x%x x%x x%x x%x x%x";
msgLogDef elx_msgBlk0748 = {
	ELX_LOG_MSG_FP_0748,
	elx_mes0748,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0749
message:  Cmpl Abort Task Set
descript: Abort Task Set completed.
data:     (1) scsi_id (2) lun_id (3) Error (4) statLocalError (5) *cmd + WD7
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char elx_mes0749[] = "%sCmpl Abort Task Set Data: x%x x%x x%x x%x x%x";
msgLogDef elx_msgBlk0749 = {
	ELX_LOG_MSG_FP_0749,
	elx_mes0749,
	elx_msgPreambleFPi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0754
message:  SCSI timeout
descript: An FCP IOCB command was posted to a ring and did not complete 
          within ULP timeout seconds.
data:     (1) did (2) sid (3) command (4) iotag
severity: Error
log:      Always
action:   If no I/O is going through the adapter, reboot the system; 
          If problem persists, contact Technical Support.
*/
char elx_mes0754[] = "%sSCSI timeout Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0754 = {
	ELX_LOG_MSG_FP_0754,
	elx_mes0754,
	elx_msgPreambleFPe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_TIMEOUT
};

/*
 *  Begin NODE LOG Message Structures
 */

/*
msgName: elx_mes0900
message:  Cleanup node for NPort <nlp_DID>
descript: The driver node table entry for a remote NPort was removed.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0900[] = "%sCleanup node for NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk0900 = {
	ELX_LOG_MSG_ND_0900,
	elx_mes0900,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0901
message:  FIND node DID mapped
descript: The driver is searching for a node table entry, on the 
          mapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0901[] = "%sFIND node DID mapped Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0901 = {
	ELX_LOG_MSG_ND_0901,
	elx_mes0901,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0902
message:  FIND node DID mapped
descript: The driver is searching for a node table entry, on the 
          mapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0902[] = "%sFIND node DID mapped Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0902 = {
	ELX_LOG_MSG_ND_0902,
	elx_mes0902,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0903
message:  Add scsiid <sid> to BIND list 
descript: The driver is putting the node table entry on the binding list.
data:     (1) bind_cnt (2) nlp_DID (3) bind_type (4) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0903[] = "%sAdd scsiid %d to BIND list Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0903 = {
	ELX_LOG_MSG_ND_0903,
	elx_mes0903,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0904
message:  Add NPort <did> to PLOGI list
descript: The driver is putting the node table entry on the plogi list.
data:     (1) plogi_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0904[] = "%sAdd NPort x%x to PLOGI list Data: x%x x%x";
msgLogDef elx_msgBlk0904 = {
	ELX_LOG_MSG_ND_0904,
	elx_mes0904,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0905
message:  Add NPort <did> to ADISC list
descript: The driver is putting the node table entry on the adisc list.
data:     (1) adisc_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0905[] = "%sAdd NPort x%x to ADISC list Data: x%x x%x";
msgLogDef elx_msgBlk0905 = {
	ELX_LOG_MSG_ND_0905,
	elx_mes0905,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0906
message:  Add NPort <did> to UNMAP list
descript: The driver is putting the node table entry on the unmap list.
data:     (1) unmap_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0906[] = "%sAdd NPort x%x to UNMAP list Data: x%x x%x";
msgLogDef elx_msgBlk0906 = {
	ELX_LOG_MSG_ND_0906,
	elx_mes0906,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0907
message:  Add NPort <did> to MAP list scsiid <sid>
descript: The driver is putting the node table entry on the mapped list.
data:     (1) map_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0907[] = "%sAdd NPort x%x to MAP list scsiid %d Data: x%x x%x";
msgLogDef elx_msgBlk0907 = {
	ELX_LOG_MSG_ND_0907,
	elx_mes0907,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0908
message:  FIND node DID bind
descript: The driver is searching for a node table entry, on the 
          binding list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0908[] = "%sFIND node DID bind Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0908 = {
	ELX_LOG_MSG_ND_0908,
	elx_mes0908,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0910
message:  FIND node DID unmapped
descript: The driver is searching for a node table entry, on the 
          unmapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0910[] = "%sFIND node DID unmapped Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0910 = {
	ELX_LOG_MSG_ND_0910,
	elx_mes0910,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0911
message:  FIND node DID unmapped
descript: The driver is searching for a node table entry, on the 
          unmapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0911[] = "%sFIND node DID unmapped Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0911 = {
	ELX_LOG_MSG_ND_0911,
	elx_mes0911,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0927
message:  GET nodelist
descript: The driver is allocating a buffer to hold a node table entry.
data:     (1) bp (2) fc_free
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0927[] = "%sGET nodelist Data: x%x x%x";
msgLogDef elx_msgBlk0927 = {
	ELX_LOG_MSG_ND_0927,
	elx_mes0927,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0928
message:  PUT nodelist
descript: The driver is freeing a node table entry buffer.
data:     (1) bp (2) fc_free
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0928[] = "%sPUT nodelist Data: x%x x%x";
msgLogDef elx_msgBlk0928 = {
	ELX_LOG_MSG_ND_0928,
	elx_mes0928,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0929
message:  FIND node DID unmapped
descript: The driver is searching for a node table entry, on the 
          unmapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0929[] = "%sFIND node DID unmapped Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0929 = {
	ELX_LOG_MSG_ND_0929,
	elx_mes0929,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0930
message:  FIND node DID mapped
descript: The driver is searching for a node table entry, on the 
          mapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0930[] = "%sFIND node DID mapped Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0930 = {
	ELX_LOG_MSG_ND_0930,
	elx_mes0930,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0931
message:  FIND node DID bind
descript: The driver is searching for a node table entry, on the 
          binding list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0931[] = "%sFIND node DID bind Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk0931 = {
	ELX_LOG_MSG_ND_0931,
	elx_mes0931,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes0932
message:  FIND node did <did> NOT FOUND
descript: The driver was searching for a node table entry based on DID 
          and the entry was not found.
data:     (1) order
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char elx_mes0932[] = "%sFIND node did x%x NOT FOUND Data: x%x";
msgLogDef elx_msgBlk0932 = {
	ELX_LOG_MSG_ND_0932,
	elx_mes0932,
	elx_msgPreambleNDi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin MISC LOG message structures
 */

/*
msgName: elx_mes1201
message:  linux_kmalloc: Bad phba
descript: The driver manages its own memory for internal usage. This 
          error indicates a problem occurred in the driver memory 
          management routines. This error could also indicate the host 
          system in low on memory resources.
data:     (1) size (2) type (3) fc_idx_dmapool
severity: Error
log:      Always
action:   This error could indicate a driver or host operating system 
          problem. If problems persist report these errors to Technical 
          Support.
*/
char elx_mes1201[] = "%slinux_kmalloc: Bad phba Data: x%x x%x x%x";
msgLogDef elx_msgBlk1201 = {
	ELX_LOG_MSG_MI_1201,
	elx_mes1201,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1202
message:  linux_kmalloc: Bad size
descript: The driver manages its own memory for internal usage. This 
          error indicates a problem occurred in the driver memory 
          management routines. This error could also indicate the host 
          system in low on memory resources.
data:     (1) size (2) type (3) fc_idx_dmapool
severity: Error
log:      Always
action:   This error could indicate a driver or host operating system 
          problem. If problems persist report these errors to Technical 
          Support.
*/
char elx_mes1202[] = "%slinux_kmalloc: Bad size Data: x%x x%x x%x";
msgLogDef elx_msgBlk1202 = {
	ELX_LOG_MSG_MI_1202,
	elx_mes1202,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1204
message:  linux_kmalloc: Bad virtual addr
descript: The driver manages its own memory for internal usage. This 
          error indicates a problem occurred in the driver memory 
          management routines. This error could also indicate the host 
          system in low on memory resources.
data:     (1) i (2) size ( 3) type (4) fc_idx_dmapool
severity: Error
log:      Always
action:   This error could indicate a driver or host operating system 
          problem. If problems persist report these errors to Technical 
          Support.
*/
char elx_mes1204[] = "%slinux_kmalloc: Bad virtual addr Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk1204 = {
	ELX_LOG_MSG_MI_1204,
	elx_mes1204,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1205
message:  linux_kmalloc: dmapool FULL
descript: The driver manages its own memory for internal usage. This 
          error indicates a problem occurred in the driver memory 
          management routines. This error could also indicate the host 
          system in low on memory resources.
data:     (1) i (2) size (3) type (4) fc_idx_dmapool
severity: Error
log:      Always
action:   This error could indicate a driver or host operating system 
          problem. If problems persist report these errors to Technical 
          Support.
*/
char elx_mes1205[] = "%slinux_kmalloc: dmapool FULL Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk1205 = {
	ELX_LOG_MSG_MI_1205,
	elx_mes1205,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1206
message:  linux_kfree: Bad phba
descript: The driver manages its own memory for internal usage. This 
          error indicates a problem occurred in the driver memory 
          management routines. This error could also indicate the host 
          system in low on memory resources.
data:     (1) size (2) fc_idx_dmapool
severity: Error
log:      Always
action:   This error could indicate a driver or host operating system 
          problem. If problems persist report these errors to Technical 
          Support.
*/
char elx_mes1206[] = "%slinux_kfree: Bad phba Data: x%x x%x";
msgLogDef elx_msgBlk1206 = {
	ELX_LOG_MSG_MI_1206,
	elx_mes1206,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1207
message:  linux_kfree: NOT in dmapool
descript: The driver manages its own memory for internal usage. This 
          error indicates a problem occurred in the driver memory 
          management routines. This error could also indicate the host 
          system in low on memory resources.
data:     (1) virt (2) size (3) fc_idx_dmapool
severity: Error
log:      Always
action:   This error could indicate a driver or host operating system 
          problem. If problems persist report these errors to Technical 
          Support.
*/
char elx_mes1207[] = "%slinux_kfree: NOT in dmapool Data: x%x x%x x%x";
msgLogDef elx_msgBlk1207 = {
	ELX_LOG_MSG_MI_1207,
	elx_mes1207,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1208
descript: The CT response returned more data than the user buffer could hold. 
message:  C_CT Request error
data:     (1) dfc_flag (2) 4096
severity: Information
log:      LOG_MISC verbose
action:   Modify user application issuing CT request to allow for a larger 
          response buffer.
*/
char elx_mes1208[] = "%sC_CT Request error Data: x%x x%x";
msgLogDef elx_msgBlk1208 = {
	ELX_LOG_MSG_MI_1208,
	elx_mes1208,
	elx_msgPreambleMIi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1210
message:  Convert ASC to hex. Input byte cnt < 1
descript: ASCII string to hex conversion failed. Input byte count < 1.
data:     none
severity: Error
log:      Always
action:   This error could indicate a software driver problem. 
          If problems persist report these errors to Technical Support.
*/
char elx_mes1210[] = "%sConvert ASC to hex. Input byte cnt < 1";
msgLogDef elx_msgBlk1210 = {
	ELX_LOG_MSG_MI_1210,
	elx_mes1210,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1211
message:  Convert ASC to hex. Input byte cnt > max <num>
descript: ASCII string to hex conversion failed. Input byte count > max <num>.
data:     none
severity: Error
log:      Always
action:   This error could indicate a software driver problem. 
          If problems persist report these errors to Technical Support.
*/
char elx_mes1211[] = "%sConvert ASC to hex. Input byte cnt > max %d";
msgLogDef elx_msgBlk1211 = {
	ELX_LOG_MSG_MI_1211,
	elx_mes1211,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1212
message:  Convert ASC to hex. Output buffer to small 
descript: ASCII string to hex conversion failed. The output buffer byte 
          size is less than 1/2 of input byte count. Every 2 input chars 
          (bytes) require 1 output byte.
data:     none
severity: Error
log:      Always
action:   This error could indicate a software driver problem. 
          If problems persist report these errors to Technical Support.
*/
char elx_mes1212[] = "%sConvert ASC to hex. Output buffer too small";
msgLogDef elx_msgBlk1212 = {
	ELX_LOG_MSG_MI_1212,
	elx_mes1212,
	elx_msgPreambleMIe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1213
message:  Convert ASC to hex. Input char seq not ASC hex.
descript: The ASCII hex input string contains a non-ASCII hex characters
data:     none
severity: Error configuration
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char elx_mes1213[] = "%sConvert ASC to hex. Input char seq not ASC hex.";
msgLogDef elx_msgBlk1213 = {
	ELX_LOG_MSG_MI_1213,
	elx_mes1213,
	elx_msgPreambleMIc,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR_CFG,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin LINK LOG Message Structures
 */

/*
msgName: elx_mes1300
message:  Re-establishing Link, timer expired
descript: The driver detected a condition where it had to re-initialize 
          the link.
data:     (1) fc_flag (2) fc_ffstate
severity: Error
log:      Always
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char elx_mes1300[] = "%sRe-establishing Link, timer expired Data: x%x x%x";
msgLogDef elx_msgBlk1300 = {
	ELX_LOG_MSG_LK_1300,
	elx_mes1300,
	elx_msgPreambleLKe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1301
message:  Re-establishing Link
descript: The driver detected a condition where it had to re-initialize 
          the link.
data:     (1) status (2) status1 (3) status2
severity: Information
log:      LOG_LINK_EVENT verbose
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char elx_mes1301[] = "%sRe-establishing Link Data: x%x x%x x%x";
msgLogDef elx_msgBlk1301 = {
	ELX_LOG_MSG_LK_1301,
	elx_mes1301,
	elx_msgPreambleLKi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1302
message:  Reset link speed to auto. 1G HBA cfg'd for 2G
descript: The driver is reinitializing the link speed to auto-detect.
data:     (1) current link speed
severity: Warning
log:      LOG_LINK_EVENT verbose
action:   None required
*/
char elx_mes1302[] =
    "%sReset link speed to auto. 1G HBA cfg'd for 2G Data: x%x";
msgLogDef elx_msgBlk1302 = {
	ELX_LOG_MSG_LK_1302,
	elx_mes1302,
	elx_msgPreambleLKw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1303
message:  Link Up Event <eventTag> received
descript: A link up event was received. It is also possible for 
          multiple link events to be received together. 
data:     (1) fc_eventTag (2) granted_AL_PA (3) UlnkSpeed (4) alpa_map[0]
detail:   If link events received, log (1) last event number 
          received, (2) ALPA granted, (3) Link speed 
          (4) number of entries in the loop init LILP ALPA map. 
          An ALPA map message is also recorded if LINK_EVENT 
          verbose mode is set. Each ALPA map message contains 
          16 ALPAs. 
severity: Error
log:      Always
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char elx_mes1303[] = "%sLink Up Event x%x received Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk1303 = {
	ELX_LOG_MSG_LK_1303,
	elx_mes1303,
	elx_msgPreambleLKe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1304
message:  Link Up Event ALPA map
descript: A link up event was received.
data:     (1) wd1 (2) wd2 (3) wd3 (4) wd4
severity: Warning
log:      LOG_LINK_EVENT verbose
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char elx_mes1304[] = "%sLink Up Event ALPA map Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk1304 = {
	ELX_LOG_MSG_LK_1304,
	elx_mes1304,
	elx_msgPreambleLKw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1305
message:  Link Down Event <eventTag> received
descript: A link down event was received.
data:     (1) fc_eventTag (2) hba_state (3) fc_flag
severity: Error
log:      Always
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char elx_mes1305[] = "%sLink Down Event x%x received Data: x%x x%x x%x";
msgLogDef elx_msgBlk1305 = {
	ELX_LOG_MSG_LK_1305,
	elx_mes1305,
	elx_msgPreambleLKe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1306
message:  Link Down timeout
descript: The link was down for greater than the configuration parameter 
          (lpfc_linkdown_tmo) seconds. All I/O associated with the devices
          on this link will be failed.  
data:     (1) hba_state (2) fc_flag (3) fc_ns_retry
severity: Warning
log:      LOG_LINK_EVENT | LOG_DISCOVERY verbose
action:   Check HBA cable/connection to Fibre Channel network.
*/
char elx_mes1306[] = "%sLink Down timeout Data: x%x x%x x%x";
msgLogDef elx_msgBlk1306 = {
	ELX_LOG_MSG_LK_1306,
	elx_mes1306,
	elx_msgPreambleLKw,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_WARN,
	LOG_LINK_EVENT | LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1307
message:  READ_LA mbox error <mbxStatus> state <hba_state>
descript: The driver cannot determine what type of link event occurred.
data:     None
severity: Information
log:      LOG_LINK_EVENT verbose
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network. Could indicate
          possible hardware or firmware problem.
*/
char elx_mes1307[] = "%sREAD_LA mbox error x%x state x%x";
msgLogDef elx_msgBlk1307 = {
	ELX_LOG_MSG_LK_1307,
	elx_mes1307,
	elx_msgPreambleLKi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin XXX LOG Message Structures
 */

/*
 *  Begin IOCTL Message Structures
 */

/*
msgName: elx_mes1600
message:  dfc_ioctl entry
descript: Entry point for processing diagnostic ioctl.
data:     (1) c_cmd (2) c_arg1 (3) c_arg2 (4) c_outsz
severity: Information
log:      LOG_IOC verbose
action:   No action needed, informational
*/
char elx_mes1600[] = "%sdfc_ioctl entry Data: x%x x%x x%x x%x";
msgLogDef elx_msgBlk1600 = {
	ELX_LOG_MSG_IO_1600,
	elx_mes1600,
	elx_msgPreambleIOi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IOC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1601
message:  dfc_ioctl exit
descript: Exit point for processing diagnostic ioctl.
data:     (1) rc (2) c_outsz (3) c_dataout
severity: Information
log:      LOG_IOC verbose
action:   No action needed, informational
*/
char elx_mes1601[] = "%sdfc_ioctl exit Data: x%x x%x x%x";
msgLogDef elx_msgBlk1601 = {
	ELX_LOG_MSG_IO_1601,
	elx_mes1601,
	elx_msgPreambleIOi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IOC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1602
message:  dfc_data_alloc
descript: Allocating data buffer to process dfc ioct.
data:     (1) fc_dataout (2) fc_outsz
severity: Iniformation
log:      LOG_IOC verbose
action:   No action needed, informational
*/
char elx_mes1602[] = "%sdfc_data_alloc Data: x%x x%x";
msgLogDef elx_msgBlk1602 = {
	ELX_LOG_MSG_IO_1602,
	elx_mes1602,
	elx_msgPreambleIOi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IOC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1603
message:  dfc_data_free
descript: Freeing data buffer to process dfc ioct.
data:     (1) fc_dataout (2) fc_outsz
severity: Information
log:      LOG_IOC verbose
action:   No action needed, informational
*/
char elx_mes1603[] = "%sdfc_data_free Data: x%x x%x";
msgLogDef elx_msgBlk1603 = {
	ELX_LOG_MSG_IO_1603,
	elx_mes1603,
	elx_msgPreambleIOi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IOC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1604
message:  lpfc_ioctl:error
descript: SCSI send request buffer size limited exceeded
data:     (1) error number index
severity: Error
log:      Always
action:   Reduce application program's SCSI send request buffer size to < 320K bytes.  
*/
char elx_mes1604[] = "%slpfc_ioctl:error Data: %d";
msgLogDef elx_msgBlk1604 = {
	ELX_LOG_MSG_IO_1604,
	elx_mes1604,
	elx_msgPreambleIOe,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_ERR,
	LOG_IOC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: elx_mes1605
message:  Issue Report LUN on NPort <nlp_DID>
descript: The driver issued an Ioctl REPORT_LUN SCSI command to a FCP target.
data:     (1) nlp_failMask (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_IOC verbose
action:   No action needed, informational
*/
char elx_mes1605[] = "%sIssue Report LUN on NPort x%x Data: x%x x%x x%x";
msgLogDef elx_msgBlk1605 = {
	ELX_LOG_MSG_IO_1605,
	elx_mes1605,
	elx_msgPreambleIOi,
	ELX_MSG_OPUT_GLOB_CTRL,
	ELX_LOG_MSG_TYPE_INFO,
	LOG_IOC,
	ERRID_LOG_UNEXPECT_EVENT
};

void
elx_read_rev(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));
	mb->un.varRdRev.cv = 1;
	mb->mbxCommand = MBX_READ_REV;
	mb->mbxOwner = OWN_HOST;
	return;
}

void
elx_config_ring(elxHBA_t * phba, int ring, ELX_MBOXQ_t * pmb)
{
	int i;
	MAILBOX_t *mb;
	ELX_SLI_t *psli;
	ELX_RING_INIT_t *pring;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	mb->un.varCfgRing.ring = ring;
	mb->un.varCfgRing.maxOrigXchg = 0;
	mb->un.varCfgRing.maxRespXchg = 0;
	mb->un.varCfgRing.recvNotify = 1;

	psli = &phba->sli;
	pring = &psli->sliinit.ringinit[ring];
	mb->un.varCfgRing.numMask = pring->num_mask;
	mb->mbxCommand = MBX_CONFIG_RING;
	mb->mbxOwner = OWN_HOST;

	/* Is this ring configured for a specific profile */
	if (pring->prt[0].profile) {
		mb->un.varCfgRing.profile = pring->prt[0].profile;
		return;
	}

	/* Otherwise we setup specific rctl / type masks for this ring */
	for (i = 0; i < pring->num_mask; i++) {
		mb->un.varCfgRing.rrRegs[i].rval = pring->prt[i].rctl;
		if (mb->un.varCfgRing.rrRegs[i].rval != FC_ELS_REQ)	/* ELS request */
			mb->un.varCfgRing.rrRegs[i].rmask = 0xff;
		else
			mb->un.varCfgRing.rrRegs[i].rmask = 0xfe;
		mb->un.varCfgRing.rrRegs[i].tval = pring->prt[i].type;
		mb->un.varCfgRing.rrRegs[i].tmask = 0xff;
	}

	return;
}

int
elx_config_port(elxHBA_t * phba, ELX_MBOXQ_t * pmb)
{
	ELX_SLI_t *psli;
	MAILBOX_t *mb;
	uint32_t *hbainit;
	elx_dma_addr_t pdma_addr;
	uint32_t offset;

	mb = &pmb->mb;
	memset((void *)pmb, 0, sizeof (ELX_MBOXQ_t));

	psli = &phba->sli;
	mb->mbxCommand = MBX_CONFIG_PORT;
	mb->mbxOwner = OWN_HOST;

	mb->un.varCfgPort.pcbLen = sizeof (PCB_t);
	offset =
	    (uint8_t *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb) -
	    (uint8_t *) phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	mb->un.varCfgPort.pcbLow = putPaddrLow(pdma_addr);
	mb->un.varCfgPort.pcbHigh = putPaddrHigh(pdma_addr);

	/* Now setup pcb */
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.type =
	    TYPE_NATIVE_SLI2;
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.feature =
	    FEATURE_INITIAL_SLI2;

	/* Setup Mailbox pointers */
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.mailBoxSize =
	    sizeof (MAILBOX_t);
	offset =
	    (uint8_t *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.mbx) -
	    (uint8_t *) phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.mbAddrHigh =
	    putPaddrHigh(pdma_addr);
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.mbAddrLow =
	    putPaddrLow(pdma_addr);

	/* Setup Host Group ring counters */
	if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
		offset =
		    (uint8_t *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.
				 mbx.us.s2.host) -
		    (uint8_t *) phba->slim2p.virt;
		pdma_addr = phba->slim2p.phys + offset;
		((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.hgpAddrHigh =
		    putPaddrHigh(pdma_addr);
		((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.hgpAddrLow =
		    putPaddrLow(pdma_addr);
	} else {
		uint32_t Laddr;
		HGP hgp;

		((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.hgpAddrHigh =
		    (uint32_t)
		    (psli->sliinit.elx_sli_read_pci) (phba, PCI_BAR_1_REGISTER);
		Laddr =
		    (psli->sliinit.elx_sli_read_pci) (phba, PCI_BAR_0_REGISTER);
		Laddr &= ~0x4;
		((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.hgpAddrLow =
		    (uint32_t) (Laddr + (SLIMOFF * sizeof (uint32_t)));
		memset((void *)&hgp, 0, sizeof (HGP));
		(psli->sliinit.elx_sli_write_slim) ((void *)phba, (void *)&hgp,
						    (SLIMOFF *
						     sizeof (uint32_t)),
						    sizeof (HGP));
	}

	/* Setup Port Group ring counters */
	offset =
	    (uint8_t *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.mbx.us.
			 s2.port) - (uint8_t *) phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.pgpAddrHigh =
	    putPaddrHigh(pdma_addr);
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.pgpAddrLow =
	    putPaddrLow(pdma_addr);

	/* Use callback routine to setp rings in the pcb */
	hbainit = (psli->sliinit.elx_sli_config_pcb_setup) (phba);
	if (hbainit != NULL)
		memcpy(&mb->un.varCfgPort.hbainit, hbainit, 20);

	/* Swap PCB if needed */
	elx_sli_pcimem_bcopy((uint32_t
			      *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.
				  pcb),
			     (uint32_t
			      *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.
				  pcb), sizeof (PCB_t));

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORDEV);

	/* Service Level Interface (SLI) 2 selected */
	elx_printf_log(phba->brd_no, &elx_msgBlk0405,	/* ptr to msg structure */
		       elx_mes0405,	/* ptr to msg */
		       elx_msgBlk0405.msgPreambleStr);	/* begin & end varargs */
	return (0);
}

void
elx_mbox_put(elxHBA_t * phba, ELX_MBOXQ_t * mbq)
{				/* pointer to mbq entry */
	ELX_SLI_t *psli;

	psli = &phba->sli;

	/* mboxq is a single linked list with q_first pointing to the first
	 * element and q_last pointing to the last.
	 */
	if (psli->mboxq.q_first) {
		/* queue command to end of list */
		((ELX_MBOXQ_t *) (psli->mboxq.q_last))->q_f = mbq;
		psli->mboxq.q_last = (ELX_SLINK_t *) mbq;
	} else {
		/* add command to empty list */
		psli->mboxq.q_first = (ELX_SLINK_t *) mbq;
		psli->mboxq.q_last = (ELX_SLINK_t *) mbq;
	}

	mbq->q_f = 0;
	psli->mboxq.q_cnt++;

	return;
}

ELX_MBOXQ_t *
elx_mbox_get(elxHBA_t * phba)
{
	ELX_MBOXQ_t *p_first = 0;
	ELX_SLI_t *psli;

	psli = &phba->sli;

	/* mboxq is a single linked list with q_first pointing to the first
	 * element and q_last pointing to the last.
	 */
	if (psli->mboxq.q_first) {
		p_first = (ELX_MBOXQ_t *) psli->mboxq.q_first;
		if ((psli->mboxq.q_first = (ELX_SLINK_t *) p_first->q_f) == 0) {
			psli->mboxq.q_last = 0;
		}
		p_first->q_f = 0;
		psli->mboxq.q_cnt--;
	}

	return (p_first);
}

void *elx_mem_alloc_dma(elxHBA_t *, uint32_t, uint32_t);

DMABUF_t *
elx_mem_alloc_dmabuf(elxHBA_t * phba, uint32_t size)
{
	return ((DMABUF_t *) elx_mem_alloc_dma(phba, size, ELX_MEM_DBUF));
}

void *
elx_mem_alloc_dma(elxHBA_t * phba, uint32_t size, uint32_t type)
{
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	DMABUF_t *matp;
	DMABUFEXT_t *matpext;
	void *ptr;

	buf_info = &bufinfo;
	switch (type) {
	case ELX_MEM_DBUF:
		buf_info->size = sizeof (DMABUF_t);
		break;
	case ELX_MEM_DBUFEXT:
		buf_info->size = sizeof (DMABUFEXT_t);
		break;
	}
	buf_info->flags = ELX_MBUF_VIRT;
	buf_info->align = sizeof (void *);
	buf_info->dma_handle = 0;

	elx_malloc(phba, buf_info);
	if (buf_info->virt == 0) {
		return (0);
	}

	ptr = buf_info->virt;

	buf_info->size = size;
	buf_info->flags = ELX_MBUF_DMA;
	buf_info->dma_handle = 0;
	buf_info->virt = 0;

	switch (size) {
	case 1024:
		buf_info->align = 1024;
		break;

	case 2048:
		buf_info->align = 2048;
		break;

	case 4096:
		buf_info->align = 4096;
		break;

	default:
		buf_info->align = sizeof (void *);
		break;
	}

	elx_malloc(phba, buf_info);
	/* if we fail the buffer allocation, free the container */
	if (buf_info->virt == 0) {
		buf_info->virt = ptr;
		buf_info->flags = ELX_MBUF_VIRT;
		elx_free(phba, buf_info);
		return (0);
	}
	memset(buf_info->virt, 0, size);
	switch (type) {
	case ELX_MEM_DBUF:
		matp = (DMABUF_t *) ptr;
		memset(matp, 0, sizeof (DMABUF_t));
		matp->virt = buf_info->virt;
		if (buf_info->dma_handle) {
			matp->dma_handle = buf_info->dma_handle;
			matp->data_handle = buf_info->data_handle;
		}
		matp->phys = buf_info->phys;
		ptr = (void *)matp;
		break;
	case ELX_MEM_DBUFEXT:
		matpext = (DMABUFEXT_t *) ptr;
		memset(matpext, 0, sizeof (DMABUFEXT_t));
		matpext->dma.virt = buf_info->virt;
		if (buf_info->dma_handle) {
			matpext->dma.dma_handle = buf_info->dma_handle;
			matpext->dma.data_handle = buf_info->data_handle;
		}
		matpext->dma.phys = buf_info->phys;
		matpext->size = size;
		ptr = (void *)matpext;
		break;
	}
	return (ptr);
}

DMABUFIP_t *
elx_mem_alloc_ipbuf(elxHBA_t * phba, uint32_t size)
{
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	DMABUFIP_t *matip;

	buf_info = &bufinfo;
	buf_info->size = sizeof (DMABUFIP_t);
	buf_info->flags = ELX_MBUF_VIRT;
	buf_info->align = sizeof (void *);
	buf_info->dma_handle = 0;

	elx_malloc(phba, buf_info);
	if (buf_info->virt == 0) {
		return (0);
	}

	matip = (DMABUFIP_t *) buf_info->virt;
	memset(matip, 0, sizeof (DMABUFIP_t));

	elx_ip_get_rcv_buf(phba, matip, size);
	if (matip->ipbuf == 0) {
		elx_free(phba, buf_info);
		matip = 0;
	}
	return (matip);
}

uint8_t *
elx_mem_alloc_buf(elxHBA_t * phba, uint32_t size)
{
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;

	buf_info = &bufinfo;
	buf_info->size = size;
	buf_info->flags = ELX_MBUF_VIRT;
	buf_info->align = sizeof (void *);
	buf_info->dma_handle = 0;

	elx_malloc(phba, buf_info);
	if (buf_info->virt == 0) {
		return (0);
	}
	memset(buf_info->virt, 0, size);
	return (buf_info->virt);
}

uint32_t
elx_mem_alloc_pool(elxHBA_t * phba, MEMSEG_t * mp, uint32_t cnt)
{
	uint8_t *bp;
	uint8_t *oldbp;
	DMABUF_t *matp;
	DMABUFIP_t *matip;
	int i;

	for (i = 0; i < cnt; i++) {
		/* If this is a DMA buffer we need alignment on a page so we don't
		 * want to worry about buffers spanning page boundries when mapping
		 * memory for the adapter.
		 */
		if (mp->elx_memflag & ELX_MEM_DMA) {
			if ((matp =
			     elx_mem_alloc_dmabuf(phba,
						  mp->elx_memsize)) == 0) {
				return (i);
			}
			/* Link buffer into beginning of list. The first pointer in
			 * each buffer is a forward pointer to the next buffer.
			 */
			oldbp = (uint8_t *) mp->mem_hdr.q_first;
			if (oldbp == 0)
				mp->mem_hdr.q_last = (ELX_SLINK_t *) matp;
			mp->mem_hdr.q_first = (ELX_SLINK_t *) matp;
			matp->next = (DMABUF_t *) oldbp;
		} else if (mp->elx_memflag & ELX_MEM_ATTACH_IPBUF) {
			if ((matip =
			     elx_mem_alloc_ipbuf(phba, mp->elx_memsize)) == 0) {
				return (i);
			}
			/* Link buffer into beginning of list. The first pointer in
			 * each buffer is a forward pointer to the next buffer.
			 */
			oldbp = (uint8_t *) mp->mem_hdr.q_first;
			if (oldbp == 0)
				mp->mem_hdr.q_last = (ELX_SLINK_t *) matip;
			mp->mem_hdr.q_first = (ELX_SLINK_t *) matip;
			matip->dma.next = (DMABUF_t *) oldbp;
		} else {
			/* Does not have to be DMAable */
			if ((bp =
			     elx_mem_alloc_buf(phba, mp->elx_memsize)) == 0) {
				return (i);
			}
			/* Link buffer into beginning of list. The first pointer in
			 * each buffer is a forward pointer to the next buffer.
			 */
			oldbp = (uint8_t *) mp->mem_hdr.q_first;
			if (oldbp == 0)
				mp->mem_hdr.q_last = (ELX_SLINK_t *) bp;
			mp->mem_hdr.q_first = (ELX_SLINK_t *) bp;
			*((uint8_t * *)bp) = oldbp;
		}
	}

	return (i);
}

int
elx_mem_alloc(elxHBA_t * phba)
{
	MEMSEG_t *mp;
	int j, cnt;
	unsigned long iflag;

	ELX_MEM_LOCK(phba, iflag);
	/* Allocate buffer pools for above buffer structures */
	for (j = 0; j < ELX_MAX_SEG; j++) {
		mp = &phba->memseg[j];

		mp->mem_hdr.q_first = 0;
		mp->mem_hdr.q_last = 0;

		cnt = mp->mem_hdr.q_max;
		/* free blocks = total blocks right now */
		if ((mp->mem_hdr.q_cnt =
		     elx_mem_alloc_pool(phba, mp, cnt)) != cnt) {
			elx_mem_free(phba);
			return (0);
		}
	}
	ELX_MEM_UNLOCK(phba, iflag);

	return (1);
}

int
elx_mem_free(elxHBA_t * phba)
{
	uint8_t *bp;
	MEMSEG_t *mp;
	DMABUF_t *mm;
	DMABUFIP_t *mmip;
	ELX_MBOXQ_t *mbox, *mbsave;
	ELX_SLI_t *psli;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	int j;
	unsigned long iflag;

	buf_info = &bufinfo;

	ELX_SLI_LOCK(phba, iflag);
	/* free the mapped address match area for each ring */
	psli = &phba->sli;

	/* Free everything on mbox queue */
	mbox = (ELX_MBOXQ_t *) (psli->mboxq.q_first);
	while (mbox) {
		mbsave = mbox;
		mbox = (ELX_MBOXQ_t *) mbox->q_f;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) mbsave);
	}
	psli->mboxq.q_first = 0;
	psli->mboxq.q_last = 0;
	psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
	if (psli->mbox_active) {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) psli->mbox_active);
		psli->mbox_active = 0;
	}
	ELX_SLI_UNLOCK(phba, iflag);

	ELX_MEM_LOCK(phba, iflag);
	/* Loop through all memory buffer pools */
	for (j = 0; j < ELX_MAX_SEG; j++) {
		mp = &phba->memseg[j];
		/* Free memory associated with all buffers on free buffer pool */
		while ((bp = (uint8_t *) mp->mem_hdr.q_first) != 0) {
			mp->mem_hdr.q_first = *((ELX_SLINK_t * *)bp);
			if (mp->elx_memflag & ELX_MEM_DMA) {
				mm = (DMABUF_t *) bp;
				bp = mm->virt;
				buf_info->size = mp->elx_memsize;
				buf_info->virt = (uint32_t *) bp;
				buf_info->phys = mm->phys;
				buf_info->flags = ELX_MBUF_DMA;
				if (mm->dma_handle) {
					buf_info->dma_handle = mm->dma_handle;
					buf_info->data_handle = mm->data_handle;
				}
				elx_free(phba, buf_info);

				buf_info->size = sizeof (DMABUF_t);
				buf_info->virt = (uint32_t *) mm;
				buf_info->phys = 0;
				buf_info->flags = ELX_MBUF_VIRT;
				buf_info->dma_handle = 0;
				elx_free(phba, buf_info);
			} else if (mp->elx_memflag & ELX_MEM_ATTACH_IPBUF) {
				mmip = (DMABUFIP_t *) bp;
				mmip->dma.next = 0;
				elx_ip_free_rcv_buf(phba, mmip,
						    mp->elx_memsize);

				buf_info->size = sizeof (DMABUFIP_t);
				buf_info->virt = (uint32_t *) mmip;
				buf_info->phys = 0;
				buf_info->flags = ELX_MBUF_VIRT;
				buf_info->dma_handle = 0;
				elx_free(phba, buf_info);
			} else {
				buf_info->size = mp->elx_memsize;
				buf_info->virt = (uint32_t *) bp;
				buf_info->phys = 0;
				buf_info->flags = ELX_MBUF_VIRT;
				buf_info->dma_handle = 0;
				elx_free(phba, buf_info);
			}
		}
		mp->mem_hdr.q_last = 0;
		mp->mem_hdr.q_cnt = 0;
	}
	ELX_MEM_UNLOCK(phba, iflag);

	return (1);
}

void *
elx_mem_get(elxHBA_t * phba, int arg)
{
	uint8_t *bp = NULL;
	MEMSEG_t *mp;
	unsigned long iflag;
	int low;
	uint32_t cnt, ask;
	uint32_t seg = arg & MEM_SEG_MASK;

	/* range check on seg argument */
	if (seg >= ELX_MAX_SEG) {
		return ((uint8_t *) 0);
	}

	ELX_MEM_LOCK(phba, iflag);
	mp = &phba->memseg[seg];

	if ((low = (!(arg & MEM_PRI) && (mp->mem_hdr.q_cnt <= mp->elx_lowmem)))) {
		/* Memory Buffer Pool is below low water mark */
		elx_printf_log(phba->brd_no, &elx_msgBlk0406,	/* ptr to msg structure */
			       elx_mes0406,	/* ptr to msg */
			       elx_msgBlk0406.msgPreambleStr,	/* begin varargs */
			       seg, mp->elx_lowmem, low);	/* end varargs */

		if (mp->elx_memflag & ELX_MEM_GETMORE) {
			goto getmore;
		}
		ELX_MEM_UNLOCK(phba, iflag);

		/* Low priority request and not enough buffers, so fail */
		return (bp);
	}

      top:
	bp = (uint8_t *) mp->mem_hdr.q_first;

	if (bp) {
		/* If a memory block exists, take it off freelist and return it to the user. */
		if (mp->mem_hdr.q_last == (ELX_SLINK_t *) bp) {
			mp->mem_hdr.q_last = 0;
		}

		mp->mem_hdr.q_first = *((ELX_SLINK_t * *)bp);
		*((uint8_t * *)bp) = 0;
		mp->mem_hdr.q_cnt--;
	} else {
		if (mp->elx_memflag & ELX_MEM_GETMORE) {
		      getmore:
			/* Grow the pool by: (current max / 4) + 32.
			 * This guarantees that the pool grows by at least 32 buffers.
			 */
			ask =
			    ((mp->mem_hdr.q_max >> 2) & 0xffff) +
			    ELX_MIN_POOL_GROWTH;
			if (mp->elx_memflag & ELX_MEM_BOUND) {
				if (mp->mem_hdr.q_max == mp->elx_himem) {
					/* Hit himem water mark.  The driver cannot alloc more memory */
					elx_printf_log(phba->brd_no, &elx_msgBlk0407,	/* ptr to msg structure */
						       elx_mes0407,	/* ptr to msg */
						       elx_msgBlk0407.msgPreambleStr,	/* begin varargs */
						       seg, mp->mem_hdr.q_max, mp->elx_himem);	/* end varargs */

					ELX_MEM_UNLOCK(phba, iflag);
					return (bp);
				}

				/* Don't exceed the himem limitation. */
				ask =
				    ((mp->mem_hdr.q_max + ask) >
				     mp->elx_himem) ? (mp->elx_himem -
						       mp->mem_hdr.q_max) : ask;
			}

			/* if allocated some buffers, update counts and goto top: to use first buffer */
			if ((cnt = elx_mem_alloc_pool(phba, mp, ask)) != 0) {
				mp->mem_hdr.q_cnt += cnt;
				mp->mem_hdr.q_max += cnt;
				goto top;
			}
		}

		/* Memory Buffer Pool is either out of buffers or the memory allocation failed.  */
		elx_printf_log(phba->brd_no, &elx_msgBlk0409,	/* ptr to msg structure */
			       elx_mes0409,	/* ptr to msg */
			       elx_msgBlk0409.msgPreambleStr,	/* begin varargs */
			       seg, mp->mem_hdr.q_cnt, mp->mem_hdr.q_max);	/* end varargs */
	}

	if (seg == MEM_NLP) {
		/* GET nodelist */
		elx_printf_log(phba->brd_no, &elx_msgBlk0927,	/* ptr to msg structure */
			       elx_mes0927,	/* ptr to msg */
			       elx_msgBlk0927.msgPreambleStr,	/* begin varargs */
			       (unsigned long)bp, mp->mem_hdr.q_cnt);	/* end varargs */

		/* NLP_FREED_NODE flag is to protect the node from being freed
		 * more then once. For driver_abort and other cases where the DSM 
		 * calls itself recursively, its possible to free the node twice.
		 */
		if (bp) {
			((ELX_NODELIST_t *) bp)->nlp_rflag &= ~NLP_FREED_NODE;
		}
	}

	ELX_MEM_UNLOCK(phba, iflag);
	return (bp);
}

uint8_t *
elx_mem_put(elxHBA_t * phba, int seg, uint8_t * bp)
{
	MEMSEG_t *mp;
	uint8_t *oldbp;
	unsigned long iflag;

	/* range check on seg argument */
	if (seg >= ELX_MAX_SEG)
		return ((uint8_t *) 0);

	ELX_MEM_LOCK(phba, iflag);
	mp = &phba->memseg[seg];

	if ((seg == MEM_IP_RCV_BUF) && ((DMABUFIP_t *) bp)->ipbuf == NULL) {
		/* 
		 * If IP buff is null, the network driver gave the buffer to upper layer.
		 * Acquire a new buffer and detach the dma mapping of previous 
		 * buffer.
		 */
		DMABUFIP_t *tmp;
		MBUF_INFO_t *buf_info;
		MBUF_INFO_t bufinfo;

		tmp = (DMABUFIP_t *) bp;
		buf_info = &bufinfo;
		buf_info->phys = tmp->dma.phys;
		buf_info->virt = tmp->dma.virt;
		buf_info->size = mp->elx_memsize;
		buf_info->flags = ELX_MBUF_PHYSONLY;
		buf_info->dma_handle = tmp->dma.dma_handle;
		elx_free(phba, buf_info);

		elx_ip_get_rcv_buf(phba, tmp, mp->elx_memsize);
	}
	if (seg == MEM_NLP) {
		ELX_NODELIST_t *ndlp;

		ndlp = (ELX_NODELIST_t *) bp;

		if (ndlp) {
			/* PUT nodelist */
			elx_printf_log(phba->brd_no, &elx_msgBlk0928,	/* ptr to msg structure */
				       elx_mes0928,	/* ptr to msg */
				       elx_msgBlk0928.msgPreambleStr,	/* begin varargs */
				       (unsigned long)bp, mp->mem_hdr.q_cnt + 1);	/* end varargs */

			/* NLP_FREED_NODE flag is to protect the node from being freed
			 * more then once. For driver_abort and other cases where the DSM 
			 * calls itself recursively, its possible to free the node twice.
			 */
			if (ndlp->nlp_rflag & NLP_FREED_NODE) {
				ELX_MEM_UNLOCK(phba, iflag);
				return (bp);
			}
			ndlp->nlp_rflag |= NLP_FREED_NODE;
		}
	}

	if (bp) {
		/* If a memory block exists, put it on freelist 
		 * and return it to the user.
		 */
		oldbp = (uint8_t *) mp->mem_hdr.q_first;
		mp->mem_hdr.q_first = (ELX_SLINK_t *) bp;
		*((uint8_t * *)bp) = oldbp;
		if (oldbp == 0)
			mp->mem_hdr.q_last = (ELX_SLINK_t *) bp;
		mp->mem_hdr.q_cnt++;
	}

	ELX_MEM_UNLOCK(phba, iflag);
	return (bp);
}

#include "elx_cfgparm.h"

struct elxHBA;

/* *********************************************************************
**
**    Forward declaration of internal routines to SCHED
**
** ******************************************************************** */

static void elx_sched_internal_check(elxHBA_t * hba);

/* ***************************************************************
**
**  Initialize HBA, TARGET and LUN SCHED structures
**  Basically clear them, set MaxQueue Depth
** and mark them ready to go
**
** **************************************************************/

void
elx_sched_init_hba(elxHBA_t * hba, uint16_t maxOutstanding)
{
	memset(&hba->hbaSched, 0, sizeof (hba->hbaSched));
	hba->hbaSched.maxOutstanding = maxOutstanding;
	hba->hbaSched.status = ELX_SCHED_STATUS_OKAYTOSEND;
	elx_sch_init_lock(hba);
}

void
elx_sched_target_init(ELXSCSITARGET_t * target, uint16_t maxOutstanding)
{
	memset(&target->targetSched, 0, sizeof (target->targetSched));
	target->targetSched.maxOutstanding = maxOutstanding;
	target->targetSched.status = ELX_SCHED_STATUS_OKAYTOSEND;
	return;
}

void
elx_sched_lun_init(ELXSCSILUN_t * lun, uint16_t maxOutstanding)
{
	memset(&lun->lunSched, 0, sizeof (lun->lunSched));
	lun->lunSched.maxOutstanding = maxOutstanding;
	lun->lunSched.status = ELX_SCHED_STATUS_OKAYTOSEND;

	return;
}

void
elx_sched_pause_target(ELXSCSITARGET_t * target)
{
	target->targetSched.status = ELX_SCHED_STATUS_PAUSED;

	return;
}

void
elx_sched_pause_hba(elxHBA_t * hba)
{
	hba->hbaSched.status = ELX_SCHED_STATUS_PAUSED;

	return;
}

void
elx_sched_continue_target(ELXSCSITARGET_t * target)
{
	unsigned long lockFlag;
	target->targetSched.status = ELX_SCHED_STATUS_OKAYTOSEND;
	ELX_SCH_LOCK(target->pHba, lockFlag);
	/* Make target the next ELXSCSITARGET_t to process */
	elx_sched_internal_check(target->pHba);
	ELX_SCH_UNLOCK(target->pHba, lockFlag);
	return;
}

void
elx_sched_continue_hba(elxHBA_t * hba)
{
	unsigned long lockFlag;
	hba->hbaSched.status = ELX_SCHED_STATUS_OKAYTOSEND;
	ELX_SCH_LOCK(hba, lockFlag);
	elx_sched_internal_check(hba);
	ELX_SCH_UNLOCK(hba, lockFlag);
	return;
}

void
elx_sched_sli_done(elxHBA_t * pHba,
		   ELX_IOCBQ_t * pIocbIn, ELX_IOCBQ_t * pIocbOut)
{
	ELX_SCSI_BUF_t *pCommand = (ELX_SCSI_BUF_t *) pIocbIn->context1;
	ELXSCSILUN_t *plun = pCommand->pLun;
	static int doNotCheck = 0;
	unsigned long lockFlag;
	elxCfgParam_t *clp;
	FCP_RSP *fcprsp;

	ELX_SCH_LOCK(pHba, lockFlag);
	plun->lunSched.currentOutstanding--;
	plun->pTarget->targetSched.currentOutstanding--;

	pCommand->result = pIocbOut->iocb.un.ulpWord[4];
	pCommand->status = pIocbOut->iocb.ulpStatus;
	pCommand->IOxri = pIocbOut->iocb.ulpContext;
	if (pCommand->status) {
		plun->errorcnt++;
	}
	plun->iodonecnt++;

	pHba->hbaSched.currentOutstanding--;
	ELX_SCH_UNLOCK(pHba, lockFlag);

	elx_pci_dma_sync((void *)pHba, (void *)pCommand->dma_ext,
			 1024, ELX_DMA_SYNC_FORCPU);

	fcprsp = pCommand->fcp_rsp;
	if ((pCommand->status == IOSTAT_FCP_RSP_ERROR) &&
	    (fcprsp->rspStatus3 == SCSI_STAT_QUE_FULL)) {

		/* Received Queue Full status from FCP device (tgt> <lun> */
		elx_printf_log(pHba->brd_no, &elx_msgBlk0736,	/* ptr to msg structure */
			       elx_mes0736,	/* ptr to msg */
			       elx_msgBlk0736.msgPreambleStr,	/* begin varargs */
			       pCommand->scsi_target, pCommand->scsi_lun, pCommand->qfull_retry_count, plun->qfull_retries, plun->lunSched.currentOutstanding, plun->lunSched.maxOutstanding);	/* end varargs */

		if ((plun->qfull_retries > 0) &&
		    (pCommand->qfull_retry_count < plun->qfull_retries)) {
			clp = &pHba->config[0];
			if (clp[ELX_CFG_DQFULL_THROTTLE_UP_TIME].a_current) {
				elx_scsi_lower_lun_qthrottle(pHba, pCommand);
			}
			if (plun->qfull_retry_interval > 0) {
				/*
				 * force to retrying after delay 1 second
				 */
				plun->qfull_tmo_id =
				    elx_clk_set(pHba, 0, elx_qfull_retry,
						pCommand, 0);
			} else {
				elx_qfull_retry(pHba, pCommand, 0);
			}
			pCommand->qfull_retry_count++;
			goto skipcmpl;
		}
	}

	(pCommand->cmd_cmpl) (pHba, pCommand);

      skipcmpl:

	ELX_SCH_LOCK(pHba, lockFlag);
	if (!doNotCheck) {
		doNotCheck = 1;
		elx_sched_internal_check(pHba);
		doNotCheck = 0;
	}
	ELX_SCH_UNLOCK(pHba, lockFlag);
	return;
}

void
elx_sched_check(elxHBA_t * hba)
{
	unsigned long lockFlag;

	ELX_SCH_LOCK(hba, lockFlag);
	elx_sched_internal_check(hba);
	ELX_SCH_UNLOCK(hba, lockFlag);

	return;
}

static void
elx_sched_internal_check(elxHBA_t * hba)
{
	ELX_SCHED_HBA_t *hbaSched = &hba->hbaSched;
	ELX_SLI_t *psli;
	ELX_NODELIST_t *ndlp;
	int numberOfFailedTargetChecks = 0;
	int didSuccessSubmit = 0;	/* SLI optimization for Port signals */
	int stopSched = 0;	/* used if SLI rejects on interloop */

	psli = &hba->sli;

	/* Service the High Priority Queue first */
	if (elx_tqs_getcount(&hba->hbaSched.highPriorityCmdList))
		elx_sched_service_high_priority_queue(hba);

	/* If targetCount is identically 0 then there are no Targets on the ring therefore
	   no pending commands on any LUN           
	 */
	if ((hbaSched->targetCount == 0) ||
	    (hbaSched->status == ELX_SCHED_STATUS_PAUSED))
		return;

	/* We are going to cycle through the Targets
	   on a round robin basis until we make a pass through
	   with nothing to schedule. 
	 */

	while ((stopSched == 0) &&
	       (hbaSched->currentOutstanding < hbaSched->maxOutstanding) &&
	       (numberOfFailedTargetChecks < hbaSched->targetCount)) {
		ELXSCSITARGET_t *target = hbaSched->nextTargetToCheck;
		ELX_SCHED_TARGET_t *targetSched = &target->targetSched;
		ELXSCSITARGET_t *newNext = targetSched->targetRing.q_f;
		int numberOfFailedLunChecks = 0;

		if ((targetSched->currentOutstanding <
		     targetSched->maxOutstanding)
		    && (targetSched->status != ELX_SCHED_STATUS_PAUSED)) {
			while (numberOfFailedLunChecks < targetSched->lunCount) {
				ELXSCSILUN_t *lun =
				    target->targetSched.nextLunToCheck;
				ELX_SCHED_LUN_t *lunSched = &lun->lunSched;
				ELXSCSILUN_t *newNextLun =
				    lunSched->lunRing.q_f;

				if ((lunSched->currentOutstanding <
				     lunSched->maxOutstanding)
				    && (lunSched->status !=
					ELX_SCHED_STATUS_PAUSED)) {
					ELX_SCSI_BUF_t *command;
					int sliStatus;
					ELX_IOCBQ_t *pIocbq;

					command =
					    elx_tqs_dequeuefirst(&lunSched->
								 commandList,
								 commandSched.
								 nextCommand);

					if (!command) {
						numberOfFailedLunChecks++;
						targetSched->nextLunToCheck =
						    newNextLun;
						continue;
					}

					ndlp = command->pLun->pnode;
					if (ndlp == 0) {
						numberOfFailedLunChecks++;
						elx_sched_queue_command(hba,
									command);
						targetSched->nextLunToCheck =
						    newNextLun;
						continue;
					}

					pIocbq = &command->cur_iocbq;
					/*  Current assumption is let SLI queue it until it busy us */

					pIocbq->context1 = command;
					pIocbq->iocb_cmpl = elx_sched_sli_done;

					/* put the RPI number and NODELIST info in the IOCB command */
					pIocbq->iocb.ulpContext = ndlp->nlp_rpi;
					if (ndlp->
					    nlp_fcp_info & NLP_FCP_2_DEVICE) {
						pIocbq->iocb.ulpFCP2Rcvy = 1;
					}
					pIocbq->iocb.ulpClass =
					    (ndlp->nlp_fcp_info & 0x0f);

					/* Get an iotag and finish setup of IOCB  */
					pIocbq->iocb.ulpIoTag =
					    elx_sli_next_iotag(hba,
							       &psli->
							       ring[psli->
								    fcp_ring]);
					if (pIocbq->iocb.ulpIoTag == 0) {
						stopSched = 1;
						elx_tqs_putfirst(&lunSched->
								 commandList,
								 command,
								 commandSched.
								 nextCommand);
						break;
					}

					sliStatus = elx_sli_issue_iocb(hba,
								       &psli->
								       ring
								       [psli->
									fcp_ring],
								       pIocbq,
								       SLI_IOCB_RET_IOCB);

					switch (sliStatus) {
					case IOCB_ERROR:
					case IOCB_BUSY:
						stopSched = 1;
						elx_tqs_putfirst(&lunSched->
								 commandList,
								 command,
								 commandSched.
								 nextCommand);
						break;

					case IOCB_SUCCESS:
						didSuccessSubmit = 1;
						lunSched->currentOutstanding++;
						targetSched->
						    currentOutstanding++;
						hbaSched->currentOutstanding++;
						targetSched->nextLunToCheck =
						    newNextLun;
						break;

					default:

						break;
					}

					/* 
					 * Check if there is any pending command on the lun. If not 
					 * remove the lun. If this is the last lun in the target, the
					 * target also will get removed from the scheduler ring.
					 */
					if (!lunSched->commandList.q_cnt)
						elx_sched_remove_lun_from_ring
						    (hba, lun);

					break;

					/* This brace ends LUN window open */
				} else {
					numberOfFailedLunChecks++;
					targetSched->nextLunToCheck =
					    newNextLun;
				}
				/* This brace ends While looping through LUNs on a Target */
			}

			if (numberOfFailedLunChecks >= targetSched->lunCount)
				numberOfFailedTargetChecks++;
			else
				numberOfFailedTargetChecks = 0;
		} /* if Target isn't pended */
		else
			numberOfFailedTargetChecks++;

		hbaSched->nextTargetToCheck = newNext;
	}			/* While looping through Targets on HBA */

	return;
}

void
elx_sched_service_high_priority_queue(struct elxHBA *hba)
{
	ELX_SLI_t *psli;
	ELX_NODELIST_t *ndlp;

	ELX_IOCBQ_t *pIocbq;
	ELX_SCSI_BUF_t *command;
	int sliStatus;

	psli = &hba->sli;

	/* 
	 * Iterate through highprioritycmdlist if any cmds waiting on it
	 * dequeue first cmd from highPriorityCmdList
	 * 
	 */
	while (elx_tqs_getcount(&hba->hbaSched.highPriorityCmdList)) {
		command =
		    elx_tqs_dequeuefirst(&hba->hbaSched.highPriorityCmdList,
					 commandSched.nextCommand);

		if (!command) {
			continue;
		}

		if ((command->pLun) && (command->pLun->pnode)) {

			ndlp = command->pLun->pnode;
			if (ndlp == 0) {

			} else {
				/* put the RPI number and NODELIST info in the IOCB command */
				pIocbq = &command->cur_iocbq;
				pIocbq->iocb.ulpContext = ndlp->nlp_rpi;
				if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
					pIocbq->iocb.ulpFCP2Rcvy = 1;
				}
				pIocbq->iocb.ulpClass =
				    (ndlp->nlp_fcp_info & 0x0f);
			}
		}

		pIocbq = &command->cur_iocbq;
		/*  Current assumption is let SLI queue it until it busy us */

		pIocbq->context1 = command;

		/* Fill in iocb completion callback  */
		pIocbq->iocb_cmpl = elx_sli_wake_iocb_high_priority;

		/* Fill in iotag if we don't have one yet */
		if (pIocbq->iocb.ulpIoTag == 0) {
			pIocbq->iocb.ulpIoTag = elx_sli_next_iotag(hba,
								   &psli->
								   ring[psli->
									fcp_ring]);
		}

		sliStatus = elx_sli_issue_iocb(hba,
					       &psli->ring[psli->fcp_ring],
					       pIocbq,
					       SLI_IOCB_HIGH_PRIORITY |
					       SLI_IOCB_RET_IOCB);

		switch (sliStatus) {
		case IOCB_ERROR:
		case IOCB_BUSY:
			/* We'll put it back to the head of the q and try again */
			elx_tqs_putfirst(&hba->hbaSched.highPriorityCmdList,
					 command, commandSched.nextCommand);
			break;

		case IOCB_SUCCESS:
			hba->hbaSched.currentOutstanding++;
			break;

		default:

			break;
		}

		break;
	}

	return;
}

ELX_SCSI_BUF_t *
elx_sched_dequeue(elxHBA_t * hba, ELX_SCSI_BUF_t * ourCommand)
{
	ELX_SCSI_BUF_t *previousCommand = NULL;
	ELX_SCSI_BUF_t *currentCommand;
	ELX_SCHED_LUN_t *pLunSched;
	unsigned long lockFlag;

	ELX_SCH_LOCK(hba, lockFlag);
	pLunSched = &ourCommand->pLun->lunSched;

	currentCommand = elx_tqs_getfirst(&pLunSched->commandList);
	while (currentCommand && currentCommand != ourCommand) {
		previousCommand = currentCommand;
		currentCommand = elx_tqs_getnext(currentCommand,
						 commandSched.nextCommand);
	}

	if (currentCommand == ourCommand) {	/* found it */
		elx_tqs_dequeue(&pLunSched->commandList, currentCommand,
				commandSched.nextCommand, previousCommand);
		if (elx_tqs_getcount(&pLunSched->commandList) == 0)
			elx_sched_remove_lun_from_ring(hba, ourCommand->pLun);
	}

	ELX_SCH_UNLOCK(hba, lockFlag);
	return (currentCommand);;
}

uint32_t
elx_sched_flush_command(elxHBA_t * pHba,
			ELX_SCSI_BUF_t * command,
			uint8_t iocbStatus, uint32_t word4)
{
	ELX_SCSI_BUF_t *foundCommand = elx_sched_dequeue(pHba, command);
	uint32_t found = 0;

	if (foundCommand) {
		IOCB_t *pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
		found++;
		pIOCB->ulpStatus = iocbStatus;
		foundCommand->status = iocbStatus;
		if (word4) {
			pIOCB->un.ulpWord[4] = word4;
			foundCommand->result = word4;
		}

		if (foundCommand->status) {
			foundCommand->pLun->errorcnt++;
		}
		foundCommand->pLun->iodonecnt++;

		(command->cmd_cmpl) (pHba, command);
	} else {
		/* if we couldn't find this command is not in the scheduler,
		   look for it in the SLI layer */
		if (elx_sli_abort_iocb_context1
		    (pHba, &pHba->sli.ring[pHba->sli.fcp_ring], command) == 0) {
			found++;
		}
	}

	return found;
}

uint32_t
elx_sched_flush_lun(elxHBA_t * pHba,
		    ELXSCSILUN_t * lun, uint8_t iocbStatus, uint32_t word4)
{
	int numberFlushed = 0;
	unsigned long lockFlag;

	ELX_SCH_LOCK(pHba, lockFlag);
	while (elx_tqs_getcount(&lun->lunSched.commandList)) {
		IOCB_t *pIOCB;
		ELX_SCSI_BUF_t *command =
		    elx_tqs_dequeuefirst(&lun->lunSched.commandList,
					 commandSched.nextCommand);
		pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
		pIOCB->ulpStatus = iocbStatus;
		command->status = iocbStatus;
		if (word4) {
			pIOCB->un.ulpWord[4] = word4;
			command->result = word4;
		}

		if (command->status) {
			lun->errorcnt++;
		}
		lun->iodonecnt++;

		(command->cmd_cmpl) (pHba, command);

		numberFlushed++;
	}
	elx_sched_remove_lun_from_ring(pHba, lun);
	ELX_SCH_UNLOCK(pHba, lockFlag);

	/* flush the SLI layer also */
	elx_sli_abort_iocb_lun(pHba, &pHba->sli.ring[pHba->sli.fcp_ring],
			       lun->pTarget->scsi_id, lun->lun_id);

	return (numberFlushed);
}

uint32_t
elx_sched_flush_target(elxHBA_t * pHba,
		       ELXSCSITARGET_t * target,
		       uint8_t iocbStatus, uint32_t word4)
{
	ELXSCSILUN_t *lun;
	int numberFlushed = 0;
	unsigned long lockFlag;

	ELX_SCH_LOCK(pHba, lockFlag);
	/* walk the list of LUNs on this target and flush each LUN.  We
	   accomplish this by pulling the first LUN off the head of the
	   queue until there aren't any LUNs left */
	while (target->targetSched.lunList) {
		lun = target->targetSched.lunList;

		while (elx_tqs_getcount(&lun->lunSched.commandList)) {
			IOCB_t *pIOCB;
			ELX_SCSI_BUF_t *command =
			    elx_tqs_dequeuefirst(&lun->lunSched.commandList,
						 commandSched.nextCommand);

			pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
			pIOCB->ulpStatus = iocbStatus;
			command->status = iocbStatus;
			if (word4) {
				pIOCB->un.ulpWord[4] = word4;
				command->result = word4;
			}

			if (command->status) {
				lun->errorcnt++;
			}
			lun->iodonecnt++;

			(command->cmd_cmpl) (pHba, command);

			numberFlushed++;
		}

		elx_sched_remove_lun_from_ring(pHba, lun);
	}
	elx_sched_remove_target_from_ring(pHba, target);
	ELX_SCH_UNLOCK(pHba, lockFlag);

	/* flush the SLI layer also */
	elx_sli_abort_iocb_tgt(pHba, &pHba->sli.ring[pHba->sli.fcp_ring],
			       target->scsi_id);

	return (numberFlushed);
}

uint32_t
elx_sched_flush_hba(elxHBA_t * pHba, uint8_t iocbStatus, uint32_t word4)
{
	int numberFlushed = 0;
	ELXSCSITARGET_t *target;
	ELXSCSILUN_t *lun;
	unsigned long lockFlag;

	ELX_SCH_LOCK(pHba, lockFlag);
	while (pHba->hbaSched.targetList) {
		target = pHba->hbaSched.targetList;

		while (target->targetSched.lunList) {
			lun = target->targetSched.lunList;

			while (elx_tqs_getcount(&lun->lunSched.commandList)) {
				IOCB_t *pIOCB;
				ELX_SCSI_BUF_t *command =
				    elx_tqs_dequeuefirst(&lun->lunSched.
							 commandList,
							 commandSched.
							 nextCommand);

				pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
				pIOCB->ulpStatus = iocbStatus;
				command->status = iocbStatus;
				if (word4) {
					pIOCB->un.ulpWord[4] = word4;
					command->result = word4;
				}

				if (command->status) {
					lun->errorcnt++;
				}
				lun->iodonecnt++;

				(command->cmd_cmpl) (pHba, command);

				numberFlushed++;
			}

			elx_sched_remove_lun_from_ring(pHba, lun);
		}
		elx_sched_remove_target_from_ring(pHba, target);
	}
	ELX_SCH_UNLOCK(pHba, lockFlag);

	/* flush the SLI layer also */
	elx_sli_abort_iocb_hba(pHba, &pHba->sli.ring[pHba->sli.fcp_ring]);

	return (numberFlushed);
}

void
elx_sched_submit_command(elxHBA_t * hba, ELX_SCSI_BUF_t * command)
{
	ELX_NODELIST_t *ndlp;
	uint16_t okayToSchedule = 1;
	unsigned long lockFlag;

	ELX_SCH_LOCK(hba, lockFlag);

	/* If we have a command see if we can cut through */
	if (command != 0) {

		/* Just some short cuts */
		ELX_SCHED_HBA_t *hbaSched = &hba->hbaSched;
		ELX_SCHED_LUN_t *lunSched = &command->pLun->lunSched;
		ELX_SCHED_TARGET_t *targetSched =
		    &command->pLun->pTarget->targetSched;
		ELX_IOCBQ_t *pIocbq = &command->cur_iocbq;
		ELX_SLI_t *psli = &hba->sli;

		/*    Set it up so SLI calls us when it is done       */

		ndlp = command->pLun->pnode;
		if (ndlp == 0) {
			/* For now, just requeue to scheduler if ndlp is not available yet */
			elx_sched_queue_command(hba, command);
			ELX_SCH_UNLOCK(hba, lockFlag);
			return;
		}

		pIocbq->context1 = command;
		pIocbq->iocb_cmpl = elx_sched_sli_done;

		/* put the RPI number and NODELIST info in the IOCB command */
		pIocbq->iocb.ulpContext = ndlp->nlp_rpi;
		if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
			pIocbq->iocb.ulpFCP2Rcvy = 1;
		}
		pIocbq->iocb.ulpClass = (ndlp->nlp_fcp_info & 0x0f);
		/* Get an iotag and finish setup of IOCB  */
		pIocbq->iocb.ulpIoTag = elx_sli_next_iotag(hba,
							   &psli->ring[psli->
								       fcp_ring]);

		if ((pIocbq->iocb.ulpIoTag != 0) &&
		    (hbaSched->currentOutstanding < hbaSched->maxOutstanding) &&
		    (hbaSched->status & ELX_SCHED_STATUS_OKAYTOSEND) &&
		    (targetSched->lunCount == 0) &&
		    (targetSched->currentOutstanding <
		     targetSched->maxOutstanding)
		    && (targetSched->status & ELX_SCHED_STATUS_OKAYTOSEND)
		    && (lunSched->currentOutstanding < lunSched->maxOutstanding)
		    && (lunSched->status & ELX_SCHED_STATUS_OKAYTOSEND)
		    ) {

			/* The scheduler, target and lun are all in a position to accept
			 * a send operation.  Call the SLI layer and issue the IOCB.
			 */

			int sliStatus;

			sliStatus =
			    elx_sli_issue_iocb(hba, &psli->ring[psli->fcp_ring],
					       pIocbq, SLI_IOCB_RET_IOCB);

			switch (sliStatus) {
			case IOCB_ERROR:
			case IOCB_BUSY:
				okayToSchedule = 0;
				elx_sched_queue_command(hba, command);
				break;
			case IOCB_SUCCESS:
				lunSched->currentOutstanding++;
				targetSched->currentOutstanding++;
				hbaSched->currentOutstanding++;
				break;
			default:

				break;
			}

			/* Remove this state to cause a scan of queues if submit worked. */
			okayToSchedule = 0;
		} else {
			/* This clause is execute only if there are outstanding
			 * commands in the scheduler.
			 */
			elx_sched_queue_command(hba, command);
		}
	}

	/* if(command) */
	/* We either queued something or someone called us to schedule
	   so now go schedule. */
	if (okayToSchedule)
		elx_sched_internal_check(hba);
	ELX_SCH_UNLOCK(hba, lockFlag);
	return;
}

void
elx_sched_queue_command(elxHBA_t * hba, ELX_SCSI_BUF_t * command)
{
	ELXSCSILUN_t *lun = command->pLun;
	ELX_SCHED_LUN_t *lunSched = &lun->lunSched;

	elx_tqs_enqueue(&lunSched->commandList, command,
			commandSched.nextCommand);
	elx_sched_add_lun_to_ring(hba, lun);

	return;
}

void
elx_sched_add_target_to_ring(elxHBA_t * hba, ELXSCSITARGET_t * target)
{
	ELX_SCHED_TARGET_t *targetSched = &target->targetSched;
	ELX_SCHED_HBA_t *hbaSched = &hba->hbaSched;

	if ((elx_tqd_onque(targetSched->targetRing)) ||	/* Already on list */
	    (targetSched->lunCount == 0)	/* nothing to schedule */
	    )
		return;

	elx_tqd_enque(target, hbaSched->targetList, targetSched.targetRing);
	if (hbaSched->targetCount == 0) {
		hbaSched->targetList = hbaSched->nextTargetToCheck = target;
	}
	hbaSched->targetCount++;
	return;
}

void
elx_sched_add_lun_to_ring(elxHBA_t * hba, ELXSCSILUN_t * lun)
{
	ELX_SCHED_LUN_t *lunSched = &lun->lunSched;
	ELXSCSITARGET_t *target = lun->pTarget;
	ELX_SCHED_TARGET_t *targetSched = &target->targetSched;

	if ((elx_tqd_onque(lunSched->lunRing)) ||	/* Already on list */
	    (elx_tqs_getcount(&lunSched->commandList) == 0)	/* nothing to schedule */
	    )
		return;

	elx_tqd_enque(lun, targetSched->lunList, lunSched.lunRing);

	if (targetSched->lunCount == 0) {
		targetSched->lunList = targetSched->nextLunToCheck = lun;
	}
	targetSched->lunCount++;
	elx_sched_add_target_to_ring(hba, target);
	return;
}

void
elx_sched_remove_target_from_ring(elxHBA_t * hba, ELXSCSITARGET_t * target)
{
	ELX_SCHED_TARGET_t *targetSched = &target->targetSched;
	ELX_SCHED_HBA_t *hbaSched = &hba->hbaSched;

	if (!elx_tqd_onque(targetSched->targetRing))
		return;		/* Not on Ring */
	hbaSched->targetCount--;
	if (hbaSched->targetCount) {	/*  Delink the LUN from the Ring */
		hbaSched->targetList = elx_tqd_getnext(targetSched->targetRing);	/* Just in case hba -> this target */
		if (hbaSched->nextTargetToCheck == target)
			hbaSched->nextTargetToCheck = hbaSched->targetList;
	} else
		hbaSched->targetList = NULL;
	elx_tqd_deque(target, targetSched.targetRing)
	    return;
}

void
elx_sched_remove_lun_from_ring(elxHBA_t * hba, ELXSCSILUN_t * lun)
{
	ELXSCSITARGET_t *target = lun->pTarget;
	ELX_SCHED_TARGET_t *targetSched = &target->targetSched;
	ELX_SCHED_LUN_t *lunSched = &lun->lunSched;

	if (!elx_tqd_onque(lunSched->lunRing))
		return;		/* Not on Ring  */
	targetSched->lunCount--;

	if (targetSched->lunCount) {	/*  Delink the LUN from the Ring */
		targetSched->lunList = elx_tqd_getnext(lunSched->lunRing);	/* Just in case target -> this lun */
		if (targetSched->nextLunToCheck == lun)
			targetSched->nextLunToCheck = targetSched->lunList;
	} else
		targetSched->lunList = NULL;	/*   Ring is empty */

	elx_tqd_deque(lun, lunSched.lunRing);

	if (!targetSched->lunCount)
		elx_sched_remove_target_from_ring(hba, target);

	return;
}

/* Functions required by the scsiport module. */

/* This routine allocates a scsi buffer, which contains all the necessary
 * information needed to initiate a SCSI I/O. The non-DMAable region of
 * the buffer contains the area to build the IOCB. The DMAable region contains
 * the memory for the FCP CMND, FCP RSP, and the inital BPL. 
 * In addition to allocating memeory, the FCP CMND and FCP RSP BDEs are setup
 * in the BPL and the BPL BDE is setup in the IOCB.
 */
ELX_SCSI_BUF_t *
elx_get_scsi_buf(elxHBA_t * phba)
{
	ELX_SCSI_BUF_t *psb;
	DMABUF_t *pdma;
	ULP_BDE64 *bpl;
	IOCB_t *cmd;
	uint8_t *ptr;
	elx_dma_addr_t pdma_phys;

	/* Get a SCSI buffer for an I/O */
	if ((psb = (ELX_SCSI_BUF_t *) elx_mem_get(phba, MEM_SCSI_BUF)) == 0) {
		return (0);
	}
	memset(psb, 0, sizeof (ELX_SCSI_BUF_t));

	/* Get a SCSI DMA extention for an I/O */
	/*
	 * The DMA buffer for FCP_CMND, FCP_RSP and BPL use MEM_SCSI_DMA_EXT
	 *  memory segment.
	 *
	 *    The size of MEM_BPL   = 1024 bytes.
	 *
	 *    The size of FCP_CMND  = 32 bytes.         
	 *    The size of FCP_RSP   = 160 bytes + 8 extra.         
	 *    The size of ULP_BDE64 = 12 bytes and driver can only support
	 *       ELX_SCSI_INITIAL_BPL_SIZE (65) S/G segments for scsi data.
	 *       One ULP_BDE64 is used for each of the FCP_CMND and FCP_RSP
	 *
	 *    Total usage for each I/O use 32 + 168 + (2 * 12) +
	 *    (65 * 12) = 1004 bytes.
	 */
	if ((pdma = (DMABUF_t *) elx_mem_get(phba, MEM_SCSI_DMA_EXT)) == 0) {
		elx_mem_put(phba, MEM_SCSI_BUF, (uint8_t *) psb);
		return (0);
	}
	/* Save DMABUF ptr for put routine */
	psb->dma_ext = pdma;

	/* This is used to save extra BPLs that are chained to pdma.
	 * Only used if I/O has more then 65 data segments.
	 */
	pdma->next = 0;

	/* Save virtual ptrs to FCP Command, Response, and BPL */
	ptr = (uint8_t *) pdma->virt;
	/* zero out MEM_SCSI_DMA_EXT buffer (MEM_BUF) which is 1024 bytes */
	memset(ptr, 0, 1024);
	psb->fcp_cmnd = (FCP_CMND *) ptr;
	ptr += sizeof (FCP_CMND);
	psb->fcp_rsp = (FCP_RSP *) ptr;
	ptr += (sizeof (FCP_RSP) + 0x8);	/* extra 8 to be safe */
	psb->fcp_bpl = (ULP_BDE64 *) ptr;
	psb->scsi_hba = phba;

	/* Since this is for a FCP cmd, the first 2 BDEs in the BPL are always
	 * the FCP CMND and FCP RSP, so lets just set it up right here.
	 */
	bpl = psb->fcp_bpl;
	/* ptr points to physical address of FCP CMD */
	pdma_phys = pdma->phys;
	bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(pdma_phys));
	bpl->addrLow = PCIMEM_LONG(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (FCP_CMND);
	bpl->tus.f.bdeFlags = BUFF_USE_CMND;
	bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
	bpl++;

	/* Setup FCP RSP */
	pdma_phys += sizeof (FCP_CMND);
	bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(pdma_phys));
	bpl->addrLow = PCIMEM_LONG(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (FCP_RSP);
	bpl->tus.f.bdeFlags = (BUFF_USE_CMND | BUFF_USE_RCV);
	bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
	bpl++;

	/* Since the IOCB for the FCP I/O is built into the ELX_SCSI_BUF_t,
	 * lets setup what we can right here.
	 */
	pdma_phys += (sizeof (FCP_RSP) + 0x8);
	cmd = &psb->cur_iocbq.iocb;
	cmd->un.fcpi64.bdl.ulpIoTag32 = 0;
	cmd->un.fcpi64.bdl.addrHigh = putPaddrHigh(pdma_phys);
	cmd->un.fcpi64.bdl.addrLow = putPaddrLow(pdma_phys);
	cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof (ULP_BDE64));
	cmd->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->ulpBdeCount = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpOwner = OWN_CHIP;

	return (psb);
}

/* This routine frees a scsi buffer, both DMAable and non-DMAable regions */
void
elx_free_scsi_buf(ELX_SCSI_BUF_t * psb)
{
	elxHBA_t *phba;
	DMABUF_t *pdma;
	DMABUF_t *pbpl;
	DMABUF_t *pnext;

	phba = psb->scsi_hba;
	if (psb) {
		if ((pdma = psb->dma_ext)) {
			/* Check to see if there were any extra buffers used to chain BPLs */
			pbpl = pdma->next;
			while (pbpl) {
				pnext = pbpl->next;
				elx_mem_put(phba, MEM_BPL, (uint8_t *) pbpl);
				pbpl = pnext;
			}
			elx_mem_put(phba, MEM_SCSI_DMA_EXT, (uint8_t *) pdma);
		}
		elx_mem_put(phba, MEM_SCSI_BUF, (uint8_t *) psb);
	}
	return;
}

ELXSCSILUN_t *
elx_find_lun_device(ELX_SCSI_BUF_t * elx_cmd)
{
	/* Search through the LUN list to find the LUN that has properties
	   matching those outlined in this function's parameters. */
	return elx_cmd->scsi_hba->elx_tran_find_lun(elx_cmd);
}

/*
 * Generic routine used the setup and initiate a SCSI I/O.
 */
int
elx_scsi_cmd_start(ELX_SCSI_BUF_t * elx_cmd)
{

	elxHBA_t *phba;
	ELX_SLI_t *psli;
	elxCfgParam_t *clp;
	ELX_IOCBQ_t *piocbq;
	IOCB_t *piocb;
	FCP_CMND *fcp_cmnd;
	ELXSCSILUN_t *lun_device;
	ELX_NODELIST_t *ndlp;

	/* interrupt coalescing - check for IOCB response ring
	   completions and handle any */

	/* map bus/target to lun-device pointer */
	/* This function will handle all mapping, LUN mapping, LUN masking, etc. */
	lun_device = elx_find_lun_device(elx_cmd);

	/* Make sure the HBA is online (cable plugged) and that this target
	   is not in an error recovery mode.
	 */
	if (lun_device == 0) {
		return FAILURE;
	}

	ndlp = (ELX_NODELIST_t *) lun_device->pTarget->pcontext;
	phba = lun_device->pHBA;

	if ((lun_device->pTarget->targetFlags & FC_NPR_ACTIVE) ||
	    (lun_device->pTarget->rptLunState == REPORT_LUN_ONGOING)) {
		/* Make sure the target is paused. */
		elx_sched_pause_target(lun_device->pTarget);
	} else {
		if ((lun_device->failMask & ELX_DEV_FATAL_ERROR) || (ndlp == 0)) {

			elx_cmd->result = 0;
			elx_cmd->status = IOSTAT_DRIVER_REJECT;
			elx_os_return_scsi_cmd(phba, elx_cmd);
			return 0;
		}
	}

	/* allocate an iocb command */
	piocbq = &(elx_cmd->cur_iocbq);
	piocb = &piocbq->iocb;

	clp = &phba->config[0];
	psli = &phba->sli;

	elx_cmd->pLun = lun_device;

	/* Note: ndlp may be 0 in recovery mode */
	elx_cmd->pLun->pnode = ndlp;
	elx_cmd->cmd_cmpl = elx_os_return_scsi_cmd;

	if (elx_os_prep_io(phba, elx_cmd)) {
		return 1;
	}

	/* ulpTimeout is only one byte */
	if (elx_cmd->timeout > 0xff) {
		/*
		 * The driver provides the timeout mechanism for this command.
		 */
		piocb->ulpTimeout = 0;
	} else {
		piocb->ulpTimeout = elx_cmd->timeout;
	}

	/*
	 * Setup driver timeout, in case the command does not complete
	 * Driver timeout should be greater than ulpTimeout
	 */

	piocbq->drvrTimeout = elx_cmd->timeout + ELX_DRVR_TIMEOUT;

	fcp_cmnd = elx_cmd->fcp_cmnd;
	putLunHigh(fcp_cmnd->fcpLunMsl, lun_device->lun_id);
	putLunLow(fcp_cmnd->fcpLunLsl, lun_device->lun_id);

	/*
	 * Setup addressing method
	 * The Logical Unit Addressing method is not supported at
	 * this current release.
	 */
	if (lun_device->pTarget->addrMode == VOLUME_SET_ADDRESSING) {
		fcp_cmnd->fcpLunMsl |= SWAP_DATA(0x40000000);
	}

	elx_pci_dma_sync((void *)phba, (void *)elx_cmd->dma_ext,
			 1024, ELX_DMA_SYNC_FORDEV);

	if (!(piocbq->iocb_flag & ELX_IO_POLL)) {
		lun_device->qcmdcnt++;
		/* Pass the command on down to the SLI layer. */
		elx_sched_submit_command(phba, elx_cmd);
	} else {
		int rc;

		piocbq->context1 = elx_cmd;
		piocbq->iocb_cmpl = elx_sched_sli_done;

		/* put the RPI number and NODELIST info in the IOCB command */
		piocbq->iocb.ulpContext = ndlp->nlp_rpi;
		if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
			piocbq->iocb.ulpFCP2Rcvy = 1;
		}
		piocbq->iocb.ulpClass = (ndlp->nlp_fcp_info & 0x0f);
		/* Get an iotag and finish setup of IOCB  */
		piocbq->iocb.ulpIoTag = elx_sli_next_iotag(phba,
							   &psli->ring[psli->
								       fcp_ring]);

		/* Poll for command completion */
		rc = elx_sli_issue_iocb(phba, &phba->sli.ring[psli->fcp_ring],
					piocbq,
					(SLI_IOCB_RET_IOCB | SLI_IOCB_POLL));
		return (rc);
	}

	/* Return success. */
	return 0;
}

int
elx_scsi_prep_task_mgmt_cmd(elxHBA_t * phba,
			    ELX_SCSI_BUF_t * elx_cmd, uint8_t task_mgmt_cmd)
{

	ELX_SLI_t *psli;
	elxCfgParam_t *clp;
	ELX_IOCBQ_t *piocbq;
	IOCB_t *piocb;
	FCP_CMND *fcp_cmnd;
	ELXSCSILUN_t *lun_device;
	ELX_NODELIST_t *ndlp;

	lun_device = elx_find_lun_device(elx_cmd);
	if (lun_device == 0) {
		return 0;
	}

	ndlp = (ELX_NODELIST_t *) lun_device->pTarget->pcontext;

	if ((lun_device->failMask & ELX_DEV_FATAL_ERROR) || (ndlp == 0)) {
		return 0;
	}

	/* allocate an iocb command */
	psli = &phba->sli;
	piocbq = &(elx_cmd->cur_iocbq);
	piocb = &piocbq->iocb;

	clp = &phba->config[0];

	fcp_cmnd = elx_cmd->fcp_cmnd;
	putLunHigh(fcp_cmnd->fcpLunMsl, lun_device->lun_id);
	putLunLow(fcp_cmnd->fcpLunLsl, lun_device->lun_id);
	if (lun_device->pTarget->addrMode == VOLUME_SET_ADDRESSING) {
		fcp_cmnd->fcpLunMsl |= SWAP_DATA(0x40000000);
	}
	fcp_cmnd->fcpCntl2 = task_mgmt_cmd;

	piocb->ulpIoTag =
	    elx_sli_next_iotag(phba, &phba->sli.ring[psli->fcp_ring]);
	piocb->ulpCommand = CMD_FCP_ICMND64_CR;

	piocb->ulpContext = ndlp->nlp_rpi;
	if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
		piocb->ulpFCP2Rcvy = 1;
	}
	piocb->ulpClass = (ndlp->nlp_fcp_info & 0x0f);

	/* ulpTimeout is only one byte */
	if (elx_cmd->timeout > 0xff) {
		/*
		 * Do not timeout the command at the firmware level.
		 * The driver will provide the timeout mechanism.
		 */
		piocb->ulpTimeout = 0;
	} else {
		piocb->ulpTimeout = elx_cmd->timeout;
	}

	lun_device->pnode = ndlp;
	elx_cmd->pLun = lun_device;

	switch (task_mgmt_cmd) {
	case LUN_RESET:
		/* Issue LUN Reset to TGT <num> LUN <num> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0703,	/* ptr to msg structure */
			       elx_mes0703,	/* ptr to msg */
			       elx_msgBlk0703.msgPreambleStr,	/* begin varargs */
			       elx_cmd->scsi_target, elx_cmd->scsi_lun, ndlp->nlp_rpi, ndlp->nlp_rflag);	/* end varargs */

		break;
	case ABORT_TASK_SET:
		/* Issue Abort Task Set to TGT <num> LUN <num> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0701,	/* ptr to msg structure */
			       elx_mes0701,	/* ptr to msg */
			       elx_msgBlk0701.msgPreambleStr,	/* begin varargs */
			       elx_cmd->scsi_target, elx_cmd->scsi_lun, ndlp->nlp_rpi, ndlp->nlp_rflag);	/* end varargs */

		break;
	case TARGET_RESET:
		/* Issue Target Reset to TGT <num> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0702,	/* ptr to msg structure */
			       elx_mes0702,	/* ptr to msg */
			       elx_msgBlk0702.msgPreambleStr,	/* begin varargs */
			       elx_cmd->scsi_target, ndlp->nlp_rpi, ndlp->nlp_rflag);	/* end varargs */
		break;
	}

	return (1);
}

/* returns:  0 if we successfully find and abort the command,
             1 if we couldn't find the command
*/
int
elx_scsi_cmd_abort(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{

	/* when this function returns, the command has been aborted and
	   returned to the OS, or it was returned before we could abort
	   it */

	/* tell the scheduler to find this command on LUN queue and remove
	   it.  It's up to the scheduler to remove the command from the SLI
	   layer. */
	if (elx_sched_flush_command(phba, elx_cmd, ELX_CMD_STATUS_ABORTED, 0)) {
		return 1;
	} else {
		/* couldn't find command - fail */

		return 0;
	}
}

int
elx_scsi_hba_reset(elxHBA_t * phba, uint32_t bus)
{

	/* reset bus */
	/* tell the scheduler to find all commands on this HBA/bus and
	   remove them.  It's up to the scheduler to remove the command
	   from the SLI layer. */
	if (!elx_sched_flush_hba(phba, ELX_CMD_STATUS_ABORTED, 0)) {
		/* REMOVE - the driver needs a log message here */
	}

	return 0;
}

int
elx_scsi_lun_reset(ELX_SCSI_BUF_t * external_cmd,
		   elxHBA_t * phba,
		   uint32_t bus, uint32_t target, uint64_t lun, uint32_t flag)
{
	ELX_SCSI_BUF_t *elx_cmd;
	ELX_IOCBQ_t *piocbq;
	ELX_SLI_t *psli;
	ELXSCSILUN_t *plun;
	ELX_IOCBQ_t *piocbqrsp = NULL;
	ELX_SCSI_BUF_t *internal_cmd = NULL;
	int ret = 0;

	/* Allocate command buf if internal command */
	if (!(flag & ELX_EXTERNAL_RESET)) {
		if ((internal_cmd = elx_get_scsi_buf(phba)) == 0) {
			return (FAILURE);
		}
		elx_cmd = internal_cmd;
	} else {
		elx_cmd = external_cmd;
	}

	elx_cmd->scsi_hba = phba;
	elx_cmd->scsi_bus = bus;
	elx_cmd->scsi_target = target;
	elx_cmd->scsi_lun = lun;

	/*
	 * Reset a device with either a LUN reset or an ABORT TASK
	 * reset depending on the caller's flag value.
	 */
	if (flag & ELX_ISSUE_LUN_RESET) {
		ret = elx_scsi_prep_task_mgmt_cmd(phba, elx_cmd, LUN_RESET);
	} else {
		if (flag & ELX_ISSUE_ABORT_TSET) {
			ret =
			    elx_scsi_prep_task_mgmt_cmd(phba, elx_cmd,
							ABORT_TASK_SET);
		} else {
			ret = 0;
		}
	}

	if (ret) {
		psli = &phba->sli;
		piocbq = &(elx_cmd->cur_iocbq);
		if (flag & ELX_EXTERNAL_RESET) {

			/* get a buffer for this response IOCB command */
			if ((piocbqrsp =
			     (ELX_IOCBQ_t *) elx_mem_get(phba,
							 MEM_IOCB)) == 0) {
				if (internal_cmd) {
					elx_free_scsi_buf(internal_cmd);
					internal_cmd = NULL;
					elx_cmd = NULL;
				}
				return (ENOMEM);
			}
			memset((void *)piocbqrsp, 0, sizeof (ELX_IOCBQ_t));

			piocbq->iocb_flag |= ELX_IO_POLL;
			piocbq->iocb_cmpl = elx_sli_wake_iocb_high_priority;

			ret =
			    elx_sli_issue_iocb_wait_high_priority(phba,
								  &phba->sli.
								  ring[psli->
								       fcp_ring],
								  piocbq,
								  SLI_IOCB_USE_TXQ,
								  piocbqrsp,
								  elx_cmd->
								  timeout +
								  ELX_DRVR_TIMEOUT);
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;

			elx_cmd->result = piocbqrsp->iocb.un.ulpWord[4];
			elx_cmd->status = piocbqrsp->iocb.ulpStatus;

			/* tell the scheduler to find all commands on this LUN queue and
			 * remove them.  It's up to the scheduler to remove the command
			 * from the SLI layer.
			 */
			plun = elx_find_lun_device(elx_cmd);
			if (plun) {
				if (!elx_sched_flush_lun
				    (phba, plun, ELX_CMD_STATUS_ABORTED, 0)) {

				}
			}
			/* Done with piocbqrsp, return to free list */
			if (piocbqrsp) {
				elx_mem_put(phba, MEM_IOCB,
					    (uint8_t *) piocbqrsp);
			}

			/* If this was an external lun reset, issue a message indicating
			 * its completion. 
			 */
			if (flag & ELX_ISSUE_LUN_RESET) {
				elx_printf_log(phba->brd_no, &elx_msgBlk0748,	/* ptr to msg structure */
					       elx_mes0748,	/* ptr to msg */
					       elx_msgBlk0748.msgPreambleStr,	/* begin varargs */
					       elx_cmd->scsi_target, elx_cmd->scsi_lun, ret, elx_cmd->status, elx_cmd->result);	/* end varargs */
			}
		} else {

			ret =
			    elx_sli_issue_iocb(phba,
					       &phba->sli.ring[psli->fcp_ring],
					       piocbq,
					       SLI_IOCB_HIGH_PRIORITY |
					       SLI_IOCB_RET_IOCB);
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;
		}
	}

	if (internal_cmd) {
		elx_free_scsi_buf(internal_cmd);
		internal_cmd = NULL;
		elx_cmd = NULL;
	}

	return (ret);

}

int
elx_scsi_tgt_reset(ELX_SCSI_BUF_t * external_cmd,
		   elxHBA_t * phba,
		   uint32_t bus, uint32_t target, uint32_t flag)
{
	ELX_SCSI_BUF_t *elx_cmd;
	ELX_IOCBQ_t *piocbq;
	ELX_SLI_t *psli;
	ELX_SCHED_HBA_t *phbaSched;
	ELXSCSITARGET_t *ptarget;
	ELXSCSILUN_t *plun;
	ELX_IOCBQ_t *piocbqrsp = NULL;
	ELX_SCSI_BUF_t *internal_cmd = NULL;
	int ret = 0;

	/* Allocate command buf if internal command */
	if (!(flag & ELX_EXTERNAL_RESET)) {
		if ((internal_cmd = elx_get_scsi_buf(phba)) == 0) {
			return (FAILURE);
		}
		elx_cmd = internal_cmd;
	} else {
		elx_cmd = external_cmd;
	}

	elx_cmd->scsi_hba = phba;
	elx_cmd->scsi_bus = bus;
	elx_cmd->scsi_target = target;

	/*
	 * target reset a device
	 */
	ret = elx_scsi_prep_task_mgmt_cmd(phba, elx_cmd, TARGET_RESET);
	if (ret) {
		psli = &phba->sli;
		piocbq = &(elx_cmd->cur_iocbq);
		if (flag & ELX_EXTERNAL_RESET) {

			/* get a buffer for this IOCB command response */
			if ((piocbqrsp =
			     (ELX_IOCBQ_t *) elx_mem_get(phba,
							 MEM_IOCB)) == 0) {
				if (internal_cmd) {
					elx_free_scsi_buf(internal_cmd);
					internal_cmd = NULL;
					elx_cmd = NULL;
				}
				return (ENOMEM);
			}
			memset((void *)piocbqrsp, 0, sizeof (ELX_IOCBQ_t));

			piocbq->iocb_flag |= ELX_IO_POLL;
			piocbq->iocb_cmpl = elx_sli_wake_iocb_high_priority;

			ret =
			    elx_sli_issue_iocb_wait_high_priority(phba,
								  &phba->sli.
								  ring[psli->
								       fcp_ring],
								  piocbq,
								  SLI_IOCB_HIGH_PRIORITY,
								  piocbqrsp,
								  elx_cmd->
								  timeout +
								  ELX_DRVR_TIMEOUT);
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;

			elx_cmd->result = piocbqrsp->iocb.un.ulpWord[4];
			elx_cmd->status = piocbqrsp->iocb.ulpStatus;

			/* tell the scheduler to find all commands on this Tgt queue and
			 * remove them.  It's up to the scheduler to remove the command
			 * from the SLI layer.
			 */
			plun = elx_find_lun_device(elx_cmd);
			if ((plun == 0) || (plun->pTarget == 0)) {

				phbaSched = &phba->hbaSched;
				ptarget = phbaSched->targetList;
				do {
					if ((ptarget == NULL)
					    || (ptarget->scsi_id == target)) {
						break;
					}
					ptarget =
					    ptarget->targetSched.targetRing.q_f;
				} while (ptarget != phbaSched->targetList);
			} else {
				ptarget = plun->pTarget;
			}

			if (ptarget) {
				if (!elx_sched_flush_target
				    (phba, ptarget, ELX_CMD_STATUS_ABORTED,
				     0)) {

				}
			}
			/* Done with piocbqrsp, return to free list */
			if (piocbqrsp) {
				elx_mem_put(phba, MEM_IOCB,
					    (uint8_t *) piocbqrsp);
			}
		} else {

			ret =
			    elx_sli_issue_iocb(phba,
					       &phba->sli.ring[psli->fcp_ring],
					       piocbq,
					       SLI_IOCB_HIGH_PRIORITY |
					       SLI_IOCB_RET_IOCB);
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;
		}
	}

	if (internal_cmd) {
		elx_free_scsi_buf(internal_cmd);
		internal_cmd = NULL;
		elx_cmd = NULL;
	}

	return (ret);
}

#include "lpfc_crtn.h"
void
elx_scsi_lower_lun_qthrottle(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{
	ELXSCSILUN_t *plun;
	elxCfgParam_t *clp;

	clp = &phba->config[0];
	plun = elx_cmd->pLun;

	if (plun->lunSched.maxOutstanding > ELX_MIN_QFULL) {
		if (plun->lunSched.currentOutstanding > ELX_MIN_QFULL) {
			/*
			 * knock the current queue throttle down to (active_io_count - 1)
			 */
			plun->lunSched.maxOutstanding =
			    plun->lunSched.currentOutstanding - 1;

			/*
			 * Delay ELX_NO_DEVICE_DELAY seconds before sending I/O this device again.
			 * stop_send_io will be decreament by 1 in lpfc_qthrottle_up();
			 */
			plun->stop_send_io =
			    clp[ELX_CFG_NO_DEVICE_DELAY].a_current;

			/*
			 * Kick off the lpfc_qthrottle_up()
			 */
			if (phba->dqfull_clk == 0) {
				phba->dqfull_clk = elx_clk_set(phba,
							       clp
							       [ELX_CFG_DQFULL_THROTTLE_UP_TIME].
							       a_current,
							       lpfc_qthrottle_up,
							       0, 0);
			}
		} else {
			plun->lunSched.maxOutstanding = ELX_MIN_QFULL;
		}
	}
}

void
elx_qfull_retry(elxHBA_t * phba, void *n1, void *n2)
{
	elx_sched_queue_command(phba, (ELX_SCSI_BUF_t *) n1);
}

int elx_sli_reset_on_init = 0;

int elx_sli_handle_mb_event(elxHBA_t *);
int elx_sli_handle_ring_event(elxHBA_t *, ELX_SLI_RING_t *, uint32_t);
int elx_sli_ringtx_put(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *);
int elx_sli_ringtxcmpl_put(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *);
ELX_IOCBQ_t *elx_sli_ringtx_get(elxHBA_t *, ELX_SLI_RING_t *);
ELX_IOCBQ_t *elx_sli_ringtxcmpl_get(elxHBA_t *, ELX_SLI_RING_t *,
				    ELX_IOCBQ_t *, uint32_t);
DMABUF_t *elx_sli_ringpostbuf_search(elxHBA_t *, ELX_SLI_RING_t *,
				     elx_dma_addr_t, int);

/* This will save a huge switch to determine if the IOCB cmd
 * is unsolicited or solicited.
 */
#define ELX_UNKNOWN_IOCB 0
#define ELX_UNSOL_IOCB   1
#define ELX_SOL_IOCB     2
#define ELX_ABORT_IOCB   3
uint8_t elx_sli_iocb_cmd_type[CMD_MAX_IOCB_CMD] = {
	ELX_UNKNOWN_IOCB,	/* 0x00 */
	ELX_UNSOL_IOCB,		/* CMD_RCV_SEQUENCE_CX     0x01 */
	ELX_SOL_IOCB,		/* CMD_XMIT_SEQUENCE_CR    0x02 */
	ELX_SOL_IOCB,		/* CMD_XMIT_SEQUENCE_CX    0x03 */
	ELX_SOL_IOCB,		/* CMD_XMIT_BCAST_CN       0x04 */
	ELX_SOL_IOCB,		/* CMD_XMIT_BCAST_CX       0x05 */
	ELX_UNKNOWN_IOCB,	/* CMD_QUE_RING_BUF_CN     0x06 */
	ELX_UNKNOWN_IOCB,	/* CMD_QUE_XRI_BUF_CX      0x07 */
	ELX_UNKNOWN_IOCB,	/* CMD_IOCB_CONTINUE_CN    0x08 */
	ELX_UNKNOWN_IOCB,	/* CMD_RET_XRI_BUF_CX      0x09 */
	ELX_SOL_IOCB,		/* CMD_ELS_REQUEST_CR      0x0A */
	ELX_SOL_IOCB,		/* CMD_ELS_REQUEST_CX      0x0B */
	ELX_UNKNOWN_IOCB,	/* 0x0C */
	ELX_UNSOL_IOCB,		/* CMD_RCV_ELS_REQ_CX      0x0D */
	ELX_ABORT_IOCB,		/* CMD_ABORT_XRI_CN        0x0E */
	ELX_ABORT_IOCB,		/* CMD_ABORT_XRI_CX        0x0F */
	ELX_ABORT_IOCB,		/* CMD_CLOSE_XRI_CR        0x10 */
	ELX_ABORT_IOCB,		/* CMD_CLOSE_XRI_CX        0x11 */
	ELX_SOL_IOCB,		/* CMD_CREATE_XRI_CR       0x12 */
	ELX_SOL_IOCB,		/* CMD_CREATE_XRI_CX       0x13 */
	ELX_SOL_IOCB,		/* CMD_GET_RPI_CN          0x14 */
	ELX_SOL_IOCB,		/* CMD_XMIT_ELS_RSP_CX     0x15 */
	ELX_SOL_IOCB,		/* CMD_GET_RPI_CR          0x16 */
	ELX_ABORT_IOCB,		/* CMD_XRI_ABORTED_CX      0x17 */
	ELX_SOL_IOCB,		/* CMD_FCP_IWRITE_CR       0x18 */
	ELX_SOL_IOCB,		/* CMD_FCP_IWRITE_CX       0x19 */
	ELX_SOL_IOCB,		/* CMD_FCP_IREAD_CR        0x1A */
	ELX_SOL_IOCB,		/* CMD_FCP_IREAD_CX        0x1B */
	ELX_SOL_IOCB,		/* CMD_FCP_ICMND_CR        0x1C */
	ELX_SOL_IOCB,		/* CMD_FCP_ICMND_CX        0x1D */
	ELX_UNKNOWN_IOCB,	/* 0x1E */
	ELX_SOL_IOCB,		/* CMD_FCP_TSEND_CX        0x1F */
	ELX_SOL_IOCB,		/* CMD_ADAPTER_MSG         0x20 */
	ELX_SOL_IOCB,		/* CMD_FCP_TRECEIVE_CX     0x21 */
	ELX_SOL_IOCB,		/* CMD_ADAPTER_DUMP        0x22 */
	ELX_SOL_IOCB,		/* CMD_FCP_TRSP_CX         0x23 */
	/* 0x24 - 0x80 */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	/* 0x30 */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	/* 0x40 */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	/* 0x50 */
	ELX_SOL_IOCB,
	ELX_SOL_IOCB,
	ELX_UNKNOWN_IOCB,
	ELX_SOL_IOCB,
	ELX_SOL_IOCB,
	ELX_UNSOL_IOCB,
	ELX_UNSOL_IOCB,
	ELX_SOL_IOCB,
	ELX_SOL_IOCB,

	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	/* 0x60 */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	/* 0x70 */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	/* 0x80 */
	ELX_UNKNOWN_IOCB,
	ELX_UNSOL_IOCB,		/* CMD_RCV_SEQUENCE64_CX   0x81 */
	ELX_SOL_IOCB,		/* CMD_XMIT_SEQUENCE64_CR  0x82 */
	ELX_SOL_IOCB,		/* CMD_XMIT_SEQUENCE64_CX  0x83 */
	ELX_SOL_IOCB,		/* CMD_XMIT_BCAST64_CN     0x84 */
	ELX_SOL_IOCB,		/* CMD_XMIT_BCAST64_CX     0x85 */
	ELX_UNKNOWN_IOCB,	/* CMD_QUE_RING_BUF64_CN   0x86 */
	ELX_UNKNOWN_IOCB,	/* CMD_QUE_XRI_BUF64_CX    0x87 */
	ELX_UNKNOWN_IOCB,	/* CMD_IOCB_CONTINUE64_CN  0x88 */
	ELX_UNKNOWN_IOCB,	/* CMD_RET_XRI_BUF64_CX    0x89 */
	ELX_SOL_IOCB,		/* CMD_ELS_REQUEST64_CR    0x8A */
	ELX_SOL_IOCB,		/* CMD_ELS_REQUEST64_CX    0x8B */
	ELX_ABORT_IOCB,		/* CMD_ABORT_MXRI64_CN     0x8C */
	ELX_UNSOL_IOCB,		/* CMD_RCV_ELS_REQ64_CX    0x8D */
	/* 0x8E - 0x94 */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_SOL_IOCB,		/* CMD_XMIT_ELS_RSP64_CX   0x95 */
	ELX_UNKNOWN_IOCB,	/* 0x96 */
	ELX_UNKNOWN_IOCB,	/* 0x97 */
	ELX_SOL_IOCB,		/* CMD_FCP_IWRITE64_CR     0x98 */
	ELX_SOL_IOCB,		/* CMD_FCP_IWRITE64_CX     0x99 */
	ELX_SOL_IOCB,		/* CMD_FCP_IREAD64_CR      0x9A */
	ELX_SOL_IOCB,		/* CMD_FCP_IREAD64_CX      0x9B */
	ELX_SOL_IOCB,		/* CMD_FCP_ICMND64_CR      0x9C */
	ELX_SOL_IOCB,		/* CMD_FCP_ICMND64_CX      0x9D */
	ELX_UNKNOWN_IOCB,	/* 0x9E */
	ELX_SOL_IOCB,		/* CMD_FCP_TSEND64_CX      0x9F */
	ELX_UNKNOWN_IOCB,	/* 0xA0 */
	ELX_SOL_IOCB,		/* CMD_FCP_TRECEIVE64_CX   0xA1 */
	ELX_UNKNOWN_IOCB,	/* 0xA2 */
	ELX_SOL_IOCB,		/* CMD_FCP_TRSP64_CX       0xA3 */
	/* 0xA4 - 0xC1 */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_SOL_IOCB,		/* CMD_GEN_REQUEST64_CR    0xC2 */
	ELX_SOL_IOCB,		/* CMD_GEN_REQUEST64_CX    0xC3 */
	/* 0xC4 - 0xCF */
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,

	ELX_SOL_IOCB,
	ELX_SOL_IOCB,		/* CMD_SENDTEXT_CR              0xD1 */
	ELX_SOL_IOCB,		/* CMD_SENDTEXT_CX              0xD2 */
	ELX_SOL_IOCB,		/* CMD_RCV_LOGIN                0xD3 */
	ELX_SOL_IOCB,		/* CMD_ACCEPT_LOGIN             0xD4 */
	ELX_SOL_IOCB,		/* CMD_REJECT_LOGIN             0xD5 */
	ELX_UNSOL_IOCB,
	/* 0xD7 - 0xDF */
	ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB, ELX_UNKNOWN_IOCB,
	/* 0xE0 */
	ELX_UNSOL_IOCB,
	ELX_SOL_IOCB,
	ELX_SOL_IOCB,
	ELX_SOL_IOCB,
	ELX_SOL_IOCB,
	ELX_UNSOL_IOCB
};

int
elx_sli_hba_setup(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_MBOXQ_t *pmb;
	int read_rev_reset, i, rc;
	uint32_t status;
	unsigned long iflag;

	psli = &phba->sli;

	/* Setep SLI interface for HBA register and HBA SLIM access */
	(psli->sliinit.elx_sli_setup_slim_access) (phba);

	/* Set board state to initialization started */
	phba->hba_state = ELX_INIT_START;
	read_rev_reset = 0;

	iflag = phba->iflag;
	ELX_DRVR_UNLOCK(phba, iflag);

	/* On some platforms/OS's, the driver can't rely on the state the adapter
	 * may be in.  For this reason, the driver is allowed to reset 
	 * the HBA before initialization.
	 */
	if (elx_sli_reset_on_init) {
		phba->hba_state = 0;	/* Don't skip post */
		elx_sli_brdreset(phba);
		phba->hba_state = ELX_INIT_START;
		if (elx_in_intr())
			mdelay(2500);
		else
			elx_sleep_ms(phba, 2500);
	}

      top:
	/* Read the HBA Host Status Register */
	status = (psli->sliinit.elx_sli_read_HS) (phba);

	i = 0;			/* counts number of times thru while loop */

	/* Check status register to see what current state is */
	while ((status & (HS_FFRDY | HS_MBRDY)) != (HS_FFRDY | HS_MBRDY)) {

		/* Check every 100ms for 5 retries, then every 500ms for 5, then
		 * every 2.5 sec for 5, then reset board and every 2.5 sec for 4.
		 */
		if (i++ >= 20) {
			/* Adapter failed to init, timeout, status reg <status> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0436,	/* ptr to msg structure */
				       elx_mes0436,	/* ptr to msg */
				       elx_msgBlk0436.msgPreambleStr,	/* begin varargs */
				       status);	/* end varargs */
			phba->hba_state = ELX_HBA_ERROR;
			ELX_DRVR_LOCK(phba, iflag);
			return (ETIMEDOUT);
		}

		/* Check to see if any errors occurred during init */
		if (status & HS_FFERM) {
			/* ERROR: During chipset initialization */
			/* Adapter failed to init, chipset, status reg <status> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0437,	/* ptr to msg structure */
				       elx_mes0437,	/* ptr to msg */
				       elx_msgBlk0437.msgPreambleStr,	/* begin varargs */
				       status);	/* end varargs */
			phba->hba_state = ELX_HBA_ERROR;
			ELX_DRVR_LOCK(phba, iflag);
			return (EIO);
		}

		if (i <= 5) {
			if (elx_in_intr())
				mdelay(100);
			else
				elx_sleep_ms(phba, 100);
		} else if (i <= 10) {
			if (elx_in_intr())
				mdelay(500);
			else
				elx_sleep_ms(phba, 500);
		} else {
			if (elx_in_intr())
				mdelay(2500);
			else
				elx_sleep_ms(phba, 2500);
		}

		if (i == 15) {
			phba->hba_state = 0;	/* Don't skip post */
			elx_sli_brdreset(phba);
			phba->hba_state = ELX_INIT_START;
		}
		/* Read the HBA Host Status Register */
		status = (psli->sliinit.elx_sli_read_HS) (phba);
	}

	/* Check to see if any errors occurred during init */
	if (status & HS_FFERM) {
		/* ERROR: During chipset initialization */
		/* Adapter failed to init, chipset, status reg <status> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0438,	/* ptr to msg structure */
			       elx_mes0438,	/* ptr to msg */
			       elx_msgBlk0438.msgPreambleStr,	/* begin varargs */
			       status);	/* end varargs */
		phba->hba_state = ELX_HBA_ERROR;
		ELX_DRVR_LOCK(phba, iflag);
		return (EIO);
	}

	/* Clear all interrupt enable conditions */
	(psli->sliinit.elx_sli_write_HC) (phba, 0);

	/* setup host attn register */
	(psli->sliinit.elx_sli_write_HA) (phba, 0xffffffff);

	/* Get a Mailbox buffer to setup mailbox commands for HBA initialization */
	if ((pmb =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, (MEM_MBOX | MEM_PRI))) == 0) {
		phba->hba_state = ELX_HBA_ERROR;
		ELX_DRVR_LOCK(phba, iflag);
		return (ENOMEM);
	}

	/* Call pre CONFIG_PORT mailbox command initialization.  A value of 0 
	 * means the call was successful.  Any other nonzero value is a failure,
	 * but if ERESTART is returned, the driver may reset the HBA and try again.
	 */
	if ((rc = (psli->sliinit.elx_sli_config_port_prep) (phba))) {
		if ((rc == ERESTART) && (read_rev_reset == 0)) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			phba->hba_state = 0;	/* Don't skip post */
			elx_sli_brdreset(phba);
			phba->hba_state = ELX_INIT_START;
			if (elx_in_intr())
				mdelay(500);
			else
				elx_sleep_ms(phba, 500);
			read_rev_reset = 1;
			goto top;
		}
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		ELX_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}

	/* Setup and issue mailbox CONFIG_PORT command */
	phba->hba_state = ELX_INIT_MBX_CMDS;
	elx_config_port(phba, pmb);
	if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> CONFIG_PORT, mbxStatus <status> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0442,	/* ptr to msg structure */
			       elx_mes0442,	/* ptr to msg */
			       elx_msgBlk0442.msgPreambleStr,	/* begin varargs */
			       pmb->mb.mbxCommand, pmb->mb.mbxStatus, 0);	/* end varargs */

		/* This clause gives the config_port call is given multiple chances to succeed. */
		if (read_rev_reset == 0) {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			phba->hba_state = 0;	/* Don't skip post */
			elx_sli_brdreset(phba);
			phba->hba_state = ELX_INIT_START;
			if (elx_in_intr())
				mdelay(2500);
			else
				elx_sleep_ms(phba, 2500);
			read_rev_reset = 1;
			goto top;
		}

		psli->sliinit.sli_flag &= ~ELX_SLI2_ACTIVE;
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		ELX_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}

	if ((rc = elx_sli_ring_map(phba))) {
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		ELX_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}
	psli->sliinit.sli_flag |= ELX_PROCESS_LA;

	/* Call post CONFIG_PORT mailbox command initialization. */
	if ((rc = (psli->sliinit.elx_sli_config_port_post) (phba))) {
		phba->hba_state = ELX_HBA_ERROR;
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		ELX_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	ELX_DRVR_LOCK(phba, iflag);
	return (0);
}

int
elx_sli_ring_map(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_MBOXQ_t *pmb;
	MAILBOX_t *pmbox;
	int i;

	psli = &phba->sli;

	/* Get a Mailbox buffer to setup mailbox commands for HBA initialization */
	if ((pmb =
	     (ELX_MBOXQ_t *) elx_mem_get(phba, (MEM_MBOX | MEM_PRI))) == 0) {
		phba->hba_state = ELX_HBA_ERROR;
		return (ENOMEM);
	}
	pmbox = &pmb->mb;

	/* Initialize the ELX_SLI_RING_t structure for each ring */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		/* Issue a CONFIG_RING mailbox command for each ring */
		phba->hba_state = ELX_INIT_MBX_CMDS;
		elx_config_ring(phba, i, pmb);
		if (elx_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/* Adapter failed to init, mbxCmd <cmd> CFG_RING, mbxStatus <status>, ring <num> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0446,	/* ptr to msg structure */
				       elx_mes0446,	/* ptr to msg */
				       elx_msgBlk0446.msgPreambleStr,	/* begin varargs */
				       pmbox->mbxCommand, pmbox->mbxStatus, i);	/* end varargs */
			phba->hba_state = ELX_HBA_ERROR;
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			return (ENXIO);
		}
	}
	elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	return (0);
}

int
elx_sli_intr(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	uint32_t ha_copy, status;
	int i;
	unsigned long iflag;

	psli = &phba->sli;
	psli->slistat.sliIntr++;
	ha_copy = (psli->sliinit.elx_sli_intr_prep) ((void *)phba);

	if (!ha_copy) {
		(psli->sliinit.elx_sli_intr_post) ((void *)phba);
		/*
		 * Don't claim that interrupt
		 */
		return (1);
	}

	ELX_SLI_LOCK(phba, iflag);

	if (ha_copy & HA_ERATT) {	/* Link / board error */
		psli->slistat.errAttnEvent++;
		/* do what needs to be done, get error from STATUS REGISTER */
		status = (psli->sliinit.elx_sli_read_HS) (phba);
		/* Clear Chip error bit */
		(psli->sliinit.elx_sli_write_HA) (phba, HA_ERATT);

		ELX_SLI_UNLOCK(phba, iflag);
		/* Process the Error Attention */
		(psli->sliinit.elx_sli_handle_eratt) (phba, status);
		return (0);
	}
	ELX_SLI_UNLOCK(phba, iflag);

	if (ha_copy & HA_MBATT) {	/* Mailbox interrupt */
		elx_sli_handle_mb_event(phba);
	}

	if (ha_copy & HA_LATT) {	/* Link Attention interrupt */
		/* Process the Link Attention */
		if (psli->sliinit.sli_flag & ELX_PROCESS_LA) {
			(psli->sliinit.elx_sli_handle_latt) (phba);
		}
	}

	/* Now process each ring */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];

		ELX_SLI_LOCK(phba, iflag);
		if ((ha_copy & HA_RXATT)
		    || (pring->flag & ELX_DEFERRED_RING_EVENT)) {
			if (pring->flag & ELX_STOP_IOCB_MASK) {
				pring->flag |= ELX_DEFERRED_RING_EVENT;
			} else {
				ELX_SLI_UNLOCK(phba, iflag);
				elx_sli_handle_ring_event(phba, pring,
							  (ha_copy &
							   HA_RXMASK));
				ELX_SLI_LOCK(phba, iflag);
				pring->flag &= ~ELX_DEFERRED_RING_EVENT;
			}
		}
		ELX_SLI_UNLOCK(phba, iflag);
		ha_copy = (ha_copy >> 4);
	}

	(psli->sliinit.elx_sli_intr_post) ((void *)phba);
	return (0);
}

int
elx_sli_chk_mbxCommand(uint8_t mbxCommand)
{
	uint8_t ret;

	switch (mbxCommand) {
	case MBX_LOAD_SM:
	case MBX_READ_NV:
	case MBX_WRITE_NV:
	case MBX_RUN_BIU_DIAG:
	case MBX_INIT_LINK:
	case MBX_DOWN_LINK:
	case MBX_CONFIG_LINK:
	case MBX_CONFIG_RING:
	case MBX_RESET_RING:
	case MBX_READ_CONFIG:
	case MBX_READ_RCONFIG:
	case MBX_READ_SPARM:
	case MBX_READ_STATUS:
	case MBX_READ_RPI:
	case MBX_READ_XRI:
	case MBX_READ_REV:
	case MBX_READ_LNK_STAT:
	case MBX_REG_LOGIN:
	case MBX_UNREG_LOGIN:
	case MBX_READ_LA:
	case MBX_CLEAR_LA:
	case MBX_DUMP_MEMORY:
	case MBX_DUMP_CONTEXT:
	case MBX_RUN_DIAGS:
	case MBX_RESTART:
	case MBX_UPDATE_CFG:
	case MBX_DOWN_LOAD:
	case MBX_DEL_LD_ENTRY:
	case MBX_RUN_PROGRAM:
	case MBX_SET_MASK:
	case MBX_SET_SLIM:
	case MBX_UNREG_D_ID:
	case MBX_CONFIG_FARP:
	case MBX_LOAD_AREA:
	case MBX_RUN_BIU_DIAG64:
	case MBX_CONFIG_PORT:
	case MBX_READ_SPARM64:
	case MBX_READ_RPI64:
	case MBX_REG_LOGIN64:
	case MBX_READ_LA64:
	case MBX_FLASH_WR_ULA:
	case MBX_SET_DEBUG:
	case MBX_LOAD_EXP_ROM:
		ret = mbxCommand;
		break;
	default:
		ret = MBX_SHUTDOWN;
		break;
	}
	return (ret);
}

int
elx_sli_handle_mb_event(elxHBA_t * phba)
{
	MAILBOX_t *mbox;
	MAILBOX_t *pmbox;
	ELX_MBOXQ_t *pmb;
	ELX_SLI_t *psli;
	PGP *pgp;
	ELX_SLI_RING_t *pring;
	int i;
	unsigned long iflag;
	uint32_t status;
	uint32_t portCmdGet, portGetIndex;

	psli = &phba->sli;
	/* We should only get here if we are in SLI2 mode */
	if (!(psli->sliinit.sli_flag & ELX_SLI2_ACTIVE)) {
		return (1);
	}

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 sizeof (MAILBOX_t), ELX_DMA_SYNC_FORCPU);
	psli->slistat.mboxEvent++;

	ELX_SLI_LOCK(phba, iflag);

	/* Get a Mailbox buffer to setup mailbox commands for callback */
	if ((pmb = psli->mbox_active)) {
		pmbox = &pmb->mb;
		mbox = (MAILBOX_t *) psli->MBhostaddr;

		/* First check out the status word */
		elx_sli_pcimem_bcopy((uint32_t *) mbox, (uint32_t *) pmbox,
				     sizeof (uint32_t));

		/* Sanity check to ensure the host owns the mailbox */
		if (pmbox->mbxOwner != OWN_HOST) {
			/* Lets try for a while */
			for (i = 0; i < 10240; i++) {
				elx_pci_dma_sync((void *)phba,
						 (void *)&phba->slim2p,
						 sizeof (MAILBOX_t),
						 ELX_DMA_SYNC_FORCPU);
				/* First copy command data */
				elx_sli_pcimem_bcopy((uint32_t *) mbox,
						     (uint32_t *) pmbox,
						     sizeof (uint32_t));
				if (pmbox->mbxOwner == OWN_HOST)
					goto mbout;
			}
			/* Stray Mailbox Interrupt, mbxCommand <cmd> mbxStatus <status> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0304,	/* ptr to msg structure */
				       elx_mes0304,	/* ptr to msg */
				       elx_msgBlk0304.msgPreambleStr,	/* begin varargs */
				       pmbox->mbxCommand, pmbox->mbxStatus);	/* end varargs */

			psli->sliinit.sli_flag |= ELX_SLI_MBOX_ACTIVE;
			ELX_SLI_UNLOCK(phba, iflag);
			return (1);
		}

	      mbout:
		if (psli->mbox_tmo) {
			elx_clk_can(phba, psli->mbox_tmo);
			psli->mbox_tmo = 0;
		}

		/*
		 * It is a fatal error if unknown mbox command completion.
		 */
		if (elx_sli_chk_mbxCommand(pmbox->mbxCommand) == MBX_SHUTDOWN) {

			/* Unknow mailbox command compl */
			elx_printf_log(phba->brd_no, &elx_msgBlk0323,	/* ptr to msg structure */
				       elx_mes0323,	/* ptr to msg */
				       elx_msgBlk0323.msgPreambleStr,	/* begin varargs */
				       pmbox->mbxCommand);	/* end varargs */
			phba->hba_state = ELX_HBA_ERROR;
			phba->hba_flag |= FC_STOP_IO;
			(psli->sliinit.elx_sli_handle_eratt) (phba, HS_FFER3);
			return (0);
		}

		psli->mbox_active = 0;
		if (pmbox->mbxStatus) {
			psli->slistat.mboxStatErr++;
			if (pmbox->mbxStatus == MBXERR_NO_RESOURCES) {
				/* Mbox cmd cmpl error - RETRYing */
				elx_printf_log(phba->brd_no, &elx_msgBlk0305,	/* ptr to msg structure */
					       elx_mes0305,	/* ptr to msg */
					       elx_msgBlk0305.msgPreambleStr,	/* begin varargs */
					       pmbox->mbxCommand, pmbox->mbxStatus, pmbox->un.varWords[0], phba->hba_state);	/* end varargs */
				pmbox->mbxStatus = 0;
				pmbox->mbxOwner = OWN_HOST;
				psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
				ELX_SLI_UNLOCK(phba, iflag);
				if (elx_sli_issue_mbox(phba, pmb, MBX_NOWAIT) ==
				    MBX_SUCCESS) {
					return (0);
				}
				ELX_SLI_LOCK(phba, iflag);
			}
		}

		/* Mailbox Cmpl, wd0 <pmbox> wd1 <varWord> wd2 <varWord> cmpl <mbox_cmpl) */
		elx_printf_log(phba->brd_no, &elx_msgBlk0307,	/* ptr to msg structure */
			       elx_mes0307,	/* ptr to msg */
			       elx_msgBlk0307.msgPreambleStr,	/* begin varargs */
			       *((uint32_t *) pmbox), pmbox->un.varWords[0], pmbox->un.varWords[1], pmb->mbox_cmpl);	/* end varargs */

		if (pmb->mbox_cmpl) {
			/* Copy entire mbox completion over buffer */
			elx_sli_pcimem_bcopy((uint32_t *) mbox,
					     (uint32_t *) pmbox,
					     (sizeof (uint32_t) *
					      (MAILBOX_CMD_WSIZE)));

			ELX_SLI_UNLOCK(phba, iflag);
			(pmb->mbox_cmpl) ((void *)phba, pmb);

			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
		}
	}

      top:
	psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
	/* Process next mailbox command if there is one */
	if ((pmb = elx_mbox_get(phba))) {
		ELX_SLI_UNLOCK(phba, iflag);
		if (elx_sli_issue_mbox(phba, pmb, MBX_NOWAIT) ==
		    MBX_NOT_FINISHED) {
			ELX_SLI_LOCK(phba, iflag);
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
			goto top;
		}
		ELX_SLI_LOCK(phba, iflag);
	} else {
		/* Turn on IOCB processing */
		for (i = 0; i < psli->sliinit.num_rings; i++) {
			pring = &psli->ring[i];
			pgp =
			    (PGP *) & (((MAILBOX_t *) psli->MBhostaddr)->us.s2.
				       port[i]);
			/* If the ring is active, flag it */
			if (psli->ring[i].cmdringaddr) {
				if (psli->ring[i].flag & ELX_STOP_IOCB_MBX) {
					psli->ring[i].flag &=
					    ~ELX_STOP_IOCB_MBX;
					ELX_SLI_UNLOCK(phba, iflag);
					portGetIndex =
					    elx_sli_resume_iocb(phba, pring);
					/* Make sure the host slim pointers are up-to-date before
					 * continuing.  An update is NOT guaranteed on the first read.
					 */
					status = pgp->cmdGetInx;
					portCmdGet = PCIMEM_LONG(status);
					if (portGetIndex != portCmdGet) {
						elx_sli_resume_iocb(phba,
								    pring);
					}
					ELX_SLI_LOCK(phba, iflag);

					/* If this is the FCP ring, the scheduler needs to be restarted. */
					if (pring->ringno == psli->fcp_ring) {
						elx_sched_check(phba);
					}
				}
			}
		}
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (0);
}

int
elx_sli_handle_ring_event(elxHBA_t * phba,
			  ELX_SLI_RING_t * pring, uint32_t mask)
{
	ELX_SLI_t *psli;
	IOCB_t *entry;
	IOCB_t *irsp = NULL;
	ELX_IOCBQ_t *rspiocbp;
	ELX_IOCBQ_t *cmdiocbp;
	ELX_IOCBQ_t *saveq;
	ELX_RING_INIT_t *pringinit;
	HGP *hgp;
	PGP *pgp;
	MAILBOX_t *mbox;
	uint32_t status;
	uint32_t portRspPut, portRspMax;
	uint32_t portCmdGet, portGetIndex;
	int ringno, i, loopcnt;
	uint8_t type;
	unsigned long iflag;
	int rc = 1;

	psli = &phba->sli;
	ringno = pring->ringno;
	psli->slistat.iocbEvent[ringno]++;

	/* At this point we assume SLI-2 */
	mbox = (MAILBOX_t *) psli->MBhostaddr;
	pgp = (PGP *) & mbox->us.s2.port[ringno];
	hgp = (HGP *) & mbox->us.s2.host[ringno];

	ELX_SLI_LOCK(phba, iflag);
	/* portRspMax is the number of rsp ring entries for this specific ring. */
	portRspMax = psli->sliinit.ringinit[ringno].numRiocb;

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORCPU);

	rspiocbp = 0;
	loopcnt = 0;

	/* Gather iocb entries off response ring.
	 * rspidx is the IOCB index of the next IOCB that the driver
	 * is going to process.
	 */
	entry = (IOCB_t *) IOCB_ENTRY(pring->rspringaddr, pring->rspidx);
	status = pgp->rspPutInx;
	portRspPut = PCIMEM_LONG(status);

	if (portRspPut >= portRspMax) {

		/* Ring <ringno> handler: portRspPut <portRspPut> is bigger then rsp ring <portRspMax> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0312,	/* ptr to msg structure */
			       elx_mes0312,	/* ptr to msg */
			       elx_msgBlk0312.msgPreambleStr,	/* begin varargs */
			       ringno, portRspPut, portRspMax);	/* end varargs */
		ELX_SLI_UNLOCK(phba, iflag);
		/*
		 * Treat it as adapter hardware error.
		 */
		phba->hba_state = ELX_HBA_ERROR;
		phba->hba_flag |= FC_STOP_IO;
		(psli->sliinit.elx_sli_handle_eratt) (phba, HS_FFER3);
		return (1);
	}

	/* Get the next available response iocb.
	 * rspidx is the IOCB index of the next IOCB that the driver
	 * is going to process.
	 */
	while (pring->rspidx != portRspPut) {
		/* get an iocb buffer to copy entry into */
		if ((rspiocbp =
		     (ELX_IOCBQ_t *) elx_mem_get(phba,
						 MEM_IOCB | MEM_PRI)) == NULL) {
			break;
		}

		elx_sli_pcimem_bcopy((uint32_t *) entry,
				     (uint32_t *) & rspiocbp->iocb,
				     sizeof (IOCB_t));
		irsp = &rspiocbp->iocb;

		/* bump iocb available response index */
		if (++pring->rspidx >= portRspMax) {
			pring->rspidx = 0;
		}

		/* Let the HBA know what IOCB slot will be the next one the driver
		 * will read a response from.
		 */
		if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
			status = (uint32_t) pring->rspidx;
			hgp->rspGetInx = PCIMEM_LONG(status);

			/* Since this may be expensive, sync it every 4 IOCBs */
			loopcnt++;
			if ((loopcnt & 0x3) == 0) {
				/* sync hgp->rspGetInx in the MAILBOX_t */
				elx_pci_dma_sync((void *)phba,
						 (void *)&phba->slim2p,
						 sizeof (MAILBOX_t),
						 ELX_DMA_SYNC_FORDEV);
			}
		} else {
			status = (uint32_t) pring->rspidx;
			(psli->sliinit.elx_sli_write_slim) ((void *)phba,
							    (void *)&status,
							    (int)((SLIMOFF +
								   (ringno *
								    2) +
								   1) * 4),
							    sizeof (uint32_t));
		}

		/* chain all iocb entries until LE is set */
		if (pring->iocb_continueq.q_first == NULL) {
			pring->iocb_continueq.q_first =
			    (ELX_SLINK_t *) rspiocbp;
			pring->iocb_continueq.q_last = (ELX_SLINK_t *) rspiocbp;
		} else {
			((ELX_IOCBQ_t *) (pring->iocb_continueq.q_last))->q_f =
			    (ELX_IOCBQ_t *) rspiocbp;
			pring->iocb_continueq.q_last = (ELX_SLINK_t *) rspiocbp;
		}
		rspiocbp->q_f = 0;
		pring->iocb_continueq.q_cnt++;

		/* when LE is set, entire Command has been received */
		if (irsp->ulpLe) {
			/* get a ptr to first iocb entry in chain and process it */
			saveq = (ELX_IOCBQ_t *) pring->iocb_continueq.q_first;
			irsp = &(saveq->iocb);

			pring->iocb_continueq.q_first = 0;
			pring->iocb_continueq.q_last = 0;
			pring->iocb_continueq.q_cnt = 0;

			psli->slistat.iocbRsp[ringno]++;

			/* Determine if IOCB command is a solicited or unsolicited event */
			type =
			    elx_sli_iocb_cmd_type[(irsp->
						   ulpCommand & CMD_IOCB_MASK)];
			if (type == ELX_SOL_IOCB) {
				/* Solicited Responses */
				/* Based on the iotag field, get the cmd IOCB from the txcmplq */
				if ((cmdiocbp =
				     elx_sli_ringtxcmpl_get(phba, pring, saveq,
							    0))) {
					/* Call the specified completion routine */
					if (cmdiocbp->iocb_cmpl) {
						ELX_SLI_UNLOCK(phba, iflag);
						(cmdiocbp->
						 iocb_cmpl) ((void *)phba,
							     cmdiocbp, saveq);
						ELX_SLI_LOCK(phba, iflag);
						if (cmdiocbp->
						    iocb_flag & ELX_IO_POLL) {
							rc = 0;
						}
					} else {
						elx_mem_put(phba, MEM_IOCB,
							    (uint8_t *)
							    cmdiocbp);
					}
				} else {
					/* Could not find the initiating command based of the
					 * response iotag.
					 */
					/* Ring <ringno> handler: unexpected completion IoTag <IoTag> */
					elx_printf_log(phba->brd_no, &elx_msgBlk0322,	/* ptr to msg structure */
						       elx_mes0322,	/* ptr to msg */
						       elx_msgBlk0322.msgPreambleStr,	/* begin varargs */
						       ringno, saveq->iocb.ulpIoTag, saveq->iocb.ulpStatus, saveq->iocb.un.ulpWord[4], saveq->iocb.ulpCommand, saveq->iocb.ulpContext);	/* end varargs */
				}
			} else if (type == ELX_UNSOL_IOCB) {
				WORD5 *w5p;
				uint32_t Rctl, Type;
				uint32_t match;

				match = 0;
				if ((irsp->ulpCommand == CMD_RCV_ELS_REQ64_CX)
				    || (irsp->ulpCommand ==
					CMD_RCV_ELS_REQ_CX)) {
					Rctl = FC_ELS_REQ;
					Type = FC_ELS_DATA;
				} else {
					w5p =
					    (WORD5 *) & (saveq->iocb.un.
							 ulpWord[5]);
					Rctl = w5p->hcsw.Rctl;
					Type = w5p->hcsw.Type;
				}
				/* unSolicited Responses */
				pringinit = &psli->sliinit.ringinit[ringno];
				if (pringinit->prt[0].profile) {
					/* If this ring has a profile set, just send it to prt[0] */
					ELX_SLI_UNLOCK(phba, iflag);
					(pringinit->prt[0].
					 elx_sli_rcv_unsol_event)
					    (phba, pring, saveq);
					ELX_SLI_LOCK(phba, iflag);
					match = 1;
				} else {
					/* We must search, based on rctl / type for the right routine */
					for (i = 0; i < pringinit->num_mask;
					     i++) {
						if ((pringinit->prt[i].rctl ==
						     Rctl)
						    && (pringinit->prt[i].
							type == Type)) {
							ELX_SLI_UNLOCK(phba,
								       iflag);
							(pringinit->prt[i].
							 elx_sli_rcv_unsol_event)
							    (phba, pring,
							     saveq);
							ELX_SLI_LOCK(phba,
								     iflag);
							match = 1;
							break;
						}
					}
				}
				if (match == 0) {
					/* Unexpected Rctl / Type received */
					/* Ring <ringno> handler: unexpected Rctl <Rctl> Type <Type> received */
					elx_printf_log(phba->brd_no, &elx_msgBlk0313,	/* ptr to msg structure */
						       elx_mes0313,	/* ptr to msg */
						       elx_msgBlk0313.msgPreambleStr,	/* begin varargs */
						       ringno, Rctl, Type);	/* end varargs */
				}
			} else if (type == ELX_ABORT_IOCB) {
				/* Solicited ABORT Responses */
				/* Based on the iotag field, get the cmd IOCB from the txcmplq */
				if ((irsp->ulpCommand != CMD_XRI_ABORTED_CX) &&
				    ((cmdiocbp =
				      elx_sli_ringtxcmpl_get(phba, pring, saveq,
							     0)))) {
					/* Call the specified completion routine */
					if (cmdiocbp->iocb_cmpl) {
						ELX_SLI_UNLOCK(phba, iflag);
						(cmdiocbp->
						 iocb_cmpl) ((void *)phba,
							     cmdiocbp, saveq);
						ELX_SLI_LOCK(phba, iflag);
					} else {
						elx_mem_put(phba, MEM_IOCB,
							    (uint8_t *)
							    cmdiocbp);
					}
				}
			} else if (type == ELX_UNKNOWN_IOCB) {
				if (irsp->ulpCommand == CMD_ADAPTER_MSG) {

					char adaptermsg[ELX_MAX_ADPTMSG];

					memset((void *)adaptermsg, 0,
					       ELX_MAX_ADPTMSG);
					memcpy(&adaptermsg[0], (uint8_t *) irsp,
					       MAX_MSG_DATA);
					elx_printf("elx%d: %s", phba->brd_no,
						   adaptermsg);
				} else {
					/* Unknown IOCB command */
					elx_printf_log(phba->brd_no, &elx_msgBlk0321,	/* ptr to msg struct */
						       elx_mes0321,	/* ptr to msg */
						       elx_msgBlk0321.msgPreambleStr,	/* begin varargs */
						       irsp->ulpCommand, irsp->ulpStatus, irsp->ulpIoTag, irsp->ulpContext);	/* end varargs */
				}
			}

			/* Free up iocb buffer chain for command just processed */
			while (saveq) {
				rspiocbp = saveq;
				saveq = (ELX_IOCBQ_t *) rspiocbp->q_f;
				elx_mem_put(phba, MEM_IOCB,
					    (uint8_t *) rspiocbp);
			}

		}
		/* Entire Command has been received */
		entry =
		    (IOCB_t *) IOCB_ENTRY(pring->rspringaddr, pring->rspidx);

		/* If the port response put pointer has not been updated, sync the pgp->rspPutInx
		 * in the MAILBOX_tand fetch the new port response put pointer.
		 */
		if (pring->rspidx == portRspPut) {
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 sizeof (MAILBOX_t),
					 ELX_DMA_SYNC_FORCPU);
			status = pgp->rspPutInx;
			portRspPut = PCIMEM_LONG(status);
		}
	}			/* while (pring->rspidx != portRspPut) */

	if ((rspiocbp != NULL) && (mask & HA_R0RE_REQ)) {
		/* At least one response entry has been freed */
		psli->slistat.iocbRspFull[ringno]++;
		/* SET RxRE_RSP in Chip Att register */
		status = ((CA_R0ATT | CA_R0RE_RSP) << (ringno * 4));
		(psli->sliinit.elx_sli_write_CA) (phba, status);
	}
	ELX_SLI_UNLOCK(phba, iflag);
	if ((mask & HA_R0CE_RSP) && (pring->flag & ELX_CALL_RING_AVAILABLE)) {
		pring->flag &= ~ELX_CALL_RING_AVAILABLE;
		psli->slistat.iocbCmdEmpty[ringno]++;
		portGetIndex = elx_sli_resume_iocb(phba, pring);

		/* Read the new portGetIndex value twice to ensure it was updated correctly. */
		status = pgp->cmdGetInx;
		portCmdGet = PCIMEM_LONG(status);
		if (portGetIndex != portCmdGet) {
			elx_sli_resume_iocb(phba, pring);
		}
		if ((psli->sliinit.ringinit[ringno].elx_sli_cmd_available))
			(psli->sliinit.ringinit[ringno].
			 elx_sli_cmd_available) (phba, pring);

		/* Restart the scheduler on the FCP ring. */
		if (pring->ringno == psli->fcp_ring) {
			elx_sched_check(phba);
		}
	}
	return (rc);
}

/*! elx_mbox_timeout
 * 
 * \pre
 * \post
 * \param hba Pointer to per elxHBA_t structure
 * \param l1  Pointer to the driver's mailbox queue.
 * \return 
 *   void
 *
 * \b Description:
 *
 * This routine handles mailbox timeout events at timer interrupt context.
 */
void
elx_mbox_timeout(elxHBA_t * phba, void *l1, void *l2)
{
	ELX_SLI_t *psli;
	ELX_MBOXQ_t *pmbox;
	MAILBOX_t *mb;
	unsigned long iflag;

	psli = &phba->sli;
	pmbox = (ELX_MBOXQ_t *) l1;
	mb = &pmbox->mb;

	/* Mbox cmd <mbxCommand> timeout */
	elx_printf_log(phba->brd_no, &elx_msgBlk0310,	/* ptr to msg structure */
		       elx_mes0310,	/* ptr to msg */
		       elx_msgBlk0310.msgPreambleStr,	/* begin varargs */
		       mb->mbxCommand, phba->hba_state, psli->sliinit.sli_flag, psli->mbox_active);	/* end varargs */

	ELX_SLI_LOCK(phba, iflag);
	if (psli->mbox_active == pmbox) {
		psli->mbox_active = 0;
		if (pmbox->mbox_cmpl) {
			ELX_SLI_UNLOCK(phba, iflag);
			mb->mbxStatus = MBX_NOT_FINISHED;
			(pmbox->mbox_cmpl) ((void *)phba, pmbox);

			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmbox);
		}
		psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
	}
	ELX_SLI_UNLOCK(phba, iflag);

	elx_mbox_abort(phba);
	return;
}

void
elx_mbox_abort(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_MBOXQ_t *pmbox;
	MAILBOX_t *mb;
	unsigned long iflag;

	psli = &phba->sli;

	ELX_SLI_LOCK(phba, iflag);

	if (psli->mbox_active) {
		if (psli->mbox_tmo) {
			elx_clk_can(phba, psli->mbox_tmo);
			psli->mbox_tmo = 0;
		}
		pmbox = psli->mbox_active;
		mb = &pmbox->mb;
		psli->mbox_active = 0;
		if (pmbox->mbox_cmpl) {
			ELX_SLI_UNLOCK(phba, iflag);
			mb->mbxStatus = MBX_NOT_FINISHED;
			(pmbox->mbox_cmpl) ((void *)phba, pmbox);

			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmbox);
		}
		psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
	}

	/* Abort all the non active mailbox commands. */
	pmbox = elx_mbox_get(phba);
	while (pmbox) {
		mb = &pmbox->mb;
		if (pmbox->mbox_cmpl) {
			ELX_SLI_UNLOCK(phba, iflag);
			mb->mbxStatus = MBX_NOT_FINISHED;
			(pmbox->mbox_cmpl) ((void *)phba, pmbox);

			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmbox);
		}
		pmbox = elx_mbox_get(phba);
	}

	ELX_SLI_UNLOCK(phba, iflag);

	return;
}

int
elx_sli_issue_mbox(elxHBA_t * phba, ELX_MBOXQ_t * pmbox, uint32_t flag)
{
	MAILBOX_t *mbox;
	MAILBOX_t *mb;
	ELX_SLI_t *psli;
	uint32_t status, evtctr;
	uint32_t ha_copy;
	int i;
	unsigned long drvr_flag;
	unsigned long iflag;
	volatile uint32_t word0, ldata;

	psli = &phba->sli;
	if (flag & MBX_POLL) {
		ELX_DRVR_LOCK(phba, drvr_flag);
	}

	ELX_SLI_LOCK(phba, iflag);

	mb = &pmbox->mb;
	status = MBX_SUCCESS;

	if (psli->sliinit.sli_flag & ELX_SLI_MBOX_ACTIVE) {
		/* Polling for a mbox command when another one is
		 * already active is not allowed in SLI. Also, the driver must 
		 * have established SLI2 mode to queue and process multiple mbox commands.
		 */

		if (flag & MBX_POLL) {
			psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
			ELX_SLI_UNLOCK(phba, iflag);
			ELX_DRVR_UNLOCK(phba, drvr_flag);
			goto mbxerr;
		}

		if (!(psli->sliinit.sli_flag & ELX_SLI2_ACTIVE)) {
			psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
			ELX_SLI_UNLOCK(phba, iflag);
			goto mbxerr;
		}

		/* Handle STOP IOCB processing flag. This is only meaningful
		 * if we are not polling for mbox completion.
		 */
		if (flag & MBX_STOP_IOCB) {
			flag &= ~MBX_STOP_IOCB;
			/* Now flag each ring */
			for (i = 0; i < psli->sliinit.num_rings; i++) {
				/* If the ring is active, flag it */
				if (psli->ring[i].cmdringaddr) {
					psli->ring[i].flag |= ELX_STOP_IOCB_MBX;
				}
			}
		}

		/* Another mailbox command is still being processed, queue this
		 * command to be processed later.
		 */
		elx_mbox_put(phba, pmbox);

		/* Mbox cmd issue - BUSY */
		elx_printf_log(phba->brd_no, &elx_msgBlk0308,	/* ptr to msg structure */
			       elx_mes0308,	/* ptr to msg */
			       elx_msgBlk0308.msgPreambleStr,	/* begin varargs */
			       mb->mbxCommand, phba->hba_state, psli->sliinit.sli_flag, flag);	/* end varargs */

		psli->slistat.mboxBusy++;
		ELX_SLI_UNLOCK(phba, iflag);
		if (flag == MBX_POLL) {
			ELX_DRVR_UNLOCK(phba, drvr_flag);
		}
		return (MBX_BUSY);
	}

	/* Handle STOP IOCB processing flag. This is only meaningful
	 * if we are not polling for mbox completion.
	 */
	if (flag & MBX_STOP_IOCB) {
		flag &= ~MBX_STOP_IOCB;
		if (flag == MBX_NOWAIT) {
			/* Now flag each ring */
			for (i = 0; i < psli->sliinit.num_rings; i++) {
				/* If the ring is active, flag it */
				if (psli->ring[i].cmdringaddr) {
					psli->ring[i].flag |= ELX_STOP_IOCB_MBX;
				}
			}
		}
	}

	psli->sliinit.sli_flag |= ELX_SLI_MBOX_ACTIVE;

	/* If we are not polling, we MUST be in SLI2 mode */
	if (flag != MBX_POLL) {
		if (!(psli->sliinit.sli_flag & ELX_SLI2_ACTIVE)) {
			psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
			ELX_SLI_UNLOCK(phba, iflag);
		      mbxerr:
			/* Mbox command <mbxCommand> cannot issue */
			elx_printf_log(phba->brd_no, &elx_msgBlk0311,	/* ptr to msg structure */
				       elx_mes0311,	/* ptr to msg */
				       elx_msgBlk0311.msgPreambleStr,	/* begin varargs */
				       mb->mbxCommand, phba->hba_state, psli->sliinit.sli_flag, flag);	/* end varargs */
			return (MBX_NOT_FINISHED);
		}
		/* timeout active mbox command */
		if (psli->mbox_tmo) {
			elx_clk_res(phba, ELX_MBOX_TMO, psli->mbox_tmo);
		} else {
			psli->mbox_tmo = elx_clk_set(phba, ELX_MBOX_TMO,
						     elx_mbox_timeout, pmbox,
						     0);
		}
	}

	/* Mailbox cmd <cmd> issue */
	elx_printf_log(phba->brd_no, &elx_msgBlk0309,	/* ptr to msg structure */
		       elx_mes0309,	/* ptr to msg */
		       elx_msgBlk0309.msgPreambleStr,	/* begin varargs */
		       mb->mbxCommand, phba->hba_state, psli->sliinit.sli_flag, flag);	/* end varargs */

	psli->slistat.mboxCmd++;
	evtctr = psli->slistat.mboxEvent;

	/* next set own bit for the adapter and copy over command word */
	mb->mbxOwner = OWN_CHIP;

	if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
		/* First copy command data to host SLIM area */
		mbox = (MAILBOX_t *) psli->MBhostaddr;
		elx_sli_pcimem_bcopy((uint32_t *) mb, (uint32_t *) mbox,
				     (sizeof (uint32_t) * (MAILBOX_CMD_WSIZE)));
		elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
				 sizeof (MAILBOX_t), ELX_DMA_SYNC_FORDEV);

	} else {
		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* copy command data into host mbox for cmpl */
			mbox = (MAILBOX_t *) psli->MBhostaddr;
			elx_sli_pcimem_bcopy((uint32_t *) mb, (uint32_t *) mbox,
					     (sizeof (uint32_t) *
					      (MAILBOX_CMD_WSIZE)));
		}

		/* First copy mbox command data to HBA SLIM, skip past first word */
		(psli->sliinit.elx_sli_write_slim) ((void *)phba,
						    (void *)&mb->un.varWords[0],
						    sizeof (uint32_t),
						    ((MAILBOX_CMD_WSIZE -
						      1) * sizeof (uint32_t)));

		/* Next copy over first word, with mbxOwner set */
		ldata = *((volatile uint32_t *)mb);
		(psli->sliinit.elx_sli_write_slim) ((void *)phba,
						    (void *)&ldata, 0,
						    sizeof (uint32_t));

		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* switch over to host mailbox */
			psli->sliinit.sli_flag |= ELX_SLI2_ACTIVE;
		}
	}

	/* interrupt board to doit right away */
	(psli->sliinit.elx_sli_write_CA) (phba, CA_MBATT);

	switch (flag) {
	case MBX_NOWAIT:
		/* Don't wait for it to finish, just return */
		psli->mbox_active = pmbox;
		break;

	case MBX_POLL:
		i = 0;
		psli->mbox_active = 0;
		if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 sizeof (MAILBOX_t),
					 ELX_DMA_SYNC_FORCPU);

			/* First read mbox status word */
			mbox = (MAILBOX_t *) psli->MBhostaddr;
			word0 = *((volatile uint32_t *)mbox);
			word0 = PCIMEM_LONG(word0);
		} else {
			/* First read mbox status word */
			(psli->sliinit.elx_sli_read_slim) ((void *)phba,
							   (void *)&word0, 0,
							   sizeof (uint32_t));
		}

		/* Read the HBA Host Attention Register */
		ha_copy = (psli->sliinit.elx_sli_read_HA) (phba);

		/* Wait for command to complete */
		while (((word0 & OWN_CHIP) == OWN_CHIP)
		       || !(ha_copy & HA_MBATT)) {
			if (i++ >= 100) {
				psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;

				ELX_SLI_UNLOCK(phba, iflag);
				ELX_DRVR_UNLOCK(phba, drvr_flag);
				return (MBX_NOT_FINISHED);
			}

			/* Check if we took a mbox interrupt while we were polling */
			if (((word0 & OWN_CHIP) != OWN_CHIP)
			    && (evtctr != psli->slistat.mboxEvent))
				break;

			ELX_SLI_UNLOCK(phba, iflag);
			ELX_DRVR_UNLOCK(phba, drvr_flag);

			/* If in interrupt context do not sleep */
			if (elx_in_intr())
				mdelay(i);
			else
				elx_sleep_ms(phba, i);

			ELX_DRVR_LOCK(phba, drvr_flag);
			ELX_SLI_LOCK(phba, iflag);

			if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
				elx_pci_dma_sync((void *)phba,
						 (void *)&phba->slim2p,
						 sizeof (MAILBOX_t),
						 ELX_DMA_SYNC_FORCPU);

				/* First copy command data */
				mbox = (MAILBOX_t *) psli->MBhostaddr;
				word0 = *((volatile uint32_t *)mbox);
				word0 = PCIMEM_LONG(word0);
				if (mb->mbxCommand == MBX_CONFIG_PORT) {
					MAILBOX_t *slimmb;
					volatile uint32_t slimword0;
					/* Check real SLIM for any errors */
					(psli->sliinit.
					 elx_sli_read_slim) ((void *)phba,
							     (void *)&slimword0,
							     0,
							     sizeof (uint32_t));
					slimmb = (MAILBOX_t *) & slimword0;
					if (((slimword0 & OWN_CHIP) != OWN_CHIP)
					    && slimmb->mbxStatus) {
						psli->sliinit.sli_flag &=
						    ~ELX_SLI2_ACTIVE;
						word0 = slimword0;
					}
				}
			} else {
				/* First copy command data */
				(psli->sliinit.elx_sli_read_slim) ((void *)phba,
								   (void *)
								   &word0, 0,
								   sizeof
								   (uint32_t));
			}
			/* Read the HBA Host Attention Register */
			ha_copy = (psli->sliinit.elx_sli_read_HA) (phba);
		}

		if (psli->sliinit.sli_flag & ELX_SLI2_ACTIVE) {
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 sizeof (MAILBOX_t),
					 ELX_DMA_SYNC_FORCPU);

			/* First copy command data */
			mbox = (MAILBOX_t *) psli->MBhostaddr;
			/* copy results back to user */
			elx_sli_pcimem_bcopy((uint32_t *) mbox, (uint32_t *) mb,
					     (sizeof (uint32_t) *
					      MAILBOX_CMD_WSIZE));
		} else {
			/* First copy command data */
			(psli->sliinit.elx_sli_read_slim) ((void *)phba,
							   (void *)mb, 0,
							   (sizeof (uint32_t) *
							    (MAILBOX_CMD_WSIZE)));
		}

		(psli->sliinit.elx_sli_write_HA) (phba, HA_MBATT);

		psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
		status = mb->mbxStatus;
	}

	ELX_SLI_UNLOCK(phba, iflag);
	if (flag == MBX_POLL) {
		ELX_DRVR_UNLOCK(phba, drvr_flag);
	}
	return (status);
}

int
elx_sli_issue_iocb(elxHBA_t * phba,
		   ELX_SLI_RING_t * pring, ELX_IOCBQ_t * piocb, uint32_t flag)
{
	ELX_SLI_t *psli;
	IOCB_t *iocb;
	IOCB_t *icmd = NULL;
	HGP *hgp;
	PGP *pgp;
	MAILBOX_t *mbox;
	ELX_IOCBQ_t *nextiocb;
	ELX_IOCBQ_t *lastiocb;
	uint32_t status;
	int ringno, loopcnt;
	uint32_t portCmdGet, portCmdMax;
	unsigned long iflag;

	psli = &phba->sli;
	ringno = pring->ringno;

	/* At this point we assume SLI-2 */
	mbox = (MAILBOX_t *) psli->MBhostaddr;
	pgp = (PGP *) & mbox->us.s2.port[ringno];
	hgp = (HGP *) & mbox->us.s2.host[ringno];

	ELX_SLI_LOCK(phba, iflag);

	/* portCmdMax is the number of cmd ring entries for this specific ring. */
	portCmdMax = psli->sliinit.ringinit[ringno].numCiocb;

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORCPU);

	/* portCmdGet is the IOCB index of the next IOCB that the HBA
	 * is going to process.
	 */
	status = pgp->cmdGetInx;
	portCmdGet = PCIMEM_LONG(status);

	/* We should never get an IOCB if we are in a < LINK_DOWN state */
	if (phba->hba_state < ELX_LINK_DOWN) {
		/* If link is not initialized, just return */

		ELX_SLI_UNLOCK(phba, iflag);
		return (IOCB_ERROR);
	}

	/* Check to see if we are blocking IOCB processing because of a
	 * outstanding mbox command.
	 */
	if (pring->flag & ELX_STOP_IOCB_MBX) {
		/* Queue command to ring xmit queue */
		if (!(flag & SLI_IOCB_RET_IOCB)) {
			elx_sli_ringtx_put(phba, pring, piocb);
		}
		psli->slistat.iocbCmdDelay[ringno]++;
		ELX_SLI_UNLOCK(phba, iflag);
		return (IOCB_BUSY);
	}

	if (phba->hba_state == ELX_LINK_DOWN) {
		icmd = &piocb->iocb;
		if ((icmd->ulpCommand == CMD_QUE_RING_BUF_CN) ||
		    (icmd->ulpCommand == CMD_QUE_RING_BUF64_CN) ||
		    (icmd->ulpCommand == CMD_CLOSE_XRI_CN) ||
		    (icmd->ulpCommand == CMD_ABORT_XRI_CN)) {
			/* For IOCBs, like QUE_RING_BUF, that have no rsp ring 
			 * completion, iocb_cmpl MUST be 0.
			 */
			if (piocb->iocb_cmpl) {
				piocb->iocb_cmpl = 0;
			}
		} else {
			if ((icmd->ulpCommand != CMD_CREATE_XRI_CR)) {
				/* Queue command to ring xmit queue */
				if (!(flag & SLI_IOCB_RET_IOCB)) {
					elx_sli_ringtx_put(phba, pring, piocb);
				}

				/* If link is down, just return */
				psli->slistat.iocbCmdDelay[ringno]++;
				ELX_SLI_UNLOCK(phba, iflag);
				return (IOCB_BUSY);
			}
		}
		/* Only CREATE_XRI and QUE_RING_BUF can be issued if the link
		 * is not up.
		 */
	} else {
		/* For FCP commands, we must be in a state where we can process
		 * link attention events.
		 */
		if (!(psli->sliinit.sli_flag & ELX_PROCESS_LA) &&
		    (pring->ringno == psli->fcp_ring)) {
			/* Queue command to ring xmit queue */
			if (!(flag & SLI_IOCB_RET_IOCB)) {
				elx_sli_ringtx_put(phba, pring, piocb);
			}
			psli->slistat.iocbCmdDelay[ringno]++;
			ELX_SLI_UNLOCK(phba, iflag);
			return (IOCB_BUSY);
		}
	}

	/* onetime should only be set for QUE_RING_BUF or CREATE_XRI
	 * iocbs sent with link down.
	 */

	/* Get the next available command iocb.
	 * cmdidx is the IOCB index of the next IOCB that the driver
	 * is going to issue a command with.
	 */
	iocb = (IOCB_t *) IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

	if (portCmdGet >= portCmdMax) {

		/* Ring <ringno> issue: portCmdGet <portCmdGet> is bigger then cmd ring <portCmdMax> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0314,	/* ptr to msg structure */
			       elx_mes0314,	/* ptr to msg */
			       elx_msgBlk0314.msgPreambleStr,	/* begin varargs */
			       ringno, portCmdGet, portCmdMax);	/* end varargs */
		/* Queue command to ring xmit queue */
		if (!(flag & SLI_IOCB_RET_IOCB)) {
			elx_sli_ringtx_put(phba, pring, piocb);
		}
		psli->slistat.iocbCmdDelay[ringno]++;
		ELX_SLI_UNLOCK(phba, iflag);
		/*
		 * Treat it as adapter hardware error.
		 */
		phba->hba_state = ELX_HBA_ERROR;
		phba->hba_flag |= FC_STOP_IO;
		(psli->sliinit.elx_sli_handle_eratt) (phba, HS_FFER3);
		return (IOCB_BUSY);
	}

	/* Bump driver iocb command index to next IOCB */
	if (++pring->cmdidx >= portCmdMax) {
		pring->cmdidx = 0;
	}
	lastiocb = 0;
	loopcnt = 0;

	/* Check to see if this is a high priority
	   command. If so bypass tx queue processing.
	 */

	if (flag & SLI_IOCB_HIGH_PRIORITY) {
		nextiocb = NULL;
		goto afterloop;
	}

	/* While IOCB entries are available */
	while (pring->cmdidx != portCmdGet) {
		/* If there is anything on the tx queue, process it before piocb */
		if (((nextiocb = elx_sli_ringtx_get(phba, pring)) == NULL)
		    && (piocb == NULL)) {
		      issueout:
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 ELX_SLIM2_PAGE_AREA,
					 ELX_DMA_SYNC_FORCPU);
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 ELX_SLIM2_PAGE_AREA,
					 ELX_DMA_SYNC_FORDEV);

			/* Make sure cmdidx is in sync with the HBA's current value. */
			if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
				status = hgp->cmdPutInx;
				pring->cmdidx = (uint8_t) PCIMEM_LONG(status);
			} else {
				(psli->sliinit.elx_sli_read_slim) ((void *)phba,
								   (void *)
								   &status,
								   (int)((SLIMOFF + (ringno * 2)) * 4), sizeof (uint32_t));
				pring->cmdidx = (uint8_t) status;
			}

			/* Interrupt the HBA to let it know there is work to do
			 * in ring ringno.
			 */
			status = ((CA_R0ATT) << (ringno * 4));
			(psli->sliinit.elx_sli_write_CA) (phba, status);

			/* If we are waiting for the IOCB to complete before returning */
			if ((flag & SLI_IOCB_POLL) && lastiocb) {
				uint32_t loopcnt, ha_copy;
				int retval;

				/* Wait 10240 loop iterations + 30 seconds before timing out the IOCB. */
				for (loopcnt = 0; loopcnt < (10240 + 30);
				     loopcnt++) {
					ha_copy =
					    (psli->sliinit.
					     elx_sli_intr_prep) ((void *)phba);
					ha_copy = (ha_copy >> (ringno * 4));
					if (ha_copy & HA_RXATT) {
						ELX_SLI_UNLOCK(phba, iflag);
						retval =
						    elx_sli_handle_ring_event
						    (phba, pring,
						     (ha_copy & HA_RXMASK));
						ELX_SLI_LOCK(phba, iflag);
						/*
						 * The IOCB requires to poll for completion.
						 * If retval is identically 0, the iocb has been handled.  
						 * Otherwise, wait and retry.
						 */
						if (retval == 0) {
							break;
						}
					}
					if (loopcnt > 10240) {
						elx_sleep_ms(phba, 1000);	/* 1 second delay */
					}
				}
				if (loopcnt >= (10240 + 30)) {
					/* Command timed out */
					/* Based on the iotag field, get the cmd IOCB from the txcmplq */
					if ((lastiocb =
					     elx_sli_ringtxcmpl_get(phba, pring,
								    lastiocb,
								    0))) {
						/* Call the specified completion routine */
						icmd = &lastiocb->iocb;
						icmd->ulpStatus =
						    IOSTAT_LOCAL_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SEQUENCE_TIMEOUT;
						if (lastiocb->iocb_cmpl) {
							ELX_SLI_UNLOCK(phba,
								       iflag);
							(lastiocb->
							 iocb_cmpl) ((void *)
								     phba,
								     lastiocb,
								     lastiocb);
							ELX_SLI_LOCK(phba,
								     iflag);
						} else {
							elx_mem_put(phba,
								    MEM_IOCB,
								    (uint8_t *)
								    lastiocb);
						}
					}
				}
			}
			ELX_SLI_UNLOCK(phba, iflag);
			return (IOCB_SUCCESS);
		}

	      afterloop:

		/* If there is nothing left in the tx queue, now we can send piocb */
		if (nextiocb == NULL) {
			nextiocb = piocb;
			piocb = NULL;
		}
		icmd = &nextiocb->iocb;

		/* issue iocb command to adapter */
		elx_sli_pcimem_bcopy((uint32_t *) icmd, (uint32_t *) iocb,
				     sizeof (IOCB_t));
		psli->slistat.iocbCmd[ringno]++;

		/* If there is no completion routine to call, we can release the IOCB
		 * buffer back right now. For IOCBs, like QUE_RING_BUF, that have no
		 * rsp ring completion, iocb_cmpl MUST be 0.
		 */
		if (nextiocb->iocb_cmpl) {
			elx_sli_ringtxcmpl_put(phba, pring, nextiocb);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) nextiocb);
		}

		/* Let the HBA know what IOCB slot will be the next one the driver
		 * will put a command into.
		 */
		if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
			status = (uint32_t) pring->cmdidx;
			hgp->cmdPutInx = PCIMEM_LONG(status);

			/* Since this may be expensive, sync it every 4 IOCBs */
			loopcnt++;
			if ((loopcnt & 0x3) == 0) {
				/* sync hgp->cmdPutInx in the MAILBOX_t */
				elx_pci_dma_sync((void *)phba,
						 (void *)&phba->slim2p,
						 sizeof (MAILBOX_t),
						 ELX_DMA_SYNC_FORDEV);
			}
		} else {
			status = (uint32_t) pring->cmdidx;
			(psli->sliinit.elx_sli_write_slim) ((void *)phba,
							    (void *)&status,
							    (int)((SLIMOFF +
								   (ringno *
								    2)) * 4),
							    sizeof (uint32_t));
		}

		/* Get the next available command iocb.
		 * cmdidx is the IOCB index of the next IOCB that the driver
		 * is going to issue a command with.
		 */
		iocb = (IOCB_t *) IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

		/* Bump driver iocb command index to next IOCB */
		if (++pring->cmdidx >= portCmdMax) {
			pring->cmdidx = 0;
		}

		lastiocb = nextiocb;

		/* Make sure the ring's command index has been updated.  If 
		 * not, sync the slim memory area and refetch the command index.
		 */
		if (pring->cmdidx == portCmdGet) {
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 sizeof (MAILBOX_t),
					 ELX_DMA_SYNC_FORCPU);
			status = pgp->cmdGetInx;
			portCmdGet = PCIMEM_LONG(status);
		}

	}			/* pring->cmdidx != portCmdGet */

	if (piocb == NULL && !(flag & SLI_IOCB_HIGH_PRIORITY)) {
		goto issueout;
	} else if (piocb == NULL) {
		elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
				 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORCPU);
		elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
				 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORDEV);

		/* Make sure cmdidx is in sync with the HBA's current value. */
		if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
			status = hgp->cmdPutInx;
			pring->cmdidx = (uint8_t) PCIMEM_LONG(status);
		} else {
			(psli->sliinit.elx_sli_read_slim) ((void *)phba,
							   (void *)&status,
							   (int)((SLIMOFF +
								  (ringno *
								   2)) * 4),
							   sizeof (uint32_t));
			pring->cmdidx = (uint8_t) status;
		}

		/* Interrupt the HBA to let it know there is work to do
		 * in ring ringno.
		 */
		status = ((CA_R0ATT) << (ringno * 4));
		(psli->sliinit.elx_sli_write_CA) (phba, status);

		ELX_SLI_UNLOCK(phba, iflag);
		return (IOCB_SUCCESS);
	}

	/* This code is executed only if the command ring is full.  Wait for the
	 * HBA to process some entries before handing it more work.
	 */

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORCPU);
	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORDEV);

	/* Make sure cmdidx is in sync with the HBA's current value. */
	if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
		status = hgp->cmdPutInx;
		pring->cmdidx = (uint8_t) PCIMEM_LONG(status);
	} else {
		(psli->sliinit.elx_sli_read_slim) ((void *)phba,
						   (void *)&status,
						   (int)((SLIMOFF +
							  (ringno * 2)) * 4),
						   sizeof (uint32_t));
		pring->cmdidx = (uint8_t) status;
	}

	pring->flag |= ELX_CALL_RING_AVAILABLE;	/* indicates cmd ring was full */
	/* 
	 * Set ring 'ringno' to SET R0CE_REQ in Chip Att register.
	 * The HBA will tell us when an IOCB entry is available.
	 */
	status = ((CA_R0ATT | CA_R0CE_REQ) << (ringno * 4));
	(psli->sliinit.elx_sli_write_CA) (phba, status);

	psli->slistat.iocbCmdFull[ringno]++;

	/* Queue command to ring xmit queue */
	if ((!(flag & SLI_IOCB_RET_IOCB)) && piocb) {
		elx_sli_ringtx_put(phba, pring, piocb);
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (IOCB_BUSY);
}

int
elx_sli_resume_iocb(elxHBA_t * phba, ELX_SLI_RING_t * pring)
{
	ELX_SLI_t *psli;
	IOCB_t *iocb;
	IOCB_t *icmd = NULL;
	HGP *hgp;
	PGP *pgp;
	MAILBOX_t *mbox;
	ELX_IOCBQ_t *nextiocb;
	uint32_t status;
	int ringno, loopcnt;
	uint32_t portCmdGet, portCmdMax;
	unsigned long iflag;

	psli = &phba->sli;
	ringno = pring->ringno;

	/* At this point we assume SLI-2 */
	mbox = (MAILBOX_t *) psli->MBhostaddr;
	pgp = (PGP *) & mbox->us.s2.port[ringno];
	hgp = (HGP *) & mbox->us.s2.host[ringno];

	ELX_SLI_LOCK(phba, iflag);

	/* portCmdMax is the number of cmd ring entries for this specific ring. */
	portCmdMax = psli->sliinit.ringinit[ringno].numCiocb;

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORCPU);

	/* portCmdGet is the IOCB index of the next IOCB that the HBA
	 * is going to process.
	 */
	status = pgp->cmdGetInx;
	portCmdGet = PCIMEM_LONG(status);

	/* First check to see if there is anything on the txq to send */
	if (pring->txq.q_cnt == 0) {
		ELX_SLI_UNLOCK(phba, iflag);
		return (portCmdGet);
	}

	if (phba->hba_state <= ELX_LINK_DOWN) {
		ELX_SLI_UNLOCK(phba, iflag);
		return (portCmdGet);
	}
	/* For FCP commands, we must be in a state where we can process
	 * link attention events.
	 */
	if (!(psli->sliinit.sli_flag & ELX_PROCESS_LA) &&
	    (pring->ringno == psli->fcp_ring)) {
		ELX_SLI_UNLOCK(phba, iflag);
		return (portCmdGet);
	}

	/* Check to see if we are blocking IOCB processing because of a
	 * outstanding mbox command.
	 */
	if (pring->flag & ELX_STOP_IOCB_MBX) {
		ELX_SLI_UNLOCK(phba, iflag);
		return (portCmdGet);
	}

	/* Get the next available command iocb.
	 * cmdidx is the IOCB index of the next IOCB that the driver
	 * is going to issue a command with.
	 */
	iocb = (IOCB_t *) IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

	if (portCmdGet >= portCmdMax) {

		/* Ring <ringno> issue: portCmdGet <portCmdGet> is bigger then cmd ring <portCmdMax> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0315,	/* ptr to msg structure */
			       elx_mes0315,	/* ptr to msg */
			       elx_msgBlk0315.msgPreambleStr,	/* begin varargs */
			       ringno, portCmdGet, portCmdMax);	/* end varargs */
		ELX_SLI_UNLOCK(phba, iflag);
		return (portCmdGet);
	}

	/* Bump driver iocb command index to next IOCB */
	if (++pring->cmdidx >= portCmdMax) {
		pring->cmdidx = 0;
	}
	loopcnt = 0;

	/* While IOCB entries are available */
	while (pring->cmdidx != portCmdGet) {
		/* If there is anything on the tx queue, process it */
		if ((nextiocb = elx_sli_ringtx_get(phba, pring)) == NULL) {

			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 ELX_SLIM2_PAGE_AREA,
					 ELX_DMA_SYNC_FORCPU);
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 ELX_SLIM2_PAGE_AREA,
					 ELX_DMA_SYNC_FORDEV);

			/* Make sure cmdidx is in sync with the HBA's current value. */
			if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
				status = hgp->cmdPutInx;
				pring->cmdidx = (uint8_t) PCIMEM_LONG(status);
			} else {
				(psli->sliinit.elx_sli_read_slim) ((void *)phba,
								   (void *)
								   &status,
								   (int)((SLIMOFF + (ringno * 2)) * 4), sizeof (uint32_t));
				pring->cmdidx = (uint8_t) status;
			}

			/* Interrupt the HBA to let it know there is work to do
			 * in ring ringno.
			 */
			status = ((CA_R0ATT) << (ringno * 4));
			(psli->sliinit.elx_sli_write_CA) (phba, status);

			ELX_SLI_UNLOCK(phba, iflag);
			return (portCmdGet);
		}
		icmd = &nextiocb->iocb;

		/* issue iocb command to adapter */
		elx_sli_pcimem_bcopy((uint32_t *) icmd, (uint32_t *) iocb,
				     sizeof (IOCB_t));
		psli->slistat.iocbCmd[ringno]++;

		/* If there is no completion routine to call, we can release the IOCB
		 * buffer back right now. For IOCBs, like QUE_RING_BUF, that have no
		 * rsp ring completion, iocb_cmpl MUST be 0.
		 */
		if (nextiocb->iocb_cmpl) {
			elx_sli_ringtxcmpl_put(phba, pring, nextiocb);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) nextiocb);
		}

		/* Let the HBA know what IOCB slot will be the next one the driver
		 * will put a command into.
		 */
		if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
			status = (uint32_t) pring->cmdidx;
			hgp->cmdPutInx = PCIMEM_LONG(status);

			/* Since this may be expensive, sync it every 4 IOCBs */
			loopcnt++;
			if ((loopcnt & 0x3) == 0) {
				/* sync hgp->cmdPutInx in the MAILBOX_t */
				elx_pci_dma_sync((void *)phba,
						 (void *)&phba->slim2p,
						 sizeof (MAILBOX_t),
						 ELX_DMA_SYNC_FORDEV);
			}
		} else {
			status = (uint32_t) pring->cmdidx;
			(psli->sliinit.elx_sli_write_slim) ((void *)phba,
							    (void *)&status,
							    (int)((SLIMOFF +
								   (ringno *
								    2)) * 4),
							    sizeof (uint32_t));
		}

		/* Get the next available command iocb.
		 * cmdidx is the IOCB index of the next IOCB that the driver
		 * is going to issue a command with.
		 */
		iocb = (IOCB_t *) IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

		/* Bump driver iocb command index to next IOCB */
		if (++pring->cmdidx >= portCmdMax) {
			pring->cmdidx = 0;
		}

		/* Make sure the ring's command index has been updated.  If 
		 * not, sync the slim memory area and refetch the command index.
		 */
		if (pring->cmdidx == portCmdGet) {
			/* sync pgp->cmdGetInx in the MAILBOX_t */
			elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
					 sizeof (MAILBOX_t),
					 ELX_DMA_SYNC_FORCPU);
			status = pgp->cmdGetInx;
			portCmdGet = PCIMEM_LONG(status);
		}
	}

	/* This code is executed only if the command ring is full.  Wait for the
	 * HBA to process some entries before handing it more work.
	 */

	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORCPU);
	elx_pci_dma_sync((void *)phba, (void *)&phba->slim2p,
			 ELX_SLIM2_PAGE_AREA, ELX_DMA_SYNC_FORDEV);

	/* Make sure cmdidx is in sync with the HBA's current value. */
	if (psli->sliinit.sli_flag & ELX_HGP_HOSTSLIM) {
		status = hgp->cmdPutInx;
		pring->cmdidx = (uint8_t) PCIMEM_LONG(status);
	} else {
		(psli->sliinit.elx_sli_read_slim) ((void *)phba,
						   (void *)&status,
						   (int)((SLIMOFF +
							  (ringno * 2)) * 4),
						   sizeof (uint32_t));
		pring->cmdidx = (uint8_t) status;
	}

	pring->flag |= ELX_CALL_RING_AVAILABLE;	/* indicates cmd ring was full */
	/* 
	 * Set ring 'ringno' to SET R0CE_REQ in Chip Att register.
	 * The HBA will tell us when an IOCB entry is available.
	 */
	status = ((CA_R0ATT | CA_R0CE_REQ) << (ringno * 4));
	(psli->sliinit.elx_sli_write_CA) (phba, status);

	psli->slistat.iocbCmdFull[ringno]++;

	ELX_SLI_UNLOCK(phba, iflag);
	return (portCmdGet);
}

int
elx_sli_brdreset(elxHBA_t * phba)
{
	MAILBOX_t *swpmb;
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	uint16_t cfg_value, skip_post;
	volatile uint32_t word0;
	unsigned long iflag;
	int i;

	psli = &phba->sli;

	ELX_SLI_LOCK(phba, iflag);
	/* A board reset must use REAL SLIM. */
	psli->sliinit.sli_flag &= ~ELX_SLI2_ACTIVE;

	word0 = 0;
	swpmb = (MAILBOX_t *) & word0;
	swpmb->mbxCommand = MBX_RESTART;
	swpmb->mbxHc = 1;

	(psli->sliinit.elx_sli_write_slim) ((void *)phba, (void *)swpmb,
					    0, sizeof (uint32_t));

	/* Only skip post after fc_ffinit is completed */
	if (phba->hba_state) {
		skip_post = 1;
		word0 = 1;	/* This is really setting up word1 */
		(psli->sliinit.elx_sli_write_slim) ((void *)phba, (void *)swpmb,
						    sizeof (uint32_t),
						    sizeof (uint32_t));
	} else {
		skip_post = 0;
	}

	/* Turn off SERR, PERR in PCI cmd register */
	phba->hba_state = ELX_INIT_START;

	ELX_SLI_UNLOCK(phba, iflag);
	/* Call the registered board reset routine */
	(psli->sliinit.elx_sli_brdreset) (phba);

	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		elx_sli_abort_iocb_ring(phba, pring, ELX_SLI_ABORT_IMED);
	}
	ELX_SLI_LOCK(phba, iflag);

	/* Turn off parity checking and serr during the physical reset */
	cfg_value = (psli->sliinit.elx_sli_read_pci_cmd) ((void *)phba);
	(psli->sliinit.elx_sli_write_pci_cmd) ((void *)phba,
					       (uint16_t) (cfg_value &
							   ~(CMD_PARITY_CHK |
							     CMD_SERR_ENBL)));

	/* Now toggle INITFF bit in the Host Control Register */
	(psli->sliinit.elx_sli_write_HC) (phba, HC_INITFF);
	mdelay(1);
	(psli->sliinit.elx_sli_write_HC) (phba, 0);

	/* Restore PCI cmd register */
	(psli->sliinit.elx_sli_write_pci_cmd) ((void *)phba, cfg_value);

	phba->hba_state = ELX_INIT_START;

	/* Initialize relevant SLI info */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		pring->flag = 0;
		pring->rspidx = 0;
		pring->cmdidx = 0;
		pring->missbufcnt = 0;
	}

	ELX_SLI_UNLOCK(phba, iflag);
	if (skip_post) {
		mdelay(100);
	} else {
		mdelay(2000);
	}
	return (0);
}

int
elx_sli_setup(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	int i, cnt;
	unsigned long iflag;

	psli = &phba->sli;
	buf_info = &bufinfo;

	ELX_SLI_LOCK(phba, iflag);
	/* Initialize list headers for txq and txcmplq as double linked lists */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		pring->ringno = i;
		pring->txq.q_f = &pring->txq;
		pring->txq.q_b = &pring->txq;
		pring->txcmplq.q_f = &pring->txcmplq;
		pring->txcmplq.q_b = &pring->txcmplq;
		pring->postbufq.q_first = 0;
		pring->postbufq.q_last = 0;
		cnt = psli->sliinit.ringinit[i].fast_iotag;
		if (cnt) {
			/* Allocate space needed for fast lookup */
			buf_info->size = (cnt * sizeof (ELX_IOCBQ_t *));
			buf_info->flags = 0;
			buf_info->align = sizeof (void *);
			buf_info->dma_handle = 0;

			/* Create a table to relate FCP iotags to fc_buf addresses */
			elx_malloc(phba, buf_info);
			if (buf_info->virt == 0) {
				ELX_SLI_UNLOCK(phba, iflag);
				return (0);
			}
			pring->fast_lookup = (void *)buf_info->virt;
			memset((char *)pring->fast_lookup, 0, cnt);
		}
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (1);
}

int
elx_sli_hba_down(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	ELX_SLI_RING_t *pring;
	ELX_MBOXQ_t *pmb;
	ELX_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd = NULL;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	int i, cnt;
	unsigned long iflag;

	psli = &phba->sli;
	buf_info = &bufinfo;

/*
   phba->hba_state = ELX_INIT_START;
*/

	iflag = phba->iflag;
	ELX_DRVR_UNLOCK(phba, iflag);

	(psli->sliinit.elx_sli_hba_down_prep) (phba);

	ELX_SLI_LOCK(phba, iflag);

	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		pring->flag |= ELX_DEFERRED_RING_EVENT;
		/* Error everything on txq and txcmplq */
		next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
		pring->txq.q_f = &pring->txq;
		pring->txq.q_b = &pring->txq;
		pring->txq.q_cnt = 0;
		while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
			iocb = next_iocb;
			next_iocb = next_iocb->q_f;
			iocb->q_f = 0;
			if (iocb->iocb_cmpl) {
				icmd = &iocb->iocb;
				icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_DOWN;
				ELX_SLI_UNLOCK(phba, iflag);
				(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
				ELX_SLI_LOCK(phba, iflag);
			} else {
				elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
			}
		}

		/* Free any memory allocated for fast lookup */
		cnt = psli->sliinit.ringinit[i].fast_iotag;
		if (pring->fast_lookup) {
			buf_info->size = (cnt * sizeof (ELX_IOCBQ_t *));
			buf_info->virt = (uint32_t *) pring->fast_lookup;
			buf_info->phys = 0;
			buf_info->flags = ELX_MBUF_VIRT;
			buf_info->dma_handle = 0;
			elx_free(phba, buf_info);
			pring->fast_lookup = 0;
		}

		while ((ELX_IOCBQ_t *) & pring->txcmplq !=
		       (ELX_IOCBQ_t *) pring->txcmplq.q_f) {

			next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
			pring->txcmplq.q_f = &pring->txcmplq;
			pring->txcmplq.q_b = &pring->txcmplq;
			pring->txcmplq.q_cnt = 0;
			while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
				iocb = next_iocb;
				next_iocb = next_iocb->q_f;
				iocb->q_f = 0;
				if (iocb->iocb_cmpl) {
					icmd = &iocb->iocb;
					icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
					icmd->un.ulpWord[4] = IOERR_SLI_DOWN;
					ELX_SLI_UNLOCK(phba, iflag);
					(iocb->iocb_cmpl) ((void *)phba, iocb,
							   iocb);
					ELX_SLI_LOCK(phba, iflag);
				} else {
					elx_mem_put(phba, MEM_IOCB,
						    (uint8_t *) iocb);
				}
			}
		}

	}
	/* Return any active mbox cmds */
	if (psli->mbox_tmo) {
		elx_clk_can(phba, psli->mbox_tmo);
		psli->mbox_tmo = 0;
	}
	if ((psli->mbox_active)) {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) psli->mbox_active);
	}
	psli->sliinit.sli_flag &= ~ELX_SLI_MBOX_ACTIVE;
	psli->mbox_active = 0;

	/* Return any pending mbox cmds */
	while ((pmb = elx_mbox_get(phba))) {
		elx_mem_put(phba, MEM_MBOX, (uint8_t *) pmb);
	}
	psli->mboxq.q_first = 0;
	psli->mboxq.q_last = 0;

	ELX_SLI_UNLOCK(phba, iflag);

	/*
	 * Adapter can not handle any mbox command.
	 * Skip borad reset.
	 */
	if (phba->hba_state != ELX_HBA_ERROR) {
		phba->hba_state = ELX_INIT_START;
		elx_sli_brdreset(phba);
	}

	ELX_DRVR_LOCK(phba, iflag);
	return (1);
}

void
elx_sli_pcimem_bcopy(uint32_t * src, uint32_t * dest, uint32_t cnt)
{
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = *src++;
		ldata = PCIMEM_LONG(ldata);
		*dest++ = ldata;
	}
}

int
elx_sli_ringpostbuf_put(elxHBA_t * phba, ELX_SLI_RING_t * pring, DMABUF_t * mp)
{
	ELX_SLINK_t *slp;
	unsigned long iflag;

	ELX_SLI_LOCK(phba, iflag);
	/* Stick DMABUF_t at end of postbufq so driver can look it up later */
	slp = &pring->postbufq;
	if (slp->q_first) {
		((DMABUF_t *) slp->q_last)->next = mp;
	} else {
		slp->q_first = (ELX_SLINK_t *) mp;
	}
	slp->q_last = (ELX_SLINK_t *) mp;
	mp->next = 0;
	slp->q_cnt++;
	ELX_SLI_UNLOCK(phba, iflag);
	return (0);
}

DMABUF_t *
elx_sli_ringpostbuf_get(elxHBA_t * phba,
			ELX_SLI_RING_t * pring, elx_dma_addr_t phys)
{
	return (elx_sli_ringpostbuf_search(phba, pring, phys, 1));
}

DMABUF_t *
elx_sli_ringpostbuf_search(elxHBA_t * phba,
			   ELX_SLI_RING_t * pring,
			   elx_dma_addr_t phys, int unlink)
{
	DMABUF_t *mp;
	DMABUF_t *mpprev;
	ELX_SLINK_t *slp;
	unsigned long iflag;
	int count;

	ELX_SLI_LOCK(phba, iflag);
	slp = &pring->postbufq;

	/* Search postbufq, from the begining, looking for a match on phys */
	mpprev = 0;
	count = 0;
	mp = (DMABUF_t *) slp->q_first;
	while (mp) {
		count++;
		if (mp->phys == phys) {
			/* If we find a match, deque it and return it to the caller */
			if (unlink) {
				if (mpprev) {
					mpprev->next = mp->next;
				} else {
					slp->q_first = (ELX_SLINK_t *) mp->next;
				}
				if (slp->q_last == (ELX_SLINK_t *) mp)
					slp->q_last = (ELX_SLINK_t *) mpprev;

				slp->q_cnt--;

				elx_pci_dma_sync((void *)phba, (void *)mp, 0,
						 ELX_DMA_SYNC_FORCPU);
			}
			ELX_SLI_UNLOCK(phba, iflag);
			return (mp);
		}
		mpprev = mp;
		mp = mp->next;
	}
	ELX_SLI_UNLOCK(phba, iflag);
	/* Cannot find virtual addr for mapped buf on ring <num> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0410,	/* ptr to msg structure */
		       elx_mes0410,	/* ptr to msg */
		       elx_msgBlk0410.msgPreambleStr,	/* begin varargs */
		       pring->ringno, phys, slp->q_first, slp->q_last, slp->q_cnt);	/* end varargs */
	return (0);
}

int
elx_sli_ringtx_put(elxHBA_t * phba, ELX_SLI_RING_t * pring, ELX_IOCBQ_t * piocb)
{
	ELX_DLINK_t *dlp;
	ELX_DLINK_t *dlp_end;

	/* Stick IOCBQ_t at end of txq so driver can issue it later */
	dlp = &pring->txq;
	dlp_end = (ELX_DLINK_t *) dlp->q_b;
	elx_enque(piocb, dlp_end);
	dlp->q_cnt++;
	return (0);
}

int
elx_sli_ringtxcmpl_put(elxHBA_t * phba,
		       ELX_SLI_RING_t * pring, ELX_IOCBQ_t * piocb)
{
	ELX_DLINK_t *dlp;
	ELX_DLINK_t *dlp_end;
	IOCB_t *icmd = NULL;
	ELX_SLI_t *psli;
	uint8_t *ptr;
	uint16_t iotag;

	dlp = &pring->txcmplq;
	dlp_end = (ELX_DLINK_t *) dlp->q_b;

	elx_enque(((ELX_DLINK_t *) piocb), dlp_end);
	dlp->q_cnt++;
	ptr = (uint8_t *) (pring->fast_lookup);

	if (ptr) {
		/* Setup fast lookup based on iotag for completion */
		psli = &phba->sli;
		icmd = &piocb->iocb;
		iotag = icmd->ulpIoTag;
		if (iotag < psli->sliinit.ringinit[pring->ringno].fast_iotag) {
			ptr += (iotag * sizeof (ELX_IOCBQ_t *));
			*((ELX_IOCBQ_t **) ptr) = piocb;
		} else {

			/* Cmd ring <ringno> put: iotag <iotag> greater then configured max <fast_iotag> wd0 <icmd> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0316,	/* ptr to msg structure */
				       elx_mes0316,	/* ptr to msg */
				       elx_msgBlk0316.msgPreambleStr,	/* begin varargs */
				       pring->ringno, iotag, psli->sliinit.ringinit[pring->ringno].fast_iotag, *(((uint32_t *) icmd) + 7));	/* end varargs */
		}
	}
	return (0);
}

ELX_IOCBQ_t *
elx_sli_ringtx_get(elxHBA_t * phba, ELX_SLI_RING_t * pring)
{
	ELX_DLINK_t *dlp;
	ELX_IOCBQ_t *cmd_iocb;
	ELX_IOCBQ_t *next_iocb;

	dlp = &pring->txq;
	cmd_iocb = 0;
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	if (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		/* If the first ptr is not equal to the list header, 
		 * deque the IOCBQ_t and return it.
		 */
		cmd_iocb = next_iocb;
		elx_deque(cmd_iocb);
		dlp->q_cnt--;
	}
	return (cmd_iocb);
}

ELX_IOCBQ_t *
elx_sli_ringtxcmpl_get(elxHBA_t * phba,
		       ELX_SLI_RING_t * pring,
		       ELX_IOCBQ_t * prspiocb, uint32_t srch)
{
	ELX_DLINK_t *dlp;
	IOCB_t *icmd = NULL;
	IOCB_t *irsp = NULL;
	ELX_IOCBQ_t *cmd_iocb;
	ELX_IOCBQ_t *next_iocb;
	ELX_SLI_t *psli;
	uint8_t *ptr;
	uint16_t iotag;

	dlp = &pring->txcmplq;
	ptr = (uint8_t *) (pring->fast_lookup);

	if (ptr && (srch == 0)) {
		/* Use fast lookup based on iotag for completion */
		psli = &phba->sli;
		irsp = &prspiocb->iocb;
		iotag = irsp->ulpIoTag;
		if (iotag < psli->sliinit.ringinit[pring->ringno].fast_iotag) {
			ptr += (iotag * sizeof (ELX_IOCBQ_t *));
			cmd_iocb = *((ELX_IOCBQ_t **) ptr);
			*((ELX_IOCBQ_t **) ptr) = 0;
			if (cmd_iocb == NULL) {

				goto search;
			}
			elx_deque(cmd_iocb);
			dlp->q_cnt--;
		} else {

			/* Rsp ring <ringno> get: iotag <iotag> greater then configured max <fast_iotag> wd0 <irsp> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0317,	/* ptr to msg structure */
				       elx_mes0317,	/* ptr to msg */
				       elx_msgBlk0317.msgPreambleStr,	/* begin varargs */
				       pring->ringno, iotag, psli->sliinit.ringinit[pring->ringno].fast_iotag, *(((uint32_t *) irsp) + 7));	/* end varargs */
			cmd_iocb = 0;
			goto search;
		}
	} else {
		cmd_iocb = 0;
	      search:
		irsp = &prspiocb->iocb;
		iotag = irsp->ulpIoTag;

		/* Search through txcmpl from the begining */
		next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
		while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
			icmd = &next_iocb->iocb;
			if (iotag == icmd->ulpIoTag) {
				/* found a match! */
				cmd_iocb = next_iocb;
				elx_deque(cmd_iocb);
				dlp->q_cnt--;
				break;
			}
			next_iocb = next_iocb->q_f;
		}
	}

	return (cmd_iocb);
}

uint32_t
elx_sli_next_iotag(elxHBA_t * phba, ELX_SLI_RING_t * pring)
{
	ELX_RING_INIT_t *pringinit;
	ELX_SLI_t *psli;
	uint8_t *ptr;
	int i;
	unsigned long iflag;

	psli = &phba->sli;
	ELX_SLI_LOCK(phba, iflag);
	pringinit = &psli->sliinit.ringinit[pring->ringno];
	for (i = 0; i < pringinit->iotag_max; i++) {
		/* Never give an iotag of 0 back */
		pringinit->iotag_ctr++;
		if (pringinit->iotag_ctr == pringinit->iotag_max) {
			pringinit->iotag_ctr = 1;	/* Never use 0 as an iotag */
		}
		/* If fast_iotaging is used, we can ensure that the iotag 
		 * we give back is not already in use.
		 */
		if (pring->fast_lookup) {
			ptr = (uint8_t *) (pring->fast_lookup);
			ptr += (pringinit->iotag_ctr * sizeof (ELX_IOCBQ_t *));
			if (*((ELX_IOCBQ_t **) ptr) != 0)
				continue;
		}
		ELX_SLI_UNLOCK(phba, iflag);
		return (pringinit->iotag_ctr);
	}

	/* Outstanding I/O count for ring <ringno> is at max <fast_iotag> */
	elx_printf_log(phba->brd_no, &elx_msgBlk0318,	/* ptr to msg structure */
		       elx_mes0318,	/* ptr to msg */
		       elx_msgBlk0318.msgPreambleStr,	/* begin varargs */
		       pring->ringno, psli->sliinit.ringinit[pring->ringno].fast_iotag);	/* end varargs */
	ELX_SLI_UNLOCK(phba, iflag);
	return (0);
}

void
elx_sli_abort_cmpl(elxHBA_t * phba,
		   ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	elx_mem_put(phba, MEM_IOCB, (uint8_t *) cmdiocb);
	return;
}

void
elx_sli_abort_elsreq_cmpl(elxHBA_t * phba,
			  ELX_IOCBQ_t * cmdiocb, ELX_IOCBQ_t * rspiocb)
{
	/* Free the resources associated with the ELS_REQUEST64 IOCB the driver
	 * just aborted.
	 * In this case, context2  = cmd,  context2->next = rsp, context3 = bpl 
	 */
	if (cmdiocb->context2) {
		/* Free the response IOCB before completing the abort command.  */
		if (((DMABUF_t *) (cmdiocb->context2))->next) {
			elx_mem_put(phba, MEM_BUF,
				    (uint8_t
				     *) (((DMABUF_t *) (cmdiocb->context2))->
					 next));
		}
		elx_mem_put(phba, MEM_BUF, (uint8_t *) (cmdiocb->context2));
	}

	if (cmdiocb->context3) {
		elx_mem_put(phba, MEM_BPL, (uint8_t *) (cmdiocb->context3));
	}
	elx_mem_put(phba, MEM_IOCB, (uint8_t *) cmdiocb);
	return;
}

int
elx_sli_abort_iocb(elxHBA_t * phba, ELX_SLI_RING_t * pring, ELX_IOCBQ_t * piocb)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *abtsiocbp;
	uint8_t *ptr;
	IOCB_t *abort_cmd = NULL, *cmd = NULL;
	unsigned long iflag;
	uint16_t iotag;

	psli = &phba->sli;
	ELX_SLI_LOCK(phba, iflag);

	cmd = &piocb->iocb;

	if (piocb->abort_count == 2) {
		/* Clear fast_lookup entry, if any */
		iotag = cmd->ulpIoTag;
		ptr = (uint8_t *) (pring->fast_lookup);
		if (ptr
		    && (iotag <
			psli->sliinit.ringinit[pring->ringno].fast_iotag)) {
			ELX_IOCBQ_t *cmd_iocb;
			ptr += (iotag * sizeof (ELX_IOCBQ_t *));
			cmd_iocb = *((ELX_IOCBQ_t **) ptr);
			*((ELX_IOCBQ_t **) ptr) = 0;
		}

		/* Dequeue and complete with error */
		elx_deque(piocb);
		pring->txcmplq.q_cnt--;

		if (piocb->iocb_cmpl) {
			cmd->ulpStatus = IOSTAT_DRIVER_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			ELX_SLI_UNLOCK(phba, iflag);
			(piocb->iocb_cmpl) ((void *)phba, piocb, piocb);
			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) piocb);
		}
		ELX_SLI_UNLOCK(phba, iflag);
		return (1);
	}

	/* issue ABTS for this IOCB based on iotag */

	if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba, MEM_IOCB | MEM_PRI))
	    == NULL) {
		ELX_SLI_UNLOCK(phba, iflag);
		return (0);
	}

	memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
	abort_cmd = &abtsiocbp->iocb;

	abort_cmd->un.acxri.abortType = ABORT_TYPE_ABTS;
	abort_cmd->un.acxri.abortContextTag = cmd->ulpContext;
	abort_cmd->un.acxri.abortIoTag = cmd->ulpIoTag;

	abort_cmd->ulpLe = 1;
	abort_cmd->ulpClass = cmd->ulpClass;
	if (phba->hba_state >= ELX_LINK_UP) {
		abort_cmd->ulpCommand = CMD_ABORT_XRI_CN;
	} else {
		abort_cmd->ulpCommand = CMD_CLOSE_XRI_CN;
	}
	abort_cmd->ulpOwner = OWN_CHIP;

	ELX_SLI_UNLOCK(phba, iflag);
	/* set up an iotag  */
	abort_cmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

	if (elx_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
	    == IOCB_ERROR) {
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
		return (0);
	}

	piocb->abort_count++;
	return (1);
}

int
elx_sli_abort_iocb_ring(elxHBA_t * phba, ELX_SLI_RING_t * pring, uint32_t flag)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *iocb, *next_iocb;
	ELX_IOCBQ_t *abtsiocbp;
	uint8_t *ptr;
	IOCB_t *icmd = NULL, *cmd = NULL;
	unsigned long iflag;
	int errcnt;
	uint16_t iotag;

	psli = &phba->sli;
	errcnt = 0;
	ELX_SLI_LOCK(phba, iflag);

	/* Error everything on txq and txcmplq 
	 * First do the txq.
	 */
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	pring->txq.q_f = &pring->txq;
	pring->txq.q_b = &pring->txq;
	pring->txq.q_cnt = 0;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		iocb->q_f = 0;
		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			ELX_SLI_UNLOCK(phba, iflag);
			(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
		}
	}

	/* Next issue ABTS for everything on the txcmplq */
	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	if (flag == ELX_SLI_ABORT_IMED) {
		pring->txcmplq.q_f = &pring->txcmplq;
		pring->txcmplq.q_b = &pring->txcmplq;
		pring->txcmplq.q_cnt = 0;
	}
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;

		if (flag == ELX_SLI_ABORT_IMED) {
			/* Imediate abort of IOCB, deque and call compl */
			iocb->q_f = 0;
		}

		/* issue ABTS for this IOCB based on iotag */

		if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba,
							     MEM_IOCB |
							     MEM_PRI)) ==
		    NULL) {
			errcnt++;
			continue;
		}
		memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= ELX_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}
		icmd->ulpOwner = OWN_CHIP;

		if (flag == ELX_SLI_ABORT_IMED) {
			/* Clear fast_lookup entry, if any */
			iotag = cmd->ulpIoTag;
			ptr = (uint8_t *) (pring->fast_lookup);
			if (ptr
			    && (iotag <
				psli->sliinit.ringinit[pring->ringno].
				fast_iotag)) {
				ptr += (iotag * sizeof (ELX_IOCBQ_t *));
				*((ELX_IOCBQ_t **) ptr) = 0;
			}
			/* Imediate abort of IOCB, deque and call compl */
			if (iocb->iocb_cmpl) {
				cmd->ulpStatus = IOSTAT_DRIVER_REJECT;
				cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
				ELX_SLI_UNLOCK(phba, iflag);
				(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
				ELX_SLI_LOCK(phba, iflag);
			} else {
				elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
			}
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
		} else {
			ELX_SLI_UNLOCK(phba, iflag);
			/* set up an iotag  */
			icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

			if (elx_sli_issue_iocb
			    (phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
			    == IOCB_ERROR) {
				ELX_SLI_LOCK(phba, iflag);
				elx_mem_put(phba, MEM_IOCB,
					    (uint8_t *) abtsiocbp);
				errcnt++;
				continue;
			}
			ELX_SLI_LOCK(phba, iflag);
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (errcnt);
}

int
elx_sli_issue_abort_iotag32(elxHBA_t * phba,
			    ELX_SLI_RING_t * pring, ELX_IOCBQ_t * cmdiocb)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = NULL;
	IOCB_t *iabt = NULL;
	uint32_t iotag32;
	unsigned long iflag;

	psli = &phba->sli;
	ELX_SLI_LOCK(phba, iflag);

	/* issue ABTS for this IOCB based on iotag */

	if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba,
						     MEM_IOCB | MEM_PRI)) ==
	    NULL) {
		return (0);
	}
	memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
	iabt = &abtsiocbp->iocb;

	icmd = &cmdiocb->iocb;
	switch (icmd->ulpCommand) {
	case CMD_ELS_REQUEST64_CR:
		iotag32 = icmd->un.elsreq64.bdl.ulpIoTag32;
		/* Even though we abort the ELS command, the firmware may access the BPL or other resources
		 * before it processes our ABORT_MXRI64. Thus we must delay reusing the cmdiocb resources till
		 * the actual abort request completes.
		 */
		abtsiocbp->context1 = (void *)((unsigned long)icmd->ulpCommand);
		abtsiocbp->context2 = cmdiocb->context2;
		abtsiocbp->context3 = cmdiocb->context3;
		cmdiocb->context2 = 0;
		cmdiocb->context3 = 0;
		abtsiocbp->iocb_cmpl = elx_sli_abort_elsreq_cmpl;
		break;
	default:
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
		return (0);
	}

	iabt->un.amxri.abortType = ABORT_TYPE_ABTS;
	iabt->un.amxri.iotag32 = iotag32;

	iabt->ulpLe = 1;
	iabt->ulpClass = CLASS3;
	iabt->ulpCommand = CMD_ABORT_MXRI64_CN;
	iabt->ulpOwner = OWN_CHIP;

	ELX_SLI_UNLOCK(phba, iflag);
	/* set up an iotag  */
	iabt->ulpIoTag = elx_sli_next_iotag(phba, pring);

	if (elx_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
	    == IOCB_ERROR) {
		elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
		return (0);
	}

	return (1);
}

int
elx_sli_abort_iocb_ctx(elxHBA_t * phba, ELX_SLI_RING_t * pring, uint32_t ctx)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *iocb, *next_iocb;
	ELX_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = NULL, *cmd = NULL;
	unsigned long iflag;
	int errcnt;

	psli = &phba->sli;
	errcnt = 0;
	ELX_SLI_LOCK(phba, iflag);

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;
		if (cmd->ulpContext != ctx) {
			continue;
		}

		elx_deque(iocb);
		pring->txq.q_cnt--;
		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			ELX_SLI_UNLOCK(phba, iflag);
			(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
		}
	}

	/* Next check the txcmplq */
	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;
		if (cmd->ulpContext != ctx) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */

		if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba,
							     MEM_IOCB |
							     MEM_PRI)) ==
		    NULL) {
			errcnt++;
			continue;
		}
		memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= ELX_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}
		icmd->ulpOwner = OWN_CHIP;

		ELX_SLI_UNLOCK(phba, iflag);
		/* set up an iotag  */
		icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

		if (elx_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
			errcnt++;
			ELX_SLI_LOCK(phba, iflag);
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
		ELX_SLI_LOCK(phba, iflag);
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (errcnt);
}

int
elx_sli_abort_iocb_context1(elxHBA_t * phba, ELX_SLI_RING_t * pring, void *ctx)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *iocb, *next_iocb;
	ELX_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = NULL, *cmd = NULL;
	unsigned long iflag;
	int errcnt;

	psli = &phba->sli;
	errcnt = 0;
	ELX_SLI_LOCK(phba, iflag);

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;
		if (iocb->context1 != ctx) {
			continue;
		}

		elx_deque(iocb);
		pring->txq.q_cnt--;
		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			ELX_SLI_UNLOCK(phba, iflag);
			(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
		}
	}

	/* Next check the txcmplq */
	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;
		if (iocb->context1 != ctx) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */

		if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba,
							     MEM_IOCB |
							     MEM_PRI)) ==
		    NULL) {
			errcnt++;
			continue;
		}
		memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= ELX_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}
		icmd->ulpOwner = OWN_CHIP;

		ELX_SLI_UNLOCK(phba, iflag);
		/* set up an iotag  */
		icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

		if (elx_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
			errcnt++;
			ELX_SLI_LOCK(phba, iflag);
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
		ELX_SLI_LOCK(phba, iflag);
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (errcnt);
}

int
elx_sli_abort_iocb_lun(elxHBA_t * phba,
		       ELX_SLI_RING_t * pring,
		       uint16_t scsi_target, uint64_t scsi_lun)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *iocb, *next_iocb;
	ELX_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = NULL, *cmd = NULL;
	ELX_SCSI_BUF_t *elx_cmd;
	unsigned long iflag;
	int errcnt;

	psli = &phba->sli;
	errcnt = 0;
	ELX_SLI_LOCK(phba, iflag);

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a ELX_SCSI_BUF_t */
		elx_cmd = (ELX_SCSI_BUF_t *) (iocb->context1);
		if ((elx_cmd == 0) ||
		    (elx_cmd->scsi_target != scsi_target) ||
		    (elx_cmd->scsi_lun != scsi_lun)) {
			continue;
		}

		elx_deque(iocb);
		pring->txq.q_cnt--;
		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			ELX_SLI_UNLOCK(phba, iflag);
			(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
		}
	}

	/* Next check the txcmplq */
	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a ELX_SCSI_BUF_t */
		elx_cmd = (ELX_SCSI_BUF_t *) (iocb->context1);
		if ((elx_cmd == 0) ||
		    (elx_cmd->scsi_target != scsi_target) ||
		    (elx_cmd->scsi_lun != scsi_lun)) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */

		if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba,
							     MEM_IOCB |
							     MEM_PRI)) ==
		    NULL) {
			errcnt++;
			continue;
		}
		memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= ELX_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}
		icmd->ulpOwner = OWN_CHIP;

		ELX_SLI_UNLOCK(phba, iflag);
		/* set up an iotag  */
		icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

		if (elx_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
			errcnt++;
			ELX_SLI_LOCK(phba, iflag);
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
		ELX_SLI_LOCK(phba, iflag);
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (errcnt);
}

int
elx_sli_abort_iocb_tgt(elxHBA_t * phba,
		       ELX_SLI_RING_t * pring, uint16_t scsi_target)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *iocb, *next_iocb;
	ELX_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = NULL, *cmd = NULL;
	ELX_SCSI_BUF_t *elx_cmd;
	unsigned long iflag;
	int errcnt;

	psli = &phba->sli;
	errcnt = 0;
	ELX_SLI_LOCK(phba, iflag);

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a ELX_SCSI_BUF_t */
		elx_cmd = (ELX_SCSI_BUF_t *) (iocb->context1);
		if ((elx_cmd == 0) || (elx_cmd->scsi_target != scsi_target)) {
			continue;
		}

		elx_deque(iocb);
		pring->txq.q_cnt--;
		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			ELX_SLI_UNLOCK(phba, iflag);
			(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
		}
	}

	/* Next check the txcmplq */
	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a ELX_SCSI_BUF_t */
		elx_cmd = (ELX_SCSI_BUF_t *) (iocb->context1);
		if ((elx_cmd == 0) || (elx_cmd->scsi_target != scsi_target)) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */

		if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba,
							     MEM_IOCB |
							     MEM_PRI)) ==
		    NULL) {
			errcnt++;
			continue;
		}
		memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= ELX_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}
		icmd->ulpOwner = OWN_CHIP;

		ELX_SLI_UNLOCK(phba, iflag);
		/* set up an iotag  */
		icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

		if (elx_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
			errcnt++;
			ELX_SLI_LOCK(phba, iflag);
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
		ELX_SLI_LOCK(phba, iflag);
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (errcnt);
}

int
elx_sli_abort_iocb_hba(elxHBA_t * phba, ELX_SLI_RING_t * pring)
{
	ELX_SLI_t *psli;
	ELX_IOCBQ_t *iocb, *next_iocb;
	ELX_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = NULL, *cmd = NULL;
	ELX_SCSI_BUF_t *elx_cmd;
	unsigned long iflag;
	int errcnt;

	psli = &phba->sli;
	errcnt = 0;
	ELX_SLI_LOCK(phba, iflag);

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	next_iocb = (ELX_IOCBQ_t *) pring->txq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a ELX_SCSI_BUF_t */
		elx_cmd = (ELX_SCSI_BUF_t *) (iocb->context1);
		if (elx_cmd == 0) {
			continue;
		}

		elx_deque(iocb);
		pring->txq.q_cnt--;
		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_DRIVER_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			ELX_SLI_UNLOCK(phba, iflag);
			(iocb->iocb_cmpl) ((void *)phba, iocb, iocb);
			ELX_SLI_LOCK(phba, iflag);
		} else {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) iocb);
		}
	}

	/* Next check the txcmplq */
	next_iocb = (ELX_IOCBQ_t *) pring->txcmplq.q_f;
	while (next_iocb != (ELX_IOCBQ_t *) & pring->txcmplq) {
		iocb = next_iocb;
		next_iocb = next_iocb->q_f;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a ELX_SCSI_BUF_t */
		elx_cmd = (ELX_SCSI_BUF_t *) (iocb->context1);
		if (elx_cmd == 0) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */

		if ((abtsiocbp = (ELX_IOCBQ_t *) elx_mem_get(phba,
							     MEM_IOCB |
							     MEM_PRI)) ==
		    NULL) {
			errcnt++;
			continue;
		}
		memset((void *)abtsiocbp, 0, sizeof (ELX_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= ELX_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}
		icmd->ulpOwner = OWN_CHIP;

		ELX_SLI_UNLOCK(phba, iflag);
		/* set up an iotag  */
		icmd->ulpIoTag = elx_sli_next_iotag(phba, pring);

		if (elx_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			elx_mem_put(phba, MEM_IOCB, (uint8_t *) abtsiocbp);
			errcnt++;
			ELX_SLI_LOCK(phba, iflag);
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
		ELX_SLI_LOCK(phba, iflag);
	}
	ELX_SLI_UNLOCK(phba, iflag);
	return (errcnt);
}

/****************************************/
/* Print Format Declarations Start Here */
/****************************************/
#define  LENGTH_LINE 71
#define  MAX_IO_SIZE 32 * 2	/* iobuf cache size */
#define  MAX_TBUFF   18 * 2	/* temp buffer size */

typedef union {			/* Pointer to table of arguments. */
	ulong *ip;
	ulong *lip;
	ulong *uip;
	ulong *luip;
	ulong **luipp;
	uint8_t *cp;
	uint8_t **csp;
} ARGLIST;

typedef struct {
	uint8_t *string;
	int index;
	int count;
	uint8_t buf[MAX_IO_SIZE + MAX_TBUFF];	/* extra room to convert numbers */
} PRINTBLK;

/*
 * ASCII string declarations
 */
static uint8_t dig[] = { "0123456789ABCDEF" };
static uint8_t ds_disabled[] = "disabled";
static uint8_t ds_enabled[] = "enabled";
static uint8_t ds_none[] = "none";
static uint8_t ds_null_string[] = "";
static uint8_t ds_unknown[] = "unknown";

extern elxDRVR_t elxDRVR;

uint32_t elx_dbg_flag = 0;

/*
 * Function Declarations: Local
 */
int elx_add_char(PRINTBLK * io, uint8_t ch);
int elx_add_string(PRINTBLK * io, uint8_t * string);
int elx_sprintf_fargs(uint8_t * string, void *control, va_list args);
int elx_expanded_len(uint8_t * sp);
void elx_print_string(PRINTBLK * io);

int
elx_expanded_len(uint8_t * sp)
{
	register int i;
	uint8_t c;

	i = 0;
	while ((c = *sp++) != 0) {
		if (c < 0x1b) {
			if ((c == '\r') || (c == '\n'))
				/* stop at cr or lf */
				break;

			/* double it */
			i++;
		}
		i++;
	}
	return (i);
}

int
elx_str_itos(int val, uint8_t * cp, int base)
{
	uint8_t tempc[16];
	uint8_t *tcp;
	int n = 0;		/* number of characters in result */
	ulong uval;		/* unsigned value */

	*(tcp = (tempc + 15)) = 0;
	if (base < 0) {
		/* needs signed conversion */
		base = -base;
		if (val < 0) {
			n = 1;
			val = -val;
		}
		do {
			*(--tcp) = dig[(int)(val % base)];
			val /= base;
		} while (val);
	} else {
		uval = val;
		do {
			*(--tcp) = dig[(int)(uval % base)];
			uval /= base;
		} while (uval);
	}
	if (n)
		*(--tcp) = '-';
	n = (int)((long)&tempc[15] - (long)tcp);
	memcpy(cp, tcp, n + 1);	/* from, to, cnt */
	return (n);
}

int
elx_add_char(PRINTBLK * io, uint8_t ch)
{
/* 
 * Define the following definitions per Operating System type.
 */
#define OS_TYPE_UNIX

	int index;

	if (ch < 0x1b) {
		switch (ch) {
		case 0xd:	/* carriage return */
			io->count = -1;	/* will be incremented to 0, below */
			break;
		case 0x8:	/* back space */
			io->count -= 2;	/* will be incremented to 1 less, below */
			break;
		case 0xa:	/* line feed */
#ifndef OS_TYPE_UNIX
			elx_add_char(io, '\r');
#endif
		case 0x7:	/* bell */
		case 0x9:	/* hortizontal tab */
		case 0xe:	/* shift out */
		case 0xf:	/* shift in */
			io->count--;	/* will be incremented to same, below */
			break;
		default:
			elx_add_char(io, '^');
			ch |= 0x40;
			break;
		}
	}
	io->count++;
	if (io->string != NULL) {
		*io->string = ch;
		*++io->string = '\0';
		return (0);
	}

	index = io->index;
	if (index < (MAX_IO_SIZE + MAX_TBUFF - 2)) {
		io->buf[index] = ch;
		io->buf[++index] = '\0';
	}
	return (++io->index);
}

int
elx_add_string(PRINTBLK * io, uint8_t * string)
{
	union {
		uint8_t *cp;
		uint8_t byt[8];
	} utmp;

	if (io->string != NULL) {
		io->string = (uint8_t *) elx_str_cpy((char *)io->string, (char *)string);	/*dst,src */
		return (0);
	}
	utmp.cp = (uint8_t *) elx_str_cpy((char *)&io->buf[io->index], (char *)string);	/* dst, src */
	return (io->index = utmp.cp - io->buf);	/* Calulate and return str index */
}

void
elx_print_string(PRINTBLK * io)
{
	io->index = 0;
	elx_print((char *)&io->buf[0], 0, 0);
}

int
elx_fmtout(uint8_t * ostr,	/* Output buffer, or NULL if temp */
	   uint8_t * control,	/* Control string */
	   va_list inarg)
{				/* Argument list */
	short temp;		/* Output channel number if string NULL. */
	int leftadj;		/* Negative pararameter width specified. */
	int longflag;		/* Integer is long. */
	int box;		/* not from body */
	int chr;		/* control string character */
	uint8_t padchar;	/* Pad character, typically space. */
	int width;		/* Width of subfield. */
	int length;		/* Length of subfield. */
	ARGLIST arg;
	PRINTBLK *io;

	union {			/* Accumulate parameter value here. */
		uint16_t tlong;
		uint16_t tulong;
		long ltlong;
		ulong ltulong;
		uint8_t str[4];
		uint16_t twds[2];
	} lw;

	union {			/* Used by case %c */
		int intchar;
		uint8_t chr[4];
	} ichar;

	io = elx_kmem_alloc(sizeof (PRINTBLK), ELX_MEM_NDELAY);
	if (io == NULL) {

		elx_print
		    ("lpfc: Not enough mem available for logging messages.\n",
		     0, 0);
		return -1;
	}

	arg.uip = (ulong *) inarg;
	io->index = 0;
	io->count = 0;
	box = 0;

	if ((io->string = ostr) != (uint8_t *) NULL)
		*ostr = 0;	/* initialize output string to null */
	control--;

	while ((length = *++control) != 0) {	/* while more in control string */
		if (length != '%') {	/* format control */
			/* no control string, copy to output */
			if ((length == '\n') && box) {
				elx_print((char *)&io->buf[0], 0, 0);
				continue;
			}
			if (elx_add_char(io, (uint8_t) length) >= MAX_IO_SIZE)
				elx_print_string(io);	/* write it */
			continue;
		}
		leftadj = (*++control == '-');
		if (leftadj)
			++control;
		padchar = ' ';
		width = 0;
		if ((uint16_t) (length = (*control - '0')) <= 9) {
			if (length == 0)
				padchar = '0';
			width = length;
			while ((uint16_t) (length = (*++control - '0')) <= 9)
				width = width * 10 + length;
		}
		longflag = (*control == 'l');
		if (longflag)
			++control;

		chr = (int)(*control);
		if (chr != 'E') {
			chr |= 0x20;
		}

		switch (chr) {	/* switch on control (case insensitive except for 'E') */
		case 'a':
			longflag = 1;
			temp = 16;
			padchar = '0';
			length = width = 8;
			goto nosign;
		case 'b':
			temp = 2;
			goto nosign;
		case 'o':
			temp = 8;
			goto nosign;
		case 'u':
			temp = 10;
			goto nosign;
		case 'x':
			temp = 16;
			goto nosign;

			/* Ethernet address on recursive call */

		case 'e':
			ostr = (uint8_t *) va_arg(inarg, caddr_t);
			if ((chr == 'e') &&
			    ((*(long *)ostr) == (long)NULL) &&
			    ((*(uint16_t *) & ostr[4]) == (uint16_t) 0)) {
				ostr = (uint8_t *) ds_unknown;
				length = 7;
				break;
			}
			temp = -1;
			length = MAX_IO_SIZE - 1;
			elx_str_cpy((char *)&io->buf[MAX_IO_SIZE], "00-00-00-00-00-00");	/* dst, src */
			do {
				elx_str_itos((long)(ostr[++temp] + 256), lw.str,
					     16);
				io->buf[++length] = lw.str[1];
				io->buf[++length] = lw.str[2];
			} while (++length < MAX_IO_SIZE + 17);
			ostr = &io->buf[MAX_IO_SIZE];
			length = 17;
			break;

			/* FC Portname or FC Nodename address on recursive call */

		case 'E':
			ostr = (uint8_t *) va_arg(inarg, caddr_t);
			if ((chr == 'E') &&
			    ((*(long *)ostr) == (long)NULL) &&
			    ((*(long *)&ostr[4]) == (long)NULL)) {
				ostr = (uint8_t *) ds_unknown;
				length = 7;
				break;
			}
			temp = -1;
			length = MAX_IO_SIZE - 1;
			elx_str_cpy((char *)&io->buf[MAX_IO_SIZE], "00-00-00-00-00-00-00-00");	/* dst, src */
			do {
				elx_str_itos((long)(ostr[++temp] + 256), lw.str,
					     16);
				io->buf[++length] = lw.str[1];
				io->buf[++length] = lw.str[2];
			} while (++length < MAX_IO_SIZE + 23);
			ostr = &io->buf[MAX_IO_SIZE];
			length = 23;
			break;

		case 'f':	/* flags */
			ostr = (uint8_t *) ds_disabled;
			length = 8;
			if (va_arg(inarg, caddr_t) != 0) {	/* test value */
				ostr = (uint8_t *) ds_enabled;
				length = 7;
			}
			if (chr == 'F') {
				length -= 7;
				ostr = (uint8_t *) "-";
			}
			break;

			/* IP address string use recursive call */

		case 'i':
			ostr = (uint8_t *) va_arg(inarg, caddr_t);
			if ((chr == 'i') && *(long *)ostr == (long)NULL)
				goto putnone;
			temp = 0;
			length = MAX_IO_SIZE;
			do {
				length +=
				    elx_str_itos((long)ostr[temp],
						 &io->buf[length], 10);
				if (++temp >= 4)
					break;
				io->buf[length] = '.';
				length++;
			} while (1);
			ostr = &io->buf[MAX_IO_SIZE];
			length -= MAX_IO_SIZE;
			break;

		case 'y':	/* flags */
			if (va_arg(inarg, caddr_t) != 0) {	/* test value */
				ostr = (uint8_t *) "yes";
				length = 3;
			} else {
				ostr = (uint8_t *) "no";
				length = 2;
			}
			break;

		case 'c':
			if (chr == 'C') {	/* normal, control, or none */
				if ((length = va_arg(inarg, int)) < ' ') {
					if (length == 0) {
						ostr = (uint8_t *) ds_none;
						length = 4;
					} else {
						io->buf[MAX_IO_SIZE] = '^';
						io->buf[MAX_IO_SIZE + 1] =
						    ((uint8_t) length) + '@';
						io->buf[MAX_IO_SIZE + 2] = 0;
						ostr = &io->buf[MAX_IO_SIZE];
						length = 2;
					}
					arg.ip++;
					break;
				}
			}

			/* normal, control, or none */
			/*
			   va_arg returns type of arg being process.
			   Second arg passed to va_arg() specifies return 
			   value type.
			 */
			ichar.intchar = va_arg(inarg, int);
#if LITTLE_ENDIAN_HW
			ostr = &ichar.chr[0];
#else
			ostr = &ichar.chr[3];
#endif
			length = 1;
			break;

		case 'd':
			temp = -10;
		      nosign:

			if (longflag)
				lw.ltulong = va_arg(inarg, ulong);
			else if (temp < 0)
				lw.ltlong = va_arg(inarg, long);
			else
				lw.ltulong = va_arg(inarg, ulong);
/*
   nosign2:
*/
			length = elx_str_itos(lw.ltlong, ostr =
					      &io->buf[MAX_IO_SIZE], temp);
			break;

		case 's':
			ostr = (uint8_t *) va_arg(inarg, caddr_t);	/* string */
			if ((chr == 's') || (*ostr != '\0')) {
				length = elx_expanded_len(ostr);
				break;
			}
		      putnone:
			ostr = (uint8_t *) ds_none;
			length = 4;
			break;

		case 't':	/* tabbing */
			if ((width -= io->count) < 0)	/* Spaces required to get to column. */
				width = 0;
			length = 0;	/* nothing other than width padding. */
			ostr = (uint8_t *) ds_null_string;
			break;
		case ' ':
			width = va_arg(inarg, int);
			length = 0;	/* nothing other than width padding. */
			ostr = (uint8_t *) ds_null_string;
			break;

		default:
			ostr = control;
			length = 1;
			break;
		}		/* switch on control */

		if (length < 0) {	/* non printing */
			if (elx_add_string(io, ostr) >= MAX_IO_SIZE)
				elx_print_string(io);	/* no more room, dump current buffer */
			continue;
		}
		/* non printing */
		if (!leftadj && width > length) {
			while (--width >= length) {
				if (elx_add_char(io, padchar) >= MAX_IO_SIZE)
					elx_print_string(io);	/* write it */
			}
		}

		if (width > length)
			width -= length;
		else
			width = 0;

		if (length <= 1) {
			if (length == 1) {
				if (elx_add_char(io, *ostr) >= MAX_IO_SIZE)
					elx_print_string(io);	/* write it */
			}
		} else {
			while ((temp = *ostr++) != 0) {
				if (elx_add_char(io, (uint8_t) temp) >=
				    MAX_IO_SIZE)
					elx_print_string(io);	/* write it */
			}
		}

		while (--width >= 0) {
			if (elx_add_char(io, padchar) >= MAX_IO_SIZE)
				elx_print_string(io);	/* write it */
		}
	}			/* while more in control string */

	/* The io->index should be O now, but if it is not, write the
	 * remainder to console.
	 */
	if (io->index)
		elx_print_string(io);

	elx_kmem_free(io, sizeof (PRINTBLK));

	return (io->count);
}

int
elx_printf(void *control, ...)
{
	int iocnt;
	va_list args;
	va_start(args, control);

	iocnt = elx_fmtout((uint8_t *) NULL, (uint8_t *) control, args);
	va_end(args);
	return (iocnt);
}

int
elx_sprintf_fargs(uint8_t * string, void *control, va_list args)
{
	int i;
	i = elx_fmtout((uint8_t *) string, (uint8_t *) control, args);
	return (i);
}

int
elx_str_sprintf(void *string,	/* output buffer */
		void *control,	/* format string */
		...)
{				/* control arguments */
	int iocnt;
	va_list args;
	va_start(args, control);

	iocnt = elx_fmtout((uint8_t *) string, (uint8_t *) control, args);
	va_end(args);
	return (iocnt);
}

int
elx_printf_log(int brdno, msgLogDef * msg,	/* Pointer to LOG msg structure */
	       void *control, ...)
{
	uint8_t str2[MAX_IO_SIZE + MAX_TBUFF];	/* extra room to convert numbers */
	int iocnt;
	va_list args;
	va_start(args, control);

	if (elx_log_chk_msg_disabled(brdno, msg))
		return (0);	/* This LOG message disabled */

	/* 
	   If LOG message is disabled via any SW method, we SHOULD NOT get this far!
	   We should have taken the above return. 
	 */

	str2[0] = '\0';
	iocnt = elx_sprintf_fargs(str2, control, args);
	va_end(args);
	return (elx_printf_log_msgblk(brdno, msg, (char *)str2));
}

int
elx_log_chk_msg_disabled(int brdno, msgLogDef * msg)
{				/* Pointer to LOG msg structure */
	elxHBA_t *phba;
	elxCfgParam_t *clp;
	int verbose;

	verbose = 0;

	if (msg->msgOutput == ELX_MSG_OPUT_DISA)
		return (1);	/* This LOG message disabled */

	if ((phba = elxDRVR.pHba[brdno])) {
		clp = &phba->config[0];
		verbose = clp[ELX_CFG_LOG_VERBOSE].a_current;
	}

	if (msg->msgOutput == ELX_MSG_OPUT_FORCE) {
		return (0);	/* This LOG message enabled */
	}

	if ((msg->msgType == ELX_LOG_MSG_TYPE_INFO) ||
	    (msg->msgType == ELX_LOG_MSG_TYPE_WARN)) {
		/* LOG msg is INFO or WARN */
		if ((msg->msgMask & verbose) == 0)
			return (1);	/* This LOG message disabled */
	}

	return (0);		/* This LOG message enabled */
}

int
elx_str_ctox(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	else if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	else if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	else
		return (-1);
}

int
elx_str_atox(elxHBA_t * phba, int input_bc,	/* Number of bytes (ASC hex chars) to be converted */
	     int output_bc,	/* Number of bytes in hex output buffer (modulo INT) */
	     char *inp,		/* Pointer to ASC hex input character sequence */
	     char *outp)
{				/* Pointer to hex output buffer */
#define HEX_DIGITS_PER_BYTE        2
#define MAX_ASC_HEX_CHARS_INPUT   32	/* Limit damage if over-write */
#define MAX_BUF_SIZE_HEX_OUTPUT   (MAX_ASC_HEX_CHARS_INPUT / HEX_DIGITS_PER_BYTE)

	int lowNib, hiNib;
	int inputCharsConverted;
	uint8_t twoHexDig;

	inputCharsConverted = 0;
	lowNib = -1;
	hiNib = -1;

	if (input_bc < 1) {
		/* Convert ASC to hex. Input byte cnt < 1. */
		elx_printf_log(phba->brd_no, &elx_msgBlk1210,	/* ptr to msg structure */
			       elx_mes1210,	/* ptr to msg */
			       elx_msgBlk1210.msgPreambleStr);	/* begin & end varargs */
		return (-1);
	}
	if (input_bc > MAX_ASC_HEX_CHARS_INPUT) {
		/* Convert ASC to hex. Input byte cnt > max <num> */
		elx_printf_log(phba->brd_no, &elx_msgBlk1211,	/* ptr to msg structure */
			       elx_mes1211,	/* ptr to msg */
			       elx_msgBlk1211.msgPreambleStr,	/* begin varargs */
			       MAX_ASC_HEX_CHARS_INPUT);	/* end varargs */
		return (-2);
	}
	if ((output_bc * 2) < input_bc) {
		/* Convert ASC to hex. Output buffer to small. */
		elx_printf_log(phba->brd_no, &elx_msgBlk1212,	/* ptr to msg structure */
			       elx_mes1212,	/* ptr to msg */
			       elx_msgBlk1212.msgPreambleStr);	/* begin & end varargs */
		return (-4);
	}

	while (input_bc) {
		twoHexDig = 0;
		lowNib = -1;
		hiNib = elx_str_ctox(*inp++);
		if (--input_bc > 0) {
			lowNib = elx_str_ctox(*inp++);
			input_bc--;
		}
		if ((lowNib < 0) || (hiNib < 0)) {
			/* Convert ASC to hex. Input char seq not ASC hex. */
			elx_printf_log(phba->brd_no, &elx_msgBlk1213,	/* ptr to msg structure */
				       elx_mes1213,	/* ptr to msg */
				       elx_msgBlk1213.msgPreambleStr);	/* begin & end varargs */
			return (-4);
		}
		if (lowNib >= 0) {
			/* There were 2 digits */
			hiNib <<= 4;
			twoHexDig = (hiNib | lowNib);
			inputCharsConverted += 2;
		} else {
			/* There was a single digit */
			twoHexDig = lowNib;
			inputCharsConverted++;
		}
		*outp++ = twoHexDig;
	}			/* while */
	return (inputCharsConverted);	/* ASC to hex conversion complete. Return # of chars converted */
}

char *
elx_str_cpy(char *str1, char *str2)
{
	char *temp;
	temp = str1;

	while ((*str1++ = *str2++) != '\0') {
		continue;
	}
	return (temp);
}

int
elx_str_ncmp(char *str1, char *str2, int cnt)
{
	int c1, c2;
	int dif;

	while (cnt--) {
		c1 = (int)*str1++ & 0xff;
		c2 = (int)*str2++ & 0xff;
		if ((c1 | c2) == 0)
			return (0);	/* strings equal */
		if ((dif = c1 - c2) == 0)
			continue;	/* chars are equal */
		if (c1 == 0)
			return (-1);	/* str1 < str2 */
		if (c2 == 0)
			return (1);	/* str1 > str2 */
		return (dif);
	}
	return (0);		/* strings equal */
}

int
elx_is_digit(int chr)
{
	if ((chr >= '0') && (chr <= '9'))
		return (1);
	return (0);
}				/* fc_is_digit */

int
elx_str_len(char *str)
{
	int n;

	for (n = 0; *str++ != '\0'; n++) ;

	return (n);
}

extern elxDRVR_t elxDRVR;

ELXCLOCK_t *elx_clkgetb(elxHBA_t * phba);
int elx_que_tin(ELX_DLINK_t * blk, ELX_DLINK_t * hdr);

int elx_timer_inp = 0;

/* 
 * boolean to test if block is linked into specific queue
 *  (intended for assertions)
 */
#define elx_inque(x,hdr)  elx_que_tin((ELX_DLINK_t *)(x), (ELX_DLINK_t *)(hdr))
#define ELX_MAX_CLK_TIMEOUT 0xfffffff

ELXCLOCK_t *
elx_clkgetb(elxHBA_t * phba)
{
	ELXCLOCK_t *cb;

	if (phba) {
		cb = (ELXCLOCK_t *) elx_mem_get(phba, MEM_CLOCK);
	} else {
		cb = 0;
	}

	if (cb)
		cb->cl_phba = (void *)phba;
	return (cb);
}

void
elx_clkrelb(elxHBA_t * phba, ELXCLOCK_t * cb)
{
	if (phba) {
		elx_mem_put(phba, MEM_CLOCK, (uint8_t *) cb);
	} else {
		cb->cl_tix = (uint32_t) - 1;
	}
}

int
elx_clk_can(elxHBA_t * phba, ELXCLOCK_t * cb)
{
	ELXCLOCK_INFO_t *clock_info;
	unsigned long iflag;

	if (cb == 0)
		return (0);

	clock_info = &elxDRVR.elx_clock_info;
	ELX_CLK_LOCK(iflag);

	/*  Make sure timer has not expired */
	if (!elx_inque(cb, &clock_info->elx_clkhdr)) {
		ELX_CLK_UNLOCK(iflag);
		return (0);
	}

	elx_clock_deque(cb);

	/* Release clock block */
	elx_clkrelb(phba, cb);
	ELX_CLK_UNLOCK(iflag);
	return (1);
}

unsigned long
elx_clk_rem(elxHBA_t * phba, ELXCLOCK_t * cb)
{
	ELXCLOCK_INFO_t *clock_info;
	ELXCLOCK_t *x;
	unsigned long tix;
	unsigned long iflag;

	clock_info = &elxDRVR.elx_clock_info;
	ELX_CLK_LOCK(iflag);

	tix = 0;
	/* get top of clock queue */
	x = (ELXCLOCK_t *) & clock_info->elx_clkhdr;

	/* Add up ticks in blocks upto specified request */
	do {
		x = x->cl_fw;
		if (x == (ELXCLOCK_t *) & clock_info->elx_clkhdr) {
			ELX_CLK_UNLOCK(iflag);
			return (0);
		}
		tix += x->cl_tix;
	} while (x != cb);

	ELX_CLK_UNLOCK(iflag);
	return (tix);
}

unsigned long
elx_clk_res(elxHBA_t * phba, unsigned long tix, ELXCLOCK_t * cb)
{
	ELXCLOCK_t *x;
	ELXCLOCK_INFO_t *clock_info;
	unsigned long iflag;

	clock_info = &elxDRVR.elx_clock_info;
	ELX_CLK_LOCK(iflag);

	/*  Make sure timer has not expired */
	if (!elx_inque(cb, &clock_info->elx_clkhdr)) {
		ELX_CLK_UNLOCK(iflag);
		return (0);
	}
	if (tix <= 0) {
		ELX_CLK_UNLOCK(iflag);
		return (0);
	}

	/* Round up 1 sec to account for partial first tick */
	tix++;

	elx_clock_deque(cb);

	/* Insert block into queue by order of amount of clock ticks,
	 * each block contains difference in ticks between itself and
	 * its predecessor.
	 */
	x = (ELXCLOCK_t *) clock_info->elx_clkhdr.q_f;
	while (x != (ELXCLOCK_t *) & clock_info->elx_clkhdr) {
		if (x->cl_tix >= tix) {
			/* if inserting in middle of que, adjust next tix */
			x->cl_tix -= tix;
			break;
		}
		tix -= x->cl_tix;
		x = x->cl_fw;
	}

	/* back up one in que */
	x = x->cl_bw;
	elx_enque(cb, x);
	clock_info->elx_clkhdr.q_cnt++;
	cb->cl_tix = tix;

	ELX_CLK_UNLOCK(iflag);
	return (1);
}

ELXCLOCK_t *
elx_clk_set(elxHBA_t * phba,
	    unsigned long tix,
	    void (*func) (elxHBA_t *, void *, void *), void *arg1, void *arg2)
{
	ELXCLOCK_INFO_t *clock_info;
	ELXCLOCK_t *x;
	ELXCLOCK_t *cb;
	unsigned long iflag;

	if (tix > ELX_MAX_CLK_TIMEOUT) {
		return (0);
	}

	/* round up 1 sec to account for partial first tick */
	tix++;
	clock_info = &elxDRVR.elx_clock_info;
	ELX_CLK_LOCK(iflag);

	/* Allocate a CLOCK block */
	if ((cb = elx_clkgetb(phba)) == 0) {
		ELX_CLK_UNLOCK(iflag);
		return (0);
	}

	/* Insert block into queue by order of amount of clock ticks,
	 * each block contains difference in ticks between itself and
	 *its predecessor.
	 */
	x = (ELXCLOCK_t *) clock_info->elx_clkhdr.q_f;
	while (x != (ELXCLOCK_t *) & clock_info->elx_clkhdr) {
		if (x->cl_tix >= tix) {
			/* if inserting in middle of que, adjust next tix */
			if (x->cl_tix > tix) {
				x->cl_tix -= tix;
				break;
			}
			/* Another clock expires at same time.  Maintain the order of requests. */
			for (x = x->cl_fw;
			     x != (ELXCLOCK_t *) & clock_info->elx_clkhdr;
			     x = x->cl_fw) {
				if (x->cl_tix != 0)
					break;
			}

			tix = 0;
			break;
		}

		tix -= x->cl_tix;
		x = x->cl_fw;
	}

	/* back up one in que */
	x = x->cl_bw;

	/* Count the current number of unexpired clocks */
	clock_info->elx_clkhdr.q_cnt++;
	elx_enque(cb, x);
	cb->cl_func = (void (*)(void *, void *, void *))func;
	cb->cl_arg1 = arg1;
	cb->cl_arg2 = arg2;
	cb->cl_tix = tix;
	ELX_CLK_UNLOCK(iflag);
	return ((ELXCLOCK_t *) cb);
}

void
elx_timer(void *p)
{
	elxHBA_t *phba;
	ELXCLOCK_t *x;
	ELXCLOCK_INFO_t *clock_info;
	unsigned long iflag;
	unsigned long tix;

	clock_info = &elxDRVR.elx_clock_info;
	ELX_CLK_LOCK(iflag);
	if (elx_timer_inp) {
		ELX_CLK_UNLOCK(iflag);
		return;
	}
	elx_timer_inp = 1;

	/* Increment time_sample value */
	clock_info->ticks++;

	x = (ELXCLOCK_t *) clock_info->elx_clkhdr.q_f;

	/* counter for propagating negative values */
	tix = 0;
	/* If there are expired clocks */
	if (x != (ELXCLOCK_t *) & clock_info->elx_clkhdr) {
		x->cl_tix = x->cl_tix - 1;
		if (x->cl_tix <= 0) {
			/* Loop thru all clock blocks */
			while (x != (ELXCLOCK_t *) & clock_info->elx_clkhdr) {
				x->cl_tix += tix;
				/* If # of ticks left > 0, break out of loop */
				if (x->cl_tix > 0)
					break;
				tix = x->cl_tix;

				/* Deque expired clock */
				elx_deque(x);
				/* Decrement count of unexpired clocks */
				clock_info->elx_clkhdr.q_cnt--;

				phba = x->cl_phba;
				if (phba) {
					ELX_CLK_UNLOCK(iflag);
					ELX_DRVR_LOCK(phba, iflag);
					/* Call timeout routine */
					(*x->cl_func) (phba, x->cl_arg1,
						       x->cl_arg2);
					ELX_DRVR_UNLOCK(phba, iflag);
					ELX_CLK_LOCK(iflag);
				}
				/* Release clock block */
				elx_clkrelb(phba, x);

				/* start over */
				x = (ELXCLOCK_t *) clock_info->elx_clkhdr.q_f;
			}
		}
	}
	elx_timer_inp = 0;
	ELX_CLK_UNLOCK(iflag);
	return;
}

void
elx_clock_deque(ELXCLOCK_t * cb)
{
	ELXCLOCK_t *x;
	ELXCLOCK_INFO_t *clock_info;

	clock_info = &elxDRVR.elx_clock_info;
	/*
	 ***  Remove the block from its present spot, but first adjust
	 ***  tix field of any successor.
	 */
	if (cb->cl_fw != (ELXCLOCK_t *) & clock_info->elx_clkhdr) {
		x = cb->cl_fw;
		x->cl_tix += cb->cl_tix;
	}

	/* Decrement count of unexpired clocks */
	clock_info->elx_clkhdr.q_cnt--;

	elx_deque(cb);
}

void
elx_clock_init()
{
	ELXCLOCK_INFO_t *clock_info;

	clock_info = &elxDRVR.elx_clock_info;

	/* Initialize clock queue */
	clock_info->elx_clkhdr.q_f = (ELX_DLINK_t *) & clock_info->elx_clkhdr;
	clock_info->elx_clkhdr.q_b = (ELX_DLINK_t *) & clock_info->elx_clkhdr;
	clock_info->elx_clkhdr.q_cnt = 0;

	/* Initialize clock globals */
	clock_info->ticks = 0;
	clock_info->tmr_ct = 0;
}

int
elx_que_tin(ELX_DLINK_t * blk, ELX_DLINK_t * hdr)
{
	ELX_DLINK_t *x;

	x = hdr->q_f;
	while (x != hdr) {
		if (x == blk) {
			return (1);
		}
		x = x->q_f;
	}
	return (0);
}
