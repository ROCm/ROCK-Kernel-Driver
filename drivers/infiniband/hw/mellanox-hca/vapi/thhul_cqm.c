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

#define C_THHUL_CQM_C

#include <hh.h>
#include <hhul.h>
#include <MT23108.h>
#include <thhul.h>
#include <thhul_hob.h>
#include <thhul_srqm.h>
#include <thhul_qpm.h>
#include <uar.h>
#include "thhul_cqm.h"

#include <mtperf.h>
MTPERF_NEW_SEGMENT(free_cqe,5000);

/* Uncomment the line below in order to get CQ dump when the same WQE is used twice simultaneously*/
/* #define THHUL_CQM_DEBUG_WQE_REUSE */

/* Always support fork (assumes number of CQs per process is limited) */
#define MT_FORK_SUPPORT


/* Limit kmalloc to 2 pages (if this fails, vmalloc will fail too) */
#define CQ_KMALLOC_LIMIT (2*MOSAL_SYS_PAGE_SIZE)

/* Maximum CQ doorbell to coalesce/delay (too much is not effective ?) */
#define MAX_CQDB2DELAY 255

/* CQE size */
#define LOG2_CQE_SZ 5   /* 32 bytes */
#define CQE_SZ (1U<<LOG2_CQE_SZ)

/* Assuming that in original CQE (big-endian) the owner field is in the last byte (bit 7) */
#define CQE_OWNER_SHIFT  7          /* Bit of ownership (in the byte) */
#define CQE_OWNER_BYTE_OFFSET 0x1F  /* Last byte of a CQE */
#define CQE_OPCODE_BYTE_OFFSET 0x1C  /* Opcode/status byte */
#define CQE_WQE_ADR_BIT_SZ  6       /* Number of bits in "wqe_adr" field */

/* new CQE error handling */
#define CQE_ERROR_ON_SQ  0xFF       /* Opcode field value for SQ completion error */
#define CQE_ERROR_ON_RQ  0xFE       /* Opcode field value for RQ completion error */
#define CQE_ERROR_STATUS_MASK 0xFE  /* Common mask for both 0xFF and 0xFE "error status" opcodes */
#define CQE_ERROR_SYNDROM_BIT_OFFSET (MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,\
                                                immediate_ethertype_pkey_indx_eecredits)+24)
#define CQE_ERROR_SYNDROM_BIT_SIZE 8 
#define CQE_ERROR_VENDOR_SYNDROM_BIT_OFFSET (CQE_ERROR_SYNDROM_BIT_OFFSET-8)
#define CQE_ERROR_VENDOR_SYNDROM_BIT_SIZE 8
#define CQE_ERROR_DBDCNT_BIT_OFFSET (MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,\
                                                immediate_ethertype_pkey_indx_eecredits))
#define CQE_ERROR_DBDCNT_BIT_SIZE 16 


#define MAX_CQ_NUM 0x00FFFFFF
#define INVALID_CQ_NUM 0xFFFFFFFF
#define MAX_NCOMP_NOTIF 0x7FFF    /* req_ncomp_notif support up to 2^15-1 */

/* IB opcodes for RQ CQEs (bits 4:0) */
typedef enum {
  IB_OP_SEND_LAST       =2,
  IB_OP_SEND_ONLY       =4,
  IB_OP_SEND_IMM_LAST   =3,
  IB_OP_SEND_IMM_ONLY   =5,
  IB_OP_RDMAW_IMM_LAST  =9,
  IB_OP_RDMAW_IMM_ONLY  =11
} THHUL_ib_opcode_t;


typedef enum {
    THHUL_CQ_PREP,        /* Before THHUL_cqm_create_cq_done */
    THHUL_CQ_IDLE,        /* No resize is in progress */
    THHUL_CQ_RESIZE_PREP, /* Allocation and activation of resized buffer is in progress
                             (before return from HH_resize_cq) */
    THHUL_CQ_RESIZE_DONE  /* In fixed resize-CQ flow, the transition to the resized buffer may 
                           * happen before the return from VIPKL_resize_cq 
                           * (before THHUL_cqm_resize_cq_done)                                */
} THHUL_cq_state_t;

typedef struct {
  void* cqe_buf_orig;         /* Address returned from VMALLOC */
  MT_virt_addr_t cqe_buf_base;   /* Aligned to CQE size */
  u_int8_t log2_num_o_cqes;   /* (log2) number of CQEs in the buffer (including one reserved) */
  u_int16_t spare_cqes;       /* CQEs beyond requested number of CQEs for this buffer */
  u_int32_t consumer_index;
} THHUL_cqe_buf_t;

typedef struct THHUL_cq_st {
  THHUL_cqe_buf_t cur_buf;/* Main CQ buffer properties */
  THHUL_cqe_buf_t resized_buf;/* Next (resized) CQ buffer properties */
  u_int16_t pending_cq_dbell; /* Pending CQ doorbells 
                               * (DBs to rung when reach spare_cqes or on event req.) */
  u_int16_t cur_spare_cqes;   /* CQEs beyond requested number of CQEs (for CQ DB coalescing) */
  THHUL_cq_state_t cq_state;
  THH_uar_t uar;
  HH_cq_hndl_t  cq_num;       /* Hardware CQ number */
  THHUL_qpm_t qpm;            /* Make this available close to the CQ context */
  MOSAL_spinlock_t cq_lock;
  MT_bool cq_resize_fixed; /* FW fix for FM issue #16966/#17002: comp. events during resize */
  /* Above piece of information is held here to reduce overhead when polling the CQ         */
  /* (on account of memory footprint) */
  /* CQ list administration */
  struct THHUL_cq_st *next;
} THHUL_cq_t;

struct THHUL_cqm_st {
  THHUL_cq_t *cq_list;
  MOSAL_mutex_t cqm_lock; /* This protects list - not time critical (only for adding/removing) */
} /* *THHUL_cqm_t */;

struct THHUL_cqe_st {
  IB_wqpn_t qpn;
  IB_eecn_t eecn;
  IB_wqpn_t rqpn;
  IB_sl_t   sl;
  MT_bool grh_present;
  u_int8_t  ml_path;
  IB_lid_t  rlid;

} THHUL_cqe_t;

#ifdef THHUL_CQM_DEBUG_WQE_REUSE
#define DUMP_CQE_PRINT_CMD MTL_ERROR4
#else
#define DUMP_CQE_PRINT_CMD MTL_DEBUG4
#endif

#define DUMP_CQE(cq_num,cqe_index,cqe) \
    {DUMP_CQE_PRINT_CMD("CQ[0x%X]:cqe[0x%X] = %08X %08X %08X %08X %08X %08X %08X %08X\n", \
      cq_num,cqe_index,                                                      \
      MOSAL_be32_to_cpu(cqe[0]),                                             \
      MOSAL_be32_to_cpu(cqe[1]),                                             \
      MOSAL_be32_to_cpu(cqe[2]),                                             \
      MOSAL_be32_to_cpu(cqe[3]),                                             \
      MOSAL_be32_to_cpu(cqe[4]),                                             \
      MOSAL_be32_to_cpu(cqe[5]),                                             \
      MOSAL_be32_to_cpu(cqe[6]),                                             \
      MOSAL_be32_to_cpu(cqe[7]));}


/**********************************************************************************************
 *                    Private inline functions 
 **********************************************************************************************/

/* Check if a CQE is in hardware ownership (over original CQE) */
inline static MT_bool is_cqe_hw_own(volatile u_int32_t* cqe_p)
{
  return (((volatile u_int8_t*)cqe_p)[CQE_OWNER_BYTE_OFFSET] >> CQE_OWNER_SHIFT);  
  /* bit is '1 for HW-own.*/
}

/* Return CQE to HW ownership (change bit directly over CQE) */
inline static void set_cqe_to_hw_own(volatile u_int32_t *cqe_p)
{
  ((volatile u_int8_t*)cqe_p)[CQE_OWNER_BYTE_OFFSET]= (1 << CQE_OWNER_SHIFT);
}

/* Set ownership of CQE to hardware ownership (over original CQE) and */
/* increment consumer index (software + hardware) */
/* This function assumes CQ lock is already acquired by this thread */
inline static void  free_cqe(THHUL_cq_t *cq_p, volatile u_int32_t* cqe_p)
{
#ifndef NO_CQ_CI_DBELL
  HH_ret_t rc;
#endif  
  /* Pass ownership to HW */
  set_cqe_to_hw_own(cqe_p);
  
#ifndef NO_CQ_CI_DBELL
  if (cq_p->pending_cq_dbell >= cq_p->cur_spare_cqes) {
    MTL_DEBUG4(MT_FLFMT("%s: Ringing CQ DB (CQN=0x%X) to increment CI by %u"),__func__,
               cq_p->cq_num, cq_p->pending_cq_dbell+1);
    /* Ring CQ-cmd doorbell to update consumer index (pending + this CQE)*/
    rc= THH_uar_cq_cmd(cq_p->uar,TAVOR_IF_UAR_CQ_INC_CI,cq_p->cq_num,cq_p->pending_cq_dbell);
    if (rc != HH_OK) {
      MTL_ERROR2(MT_FLFMT("%s: Failed THH_uar_cq_cmd (%s)"), __func__, HH_strerror_sym(rc));
      /* Even though, this is not a show stopper. Let's continue until we get CQ error. */
      cq_p->pending_cq_dbell++; /* Maybe we will get luckier next time */
    } else {
      cq_p->pending_cq_dbell= 0;
    }
  } else { /* postpone CQ doorbell ringing on account of spare CQEs */
    cq_p->pending_cq_dbell++;
  }
#endif  /* NO_CQ_CI_DBELL */
  /* Update software consumer index */
  /*(modulo number of CQEs in buffer, which is one more than the maximum CQEs outstanding) */ 
  cq_p->cur_buf.consumer_index= (cq_p->cur_buf.consumer_index + 1) & MASK32(cq_p->cur_buf.log2_num_o_cqes); 
}

/* Reuse given CQE in order to report "flush-error" for the next WQE in a queue */
inline static void recycle_cqe(volatile u_int32_t* cqe_p, 
  u_int32_t next_wqe_addr_32lsb, u_int32_t new_dbd_cnt)
{
  /* Operations done directly on original "big-endian" CQE */
  /* Set next WQE's address */
  cqe_p[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,wqe_adr)>>2]= MOSAL_cpu_to_be32(
      next_wqe_addr_32lsb & (~MASK32(CQE_WQE_ADR_BIT_SZ)) ); /* Mask off "wqe_adr" */
  /* Mark as "ERR_FLUSH" with updated dbd_cnt */
  cqe_p[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,immediate_ethertype_pkey_indx_eecredits)>>2]=
    MOSAL_cpu_to_be32(
      (TAVOR_IF_COMP_STATUS_ERR_FLUSH << (CQE_ERROR_SYNDROM_BIT_OFFSET & MASK32(5)) ) |
      new_dbd_cnt);
}

/* Translate from Tavor's error syndrom (in CQE.ib_syn) status encoding to VAPI's */
inline static VAPI_wc_status_t decode_error_syndrome(tavor_if_comp_status_t tstatus)
{
  switch (tstatus) {
    case TAVOR_IF_COMP_STATUS_ERR_LCL_LEN: 
      return VAPI_LOC_LEN_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_LCL_QP_OP:
      return VAPI_LOC_QP_OP_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_LCL_EE_OP:
      return VAPI_LOC_EE_OP_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_LCL_PROT:
      return VAPI_LOC_PROT_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_FLUSH:
      return VAPI_WR_FLUSH_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_MWIN_BIND:
      return VAPI_MW_BIND_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_BAD_RESP:
      return VAPI_BAD_RESP_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_LCL_ACCS:
      return VAPI_LOC_ACCS_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_RMT_INVAL_REQ:
      return VAPI_REM_INV_REQ_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_RMT_ACCSS:
      return VAPI_REM_ACCESS_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_RMT_OP:
      return VAPI_REM_OP_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_TRANS_RETRY_EX:
      return VAPI_RETRY_EXC_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_RNR_RETRY_EX:
      return VAPI_RNR_RETRY_EXC_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_LCL_RDD_VIOL:
      return VAPI_LOC_RDD_VIOL_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_RMT_INVAL_REQ_RD:
      return VAPI_REM_INV_RD_REQ_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_RMT_ABORT:
      return VAPI_REM_ABORT_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_INVAL_EEC_NUM:
      return VAPI_INV_EECN_ERR;
    case TAVOR_IF_COMP_STATUS_ERR_INVAL_EEC_STT:
      return VAPI_INV_EEC_STATE_ERR;
    default:
      MTL_ERROR1(MT_FLFMT("Invalid CQE error syndrome (0x%X)"),tstatus);
      return VAPI_COMP_GENERAL_ERR;
  }
}

inline static HH_ret_t decode_opcode(MT_bool send_q, u_int8_t cqe_opcode,
  VAPI_cqe_opcode_t *vapi_cqe_opcode_p, MT_bool *immediate_valid_p)
{
  *immediate_valid_p= FALSE;  /* Innocent until proven guilty... */
  if (send_q) { /* Send queue - use "nopcode" encoding */
    switch (cqe_opcode) {
      case TAVOR_IF_NOPCODE_RDMAW:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_RDMA_WRITE;
        return HH_OK;
      case TAVOR_IF_NOPCODE_RDMAW_IMM:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_RDMA_WRITE;
        *immediate_valid_p= TRUE;
        return HH_OK;
      case TAVOR_IF_NOPCODE_SEND:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_SEND_DATA;
        return HH_OK;
      case TAVOR_IF_NOPCODE_SEND_IMM:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_SEND_DATA;
        *immediate_valid_p= TRUE;
        return HH_OK;
      case TAVOR_IF_NOPCODE_RDMAR:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_RDMA_READ;
        return HH_OK;
      case TAVOR_IF_NOPCODE_ATOM_CMPSWP:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_COMP_SWAP;
        return HH_OK;
      case TAVOR_IF_NOPCODE_ATOM_FTCHADD:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_FETCH_ADD;
        return HH_OK;
      case TAVOR_IF_NOPCODE_BIND_MEMWIN:
        *vapi_cqe_opcode_p= VAPI_CQE_SQ_BIND_MRW;
        return HH_OK;
      default:
        return HH_EINVAL; /* Invalid opcode - shouldn't happen */
    }
  
  } else {  /* receive queue - use IB encoding */
    /* bits 4:0 are of the opcode are common to all transport types */
    switch (cqe_opcode & MASK32(5)) { 
      case IB_OP_SEND_LAST:
      case IB_OP_SEND_ONLY:
        *vapi_cqe_opcode_p= VAPI_CQE_RQ_SEND_DATA;
        return HH_OK;
      case IB_OP_SEND_IMM_LAST:
      case IB_OP_SEND_IMM_ONLY:
        *vapi_cqe_opcode_p= VAPI_CQE_RQ_SEND_DATA;
        *immediate_valid_p= TRUE;
        return HH_OK;
      case IB_OP_RDMAW_IMM_LAST:
      case IB_OP_RDMAW_IMM_ONLY:
        *vapi_cqe_opcode_p= VAPI_CQE_RQ_RDMA_WITH_IMM;
        *immediate_valid_p= TRUE;
        return HH_OK;
      default:
        return HH_EINVAL;
    }
  }
}

/* Extract CQE fields but for "status", "free_res_count" and "id" (already filled in poll4cqe) */
/* This function is used only for successfull completions. */
/* Given CQE is already in CPU endianess */
inline static HH_ret_t extract_cqe(u_int32_t *cqe, VAPI_wc_desc_t *vapi_cqe_p, 
  VAPI_special_qp_t qp_type, VAPI_ts_type_t qp_ts_type) 
{
  HH_ret_t rc;
  MT_bool send_cqe= MT_EXTRACT_ARRAY32(cqe,
    MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,s),
    MT_BIT_SIZE(tavorprm_completion_queue_entry_st,s));
  
  rc= decode_opcode(
    send_cqe ,
    MT_EXTRACT_ARRAY32(cqe,
      MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,opcode),
      MT_BIT_SIZE(tavorprm_completion_queue_entry_st,opcode)   ),
    &(vapi_cqe_p->opcode),&(vapi_cqe_p->imm_data_valid));
  if (rc != HH_OK) {
    MTL_ERROR4(MT_FLFMT("Invalid %s Opcode=0x%X"),send_cqe ? "send" : "receive",
    MT_EXTRACT_ARRAY32(cqe,
      MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,opcode),
      MT_BIT_SIZE(tavorprm_completion_queue_entry_st,opcode)   ) );
    return rc;
  }
  if (send_cqe && 
      ((vapi_cqe_p->opcode == VAPI_CQE_SQ_COMP_SWAP) || 
       (vapi_cqe_p->opcode == VAPI_CQE_SQ_FETCH_ADD)   
       ) 
      ) { /* Atomic operations are always of length 8 */
    vapi_cqe_p->byte_len= 8;
  } else { /* Get bytes transfered from CQE (see FM issue #15659) */
    vapi_cqe_p->byte_len= cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,byte_cnt)>>2];
  }
  
  /* Get data from immediate_ethertype_pkey_indx_eecredits if valid */
      
  switch (qp_ts_type) {
    case VAPI_TS_UD:
      if (!send_cqe) {   /* see IB-spec. 11.4.2.1: Output Modifiers */
        vapi_cqe_p->remote_node_addr.type= VAPI_RNA_UD;
        vapi_cqe_p->remote_node_addr.qp_ety.qp= 
          cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,rqpn)>>2] & MASK32(24);
        vapi_cqe_p->remote_node_addr.ee_dlid.dst_path_bits= MT_EXTRACT_ARRAY32(cqe,
          MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,ml_path),
          MT_BIT_SIZE(tavorprm_completion_queue_entry_st,ml_path)   );
        vapi_cqe_p->remote_node_addr.slid= MT_EXTRACT_ARRAY32(cqe,
          MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,rlid),MT_BIT_SIZE(tavorprm_completion_queue_entry_st,rlid));
        vapi_cqe_p->remote_node_addr.sl= MT_EXTRACT_ARRAY32(cqe,
          MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,sl),MT_BIT_SIZE(tavorprm_completion_queue_entry_st,sl));
        vapi_cqe_p->grh_flag= MT_EXTRACT_ARRAY32(cqe,
          MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,g),MT_BIT_SIZE(tavorprm_completion_queue_entry_st,g));
      }
      break;
    case VAPI_TS_RD:
      if (!send_cqe) {  /* see IB-spec. 11.4.2.1: Output Modifiers */
        vapi_cqe_p->remote_node_addr.type= VAPI_RNA_RD;
        vapi_cqe_p->remote_node_addr.qp_ety.qp= 
          cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,rqpn)>>2] & MASK32(24);
        vapi_cqe_p->remote_node_addr.ee_dlid.loc_eecn= 
          cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,my_ee)>>2] & MASK32(24);
      }
      break;
    default:
      break;
  }
      

  switch (qp_type) {
  case VAPI_REGULAR_QP:
    /*if (vapi_cqe_p->imm_data_valid) */ 
    /* Copy immediate_ethertype_pkey_indx_eecredits even if no immediate data, 
     * in order to get eecredits (for SQ) */
        vapi_cqe_p->imm_data = 
            cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,immediate_ethertype_pkey_indx_eecredits)>>2];
    break;
  case VAPI_RAW_ETY_QP:
    vapi_cqe_p->remote_node_addr.type= VAPI_RNA_RAW_ETY;
    vapi_cqe_p->remote_node_addr.qp_ety.ety= 
      cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,immediate_ethertype_pkey_indx_eecredits)>>2];
    break;
  
  case VAPI_GSI_QP:
    vapi_cqe_p->pkey_ix= 
      cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,immediate_ethertype_pkey_indx_eecredits)>>2]
        >> 16;  /* Pkey index is on bits 31:16 */
    vapi_cqe_p->imm_data_valid= FALSE;  
    /* QP1's RQ requests complete as "send w/immediate", even though it is just a send */
    break;
  
  default:
    break;

  }

  return HH_OK;
}

/* Given CQ must be locked when calling this function */
static inline void sync_consumer_index(THHUL_cq_t *cq_p, const char* caller)
{
  HH_ret_t rc;

  if (cq_p->pending_cq_dbell > 0) {
    MTL_DEBUG4(MT_FLFMT("%s: Ringing CQ DB (CQN=0x%X) to increment CI by %u"),caller,
               cq_p->cq_num, cq_p->pending_cq_dbell);
    rc= THH_uar_cq_cmd(cq_p->uar,TAVOR_IF_UAR_CQ_INC_CI,cq_p->cq_num,cq_p->pending_cq_dbell-1);
    if (rc != HH_OK) {
      MTL_ERROR2(MT_FLFMT("%s: Failed THH_uar_cq_cmd (%s)"), caller, HH_strerror_sym(rc));
      /* Even though, this is not a show stopper. Let's continue until we get CQ error. */
    } else {
      cq_p->pending_cq_dbell= 0;
    }
  }
}

/**********************************************************************************************
 *                    Private functions declarations
 **********************************************************************************************/
static HH_ret_t cqe_buf_alloc(THHUL_cqe_buf_t *cqe_buf, VAPI_cqe_num_t num_o_cqes);

static void cqe_buf_free(THHUL_cqe_buf_t *cqe_buf);

static const char* cq_state_str(THHUL_cq_state_t cq_state)
{
  switch (cq_state) {
    case THHUL_CQ_PREP: return "THHUL_CQ_PREP";
    case THHUL_CQ_IDLE: return "THHUL_CQ_IDLE";
    case THHUL_CQ_RESIZE_PREP: return "THHUL_CQ_RESIZE_PREP";
    default: return "(unknown state)";
  }
}

static u_int32_t cqe_buf_cleanup(THHUL_cqe_buf_t *cqe_buf,IB_wqpn_t qp,
                                 THHUL_srqm_t srqm, HHUL_srq_hndl_t srq,
                                 u_int32_t *cur_producer_index_p);

static void cqe_buf_cpy2resized(
  THHUL_cqe_buf_t *cur_buf, 
  THHUL_cqe_buf_t *resized_buf, 
  MT_bool compute_new_pi); /* New resize-CQ flow */

static VAPI_cqe_num_t count_cqes(/*IN*/ THHUL_cq_t *cq_p, 
                                 /*IN*/ VAPI_cqe_num_t cqe_num_limit,
                                 /*OUT*/ VAPI_cqe_num_t *hw_cqe_cnt_p);



/**********************************************************************************************
 *                    Public API Functions (defined in thhul_hob.h)
 **********************************************************************************************/


HH_ret_t THHUL_cqm_create( 
  /*IN*/ THHUL_hob_t  hob, 
  /*OUT*/ THHUL_cqm_t *cqm_p 
) 
{ 
  THHUL_cqm_t new_cqm;

  new_cqm= (THHUL_cqm_t)MALLOC(sizeof(struct THHUL_cqm_st));
  if (new_cqm == NULL) {
    MTL_ERROR1("THHUL_cqm_create: Failed to allocate memory for a new CQM.\n");
    return HH_EAGAIN;
  }
  new_cqm->cq_list= NULL;
  MOSAL_mutex_init(&(new_cqm->cqm_lock));

  *cqm_p= new_cqm;
  return HH_OK;
}

HH_ret_t THHUL_cqm_destroy (
  /*IN*/ THHUL_cqm_t cqm
) 
{ 
    THHUL_cq_t *thhul_cq;
       
    while (cqm->cq_list) {  
        thhul_cq = cqm->cq_list;
        cqm->cq_list= thhul_cq->next;
        cqe_buf_free(&(thhul_cq->cur_buf));
        FREE(thhul_cq);
    }

    MOSAL_mutex_free(&(cqm->cqm_lock));
    FREE(cqm);
    return HH_OK; 
}


HH_ret_t THHUL_cqm_create_cq_prep(
  /*IN*/  HHUL_hca_hndl_t hca, 
  /*IN*/  VAPI_cqe_num_t  num_o_cqes, 
  /*OUT*/ HHUL_cq_hndl_t  *hhul_cq_p, 
  /*OUT*/ VAPI_cqe_num_t  *num_o_cqes_p, 
  /*OUT*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
) 
{ 
  THHUL_cqm_t cqm;
  THHUL_cq_t *new_cq;
  THH_cq_ul_resources_t *ul_res_p= (THH_cq_ul_resources_t*)cq_ul_resources_p;
  HH_ret_t rc;
  THH_hca_ul_resources_t hca_ul_res;

  rc= THHUL_hob_get_cqm(hca,&cqm);
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_cqm_create_cq_prep: Invalid HCA handle.\n");
    return HH_EINVAL;
  }

  rc= THHUL_hob_get_hca_ul_res(hca,&hca_ul_res);
  if (rc != HH_OK) {
    MTL_ERROR2("THHUL_cqm_create_cq_prep: Failed THHUL_hob_get_hca_ul_res (err=%d).\n",rc);
    return rc;
  }

  if (num_o_cqes > hca_ul_res.max_num_ent_cq) {
      MTL_ERROR2("THHUL_cqm_create_cq_prep: cq_num_of_entries requested exceeds hca cap\n");
      return HH_E2BIG_CQE_NUM;
  }

  new_cq= (THHUL_cq_t*)MALLOC(sizeof(THHUL_cq_t));
  if (new_cq == NULL) {
    MTL_ERROR1("THHUL_cqm_create_cq_prep: Failed to allocate THHUL_cq_t.\n");
    return HH_EAGAIN;
  }

  rc= cqe_buf_alloc(&new_cq->cur_buf,num_o_cqes);
  if (rc != HH_OK) goto failed_cqe_buf;
  new_cq->cq_state= THHUL_CQ_PREP; 
  new_cq->cur_spare_cqes= new_cq->cur_buf.spare_cqes;
  new_cq->pending_cq_dbell= 0;
  
  new_cq->cq_resize_fixed= (hca_ul_res.version.fw_ver_major >= 3);

  rc= THHUL_hob_get_uar(hca,&(new_cq->uar));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_cqm_create_cq_prep: Failed getting THHUL_hob's UAR (err=%d).\n",rc);
    goto failed_uar;
  }
  rc= THH_uar_get_index(new_cq->uar,&(ul_res_p->uar_index)); 
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_cqm_create_cq_prep: Failed getting UAR index.\n");
    goto failed_uar;
  }

  rc= THHUL_hob_get_qpm(hca,&(new_cq->qpm));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_cqm_create_cq_prep: Failed getting THHUL_hob's QPM (err=%d).\n",rc);
    goto failed_qpm;
  }

  new_cq->cq_num= INVALID_CQ_NUM;
  MOSAL_spinlock_init(&(new_cq->cq_lock));
  /* Add CQ to CQs list */
  if (MOSAL_mutex_acq(&(cqm->cqm_lock),TRUE) != 0)  {rc= HH_EINTR; goto failed_mutex;}
  new_cq->next= cqm->cq_list; /* Add before first (if any) */
  cqm->cq_list= new_cq;
  MOSAL_mutex_rel(&(cqm->cqm_lock));

  /* Output modifiers */
  *hhul_cq_p= (HHUL_cq_hndl_t)new_cq;
  /* One CQE is always reserved (for CQ-full indication) */
  *num_o_cqes_p= (1U << new_cq->cur_buf.log2_num_o_cqes) - 1 - new_cq->cur_buf.spare_cqes; 
  ul_res_p->cqe_buf= new_cq->cur_buf.cqe_buf_base;  /* Registration is required only from here */
  ul_res_p->cqe_buf_sz= (1U << new_cq->cur_buf.log2_num_o_cqes) * CQE_SZ ;  
  /* ul_res_p->uar_index ==> already set above */
  return HH_OK;

  failed_mutex:
  failed_qpm:
  failed_uar:
    cqe_buf_free(&(new_cq->cur_buf));
  failed_cqe_buf:
    FREE(new_cq);
    return rc;   
}


HH_ret_t THHUL_cqm_create_cq_done(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_cq_hndl_t hhul_cq, 
  /*IN*/ HH_cq_hndl_t hh_cq, 
  /*IN*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
) 
{ 
  THHUL_cq_t *cq= (THHUL_cq_t*)hhul_cq;
  
  if (cq == NULL) {
    MTL_ERROR1("THHUL_cqm_create_cq_done: NULL CQ handle.\n");
    return HH_EINVAL;
  }
  if (hh_cq > MAX_CQ_NUM) {
    MTL_ERROR1("THHUL_cqm_create_cq_done: Invalid CQ number (0x%X).\n",hh_cq);
    return HH_EINVAL;
  }
  
  MOSAL_spinlock_irq_lock(&(cq->cq_lock));
  if (cq->cq_state != THHUL_CQ_PREP) {
    MOSAL_spinlock_unlock(&(cq->cq_lock));
    MTL_ERROR1("THHUL_cqm_create_cq_done: Library inconsistancy ! Given CQ is not in THHUL_CQ_PREP state.\n");
    return HH_ERR;
  }
  cq->cq_state= THHUL_CQ_IDLE; 
  cq->cq_num= hh_cq;
  MOSAL_spinlock_unlock(&(cq->cq_lock));
  
  return HH_OK; 
}


HH_ret_t THHUL_cqm_destroy_cq_done(
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq 
) 
{ 
  THHUL_cq_t *thhul_cq= (THHUL_cq_t*)cq;
  THHUL_cq_t *prev_cq,*cur_cq;
  THHUL_cqm_t cqm;
  HH_ret_t rc;

  rc= THHUL_hob_get_cqm(hca_hndl,&cqm);
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_cqm_destroy_cq_done: Invalid HCA handle.\n");
    return HH_EINVAL;
  }
  if (cq == NULL) {
    MTL_ERROR1("THHUL_cqm_destroy_cq_done: NULL CQ handle.\n");
    return HH_EINVAL;
  }
  
  if (thhul_cq->cq_state == THHUL_CQ_RESIZE_PREP) {
    /* Someone invoked VAPI_destroy_cq while invoking VAPI_resize_cq */
    MTL_ERROR2(MT_FLFMT("%s: Invoked while in THHUL_CQ_RESIZE_PREP (cqn=0x%X) !"),__func__,
               thhul_cq->cq_num);
    return HH_EBUSY;
  }
  /* Remove from CQs list */
  MOSAL_mutex_acq_ui(&(cqm->cqm_lock));
  for (cur_cq= cqm->cq_list, prev_cq= NULL; cur_cq != NULL; 
       prev_cq= cur_cq, cur_cq= cur_cq->next) {
    if (cur_cq == cq) break;
  }
  if (cur_cq == NULL) { /* CQ not found */
    MOSAL_mutex_rel(&(cqm->cqm_lock));
    MTL_ERROR1("THHUL_cqm_destroy_cq_done: invalid CQ handle (not found).\n");
    return HH_EINVAL;
  }
  if (prev_cq == NULL) {  /* First in list */
    cqm->cq_list= thhul_cq->next; /* Make next be the first */
  } else {  
    prev_cq->next= thhul_cq->next;  /* Link previous to next */
  }
  MOSAL_mutex_rel(&(cqm->cqm_lock));
  
  /* Cleanup CQ resources */
  cqe_buf_free(&(thhul_cq->cur_buf));
  FREE(thhul_cq);
  return HH_OK;
}


HH_ret_t THHUL_cqm_resize_cq_prep(
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cqe_num_t num_o_cqes, 
  /*OUT*/ VAPI_cqe_num_t *num_o_cqes_p, 
  /*OUT*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
) 
{ 
  THHUL_cq_t *cq_p= (THHUL_cq_t*)cq;
  THH_cq_ul_resources_t *ul_res_p= (THH_cq_ul_resources_t*)cq_ul_resources_p;
  HH_ret_t rc;
  THH_hca_ul_resources_t hca_ul_res;
  THHUL_cqe_buf_t new_buf;

  if (cq_p == NULL) {
    MTL_ERROR1("%s: NULL CQ handle.\n",__func__);
    return HH_EINVAL;
  }
  
  rc= THHUL_hob_get_hca_ul_res(hca_hndl,&hca_ul_res);
  if (rc != HH_OK) {
      MTL_ERROR2("THHUL_cqm_create_cq_prep: Failed THHUL_hob_get_hca_ul_res (err=%d).\n",rc);
      return rc;
  }

  if (num_o_cqes > hca_ul_res.max_num_ent_cq) {
    MTL_ERROR2("THHUL_cqm_create_cq_prep: cq_num_of_entries requested exceeds hca cap\n");
    return HH_E2BIG_CQE_NUM;
  }

  rc= cqe_buf_alloc(&new_buf,num_o_cqes);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Allocating buffer for resized CQ 0x%X has failed"),__func__,
                cq_p->cq_num);
    return rc;
  }
  
  MOSAL_spinlock_irq_lock(&(cq_p->cq_lock));
  if (cq_p->cq_state != THHUL_CQ_IDLE) {
    MTL_ERROR1("%s: CQ is not in IDLE state (current state=%d=%s)\n", __func__,
               cq_p->cq_state,cq_state_str(cq_p->cq_state));
    MOSAL_spinlock_unlock(&(cq_p->cq_lock));
    cqe_buf_free(&new_buf);
    return HH_EBUSY;
  }
  memcpy(&cq_p->resized_buf, &new_buf, sizeof(THHUL_cqe_buf_t));

  /* Update CQ to real number of outstanding CQEs */
  /* (avoid failure in case of reduction in CQ size which matches real num. of outstanding CQEs)*/
  sync_consumer_index(cq_p, __func__);
  /* start working with the limitations of the new CQEs buffer 
   * (after resizing we don't want to be in "overdraft")      */
  if (cq_p->cur_buf.spare_cqes > cq_p->resized_buf.spare_cqes) { 
    cq_p->cur_spare_cqes = cq_p->resized_buf.spare_cqes;
  }
  
  cq_p->cq_state= THHUL_CQ_RESIZE_PREP; 
  MOSAL_spinlock_unlock(&(cq_p->cq_lock));
  
  memset(ul_res_p,0,sizeof(THH_cq_ul_resources_t));
  ul_res_p->cqe_buf= cq_p->resized_buf.cqe_buf_base;  /* Registration is required only from here */
  ul_res_p->cqe_buf_sz= (1U<<cq_p->resized_buf.log2_num_o_cqes) * CQE_SZ ;  
  *num_o_cqes_p= (1U<<cq_p->resized_buf.log2_num_o_cqes) - 1 - cq_p->resized_buf.spare_cqes;

  return HH_OK; 
}


HH_ret_t THHUL_cqm_resize_cq_done( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
) 
{ 
  THHUL_cq_t *cq_p= (THHUL_cq_t*)cq;
  THH_cq_ul_resources_t *ul_res_p= (THH_cq_ul_resources_t*)cq_ul_resources_p;
  THHUL_cqe_buf_t rm_buf;
  
  if (cq_p == NULL) {
    MTL_ERROR1("THHUL_cqm_resize_cq_done: NULL CQ handle.\n");
    return HH_EINVAL;
  }
  
  MOSAL_spinlock_irq_lock(&(cq_p->cq_lock));
  
  if (cq_p->cq_state == THHUL_CQ_RESIZE_PREP) { 
    /* Still polling in old buffer */
    if (!ul_res_p) { /* HH_resize_cq  failed - clean up allocated buffer */
       /* save buffer info to perform cleanup outside of spinlock */
      memcpy(&rm_buf,&(cq_p->resized_buf),sizeof(THHUL_cqe_buf_t));
      /* restore original "spare_cqes" of cur_buf (may have been reduced in resize_cq_prep) */
      cq_p->cur_spare_cqes= cq_p->cur_buf.spare_cqes;

    } else { /* Activate resized buffer */
      cq_p->resized_buf.consumer_index= ul_res_p->new_producer_index; /* for old resize flow */
      /* copy CQEs from old buffer to resized buffer */
      cqe_buf_cpy2resized(&(cq_p->cur_buf),&(cq_p->resized_buf), cq_p->cq_resize_fixed); 
      memcpy(&rm_buf,&(cq_p->cur_buf),sizeof(THHUL_cqe_buf_t)); /* save old buffer */
      /* Make resize buffer current. New consumer index is already updated. */
      memcpy(&(cq_p->cur_buf),&(cq_p->resized_buf),sizeof(THHUL_cqe_buf_t));
      cq_p->cur_spare_cqes= cq_p->cur_buf.spare_cqes; /* work with the spare CQEs of new buffer */
    }
  
  } else if (cq_p->cq_state == THHUL_CQ_RESIZE_DONE) {
    /* Transition to resized buffer already done. No CQEs to copy. Just free old buffer */
    /* (transition done in cq_transition_to_resized_buf)                                */
    if (!ul_res_p) { 
      /* Sanity check - RESIZE_CQ command is not suppose to fail if new buffer was activated */
      MTL_ERROR1(MT_FLFMT("%s: Inconsistancy ! Got failure in RESIZE_CQ"
                          " after finding CQEs in the new buffer (cqn=0x%X)"),
        __func__, cq_p->cq_num);
    }
    memcpy(&rm_buf,&(cq_p->resized_buf),sizeof(THHUL_cqe_buf_t)); /* save old buffer */
  
  } else { /* Invalid CQ state for this function call */
    MOSAL_spinlock_unlock(&(cq_p->cq_lock));
    MTL_ERROR1("THHUL_cqm_resize_cq_done: Given CQ is not in THHUL_CQ_RESIZE_PREP/DONE state.\n");
    return HH_ERR;
  }
  
  /* Good flow finalization */
  cq_p->cq_state= THHUL_CQ_IDLE; /* new resize may be initiated */
  MOSAL_spinlock_unlock(&(cq_p->cq_lock));
  cqe_buf_free(&rm_buf); /* Free old CQEs buffer (must be done outside spinlock section) */
  return HH_OK; 
}


HH_ret_t THHUL_cqm_cq_cleanup( 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ IB_wqpn_t qp,
  /*IN*/ THHUL_srqm_t srqm,
  /*IN*/ HHUL_srq_hndl_t srq
) 
{ 
  THHUL_cq_t *thhul_cq_p= (THHUL_cq_t*)cq;
  u_int32_t removed_cqes= 0;
  u_int32_t cur_buf_pi; /* Current buffer producer index */
  HH_ret_t rc= HH_OK;
  
  MTL_DEBUG2("THHUL_cqm_cq_cleanup(cq_p=%p,qp=%d) {\n",thhul_cq_p,qp);
  if (thhul_cq_p == NULL) {
    MTL_ERROR1("THHUL_cqm_cq_cleanup: NULL CQ handle.\n");
    return HH_EINVAL;
  }
  
  MOSAL_spinlock_irq_lock(&(thhul_cq_p->cq_lock));

  removed_cqes+= cqe_buf_cleanup(&(thhul_cq_p->cur_buf), qp, srqm, srq, &cur_buf_pi );
  /* In case we are resizing the CQ, new buffer may already include CQEs of that QP */
  if (thhul_cq_p->cq_state == THHUL_CQ_RESIZE_PREP) {
    /* Update resized's CI based on new resize-cq flow (FM issue #16969). 
     * (This flow will fail for old flow anyway - so no need to distinguish between cases) 
     */
    thhul_cq_p->resized_buf.consumer_index= 
      cur_buf_pi & MASK32(thhul_cq_p->resized_buf.log2_num_o_cqes);
    removed_cqes+= cqe_buf_cleanup(&(thhul_cq_p->resized_buf), qp, srqm, srq, &cur_buf_pi);
  }
    
#ifndef NO_CQ_CI_DBELL
  /* Ring CQ-cmd doorbell to update consumer index (Removed CQEs + pending CQ doorbells) */
  if (removed_cqes) {
    rc= THH_uar_cq_cmd(thhul_cq_p->uar,TAVOR_IF_UAR_CQ_INC_CI,thhul_cq_p->cq_num,
                       thhul_cq_p->pending_cq_dbell + removed_cqes - 1);
    if (rc != HH_OK) {
      MTL_ERROR2(MT_FLFMT("%s: Failed THH_uar_cq_cmd (%s)"), __func__, HH_strerror_sym(rc));
      /* Even though, this is not a show stopper. Let's continue until we get CQ error. */
      thhul_cq_p->pending_cq_dbell += removed_cqes; /* Maybe we will get luckier next time */
    } else {
      thhul_cq_p->pending_cq_dbell= 0;
    }
  }
#endif
  
  MOSAL_spinlock_unlock(&(thhul_cq_p->cq_lock));

  return rc; 
}

#ifdef THHUL_CQM_DEBUG_WQE_REUSE
void THHUL_cqm_dump_cq(
  /*IN*/ HHUL_cq_hndl_t cq 
)
{
  THHUL_cq_t *thhul_cq_p= (THHUL_cq_t*)cq;
  volatile u_int32_t *cur_cqe;
  int cqe_index;
  static HHUL_cq_hndl_t last_cq= NULL;
  static u_int32_t last_consumer_index= 0xFFFFFFFF;

  /* Do not dump again for the same CQ/consumer-index */
  if ((cq == last_cq) && (last_consumer_index == thhul_cq_p->cur_buf.consumer_index))  return;
  last_cq= cq; last_consumer_index= thhul_cq_p->cur_buf.consumer_index;

  MTL_ERROR4("THHUL_cqm_dump_cq: cq=%d consumer_index=%d\n",
    thhul_cq_p->cq_num,last_consumer_index);
  for (cqe_index= 0; cqe_index < (1 << thhul_cq_p->cur_buf.log2_num_o_cqes); cqe_index++) {
    cur_cqe= (volatile u_int32_t *)
      (thhul_cq_p->cur_buf.cqe_buf_base + (cqe_index << LOG2_CQE_SZ)); 
    DUMP_CQE(thhul_cq_p->cq_num,cqe_index,cur_cqe);
  }

}
#endif

/* Check if can transition to new (resized) CQEs buffer and make cur_buf<--resized_buf
 * - Valid for fixed resize cq FW version
 * - Return TRUE if transition and cur_cqe is updated to new location in resized buf.
 * - Must be invoked with CQ lock locked and only when CQE at current CI is invalid
 */
static MT_bool cq_transition_to_resized_buf(
  THHUL_cq_t *cq_p,
  volatile u_int32_t **cur_cqe_p
)
{
  THHUL_cqe_buf_t rm_buf;
  
  if ((cq_p->cq_state == THHUL_CQ_RESIZE_PREP) &&
      (cq_p->cq_resize_fixed))                   { /* Peek into resized buffer */
    cq_p->resized_buf.consumer_index= /* Expected new CI */
      cq_p->cur_buf.consumer_index & MASK32(cq_p->resized_buf.log2_num_o_cqes);
    *cur_cqe_p= (volatile u_int32_t *)
      (cq_p->resized_buf.cqe_buf_base + 
       (cq_p->resized_buf.consumer_index << LOG2_CQE_SZ)); 
    if (!is_cqe_hw_own(*cur_cqe_p)) { /* Found CQE in new (resized) buffer */
      MTL_DEBUG4(MT_FLFMT("%s: transition to resized (cqn=0x%x old_pi=%u new_pi=%u cur_cqe=%p)"),
                 __func__,
                 cq_p->cq_num, cq_p->cur_buf.consumer_index, cq_p->resized_buf.consumer_index,
                 *cur_cqe_p);
      /* Transition to resized buffer */
      memcpy(&rm_buf,&(cq_p->cur_buf),sizeof(THHUL_cqe_buf_t)); /* save old buffer */
      /* Make resize buffer current. New consumer index is already updated. */
      memcpy(&(cq_p->cur_buf),&(cq_p->resized_buf),sizeof(THHUL_cqe_buf_t));
      cq_p->cur_spare_cqes= cq_p->cur_buf.spare_cqes; /* work with the spare CQEs of new buffer */
      /* Save old buffer to be freed in "resize_done" (no need to hurry...) */
      memcpy(&(cq_p->resized_buf),&rm_buf,sizeof(THHUL_cqe_buf_t)); 
      cq_p->cq_state= THHUL_CQ_RESIZE_DONE; /* THHUL_cqm_resize_cq_done is still expected */
      return TRUE;
    }
  }
  return FALSE;
}

HH_ret_t THHUL_cqm_poll4cqe( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*OUT*/ VAPI_wc_desc_t *vapi_cqe_p 
) 
{ 
  THHUL_cq_t *thhul_cq_p= (THHUL_cq_t*)cq;
  volatile u_int32_t *cur_cqe;
  u_int32_t wqe_addr_32lsb,next_wqe_addr_32lsb;
  IB_wqpn_t qpn;
  u_int8_t opcode;
  u_int8_t dbd_bit;
  VAPI_special_qp_t qp_type;
  VAPI_ts_type_t qp_ts_type;
  u_int32_t i,dbd_cnt;
  HH_ret_t rc;
  u_int32_t cqe_cpy[CQE_SZ>>2]; /* CQE copy */
  /* The CQE copy is required for 2 reasons:
   * 1) Hold in CPU endianess. 
   * 2) Free real CQE as soon as possible in order to release CQ lock quickly.
   */

  if (thhul_cq_p == NULL) {
    MTL_ERROR1("THHUL_cqm_poll4cqe: NULL CQ handle.\n");
    return HH_EINVAL_CQ_HNDL;
  }

  MOSAL_spinlock_irq_lock(&(thhul_cq_p->cq_lock));

  /* Check if CQE at consumer index is valid */
  cur_cqe= (volatile u_int32_t *)
    (thhul_cq_p->cur_buf.cqe_buf_base + (thhul_cq_p->cur_buf.consumer_index << LOG2_CQE_SZ)); 
  if (is_cqe_hw_own(cur_cqe) &&                 /* CQE is still in HW ownership */
      (!cq_transition_to_resized_buf(thhul_cq_p, &cur_cqe)) )  { 

    MOSAL_spinlock_unlock(&(thhul_cq_p->cq_lock));    
#if 0
    THHUL_cqm_dump_cq(cq);
#endif
    return HH_CQ_EMPTY;
  }

  /* Make CQE copy in correct endianess */
  for (i= 0; i < (CQE_SZ>>2); i++) {  
    cqe_cpy[i]= MOSAL_be32_to_cpu(cur_cqe[i]);
  }
  /* Extract QP/WQE context fields from the CQE */
  wqe_addr_32lsb= (cqe_cpy[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,wqe_adr)>>2] & 
                   (~MASK32(CQE_WQE_ADR_BIT_SZ)) );
  qpn= (cqe_cpy[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,my_qpn)>>2] & MASK32(24) );
  vapi_cqe_p->local_qp_num= qpn;
  /* new CQE: completion status is taken from "opcode" field */
  opcode= MT_EXTRACT_ARRAY32(cqe_cpy,
  	MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,opcode), 
  	MT_BIT_SIZE(tavorprm_completion_queue_entry_st,opcode));

  if ((opcode & CQE_ERROR_STATUS_MASK) != CQE_ERROR_STATUS_MASK) {  /* Completed OK */	
    MTPERF_TIME_START(free_cqe);
    free_cqe(thhul_cq_p,cur_cqe); /* Free original CQE and update consumer index */
    MTPERF_TIME_END(free_cqe);

  /* DEBUG: Sanity check that the same WQE is not used twice simultaneosly */
#ifdef THHUL_CQM_DEBUG_WQE_REUSE
    /* Get next CQE and check if valid and NDA equals freed CQE's */
  cur_cqe= (volatile u_int32_t *)
    (thhul_cq_p->cur_buf.cqe_buf_base + (thhul_cq_p->cur_buf.consumer_index << LOG2_CQE_SZ)); 
  if ((!is_cqe_hw_own(cur_cqe)) &&
      ( (MOSAL_be32_to_cpu(
             cur_cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,wqe_adr)>>2]) & 
           (~MASK32(CQE_WQE_ADR_BIT_SZ)) ) == wqe_addr_32lsb) ){ 
    MTL_ERROR1(MT_FLFMT("%s: Duplicate NDA on next CQE (NDA=0x%X , consumer index=%u,%u)"),
           __func__, wqe_addr_32lsb, 
           thhul_cq_p->cur_buf.consumer_index-1, thhul_cq_p->cur_buf.consumer_index);
    THHUL_cqm_dump_cq(cq);
  }
#endif 

#ifndef IVAPI_THH
    rc= THHUL_qpm_comp_ok(thhul_cq_p->qpm, qpn, wqe_addr_32lsb,
      &qp_type,&qp_ts_type,&(vapi_cqe_p->id),&(vapi_cqe_p->free_res_count));
#else
    rc= THHUL_qpm_comp_ok(thhul_cq_p->qpm, qpn, wqe_addr_32lsb,
      &qp_type,&qp_ts_type,&(vapi_cqe_p->id),&(vapi_cqe_p->free_res_count),NULL);
#endif    
    
    MOSAL_spinlock_unlock(&(thhul_cq_p->cq_lock));    
    
    if (rc != HH_OK) {
      MTL_ERROR1("THHUL_cqm_poll4cqe: Failed updating associated QP.\n");
      for (i= 0; i < (CQE_SZ>>2); i++) {  
        MTL_ERROR1(MT_FLFMT("CQ[0x%X][%u][%u]=0x%X"),thhul_cq_p->cq_num,
                   (thhul_cq_p->cur_buf.consumer_index - 1) & MASK32(thhul_cq_p->cur_buf.log2_num_o_cqes),
                   i, cqe_cpy[i]);
      }
      return HH_EFATAL; /* unexpected error */
    }
    /* Extract the rest of the CQE fields into vapi_cqe_p*/
    rc= extract_cqe(cqe_cpy,vapi_cqe_p,qp_type,qp_ts_type); 
    vapi_cqe_p->status= VAPI_SUCCESS;
  
  } else {  /* Completion with error  */
    MTL_DEBUG4("THHUL_cqm_poll4cqe: completion with error: cq=%d consumer_index=%d\n",
      thhul_cq_p->cq_num,thhul_cq_p->cur_buf.consumer_index);
    DUMP_CQE(thhul_cq_p->cq_num,thhul_cq_p->cur_buf.consumer_index,cur_cqe);
    rc= THHUL_qpm_comp_err(thhul_cq_p->qpm, qpn, wqe_addr_32lsb,
      &(vapi_cqe_p->id),&(vapi_cqe_p->free_res_count),&next_wqe_addr_32lsb,&dbd_bit);
    if (rc != HH_OK) {
      MTL_ERROR1("THHUL_cqm_poll4cqe: Failed updating associated QP (QPn=0x%X , CQn=0x%X).\n",
                 qpn, thhul_cq_p->cq_num);
      MOSAL_spinlock_unlock(&(thhul_cq_p->cq_lock));    
      return HH_EFATAL; /* unexpected error */
    }
    vapi_cqe_p->status= decode_error_syndrome((tavor_if_comp_status_t)MT_EXTRACT_ARRAY32(cqe_cpy,
      CQE_ERROR_SYNDROM_BIT_OFFSET, CQE_ERROR_SYNDROM_BIT_SIZE) );
    vapi_cqe_p->vendor_err_syndrome= MT_EXTRACT_ARRAY32(cqe_cpy,
      CQE_ERROR_VENDOR_SYNDROM_BIT_OFFSET, CQE_ERROR_VENDOR_SYNDROM_BIT_SIZE);
    dbd_cnt= MT_EXTRACT_ARRAY32(cqe_cpy,CQE_ERROR_DBDCNT_BIT_OFFSET, CQE_ERROR_DBDCNT_BIT_SIZE);
    if ((next_wqe_addr_32lsb == THHUL_QPM_END_OF_WQE_CHAIN) ||    /* End of WQE chain */
        ((dbd_cnt + 1 - dbd_bit) == 0) )                       {  /* or dbd counter reached 0 */
      if ((next_wqe_addr_32lsb == THHUL_QPM_END_OF_WQE_CHAIN) && (dbd_cnt > 0)) {
      MTL_ERROR1(MT_FLFMT("%s: CQ[0x%X]:CQE[0x%X]: Reached end of chain while dbd_cnt==%u"),
        __func__, thhul_cq_p->cq_num, thhul_cq_p->cur_buf.consumer_index, dbd_cnt);
      }
      MTPERF_TIME_START(free_cqe);
      free_cqe(thhul_cq_p,cur_cqe); /* Free original CQE and update consumer index */
      MTPERF_TIME_END(free_cqe);
    } else {
        recycle_cqe(cur_cqe, next_wqe_addr_32lsb, dbd_cnt - dbd_bit);
    } 
    MOSAL_spinlock_unlock(&(thhul_cq_p->cq_lock)); 
    /* Only WQE-ID, free_res_count and status are required for completion with error. 
     * No other CQE fields are extracted (see IB-spec. 11.4.2.1).                   
     * Even though, for the sake of some legacy code:
     * ...putting an opcode to distinguish completion of SQ from RQ*/
    if (opcode == CQE_ERROR_ON_SQ) { 
      vapi_cqe_p->opcode= VAPI_CQE_SQ_SEND_DATA; 
    } else { /* receive queue completion */
      vapi_cqe_p->opcode= VAPI_CQE_RQ_SEND_DATA; 
    }
  }

  return rc;   
}

HH_ret_t THHUL_cqm_peek_cq( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cqe_num_t cqe_num
)
{
  THHUL_cq_t *cq_p= (THHUL_cq_t*)cq;
  volatile u_int32_t *cur_cqe;
  HH_ret_t ret;

  /* parameters checks */
  if (cq_p == NULL) {
    MTL_ERROR1("THHUL_cqm_peek_cq: NULL CQ handle.\n");
    return HH_EINVAL_CQ_HNDL;
  }

  /* Find CQE and check ownership */
  MOSAL_spinlock_irq_lock(&(cq_p->cq_lock));
  /* The following check must be done with CQ-lock locked, since resize may change cur_buf 
     on the same time                                                                       */
  if ((cqe_num >= ((1U << cq_p->cur_buf.log2_num_o_cqes) - cq_p->cur_buf.spare_cqes)) || (cqe_num == 0)) { 
    /* reminder: 1 CQE is always reserved */
    MTL_ERROR2("THHUL_cqm_peek_cq(cqn=0x%X): cqe_num=%u , max_num_o_cqes=%u .\n",
               cq_p->cq_num,cqe_num,
               ((1U << cq_p->cur_buf.log2_num_o_cqes) - cq_p->cur_buf.spare_cqes - 1));
    ret= HH_E2BIG_CQE_NUM;
  } else {
    cur_cqe= (volatile u_int32_t *)
      (cq_p->cur_buf.cqe_buf_base + 
       (((cq_p->cur_buf.consumer_index + cqe_num - 1) & MASK32(cq_p->cur_buf.log2_num_o_cqes)) 
        << LOG2_CQE_SZ));
    ret= ( (!is_cqe_hw_own(cur_cqe)) || (count_cqes(cq_p,cqe_num,NULL) >= cqe_num)) ? HH_OK : HH_CQ_EMPTY ;
  }
  
  MOSAL_spinlock_unlock(&(cq_p->cq_lock));    

  return ret; 
}



HH_ret_t THHUL_cqm_req_comp_notif( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cq_notif_type_t notif_type 
) 
{ 
  THHUL_cq_t *thhul_cq_p= (THHUL_cq_t*)cq;
  u_int32_t last_consumer_index;
  HH_ret_t rc;

  if (thhul_cq_p == NULL) {
    MTL_ERROR1("THHUL_cqm_req_comp_notif: NULL CQ handle.\n");
    return HH_EINVAL_CQ_HNDL;
  }
  
  MOSAL_spinlock_irq_lock(&(thhul_cq_p->cq_lock));
#ifdef NO_CQ_CI_DBELL
  /* In "overrun ignore" mode, last consumed must be given */
  last_consumer_index= 
    ((thhul_cq_p->cur_buf.consumer_index - 1) & MASK32(thhul_cq_p->cur_buf.log2_num_o_cqes));
#else
  /* Consumer index is updated on every poll, so InfiniHost has its updated value */
  last_consumer_index= 0xFFFFFFFF ; /* Use current CI value */
#endif
  sync_consumer_index(thhul_cq_p, __func__);/*Consumer index must be updated before req. an event*/
  MOSAL_spinlock_unlock(&(thhul_cq_p->cq_lock));

  switch (notif_type) {
    case VAPI_SOLIC_COMP:
      rc= THH_uar_cq_cmd(thhul_cq_p->uar,TAVOR_IF_UAR_CQ_NOTIF_SOLIC_COMP,
                         thhul_cq_p->cq_num,last_consumer_index);
      break;
    case VAPI_NEXT_COMP:
      rc= THH_uar_cq_cmd(thhul_cq_p->uar,TAVOR_IF_UAR_CQ_NOTIF_NEXT_COMP,
                         thhul_cq_p->cq_num,last_consumer_index);
      break;
    default:
      rc= HH_EINVAL;  /* Invalid notification request */
  }
  
  return rc;  
}

HH_ret_t THHUL_cqm_req_ncomp_notif( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cqe_num_t cqe_num 
) 
{ 
  THHUL_cq_t *cq_p= (THHUL_cq_t*)cq;
  VAPI_cqe_num_t hw_cqe_cnt,sw_cqe_cnt;
  HH_ret_t rc;

  if (cq_p == NULL) {
    MTL_ERROR1("THHUL_cqm_req_ncomp_notif: NULL CQ handle.\n");
    return HH_EINVAL;
  }

  /* cqe_num fix (due to CQEs with error... external CQEs) :
   * Check if cqe_num requirement was already fulfilled with external/sw CQEs
   * If yes: generate immediate event by setting cqe_num to 1
   * Otherwise: cqe_num is set to the HW number of CQEs based on current difference from sw_cnt 
   */
  MOSAL_spinlock_irq_lock(&(cq_p->cq_lock));
  /* Check cqe_num limits (must be done with lock held to avoid a race with "resize") */
  if ((cqe_num >= ((1U << cq_p->cur_buf.log2_num_o_cqes) - cq_p->cur_buf.spare_cqes)) || 
      (cqe_num == 0)  ||
      (cqe_num > MAX_NCOMP_NOTIF)) { 
    /* reminder: 1 CQE is always reserved */
    MTL_ERROR2("THHUL_cqm_req_ncomp_notif(cqn=%d): cqe_num=%d , max_num_o_cqes=%d .\n",
               cq_p->cq_num,cqe_num,
               ((1U << cq_p->cur_buf.log2_num_o_cqes) - cq_p->cur_buf.spare_cqes - 1));
    MOSAL_spinlock_unlock(&(cq_p->cq_lock));
    return HH_E2BIG_CQE_NUM;
  }
  sw_cqe_cnt= count_cqes(cq_p,cqe_num,&hw_cqe_cnt);
  cqe_num= (sw_cqe_cnt >= cqe_num) ? 1 : hw_cqe_cnt + (cqe_num - sw_cqe_cnt) ;

  sync_consumer_index(cq_p, __func__);/*Consumer index must be updated before req. an event*/

  rc= THH_uar_cq_cmd(cq_p->uar,TAVOR_IF_UAR_CQ_NOTIF_NCOMP,cq_p->cq_num,cqe_num);
  MOSAL_spinlock_unlock(&(cq_p->cq_lock));
  
  return rc;  
}

/**********************************************************************************************
 *                    Private Functions 
 **********************************************************************************************/


static HH_ret_t cqe_buf_alloc(THHUL_cqe_buf_t *cqe_buf, VAPI_cqe_num_t num_o_cqes)
{
  u_int32_t i;
  volatile u_int8_t* cur_cqe_owner_byte;
  VAPI_cqe_num_t actual_num_o_cqes; /* Number of CQEs in the CQEs buffer */
  VAPI_cqe_num_t possible_spare_cqes;
  
  cqe_buf->log2_num_o_cqes= floor_log2(num_o_cqes) + 1; /* next power of 2 including extra CQE */
  actual_num_o_cqes= (1 << cqe_buf->log2_num_o_cqes);  
#ifdef MT_KERNEL
  if (((actual_num_o_cqes + 1) * CQE_SZ) <= CQ_KMALLOC_LIMIT) 
    cqe_buf->cqe_buf_orig= 
      MOSAL_pci_phys_alloc_consistent((actual_num_o_cqes + 1) * CQE_SZ, LOG2_CQE_SZ); /* one extra for alignment */
  else
#endif
#if !defined(MT_KERNEL) && defined(MT_FORK_SUPPORT)
/* Fork workaroud - cover full pages */
    cqe_buf->cqe_buf_orig= 
      MOSAL_pci_virt_alloc_consistent(
        (MOSAL_SYS_PAGE_SIZE-1)/*for page alignement*/ +
            MT_UP_ALIGNX_ULONG_PTR(actual_num_o_cqes * CQE_SZ, MOSAL_SYS_PAGE_SHIFT),
        LOG2_CQE_SZ);
#else
    cqe_buf->cqe_buf_orig= 
      MOSAL_pci_virt_alloc_consistent((actual_num_o_cqes + 1) * CQE_SZ, LOG2_CQE_SZ); /* one extra for alignment */
#endif
  if (cqe_buf->cqe_buf_orig == NULL) {
    MTL_ERROR1("%s: Failed to allocate CQEs buffer of 0x%X bytes.\n",__func__,
      (actual_num_o_cqes + 1) * CQE_SZ);
    return HH_EAGAIN;
  }
#if !defined(MT_KERNEL) && defined(MT_FORK_SUPPORT)
  cqe_buf->cqe_buf_base= MT_UP_ALIGNX_VIRT((MT_virt_addr_t)cqe_buf->cqe_buf_orig,MOSAL_SYS_PAGE_SHIFT);
#else  
  /* buffer must be aligned to CQE size */
  cqe_buf->cqe_buf_base= MT_UP_ALIGNX_VIRT((MT_virt_addr_t)cqe_buf->cqe_buf_orig,LOG2_CQE_SZ);
#endif
  /* Initialize all CQEs to HW ownership */
  cur_cqe_owner_byte= (volatile u_int8_t*)(cqe_buf->cqe_buf_base+CQE_OWNER_BYTE_OFFSET);
  for (i= 0; i < actual_num_o_cqes; i++) { 
    *cur_cqe_owner_byte= (1<<CQE_OWNER_SHIFT);
    cur_cqe_owner_byte+= CQE_SZ;
  }
  possible_spare_cqes= actual_num_o_cqes - 1/*Reserved CQE*/ - num_o_cqes; /* For CQ DB coalescing */
  cqe_buf->spare_cqes= (possible_spare_cqes > MAX_CQDB2DELAY) ? MAX_CQDB2DELAY : possible_spare_cqes;
  MTL_DEBUG4(MT_FLFMT("%s: spare_cqes=%u"),__func__,cqe_buf->spare_cqes);
  cqe_buf->consumer_index= 0;
  return HH_OK;
}

static void cqe_buf_free(THHUL_cqe_buf_t *cqe_buf)
{
  /* Cleanup CQ resources */
#ifdef MT_KERNEL
  if ((((1U<<cqe_buf->log2_num_o_cqes)+1)*CQE_SZ) <= CQ_KMALLOC_LIMIT) 
    MOSAL_pci_phys_free_consistent(cqe_buf->cqe_buf_orig, ((1U<<cqe_buf->log2_num_o_cqes)+1)*CQE_SZ);
  else
#endif
    MOSAL_pci_virt_free_consistent(cqe_buf->cqe_buf_orig, ((1U<<cqe_buf->log2_num_o_cqes)+1)*CQE_SZ);
}

/* Perform the "CQ cleanup" flow (removing of CQEs of a RESET QP) and return 
 * amount of removed CQEs (the change in consumer index)
 * - cur_pi_p : return PI at given buffer (to be used in THHUL_CQ_RESIZE_PREP state)
 */
static u_int32_t cqe_buf_cleanup(THHUL_cqe_buf_t *cqe_buf,IB_wqpn_t qp,
                                 THHUL_srqm_t srqm, HHUL_srq_hndl_t srq,
                                 u_int32_t *cur_producer_index_p)
{
  u_int32_t cur_tail_index,next_consumer_index,cur_cqe_index;
  u_int32_t outstanding_cqes,i;
  u_int32_t removed_cqes= 0;
  volatile u_int32_t *cur_cqe_p;
  volatile u_int32_t *next_cqe_p;
  IB_wqpn_t cur_qpn;
  const u_int32_t num_o_cqes_mask= MASK32(cqe_buf->log2_num_o_cqes);
  MT_bool is_rq_cqe;
  u_int32_t wqe_addr_32lsb;
  VAPI_wr_id_t wqe_id;
  

  /* Find last CQE is software ownership (cur_tail) */
  outstanding_cqes= 0;
  cur_tail_index= cqe_buf->consumer_index;
#ifndef NO_CQ_CI_DBELL
  while (1) { /* Break out when find a CQE in HW ownership */
    /* (there must be at least one CQE in HW ownership - the reserved "full" CQE)*/
#else
  while (outstanding_cqes <= (1<<cqe_buf->log2_num_o_cqes)) {
    /* In CQ-overrun-ignore mode, all CQEs in CQ may be in SW ownership... */
#endif
    cur_cqe_p= (volatile u_int32_t *)
      (cqe_buf->cqe_buf_base + (cur_tail_index << LOG2_CQE_SZ));
    if (is_cqe_hw_own(cur_cqe_p))  break; /* no more outstanding CQEs */ 

    outstanding_cqes++;
    cur_tail_index= (cur_tail_index + 1) & num_o_cqes_mask;
  }
  *cur_producer_index_p= cur_tail_index; /* To be used on resized buffer */
  /* move back to last in SW ownership */
  cur_cqe_index= next_consumer_index= (cur_tail_index - 1) & num_o_cqes_mask;

  MTL_DEBUG4(MT_FLFMT("Found %d outstanding CQEs. Last CQE at index %d."),
    outstanding_cqes,cur_cqe_index);
  
  /* Scan back CQEs (cur_cqe) and move all CQEs not of given qpn back to "cur_head" */
  for (i= 0; i < outstanding_cqes; i++) {
    cur_cqe_p= (volatile u_int32_t *)
      (cqe_buf->cqe_buf_base + (cur_cqe_index << LOG2_CQE_SZ));
    cur_qpn= 
      MOSAL_be32_to_cpu(cur_cqe_p[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,my_qpn)>>2])
      & MASK32(24);

    if (cur_qpn == qp) {  /* A CQE to remove */
      /* Go back only with the cur_cqe_index, leave next_consumer_index behind (for next copy) */
      cur_cqe_index= (cur_cqe_index - 1) & num_o_cqes_mask;
      
      /* If associated with SRQ must invoke THHUL_srqm_comp to release WQE */
      if (srq != HHUL_INVAL_SRQ_HNDL) {
        is_rq_cqe= (MT_EXTRACT32(
          MOSAL_be32_to_cpu(cur_cqe_p[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,s)>>2]),
          MT_BIT_OFFSET(tavorprm_completion_queue_entry_st,s) & MASK32(5),
          MT_BIT_SIZE(tavorprm_completion_queue_entry_st,s))                                    == 0);
        if (is_rq_cqe) { /* Completion must be reported to SRQ */
          wqe_addr_32lsb= /* Mask off size bits from dword of WQE addr. */
            MOSAL_be32_to_cpu(
              cur_cqe_p[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,wqe_adr)>>2]
            ) & 
            (0xFFFFFFFF << MT_BIT_SIZE(tavorprm_completion_queue_entry_st,wqe_size));
          THHUL_srqm_comp(srqm, srq, wqe_addr_32lsb, &wqe_id); /* Release WQE */
        }
      }

    } else { /* A CQE to copy (if any CQE was removed) */
      if (cur_cqe_index != next_consumer_index) { /* Copy required */
        MTL_DEBUG4(MT_FLFMT("Moving CQE at index %d to index %d\n"),
          cur_cqe_index,next_consumer_index); 
        next_cqe_p= (volatile u_int32_t *)
          (cqe_buf->cqe_buf_base + (next_consumer_index << LOG2_CQE_SZ));
        memcpy((void*)next_cqe_p,(void*)cur_cqe_p,CQE_SZ);
      }
      /* Go back with both indices */
      cur_cqe_index= (cur_cqe_index - 1) & num_o_cqes_mask;
      next_consumer_index= (next_consumer_index - 1) & num_o_cqes_mask;
    }
  }
  
  if (cur_cqe_index != next_consumer_index) { /* CQEs were removed */
    /* Return to hardware ownership CQEs at amount of removed CQEs (from consumer index side) */
    for (cur_cqe_index= cqe_buf->consumer_index; ; 
         cur_cqe_index= (cur_cqe_index + 1) & num_o_cqes_mask) {
      removed_cqes++;
      cur_cqe_p= (volatile u_int32_t *) 
        (cqe_buf->cqe_buf_base + (cur_cqe_index << LOG2_CQE_SZ));
      set_cqe_to_hw_own(cur_cqe_p);
      if (cur_cqe_index == next_consumer_index)  break; /* Returned all including this one */
    
    }
    
    /* update consumer index - go back to location of last copied */
    cqe_buf->consumer_index= (next_consumer_index + 1) & num_o_cqes_mask; 
  }

  return removed_cqes;
}

/* This function copies any old CQEs left in cur_buf to resized_buf */
/* (Should be called with CQ lock held) */
static void cqe_buf_cpy2resized(
  THHUL_cqe_buf_t *cur_buf, 
  THHUL_cqe_buf_t *resized_buf, 
  MT_bool compute_new_pi) /* New resize-CQ flow */
{
  u_int32_t cur_cqe_index;
  u_int32_t outs_cqes_at_ci=0,outs_cqes_at_cq_base=0;
  u_int32_t *outs_cqes_p; /* pointer to currect count (one of the above) */
  volatile u_int32_t *cur_cqe_p;
  void *cur_cpy_p;     /* Pointer to copy (from) point */
  void *resized_cpy_p; /* Pointer to copy (to) point */
  u_int32_t resized_cqes_at_top; /* CQEs available to buffer's top - when wrap around */
  u_int32_t resized_cur_pi; /* Current "producer index" */
  const u_int32_t num_o_cqes_mask= MASK32(cur_buf->log2_num_o_cqes);
  const u_int32_t new_num_o_cqes_mask= MASK32(resized_buf->log2_num_o_cqes); /* for modulo */
  

  /* Count number of CQEs in cur_buf */
  outs_cqes_p= &outs_cqes_at_ci; /* First count the CQEs above the consumer index */
  cur_cqe_index= cur_buf->consumer_index;
  while (1) { /* Break out when find a CQE in HW ownership */
    /* (there must be at least one CQE in HW ownership - the reserved "full" CQE)*/
    cur_cqe_p= (volatile u_int32_t *)
      (cur_buf->cqe_buf_base + (cur_cqe_index << LOG2_CQE_SZ));
    if (is_cqe_hw_own(cur_cqe_p))  break; /* no more outstanding CQEs */ 
    (*outs_cqes_p)++;
    cur_cqe_index= (cur_cqe_index + 1) & num_o_cqes_mask;
    if (cur_cqe_index == 0) { /* next CQEs are at CQ base */
      outs_cqes_p= &outs_cqes_at_cq_base;
    }
  }

  if (compute_new_pi) {
    resized_buf->consumer_index= cur_cqe_index & new_num_o_cqes_mask; /* New fixed resize-CQ */
    MTL_DEBUG5(MT_FLFMT("%s: old_pi=%u new_pi=%u new_log2_sz=%u"), __func__,
                cur_cqe_index, resized_buf->consumer_index, resized_buf->log2_num_o_cqes);
  } else { /* legacy flow */
    /* Number of outstanding CQEs in old buffer is always less than new consumer index */
    if (resized_buf->consumer_index < outs_cqes_at_ci + outs_cqes_at_cq_base) { /* sanity check */
      MTL_ERROR1(MT_FLFMT(
        "THHUL_cqm_resize_cq_done: Unexpected error !"
        " found more outstanding CQEs (%d) than resized buffer's consumer index (%d) !"),
        outs_cqes_at_ci + outs_cqes_at_cq_base,resized_buf->consumer_index);
      return;
    }
  }

  resized_buf->consumer_index = /* This computation should work for legacy mode, too */
    (resized_buf->consumer_index - outs_cqes_at_ci - outs_cqes_at_cq_base) & new_num_o_cqes_mask;
  resized_cur_pi= resized_buf->consumer_index; /* Where CQE copy starts at resized buffer */

  if (outs_cqes_at_ci > 0) { /* First copy CQEs above consumer index */
    cur_cpy_p= (void *)
      (cur_buf->cqe_buf_base + (cur_buf->consumer_index << LOG2_CQE_SZ));
    resized_cpy_p= (void *)
      (resized_buf->cqe_buf_base + (resized_buf->consumer_index << LOG2_CQE_SZ));
    resized_cqes_at_top= (1U<<resized_buf->log2_num_o_cqes) - resized_cur_pi;
    if (resized_cqes_at_top > outs_cqes_at_ci)  { /* enough room for all CQEs at CI ? */
      memcpy(resized_cpy_p, cur_cpy_p, outs_cqes_at_ci << LOG2_CQE_SZ);
    } else {
      memcpy(resized_cpy_p, cur_cpy_p, resized_cqes_at_top << LOG2_CQE_SZ);
      resized_cpy_p= (void *)(resized_buf->cqe_buf_base);
      cur_cpy_p= (char*)cur_cpy_p + (resized_cqes_at_top << LOG2_CQE_SZ);
      memcpy(resized_cpy_p, cur_cpy_p, (outs_cqes_at_ci - resized_cqes_at_top) << LOG2_CQE_SZ);
    }
    resized_cur_pi= (resized_cur_pi + outs_cqes_at_ci) & new_num_o_cqes_mask;
  }
  if (outs_cqes_at_cq_base > 0) { /* Next copy CQEs at CQ base (wrap around...) */
    cur_cpy_p= (void *) cur_buf->cqe_buf_base ;
    resized_cpy_p= (void *) (resized_buf->cqe_buf_base + (resized_cur_pi << LOG2_CQE_SZ)) ;
    resized_cqes_at_top= (1U<<resized_buf->log2_num_o_cqes) - resized_cur_pi;
    if (resized_cqes_at_top > outs_cqes_at_cq_base)  { /* enough room for all CQEs at base ? */
      memcpy(resized_cpy_p, cur_cpy_p, outs_cqes_at_cq_base << LOG2_CQE_SZ);
    } else {
      memcpy(resized_cpy_p, cur_cpy_p, resized_cqes_at_top << LOG2_CQE_SZ);
      resized_cpy_p= (void *)(resized_buf->cqe_buf_base);
      cur_cpy_p= (char*)cur_cpy_p + (resized_cqes_at_top << LOG2_CQE_SZ);
      memcpy(resized_cpy_p, cur_cpy_p, (outs_cqes_at_cq_base - resized_cqes_at_top) << LOG2_CQE_SZ);
    }
  }

  return;
}

/* Count number of real CQEs (i.e., including CQEs with error) up to given limit */
/* (return the real number of CQEs) */
/* The function must be invoked with CQ lock held */
static VAPI_cqe_num_t count_cqes( 
  /*IN*/ THHUL_cq_t *cq_p,
  /*IN*/ VAPI_cqe_num_t cqe_cnt_limit, /* Limit count up to given HW CQEs */
  /*OUT*/ VAPI_cqe_num_t *hw_cqe_cnt_p /* HW CQEs count (optional) */
)
{
  volatile u_int32_t *cur_cqe;
  VAPI_cqe_num_t sw_cqe_cntr= 0;
  VAPI_cqe_num_t hw_cqe_cntr= 0;
  u_int32_t wqe_addr_32lsb;
  IB_wqpn_t qpn;
  u_int8_t opcode;
  u_int16_t dbdcnt= 0;

  /* Count CQEs including "external" of CQEs with error */
  while (hw_cqe_cntr < cqe_cnt_limit) {
    /* Find CQE and check ownership */
    cur_cqe= (volatile u_int32_t *)
      (cq_p->cur_buf.cqe_buf_base + 
       (((cq_p->cur_buf.consumer_index + hw_cqe_cntr) & MASK32(cq_p->cur_buf.log2_num_o_cqes)) 
        << LOG2_CQE_SZ));
    if (is_cqe_hw_own(cur_cqe))  break; /* no more CQEs */

    opcode= ((volatile u_int8_t*)cur_cqe)[CQE_OPCODE_BYTE_OFFSET]; /* get completion status */
    if ((opcode & CQE_ERROR_STATUS_MASK) != CQE_ERROR_STATUS_MASK) {  /* Completed OK */	
      sw_cqe_cntr++;
    } else { /* CQE with error - count external CQEs */
      
      wqe_addr_32lsb= (
        MOSAL_cpu_to_be32(
          cur_cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st, wqe_adr)>>2])
          & 
          (~MASK32(CQE_WQE_ADR_BIT_SZ)));

      qpn= (
        MOSAL_cpu_to_be32(
          cur_cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st, my_qpn)>>2]) 
          & 
          MASK32(24) );
      
      dbdcnt= (
        MOSAL_cpu_to_be32(
          cur_cqe[MT_BYTE_OFFSET(tavorprm_completion_queue_entry_st,
                              immediate_ethertype_pkey_indx_eecredits)>>2]) 
          & 
          MASK32(CQE_ERROR_DBDCNT_BIT_SIZE) );

      /* Add total number of WQEs "hang" over given CQE with error */
      sw_cqe_cntr+= THHUL_qpm_wqe_cnt(cq_p->qpm, qpn, wqe_addr_32lsb, dbdcnt);
    }
    hw_cqe_cntr++; /* Continue to the next HW CQE */
  }
  
  if (hw_cqe_cnt_p)  *hw_cqe_cnt_p= hw_cqe_cntr;
  MTL_DEBUG5(MT_FLFMT("%s: cqe_cnt_limit=%d sw_cqe_cntr=%d hw_cqe_cntr=%d\n"),__func__,
              cqe_cnt_limit,sw_cqe_cntr,hw_cqe_cntr);
  return sw_cqe_cntr;
}

