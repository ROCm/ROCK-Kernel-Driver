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

#ifndef  _H_ELX_DISC
#define  _H_ELX_DISC

#ifdef __cplusplus
extern "C" {
#endif

/* This is a common structure definition for a Node List Entry.
 * This can be used by both Fibre Channel and iSCSI protocols.
 */
	struct elx_nodelist {
		struct elx_nodelist *nlp_listp_next;
		struct elx_nodelist *nlp_listp_prev;
		uint32_t nlp_failMask;	/* failure mask for device */
		uint16_t nlp_type;
#define NLP_FC_NODE        0x1	/* entry is an FC node */
#define NLP_ISCSI          0x2	/* entry is an iSCSI node */
#define NLP_FABRIC         0x4	/* entry represents a Fabric entity */
#define NLP_FCP_TARGET     0x8	/* entry is an FCP target */
#define NLP_IP_NODE        0x10	/* entry is an IP/FC node */

		uint16_t nlp_rpi;
		uint8_t nlp_fcp_info;	/* Remote class info */
#define NLP_FCP_2_DEVICE   0x10	/* FCP-2 device */

		uint8_t nlp_ip_info;	/* Remote class info */
		volatile int nlp_rflag;
#define NLP_SFR_ACTIVE     0x1	/* iSCSI Session Failure Recovery activated */
#define NLP_NPR_ACTIVE     0x2	/* lpfc NPort recovery activated */
#define NLP_FREED_NODE     0x4	/* nodelist entry is on free list */
	};
	typedef struct elx_nodelist ELX_NODELIST_t;

/* Defines for failMask bitmask
 * These are reasons that the device is not currently available 
 * for I/O to be sent.
 */
#define ELX_DEV_LINK_DOWN       0x1	/* Link is down */
#define ELX_DEV_DISAPPEARED     0x2	/* Device disappeared */
#define ELX_DEV_RPTLUN          0x4	/* Device needs report luns cmd */
#define ELX_DEV_INQSN_VALID     0x8	/* Validating Inquiry SN */
/* If only these bits are set, the driver is trying to recover */
#define ELX_DEV_HOLD_IO         0xf

#define ELX_DEV_INVALID         0x10	/* DEV determined invalid by drvr */
#define ELX_DEV_MAINT_MODE      0x20	/* HBA is in maintance mode */
#define ELX_DEV_INACTIVE        0x40	/* DEV made inactive by drvr internally */
#define ELX_DEV_DISCONNECTED    0x80	/* noactive connection to remote dev */
#define ELX_DEV_USER_INITIATED  0x200	/* DEV taken offline by admin */
/* If any of these bits are set, the device is gone */
#define ELX_DEV_FATAL_ERROR     0x3f0

#define ELX_DEV_DRVR_BITS       0x1ff	/* all valid driver failMask bits */
#define ELX_DEV_ALL_BITS        0x3ff	/* all valid failMask bits */

/* These defines are used for set failMask routines */
#define ELX_SET_BITMASK		1
#define ELX_CLR_BITMASK		2

#ifdef __cplusplus
}
#endif
#endif				/* _H_ELX_DISC */
