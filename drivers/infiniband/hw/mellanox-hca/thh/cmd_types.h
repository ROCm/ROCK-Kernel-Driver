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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_CMD_TYPES_H
#define H_CMD_TYPES_H

#include <mtl_types.h>
#include <tavor_if_defs.h>
#include <thh.h>


typedef MT_size_t THH_mpt_index_t; /* ?? */
typedef u_int16_t THH_mcg_hash_t; /* !!! to be defined in THH_mcgm !!! */

/* matan: SQD event req is passed to HCA on opcode modifier field.
   however, THH_cmd_MODIFY_QPEE() has no input argument selecting 
   sqd_event = 0/1. in order not to change the existing API, we define 
   the flag below & mask it off on entry of THH_cmd_MODIFY_QPEE(). if 
   (cmd == RTS2SQD) its value is used as sqd_event parameter.*/
#define THH_CMD_SQD_EVENT_REQ	0x80000000

/* QP/EE transitions */
enum {
  QPEE_TRANS_RST2INIT 				= TAVOR_IF_CMD_RST2INIT_QPEE,
  QPEE_TRANS_INIT2INIT				= TAVOR_IF_CMD_INIT2INIT_QPEE,
  QPEE_TRANS_INIT2RTR 				= TAVOR_IF_CMD_INIT2RTR_QPEE,
  QPEE_TRANS_RTR2RTS  				= TAVOR_IF_CMD_RTR2RTS_QPEE,
  QPEE_TRANS_RTS2RTS  				= TAVOR_IF_CMD_RTS2RTS_QPEE,
  QPEE_TRANS_SQERR2RTS				= TAVOR_IF_CMD_SQERR2RTS_QPEE,
  QPEE_TRANS_2ERR     				= TAVOR_IF_CMD_2ERR_QPEE,
  QPEE_TRANS_RTS2SQD  				= TAVOR_IF_CMD_RTS2SQD_QPEE,
  QPEE_TRANS_RTS2SQD_WITH_EVENT  	= TAVOR_IF_CMD_RTS2SQD_QPEE | THH_CMD_SQD_EVENT_REQ,
  QPEE_TRANS_SQD2RTS  				= TAVOR_IF_CMD_SQD2RTS_QPEE,
  QPEE_TRANS_ERR2RST  				= TAVOR_IF_CMD_ERR2RST_QPEE
};

typedef u_int32_t THH_qpee_transition_t;


enum {
  THH_CMD_STAT_OK = TAVOR_IF_CMD_STAT_OK,    /* command completed successfully */
  THH_CMD_STAT_INTERNAL_ERR = TAVOR_IF_CMD_STAT_INTERNAL_ERR,   /* Internal error (such as a bus error) occurred while processing command */
  THH_CMD_STAT_BAD_OP = TAVOR_IF_CMD_STAT_BAD_OP,         /* Operation/command not supported or opcode modifier not supported */
  THH_CMD_STAT_BAD_PARAM = TAVOR_IF_CMD_STAT_BAD_PARAM,      /* Parameter not supported or parameter out of range */
  THH_CMD_STAT_BAD_SYS_STATE = TAVOR_IF_CMD_STAT_BAD_SYS_STATE,  /* System not enabled or bad system state */
  THH_CMD_STAT_BAD_RESOURCE = TAVOR_IF_CMD_STAT_BAD_RESOURCE,   /* Attempt to access reserved or unallocaterd resource */
  THH_CMD_STAT_RESOURCE_BUSY = TAVOR_IF_CMD_STAT_RESOURCE_BUSY,  /* Requested resource is currently executing a command, or is otherwise busy */
  THH_CMD_STAT_DDR_MEM_ERR = TAVOR_IF_CMD_STAT_DDR_MEM_ERR,    /* memory error */
  THH_CMD_STAT_EXCEED_LIM = TAVOR_IF_CMD_STAT_EXCEED_LIM,     /* Required capability exceeds device limits */
  THH_CMD_STAT_BAD_RES_STATE = TAVOR_IF_CMD_STAT_BAD_RES_STATE,  /* Resource is not in the appropriate state or ownership */
  THH_CMD_STAT_BAD_INDEX = TAVOR_IF_CMD_STAT_BAD_INDEX,      /* Index out of range */
  THH_CMD_STAT_BAD_QPEE_STATE = TAVOR_IF_CMD_STAT_BAD_QPEE_STATE, /* Attempt to modify a QP/EE which is not in the presumed state */
  THH_CMD_STAT_BAD_SEG_PARAM = TAVOR_IF_CMD_STAT_BAD_SEG_PARAM,  /* Bad segment parameters (Address/Size) */
  THH_CMD_STAT_REG_BOUND = TAVOR_IF_CMD_STAT_REG_BOUND,      /* Memory Region has Memory Windows bound to */
  THH_CMD_STAT_BAD_PKT = TAVOR_IF_CMD_STAT_BAD_PKT,        /* Bad management packet (silently discarded) */
  THH_CMD_STAT_BAD_SIZE = TAVOR_IF_CMD_STAT_BAD_SIZE,      /* More outstanding CQEs in CQ than new CQ size */

  /* driver added statuses */
  THH_CMD_STAT_EAGAIN = 0x0100,  /* No (software) resources to enqueue given command - retry later*/
  THH_CMD_STAT_EABORT = 0x0101,  /* Command aborted (due to change in cmdif state) */
  THH_CMD_STAT_ETIMEOUT = 0X0102, /* command not completed after timeout */
  THH_CMD_STAT_EFATAL = 0x0103, /* unexpected error - fatal */
  THH_CMD_STAT_EBADARG = 0x0104, /* bad argument */
  THH_CMD_STAT_EINTR = 0X0105    /* process received signal */
};

typedef u_int32_t THH_cmd_status_t;


#define THH_CMD_STAT_OK_STR "TAVOR_IF_CMD_STAT_OK - command completed successfully"
#define THH_CMD_STAT_INTERNAL_ERR_STR "TAVOR_IF_CMD_STAT_INTERNAL_ERR = Internal error (such as a bus error) occurred while processing command"
#define THH_CMD_STAT_BAD_OP_STR "TAVOR_IF_CMD_STAT_BAD_OP - Operation/command not supported or opcode modifier not supported"
#define THH_CMD_STAT_BAD_PARAM_STR "TAVOR_IF_CMD_STAT_BAD_PARAM - Parameter not supported or parameter out of range"
#define THH_CMD_STAT_BAD_SYS_STATE_STR "TAVOR_IF_CMD_STAT_BAD_SYS_STATE - System not enabled or bad system state"
#define THH_CMD_STAT_BAD_RESOURCE_STR "TAVOR_IF_CMD_STAT_BAD_RESOURCE - Attempt to access reserved or unallocaterd resource"
#define THH_CMD_STAT_RESOURCE_BUSY_STR "TAVOR_IF_CMD_STAT_RESOURCE_BUSY - Requested resource is currently executing a command, or is otherwise busy"
#define THH_CMD_STAT_DDR_MEM_ERR_STR "TAVOR_IF_CMD_STAT_DDR_MEM_ERR - memory error"
#define THH_CMD_STAT_EXCEED_LIM_STR "TAVOR_IF_CMD_STAT_EXCEED_LIM - Required capability exceeds device limits"
#define THH_CMD_STAT_BAD_RES_STATE_STR "TAVOR_IF_CMD_STAT_BAD_RES_STATE - Resource is not in the appropriate state or ownership"
#define THH_CMD_STAT_BAD_INDEX_STR "TAVOR_IF_CMD_STAT_BAD_INDEX - Index out of range"
#define THH_CMD_STAT_BAD_QPEE_STATE_STR "TAVOR_IF_CMD_STAT_BAD_QPEE_STATE - Attempt to modify a QP/EE which is not in the presumed state"
#define THH_CMD_STAT_BAD_SEG_PARAM_STR "TAVOR_IF_CMD_STAT_BAD_SEG_PARAM - Bad segment parameters (Address/Size)"
#define THH_CMD_STAT_REG_BOUND_STR "TAVOR_IF_CMD_STAT_REG_BOUND - Memory Region has Memory Windows bound to"
#define THH_CMD_STAT_BAD_PKT_STR "TAVOR_IF_CMD_STAT_BAD_PKT - Bad management packet (silently discarded)"
#define THH_CMD_STAT_BAD_SIZE_STR "THH_CMD_STAT_BAD_SIZE - More outstanding CQEs in CQ than new CQ size"
#define THH_CMD_STAT_EAGAIN_STR "0x0100 - No (software) resources to enqueue given command - retry later"
#define THH_CMD_STAT_EABORT_STR "0x0101 - Command aborted (due to change in cmdif state)"
#define THH_CMD_STAT_ETIMEOUT_STR "0X0102 - command not completed after timeout"
#define THH_CMD_STAT_EFATAL_STR "0x0103 - unexpected error - fatal"
#define THH_CMD_STAT_EBADARG_STR "0x0104 - bad argument"
#define THH_CMD_STAT_EINTR_STR "0x0105 - process received signal"


enum {
  DDR_STAT_OK = 0,
  DDR_STAT_CAL_1_ERR = 1,
  DDR_STAT_CAL_2_ERR = 2
};
typedef u_int32_t THH_ddr_cal_status_t;

enum {
  DIM_DI_NONE = 0,
  DIM_DI_PARITY = 1,
  DIM_DI_ECC = 2
};
typedef u_int32_t THH_dim_integrity_t;

enum {
  DDR_NO_AUTO=0,
  DDR_AUTO_PRECHARGE_PER_TRANSLATION=1,
  DDR_AUTO_PRECHARGE_PER_64BIT=2
};
typedef u_int32_t THH_dim_apm_t;

enum {
  DIM_STAT_ENABLED = 0,
  DIM_STAT_DISABLED = 1
};
typedef u_int32_t THH_dim_status_t;

typedef struct THH_dim_info_t {
  u_int32_t dimmsize;	/* Size of DIMM in units of 2^20 Bytes. */
                        /* This value is valid only when DIMMStatus is DIM_STAT_OPERATIONAL. */
  THH_dim_integrity_t di;	/* Data Integrity Configuration */
  u_int8_t dimmslot;	/* Slot number in which the Logical DIMM is located. */
  /*;Two logical DIMMs may be on same module and therefore on same slot.\;This value is only valid when DIMMStatus is not 1. */
  THH_dim_status_t dimmstatus;	/* When it is 1-255 the Logical DIMM in question is disabled*/
  /* 0 - DIMM is Operational \;1 - No DIMM detected */
  /* 2 - DIMM max freq is smaller than DMU freq\;3- DIMM min freq is greater than DMU freq\;4 - DIMM CAS Latency does not match the other DIMMs\;5 - DIMM CAS Latency is not supported.\;6 - DIMM chip width does not match the other DIMMs. (x4/x8/x16)\;6 - DIMM chip width is not supported\;7 - DIMM buffered/unbuffered does not match the other DIMMs\;8 - DIMM does not support 8 byte bursts\;9 - DIMM does not have 4 banks\;10 - 255 Other DIMM Errors */
  u_int64_t vendor_id; /* JDEC Manufacturer ID (64 bits) */
}
THH_dim_info_t;


enum {
  THH_OWNER_SW = 0,
  THH_OWNER_HW = 1
};
typedef u_int32_t THH_owner_t;

typedef struct {
  u_int64_t qpc_base_addr;	/* QPC Base Address. Table must be aligned on its size */
  u_int8_t  log_num_of_qp;	/* Log base 2 of number of supported QPs */
  u_int64_t eec_base_addr;	/* EEC Base Address. Table must be aligned on its size */
  u_int64_t srqc_base_addr;	/* SRQC Base Address. Table must be aligned on its size */
  u_int8_t  log_num_of_srq;	/* Log base 2 of number of supported SRQs */
  u_int8_t log_num_of_ee;   /* Log base 2 of number of supported EEs. */
  u_int64_t cqc_base_addr;  /* CQC Base Address. Table must be aligned on its size */
  u_int8_t log_num_of_cq;   /* Log base 2 of number of supported CQs. */
  u_int64_t eqpc_base_addr;	/* Extended QPC Base Address. Table has same number of entries as QPC table. Table must be aligned to entry size. */
  u_int64_t eeec_base_addr;	/* Extended EEC Base Address. Table has same number of entries as EEC table. Table must be aligned to entry size. */
  u_int64_t eqc_base_addr;	/* EQC Base Address. */
  u_int8_t log_num_eq;      /* Log base 2 of number of supported EQs. Must be 6 in MT23108 */
  u_int64_t rdb_base_addr;  /* Base address of table that holds remote read and remote atomic requests. Table must be aligned to RDB entry size (32 bytes). Table size is implicitly defined when QPs/EEs are configured with indexes into this table. */
}
THH_contexts_prms_t;

typedef struct { /* Protected UD-AV table parameters */
  u_int32_t l_key;	/* L_Key used to access TPT */
  u_int32_t pd;	/* PD used by TPT for matching against PD of region entry being accessed. */
  MT_bool xlation_en;	/* When cleared, address is physical address and no translation will be done. When set, address is virtual. TPT will be accessed in both cases for address decoding purposes. */
}
THH_ud_av_tbl_prms_t;

typedef struct {
  u_int64_t mc_base_addr;	/* Base Address of the Multicast Table. The base address must be aligned to the entry size. */
  u_int16_t log_mc_table_entry_sz;	/* Log2 of the Size of multicast group member (MGM) entry. Must be greater than 5 (to allow CTRL and GID sections). That implies the number of QPs per MC table entry. */
  u_int32_t mc_table_hash_sz;	/* Number of entries in multicast DGID hash table (must be power of 2).
                                 INIT_HCA - the required number of entries
                                 QUERY_HCA - the actual number of entries assigned by firmware (will be less than or equal to the amount required in INIT_HCA) */
  u_int8_t log_mc_table_sz; /* Log2 of the overall number of MC entries in the MCG table (includes both hash and auxiliary tables) */
  u_int8_t mc_hash_fn;	/* Multicast hash function\;0 - Default hash function\;other - reserved */
}
THH_mcast_prms_t;

typedef struct {
  u_int64_t mpt_base_adr;	/* MPT - Memory Protection Table base physical address. Entry size is 64 bytes. Table must be aligned to its size. */
  u_int8_t log_mpt_sz;  /* Log (base 2) of the number of region/windows entries in the MPT table. */
  u_int8_t pfto;	/* Page Fault RNR Timeout - The field returned in RNR Naks generated when a page fault is detected. It has no effect when on-demand-paging is not used. */
  u_int8_t mtt_version;  /* Version of MTT page walk. Must be zero */
  u_int64_t mtt_base_addr;	/* MTT - Memory Translation table base physical address. Table must be aligned to its size. */
  u_int8_t mtt_segment_size;  /* The size of MTT segment is 64*2^MTT_Segment_Size bytes */
}
THH_tpt_prms_t;

typedef struct {
  u_int64_t uar_base_addr; /* UAR Base Address (QUERY_HCA only) */
  u_int8_t uar_page_sz;	/* This field defines the size of each UAR page. Size of UAR Page is 4KB*2^UAR_Page_Size */
  u_int64_t uar_scratch_base_addr;	/* Base address of UAR scratchpad. Number of entries in table is UAR BAR size divided by UAR Page Size. Table must be aligned to entry size. */
}
THH_uar_prms_t;

typedef struct {
  u_int32_t tbd; /* To Be Defined */
}
THH_sched_arb_t;

typedef struct {
  u_int8_t hca_core_clock;	/* Internal Clock Period (in units of 1/16 ns) (QUERY_HCA only) */
  u_int16_t router_qp;	/* Upper 16 bit to be used as a QP number for router mode. Low order 8 bits are taken from the TClass field of the incoming packet.\;Valid only if RE bit is set */
  MT_bool re;	/* Router Mode Enable\;If this bit is set, entire packet (including all headers and ICRC) will be considered as a data payload and will be scattered to memory as specified in the descriptor that is posted on the QP matching the TClass field of packet. */
  MT_bool udp;	/* UD Port Check Enable\;0 - Port field in Address Vector is ignored\;1 - HCA will check the port field in AV entry (fetched for UD descriptor) against the Port of the UD QP executing the descriptor. */
  MT_bool he;	/* host is big endian - Used for Atomic Operations */
  MT_bool ud;   /* enable UD address vector protection (privileged UDAVs). 0 = disabled; 1 = enabled */
  THH_contexts_prms_t qpc_eec_cqc_eqc_rdb_parameters;
  THH_ud_av_tbl_prms_t udavtable_memory_parameters;	/* Memory Access Parameters for UD Address Vector
Table. Used for QPs/EEc that are configured to use protected Address Vectors. */
  THH_mcast_prms_t multicast_parameters;
  THH_tpt_prms_t tpt_parameters;
  THH_uar_prms_t uar_parameters;	/* UAR Parameters */
}
THH_hca_props_t;


enum {
  EQ_STATE_RSRV1 = 0,
  EQ_STATE_ARMED = 1,
  EQ_STATE_ALWAYS_ARMED = 2,
  EQ_STATE_FIRED = 3
};
typedef u_int32_t THH_eq_state_t;

enum {
  EQ_STATUS_OK = 0,
  EQ_STATUS_OVERFLOW = 9,
  EQ_STATUS_WRITE_FAILURE = 10
};
typedef u_int32_t THH_eq_status_t;

typedef struct {
  THH_eq_state_t st;	/* Event delivery state machine\;01 - Armed\;11 - Fired\;10,00 - Reserved */
  MT_bool oi;	/* Overrun detection ignore */
  MT_bool tr;	/* Translation Required. If set - EQ access undergo address translation. */
  THH_owner_t owner;	/* SW/HW ownership */
  THH_eq_status_t status;	/* EQ status:\;0000 - OK\;1001 - EQ overflow\;1010 - EQ write failure */
  u_int64_t start_address;	/* Start Address of Event Queue. Must be aligned on 32-byte boundary */
  u_int32_t usr_page;
  u_int8_t log_eq_size;	/* Log2 of the amount of entries in the EQ */
  u_int8_t intr;	/* Interupt (message) to be generated to report event to INT layer.\;0000iiii - specifies GPIO pin to be asserted\;1jjjjjjj - specificies type of interrupt message to be generated (total 128 different messages supported). */
  u_int32_t lost_count;	/* Number of events lost due to EQ overrun */
  u_int32_t lkey;	/* Memory key (L-Key) to be used to access EQ */
  u_int32_t pd;     /* Protection Domain */
  u_int32_t consumer_indx;	/* Contains next entry to be read upon poll for completion. Must be initalized to '0 while opening EQ */
  u_int32_t producer_indx;	/* Contains next entry in EQ to be written by the HCA. Must be initialized to '1 while opening EQ. */
}
THH_eqc_t;

enum {
  CQ_STATE_DISARMED = 0x0,
  CQ_STATE_ARMED = 0x1,
  CQ_STATE_ARMED_SOLICITED = 0x4,
  CQ_STATE_FIRED = 0xA
};
typedef u_int32_t THH_cq_state_t;

enum {
  CQ_STATUS_OK = 0,
  CQ_STATUS_OVERFLOW = 9,
  CQ_STATUS_WRITE_FAILURE = 10
};
typedef u_int32_t THH_cq_status_t;

typedef struct {
  THH_cq_state_t st; /* Event delivery state machine (Regular Events)\;0 - Disarmed\;1 - Armed\;Armed_Solicited=4\;0xA - Fired\;other - Reserved */
  MT_bool oi;	/* Ignore overrun of this CQ if this bit is set */
  MT_bool tr;	/* Translation Required - if set, accesses to CQ will undergo address translation. */
  THH_cq_status_t status;	/* CQ status\;0000 -  OK\;1001 - CQ overflow\;1010 - CQ write failure */
  u_int64_t start_address;	/* Start address of CQ. Must be aligned on CQE size (32 bytes) */
  u_int32_t usr_page;	/* UAR page this CQ can be accessed through (ringinig CQ doorbells) */
  u_int8_t log_cq_size;	/* amount of entries in CQ 2^Log_CQ_size */
  THH_eqn_t e_eqn;	/* Event Queue this CQ reports errors to (e.g. CQ overflow) */
  THH_eqn_t c_eqn;	/* Event Queue this CQ reports completion events to */
  u_int32_t pd;	/* Protection Domain */
  u_int32_t l_key;	/* Memory key (L_Key) to be used to access CQ */
  u_int32_t last_notified_indx;	/* Maintained by HW, not to be altered by SW */
  u_int32_t solicit_producer_indx;	/* Maintained by HW, not to be altered by SW. points to last CQE reported for message with S-bit set */
  u_int32_t consumer_indx;	/* Contains index to the next entry to be read upon poll for completion. The first completion after passing ownership of CQ from software to hardware will be reported to value passed in this field. */
  u_int32_t producer_indx;	/* Maintained by HW, not to be altered by SW. Points to the next entry to be written to by Hardware. CQ overrun is reported if Producer_indx + 1 equals to Consumer_indx. */
  u_int32_t cqn;	/* CQ number. Least significant bits are constrained by the position of this CQ in CQC table */
}
THH_cqc_t;

enum {
  PM_STATE_ARMED = 0,
  PM_STATE_REARM = 1,
  PM_STATE_MIGRATED = 3
};
typedef u_int32_t THH_pm_state_t;

enum {
  THH_ST_RC = 0,
  THH_ST_UC = 1,
  THH_ST_RD = 2,
  THH_ST_UD = 3,
  THH_ST_MLX = 7
};
typedef u_int32_t THH_service_type_t;

typedef struct {
  u_int8_t pkey_index;	/* PKey table index */
  IB_port_t port_number;	/* Specific port associated with this QP/EE.\;0 - Port 1\;1 - Port 2 */
  IB_lid_t rlid;	/* Remte (Destination) LID */
  u_int8_t my_lid_path_bits;	/* Source LID - the lower 7 bits (upper bits are taken from PortInfo) */
  MT_bool g;	/* Global address enable - if set, GRH will be formed for packet header */
  u_int8_t rnr_retry;	/* RNR retry count */
  u_int8_t hop_limit;	/* IPv6 hop limit */
  u_int8_t max_stat_rate;	/* Maximum static rate control. \;0 - 4X injection rate\;1 - 1X injection rate\;other - reserved\; */
  /* removed - u_int8_t MSG;*/	/* Message size (valid for UD AV only), size is 256*2^MSG bytes */
  u_int8_t mgid_index;	/* Index to port GID table */
  u_int8_t ack_timeout;	/* Locak ACK timeout */
  u_int32_t flow_label;	/* IPv6 flow label */
  u_int8_t tclass;	/* IPv6 TClass */
  IB_sl_t sl;	/* InfiniBand Service Level (SL) */
  IB_gid_t rgid; /* Remote GID */
}
THH_address_path_t;

typedef struct {
  u_int8_t ver;	/* Version of QPC format. Must be zero for MT23108 */
  /* MT_bool te;	 */ /* Address translation enable. If cleared - no address translation will be performed for all memory accesses (data buffers and descriptors) associated with this QP. Present in all transports, invalid (reserved) in EE comnext */
  /* u_int8_t ce;  Cache Mode. Must be set to '1 for proper HCA operation */
  MT_bool de;	/* Send/Receive Descriptor Event enable - if set, events can be generated upon descriptors' completion on send/receive queue (controlled by E bit in WQE). Invalid in EE context */
  THH_pm_state_t pm_state;	/* Path migration state (Migrated, Armed or Rearm)\;11-Migrated\;00-Armed\;01-Rearm */
  THH_service_type_t st;	/* Service type (invalid in EE context):\;000-Reliable Connection\;001-Unreliable Connection\;010-Reliable Datagram\;011-Unreliable Datagram\;111-MLX transport (raw bits injection). Used fro management QPs and RAW */
  VAPI_qp_state_t	state;  /* For QUERY_QPEE */
  MT_bool sq_draining; /* query only - when (qp_state == VAPI_SQD) indicates whether sq is in drain process (TRUE), or drained.*/
  u_int8_t sched_queue;	/* Schedule queue to be used for WQE scheduling to execution. Detrmines QOS for this QP. */
  u_int8_t msg_max;	/* Max message size allowed on the QP. Maximum message size is two in the power of 2^msg_Max */
  IB_mtu_t mtu;	/* MTU of the QP. Must be the same for both paths (primery and laternative). Encoding is per IB spec. Not valid (reserved) in EE context */
  u_int32_t usr_page;	/* Index (offset) of user page allocated for this QP (see "non_privileged Access to the HCA Hardware"). Not valid (reserved) in EE context. */
  u_int32_t local_qpn_een;	/* Local QO number Lower bits determine position of this record in QPC table, and - thus - constrained */
  u_int32_t remote_qpn_een;	/* Remote QP/EE number */
  THH_address_path_t primary_address_path;	/* see Table 6, "Address path format" on page 27. */
  THH_address_path_t alternative_address_path;	/* see Table 6, "Address Path Format, on page 27 */
  u_int32_t rdd;	/* Reliable Datagram Domain */
  u_int32_t pd;	/* QP protection domain.  Not valid (reserved) in EE context. */
  u_int32_t wqe_base_adr;	/* Bits 63:32 of WQE address for both SQ and RQ. \;Reserved for EE context. */
  u_int32_t wqe_lkey;	/* memory key (L-Key) to be used to access WQEs. Not valid (reserved) in EE context. */
  MT_bool ssc;	/* If set - all send WQEs generate CQEs. If celared - only send WQEs with C bit set generate completion. Not valid (reserved) in EE context. */
  MT_bool sic;	/* If zero - Ignore end to end credits on send queue. Not valid (reserved) in EE context. */
  MT_bool sae;	/* If set - Atomic operations enabled on send queue. Not valid (reserved) in EE context. */
  MT_bool swe;	/* If set - RDMA - write enabled on send queue. Not valid (reserved) in EE context. */
  MT_bool sre;	/* If set - RDMA - read enabled on send queue. Not valid (reserved) in EE context. */
  u_int8_t retry_count;	/* Maximum retry count */
  u_int8_t sra_max;	/* Maximum number of outstanding RDMA-read/Atomic operations allowed in the send queue. Maximum number is 2^SRA_Max. Not valid (reserved) in EE context. */
  u_int8_t flight_lim;	/* Number of outstanding (in-flight) messages on the wire allowed for this send queue. \;Number of outstanding messages is 2^Flight_Lim. \;Must be 0 for EE context. */
  u_int8_t ack_req_freq;	/* ACK required frequency. ACK required bit will be set in every 2^AckReqFreq packets at least.  Not valid (reserved) in EE context. */
  u_int32_t next_send_psn;	/* Next PSN to be sent */
  u_int32_t cqn_snd;	/* CQ number completions from this queue to be reported to.  Not valid (reserved) in EE context. */
  u_int64_t next_snd_wqe;	/* Pointer and properties of next WQE on send queue. The format is same as next segment (first 8 bytes) in the WQE. Not valid (reserved) in EE context. */
  MT_bool rsc;	/* If set - all receive WQEs generate CQEs. If celared - only receive WQEs with C bit set generate completion. Not valid (reserved) in EE context. */
  MT_bool ric;	/* Invalid Credits. If this bit is set, place "Invalid Credits" to ACKs sent from this queue.  Not valid (reserved) in EE context. */
  MT_bool rae;	/* If set - Atomic operations enabled. on receive queue. Not valid (reserved) in EE context. */
  MT_bool rwe;	/* If set - RDMA - write enabled on receive queue. Not valid (reserved) in EE context. */
  MT_bool rre;	/* If set - RDMA - read enabled on receive queue. Not valid (reserved) in EE context. */
  u_int8_t rra_max;	/* Maximum number of outstanding RDMA-read/Atomic operations allowed on receive queue is 2^RRA_Max. \;Must be 0 for EE context. */
  u_int32_t next_rcv_psn;	/* Next (expected) PSN on receive */
  u_int8_t min_rnr_nak;	/* Minimum RNR NAK timer value (TTTTT field encoding). Not valid (reserved) in EE context. */
  u_int32_t ra_buff_indx;	/* Index to outstanding read/atomic buffer. */
  u_int32_t cqn_rcv;	/* CQ number completions from receive queue to be reported to. Not valid (reserved) in EE context. */
  u_int64_t next_rcv_wqe;	/* Pointer and properties of next WQE on the receive queue. Ths format is same as next segment (first 8 bytes) in the WQE Not valid (reserved) in EE context. */
  u_int32_t q_key;	/* Q_Key to be validated against received datagrams and sent if MSB of Q_Key specified in the WQE is set.  Not valid (reserved) in EE context. */
  u_int32_t srqn;	/* Specifies the SRQ number from which the QP dequeues receive descriptors. Valid only if srq bit is set. Not valid (reserved) in EE context. */
  MT_bool srq;	/* If set, this queue is fed by descriptors from SRQ specified in the srqn field. Not valid (reserved) in EE context. */
}
THH_qpee_context_t;

typedef struct THH_srq_context_st {
  u_int32_t pd;	/* SRQ protection domain */
  u_int32_t uar; /* UAR index for doorbells of this SRQ */
  u_int32_t l_key;	/* memory key (L-Key) to be used to access WQEs */
  u_int32_t wqe_addr_h;	/* Bits 63:32 of WQE address for SRQ */
  u_int32_t next_wqe_addr_l; /* Bits 31:0 of next WQE address (valid only on QUERY/HW2SW) */
  u_int32_t ds;         /* Descriptor size for SRQ (divided by 16) */
  u_int16_t wqe_cnt;    /* WQE count on SRQ */
  u_int8_t  state;      /* SRQ state (QUERY)- 0xF=SW-own 0x0=HW-own 0x1=SRQ-error */
} THH_srq_context_t;

typedef struct {
  u_int8_t ver;	/* Version. Must be zero for InfiniHost */
  MT_bool r_w; /* Defines whether this entry is Region (TRUE) or Window (FALSE) */
  MT_bool pa;	/* Physical address. If set, no virtual-to-physical address translation will be performed for this region */
  MT_bool lr;	/* If set - local read access enabled */
  MT_bool lw;	/* If set - local write access enabled */
  MT_bool rr;	/* If set - Remote read access enabled. */
  MT_bool rw;	/* If set - remote write access enabled */
  MT_bool a;	/* If set - Remote Atomic access is enabled */
  MT_bool eb;	/* If set - Bind is enabled. Valid for region entry only. */
  MT_bool pw;	/* If set, all writes to this region are posted writes */
  MT_bool m_io;	/* If set - memory command is used on the uplink bus, if cleared - IO. If IO configured - PW bit must be cleared. */
  u_int8_t status;	/* 0 valid window/region\;1 valid unbound window */
  u_int8_t page_size;	/* Page size used for the region. Actual size is [4K]*2^Page_size bytes. */
  u_int32_t mem_key;	/* The memory Key. This field is compared to key used to access the region/window. Lower-order bits are restricted (index to the table). */
  u_int32_t pd;	/* Protection Domain */
  u_int64_t start_address;	/* Start Address - Virtual Address where this region/window starts */
  u_int64_t reg_wnd_len;	/* Region/Window Length */
  u_int32_t lkey;	/* LKey used for accessing the MTT (for bound windows) */
  u_int32_t win_cnt;	/* Number of windows bounded to this region. Valid for regions only. */
  u_int32_t win_cnt_limit;	/* The number of windows (limit) that can be bounded to this region. If bind operation is attempted while Win_cnt_limit, the operation will be aborted, CQE with error will be generated and QP will transfer to error state. Zero means no limit. */
  u_int64_t mtt_seg_adr;	/* Base (first) address of the MTT segment, aligned on segment_size boundary */
}
THH_mpt_entry_t;

typedef struct {
  u_int64_t ptag;	/* physical tag (full address). Low order bits are masked according to page size*/
  MT_bool p;	/* Present bit. If set, page entry is valid. If cleared, access to this page will generate 'non-present page access fault'. */
}
THH_mtt_entry_t;

typedef struct THH_port_init_props_st{  /* !!! This is going to be changed for updated INIT_IB !!! */
  MT_bool e;	/* Port Physical Link Enable */
  u_int8_t vl_cap;	/* Maximum VLs supported on the port, excluding VL15 */
  IB_link_width_t port_width_cap;	/* IB Port Width */
  IB_mtu_t mtu_cap;	/* Maximum MTU Supported */
  u_int16_t max_gid;	/* Maximum number of GIDs for the port */
  u_int16_t max_pkey;	/* Maximum pkeys for the port */
  MT_bool    g0;        /* ADDED FOR NEW INIT_IB */
  IB_guid_t  guid0;     /* ADDED FOR NEW INIT_IB */
}
THH_port_init_props_t;

typedef struct {
    MT_bool    rqk;               /* reset QKey Violation counter */
    u_int32_t  capability_mask;   /*  PortInfo Capability Mask */
} THH_set_ib_props_t;

#if 0
typedef struct {   /* To be used for MODIFY_HCA */

} THH_port_props_t;
#endif

typedef struct {
  u_int8_t log_max_qp; /* Log2 of the Maximum number of QPs supported */
  u_int8_t log2_rsvd_qps; /* Log2 of the number of QPs reserved for firmware use */
  u_int8_t log_max_qp_sz; /* Log2 of the maximum WQEs allowed on the RQ or the SQ */
  u_int8_t log_max_srqs; /* Log2 of the Maximum number of SRQs supported */
  u_int8_t log2_rsvd_srqs; /* Log2 of the number of SRQs reserved for firmware use */
  u_int8_t log_max_srq_sz; /* Log2 of the maximum WQEs allowed on the SRQ */
  u_int8_t log_max_ee;	/* Log2 of the Maximum number of EE contexts supported */
  u_int8_t log2_rsvd_ees; /* Log2 of the number of EECs reserved for firmware use */
  u_int8_t log_max_cq;	/* Log2 of the Maximum number of CQs supported */
  u_int8_t log2_rsvd_cqs; /* Log2 of the number of CQs reserved for firmware use */
  u_int8_t log_max_cq_sz;	/* Log2 of the Maximum CQEs allowed in a CQ */
  u_int8_t log_max_eq;	/* Log2 of the Maximum number of EQs */
  u_int8_t num_rsvd_eqs; /* The number of EQs reserved for firmware use */
  u_int8_t log_max_mpts;	/* Log2 of the Maximum number of MPT entries (the number of Regions/Windows) */
  u_int8_t log_max_mtt_seg;	/* Log2 of the Maximum number of MTT segments */
  u_int8_t log2_rsvd_mrws; /* Log2 of the number of MPTs reserved for firmware use */
  u_int8_t log_max_mrw_sz;	/* Log2 of the Maximum Size of Memory Region/Window */
  u_int8_t log2_rsvd_mtts; /* Log2 of the number of MTT segments reserved for firmware use */
  u_int8_t log_max_av; /* Log2 of the Maximum number of Address Vectors */
  u_int8_t log_max_ra_res_qp; /* Log2 of the Maximum number of outstanding RDMA read/Atomic per QP as a responder */
  u_int8_t log_max_ra_req_qp; /* Log2 of the maximum number of outstanding RDMA read/Atomic per QP as a requester */
  u_int8_t log_max_ra_res_global; /* Log2 of the maximum number of RDMA read/atomic operations the HCA responder can support globally. That implies the RDB table size. */
  u_int8_t local_ca_ack_delay; /* The Local CA ACK Delay. This is the value recommended to be returned in Query HCA verb.
                               The delay value in microseconds is computed using 4.096us * 2^(Local_CA_ACK_Delay). */
  u_int8_t log_max_gid;	/* Log2 of the maximum number of GIDs per port */
  u_int8_t log_max_pkey;	/* Log2 of the max PKey Table Size (per IB port) */
  u_int8_t num_ports;	/* Number of IB ports */
  IB_link_width_t max_port_width;	/* IB Port Width */
  IB_mtu_t max_mtu;	/* Maximum MTU Supported */
  u_int8_t max_vl; /* Maximum number of VLs per IB port excluding VL15 */
  MT_bool rc;	/* RC Transport supported */
  MT_bool uc;	/* UC Transport Supported */
  MT_bool ud;	/* UD Transport Supported */
  MT_bool rd;	/* RD Transport Supported */
  MT_bool raw_ipv6;	/* Raw IPv6 Transport Supported */
  MT_bool raw_ether;	/* Raw Ethertype Transport Supported */
  MT_bool srq;  /* SRQ is supported */
  MT_bool pkv;	/* PKey Violation Counter Supported */
  MT_bool qkv;	/* QKey Violation Coutner Supported */
  MT_bool mw;	/* Memory windows supported */
  MT_bool apm;	/* Automatic Path Migration Supported */
  MT_bool atm;	/* Atomic operations supported (atomicity is guaranteed between QPs on this HCA) */
  MT_bool rm;	/* Raw Multicast Supported */
  MT_bool avp;	/* Address Vector Port checking supported */
  MT_bool udm; /* UD Multicast supported */
  MT_bool pg;	/* Paging on demand supported */
  MT_bool r;	/* Router mode supported */
  u_int8_t log_pg_sz;	/* Log2 of system page size */
  u_int8_t uar_sz;  /* UAR Area Size = 1MB * 2^max_uar_sz */
  u_int8_t num_rsvd_uars; /* The number of UARs reserved for firmware use */
  u_int16_t max_desc_sz;	/* Max descriptor size */
  u_int8_t max_sg; /* Maximum S/G list elements in a WQE */
  u_int8_t log_max_mcg;	/* Log2 of the maximum number of multicast groups */
  u_int8_t log_max_qp_mcg;	/* Log2 of the maximum number of QPs per multicast group */
  /*MT_bool mce;	/ * Multicast support for extended QP list. - removed */
  u_int8_t log_max_rdds; /* Log2 of the maximum number of RDDs */
  u_int8_t num_rsvd_rdds;	/* The number of RDDs reserved for firmware use  */
  u_int8_t log_max_pd; /* Log2 of the maximum number of PDs */
  u_int8_t num_rsvd_pds;	/* The number of PDs reserved for firmware use  */
  u_int16_t qpc_entry_sz;	/* QPC Entry Size for the device\;For Tavor entry size is 256 bytes */
  u_int16_t eec_entry_sz;	/* EEC Entry Size for the device\;For Tavor entry size is 256 bytes */
  u_int16_t eqpc_entry_sz;	/* Extended QPC entry size for the device\;For Tavor entry size is 32 bytes */
  u_int16_t eeec_entry_sz;	/* Extended EEC entry size for the device\;For Tavor entry size is 32 bytes */
  u_int16_t cqc_entry_sz;	/* CQC entry size for the device\;For Tavor entry size is 64 bytes */
  u_int16_t eqc_entry_sz;	/* EQ context entry size for the device\;For Tavor entry size is 64 bytes */
  u_int16_t srq_entry_sz;	/* SRQ entry size for the device\;For Tavor entry size is 32 bytes */
  u_int16_t uar_scratch_entry_sz;	/* UAR Scratchpad Entry Size\;For Tavor entry size is 32 bytes */
}
THH_dev_lim_t;

typedef struct {
  u_int16_t fw_rev_major;	/* Firmware Revision - Major */
  u_int16_t fw_rev_minor;	/* Firmware Revision - Minor */
  u_int16_t fw_rev_subminor;	/* Firmware Sub-minor version (Patch level).  */
  u_int16_t cmd_interface_rev;	/* Command Interface Interpreter Revision ID */
  u_int8_t log_max_outstanding_cmd;	/* Log2 of the maximum number of commands the HCR can support simultaneously */
  u_int64_t fw_base_addr;	/* Physical Address of Firmware Area in DDR Memory */
  u_int64_t fw_end_addr;	/* End of firmware address in DDR memory */
  u_int64_t error_buf_start;	/* Read Only buffer for catastrofic error reports (phys addr) */
  u_int32_t error_buf_size;      /* size of catastrophic error report buffer in words */
}
THH_fw_props_t;

typedef struct {
  u_int32_t vendor_id;	/* Adapter vendor ID */
  u_int32_t device_id;  /* Adapter Device ID */
  u_int32_t revision_id;  /* Adapter Revision ID */
  u_int8_t intapin;	/* Interrupt Signal ID of HCA device pin that is connected to the INTA trace in the HCA board.
                       0..39 and 63 are valid values. 255 means INTA trace in board is not connected to the HCA device.
                       All other values are reserved */
}
THH_adapter_props_t;

typedef struct {
  u_int64_t ddr_start_adr; /* DDR memory start address */
  u_int64_t ddr_end_adr;	/* DDR memory end address (excluding DDR memory reserved for firmware) */
  MT_bool dh;	/* When Set DDR is Hidden and cannot be accessed from PCI bus */
  THH_dim_integrity_t di;
  THH_dim_apm_t ap;
  THH_dim_info_t dimm0;
  THH_dim_info_t dimm1;
  THH_dim_info_t dimm2;
  THH_dim_info_t dimm3;
}
THH_ddr_props_t;


typedef struct {
  u_int32_t next_gid_index;
  IB_gid_t  mgid;       /* Group's GID */
  u_int32_t valid_qps; /* number of QPs in given group (size of qps array) */
  IB_wqpn_t *qps;      /* QPs array with valid_qps QPN entries */
}
THH_mcg_entry_t;

#endif /* H_CMD_TYPES_H */

