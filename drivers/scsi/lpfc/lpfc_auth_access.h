/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2006-2008 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#define FC_SC_REQ_TIMEOUT (60*HZ)

enum fc_sc_service_state {
	FC_SC_SERVICESTATE_UNKNOWN,
	FC_SC_SERVICESTATE_ONLINE,
	FC_SC_SERVICESTATE_OFFLINE,
	FC_SC_SERVICESTATE_ERROR,
};

struct fc_security_request {
	struct list_head rlist;
	int pid;
	u32 tran_id;
	u32 req_type;
	struct timer_list timer;
	struct lpfc_vport *vport;
	u32 data_len;
	void *data;
};

struct fc_sc_msg_work_q_wrapper {
	struct work_struct work;
	struct fc_security_request *fc_sc_req;
	u32 data_len;
	int status;
	u32 msgtype;
};
struct fc_sc_notify_work_q_wrapper {
	struct work_struct work;
	struct Scsi_Host *shost;
	int msg;
};

#define FC_DHCHAP	1
#define FC_FCAP		2
#define FC_FCPAP	3
#define FC_KERBEROS	4

#define FC_AUTHMODE_UNKNOWN	0
#define FC_AUTHMODE_NONE	1
#define FC_AUTHMODE_ACTIVE	2
#define FC_AUTHMODE_PASSIVE	3

#define FC_SP_HASH_MD5  0x5
#define FC_SP_HASH_SHA1 0x6

#define DH_GROUP_NULL	0x00
#define DH_GROUP_1024	0x01
#define DH_GROUP_1280	0x02
#define DH_GROUP_1536	0x03
#define DH_GROUP_2048	0x04

#define MAX_AUTH_REQ_SIZE 1024
#define MAX_AUTH_RSP_SIZE 1024

#define AUTH_FABRIC_WWN	0xFFFFFFFFFFFFFFFFLL

struct fc_auth_req {
	uint64_t local_wwpn;
	uint64_t remote_wwpn;
	union {
		struct dhchap_challenge_req {
			uint32_t transaction_id;
			uint32_t dh_group_id;
			uint32_t hash_id;
		} dhchap_challenge;
		struct dhchap_reply_req {
			uint32_t transaction_id;
			uint32_t dh_group_id;
			uint32_t hash_id;
			uint32_t bidirectional;
			uint32_t received_challenge_len;
			uint32_t received_public_key_len;
			uint8_t  data[0];
		} dhchap_reply;
		struct dhchap_success_req {
			uint32_t transaction_id;
			uint32_t dh_group_id;
			uint32_t hash_id;
			uint32_t our_challenge_len;
			uint32_t received_response_len;
			uint32_t received_public_key_len;
			uint32_t received_challenge_len;
			uint8_t  data[0];
		} dhchap_success;
	} u;
} __attribute__ ((packed));

struct fc_auth_rsp {
	uint64_t local_wwpn;
	uint64_t remote_wwpn;
	union {
		struct authinfo {
			uint8_t  auth_mode;
			uint16_t auth_timeout;
			uint8_t  bidirectional;
			uint8_t  type_priority[4];
			uint16_t type_len;
			uint8_t  hash_priority[4];
			uint16_t hash_len;
			uint8_t  dh_group_priority[8];
			uint16_t dh_group_len;
			uint32_t reauth_interval;
		} dhchap_security_config;
		struct dhchap_challenge_rsp {
			uint32_t transaction_id;
			uint32_t our_challenge_len;
			uint32_t our_public_key_len;
			uint8_t  data[0];
		} dhchap_challenge;
		struct dhchap_reply_rsp {
			uint32_t transaction_id;
			uint32_t our_challenge_rsp_len;
			uint32_t our_public_key_len;
			uint32_t our_challenge_len;
			uint8_t  data[0];
		} dhchap_reply;
		struct dhchap_success_rsp {
			uint32_t transaction_id;
			uint32_t authenticated;
			uint32_t response_len;
			uint8_t  data[0];
		} dhchap_success;
	} u;
} __attribute__ ((packed));

int
lpfc_fc_security_get_config(struct Scsi_Host *shost,
			struct fc_auth_req *auth_req,
			u32 req_len,
			struct fc_auth_rsp *auth_rsp,
			u32 rsp_len);
int
lpfc_fc_security_dhchap_make_challenge(struct Scsi_Host *shost,
			struct fc_auth_req *auth_req,
			u32 req_len,
			struct fc_auth_rsp *auth_rsp,
			u32 rsp_len);
int
lpfc_fc_security_dhchap_make_response(struct Scsi_Host *shost,
			struct fc_auth_req *auth_req,
			u32 req_len,
			struct fc_auth_rsp *auth_rsp,
			u32 rsp_len);
int
lpfc_fc_security_dhchap_authenticate(struct Scsi_Host *shost,
			struct fc_auth_req *auth_req,
			u32 req_len,
			struct fc_auth_rsp *auth_rsp,
			u32 rsp_len);

int lpfc_fc_queue_security_work(struct lpfc_vport *,
		struct work_struct *);

/*
 * FC Transport Message Types
 */
	/* user -> kernel */
#define FC_NL_EVENTS_REG			0x0001
#define FC_NL_EVENTS_DEREG			0x0002
#define FC_NL_SC_REG				0x0003
#define FC_NL_SC_DEREG				0x0004
#define FC_NL_SC_GET_CONFIG_RSP			0x0005
#define FC_NL_SC_SET_CONFIG_RSP			0x0006
#define FC_NL_SC_DHCHAP_MAKE_CHALLENGE_RSP	0x0007
#define FC_NL_SC_DHCHAP_MAKE_RESPONSE_RSP	0x0008
#define FC_NL_SC_DHCHAP_AUTHENTICATE_RSP	0x0009
	/* kernel -> user */
/* #define FC_NL_ASYNC_EVENT			0x0100 */
#define FC_NL_SC_GET_CONFIG_REQ			0x0020
#define FC_NL_SC_SET_CONFIG_REQ			0x0030
#define FC_NL_SC_DHCHAP_MAKE_CHALLENGE_REQ	0x0040
#define FC_NL_SC_DHCHAP_MAKE_RESPONSE_REQ	0x0050
#define FC_NL_SC_DHCHAP_AUTHENTICATE_REQ	0x0060

/*
 * Message Structures :
 */

/* macro to round up message lengths to 8byte boundary */
#define FC_NL_MSGALIGN(len)		(((len) + 7) & ~7)

#define FC_NETLINK_API_VERSION		1

/* Single Netlink Message type to send all FC Transport messages */
#define FC_TRANSPORT_MSG		(NLMSG_MIN_TYPE + 1)

/* SCSI_TRANSPORT_MSG event message header */
/*
struct scsi_nl_hdr {
	uint8_t version;
	uint8_t transport;
	uint16_t magic;
	uint16_t msgtype;
	uint16_t msglen;
} __attribute__((aligned(sizeof(uint64_t))));
*/
struct fc_nl_sc_message {
	uint16_t msgtype;
	uint16_t rsvd;
	uint32_t tran_id;
	uint32_t data_len;
	uint8_t data[0];
} __attribute__((aligned(sizeof(uint64_t))));

