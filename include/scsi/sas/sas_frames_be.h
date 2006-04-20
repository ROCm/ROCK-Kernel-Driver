/*
 * SAS Frames Big endian bitfield order
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
 * $Id: //depot/sas-class/sas_frames_be.h#11 $
 */

#ifndef _SAS_FRAMES_BE_H_
#define _SAS_FRAMES_BE_H_

#ifndef __BIG_ENDIAN_BITFIELD
#error "Wrong header file included!"
#endif

struct sas_identify_frame {
	/* Byte 0 */
	u8  _un0:1;
	u8  dev_type:3;
	u8  frame_type:4;

	/* Byte 1 */
	u8  _un1;

	/* Byte 2 */
	union {
		struct {
			u8  _un247:4;
			u8  ssp_iport:1;
			u8  stp_iport:1;
			u8  smp_iport:1;
			u8  _un20:1;
		};
		u8 initiator_bits;
	};

	/* Byte 3 */
	union {
		struct {
			u8 _un347:4;
			u8 ssp_tport:1;
			u8 stp_tport:1;
			u8 smp_tport:1;
			u8 _un30:1;
		};
		u8 target_bits;
	};

	/* Byte 4 - 11 */
	u8 _un4_11[8];

	/* Byte 12 - 19 */
	u8 sas_addr[SAS_ADDR_SIZE];

	/* Byte 20 */
	u8 phy_id;

	u8 _un21_27[7];

	__be32 crc;
} __attribute__ ((packed));

struct ssp_frame_hdr {
	u8     frame_type;
	u8     hashed_dest_addr[HASHED_SAS_ADDR_SIZE];
	u8     _r_a;
	u8     hashed_src_addr[HASHED_SAS_ADDR_SIZE];
	__be16 _r_b;

	u8     _r_c:5;
	u8     retry_data_frames:1;
	u8     retransmit:1;
	u8     changing_data_ptr:1;

	u8     _r_d:6;
	u8     num_fill_bytes:2;

	u32    _r_e;
	__be16 tag;
	__be16 tptt;
	__be32 data_offs;
} __attribute__ ((packed));

struct ssp_response_iu {
	u8     _r_a[10];

	u8     _r_b:6;
	u8     datapres:2;

	u8     status;

	u32    _r_c;

	__be32 sense_data_len;
	__be32 response_data_len;

	u8     resp_data[0];
	u8     sense_data[0];
} __attribute__ ((packed));

/* ---------- SMP ---------- */

struct report_general_resp {
	__be16  change_count;
	__be16  route_indexes;
	u8      _r_a;
	u8      num_phys;

	u8      _r_b:6;
	u8      configuring:1;
	u8      conf_route_table:1;

	u8      _r_c;

	u8      enclosure_logical_id[8];

	u8      _r_d[12];
} __attribute__ ((packed));

struct discover_resp {
	u8    _r_a[5];

	u8    phy_id;
	__be16 _r_b;

	u8    _r_d:1;
	u8    attached_dev_type:3;
	u8    _r_c:4;

	u8    _r_e:4;
	u8    linkrate:4;

	u8    _r_f:4;
	u8    iproto:3;
	u8    attached_sata_host:1;

	u8    attached_sata_ps:1;
	u8    _r_g:3;
	u8    tproto:3;
	u8    attached_sata_dev:1;

	u8    sas_addr[8];
	u8    attached_sas_addr[8];
	u8    attached_phy_id;

	u8    _r_h[7];

	u8    pmin_linkrate:4;
	u8    hmin_linkrate:4;
	u8    pmax_linkrate:4;
	u8    hmax_linkrate:4;

	u8    change_count;

	u8    virtual:1;
	u8    _r_i:3;
	u8    pptv:4;

	u8    _r_j:4;
	u8    routing_attr:4;

	u8    conn_type;
	u8    conn_el_index;
	u8    conn_phy_link;

	u8    _r_k[8];
} __attribute__ ((packed));

struct report_phy_sata_resp {
	u8    _r_a[5];

	u8    phy_id;
	u8    _r_b;

	u8    _r_c:6;
	u8    affil_supp:1;
	u8    affil_valid:1;

	u32   _r_d;

	u8    stp_sas_addr[8];

	struct dev_to_host_fis fis;

	u32   _r_e;

	u8    affil_stp_ini_addr[8];

	__be32 crc;
} __attribute__ ((packed));

struct smp_resp {
	u8    frame_type;
	u8    function;
	u8    result;
	u8    reserved;
	union {
		struct report_general_resp  rg;
		struct discover_resp        disc;
		struct report_phy_sata_resp rps;
	};
} __attribute__ ((packed));

#endif /* _SAS_FRAMES_BE_H_ */
