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
#ifndef H_VAPI_TYPES_H
#define H_VAPI_TYPES_H

#include <mtl_common.h>
#include <ib_defs.h>


/* This is same as constant in MDOAL and MDHAL */
#define HCA_MAXNAME 32


/*
 * HCA Types
 *
 *
 */
typedef     char            VAPI_hca_id_t[HCA_MAXNAME]; /* NULL terminated string of up to HCA_MAXNAME-1 chars */
typedef     u_int32_t       VAPI_hca_hndl_t;      /* HCA handle                */
typedef     MT_ulong_ptr_t   VAPI_pd_hndl_t;       /* Protection Domain handle  */
typedef     MT_ulong_ptr_t   VAPI_ud_av_hndl_t;    /* UD Address Vector Handle  */
typedef     MT_ulong_ptr_t   VAPI_cq_hndl_t;       /* Completion Queue handle   */
typedef     MT_ulong_ptr_t   VAPI_qp_hndl_t;       /* Queue Pair handle         */
typedef     MT_ulong_ptr_t   VAPI_srq_hndl_t;      /* Shared Receive Queue handle */
typedef     u_int32_t        VAPI_devmem_hndl_t;
typedef     u_int32_t       VAPI_mr_hndl_t;       /* Memory Region Handle      */
typedef     MT_ulong_ptr_t   VAPI_mw_hndl_t;       /* Memory Window Handle      */
typedef     u_int32_t       VAPI_eec_hndl_t;      /* E2E Context Handle        */
typedef     u_int32_t       VAPI_pkey_ix_t;       /* Pkey index                */
typedef     u_int32_t       VAPI_rdd_hndl_t;      /* RD domain handle          */
typedef     u_int32_t       VAPI_key_counter_t;   /* QKEY/PKEY violation counter type */
typedef     IB_wqpn_t       VAPI_qp_num_t;        /* QP number (24 bits)       */
typedef     IB_eecn_t       VAPI_eec_num_t;       /* EEC number (24 bits)      */
typedef     u_int8_t        VAPI_sched_queue_t;   /* Schedule queue index      */
typedef     IB_psn_t        VAPI_psn_t;           /* PSN number (24 bits)      */
typedef     IB_qkey_t       VAPI_qkey_t;          /* QKey (32 bits)            */
typedef     u_int8_t        VAPI_retry_count_t;   /* Number of retries         */
typedef     u_int8_t       VAPI_timeout_t;        /* Timeout= 4.096usec * 2^<this_type> */
typedef     u_int32_t       VAPI_cqe_num_t;       /* number of entries in CQ*/
typedef     u_int32_t       VAPI_lkey_t;          /* L Key                     */
typedef     IB_rkey_t       VAPI_rkey_t;          /* R Key                     */                             
typedef     IB_virt_addr_t  VAPI_virt_addr_t;     /* Virtual Address (/Length) */
typedef     u_int64_t       VAPI_phy_addr_t;      /* Physical address (/length)*/
typedef     u_int64_t       VAPI_size_t;
typedef     u_int64_t       VAPI_wr_id_t;         /* Work request id           */
typedef     u_int32_t       VAPI_imm_data_t;      /* Immediate data            */
typedef     u_int16_t       VAPI_ethertype_t;     /* Ethertype                 */
/* TBD: Those two types below may be removed... (check references) */
typedef     IB_gid_t        VAPI_gid_t;           /* GID                       */
typedef     IB_pkey_t       VAPI_pkey_t;          /* PKey (16 bits)            */

/* Use the following macro to init handles and denote uninitialized handles */
#define VAPI_INVAL_HNDL 0xFFFFFFFF
#define VAPI_INVAL_PD_HNDL VAPI_INVAL_HNDL
#define VAPI_INVAL_SRQ_HNDL VAPI_INVAL_HNDL
#define VAPI_INVAL_PD_HNDL VAPI_INVAL_HNDL

#define EVAPI_DEFAULT_AVS_PER_PD  0xFFFFFFFF

/* HCA Cap Flags */
typedef enum {
  VAPI_RESIZE_OUS_WQE_CAP     = 1, 
  VAPI_BAD_PKEY_COUNT_CAP     = (1<<1), 
  VAPI_BAD_QKEY_COUNT_CAP     = (1<<2),
  VAPI_RAW_MULTI_CAP          = (1<<3), 
  VAPI_AUTO_PATH_MIG_CAP      = (1<<4),
  VAPI_CHANGE_PHY_PORT_CAP    = (1<<5),   
  VAPI_UD_AV_PORT_ENFORCE_CAP = (1<<6),   /* IBTA comment #1821 */
  VAPI_CURR_QP_STATE_MOD_CAP  = (1<<7),   /*IB Spec 1.09 sec 11.2.1.2 */
  VAPI_SHUTDOWN_PORT_CAP      = (1<<8),   /*IB Spec 1.09 sec 11.2.1.2 */
  VAPI_INIT_TYPE_CAP          = (1<<9),   /*IB Spec 1.09 sec 11.2.1.2 */
  VAPI_PORT_ACTIVE_EV_CAP     = (1<<10),  /*IB Spec 1.09 sec 11.2.1.2 */
  VAPI_SYS_IMG_GUID_CAP       = (1<<11),  /*IB Spec 1.09 sec 11.2.1.2 */
  VAPI_RC_RNR_NAK_GEN_CAP     = (1<<12)   /*IB Spec 1.09 sec 11.2.1.2 */
} VAPI_hca_cap_flags_t;


/* HCA attributes mask enumeration */
typedef enum {
  HCA_ATTR_IS_SM              = 1,
  HCA_ATTR_IS_SNMP_TUN_SUP    = 2,
  HCA_ATTR_IS_DEV_MGT_SUP     = 4,
  HCA_ATTR_IS_VENDOR_CLS_SUP  = 8,
  HCA_ATTR_MAX                = 16 /*Dummy enum entry: always keep it the last one */
} VAPI_hca_attr_mask_enum_t;


/* HCA attributes mask */
typedef u_int32_t VAPI_hca_attr_mask_t;

#define HCA_ATTR_MASK_CLR_ALL(mask)   ((mask)=0)
#define HCA_ATTR_MASK_SET_ALL(mask)   ((mask)=(HCA_ATTR_MAX-1))
#define HCA_ATTR_MASK_SET(mask,attr)  ((mask)|=(attr))
#define HCA_ATTR_MASK_CLR(mask,attr)  ((mask)&=(~(attr)))
#define HCA_ATTR_IS_FLAGS_SET(mask)   (((mask)&(\
        HCA_ATTR_IS_SM|\
        HCA_ATTR_IS_SNMP_TUN_SUP|\
        HCA_ATTR_IS_DEV_MGT_SUP|\
        HCA_ATTR_IS_VENDOR_CLS_SUP))!=0)
#define HCA_ATTR_IS_SET(mask,attr)    (((mask)&(attr))!=0)

/* QP attributes mask enumeration */
typedef enum {
  QP_ATTR_QP_STATE            = 0x1,   		/* QP next state      										*/
  QP_ATTR_EN_SQD_ASYN_NOTIF   = 0x2,   		/* Enable SQD affiliated asynchronous event notification 	*/
  QP_ATTR_QP_NUM              = 0x4,		/* Queue Pair Number. [Mellanox specific] 					*/
  QP_ATTR_REMOTE_ATOMIC_FLAGS = 0x8,		/* Enable/Disable RDMA and atomic 							*/
  QP_ATTR_PKEY_IX             = 0x10,		/* Primary PKey index                 						*/
  QP_ATTR_PORT                = 0x20,		/* Primary port                       						*/
  QP_ATTR_QKEY                = 0x40,		/* QKey (UD/RD only) 										*/
  QP_ATTR_AV                  = 0x80,		/* Primary remote node address vector (RC/UC QP only)		*/
  QP_ATTR_PATH_MTU            = 0x100,   	/* Path MTU : 6 bits (connected services only) 				*/
  QP_ATTR_TIMEOUT             = 0x200,   	/* Local Ack Timeout (RC only) 								*/
  QP_ATTR_RETRY_COUNT         = 0x400,    	/* retry count     (RC only) 								*/
  QP_ATTR_RNR_RETRY           = 0x800,    	/* RNR retry count (RC only) 								*/
  QP_ATTR_RQ_PSN              = 0x1000,   	/* Packet Sequence Number for RQ                      		*/
  QP_ATTR_QP_OUS_RD_ATOM      = 0x2000,   	/* Maximum number of oust. RDMA read/atomic as target 		*/
  QP_ATTR_ALT_PATH            = 0x4000,  	/* Alternate remote node address vector               		*/
  QP_ATTR_RSRV_1              = 0x8000,	   	/* reserved 												*/
  QP_ATTR_RSRV_2              = 0x10000,  	/* reserved 								*/
  QP_ATTR_RSRV_3              = 0x20000,  	/* reserved 								*/
  QP_ATTR_RSRV_4              = 0x40000,  	/* reserved 								*/
  QP_ATTR_RSRV_5              = 0x80000, 	/* reserved                          		*/
  QP_ATTR_RSRV_6              = 0x100000, 	/* reserved                       					*/
  QP_ATTR_MIN_RNR_TIMER       = 0x200000, 	/* Minimum RNR NAK timer                              		*/
  QP_ATTR_SQ_PSN              = 0x400000, 	/* Packet sequence number for SQ                      		*/
  QP_ATTR_OUS_DST_RD_ATOM     = 0x800000, 	/* Number of outstanding RDMA rd/atomic ops at destination	*/
  QP_ATTR_PATH_MIG_STATE      = 0x1000000, 	/* Migration state                                    		*/
  QP_ATTR_CAP                 = 0x2000000, 	/* QP capabilities max_sq/rq_ous_wr only valid        		*/
  QP_ATTR_DEST_QP_NUM         = 0x4000000, 	/* Destination QP number (RC/UC)                      		*/
  QP_ATTR_SCHED_QUEUE         = 0x8000000   /* Schedule queue for QoS association */
} VAPI_qp_attr_mask_enum_t;        

/* QP attributes mask */
typedef u_int32_t   VAPI_qp_attr_mask_t;

#define QP_ATTR_MASK_CLR_ALL(mask)  ((mask)=0)
#define QP_ATTR_MASK_SET_ALL(mask)  ((mask)=(0x07FFFFFF))
#define QP_ATTR_MASK_SET(mask,attr) ((mask)=((mask)|(attr)))
#define QP_ATTR_MASK_CLR(mask,attr) ((mask)=((mask)&(~(attr))))
#define QP_ATTR_IS_SET(mask,attr)   (((mask)&(attr))!=0)


/* HCA Atomic Operation Capabilities */
typedef enum {
  VAPI_ATOMIC_CAP_NONE, /* No Atomic ops supported */
  VAPI_ATOMIC_CAP_HCA,  /* Atomic cap supported within this HCA QPs */
  VAPI_ATOMIC_CAP_GLOB  /* Atomicity supported among all entities in this sytem */
} VAPI_atomic_cap_t;

/* Signalling type */
typedef enum {
  VAPI_SIGNAL_ALL_WR, 
  VAPI_SIGNAL_REQ_WR
} VAPI_sig_type_t;

/* Transport Service Type */
enum {
  VAPI_TS_RC=IB_TS_RC,
  VAPI_TS_RD=IB_TS_RD,
  VAPI_TS_UC=IB_TS_UC,
  VAPI_TS_UD=IB_TS_UD,
  VAPI_TS_RAW=IB_TS_RAW,
  VAPI_NUM_TS_TYPES
}; 
typedef IB_ts_t VAPI_ts_type_t;

/* The following value to be used for reserved GRH buffer space in UD RQ */
/* (The offset of the payload in buffers posted to the UD RQ)            */
#define VAPI_GRH_LEN 40

/* QP state   */
enum {
  VAPI_RESET,VAPI_INIT,VAPI_RTR,VAPI_RTS,VAPI_SQD,VAPI_SQE,VAPI_ERR
};
typedef u_int32_t VAPI_qp_state_t;

/* Migration state  */
typedef enum {
  VAPI_MIGRATED, VAPI_REARM, VAPI_ARMED
} VAPI_mig_state_t;

/* Special QP Types */
enum { 
  VAPI_REGULAR_QP= 0, /* Encoding for non-special QP */
  VAPI_SMI_QP, VAPI_GSI_QP, VAPI_RAW_IPV6_QP, VAPI_RAW_ETY_QP 
};
typedef u_int32_t VAPI_special_qp_t;

/* Just a generic name for a type used to identify QP type */
typedef VAPI_special_qp_t VAPI_qp_type_t; 

/* RDMA/Atomic Access Control */
typedef enum {
  VAPI_EN_REM_WRITE=1, VAPI_EN_REM_READ=2, VAPI_EN_REM_ATOMIC_OP=4
} 
VAPI_rdma_atom_acl_enum_t;
typedef u_int32_t VAPI_rdma_atom_acl_t;

/* Memory region/window types */
typedef enum {
  VAPI_MR,    /* Memory region */
  VAPI_MW,    /* Memory Window */
  VAPI_MPR,   /* Physical memory region */
  VAPI_MSHAR  /* Shared memory region */
} VAPI_mrw_type_t;

/* Remote node address type */
typedef enum {
  VAPI_RNA_RD, 
  VAPI_RNA_UD, 
  VAPI_RNA_RAW_ETY, 
  VAPI_RNA_RAW_IPV6 
} VAPI_remote_node_addr_type_t;

/* Memory region/window ACLs */
enum {
  VAPI_EN_LOCAL_WRITE=  1, 
  VAPI_EN_REMOTE_WRITE= 1<<1, 
  VAPI_EN_REMOTE_READ=  1<<2, 
  VAPI_EN_REMOTE_ATOM=  1<<3, 
  VAPI_EN_MEMREG_BIND=  1<<4
};
typedef u_int32_t VAPI_mrw_acl_t;

/* Memory region change type */
typedef enum {
  VAPI_MR_CHANGE_TRANS= 1,
  VAPI_MR_CHANGE_PD=    1<<1,
  VAPI_MR_CHANGE_ACL=   1<<2
} VAPI_mr_change_flags_t; 

typedef u_int32_t VAPI_mr_change_t; /*  VAPI_mr_change_flags_t combination */

typedef enum {
    EVAPI_DEVMEM_EXT_DRAM   /* External attached SDRAM */
}EVAPI_devmem_type_t;


/* Work requests opcodes */
/* Note. The following enum must be maintained zero based without holes */
typedef enum {
  VAPI_RDMA_WRITE,
  VAPI_RDMA_WRITE_WITH_IMM,
  VAPI_SEND,
  VAPI_SEND_WITH_IMM,
  VAPI_RDMA_READ,
  VAPI_ATOMIC_CMP_AND_SWP,
  VAPI_ATOMIC_FETCH_AND_ADD,
  VAPI_RECEIVE,
  VAPI_NUM_OPCODES
} VAPI_wr_opcode_t;

/* Completion Opcodes */
typedef enum {
  VAPI_CQE_SQ_SEND_DATA,        
  VAPI_CQE_SQ_RDMA_WRITE,
  VAPI_CQE_SQ_RDMA_READ,
  VAPI_CQE_SQ_COMP_SWAP,
  VAPI_CQE_SQ_FETCH_ADD,
  VAPI_CQE_SQ_BIND_MRW,
  VAPI_CQE_RQ_SEND_DATA,
  VAPI_CQE_RQ_RDMA_WITH_IMM,    /* For RDMA Write Only */
  VAPI_CQE_INVAL_OPCODE = 0xFFFFFFFF  /* special value to return on CQE with error */
} VAPI_cqe_opcode_t;


/* Work completion status */
enum {
  VAPI_SUCCESS = IB_COMP_SUCCESS,
  VAPI_LOC_LEN_ERR = IB_COMP_LOC_LEN_ERR,
  VAPI_LOC_QP_OP_ERR = IB_COMP_LOC_QP_OP_ERR,
  VAPI_LOC_EE_OP_ERR = IB_COMP_LOC_EE_OP_ERR,
  VAPI_LOC_PROT_ERR = IB_COMP_LOC_PROT_ERR,
  VAPI_WR_FLUSH_ERR = IB_COMP_WR_FLUSH_ERR,
  VAPI_MW_BIND_ERR = IB_COMP_MW_BIND_ERR,
  VAPI_BAD_RESP_ERR = IB_COMP_BAD_RESP_ERR,
  VAPI_LOC_ACCS_ERR = IB_COMP_LOC_ACCS_ERR,
  VAPI_REM_INV_REQ_ERR = IB_COMP_REM_INV_REQ_ERR,
  VAPI_REM_ACCESS_ERR = IB_COMP_REM_ACCESS_ERR,
  VAPI_REM_OP_ERR = IB_COMP_REM_OP_ERR,
  VAPI_RETRY_EXC_ERR = IB_COMP_RETRY_EXC_ERR,
  VAPI_RNR_RETRY_EXC_ERR = IB_COMP_RNR_RETRY_EXC_ERR,
  VAPI_LOC_RDD_VIOL_ERR = IB_COMP_LOC_RDD_VIOL_ERR,
  VAPI_REM_INV_RD_REQ_ERR = IB_COMP_REM_INV_RD_REQ_ERR,
  VAPI_REM_ABORT_ERR= IB_COMP_REM_ABORT_ERR,
  VAPI_INV_EECN_ERR = IB_COMP_INV_EECN_ERR,
  VAPI_INV_EEC_STATE_ERR = IB_COMP_INV_EEC_STATE_ERR,
/*  VAPI_COMP_LOC_TOUT = IB_COMP_LOC_TOUT,*/ /* Use VAPI_RETRY_EXC_ERR instead */
/*  VAPI_COMP_RNR_TOUT = IB_COMP_RNR_TOUT,*/ /* Use VAPI_RNR_RETRY_EXC_ERR instead */

  VAPI_COMP_FATAL_ERR = IB_COMP_FATAL_ERR,
  VAPI_COMP_GENERAL_ERR = IB_COMP_GENERAL_ERR
};
typedef u_int32_t VAPI_wc_status_t;

/* Vendor specific error syndrome */
typedef u_int32_t EVAPI_vendor_err_syndrome_t;

/* work request completion type */
typedef enum {
  VAPI_SIGNALED, VAPI_UNSIGNALED
} VAPI_comp_type_t;

/* Completion Notification Type */
typedef enum {
  VAPI_NOTIF_NONE,  /* No completion notification requested */
  VAPI_SOLIC_COMP,    /* Notify on solicited completion event only */
  VAPI_NEXT_COMP   /* Notify on next completion */
} VAPI_cq_notif_type_t;

typedef VAPI_cq_hndl_t  EVAPI_compl_handler_hndl_t;       /* EVAPI completion handler handle */

typedef u_int32_t VAPI_k_cq_hndl_t; /* Kernel level CQ access */

/* Completion Event Handler Pointer */
typedef void (MT_API *VAPI_completion_event_handler_t)(
                                                /*IN*/ VAPI_hca_hndl_t hca_hndl,
                                                /*IN*/ VAPI_cq_hndl_t cq_hndl,
                                                /*IN*/ void* private_data
                                               );

/* CQ destruction callback */
typedef void (MT_API *EVAPI_destroy_cq_cbk_t)(
                                        IN VAPI_hca_hndl_t k_hca_hndl,
                                        IN VAPI_k_cq_hndl_t k_cq_hndl,
                                        IN void* private_data
                                       );


typedef u_int32_t VAPI_k_qp_hndl_t; /* Kernel level QP access */

typedef void (MT_API *EVAPI_destroy_qp_cbk_t)(
                                        IN VAPI_hca_hndl_t k_hca_hndl,
                                        IN VAPI_k_qp_hndl_t k_qp_hndl,
                                        IN void* private_data
                                       );
 

/* Event Record Event Types, valid modifier mentionned where unclear */
typedef enum {
  VAPI_QP_PATH_MIGRATED,             /*QP*/
  VAPI_EEC_PATH_MIGRATED,            /*EEC*/
  VAPI_QP_COMM_ESTABLISHED,          /*QP*/
  VAPI_EEC_COMM_ESTABLISHED,         /*EEC*/ 
  VAPI_SEND_QUEUE_DRAINED,           /*QP*/
  VAPI_RECEIVE_QUEUE_DRAINED,        /*QP (Last WQE Reached) */
  VAPI_SRQ_LIMIT_REACHED,            /*SRQ*/
  VAPI_SRQ_CATASTROPHIC_ERROR,       /*SRQ*/
  VAPI_CQ_ERROR,                     /*CQ*/
  VAPI_LOCAL_WQ_INV_REQUEST_ERROR,   /*QP*/
  VAPI_LOCAL_WQ_ACCESS_VIOL_ERROR,   /*QP*/
  VAPI_LOCAL_WQ_CATASTROPHIC_ERROR,  /*QP*/
  VAPI_PATH_MIG_REQ_ERROR,           /*QP*/
  VAPI_LOCAL_EEC_CATASTROPHIC_ERROR, /*EEC*/
  VAPI_LOCAL_CATASTROPHIC_ERROR,     /*none*/
  VAPI_PORT_ERROR,                   /*PORT*/
  VAPI_PORT_ACTIVE                   /*PORT*/
} VAPI_event_record_type_t;


typedef enum {
  VAPI_EV_SYNDROME_NONE, /* no special syndrom for this event */
  VAPI_CATAS_ERR_FW_INTERNAL,
  VAPI_CATAS_ERR_EQ_OVERFLOW,
  VAPI_CATAS_ERR_MISBEHAVED_UAR_PAGE,
  VAPI_CATAS_ERR_UPLINK_BUS_ERR, 
  VAPI_CATAS_ERR_HCA_DDR_DATA_ERR,
  VAPI_CATAS_ERR_INTERNAL_PARITY_ERR,
  VAPI_CATAS_ERR_MASTER_ABORT,
  VAPI_CATAS_ERR_GO_BIT,
  VAPI_CATAS_ERR_CMD_TIMEOUT,
  VAPI_CATAS_ERR_FATAL_CR,       /* unexpected read from CR space */
  VAPI_CATAS_ERR_FATAL_TOKEN,    /* invalid token on command completion */
  VAPI_CATAS_ERR_GENERAL,        /* reason is not known */
  VAPI_CQ_ERR_OVERRUN,
  VAPI_CQ_ERR_ACCESS_VIOL
} VAPI_event_syndrome_t;

 /* Event Record  */
typedef struct {
  VAPI_event_record_type_t    type;           /* event record type            */
  VAPI_event_syndrome_t  syndrome;
  union {
    VAPI_qp_hndl_t              qp_hndl;        /* Affiliated QP handle         */
    VAPI_srq_hndl_t             srq_hndl;       /* Affiliated SRQ handle        */
    VAPI_eec_hndl_t             eec_hndl;       /* Affiliated EEC handle        */
    VAPI_cq_hndl_t              cq_hndl;        /* Affiliated CQ handle         */
    IB_port_t                   port_num;       /* Affiliated Port number       */
  } modifier;
}  VAPI_event_record_t;

/* Async Event Handler */
typedef void (MT_API *VAPI_async_event_handler_t)(
                                           /*IN*/ VAPI_hca_hndl_t hca_hndl,
                                           /*IN*/ VAPI_event_record_t *event_record_p, 
                                           /*IN*/ void* private_data
                                          );

typedef u_int32_t EVAPI_async_handler_hndl_t;  /* EVAPI async event handler handle */


/* HCA Verbs returns values */
#define VAPI_ERROR_LIST \
VAPI_ERROR_INFO( VAPI_OK,                    = 0  ,"Operation Completed Successfully") \
VAPI_ERROR_INFO( VAPI_EGEN,                  =-255,"Generic error") \
VAPI_ERROR_INFO( VAPI_EFATAL,                EMPTY,"Fatal error (Local Catastrophic Error)") \
VAPI_ERROR_INFO( VAPI_EAGAIN,                EMPTY,"Resources temporary unavailable") \
VAPI_ERROR_INFO( VAPI_ENOMEM,		             EMPTY,"Not enough memory") \
VAPI_ERROR_INFO( VAPI_EBUSY,                 EMPTY,"Resource is busy") \
VAPI_ERROR_INFO( VAPI_ETIMEOUT,              EMPTY,"Operation timedout") \
VAPI_ERROR_INFO( VAPI_EINTR,                 EMPTY,"Operation interrupted") \
VAPI_ERROR_INFO( VAPI_EPERM,                 EMPTY,"Not enough permissions to perform operation")\
VAPI_ERROR_INFO( VAPI_ENOSYS,                EMPTY,"Not implemented") \
VAPI_ERROR_INFO( VAPI_ESYSCALL,              EMPTY,"Error in an underlying O/S call") \
VAPI_ERROR_INFO( VAPI_EINVAL_PARAM,          EMPTY,"Invalid Parameter") \
VAPI_ERROR_INFO( VAPI_EINVAL_HCA_HNDL,       EMPTY,"Invalid HCA Handle.") \
VAPI_ERROR_INFO( VAPI_EINVAL_HCA_ID,         EMPTY,"Invalid HCA identifier") \
VAPI_ERROR_INFO( VAPI_EINVAL_COUNTER,        EMPTY,"Invalid key counter index") \
VAPI_ERROR_INFO( VAPI_EINVAL_COUNT_VAL,      EMPTY,"Invalid counter value") \
VAPI_ERROR_INFO( VAPI_EINVAL_PD_HNDL,        EMPTY,"Invalid Protection Domain") \
VAPI_ERROR_INFO( VAPI_EINVAL_RD_UNSUPPORTED, EMPTY,"RD is not supported") \
VAPI_ERROR_INFO( VAPI_EINVAL_RDD_HNDL,       EMPTY,"Invalid Reliable Datagram Domain") \
VAPI_ERROR_INFO( VAPI_EINVAL_AV_HNDL,        EMPTY,"Invalid Address Vector Handle") \
VAPI_ERROR_INFO( VAPI_E2BIG_WR_NUM,          EMPTY,"Max. WR number exceeds capabilities") \
VAPI_ERROR_INFO( VAPI_E2BIG_SG_NUM,          EMPTY,"Max. SG size exceeds capabilities") \
VAPI_ERROR_INFO( VAPI_EINVAL_SERVICE_TYPE,   EMPTY,"Invalid Service Type") \
VAPI_ERROR_INFO( VAPI_ENOSYS_ATTR,           EMPTY,"Unsupported attribute") \
VAPI_ERROR_INFO( VAPI_EINVAL_ATTR,           EMPTY,"Can not change attribute") \
VAPI_ERROR_INFO( VAPI_ENOSYS_ATOMIC,         EMPTY,"Atomic operations not supported") \
VAPI_ERROR_INFO( VAPI_EINVAL_PKEY_IX,        EMPTY,"Pkey index out of range") \
VAPI_ERROR_INFO( VAPI_EINVAL_PKEY_TBL_ENTRY, EMPTY,"Pkey index point to invalid Pkey") \
VAPI_ERROR_INFO( VAPI_EINVAL_QP_HNDL,        EMPTY,"Invalid QP Handle") \
VAPI_ERROR_INFO( VAPI_EINVAL_QP_STATE,       EMPTY,"Invalid QP State") \
VAPI_ERROR_INFO( VAPI_EINVAL_SRQ_HNDL,       EMPTY,"Invalid SRQ Handle") \
VAPI_ERROR_INFO( VAPI_ESRQ,                  EMPTY,"SRQ is in error state") \
VAPI_ERROR_INFO( VAPI_EINVAL_EEC_HNDL,       EMPTY,"Invalid EE-Context Handle") \
VAPI_ERROR_INFO( VAPI_EINVAL_MIG_STATE,      EMPTY,"Invalid Path Migration State") \
VAPI_ERROR_INFO( VAPI_EINVAL_MTU,            EMPTY,"MTU violation") \
VAPI_ERROR_INFO( VAPI_EINVAL_PORT,           EMPTY,"Invalid Port Number") \
VAPI_ERROR_INFO( VAPI_EINVAL_RNR_NAK_TIMER,  EMPTY,"Invalid RNR NAK timer field") \
VAPI_ERROR_INFO( VAPI_EINVAL_LOCAL_ACK_TIMEOUT,  EMPTY,"Invalid Local ACK timeout field") \
VAPI_ERROR_INFO( VAPI_E2BIG_RAW_DGRAM_NUM,   EMPTY,"Number of raw datagrams QP exeeded") \
VAPI_ERROR_INFO( VAPI_EINVAL_QP_TYPE,        EMPTY,"Invalid special QP type") \
VAPI_ERROR_INFO( VAPI_ENOSYS_RAW,            EMPTY,"Raw datagram QP not supported") \
VAPI_ERROR_INFO( VAPI_EINVAL_CQ_HNDL,        EMPTY,"Invalid Completion Queue Handle") \
VAPI_ERROR_INFO( VAPI_E2BIG_CQ_NUM,          EMPTY,"Number of entries in CQ exceeds Cap.") \
VAPI_ERROR_INFO( VAPI_CQ_EMPTY,              EMPTY,"CQ is empty") \
VAPI_ERROR_INFO( VAPI_EINVAL_VA,             EMPTY,"Invalid Virtual Address") \
VAPI_ERROR_INFO( VAPI_EINVAL_LEN,            EMPTY,"Invalid length") \
VAPI_ERROR_INFO( VAPI_EINVAL_ACL,            EMPTY,"Invalid ACL") \
VAPI_ERROR_INFO( VAPI_EINVAL_PADDR,          EMPTY,"Invalid physical address") \
VAPI_ERROR_INFO( VAPI_EINVAL_OFST,           EMPTY,"Invalid offset") \
VAPI_ERROR_INFO( VAPI_EINVAL_MR_HNDL,        EMPTY,"Invalid Memory Region Handle") \
VAPI_ERROR_INFO( VAPI_EINVAL_MW_HNDL,        EMPTY,"Invalid Memory Window Handle") \
VAPI_ERROR_INFO( VAPI_EINVAL_OP,             EMPTY,"Invalid operation") \
VAPI_ERROR_INFO( VAPI_EINVAL_NOTIF_TYPE,     EMPTY,"Invalid completion notification type") \
VAPI_ERROR_INFO( VAPI_EINVAL_SG_FMT,         EMPTY,"Invalid scatter/gather list format") \
VAPI_ERROR_INFO( VAPI_EINVAL_SG_NUM,         EMPTY,"Invalid scatter/gather list length") \
VAPI_ERROR_INFO( VAPI_E2BIG_MCG_SIZE,        EMPTY,"Number of QPs attached to multicast groups exceeded") \
VAPI_ERROR_INFO( VAPI_EINVAL_MCG_GID,        EMPTY,"Invalid Multicast group GID") \
VAPI_ERROR_INFO( VAPI_EOL,                   EMPTY,"End Of List") \
VAPI_ERROR_INFO( VAPI_ERROR_MAX,             EMPTY,"Dummy max error code : put all error codes before it") \
           
enum {
#define VAPI_ERROR_INFO(A, B, C) A B,
  VAPI_ERROR_LIST
#undef VAPI_ERROR_INFO
  VAPI_ERROR_DUMMY_CODE
};

typedef int32_t VAPI_ret_t;

typedef struct {
  u_int32_t          vendor_id;              /* Vendor ID */
  u_int32_t          vendor_part_id;         /* Vendor Part ID */
  u_int32_t          hw_ver;                 /* Hardware Version */
  u_int64_t          fw_ver;                 /* Device's firmware version (device specific) */
} VAPI_hca_vendor_t; 

/* HCA Port properties (port db) */
typedef struct {
  IB_mtu_t        max_mtu;                  /* Max MTU */
  u_int32_t       max_msg_sz;               /* Max message size                   */
  IB_lid_t        lid;                      /* Base IB_LID.                       */
  u_int8_t        lmc;                      /* IB_LMC for port.                   */ 
  IB_port_state_t state;                    /* Port state                         */
  IB_port_cap_mask_t capability_mask; 
  u_int8_t        max_vl_num;               /* Maximum number of VL supported by this port.  */             
  VAPI_key_counter_t bad_pkey_counter;      /* Bad PKey counter (if supported) */
  VAPI_key_counter_t qkey_viol_counter;     /* QKey violation counter          */
  IB_lid_t           sm_lid;                /* IB_LID of subnet manager to be used for this prot.     */
  IB_sl_t            sm_sl;                 /* IB_SL to be used in communication with subnet manager. */
  u_int16_t       pkey_tbl_len;             /* Current size of pkey table */
  u_int16_t       gid_tbl_len;              /* Current size of GID table */
  VAPI_timeout_t  subnet_timeout;           /* Subnet Timeout for this port (see PortInfo) */
  u_int8_t        initTypeReply;            /* optional InitTypeReply value. 0 if not supported */
} VAPI_hca_port_t; 

/* HCA Capabilities Structure */
typedef struct {
  u_int32_t       max_num_qp;          /* Maximum Number of QPs supported.                   */             
  u_int32_t       max_qp_ous_wr;       /* Maximum Number of oustanding WR on any WQ.         */             
  u_int32_t       flags;               /* Various flags (VAPI_hca_cap_flags_t)               */
  u_int32_t       max_num_sg_ent;      /* Max num of scatter/gather entries for desc other than RD */
  u_int32_t       max_num_sg_ent_rd;   /* Max num of scatter/gather entries for RD desc      */
  u_int32_t       max_num_cq;          /* Max num of supported CQs                           */
  u_int32_t       max_num_ent_cq;      /* Max num of supported entries per CQ                */
  u_int32_t       max_num_mr;          /* Maximum number of memory region supported.         */             
  u_int64_t       max_mr_size;         /* Largest contigous block of memory region in bytes. */             
  u_int32_t       max_pd_num;          /* Maximum number of protection domains supported.    */             
  u_int32_t       page_size_cap;       /* Largest page size supported by this HCA            */             
  IB_port_t       phys_port_num;       /* Number of physical port of the HCA.                */             
  u_int16_t       max_pkeys;           /* Maximum number of partitions supported .           */
  IB_guid_t       node_guid;           /* Node GUID for this hca                             */
  VAPI_timeout_t  local_ca_ack_delay;  /* Log2 4.096usec Max. RX to ACK or NAK delay */
  u_int8_t        max_qp_ous_rd_atom;  /* Maximum number of oust. RDMA read/atomic as target */             
  u_int8_t        max_ee_ous_rd_atom;  /* EE Maximum number of outs. RDMA read/atomic as target      */             
  u_int8_t        max_res_rd_atom;     /* Max. Num. of resources used for RDMA read/atomic as target */
  u_int8_t        max_qp_init_rd_atom; /* Max. Num. of outs. RDMA read/atomic as initiator           */
  u_int8_t        max_ee_init_rd_atom; /* EE Max. Num. of outs. RDMA read/atomic as initiator        */
  VAPI_atomic_cap_t   atomic_cap;        /* Level of Atomicity supported                */
  u_int32_t           max_ee_num;                   /* Maximum number of EEC supported.            */
  u_int32_t           max_rdd_num;                  /* Maximum number of IB_RDD supported             */
  u_int32_t           max_mw_num;                   /* Maximum Number of memory windows supported  */
  u_int32_t           max_raw_ipv6_qp;              /* Maximum number of Raw IPV6 QPs supported */ 
  u_int32_t           max_raw_ethy_qp;              /* Maximum number of Raw Ethertypes QPs supported */ 
  u_int32_t           max_mcast_grp_num;            /* Maximum Number of multicast groups           */       
  u_int32_t           max_mcast_qp_attach_num;      /* Maximum number of QP per multicast group    */ 
  u_int32_t           max_total_mcast_qp_attach_num;/* Maximum number of QPs which can be attached to a mcast grp */
  u_int32_t           max_ah_num;                   /* Maximum number of address vector handles */

  /* Extended HCA capabilities */

  /* FMRs (Fast Memory Regions) */
  u_int32_t      max_num_fmr;         /* maximum number FMRs  */
  u_int32_t      max_num_map_per_fmr; /* Maximum number of (re)maps per FMR before 
                                         an unmap operation in required */
  /* SRQs (Shared Receive Queues) */
  u_int32_t      max_num_srq;         /* Maximum number of SRQs. Zero if SRQs are not supported. */
  u_int32_t      max_wqe_per_srq;     /* Maximum number of WRs per SRQ.*/
  u_int32_t      max_srq_sentries;    /* Maximum scatter entries per SRQ WQE */
  MT_bool        srq_resize_supported;/* Ability to modify the maximum number of WRs per SRQ.*/

} VAPI_hca_cap_t;


/* HCA Properties for Modify HCA verb */
typedef struct {
  MT_bool      reset_qkey_counter;       /* TRUE=> reset counter.  FALSE=> do nothing */
  /* attributes in Capability Mask of port info that can be modified */
  MT_bool      is_sm;                  
  MT_bool      is_snmp_tun_sup;
  MT_bool      is_dev_mgt_sup;
  MT_bool      is_vendor_cls_sup;
} VAPI_hca_attr_t;  


/* Address Vector (For UD AV as well as address-path in connected QPs */
typedef struct {                              
  IB_gid_t            dgid MT_BYTE_ALIGN(4); /* Destination GID (alignment for IA64) */
  IB_sl_t             sl;              /* Service Level 4 bits      */
  IB_lid_t            dlid;            /* Destination LID           */
  u_int8_t            src_path_bits;   /* Source path bits 7 bits   */
  IB_static_rate_t    static_rate;     /* Maximum static rate : 6 bits  */

  MT_bool             grh_flag;        /* Send GRH flag             */
  /* For global destination or Multicast address:*/ 
  u_int8_t            traffic_class;   /* TClass 8 bits             */
  u_int8_t            hop_limit;       /* Hop Limit 8 bits          */
  u_int32_t           flow_label;      /* Flow Label 20 bits        */
  u_int8_t            sgid_index;      /* SGID index in SGID table  */

  IB_port_t           port;      /* egress port (valid for UD AV only) */
  /* Following IBTA comment 1567 - should match QP's when UD AV port is enforced */

} VAPI_ud_av_t; 


/* QP Capabilities */
typedef struct {
  u_int32_t          max_oust_wr_sq;   /* Max outstanding WR on the SQ */
  u_int32_t          max_oust_wr_rq;   /* Max outstanding WR on the RQ */
  u_int32_t          max_sg_size_sq;   /* Max scatter/gather descriptor entries on the SQ */
  u_int32_t          max_sg_size_rq;   /* Max scatter/gather descriptor entries on the RQ */
  u_int32_t          max_inline_data_sq;  /* Max bytes in inline data on the SQ */
  /* max_inline_data_sq is currently valid only for VAPI_query_qp (ignored for VAPI_create_qp) */
  /* In order to enlarge the max_inline_data_sq capability, enlarge the max_sg_size_sq parameter */
} VAPI_qp_cap_t;  

/* Queue Pair Creation Attributes */
typedef struct {
  VAPI_cq_hndl_t  sq_cq_hndl;      /* CQ handle for the SQ            */
  VAPI_cq_hndl_t  rq_cq_hndl;      /* CQ handle for the RQ            */
  VAPI_qp_cap_t   cap;             /* Requested QP capabilities       */
  VAPI_rdd_hndl_t rdd_hndl;        /* Reliable Datagram Domain handle */
  VAPI_sig_type_t sq_sig_type;     /* SQ Signalling type (SIGNAL_ALL_WR, SIGNAL_REQ_WR) */
  VAPI_sig_type_t rq_sig_type;     /* RQ Signalling type (SIGNAL_ALL_WR, SIGNAL_REQ_WR) [Mellanox Specific]*/
  VAPI_pd_hndl_t  pd_hndl;         /* Protection domain handle        */
  VAPI_ts_type_t  ts_type;         /* Transport Services Type         */
} VAPI_qp_init_attr_t;

typedef struct {
  VAPI_srq_hndl_t srq_hndl;  /* Set to VAPI_INVAL_SRQ_HNDL when QP is not associated with a SRQ */
} VAPI_qp_init_attr_ext_t;

/* Init. the extended attributes structure with the macro below to assure forward compatibility */
#define VAPI_QP_INIT_ATTR_EXT_T_INIT(qp_ext_attr_p)  (qp_ext_attr_p)->srq_hndl=VAPI_INVAL_SRQ_HNDL


/* Queue Pair Creation Returned actual Attributes */
typedef struct {
  VAPI_qp_num_t   qp_num;            /* QP number              */
  VAPI_qp_cap_t   cap;               /* Actual QP capabilities */
} VAPI_qp_prop_t;


/* Queue Pair Full Attributes (for modify QP) */
typedef struct {
  VAPI_qp_state_t     qp_state;            /* QP next state      */
  MT_bool             en_sqd_asyn_notif;   /* Enable SQD affiliated asynchronous event notification */
  MT_bool			  sq_draining;		   /* query only - when (qp_state == VAPI_SQD) indicates whether sq is in drain process (TRUE), or drained.*/
  VAPI_qp_num_t       qp_num;              /* Queue Pair Number. [Mellanox specific] */
  VAPI_rdma_atom_acl_t remote_atomic_flags;/* Enable/Disable RDMA and atomic */
  VAPI_qkey_t         qkey;                /* QKey (UD/RD only) */
  IB_mtu_t            path_mtu;            /* Path MTU : 6 bits (connected services only) */
  VAPI_mig_state_t    path_mig_state;      /* Migration state                                    */
  VAPI_psn_t          rq_psn;              /* Packet Sequence Number for RQ                      */
  VAPI_psn_t          sq_psn;              /* Packet sequence number for SQ                      */
  u_int8_t            qp_ous_rd_atom;      /* Maximum number of oust. RDMA read/atomic as target */
  u_int8_t            ous_dst_rd_atom;     /* Number of outstanding RDMA rd/atomic ops at destination */
  IB_rnr_nak_timer_code_t min_rnr_timer;   /* Minimum RNR NAK timer                              */
  VAPI_qp_cap_t       cap;                 /* QP capabilities max_sq/rq_ous_wr only valid        */
  VAPI_qp_num_t       dest_qp_num;         /* Destination QP number (RC/UC)                      */
  VAPI_sched_queue_t  sched_queue;         /* Schedule queue (optional) */

  /* Primary path (RC/UC only) */
  VAPI_pkey_ix_t      pkey_ix;             /* Primary PKey index                 */
  IB_port_t           port;                /* Primary port                       */
  VAPI_ud_av_t        av;                  /* Primary remote node address vector (RC/UC QP only)*/
  VAPI_timeout_t      timeout;             /* Local Ack Timeout (RC only) */
  VAPI_retry_count_t  retry_count;         /* retry count     (RC only) */
  VAPI_retry_count_t  rnr_retry;           /* RNR retry count (RC only) */
  
  /* Alternate path (RC/UC only) */
  VAPI_pkey_ix_t      alt_pkey_ix;         /* Alternative PKey index                             */
  IB_port_t           alt_port;            /* Alternative port                       */
  VAPI_ud_av_t        alt_av;              /* Alternate remote node address vector               */ 
  VAPI_timeout_t      alt_timeout;         /* Local Ack Timeout (RC only) */
} VAPI_qp_attr_t;

/* SRQ's attributes */
typedef struct {
  VAPI_pd_hndl_t     pd_hndl;     /* SRQ's PD. (Ignored on VAPI_modify_srq). */
  u_int32_t          max_outs_wr; /* Max. outstanding WQEs */
  u_int32_t          max_sentries;/* Max. scatter entries  (Ignored on VAPI_modify_srq) */
  u_int32_t          srq_limit;   /* SRQ's limit (Ignored on VAPI_create_srq) */
} VAPI_srq_attr_t;

#define VAPI_SRQ_ATTR_T_INIT(srq_attr_p)  {    \
  (srq_attr_p)->pd_hndl=VAPI_INVAL_PD_HNDL;    \
  (srq_attr_p)->max_outs_wr= 0;                \
  (srq_attr_p)->max_sentries= 0;               \
  (srq_attr_p)->srq_limit= 0;                  \
}

/* VAPI_modify_srq attributes mask */
#define VAPI_SRQ_ATTR_LIMIT       (1)
#define VAPI_SRQ_ATTR_MAX_OUTS_WR (1<<1)
typedef u_int8_t VAPI_srq_attr_mask_t;


/* Physical memory buffer */
typedef struct {
  VAPI_phy_addr_t     start;
  VAPI_phy_addr_t     size;
} VAPI_phy_buf_t;


/* Memory Region/Window */
typedef struct {
  VAPI_mrw_type_t     type;  /* But not VAPI_MW */
  VAPI_lkey_t         l_key; 
  VAPI_rkey_t         r_key;
  VAPI_virt_addr_t    start;
  VAPI_size_t         size;
  VAPI_pd_hndl_t      pd_hndl;
  VAPI_mrw_acl_t      acl; 
  /* Physical buffers list : for physical memory region only (type==VAPI_MPR) */
  MT_size_t           pbuf_list_len;
  VAPI_phy_buf_t      *pbuf_list_p;
  VAPI_phy_addr_t     iova_offset;  /* Offset of "start" in first buffer */
} VAPI_mr_t;

typedef VAPI_mr_t  VAPI_mrw_t; /* for backward compatibility */

typedef struct
{
  VAPI_lkey_t     mr_lkey; /* L-Key of memory region to bind to */
  IB_virt_addr_t  start;   /* Memory window's virtual byte address */
  VAPI_size_t     size;    /* Size of memory window in bytes */
  VAPI_mrw_acl_t  acl;     /* Access Control (R/W permission - local/remote) */
} VAPI_mw_bind_t;


/* Scatter/ Gather Entry */
typedef struct {
  VAPI_virt_addr_t    addr;
  u_int32_t           len;
  VAPI_lkey_t         lkey;
} VAPI_sg_lst_entry_t;

/* Send Request Descriptor */
typedef struct {
  VAPI_wr_id_t         id;
  VAPI_wr_opcode_t     opcode;
  VAPI_comp_type_t     comp_type;
  VAPI_sg_lst_entry_t *sg_lst_p;
  u_int32_t            sg_lst_len;
  VAPI_imm_data_t      imm_data;
  MT_bool              fence;
  VAPI_ud_av_hndl_t    remote_ah;
  VAPI_qp_num_t        remote_qp;
  VAPI_qkey_t          remote_qkey;
  VAPI_ethertype_t     ethertype;
  IB_eecn_t            eecn;
  MT_bool              set_se;
  VAPI_virt_addr_t     remote_addr;
  VAPI_rkey_t          r_key;
   /* atomic_operands */
  u_int64_t compare_add; /* First operand: Used for both "Compare & Swap" and "Fetch & Add" */
  u_int64_t swap;        /* Second operand: Used for "Compare & Swap" */
  /* Atomic's "data segment" is the scather list defined in sg_lst_p+sg_lst_len (like RDMA-Read) */
} VAPI_sr_desc_t;

/* Receive Request Descriptor */
typedef struct {
  VAPI_wr_id_t         id;
  VAPI_wr_opcode_t     opcode;      /* RECEIVE */
  VAPI_comp_type_t     comp_type;   /* Mellanox Specific */
  VAPI_sg_lst_entry_t *sg_lst_p;
  u_int32_t            sg_lst_len;
} VAPI_rr_desc_t;

/* Remote node address for completion entry */
typedef struct {
  VAPI_remote_node_addr_type_t  type;
  IB_lid_t                      slid;
  IB_sl_t                       sl;

  union {
    VAPI_qp_num_t      qp;  /* source QP (valid on type==RD,UD) */
    VAPI_ethertype_t   ety; /* ethertype (valid on type==RAW_ETY) */
  } qp_ety;

  union {
    VAPI_eec_num_t     loc_eecn; /* local EEC number (valid on type==RD) */
    u_int8_t           dst_path_bits; /* dest path bits (valid on type==UD and RAW* ) */
  } ee_dlid;
} VAPI_remote_node_addr_t;


/* Work Completion Descriptor */
typedef struct {
  VAPI_wc_status_t        status;
  VAPI_wr_id_t            id;
  IB_wqpn_t               local_qp_num;   /* QP number this completion was generated for */
  VAPI_cqe_opcode_t       opcode;    
  u_int32_t               byte_len;       /* Num. of bytes transferred */
  MT_bool                 imm_data_valid; /* Imm. data indicator */
  VAPI_imm_data_t         imm_data;
  VAPI_remote_node_addr_t remote_node_addr;
  MT_bool                 grh_flag;       
  VAPI_pkey_ix_t          pkey_ix;        /* for GSI */
  /* Vendor specific error syndrome (valid when status != VAPI_SUCCESS) */
  EVAPI_vendor_err_syndrome_t vendor_err_syndrome; 
  u_int32_t               free_res_count;
} VAPI_wc_desc_t;



/**********************************/
/* Fast memory regions data types */
/**********************************/

typedef struct {
  VAPI_pd_hndl_t    pd_hndl;
  VAPI_mrw_acl_t    acl; 
  u_int32_t         max_pages;  /* Maximum number of pages that can be mapped using this region *
                                 *(virtual mapping only)                                        */
  u_int8_t          log2_page_sz;	/* Fixed page size for all maps on a given FMR */
  u_int32_t         max_outstanding_maps; /* Maximum maps before invoking unmap for this region */
} EVAPI_fmr_t;

typedef struct {
  VAPI_virt_addr_t     start; 	  		  /* Mapped memory virtual address */
  VAPI_size_t          size;  	   		  /*  Size of memory mapped to this region */
  MT_size_t            page_array_len;    /* If >0 then no memory locking is done and page table is taken from array below */
  VAPI_phy_addr_t     *page_array;		  /* Page size is set in EVAPI_alloc_fmr by log2_page_sz field */ 
} EVAPI_fmr_map_t;

typedef VAPI_mr_hndl_t EVAPI_fmr_hndl_t;


typedef struct {
    VAPI_size_t         total_mem;
	VAPI_size_t         free_mem;
	VAPI_size_t         largest_chunk;
} EVAPI_devmem_info_t;



/*EVAPI_process_local_mad options*/

/* enumeration of options (effectively, bits in a bitmask) */
typedef enum {
  EVAPI_MAD_IGNORE_MKEY       = 1  /* process_local_mad will not validate the MKEY */
} EVAPI_proc_mad_opt_enum_t;

/* Associated "bitmask" type for use in structs and argument lists */
typedef u_int32_t EVAPI_proc_mad_opt_t;


/* profile typedef */

typedef struct EVAPI_hca_profile_t {
    u_int32_t   num_qp;   /* min number of QPs to configure */
    u_int32_t   num_cq;   /* min number of CQs to configure */
    u_int32_t   num_pd;   /* min number of PDs to configure */
    u_int32_t   num_mr;   /* min number of mem regions to configure */
    u_int32_t   num_mw;   /* min number of mem windows to configure */
    u_int32_t   max_qp_ous_rd_atom; /* max outstanding read/atomic operations as target PER QP */
    u_int32_t   max_mcg;  /* max number of multicast groups for this HCA */
    u_int32_t   qp_per_mcg;  /* max number of QPs per mcg */
    MT_bool     require;  /* if TRUE, EVAPI_open_hca will fail if cannot use exact profile values
                             to open the HCA */
} EVAPI_hca_profile_t;


#endif /*H_VAPI_TYPES_H*/

