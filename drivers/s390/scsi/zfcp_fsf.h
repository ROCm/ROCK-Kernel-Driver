/* 
 * 
 * linux/drivers/s390/scsi/zfcp_fsf.h
 * 
 * FCP adapter driver for IBM eServer zSeries 
 * 
 * Copyright 2002 IBM Corporation 
 * Author(s): Martin Peschke <mpeschke@de.ibm.com> 
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com> 
 *            Aron Zeh <arzeh@de.ibm.com> 
 *            Wolfgang Taphorn <taphorn@de.ibm.com> 
 *            Stefan Bader <stefan.bader@de.ibm.com> 
 *            Heiko Carstens <heiko.carstens@de.ibm.com> 
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */

#ifndef FSF_H
#define FSF_H

#ifdef __KERNEL__

#define FSF_QTCB_VERSION1			0x00000001
#define FSF_QTCB_CURRENT_VERSION		FSF_QTCB_VERSION1

/* FSF commands */
#define	FSF_QTCB_FCP_CMND			0x00000001
#define	FSF_QTCB_ABORT_FCP_CMND			0x00000002
#define	FSF_QTCB_OPEN_PORT_WITH_DID		0x00000005
#define	FSF_QTCB_OPEN_LUN			0x00000006
#define	FSF_QTCB_CLOSE_LUN			0x00000007
#define	FSF_QTCB_CLOSE_PORT			0x00000008
#define	FSF_QTCB_CLOSE_PHYSICAL_PORT		0x00000009
#define	FSF_QTCB_SEND_ELS			0x0000000B
#define	FSF_QTCB_SEND_GENERIC			0x0000000C
#define	FSF_QTCB_EXCHANGE_CONFIG_DATA		0x0000000D

/* FSF QTCB types */
#define FSF_IO_COMMAND				0x00000001
#define FSF_SUPPORT_COMMAND			0x00000002
#define FSF_CONFIG_COMMAND			0x00000003

/* FSF protocol stati */
#define FSF_PROT_GOOD				0x00000001
#define FSF_PROT_QTCB_VERSION_ERROR		0x00000010
#define FSF_PROT_SEQ_NUMB_ERROR			0x00000020
#define FSF_PROT_UNSUPP_QTCB_TYPE		0x00000040
#define FSF_PROT_HOST_CONNECTION_INITIALIZING	0x00000080
#define FSF_PROT_FSF_STATUS_PRESENTED		0x00000100
#define FSF_PROT_DUPLICATE_REQUEST_ID		0x00000200
#define FSF_PROT_LINK_DOWN                      0x00000400
#define FSF_PROT_REEST_QUEUE                    0x00000800
#define FSF_PROT_ERROR_STATE			0x01000000

/* FSF stati */
#define FSF_GOOD				0x00000000
#define FSF_PORT_ALREADY_OPEN			0x00000001
#define FSF_LUN_ALREADY_OPEN			0x00000002
#define FSF_PORT_HANDLE_NOT_VALID		0x00000003
#define FSF_LUN_HANDLE_NOT_VALID		0x00000004
#define FSF_HANDLE_MISMATCH			0x00000005
#define FSF_SERVICE_CLASS_NOT_SUPPORTED		0x00000006
#define FSF_FCPLUN_NOT_VALID			0x00000009
//#define FSF_ACCESS_DENIED                     0x00000010
#define FSF_ACCESS_TYPE_NOT_VALID		0x00000011
#define FSF_LUN_IN_USE				0x00000012
#define FSF_COMMAND_ABORTED_ULP			0x00000020
#define FSF_COMMAND_ABORTED_ADAPTER		0x00000021
#define FSF_FCP_COMMAND_DOES_NOT_EXIST		0x00000022
#define FSF_DIRECTION_INDICATOR_NOT_VALID	0x00000030
#define FSF_INBOUND_DATA_LENGTH_NOT_VALID	0x00000031 /* FIX: obsolete? */
#define FSF_OUTBOUND_DATA_LENGTH_NOT_VALID	0x00000032 /* FIX: obsolete? */
#define FSF_CMND_LENGTH_NOT_VALID		0x00000033
#define FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED	0x00000040
#define FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED	0x00000041
#define FSF_REQUEST_BUF_NOT_VALID		0x00000042
#define FSF_RESPONSE_BUF_NOT_VALID		0x00000043
#define FSF_ELS_COMMAND_REJECTED		0x00000050
#define FSF_GENERIC_COMMAND_REJECTED		0x00000051
//#define FSF_AUTHORIZATION_FAILURE             0x00000053
#define FSF_PORT_BOXED				0x00000059
//#define FSF_LUN_BOXED                         0x0000005A
#define FSF_ADAPTER_STATUS_AVAILABLE		0x000000AD
#define FSF_FCP_RSP_AVAILABLE			0x000000AF
#define FSF_UNKNOWN_COMMAND			0x000000E2
//#define FSF_ERROR                             0x000000FF 

#define FSF_STATUS_QUALIFIER_SIZE		16

/* FSF status qualifier, recommendations */
#define FSF_SQ_NO_RECOM				0x00
#define FSF_SQ_FCP_RSP_AVAILABLE		0x01
#define FSF_SQ_RETRY_IF_POSSIBLE		0x02
#define FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED	0x03
#define FSF_SQ_INVOKE_LINK_TEST_PROCEDURE	0x04
#define FSF_SQ_ULP_PROGRAMMING_ERROR		0x05
#define FSF_SQ_COMMAND_ABORTED			0x06
#define FSF_SQ_NO_RETRY_POSSIBLE		0x07

/* FSF status qualifier (most significant 4 bytes), local link down */
#define FSF_PSQ_LINK_NOLIGHT			0x00000004
#define FSF_PSQ_LINK_WRAPPLUG			0x00000008
#define FSF_PSQ_LINK_NOFCP			0x00000010

/* payload size in status read buffer */
#define FSF_STATUS_READ_PAYLOAD_SIZE		4032

/* number of status read buffers that should be sent by ULP */
#define FSF_STATUS_READS_RECOM			16

/* status types in status read buffer */
#define FSF_STATUS_READ_PORT_CLOSED		0x00000001
#define FSF_STATUS_READ_INCOMING_ELS		0x00000002
#define FSF_STATUS_READ_BIT_ERROR_THRESHOLD	0x00000004
#define FSF_STATUS_READ_LINK_DOWN		0x00000005 /* FIXME: really? */
#define FSF_STATUS_READ_LINK_UP          	0x00000006

/* status subtypes in status read buffer */
#define FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT	0x00000001
#define FSF_STATUS_READ_SUB_ERROR_PORT		0x00000002

/* topologie that is detected by the adapter */
#define FSF_TOPO_ERROR				0x00000000
#define FSF_TOPO_P2P				0x00000001
#define FSF_TOPO_FABRIC				0x00000002
#define FSF_TOPO_AL				0x00000003
#define FSF_TOPO_FABRIC_VIRT			0x00000004

/* data direction for FCP commands */
#define FSF_DATADIR_WRITE			0x00000001
#define FSF_DATADIR_READ			0x00000002
#define FSF_DATADIR_READ_WRITE			0x00000003
#define FSF_DATADIR_CMND			0x00000004

/* fc service class */
#define FSF_CLASS_1				0x00000001
#define FSF_CLASS_2				0x00000002
#define FSF_CLASS_3				0x00000003

/* SBAL chaining */
#define FSF_MAX_SBALS_PER_REQ			36

/* logging space behind QTCB */
#define FSF_QTCB_LOG_SIZE			1024

struct fsf_queue_designator;
struct fsf_status_read_buffer;
struct fsf_port_closed_payload;
struct fsf_bit_error_payload;
union  fsf_prot_status_qual;
struct fsf_qual_version_error;
struct fsf_qual_sequence_error;
struct fsf_qtcb_prefix;
struct fsf_qtcb_header;
struct fsf_qtcb_bottom_config;
struct fsf_qtcb_bottom_support;
struct fsf_qtcb_bottom_io;
union  fsf_qtcb_bottom;

struct fsf_queue_designator {
	u8  cssid;
	u8  chpid;
	u8  hla;
	u8  ua;
	u32 res1;
} __attribute__ ((packed));

struct fsf_port_closed_payload {
	struct fsf_queue_designator queue_designator;
	u32                         port_handle;
} __attribute__ ((packed));

struct fsf_bit_error_payload {
	u32 res1;
	u32 link_failure_error_count;
	u32 loss_of_sync_error_count;
	u32 loss_of_signal_error_count;
	u32 primitive_sequence_error_count;
	u32 invalid_transmission_word_error_count;
	u32 crc_error_count;
	u32 primitive_sequence_event_timeout_count;
	u32 elastic_buffer_overrun_error_count;
	u32 fcal_arbitration_timeout_count;
	u32 advertised_receive_b2b_credit;
	u32 current_receive_b2b_credit;
	u32 advertised_transmit_b2b_credit;
	u32 current_transmit_b2b_credit;
} __attribute__ ((packed));

struct fsf_status_read_buffer {
	u32 status_type;
	u32 status_subtype;
	u32 length;
	u32 res1;
	struct fsf_queue_designator queue_designator;
	u32 d_id;
	u32 class;
	u64 fcp_lun;
	u8  res3[24];
	u8  payload[FSF_STATUS_READ_PAYLOAD_SIZE];
} __attribute__ ((packed));

struct fsf_qual_version_error {
	u32 fsf_version;
	u32 res1[3];
} __attribute__ ((packed));

struct fsf_qual_sequence_error {
	u32 exp_req_seq_no;
	u32 res1[3];
} __attribute__ ((packed));

struct fsf_qual_locallink_error {
	u32 code;
	u32 res1[3];
} __attribute__ ((packed));

union fsf_prot_status_qual {
	struct fsf_qual_version_error   version_error;
	struct fsf_qual_sequence_error  sequence_error;
	struct fsf_qual_locallink_error locallink_error;
} __attribute__ ((packed));

struct fsf_qtcb_prefix {
	u64 req_id;
	u32 qtcb_version;
	u32 ulp_info;
	u32 qtcb_type;
	u32 req_seq_no;
	u32 prot_status;
	union fsf_prot_status_qual prot_status_qual;
	u8  res1[20];
} __attribute__ ((packed));

union fsf_status_qual {
	u8  byte[FSF_STATUS_QUALIFIER_SIZE];
	u16 halfword[FSF_STATUS_QUALIFIER_SIZE / sizeof (u16)];
	u32 word[FSF_STATUS_QUALIFIER_SIZE / sizeof (u32)];
	struct {
		u32 this_cmd;
		u32 aborted_cmd;
	} port_handle;
	struct {
		u32 this_cmd;
		u32 aborted_cmd;
	} lun_handle;
	struct {
		u64 found;
		u64 expected;
	} fcp_lun;
} __attribute__ ((packed));

struct fsf_qtcb_header {
	u64 req_handle;
	u32 fsf_command;
	u32 res1;
	u32 port_handle;
	u32 lun_handle;
	u32 res2;
	u32 fsf_status;
	union fsf_status_qual fsf_status_qual;
	u8  res3[28];
	u16 log_start;
	u16 log_length;
	u8  res4[16];
} __attribute__ ((packed));

struct fsf_nport_serv_param {
	u8  common_serv_param[16];
	u64 wwpn;
	u64 wwnn;
	u8  class1_serv_param[16];
	u8  class2_serv_param[16];
	u8  class3_serv_param[16];
	u8  class4_serv_param[16];
	u8  vendor_version_level[16];
	u8  res1[16];
} __attribute__ ((packed));

struct fsf_plogi {
	u32    code;
	struct fsf_nport_serv_param serv_param;
} __attribute__ ((packed));

#define FSF_FCP_CMND_SIZE	288
#define FSF_FCP_RSP_SIZE	128

struct fsf_qtcb_bottom_io {
	u32 data_direction;
	u32 service_class;
	u8  res1[8];
	u32 fcp_cmnd_length;
	u8  res2[12];
	u8  fcp_cmnd[FSF_FCP_CMND_SIZE];
	u8  fcp_rsp[FSF_FCP_RSP_SIZE];
	u8  res3[64];
} __attribute__ ((packed));

struct fsf_qtcb_bottom_support {
	u8  res1[16];
	u32 d_id;
	u32 res2;
	u64 fcp_lun;
	u64 res3;
	u64 req_handle;
	u32 service_class;
	u8  res4[3];
	u8  timeout;
	u8  res5[184];
	u32 els1_length;
	u32 els2_length;
	u64 res6;
	u8  els[256];
} __attribute__ ((packed));

struct fsf_qtcb_bottom_config {
	u32 lic_version;
	u32 res1;
	u32 high_qtcb_version;
	u32 low_qtcb_version;
	u32 max_qtcb_size;
	u8  res2[12];
	u32 fc_topology;
	u32 fc_link_speed;
	u32 adapter_type;
	u32 peer_d_id;
	u8  res3[12];
	u32 s_id;
	struct fsf_nport_serv_param nport_serv_param;
	u8  res4[320];
} __attribute__ ((packed));

union fsf_qtcb_bottom {
	struct fsf_qtcb_bottom_io      io;
	struct fsf_qtcb_bottom_support support;
	struct fsf_qtcb_bottom_config  config;
};

struct fsf_qtcb {
	struct fsf_qtcb_prefix prefix;
	struct fsf_qtcb_header header;
	union  fsf_qtcb_bottom bottom;
} __attribute__ ((packed));

#endif				/* __KERNEL__ */
#endif				/* FSF_H */
