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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HBA_API_H
#define HBA_API_H

/* Library version string */
#define HBA_LIBVERSION 2

/* DLL imports for WIN32 operation */
#define HBA_API

	typedef unsigned char HBA_UINT8;	/* Unsigned  8 bits */
	typedef char HBA_INT8;	/* Signed    8 bits */
	typedef unsigned short HBA_UINT16;	/* Unsigned 16 bits */
	typedef short HBA_INT16;	/* Signed   16 bits */
	typedef unsigned int HBA_UINT32;	/* Unsigned 32 bits */
	typedef int HBA_INT32;	/* Signed   32 bits */
	typedef void *HBA_PVOID;	/* Pointer  to void */
	typedef HBA_UINT32 HBA_VOID32;	/* Opaque   32 bits */
	typedef long long HBA_INT64;
	typedef long long HBA_UINT64;

/* 6.1        Handle to Device */
	typedef HBA_UINT32 HBA_HANDLE;

#define HBA_HANDLE_INVALID                   0

/* 6.1        Status Return Values */
	typedef HBA_UINT32 HBA_STATUS;

#define HBA_STATUS_OK                        0
#define HBA_STATUS_ERROR                     1	/* Error */
#define HBA_STATUS_ERROR_NOT_SUPPORTED       2	/* Function not supported. */
#define HBA_STATUS_ERROR_INVALID_HANDLE      3	/* invalid handle */
#define HBA_STATUS_ERROR_ARG                 4	/* Bad argument */
#define HBA_STATUS_ERROR_ILLEGAL_WWN         5	/* WWN not recognized */
#define HBA_STATUS_ERROR_ILLEGAL_INDEX       6	/* Index not recognized */
#define HBA_STATUS_ERROR_MORE_DATA           7	/* Larger buffer required */
#define HBA_STATUS_ERROR_STALE_DATA          8	/* Information has changed since
						 * last call to
						 * HBA_Refreshinformation */
#define HBA_STATUS_SCSI_CHECK_CONDITION      9	/* Obvious */
#define HBA_STATUS_ERROR_BUSY                10	/* HBA busy or reserved,
						 * retry may be effective */
#define HBA_STATUS_ERROR_TRY_AGAIN           11	/* Request timedout,
						 * retry may be effective */
#define HBA_STATUS_ERROR_UNAVAILABLE         12	/* Referenced HBA has been removed
						 * or deactivated */
#define HBA_STATUS_ERROR_ELS_REJECT          13	/* The requested ELS was rejected by
						 * the local HBA */
#define HBA_STATUS_ERROR_INVALID_LUN         14	/* The specified LUN is not provided 
						 *  the specified HBA */
#define HBA_STATUS_ERROR_INCOMPATIBLE        15

#define HBA_STATUS_ERROR_AMBIGUOUS_WWN       16	/* Multiple adapters have a matching
						 * WWN. This could occur if the
						 * NodeWWN of multiple adapters is 
						 * identical */
#define HBA_STATUS_ERROR_LOCAL_BUS           17	/* A persistent binding request
						 * included a bad local SCSI bus
						 * number */
#define HBA_STATUS_ERROR_LOCAL_TARGET        18	/* A persistent binding request
						 * included a bad local SCSI target
						 * number */
#define HBA_STATUS_ERROR_LOCAL_LUN           19	/* A persistent binding request
						 * included a bad local SCSI logical
						 * unit number */
#define HBA_STATUS_ERROR_LOCAL_SCSIID_BOUND  20	/* A persistent binding set request
						 * included a local SCSI ID that was
						 * already bound */
#define HBA_STATUS_ERROR_TARGET_FCID         21	/* A persistent binding request
						 * included a bad or unlocatable FCP
						 * Target FCID */
#define HBA_STATUS_ERROR_TARGET_NODE_WWN     22	/* A persistent binding request 
						 * included a bad FCP Target Node
						 * WWN */
#define HBA_STATUS_ERROR_TARGET_PORT_WWN     23	/* A persistent binding request
						 * included a bad FCP Target Port
						 * WWN */
#define HBA_STATUS_ERROR_TARGET_LUN          24	/* A persistent binding request
						 * included an FCP Logical Unit Number
						 * not defined by the identified 
						 * Target*/
#define HBA_STATUS_ERROR_TARGET_LUID         25	/* A persistent binding request
						 * included an undefined or otherwise
						 * inaccessible Logical Unit Unique
						 * Identifier */
#define HBA_STATUS_ERROR_NO_SUCH_BINDING     26	/* A persistent binding remove request
						 * included a binding which did not
						 * match a binding established by the
						 * specified port */
#define HBA_STATUS_ERROR_NOT_A_TARGET        27	/* A SCSI command was requested to an
						 * Nx_Port that was not a SCSI
						 * Target Port */
#define HBA_STATUS_ERROR_UNSUPPORTED_FC4     28	/* A request was made concerning an 
						 * unsupported FC-4 protocol */
#define HBA_STATUS_ERROR_INCAPABLE           29	/* A request was made to enable 
						 * unimplemented capabilities for a 
						 * port */
#define HBA_STATUS_ERROR_TARGET_BUSY         30	/* A SCSI function was requested
						 * at a time when issuing the 
						 * requested command would cause a
						 * a SCSI overlapped command 
						 * condition (see SAM-3) */

/* 6.4.1        Port Operational Modes Values */
	typedef HBA_UINT32 HBA_PORTTYPE;

#define HBA_PORTTYPE_UNKNOWN                1	/* Unknown */
#define HBA_PORTTYPE_OTHER                  2	/* Other */
#define HBA_PORTTYPE_NOTPRESENT             3	/* Not present */
#define HBA_PORTTYPE_NPORT                  5	/* Fabric  */
#define HBA_PORTTYPE_NLPORT                 6	/* Public Loop */
#define HBA_PORTTYPE_FLPORT                 7
#define HBA_PORTTYPE_FPORT                  8	/* Fabric Port */
#define HBA_PORTTYPE_LPORT                  20	/* Private Loop */
#define HBA_PORTTYPE_PTP                    21	/* Point to Point */

	typedef HBA_UINT32 HBA_PORTSTATE;
#define HBA_PORTSTATE_UNKNOWN               1	/* Unknown */
#define HBA_PORTSTATE_ONLINE                2	/* Operational */
#define HBA_PORTSTATE_OFFLINE               3	/* User Offline */
#define HBA_PORTSTATE_BYPASSED              4	/* Bypassed */
#define HBA_PORTSTATE_DIAGNOSTICS           5	/* In diagnostics mode */
#define HBA_PORTSTATE_LINKDOWN              6	/* Link Down */
#define HBA_PORTSTATE_ERROR                 7	/* Port Error */
#define HBA_PORTSTATE_LOOPBACK              8	/* Loopback */

	typedef HBA_UINT32 HBA_PORTSPEED;
#define HBA_PORTSPEED_UNKNOWN               0	/* Unknown - transceiver incable
						 * of reporting */
#define HBA_PORTSPEED_1GBIT                 1	/* 1 GBit/sec */
#define HBA_PORTSPEED_2GBIT                 2	/* 2 GBit/sec */
#define HBA_PORTSPEED_10GBIT                4	/* 10 GBit/sec */
#define HBA_PORTSPEED_NOT_NEGOTIATED        5	/* Speed not established */

/* 6.4.1.4        See "Class of Service  - Format" in GC-GS-4 */

	typedef HBA_UINT32 HBA_COS;

/* 6.4.1.5        Fc4Types Values */

	typedef struct HBA_fc4types {
		HBA_UINT8 bits[32];	/* 32 bytes of FC-4 per GS-2 */
	} HBA_FC4TYPES, *PHBA_FC4TYPES;

/* 6.1        Basic Types */

	typedef struct HBA_wwn {
		HBA_UINT8 wwn[8];
	} HBA_WWN, *PHBA_WWN;

	typedef struct HBA_ipaddress {
		int ipversion;	/* see enumerations in RNID */
		union {
			unsigned char ipv4address[4];
			unsigned char ipv6address[16];
		} ipaddress;
	} HBA_IPADDRESS, *PHBA_IPADDRESS;

	typedef HBA_INT8 HBA_BOOLEAN;

/* 6.3.1        Adapter Attributes */
	typedef struct hba_AdapterAttributes {
		char Manufacturer[64];
		char SerialNumber[64];
		char Model[256];
		char ModelDescription[256];
		HBA_WWN NodeWWN;
		char NodeSymbolicName[256];
		char HardwareVersion[256];
		char DriverVersion[256];
		char OptionROMVersion[256];
		char FirmwareVersion[256];
		HBA_UINT32 VendorSpecificID;
		HBA_UINT32 NumberOfPorts;
		char DriverName[256];
	} HBA_ADAPTERATTRIBUTES, *PHBA_ADAPTERATTRIBUTES;

/* 6.4.1.6        Port Attributes */
	typedef struct HBA_PortAttributes {
		HBA_WWN NodeWWN;
		HBA_WWN PortWWN;
		HBA_UINT32 PortFcId;
		HBA_PORTTYPE PortType;
		HBA_PORTSTATE PortState;
		HBA_COS PortSupportedClassofService;
		HBA_FC4TYPES PortSupportedFc4Types;
		HBA_FC4TYPES PortActiveFc4Types;
		char PortSymbolicName[256];
		char OSDeviceName[256];
		HBA_PORTSPEED PortSupportedSpeed;
		HBA_PORTSPEED PortSpeed;
		HBA_UINT32 PortMaxFrameSize;
		HBA_WWN FabricName;
		HBA_UINT32 NumberofDiscoveredPorts;
	} HBA_PORTATTRIBUTES, *PHBA_PORTATTRIBUTES;

	typedef struct HBA_PortStatistics {
		HBA_INT64 SecondsSinceLastReset;
		HBA_INT64 TxFrames;
		HBA_INT64 TxWords;
		HBA_INT64 RxFrames;
		HBA_INT64 RxWords;
		HBA_INT64 LIPCount;
		HBA_INT64 NOSCount;
		HBA_INT64 ErrorFrames;
		HBA_INT64 DumpedFrames;
		HBA_INT64 LinkFailureCount;
		HBA_INT64 LossOfSyncCount;
		HBA_INT64 LossOfSignalCount;
		HBA_INT64 PrimitiveSeqProtocolErrCount;
		HBA_INT64 InvalidTxWordCount;
		HBA_INT64 InvalidCRCCount;
	} HBA_PORTSTATISTICS, *PHBA_PORTSTATISTICS;

/* 6.6.1                FCP Attributes */

	typedef enum HBA_fcpbindingtype { TO_D_ID, TO_WWN,
		    TO_OTHER } HBA_FCPBINDINGTYPE;

	typedef struct HBA_ScsiId {
		char OSDeviceName[256];
		HBA_UINT32 ScsiBusNumber;
		HBA_UINT32 ScsiTargetNumber;
		HBA_UINT32 ScsiOSLun;
	} HBA_SCSIID, *PHBA_SCSIID;

	typedef struct HBA_FcpId {
		HBA_UINT32 FcId;
		HBA_WWN NodeWWN;
		HBA_WWN PortWWN;
		HBA_UINT64 FcpLun;
	} HBA_FCPID, *PHBA_FCPID;

	typedef struct HBA_LUID {
		char buffer[256];
	} HBA_LUID, *PHBA_LUID;

	typedef struct HBA_FcpScsiEntry {
		HBA_SCSIID ScsiId;
		HBA_FCPID FcpId;
	} HBA_FCPSCSIENTRY, *PHBA_FCPSCSIENTRY;

	typedef struct HBA_FcpScsiEntryV2 {
		HBA_SCSIID ScsiId;
		HBA_FCPID FcpId;
		HBA_LUID LUID;
	} HBA_FCPSCSIENTRYV2, *PHBA_FCPSCSIENTRYV2;

	typedef struct HBA_FCPTargetMapping {
		HBA_UINT32 NumberOfEntries;
		HBA_FCPSCSIENTRY entry[1];	/* Variable length array
						 * containing mappings */
	} HBA_FCPTARGETMAPPING, *PHBA_FCPTARGETMAPPING;

	typedef struct HBA_FCPTargetMappingV2 {
		HBA_UINT32 NumberOfEntries;
		HBA_FCPSCSIENTRYV2 entry[1];	/* Variable length array
						 * containing mappings */
	} HBA_FCPTARGETMAPPINGV2, *PHBA_FCPTARGETMAPPINGV2;

	typedef struct HBA_FCPBindingEntry {
		HBA_FCPBINDINGTYPE type;
		HBA_SCSIID ScsiId;
		HBA_FCPID FcpId;	/* WWN valid only if type is
					 * to WWN, FcpLun always valid */
		HBA_UINT32 FcId;
	} HBA_FCPBINDINGENTRY, *PHBA_FCPBINDINGENTRY;

	typedef struct HBA_FCPBinding {
		HBA_UINT32 NumberOfEntries;
		HBA_FCPBINDINGENTRY entry[1];	/* Variable length array */
	} HBA_FCPBINDING, *PHBA_FCPBINDING;

/* 6.7.1        FC-3 Management Atrributes */

	typedef enum HBA_wwntype { NODE_WWN, PORT_WWN } HBA_WWNTYPE;

	typedef struct HBA_MgmtInfo {
		HBA_WWN wwn;
		HBA_UINT32 unittype;
		HBA_UINT32 PortId;
		HBA_UINT32 NumberOfAttachedNodes;
		HBA_UINT16 IPVersion;
		HBA_UINT16 UDPPort;
		HBA_UINT8 IPAddress[16];
		HBA_UINT16 reserved;
		HBA_UINT16 TopologyDiscoveryFlags;
	} HBA_MGMTINFO, *PHBA_MGMTINFO;

/* Event Codes */
#define HBA_EVENT_LIP_OCCURRED           1
#define HBA_EVENT_LINK_UP                2
#define HBA_EVENT_LINK_DOWN              3
#define HBA_EVENT_LIP_RESET_OCCURRED     4
#define HBA_EVENT_RSCN                   5
#define HBA_EVENT_PROPRIETARY            0xFFFF

	typedef struct HBA_Link_EventInfo {
		HBA_UINT32 PortFcId;	/* Port where event occurred */
		HBA_UINT32 Reserved[3];
	} HBA_LINK_EVENTINFO, *PHBA_LINK_EVENTINFO;

	typedef struct HBA_RSCN_EventInfo {
		HBA_UINT32 PortFcId;	/* Port where event occurred */
		HBA_UINT32 NPortPage;	/* Reference FC-FS for RSCN ELS
					 * "Affected N-Port Pages"*/
		HBA_UINT32 Reserved[2];
	} HBA_RSCN_EVENTINFO, *PHBA_RSCN_EVENTINFO;

	typedef struct HBA_Pty_EventInfo {
		HBA_UINT32 PtyData[4];	/* Proprietary data */
	} HBA_PTY_EVENTINFO, *PHBA_PTY_EVENTINFO;

	typedef struct HBA_EventInfo {
		HBA_UINT32 EventCode;
		union {
			HBA_LINK_EVENTINFO Link_EventInfo;
			HBA_RSCN_EVENTINFO RSCN_EventInfo;
			HBA_PTY_EVENTINFO Pty_EventInfo;
		} Event;
	} HBA_EVENTINFO, *PHBA_EVENTINFO;

/* Persistant Binding... */
	typedef HBA_UINT32 HBA_BIND_TYPE;
#define HBA_BIND_TO_D_ID                0x0001
#define HBA_BIND_TO_WWPN                0x0002
#define HBA_BIND_TO_WWNN                0x0004
#define HBA_BIND_TO_LUID                0x0008
#define HBA_BIND_TARGETS                0x0800

/* A bit mask of Rev 2.0 persistent binding capabilities */
	typedef HBA_UINT32 HBA_BIND_CAPABILITY;
/* The following are bit flags indicating persistent binding capabilities */
#define HBA_CAN_BIND_TO_D_ID                0x0001
#define HBA_CAN_BIND_TO_WWPN                0x0002
#define HBA_CAN_BIND_TO_WWNN                0x0004
#define HBA_CAN_BIND_TO_LUID                0x0008
#define HBA_CAN_BIND_ANY_LUNS               0x0400
#define HBA_CAN_BIND_TARGETS                0x0800
#define HBA_CAN_BIND_AUTOMAP                0x1000
#define HBA_CAN_BIND_CONFIGURED             0x2000

#define HBA_BIND_STATUS_DISABLED            0x00
#define HBA_BIND_STATUS_ENABLED             0x01

	typedef HBA_UINT32 HBA_BIND_STATUS;

#define HBA_BIND_EFFECTIVE_AT_REBOOT        0x00
#define HBA_BIND_EFFECTIVE_IMMEDIATE        0x01

	typedef HBA_UINT32 HBA_BIND_EFFECTIVE;

	typedef struct HBA_FCPBindingEntry2 {
		HBA_BIND_TYPE type;
		HBA_SCSIID ScsiId;
		HBA_FCPID FcpId;
		HBA_LUID LUID;
		HBA_STATUS status;
	} HBA_FCPBINDINGENTRY2, *PHBA_FCPBINDINGENTRY2;

	typedef struct HBA_FcpBinding2 {
		HBA_UINT32 NumberOfEntries;
		HBA_FCPBINDINGENTRY2 entry[1];	/* Variable length array */
	} HBA_FCPBINDING2, *PHBA_FCPBINDING2;

/* FC-4 Instrumentation */
	typedef struct HBA_FC4Statistics {
		HBA_INT64 InputRequests;
		HBA_INT64 OutputRequests;
		HBA_INT64 ControlRequests;
		HBA_INT64 InputMegabytes;
		HBA_INT64 OutputMegabytes;
	} HBA_FC4STATISTICS, *PHBA_FC4STATISTICS;

	typedef void *HBA_CALLBACKHANDLE;
/* Adapter Level Events */
#define HBA_EVENT_ADAPTER_UNKNOWN       0x100
#define HBA_EVENT_ADAPTER_ADD           0x101
#define HBA_EVENT_ADAPTER_REMOVE        0x102
#define HBA_EVENT_ADAPTER_CHANGE        0x103

/* Port Level Events */
#define HBA_EVENT_PORT_UNKNOWN          0x200
#define HBA_EVENT_PORT_OFFLINE          0x201
#define HBA_EVENT_PORT_ONLINE           0x202
#define HBA_EVENT_PORT_NEW_TARGETS      0x203
#define HBA_EVENT_PORT_FABRIC           0x204

/* Port Statistics Events */
#define HBA_EVENT_PORT_STAT_THRESHOLD   0x301
#define HBA_EVENT_PORT_STAT_GROWTH      0x302

/* Target Level Events */
#define HBA_EVENT_TARGET_UNKNOWN        0x400
#define HBA_EVENT_TARGET_OFFLINE        0x401
#define HBA_EVENT_TARGET_ONLINE         0x402
#define HBA_EVENT_TARGET_REMOVED        0x403

/* Fabric Link  Events */
#define HBA_EVENT_LINK_UNKNOWN          0x500
#define HBA_EVENT_LINK_INCIDENT         0x501

/* Used for OSDeviceName */
	typedef struct HBA_osdn {
		char drvname[32];
		uint32_t instance;
		uint32_t target;
		uint32_t lun;
		uint32_t bus;
		char flags;
		char sizeSN;
		char InquirySN[64];
	} HBA_OSDN;

/* type definitions for GetBindList function */
	typedef enum HBA_bindtype { BIND_NONE, BIND_WWNN, BIND_WWPN, BIND_DID,
		    BIND_ALPA } HBA_BINDTYPE;
/* Bind Entry flags */
#define         HBA_BIND_AUTOMAP  0x1	/* Node is automapped            */
#define         HBA_BIND_BINDLIST 0x2	/* entry in bind list not mapped */
#define         HBA_BIND_MAPPED   0x4	/* Node is mapped to  a scsiid   */
#define         HBA_BIND_UNMAPPED 0x8	/* Node is unmapped              */
#define         HBA_BIND_NODEVTMO 0x10	/* NODEVTMO flag of the node     */
#define         HBA_BIND_NOSCSIID 0x20	/* No scsi id is assigned yet    */
#define         HBA_BIND_RPTLUNST 0x40	/* Node is in report lun cmpl st */
	typedef struct {
		HBA_BINDTYPE bind_type;
		HBA_UINT32 scsi_id;
		HBA_UINT32 did;
		HBA_WWN wwnn;
		HBA_WWN wwpn;
		HBA_UINT32 flags;
	} HBA_BIND_ENTRY;

	typedef struct {
		HBA_UINT32 NumberOfEntries;
		HBA_BIND_ENTRY entry[1];	/* Variable length array */
	} HBA_BIND_LIST, *HBA_BIND_LIST_PTR;

#endif				/* HBA_API_H */

#ifdef __cplusplus
}
#endif
