/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003 QLogic Corporation
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
 * File Name: exioct.h
 *
 * San/Device Management Ioctl Header
 * File is created to adhere to Solaris requirement using 8-space tabs.
 *
 * !!!!! PLEASE DO NOT REMOVE THE TABS !!!!!
 * !!!!! PLEASE NO SINGLE LINE COMMENTS: // !!!!!
 * !!!!! PLEASE NO MORE THAN 80 CHARS PER LINE !!!!!
 *
 * Revision History:
 *
 * Rev. 0	March 1, 2000
 * YPL	- Created.
 *
 * Rev. 1	March 2, 2000
 * RLU	- Updated with latest definitions.  Added more comments.
 *
 * Rev. 2	May 16, 2000		    
 * SP	- Updated definitions and changed structures (March 27, 2000)
 * SP   - Addded structures 
 *
 * Rev. 3	June 1, 2000		     
 * THL	- Made major changes to include all changes talked in our meeting.
 *
 * Rev. 4	June 5, 2000
 * RLU	- Added new definitions/structures for SDM_GET_AEN and SDM_REG_AEN 
 *	  functions.
 *	- Major definition/structure name changes as discussed in meetings.
 *	- Deleted duplicated command code and structure definitions.
 *
 * Rev. 4.1	June 14, 2000
 * WTR  - Moved Solaris specific defines to exioctso.h. This makes it
 *	  possible for application developers to include only exioct.h
 *	  in their Solaris application development.
 *
 * Rev. 4.2	June 15, 2000
 * THL  - Changed UINT16 and UINT32 back to WORD and DWORD for NT; otherwise,
 *	  NT will get a compilation error for redefining UINT16 and UINT32.
 *	  Added RISC_CODE/FLASH_RAM macros.
 *
 * Rev. 4.3	June 22, 2000
 * THL  - Changed SDM_FC_ADDR according to External Ioctls document.
 *	  Added SDM_DEF_TYPE macros.
 *
 * Rev. 4.4	June 22, 2000
 * THL  - Moved NT specific defines to exioctnt.h.
 *
 * Rev. 4.5     August 15, 2000
 * SP   - Rolled back some changes made by Todd R.
 *	  Kept new status code SDM_STATUS_NO_MEMORY
 *	  Port types fabric and tape device 
 *
 * Rev. 4.7     Sep 6, 2000
 * YPL  - Replace SDM_ with EXT_, _ISP with _CHIP.
 *	  Add vendor specific statuses, device update, config defines.
 *
 * Rev. 5.0     Sep 13, 2000
 * YPL  - Update version to 5, remove max defines, make port type bit.
 *	  Change HBA_PORT_PROPERTY to have bus/target/lun defined as UINT16
 *
 * Rev. 5.1     Sep 22, 2000
 * THL  - Add destination address for specify scsi address or FC address.
 *	  Remove "not support" comment and add more macros.
 *
 * Rev. 5.2     Sep 27, 2000
 * THL  - Add new macros and structure for add and swap target device.
 *	  Create new data structure for get port database.
 * TLE  - Merge changes needed for FailOver
 *
 * Rev. 5.3     Sep 29, 2000
 * THL  - Add access mode for NVRAM.
 * 
 * Rev. 5.4     Oct 03, 2000
 * THL  - Add EXT_SC_GET_FC_STATISTICS.
 *
 * Rev. 5.5	Oct 18, 2000
 * THL  - Remove duplicated EXT_DEF_ADDR_MODE_32 and EXT_DEF_ADDR_MODE_16.
 *	  Reformat new data structures and defines.
 *
 * Rev. 5.6	Oct 19, 2000
 * RLU	- Changed file name from ExIoct.h to exioct.h.
 *	- Added definition of EXT_RNID_DATA for API implementation.
 *	- Reformat some lines to conform to the format agreed
 *	  upon in IOCTL meeting (and mentioned at beginning of
 *	  this file).
 *
 * Rev. 5.7 Oct 25, 2000
 * BN   - Added LUN bitmask structure and macros
 *
 * Rev. 5.8 Oct 25, 2000
 * BN   - Added EXT_CC_DRIVER_PROP define
 * 
 * Rev. 5.9 Oct 26, 2000
 * BN   - Sync with UnixApi project
 * 
 * Rev. 5.10 Oct 30, 2000
 * BN   - Remove not needed #define for EXT_CC_DRIVER_PROP
 *	- Add EXT_ to IS_LUN_BIT_SET, SET_LUN_BIT, CLR_LUN_BIT
 * 
 * Rev. 5.11 Nov 1, 2000
 * BN   - Increased [1] of EXT_DEVICEDATA to [EXT_MAX_TARGET]
 * TLE  - Decreased [EXT_MAX_TARGET] of EXT_DEVICEDATA to [1]
 * 
 * Rev. 5.12	Nov 7, 2000
 * RLU	- Deleted EXT_DEF_MAX_LUNS define and changed all references
 *	  to it to use EXT_MAX_LUN.
 *	- Changed the revision numbers for the last 2 revisions down
 *	  to use 5.x.
 * 
 * Rev. 5.13	Nov 14, 2000
 * WTR	- Fixed pointer referencing problem in the LUN_BIT_MASK macros.
 *	  Updated comment at bit mask definition.
 *
 * Rev. 5.14	Dec 6, 2000
 * THL	- Added Local and LoopID to discovered port/target property.
 * 
 * Rev. 5.15	Dec 24, 2000
 * YPL	- Enhance port connection modes and driver attrib
 * 
 * Rev. 5.16	Dec 27, 2000
 * TLE  - Add BufferHandle member to _EXT_ASYNC_EVENT data structure for
 *	  SCTP support
 * 
 * Rev. 5.17	Jan 10, 2001
 * YPL  - Add edtov, ratov & fabric name in port property
 * 
 * Rev. 5.18	Feb 28, 2001
 * YPL  - Remove SCTP fields and add fabric parameter flags in port property
 * 
 * Rev. 5.19	Mar 08, 2001
 * YPL  - Remove SCTP fields from hba port prop
 * 
 * Rev. 5.20	June 11, 2001
 * YPL  - Change to reserved fields and add fabric name field in port property
 * 
 * Rev. 5.21	June 29, 2001
 * YPL  - Merge in changes decided long time ago (use _DEF_ for defines) &
 *	  reserved some EXT_CC for legacy ioctls, plus add RNID dataformat
 *	  values definition
 * 
 * Rev. 5.21    Sep 18, 2001
 * SP   - Added New return status codes
 *
 * Rev.	5.22	Oct 23, 2001
 * SP	- Change reserve fields to add fields to EXT_HBA_PORT
 *	  Added port speeds and FC4Types fields  and related definitions
 *
 * Rev.	5.23	Dec 04, 2001
 * RL	- Added port speed value definition.
 *
 * Rev. 5.24	Jan 20, 2002
 * JJ	- Added PCI device function bits field in EXT_CHIP structure.
 *
 * Rev. 5.25	Feb 04, 2002
 * JJ	- Added 16 bytes CDB support.  Also added SenseLength field
 *	  in SCSI_PASSTHRU structure.
 *
 * Rev. 5.26	Feb 12, 2002
 * AV	- Changed type size used in SCSI_PASSTHRU structure definitions
 *	  to re-enable gcc's automatic structure padding for backward
 *	  compatibility.
 *
 * Rev. 5.27	Mar 01, 2002
 * RL	- Added new SC value for SCSI3 command passthru.
 *
 * Rev. 5.28	Dec 09, 2002
 * Sync up with NT version of exioct.h:
 * TLE	- Modify EXT_RNID_REQ data structure for IBM SendRNID workaround
 * YPL	- Add firmware state (online diagnostics)
 * YPL	- Add ELS PS
 * YPL	- Add els event, # of els buffers & size
 *
 * Rev. 5.29     April 21, 2003
 * RA   - Defined the structure EXT_BEACON_CONTROL and subcommand code:
 *        EXT_SC_GET_BEACON_STATE,EXT_SC_SET_BEACON_STATE for the
 *        led blinking feature.
 *
 * Rev. 5.30     July 21, 2003
 * RL	- Added new statistics fields in HBA_PORT_STAT struct.
 */

#ifndef	_EXIOCT_H
#define	_EXIOCT_H

/*
 * NOTE: the following version defines must be updated each time the
 *	 changes made may affect the backward compatibility of the
 *	 input/output relations of the SDM IOCTL functions.
 */
#define	EXT_VERSION					5


/*
 * OS independent General definitions
 */
#define	EXT_DEF_SIGNATURE_SIZE				8
#define	EXT_DEF_WWN_NAME_SIZE				8
#define	EXT_DEF_WWP_NAME_SIZE				8
#define	EXT_DEF_SERIAL_NUM_SIZE				4
#define	EXT_DEF_PORTID_SIZE				4
#define	EXT_DEF_PORTID_SIZE_ACTUAL			3
#define	EXT_DEF_MAX_STR_SIZE				128
#define	EXT_DEF_SCSI_PASSTHRU_CDB_LENGTH		16

#define	EXT_DEF_ADDR_MODE_32				1
#define	EXT_DEF_ADDR_MODE_64				2

/*
 * ***********************************************************************
 * X OS type definitions
 * ***********************************************************************
 */
#ifdef _MSC_VER						/* NT */

#pragma pack(1)
#include "ExIoctNT.h"

#elif defined(linux)					/* Linux */

#include "exioctln.h"

#elif defined(sun) || defined(__sun)			/* Solaris */

#include "exioctso.h"

#endif

/*
 * ***********************************************************************
 * OS dependent General configuration defines
 * ***********************************************************************
 */
#define	EXT_DEF_MAX_HBA                 EXT_DEF_MAX_HBA_OS
#define	EXT_DEF_MAX_BUS                 EXT_DEF_MAX_BUS_OS
#define	EXT_DEF_MAX_TARGET              EXT_DEF_MAX_TARGET_OS
#define	EXT_DEF_MAX_LUN                 EXT_DEF_MAX_LUN_OS

/*
 * ***********************************************************************
 * Common header struct definitions for San/Device Mgmt
 * ***********************************************************************
 */
typedef struct {
	UINT64    Signature;			/* 8 chars string */
	UINT16    AddrMode;			/* 2 */
	UINT16    Version;			/* 2 */
	UINT16    SubCode;			/* 2 */
	UINT16    Instance;			/* 2 */
	UINT32    Status;			/* 4 */
	UINT32    DetailStatus;			/* 4 */
	UINT32    Reserved1;			/* 4 */
	UINT32    RequestLen;			/* 4 */
	UINT32    ResponseLen;			/* 4 */
	UINT64    RequestAdr;			/* 8 */
	UINT64    ResponseAdr;			/* 8 */
	UINT16    HbaSelect;			/* 2 */
	UINT16    VendorSpecificStatus[11];	/* 22 */
	UINT64    VendorSpecificData;		/* 8 chars string */
} EXT_IOCTL, *PEXT_IOCTL;			/* 84 / 0x54 */

/*
 * Addressing mode used by the user application
 */
#define	EXT_ADDR_MODE			EXT_ADDR_MODE_OS

/*
 * Status.  These macros are being used for setting Status field in
 * EXT_IOCTL structure.
 */
#define	EXT_STATUS_OK				0
#define	EXT_STATUS_ERR				1
#define	EXT_STATUS_BUSY				2
#define	EXT_STATUS_PENDING			3
#define	EXT_STATUS_SUSPENDED			4
#define	EXT_STATUS_RETRY_PENDING		5
#define	EXT_STATUS_INVALID_PARAM		6
#define	EXT_STATUS_DATA_OVERRUN			7
#define	EXT_STATUS_DATA_UNDERRUN		8
#define	EXT_STATUS_DEV_NOT_FOUND		9
#define	EXT_STATUS_COPY_ERR			10
#define	EXT_STATUS_MAILBOX			11
#define	EXT_STATUS_UNSUPPORTED_SUBCODE		12
#define	EXT_STATUS_UNSUPPORTED_VERSION		13
#define	EXT_STATUS_MS_NO_RESPONSE		14
#define	EXT_STATUS_SCSI_STATUS			15
#define	EXT_STATUS_BUFFER_TOO_SMALL		16
#define	EXT_STATUS_NO_MEMORY			17
#define	EXT_STATUS_UNKNOWN			18
#define	EXT_STATUS_UNKNOWN_DSTATUS		19
#define	EXT_STATUS_INVALID_REQUEST		20

#define EXT_STATUS_DEVICE_NOT_READY		21
#define EXT_STATUS_DEVICE_OFFLINE		22
#define EXT_STATUS_HBA_NOT_READY		23
#define EXT_STATUS_HBA_QUEUE_FULL		24

/*
 * Detail Status contains the SCSI bus status codes.
 */

#define	EXT_DSTATUS_GOOD			0x00
#define	EXT_DSTATUS_CHECK_CONDITION		0x02
#define	EXT_DSTATUS_CONDITION_MET		0x04
#define	EXT_DSTATUS_BUSY			0x08
#define	EXT_DSTATUS_INTERMEDIATE		0x10
#define	EXT_DSTATUS_INTERMEDIATE_COND_MET	0x14
#define	EXT_DSTATUS_RESERVATION_CONFLICT	0x18
#define	EXT_DSTATUS_COMMAND_TERMINATED		0x22
#define	EXT_DSTATUS_QUEUE_FULL			0x28

/*
 * Detail Status contains the needed Response buffer space(bytes)
 * when Status = EXT_STATUS_BUFFER_TOO_SMALL
 */


/*
 * Detail Status contains one of the following codes
 * when Status = EXT_STATUS_INVALID_PARAM or
 *             = EXT_STATUS_DEV_NOT_FOUND
 */
#define EXT_DSTATUS_NOADNL_INFO			0x00
#define EXT_DSTATUS_HBA_INST			0x01
#define EXT_DSTATUS_TARGET			0x02
#define EXT_DSTATUS_LUN				0x03
#define EXT_DSTATUS_REQUEST_LEN			0x04
#define EXT_DSTATUS_PATH_INDEX			0x05

/*
 * Currently supported DeviceControl / ioctl command codes
 */
#define	EXT_CC_QUERY			EXT_CC_QUERY_OS
#define	EXT_CC_SEND_FCCT_PASSTHRU	EXT_CC_SEND_FCCT_PASSTHRU_OS
#define	EXT_CC_REG_AEN			EXT_CC_REG_AEN_OS
#define	EXT_CC_GET_AEN			EXT_CC_GET_AEN_OS
#define	EXT_CC_SEND_ELS_RNID		EXT_CC_SEND_ELS_RNID_OS
#define	EXT_CC_SEND_SCSI_PASSTHRU	EXT_CC_SCSI_PASSTHRU_OS
#define	EXT_CC_SEND_ELS_PASSTHRU	EXT_CC_SEND_ELS_PASSTHRU_OS

/*
 * HBA port operations
 */
#define	EXT_CC_GET_DATA			EXT_CC_GET_DATA_OS
#define	EXT_CC_SET_DATA			EXT_CC_SET_DATA_OS


/* Reserved command codes. */
#define	EXT_CC_RESERVED0A		EXT_CC_RESERVED0A_OS    
#define	EXT_CC_RESERVED0B		EXT_CC_RESERVED0B_OS    
#define	EXT_CC_RESERVED0C		EXT_CC_RESERVED0C_OS    
#define	EXT_CC_RESERVED0D		EXT_CC_RESERVED0D_OS    
#define	EXT_CC_RESERVED0E		EXT_CC_RESERVED0E_OS    
#define	EXT_CC_RESERVED0F		EXT_CC_RESERVED0F_OS    
#define	EXT_CC_RESERVED0G		EXT_CC_RESERVED0G_OS    
#define	EXT_CC_RESERVED0H		EXT_CC_RESERVED0H_OS    
#define	EXT_CC_RESERVED0I		EXT_CC_RESERVED0I_OS
#define	EXT_CC_RESERVED0J		EXT_CC_RESERVED0J_OS
#define	EXT_CC_RESERVED0Z		EXT_CC_RESERVED0Z_OS


/*
 * ***********************************************************************
 * EXT_IOCTL SubCode definition.
 * These macros are being used for setting SubCode field in EXT_IOCTL
 * structure.
 * ***********************************************************************
 */

/*
 * Query.
 * Uses with EXT_QUERY as the ioctl code.
 */
#define	EXT_SC_QUERY_HBA_NODE		1
#define	EXT_SC_QUERY_HBA_PORT		2
#define	EXT_SC_QUERY_DISC_PORT		3
#define	EXT_SC_QUERY_DISC_TGT		4
#define	EXT_SC_QUERY_DISC_LUN		5	/* Currently Not Supported */
#define	EXT_SC_QUERY_DRIVER		6
#define	EXT_SC_QUERY_FW			7
#define	EXT_SC_QUERY_CHIP		8

/*
 * Sub codes for Get Data.
 * Use in combination with EXT_GET_DATA as the ioctl code
 */
/* 1 - 99 Common */
#define	EXT_SC_GET_SCSI_ADDR		1	/* Currently Not Supported */
#define	EXT_SC_GET_ERR_DETECTIONS	2	/* Currently Not Supported */
#define	EXT_SC_GET_STATISTICS		3
#define	EXT_SC_GET_BUS_MODE		4	/* Currently Not Supported */
#define	EXT_SC_GET_DR_DUMP_BUF		5	/* Currently Not Supported */
#define	EXT_SC_GET_RISC_CODE		6	/* Currently Not Supported */
#define	EXT_SC_GET_FLASH_RAM		7	/* for backward compatible */
#define	EXT_SC_GET_BEACON_STATE		8	 

/* 100 - 199 FC_INTF_TYPE */
#define	EXT_SC_GET_LINK_STATUS		101	/* Currently Not Supported */
#define	EXT_SC_GET_LOOP_ID		102	/* Currently Not Supported */
#define	EXT_SC_GET_LUN_BITMASK		103
#define	EXT_SC_GET_PORT_DATABASE	104	/* Currently Not Supported */
#define	EXT_SC_GET_PORT_DATABASE_MEM	105	/* Currently Not Supported */
#define	EXT_SC_GET_PORT_SUMMARY		106
#define	EXT_SC_GET_POSITION_MAP		107
#define	EXT_SC_GET_RETRY_CNT		108	/* Currently Not Supported */
#define	EXT_SC_GET_RNID			109	
#define	EXT_SC_GET_RTIN			110	/* Currently Not Supported */
#define	EXT_SC_GET_FC_LUN_BITMASK	111
#define	EXT_SC_GET_FC_STATISTICS	112	/* for backward compatible */

/* 200 - 299 SCSI_INTF_TYPE */
#define	EXT_SC_GET_SEL_TIMEOUT		201	/* Currently Not Supported */


/* 
 * Sub codes for Set Data.
 * Use in combination with EXT_SET_DATA as the ioctl code
 */
/* 1 - 99 Common */
#define	EXT_SC_RST_STATISTICS		3
#define	EXT_SC_RESERVED_BC7		7
#define	EXT_SC_SET_BEACON_STATE		8

/* 100 - 199 FC_INTF_TYPE */
#define	EXT_SC_SET_LUN_BITMASK		103
#define	EXT_SC_SET_RNID			109	
#define	EXT_SC_SET_FC_LUN_BITMASK	111
#define	EXT_SC_RESERVED_BC112	112
#define	EXT_SC_RESERVED_BC113	113

/* 200 - 299 SCSI_INTF_TYPE */

/* SCSI passthrough */
#define	EXT_SC_SEND_SCSI_PASSTHRU	0
#define	EXT_SC_SEND_FC_SCSI_PASSTHRU	1
#define	EXT_SC_SCSI3_PASSTHRU		2

/* Read */

/* Write */

/* Reset */

/* Request struct */


/*
 * Response struct
 */
typedef struct _EXT_HBA_NODE {
	UINT8     WWNN         [EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     Manufacturer [EXT_DEF_MAX_STR_SIZE];	/* 128; "QLOGIC" */
	UINT8     Model        [EXT_DEF_MAX_STR_SIZE];	/* 128; "QLA2200" */
	UINT8     SerialNum    [EXT_DEF_SERIAL_NUM_SIZE];/* 4;  123  */
	UINT8     DriverVersion[EXT_DEF_MAX_STR_SIZE];	/* 128; "7.4.3" */
	UINT8     FWVersion    [EXT_DEF_MAX_STR_SIZE];	/* 128; "2.1.6" */

	/* The following field is currently not supported */
	UINT8     OptRomVersion[EXT_DEF_MAX_STR_SIZE];	/* 128; "1.44" */

	UINT16    PortCount;				/* 2; 1 */
	UINT16    InterfaceType;			/* 2; FC/SCSI */

	/* The following two fields are not yet supported */
	UINT32    DriverAttr;				/* 4 */
	UINT32    FWAttr;				/* 4 */

	UINT32    Reserved[8];				/* 32 */
} EXT_HBA_NODE, *PEXT_HBA_NODE;				/* 696 */

/* HBA node query interface type */
#define	EXT_DEF_FC_INTF_TYPE			1
#define	EXT_DEF_SCSI_INTF_TYPE			2

typedef struct _EXT_HBA_PORT {
	UINT8     WWPN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     Id  [EXT_DEF_PORTID_SIZE];	/* 4; 3 bytes valid Port Id. */
	UINT16    Type;				/* 2; Port Type */
	UINT16    State;			/* 2; Port State */
	UINT16    Mode;				/* 2 */
	UINT16    DiscPortCount;		/* 2 */
	UINT16    DiscPortNameType;		/* 2; USE_NODE_NAME or */
						/* USE_PORT_NAME */
	UINT16    DiscTargetCount;		/* 2 */
	UINT16    Bus;				/* 2 */
	UINT16    Target;			/* 2 */
	UINT16    Lun;				/* 2 */
						/* 2 */
	UINT8     PortSupportedFC4Types;
	UINT8     PortActiveFC4Types;
	UINT8     FabricName[EXT_DEF_WWN_NAME_SIZE];	/* 8 */

						/* 2*/
	UINT8     PortSupportedSpeed;
	UINT8     PortSpeed;
	UINT16    Unused;			/* 2 */
	UINT32    Reserved[3];			/* 12 */
} EXT_HBA_PORT, *PEXT_HBA_PORT;			/* 56 */

/* port type */
#define	EXT_DEF_INITIATOR_DEV		1
#define	EXT_DEF_TARGET_DEV		2
#define	EXT_DEF_TAPE_DEV		4
#define	EXT_DEF_FABRIC_DEV		8


/* HBA port state */
#define	EXT_DEF_HBA_OK			0
#define	EXT_DEF_HBA_SUSPENDED		1
#define	EXT_DEF_HBA_LOOP_DOWN		2

/* Connection mode */
#define	EXT_DEF_UNKNOWN_MODE		0
#define	EXT_DEF_P2P_MODE		1
#define	EXT_DEF_LOOP_MODE		2
#define	EXT_DEF_FL_MODE			3
#define	EXT_DEF_N_MODE			4

/* Valid name type for Disc. port/target */
#define	EXT_DEF_USE_NODE_NAME		1
#define	EXT_DEF_USE_PORT_NAME		2

/* FC4 type values */
#define EXT_DEF_FC4_TYPE_SCSI		0x1
#define EXT_DEF_FC4_TYPE_IP		0x2
#define EXT_DEF_FC4_TYPE_SCTP		0x4
#define EXT_DEF_FC4_TYPE_VI		0x8

/* Port Speed values */
#define EXT_DEF_PORTSPEED_1GBIT		1
#define EXT_DEF_PORTSPEED_2GBIT		2
#define EXT_DEF_PORTSPEED_10GBIT	4

typedef struct _EXT_DISC_PORT {
	UINT8     WWNN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     WWPN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     Id  [EXT_DEF_PORTID_SIZE];
					/* 4; last 3 bytes used. big endian */

	/* The following fields currently are not supported */
	UINT16    Type;				/* 2; Port Type */
	UINT16    Status;			/* 2; Port Status */
	UINT16    Bus;				/* 2; n/a for Solaris */

	UINT16    TargetId;			/* 2 */
	UINT8     Local;			/* 1; Local or Remote */
	UINT8     ReservedByte[1];		/* 1 */
	
	UINT16    LoopID;			/* 2; Loop ID */
	
	UINT32    Reserved[7];			/* 28 */
} EXT_DISC_PORT, *PEXT_DISC_PORT;		/* 60 */

typedef struct _EXT_DISC_TARGET {
	UINT8     WWNN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     WWPN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     Id  [EXT_DEF_PORTID_SIZE];
					/* 4; last 3 bytes used. big endian */

	/* The following fields currently are not supported */
	UINT16    Type;				/* 2; Target Type */
	UINT16    Status;			/* 2; Target Status*/
	UINT16    Bus;				/* 2; n/a for Solaris */

	UINT16    TargetId;			/* 2 */

	/* The following field is currently not supported */
	UINT16    LunCount;			/* 2; n/a for nt */

	UINT8     Local;			/* 1; Local or Remote */
	UINT8     ReservedByte[1];		/* 1 */
	
	UINT16    LoopID;			/* 2; Loop ID */
	
	UINT16    Reserved[13];			/* 26 */
} EXT_DISC_TARGET, *PEXT_DISC_TARGET;		/* 60 */

/* The following command is not supported */
typedef struct _EXT_DISC_LUN {			/* n/a for nt */
	UINT16    Id;				/* 2 */
	UINT16    State;			/* 2 */
	UINT16    IoCount;			/* 2 */
	UINT16    Reserved[15];			/* 30 */
} EXT_DISC_LUN, *PEXT_DISC_LUN;			/* 36 */


/* SCSI address */
typedef struct _EXT_SCSI_ADDR {
	UINT16    Bus;				/* 2 */
	UINT16    Target;			/* 2 */
	UINT16    Lun;				/* 2 */
	UINT16    Padding[5];			/* 10 */
} EXT_SCSI_ADDR, *PEXT_SCSI_ADDR;		/* 16 */


/* Fibre Channel address */
typedef struct _EXT_FC_ADDR {
	union {
		UINT8    WWNN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
		UINT8    WWPN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
		UINT8    Id[EXT_DEF_PORTID_SIZE];	/* 4 */
	} FcAddr;
	UINT16    Type;					/* 2 */
	UINT16    Padding[2];				/* 2 */
} EXT_FC_ADDR, *PEXT_FC_ADDR;				/* 24 */

#define	EXT_DEF_TYPE_WWNN                   1
#define	EXT_DEF_TYPE_WWPN                   2
#define	EXT_DEF_TYPE_PORTID                 3
#define	EXT_DEF_TYPE_FABRIC                 4


/* Destination address */
typedef struct _EXT_DEST_ADDR {
	union {
		UINT8    WWNN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
		UINT8    WWPN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
		UINT8    Id[EXT_DEF_PORTID_SIZE];	/* 4 */
		struct {
			UINT16    Bus;			/* 2 */
			UINT16    Target;		/* 2 */
		} ScsiAddr;
	} DestAddr;
	UINT16    DestType;				/* 2 */
	UINT16    Lun;					/* 2 */
	UINT16    Padding[2];				/* 4 */
} EXT_DEST_ADDR, *PEXT_DEST_ADDR;			/* 16 */


#define	EXT_DEF_DESTTYPE_WWNN			1
#define	EXT_DEF_DESTTYPE_WWPN			2
#define	EXT_DEF_DESTTYPE_PORTID			3
#define	EXT_DEF_DESTTYPE_FABRIC			4
#define	EXT_DEF_DESTTYPE_SCSI			5

/* Statistic */
#if defined(linux)					/* Linux */
typedef struct _EXT_HBA_PORT_STAT {
	UINT32    ControllerErrorCount;		/* 4 */
	UINT32    DeviceErrorCount;		/* 4 */
	UINT32    TotalIoCount;			/* 4 */
	UINT32    TotalMBytes;			/* 4; MB of data processed */
	UINT32    TotalLipResets;		/* 4; Total no. of LIP Reset */
	UINT32    Reserved2;			/* 4 */
	UINT32    TotalLinkFailures;		/* 4 */
	UINT32    TotalLossOfSync;		/* 4 */
	UINT32    TotalLossOfSignals;		/* 4 */
	UINT32    PrimitiveSeqProtocolErrorCount;/* 4 */
	UINT32    InvalidTransmissionWordCount;	/* 4 */
	UINT32    InvalidCRCCount;		/* 4 */
	uint64_t  InputRequestCount;		/* 8 */
	uint64_t  OutputRequestCount;		/* 8 */
	uint64_t  ControlRequestCount;		/* 8 */
	uint64_t  InputMBytes;			/* 8 */
	uint64_t  OutputMBytes;			/* 8 */
	UINT32    Reserved[6];			/* 24 */
} EXT_HBA_PORT_STAT, *PEXT_HBA_PORT_STAT;	/* 112 */
#else
typedef struct _EXT_HBA_PORT_STAT {
	UINT32    ControllerErrorCount;		/* 4 */
	UINT32    DeviceErrorCount;		/* 4 */
	UINT32    TotalIoCount;			/* 4 */
	UINT32    TotalMBytes;			/* 4; MB of data processed */
	UINT32    TotalLipResets;		/* 4; Total no. of LIP Reset */
	UINT32    Reserved2;			/* 4 */
	UINT32    TotalLinkFailures;		/* 4 */
	UINT32    TotalLossOfSync;		/* 4 */
	UINT32    TotalLossOfSignals;		/* 4 */
	UINT32    PrimitiveSeqProtocolErrorCount;/* 4 */
	UINT32    InvalidTransmissionWordCount;	/* 4 */
	UINT32    InvalidCRCCount;		/* 4 */
	UINT64    InputRequestCount;		/* 8 */
	UINT64    OutputRequestCount;		/* 8 */
	UINT64    ControlRequestCount;		/* 8 */
	UINT64    InputMBytes;			/* 8 */
	UINT64    OutputMBytes;			/* 8 */
	UINT32    Reserved[6];			/* 24 */
} EXT_HBA_PORT_STAT, *PEXT_HBA_PORT_STAT;	/* 112 */
#endif


/* Driver property */
typedef struct _EXT_DRIVER {
	UINT8     Version[EXT_DEF_MAX_STR_SIZE];/* 128 */
	UINT16    NumOfBus;			/* 2; Port Type */
	UINT16    TargetsPerBus;		/* 2; Port Status */
	UINT16    LunsPerTarget;		/* 2 */
	UINT32    MaxTransferLen;		/* 4 */
	UINT32    MaxDataSegments;		/* 4 */
	UINT16    DmaBitAddresses;		/* 2 */
	UINT16    IoMapType;			/* 2 */
	UINT32    Attrib;			/* 4 */
	UINT32    InternalFlags[4];		/* 16 */
	UINT32    Reserved[8];			/* 32 */
} EXT_DRIVER, *PEXT_DRIVER;			/* 198 */


/* Firmware property */
typedef struct _EXT_FW {
	UINT8     Version[EXT_DEF_MAX_STR_SIZE];/* 128 */
	UINT32    Attrib;			/* 4 */
	UINT16    Reserved[33];			/* 66 */
} EXT_FW, *PEXT_FW;				/* 198 */


/* ISP/Chip property */
typedef struct _EXT_CHIP {
	UINT16    VendorId;			/* 2 */
	UINT16    DeviceId;			/* 2 */
	UINT16    SubVendorId;			/* 2 */
	UINT16    SubSystemId;			/* 2 */
	UINT16    PciBusNumber;			/* 2 */
	UINT16    PciSlotNumber;		/* 2 */
	UINT32    IoAddr;			/* 4 */
	UINT32    IoAddrLen;			/* 4 */
	UINT32    MemAddr;			/* 4 */
	UINT32    MemAddrLen;			/* 4 */
	UINT16    ChipType;			/* 2 */
	UINT16    InterruptLevel;		/* 2 */
	UINT16    OutMbx[8];			/* 16 */
	UINT16    PciDevFunc;	                /* 2 */
	UINT16    Reserved[15];			/* 30 */
} EXT_CHIP, *PEXT_CHIP;				/* 80 */


/* Request Buffer for RNID */
typedef struct _EXT_RNID_REQ {
	EXT_FC_ADDR Addr;			/* 14 */
	UINT8     DataFormat;			/* 1 */
	UINT8     Pad;				/* 1 */
	UINT8     OptWWN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     OptPortId[EXT_DEF_PORTID_SIZE];	/* 4 */
	UINT32    Reserved[12];			/* 48 */
	UINT8     Pad1[3];			/* 3 */
} EXT_RNID_REQ, *PEXT_RNID_REQ;			/* 79 */

#define EXT_DEF_RNID_DFORMAT_NONE		0
#define EXT_DEF_RNID_DFORMAT_TOPO_DISC		0xDF

/* Request Buffer for Set RNID */
typedef struct _EXT_SET_RNID_REQ {
	UINT8     IPVersion[2];
	UINT8     UDPPortNumber[2];
	UINT8     IPAddress[16];
	UINT32    Reserved[16];
} EXT_SET_RNID_REQ, *PEXT_SET_RNID_REQ;

/* RNID definition and data struct */
#define	SEND_RNID_RSP_SIZE  72

typedef struct _RNID_DATA
{
	UINT8     WWN[16];			/* 16 */
	UINT32    UnitType;			/* 4 */
	UINT8     PortId[4];			/* 4 */
	UINT32    NumOfAttachedNodes;		/* 4 */
	UINT8     IPVersion[2];			/* 2 */
	UINT8     UDPPortNumber[2];		/* 2 */
	UINT8     IPAddress[16];		/* 16 */
	UINT16    Reserved;			/* 2 */
	UINT16    TopoDiscFlags;		/* 2 */
} EXT_RNID_DATA, *PEXT_RNID_DATA;		/* 52 */


/* SCSI pass-through */
typedef struct _EXT_SCSI_PASSTHRU {
	EXT_SCSI_ADDR   TargetAddr;
	UINT8           Direction;
	UINT8           CdbLength;
	UINT8           Cdb[EXT_DEF_SCSI_PASSTHRU_CDB_LENGTH];
        UINT32          Reserved[14];
        UINT16          Reserved2;
        UINT16          SenseLength;
	UINT8           SenseData[256];
} EXT_SCSI_PASSTHRU, *PEXT_SCSI_PASSTHRU;

/* FC SCSI pass-through */
typedef struct _EXT_FC_SCSI_PASSTHRU {
	EXT_DEST_ADDR   FCScsiAddr;
	UINT8           Direction;
	UINT8           CdbLength;
	UINT8           Cdb[EXT_DEF_SCSI_PASSTHRU_CDB_LENGTH];
        UINT32          Reserved[14];
        UINT16          Reserved2;
        UINT16          SenseLength;
	UINT8           SenseData[256];
} EXT_FC_SCSI_PASSTHRU, *PEXT_FC_SCSI_PASSTHRU;

/* SCSI pass-through direction */
#define	EXT_DEF_SCSI_PASSTHRU_DATA_IN		1
#define	EXT_DEF_SCSI_PASSTHRU_DATA_OUT		2


/* EXT_REG_AEN Request struct */
typedef struct _EXT_REG_AEN {
	UINT32    Enable;	/* 4; non-0 to enable, 0 to disable. */
	UINT32    Reserved;	/* 4 */
} EXT_REG_AEN, *PEXT_REG_AEN;	/* 8 */

/* EXT_GET_AEN Response struct */
typedef struct _EXT_ASYNC_EVENT {
	UINT32	AsyncEventCode;		/* 4 */
	union {
		struct {
			UINT8   RSCNInfo[EXT_DEF_PORTID_SIZE_ACTUAL];/* 3, BE */
			UINT8   AddrFormat;			/* 1 */
			UINT32  Rsvd_1[2];			/* 8 */
		} RSCN;

		UINT32  Reserved[3];	/* 12 */
	} Payload;
} EXT_ASYNC_EVENT, *PEXT_ASYNC_EVENT;	/* 16 */


/* Asynchronous Event Codes */
#define	EXT_DEF_LIP_OCCURRED		0x8010
#define	EXT_DEF_LINK_UP			0x8011
#define	EXT_DEF_LINK_DOWN		0x8012
#define	EXT_DEF_LIP_RESET		0x8013
#define	EXT_DEF_RSCN			0x8015
#define	EXT_DEF_DEVICE_UPDATE		0x8014
#define	EXT_DEF_ELS          		0x8200

/* Required # of entries in the queue buffer allocated. */
#define	EXT_DEF_MAX_AEN_QUEUE		EXT_DEF_MAX_AEN_QUEUE_OS
#define	EXT_DEF_MAX_ELS_BUFS		EXT_DEF_MAX_ELS_BUFS_OS
#define	EXT_DEF_SIZE_ELS_BUF		EXT_DEF_SIZE_ELS_BUF_OS

/* Device type to get for EXT_SC_GET_PORT_SUMMARY */
#define	EXT_DEF_GET_KNOWN_DEVICE	0x1
#define	EXT_DEF_GET_VISIBLE_DEVICE	0x2
#define	EXT_DEF_GET_HIDDEN_DEVICE	0x4
#define	EXT_DEF_GET_FABRIC_DEVICE	0x8
#define	EXT_DEF_GET_LOOP_DEVICE		0x10

/* Each entry in device database */
typedef struct _EXT_DEVICEDATAENTRY
{
	UINT8		NodeWWN[8];	/* Node World Wide Name for device */
	UINT8		PortWWN[8];	/* Port World Wide Name for device */
	UINT8		PortID[3];	/* Current PortId for device */
	UINT8		ControlFlags;	/* Control flag */
	EXT_SCSI_ADDR	TargetAddress;	/* scsi address */
	UINT32		DeviceFlags;	/* Flags for device */
	UINT16		LoopID;		/* Loop ID */
	UINT16		BaseLunNumber;  
	UINT32		Reserved[32];
} EXT_DEVICEDATAENTRY, *PEXT_DEVICEDATAENTRY;

/* Device database information */
typedef struct _EXT_DEVICEDATA
{
	UINT32	TotalDevices;          /* Set to total number of device. */
	UINT32	ReturnListEntryCount;  /* Set to number of device entries */
		                       /* returned in list. */

	EXT_DEVICEDATAENTRY  EntryList[1]; /* Variable length */
} EXT_DEVICEDATA, *PEXT_DEVICEDATA;


/* Swap Target Device Data structure */
typedef struct _EXT_SWAPTARGETDEVICE
{
	EXT_DEVICEDATAENTRY CurrentExistDevice;
	EXT_DEVICEDATAENTRY NewDevice;
} EXT_SWAPTARGETDEVICE, *PEXT_SWAPTARGETDEVICE;

/* LUN BitMask structure definition, array of 8bit bytes,
 * 1 bit per lun.  When bit == 1, the lun is masked.
 * Most significant bit of mask[0] is lun 0.
 * Least significant bit of mask[0] is lun 7.
 */
typedef struct _EXT_LUN_BIT_MASK {
#if ((EXT_DEF_MAX_LUN & 0x7) == 0)
	UINT8	mask[EXT_DEF_MAX_LUN >> 3];
#else
	UINT8	mask[(EXT_DEF_MAX_LUN + 8) >> 3 ];
#endif
} EXT_LUN_BIT_MASK, *PEXT_LUN_BIT_MASK;

/*
 * LUN mask bit manipulation macros
 *
 *   P = Pointer to an EXT_LUN_BIT_MASK union.
 *   L = LUN number.
 */
#define EXT_IS_LUN_BIT_SET(P,L) \
    (((P)->mask[L/8] & (0x80 >> (L%8)))?1:0)

#define EXT_SET_LUN_BIT(P,L) \
    ((P)->mask[L/8] |= (0x80 >> (L%8)))

#define EXT_CLR_LUN_BIT(P,L) \
    ((P)->mask[L/8] &= ~(0x80 >> (L%8)))

#define	EXT_DEF_LUN_BITMASK_LIST_MIN_ENTRIES	1
#define	EXT_DEF_LUN_BITMASK_LIST_MAX_ENTRIES	256

#ifdef _WIN64
#define	EXT_DEF_LUN_BITMASK_LIST_HEADER_SIZE	32
#else
#define	EXT_DEF_LUN_BITMASK_LIST_HEADER_SIZE \
    offsetof(LUN_BITMASK_LIST_BUFFER, asBitmaskEntry)
#endif

#define	EXT_DEF_LUN_COUNT          2048
#define	EXT_DEF_LUN_BITMASK_BYTES  (EXT_DEF_LUN_COUNT / 8)

typedef struct _EXT_LUN_BITMASK_ENTRY
{
	UINT8	NodeName[EXT_DEF_WWN_NAME_SIZE];
	UINT8	PortName[EXT_DEF_WWN_NAME_SIZE];

	UINT32	Reserved2;
	UINT32	Reserved3;
	UINT32	Reserved4;
	UINT32	Reserved5;     /* Pad to 32-byte header.*/

	UINT8	Bitmask[EXT_DEF_LUN_BITMASK_BYTES];
} EXT_LUN_BITMASK_ENTRY, *PEXT_LUN_BITMASK_ENTRY;

/* Structure as it is stored in the config file.*/
typedef struct _LUN_BITMASK_LIST
{
	UINT16	Version;       /* Should be LUN_BITMASK_REGISTRY_VERSION */
	UINT16	EntryCount;    /* Count of variable entries following.*/
	UINT32	Reserved1;
	UINT32	Reserved2;
	UINT32	Reserved3;
	UINT32	Reserved4;
	UINT32	Reserved5;
	UINT32	Reserved6;
	UINT32	Reserved7;     /* Pad to 32-byte header.*/

	EXT_LUN_BITMASK_ENTRY BitmaskEntry[1]; /* Variable-length data.*/

} EXT_LUN_BITMASK_LIST, *PEXT_LUN_BITMASK_LIST;


#define	EXT_DEF_LUN_BITMASK_LIST_MIN_SIZE   \
    (EXT_DEF_LUN_BITMASK_LIST_HEADER_SIZE + \
    (sizeof(EXT_DEF_LUN_BITMASK_ENTRY) * EXT_DEF_LUN_BITMASK_LIST_MIN_ENTRIES))
#define	EXT_DEF_LUN_BITMASK_LIST_MAX_SIZE   \
    (EXT_DEF_LUN_BITMASK_LIST_HEADER_SIZE + \
    (sizeof(EXT_DEF_LUN_BITMASK_ENTRY) * EXT_DEF_LUN_BITMASK_LIST_MAX_ENTRIES))

/* Request Buffer for ELS PT*/
#define EXT_DEF_WWPN_VALID  1
#define EXT_DEF_WWNN_VALID  2
#define EXT_DEF_PID_VALID   4
typedef struct _EXT_ELS_PT_REQ {
	UINT8     WWNN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     WWPN[EXT_DEF_WWN_NAME_SIZE];	/* 8 */
	UINT8     Id[EXT_DEF_PORTID_SIZE];  	/* 4 */
	UINT16    ValidMask;			/* 2 */
	UINT16    Lid;				/* 2 */
	UINT16    Rxid;				/* 2 */
	UINT16    AccRjt;			/* 2 */
	UINT32    Reserved;			/* 4 */
} EXT_ELS_PT_REQ, *PEXT_ELS_PT_REQ;		/* 32 */

/* LED state information */

#define	EXT_DEF_GRN_BLINK_ON	0x01ED0017
#define	EXT_DEF_GRN_BLINK_OFF	0x01ED00FF

typedef struct _EXT_BEACON_CONTROL {
	UINT32	State;				/* 4  */
	UINT32	Reserved[3];			/* 12 */	
} EXT_BEACON_CONTROL , *PEXT_BEACON_CONTROL ;	/* 16 */

#ifdef _MSC_VER
#pragma pack()
#endif

#endif /* _EXIOCT_H */
