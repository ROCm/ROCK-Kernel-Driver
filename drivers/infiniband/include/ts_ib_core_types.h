/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: ts_ib_core_types.h 58 2004-04-16 02:09:40Z roland $
*/

#ifndef _TS_IB_CORE_TYPES_H
#define _TS_IB_CORE_TYPES_H

#if defined(__KERNEL__)
#  ifndef W2K_OS
#    include <linux/types.h>
#    include <linux/list.h>
#  endif
#else
#  ifndef W2K_OS
#    include <stdint.h>
#  endif
#endif

#ifdef W2K_OS
#include <all/common/include/w2k.h>
#endif

/* type definitions */

typedef uint16_t tTS_IB_LID;
typedef uint8_t  tTS_IB_GUID[8];
typedef uint8_t  tTS_IB_GID[16];
typedef uint8_t  tTS_IB_SL;
typedef uint8_t  tTS_IB_VL;
typedef uint16_t tTS_IB_PKEY;
typedef uint32_t tTS_IB_QKEY;
typedef uint8_t  tTS_IB_PORT;
typedef uint8_t  tTS_IB_MKEY[8];
typedef uint8_t  tTS_IB_ARB_WEIGHT;
typedef uint8_t  tTS_IB_TIME;
typedef uint32_t tTS_IB_PSN;
typedef uint32_t tTS_IB_QPN;
typedef uint32_t tTS_IB_EECN;
typedef uint64_t tTS_IB_WORK_REQUEST_ID;
typedef uint32_t tTS_IB_LKEY;
typedef uint32_t tTS_IB_RKEY;
typedef char     tTS_IB_NODE_DESC[64];

#define TS_IB_HANDLE_INVALID ((void *) 0)

typedef void *   tTS_IB_DEVICE_HANDLE;
typedef void *   tTS_IB_PD_HANDLE;
typedef void *   tTS_IB_ADDRESS_HANDLE;
typedef void *   tTS_IB_QP_HANDLE;
typedef void *   tTS_IB_CQ_HANDLE;
typedef void *   tTS_IB_RD_DOMAIN_HANDLE;
typedef void *   tTS_IB_EEC_HANDLE;
typedef void *   tTS_IB_MR_HANDLE;
typedef void *   tTS_IB_MW_HANDLE;
typedef void *   tTS_IB_FMR_HANDLE;
typedef void *   tTS_IB_FMR_POOL_HANDLE;
typedef void *   tTS_IB_ASYNC_EVENT_HANDLER_HANDLE;

/*
  Compatibility typedefs.  Just use the "struct ib_xxx *" name in new
  code, leave these typedefs for legacy code.
*/
typedef struct ib_device_properties tTS_IB_DEVICE_PROPERTIES_STRUCT,
  *tTS_IB_DEVICE_PROPERTIES;
typedef struct ib_device_changes tTS_IB_DEVICE_PROPERTIES_SET_STRUCT,
  *tTS_IB_DEVICE_PROPERTIES_SET;
typedef struct ib_port_properties tTS_IB_PORT_PROPERTIES_STRUCT,
  *tTS_IB_PORT_PROPERTIES;
typedef struct ib_port_changes tTS_IB_PORT_PROPERTIES_SET_STRUCT,
  *tTS_IB_PORT_PROPERTIES_SET;
typedef struct ib_path_record tTS_IB_PATH_RECORD_STRUCT,
  *tTS_IB_PATH_RECORD;
typedef struct ib_address_vector tTS_IB_ADDRESS_VECTOR_STRUCT,
  *tTS_IB_ADDRESS_VECTOR;
typedef struct ib_qp_limit tTS_IB_QP_LIMIT_STRUCT,
  *tTS_IB_QP_LIMIT;
typedef struct ib_qp_create_param tTS_IB_QP_CREATE_PARAM_STRUCT,
  *tTS_IB_QP_CREATE_PARAM;
typedef struct ib_qp_attribute tTS_IB_QP_ATTRIBUTE_STRUCT,
  *tTS_IB_QP_ATTRIBUTE;
typedef struct ib_gather_scatter tTS_IB_GATHER_SCATTER_STRUCT,
  *tTS_IB_GATHER_SCATTER;
typedef struct ib_send_param tTS_IB_SEND_PARAM_STRUCT,
  *tTS_IB_SEND_PARAM;
typedef struct ib_receive_param tTS_IB_RECEIVE_PARAM_STRUCT,
  *tTS_IB_RECEIVE_PARAM;
typedef struct ib_cq_entry tTS_IB_CQ_ENTRY_STRUCT,
  *tTS_IB_CQ_ENTRY;
typedef struct ib_cq_callback tTS_IB_CQ_CALLBACK_STRUCT,
  *tTS_IB_CQ_CALLBACK;
typedef struct ib_physical_buffer tTS_IB_PHYSICAL_BUFFER_STRUCT,
  *tTS_IB_PHYSICAL_BUFFER;
typedef struct ib_fmr_pool_param tTS_IB_FMR_POOL_PARAM_STRUCT,
  *tTS_IB_FMR_POOL_PARAM;
typedef struct ib_async_event_record tTS_IB_ASYNC_EVENT_RECORD_STRUCT,
  *tTS_IB_ASYNC_EVENT_RECORD;
typedef struct ib_sm_path tTS_IB_SM_PATH_STRUCT,
  *tTS_IB_SM_PATH;
typedef struct ib_port_lid tTS_IB_PORT_LID_STRUCT,
  *tTS_IB_PORT_LID;

typedef void (*tTS_IB_CQ_ENTRY_CALLBACK_FUNCTION)(tTS_IB_CQ_HANDLE cq,
                                                  tTS_IB_CQ_ENTRY  entry,
                                                  void            *arg);
typedef void (*tTS_IB_CQ_EVENT_CALLBACK_FUNCTION)(tTS_IB_CQ_HANDLE cq,
                                                  void            *arg);
typedef void (*tTS_IB_FMR_FLUSH_FUNCTION)(tTS_IB_FMR_POOL_HANDLE pool,
                                          void                  *arg);
typedef void (*tTS_IB_ASYNC_EVENT_HANDLER_FUNCTION)(tTS_IB_ASYNC_EVENT_RECORD record,
                                                    void *arg);

/* enum definitions */

#define TS_IB_MULTICAST_QPN   0xffffff

typedef enum ib_mtu {
	IB_MTU_256  = 1,
	IB_MTU_512  = 2,
	IB_MTU_1024 = 3,
	IB_MTU_2048 = 4,
	IB_MTU_4096 = 5
} tTS_IB_MTU;

typedef enum ib_port_state {
	IB_PORT_STATE_NOP    = 0,
	IB_PORT_STATE_DOWN   = 1,
	IB_PORT_STATE_INIT   = 2,
	IB_PORT_STATE_ARMED  = 3,
	IB_PORT_STATE_ACTIVE = 4
} tTS_IB_PORT_STATE;

typedef enum ib_rate {
	TS_IB_RATE_2GB5      =  2,
	TS_IB_RATE_10GB      =  3,
	TS_IB_RATE_30GB      =  4,
} tTS_IB_RATE;

typedef enum ib_static_rate {
	TS_IB_STATIC_RATE_FULL      =  0,
	TS_IB_STATIC_RATE_4X_TO_1X  =  3,
	TS_IB_STATIC_RATE_12X_TO_4X =  2,
	TS_IB_STATIC_RATE_12X_TO_1X = 11
} tTS_IB_STATIC_RATE;

typedef enum ib_rnr_timeout {
	TS_IB_RNR_TIMER_655_36 =  0,
	TS_IB_RNR_TIMER_000_01 =  1,
	TS_IB_RNR_TIMER_000_02 =  2,
	TS_IB_RNR_TIMER_000_03 =  3,
	TS_IB_RNR_TIMER_000_04 =  4,
	TS_IB_RNR_TIMER_000_06 =  5,
	TS_IB_RNR_TIMER_000_08 =  6,
	TS_IB_RNR_TIMER_000_12 =  7,
	TS_IB_RNR_TIMER_000_16 =  8,
	TS_IB_RNR_TIMER_000_24 =  9,
	TS_IB_RNR_TIMER_000_32 = 10,
	TS_IB_RNR_TIMER_000_48 = 11,
	TS_IB_RNR_TIMER_000_64 = 12,
	TS_IB_RNR_TIMER_000_96 = 13,
	TS_IB_RNR_TIMER_001_28 = 14,
	TS_IB_RNR_TIMER_001_92 = 15,
	TS_IB_RNR_TIMER_002_56 = 16,
	TS_IB_RNR_TIMER_003_84 = 17,
	TS_IB_RNR_TIMER_005_12 = 18,
	TS_IB_RNR_TIMER_007_68 = 19,
	TS_IB_RNR_TIMER_010_24 = 20,
	TS_IB_RNR_TIMER_015_36 = 21,
	TS_IB_RNR_TIMER_020_48 = 22,
	TS_IB_RNR_TIMER_030_72 = 23,
	TS_IB_RNR_TIMER_040_96 = 24,
	TS_IB_RNR_TIMER_061_44 = 25,
	TS_IB_RNR_TIMER_081_92 = 26,
	TS_IB_RNR_TIMER_122_88 = 27,
	TS_IB_RNR_TIMER_163_84 = 28,
	TS_IB_RNR_TIMER_245_76 = 29,
	TS_IB_RNR_TIMER_327_68 = 30,
	TS_IB_RNR_TIMER_491_52 = 31
} tTS_IB_RNR_TIMEOUT;

typedef enum ib_atomic_support {
	TS_IB_NO_ATOMIC_OPS,
	TS_IB_ATOMIC_HCA,
	TS_IB_ATOMIC_ALL
} tTS_IB_ATOMIC_SUPPORT;

typedef enum ib_device_properties_mask {
	TS_IB_DEVICE_SYSTEM_IMAGE_GUID            = 1 << 0
} tTS_IB_DEVICE_PROPERTIES_MASK;

typedef enum ib_port_properties_mask {
	TS_IB_PORT_SHUTDOWN_PORT                  = 1 << 0,
	TS_IB_PORT_INIT_TYPE                      = 1 << 1,
	TS_IB_PORT_QKEY_VIOLATION_COUNTER_RESET   = 1 << 2,
	TS_IB_PORT_IS_SM                          = 1 << 3,
	TS_IB_PORT_IS_SNMP_TUNNELING_SUPPORTED    = 1 << 4,
	TS_IB_PORT_IS_DEVICE_MANAGEMENT_SUPPORTED = 1 << 5,
	TS_IB_PORT_IS_VENDOR_CLASS_SUPPORTED      = 1 << 6
} tTS_IB_PORT_PROPERTIES_MASK;

typedef enum ib_transport {
	IB_TRANSPORT_RC          = 0,
	IB_TRANSPORT_UC          = 1,
	IB_TRANSPORT_RD          = 2,
	IB_TRANSPORT_UD          = 3,
} tTS_IB_TRANSPORT;

typedef enum ib_special_qp_type {
	IB_SMI_QP,
	IB_GSI_QP,
	IB_RAW_IPV6_QP,
	IB_RAW_ETHERTYPE_QP
} tTS_IB_SPECIAL_QP_TYPE;

typedef enum ib_wq_signal_policy {
	IB_WQ_SIGNAL_ALL,
	IB_WQ_SIGNAL_SELECTABLE
} tTS_IB_WQ_SIGNAL_POLICY;

typedef enum ib_qp_state {
	IB_QP_STATE_RESET,
	IB_QP_STATE_INIT,
	IB_QP_STATE_RTR,
	IB_QP_STATE_RTS,
	IB_QP_STATE_SQD,
	IB_QP_STATE_SQE,
	IB_QP_STATE_ERROR
} tTS_IB_QP_STATE;

typedef enum ib_migration_state {
	TS_IB_MIGRATED,
	TS_IB_REARM,
	TS_IB_ARMED
} tTS_IB_MIGRATION_STATE;

typedef enum ib_qp_attribute_mask {
	TS_IB_QP_ATTRIBUTE_STATE                  = 1 <<  0,
	TS_IB_QP_ATTRIBUTE_SEND_PSN               = 1 <<  1,
	TS_IB_QP_ATTRIBUTE_RECEIVE_PSN            = 1 <<  2,
	TS_IB_QP_ATTRIBUTE_DESTINATION_QPN        = 1 <<  3,
	TS_IB_QP_ATTRIBUTE_QKEY                   = 1 <<  4,
	TS_IB_QP_ATTRIBUTE_PATH_MTU               = 1 <<  5,
	TS_IB_QP_ATTRIBUTE_MIGRATION_STATE        = 1 <<  6,
	TS_IB_QP_ATTRIBUTE_INITIATOR_DEPTH        = 1 <<  7,
	TS_IB_QP_ATTRIBUTE_RESPONDER_RESOURCES    = 1 <<  8,
	TS_IB_QP_ATTRIBUTE_RETRY_COUNT            = 1 <<  9,
	TS_IB_QP_ATTRIBUTE_RNR_RETRY_COUNT        = 1 << 10,
	TS_IB_QP_ATTRIBUTE_RNR_TIMEOUT            = 1 << 11,
	TS_IB_QP_ATTRIBUTE_PKEY_INDEX             = 1 << 12,
	TS_IB_QP_ATTRIBUTE_PORT                   = 1 << 13,
	TS_IB_QP_ATTRIBUTE_ADDRESS                = 1 << 14,
	TS_IB_QP_ATTRIBUTE_LOCAL_ACK_TIMEOUT      = 1 << 15,
	TS_IB_QP_ATTRIBUTE_ALT_PKEY_INDEX         = 1 << 16,
	TS_IB_QP_ATTRIBUTE_ALT_PORT               = 1 << 17,
	TS_IB_QP_ATTRIBUTE_ALT_ADDRESS            = 1 << 18,
	TS_IB_QP_ATTRIBUTE_ALT_LOCAL_ACK_TIMEOUT  = 1 << 19,
	TS_IB_QP_ATTRIBUTE_RDMA_ATOMIC_ENABLE     = 1 << 20,
	TS_IB_QP_ATTRIBUTE_SQD_ASYNC_EVENT_ENABLE = 1 << 21
} tTS_IB_QP_ATTRIBUTE_MASK;

typedef enum ib_op {
	TS_IB_OP_RECEIVE,
	TS_IB_OP_SEND,
	TS_IB_OP_SEND_IMMEDIATE,
	TS_IB_OP_RDMA_WRITE,
	TS_IB_OP_RDMA_WRITE_IMMEDIATE,
	TS_IB_OP_RDMA_READ,
	TS_IB_OP_COMPARE_SWAP,
	TS_IB_OP_FETCH_ADD,
	TS_IB_OP_MEMORY_WINDOW_BIND
} tTS_IB_OP;

typedef enum ib_cq_callback_context {
	TS_IB_CQ_CALLBACK_INTERRUPT,
	TS_IB_CQ_CALLBACK_PROCESS
} tTS_IB_CQ_CALLBACK_CONTEXT;

typedef enum ib_cq_rearm_policy {
	TS_IB_CQ_PROVIDER_REARM,
	TS_IB_CQ_CONSUMER_REARM
} tTS_IB_CQ_REARM_POLICY;

typedef enum ib_completion_status {
  TS_IB_COMPLETION_STATUS_SUCCESS,
  TS_IB_COMPLETION_STATUS_LOCAL_LENGTH_ERROR,
  TS_IB_COMPLETION_STATUS_LOCAL_QP_OPERATION_ERROR,
  TS_IB_COMPLETION_STATUS_LOCAL_EEC_OPERATION_ERROR,
  TS_IB_COMPLETION_STATUS_LOCAL_PROTECTION_ERROR,
  TS_IB_COMPLETION_STATUS_WORK_REQUEST_FLUSHED_ERROR,
  TS_IB_COMPLETION_STATUS_MEMORY_WINDOW_BIND_ERROR,
  TS_IB_COMPLETION_STATUS_BAD_RESPONSE_ERROR,
  TS_IB_COMPLETION_STATUS_LOCAL_ACCESS_ERROR,
  TS_IB_COMPLETION_STATUS_REMOTE_INVALID_REQUEST_ERROR,
  TS_IB_COMPLETION_STATUS_REMOTE_ACCESS_ERORR,
  TS_IB_COMPLETION_STATUS_REMOTE_OPERATION_ERROR,
  TS_IB_COMPLETION_STATUS_TRANSPORT_RETRY_COUNTER_EXCEEDED,
  TS_IB_COMPLETION_STATUS_RNR_RETRY_COUNTER_EXCEEDED,
  TS_IB_COMPLETION_STATUS_LOCAL_RDD_VIOLATION_ERROR,
  TS_IB_COMPLETION_STATUS_REMOTE_INVALID_RD_REQUEST,
  TS_IB_COMPLETION_STATUS_REMOTE_ABORTED_ERROR,
  TS_IB_COMPLETION_STATUS_INVALID_EEC_NUMBER,
  TS_IB_COMPLETION_STATUS_INVALID_EEC_STATE,
  TS_IB_COMPLETION_STATUS_UNKNOWN_ERROR
} tTS_IB_COMPLETION_STATUS;

typedef enum ib_memory_access {
	TS_IB_ACCESS_LOCAL_WRITE   = 1 << 0,
	TS_IB_ACCESS_REMOTE_WRITE  = 1 << 1,
	TS_IB_ACCESS_REMOTE_READ   = 1 << 2,
	TS_IB_ACCESS_REMOTE_ATOMIC = 1 << 3,
	TS_IB_ACCESS_ENABLE_WINDOW = 1 << 4
} tTS_IB_MEMORY_ACCESS;

typedef enum ib_async_event {
	TS_IB_QP_PATH_MIGRATED,
	TS_IB_EEC_PATH_MIGRATED,
	TS_IB_QP_COMMUNICATION_ESTABLISHED,
	TS_IB_EEC_COMMUNICATION_ESTABLISHED,
	TS_IB_SEND_QUEUE_DRAINED,
	TS_IB_CQ_ERROR,
	TS_IB_LOCAL_WQ_INVALID_REQUEST_ERROR,
	TS_IB_LOCAL_WQ_ACCESS_VIOLATION_ERROR,
	TS_IB_LOCAL_WQ_CATASTROPHIC_ERROR,
	TS_IB_PATH_MIGRATION_ERROR,
	TS_IB_LOCAL_EEC_CATASTROPHIC_ERROR,
	TS_IB_LOCAL_CATASTROPHIC_ERROR,
	TS_IB_PORT_ERROR,
	TS_IB_PORT_ACTIVE,
	TS_IB_LID_CHANGE,
	TS_IB_PKEY_CHANGE,
} tTS_IB_ASYNC_EVENT;

/* structures */

#define TS_IB_DEVICE_NAME_MAX 64

enum {
	IB_DEVICE_NOTIFIER_ADD,
	IB_DEVICE_NOTIFIER_REMOVE
};

struct ib_device_notifier {
	void (*notifier)(struct ib_device_notifier *self,
			 tTS_IB_DEVICE_HANDLE device,
			 int event);
#ifdef __KERNEL__
	struct list_head list;
#endif
};

struct ib_device_properties {
	char                  name[TS_IB_DEVICE_NAME_MAX];
	char                 *provider;
	uint32_t              vendor_id;
	uint16_t              device_id;
	uint32_t              hw_rev;
	uint64_t              fw_rev;
	int                   max_qp;
	int                   max_wr_per_qp;
	int                   max_wr_per_post;
	int                   max_sg_per_wr;
	int                   max_sg_per_wr_rd;
	int                   max_cq;
	int                   max_mr;
	uint64_t              max_mr_size;
	int                   max_pd;
	int                   page_size_cap;
	int                   num_port;
	int                   max_pkey;
	tTS_IB_TIME           local_ca_ack_delay;
	int                   max_responder_per_qp;
	int                   max_responder_per_eec;
	int                   max_responder_per_hca;
	int                   max_initiator_per_qp;
	int                   max_initiator_per_eec;
	tTS_IB_ATOMIC_SUPPORT atomic_support;
	int                   max_eec;
	int                   max_rdd;
	int                   max_mw;
	int                   max_raw_ipv6_qp;
	int                   max_raw_ethertype_qp;
	int                   max_mcg;
	int                   max_mc_qp;
	int                   max_qp_per_mcg;
	int                   max_ah;
	int                   max_fmr;
	int                   max_map_per_fmr;
	tTS_IB_GUID           node_guid;
	int                   is_switch:1;
	int                   ah_port_num_check:1;
	int                   rnr_nak_supported:1;
	int                   port_shutdown_supported:1;
	int                   init_type_supported:1;
	int                   port_active_event_supported:1;
	int                   system_image_guid_supported:1;
	int                   bad_pkey_counter_supported:1;
	int                   qkey_violation_counter_supported;
	int                   modify_wr_num_supported:1;
	int                   raw_multicast_supported:1;
	int                   apm_supported:1;
	int                   qp_port_change_supported:1;
};

struct ib_device_changes {
	tTS_IB_DEVICE_PROPERTIES_MASK valid_fields;
	tTS_IB_GUID                   system_image_guid;
};

struct ib_port_properties {
	tTS_IB_MTU        max_mtu;
	uint32_t          max_message_size;
	tTS_IB_LID        lid;
	uint8_t           lmc;
	tTS_IB_PORT_STATE port_state;
	int               gid_table_length;
	int               pkey_table_length;
	int               max_vl;
	uint32_t          bad_pkey_counter;
	uint32_t          qkey_violation_counter;
	uint8_t           init_type_reply;
	tTS_IB_LID        sm_lid;
	tTS_IB_SL         sm_sl;
	tTS_IB_TIME       subnet_timeout;
	uint32_t          capability_mask;
};

struct ib_port_changes {
	tTS_IB_PORT_PROPERTIES_MASK valid_fields;
	uint8_t                     init_type;
	int                         shutdown:1;
	int                         qkey_violation_counter_reset:1;
	int                         is_sm:1;
	int                         is_snmp_tunneling_supported:1;
	int                         is_device_management_supported:1;
	int                         is_vendor_class_supported:1;
};

struct ib_path_record {
	tTS_IB_GID  dgid;
	tTS_IB_GID  sgid;
	tTS_IB_LID  dlid;
	tTS_IB_LID  slid;
	uint32_t    flowlabel;
	uint8_t     hoplmt;
	uint8_t     tclass;
	tTS_IB_PKEY pkey;
	tTS_IB_SL   sl;
	tTS_IB_MTU  mtu;
	tTS_IB_RATE rate;
	uint8_t     packet_life;
	uint8_t     preference;
};

struct ib_address_vector {
	int                service_level;
	tTS_IB_STATIC_RATE static_rate;
	int                source_path_bits;
	tTS_IB_LID         dlid;
	tTS_IB_PORT        port;
	uint32_t           flow_label;
	int                source_gid_index;
	uint8_t            hop_limit;
	uint8_t            traffic_class;
	tTS_IB_GID         dgid;
	int                use_grh:1;
};

struct ib_qp_limit {
	int max_outstanding_send_request;
	int max_outstanding_receive_request;
	int max_send_gather_element;
	int max_receive_scatter_element;
};

struct ib_qp_create_param {
	struct ib_qp_limit      limit;
	tTS_IB_PD_HANDLE        pd;
	tTS_IB_CQ_HANDLE        send_queue;
	tTS_IB_CQ_HANDLE        receive_queue;
	tTS_IB_WQ_SIGNAL_POLICY send_policy;
	tTS_IB_WQ_SIGNAL_POLICY receive_policy;
	tTS_IB_RD_DOMAIN_HANDLE rd_domain;
	tTS_IB_TRANSPORT        transport;
	void                   *device_specific;
};

struct ib_qp_attribute {
	tTS_IB_QP_ATTRIBUTE_MASK     valid_fields;
	tTS_IB_QP_STATE              state;
	tTS_IB_PSN                   send_psn;
	tTS_IB_PSN                   receive_psn;
	tTS_IB_QPN                   destination_qpn;
	tTS_IB_QKEY                  qkey;
	tTS_IB_MTU                   path_mtu;
	tTS_IB_MIGRATION_STATE       migration_state;
	int                          initiator_depth;
	int                          responder_resources;
	uint8_t                      retry_count;
	uint8_t                      rnr_retry_count;
	tTS_IB_RNR_TIMEOUT           rnr_timeout;
	int                          pkey_index;
	tTS_IB_PORT                  port;
	struct ib_address_vector     address;
	uint8_t                      local_ack_timeout;
	int                          alt_pkey_index;
	tTS_IB_PORT                  alt_port;
	struct ib_address_vector     alt_address;
	uint8_t                      alt_local_ack_timeout;
	int                          enable_atomic:1;
	int                          enable_rdma_read:1;
	int                          enable_rdma_write:1;
	int                          sqd_async_event_enable:1;
	int                          sq_drained:1;
};

struct ib_gather_scatter {
	uint64_t    address;
	uint32_t    length;
	tTS_IB_LKEY key;
};

struct ib_send_param {
	tTS_IB_WORK_REQUEST_ID work_request_id;
	tTS_IB_OP              op;
	tTS_IB_GATHER_SCATTER  gather_list;
	int                    num_gather_entries;
	uint64_t               remote_address;
	tTS_IB_RKEY            rkey;
	tTS_IB_QPN             dest_qpn;
	tTS_IB_QKEY            dest_qkey;
	tTS_IB_ADDRESS_HANDLE  dest_address;
	uint32_t               immediate_data;
	uint64_t               compare_add;
	uint64_t               swap;
	tTS_IB_EECN            eecn;
	uint16_t               ethertype;
	tTS_IB_STATIC_RATE     static_rate;
	int                    pkey_index;
	void                  *device_specific;
	int                    solicited_event:1;
	int                    signaled:1;
	int                    immediate_data_valid:1;
	int                    fence:1;
	int                    inline_data:1;
};

struct ib_receive_param {
	tTS_IB_WORK_REQUEST_ID work_request_id;
	tTS_IB_GATHER_SCATTER  scatter_list;
	int                    num_scatter_entries;
	void                  *device_specific;
	int                    signaled:1;
};

struct ib_cq_entry {
	tTS_IB_WORK_REQUEST_ID   work_request_id;
	tTS_IB_OP                op;
	uint64_t                 bytes_transferred;
	uint32_t                 immediate_data;
	tTS_IB_LID               slid;
	tTS_IB_SL                sl;
	tTS_IB_QPN               sqpn;
	uint16_t                 ethertype;
	tTS_IB_EECN              local_eecn;
	uint8_t                  dlid_path_bits;
	int                      pkey_index;
	tTS_IB_COMPLETION_STATUS status;
	uint32_t                 freed_resource_count;
	void                    *device_specific;
	int                      grh_present:1;
	int                      immediate_data_valid:1;
};

struct ib_cq_callback {
	tTS_IB_CQ_CALLBACK_CONTEXT          context;
	tTS_IB_CQ_REARM_POLICY              policy;
	union {
		tTS_IB_CQ_ENTRY_CALLBACK_FUNCTION entry;
		tTS_IB_CQ_EVENT_CALLBACK_FUNCTION event;
	} function;
	void                               *arg;
};

struct ib_physical_buffer {
	uint64_t address;
	uint64_t size;
};

struct ib_fmr_pool_param {
	int                       max_pages_per_fmr;
	tTS_IB_MEMORY_ACCESS      access;
	int                       pool_size;
	int                       dirty_watermark;
	tTS_IB_FMR_FLUSH_FUNCTION flush_function;
	void                     *flush_arg;
	int                       cache:1;
};

struct ib_async_event_record {
	tTS_IB_DEVICE_HANDLE device;
	tTS_IB_ASYNC_EVENT   event;
	union {
		tTS_IB_QP_HANDLE   qp;
		tTS_IB_EEC_HANDLE  eec;
		tTS_IB_CQ_HANDLE   cq;
		tTS_IB_PORT        port;
	} modifier;
};

struct ib_sm_path {
	tTS_IB_LID  sm_lid;
	tTS_IB_SL   sm_sl;
};

struct ib_port_lid {
	tTS_IB_LID lid;
	uint8_t    lmc;
};

/* Defines to support legacy code -- don't use the TS_IB names in new code. */
#define TS_IB_MTU_256	IB_MTU_256
#define TS_IB_MTU_512	IB_MTU_512
#define TS_IB_MTU_1024	IB_MTU_1024
#define TS_IB_MTU_2048	IB_MTU_2048
#define TS_IB_MTU_4096	IB_MTU_4096

#define TS_IB_PORT_STATE_NOP    IB_PORT_STATE_NOP
#define TS_IB_PORT_STATE_DOWN   IB_PORT_STATE_DOWN
#define TS_IB_PORT_STATE_INIT   IB_PORT_STATE_INIT
#define TS_IB_PORT_STATE_ARMED  IB_PORT_STATE_ARMED
#define TS_IB_PORT_STATE_ACTIVE IB_PORT_STATE_ACTIVE

#define TS_IB_TRANSPORT_RC IB_TRANSPORT_RC
#define TS_IB_TRANSPORT_UC IB_TRANSPORT_UC
#define TS_IB_TRANSPORT_RD IB_TRANSPORT_RD
#define TS_IB_TRANSPORT_UD IB_TRANSPORT_UD

#define TS_IB_SMI_QP           IB_SMI_QP
#define TS_IB_GSI_QP           IB_GSI_QP
#define TS_IB_RAW_IPV6_QP      IB_RAW_IPV6_QP
#define TS_IB_RAW_ETHERTYPE_QP IB_RAW_ETHERTYPE_QP

#define TS_IB_WQ_SIGNAL_ALL        IB_WQ_SIGNAL_ALL
#define TS_IB_WQ_SIGNAL_SELECTABLE IB_WQ_SIGNAL_SELECTABLE

#define TS_IB_QP_STATE_RESET IB_QP_STATE_RESET
#define TS_IB_QP_STATE_INIT  IB_QP_STATE_INIT
#define TS_IB_QP_STATE_RTR   IB_QP_STATE_RTR
#define TS_IB_QP_STATE_RTS   IB_QP_STATE_RTS
#define TS_IB_QP_STATE_SQD   IB_QP_STATE_SQD
#define TS_IB_QP_STATE_SQE   IB_QP_STATE_SQE
#define TS_IB_QP_STATE_ERROR IB_QP_STATE_ERROR

#endif /* _TS_IB_CORE_TYPES_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
