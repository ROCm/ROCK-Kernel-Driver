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
#ifndef _QLISIOLN_H_
#define _QLISIOLN_H_

#include <linux/ioctl.h>

#ifdef APILIB
#include <stdint.h>
#include <linux/types.h>
#endif

#ifndef INT8
#define	INT8	int8_t
#endif
#ifndef INT16
#define	INT16	int16_t
#endif
#ifndef INT32
#define	INT32	int32_t
#endif
#ifndef UINT8
#define	UINT8	uint8_t
#endif
#ifndef UINT16
#define	UINT16	uint16_t
#endif
#ifndef UINT32
#define	UINT32	uint32_t
#endif

#ifndef UINT64
#define UINT64  unsigned long long
#endif

#ifndef BOOLEAN
#define BOOLEAN uint8_t
#endif


#if BITS_PER_LONG <= 32
#define EXT_ADDR_MODE_OS  EXT_DEF_ADDR_MODE_32
#else
#define EXT_ADDR_MODE_OS  EXT_DEF_ADDR_MODE_64
#endif


#define QLMULTIPATH_MAGIC 'z'

#define _QLBUILD   /* for qlisioct.h to enable include of qinsdmgt.h */



/* max index values */
#define	EXT_DEF_MAX_HBA_OS		63	/* 0 - 0x3F */
#define EXT_DEF_MAX_HBAS		64

#define	EXT_DEF_MAX_BUS_OS		1

#define	EXT_DEF_MAX_TARGET_OS		255	/* 0 - 0xFF */
#define EXT_DEF_MAX_TARGETS		256

#define	EXT_DEF_MAX_LUN_OS		255	/* 0 - 0xFF */
#define EXT_DEF_MAX_LUNS		256

#define EXT_DEF_MAX_AEN_QUEUE_OS        64

#define EXT_DEF_USE_HBASELECT		0x02	/* bit 1: HbaSelect field is
						 * used to specify destination
						 * HBA of each command.
						 * SetInstance cmd is now
						 * issued only once during
						 * API initialization.
						 */


#define EXT_DEF_REGULAR_SIGNATURE	"QLOGIC"


/*************************************************************/
/*                       Command codes                       */
/*-----------------------------------------------------------*/
/* Correctly defined to work on both 32bit and 64bit kernels */
/*************************************************************/
#define	QL_IOCTL_BASE(idx)	\
    _IOWR(QLMULTIPATH_MAGIC, idx, EXT_IOCTL_ISCSI)

#define	QL_IOCTL_CMD(idx)	QL_IOCTL_BASE(idx)


/***********************************
 * These are regular command codes 
 * idx range from 0x00 to 0x2f
 ***********************************/
#define EXT_DEF_REG_CC_START_IDX	0x00

#define EXT_CC_QUERY_OS				/* QUERY */	\
    QL_IOCTL_CMD(0x00)
	
#define EXT_CC_REG_AEN_OS			/* REG_AEN */ \
    QL_IOCTL_CMD(0x01)

#define EXT_CC_GET_AEN_OS			/* GET_AEN */ \
    QL_IOCTL_CMD(0x02)

#define EXT_CC_GET_DATA_OS			/* GET_DATA */ \
    QL_IOCTL_CMD(0x03)

#define EXT_CC_SET_DATA_OS			/* SET_DATA */ \
    QL_IOCTL_CMD(0x04)
	
#define EXT_CC_SEND_SCSI_PASSTHRU_OS		/* SCSI_PASSTHRU */ \
    QL_IOCTL_CMD(0x05)

#define EXT_CC_SEND_ISCSI_PASSTHRU_OS		/* ISCSI_PASSTHRU */ \
    QL_IOCTL_CMD(0x06)

#define EXT_DEF_REG_CC_END_IDX		0x06

/***********************************
 * Internal command codes 
 * idx range from 0x10 to 0x2f
 ***********************************/
#define EXT_DEF_INT_CC_START_IDX	0x10

#define EXT_CC_RESERVED0A_OS					\
    QL_IOCTL_CMD(0x10)
#define EXT_CC_RESERVED0B_OS					\
    QL_IOCTL_CMD(0x11)
#define EXT_CC_RESERVED0C_OS					\
    QL_IOCTL_CMD(0x12)
#define EXT_CC_RESERVED0D_OS					\
    QL_IOCTL_CMD(0x13)
#define EXT_CC_RESERVED0E_OS					\
    QL_IOCTL_CMD(0x14)
#define EXT_CC_RESERVED0F_OS					\
    QL_IOCTL_CMD(0x15)
#define EXT_CC_RESERVED0G_OS					\
    QL_IOCTL_CMD(0x16)
#define EXT_CC_RESERVED0H_OS					\
    QL_IOCTL_CMD(0x17)
#define EXT_CC_RESERVED0I_OS					\
    QL_IOCTL_CMD(0x18)

#define EXT_DEF_INT_CC_END_IDX		0x18

/***********************************
 * NextGen Failover ioctl command
 * codes range from 0x37 to 0x4f.
 * See qlnfoln.h
 ***********************************/

/***********************************
 * These are a Linux driver specific
 * commands.
 * idx range from highest value 0xff
 * and in decreasing order.
 ***********************************/
#define EXT_DEF_DRV_SPC_CC_START_IDX	0xff

#define EXT_CC_GET_HBACNT			/* GET_HBACNT */ \
    QL_IOCTL_CMD(0xff)

#define EXT_CC_GET_HOST_NO			/* SET_INSTANCE */ \
    QL_IOCTL_CMD(0xfe)

#define EXT_CC_DRIVER_SPECIFIC			/* DRIVER_SPECIFIC */ \
    QL_IOCTL_CMD(0xfc)


#define EXT_DEF_DRV_SPC_CC_END_IDX	0xfc

/******************************/
/* Response struct definition */
/******************************/

/*
 * HBA Count
 */
typedef struct _EXT_HBA_COUNT {
	UINT16	HbaCnt;				/* 2 */
} EXT_HBA_COUNT, *PEXT_HBA_COUNT;		/* 2 */

/*
 * Driver Specific
 */
typedef struct _EXT_LN_DRV_VERSION {
	UINT8   Major;
	UINT8   Minor;
	UINT8   Patch;
	UINT8   Beta;
	UINT8   Reserved[4];
} EXT_LN_DRV_VERSION;				/* 8 */

typedef struct _EXT_LN_DRIVER_DATA {
	EXT_LN_DRV_VERSION	DrvVer;		/* 8 */
	UINT32	Flags;				/* 4 */
	UINT32	AdapterModel;			/* 4 */
	UINT32	Reserved[12];			/* 48 */
} EXT_LN_DRIVER_DATA, *PEXT_LN_DRIVER_DATA;	/* 64 */

/* Bit defines for the Flags field */
#define EXT_DEF_NGFO_CAPABLE		0x0001	/* bit 0 */

/* Bit defines for the AdapterModel field */
/* bit 0 to bit 7 are used by FC driver. when adding new bit
 * definitions they must be unique among all supported drivers
 */
#define EXT_DEF_QLA4010_DRIVER		0x0100	/* bit 8 */
#define EXT_DEF_QLA4022_DRIVER		0x0200	/* bit 9 */

#define EXT_DEF_QLA4XXX_DRIVER				\
    (EXT_DEF_QLA4010_DRIVER | EXT_DEF_QLA4022_DRIVER)



#endif //_QLISIOLN_H_
