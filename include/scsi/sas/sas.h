/*
 * SAS structures and definitions header file
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * $Id: //depot/sas-class/sas.h#25 $
 */

#ifndef _SAS_H_
#define _SAS_H_

#include <linux/types.h>
#include <asm/byteorder.h>

#define SAS_ADDR_SIZE        8
#define HASHED_SAS_ADDR_SIZE 3
#define SAS_ADDR(_sa)   (be64_to_cpu(*(__be64 *)(_sa)))

enum sas_oob_mode {
	OOB_NOT_CONNECTED,
	SATA_OOB_MODE,
	SAS_OOB_MODE
};

/* See sas_discover.c if you plan on changing these.
 */
enum sas_dev_type {
	NO_DEVICE   = 0,	  /* protocol */
	SAS_END_DEV = 1,	  /* protocol */
	EDGE_DEV    = 2,	  /* protocol */
	FANOUT_DEV  = 3,	  /* protocol */
	SAS_HA      = 4,
	SATA_DEV    = 5,
	SATA_PM     = 7,
	SATA_PM_PORT= 8,
};

enum sas_phy_linkrate {
	PHY_LINKRATE_NONE = 0,
	PHY_LINKRATE_UNKNOWN = 0,
	PHY_DISABLED,
	PHY_RESET_PROBLEM,
	PHY_SPINUP_HOLD,
	PHY_PORT_SELECTOR,
	PHY_LINKRATE_1_5 = 0x08,
	PHY_LINKRATE_G1  = PHY_LINKRATE_1_5,
	PHY_LINKRATE_3   = 0x09,
	PHY_LINKRATE_G2  = PHY_LINKRATE_3,
	PHY_LINKRATE_6   = 0x0A,
};

/* Partly from IDENTIFY address frame. */
enum sas_proto {
	SATA_PROTO    = 1,
	SAS_PROTO_SMP = 2,	  /* protocol */
	SAS_PROTO_STP = 4,	  /* protocol */
	SAS_PROTO_SSP = 8,	  /* protocol */
	SAS_PROTO_ALL = 0xE,
};

/* From the spec; local phys only */
enum phy_func {
	PHY_FUNC_NOP,
	PHY_FUNC_LINK_RESET,		  /* Enables the phy */
	PHY_FUNC_HARD_RESET,
	PHY_FUNC_DISABLE,
	PHY_FUNC_CLEAR_ERROR_LOG = 5,
	PHY_FUNC_CLEAR_AFFIL,
	PHY_FUNC_TX_SATA_PS_SIGNAL,
	PHY_FUNC_RELEASE_SPINUP_HOLD = 0x10, /* LOCAL PORT ONLY! */
};

#include <scsi/sas/sas_frames.h>

/* SAS LLDD would need to report only _very_few_ of those, like BROADCAST.
 * Most of those are here for completeness.
 */
enum sas_prim {
	SAS_PRIM_AIP_NORMAL = 1,
	SAS_PRIM_AIP_R0     = 2,
	SAS_PRIM_AIP_R1     = 3,
	SAS_PRIM_AIP_R2     = 4,
	SAS_PRIM_AIP_WC     = 5,
	SAS_PRIM_AIP_WD     = 6,
	SAS_PRIM_AIP_WP     = 7,
	SAS_PRIM_AIP_RWP    = 8,

	SAS_PRIM_BC_CH      = 9,
	SAS_PRIM_BC_RCH0    = 10,
	SAS_PRIM_BC_RCH1    = 11,
	SAS_PRIM_BC_R0      = 12,
	SAS_PRIM_BC_R1      = 13,
	SAS_PRIM_BC_R2      = 14,
	SAS_PRIM_BC_R3      = 15,
	SAS_PRIM_BC_R4      = 16,

	SAS_PRIM_NOTIFY_ENSP= 17,
	SAS_PRIM_NOTIFY_R0  = 18,
	SAS_PRIM_NOTIFY_R1  = 19,
	SAS_PRIM_NOTIFY_R2  = 20,

	SAS_PRIM_CLOSE_CLAF = 21,
	SAS_PRIM_CLOSE_NORM = 22,
	SAS_PRIM_CLOSE_R0   = 23,
	SAS_PRIM_CLOSE_R1   = 24,

	SAS_PRIM_OPEN_RTRY  = 25,
	SAS_PRIM_OPEN_RJCT  = 26,
	SAS_PRIM_OPEN_ACPT  = 27,

	SAS_PRIM_DONE       = 28,
	SAS_PRIM_BREAK      = 29,

	SATA_PRIM_DMAT      = 33,
	SATA_PRIM_PMNAK     = 34,
	SATA_PRIM_PMACK     = 35,
	SATA_PRIM_PMREQ_S   = 36,
	SATA_PRIM_PMREQ_P   = 37,
	SATA_SATA_R_ERR     = 38,
};

enum sas_open_rej_reason {
	/* Abandon open */
	SAS_OREJ_UNKNOWN   = 0,
	SAS_OREJ_BAD_DEST  = 1,
	SAS_OREJ_CONN_RATE = 2,
	SAS_OREJ_EPROTO    = 3,
	SAS_OREJ_RESV_AB0  = 4,
	SAS_OREJ_RESV_AB1  = 5,
	SAS_OREJ_RESV_AB2  = 6,
	SAS_OREJ_RESV_AB3  = 7,
	SAS_OREJ_WRONG_DEST= 8,
	SAS_OREJ_STP_NORES = 9,

	/* Retry open */
	SAS_OREJ_NO_DEST   = 10,
	SAS_OREJ_PATH_BLOCKED = 11,
	SAS_OREJ_RSVD_CONT0 = 12,
	SAS_OREJ_RSVD_CONT1 = 13,
	SAS_OREJ_RSVD_INIT0 = 14,
	SAS_OREJ_RSVD_INIT1 = 15,
	SAS_OREJ_RSVD_STOP0 = 16,
	SAS_OREJ_RSVD_STOP1 = 17,
	SAS_OREJ_RSVD_RETRY = 18,
};
#endif /* _SAS_H_ */
