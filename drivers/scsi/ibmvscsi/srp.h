/*****************************************************************************/
/* srp.h -- SCSI RDMA Protocol definitions                                   */
/*                                                                           */
/* Written By: Colin Devilbis, IBM Corporation                               */
/*                                                                           */
/* Copyright (C) 2003 IBM Corporation                                        */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/*                                                                           */
/* This file contains structures and definitions for the SCSI RDMA Protocol  */
/* (SRP) as defined in the T10 standard available at www.t10.org.  This      */
/* file was based on the 16a version of the standard                         */
/*                                                                           */
/*****************************************************************************/
#ifndef SRP_H
#define SRP_H

#define PACKED __attribute__((packed))

enum SRP_TYPES {
	SRP_LOGIN_REQ_TYPE = 0x00,
	SRP_LOGIN_RSP_TYPE = 0xC0,
	SRP_LOGIN_REJ_TYPE = 0x80,
	SRP_I_LOGOUT_TYPE  = 0x03,
	SRP_T_LOGOUT_TYPE  = 0x80,
	SRP_TSK_MGMT_TYPE  = 0x01,
	SRP_CMD_TYPE       = 0x02,
	SRP_RSP_TYPE       = 0xC1,
	SRP_CRED_REQ_TYPE  = 0x81,
	SRP_CRED_RSP_TYPE  = 0x41,
	SRP_AER_REQ_TYPE   = 0x82,
	SRP_AER_RSP_TYPE   = 0x42
};

enum SRP_DESCRIPTOR_FORMATS {
	SRP_NO_BUFFER = 0x00,
	SRP_DIRECT_BUFFER = 0x01,
	SRP_INDIRECT_BUFFER = 0x02
};

struct memory_descriptor {
	u64 virtual_address; // 0x00
	u32 memory_handle; // 0x08
	u32 length; // 0x0C
}; // 0x10

struct indirect_descriptor {
	struct memory_descriptor head;
	u64 total_length;
	struct memory_descriptor list[1];
};

struct SRP_GENERIC {
	u8 type; // 0x00
	u8 reserved1[7]; // 0x01
	u64 tag; // 0x08
}; // 0x10

struct SRP_LOGIN_REQ {
	u8 type;         // 0x00
	u8 reserved1[7]; // 0x01
	u64 tag;         // 0x08
	u32 max_requested_initiator_to_target_iulen; // 0x10
	u32 reserved2; // 0x14
	u16 required_buffer_formats; // 0x18
	u8 reserved3:6; u8 multi_channel_action:2; // 0x1A
	u8 reserved4; // 0x1B
	u32 reserved5; // 0x1C
	u8 initiator_port_identifier[16]; // 0x20
	u8 target_port_identifier[16]; // 0x30
}; // 0x40

struct SRP_LOGIN_RSP {
	u8 type; // 0x00
	u8 reserved1[3]; // 0x01
	u32 request_limit_delta; // 0x04
	u64 tag; // 0x08
	u32 max_initiator_to_target_iulen; // 0x10
	u32 max_target_to_initiator_iulen; // 0x14
	u16 supported_buffer_formats;       // 0x18
	u8 reserved2:6; u8 multi_channel_result:2; // 0x1A
	u8 reserved3; // 0x1B
	u8 reserved4[24]; // 0x1C
}; // 0x34

struct SRP_LOGIN_REJ {
	u8 type; // 0x00
	u8 reserved1[3]; // 0x01
	u32 reason; // 0x04
	u64 tag; // 0x08
	u64 reserved2; // 0x10
	u16 supported_buffer_formats; // 0x18
	u8 reserved3[6]; // 0x1A
}; // 0x20

struct SRP_I_LOGOUT {
	u8 type; // 0x00
	u8 reserved1[7]; // 0x01
	u64 tag; // 0x08
}; // 0x10

struct SRP_T_LOGOUT {
	u8 type; // 0x00
	u8 reserved1[3]; // 0x01
	u32 reason; // 0x04
	u64 tag; // 0x08
}; // 0x10

struct SRP_TSK_MGMT {
	u8 type; // 0x00
	u8 reserved1[7]; // 0x01
	u64 tag; // 0x08
	u32 reserved2; // 0x10
	u64 lun PACKED; // 0x14
	u8 reserved3; // 0x1C
	u8 reserved4; // 0x1D
	u8 task_mgmt_flags; // 0x1E
	u8 reserved5; // 0x1F
	u64 managed_task_tag; // 0x20
	u64 reserved6; // 0x28
}; // 0x30

struct SRP_CMD {
	u8 type;              // 0x00
	u32 reserved1 PACKED;		  // 0x01
	u8 data_out_format:4; u8 data_in_format:4; // 0x05
	u8 data_out_count;    // 0x06
	u8 data_in_count;     // 0x07
	u64 tag;              // 0x08
	u32 reserved2;        // 0x10
	u64 lun PACKED;           // 0x14
	u8 reserved3;         // 0x1C
	u8 reserved4:5; u8 task_attribute:3;  // 0x1D
	u8 reserved5;         // 0x1E
	u8 additional_cdb_len; // 0x1F
	u8 cdb[16];           // 0x20
	u8 additional_data[0x100 - 0x30]; // 0x30
}; // 0x100

struct SRP_RSP {
	u8 type; // 0x00
	u8 reserved1[3]; // 0x01
	u32 request_limit_delta; // 0x04
	u64 tag; // 0x08
	u16 reserved2; // 0x10
	u8 reserved3:2; // 0x12
		u8 diunder:1; u8 diover:1;
		u8 dounder:1; u8 doover:1;
		u8 snsvalid:1; u8 rspvalid:1;
	u8 status; // 0x13
	u32 data_in_residual_count; // 0x14
	u32 data_out_residual_count; // 0x18
	u32 sense_data_list_length; // 0x1C
	u32 response_data_list_length; // 0x20
	u8 sense_and_response_data[18]; // 0x24
}; // 0x36

struct SRP_CRED_REQ {
	u8 type; // 0x00
	u8 reserved1[3]; // 0x01
	u32 request_limit_delta; // 0x04
	u64 tag; // 0x08
}; // 0x10

struct SRP_CRED_RSP {
	u8 type; // 0x00
	u8 reserved1[7]; // 0x01
	u64 tag; // 0x08
}; // 0x10

struct SRP_AER_REQ {
	u8 type; // 0x00
	u8 reserved1[3]; // 0x01
	u32 request_limit_delta; // 0x04
	u64 tag; // 0x08
	u32 reserved2; // 0x10
	u64 lun; // 0x14
	u32 sense_data_list_length; // 0x1C
	u32 reserved3; // 0x20
	u8 sense_data[20]; // 0x24
}; // 0x38

struct SRP_AER_RSP {
	u8 type; // 0x00
	u8 reserved1[7]; // 0x01
	u64 tag; // 0x08
}; // 0x10

union SRP_IU {
	struct SRP_GENERIC generic;
	struct SRP_LOGIN_REQ login_req;
	struct SRP_LOGIN_RSP login_rsp;
	struct SRP_LOGIN_REJ login_rej;
	struct SRP_I_LOGOUT i_logout;
	struct SRP_T_LOGOUT t_logout;
	struct SRP_TSK_MGMT tsk_mgmt;
	struct SRP_CMD cmd;
	struct SRP_RSP rsp;
	struct SRP_CRED_REQ cred_req;
	struct SRP_CRED_RSP cred_rsp;
	struct SRP_AER_REQ aer_req;
	struct SRP_AER_RSP aer_rsp;
};

#endif
