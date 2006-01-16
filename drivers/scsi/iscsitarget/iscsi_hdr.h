/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#ifndef __ISCSI_HDR_H__
#define __ISCSI_HDR_H__

#include <linux/types.h>
#include <asm/byteorder.h>

#define ISCSI_VERSION			0

#define __packed __attribute__ ((packed))

struct iscsi_hdr {
	u8  opcode;			/* 0 */
	u8  flags;
	u8  spec1[2];
#if defined(__BIG_ENDIAN_BITFIELD)
	struct {			/* 4 */
		unsigned ahslength : 8;
		unsigned datalength : 24;
	} length;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u32 length;			/* 4 */
#endif
	u16 lun[4];			/* 8 */
	u32 itt;			/* 16 */
	u32 ttt;			/* 20 */
	u32 sn;				/* 24 */
	u32 exp_sn;			/* 28 */
	u32 max_sn;			/* 32 */
	u32 spec3[3];			/* 36 */
} __packed;				/* 48 */

/* Opcode encoding bits */
#define ISCSI_OP_RETRY			0x80
#define ISCSI_OP_IMMEDIATE		0x40
#define ISCSI_OPCODE_MASK		0x3F

/* Client to Server Message Opcode values */
#define ISCSI_OP_NOOP_OUT		0x00
#define ISCSI_OP_SCSI_CMD		0x01
#define ISCSI_OP_SCSI_TASK_MGT_MSG	0x02
#define ISCSI_OP_LOGIN_CMD		0x03
#define ISCSI_OP_TEXT_CMD		0x04
#define ISCSI_OP_SCSI_DATA_OUT		0x05
#define ISCSI_OP_LOGOUT_CMD		0x06
#define ISCSI_OP_SNACK_CMD		0x10

#define ISCSI_OP_VENDOR1_CMD		0x1c
#define ISCSI_OP_VENDOR2_CMD		0x1d
#define ISCSI_OP_VENDOR3_CMD		0x1e
#define ISCSI_OP_VENDOR4_CMD		0x1f

/* Server to Client Message Opcode values */
#define ISCSI_OP_NOOP_IN		0x20
#define ISCSI_OP_SCSI_RSP		0x21
#define ISCSI_OP_SCSI_TASK_MGT_RSP	0x22
#define ISCSI_OP_LOGIN_RSP		0x23
#define ISCSI_OP_TEXT_RSP		0x24
#define ISCSI_OP_SCSI_DATA_IN		0x25
#define ISCSI_OP_LOGOUT_RSP		0x26
#define ISCSI_OP_R2T			0x31
#define ISCSI_OP_ASYNC_MSG		0x32
#define ISCSI_OP_REJECT			0x3f

struct iscsi_ahs_hdr {
	u16 ahslength;
	u8 ahstype;
} __packed;

#define ISCSI_AHSTYPE_CDB		1
#define ISCSI_AHSTYPE_RLENGTH		2

union iscsi_sid {
	struct {
		u8 isid[6];		/* Initiator Session ID */
		u16 tsih;		/* Target Session ID */
	} id;
	u64 id64;
} __packed;

struct iscsi_scsi_cmd_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u16 lun[4];
	u32 itt;
	u32 data_length;
	u32 cmd_sn;
	u32 exp_stat_sn;
	u8  scb[16];
} __packed;

#define ISCSI_CMD_FINAL		0x80
#define ISCSI_CMD_READ		0x40
#define ISCSI_CMD_WRITE		0x20
#define ISCSI_CMD_ATTR_MASK	0x07
#define ISCSI_CMD_UNTAGGED	0x00
#define ISCSI_CMD_SIMPLE	0x01
#define ISCSI_CMD_ORDERED	0x02
#define ISCSI_CMD_HEAD_OF_QUEUE	0x03
#define ISCSI_CMD_ACA		0x04

struct iscsi_cdb_ahdr {
	u16 ahslength;
	u8  ahstype;
	u8  reserved;
	u8  cdb[0];
} __packed;

struct iscsi_rlength_ahdr {
	u16 ahslength;
	u8  ahstype;
	u8  reserved;
	u32 read_length;
} __packed;

struct iscsi_scsi_rsp_hdr {
	u8  opcode;
	u8  flags;
	u8  response;
	u8  cmd_status;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd1[2];
	u32 itt;
	u32 snack;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 exp_data_sn;
	u32 bi_residual_count;
	u32 residual_count;
} __packed;

#define ISCSI_FLG_RESIDUAL_UNDERFLOW		0x02
#define ISCSI_FLG_RESIDUAL_OVERFLOW		0x04
#define ISCSI_FLG_BIRESIDUAL_UNDERFLOW		0x08
#define ISCSI_FLG_BIRESIDUAL_OVERFLOW		0x10

#define ISCSI_RESPONSE_COMMAND_COMPLETED	0x00
#define ISCSI_RESPONSE_TARGET_FAILURE		0x01

struct iscsi_sense_data {
	u16 length;
	u8  data[0];
} __packed;

struct iscsi_task_mgt_hdr {
	u8  opcode;
	u8  function;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u16 lun[4];
	u32 itt;
	u32 rtt;
	u32 cmd_sn;
	u32 exp_stat_sn;
	u32 ref_cmd_sn;
	u32 exp_data_sn;
	u32 rsvd2[2];
} __packed;

#define ISCSI_FUNCTION_MASK			0x7f

#define ISCSI_FUNCTION_ABORT_TASK		1
#define ISCSI_FUNCTION_ABORT_TASK_SET		2
#define ISCSI_FUNCTION_CLEAR_ACA		3
#define ISCSI_FUNCTION_CLEAR_TASK_SET		4
#define ISCSI_FUNCTION_LOGICAL_UNIT_RESET	5
#define ISCSI_FUNCTION_TARGET_WARM_RESET	6
#define ISCSI_FUNCTION_TARGET_COLD_RESET	7
#define ISCSI_FUNCTION_TASK_REASSIGN		8

struct iscsi_task_rsp_hdr {
	u8  opcode;
	u8  flags;
	u8  response;
	u8  rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 itt;
	u32 rsvd3;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 rsvd4[3];
} __packed;

#define ISCSI_RESPONSE_FUNCTION_COMPLETE	0
#define ISCSI_RESPONSE_UNKNOWN_TASK		1
#define ISCSI_RESPONSE_UNKNOWN_LUN		2
#define ISCSI_RESPONSE_TASK_ALLEGIANT		3
#define ISCSI_RESPONSE_FAILOVER_UNSUPPORTED	4
#define ISCSI_RESPONSE_FUNCTION_UNSUPPORTED	5
#define ISCSI_RESPONSE_NO_AUTHORIZATION		6
#define ISCSI_RESPONSE_FUNCTION_REJECTED	255

struct iscsi_data_out_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u16 lun[4];
	u32 itt;
	u32 ttt;
	u32 rsvd2;
	u32 exp_stat_sn;
	u32 rsvd3;
	u32 data_sn;
	u32 buffer_offset;
	u32 rsvd4;
} __packed;

struct iscsi_data_in_hdr {
	u8  opcode;
	u8  flags;
	u8  rsvd1;
	u8  cmd_status;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 itt;
	u32 ttt;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 data_sn;
	u32 buffer_offset;
	u32 residual_count;
} __packed;

#define ISCSI_FLG_STATUS		0x01

struct iscsi_r2t_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u16 lun[4];
	u32 itt;
	u32 ttt;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 r2t_sn;
	u32 buffer_offset;
	u32 data_length;
} __packed;

struct iscsi_async_msg_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u16 lun[4];
	u32 ffffffff;
	u32 rsvd2;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u8  async_event;
	u8  async_vcode;
	u16 param1;
	u16 param2;
	u16 param3;
	u32 rsvd3;
} __packed;

#define ISCSI_ASYNC_SCSI		0
#define ISCSI_ASYNC_LOGOUT		1
#define ISCSI_ASYNC_DROP_CONNECTION	2
#define ISCSI_ASYNC_DROP_SESSION	3
#define ISCSI_ASYNC_PARAM_REQUEST	4
#define ISCSI_ASYNC_VENDOR		255

struct iscsi_text_req_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 itt;
	u32 ttt;
	u32 cmd_sn;
	u32 exp_stat_sn;
	u32 rsvd3[4];
} __packed;

struct iscsi_text_rsp_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 itt;
	u32 ttt;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 rsvd3[3];
} __packed;

struct iscsi_login_req_hdr {
	u8  opcode;
	u8  flags;
	u8  max_version;		/* Max. version supported */
	u8  min_version;		/* Min. version supported */
	u8  ahslength;
	u8  datalength[3];
	union iscsi_sid sid;
	u32 itt;			/* Initiator Task Tag */
	u16 cid;			/* Connection ID */
	u16 rsvd1;
	u32 cmd_sn;
	u32 exp_stat_sn;
	u32 rsvd2[4];
} __packed;

struct iscsi_login_rsp_hdr {
	u8  opcode;
	u8  flags;
	u8  max_version;		/* Max. version supported */
	u8  active_version;		/* Active version */
	u8  ahslength;
	u8  datalength[3];
	union iscsi_sid sid;
	u32 itt;			/* Initiator Task Tag */
	u32 rsvd1;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u8  status_class;		/* see Login RSP ststus classes below */
	u8  status_detail;		/* see Login RSP Status details below */
	u8  rsvd2[10];
} __packed;

#define ISCSI_FLG_FINAL			0x80
#define ISCSI_FLG_TRANSIT		0x80
#define ISCSI_FLG_CSG_SECURITY		0x00
#define ISCSI_FLG_CSG_LOGIN		0x04
#define ISCSI_FLG_CSG_FULL_FEATURE	0x0c
#define ISCSI_FLG_CSG_MASK		0x0c
#define ISCSI_FLG_NSG_SECURITY		0x00
#define ISCSI_FLG_NSG_LOGIN		0x01
#define ISCSI_FLG_NSG_FULL_FEATURE	0x03
#define ISCSI_FLG_NSG_MASK		0x03

/* Login Status response classes */
#define ISCSI_STATUS_SUCCESS		0x00
#define ISCSI_STATUS_REDIRECT		0x01
#define ISCSI_STATUS_INITIATOR_ERR	0x02
#define ISCSI_STATUS_TARGET_ERR		0x03

/* Login Status response detail codes */
/* Class-0 (Success) */
#define ISCSI_STATUS_ACCEPT		0x00

/* Class-1 (Redirection) */
#define ISCSI_STATUS_TGT_MOVED_TEMP	0x01
#define ISCSI_STATUS_TGT_MOVED_PERM	0x02

/* Class-2 (Initiator Error) */
#define ISCSI_STATUS_INIT_ERR		0x00
#define ISCSI_STATUS_AUTH_FAILED	0x01
#define ISCSI_STATUS_TGT_FORBIDDEN	0x02
#define ISCSI_STATUS_TGT_NOT_FOUND	0x03
#define ISCSI_STATUS_TGT_REMOVED	0x04
#define ISCSI_STATUS_NO_VERSION		0x05
#define ISCSI_STATUS_TOO_MANY_CONN	0x06
#define ISCSI_STATUS_MISSING_FIELDS	0x07
#define ISCSI_STATUS_CONN_ADD_FAILED	0x08
#define ISCSI_STATUS_INV_SESSION_TYPE	0x09
#define ISCSI_STATUS_SESSION_NOT_FOUND	0x0a
#define ISCSI_STATUS_INV_REQ_TYPE	0x0b

/* Class-3 (Target Error) */
#define ISCSI_STATUS_TARGET_ERROR	0x00
#define ISCSI_STATUS_SVC_UNAVAILABLE	0x01
#define ISCSI_STATUS_NO_RESOURCES	0x02

struct iscsi_logout_req_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 itt;
	u16 cid;
	u16 rsvd3;
	u32 cmd_sn;
	u32 exp_stat_sn;
	u32 rsvd4[4];
} __packed;

struct iscsi_logout_rsp_hdr {
	u8  opcode;
	u8  flags;
	u8  response;
	u8  rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 itt;
	u32 rsvd3;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 rsvd4;
	u16 time2wait;
	u16 time2retain;
	u32 rsvd5;
} __packed;

struct iscsi_snack_req_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 itt;
	u32 ttt;
	u32 rsvd3;
	u32 exp_stat_sn;
	u32 rsvd4[2];
	u32 beg_run;
	u32 run_length;
} __packed;

struct iscsi_reject_hdr {
	u8  opcode;
	u8  flags;
	u8  reason;
	u8  rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u32 rsvd2[2];
	u32 ffffffff;
	u32 rsvd3;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 data_sn;
	u32 rsvd4[2];
} __packed;

#define ISCSI_REASON_NO_FULL_FEATURE_PHASE	0x01
#define ISCSI_REASON_DATA_DIGEST_ERROR		0x02
#define ISCSI_REASON_DATA_SNACK_REJECT		0x03
#define ISCSI_REASON_PROTOCOL_ERROR		0x04
#define ISCSI_REASON_UNSUPPORTED_COMMAND	0x05
#define ISCSI_REASON_IMMEDIATE_COMMAND_REJECT	0x06
#define ISCSI_REASON_TASK_IN_PROGRESS		0x07
#define ISCSI_REASON_INVALID_SNACK		0x08
#define ISCSI_REASON_NO_BOOKMARK		0x09
#define ISCSI_REASON_BOOKMARK_REJECT		0x0a
#define ISCSI_REASON_NEGOTIATION_RESET		0x0b
#define ISCSI_REASON_WAITING_LOGOUT		0x0c


struct iscsi_nop_out_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u16 lun[4];
	u32 itt;
	u32 ttt;
	u32 cmd_sn;
	u32 exp_stat_sn;
	u32 rsvd2[4];
} __packed;

struct iscsi_nop_in_hdr {
	u8  opcode;
	u8  flags;
	u16 rsvd1;
	u8  ahslength;
	u8  datalength[3];
	u16 lun[4];
	u32 itt;
	u32 ttt;
	u32 stat_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 rsvd2[3];
} __packed;

#define ISCSI_RESERVED_TAG	(0xffffffffU)

#endif	/* __ISCSI_HDR_H__ */
