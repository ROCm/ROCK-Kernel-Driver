/*****************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic device driver for Linux 2.4.x+
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
****************************************************************************/
#ifndef _QLNFOLN_H_
#define _QLNFOLN_H_

/********************************************************
 * NextGen Failover ioctl command codes range from 0x37
 * to 0x4f.  See qlnfoln.h
 ********************************************************/
#define EXT_DEF_NFO_CC_START_IDX	0x37	/* NFO cmd start index */

#define EXT_CC_TRANSPORT_INFO					\
    QL_IOCTL_CMD(0x37)
#define EXT_CC_GET_FOM_PROP					\
    QL_IOCTL_CMD(0x38)
#define EXT_CC_GET_HBA_INFO					\
    QL_IOCTL_CMD(0x39)
#define EXT_CC_GET_DPG_PROP					\
    QL_IOCTL_CMD(0x3a)
#define EXT_CC_GET_DPG_PATH_INFO					\
    QL_IOCTL_CMD(0x3b)
#define EXT_CC_SET_DPG_PATH_INFO					\
    QL_IOCTL_CMD(0x3c)
#define EXT_CC_GET_LB_INFO					\
    QL_IOCTL_CMD(0x3d)
#define EXT_CC_GET_LB_POLICY					\
    QL_IOCTL_CMD(0x3e)
#define EXT_CC_SET_LB_POLICY					\
    QL_IOCTL_CMD(0x3f)
#define EXT_CC_GET_DPG_STATS					\
    QL_IOCTL_CMD(0x40)
#define EXT_CC_CLEAR_DPG_ERR_STATS				\
    QL_IOCTL_CMD(0x41)
#define EXT_CC_CLEAR_DPG_IO_STATS					\
    QL_IOCTL_CMD(0x42)
#define EXT_CC_CLEAR_DPG_FO_STATS					\
    QL_IOCTL_CMD(0x43)
#define EXT_CC_GET_PATHS_FOR_ALL					\
    QL_IOCTL_CMD(0x44)
#define EXT_CC_MOVE_PATH						\
    QL_IOCTL_CMD(0x45)
#define EXT_CC_VERIFY_PATH					\
    QL_IOCTL_CMD(0x46)
#define EXT_CC_GET_EVENT_LIST					\
    QL_IOCTL_CMD(0x47)
#define EXT_CC_ENABLE_FOM						\
    QL_IOCTL_CMD(0x48)
#define EXT_CC_DISABLE_FOM					\
    QL_IOCTL_CMD(0x49)
#define EXT_CC_GET_STORAGE_LIST					\
    QL_IOCTL_CMD(0x4a)

#define EXT_DEF_NFO_CC_END_IDX	0x4a	/* NFO cmd end index */


typedef struct _EXT_IOCTL_NFO {
	UINT8	Signature[NFO_DEF_SIGNATURE_SIZE];	/* 8   */
	UINT16	AddrMode;				/* 2   */
	UINT16	Version;				/* 2   */
	UINT16	SubCode;				/* 2   */
	UINT16	Instance;				/* 2   */
	UINT32	Status; 				/* 4   */
	UINT32	DetailStatus;				/* 4   */
	UINT32	Reserved1;				/* 4   */
	UINT32	RequestLen;				/* 4   */
	UINT32	ResponseLen;				/* 4   */
	UINT64	RequestAdr;				/* 8   */
	UINT64	ResponseAdr;				/* 8   */
	UINT16	HbaSelect;				/* 2   */
	UINT32	VendorSpecificStatus[11];		/* 44  */
	UINT8	VendorSpecificData[8];			/* 8  */
	UINT32	Reserved2[8];				/* 32  */
} EXT_IOCTL_NFO, *PEXT_IOCTL_NFO;			/* 138 */


#endif  /* _QLNFOLN_H_ */

