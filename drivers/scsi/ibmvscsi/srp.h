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
	SRP_I_LOGOUT_TYPE = 0x03,
	SRP_T_LOGOUT_TYPE = 0x80,
	SRP_TSK_MGMT_TYPE = 0x01,
	SRP_CMD_TYPE = 0x02,
	SRP_RSP_TYPE = 0xC1,
	SRP_CRED_REQ_TYPE = 0x81,
	SRP_CRED_RSP_TYPE = 0x41,
	SRP_AER_REQ_TYPE = 0x82,
	SRP_AER_RSP_TYPE = 0x42
};

enum SRP_DESCRIPTOR_FORMATS {
	SRP_NO_BUFFER = 0x00,
	SRP_DIRECT_BUFFER = 0x01,
	SRP_INDIRECT_BUFFER = 0x02
};

struct memory_descriptor {
	u32 reserved;
	u32 virtual_address;
	u32 memory_handle;
	u32 length;
};

struct indirect_descriptor {
	struct memory_descriptor head;
	u32 total_length;
	struct memory_descriptor list[1];
};

struct SRP_GENERIC {
	u8 type;
	u8 reserved1[7];
	u64 tag;
};

struct SRP_LOGIN_REQ {
	u8 type;
	u8 reserved1[7];
	u64 tag;
	u32 max_requested_initiator_to_target_iulen;
	u32 reserved2;
	u16 required_buffer_formats;
	u8 reserved3:6;
	u8 multi_channel_action:2;
	u8 reserved4;
	u32 reserved5;
	u8 initiator_port_identifier[16];
	u8 target_port_identifier[16];
};

struct SRP_LOGIN_RSP {
	u8 type;
	u8 reserved1[3];
	u32 request_limit_delta;
	u64 tag;
	u32 max_initiator_to_target_iulen;
	u32 max_target_to_initiator_iulen;
	u16 supported_buffer_formats;
	u8 reserved2:6;
	u8 multi_channel_result:2;
	u8 reserved3;
	u8 reserved4[24];
};

struct SRP_LOGIN_REJ {
	u8 type;
	u8 reserved1[3];
	u32 reason;
	u64 tag;
	u64 reserved2;
	u16 supported_buffer_formats;
	u8 reserved3[6];
};

struct SRP_I_LOGOUT {
	u8 type;
	u8 reserved1[7];
	u64 tag;
};

struct SRP_T_LOGOUT {
	u8 type;
	u8 reserved1[3];
	u32 reason;
	u64 tag;
};

struct SRP_TSK_MGMT {
	u8 type;
	u8 reserved1[7];
	u64 tag;
	u32 reserved2;
	u64 lun PACKED;
	u8 reserved3;
	u8 reserved4;
	u8 task_mgmt_flags;
	u8 reserved5;
	u64 managed_task_tag;
	u64 reserved6;
};

struct SRP_CMD {
	u8 type;
	u32 reserved1 PACKED;
	u8 data_out_format:4;
	u8 data_in_format:4;
	u8 data_out_count;
	u8 data_in_count;
	u64 tag;
	u32 reserved2;
	u64 lun PACKED;
	u8 reserved3;
	u8 reserved4:5;
	u8 task_attribute:3;
	u8 reserved5;
	u8 additional_cdb_len;
	u8 cdb[16];
	u8 additional_data[0x100 - 0x30];
};

struct SRP_RSP {
	u8 type;
	u8 reserved1[3];
	u32 request_limit_delta;
	u64 tag;
	u16 reserved2;
	u8 reserved3:2;
	u8 diunder:1;
	u8 diover:1;
	u8 dounder:1;
	u8 doover:1;
	u8 snsvalid:1;
	u8 rspvalid:1;
	u8 status;
	u32 data_in_residual_count;
	u32 data_out_residual_count;
	u32 sense_data_list_length;
	u32 response_data_list_length;
	u8 sense_and_response_data[18];
};

struct SRP_CRED_REQ {
	u8 type;
	u8 reserved1[3];
	u32 request_limit_delta;
	u64 tag;
};

struct SRP_CRED_RSP {
	u8 type;
	u8 reserved1[7];
	u64 tag;
};

struct SRP_AER_REQ {
	u8 type;
	u8 reserved1[3];
	u32 request_limit_delta;
	u64 tag;
	u32 reserved2;
	u64 lun;
	u32 sense_data_list_length;
	u32 reserved3;
	u8 sense_data[20];
};

struct SRP_AER_RSP {
	u8 type;
	u8 reserved1[7];
	u64 tag;
};

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
