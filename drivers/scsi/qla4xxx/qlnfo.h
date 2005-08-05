/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.6.x
 * Copyright (C) 2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/
/*
 * File Name: qlnfo.h
 *
 * Revision History:
 *
 */

#ifndef	_QLNFO_H
#define	_QLNFO_H

#include "qlud.h"

/*
 * NOTE: the following version defines must be updated each time the
 *	 changes made may affect the backward compatibility of the
 *	 input/output relations
 */
#define	NFO_VERSION             1
#define	NFO_VERSION_STR         "1.0"

/*
 * ***********************************************************************
 * Data type definitions
 * ***********************************************************************
 */
#ifdef _MSC_VER

#include "ntddscsi.h"
#include "qlnfowin.h"

/*
 * ***********************************************************************
 * OS dependent General configuration defines
 * ***********************************************************************
 */

#elif defined(linux)                 /* Linux */

#elif defined(sun) || defined(__sun) /* Solaris */

#endif

/*
 * ***********************************************************************
 * Generic definitions
 * ***********************************************************************
 */
#define	NFO_DEF_SIGNATURE_SIZE		8
#define	NFO_DEF_SIGNATURE		"QLGCNFO"

/* Constants */
#define NFO_DEF_UNSUPPORTED		0xFFFFFFFF
#define NFO_DEF_STR_NAME_SIZE_32	32
#define NFO_DEF_STR_NAME_SIZE_64	64
#define NFO_DEF_STR_NAME_SIZE_128	128
#define NFO_DEF_STR_NAME_SIZE_256	256
#define NFO_DEF_STR_NAME_SIZE_512	512
#define NFO_DEF_INQ_VENDOR_ID_SIZE	8
#define NFO_DEF_INQ_PROD_ID_SIZE	16
#define NFO_DEF_INQ_PROD_VER_SIZE	4
#define NFO_DEF_INQ_LUID_SIZE		16
#define NFO_DEF_INQ_SERIAL_NO_SIZE	16
#define NFO_DEF_PATH_ALL		NFO_DEF_UNSUPPORTED /* All paths */

/* Device transport protocol */
#define NFO_TRANSPORT_FC		1
#define NFO_TRANSPORT_ISCSI		2
#define NFO_TRANSPORT_NO_SUP		NFO_TRANSPORT_ISCSI	/* No supported */
#define NFO_TRANSPORT_UNKNOWN		NFO_DEF_UNSUPPORTED

/* Unique identification */
#define NFO_FC_WWN_SIZE			8
#define NFO_FC_PID_SIZE			4
#define NFO_IS_NAME_SIZE		256
#define NFO_IS_IP_ADDR_SIZE		16
#define NFO_IS_IP_ADDR_TYPE4		4
#define NFO_IS_IP_ADDR_TYPE6		6

/* API_INFO */
#define NFO_AI_MAXFOM_NO_LIMIT		NFO_DEF_UNSUPPORTED /* No limit */

/* FOM_PROP */
#define NFO_FP_FLG_HBA			1	/* FO implemented in HBA driver */
#define NFO_FP_FLG_DISABLE		2	/* FOM disabled */
#define NFO_FP_FLG_SUP_LB		16	/* Support load balancing */
#define NFO_FP_FLG_SUP_PATH_ORDER	32	/* Support path ordering */
#define NFO_FP_FLG_SUP_PATH_WEIGH	64	/* Support path weigh */
#define NFO_FOM_PROP_NO_SUP		1	/* Settable property supported no */

/* PATH_INFO */
#define NFO_PI_PREFERRED		1	/* Preferred path bit */
#define NFO_PATH_PROP_NO_SUP		0	/* Settable property supported no */

/* LB_POLICY */
#define NFO_LB_UNKNOWN			NFO_DEF_UNSUPPORTED
#define NFO_LB_FAILOVER_ONLY		1
#define NFO_LB_ROUND_ROBIN		2
#define NFO_LB_ROUND_ROBIN_SUBSET	3
#define NFO_LB_DYN_LEAST_QUEUE_DEPTH	4
#define NFO_LB_WEIGHTED_PATHS		5
#define NFO_LB_LEAST_BLOCKS		6
#define NFO_LB_VENDOR_SPECIFIC		7
#define NFO_LB_STATIC			8

/* SPC3 Asymmetric access state */
#define NFO_AAS_ACTIVE_OPT   	0
#define NFO_AAS_ACTIVE_NONOPT 	1
#define NFO_AAS_STANDBY		2
#define NFO_AAS_UNAVAIL		3
#define NFO_AAS_RESERVED	4
#define NFO_AAS_ILL_REQ		15

/* Device state */
#define NFO_DS_ACTIVE   	1
#define NFO_DS_PASSIVE 		2
#define NFO_DS_FAILED		3
#define NFO_DS_PENDING_REMOVE	4
#define NFO_DS_REMOVED		5
#define NFO_DS_UNAVAILABLE 	6
#define NFO_DS_TRANSITIONING 	7
#define NFO_DS_RESERVED		8

/* Fog state */
#define NFO_FOG_NORMAL   	1
#define NFO_FOG_PENDING 	2
#define NFO_FOG_FAILBACK	3
#define NFO_FOG_FAILOVER	4

/* Return status */
#define NFO_STS_BASE			0x90000000
#define NFO_STS_OK			(NFO_STS_BASE + 0)
#define NFO_STS_INV_HNDL		(NFO_STS_BASE + 1)
#define NFO_STS_INV_INSTN		(NFO_STS_BASE + 2)
#define NFO_STS_UNDERRUN		(NFO_STS_BASE + 3)
#define NFO_STS_EXISTED			(NFO_STS_BASE + 4)
#define NFO_STS_NOT_PRESENT		(NFO_STS_BASE + 5)
#define NFO_STS_FAIL                   	(NFO_STS_BASE + 6)
#define NFO_STS_NOT_YET_IMPLEMENTED	(NFO_STS_BASE + 7)
#define NFO_STS_UNSUP			(NFO_STS_BASE + 8)	/* Not supported */
#define NFO_STS_INV_INSTANCE		(NFO_STS_BASE + 9)	/* Invalid instance */
#define NFO_STS_REBOOT_NEEDED		(NFO_STS_BASE + 10)	/* Reboot needed */
#define NFO_STS_INV_PATH		(NFO_STS_BASE + 11)	/* Invalid path */
#define NFO_STS_INV_PARAM		(NFO_STS_BASE + 19)
#define NFO_STS_INV_PARAM0		(NFO_STS_BASE + 20)
#define NFO_STS_INV_PARAM1		(NFO_STS_BASE + 21)
#define NFO_STS_INV_PARAM2		(NFO_STS_BASE + 22)
#define NFO_STS_INV_PARAM3		(NFO_STS_BASE + 23)
#define NFO_STS_INV_PARAM4		(NFO_STS_BASE + 24)
#define NFO_STS_INV_PARAM5		(NFO_STS_BASE + 25)
#define NFO_STS_INV_PARAM6		(NFO_STS_BASE + 26)
#define NFO_STS_INV_PARAM7		(NFO_STS_BASE + 27)
#define NFO_STS_INV_PARAM8		(NFO_STS_BASE + 28)
#define NFO_STS_INV_PARAM9		(NFO_STS_BASE + 29)
#define NFO_STS_CFG_CHANGED		(NFO_STS_BASE + 50)
#define NFO_STS_FOM_ENABLED		(NFO_STS_BASE + 51)
#define NFO_STS_FOM_DISABLED		(NFO_STS_BASE + 52)
#define NFO_STS_FOM_ADDED		(NFO_STS_BASE + 53)
#define NFO_STS_FOM_REMOVED		(NFO_STS_BASE + 54)
#define NFO_STS_HBA_ADDED		(NFO_STS_BASE + 55)
#define NFO_STS_HBA_REMOVED		(NFO_STS_BASE + 56)
#define NFO_STS_PATH_ADDED		(NFO_STS_BASE + 57)
#define NFO_STS_PATH_REMOVED		(NFO_STS_BASE + 58)
#define NFO_STS_DEV_ADDED		(NFO_STS_BASE + 59)
#define NFO_STS_DEV_REMOVED		(NFO_STS_BASE + 60)

/* Event Codes */
#define NFO_ES_INFO		0x60000000
#define NFO_ES_WARN		0xA0000000
#define NFO_ES_ERR		0xE0000000
#define NFO_EF_FOM		0x00010000
#define NFO_EF_HBA		0x00020000
#define NFO_EF_DPG		0x00030000
#define NFO_EF_PATH		0x00040000
#define NFO_EVT_FOM_ENABLED	(NFO_ES_INFO | NFO_EF_FOM  | 1)	/* FOM enable */
#define NFO_EVT_FOM_DISABLED	(NFO_ES_INFO | NFO_EF_FOM  | 2)	/* FOM disable */
#define NFO_EVT_FOM_ADDED	(NFO_ES_INFO | NFO_EF_FOM  | 3)	/* FOM  add */
#define NFO_EVT_FOM_REMOVED	(NFO_ES_INFO | NFO_EF_FOM  | 4)	/* FOM  del */
#define NFO_EVT_HBA_ADDED	(NFO_ES_INFO | NFO_EF_HBA  | 5)	/* HBA  add */
#define NFO_EVT_HBA_REMOVED	(NFO_ES_INFO | NFO_EF_HBA  | 6)	/* HBA  del */
#define NFO_EVT_PATH_ADDED	(NFO_ES_INFO | NFO_EF_PATH | 7)	/* Path add */
#define NFO_EVT_PATH_REMOVED	(NFO_ES_INFO | NFO_EF_PATH | 8)	/* Path del */
#define NFO_EVT_DEV_ADDED	(NFO_ES_INFO | NFO_EF_PATH | 9)	/* Dev  add */
#define NFO_EVT_DEV_REMOVED	(NFO_ES_INFO | NFO_EF_PATH | 10)	/* Dev  del */
#define NFO_EVT_PATH_FAILOVER	(NFO_ES_INFO | NFO_EF_PATH | 11)	/* Path failover */
#define NFO_EVT_PATH_FAILBACK	(NFO_ES_INFO | NFO_EF_PATH | 12)	/* Path failback */
#define NFO_EVT_ER_THOLD	(NFO_ES_INFO | NFO_EF_DPG  | 13)	/* Err threshold */
#define NFO_EVT_FO_THOLD	(NFO_ES_INFO | NFO_EF_DPG  | 14)	/* Fo  threshold */
#define NFO_MAX_EVENT		(NFO_EVT_END)

#define NFO_EVENT_CB             UD_H
/*
 * ***********************************************************************
 * Common header struct definitions
 * ***********************************************************************
 */
typedef struct _NFO_API_INFO
{
	UD_UI4	Version;
	UD_UI4	MaxFOM;
	UD_UI4	Reserved[8];
} NFO_API_INFO, *PNFO_API_INFO;

typedef struct _NFO_PROP_ENTRY
{
	UD_UI4	Current;
	UD_UI4	Min;
	UD_UI4	Def;
	UD_UI4	Max;
	UD_UI1	Name[NFO_DEF_STR_NAME_SIZE_32];
	UD_UI4	Reserved[8];
} NFO_PROP_ENTRY, *PNFO_PROP_ENTRY;

typedef struct _NFO_PROP_LIST
{
	UD_UI4   Size;
	UD_UI4   Count;
	NFO_PROP_ENTRY   Entry[1];
} NFO_PROP_LIST, *PNFO_PROP_LIST;

typedef struct _NFO_FOM_PROP
{
	UD_UI4	Version;
	UD_UI4	Flag;
	UD_UI1	Name[NFO_DEF_STR_NAME_SIZE_32];
	UD_UI4 	HbaCount;
	UD_UI4 	DpgCount;
	UD_UI4	SupportedTargetCount;
	UD_UI4	CurrentTargetCount;
	UD_UI4	MaxPath;
	UD_UI4	Reserved[8];
	NFO_PROP_LIST	PropList;
} NFO_FOM_PROP, *PNFO_FOM_PROP;

typedef struct _NFO_FC_UID
{
	UD_UI1	Wwpn[NFO_FC_WWN_SIZE];
	UD_UI1	Wwnn[NFO_FC_WWN_SIZE];
	UD_UI1	Pid[NFO_FC_PID_SIZE];
	UD_UI4	Reserved[8];
} NFO_FC_UID, *PNFO_FC_UID;

typedef struct _NFO_IS_UID
{
	UD_UI4	IpType;
	UD_UI1	Ip[NFO_IS_IP_ADDR_SIZE];
	UD_UI1	Name[NFO_IS_NAME_SIZE];
	UD_UI4	Reserved[8];
} NFO_IS_UID, *PNFO_IS_UID;

typedef struct _NFO_TRANSPORT
{
	UD_UI4	Value;
	UD_UI1	Name[NFO_IS_NAME_SIZE];
	UD_UI4	Reserved[8];
} NFO_TRANSPORT, *PNFO_TRANSPORT;

typedef struct _NFO_TRANSPORT_LIST
{
	UD_UI4   Size;
	UD_UI4   Count;
	NFO_TRANSPORT	Entry[1];
} NFO_TRANSPORT_LIST, *PNFO_TRANSPORT_LIST;

typedef struct _NFO_HBA_INFO
{
	UD_UI4	Number;
	UD_UI4	Transport;
	UD_UI1	Name[NFO_DEF_STR_NAME_SIZE_64];
	union
	{
		NFO_FC_UID	FcUid;
		NFO_IS_UID	IsUid;
	} Uid;
	UD_UI4	Reserved[8];
} NFO_HBA_INFO, *PNFO_HBA_INFO;

typedef struct _NFO_HBA_INFO_LIST
{
	UD_UI4 		Size;
	UD_UI4 		Count;
	NFO_HBA_INFO	Entry[1];
} NFO_HBA_INFO_LIST, *PNFO_HBA_INFO_LIST;

typedef struct _NFO_SCSI_ADDR
{
	UD_UI4    	Number;
	UD_UI4    	Bus;
	UD_UI4    	Target;
	UD_UI4     	Lun;
} NFO_SCSI_ADDR, *PNFO_SCSI_ADDR;

typedef struct _NFO_DEV_INFO
{
	UD_UI1	Name[NFO_DEF_STR_NAME_SIZE_64];
	UD_UI1	VendorId[NFO_DEF_INQ_VENDOR_ID_SIZE];
	UD_UI1	ProductId[NFO_DEF_INQ_PROD_ID_SIZE];
	UD_UI1 	ProductVersion[NFO_DEF_INQ_PROD_VER_SIZE];
	UD_UI1 	Luid[NFO_DEF_INQ_LUID_SIZE];
	UD_UI4 	Transport;
	union
	{
		NFO_FC_UID	FcUid;
		NFO_IS_UID	IsUid;
	} Uid;
	UD_UI4	Reserved[8];
} NFO_DEV_INFO, *PNFO_DEV_INFO;

typedef struct _LB_POLICY
{
	UD_UI4	Value;
	UD_UI1	Name[NFO_DEF_STR_NAME_SIZE_32];
	UD_UI4	Reserved[8];
} NFO_LB_POLICY, *PNFO_LB_POLICY;

typedef struct _LB_POLICY_LIST
{
	UD_UI4     Size;
	UD_UI1     Count;
	NFO_LB_POLICY Entry[1];
} NFO_LB_POLICY_LIST, *PNFO_LB_POLICY_LIST;

typedef struct _LB_POLICY_INFO
{
	NFO_LB_POLICY_LIST Supported;
	UD_UI4	Current;
	UD_UI4	ActivePathCount;
	UD_UI4	Reserved[8];
} NFO_LB_POLICY_INFO, *PNFO_LB_POLICY_INFO;

typedef struct _DPG_PROP
{
	UD_UI1		Name[NFO_DEF_STR_NAME_SIZE_64];
	NFO_DEV_INFO 	DevInfo;
	UD_UI4		PathCount;
	NFO_LB_POLICY  	LbPolicy;    
	UD_UI4		Reserved[8];
} NFO_DPG_PROP, *PNFO_DPG_PROP;

typedef struct _NFO_DPG_PROP_LIST
{
	UD_UI4 		Size;
	UD_UI4 		Count;
	NFO_DPG_PROP	Entry[1];
} NFO_DPG_PROP_LIST, *PNFO_DPG_PROP_LIST;

typedef struct _NFO_PATH_INFO
{
	NFO_SCSI_ADDR 	ScsiAddr;
	UD_UI1		Name[NFO_DEF_STR_NAME_SIZE_64];
	UD_UI4  	Status;
	UD_UI4 		Flag;
	UD_UI4 		RelTgtPortId;
	UD_UI4 		TgtPortGrp;
	UD_UI4		Reserved[8];
	NFO_PROP_LIST	PropList;
} NFO_PATH_INFO, *PNFO_PATH_INFO;

typedef struct _NFO_IO_STAT
{
	UD_UI8	NoRead;
	UD_UI8	NoWrite;
	UD_UI8	MBRead;
	UD_UI8	MBWritten;
	UD_UI4	Reserved[8];
} NFO_IO_STAT, *PNFO_IO_STAT;

typedef struct _NFO_PATH_STAT
{
	UD_UI8	NoFailover;
	UD_UI8	NoFailback;
	UD_UI4	Reserved[8];
} NFO_PATH_STAT, *PNFO_PATH_STAT;

typedef struct _NFO_ER_STAT
{
	UD_UI8	NoReadRetry;
	UD_UI8	NoWriteRetry;
	UD_UI8	NoReadFailure;
	UD_UI8	NoWriteFailure;
	UD_UI8	NoFailover;
	UD_UI4	Reserved[8];
} NFO_ER_STAT, *PNFO_ER_STAT;

typedef struct _NFO_ADP_STAT
{
	NFO_IO_STAT	IoStat;
	NFO_ER_STAT	ErStat;
	NFO_PATH_STAT	PathStat;
	UD_UI4		Reserved[8];
} NFO_ADP_STAT, *PNFO_ADP_STAT;

typedef struct _NFO_STORAGE
{
	UD_UI1	Name[NFO_DEF_STR_NAME_SIZE_32];
	UD_UI4	Type;
	UD_UI4	ControlFlag;
	UD_UI4	DefaultLB;
	UD_UI4	Reserved[8];
} NFO_STORAGE, *PNFO_STORAGE;


typedef struct _NFO_STORAGE_LIST
{
	UD_UI4 		Size;
	UD_UI4		Count;
	NFO_STORAGE 	SupportList[1];
} NFO_STORAGE_LIST, *PNFO_STORAGE_LIST;

typedef struct _NFO_PATH
{
	UD_UI8		PathUid;
	UD_UI4		Fom;
	NFO_PATH_INFO	PathInfo;
	UD_UI4		DPathStatus;
	UD_UI4		HbaInstance;
	UD_UI4		DpgInstance;
	UD_UI4		StorageInstance;
	NFO_HBA_INFO	HbaInfo;
	NFO_DPG_PROP	DpgProp;
	NFO_STORAGE	Storage;
	UD_UI4		Reserved[8];
} NFO_PATH, *PNFO_PATH;

typedef struct _NFO_PATH_INFO_LIST
{
	UD_UI4 		Size;
	UD_UI4 		Count;
	NFO_PATH_INFO	Entry[1];
} NFO_PATH_INFO_LIST, *PNFO_PATH_INFO_LIST;

typedef struct _NFO_PATH_LIST
{
	UD_UI4		Size;
	UD_UI4		Count;
	NFO_PATH	Path[1];
} NFO_PATH_LIST, *PNFO_PATH_LIST;

typedef struct _NFO_EVENT_CB_ENTRY
{
	UD_UI4		Id;
	NFO_EVENT_CB	Callback;
	UD_UI4		Context;
	UD_UI4		Reserved[8];
} NFO_EVENT_CB_ENTRY, *PNFO_EVENT_CB_ENTRY;

typedef struct _NFO_EVENT_CB_LIST
{
	UD_UI4			Size;
	UD_UI4			Count;
	NFO_EVENT_CB_ENTRY	Entry[1];
} NFO_EVENT_CB_LIST, *PNFO_EVENT_CB_LIST;

typedef struct _NFO_EVT_FOM
{
	UD_UI4	Instance;
	UD_H	Handle;
	UD_UI8	Timestamp;
	UD_UI4	Reason;
	UD_UI4	Reserved[8];
} NFO_EVT_FOM, *PNFO_EVT_FOM;

typedef struct _NFO_EVT_HBA
{
	UD_UI4	Instance;
	UD_H	Handle;
	UD_UI8	Timestamp;
	UD_UI4	Reason;
	UD_UI4	Reserved[8];
} NFO_EVT_HBA, *PNFO_EVT_HBA;

typedef struct _NFO_EVT_PATH
{
	UD_UI4	Instance;
	UD_H	Handle;
	UD_UI8	Timestamp;
	UD_UI4	Reason;
	UD_UI4	Reserved[8];
} NFO_EVT_PATH, *PNFO_EVT_PATH;

typedef struct _NFO_EVT_DEV
{
	UD_UI4	Instance;
	UD_H	Handle;
	UD_UI8	Timestamp;
	UD_UI4	Reason;
	UD_UI4	Reserved[8];
} NFO_EVT_DEV, *PNFO_EVT_DEV;

typedef struct _NFO_EVT
{
	UD_UI4		Code;
	union
	{
		NFO_EVT_FOM	Fom;
		NFO_EVT_HBA	HBA;
		NFO_EVT_PATH	Path;
		NFO_EVT_DEV	Dev;
		UD_UI4		Data[1];
	} Data;
} NFO_EVT, *PNFO_EVT;

/*
 * ***********************************************************************
 * Function prototypes
 * ***********************************************************************
 */
UD_UI4 NfoGetApiInfo              (PNFO_API_INFO pApiInfo);
UD_UI4 NfoGetFomCount             (PUD_UI4  pFomCount);
UD_UI4 NfoOpenFom                 (UD_UI4 Instance, PUD_H pFomHandle);
UD_UI4 NfoCloseFom                (UD_H FomHandle);
UD_UI4 NfoGetTransportInfo        (UD_H FomHandle, UD_UI4 BufSize, PNFO_TRANSPORT_LIST pTransport);
UD_UI4 NfoGetFomProperty          (UD_H FomHandle, PNFO_FOM_PROP pProp);
UD_UI4 NfoSetFomProperty          (UD_H FomHandle, UD_UI4 BufSize, PNFO_PROP_LIST pPropList);
UD_UI4 NfoGetHbaInfo              (UD_H FomHandle, UD_UI4 HbaInstance, PNFO_HBA_INFO pInfo);
UD_UI4 NfoGetHbaInfoAll           (UD_H FomHandle, UD_UI4 HbaInstance, UD_UI4 BufSize, PNFO_HBA_INFO_LIST pHbaInfoList);
UD_UI4 NfoGetDpgProperty          (UD_H FomHandle, UD_UI4 DpgInstance, PNFO_DPG_PROP pDpgProp);
UD_UI4 NfoGetDpgPropertyAll       (UD_H FomHandle, UD_UI4 Instance, UD_UI4 BufSize, PNFO_DPG_PROP_LIST pDpgPropList);
UD_UI4 NfoGetDpgPathInfo          (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo, PNFO_PATH_INFO pPathInfo);
UD_UI4 NfoGetDpgPathInfoAll       (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 Instance, UD_UI4 BufSize, PNFO_PATH_INFO_LIST pPathInfoList);
UD_UI4 NfoSetDpgPathInfo          (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo, PNFO_PATH_INFO pPathInfo);
UD_UI4 NfoSetDpgPathInfoAll       (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 Instance, UD_UI4 BufSize, PNFO_PATH_INFO_LIST pPathInfoList);
UD_UI4 NfoGetLBInfo               (UD_H FomHandle, UD_UI4 BufSize, PNFO_LB_POLICY_LIST pLb);
UD_UI4 NfoGetLBPolicy             (UD_H FomHandle, UD_UI4 DpgInstance, PUD_UI4 pLbPolicy);
UD_UI4 NfoSetLBPolicy             (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 LbPolicy);
UD_UI4 NfoGetDpgStatistics        (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo, PNFO_ADP_STAT pAdpStat);
UD_UI4 NfoClearDpgErrStatistics   (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo);
UD_UI4 NfoClearDpgIoStatistics    (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo);
UD_UI4 NfoClearDpgFoStatistics    (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo, PNFO_PATH_STAT pFoStat);
UD_UI4 NfoMovePath                (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo);
UD_UI4 NfoVerifyPath              (UD_H FomHandle, UD_UI4 DpgInstance, UD_UI4 PathNo);
UD_UI4 NfoGetEventList            (UD_H FomHandle, UD_UI4 BufSize, PNFO_EVENT_CB_LIST pEventCbList);
UD_UI4 NfoRegisterEventCallback   (UD_H FomHandle, UD_UI4 BufSize, PNFO_EVENT_CB_LIST pEventCbList);
UD_UI4 NfoDeregisterEventCallback (UD_H FomHandle, UD_UI4 BufSize, PNFO_EVENT_CB_LIST pEventCbList);
UD_UI4 NfoEnableFom               (UD_H FomHandle);
UD_UI4 NfoDisableFom              (UD_H FomHandle);
UD_UI4 NfoGetSupportedStorageList (UD_H FomHandle, UD_UI4 BufSize, PNFO_STORAGE_LIST pStorageList);
UD_UI4 NfoGetPathAll              (UD_H FomHandle, UD_UI4 Index, UD_UI4 BufSize, PNFO_PATH_LIST pPathList);

#if 0
/* Example tables */
/* Example transport protocol table */
NFO_TRANSPORT_LIST TransportTbl =
{
	sizeof(NFO_TRANSPORT) * NFO_TRANSPORT_NO_SUP + 1,
	NFO_TRANSPORT_NO_SUP + 1,
	{ NFO_TRANSPORT_FC, "Fibre Channel" },
	{ NFO_TRANSPORT_IS, "iScsi" },
	{ NFO_TRANSPORT_UNKNWON, "Unknown" },
};

/* Example property table */
NFO_PROP_LIST FomPropTbl =
{
	sizeof(NFO_PROP) * NFO_FOM_PROP_NO_SUP,
	NFO_FOM_PROP_NO_SUP,
	{ 3, 1, 3, 10, "Io Retry Count" },
};

/* Example path property table */
NFO_PROP_LIST FomPropTbl =
{
	sizeof(NFO_PROP) * NFO_PATH_PROP_NO_SUP,
	NFO_PATH_PROP_NO_SUP,
	{ 1, 1, 1, 32, "Order" },
	{ 1, 1, 1, 10, "Weight" },
};

/* Example policy table for Active/Active model, can have one for each DPG */
NFO_LB_POLICY_LIST LbPolicyAATbl =
{
	sizeof(NFO_LB_POLICY) * 5,
	5,
	{ NFO_LB_FAILOVER_ONLY,         "Failover only" },
	{ NFO_LB_ROUND_ROBIN,           "Round Robin" },
	{ NFO_LB_DYN_LEAST_QUEUE_DEPTH, "IO Bandpass" },
	{ NFO_LB_LEAST_BLOCKS,          "MB Bandpass" },
	{ NFO_LB_STATIC,                "Static" },
};
#endif


#if defined(linux)                 /* Linux */
#include "qlnfoln.h"
#endif


#endif /* _QLNFO_H */
