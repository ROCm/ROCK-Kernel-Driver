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
#ifndef _QLINIOCT_H_
#define _QLINIOCT_H_

#include "qlisioln.h"

/*
   Ioctl
*/

/*
  General
*/

/*
 * Command Codes definitions
 */
#define INT_CC_GET_DATA			EXT_CC_RESERVED0A_OS
#define INT_CC_SET_DATA			EXT_CC_RESERVED0B_OS
#define INT_CC_DIAG_PING		EXT_CC_RESERVED0C_OS
#define INT_CC_ISCSI_LOOPBACK		EXT_CC_RESERVED0D_OS
#define INT_CC_HBA_RESET		EXT_CC_RESERVED0E_OS
#define INT_CC_COPY_FW_FLASH		EXT_CC_RESERVED0F_OS
#define INT_CC_LOGOUT_ISCSI		EXT_CC_RESERVED0G_OS
#define INT_CC_FW_PASSTHRU		EXT_CC_RESERVED0H_OS
#define INT_CC_IOCB_PASSTHRU		EXT_CC_RESERVED0I_OS

/*
 * Sub codes for Get Data.
 * Use in combination with INT_GET_DATA as the ioctl code
 */
#define INT_SC_GET_FLASH			1

/*
 * Sub codes for Set Data.
 * Use in combination with INT_SET_DATA as the ioctl code
 */
#define INT_SC_SET_FLASH			1

#define INT_DEF_DNS_ENABLE                  0x0100

/*
 * ***********************************************************************
 * INIT_FW_ISCSI_ALL
 * ***********************************************************************
 */
typedef struct _INT_INIT_FW_ISCSI_ALL {
	UINT8   Version;					/* 1   */
	UINT8   Reserved0;					/* 1   */
	UINT16  FWOptions;					/* 2   */
	UINT16  exeThrottle;					/* 2   */
	UINT8   retryCount;					/* 1   */
	UINT8   retryDelay;					/* 1   */
	UINT16  EthernetMTU;					/* 2   */
	UINT16  addFWOptions;					/* 2   */
	UINT8   HeartBeat;					/* 1   */
	UINT8   Reserved1;					/* 1   */
	UINT16  Reserved2;					/* 2   */
	UINT16  ReqQOutPtr;					/* 2   */
	UINT16  RespQInPtr;					/* 2   */
	UINT16  ReqQLen;					/* 2   */
	UINT16  RespQLen;					/* 2   */
	UINT32  ReqQAddr[2];					/* 8   */
	UINT32  RespQAddr[2];					/* 8   */
	UINT32  IntRegBufAddr[2];				/* 8   */
	UINT16  iSCSIOptions;					/* 2   */
	UINT16  TCPOptions;					/* 2   */
	UINT16  IPOptions;					/* 2   */
	UINT16  MaxRxDataSegmentLen;				/* 2   */
	UINT16  recvMarkerInt;					/* 2   */
	UINT16  sendMarkerInt;					/* 2   */
	UINT16  Reserved3;					/* 2   */
	UINT16  firstBurstSize;					/* 2   */
	UINT16  DefaultTime2Wait;				/* 2   */
	UINT16  DefaultTime2Retain;				/* 2   */
	UINT16  maxOutstandingR2T;				/* 2   */
	UINT16  keepAliveTimeout;				/* 2   */
	UINT16  portNumber;					/* 2   */
	UINT16  maxBurstSize;					/* 2   */
	UINT32  Reserved4;					/* 4   */
	UINT8   IPAddr[16];					/* 16  */
	UINT8   SubnetMask[16];					/* 16  */
	UINT8   IPGateway[16];					/* 16  */
	UINT8   DNSsvrIP[4];					/* 4  */
	UINT8   DNSsecSvrIP[4];					/* 4  */
	UINT8   Reserved5[8];					/* 8    */
	UINT8   Alias[EXT_DEF_ISCSI_ALIAS_LEN];			/* 32  */
	UINT32  targetAddr0;					/* 4   */
	UINT32  targetAddr1;					/* 4   */
	UINT32  CHAPTableAddr0;					/* 4   */
	UINT32  CHAPTableAddr1;					/* 4   */
	UINT8   EthernetMACAddr[6];				/* 6   */
	UINT16  TargetPortalGrp;				/* 2   */
	UINT8   SendScale;					/* 1   */
	UINT8   RecvScale;					/* 1   */
	UINT8   TypeOfService;					/* 1   */
	UINT8   Time2Live;					/* 1   */
	UINT16  VLANPriority;					/* 2   */
	UINT16  Reserved6;					/* 2   */
	UINT8   SecondaryIPAddr[16];				/* 16  */
	UINT8   iSNSServerAdr[4];				/* 4    */
	UINT16  iSNSServerPort;					/* 2    */
	UINT8   Reserved7[10];					/* 10  */
	UINT8   SLPDAAddr[16];					/* 16  */
	UINT8   iSCSIName[EXT_DEF_ISCSI_NAME_LEN];		/* 256 */
} INT_INIT_FW_ISCSI_ALL, *PINT_INIT_FW_ISCSI_ALL;		/* 512 */

/*
 * ***********************************************************************
 * INT_DEVICE_ENTRY_ISCSI_ALL
 * ***********************************************************************
 */
typedef struct _INT_DEVICE_ENTRY_ISCSI_ALL {
	UINT8   Options;					/* 1 */
	UINT8   Control;					/* 1 */
	UINT16  exeThrottle;					/* 2 */
	UINT16  exeCount;					/* 2 */
	UINT8   retryCount;					/* 1 */
	UINT8   retryDelay;					/* 1 */
	UINT16  iSCSIOptions;					/* 2 */
	UINT16  TCPOptions;					/* 2 */
	UINT16  IPOptions;					/* 2 */
	UINT16  MaxRxDataSegmentLen;				/* 2 */
	UINT16  RecvMarkerInterval;				/* 2 */
	UINT16  SendMarkerInterval;				/* 2 */
	UINT16  MaxTxDataSegmentLen;				/* 2 */
	UINT16  firstBurstSize;					/* 2 */
	UINT16  DefaultTime2Wait;				/* 2 */
	UINT16  DefaultTime2Retain;				/* 2 */
	UINT16  maxOutstandingR2T;				/* 2 */
	UINT16  keepAliveTimeout;				/* 2 */
	UINT8   InitiatorSessID[EXT_DEF_ISCSI_ISID_SIZE];	/* 6 */
	UINT16  TargetSessID;					/* 2 */
	UINT16  portNumber;					/* 2 */
	UINT16  maxBurstSize;					/* 2 */
	UINT16  taskMngmntTimeout;				/* 2 */
	UINT16  Reserved0;					/* 2 */
	UINT8   IPAddress[16];					/* 16  */
	UINT8   Alias[EXT_DEF_ISCSI_ALIAS_LEN];			/* 32  */
	UINT8   targetAddr[EXT_DEF_ISCSI_TADDR_SIZE];		/* 32  */
	/* need to find new definition XXX */
	UINT8   res[64];
	UINT8   iSCSIName[EXT_DEF_ISCSI_NAME_LEN];		/* 256 */
	UINT16  ddbLink;					/* 2   */
	UINT16  chapTableIndex;					/* 2   */
	UINT16  targetPortalGrp;				/* 2   */
	UINT16  Reserved1;					/* 2   */
	UINT32  statSN;						/* 4 */
	UINT32  expStatSN;					/* 4 */
} INT_DEVICE_ENTRY_ISCSI_ALL, *PINT_DEVICE_ENTRY_ISCSI_ALL;	/* 464 */

/*
 * ****************************************************************************
 * INT_DEVDDB_ENTRY
 * ****************************************************************************
 */

typedef struct _FLASH_DEVDB_ENTRY {
	INT_DEVICE_ENTRY_ISCSI_ALL      entryData;		/* 0-1C7   */
	UINT8                           RES0[0x2C];		/* 1C8-1FB */
	UINT16                          ddbValidCookie;		/* 1FC-1FD */
	UINT16                          ddbValidSize;		/* 1FE-1FF */
} FLASH_DEVDB_ENTRY, *PFLASH_DEVDB_ENTRY;

/*
 * ****************************************************************************
 * INT_FLASH_INITFW
 * ****************************************************************************
 */

typedef struct _FLASH_INITFW {
	INT_INIT_FW_ISCSI_ALL   initFWData;
	UINT32                  validCookie;
} FLASH_INITFW, *PFLASH_INITFW;


/*
 * ***********************************************************************
 * INT_ACCESS_FLASH
 * ***********************************************************************
 */

#define INT_DEF_AREA_TYPE_FW_IMAGE1		0x01
#define INT_DEF_AREA_TYPE_FW_IMAGE2		0x02
#define INT_DEF_AREA_TYPE_DRIVER		0x03
#define INT_DEF_AREA_TYPE_DDB			0x04
#define INT_DEF_AREA_TYPE_INIT_FW		0x05
#define INT_DEF_AREA_TYPE_SYS_INFO		0x06

#define INT_DEF_FLASH_BLK_SIZE			0x4000
#define INT_DEF_FLASH_PHYS_BLK_SIZE		0x20000

#define INT_ISCSI_FW_IMAGE2_FLASH_OFFSET	0x01000000
#define INT_ISCSI_SYSINFO_FLASH_OFFSET		0x02000000
#define INT_ISCSI_DRIVER_FLASH_OFFSET		0x03000000
#define INT_ISCSI_INITFW_FLASH_OFFSET		0x04000000
#define INT_ISCSI_DDB_FLASH_OFFSET		0x05000000
#define INT_ISCSI_CHAP_FLASH_OFFSET		0x06000000
#define INT_ISCSI_FW_IMAGE1_FLASH_OFFSET	0x07000000
#define INT_ISCSI_BIOS_FLASH_OFFSET		0x0d000000
#define INT_ISCSI_OFFSET_MASK			0x00FFFFFF
#define INT_ISCSI_PAGE_MASK			0x0F000000

#define INT_ISCSI_ACCESS_FLASH			0x00000000
#define INT_ISCSI_ACCESS_RAM			0x10000000
#define INT_ISCSI_ACCESS_MASK			0xF0000000

/* WRITE_FLASH option definitions */
#define INT_WRITE_FLASH_OPT_HOLD		0 /* Write data to FLASH but
						     do not Commit */
#define INT_WRITE_FLASH_OPT_CLEAR_REMAINING	1 /* Write data to FLASH but
						     do not Commit any data
						     not written before
						     commit will be cleared
						     (set to 0xFF)	*/
#define INT_WRITE_FLASH_OPT_COMMIT_DATA		2 /* Commit (Burn) data to
						     FLASH */


typedef struct _INT_ACCESS_FLASH {
	UINT32  AreaType;					/* 4   */
	UINT32  DataLen;					/* 4   */
	UINT32  DataOffset;					/* 4   */
	UINT8   FlashData[INT_DEF_FLASH_BLK_SIZE];		/* 0x4000 */
	UINT32  Options;					/* 4   */
} INT_ACCESS_FLASH, *PINT_ACCESS_FLASH;				/* 0x4010 */

/*
 * ****************************************************************************
 * INT_FLASH_DRIVER_PARAM
 * ****************************************************************************
 */

typedef struct _INT_FLASH_DRIVER_PARAM {
	UINT16  DiscoveryTimeOut;				/* 2   */
	UINT16  PortDownTimeout;				/* 2   */
	UINT32  Reserved[32];					/* 128 */
} INT_FLASH_DRIVER_PARAM, *PINT_FLASH_DRIVER_PARAM;		/* 132 */


#define VALID_FLASH_INITFW		0x11BEAD5A

#define FLASH_ISCSI_MAX_DDBS		64
#define FLASH_DDB_VALID_COOKIE		0x9034 /* this value indicates this
						  entry in flash is valid */
#define FLASH_DDB_INVALID_COOKIE	0x0    /* this value is used to set
						  the entry to invalid    */

/*
 * ****************************************************************************
 * INT_HBA_SYS_INFO
 * ****************************************************************************
 */

typedef struct _INT_HBA_SYS_INFO {
	UINT32  cookie;						/* 4   */
	UINT32  physAddrCount;					/* 4   */
	UINT8   macAddr0[6];					/* 6   */
	UINT8   reserved0[2];					/* 2   */
	UINT8   macAddr1[6];					/* 6   */
	UINT8   reserved1[2];					/* 2   */
	UINT8   macAddr2[6];					/* 6   */
	UINT8   reserved2[2];					/* 2   */
	UINT8   macAddr3[6];					/* 6   */
	UINT8   reserved3[2];					/* 2   */
	UINT8   vendorId[128];					/* 128 */
	UINT8   productId[128];					/* 128 */
	UINT32  serialNumber;					/* 4   */
	UINT32  pciDeviceVendor;				/* 4   */
	UINT32  pciDeviceId;					/* 4   */
	UINT32  pciSubsysVendor;				/* 4   */
	UINT32  pciSubsysId;					/* 4   */
	UINT32  crumbs;						/* 4   */
	UINT32  enterpriseNumber;				/* 4   */
	UINT32  crumbs2;					/* 4   */
} INT_HBA_SYS_INFO, *PINT_HBA_SYS_INFO;				/* 328 */

/*
 * ****************************************************************************
 * INT_FW_DW_HDR
 * ****************************************************************************
 */

/* File header for FW */
typedef struct _INT_FW_DL_HDR {
	UINT32  Size;		/* download size, excluding DL_HDR & EXT_HDR*/
	UINT32  Checksum;	/* Checksum of download file, excluding DL_HDR
				   & EXT_HDR */
	UINT32  HdrChecksum;	/* Checksum of header area should be zero */
	UINT32  Flags;		/* See Flags bits defined above */
	UINT32  Cookie;		/* Target specific identifier */
	UINT32  Target;		/* Target specific identifier */
	UINT32  Reserved0;	/* Reserved */
	UINT32  Reserved1;	/* Reserved */
	UINT8   Copyright[64];	/* Copyright */
	UINT8   Version[32];	/* Version String */
} INT_FW_DL_HDR, *PINT_FW_DL_HDR;

/* File header for BIOS */
typedef struct _INT_BIOS_HDR {
	UINT8   BIOSidCode55;
	UINT8   BIOSidCodeAA;
	UINT8   reserved[52];
	UINT8   BIOSminorVer;
	UINT8   BIOSmajorVer;
} INT_BIOS_HDR, *PINT_BIOS_HDR;

typedef struct _INT_SDMBIOS_NVRAM {
	UINT16  Flags;
	UINT8   PriID;
	UINT64  PriLUN;
	UINT8   SecID;
	UINT64  SecLUN;
} INT_SDMBIOS_NVRAM, *PINT_SDMBIOS_NVRAM;

/*
 * ****************************************************************************
 * INT_HBA_RESET
 * ****************************************************************************
 */

typedef struct _INT_HBA_RESET {
	UINT32  Reserved[2];					/* 8  */
} INT_HBA_RESET, *PINT_HBA_RESET;				/* 8  */

/*
 * ****************************************************************************
 * INT_COPY_FW_FLASH
 * ****************************************************************************
 */

typedef struct _INT_COPY_FW_FLASH {
	UINT32  Options;					/* 4  */
} INT_COPY_FW_FLASH, *PINT_COPY_FW_FLASH;			/* 4  */

#define INT_COPY_FLASH_PRIMARY_TO_SECONDARY	0
#define INT_COPY_FLASH_SECONDARY_TO_PRIMARY	1

/*
 * ****************************************************************************
 * INT_LOGOUT_ISCSI
 * ****************************************************************************
 */

/* Logout Options */

#define INT_DEF_CLOSE_SESSION			0x0001
#define INT_DEF_RELOGIN_CONNECTION		0x0002
#define INT_DEF_DELETE_DDB		      	0x0004
#define INT_DEF_REINDEX_DDB		      	0x0008

typedef struct _INT_LOGOUT_ISCSI {
	UINT16    TargetID;					/* 2   */
	UINT16    ConnectionID;					/* 2   */
	UINT16    Options;					/* 2   */
	UINT32    NewTargetID;					/* 4   */
} INT_LOGOUT_ISCSI, *PINT_LOGOUT_ISCSI;				/* 10  */

/*
 * ****************************************************************************
 * INT_PING
 * ****************************************************************************
 */

typedef struct _INT_PING {
	EXT_ISCSI_IP_ADDR       IPAddr;				/* 20  */
	UINT16                  PacketCount;			/* 2   */
	UINT16                  Reserved;			/* 2   */
} INT_PING, *PINT_PING;						/* 24  */

/*
 * ****************************************************************************
 * INT_IOCB_PASSTHRU
 * ****************************************************************************
 */

#define INT_DEF_IOCB_BUF_SIZE			64
#define INT_DEF_IOCB_DATA_SIZE			1500

typedef struct _INT_IOCB_PASSTHRU {
	UINT32    SendDMAOffset;				/* 4    */
	UINT32    RspDMAOffset;					/* 4    */
	UINT8     IOCBCmdBuffer[INT_DEF_IOCB_BUF_SIZE];		/* 64   */
	UINT8     IOCBStatusBuffer[INT_DEF_IOCB_BUF_SIZE];	/* 64   */
	UINT32    SendDataLen;					/* 4    */
	UINT8     SendData[INT_DEF_IOCB_DATA_SIZE];		/* 1500 */
	UINT32    RspDataLen;					/* 4    */
	UINT8     RspData[INT_DEF_IOCB_DATA_SIZE];		/* 1500 */
	UINT32    Reserved;					/* 4    */
} INT_IOCB_PASSTHRU, *PINT_IOCB_PASSTHRU;			/* 3148 */


/*
 * ****************************************************************************
 * INT_CC_FW_PASSTHRU
 * ****************************************************************************
 */

/* FW PASSTHRU Defines */
#define INT_DEF_FW_PASSHTRU_BLK_SIZE		0x4000

#define INT_DEF_DATA_TYPE_CHAP_TABLE		0x0001
#define INT_DEF_DATA_TYPE_DDB			0x0002
#define INT_DEF_DATA_TYPE_INITFW		0x0003
#define INT_DEF_DATA_TYPE_FW_IMAGE		0x0004

#define INT_DEF_DATA_LOCATION_HBA_FLASH		0x0001
#define INT_DEF_DATA_LOCATION_HBA_RAM		0x0002

#define INT_DEF_DATA_READ			0x0001
#define INT_DEF_DATA_WRITE			0x0002

#define INT_DEF_DATA_INIT			0x0001
#define INT_DEF_DATA_COMMIT			0x0002

#endif /* _QLINIOCT_H_ */
