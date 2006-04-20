/*
 * SAS Frames
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
 * $Id: //depot/sas-class/sas_frames.h#5 $
 */

#ifndef _SAS_FRAMES_
#define _SAS_FRAMES_

#define SMP_REQUEST             0x40
#define SMP_RESPONSE            0x41

#define SSP_DATA                0x01
#define SSP_XFER_RDY            0x05
#define SSP_COMMAND             0x06
#define SSP_RESPONSE            0x07
#define SSP_TASK                0x16

struct  dev_to_host_fis {
	u8     fis_type;	  /* 0x34 */
	u8     flags;
	u8     status;
	u8     error;

	u8     lbal;
	union { u8 lbam; u8 byte_count_low; };
	union { u8 lbah; u8 byte_count_high; };
	u8     device;

	u8     lbal_exp;
	u8     lbam_exp;
	u8     lbah_exp;
	u8     _r_a;

	union { u8  sector_count; u8 interrupt_reason; };
	u8     sector_count_exp;
	u8     _r_b;
	u8     _r_c;

	u32    _r_d;
} __attribute__ ((packed));

struct host_to_dev_fis {
	u8     fis_type;	  /* 0x27 */
	u8     flags;
	u8     command;
	u8     features;

	u8     lbal;
	union { u8 lbam; u8 byte_count_low; };
	union { u8 lbah; u8 byte_count_high; };
	u8     device;

	u8     lbal_exp;
	u8     lbam_exp;
	u8     lbah_exp;
	u8     features_exp;

	union { u8  sector_count; u8 interrupt_reason; };
	u8     sector_count_exp;
	u8     _r_a;
	u8     control;

	u32    _r_b;
} __attribute__ ((packed));

/* Prefer to have code clarity over header file clarity.
 */
#ifdef __LITTLE_ENDIAN_BITFIELD
#include <scsi/sas/sas_frames_le.h>
#elif defined(__BIG_ENDIAN_BITFIELD)
#include <scsi/sas/sas_frames_be.h>
#else
#error "Bitfield order not defined!"
#endif

#endif /* _SAS_FRAMES_ */
