/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 *  $Id: hci.h,v 1.15 2001/08/05 06:02:15 maxk Exp $
 */

#ifndef __HCI_H
#define __HCI_H

#include <asm/byteorder.h>

#define HCI_MAX_DEV 	8
#define HCI_MAX_FRAME_SIZE	2048

/* HCI dev events */
#define HCI_DEV_REG	1
#define HCI_DEV_UNREG   2
#define HCI_DEV_UP	3
#define HCI_DEV_DOWN	4

/* HCI device types */
#define HCI_UART 	0
#define HCI_USB		1
#define HCI_VHCI	2

/* HCI device modes */
#define HCI_NORMAL 	0x0001
#define HCI_RAW		0x0002
#define HCI_MODE_MASK   (HCI_NORMAL | HCI_RAW)
#define HCI_SOCK	0x1000

/* HCI device states */
#define HCI_INIT	0x0010
#define HCI_UP 		0x0020
#define HCI_RUNNING	0x0040

/* HCI device flags */
#define HCI_PSCAN	0x0100
#define HCI_ISCAN	0x0200
#define HCI_AUTH	0x0400

/* HCI Ioctl defines */
#define HCIDEVUP        _IOW('H', 201, int)
#define HCIDEVDOWN      _IOW('H', 202, int)
#define HCIDEVRESET     _IOW('H', 203, int)
#define HCIRESETSTAT    _IOW('H', 204, int)
#define HCIGETINFO      _IOR('H', 205, int)
#define HCIGETDEVLIST   _IOR('H', 206, int)
#define HCISETRAW       _IOW('H', 207, int)
#define HCISETSCAN      _IOW('H', 208, int)
#define HCISETAUTH      _IOW('H', 209, int)
#define HCIINQUIRY      _IOR('H', 210, int)
#define HCISETPTYPE     _IOW('H', 211, int)
#define HCIGETCONNLIST  _IOR('H', 212, int)

#ifndef __NO_HCI_DEFS

/* HCI Packet types */
#define HCI_COMMAND_PKT		0x01
#define HCI_ACLDATA_PKT 	0x02
#define HCI_SCODATA_PKT 	0x03
#define HCI_EVENT_PKT		0x04
#define HCI_UNKNOWN_PKT		0xff

/* HCI Packet types */
#define HCI_DM1 	0x0008
#define HCI_DM3 	0x0400
#define HCI_DM5 	0x4000
#define HCI_DH1 	0x0010
#define HCI_DH3 	0x0800
#define HCI_DH5 	0x8000

/* ACL flags */
#define ACL_CONT		0x0001
#define ACL_START		0x0002
#define ACL_ACTIVE_BCAST	0x0010
#define ACL_PICO_BCAST		0x0020

/* Baseband links */
#define SCO_LINK	0x00
#define ACL_LINK	0x01

/* LMP features */
#define LMP_3SLOT	0x01
#define LMP_5SLOT	0x02
#define LMP_ENCRYPT	0x04
#define LMP_SOFFSET	0x08
#define LMP_TACCURACY	0x10
#define LMP_RSWITCH	0x20
#define LMP_HOLD	0x40
#define LMP_SNIF	0x80

#define LMP_PARK	0x01
#define LMP_RSSI	0x02
#define LMP_QUALITY	0x04
#define LMP_SCO		0x08
#define LMP_HV2		0x10
#define LMP_HV3		0x20
#define LMP_ULAW	0x40
#define LMP_ALAW	0x80

#define LMP_CVSD	0x01
#define LMP_PSCHEME	0x02
#define LMP_PCONTROL	0x04

/* -----  HCI Commands ----- */
/* OGF & OCF values */

/* Informational Parameters */
#define OGF_INFO_PARAM	0x04

#define OCF_READ_LOCAL_VERSION	0x0001
typedef struct {
	__u8  status;
	__u8  hci_ver;
	__u16 hci_rev;
	__u8  lmp_ver;
	__u16 man_name;
	__u16 lmp_sub;
} __attribute__ ((packed)) read_local_version_rp;

#define OCF_READ_LOCAL_FEATURES	0x0003
typedef struct {
	__u8 status;
	__u8 features[8];
} __attribute__ ((packed)) read_local_features_rp;

#define OCF_READ_BUFFER_SIZE	0x0005
typedef struct {
	__u8 	status;
	__u16 	acl_mtu;
	__u8 	sco_mtu;
	__u16 	acl_max_pkt;
	__u16	sco_max_pkt;
} __attribute__ ((packed)) read_buffer_size_rp;

#define OCF_READ_BD_ADDR	0x0009
typedef struct {
	__u8 status;
	bdaddr_t bdaddr;
} __attribute__ ((packed)) read_bd_addr_rp;

/* Host Controller and Baseband */
#define OGF_HOST_CTL	0x03
#define OCF_RESET		0x0003
#define OCF_WRITE_AUTH_ENABLE	0x0020
	#define AUTH_DISABLED			0x00
	#define AUTH_ENABLED			0x01

#define OCF_WRITE_CA_TIMEOUT  	0x0016	
#define OCF_WRITE_PG_TIMEOUT  	0x0018

#define OCF_WRITE_SCAN_ENABLE 	0x001A
	#define SCANS_DISABLED		0x00
	#define IS_ENA_PS_DIS		0x01
	#define IS_DIS_PS_ENA		0x02
	#define IS_ENA_PS_ENA		0x03

#define OCF_SET_EVENT_FLT	0x0005
typedef struct {
	__u8 	flt_type;
	__u8 	cond_type;
	__u8 	condition[0];
} __attribute__ ((packed)) set_event_flt_cp;
#define SET_EVENT_FLT_CP_SIZE 2

/* Filter types */
#define FLT_CLEAR_ALL	0x00
#define FLT_INQ_RESULT	0x01
#define FLT_CONN_SETUP	0x02

/* CONN_SETUP Condition types */
#define CONN_SETUP_ALLOW_ALL	0x00
#define CONN_SETUP_ALLOW_CLASS	0x01
#define CONN_SETUP_ALLOW_BDADDR	0x02

/* CONN_SETUP Conditions */
#define CONN_SETUP_AUTO_OFF	0x01
#define CONN_SETUP_AUTO_ON	0x02

#define OCF_CHANGE_LOCAL_NAME	0x0013
typedef struct {
	__u8 	name[248];
} __attribute__ ((packed)) change_local_name_cp;
#define CHANGE_LOCAL_NAME_CP_SIZE 248 

#define OCF_READ_LOCAL_NAME	0x0014
typedef struct {
	__u8	status;
	__u8 	name[248];
} __attribute__ ((packed)) read_local_name_rp;
#define READ_LOCAL_NAME_RP_SIZE 249 

#define OCF_READ_CLASS_OF_DEV	0x0023
typedef struct {
	__u8	status;
	__u8 	dev_class[3];
} __attribute__ ((packed)) read_class_of_dev_rp;
#define READ_CLASS_OF_DEV_RP_SIZE 4 

#define OCF_WRITE_CLASS_OF_DEV	0x0024
typedef struct {
	__u8 	dev_class[3];
} __attribute__ ((packed)) write_class_of_dev_cp;
#define WRITE_CLASS_OF_DEV_CP_SIZE 3

/* Link Control */
#define OGF_LINK_CTL	0x01 
#define OCF_CREATE_CONN	0x0005
typedef struct {
	bdaddr_t bdaddr;
	__u16 	pkt_type;
	__u8 	pscan_rep_mode;
	__u8 	pscan_mode;
	__u16 	clock_offset;
	__u8 	role_switch;
} __attribute__ ((packed)) create_conn_cp;
#define CREATE_CONN_CP_SIZE 13

#define OCF_ACCEPT_CONN_REQ	0x0009
typedef struct {
	bdaddr_t bdaddr;
	__u8 	role;
} __attribute__ ((packed)) accept_conn_req_cp;
#define ACCEPT_CONN_REQ_CP_SIZE	7

#define OCF_DISCONNECT	0x0006
typedef struct {
	__u16 	handle;
	__u8 	reason;
} __attribute__ ((packed)) disconnect_cp;
#define DISCONNECT_CP_SIZE 3

#define OCF_INQUIRY		0x0001
typedef struct {
	__u8 	lap[3];
	__u8 	lenght;
	__u8	num_rsp;
} __attribute__ ((packed)) inquiry_cp;
#define INQUIRY_CP_SIZE 5

#define OGF_LINK_POLICY	 	0x02   /* Link Policy */

/* --------- HCI Events --------- */
#define EVT_INQUIRY_COMPLETE 	0x01

#define EVT_INQUIRY_RESULT 	0x02
typedef struct {
	bdaddr_t	bdaddr;
	__u8	pscan_rep_mode;
	__u8	pscan_period_mode;
	__u8	pscan_mode;
	__u8	class[3];
	__u16	clock_offset;
} __attribute__ ((packed)) inquiry_info;
#define INQUIRY_INFO_SIZE 14

#define EVT_CONN_COMPLETE 	0x03
typedef struct {
	__u8	status;
	__u16	handle;
	bdaddr_t	bdaddr;
	__u8	link_type;
	__u8	encr_mode;
} __attribute__ ((packed)) evt_conn_complete;
#define EVT_CONN_COMPLETE_SIZE 13

#define EVT_CONN_REQUEST	0x04
typedef struct {
	bdaddr_t 	bdaddr;
	__u8 		dev_class[3];
	__u8		link_type;
} __attribute__ ((packed)) evt_conn_request;
#define EVT_CONN_REQUEST_SIZE 10

#define EVT_DISCONN_COMPLETE	0x05
typedef struct {
	__u8 	status;
	__u16 	handle;
	__u8 	reason;
} __attribute__ ((packed)) evt_disconn_complete;
#define EVT_DISCONN_COMPLETE_SIZE 4

#define EVT_CMD_COMPLETE 	0x0e
typedef struct {
	__u8 	ncmd;
	__u16 	opcode;
} __attribute__ ((packed)) evt_cmd_complete;
#define EVT_CMD_COMPLETE_SIZE 3

#define EVT_CMD_STATUS 		0x0f
typedef struct {
	__u8 	status;
	__u8 	ncmd;
	__u16 	opcode;
} __attribute__ ((packed)) evt_cmd_status;
#define EVT_CMD_STATUS_SIZE 4

#define EVT_NUM_COMP_PKTS	0x13
typedef struct {
	__u8 	num_hndl;
	/* variable lenght part */
} __attribute__ ((packed)) evt_num_comp_pkts;
#define EVT_NUM_COMP_PKTS_SIZE 1

#define EVT_HCI_DEV_EVENT	0xfd
typedef struct {
	__u16 	event;
	__u16 	param;
} __attribute__ ((packed)) evt_hci_dev_event;
#define EVT_HCI_DEV_EVENT_SIZE 4

/* --------  HCI Packet structures  -------- */
#define HCI_TYPE_LEN	1

typedef struct {
	__u16 	opcode;		/* OCF & OGF */
	__u8 	plen;
} __attribute__ ((packed))	hci_command_hdr;
#define HCI_COMMAND_HDR_SIZE 	3

typedef struct {
	__u8 	evt;
	__u8 	plen;
} __attribute__ ((packed))	hci_event_hdr;
#define HCI_EVENT_HDR_SIZE 	2

typedef struct {
	__u16 	handle;		/* Handle & Flags(PB, BC) */
	__u16 	dlen;
} __attribute__ ((packed))	hci_acl_hdr;
#define HCI_ACL_HDR_SIZE 	4

typedef struct {
	__u16 	handle;
	__u8 	dlen;
} __attribute__ ((packed))	hci_sco_hdr;
#define HCI_SCO_HDR_SIZE 	3

/* Command opcode pack/unpack */
#define cmd_opcode_pack(ogf, ocf)	(__u16)((ocf & 0x03ff)|(ogf << 10))
#define cmd_opcode_ogf(op)		(op >> 10)
#define cmd_opcode_ocf(op)		(op & 0x03ff)

/* ACL handle and flags pack/unpack */
#define acl_handle_pack(h, f)	(__u16)((h & 0x0fff)|(f << 12))
#define acl_handle(h)		(h & 0x0fff)
#define acl_flags(h)		(h >> 12)

#endif /* _NO_HCI_DEFS */

/* HCI Socket options */
#define HCI_DATA_DIR	0x0001
#define HCI_FILTER	0x0002

/* HCI CMSG flags */
#define HCI_CMSG_DIR	0x0001

struct sockaddr_hci {
	sa_family_t    hci_family;
	unsigned short hci_dev;
};
#define HCI_DEV_NONE	0xffff

struct hci_filter {
	__u32 type_mask;
	__u32 event_mask[2];
};

struct hci_dev_req {
	__u16 dev_id;
	__u32 dev_opt;
};

struct hci_dev_list_req {
	__u16  dev_num;
	struct hci_dev_req dev_req[0];	/* hci_dev_req structures */
};

struct hci_inquiry_req {
	__u16 dev_id;
	__u16 flags;
	__u8  lap[3];
	__u8  length;
	__u8  num_rsp;
};
#define IREQ_CACHE_FLUSH 0x0001

struct hci_dev_stats {
	__u32 err_rx;
	__u32 err_tx;
	__u32 cmd_tx;
	__u32 evt_rx;
	__u32 acl_tx;
	__u32 acl_rx;
	__u32 sco_tx;
	__u32 sco_rx;
	__u32 byte_rx;
	__u32 byte_tx;
};

struct hci_dev_info {
	__u16 dev_id;
	char  name[8];

	bdaddr_t bdaddr;

	__u32 flags;
	__u8  type;

	__u8  features[8];

	__u32 pkt_type;

	__u16 acl_mtu;
	__u16 acl_max;
	__u16 sco_mtu;
	__u16 sco_max;

	struct hci_dev_stats stat;
};

struct hci_conn_info {
	__u16    handle;
	bdaddr_t bdaddr;
};

struct hci_conn_list_req {
	__u16  dev_id;
	__u16  conn_num;
	struct hci_conn_info conn_info[0];
};

#endif /* __HCI_H */
