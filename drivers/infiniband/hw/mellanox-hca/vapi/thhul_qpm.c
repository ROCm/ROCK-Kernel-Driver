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

#define C_THHUL_QPM_C

#include <mosal.h>
#include <ib_defs.h>
#include <nMPGA.h>
#include <vapi.h>
#include <tlog2.h>
#include <MT23108.h>
#include <thhul.h>
#include <uar.h>
#include <thhul_hob.h>
#include <thhul_cqm.h>
#include <thhul_srqm.h>
#include <thhul_pdm.h>
#include <udavm.h>
#include <vapi_common.h>

/* THH_qpm Pkey and GID table access for usage of the special QPs (mlx IB headers) */
#ifdef MT_KERNEL
#include <thh_hob.h>
#endif

#if defined(MT_KERNEL) && defined(__LINUX__)
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <linux/highmem.h>
#endif

#include "thhul_qpm.h"

#include <mtperf.h>
MTPERF_NEW_SEGMENT(THH_uar_sendq_dbell,200);
MTPERF_NEW_SEGMENT(WQE_build_send,2000);
MTPERF_NEW_SEGMENT(SQ_WQE_copy,2000);

#ifndef MT_KERNEL
/* instead of "ifdef"ing all over the code we define an empty macro */
#define MOSAL_pci_phys_free_consistent(addr,sz)  do {} while(0);
#endif


/* Limit kmalloc to 2 pages (if this fails, vmalloc will fail too) */
#define WQ_KMALLOC_LIMIT (4*MOSAL_SYS_PAGE_SIZE)
#define SMALL_VMALLOC_AREA (1<<28)  /* VMALLOC area of 256MB or less is considered a scarce resource */

#define LOG2_QP_HASH_TBL_SZ 8
#define QP_HASH_TBL_SZ  (1<<8)

#define WQE_ALIGN_SHIFT 6        /* WQE address should be aligned to 64 Byte */
#define WQE_SZ_MULTIPLE_SHIFT 4           /* WQE size must be 16 bytes multiple */
/* WQE segments sizes */
#define WQE_SEG_SZ_NEXT (sizeof(struct wqe_segment_next_st)/8)            /* NEXT segment */
#define WQE_SEG_SZ_CTRL (sizeof(struct wqe_segment_ctrl_send_st)/8)       /* CTRL segment */
#define WQE_SEG_SZ_RD   (sizeof(struct wqe_segment_rd_st)/8)              /* DATAGRAM:RD */
#define WQE_SEG_SZ_UD   (sizeof(struct wqe_segment_ud_st)/8)              /* DATAGRAM:UD */
#define WQE_SEG_SZ_RADDR (sizeof(struct wqe_segment_remote_address_st)/8) /* Remote address */
#define WQE_SEG_SZ_ATOMIC (sizeof(struct wqe_segment_atomic_st)/8)        /* Atomic */
#define WQE_SEG_SZ_BIND (sizeof(struct wqe_segment_bind_st)/8)            /* Bind */
/* There is either BIND or RADDR+ATOMIC */
#define WQE_SEG_SZ_BIND_RADDR_ATOMIC ((WQE_SEG_SZ_RADDR+WQE_SEG_SZ_ATOMIC) >  WQE_SEG_SZ_BIND ? \
   (WQE_SEG_SZ_RADDR+WQE_SEG_SZ_ATOMIC) : WQE_SEG_SZ_BIND )
#define WQE_SEG_SZ_SG_ENTRY (sizeof(struct wqe_segment_data_ptr_st)/8)/* Scatter/Gather entry(ptr)*/
#define WQE_SEG_SZ_SG_ENTRY_DW (sizeof(struct wqe_segment_data_ptr_st)/32)/* (same in DWORDs) */
/* INLINE segment for UD headers (SMI/GSI) */
#define IB_RWH_SZ 4
#define IB_ICRC_SZ 4
#define WQE_INLINE_SZ_BCOUNT 4
/* INLINE segment for UD headers (SMI/GSI) */
#define WQE_INLINE_SZ_UD_HDR \
  MT_UP_ALIGNX_U32((WQE_INLINE_SZ_BCOUNT+IB_LRH_LEN+IB_GRH_LEN+IB_BTH_LEN+IB_DETH_LEN),4)
/* INLINE segment for RAW-Ethertype */
#define WQE_INLINE_SZ_RAW_HDR \
  MT_UP_ALIGNX_U32((WQE_INLINE_SZ_BCOUNT+IB_LRH_LEN+IB_RWH_SZ),4)
#define WQE_INLINE_ICRC MT_UP_ALIGNX_U32(WQE_INLINE_SZ_BCOUNT+IB_ICRC_SZ,4)
#define MAX_WQE_SZ 1008
#define BIND_WQE_SZ (WQE_SEG_SZ_NEXT+WQE_SEG_SZ_CTRL+WQE_SEG_SZ_BIND)

#define MAX_ALLOC_RETRY 3  /* Maximum retries to get WQEs buffer which does not cross 4GB boundry */

#define IS_VALID_QPN(qpn) ((qpn) <=  0x00FFFFFF)
#define DEFAULT_PKEY 0xFFFF
#define QP1_PKEY_INDEX 0xFFFFFFFF

#define RESERVED_MEMBIND_EECN 0  /* Pseudo EE-context reserved for memory binding processing */

/* Dpool in size granularity of 1KB */
#define THHUL_DPOOL_SZ_MIN_KB 1 /* Minimum WQEs buffer of 1KB */
#define THHUL_DPOOL_SZ_MAX_KB 64 /* Max. is 64KB */
#define THHUL_DPOOL_SZ_UNIT_SHIFT 10 /* 1KB units shift */
#define THHUL_DPOOL_GRANULARITY_SHIFT 10 /* 1KB garnularity - for alignment */
#define THHUL_DPOOL_SZ_BASE_BUF_KB \
  (THHUL_DPOOL_SZ_MAX_KB*2) /* Size of buffer shared among dpools*/

/* Descriptors pool for small QPs */
/* This data structure allows sharing of locked pages among QPs in order to reduce amount of
  locked pages and assure they cover full pages (fork support) */
typedef struct THHUL_qp_dpool_st {
  MT_size_t buf_size_kb;     /* Each buffer in the pool */
  void* free_buf_list;
  unsigned long ref_cnt;  /* When reached zero, may be freed */
  void* orig_buf;  /* Pointer to allocated memory chunk */
  MT_size_t orig_size;
  MT_bool used_virt_alloc;
  struct THHUL_qp_dpool_st *prev; /* list of dpools of same size */
  struct THHUL_qp_dpool_st *next; /* list of dpools of same size */
} THHUL_qpm_dpool_t;

typedef struct { /* Queue resources context */
  MT_virt_addr_t wqe_buf;  /* The buffer for this queue WQEs - aligned to WQE size */ 
  VAPI_wr_id_t *wqe_id; /* Array of max_outs entries for holding each WQE ID (WQE index based) */
  u_int32_t max_outs; /* Max. outstanding (number of WQEs in buffer) */
  u_int32_t cur_outs; /* Currently outstanding */
  u_int32_t max_sg_sz;  /* Max. Scatter/Gather list size */
  MT_size_t log2_max_wqe_sz; /* WQE size is a power of 2 (software implementation requirement) */
  u_int32_t max_inline_data; /* For send queue only */
  u_int32_t next2post_index; /* Next WQE to use for posting (producer index)*/
  u_int32_t next2free_index; /* Next WQE to use free (consumer index) */
  volatile u_int32_t* last_posted_p;  /* For WQE chain linkage (== NULL if none) */
  u_int32_t *wqe_draft;
  /* Implementation note:
   * Using the "wqe_draft" scratchpad is required since we may
   * perform many read-modify-writes while packing the WQE fields and we
   * have no idea on WQEs buffer location. In cases where the actual WQE is
   * in the attached DDR memory, a direct WQE packing will increase the 
   * building latency since that memory is not cached and each "read-modify-write"
   * would consume time as well as PCI bandwidth.
   * So we build the WQE on the local stack and then copy it (along with the 
   * swapping to big-endian, if needed).
   * Also note that this allows us to allocate the WQE on after all WQE is formatted,
   * thus minimizing the QP (spin)locking time.
   */
  VAPI_qp_state_t qp_state; /* User level assumed QP state */
  /* Implementation note:
   * qp_state is held per queue in order to avoid race in qp_state updates 
   * which may result from polling different CQs for each queue.
   * We would also like to keep the common THHUL_qp_t static during the life
   * of the QP in order to avoid additional synchronization between send and
   * receive queue 
   */
  MOSAL_spinlock_t q_lock;   /* Protect concurrent usage of the queue */
} queue_res_t;

/* HHUL_qp_hndl_t is a pointer to this structure */
typedef struct THHUL_qp_st {
  VAPI_special_qp_t sqp_type; /* VAPI_REGULAR_QP for non-special QP */
  IB_ts_t ts_type;
  IB_wqpn_t qpn;
  HHUL_pd_hndl_t pd;
  THH_uar_t uar;  /* UAR to use for this QP */
  MT_bool is_priv_ud_av;      /* Privileged UD AVs are enforced */
  VAPI_lkey_t   ud_av_memkey; /* Memory key to put for UD AV handles */
  HHUL_cq_hndl_t  sq_cq;
  HHUL_cq_hndl_t  rq_cq;
  void* wqe_buf_orig;   /* Pointer returned by malloc_within_4GB() for WQE buffer */
  MT_bool used_virt_alloc;     /* Used "MOSAL_pci_virt_alloc_consistent" for buffer allocation */
  MT_size_t wqe_buf_orig_size; /* size in bytes of wqe_buf_orig */
  THHUL_qpm_dpool_t *dpool_p; /* If not NULL, wqe_buf_orig taken from this descriptors pool */
  queue_res_t sq_res;   /* Send queue resources */
  queue_res_t rq_res;   /* Receive queue resources */
  HHUL_srq_hndl_t srq;  /* Set to HHUL_INVAL_SRQ_HNDL if not associated with a SRQ */
} *THHUL_qp_t;   

typedef struct qp_hash_entry_st {  /* QPN-to-QP hash table entry */
  IB_wqpn_t qpn;
  THHUL_qp_t qp;
  struct qp_hash_entry_st *next;  /* next in this hash bin */
} qp_hash_entry_t;


struct THHUL_qpm_st { /* THHUL_qpm_t is a pointer to this */
  qp_hash_entry_t* hash_tbl[QP_HASH_TBL_SZ];
  u_int32_t qp_cnt; /* Total number of QPs */
  MOSAL_spinlock_t hash_lock; /* used for qp_cnt protection, too */
  THHUL_qpm_dpool_t *dpool_p[THHUL_DPOOL_SZ_MAX_KB - THHUL_DPOOL_SZ_MIN_KB + 1];/* KB garanularity */
#ifdef THHUL_QPM_DEBUG_DPOOL
  unsigned long dpool_cnt; 
#endif
  MOSAL_mutex_t dpool_lock;
  THHUL_srqm_t srqm;
};

/**********************************************************************************************
 *                    Private functions protoypes declarations
 **********************************************************************************************/
static HH_ret_t qp_prep(
  HHUL_hca_hndl_t hca, 
  VAPI_special_qp_t qp_type, 
  HHUL_qp_init_attr_t *qp_init_attr_p, 
  HHUL_qp_hndl_t *qp_hndl_p, 
  VAPI_qp_cap_t *qp_cap_out_p, 
  THH_qp_ul_resources_t *qp_ul_resources_p, 
  MT_bool in_ddr_mem  /* WQEs buffer allocated in attached DDR mem. or in main memory */
);

static HH_ret_t init_qp(
  HHUL_hca_hndl_t hca, 
  HHUL_qp_init_attr_t *qp_init_attr_p, 
  THHUL_qp_t new_qp
);

static HH_ret_t alloc_wqe_buf(
  /*IN*/ THHUL_qpm_t qpm,
  /*IN*/ MT_bool in_ddr_mem,   /* Allocation of WQEs buffer is requested in attached DDR mem. */
  /*IN*/ u_int32_t max_outs_wqes, /* HCA cap. */
  /*IN*/ u_int32_t max_sg_ent, /* HCA cap. of max.s/g entries */
  /*IN/OUT*/ THHUL_qp_t new_qp,
  /*OUT*/    THH_qp_ul_resources_t *qp_ul_resources_p
);

static HH_ret_t alloc_aux_data_buf(
  /*IN/OUT*/ THHUL_qp_t new_qp
);

static HH_ret_t insert_to_hash(THHUL_qpm_t qpm, THHUL_qp_t qp);

static HH_ret_t remove_from_hash(THHUL_qpm_t qpm, THHUL_qp_t qp);

#ifndef __KERNEL__
static void* dpool_alloc(THHUL_qpm_t qpm, u_int8_t buf_size_kb, THHUL_qpm_dpool_t **dpool_pp);

static void dpool_free(THHUL_qpm_t qpm, THHUL_qpm_dpool_t *dpool_p, void* buf);

#else
#define dpool_free(qpm,dpool_p,buf) \
  MTL_ERROR1(MT_FLFMT("%s: Invoked dpool_free in kernel by mistake"), __func__)
   
#endif

/**********************************************************************************************
 *                    Private inline functions 
 **********************************************************************************************/
/* Computer hash value (bin index) for given QP number */
inline static u_int32_t get_hash_index(IB_wqpn_t qpn)
{
  return (qpn & MASK32(LOG2_QP_HASH_TBL_SZ));
}

inline static u_int32_t get_wqe_index(
  /*IN*/ queue_res_t *q_res_p, 
  /*IN*/ u_int32_t wqe_addr_32lsb,
  /*OUT*/ u_int32_t *wqe_index_p
)
{
  u_int32_t wqe_buf_base_32lsb;

  /* TBD: On QP resize this will have to be modified (buffers may change during QP life cycle) */
  
  wqe_buf_base_32lsb= (u_int32_t)(q_res_p->wqe_buf);
  if (wqe_addr_32lsb >= wqe_buf_base_32lsb) { /* Assure index computation is positive */
    *wqe_index_p= (wqe_addr_32lsb - wqe_buf_base_32lsb) >> q_res_p->log2_max_wqe_sz;
    if (*wqe_index_p < q_res_p->max_outs)  { /* WQE is within this queue */
      /* TBD: check if given wqe_addr_32lsb is aligned to WQE size */
      return HH_OK;
    }
  } 
  
  return HH_EINVAL; /* WQE is not withing this queue */
}



static void dump_qp(qp_hash_entry_t *qp_p)
{
  MTL_ERROR1("==== dump of qpn=%d ====\n", qp_p->qp->qpn);
  MTL_ERROR1("sqp_type=%s\n", VAPI_special_qp_sym(qp_p->qp->sqp_type));
  MTL_ERROR1("ts_type=%s\n", VAPI_ts_type_sym(qp_p->qp->ts_type));
  MTL_ERROR1("pd=%lu\n", qp_p->qp->pd);
  MTL_ERROR1("uar=%p\n", qp_p->qp->uar);
  MTL_ERROR1("is_priv_ud_av=%s\n", qp_p->qp->is_priv_ud_av ? "Yes" : "No");
  MTL_ERROR1("ud_av_memkey=0x%x\n", qp_p->qp->ud_av_memkey);
  MTL_ERROR1("sq_cq=%p\n", qp_p->qp->sq_cq);
  MTL_ERROR1("rq_cq=%p\n", qp_p->qp->rq_cq);
  MTL_ERROR1("wqe_buf_orig=%p\n", qp_p->qp->wqe_buf_orig);
  MTL_ERROR1("used_virt_alloc=%s\n", qp_p->qp->used_virt_alloc ? "Yes" : "No");
  MTL_ERROR1("wqe_buf_orig_size="SIZE_T_FMT"\n", qp_p->qp->wqe_buf_orig_size);
  MTL_ERROR1("dpool_p=%p\n", qp_p->qp->dpool_p);
}

/* Find the queue context from the QP number and WQE address - using the hash table */
inline static HH_ret_t find_wqe(
  /*IN*/ THHUL_qpm_t qpm, 
  /*IN*/ IB_wqpn_t qpn, 
  /*IN*/ u_int32_t wqe_addr_32lsb,
  /*OUT*/ THHUL_qp_t *qp_p,
  /*OUT*/ queue_res_t **q_res_pp,
  /*OUT*/ u_int32_t *wqe_index_p,
  /*OUT*/ VAPI_wr_id_t *wqe_id_p
)
{
  u_int32_t hash_index= get_hash_index(qpn);
  qp_hash_entry_t *cur_entry;
  HH_ret_t rc;

  MOSAL_spinlock_irq_lock(&(qpm->hash_lock));
  for (cur_entry= qpm->hash_tbl[hash_index]; cur_entry != NULL;
       cur_entry= cur_entry->next) {
    if (cur_entry->qpn == qpn) break;
  }
  MOSAL_spinlock_unlock(&(qpm->hash_lock));
  if (cur_entry == NULL) {
    MTL_ERROR1(MT_FLFMT("%s(pid="MT_PID_FMT"): failed to find qpn=0x%x in the hash table"),
               __func__, MOSAL_getpid(), qpn);
    return HH_EINVAL_QP_NUM;  /* not found */
  }
  *qp_p= cur_entry->qp;
  
  /* check if this WQE is of SQ */
  *q_res_pp= &((*qp_p)->sq_res);
  rc= get_wqe_index(*q_res_pp,wqe_addr_32lsb,wqe_index_p);
  if (rc == HH_OK)  {
    *wqe_id_p= (*q_res_pp)->wqe_id[*wqe_index_p]; 
    return HH_OK;
  }
  
  /* check if this WQE is of RQ */
  if ((*qp_p)->srq == HHUL_INVAL_SRQ_HNDL) {
    *q_res_pp= &((*qp_p)->rq_res);
    rc= get_wqe_index(*q_res_pp,wqe_addr_32lsb,wqe_index_p);
    if (rc == HH_OK)  {
      *wqe_id_p= (*q_res_pp)->wqe_id[*wqe_index_p]; 
      return HH_OK;
    }
  } else { /* From SRQ ? */
    *q_res_pp= NULL;
    rc= THHUL_srqm_comp(qpm->srqm, (*qp_p)->srq, wqe_addr_32lsb, wqe_id_p);
    if (rc != HH_OK) {
      MTL_ERROR2(MT_FLFMT("%s: Failed to find WQE in SRQ (WQE=0x%X QPn=0x%X)"), __func__,
                 wqe_addr_32lsb, (*qp_p)->qpn);
    }
  }
  
  MTL_ERROR1(MT_FLFMT("%s(pid="MT_PID_FMT"): failed to find wqe"), __func__, MOSAL_getpid());
  dump_qp(cur_entry);
  return rc; /* Invalid WQE address for this QP */
}

inline static MT_bool is_qpstate_valid_2send(VAPI_qp_state_t cur_state)
{
    switch (cur_state) {
    case VAPI_RTS:
    case VAPI_SQD:
    case VAPI_ERR:
    case VAPI_SQE:  return TRUE;
                    break;
    default:        return FALSE;
    }

}
inline static MT_bool is_qpstate_valid_2recv(VAPI_qp_state_t cur_state)
{
    switch (cur_state) {
    case VAPI_INIT: 
    case VAPI_RTR:
    case VAPI_RTS:
    case VAPI_SQD:
    case VAPI_ERR:
    case VAPI_SQE:  return TRUE;
                    break;
    default:        return FALSE;
    }

}

inline static tavor_if_nopcode_t encode_nopcode(VAPI_wr_opcode_t opcode)
{
  switch (opcode) {
    case VAPI_RDMA_WRITE:
      return TAVOR_IF_NOPCODE_RDMAW;
    case VAPI_RDMA_WRITE_WITH_IMM:
      return TAVOR_IF_NOPCODE_RDMAW_IMM;
    case VAPI_SEND:
      return  TAVOR_IF_NOPCODE_SEND;
    case VAPI_SEND_WITH_IMM:
      return TAVOR_IF_NOPCODE_SEND_IMM;
    case VAPI_RDMA_READ:
      return TAVOR_IF_NOPCODE_RDMAR;
    case VAPI_ATOMIC_CMP_AND_SWP:
      return TAVOR_IF_NOPCODE_ATOM_CMPSWP;
    case VAPI_ATOMIC_FETCH_AND_ADD:
      return TAVOR_IF_NOPCODE_ATOM_FTCHADD;
    default:
      return TAVOR_IF_NOPCODE_NOP;
  }
}

/*********** WQE building functions ***********/

/* Init a not-connected (invalid) "next" segment (i.e. NDS=0) */
inline static u_int32_t WQE_init_next(u_int32_t *wqe_buf)
{
  memset(wqe_buf,0,WQE_SEG_SZ_NEXT);
  return WQE_SEG_SZ_NEXT;
}

inline static u_int32_t WQE_pack_send_next(u_int32_t *segment_p, 
  tavor_if_nopcode_t nopcode, MT_bool fence, u_int32_t dbd,
  u_int32_t next_wqe_32lsb, u_int32_t wqe_sz_16B_chunks,
  IB_eecn_t eecn)
{
  memset(segment_p,0,WQE_SEG_SZ_NEXT);  /* Clear all "RESERVED" */
  segment_p[MT_BYTE_OFFSET(wqe_segment_next_st,nda_31_6)>>2]= next_wqe_32lsb & (~MASK32(6));
  MT_INSERT_ARRAY32(segment_p,nopcode,
    MT_BIT_OFFSET(wqe_segment_next_st,nopcode),MT_BIT_SIZE(wqe_segment_next_st,nopcode));
  MT_INSERT_ARRAY32(segment_p,fence ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_next_st,f),MT_BIT_SIZE(wqe_segment_next_st,f));
  MT_INSERT_ARRAY32(segment_p,dbd,
    MT_BIT_OFFSET(wqe_segment_next_st,dbd),MT_BIT_SIZE(wqe_segment_next_st,dbd));
  MT_INSERT_ARRAY32(segment_p,wqe_sz_16B_chunks,
   MT_BIT_OFFSET(wqe_segment_next_st,nds),MT_BIT_SIZE(wqe_segment_next_st,nds));
  MT_INSERT_ARRAY32(segment_p,eecn,
    MT_BIT_OFFSET(wqe_segment_next_st,nee),MT_BIT_SIZE(wqe_segment_next_st,nee));
  return WQE_SEG_SZ_NEXT;
}

/* Pack Control segment (for sends) */
inline static u_int32_t WQE_pack_ctrl_send(u_int32_t *segment_p,  
    VAPI_comp_type_t comp_type, MT_bool se_bit, u_int32_t event_bit,
    u_int32_t imm_data)
{
  memset(segment_p,0,WQE_SEG_SZ_CTRL);  /* Clear all "RESERVED" */
  MT_INSERT_ARRAY32(segment_p,1,
    MT_BIT_OFFSET(wqe_segment_ctrl_send_st,always1),MT_BIT_SIZE(wqe_segment_ctrl_send_st,always1));
  MT_INSERT_ARRAY32(segment_p,(comp_type == VAPI_SIGNALED) ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_send_st,c),MT_BIT_SIZE(wqe_segment_ctrl_send_st,c));
  MT_INSERT_ARRAY32(segment_p,se_bit ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_send_st,s),MT_BIT_SIZE(wqe_segment_ctrl_send_st,s));
  MT_INSERT_ARRAY32(segment_p,event_bit,
    MT_BIT_OFFSET(wqe_segment_ctrl_send_st,e),MT_BIT_SIZE(wqe_segment_ctrl_send_st,e));
  segment_p[MT_BYTE_OFFSET(wqe_segment_ctrl_send_st,immediate)>>2]= imm_data;
  return WQE_SEG_SZ_CTRL;
}

inline static u_int32_t WQE_pack_ud(u_int32_t *segment_p,
  VAPI_lkey_t ud_av_memkey, u_int64_t ah, 
  IB_wqpn_t destination_qp, IB_qkey_t q_key)
{
  memset(segment_p,0,WQE_SEG_SZ_UD);  /* Clear all "RESERVED" */
  segment_p[MT_BYTE_OFFSET(wqe_segment_ud_st,l_key)>>2]= ud_av_memkey;
  segment_p[MT_BYTE_OFFSET(wqe_segment_ud_st,av_address_63_32)>>2]= (u_int32_t)(ah>>32);
  segment_p[MT_BYTE_OFFSET(wqe_segment_ud_st,av_address_31_5)>>2]= ((u_int32_t)ah & (~MASK32(5)) );
  MT_INSERT_ARRAY32(segment_p,destination_qp,
    MT_BIT_OFFSET(wqe_segment_ud_st,destination_qp),
    MT_BIT_SIZE(wqe_segment_ud_st,destination_qp));
  segment_p[MT_BYTE_OFFSET(wqe_segment_ud_st,q_key)>>2]= q_key;
  return WQE_SEG_SZ_UD;
}

inline static u_int32_t WQE_pack_rd(u_int32_t *segment_p,
  IB_wqpn_t destination_qp, IB_qkey_t q_key)
{
  memset(segment_p,0,WQE_SEG_SZ_RD);  /* Clear all "RESERVED" */
  MT_INSERT_ARRAY32(segment_p,destination_qp,
    MT_BIT_OFFSET(wqe_segment_rd_st,destination_qp),
    MT_BIT_SIZE(wqe_segment_rd_st,destination_qp));
  segment_p[MT_BYTE_OFFSET(wqe_segment_rd_st,q_key)>>2]= q_key;
  return WQE_SEG_SZ_RD;
}

inline static u_int32_t WQE_pack_remote_addr(u_int32_t *segment_p,
  IB_virt_addr_t remote_addr, IB_rkey_t remote_rkey)
{
  memset(segment_p,0,WQE_SEG_SZ_RADDR);  /* Clear all "RESERVED" */
  segment_p[MT_BYTE_OFFSET(wqe_segment_remote_address_st,remote_virt_addr_h)>>2]= 
    (u_int32_t)(remote_addr >> 32);
  segment_p[MT_BYTE_OFFSET(wqe_segment_remote_address_st,remote_virt_addr_l)>>2]= 
    (u_int32_t)(remote_addr & 0xFFFFFFFF);
  segment_p[MT_BYTE_OFFSET(wqe_segment_remote_address_st,rkey)>>2]= remote_rkey;
  return WQE_SEG_SZ_RADDR;
}


inline static u_int32_t WQE_pack_recv_next(u_int32_t *segment_p, 
  u_int32_t next_wqe_32lsb, u_int32_t wqe_sz_16B_chunks)
{
  memset(segment_p,0,WQE_SEG_SZ_NEXT);  /* Clear all "RESERVED" */
  segment_p[MT_BYTE_OFFSET(wqe_segment_next_st,nda_31_6)>>2]= ( next_wqe_32lsb & (~MASK32(6)) ) 
    | 1 ;  /* LS-bit is set to work around bug #16159/16160/16161 */;
  MT_INSERT_ARRAY32(segment_p,1, /* DBD always '1 for RQ */
    MT_BIT_OFFSET(wqe_segment_next_st,dbd),MT_BIT_SIZE(wqe_segment_next_st,dbd));
  MT_INSERT_ARRAY32(segment_p,wqe_sz_16B_chunks,
    MT_BIT_OFFSET(wqe_segment_next_st,nds),MT_BIT_SIZE(wqe_segment_next_st,nds));
  return WQE_SEG_SZ_NEXT;
}

inline static u_int32_t WQE_pack_atomic_cmpswp(u_int32_t *segment_p,
  u_int64_t cmp_data, u_int64_t swap_data)
{
  segment_p[MT_BYTE_OFFSET(wqe_segment_atomic_st,swap_add_h)>>2]= (u_int32_t)(swap_data >> 32);
  segment_p[MT_BYTE_OFFSET(wqe_segment_atomic_st,swap_add_l)>>2]= (u_int32_t)(swap_data & 0xFFFFFFFF);
  segment_p[MT_BYTE_OFFSET(wqe_segment_atomic_st,compare_h)>>2]= (u_int32_t)(cmp_data >> 32);
  segment_p[MT_BYTE_OFFSET(wqe_segment_atomic_st,compare_l)>>2]= (u_int32_t)(cmp_data & 0xFFFFFFFF);
  return WQE_SEG_SZ_ATOMIC;
}

inline static u_int32_t WQE_pack_atomic_fetchadd(u_int32_t *segment_p,u_int64_t add_data)
{
  segment_p[MT_BYTE_OFFSET(wqe_segment_atomic_st,swap_add_h)>>2]= (u_int32_t)(add_data >> 32);
  segment_p[MT_BYTE_OFFSET(wqe_segment_atomic_st,swap_add_l)>>2]= (u_int32_t)(add_data & 0xFFFFFFFF);
  return WQE_SEG_SZ_ATOMIC;
}

/* Build the scatter/gather list (pointer segments) */
inline static u_int32_t WQE_pack_sg_list(u_int32_t *segment_p,
  u_int32_t sg_lst_len,VAPI_sg_lst_entry_t *sg_lst_p)
{
   u_int32_t i;
   u_int32_t *cur_loc_p= segment_p;

   for (i= 0; i < sg_lst_len; i++ , cur_loc_p+= WQE_SEG_SZ_SG_ENTRY_DW) {
     cur_loc_p[MT_BYTE_OFFSET(wqe_segment_data_ptr_st,byte_count)>>2]= 
       (sg_lst_p[i].len & MASK32(31));
     cur_loc_p[MT_BYTE_OFFSET(wqe_segment_data_ptr_st,l_key)>>2]= sg_lst_p[i].lkey;
     cur_loc_p[MT_BYTE_OFFSET(wqe_segment_data_ptr_st,local_address_h)>>2]= 
       (u_int32_t)(sg_lst_p[i].addr >> 32);
     cur_loc_p[MT_BYTE_OFFSET(wqe_segment_data_ptr_st,local_address_l)>>2]= 
       (u_int32_t)(sg_lst_p[i].addr & 0xFFFFFFFF);
   }
   return (u_int32_t)(((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)segment_p));
}


/* Build the WQE in given wqe_buf.
 * Return WQE size.
 */
inline static u_int32_t WQE_build_send(
  THHUL_qp_t qp,
  VAPI_sr_desc_t *send_req_p,
  u_int32_t *wqe_buf)
{
  u_int8_t *cur_loc_p= (u_int8_t*)wqe_buf; /* Current location in the WQE */

  cur_loc_p+= WQE_init_next((u_int32_t*)cur_loc_p); /* Make "unlinked" "next" segment */
  cur_loc_p+= WQE_pack_ctrl_send((u_int32_t*)cur_loc_p,  /* Pack Control segment */
    send_req_p->comp_type, send_req_p->set_se, 0/*event bit*/,
    ((send_req_p->opcode == VAPI_RDMA_WRITE_WITH_IMM) ||
     (send_req_p->opcode == VAPI_SEND_WITH_IMM) ) ? send_req_p->imm_data : 0);

  /* Transport type checks: Datagram segment */
  switch (qp->ts_type) {
    case VAPI_TS_UD:  /* Check if UD (UD datagram segment) */
      cur_loc_p+= WQE_pack_ud((u_int32_t*)cur_loc_p,
        qp->ud_av_memkey,send_req_p->remote_ah,
        send_req_p->remote_qp,send_req_p->remote_qkey);
      break;
    case VAPI_TS_RD:  /* Check if RD (RD datagram segment) */
      cur_loc_p+= WQE_pack_rd((u_int32_t*)cur_loc_p,
        send_req_p->remote_qp,send_req_p->remote_qkey);
      break;
    default:
      break;
  }
  
  /* Opcode checks Remote-address/Atomic segments */
  switch (send_req_p->opcode) {
    /* For RDMA operations: only Remote-address segment */
    case VAPI_RDMA_READ:
    case VAPI_RDMA_WRITE:
    case VAPI_RDMA_WRITE_WITH_IMM:
     cur_loc_p+= WQE_pack_remote_addr((u_int32_t*)cur_loc_p,
       send_req_p->remote_addr,send_req_p->r_key);
     break;
     
    /* Check if Atomic operations (both remote-address and Atomic segments) */
    case VAPI_ATOMIC_CMP_AND_SWP:
      cur_loc_p+= WQE_pack_remote_addr((u_int32_t*)cur_loc_p,send_req_p->remote_addr,
        send_req_p->r_key);
      cur_loc_p+= WQE_pack_atomic_cmpswp((u_int32_t*)cur_loc_p,send_req_p->compare_add,
        send_req_p->swap);
      break;
    case VAPI_ATOMIC_FETCH_AND_ADD:
     cur_loc_p+= WQE_pack_remote_addr((u_int32_t*)cur_loc_p,send_req_p->remote_addr,
       send_req_p->r_key);
     cur_loc_p+= WQE_pack_atomic_fetchadd((u_int32_t*)cur_loc_p,send_req_p->compare_add);
     break;
    default: /*NOP*/
      break;
  }
  
  /* Pack scatter/gather list segments */
  cur_loc_p+= WQE_pack_sg_list((u_int32_t*)cur_loc_p,send_req_p->sg_lst_len,send_req_p->sg_lst_p);
  
  return (u_int32_t)(((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)wqe_buf));
}
  
/* Build UD header as inline data for management QPs over MLX "transport" */
inline static u_int32_t WQE_pack_mlx_ud_header(u_int32_t *segment_p,
  THHUL_qp_t qp, VAPI_sr_desc_t *send_req_p, VAPI_ud_av_t *av_p, HH_hca_hndl_t hh_hndl,
  VAPI_pkey_ix_t pkey_index /* take this index instead of QP's, if not QP1_PKEY_INDEX */)
{
  MPGA_headers_t hdrs;
  IB_LRH_st *LRH_p;
  IB_BTH_st *BTH_p;
  IB_DETH_st *DETH_p;
  u_int8_t *hdrs_buf_p;
#ifdef MT_LITTLE_ENDIAN
  u_int32_t *hdrs_buf32_p;  /* pointer for endiness swapping */
  u_int16_t i;
#endif
  u_int16_t hdrs_sz;
  MT_bool global= av_p->grh_flag;
#ifdef MT_KERNEL
  IB_port_t num_ports;
  IB_port_t port= (qp->qpn & 0xf);  /* QPN of QP used for port 1 has the even index */
  IB_pkey_t cur_pkey= 0;
  HH_ret_t rc;
  
  rc= THH_hob_get_num_ports(hh_hndl,&num_ports);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("Could not get number of HCA ports (%s).\n"),HH_strerror_sym(rc));
    return 0;
  }
  port = (port >= num_ports) ? ((port-num_ports)%num_ports)+1 : (port % num_ports)+1;
#endif

  hdrs_sz= IB_LRH_LEN+IB_BTH_LEN+IB_DETH_LEN;
  if (global)  hdrs_sz+= IB_GRH_LEN;

  /* Set inline entry control */
  *segment_p= ((1<<31) | hdrs_sz) ; /* inline entry | ByteCount */
  hdrs_buf_p= ((u_int8_t*)segment_p) + WQE_INLINE_SZ_BCOUNT /* inline ctrl */ + hdrs_sz;

  /* Put headers data into MPGA structures */  
  if (global) {
    LRH_p= &(hdrs.MPGA_G_ud_send_only.IB_LRH);
    BTH_p= &(hdrs.MPGA_G_ud_send_only.IB_BTH);
    DETH_p= &(hdrs.MPGA_G_ud_send_only.IB_DETH);
    /* Set GRH fields */
    hdrs.MPGA_G_ud_send_only.IB_GRH.IPVer= 6; /* ? */
    hdrs.MPGA_G_ud_send_only.IB_GRH.TClass= av_p->traffic_class;
    hdrs.MPGA_G_ud_send_only.IB_GRH.FlowLabel= av_p->flow_label;
    hdrs.MPGA_G_ud_send_only.IB_GRH.PayLen= IB_BTH_LEN+IB_DETH_LEN+IB_MAD_LEN+IB_ICRC_SZ;
    hdrs.MPGA_G_ud_send_only.IB_GRH.NxtHdr= 0x1B; /* IB-spec.: compliancy statement C8-7 */
    hdrs.MPGA_G_ud_send_only.IB_GRH.HopLmt= av_p->hop_limit; 
    memcpy(&(hdrs.MPGA_G_ud_send_only.IB_GRH.DGID),&(av_p->dgid),sizeof(IB_gid_t));
#ifdef MT_KERNEL
    /* SGID field is supported only in kernel space, due to limited access to the GID table */
    rc= THH_hob_get_sgid(hh_hndl,port,av_p->sgid_index,
      &(hdrs.MPGA_G_ud_send_only.IB_GRH.SGID));
    if (rc != HH_OK) {
      MTL_ERROR1(MT_FLFMT("Error in GID table access (%s).\n"),HH_strerror_sym(rc));
      return 0;
    }
#endif

  } else {  /* local - no GRH */
    LRH_p= &(hdrs.MPGA_ud_send_only.IB_LRH);
    BTH_p= &(hdrs.MPGA_ud_send_only.IB_BTH);
    DETH_p= &(hdrs.MPGA_ud_send_only.IB_DETH);
  }
  
  /* Set LRH fields */
  memset(LRH_p,0,sizeof(IB_LRH_st));
  /* VL must be set for internal loopback ("vl15" bit is ignored) */
  if (qp->sqp_type == VAPI_SMI_QP) LRH_p->VL= 15;
  else LRH_p->VL= 0;
  LRH_p->LVer= 0;
  LRH_p->SL= av_p->sl;
  LRH_p->LNH= global ? IBA_GLOBAL : IBA_LOCAL;

  LRH_p->DLID= av_p->dlid;
  LRH_p->SLID= (av_p->dlid == PERMIS_LID) ? PERMIS_LID : (IB_lid_t) av_p->src_path_bits;
  /* If DLID is permissive LID, we set SLID to the permissive LID too. */
  /* Otherwise, we put in the SLID field the source path bits, and SLR=0, so */ 
  /*   the LID is composed of actual port's LID concatenated with given path bits */

  LRH_p->PktLen= (hdrs_sz+IB_MAD_LEN + IB_ICRC_SZ) >> 2;
  /* Set BTH fields */
  memset(BTH_p,0,sizeof(IB_BTH_st));
  BTH_p->OpCode= UD_SEND_ONLY_OP;
  BTH_p->SE= send_req_p->set_se;
  BTH_p->M= 1;
  BTH_p->PadCnt= 0; /* MADs are always 4byte multiple */
  BTH_p->TVer= 0; 
#ifdef MT_KERNEL
  if (qp->sqp_type == VAPI_GSI_QP) {
    if (pkey_index == QP1_PKEY_INDEX) { /* use QP's pkey */
      rc= THH_hob_get_qp1_pkey(hh_hndl,port,&cur_pkey);
    } else {
      rc= THH_hob_get_pkey(hh_hndl,port,pkey_index,&cur_pkey);
    }
    if (rc != HH_OK) {
      MTL_ERROR1(MT_FLFMT("%s: Error in P-key table access (%s) - using pkey_index 0x%X.\n"),__func__,
                 HH_strerror_sym(rc),pkey_index);
      return 0;
    }
  } else {
      cur_pkey = DEFAULT_PKEY;
  }
  BTH_p->P_KEY= cur_pkey; 
#else
  BTH_p->P_KEY= DEFAULT_PKEY; /* For user space we do not have access to the Pkey table */
#endif
  BTH_p->DestQP= send_req_p->remote_qp;
  /* AckReq and PSN are meaningless for UD */
  /* Set DETH fields */
  memset(DETH_p,0,sizeof(IB_DETH_st));
  DETH_p->SrcQP= (qp->sqp_type == VAPI_SMI_QP) ? 0 : 1; /* invoked only for SMI or GSI */ 
  /* Qkey should be set according to IB-Spec. compliancy statement C10-15, But...      
   * Only QP1/GSI is the special QP which really validates Q-keys and it always uses
   * 0x80010000 (C9-49). So for QP1 we always put this if the high-order bit of the Qkey
   * is set.                                                                             */
  if (qp->sqp_type == VAPI_GSI_QP) {
    DETH_p->Q_Key= (send_req_p->remote_qkey & 0x80000000) ? 0x80010000: send_req_p->remote_qkey;
  } else { /* QP0 */
    /* For QP0 we don't care (QP0 always sends to another QP0 - none of which validates the Qkey) */
    DETH_p->Q_Key= send_req_p->remote_qkey; 
  }

  /* Build the headers */
  if (MPGA_make_headers(&hdrs,UD_SEND_ONLY_OP,
                        LRH_p->LNH,FALSE,IB_MAD_LEN,&hdrs_buf_p) != MT_OK) {
    return 0;
  }
  /* Verify headers size */
  if (hdrs_buf_p != (((u_int8_t*)segment_p) + WQE_INLINE_SZ_BCOUNT)) {/*Should be segment begin*/
    MTL_ERROR2(MT_FLFMT("Error in headers size (%d instead of %d).\n"),
     (unsigned) (hdrs_sz - (hdrs_buf_p - (((u_int8_t*)segment_p) + 4))), hdrs_sz);
    return 0;
  }

#ifdef MT_LITTLE_ENDIAN
  /* MPGA headers returned in BIG endian.  WQE is built in CPU endianess  - so swap bytes */
  for (i= 0 , hdrs_buf32_p= (u_int32_t*)hdrs_buf_p; i < (hdrs_sz>>2); i++) {
    hdrs_buf32_p[i]= MOSAL_cpu_to_be32(hdrs_buf32_p[i]);
  }
#endif

  return MT_UP_ALIGNX_U32(WQE_INLINE_SZ_BCOUNT + hdrs_sz , 4);  /* Align to WQE segment size */
}
  
/* Build ICRC segment for MLX (UD) */
inline static u_int32_t WQE_pack_mlx_icrc_hw(u_int32_t *segment_p)
{
  segment_p[0]= (1<<31) | 4 ; /* Inline ICRC (32 bits = 4 bytes) */
  segment_p[1]= 0;            /* Hardware generated ICRC */
  /* 2 dwords padded for a single Inline Data segment */
  return WQE_INLINE_ICRC;
}

/* Pack Control segment (for mlx-sends) */
inline static u_int32_t WQE_pack_ctrl_mlx(u_int32_t *segment_p,  
    VAPI_comp_type_t comp_type, MT_bool event_bit,
    IB_sl_t sl, IB_static_rate_t max_statrate, MT_bool slr, MT_bool v15,
    u_int16_t vcrc, IB_lid_t rlid)
{
  memset(segment_p,0,WQE_SEG_SZ_CTRL);  /* Clear all "RESERVED" */
  MT_INSERT_ARRAY32(segment_p,(comp_type == VAPI_SIGNALED) ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,c),MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,c));
  MT_INSERT_ARRAY32(segment_p,event_bit ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,e),MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,e));
  MT_INSERT_ARRAY32(segment_p,sl,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,sl),MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,sl));
  MT_INSERT_ARRAY32(segment_p,max_statrate > 0 ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,max_statrate),
    MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,max_statrate));
  MT_INSERT_ARRAY32(segment_p,slr ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,slr),MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,slr));
  MT_INSERT_ARRAY32(segment_p,v15 ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,v15),MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,v15));
  MT_INSERT_ARRAY32(segment_p,vcrc,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,vcrc),MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,vcrc));
  MT_INSERT_ARRAY32(segment_p,rlid,
    MT_BIT_OFFSET(wqe_segment_ctrl_mlx_st,rlid),MT_BIT_SIZE(wqe_segment_ctrl_mlx_st,rlid));
  return WQE_SEG_SZ_CTRL;
}

inline static u_int32_t WQE_build_send_mlx(
  HH_hca_hndl_t hh_hndl,
  THHUL_qp_t qp,
  VAPI_sr_desc_t *send_req_p,
  VAPI_pkey_ix_t pkey_index, /* take this index instead of QP's, if not QP1_PKEY_INDEX */
  u_int32_t *wqe_buf
)  
{
  
  VAPI_ud_av_t av;
  u_int8_t *cur_loc_p= (u_int8_t*)wqe_buf; /* Current location in the WQE */
  u_int8_t *prev_loc_p;
  HH_ret_t rc;

  rc= THH_udavm_parse_udav_entry((u_int32_t*)(send_req_p->remote_ah),&av);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("Invalid UD AV handle - %s"),
      HH_strerror_sym(rc));
    return 0;
  }

   
  if ((av.dlid == PERMIS_LID) && (qp->sqp_type != VAPI_SMI_QP)) {
    MTL_ERROR1(MT_FLFMT("DLID==Permissive-LID while not an SMI QP.\n"));
    return 0;
  }

  cur_loc_p+= WQE_init_next((u_int32_t*)cur_loc_p); /* Make "unlinked" "next" segment */
  cur_loc_p+= WQE_pack_ctrl_mlx((u_int32_t*)cur_loc_p,  /* Pack Control segment */
    send_req_p->comp_type, FALSE/*event bit*/,
    av.sl,av.static_rate,(av.dlid == PERMIS_LID),(qp->sqp_type == VAPI_SMI_QP),
    0/*VCRC*/,av.dlid);
    
    
  /* Build inline headers */
  switch (qp->sqp_type) {
    case VAPI_SMI_QP:  
    case VAPI_GSI_QP:  
      prev_loc_p= cur_loc_p;
      cur_loc_p+= WQE_pack_mlx_ud_header((u_int32_t*)cur_loc_p,qp,send_req_p,&av,hh_hndl,pkey_index);
      if (cur_loc_p == prev_loc_p) {
        return 0;
      }
      /* Pack scatter/gather list segments */
      cur_loc_p+= WQE_pack_sg_list((u_int32_t*)cur_loc_p,send_req_p->sg_lst_len,send_req_p->sg_lst_p);
      cur_loc_p+= WQE_pack_mlx_icrc_hw((u_int32_t*)cur_loc_p);
      break;
    case VAPI_RAW_ETY_QP:
    case VAPI_RAW_IPV6_QP:
    default:
      return 0;
  }
  

  return (u_int32_t)(((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)wqe_buf));
}

/* Pack Control segment (for receive work requests) */
inline static u_int32_t WQE_pack_ctrl_recv(u_int32_t *segment_p,  
    VAPI_comp_type_t comp_type, u_int32_t event_bit)
{
  memset(segment_p,0,WQE_SEG_SZ_CTRL);  /* Clear all "RESERVED" */
  MT_INSERT_ARRAY32(segment_p,(comp_type == VAPI_SIGNALED) ? 1 : 0,
    MT_BIT_OFFSET(wqe_segment_ctrl_recv_st,c),MT_BIT_SIZE(wqe_segment_ctrl_recv_st,c));
  MT_INSERT_ARRAY32(segment_p,event_bit,
    MT_BIT_OFFSET(wqe_segment_ctrl_recv_st,e),MT_BIT_SIZE(wqe_segment_ctrl_recv_st,e));
  return WQE_SEG_SZ_CTRL;
}

inline static u_int32_t WQE_build_recv(
  THHUL_qp_t qp,
  VAPI_rr_desc_t *recv_req_p,
  u_int32_t *wqe_buf
)
{
  u_int8_t *cur_loc_p= (u_int8_t*)wqe_buf; /* Current location in the WQE */

  cur_loc_p+= WQE_init_next((u_int32_t*)cur_loc_p); /* Make "unlinked" "next" segment */
  cur_loc_p+= WQE_pack_ctrl_recv((u_int32_t*)cur_loc_p,
    recv_req_p->comp_type, 0/*event bit*/);
  /* Pack scatter/gather list segments */
  cur_loc_p+= WQE_pack_sg_list((u_int32_t*)cur_loc_p,recv_req_p->sg_lst_len,recv_req_p->sg_lst_p);
  
  return (u_int32_t)(((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)wqe_buf));
}

inline static u_int32_t WQE_build_membind(
  HHUL_mw_bind_t *bind_props_p,
  IB_rkey_t new_rkey,
  u_int32_t *wqe_buf
)
{
  u_int32_t *cur_loc_p= wqe_buf; /* Current location in the WQE */

  cur_loc_p+= (WQE_init_next((u_int32_t*)cur_loc_p)>>2);  /* Make "unlinked" "next" segment */
  cur_loc_p+= (WQE_pack_ctrl_send((u_int32_t*)cur_loc_p,  /* Pack Control segment */
    bind_props_p->comp_type, 0/*SE bit*/, 0/*event bit*/,0/*Imm. data*/)>>2);

  memset(cur_loc_p,0,8);  /* clear reserved bits of first 2 dwords */

  /* Set access bits */
  if (bind_props_p->acl & VAPI_EN_REMOTE_READ) {
    MT_INSERT_ARRAY32(cur_loc_p,1,
      MT_BIT_OFFSET(wqe_segment_bind_st,rr),MT_BIT_SIZE(wqe_segment_bind_st,rr));
  }
  if (bind_props_p->acl & VAPI_EN_REMOTE_WRITE) {
    MT_INSERT_ARRAY32(cur_loc_p,1,
      MT_BIT_OFFSET(wqe_segment_bind_st,rw),MT_BIT_SIZE(wqe_segment_bind_st,rw));
  }
  if (bind_props_p->acl & VAPI_EN_REMOTE_ATOM) {
    MT_INSERT_ARRAY32(cur_loc_p,1,
      MT_BIT_OFFSET(wqe_segment_bind_st,a),MT_BIT_SIZE(wqe_segment_bind_st,a));
  }

  cur_loc_p[MT_BYTE_OFFSET(wqe_segment_bind_st,new_rkey)>>2]= new_rkey;
  cur_loc_p[MT_BYTE_OFFSET(wqe_segment_bind_st,region_lkey)>>2]= bind_props_p->mr_lkey;
  cur_loc_p[MT_BYTE_OFFSET(wqe_segment_bind_st,start_address_h)>>2]= 
    (u_int32_t)(bind_props_p->start >> 32);
  cur_loc_p[MT_BYTE_OFFSET(wqe_segment_bind_st,start_address_l)>>2]= 
    (u_int32_t)(bind_props_p->start & 0xFFFFFFFF);
  cur_loc_p[MT_BYTE_OFFSET(wqe_segment_bind_st,length_h)>>2]= 
    (u_int32_t)(bind_props_p->size >> 32);
  cur_loc_p[MT_BYTE_OFFSET(wqe_segment_bind_st,length_l)>>2]= 
    (u_int32_t)(bind_props_p->size & 0xFFFFFFFF);

  return (u_int32_t)(WQE_SEG_SZ_BIND + ((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)wqe_buf));
}

/* - Allocate a WQE in given send queue, 
   - put given WQE in it, 
   - link to previos WQE and 
   - ring the doorbell 
   * q_lock must be acquired before invoking this function (to protect WQEs allocation).
 */ 
inline static HH_ret_t sq_alloc_wqe_link_and_ring(THHUL_qp_t qp, 
  u_int32_t* wqe_draft, u_int32_t wqe_sz_dwords, 
#ifdef MT_LITTLE_ENDIAN
  u_int32_t swap_sz_dwords, 
#endif
  VAPI_sr_desc_t *send_req_p, tavor_if_nopcode_t nopcode)
{
  u_int32_t next_draft[WQE_SEG_SZ_NEXT>>2]; /* Build "next" segment here */
  volatile u_int32_t* next_wqe; /* Actual WQE pointer */
  u_int32_t i;
  THH_uar_sendq_dbell_t sq_dbell;
  
  /* Check if any WQEs are free to be consumed */
  if (qp->sq_res.max_outs == qp->sq_res.cur_outs) {
    MTL_ERROR4("THHUL_qpm_post_send_req: Send queue is full (%u requests outstanding).\n",
      qp->sq_res.cur_outs);
    return HH_E2BIG_WR_NUM;
  }
  /* Allocate next WQE */
  next_wqe= (u_int32_t*)(qp->sq_res.wqe_buf + 
                        (qp->sq_res.next2post_index << qp->sq_res.log2_max_wqe_sz) );
  qp->sq_res.wqe_id[qp->sq_res.next2post_index]= send_req_p->id;  /* Save WQE ID */
  qp->sq_res.next2post_index = (qp->sq_res.next2post_index + 1) % qp->sq_res.max_outs ;
  qp->sq_res.cur_outs++;
  
  /* copy (while swapping,if needed) the wqe_draft to the actual WQE */
  /* TBD: for big-endian machines we can optimize here and use memcpy */
  MTPERF_TIME_START(SQ_WQE_copy);
#ifdef MT_LITTLE_ENDIAN
  for (i= 0; i < swap_sz_dwords; i++) {
    next_wqe[i]= MOSAL_cpu_to_be32(wqe_draft[i]);
  }
  /* The rest of the WQE should be copied as is (inline data) */
  for (; i < wqe_sz_dwords; i++) {
    next_wqe[i]= wqe_draft[i];
  }
#else /* big endian */
  for (i= 0; i < wqe_sz_dwords; i++) {
    next_wqe[i]= wqe_draft[i];
  }
#endif

  MTPERF_TIME_END(SQ_WQE_copy);
  
  /* Update "next" segment of previous WQE (if any) */
  if (qp->sq_res.last_posted_p != NULL) {
    /* Build linking "next" segment in last posted WQE*/
    WQE_pack_send_next(next_draft, nopcode, send_req_p->fence,
      1/*DBD*/, (u_int32_t)(MT_ulong_ptr_t) next_wqe, wqe_sz_dwords>>2, 
      (qp->ts_type==VAPI_TS_RD) ? send_req_p->eecn : 0);
    for (i= 0;i < (WQE_SEG_SZ_NEXT>>2) ;i++) {  
      /* This copy assures big-endian as well as that NDS is written last */
      qp->sq_res.last_posted_p[i]= MOSAL_cpu_to_be32(next_draft[i]);
    }
  }
  qp->sq_res.last_posted_p= next_wqe;
  
  /* Ring doorbell (send or rd-send) */
  sq_dbell.qpn= qp->qpn;
  sq_dbell.nopcode= nopcode;
  sq_dbell.fence= send_req_p->fence;
  sq_dbell.next_addr_32lsb= (u_int32_t)((MT_virt_addr_t)next_wqe & 0xFFFFFFFF);
  sq_dbell.next_size= wqe_sz_dwords>>2;
  if (qp->ts_type == VAPI_TS_RD) {
    THH_uar_sendq_rd_dbell(qp->uar,&sq_dbell,send_req_p->eecn);
  } else {  /* non-RD send request */
    MTPERF_TIME_START(THH_uar_sendq_dbell);
    THH_uar_sendq_dbell(qp->uar,&sq_dbell);
    MTPERF_TIME_END(THH_uar_sendq_dbell);
  }

  return HH_OK;
}

/* Extract NDS directly from (big-endian) WQE */
inline static u_int8_t WQE_extract_nds(volatile u_int32_t* wqe)
{
  return MT_EXTRACT32(MOSAL_be32_to_cpu(wqe[MT_BYTE_OFFSET(wqe_segment_next_st,nds) >> 2]),
                   MT_BIT_OFFSET(wqe_segment_next_st,nds) & MASK32(5),
                   MT_BIT_SIZE(wqe_segment_next_st,nds) & MASK32(5));
}

/* Extract NDA directly from (big-endian) WQE */
inline static u_int32_t WQE_extract_nda(volatile u_int32_t* wqe)
{
  return (MOSAL_be32_to_cpu(wqe[MT_BYTE_OFFSET(wqe_segment_next_st,nda_31_6) >> 2]) & (~MASK32(6)) );
}

inline static u_int8_t WQE_extract_dbd(volatile u_int32_t* wqe)
{
  return MT_EXTRACT32(MOSAL_be32_to_cpu(wqe[MT_BYTE_OFFSET(wqe_segment_next_st,dbd) >> 2]),
                   MT_BIT_OFFSET(wqe_segment_next_st,dbd) & MASK32(5),
                   MT_BIT_SIZE(wqe_segment_next_st,dbd) & MASK32(5));
}

#ifdef DUMP_SEND_REQ
  static void dump_send_req(THHUL_qp_t qp, HHUL_send_req_t *sr);
#endif

/**********************************************************************************************
 *                    Public API Functions (defined in thhul_hob.h)
 **********************************************************************************************/

HH_ret_t THHUL_qpm_create( 
  THHUL_hob_t hob, 
  THHUL_srqm_t srqm,
  THHUL_qpm_t *qpm_p 
) 
{ 
  int i;

  *qpm_p= (THHUL_qpm_t) MALLOC(sizeof(struct THHUL_qpm_st));
  if (*qpm_p == NULL) {
    MTL_ERROR1("THHUL_qpm_create: Failed allocating THHUL_qpm_st.\n");
    return HH_EAGAIN;
  }

  /* init internal data structures */
  for (i= 0; i < QP_HASH_TBL_SZ; i++) {
    (*qpm_p)->hash_tbl[i]= NULL;
  }
  (*qpm_p)->qp_cnt= 0;
  (*qpm_p)->srqm= srqm;
  for (i= THHUL_DPOOL_SZ_MIN_KB; i <= THHUL_DPOOL_SZ_MAX_KB; i++) {
    (*qpm_p)->dpool_p[i - THHUL_DPOOL_SZ_MIN_KB]= NULL;
  }
  MOSAL_mutex_init(&(*qpm_p)->dpool_lock);
  MOSAL_spinlock_init(&((*qpm_p)->hash_lock));

  return HH_OK;
}


HH_ret_t THHUL_qpm_destroy( 
   THHUL_qpm_t qpm 
) 
{ 
  
    THHUL_qp_t qp;
    qp_hash_entry_t *entry2remove_p;
    int i;

    for (i= 0; i < QP_HASH_TBL_SZ; i++) {
        while (qpm->hash_tbl[i]) {
                entry2remove_p = qpm->hash_tbl[i];
                qpm->hash_tbl[i] = entry2remove_p->next;
                qp = entry2remove_p->qp;
                FREE(entry2remove_p); 

                   /* Clean all CQEs which refer to this QP */
                   THHUL_cqm_cq_cleanup(qp->rq_cq, qp->qpn, qpm->srqm, qp->srq);
                   if (qp->sq_cq != qp->rq_cq) /* additional cleaning required only if SQ's CQ is different */
                   THHUL_cqm_cq_cleanup(qp->sq_cq, qp->qpn, qpm->srqm, HHUL_INVAL_SRQ_HNDL);
                                  
                   /* Free QP resources: Auxilary buffer + WQEs buffer */
                 if (qp->sq_res.wqe_id != NULL) {
                   THH_SMART_FREE(qp->sq_res.wqe_id, qp->sq_res.max_outs * sizeof(VAPI_wr_id_t)); 
                 }
                 if (qp->rq_res.wqe_id != NULL) {
                   THH_SMART_FREE(qp->rq_res.wqe_id, qp->rq_res.max_outs * sizeof(VAPI_wr_id_t)); 
                 }
                 if (qp->wqe_buf_orig != NULL) {/* WQEs buffer were allocated in process mem. or by the THH_qpm ? */ 
                   MTL_DEBUG4(MT_FLFMT("Freeing WQEs buffer at 0x%p"),qp->wqe_buf_orig);
                   if (qp->dpool_p == NULL) { /* direct allocation */
                     if (qp->used_virt_alloc) 
                       MOSAL_pci_virt_free_consistent(qp->wqe_buf_orig, qp->wqe_buf_orig_size);
                     else
                       MOSAL_pci_phys_free_consistent(qp->wqe_buf_orig, qp->wqe_buf_orig_size);    
                   } else { /* used dpool */
                     dpool_free(qpm, qp->dpool_p, qp->wqe_buf_orig);
                   }
                 }
                 FREE(qp->sq_res.wqe_draft);
                 FREE(qp->rq_res.wqe_draft);
                 FREE(qp);
        }/* while (hash_tbl[i]..)*/
    }/* for (i.. QP_HASH_TBL_SZ)*/

    
    FREE(qpm);
    return HH_OK;
}


HH_ret_t THHUL_qpm_create_qp_prep( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_init_attr_t *qp_init_attr_p, 
   HHUL_qp_hndl_t *qp_hndl_p, 
   VAPI_qp_cap_t *qp_cap_out_p, 
   void/*THH_qp_ul_resources_t*/ *qp_ul_resources_p 
) 
{ 
  return qp_prep(hca,VAPI_REGULAR_QP,qp_init_attr_p,qp_hndl_p,qp_cap_out_p,
    (THH_qp_ul_resources_t*)qp_ul_resources_p,
    FALSE);  /* Default is allocation of WQEs buffer in host's mem. */
}

HH_ret_t THHUL_qpm_special_qp_prep( 
   HHUL_hca_hndl_t hca, 
   VAPI_special_qp_t qp_type, 
   IB_port_t port, 
   HHUL_qp_init_attr_t *qp_init_attr_p, 
   HHUL_qp_hndl_t *qp_hndl_p, 
   VAPI_qp_cap_t *qp_cap_out_p, 
   void/*THH_qp_ul_resources_t*/ *qp_ul_resources_p 
) 
{ 
  return qp_prep(hca,qp_type,qp_init_attr_p,qp_hndl_p,qp_cap_out_p,
    (THH_qp_ul_resources_t*)qp_ul_resources_p,
    FALSE);  /* For special QPs no performance issue - WQEs in main memory */
}


HH_ret_t THHUL_qpm_create_qp_done( 
  HHUL_hca_hndl_t hca, 
  HHUL_qp_hndl_t hhul_qp, 
  IB_wqpn_t hh_qp, 
  void/*THH_qp_ul_resources_t*/ *qp_ul_resources_p
) 
{ 
  THHUL_qpm_t qpm;
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  THH_qp_ul_resources_t *ul_res_p= (THH_qp_ul_resources_t*)qp_ul_resources_p;
  HH_ret_t rc;
  
  rc= THHUL_hob_get_qpm(hca,&qpm);
  if (rc != HH_OK) {
    MTL_ERROR4("THHUL_qpm_create_qp_done: Invalid HCA handle (%p).",hca);
    return HH_EINVAL;
  }
  if (qp == NULL) {
    MTL_ERROR4("THHUL_qpm_create_qp_done: NULL hhul_qp handle.");
    return HH_EINVAL;
  }
  
  if ((qp->wqe_buf_orig == NULL) && (qp->wqe_buf_orig_size != 0)) { 
    /* WQEs buffer allocated in DDR mem. by THH_qpm */
    if (ul_res_p->wqes_buf == 0) {
      MTL_ERROR1(MT_FLFMT("Got NULL WQEs buffer from qp_ul_res for new qpn=%d.\n"),qp->qpn);
      return HH_EINVAL;
    }
    /* Set the per queue resources */
    qp->rq_res.wqe_buf= MT_UP_ALIGNX_VIRT(ul_res_p->wqes_buf,qp->rq_res.log2_max_wqe_sz);
    if (qp->rq_res.wqe_buf != ul_res_p->wqes_buf) {
      MTL_ERROR1(
        "THHUL_qpm_create_qp_done: Buffer allocated by THH_qpm ("VIRT_ADDR_FMT") "
        "is not aligned to RQ WQE size (%d bytes).\n",
        ul_res_p->wqes_buf,1<<qp->rq_res.log2_max_wqe_sz);
      return HH_EINVAL;
    }
    /* SQ is after RQ - aligned to its WQE size */
    qp->sq_res.wqe_buf= MT_UP_ALIGNX_VIRT(qp->rq_res.wqe_buf + 
        (qp->rq_res.max_outs << qp->rq_res.log2_max_wqe_sz), /* End of RQ WQEs buffer */
      qp->sq_res.log2_max_wqe_sz); 
  }
  
  qp->qpn= hh_qp;
  /* Insert QP to the hash table with the given QP number */
  rc= insert_to_hash(qpm,qp);
  if (rc != HH_OK) {
    MTL_ERROR2("THHUL_qpm_create_qp_done: Failed inserting to hash table "
               "(QP will remain unusable) !"); 
    qp->qpn= 0xFFFFFFFF; /* Mark that QP initialization was not completed with invalid QP num. */
    return rc;
  }

  MTL_DEBUG4(MT_FLFMT("%s: qpn=0x%X rq_res{buf_p=%p, sz=0x%X} sq_res{buf_p=%p, sz=0x%X}"), __func__,
             qp->qpn, 
             (void*)qp->rq_res.wqe_buf, (1 << qp->rq_res.log2_max_wqe_sz) * qp->rq_res.max_outs,
             (void*)qp->sq_res.wqe_buf, (1 << qp->sq_res.log2_max_wqe_sz) * qp->sq_res.max_outs);

  return HH_OK;
}


HH_ret_t THHUL_qpm_destroy_qp_done( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_qp 
) 
{ 
  THHUL_qpm_t qpm;
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  HH_ret_t rc;
  
  MTL_DEBUG1("THHUL_qpm_destroy_qp_done(hca=%s,hhul_qp=%p) {\n",hca->dev_desc,qp);
  rc= THHUL_hob_get_qpm(hca,&qpm);
  if (rc != HH_OK) {
    MTL_ERROR4("THHUL_qpm_destroy_qp_done: Invalid HCA handle (%p).",hca);
    return HH_EINVAL;
  }
  MTL_DEBUG4(MT_FLFMT("Got qpm with %d QPs"),qpm->qp_cnt);
  
  if (IS_VALID_QPN(qp->qpn)) {	/* QP has completed THHUL_qpm_create_qp_done successfully */
    
    /* Clean all CQEs which refer to this QP */
    THHUL_cqm_cq_cleanup(qp->rq_cq, qp->qpn, qpm->srqm, qp->srq);
    if (qp->sq_cq != qp->rq_cq) /* additional cleaning required only if SQ's CQ is different */
    THHUL_cqm_cq_cleanup(qp->sq_cq, qp->qpn, qpm->srqm, HHUL_INVAL_SRQ_HNDL);
    
    /* Remove QP from hash table (after assured no more CQEs of this QP exist) */
    rc= remove_from_hash(qpm,qp);
    if (rc != HH_OK) {
      MTL_ERROR2("THHUL_qpm_destroy_qp_done: Failed removing qp from hash table "
                 "(assuming invalid QP handle) !"); 
      return HH_EINVAL_QP_NUM;
    }
    MTL_DEBUG4(MT_FLFMT("QP %d removed from hash table"),qp->qpn);
  
  }

  /* Free QP resources: Auxilary buffer + WQEs buffer */
  MTL_DEBUG4(MT_FLFMT("Freeing user level WQE-IDs auxilary buffers"));
  if (qp->sq_res.wqe_id != NULL) {
    THH_SMART_FREE(qp->sq_res.wqe_id, qp->sq_res.max_outs * sizeof(VAPI_wr_id_t)); 
  }
  if (qp->rq_res.wqe_id != NULL) {
    THH_SMART_FREE(qp->rq_res.wqe_id, qp->rq_res.max_outs * sizeof(VAPI_wr_id_t)); 
  }
  if (qp->wqe_buf_orig != NULL) {/* WQEs buffer were allocated in process mem. or by the THH_qpm ? */ 
    MTL_DEBUG4(MT_FLFMT("Freeing WQEs buffer at 0x%p"),qp->wqe_buf_orig);
    if (qp->dpool_p == NULL) { /* direct allocation */
      if (qp->used_virt_alloc) 
        MOSAL_pci_virt_free_consistent(qp->wqe_buf_orig, qp->wqe_buf_orig_size);
      else
        MOSAL_pci_phys_free_consistent(qp->wqe_buf_orig, qp->wqe_buf_orig_size);    
    } else { /* used dpool */
      dpool_free(qpm, qp->dpool_p, qp->wqe_buf_orig);
    }
  }
  if ( qp->sq_res.wqe_draft ) FREE(qp->sq_res.wqe_draft);
  if ( qp->rq_res.wqe_draft ) FREE(qp->rq_res.wqe_draft);
  FREE(qp);
  /* update QPs counter */
  MOSAL_spinlock_irq_lock(&(qpm->hash_lock));
  qpm->qp_cnt--;
  MOSAL_spinlock_unlock(&(qpm->hash_lock));
  MTL_DEBUG1("} /* THHUL_qpm_destroy_qp_done */ \n");
  return HH_OK;  
}

HH_ret_t THHUL_qpm_modify_qp_done( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_qp, 
   VAPI_qp_state_t cur_state 
) 
{ 
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  THHUL_qpm_t qpm;
  HH_ret_t rc;
  
  if (qp == NULL) {
    MTL_ERROR1("THHUL_qpm_modify_qp_done: NULL hhul_qp.\n");
    return HH_EINVAL;
  }

  rc= THHUL_hob_get_qpm(hca,&qpm);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed to get QPM handle (%d=%s)"), __func__, rc, HH_strerror_sym(rc));
    return rc;
  }

  /* Update in RQ */
  if (cur_state == VAPI_RESET) {  
    /* Cleanup all CQEs of RQ (flush) when moving to reset state */
    THHUL_cqm_cq_cleanup(qp->rq_cq, qp->qpn, qpm->srqm, qp->srq);
    MOSAL_spinlock_irq_lock(&(qp->rq_res.q_lock));
    qp->rq_res.cur_outs= 0;
    qp->rq_res.next2free_index= qp->rq_res.next2post_index= 0;
    qp->rq_res.last_posted_p= NULL;
    qp->rq_res.qp_state= VAPI_RESET;
    MOSAL_spinlock_unlock(&(qp->rq_res.q_lock));
  } else {
    qp->rq_res.qp_state= cur_state;
  }
  
  /* Update in SQ */
  if (cur_state == VAPI_RESET) {  
    /* Cleanup all CQEs of SQ (flush) when moving to reset state */
    if (qp->sq_cq != qp->rq_cq) /* additional cleaning required only if SQ's CQ is different */
      THHUL_cqm_cq_cleanup(qp->sq_cq, qp->qpn, qpm->srqm, HHUL_INVAL_SRQ_HNDL);
    MOSAL_spinlock_irq_lock(&(qp->sq_res.q_lock));
    qp->sq_res.cur_outs= 0;
    qp->sq_res.next2free_index= qp->sq_res.next2post_index= 0;
    qp->sq_res.last_posted_p= NULL;
    qp->sq_res.qp_state= VAPI_RESET;
    MOSAL_spinlock_unlock(&(qp->sq_res.q_lock));
  } else {
    qp->sq_res.qp_state= cur_state;
  }

  return HH_OK;
}



HH_ret_t THHUL_qpm_post_send_req( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_qp, 
   VAPI_sr_desc_t *send_req_p 
) 
{ 
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  u_int32_t* wqe_draft= qp->sq_res.wqe_draft;
  u_int32_t wqe_sz_dwords;
  THH_hca_ul_resources_t hca_ul_res;
  HH_ret_t rc;

  if (!is_qpstate_valid_2send(qp->sq_res.qp_state)) {
    MTL_ERROR1(MT_FLFMT("%s failed: qp state %d not valid to send \n"),__func__,qp->sq_res.qp_state);
    return HH_EINVAL_QP_STATE;
  }
  
  if (qp->sq_res.max_sg_sz < send_req_p->sg_lst_len) {
    MTL_ERROR2(
      "THHUL_qpm_post_send_req: Scatter/Gather list is too large (%d entries > max_sg_sz=%d)\n",
      send_req_p->sg_lst_len,qp->sq_res.max_sg_sz);
    return HH_EINVAL_SG_NUM;
  }
   
#ifdef DUMP_SEND_REQ 
  dump_send_req(qp,send_req_p);
#endif
  
  MOSAL_spinlock_irq_lock(&(qp->sq_res.q_lock)); /* protect wqe_draft and WQE allocation/link */
  
  if (qp->sqp_type == VAPI_REGULAR_QP)  {
    MTPERF_TIME_START(WQE_build_send);
    wqe_sz_dwords= (WQE_build_send(qp,send_req_p,wqe_draft) >> 2);
    MTPERF_TIME_END(WQE_build_send);
#ifdef MAX_DEBUG
    if ((wqe_sz_dwords<<2) > (1U << qp->sq_res.log2_max_wqe_sz)) {
      MTL_ERROR1(MT_FLFMT("QP 0x%X: Send WQE too large (%d > max=%d)"),
        qp->qpn,(wqe_sz_dwords<<2),(1U << qp->sq_res.log2_max_wqe_sz));
	}
#endif
  } else { /* special QP */
    if (send_req_p->opcode != VAPI_SEND)  {
      MOSAL_spinlock_unlock(&(qp->sq_res.q_lock)); 
      return HH_EINVAL_OPCODE;
    }
    send_req_p->fence= FALSE; /* required for MLX requests */
    rc= THHUL_hob_get_hca_ul_res(hca,&hca_ul_res);
    if (rc != HH_OK) {
      MOSAL_spinlock_unlock(&(qp->sq_res.q_lock)); 
      return HH_EINVAL_HCA_HNDL;
    }
    wqe_sz_dwords= 
      (WQE_build_send_mlx(hca_ul_res.hh_hca_hndl,qp,send_req_p,QP1_PKEY_INDEX,wqe_draft) >> 2);
    if (wqe_sz_dwords == 0) {
      MOSAL_spinlock_unlock(&(qp->sq_res.q_lock)); 
      MTL_ERROR1(MT_FLFMT("Failed building MLX headers for special QP.\n"));
      return HH_EINVAL_WQE;
    }
  }

  rc= sq_alloc_wqe_link_and_ring(qp,wqe_draft,wqe_sz_dwords,
#ifdef MT_LITTLE_ENDIAN
           wqe_sz_dwords,
#endif
           send_req_p,encode_nopcode(send_req_p->opcode)); 
  MOSAL_spinlock_unlock(&(qp->sq_res.q_lock)); 
  return rc;
}

HH_ret_t THHUL_qpm_post_inline_send_req( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_qp, 
   VAPI_sr_desc_t *send_req_p 
) 
{ 
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  u_int32_t* wqe_draft= qp->sq_res.wqe_draft;
  u_int8_t *cur_loc_p= (u_int8_t*)wqe_draft; /* Current location in the WQE */
  u_int8_t *wqe_edge_p= ((u_int8_t*)wqe_draft)+(1<<qp->sq_res.log2_max_wqe_sz);
  u_int32_t wqe_sz_dwords;
  u_int32_t* inline_p; /* inline control word */
  u_int32_t i;  
  HH_ret_t rc;

#ifdef DUMP_SEND_REQ 
  dump_send_req(qp,send_req_p);
#endif
  
  if (!is_qpstate_valid_2send(qp->sq_res.qp_state)) {
   MTL_ERROR1(MT_FLFMT("%s failed: qp state %d not valid to send \n"),__func__,qp->sq_res.qp_state);
   return HH_EINVAL_QP_STATE;
 }

  MOSAL_spinlock_irq_lock(&(qp->sq_res.q_lock)); /* protect wqe_draft and WQE allocation/link */
  
  cur_loc_p+= WQE_init_next((u_int32_t*)cur_loc_p); /* Make "unlinked" "next" segment */
  cur_loc_p+= WQE_pack_ctrl_send((u_int32_t*)cur_loc_p,  /* Pack Control segment */
    send_req_p->comp_type, send_req_p->set_se, 0/*event bit*/,
    ((send_req_p->opcode == VAPI_RDMA_WRITE_WITH_IMM) ||
     (send_req_p->opcode == VAPI_SEND_WITH_IMM) ) ? send_req_p->imm_data : 0);

  /* Transport type checks: Datagram segment */
  switch (qp->ts_type) {
    case VAPI_TS_UD:  /* Check if UD (UD datagram segment) */
      cur_loc_p+= WQE_pack_ud((u_int32_t*)cur_loc_p,
        qp->ud_av_memkey,send_req_p->remote_ah,
        send_req_p->remote_qp,send_req_p->remote_qkey);
      break;
    case VAPI_TS_RD:  /* Check if RD (RD datagram segment) */
      cur_loc_p+= WQE_pack_rd((u_int32_t*)cur_loc_p,
        send_req_p->remote_qp,send_req_p->remote_qkey);
      break;
    default:
      break;
  }
  
  /* Opcode checks + Remote-address/Atomic segments */
  switch (send_req_p->opcode) {
    /* For RDMA operations: only Remote-address segment */
    case VAPI_RDMA_WRITE:
    case VAPI_RDMA_WRITE_WITH_IMM:
      cur_loc_p+= WQE_pack_remote_addr((u_int32_t*)cur_loc_p,
        send_req_p->remote_addr,send_req_p->r_key);
      break;
    
    case VAPI_SEND:
    case VAPI_SEND_WITH_IMM:
      break; /* Valid opcodes for "inline" but no extra WQE segment */
    default: 
      MOSAL_spinlock_unlock(&(qp->sq_res.q_lock));
      return HH_EINVAL_OPCODE; /* Invalid opcode */
  }
  
  inline_p= (u_int32_t*)cur_loc_p;
  cur_loc_p+= WQE_INLINE_SZ_BCOUNT;
  /* copy inline data to WQE */
  for (i= 0; i < send_req_p->sg_lst_len; i++) {
    if ((cur_loc_p+send_req_p->sg_lst_p[i].len) > wqe_edge_p) {
      MOSAL_spinlock_unlock(&(qp->sq_res.q_lock));
      MTL_ERROR2(MT_FLFMT("too much inline data for inline send request (qpn=0x%X)"),qp->qpn);
      return HH_EINVAL_SG_NUM;
    }
    if (send_req_p->sg_lst_p[i].addr > (MT_virt_addr_t)MAKE_ULONGLONG(0xFFFFFFFFFFFFFFFF)) {
        MOSAL_spinlock_unlock(&(qp->sq_res.q_lock));
        MTL_ERROR2(MT_FLFMT("sg list addr %d has non-zero upper bits (qpn=0x%X, addr="U64_FMT") \n"),
               i,qp->qpn,send_req_p->sg_lst_p[i].addr );
       return HH_EINVAL_SG_FMT;
    }
    memcpy(cur_loc_p, (void*)(MT_virt_addr_t)(send_req_p->sg_lst_p[i].addr),
           send_req_p->sg_lst_p[i].len); 
    cur_loc_p+= send_req_p->sg_lst_p[i].len;
  }
  *inline_p= 
    (u_int32_t)(0x80000000 | (((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)inline_p) - 4)); /*inline:size*/

  wqe_sz_dwords= (MT_UP_ALIGNX_U32( (u_int32_t)(((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)wqe_draft)),
                                WQE_SZ_MULTIPLE_SHIFT) >> 2); 
#ifdef MAX_DEBUG
  if ((wqe_sz_dwords<<2) > (1U << qp->sq_res.log2_max_wqe_sz)) {
    MTL_ERROR1(MT_FLFMT("QP 0x%X: Send WQE too large (%d > max=%d) !!!!!!!!!"),
      qp->qpn,(wqe_sz_dwords<<2),(1U << qp->sq_res.log2_max_wqe_sz));
  	}
#endif

  rc= sq_alloc_wqe_link_and_ring(qp,wqe_draft,wqe_sz_dwords,
#ifdef MT_LITTLE_ENDIAN
           (u_int32_t)(inline_p - wqe_draft + 1), /* swap all up to data */
#endif
           send_req_p,encode_nopcode(send_req_p->opcode)); 
  MOSAL_spinlock_unlock(&(qp->sq_res.q_lock));
  return rc;
}

HH_ret_t THHUL_qpm_post_send_reqs( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp, 
  /*IN*/ u_int32_t num_of_requests,
  /*IN*/ VAPI_sr_desc_t *send_req_array 
)
{
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  u_int32_t* wqe_draft= qp->sq_res.wqe_draft;
  u_int32_t next_draft[WQE_SEG_SZ_NEXT>>2]; /* Build "next" segment here */
  volatile u_int32_t* next_wqe= NULL;
  volatile u_int32_t* prev_wqe_p= NULL; 
  MT_virt_addr_t first_wqe_nda= 0;
  u_int32_t first_wqe_nds= 0;
  u_int32_t wqe_sz_dwords,i;
  u_int32_t next2post_index,reqi;
  THH_uar_sendq_dbell_t sq_dbell;

  if (qp->sqp_type != VAPI_REGULAR_QP) {
    MTL_ERROR4(MT_FLFMT("THHUL_qpm_post_send_reqs is not supporeted for special QPs"));
    return HH_ENOSYS;
  }
  
  if (num_of_requests == 0) {
    MTL_ERROR4(MT_FLFMT("THHUL_qpm_post_send_reqs: num_of_requeusts=0 !"));
    return HH_EINVAL_PARAM;
  }
  
  if (!is_qpstate_valid_2send(qp->sq_res.qp_state)) {
   MTL_ERROR1(MT_FLFMT("%s failed: qp state %d not valid to send \n"),__func__,qp->sq_res.qp_state);
   return HH_EINVAL_QP_STATE;
 }

  MOSAL_spinlock_irq_lock(&(qp->sq_res.q_lock)); /* protect wqe_draft as well as WQE allocation/link */
  
  /* Check for available WQEs */
  if (qp->sq_res.max_outs < (qp->sq_res.cur_outs + num_of_requests)) {
    MTL_ERROR4("THHUL_qpm_post_send_reqs: Not enough WQEs for %u requests (%u requests outstanding).\n",
               num_of_requests,qp->sq_res.cur_outs);
    MOSAL_spinlock_unlock(&(qp->sq_res.q_lock));
    return HH_E2BIG_WR_NUM;
  }

  /* We hold this value on a seperate var. for easy rollback in case of an error */
  next2post_index= qp->sq_res.next2post_index;

  /* Build and link all WQEs */
  for (reqi= 0; reqi < num_of_requests; reqi++) {
    if (qp->sq_res.max_sg_sz < send_req_array[reqi].sg_lst_len) {
      MTL_ERROR2(
                "THHUL_qpm_post_send_req: S/G list of request %d is too large (%d entries > max_sg_sz=%d)\n",
                reqi,send_req_array[reqi].sg_lst_len,qp->sq_res.max_sg_sz);
      MOSAL_spinlock_unlock(&(qp->sq_res.q_lock));
      return HH_EINVAL_SG_NUM;
    }

    MTPERF_TIME_START(WQE_build_send);
    wqe_sz_dwords= (WQE_build_send(qp,send_req_array+reqi,wqe_draft) >> 2);
    MTPERF_TIME_END(WQE_build_send);
#ifdef MAX_DEBUG
    if ((wqe_sz_dwords<<2) > (1U << qp->sq_res.log2_max_wqe_sz)) {
      MTL_ERROR1(MT_FLFMT("QP 0x%X: Send WQE too large (%d > max=%d)"),
                 qp->qpn,(wqe_sz_dwords<<2),(1U << qp->sq_res.log2_max_wqe_sz));
    	}
#endif
    /* Allocate next WQE */
    next_wqe= (u_int32_t*)(qp->sq_res.wqe_buf + 
                          (next2post_index << qp->sq_res.log2_max_wqe_sz) );
    qp->sq_res.wqe_id[next2post_index]= send_req_array[reqi].id;  /* Save WQE ID */
    next2post_index = (next2post_index + 1) % qp->sq_res.max_outs ;
    /* copy (while swapping,if needed) the wqe_draft to the actual WQE */
    /* TBD: for big-endian machines we can optimize here and use memcpy */
    MTPERF_TIME_START(SQ_WQE_copy);
    for (i= 0; i < wqe_sz_dwords; i++) {
      next_wqe[i]= MOSAL_cpu_to_be32(wqe_draft[i]);
    }

    if (reqi == 0) { /* For the first WQE save info for linking it later */
      first_wqe_nda= (MT_virt_addr_t)next_wqe;
      first_wqe_nds= (wqe_sz_dwords>>2);
    
    } else { /* Not first - link to previous with DBD=0 */
      /* Build linking "next" segment in last posted WQE*/
      WQE_pack_send_next(next_draft, encode_nopcode(send_req_array[reqi].opcode), 
        send_req_array[reqi].fence,0/*DBD*/, (u_int32_t)(MT_ulong_ptr_t)next_wqe, wqe_sz_dwords>>2, 
        (qp->ts_type==VAPI_TS_RD) ? send_req_array[reqi].eecn : 0 );
      for (i= 0;i < (WQE_SEG_SZ_NEXT>>2) ;i++) {  
        /* This copy assures big-endian as well as that NDS is written last */
        prev_wqe_p[i]= MOSAL_cpu_to_be32(next_draft[i]);
      }
    }
    prev_wqe_p= next_wqe;

  }
  
  if (qp->sq_res.last_posted_p != NULL) { /* link chain to previous WQE */
    /* Build linking "next" segment with DBD set */
    WQE_pack_send_next(next_draft, encode_nopcode(send_req_array[0].opcode), 
      send_req_array[0].fence,1/*DBD*/, (u_int32_t)first_wqe_nda, first_wqe_nds, 
      (qp->ts_type==VAPI_TS_RD) ? send_req_array[0].eecn : 0 );
    for (i= 0;i < (WQE_SEG_SZ_NEXT>>2) ;i++) {  
      /* This copy assures big-endian as well as that NDS is written last */
      qp->sq_res.last_posted_p[i]= MOSAL_cpu_to_be32(next_draft[i]);
    }
  }
  
  /* Update QP status */
  qp->sq_res.last_posted_p= next_wqe; 
  qp->sq_res.next2post_index= next2post_index;
  qp->sq_res.cur_outs+= num_of_requests;

  /* Ring doorbell (send or rd-send) */
  sq_dbell.qpn= qp->qpn;
  sq_dbell.nopcode= encode_nopcode(send_req_array[0].opcode);
  sq_dbell.fence= send_req_array[0].fence;
  sq_dbell.next_addr_32lsb= (u_int32_t)(first_wqe_nda & 0xFFFFFFFF);
  sq_dbell.next_size= first_wqe_nds;
  if (qp->ts_type == VAPI_TS_RD) {
    THH_uar_sendq_rd_dbell(qp->uar,&sq_dbell,send_req_array[0].eecn);
  } else {  /* non-RD send request */
    MTPERF_TIME_START(THH_uar_sendq_dbell);
    THH_uar_sendq_dbell(qp->uar,&sq_dbell);
    MTPERF_TIME_END(THH_uar_sendq_dbell);
  }

  MOSAL_spinlock_unlock(&(qp->sq_res.q_lock)); 
  return HH_OK;
}

HH_ret_t THHUL_qpm_post_gsi_send_req( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_qp, 
   VAPI_sr_desc_t *send_req_p,
   VAPI_pkey_ix_t pkey_index
) 
{ 
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  u_int32_t* wqe_draft= qp->sq_res.wqe_draft;
  u_int32_t wqe_sz_dwords;
  THH_hca_ul_resources_t hca_ul_res;
  HH_ret_t rc;

  if (qp->sqp_type != VAPI_GSI_QP)  {
    MTL_ERROR2(MT_FLFMT("Invoked for non-GSI QP (qpn=0x%X)"),qp->qpn);
    return HH_EINVAL_QP_NUM;
  }
  
  if (qp->sq_res.max_sg_sz < send_req_p->sg_lst_len) {
    MTL_ERROR2(
      "%s: Scatter/Gather list is too large (%d entries > max_sg_sz=%d)\n",__func__,
      send_req_p->sg_lst_len,qp->sq_res.max_sg_sz);
    return HH_EINVAL_SG_NUM;
  }
   
  if (!is_qpstate_valid_2send(qp->sq_res.qp_state)) {
   MTL_ERROR1(MT_FLFMT("%s failed: qp state %d not valid to send \n"),__func__,qp->sq_res.qp_state);
   return HH_EINVAL_QP_STATE;
 }

#ifdef DUMP_SEND_REQ 
  dump_send_req(qp,send_req_p);
#endif
  
  send_req_p->fence= FALSE; /* required for MLX requests */
  rc= THHUL_hob_get_hca_ul_res(hca,&hca_ul_res);
  if (rc != HH_OK) {
    return HH_EINVAL_HCA_HNDL;
  }

  MOSAL_spinlock_irq_lock(&(qp->sq_res.q_lock)); /* protect wqe_draft and WQE allocation/link */
  
  wqe_sz_dwords= 
    (WQE_build_send_mlx(hca_ul_res.hh_hca_hndl,qp,send_req_p,pkey_index,wqe_draft) >> 2);
  if (wqe_sz_dwords == 0) {
    MOSAL_spinlock_unlock(&(qp->sq_res.q_lock)); 
    MTL_ERROR1(MT_FLFMT("Failed building MLX headers for special QP.\n"));
    return HH_EINVAL_WQE;
  }

  rc= sq_alloc_wqe_link_and_ring(qp,wqe_draft,wqe_sz_dwords,
#ifdef MT_LITTLE_ENDIAN
           wqe_sz_dwords,
#endif
           send_req_p,encode_nopcode(send_req_p->opcode)); 
  
  MOSAL_spinlock_unlock(&(qp->sq_res.q_lock)); 
  return rc;
}


HH_ret_t THHUL_qpm_post_recv_req( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_qp, 
   VAPI_rr_desc_t *recv_req_p 
) 
{ 
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  u_int32_t* wqe_draft= qp->rq_res.wqe_draft;
  u_int32_t next_draft[WQE_SEG_SZ_NEXT>>2]; /* Build "next" segment here */
  volatile u_int32_t* next_wqe; /* Actual WQE pointer */
  u_int32_t i, wqe_sz_dwords;
  THH_uar_recvq_dbell_t rq_dbell;

  if (qp->srq != HHUL_INVAL_SRQ_HNDL) {
    MTL_ERROR1(MT_FLFMT("%s: Used for QP 0x%X which is associated with SRQ handle 0x%p"), __func__,
               qp->qpn, qp->srq);
    return HH_EINVAL_SRQ_HNDL;
  }

  if (!is_qpstate_valid_2recv(qp->rq_res.qp_state)) {
   MTL_ERROR1(MT_FLFMT("%s failed: qp state %d not valid to recv \n"),__func__,qp->rq_res.qp_state);
   return HH_EINVAL_QP_STATE;
 }

  if (qp->rq_res.max_sg_sz < recv_req_p->sg_lst_len) {
    MTL_ERROR2(
      "THHUL_qpm_post_recv_req: Scatter/Gather list is too large (%d entries > max_sg_sz=%d)\n",
      recv_req_p->sg_lst_len,qp->rq_res.max_sg_sz);
    return HH_EINVAL_SG_NUM;
  }
   
  MOSAL_spinlock_irq_lock(&(qp->rq_res.q_lock)); /* protect wqe_draft as well as WQE allocation/link */
  
  /* Build WQE */
  wqe_sz_dwords= (WQE_build_recv(qp,recv_req_p,wqe_draft) >> 2);
#ifdef MAX_DEBUG
    if ((wqe_sz_dwords<<2) > (1U << qp->rq_res.log2_max_wqe_sz)) {
      MTL_ERROR1(MT_FLFMT("QP 0x%X: Receive WQE too large (%d > max=%d)"),
        qp->qpn,(wqe_sz_dwords<<2),(1U << qp->rq_res.log2_max_wqe_sz));
    	}
#endif

  /* Check if any WQEs are free to be consumed */
  if (qp->rq_res.max_outs == qp->rq_res.cur_outs) {
    MOSAL_spinlock_unlock(&(qp->rq_res.q_lock));
    MTL_ERROR4("THHUL_qpm_post_recv_req: Receive queue is full (%d requests outstanding).\n",
      qp->rq_res.cur_outs);
    return HH_E2BIG_WR_NUM;
  }
  /* Allocate next WQE */
  next_wqe= (u_int32_t*) (qp->rq_res.wqe_buf + 
                          (qp->rq_res.next2post_index << qp->rq_res.log2_max_wqe_sz) );
  qp->rq_res.wqe_id[qp->rq_res.next2post_index]= recv_req_p->id;  /* Save WQE ID */
  qp->rq_res.next2post_index = (qp->rq_res.next2post_index + 1) % qp->rq_res.max_outs ;
  qp->rq_res.cur_outs++;
  
  /* copy (while swapping,if needed) the wqe_draft to the actual WQE */
  /* TBD: for big-endian machines we can optimize here and use memcpy */
  for (i= 0; i < wqe_sz_dwords; i++) {
    next_wqe[i]= MOSAL_cpu_to_be32(wqe_draft[i]);
  }

  /* Update "next" segment of previous WQE (if any) */
  if (qp->rq_res.last_posted_p != NULL) {
    /* Build linking "next" segment in last posted WQE */
    WQE_pack_recv_next(next_draft, (u_int32_t)(MT_ulong_ptr_t) next_wqe, wqe_sz_dwords>>2);
    for (i= 0;i < (WQE_SEG_SZ_NEXT>>2) ;i++) {  
      /* This copy assures big-endian as well as that NDS is written last */
      qp->rq_res.last_posted_p[i]= MOSAL_cpu_to_be32(next_draft[i]);
    }
  }
  qp->rq_res.last_posted_p= next_wqe;
  
  /* Ring doorbell */
  rq_dbell.qpn= qp->qpn;
  rq_dbell.next_addr_32lsb= (u_int32_t)((MT_virt_addr_t)next_wqe & 0xFFFFFFFF);
  rq_dbell.next_size= wqe_sz_dwords>>2;
  rq_dbell.credits= 1;
  THH_uar_recvq_dbell(qp->uar,&rq_dbell);

  MOSAL_spinlock_unlock(&(qp->rq_res.q_lock));
  return HH_OK;
  
}


HH_ret_t THHUL_qpm_post_recv_reqs(
                                 /*IN*/ HHUL_hca_hndl_t hca, 
                                 /*IN*/ HHUL_qp_hndl_t hhul_qp, 
                                 /*IN*/ u_int32_t num_of_requests,
                                 /*IN*/ VAPI_rr_desc_t *recv_req_array 
                                 )
{
  THHUL_qp_t qp= (THHUL_qp_t)hhul_qp;
  u_int32_t* wqe_draft= qp->rq_res.wqe_draft;
  u_int32_t next_draft[WQE_SEG_SZ_NEXT>>2]; /* Build "next" segment here */
  volatile u_int32_t* next_wqe= NULL; /* Actual WQE pointer */
  volatile u_int32_t* prev_wqe_p= qp->rq_res.last_posted_p; 
  u_int32_t wqe_sz_dwords= 0;
  u_int32_t i,reqi,next2post_index;
  THH_uar_recvq_dbell_t rq_dbell;

  if (qp->srq != HHUL_INVAL_SRQ_HNDL) {
    MTL_ERROR1(MT_FLFMT("%s: Used for QP 0x%X which is associated with SRQ 0x%p"), __func__,
               qp->qpn, qp->srq);
    return HH_EINVAL_SRQ_HNDL;
  }

  if (!is_qpstate_valid_2recv(qp->rq_res.qp_state)) {
    MTL_ERROR1(MT_FLFMT("%s failed: qp state %d not valid to recv \n"),__func__,qp->rq_res.qp_state);
    return HH_EINVAL_QP_STATE;
  }

  if (num_of_requests == 0) {
    MTL_ERROR4(MT_FLFMT("THHUL_qpm_post_recv_reqs: num_of_requeusts=0 !"));
    return HH_EINVAL_PARAM;
  }

  /* Check parameters of all WQEs first - must assure all posted successfully */
  for (reqi= 0; reqi < num_of_requests; reqi++) {
    if (qp->rq_res.max_sg_sz < recv_req_array[reqi].sg_lst_len) {
      MTL_ERROR2(
        "THHUL_qpm_post_recv_reqs: S/G list of req. #%d is too large (%d entries > max_sg_sz=%d)\n",
                reqi,recv_req_array[reqi].sg_lst_len,qp->rq_res.max_sg_sz);
      return HH_EINVAL_SG_NUM;
    }
  }

  MOSAL_spinlock_irq_lock(&(qp->rq_res.q_lock)); /* protect wqe_draft as well as WQE allocation/link */
  
  /* Check for available WQEs */
  if (qp->rq_res.max_outs < (qp->rq_res.cur_outs + num_of_requests)) {
    MTL_ERROR4("THHUL_qpm_post_recv_reqs: Not enough WQEs for %d requests (%d requests outstanding).\n",
               num_of_requests,qp->rq_res.cur_outs);
    MOSAL_spinlock_unlock(&(qp->rq_res.q_lock));
    return HH_E2BIG_WR_NUM;
  }

  rq_dbell.qpn= qp->qpn; /* Fixed for all doorbells */
  rq_dbell.credits= 0; /* During the loop, doorbell is rung every 256 WQEs */
  
  /* We hold this value on a seperate var. for easy rollback in case of an error */
  next2post_index= qp->rq_res.next2post_index;

  /* Build and link and ring all WQEs */
  for (reqi= 0; reqi < num_of_requests; reqi++) {
    
    /* Build WQE */
    wqe_sz_dwords= (WQE_build_recv(qp,recv_req_array+reqi,wqe_draft) >> 2);
  #ifdef MAX_DEBUG
    if ((wqe_sz_dwords<<2) > (1U << qp->rq_res.log2_max_wqe_sz)) {
      MTL_ERROR1(MT_FLFMT("QP 0x%X: Receive WQE too large (%d > max=%d)"),
                 qp->qpn,(wqe_sz_dwords<<2),(1U << qp->rq_res.log2_max_wqe_sz));
    	}
  #endif
    
    /* Allocate next WQE */
    next_wqe= (u_int32_t*) (qp->rq_res.wqe_buf + 
                            (next2post_index << qp->rq_res.log2_max_wqe_sz) );
    qp->rq_res.wqe_id[next2post_index]= recv_req_array[reqi].id;  /* Save WQE ID */
    next2post_index = (next2post_index + 1) % qp->rq_res.max_outs ;

    /* copy (while swapping,if needed) the wqe_draft to the actual WQE */
    /* TBD: for big-endian machines we can optimize here and use memcpy */
    for (i= 0; i < wqe_sz_dwords; i++) {
      next_wqe[i]= MOSAL_cpu_to_be32(wqe_draft[i]);
    }
    
    if ((reqi & 0xFF) == 0) { 
      /* save NDA+NDS of first WQE in each 256 WQEs chain for the doorbell */
      rq_dbell.next_addr_32lsb= (u_int32_t)((MT_virt_addr_t)next_wqe & 0xFFFFFFFF);
      rq_dbell.next_size= wqe_sz_dwords>>2;
    }

    if (prev_wqe_p != NULL) { /* first in the chain may be the first since reset */
      /* Update "next" segment of previous WQE */
      /* Build linking "next" segment in last posted WQE */
      WQE_pack_recv_next(next_draft, (u_int32_t)(MT_ulong_ptr_t)next_wqe, wqe_sz_dwords>>2);
      for (i= 0;i < (WQE_SEG_SZ_NEXT>>2) ;i++) {
        /* This copy assures big-endian as well as that NDS is written last */
        prev_wqe_p[i]= MOSAL_cpu_to_be32(next_draft[i]);
      }
    }
    prev_wqe_p= next_wqe;

    if ((reqi & 0xFF) == 0xFF) { /* last in 256 WQEs chain - ring doorbell */
      /* Ring doorbell on the first WQE only */
      THH_uar_recvq_dbell(qp->uar,&rq_dbell);
    }
  }

  if ((reqi & 0xFF) != 0) { /* Doorbel for last WQEs was not rung */
    rq_dbell.credits= (reqi & 0xFF);
    THH_uar_recvq_dbell(qp->uar,&rq_dbell);
  }
  
  qp->rq_res.last_posted_p= next_wqe;

  /* update producer index + cur. outstanding  (now that no error was found) */
  qp->rq_res.next2post_index = next2post_index;
  qp->rq_res.cur_outs+= num_of_requests;
  
  MOSAL_spinlock_unlock(&(qp->rq_res.q_lock));
  return HH_OK;
}



HH_ret_t THHUL_qpm_post_bind_req(
  /*IN*/ HHUL_mw_bind_t *bind_props_p,
  /*IN*/ IB_rkey_t new_rkey
)
{
  THHUL_qp_t qp= (THHUL_qp_t)bind_props_p->qp;
  u_int32_t wqe_draft[BIND_WQE_SZ>>2];  /* Build the WQE here */
  u_int32_t wqe_sz_dwords;
  VAPI_sr_desc_t send_req;

  if ((qp->sqp_type != VAPI_REGULAR_QP) ||
      ((qp->ts_type != VAPI_TS_RC) && (qp->ts_type != VAPI_TS_RD) && (qp->ts_type != VAPI_TS_UC))){
    MTL_ERROR1(MT_FLFMT("Invalid QP type for binding memory windows (qpn=0x%X)."),qp->qpn);
    return HH_EINVAL_QP_NUM;
  }
  
  
  if (!is_qpstate_valid_2send(qp->sq_res.qp_state)) {
    MTL_ERROR1(MT_FLFMT("%s failed: qp state %d not valid to send \n"),__func__,qp->sq_res.qp_state);
    return HH_EINVAL_QP_STATE;
  }

  
  wqe_sz_dwords= (WQE_build_membind(bind_props_p,new_rkey,wqe_draft) >> 2);
#ifdef MAX_DEBUG
  if ((wqe_sz_dwords<<2) > (1U << qp->sq_res.log2_max_wqe_sz)) {
    MTL_ERROR1(MT_FLFMT("QP 0x%X: Send WQE too large (%d > max=%d)"),
      qp->qpn,(wqe_sz_dwords<<2),(1U << qp->sq_res.log2_max_wqe_sz));
  	}
#endif

  send_req.id= bind_props_p->id;
  send_req.fence= TRUE; /* just in case, though implicitly fenced */
  if (qp->ts_type == VAPI_TS_RD) {
    send_req.eecn= RESERVED_MEMBIND_EECN;
  }

  return sq_alloc_wqe_link_and_ring(qp,wqe_draft,wqe_sz_dwords,
#ifdef MT_LITTLE_ENDIAN
           wqe_sz_dwords,
#endif
           &send_req,TAVOR_IF_NOPCODE_BIND_MEMWIN);
}



HH_ret_t THHUL_qpm_comp_ok( 
  THHUL_qpm_t qpm, 
  IB_wqpn_t qpn,
  u_int32_t wqe_addr_32lsb, 
  VAPI_special_qp_t *qp_type_p,
  IB_ts_t *qp_ts_type_p,
  VAPI_wr_id_t *wqe_id_p,
  u_int32_t *wqes_released_p
#ifdef IVAPI_THH
   , u_int32_t *reserved_p
#endif 
) 
{ 
  u_int32_t freed_wqe_index;
  queue_res_t *associated_q= NULL;
  THHUL_qp_t  qp;
  HH_ret_t rc;

  rc= find_wqe(qpm,qpn,wqe_addr_32lsb,&qp,&associated_q,&freed_wqe_index,wqe_id_p);
  if (rc != HH_OK) {
    MTL_ERROR2("%s: Given QPN/WQE is not associated with any queue (qpn=0x%X,wqe=0x%X).\n", 
               __func__,qpn,wqe_addr_32lsb);
    return HH_EINVAL;
  }

  if ( (qp->ts_type == IB_TS_RD) && (qp->sqp_type == VAPI_REGULAR_QP) ) {
    /* RD is a completely different story due to out of order completion */
    MTL_ERROR4("THHUL_qpm_comp_ok: RD WQEs tracking not supported, yet.\n");
    return HH_ENOSYS; /* TBD: implement when THH should support RD */
  }
  
  *qp_type_p= qp->sqp_type;
  *qp_ts_type_p= qp->ts_type;

  if (associated_q != NULL) { /* Release WQEs (if not from SRQ) */
    MOSAL_spinlock_irq_lock(&(associated_q->q_lock));
    *wqes_released_p= 
      (associated_q->next2free_index <= freed_wqe_index) ? 
      /* Unsigned computation depends on cycic indecies relation (who is the upper index) */
        1+ freed_wqe_index - associated_q->next2free_index :
        1+ associated_q->max_outs - (associated_q->next2free_index - freed_wqe_index);
      /* The +1 results from the fact that next2free_index should be counted as well */
    associated_q->next2free_index= (freed_wqe_index + 1) % associated_q->max_outs;
    associated_q->cur_outs -= *wqes_released_p;
    MOSAL_spinlock_unlock(&(associated_q->q_lock));
  }

  return HH_OK;
}


HH_ret_t THHUL_qpm_comp_err( 
  THHUL_qpm_t qpm, 
  IB_wqpn_t qpn, 
  u_int32_t wqe_addr_32lsb, 
  VAPI_wr_id_t *wqe_id_p,
  u_int32_t *wqes_released_p, 
  u_int32_t *next_wqe_32lsb_p,
  u_int8_t  *dbd_bit_p
) 
{ 
  u_int32_t freed_wqe_index;
  queue_res_t *associated_q;
  THHUL_qp_t  qp;
  u_int32_t* completed_wqe;
  HH_ret_t rc;

  rc= find_wqe(qpm,qpn,wqe_addr_32lsb,&qp,&associated_q,&freed_wqe_index,wqe_id_p);
  if (rc != HH_OK) {
    MTL_ERROR2(
      "%s: Given QPN/WQE is not associated with any queue (qpn=0x%X,wqe=0x%X).\n",__func__,
      qpn,wqe_addr_32lsb);
    return HH_EINVAL;
  }

  if ( (qp->ts_type == IB_TS_RD) && (qp->sqp_type == VAPI_REGULAR_QP) ) {
    /* RD is a completely different story due to out of order completion */
    MTL_ERROR4("%s: RD WQEs tracking not supported, yet.\n", __func__);
    return HH_ENOSYS; /* TBD: implement when THH should support RD */
  }

  if (associated_q != NULL) { /* Not from SRQ */
    MOSAL_spinlock_irq_lock(&(associated_q->q_lock));
    *wqes_released_p= 
      (associated_q->next2free_index <= freed_wqe_index) ? 
      /* Unsigned computation depends on cycic indecies relation (who is the upper index) */
        1+ freed_wqe_index - associated_q->next2free_index :
        1+ associated_q->max_outs - (associated_q->next2free_index - freed_wqe_index);
      /* The +1 results from the fact that next2free_index should be counted as well */
    associated_q->next2free_index= (freed_wqe_index + 1) % associated_q->max_outs;
    associated_q->cur_outs -= *wqes_released_p;
    if (sizeof(MT_virt_addr_t) <= 4) { /* Optimization for 32bit machines */
      completed_wqe= (u_int32_t*)(MT_virt_addr_t) wqe_addr_32lsb;
    } else {
      completed_wqe= (u_int32_t*)(MT_virt_addr_t)
        (((associated_q->wqe_buf) & MAKE_ULONGLONG(0xFFFFFFFF00000000)) | (u_int64_t)wqe_addr_32lsb );
    }
    if (WQE_extract_nds(completed_wqe) == 0) {
      *next_wqe_32lsb_p= THHUL_QPM_END_OF_WQE_CHAIN;  /* Chain end reached */
    } else {
      *next_wqe_32lsb_p= WQE_extract_nda(completed_wqe);
    }
    *dbd_bit_p= WQE_extract_dbd(completed_wqe);
    MOSAL_spinlock_unlock(&(associated_q->q_lock));
  
  } else { /* SRQ - all WQEs generate CQEs... no need to provide NDA */
    *wqes_released_p= 1;
    *next_wqe_32lsb_p= THHUL_QPM_END_OF_WQE_CHAIN;  /* Chain end reached */
  }

  return HH_OK;
}

u_int32_t THHUL_qpm_wqe_cnt( 
  /*IN*/THHUL_qpm_t qpm, 
  /*IN*/IB_wqpn_t qpn, 
  /*IN*/u_int32_t wqe_addr_32lsb, 
  /*IN*/u_int16_t dbd_cnt)
{
  u_int32_t cur_wqe_index;
  queue_res_t *associated_q;
  THHUL_qp_t  qp;
  volatile u_int32_t *cur_wqe_p;
  u_int32_t wqe_cntr= 0;
  VAPI_wr_id_t wqe_id;
  HH_ret_t rc;

  rc= find_wqe(qpm,qpn,wqe_addr_32lsb,&qp,&associated_q,&cur_wqe_index,&wqe_id);
  if (rc != HH_OK) {
    MTL_ERROR2(
      "%s: Given QPN/WQE is not associated with any queue (qpn=%d,wqe=0x%X).\n",__func__,
      qpn,wqe_addr_32lsb);
    return 0;
  }

  if ( (qp->ts_type == IB_TS_RD) && (qp->sqp_type == VAPI_REGULAR_QP) ) {
    /* RD is a completely different story due to out of order completion */
    MTL_ERROR4("%s: RD WQEs tracking not supported, yet.\n",__func__);
    return 0; /* TBD: implement when THH should support RD */
  }

  if (associated_q == NULL) { /* SRQ */
    /* Only one WQE per CQE for SRQs */
    return 1;
  }

  dbd_cnt++;  /* count down to zero (dbd_cnt==0 when waiting for next dbd bit set) */
  MOSAL_spinlock_irq_lock(&(associated_q->q_lock));
  do {
    wqe_cntr++;
    cur_wqe_p= (u_int32_t*)(associated_q->wqe_buf + 
                          (cur_wqe_index << associated_q->log2_max_wqe_sz) );
    dbd_cnt-= WQE_extract_dbd(cur_wqe_p);
    cur_wqe_index= (cur_wqe_index + 1) % associated_q->max_outs;
  } while ((dbd_cnt > 0) && (WQE_extract_nds(cur_wqe_p) != 0));
  MOSAL_spinlock_unlock(&(associated_q->q_lock));

  return wqe_cntr;
}

/**********************************************************************************************
 *                    Private Functions
 **********************************************************************************************/

/* 
 * Prepare QP resources before creation.
 * To be used by both THH_qpm_create_qp_prep and THH_qpm_special_qp_prep
 */
static HH_ret_t qp_prep(
  HHUL_hca_hndl_t hca, 
  VAPI_special_qp_t qp_type, 
  HHUL_qp_init_attr_t *qp_init_attr_p, 
  HHUL_qp_hndl_t *qp_hndl_p, 
  VAPI_qp_cap_t *qp_cap_out_p, 
  THH_qp_ul_resources_t *qp_ul_resources_p,
  MT_bool in_ddr_mem  /* WQEs buffer allocated in attached DDR mem. or in main memory */
)
{
  THHUL_qpm_t qpm;
  THH_hca_ul_resources_t hca_ul_res;
  THHUL_qp_t new_qp;
  HH_ret_t rc;
  THHUL_pdm_t pdm;
  MT_bool pd_ok_for_sqp;
  VAPI_lkey_t ud_av_memkey; /*irrelevant here */
  
  rc= THHUL_hob_get_qpm(hca,&qpm);
  if (rc != HH_OK) {
    MTL_ERROR4(MT_FLFMT("qp_prep: Invalid HCA handle (%p)."),hca);
    return HH_EINVAL;
  }
  rc= THHUL_hob_get_hca_ul_res(hca,&hca_ul_res);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("qp_prep: Failed THHUL_hob_get_hca_ul_res (err=%d).\n"),rc);
    return rc;
  }
  
  rc= THHUL_hob_get_pdm(hca,&pdm);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("qp_prep: Failed THHUL_hob_get_pdm (err=%d).\n"),rc);
    return rc;
  }
  
  rc= THHUL_pdm_get_ud_av_memkey_sqp_ok(pdm,qp_init_attr_p->pd,&pd_ok_for_sqp,&ud_av_memkey);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("qp_prep: Failed THHUL_pdm_get_ud_av_memkey_sqp_ok (err=%d).\n"),rc);
    return rc;
  }
  
  if (qp_type != VAPI_REGULAR_QP && pd_ok_for_sqp == FALSE) {
      /* the protection domain uses DDR memory for UDAV's -- not good for sqps */
      MTL_ERROR2(MT_FLFMT("***WARNING***: AVs for special QPs should use HOST memory; the provided PD has its AVs in DDR memory.\n"));
      //return HH_EINVAL;
  }

  (new_qp)= (THHUL_qp_t)MALLOC(sizeof(struct THHUL_qp_st));
  if (new_qp == NULL) {
    MTL_ERROR1(MT_FLFMT("qp_prep: Failed allocating THHUL_qp_t.\n"));
    return HH_EAGAIN;
  }
  memset(new_qp,0,sizeof(struct THHUL_qp_st));

  new_qp->sqp_type= qp_type;
  
  rc= init_qp(hca,qp_init_attr_p,new_qp);
  if (rc != HH_OK) {
    goto failed_init_qp;
  }

  rc= alloc_wqe_buf(qpm, in_ddr_mem,hca_ul_res.max_qp_ous_wr,hca_ul_res.max_num_sg_ent,
                    new_qp,qp_ul_resources_p);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT(": Failed allocating WQEs buffers.\n"));
    goto failed_alloc_wqe;
  }

  rc= alloc_aux_data_buf(new_qp);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT(": Failed allocating auxilary buffers.\n"));
    goto failed_alloc_aux;
  }
  
  /* Set output modifiers */
  *qp_hndl_p= new_qp;
  qp_cap_out_p->max_oust_wr_rq= new_qp->rq_res.max_outs;
  qp_cap_out_p->max_oust_wr_sq= new_qp->sq_res.max_outs;
  qp_cap_out_p->max_sg_size_rq= new_qp->rq_res.max_sg_sz;
  qp_cap_out_p->max_sg_size_sq= new_qp->sq_res.max_sg_sz;
  qp_cap_out_p->max_inline_data_sq= new_qp->sq_res.max_inline_data; 
  rc= THH_uar_get_index(new_qp->uar,&(qp_ul_resources_p->uar_index)); 
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT(": Failed getting UAR index.\n"));
    goto failed_uar_index;
  }
  /* wqe_buf data in qp_ul_resources_p is already set in alloc_wqe_buf */
  
  /* update QPs counter */
  MOSAL_spinlock_irq_lock(&(qpm->hash_lock));
  qpm->qp_cnt++;
  MOSAL_spinlock_unlock(&(qpm->hash_lock));

  return HH_OK;

  /* Error cleanup */
  failed_uar_index:
    if (new_qp->sq_res.wqe_id != NULL) {
      THH_SMART_FREE(new_qp->sq_res.wqe_id, new_qp->sq_res.max_outs * sizeof(VAPI_wr_id_t)); 
    }
    if (new_qp->rq_res.wqe_id != NULL) {
      THH_SMART_FREE(new_qp->rq_res.wqe_id, new_qp->rq_res.max_outs * sizeof(VAPI_wr_id_t)); 
    }
  failed_alloc_aux:
    if (new_qp->wqe_buf_orig != NULL) {/* WQEs buffer were allocated in process mem. or by the THH_qpm ? */ 
      /* If allocated here than should be freed */
      if (new_qp->dpool_p == NULL) { /* direct allocation */
        if (new_qp->used_virt_alloc) 
          MOSAL_pci_virt_free_consistent(new_qp->wqe_buf_orig, new_qp->wqe_buf_orig_size);
        else
          MOSAL_pci_phys_free_consistent(new_qp->wqe_buf_orig, new_qp->wqe_buf_orig_size);    
      } else { /* used dpool */
        dpool_free(qpm, new_qp->dpool_p, new_qp->wqe_buf_orig);
      }
    }
  failed_alloc_wqe:
  failed_init_qp:
    FREE(new_qp);
  return rc;
}


/* Allocate THHUL_qp_t object and initialize it */
static HH_ret_t init_qp(
  HHUL_hca_hndl_t hca, 
  HHUL_qp_init_attr_t *qp_init_attr_p, 
  THHUL_qp_t new_qp
)
{
  HH_ret_t rc;
  THHUL_pdm_t pdm;
  MT_bool ok_sqp; /* irrelevant here */

  rc= THHUL_hob_get_uar(hca,&(new_qp->uar));
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("init_qp: Failed getting THHUL_hob's UAR (err=%d).\n"),rc);
    return rc;
  }
  rc= THHUL_hob_get_pdm(hca,&pdm);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("init_qp: Failed THHUL_hob_get_pdm (err=%d).\n"),rc);
    return rc;
  }
  rc= THHUL_hob_is_priv_ud_av(hca,&(new_qp->is_priv_ud_av));
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("init_qp: Failed  THHUL_hob_is_priv_ud_av (err=%d).\n"),rc);
    return rc;
  }
  rc= THHUL_pdm_get_ud_av_memkey_sqp_ok(pdm,qp_init_attr_p->pd,&ok_sqp,&(new_qp->ud_av_memkey));
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("init_qp: Failed THHUL_pdm_get_ud_av_memkey (err=%d).\n"),rc);
    return rc;
  }
  
  new_qp->dpool_p= NULL;
  new_qp->qpn= 0xFFFFFFFF;  /* Init to invalid QP num. until create_qp_done is invoked */
  new_qp->pd= qp_init_attr_p->pd;
  switch (new_qp->sqp_type) {  /* Set transport type appropriate to QP type */
    case VAPI_REGULAR_QP:
      new_qp->ts_type= qp_init_attr_p->ts_type;
      break;
    
    case VAPI_SMI_QP:
    case VAPI_GSI_QP:
      new_qp->ts_type= VAPI_TS_UD;
      break;
    
    case VAPI_RAW_IPV6_QP:
    case VAPI_RAW_ETY_QP:
      new_qp->ts_type= VAPI_TS_RAW;
      break;

    default:
      MTL_ERROR1(MT_FLFMT("Invalid QP type (sqp_type=%d)"),new_qp->sqp_type);
      return HH_EINVAL;
  }
  
  new_qp->srq= qp_init_attr_p->srq;
  /* Init RQ */
  new_qp->rq_res.qp_state= VAPI_RESET;
  if (qp_init_attr_p->srq == HHUL_INVAL_SRQ_HNDL) {
    new_qp->rq_res.max_outs= qp_init_attr_p->qp_cap.max_oust_wr_rq;
    new_qp->rq_res.max_sg_sz= qp_init_attr_p->qp_cap.max_sg_size_rq;
  } else { /* QP associated with SRQ */
    MTL_DEBUG4(MT_FLFMT("%s: Ignoring RQ attributes for a SRQ associated QP"), __func__);
    new_qp->rq_res.max_outs= 0;
    new_qp->rq_res.max_sg_sz= 0;
  }
  new_qp->rq_res.next2free_index= new_qp->rq_res.next2post_index= 0;
  new_qp->rq_res.cur_outs= 0;
  new_qp->rq_res.last_posted_p= NULL;
  new_qp->rq_cq= qp_init_attr_p->rq_cq;
  MOSAL_spinlock_init(&(new_qp->rq_res.q_lock));

  /* Init SQ */
  new_qp->sq_res.qp_state= VAPI_RESET;
  new_qp->sq_res.max_outs= qp_init_attr_p->qp_cap.max_oust_wr_sq;
  new_qp->sq_res.max_sg_sz= qp_init_attr_p->qp_cap.max_sg_size_sq;
  new_qp->sq_res.max_inline_data= qp_init_attr_p->qp_cap.max_inline_data_sq;
  new_qp->sq_res.cur_outs= 0;
  new_qp->sq_res.next2free_index= new_qp->sq_res.next2post_index= 0;
  new_qp->sq_res.last_posted_p= NULL;
  new_qp->sq_cq= qp_init_attr_p->sq_cq;
  MOSAL_spinlock_init(&(new_qp->sq_res.q_lock));

  return HH_OK;
}

inline static MT_bool within_4GB(void* base, MT_size_t bsize)
{
  u_int64_t start_addr;
  u_int64_t end_addr;

  if (sizeof(MT_virt_addr_t) <=4)  return TRUE;  /* For 32 bits machines no check is required */
  start_addr= (u_int64_t)(MT_virt_addr_t)base;
  end_addr= start_addr+bsize-1;
  return ((start_addr >> 32) == (end_addr >> 32));  /* TRUE if 32 MS-bits equal */

}

inline static void* malloc_within_4GB(MT_size_t bsize, MT_bool *used_virt_alloc_p)
{
  void* buf[MAX_ALLOC_RETRY]={NULL};
  MT_bool used_virt_alloc[MAX_ALLOC_RETRY];
  int i,j;

  for (i= 0; i < MAX_ALLOC_RETRY; i++) {  /* Retry to avoid crossing 4GB */
#if defined(MT_KERNEL) && defined(__LINUX__)
    /* Consider using low memory (kmalloc) up to WQ_KMALLOC_LIMIT or for small vmalloc area */
    if (bsize <= WQ_KMALLOC_LIMIT) {
      buf[i]= (void*)MOSAL_pci_phys_alloc_consistent(bsize,0); /* try to use kmalloc */
      used_virt_alloc[i]= FALSE;
    } 
    if (buf[i] == NULL)  /* failed kmalloc, or did not even try it */
#endif
    {
      buf[i]= (void*)MOSAL_pci_virt_alloc_consistent(bsize, 0); //TODO: must pass proper alignment here. For now thhul_qpm is unused in Darwin.
      used_virt_alloc[i]= TRUE;
    }
    if (buf[i] == NULL) {
      MTL_ERROR3("malloc_within_4GB: Failed allocating buffer of "SIZE_T_FMT" bytes (iteration %d).\n",
        bsize,i);
    /* Free previously allocated buffers if any*/
      for (j= i; j > 0; j--) {
        if (used_virt_alloc[j-1]) {
          MOSAL_pci_virt_free_consistent(buf[j-1], bsize);
        } else {
          MOSAL_pci_phys_free_consistent(buf[j-1], bsize);
        }
      }
      return NULL;
    }
    if (within_4GB(buf[i],bsize)) break;
  }
  if (i == MAX_ALLOC_RETRY) { /* Failed */
    MTL_ERROR2("malloc_within_4GB: Failed allocating buffer of "SIZE_T_FMT" bytes within 4GB boundry "
      "(%d retries).\n", bsize, MAX_ALLOC_RETRY); 
    /* Free all allocated buffers */
    for (i= 0; i < MAX_ALLOC_RETRY; i++) {
      if (used_virt_alloc[i]) {
        MOSAL_pci_virt_free_consistent(buf[i], bsize);
      } else {
        MOSAL_pci_phys_free_consistent(buf[i], bsize);
      }
    }
    return NULL;
  }
  /* Free disqualified buffers if any */
  for (j= i; j > 0; j--) {
    if (used_virt_alloc[j-1]) {
      MOSAL_pci_virt_free_consistent(buf[j-1], bsize);
    } else {
      MOSAL_pci_phys_free_consistent(buf[j-1], bsize);
    }
  }

  *used_virt_alloc_p= used_virt_alloc[i];
  return  buf[i]; /* This is the one buffer which does not cross 4GB boundry */
}

/* Allocate the WQEs buffer for sendQ and recvQ */
/* This function should be invoked after queue properties are set by alloc_init_qp */
static HH_ret_t alloc_wqe_buf(
  /*IN*/ THHUL_qpm_t qpm,
  /*IN*/ MT_bool in_ddr_mem,   /* Allocation of WQEs buffer is requested in attached DDR mem. */
  /*IN*/ u_int32_t max_outs_wqes, /* HCA cap. */
  /*IN*/ u_int32_t max_sg_ent, /* HCA cap. of max.s/g entries */
  /*IN/OUT*/ THHUL_qp_t new_qp,
  /*OUT*/    THH_qp_ul_resources_t *qp_ul_resources_p
)
{
  u_int32_t wqe_sz_rq,buf_sz_rq,rq_wqe_base_sz;
  u_int32_t wqe_sz_sq,buf_sz_sq,sq_wqe_base_sz;
  u_int32_t sq_sg_seg_sz,sq_inline_seg_sz;
  u_int8_t log2_wqe_sz_rq,log2_wqe_sz_sq;
  HH_ret_t ret;

  /* Check requested capabilities */
  if ((new_qp->rq_res.max_outs == 0) && (new_qp->sq_res.max_outs == 0)) {
    if (new_qp->srq == HHUL_INVAL_SRQ_HNDL) {
      MTL_ERROR3(MT_FLFMT("Got a request for a QP with 0 WQEs on both SQ and RQ - rejecting !"));
      return HH_EINVAL_PARAM;
    } else { /* QP has no WQEs buffer - uses SRQ only */
      new_qp->rq_res.wqe_draft = NULL;
      new_qp->sq_res.wqe_draft = NULL;
      new_qp->wqe_buf_orig= NULL;
      new_qp->wqe_buf_orig_size= 0;
      qp_ul_resources_p->wqes_buf= 0;   
      qp_ul_resources_p->wqes_buf_sz= 0; /* No WQEs buffer to register */
      return HH_OK;
    }
  }
  if ((new_qp->rq_res.max_outs > max_outs_wqes) || (new_qp->sq_res.max_outs > max_outs_wqes)) {
    MTL_ERROR2(MT_FLFMT(
      "QP cap. requested (rq_res.max_outs=%u, sq_res.max_outs=%u) exceeds HCA cap. (max_qp_ous_wr=%u)"),
      new_qp->rq_res.max_outs, new_qp->sq_res.max_outs, max_outs_wqes);
    return HH_E2BIG_WR_NUM;
  }
  /* Avoid a work queue of a single WQE (linking a WQE to itself may be problematic) */
  if (new_qp->rq_res.max_outs == 1)  new_qp->rq_res.max_outs= 2;
  if (new_qp->sq_res.max_outs == 1)  new_qp->sq_res.max_outs= 2;
  
  if ((new_qp->rq_res.max_sg_sz > max_sg_ent) || (new_qp->sq_res.max_sg_sz > max_sg_ent)) {
    MTL_ERROR2(MT_FLFMT(
      "QP cap. requested (rq_res.max_sg_sz=%u, sq_res.max_sg_sz=%u) exceeds HCA cap. (max_sg_ent=%u)"),
      new_qp->rq_res.max_sg_sz, new_qp->sq_res.max_sg_sz, max_sg_ent);
    return HH_E2BIG_SG_NUM;
  }

  /* Compute RQ WQE requirements */
  if (new_qp->rq_res.max_outs == 0) {
    log2_wqe_sz_rq= 0;
    wqe_sz_rq= 0;
    buf_sz_rq= 0;
    new_qp->rq_res.wqe_draft = NULL;
  } else {
    rq_wqe_base_sz= WQE_SEG_SZ_NEXT + WQE_SEG_SZ_CTRL; 
    wqe_sz_rq= rq_wqe_base_sz + (new_qp->rq_res.max_sg_sz * WQE_SEG_SZ_SG_ENTRY);
    if (wqe_sz_rq > MAX_WQE_SZ) {
      MTL_ERROR2(
        MT_FLFMT("required RQ capabilities (max_sg_sz=%d) require a too large WQE (%d bytes)"),
          new_qp->rq_res.max_sg_sz, wqe_sz_rq);
      return HH_E2BIG_SG_NUM;
    }
    log2_wqe_sz_rq= ceil_log2(wqe_sz_rq);  /* Align to next power of 2 */
    /* A WQE must be aligned to 64B (WQE_ALIGN_SHIFT) so we take at least this size */
    if (log2_wqe_sz_rq < WQE_ALIGN_SHIFT)  log2_wqe_sz_rq= WQE_ALIGN_SHIFT;
    wqe_sz_rq= (1<<log2_wqe_sz_rq);
    MTL_DEBUG5("alloc_wqe_buf: Allocating RQ WQE of size %d.\n",wqe_sz_rq);
    /* Compute real number of s/g entries based on rounded up WQE size */
    new_qp->rq_res.max_sg_sz= (wqe_sz_rq - rq_wqe_base_sz) / WQE_SEG_SZ_SG_ENTRY;  
    /* Make sure we do not exceed reported HCA cap. */
    new_qp->rq_res.max_sg_sz= (new_qp->rq_res.max_sg_sz > max_sg_ent) ? 
      max_sg_ent : new_qp->rq_res.max_sg_sz;
    new_qp->rq_res.wqe_draft= (u_int32_t *)MALLOC(wqe_sz_rq);
    if (new_qp->rq_res.wqe_draft == NULL) {
      MTL_ERROR2(MT_FLFMT("Failed allocating %d bytes for RQ's wqe draft"),wqe_sz_rq);
      return HH_EAGAIN;
    }
  }
  
  if (new_qp->sq_res.max_outs == 0) {
    sq_wqe_base_sz= 0;
    log2_wqe_sz_sq= 0;
    wqe_sz_sq= 0;
    buf_sz_sq= 0;
    new_qp->sq_res.wqe_draft = NULL;
  } else {
    /* Compute SQ WQE requirements */
    wqe_sz_sq= /* "next" and "ctrl" are included in the WQE of any transport */
      WQE_SEG_SZ_NEXT + WQE_SEG_SZ_CTRL;

    switch (new_qp->sqp_type) {
      /* For special QPs additional reservation required for the headers (MLX+inline) */
      case VAPI_SMI_QP:
      case VAPI_GSI_QP:
        /* SMI/GSI ==> UD headers */
        wqe_sz_sq+= WQE_INLINE_SZ_UD_HDR;
        wqe_sz_sq+= WQE_INLINE_ICRC;
        break;
      case VAPI_RAW_ETY_QP:
        /* Raw-Ethertype ==> LRH+RWH */
        wqe_sz_sq+= WQE_INLINE_SZ_RAW_HDR;
        break;
      case VAPI_RAW_IPV6_QP:
        /* IPv6 routing headers are given by the consumer in the gather list (?) */
        break;
      default:  /* Normal QP - add relevant transport WQE segments */
        if (new_qp->ts_type == VAPI_TS_UD) {
          wqe_sz_sq+= WQE_SEG_SZ_UD;
        } else if (new_qp->ts_type == VAPI_TS_RD) {
          wqe_sz_sq+= WQE_SEG_SZ_RD;
        }
        if ((new_qp->ts_type == VAPI_TS_RC) ||
            (new_qp->ts_type == VAPI_TS_RD) ||
            (new_qp->ts_type == VAPI_TS_UC)   ) {
          wqe_sz_sq+= WQE_SEG_SZ_BIND_RADDR_ATOMIC;
        }
    }

    if (wqe_sz_sq > MAX_WQE_SZ) {
      MTL_ERROR2(MT_FLFMT("required SQ capabilities(max_sg_sz=%d , max_inline_data=%d) "
                          "require a too large WQE (%d bytes)"),
        new_qp->sq_res.max_sg_sz, new_qp->sq_res.max_inline_data, wqe_sz_sq);
      ret= HH_E2BIG_SG_NUM;
      goto failed_sq2big;
    }

    sq_wqe_base_sz= wqe_sz_sq; /* WQE base without data segments */
    /* Compute data segments size for sendQ */
    sq_sg_seg_sz= new_qp->sq_res.max_sg_sz * WQE_SEG_SZ_SG_ENTRY; /* data pointers segments */
  #ifndef QPM_SUPPORT_INLINE_DATA_SET
    /* max_inline_data from create-qp cap. is not supported by default due to backward compat. */
    new_qp->sq_res.max_inline_data= 64; /* Current default minimum */
  #endif
    sq_inline_seg_sz=  /* Compute inline data segment size */ 
      MT_UP_ALIGNX_U32(WQE_INLINE_SZ_BCOUNT + new_qp->sq_res.max_inline_data,WQE_SZ_MULTIPLE_SHIFT);
    wqe_sz_sq+= ((sq_inline_seg_sz > sq_sg_seg_sz) ? sq_inline_seg_sz : sq_sg_seg_sz); 

    log2_wqe_sz_sq= ceil_log2(wqe_sz_sq);  /* Align to next power of 2 */
    /* A WQE must be aligned to 64B (WQE_ALIGN_SHIFT) so we take at least this size */
    if (log2_wqe_sz_sq < WQE_ALIGN_SHIFT)  log2_wqe_sz_sq= WQE_ALIGN_SHIFT;
    wqe_sz_sq= (1<<log2_wqe_sz_sq);
    MTL_DEBUG5("alloc_wqe_buf: Allocating SQ WQE of size %d.\n",wqe_sz_sq);
    /* Compute real number of s/g entries based on rounded up WQE size */
    new_qp->sq_res.max_sg_sz= (wqe_sz_sq - sq_wqe_base_sz) / WQE_SEG_SZ_SG_ENTRY;  
    /* Make sure we do not exceed reported HCA cap. */
    new_qp->sq_res.max_sg_sz= (new_qp->sq_res.max_sg_sz > max_sg_ent) ? 
      max_sg_ent : new_qp->sq_res.max_sg_sz;
    new_qp->sq_res.wqe_draft= (u_int32_t *)MALLOC(wqe_sz_sq);
    if (new_qp->sq_res.wqe_draft == NULL) {
      MTL_ERROR2(MT_FLFMT("Failed allocating %d bytes for SQ's wqe draft"),wqe_sz_sq);
      ret= HH_EAGAIN;
      goto failed_sq_draft;
    }
  }


  buf_sz_rq= new_qp->rq_res.max_outs * wqe_sz_rq;
  buf_sz_sq= new_qp->sq_res.max_outs * wqe_sz_sq;
  
  
  if ((in_ddr_mem) ||  /* Allocate WQEs buffer by THH_qpm in the attached DDR memory */
      (buf_sz_rq+buf_sz_sq == 0)) {/* Or no WQE allocation (possible if SRQ is used) */
    new_qp->wqe_buf_orig= NULL;
  } else { /* Allocate WQEs buffer in main memory */
#ifdef __KERNEL__
    new_qp->wqe_buf_orig_size = 
      buf_sz_rq+((wqe_sz_rq != 0) ? (wqe_sz_rq-1):0)+
      buf_sz_sq+((wqe_sz_sq != 0) ? (wqe_sz_sq-1):0);
    new_qp->wqe_buf_orig= malloc_within_4GB(new_qp->wqe_buf_orig_size, &new_qp->used_virt_alloc);
    /* Make RQ (first WQEs buffer) start at page boundry) */
    new_qp->rq_res.wqe_buf= MT_UP_ALIGNX_VIRT((MT_virt_addr_t)(new_qp->wqe_buf_orig),
                                              log2_wqe_sz_rq);
#else 
/* In user space we need to take care of pages sharing on memory locks (fork issues) */
    /* Allocate one more for each queue in order to make each aligned to its WQE size */
    /* (Assures no WQE crosses a page boundry, since we make WQE size a power of 2)   */ 
    new_qp->wqe_buf_orig_size = buf_sz_rq+buf_sz_sq+((wqe_sz_sq != 0) ? (wqe_sz_sq-1):0);
    if (new_qp->wqe_buf_orig_size > (THHUL_DPOOL_SZ_MAX_KB << THHUL_DPOOL_SZ_UNIT_SHIFT)) {
      /* Large WQEs buffer - allocate directly */
      /* Assure the buffer covers whole pages (no sharing of locked memory with other data) */
      new_qp->wqe_buf_orig_size = (MOSAL_SYS_PAGE_SIZE-1) + /* For alignment */
          MT_UP_ALIGNX_U32(new_qp->wqe_buf_orig_size, MOSAL_SYS_PAGE_SHIFT);
      /* Prevent other data reside in the last page of the buffer... */
      /* cover last page (last WQE can be at last page begin and its size is 64B min.)*/
      new_qp->wqe_buf_orig= malloc_within_4GB(new_qp->wqe_buf_orig_size, &new_qp->used_virt_alloc);
      /* Make RQ (first WQEs buffer) start at page boundry) */
      new_qp->rq_res.wqe_buf= MT_UP_ALIGNX_VIRT((MT_virt_addr_t)(new_qp->wqe_buf_orig),
                                                MOSAL_SYS_PAGE_SHIFT);
    } else { /* small WQEs buffer - use dpool */
      /* Round size up to next KB */
      new_qp->wqe_buf_orig_size= 
        MT_UP_ALIGNX_U32(new_qp->wqe_buf_orig_size, THHUL_DPOOL_GRANULARITY_SHIFT);
      new_qp->wqe_buf_orig= dpool_alloc(qpm, new_qp->wqe_buf_orig_size >> THHUL_DPOOL_SZ_UNIT_SHIFT,
                                        &new_qp->dpool_p);
      new_qp->rq_res.wqe_buf= (MT_virt_addr_t)new_qp->wqe_buf_orig; /* no need to align to WQE size */
      /* All dpool buffers are aligned to at least 1KB - see comment in dpool_create() */
    }
 #endif

    if (new_qp->wqe_buf_orig == NULL) {
      MTL_ERROR2("alloc_wqe_buf: Failed allocation of WQEs buffer of "SIZE_T_FMT" bytes within "
        "4GB boundries.\n",new_qp->wqe_buf_orig_size);
      ret= HH_EAGAIN;
      goto failed_wqe_buf;
    }
  }

  /* Set the per queue resources */
  new_qp->rq_res.log2_max_wqe_sz= log2_wqe_sz_rq;
  /* SQ is after RQ - aligned to its WQE size */
  new_qp->sq_res.wqe_buf= MT_UP_ALIGNX_VIRT((new_qp->rq_res.wqe_buf + buf_sz_rq),log2_wqe_sz_sq); 
  new_qp->sq_res.log2_max_wqe_sz= log2_wqe_sz_sq;
  //MTL_DEBUG5(MT_FLFMT("sq_inline_seg_sz=%d  sq_sg_seg_sz=%d"),sq_inline_seg_sz,sq_sg_seg_sz);
  if (wqe_sz_sq <= MAX_WQE_SZ) { /* update actual space for inline data */
    new_qp->sq_res.max_inline_data= wqe_sz_sq - sq_wqe_base_sz - 4; 
  } else { /* Due to alignment we have a WQE of 1024B, but actual WQE is only MAX_WQE_SZ (1008B)*/
    new_qp->sq_res.max_inline_data= MAX_WQE_SZ - sq_wqe_base_sz - 4; 
  }
  
  /* Set the qp_ul_resources_p */
  if (in_ddr_mem) {
    qp_ul_resources_p->wqes_buf= 0;   /* Allocate in attached DDR memory */
  } else {
    /* Actual buffer starts at beginning of the RQ WQEs buffer (if exists) */
    qp_ul_resources_p->wqes_buf= (buf_sz_rq != 0) ? new_qp->rq_res.wqe_buf : new_qp->sq_res.wqe_buf;
  }
  /* Actual buffer size is the difference from the real buffer start to end of SQ buffer */
  /* (even if buffer is allocated in DDR mem. and this computation is done from 0 it is valid) */
  qp_ul_resources_p->wqes_buf_sz= (new_qp->sq_res.wqe_buf + buf_sz_sq) - 
    qp_ul_resources_p->wqes_buf;

  return HH_OK;

  failed_wqe_buf:
    if ( new_qp->sq_res.wqe_draft ) FREE(new_qp->sq_res.wqe_draft);
  failed_sq2big:
  failed_sq_draft:
    if ( new_qp->rq_res.wqe_draft ) FREE(new_qp->rq_res.wqe_draft);
    return ret;
}


/* Allocate the auxilary WQEs data 
 * (a software context of a WQE which does not have to be in the registered WQEs buffer) */
static HH_ret_t alloc_aux_data_buf(
  /*IN/OUT*/ THHUL_qp_t new_qp
)
{
  /* RQ auxilary buffer: WQE ID per WQE */ 
  if (new_qp->rq_res.max_outs > 0) {
    new_qp->rq_res.wqe_id= (VAPI_wr_id_t*)
      THH_SMART_MALLOC(new_qp->rq_res.max_outs * sizeof(VAPI_wr_id_t)); 
    if (new_qp->rq_res.wqe_id == NULL) {
      MTL_ERROR1("alloc_aux_data_buf: Failed allocating RQ auxilary buffer.\n");
      return HH_EAGAIN;
    }
  }

  /* SQ auxilary buffer: WQE ID per WQE */ 
  if (new_qp->sq_res.max_outs > 0) {
    new_qp->sq_res.wqe_id= (VAPI_wr_id_t*)
      THH_SMART_MALLOC(new_qp->sq_res.max_outs * sizeof(VAPI_wr_id_t)); 
    if (new_qp->sq_res.wqe_id == NULL) {
      MTL_ERROR1("alloc_aux_data_buf: Failed allocating RQ auxilary buffer.\n");
      /* Free any memory chunk allocated by this function */
      if (new_qp->rq_res.wqe_id != NULL) {
        THH_SMART_FREE(new_qp->rq_res.wqe_id,new_qp->rq_res.max_outs * sizeof(VAPI_wr_id_t)); 
      }
      return HH_EAGAIN;
    }
  }
  
  return HH_OK;
}



/* Insert given QP to the QPM's hash table */
/* This function assumes this QP is not in hash table */
static HH_ret_t insert_to_hash(THHUL_qpm_t qpm, THHUL_qp_t qp)
{
  u_int32_t hash_index= get_hash_index(qp->qpn);
  qp_hash_entry_t* new_entry_p;
  
  /* Allocate hash table entry for the new QP */
  new_entry_p= (qp_hash_entry_t*)MALLOC(sizeof(qp_hash_entry_t));
  if (new_entry_p == NULL) {
    MTL_ERROR2("insert_to_hash: Failed allocating hash table entry.\n");
    return HH_EAGAIN;
  }
  /* Set entry key (QPN) and value (QP pointer) */
  new_entry_p->qpn= qp->qpn;
  new_entry_p->qp= qp;

  /* Add to the hash bin */
  MOSAL_spinlock_irq_lock(&(qpm->hash_lock));
  if (qpm->hash_tbl[hash_index] == NULL) {  /* First entry in the bin */
    new_entry_p->next= NULL;
    qpm->hash_tbl[hash_index]= new_entry_p;
  } else {         /* Add as first before existing entries in the bin */
    new_entry_p->next= qpm->hash_tbl[hash_index];
    qpm->hash_tbl[hash_index]= new_entry_p;
  }
  MOSAL_spinlock_unlock(&(qpm->hash_lock));
  
  return HH_OK;
}

/* Remove given QP from the QPM's hash table */
static HH_ret_t remove_from_hash(THHUL_qpm_t qpm, THHUL_qp_t qp)
{
  u_int32_t hash_index= get_hash_index(qp->qpn);
  qp_hash_entry_t *entry2remove_p;
  qp_hash_entry_t *prev_p= NULL;
  
  MOSAL_spinlock_irq_lock(&(qpm->hash_lock));

  /* Scan hash bin to find given QP's entry */
  for (entry2remove_p= qpm->hash_tbl[hash_index]; entry2remove_p != NULL;
       entry2remove_p= entry2remove_p->next) {
    if (entry2remove_p->qp == qp) break;
    prev_p= entry2remove_p;
  }
  if (entry2remove_p == NULL) {
    MTL_ERROR4("THHUL_qpm::remove_from_hash: qpn=%d not found in the hash table.\n",
      qp->qpn);
    MOSAL_spinlock_unlock(&(qpm->hash_lock));
    return HH_EINVAL;
  }
  /* Remove entry */
  /* prev==NULL ==> next should be put directly in hash array */
  if (prev_p == NULL) {
    qpm->hash_tbl[hash_index]= entry2remove_p->next;
  } else { /* else, attach next to prev */
    prev_p->next= entry2remove_p->next;
  }

  MOSAL_spinlock_unlock(&(qpm->hash_lock));

  FREE(entry2remove_p); 
  return HH_OK;
}


#ifndef __KERNEL__
/********************************
 * Descriptors pool functions  - not used in kernel space
 ********************************/

#ifdef THHUL_QPM_DEBUG_DPOOL

static void dpool_dump_list(THHUL_qpm_t qpm, MT_size_t size_index, 
                            const char *context_text, THHUL_qpm_dpool_t *dpool_context)
{
  THHUL_qpm_dpool_t *cur_dpool_p;
  unsigned long cntr= 0;
  
  MTL_ERROR1(MT_FLFMT("[%s - dpool_p=%p] Found inconsistancy in dpool list for buffers of %u KB:"),
             context_text, dpool_context, size_index + THHUL_DPOOL_SZ_MIN_KB);
  cur_dpool_p= qpm->dpool_p[size_index];
  while ((cur_dpool_p != NULL) && (cur_dpool_p->next != qpm->dpool_p[size_index]) &&
         (cntr < qpm->dpool_cnt)) {
    MTL_ERROR1("(%p <- %p -> %p) ", cur_dpool_p->prev, cur_dpool_p, cur_dpool_p->next);
    cntr++;
    cur_dpool_p= cur_dpool_p->next;
  }
  MTL_ERROR1("(End of list)\n");
  getchar();
}

static MT_bool dpool_check_consistancy(THHUL_qpm_t qpm, 
                                       const char *context_text, THHUL_qpm_dpool_t *dpool_context)
{
  THHUL_qpm_dpool_t *cur_dpool_p;
  MT_size_t size_index;
  unsigned long cntr= 0;

  for (size_index= 0; 
       size_index < (THHUL_DPOOL_SZ_MAX_KB - THHUL_DPOOL_SZ_MIN_KB + 1);
       size_index++) {
    cur_dpool_p= qpm->dpool_p[size_index];
    while ((cur_dpool_p != NULL) && (cur_dpool_p->next != qpm->dpool_p[size_index])) {
      if ((cur_dpool_p->next == NULL) ||  
          (cur_dpool_p->prev == NULL) ||
          (cur_dpool_p->next->prev != cur_dpool_p) ||
          (cur_dpool_p->prev->next != cur_dpool_p))  {
        dpool_dump_list(qpm, size_index, context_text, dpool_context);
        return FALSE;
      }
      cntr++;
      if (cntr > qpm->dpool_cnt) {
        MTL_ERROR1(MT_FLFMT("Reading more dpool objects in list than total (%lu)"), 
                   qpm->dpool_cnt);
        dpool_dump_list(qpm, size_index, context_text, dpool_context);
        return FALSE;
      }
      cur_dpool_p= cur_dpool_p->next;
    }
  }
  return TRUE;
}

#endif /*DEBUG_DPOOL*/


static THHUL_qpm_dpool_t * dpool_create(THHUL_qpm_t qpm, u_int8_t buf_size_kb)
{
  THHUL_qpm_dpool_t *new_dpool_p;
  MT_virt_addr_t orig_buf_limit;
  MT_virt_addr_t cur_buf;
  const MT_size_t size_index= buf_size_kb - THHUL_DPOOL_SZ_MIN_KB;
  const MT_size_t buf_size= (buf_size_kb << THHUL_DPOOL_SZ_UNIT_SHIFT);

  new_dpool_p= TMALLOC(THHUL_qpm_dpool_t);
  if (new_dpool_p == NULL) {
    MTL_ERROR2(MT_FLFMT("%s: Failed allocating THHUL_qpm_dpool_t"), __func__);
    return NULL;
  }
  
  /* Allocate descriptors pool memory - aligned on page start */
  new_dpool_p->orig_size= (THHUL_DPOOL_SZ_BASE_BUF_KB << THHUL_DPOOL_SZ_UNIT_SHIFT) + 
                          (MOSAL_SYS_PAGE_SIZE - 1);
  new_dpool_p->orig_buf= malloc_within_4GB(new_dpool_p->orig_size,
                                           &new_dpool_p->used_virt_alloc) ;
  if (new_dpool_p->orig_buf == NULL) {
    MTL_ERROR2(MT_FLFMT("%s: Failed allocating descriptors pool memory of "SIZE_T_FMT" B"),
               __func__, new_dpool_p->orig_size);
    goto failed_orig_buf;
  }
  orig_buf_limit= (MT_virt_addr_t) new_dpool_p->orig_buf + new_dpool_p->orig_size;

  new_dpool_p->free_buf_list= NULL;
  /* First buffer starts at page boundry and all buffers are of 1KB size multiples */
  /* So all buffers of the dpool are aligned to 1KB, i.e., aligned to any size of our */
  /* WQEs which are all (stride) of power of 2 */
  for (cur_buf= MT_UP_ALIGNX_VIRT((MT_virt_addr_t)new_dpool_p->orig_buf,MOSAL_SYS_PAGE_SHIFT);
       (cur_buf+buf_size) < orig_buf_limit ; 
       cur_buf+= buf_size ) {
    *(void**)cur_buf= new_dpool_p->free_buf_list; /* link before first */
    new_dpool_p->free_buf_list= (void*)cur_buf;
  }

  new_dpool_p->buf_size_kb= buf_size_kb;
  new_dpool_p->ref_cnt= 0;

  if (qpm->dpool_p[size_index] == NULL) { /* first */
    new_dpool_p->next=  new_dpool_p->prev= new_dpool_p;
  } else {
    new_dpool_p->next= qpm->dpool_p[size_index]; /* link to first */
    new_dpool_p->prev= new_dpool_p->next->prev;  /* reverse-link to last */
    new_dpool_p->prev->next= new_dpool_p->next->prev= new_dpool_p;
  }
  qpm->dpool_p[size_index]= new_dpool_p; /* make first */
  
#ifdef THHUL_QPM_DEBUG_DPOOL
  qpm->dpool_cnt++; 
  MTL_ERROR1("%s: dpool_cnt=%lu  (%p <- %p -> %p) \n", __func__,
             qpm->dpool_cnt, new_dpool_p->prev, new_dpool_p, new_dpool_p->next);
  dpool_check_consistancy(qpm, "After inserting new dpool", new_dpool_p);
#endif
  return new_dpool_p;

  failed_orig_buf:
    FREE(new_dpool_p);
    return NULL;
}

static void dpool_destroy(THHUL_qpm_t qpm, THHUL_qpm_dpool_t *dpool_p)
{
  const MT_size_t size_index= dpool_p->buf_size_kb - THHUL_DPOOL_SZ_MIN_KB;
  
  /* Assumes ref_cnt==0 */
  /* bypass this item */
  dpool_p->prev->next= dpool_p->next;
  dpool_p->next->prev= dpool_p->prev;
  if (qpm->dpool_p[size_index] == dpool_p) { /* if it was the first */
    if (dpool_p->next == dpool_p) { /* and only... */
      qpm->dpool_p[size_index]= NULL;
    } else {                        /* else, make next be first */
      qpm->dpool_p[size_index]= dpool_p->next;
    }
  }
  
#ifdef THHUL_QPM_DEBUG_DPOOL
  qpm->dpool_cnt--; 
  MTL_ERROR1(MT_FLFMT("%s: dpool_cnt=%lu  (%p <- %p -> %p) "), __func__,
             qpm->dpool_cnt, dpool_p->prev, dpool_p, dpool_p->next);
  dpool_check_consistancy(qpm, "After removing a dpool", dpool_p);
#endif

  if (dpool_p->used_virt_alloc) 
    MOSAL_pci_virt_free_consistent(dpool_p->orig_buf, dpool_p->orig_size);
  else
    MOSAL_pci_phys_free_consistent(dpool_p->orig_buf, dpool_p->orig_size);    

  FREE(dpool_p);
}

static void* dpool_alloc(THHUL_qpm_t qpm, u_int8_t buf_size_kb, THHUL_qpm_dpool_t **dpool_pp)
{
  THHUL_qpm_dpool_t *dpool_p;
  void* alloc_buf;
  const MT_size_t size_index= buf_size_kb - THHUL_DPOOL_SZ_MIN_KB;

  if ((buf_size_kb < THHUL_DPOOL_SZ_MIN_KB) || (buf_size_kb > THHUL_DPOOL_SZ_MAX_KB)) {
    MTL_ERROR2(MT_FLFMT("%s: Given buf_size_kb=0x%u "
                        "(THHUL_DPOOL_SZ_MIN_KB=%u , THHUL_DPOOL_SZ_MAX_KB=%u)"), __func__,
               buf_size_kb, THHUL_DPOOL_SZ_MIN_KB, THHUL_DPOOL_SZ_MAX_KB);
    return NULL;
  }

  MOSAL_mutex_acq_ui(&qpm->dpool_lock);
  
  dpool_p= qpm->dpool_p[size_index];
  /* If no dpool for this size or existing dpool is full (empty free list) */
  if ((dpool_p == NULL) || (dpool_p->free_buf_list == NULL)) {
    dpool_p= dpool_create(qpm, buf_size_kb);
    if (dpool_p == NULL)  return NULL;
  }

  alloc_buf= dpool_p->free_buf_list;
  dpool_p->free_buf_list= *(void**)alloc_buf; /* next is embedded in free buffer */
  dpool_p->ref_cnt++;

  if ((dpool_p->free_buf_list == NULL) && (dpool_p->prev != dpool_p)) { 
    /* If emptied and not the only dpool for this size - move to end of dpool list for this size */
    qpm->dpool_p[size_index]= dpool_p->next; /* "shift" first */
  }

#ifdef THHUL_QPM_DEBUG_DPOOL
  dpool_check_consistancy(qpm, "After moving dpool to end of list", dpool_p);
#endif

  MOSAL_mutex_rel(&qpm->dpool_lock);

  *dpool_pp= dpool_p;
  return alloc_buf;
}

static void dpool_free(THHUL_qpm_t qpm, THHUL_qpm_dpool_t *dpool_p, void* buf)
{
  const MT_size_t size_index= dpool_p->buf_size_kb - THHUL_DPOOL_SZ_MIN_KB;
  /* no check on this - assumes dpool is trusted (value checked on creation) */
 
  MOSAL_mutex_acq_ui(&qpm->dpool_lock);
  /* put in free list of associated dpool */
  *(void**)buf= dpool_p->free_buf_list;
  dpool_p->free_buf_list= buf;
  dpool_p->ref_cnt--;
  if (dpool_p->ref_cnt == 0) {
    /* if reached ref_cnt 0, probably not much of this size - compact dpools list */
    dpool_destroy(qpm,dpool_p);

  } else if (qpm->dpool_p[size_index] != dpool_p)  {
    /* if not the first dpool for this size */
    /* Move to begining of dpool list for this size - it has what to offer... */
    if (dpool_p->next != dpool_p->prev) {
      /* more than 2 items - really need to move */
      /* first disconnect */
      dpool_p->prev->next= dpool_p->next;
      dpool_p->next->prev= dpool_p->prev;
      /* Now connect between first and last */
      dpool_p->next= qpm->dpool_p[size_index]; /* link to first */
      dpool_p->prev= dpool_p->next->prev;       /* reverse-link to last */
      dpool_p->prev->next= dpool_p->next->prev= dpool_p;
    }
    /* (after moved to new location) make first */
    qpm->dpool_p[size_index]= dpool_p; 
  }
  
#ifdef THHUL_QPM_DEBUG_DPOOL
  dpool_check_consistancy(qpm, "After moving dpool to start of list", dpool_p);
#endif
  
  MOSAL_mutex_rel(&qpm->dpool_lock);
}
#endif

#ifdef DUMP_SEND_REQ
static void dump_send_req(THHUL_qp_t qp, HHUL_send_req_t *sr)
{
  int i;

  MTL_DEBUG4(MT_FLFMT("QP 0x%X - Send: %d S/G entries"),qp->qpn,sr->sg_lst_len); /* Build WQE */
  for (i= 0; i < sr->sg_lst_len; i++) {
    MTL_DEBUG4(MT_FLFMT("Entry %d: lkey=0x%X va=0x%X len=%d"),i,
      sr->sg_lst_p[i].lkey,(MT_virt_addr_t)sr->sg_lst_p[i].addr,sr->sg_lst_p[i].len);
  }
}
#endif
