/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef _QLA_NLNK_H_
#define _QLA_NLNK_H_

#ifndef NETLINK_FCTRANSPORT
#define NETLINK_FCTRANSPORT 20
#endif
#define QL_FC_NL_GROUP_CNT	0

#define FC_TRANSPORT_MSG		NLMSG_MIN_TYPE + 1

/*
 * Transport Message Types
 */
#define FC_NL_VNDR_SPECIFIC	0x8000

/*
 * Structures
 */

struct qla84_mgmt_param {
	union {
		struct {
			uint32_t start_addr;
		} mem; /* for QLA84_MGMT_READ/WRITE_MEM */
		struct {
			uint32_t id;
#define QLA84_MGMT_CONFIG_ID_UIF	1
#define QLA84_MGMT_CONFIG_ID_FCOE_COS	2
#define QLA84_MGMT_CONFIG_ID_PAUSE	3
#define QLA84_MGMT_CONFIG_ID_TIMEOUTS	4

			uint32_t param0;
			uint32_t param1;
		} config; /* for QLA84_MGMT_CHNG_CONFIG */

		struct {
			uint32_t type;
#define QLA84_MGMT_INFO_CONFIG_LOG_DATA		1 /* Get Config Log Data */
#define QLA84_MGMT_INFO_LOG_DATA		2 /* Get Log Data */
#define QLA84_MGMT_INFO_PORT_STAT		3 /* Get Port Statistics */
#define QLA84_MGMT_INFO_LIF_STAT		4 /* Get LIF Statistics  */
#define QLA84_MGMT_INFO_ASIC_STAT		5 /* Get ASIC Statistics */
#define QLA84_MGMT_INFO_CONFIG_PARAMS		6 /* Get Config Parameters */
#define QLA84_MGMT_INFO_PANIC_LOG		7 /* Get Panic Log */

			uint32_t context;
/*
 * context definitions for QLA84_MGMT_INFO_CONFIG_LOG_DATA
 */
#define IC_LOG_DATA_LOG_ID_DEBUG_LOG			0
#define IC_LOG_DATA_LOG_ID_LEARN_LOG			1
#define IC_LOG_DATA_LOG_ID_FC_ACL_INGRESS_LOG		2
#define IC_LOG_DATA_LOG_ID_FC_ACL_EGRESS_LOG		3
#define IC_LOG_DATA_LOG_ID_ETHERNET_ACL_INGRESS_LOG	4
#define IC_LOG_DATA_LOG_ID_ETHERNET_ACL_EGRESS_LOG	5
#define IC_LOG_DATA_LOG_ID_MESSAGE_TRANSMIT_LOG		6
#define IC_LOG_DATA_LOG_ID_MESSAGE_RECEIVE_LOG		7
#define IC_LOG_DATA_LOG_ID_LINK_EVENT_LOG		8
#define IC_LOG_DATA_LOG_ID_DCX_LOG			9

/*
 * context definitions for QLA84_MGMT_INFO_PORT_STAT
 */
#define IC_PORT_STATISTICS_PORT_NUMBER_ETHERNET_PORT0	0
#define IC_PORT_STATISTICS_PORT_NUMBER_ETHERNET_PORT1	1
#define IC_PORT_STATISTICS_PORT_NUMBER_NSL_PORT0	2
#define IC_PORT_STATISTICS_PORT_NUMBER_NSL_PORT1	3
#define IC_PORT_STATISTICS_PORT_NUMBER_FC_PORT0		4
#define IC_PORT_STATISTICS_PORT_NUMBER_FC_PORT1		5


/*
 * context definitions for QLA84_MGMT_INFO_LIF_STAT
 */
#define IC_LIF_STATISTICS_LIF_NUMBER_ETHERNET_PORT0	0
#define IC_LIF_STATISTICS_LIF_NUMBER_ETHERNET_PORT1	1
#define IC_LIF_STATISTICS_LIF_NUMBER_FC_PORT0		2
#define IC_LIF_STATISTICS_LIF_NUMBER_FC_PORT1		3
#define IC_LIF_STATISTICS_LIF_NUMBER_CPU		6

		} info; /* for QLA84_MGMT_GET_INFO */
	} u;
};

#define QLFC_MAX_AEN	256
struct qlfc_aen_entry {
	uint16_t event_code;
	uint16_t payload[3];
};

struct qlfc_aen_log {
	uint32_t num_events;
	struct qlfc_aen_entry aen[QLFC_MAX_AEN];
};

struct qla84_msg_mgmt {
	uint16_t cmd;
#define QLA84_MGMT_READ_MEM	0x00
#define QLA84_MGMT_WRITE_MEM	0x01
#define QLA84_MGMT_CHNG_CONFIG	0x02
#define QLA84_MGMT_GET_INFO	0x03
	uint16_t rsrvd;
	struct qla84_mgmt_param mgmtp;/* parameters for cmd */
	uint32_t len; /* bytes in payload following this struct */
	uint8_t payload[0]; /* payload for cmd */
};

struct msg_update_fw {
	/*
	 * diag_fw = 0  operational fw
	 *	otherwise diagnostic fw
	 * offset, len, fw_len are present to overcome the current limitation
	 * of 128Kb xfer size. The fw is sent in smaller chunks. Each chunk
	 * specifies the byte "offset" where it fits in the fw buffer. The
	 * number of bytes in each chunk is specified in "len". "fw_len"
	 * is the total size of fw. The first chunk should start at offset = 0.
	 * When offset+len == fw_len, the fw is written to the HBA.
	 */
	uint32_t diag_fw;
	uint32_t offset;/* start offset */
	uint32_t len;	/* num bytes in cur xfer */
	uint32_t fw_len; /* size of fw in bytes */
	uint8_t fw_bytes[0];
};

struct qla_fc_msg {

	uint64_t magic;
#define QL_FC_NL_MAGIC	0x107784DDFCAB1FC1
	uint16_t host_no;
	uint16_t vmsg_datalen;

	uint32_t cmd;
#define QLA84_RESET	0x01
#define QLA84_UPDATE_FW	0x02
#define QLA84_MGMT_CMD	0x03
#define QLFC_GET_AEN	0x04

	uint32_t error; /* interface or resource error holder*/

	union {
		union {
			struct msg_reset {
				/*
				 * diag_fw = 0  for operational fw
				 * otherwise diagnostic fw
				 */
				uint32_t diag_fw;
			} qla84_reset;

			struct msg_update_fw qla84_update_fw;
			struct qla84_msg_mgmt mgmt;
		} utok;

		union {
			struct qla84_msg_mgmt mgmt;
			struct qlfc_aen_log aen_log;
		} ktou;
	} u;
} __attribute__ ((aligned (sizeof(uint64_t))));

#endif /* _QLA_NLNK_H_ */
