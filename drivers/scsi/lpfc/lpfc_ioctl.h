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

#ifndef _H_LPFC_IOCTL
#define _H_LPFC_IOCTL

#include "elx_ioctl.h"

/* LPFC Ioctls() 0x40 - 0x7F */

/* LPFC_FIRST_IOCTL_USED         0x40     First defines Ioctl used  */
#define LPFC_LIP                       0x41	/* Issue a LIP */
#define LPFC_CT            0x42	/* Send CT passthru command */
#define LPFC_LISTN                     0x43	/* List nodes for adapter (by WWPN, WWNN and DID) */

/*  HBA API specific Ioctls() */

#define LPFC_HBA_ADAPTERATTRIBUTES     0x48	/* Get attributes of the adapter */
#define LPFC_HBA_PORTATTRIBUTES           0x49	/* Get attributes of  the adapter  Port */
#define LPFC_HBA_PORTSTATISTICS        0x4a	/* Get statistics of  the adapter  Port */
#define LPFC_HBA_DISCPORTATTRIBUTES       0x4b	/* Get attibutes of the discovered adapter Ports */
#define LPFC_HBA_WWPNPORTATTRIBUTES       0x4c	/* Get attributes of  the Port specified by WWPN */
#define LPFC_HBA_INDEXPORTATTRIBUTES      0x4d	/* Get attributes of  the Port specified by index */
#define LPFC_HBA_FCPTARGETMAPPING      0x4e	/* Get device info for each FCP target */
#define LPFC_HBA_FCPBINDING            0x4f	/* Get binding info for each FCP target */
#define LPFC_HBA_SETMGMTINFO           0x50	/* Sets driver values with HBA_MGMTINFO vals */
#define LPFC_HBA_GETMGMTINFO           0x51	/* Get driver values for HBA_MGMTINFO vals */
#define LPFC_HBA_RNID                  0x52	/* Send an RNID request */
#define LPFC_HBA_GETEVENT              0x53	/* Get event data */
#define LPFC_HBA_RESETSTAT          0x54	/* Resets counters for the specified board */
#define LPFC_HBA_SEND_SCSI             0x55	/* Send SCSI requests to target */
#define LPFC_HBA_REFRESHINFO           0x56	/* Do a refresh (read) of the values */
#define LPFC_SEND_ELS               0x57	/* Send out an ELS command */
#define LPFC_HBA_SEND_FCP              0x58	/* Send out a FCP command */
#define LPFC_HBA_SET_EVENT                0x59	/* Set FCP event(s) */
#define LPFC_HBA_GET_EVENT                0x5a	/* Get  FCP event(s) */
#define LPFC_HBA_SEND_MGMT_CMD            0x5b	/* Send a management command */
#define LPFC_HBA_SEND_MGMT_RSP            0x5c	/* Send a management response */

#define LPFC_UNUSED           0x61	/* Report statistics on failed I/O */
#define LPFC_RESET_QDEPTH        0x62	/* Reset adapter Q depth */
#define LPFC_OUTFCPIO            0x63	/* Number of outstanding I/Os */
#define LPFC_GETCFG                     0x64	/* Get configuration parameters */
#define LPFC_SETCFG                 0x65	/* Set configuration parameters */
#define LPFC_TRACE                     0x66
#define LPFC_STAT                   0x67	/* Statistics for SLI/FC/IP */

/*  LPFC_LAST_IOCTL_USED         0x67  Last LPFC Ioctl used  */

/* Structure for OUTFCPIO command */

struct out_fcp_devp {
	uint16_t target;
	uint16_t lun;
	uint16_t tx_count;
	uint16_t txcmpl_count;
	uint16_t delay_count;
	uint16_t sched_count;
	uint16_t lun_qdepth;
	uint16_t current_qdepth;
	uint32_t qfullcnt;
	uint32_t qcmdcnt;
	uint32_t iodonecnt;
	uint32_t errorcnt;
};

#define MREC_MAX 16
#define arecord(a, b, c, d)

struct rec {
	void *arg0;
	void *arg1;
	void *arg2;
	void *arg3;
};

/*
 * This structure needs to fit in di->fc_dataout alloc'ed memory
 * array in dfc_un for dfc.c / C_TRACE
 */
struct mrec {
	ulong reccnt;
	struct rec rectbl[MREC_MAX];
};

typedef struct fcEVT {		/* Kernel level Event structure */
	uint32_t evt_handle;
	uint32_t evt_mask;
	uint32_t evt_data0;
	uint16_t evt_sleep;
	uint16_t evt_flags;
	void *evt_type;
	void *evt_next;
	void *evt_data1;
	void *evt_data2;
} fcEVT_t;

typedef struct fcEVTHDR {	/* Kernel level Event Header */
	uint32_t e_handle;
	uint32_t e_mask;
	uint16_t e_mode;
#define E_SLEEPING_MODE     0x0001
	uint16_t e_refcnt;
	uint16_t e_flag;
#define E_GET_EVENT_ACTIVE  0x0001
	fcEVT_t *e_head;
	fcEVT_t *e_tail;
	void *e_next_header;
	void *e_type;
} fcEVTHDR_t;

#endif				/* _H_LPFC_IOCTL */
