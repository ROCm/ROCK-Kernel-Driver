/*****************************************************************************
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
****************************************************************************/

/*
 * File Name: exioctln.h

   Rev 16    July 31, 2003	RL
	     - Added definitions for Status field in discovered target
	       structure.
	     - Updated ioctl command value assignment on PPC64 so this
	       file can be shared with API lib.

   Rev 15    June 03, 2003	RL
	     - Modified ioctl command code value assignment so it also
	       works on PPC64.

   Rev 14    February 25, 2003	RL
             - Added EXT_CC_DRIVER_SPECIFIC ioctl command to return
	       some driver specific data that can be used by API library
	       to determine how to maintain backward compatibility
	       of certain features.

   Rev 13    January 31, 2003	RL
             - Changed the value of EXT_DEF_USE_HBASELECT to avoid
               conflicting with older implementation of FO API lib.

   Rev 12    January 20, 2003	RL
             - Added EXT_DEF_USE_HBASELECT definition for use by
               the SETINSTANCE command.
 
   Rev 11    December 10, 2002	RL
             - Added EXT_CC_SEND_ELS_PASSTHRU_OS definition.
 
   Rev 10    October 26, 2001	RL
             - Corrected MAX_HBA, MAX_TARGET and MAX_LUN values to 255.
 
   Rev 9     July 26, 2001	RL
             - Added definition of signed types.
 
   Rev 8     July 05, 2001	RL
             - Redefined ioctl command values.
 
   Rev 7     Nov 06, 2000   BN
             - Added EXT_DEF_MAX_AEN_QUEUE_OS define
             - Added define for handle_hba_t
 
   Rev 6     Oct 25, 2000   BN
             - Added EXT_CC_DRIVER_PROP_OS define
 
   Rev 5     Oct 25, 2000   BN
             - Redo the copyright header and add AEN details
 
   Rev 4     Oct 23, 2000   BN
             - Added definition for BOOLEAN
 
   Rev 3     Oct 23, 2000   BN
             - Added definitions for EXT_ADDR_MODE_OS
               and also include of <linux/ioctl.h>
 
   Rev 2     Oct 18, 2000   BN
             - Enable API Exention support
 
   Rev 1     Original version Sep 7, 2000   BN
 
*/


#ifndef _EXIOCT_LN_H_
#define _EXIOCT_LN_H_

#include <linux/ioctl.h>

#ifdef APILIB
#include <stdint.h>
#endif


#define	INT8	int8_t
#define	INT16	int16_t
#define	INT32	int32_t
#define	UINT8	uint8_t
#define	UINT16	uint16_t
#define	UINT32	uint32_t
#define	UINT64	void *
#define BOOLEAN uint8_t

typedef struct  track_instance {
	int   handle;
} track_instance_t;


#if BITS_PER_LONG <= 32
#define EXT_ADDR_MODE_OS  EXT_DEF_ADDR_MODE_32
#else
#define EXT_ADDR_MODE_OS  EXT_DEF_ADDR_MODE_64
#endif


#define QLMULTIPATH_MAGIC 'y'

#define _QLBUILD   /* for exioct.h to enable include of qinsdmgt.h */



#define	EXT_DEF_MAX_HBA_OS		255	/* 0 - 0xFE */
#define	EXT_DEF_MAX_BUS_OS		1
#define	EXT_DEF_MAX_TARGET_OS		255	/* 0 - 0xFE */
#define	EXT_DEF_MAX_LUN_OS		255	/* 0 - 0xFE */

#define EXT_DEF_MAX_AEN_QUEUE_OS        64

#define EXT_DEF_FC_HEADER_LEN		24
#define EXT_DEF_ELS_RJT_LENGTH		0x08	/* 8  */
#define EXT_DEF_ELS_RPS_ACC_LENGTH	0x40	/* 64 */
#define EXT_DEF_ELS_RLS_ACC_LENGTH	0x1C	/* 28 */

#define EXT_DEF_USE_HBASELECT		0x02	/* bit 1: HbaSelect field now
						 * used to specify destination
						 * HBA of each command.
						 * SetInstance cmd is now
						 * issued only once during
						 * API initialization.
						 */

/* target status flags */
#define	EXT_DEF_TGTSTAT_OFFLINE		0x01
#define EXT_DEF_TGTSTAT_IN_CFG		0x02


/*****************/
/* Command codes */
/*****************/
#define	QL_IOCTL_BASE(idx)	\
    _IOWR(QLMULTIPATH_MAGIC, idx, size_t)

#ifndef APILIB
  #if CONFIG_PPC64
  #define	QL_IOCTL_CMD(idx)	(QL_IOCTL_BASE(idx) - 0x40000)
  #else
  #define	QL_IOCTL_CMD(idx)	QL_IOCTL_BASE(idx)
  #endif
#else
  #define	QL_IOCTL_CMD(idx)	QL_IOCTL_BASE(idx)
#endif

/***************************************************************
 * These are regular/external command codes, starting from 0.
 * The regular command code end index must be updated whenever
 * adding new commands. 
 ***************************************************************/
#define EXT_DEF_LN_REG_CC_START_IDX	0x00	/* reg cmd start index */

#define EXT_CC_QUERY_OS					/* QUERY */	\
    QL_IOCTL_CMD(0x00)
#define EXT_CC_SEND_FCCT_PASSTHRU_OS			/* FCCT_PASSTHRU */ \
    QL_IOCTL_CMD(0x01)
#define	EXT_CC_REG_AEN_OS				/* REG_AEN */	\
    QL_IOCTL_CMD(0x02)
#define	EXT_CC_GET_AEN_OS				/* GET_AEN */	\
    QL_IOCTL_CMD(0x03)
#define	EXT_CC_SEND_ELS_RNID_OS				/* SEND_ELS_RNID */ \
    QL_IOCTL_CMD(0x04)
#define	EXT_CC_SCSI_PASSTHRU_OS				/* SCSI_PASSTHRU */ \
    QL_IOCTL_CMD(0x05)

#define EXT_CC_GET_DATA_OS				/* GET_DATA */	\
    QL_IOCTL_CMD(0x06)
#define EXT_CC_SET_DATA_OS				/* SET_DATA */	\
    QL_IOCTL_CMD(0x07)

#define EXT_DEF_LN_REG_CC_END_IDX	0x07	/* reg cmd end index */

/*****************************************
 * Following are internal command codes.
 * See inioct.h.
 *****************************************/
#define EXT_DEF_LN_INT_CC_START_IDX	0x08	/* int cmd start index */
#define EXT_CC_RESERVED0A_OS						\
    QL_IOCTL_CMD(0x08)
#define EXT_CC_RESERVED0B_OS						\
    QL_IOCTL_CMD(0x09)

#define EXT_CC_RESERVED0C_OS						\
    QL_IOCTL_CMD(0x0a)
#define EXT_CC_RESERVED0D_OS						\
    QL_IOCTL_CMD(0x0b)

#define EXT_CC_RESERVED0E_OS						\
    QL_IOCTL_CMD(0x0c)
#define EXT_CC_RESERVED0F_OS						\
    QL_IOCTL_CMD(0x0d)

#define EXT_CC_RESERVED0G_OS						\
    QL_IOCTL_CMD(0x0e)
#define EXT_CC_RESERVED0H_OS						\
    QL_IOCTL_CMD(0x0f)

#define EXT_CC_RESERVED0I_OS						\
    QL_IOCTL_CMD(0x10)
#define EXT_CC_RESERVED0J_OS						\
    QL_IOCTL_CMD(0x11)

#define EXT_DEF_LN_INT_CC_END_IDX	0x11	/* supported int cmd end idx */

#define EXT_CC_RESERVED0Z_OS						\
    QL_IOCTL_CMD(0x21)

/********************************************************/
/* These are additional regular/external command codes. */
/********************************************************/
#define EXT_DEF_LN_ADD_CC_START_IDX	0x30	/* additional cmd start index */
#define EXT_CC_SEND_ELS_PASSTHRU_OS					\
    QL_IOCTL_CMD(0x30)
#define EXT_DEF_LN_ADD_CC_END_IDX	0x30	/* additional cmd end index */


/********************************************************
 * Failover ioctl command codes range from 0xc0 to 0xdf.
 * See definition in qlfoln.h.
 ********************************************************/


/*******************************************************************/
/* These are Linux driver implementation specific commands. Values */
/* start from highest possible value and in decreasing order.      */
/*******************************************************************/
#define EXT_DEF_LN_SPC_CC_START_IDX	0xff	/* LN specific cmd start idx */

#define EXT_CC_STARTIOCTL				/* STARTIOCTL */ \
    QL_IOCTL_CMD(0xff)
#define EXT_CC_SETINSTANCE				/* SETINSTANCE */ \
    QL_IOCTL_CMD(0xfe)
#define	EXT_CC_WWPN_TO_SCSIADDR				/* WWPN_TO_SCSIADDR */ \
    QL_IOCTL_CMD(0xfd)
#define	EXT_CC_DRIVER_SPECIFIC				/* DRIVER_SPECIFIC */ \
    QL_IOCTL_CMD(0xfc)

#define EXT_DEF_LN_SPC_CC_END_IDX	0xfc	/* LN specific cmd end idx */


/*
 * Response struct definition
 */
typedef struct _EXT_LN_DRV_VERSION {
	UINT8	Major;
	UINT8	Minor;
	UINT8	Patch;
	UINT8	Beta;
	UINT8	Reserved[4];
} EXT_LN_DRV_VERSION;				/* 8 */

typedef struct _EXT_LN_DRIVER_DATA {
	EXT_LN_DRV_VERSION  	DrvVer;		/* 8 */
	UINT32    Reserved[14];			/* 56 */
} EXT_LN_DRIVER_DATA, *PEXT_LN_DRIVER_DATA;	/* 64 */






/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

#endif /* _EXIOCT_LN_H_ */

