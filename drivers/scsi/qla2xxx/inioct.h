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
 * File Name: inioct.h
 *
 * San/Device Management Ioctl Header
 * File is created to adhere to Solaris requirement using 8-space tabs.
 *
 * !!!!! PLEASE DO NOT REMOVE THE TABS !!!!!
 * !!!!! PLEASE NO SINGLE LINE COMMENTS: // !!!!!
 * !!!!! PLEASE NO MORE THAN 80 CHARS PER LINE !!!!!
 *
 *
 * Revision History:
 *
 * Rev. 0	June 15, 2001
 * YPL	- Created.
 *
 * Rev. 1	June 26, 2001
 * YPL	- Change the loop back structure and delete cc that is not used.
 *
 * Rev. 2	June 29, 2001
 * YPL	- Use new EXT_CC defines from exioct.h
 *
 * Rev. 3	July 12, 2001
 * RL	- Added definitions for loopback mbx command completion codes.
 *
 * Rev. 4	July 12, 2001
 * RL	- Added definitions for loopback mbx command completion codes.
 *
 * Rev. 5	October 9, 2002
 * AV	- Added definition for Read Option ROM IOCTL.
 *
 * Rev. 6	May 27, 2003
 * RL	- Modified loopback rsp buffer structure definition to add
 *        diagnostic Echo command support.
 *
 */

#ifndef	_INIOCT_H
#define	_INIOCT_H

/*
 * ***********************************************************************
 * X OS type definitions
 * ***********************************************************************
 */
#ifdef _MSC_VER						/* NT */
#pragma pack(1)
#endif

/*
 * ***********************************************************************
 * INT_IOCTL SubCode definition.
 * These macros are being used for setting SubCode field in EXT_IOCTL
 * structure.
 * ***********************************************************************
 */

/*
 * Currently supported DeviceControl / ioctl command codes
 */
#define	INT_CC_GET_PORT_STAT_FC		EXT_CC_RESERVED0A_OS
#define	INT_CC_LOOPBACK			EXT_CC_RESERVED0B_OS
#define	INT_CC_UPDATE_OPTION_ROM	EXT_CC_RESERVED0C_OS
#define	INT_CC_ADD_TARGET_DEVICE	EXT_CC_RESERVED0D_OS
#define	INT_CC_READ_NVRAM		EXT_CC_RESERVED0E_OS
#define	INT_CC_UPDATE_NVRAM		EXT_CC_RESERVED0F_OS
#define	INT_CC_SWAP_TARGET_DEVICE	EXT_CC_RESERVED0G_OS
#define	INT_CC_READ_OPTION_ROM		EXT_CC_RESERVED0H_OS
#define	INT_CC_LEGACY_LOOPBACK		EXT_CC_RESERVED0Z_OS



/* NVRAM */
#define	INT_SC_NVRAM_HARDWARE		0	/* Save */
#define	INT_SC_NVRAM_DRIVER		1	/* Driver (Apply) */
#define	INT_SC_NVRAM_ALL		2	/* NVRAM/Driver (Save+Apply) */

/* Loopback */
typedef struct _INT_LOOPBACK_REQ
{
	UINT16 Options;				/* 2   */
	UINT32 TransferCount;			/* 4   */
	UINT32 IterationCount;			/* 4   */
	UINT64 BufferAddress;			/* 8  */
	UINT32 BufferLength;			/* 4  */
	UINT16 Reserved[9];			/* 18  */
} INT_LOOPBACK_REQ, *PINT_LOOPBACK_REQ;		/* 408 */

typedef struct _INT_LOOPBACK_RSP
{
	UINT64 BufferAddress;			/* 8  */
	UINT32 BufferLength;			/* 4  */
	UINT16 CompletionStatus;		/* 2  */
	UINT16 CrcErrorCount;			/* 2  */
	UINT16 DisparityErrorCount;		/* 2  */
	UINT16 FrameLengthErrorCount;		/* 2  */
	UINT32 IterationCountLastError;		/* 4  */
	UINT8  CommandSent;			/* 1  */
	UINT8  Reserved1;			/* 1  */
	UINT16 Reserved2[7];			/* 16 */
} INT_LOOPBACK_RSP, *PINT_LOOPBACK_RSP;		/* 40 */

/* definition for interpreting CompletionStatus values */
#define	INT_DEF_LB_COMPLETE	0x4000
#define INT_DEF_LB_ECHO_CMD_ERR 0x4005
#define	INT_DEF_LB_PARAM_ERR	0x4006
#define	INT_DEF_LB_LOOP_DOWN	0x400b
#define	INT_DEF_LB_CMD_ERROR	0x400c

/* definition for interpreting CommandSent field */
#define INT_DEF_LB_LOOPBACK_CMD 	0
#define INT_DEF_LB_ECHO_CMD		1

#ifdef _MSC_VER
#pragma pack()
#endif

#endif /* _INIOCT_H */
