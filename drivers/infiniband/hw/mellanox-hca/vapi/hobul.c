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

#include "hobul.h"
#include <cqm.h>
#include <qpm.h>
#include <vip.h>
#include <hhul.h>
#include <vipkl.h>
#include <vip_array.h>
#include <vapi_common.h>
#include <vip_hashp.h>
#include <vip_hashp2p.h>
#include <vipkl_eq.h>

#include <mtperf.h>
MTPERF_NEW_SEGMENT(HHUL_post_send_req,10000);
MTPERF_NEW_SEGMENT(HHUL_post_inline_send_req,10000);
MTPERF_NEW_SEGMENT(HHUL_poll4cqe,10000);

#define HOBUL_INC_REF_CNT(hobul_p) \
  MOSAL_spinlock_lock(&hobul_p->ref_lock);  \
  MTL_DEBUG5(MT_FLFMT("%s: HOBUL_INC_REF_CNT: "SIZE_T_DFMT" -> "SIZE_T_DFMT),__func__,  \
    hobul_p->ref_cnt,hobul_p->ref_cnt+1);                           \
  hobul_p->ref_cnt++;                       \
  MOSAL_spinlock_unlock(&hobul_p->ref_lock)

#define HOBUL_DEC_REF_CNT(hobul_p) \
  MOSAL_spinlock_lock(&hobul_p->ref_lock);  \
  MTL_DEBUG5(MT_FLFMT("%s: HOBUL_DEC_REF_CNT: "SIZE_T_DFMT" -> "SIZE_T_DFMT),__func__,  \
    hobul_p->ref_cnt,hobul_p->ref_cnt-1);                           \
  hobul_p->ref_cnt--;                       \
  MOSAL_spinlock_unlock(&hobul_p->ref_lock)


typedef struct pd_info_st {        /* struct of PD related resources */
  PDM_pd_hndl_t     vipkl_pd_hndl; /* For VIPKL access */
  HHUL_pd_hndl_t    hhul_pd_hndl;  /* For HHUL access */
} pd_info_t;


typedef struct cq_info_st {         /* struct of CQ related resources */
  CQM_cq_hndl_t     vipkl_cq_hndl;  /* For VIPKL access */
  HH_cq_hndl_t      cq_num;         /* For debug only */
  HHUL_cq_hndl_t    hhul_cq_hndl;   /* For HHUL access */
  VIPKL_cqblk_hndl_t   cq_block_hndl;  /* This handle is used to block until CQ event is received */
  /* (see comment on EVAPI_poll_cq_block() ) */
  VAPI_cqe_num_t    num_o_cqes;     /* For get_cq_props */
  void *            priv_context;   /* For user's use */
} cq_info_t;

typedef struct srq_info_st {        /* struct of SRQ related resources */
  SRQM_srq_hndl_t   vipkl_srq_hndl; /* For VIPKL access */
  HH_srq_hndl_t     hh_srq;         /* For debug only */
  HHUL_srq_hndl_t   hhul_srq_hndl;  /* For HHUL access */
  /* Info. for queries */
  VAPI_pd_hndl_t     pd_hndl;     /* SRQ's PD. (Ignored on VAPI_modify_srq). */
  u_int32_t          max_outs_wr; /* Max. outstanding WQEs */
  u_int32_t          max_sentries;/* Max. scatter entries  (Ignored on VAPI_modify_srq) */
} srq_info_t;

typedef struct qp_info_st {
  QPM_qp_hndl_t     vipkl_qp_hndl;  /* For VIPKL access */
  VAPI_qp_type_t    qp_type;
  HHUL_qp_hndl_t    hhul_qp_hndl;   /* For HHUL access */
  VAPI_qp_num_t     qp_num;         /* For debug only */
  VAPI_qp_cap_t     qp_cap;         /* For query_qp  */
  MOSAL_mutex_t     modify_qp_mutex; /* Assure only one modification is done at a time */
  void *            priv_context;   /* For user's use */
  VAPI_ts_type_t    ts;
  VAPI_srq_hndl_t   associated_srq; 
} qp_info_t;

typedef struct mw_info_st {
  IB_rkey_t       initial_rkey;     /* For VIPKL access */
  HHUL_mw_hndl_t  hhul_mw_hndl;       /* For HHUL access */
  VAPI_pd_hndl_t  pd;               /* For query response */
} mw_info_t;


typedef  MT_bool op_ts_matrix[VAPI_NUM_TS_TYPES][VAPI_NUM_OPCODES];

static op_ts_matrix snd_matrix;

/* IB-spec 1-1 , table 81 */
/*  VAPI_RDMA_WRITE, VAPI_RDMA_WRITE_WITH_IMM,VAPI_SEND,VAPI_SEND_WITH_IMM,VAPI_RDMA_READ,
    VAPI_ATOMIC_CMP_AND_SWP,VAPI_ATOMIC_FETCH_AND_ADD */
  
#ifndef MT_KERNEL
typedef enum {EQ_POLLT_INIT_REQ,EQ_POLLT_INIT_DONE,EQ_POLLT_EXIT_DONE} eq_pollt_state_t;

/* polling thread context */
typedef struct eq_pollt_st {
  struct HOBUL_st *hobul;
  MOSAL_thread_t    mosal_thread; 
  volatile eq_pollt_state_t  state;
  VIPKL_EQ_hndl_t   vipkl_eq;
  VIPKL_EQ_cbk_type_t cbk_type;
} eq_pollt_t;
#endif

typedef struct HOBUL_st {
  VIP_hca_hndl_t    vipkl_hndl;
  HH_hca_hndl_t     hh_hndl; 
  HHUL_hca_hndl_t   hhul_hndl;              /* user level HCA resources handle for HH */
  MT_size_t         ref_cnt;                /* Resources reference count */
  MOSAL_spinlock_t  ref_lock;               /* protect ref_cnt */
  u_int32_t         vendor_id;              /* \                                              */
  u_int32_t         device_id;              /*  >  3 items needed for initializing user level */
  void              *hca_ul_resources_p;    /* /                                              */
  MT_size_t         hca_ul_resources_sz;    /* Needed for alloc./rel. user resources for HCAs */
  MT_size_t         cq_ul_resources_sz;     /* Needed for allocating user resources for CQs  */
  MT_size_t         qp_ul_resources_sz;     /* Needed for allocating user resources for QPs  */
  MT_size_t         srq_ul_resources_sz;    /* Needed for allocating user resources for SRQs */
  MT_size_t         pd_ul_resources_sz;     /* Needed for allocating user resources for PDs  */
  VAPI_hca_cap_t    hca_caps;               /* HCA capabilities (mirror from kernel space) */
  /* Data structures to track allocated user level resources (for cleanup and handles validation) */ 
  VIP_hashp2p_p_t   cq_info_db;           
  VIP_array_p_t     srq_info_db;
  VIP_hashp2p_p_t   qp_info_db;
  VIP_hashp2p_p_t   pd_info_db;
  VIP_hashp2p_p_t   mw_info_db;
  VIP_hashp_p_t     pd_rev_info_db;
  VIP_hashp_p_t     cq_rev_info_db;
  EM_async_ctx_hndl_t async_hndl_ctx; /* the async handler context for this hobul */
#ifndef MT_KERNEL
  /* EQs are required only for user level event forwarding */
  /* polling threadd context */
  eq_pollt_t pollt_ctx[2];  /* 2 threads: VIPKL_EQ_COMP_EVENTH and VIPKL_EQ_ASYNC_EVENTH */
#endif  
} HOBUL_t;


/* Generic function to add/remove from info data structures (lists for this implementation) */
/* Use the return of "add_" functions as the handle
 *   (the pointer to the info struct in this imp.) 
 * Use the remove functions to get back the allocated info pointer
 */
static inline VAPI_pd_hndl_t add_to_pd_info_db(HOBUL_t *hobul, pd_info_t *pd_info_p);
static inline pd_info_t * remove_from_pd_info_db(HOBUL_t *hobul, VAPI_pd_hndl_t pd_hndl);
static inline VAPI_cq_hndl_t add_to_cq_info_db(HOBUL_t *hobul, cq_info_t *cq_info_p);
static inline cq_info_t * remove_from_cq_info_db(HOBUL_t *hobul, VAPI_cq_hndl_t cq_hndl);
static inline VAPI_qp_hndl_t add_to_qp_info_db(HOBUL_t *hobul, qp_info_t *qp_info_p);
static inline qp_info_t * remove_from_qp_info_db(HOBUL_t *hobul, VAPI_qp_hndl_t qp_hndl);
static inline VAPI_mw_hndl_t add_to_mw_info_db(HOBUL_t *hobul, mw_info_t *mw_info_p);
static inline mw_info_t * remove_from_mw_info_db(HOBUL_t *hobul, VAPI_mw_hndl_t mw_hndl);
/* Handles conversion (VIPKL's to VAPI's) */
static VAPI_pd_hndl_t vipkl2vapi_pd(HOBUL_t *hobul, PDM_pd_hndl_t vipkl_pd_hndl);
static VAPI_cq_hndl_t vipkl2vapi_cq(HOBUL_t *hobul, CQM_cq_hndl_t vipkl_cq_hndl);
#if 0
static VAPI_qp_hndl_t vipkl2vapi_qp(HOBUL_t *hobul, QPM_qp_hndl_t vipkl_qp_hndl);
static VAPI_mw_hndl_t vipkl2vapi_mw(HOBUL_t *hobul, MM_mrw_hndl_t vipkl_qp_hndl);
#endif

/* lookup function in order to validate given handle */
static inline pd_info_t* get_pd_info(HOBUL_t *hobul_p, VAPI_pd_hndl_t pd);
static inline cq_info_t* get_cq_info(HOBUL_t *hobul_p, VAPI_cq_hndl_t cq); 
static inline qp_info_t* get_qp_info(HOBUL_t *hobul_p, VAPI_qp_hndl_t qp); 
static inline mw_info_t* get_mw_info(HOBUL_t *hobul_p, VAPI_mw_hndl_t mw); 

  


/* macros */
#define GET_PD_INFO(hobul_p,vapi_pd_hndl) get_pd_info(hobul_p,vapi_pd_hndl)
#define GET_CQ_INFO(hobul_p,vapi_cq_hndl) get_cq_info(hobul_p,vapi_cq_hndl)
#define GET_QP_INFO(hobul_p,vapi_qp_hndl) get_qp_info(hobul_p,vapi_qp_hndl)
#define GET_MW_INFO(hobul_p,vapi_mw_hndl) get_mw_info(hobul_p,vapi_mw_hndl)

static VAPI_pd_hndl_t vipkl2vapi_pd(HOBUL_t *hobul_p, PDM_pd_hndl_t vipkl_pd_hndl);
static VAPI_cq_hndl_t vipkl2vapi_cq(HOBUL_t *hobul_p, CQM_cq_hndl_t vipkl_cq_hndl);

static inline VIP_ret_t add_to_pd_rev_info_db(HOBUL_t *hobul_p, pd_info_t *pd_info_p);
static inline VIP_ret_t add_to_cq_rev_info_db(HOBUL_t *hobul_p, cq_info_t *cq_info_p);
static inline VAPI_pd_hndl_t remove_from_pd_rev_info_db(HOBUL_t *hobul_p, PDM_pd_hndl_t pd_hndl);
static inline VAPI_cq_hndl_t remove_from_cq_rev_info_db(HOBUL_t *hobul_p, CQM_cq_hndl_t cq_hndl);



#ifndef MT_KERNEL
static void* eq_poll_thread(void* eq_pollt_ptr)
{
  eq_pollt_t *eq_pollt_p= (eq_pollt_t*)eq_pollt_ptr;
  const HOBUL_t *hobul_p= eq_pollt_p->hobul;
  const VAPI_hca_hndl_t vapi_hca= hobul_p->vipkl_hndl; 
  const VIP_hca_hndl_t vipkl_hndl= hobul_p->vipkl_hndl;
  const VIPKL_EQ_cbk_type_t cbk_type= eq_pollt_p->cbk_type;
  VIPKL_EQ_event_t eqe;
  VIP_ret_t rc;


  rc= VIPKL_EQ_new(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,cbk_type, hobul_p->async_hndl_ctx,
                   &(eq_pollt_p->vipkl_eq));
  if (rc != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed initializing EQ polling thread for %s (%s)"),__func__,
               cbk_type == VIPKL_EQ_COMP_EVENTH ? "completion events": "async. events" ,
               VAPI_strerror_sym(rc));
    eq_pollt_p->state= EQ_POLLT_EXIT_DONE;
    return (void*)(MT_ulong_ptr_t)rc;
  }
  eq_pollt_p->state= EQ_POLLT_INIT_DONE;

  while (1) { /* Continue until EQ is invalid */
    rc= VIPKL_EQ_poll(VIP_RSCT_NULL_USR_CTX,vipkl_hndl,eq_pollt_p->vipkl_eq,&eqe);
    if (rc != VIP_OK) {
      MTL_DEBUG5(MT_FLFMT("VIPKL_EQ_poll returned %s"), VAPI_strerror_sym(rc));
      if (rc != VIP_EINTR) { /* failed and not due to "signal" interruption --> exit */
        break;
      }
      continue; /* polling/blocking  was just interrupted - continue */
    }
    switch (cbk_type) {
      case VIPKL_EQ_COMP_EVENTH:
        eqe.eventh.comp(vapi_hca,eqe.event_record.modifier.cq_hndl,eqe.private_data);
        break;
      case VIPKL_EQ_ASYNC_EVENTH:
        eqe.eventh.async(vapi_hca,&eqe.event_record,eqe.private_data);
        break;
    }
  }

  eq_pollt_p->state= EQ_POLLT_EXIT_DONE;
  return (void*)VIP_OK;
}

static VIP_ret_t start_eq_poll_thread(HOBUL_t *hobul_p,VIPKL_EQ_cbk_type_t cbk_type)
{
  call_result_t  mosal_ret = MT_OK;
  const int pollt_index= cbk_type;
  
  /* A check to validate correctness of using the EQ callback types as indecies into pollt_ctx */
  if ((VIPKL_EQ_COMP_EVENTH != 0) || (VIPKL_EQ_ASYNC_EVENTH != 1)) {
    MTL_ERROR1(MT_FLFMT(
     "%s: VIPKL_EQ_cbk_t enumeration values has changed - pollt_ctx indexing should be changed !!"),
               __func__);
    return VIP_EFATAL;
  }

  hobul_p->pollt_ctx[pollt_index].cbk_type= cbk_type;
  hobul_p->pollt_ctx[pollt_index].state= EQ_POLLT_INIT_REQ;
  
  mosal_ret = MOSAL_thread_start(&(hobul_p->pollt_ctx[pollt_index].mosal_thread),
                                 MOSAL_THREAD_FLAGS_DETACHED,
                                 eq_poll_thread,&(hobul_p->pollt_ctx[pollt_index]));
  switch(mosal_ret) {
    case MT_OK:
      break;
    case MT_EAGAIN:
      MTL_ERROR2(MT_FLFMT("Failed EQ polling thread launch: Out of resources"));
      return VIP_EAGAIN;
    case MT_EPERM:
      MTL_ERROR2(MT_FLFMT("Failed EQ polling thread launch: No permissions"));
      return VIP_EPERM;
    case MT_EINVAL:
      MTL_ERROR2(MT_FLFMT("Failed EQ polling thread launch: Invalid parameter"));
      return VIP_EINVAL_PARAM;
    default:
      MTL_ERROR2(MT_FLFMT("Failed EQ polling thread launch: General error"));
      return VIP_EGEN;
  }
  
  while (hobul_p->pollt_ctx[pollt_index].state == EQ_POLLT_INIT_REQ);  /* Waiting for thread to complete initialization */
  if (hobul_p->pollt_ctx[pollt_index].state == EQ_POLLT_EXIT_DONE) {
    MTL_ERROR2(MT_FLFMT("Failed EQ polling thread initialization"));
    return VIP_EGEN;
  }
  return VIP_OK;
}

static VIP_ret_t stop_eq_poll_thread(HOBUL_t *hobul_p, VIPKL_EQ_cbk_type_t cbk_type)
{
  VIP_ret_t rc;
  const int pollt_index= cbk_type;
  
  /* cause EQ destruction - indirectly cause the polling thread to exit */
  rc= VIPKL_EQ_del(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,hobul_p->pollt_ctx[pollt_index].vipkl_eq);  
  if (rc != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("Failed VIPKL_EQ_del (%s)"), VAPI_strerror_sym(rc));
    return rc;
  }
  /* Waiting for thread to complete cleanup */
  while (hobul_p->pollt_ctx[pollt_index].state != EQ_POLLT_EXIT_DONE);  
  return VIP_OK;
}
#endif


static void init_opcode_validity_matrix(void)
{
  int i, j;

  /* first init all entries to FALSE */
  for ( i=0; i<VAPI_NUM_TS_TYPES; ++i ) {
    for ( j=0; j<VAPI_NUM_OPCODES; ++j ) {
      snd_matrix[i][j] = FALSE;
    }
  }
  
  /* VAPI_TS_RC */
  snd_matrix[VAPI_TS_RC][VAPI_RDMA_WRITE] = TRUE;
  snd_matrix[VAPI_TS_RC][VAPI_RDMA_WRITE_WITH_IMM] = TRUE;
  snd_matrix[VAPI_TS_RC][VAPI_SEND] = TRUE;
  snd_matrix[VAPI_TS_RC][VAPI_SEND_WITH_IMM] = TRUE;
  snd_matrix[VAPI_TS_RC][VAPI_RDMA_READ] = TRUE;
  snd_matrix[VAPI_TS_RC][VAPI_ATOMIC_CMP_AND_SWP] = TRUE;
  snd_matrix[VAPI_TS_RC][VAPI_ATOMIC_FETCH_AND_ADD] = TRUE;
  snd_matrix[VAPI_TS_RC][VAPI_RECEIVE] = FALSE;

  /* VAPI_TS_RD */
  snd_matrix[VAPI_TS_RD][VAPI_RDMA_WRITE] = TRUE;
  snd_matrix[VAPI_TS_RD][VAPI_RDMA_WRITE_WITH_IMM] = TRUE;
  snd_matrix[VAPI_TS_RD][VAPI_SEND] = TRUE;
  snd_matrix[VAPI_TS_RD][VAPI_SEND_WITH_IMM] = TRUE;
  snd_matrix[VAPI_TS_RD][VAPI_RDMA_READ] = TRUE;
  snd_matrix[VAPI_TS_RD][VAPI_ATOMIC_CMP_AND_SWP] = TRUE;
  snd_matrix[VAPI_TS_RD][VAPI_ATOMIC_FETCH_AND_ADD] = TRUE;
  snd_matrix[VAPI_TS_RD][VAPI_RECEIVE] = FALSE;

  /* VAPI_TS_UC */
  snd_matrix[VAPI_TS_UC][VAPI_RDMA_WRITE] = TRUE;
  snd_matrix[VAPI_TS_UC][VAPI_RDMA_WRITE_WITH_IMM] = TRUE;
  snd_matrix[VAPI_TS_UC][VAPI_SEND] = TRUE;
  snd_matrix[VAPI_TS_UC][VAPI_SEND_WITH_IMM] = TRUE;
  snd_matrix[VAPI_TS_UC][VAPI_RDMA_READ] = FALSE;
  snd_matrix[VAPI_TS_UC][VAPI_ATOMIC_CMP_AND_SWP] = FALSE;
  snd_matrix[VAPI_TS_UC][VAPI_ATOMIC_FETCH_AND_ADD] = FALSE;
  snd_matrix[VAPI_TS_UC][VAPI_RECEIVE] = FALSE;

  /* VAPI_TS_UD */
  snd_matrix[VAPI_TS_UD][VAPI_RDMA_WRITE] = FALSE;
  snd_matrix[VAPI_TS_UD][VAPI_RDMA_WRITE_WITH_IMM] = FALSE;
  snd_matrix[VAPI_TS_UD][VAPI_SEND] = TRUE;
  snd_matrix[VAPI_TS_UD][VAPI_SEND_WITH_IMM] = TRUE;
  snd_matrix[VAPI_TS_UD][VAPI_RDMA_READ] = FALSE;
  snd_matrix[VAPI_TS_UD][VAPI_ATOMIC_CMP_AND_SWP] = FALSE;
  snd_matrix[VAPI_TS_UD][VAPI_ATOMIC_FETCH_AND_ADD] = FALSE;
  snd_matrix[VAPI_TS_UD][VAPI_RECEIVE] = FALSE;

  /* VAPI_TS_RAW */
  snd_matrix[VAPI_TS_RAW][VAPI_RDMA_WRITE] = FALSE;
  snd_matrix[VAPI_TS_RAW][VAPI_RDMA_WRITE_WITH_IMM] = FALSE;
  snd_matrix[VAPI_TS_RAW][VAPI_SEND] = TRUE;
  snd_matrix[VAPI_TS_RAW][VAPI_SEND_WITH_IMM] = FALSE;
  snd_matrix[VAPI_TS_RAW][VAPI_RDMA_READ] = FALSE;
  snd_matrix[VAPI_TS_RAW][VAPI_ATOMIC_CMP_AND_SWP] = FALSE;
  snd_matrix[VAPI_TS_RAW][VAPI_ATOMIC_FETCH_AND_ADD] = FALSE;
  snd_matrix[VAPI_TS_RAW][VAPI_RECEIVE] = FALSE;
  return;
}


VIP_ret_t HOBUL_new(/*IN*/ VIP_hca_hndl_t vipkl_hndl,/*OUT*/ HOBUL_hndl_t *hobul_hndl_p)
{
  HOBUL_t *new_hobul;
  VAPI_hca_id_t dev_name;
  VIP_ret_t ret;
  HH_hca_dev_t  hca_ul_info;
  
  init_opcode_validity_matrix();
  new_hobul = TMALLOC(HOBUL_t); /* small */
  
  if (new_hobul == NULL) {
    return VIP_EAGAIN;
  }
  memset(new_hobul, 0, sizeof(HOBUL_t));
  new_hobul->vipkl_hndl=vipkl_hndl;

  ret = VIPKL_get_hh_hndl(vipkl_hndl, &(new_hobul->hh_hndl));
  if (ret != VIP_OK) {
    FREE(new_hobul);
    return ret;  
  }

  /* get info about the HCA capabilities from hobkl */
  ret =  VIPKL_query_hca_cap(new_hobul->vipkl_hndl, &(new_hobul->hca_caps) );
  if (ret != VIP_OK)  goto hobul_init_fail;
  
  /* get info about the HCA device name from hobkl */
  ret =  VIPKL_get_hca_id(new_hobul->vipkl_hndl, &dev_name );
  if (ret != VIP_OK)  goto hobul_init_fail;

  /* initialize ul resources in HH (kernel) */
  ret = VIPKL_get_hca_ul_info(vipkl_hndl,&hca_ul_info);
  if (ret != VIP_OK)  goto hobul_init_fail;
  
  /* Initialize HOBUL's fields */
  new_hobul->vendor_id =  hca_ul_info.vendor_id;
  new_hobul->device_id =  hca_ul_info.dev_id;
  new_hobul->ref_cnt= 0;
  MOSAL_spinlock_init(&new_hobul->ref_lock);
  new_hobul->cq_ul_resources_sz = hca_ul_info.cq_ul_resources_sz;
  new_hobul->srq_ul_resources_sz = hca_ul_info.srq_ul_resources_sz;
  new_hobul->qp_ul_resources_sz = hca_ul_info.qp_ul_resources_sz;
  new_hobul->pd_ul_resources_sz = hca_ul_info.pd_ul_resources_sz;
  new_hobul->hca_ul_resources_sz = hca_ul_info.hca_ul_resources_sz;
  /* Initialize "info" databases */
  ret = VIP_hashp2p_create_maxsize(0, new_hobul->hca_caps.max_pd_num, &(new_hobul->pd_info_db));
  if (ret != VIP_OK) {
      MTL_ERROR1("%s: ERROR (%d) : could not create pd_info_db\n", __func__, ret);
      goto hobul_init_fail;
  }
  ret = VIP_hashp2p_create_maxsize(0, new_hobul->hca_caps.max_num_cq, &(new_hobul->cq_info_db));
  if (ret != VIP_OK) {
      MTL_ERROR1("%s: ERROR (%d) : could not create cq_info_db\n", __func__, ret);
      goto hobul_init_cqdb_fail;
  }

  ret = VIP_array_create_maxsize(0, new_hobul->hca_caps.max_num_srq, &(new_hobul->srq_info_db));
  if (ret != VIP_OK) {
      MTL_ERROR1("%s: ERROR (%d) : could not create srq_info_db\n", __func__, ret);
      goto hobul_init_srqdb_fail;
  }

  ret = VIP_hashp2p_create_maxsize(0, new_hobul->hca_caps.max_num_qp, &(new_hobul->qp_info_db));
  if (ret != VIP_OK) {
      MTL_ERROR1("%s: ERROR (%d) : could not create qp_info_db\n", __func__, ret);
      goto hobul_init_qpdb_fail;
  }
  
  ret = VIP_hashp2p_create_maxsize(0, new_hobul->hca_caps.max_mw_num, &(new_hobul->mw_info_db));
  if (ret != VIP_OK) {
      MTL_ERROR1("%s: ERROR (%d) : could not create mw_info_db\n", __func__, ret);
      goto hobul_init_mwdb_fail;
  }
  
  ret = VIP_hashp_create_maxsize(0, new_hobul->hca_caps.max_pd_num, &(new_hobul->pd_rev_info_db));
  if (ret != VIP_OK) {
      MTL_ERROR1("%s: ERROR (%d) : could not create mw_info_db\n", __func__, ret);
      goto hobul_init_qprevdb_fail;
  }
  
  ret = VIP_hashp_create_maxsize(0, new_hobul->hca_caps.max_num_cq, &(new_hobul->cq_rev_info_db));
  if (ret != VIP_OK) {
      MTL_ERROR1("%s: ERROR (%d) : could not create mw_info_db\n", __func__, ret);
      goto hobul_init_cqrevdb_fail;
  }
  
  MTL_DEBUG4("%s: HCA_ul_info:\nvendor_id=%d\ndev_id=%d\ncq_ul_resources_sz="SIZE_T_FMT
           "\nqp_ul_resources_sz="SIZE_T_FMT"\npd_ul_resources_sz="SIZE_T_FMT"\n", __func__,
           hca_ul_info.vendor_id, hca_ul_info.dev_id, hca_ul_info.cq_ul_resources_sz, 
           hca_ul_info.qp_ul_resources_sz, hca_ul_info.pd_ul_resources_sz);
    
  /* malloc a memory region for saving the ul resources context */
  new_hobul->hca_ul_resources_p = (void*)MALLOC(hca_ul_info.hca_ul_resources_sz);
  if (new_hobul->hca_ul_resources_p == NULL)  {
    ret= VIP_EAGAIN;
    goto alloc_hca_ul_res_fail;
  }
  memset(new_hobul->hca_ul_resources_p, 0, hca_ul_info.hca_ul_resources_sz);

  /* now allocate the ul resources */
  /* kernel level call.  The protection context obtained here will be used only in the case that */
  /* we are running entirely in kernel mode, with no kernel wrapper for VIPKL. */
  ret = VIPKL_alloc_ul_resources(vipkl_hndl, MOSAL_get_kernel_prot_ctx(), 
                                 hca_ul_info.hca_ul_resources_sz, new_hobul->hca_ul_resources_p,
                                 &(new_hobul->async_hndl_ctx));
  if (ret != VIP_OK)  goto alloc_ul_res_fail;
  
#ifndef MT_KERNEL
  new_hobul->pollt_ctx[0].hobul= new_hobul;
  ret= start_eq_poll_thread(new_hobul,VIPKL_EQ_COMP_EVENTH);
  if (ret != VIP_OK)  goto start_comp_eq_poll_fail;
  new_hobul->pollt_ctx[1].hobul= new_hobul;
  ret= start_eq_poll_thread(new_hobul,VIPKL_EQ_ASYNC_EVENTH);
  if (ret != VIP_OK)  goto start_async_eq_poll_fail;
#endif

  /* allocate the HHUL resources */
  ret = HHUL_alloc_hca_hndl(hca_ul_info.vendor_id, hca_ul_info.dev_id,
                            new_hobul->hca_ul_resources_p, &(new_hobul->hhul_hndl));
  if (ret != VIP_OK) goto  alloc_hca_hndl_fail;

  /* return values */
  *hobul_hndl_p = (HOBUL_hndl_t) new_hobul;
  return VIP_OK;

  /* Errors exit points - for appropriate cleanup */
  alloc_hca_hndl_fail:
#ifndef MT_KERNEL
    stop_eq_poll_thread(new_hobul,VIPKL_EQ_ASYNC_EVENTH);
  
  start_async_eq_poll_fail:  
    stop_eq_poll_thread(new_hobul,VIPKL_EQ_COMP_EVENTH);
  
  start_comp_eq_poll_fail:
#endif    
    VIPKL_free_ul_resources(vipkl_hndl, hca_ul_info.hca_ul_resources_sz, 
      new_hobul->hca_ul_resources_p, new_hobul->async_hndl_ctx); 

  alloc_ul_res_fail:  
    FREE(new_hobul->hca_ul_resources_p);

  alloc_hca_ul_res_fail:
      VIP_hashp_destroy(new_hobul->cq_rev_info_db, NULL, NULL);

  hobul_init_cqrevdb_fail:
      VIP_hashp_destroy(new_hobul->pd_rev_info_db, NULL, NULL);

  hobul_init_qprevdb_fail:
      VIP_hashp2p_destroy(new_hobul->mw_info_db, NULL, NULL);

  hobul_init_mwdb_fail:
      VIP_hashp2p_destroy(new_hobul->qp_info_db, NULL, NULL);

  hobul_init_qpdb_fail:
      VIP_array_destroy(new_hobul->srq_info_db, NULL);

  hobul_init_srqdb_fail:
      VIP_hashp2p_destroy(new_hobul->cq_info_db, NULL, NULL);

  hobul_init_cqdb_fail:
      VIP_hashp2p_destroy(new_hobul->pd_info_db, NULL, NULL);

  hobul_init_fail:
    FREE(new_hobul);
    return ret;  

}


/* The call must be invoked only when all resources associated with this HOBUL are freed */
/* (but for the hca_ul_resources of course) */
VIP_ret_t HOBUL_delete(/*IN*/ HOBUL_hndl_t hobul_hndl)
{
  VIP_ret_t ret;
  HH_ret_t hhret;
  HOBUL_t *hobul_p;

  hobul_p = (HOBUL_t *)hobul_hndl;
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  MOSAL_spinlock_lock(&hobul_p->ref_lock);  
  if (hobul_p->ref_cnt != 0) {
    MTL_ERROR4(MT_FLFMT("%s: Invoked while "SIZE_T_FMT" resources are still allocated"),__func__,
                hobul_p->ref_cnt);
    MOSAL_spinlock_unlock(&hobul_p->ref_lock);
    return VIP_EBUSY;
  }
  MOSAL_spinlock_unlock(&hobul_p->ref_lock);

  /* Free hobul's resources in proper dependency order */
  /* Assume all tables are empty */
  VIP_hashp_destroy(hobul_p->cq_rev_info_db, NULL, NULL);
  VIP_hashp_destroy(hobul_p->pd_rev_info_db, NULL, NULL);
  VIP_hashp2p_destroy(hobul_p->mw_info_db, NULL, NULL);
  VIP_hashp2p_destroy(hobul_p->qp_info_db, NULL, NULL);
  VIP_array_destroy(hobul_p->srq_info_db, NULL);
  VIP_hashp2p_destroy(hobul_p->cq_info_db, NULL, NULL);
  VIP_hashp2p_destroy(hobul_p->pd_info_db, NULL, NULL);
  
  hhret = HHUL_cleanup_user_level (hobul_p->hhul_hndl);
  if (hhret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("Failed HHUL_cleanup_user_level (%s)"),HH_strerror_sym(hhret));
    /* Continue anyway - we have nothing better to do after all resources were freed */
  }
#ifndef MT_KERNEL
  ret= stop_eq_poll_thread(hobul_p,VIPKL_EQ_ASYNC_EVENTH);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed stop_eq_poll_thread for async. events (%s)"),__func__,
               VAPI_strerror_sym(ret));;
    /* Continue anyway - we have nothing better to do after all resources were freed */
  }
  ret= stop_eq_poll_thread(hobul_p,VIPKL_EQ_COMP_EVENTH);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed stop_eq_poll_thread for comp. events (%s)"),__func__,
               VAPI_strerror_sym(ret));;
    /* Continue anyway - we have nothing better to do after all resources were freed */
  }
#endif
  ret = VIPKL_free_ul_resources(hobul_p->vipkl_hndl, hobul_p->hca_ul_resources_sz, 
                                hobul_p->hca_ul_resources_p, hobul_p->async_hndl_ctx);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("Failed VIPKL_free_ul_resources (%s)"),VAPI_strerror_sym(ret));;
    /* Continue anyway - we have nothing better to do after all resources were freed */
  }
  FREE(hobul_p->hca_ul_resources_p);

  FREE(hobul_p);

  return ret;
}


static void HOBUL_free_func(VIP_hashp2p_key_t key, VIP_hashp2p_value_t info_obj_p,void* ctx)
{
    FREE(info_obj_p);
}

void HOBUL_delete_force(/*IN*/ HOBUL_hndl_t hobul_hndl)
{
  HH_ret_t hhret;
  HOBUL_t *hobul_p = (HOBUL_t *)hobul_hndl;

  FUNC_IN;

  /* Free hobul's resources in proper dependency order */
  VIP_hashp_destroy(hobul_p->cq_rev_info_db, NULL, NULL);
  VIP_hashp_destroy(hobul_p->pd_rev_info_db, NULL, NULL);
  VIP_hashp2p_destroy(hobul_p->mw_info_db, HOBUL_free_func, NULL);
  VIP_hashp2p_destroy(hobul_p->qp_info_db, HOBUL_free_func, NULL);
  VIP_hashp2p_destroy(hobul_p->cq_info_db, HOBUL_free_func, NULL);
  VIP_hashp2p_destroy(hobul_p->pd_info_db, HOBUL_free_func, NULL);
  
  MTL_DEBUG2(MT_FLFMT("before   HHUL_cleanup_user_level \n"));
  hhret = HHUL_cleanup_user_level(hobul_p->hhul_hndl);
  if (hhret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("Failed HHUL_cleanup_user_level (%s)"),HH_strerror_sym(hhret));
    /* Continue anyway - we have nothing better to do after all resources were freed */
  }
  
  FREE(hobul_p->hca_ul_resources_p);

  FREE(hobul_p);
}


VIP_ret_t HOBUL_inc_ref_cnt(/*IN*/ HOBUL_hndl_t hobul_hndl)
{
  HOBUL_t *hobul_p;

  hobul_p = (HOBUL_t *)hobul_hndl;
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  HOBUL_INC_REF_CNT(hobul_p);
  return VIP_OK;
}

VIP_ret_t HOBUL_dec_ref_cnt(/*IN*/ HOBUL_hndl_t hobul_hndl)
{
  HOBUL_t *hobul_p;

  hobul_p = (HOBUL_t *)hobul_hndl;
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  HOBUL_DEC_REF_CNT(hobul_p);
  return VIP_OK;
}


VIP_ret_t HOBUL_create_cq( /*IN*/ HOBUL_hndl_t hobul_hndl,
                          /*IN*/ VAPI_cqe_num_t min_num_o_entries,
                          /*OUT*/ VAPI_cq_hndl_t *cq_hndl_p, 
                          /*OUT*/ VAPI_cqe_num_t *num_o_entries_p)
{
  VIP_ret_t         ret;
  HH_ret_t          hh_ret;
  HOBUL_t           *hobul_p= (HOBUL_t *)hobul_hndl;
  void              *cq_ul_resources_p;
  cq_info_t         *cq_info_p;
  VAPI_cq_hndl_t    local_cq_hndl;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  HOBUL_INC_REF_CNT(hobul_p);

  if (min_num_o_entries > hobul_p->hca_caps.max_num_ent_cq) {
    ret=VIP_E2BIG_CQ_NUM;
    goto num_ent_fail;
  }

  cq_info_p= TMALLOC(cq_info_t);
  if (cq_info_p == NULL)  {
    MTL_ERROR1(MT_FLFMT("Failed memory allocation for cq_info_p"));
    ret= VIP_EAGAIN;
    goto malloc_fail;
  }

  /* allocate CQ resources */
  /* first, allocate the ul resources buffer */
  cq_ul_resources_p = (void*)MALLOC(hobul_p->cq_ul_resources_sz);
  if (cq_ul_resources_p == NULL)  {
    MTL_ERROR1(MT_FLFMT("Failed memory allocation for cq_ul_resources (%ld bytes)"),
      (unsigned long) hobul_p->cq_ul_resources_sz);
    ret= VIP_EAGAIN; 
	goto alloc_ul_res_p_fail;
  }

  hh_ret = HHUL_create_cq_prep(hobul_p->hhul_hndl, min_num_o_entries,&(cq_info_p->hhul_cq_hndl), 
                               num_o_entries_p, cq_ul_resources_p);
  if (hh_ret != HH_OK)  {
	  MTL_ERROR1(MT_FLFMT("HHUL_create_cq_prep() failed."));
    ret= (VIP_ret_t) hh_ret;
    goto create_cq_prep_fail;
  }
  cq_info_p->num_o_cqes= *num_o_entries_p; /* Save for get_cq_props */

  /* kernel level call */
  ret = VIPKL_create_cq(VIP_RSCT_NULL_USR_CTX, hobul_p->vipkl_hndl, 
                        (VAPI_cq_hndl_t)(cq_info_p->hhul_cq_hndl), MOSAL_get_kernel_prot_ctx(),
                        hobul_p->async_hndl_ctx, hobul_p->cq_ul_resources_sz, cq_ul_resources_p, 
                        &(cq_info_p->vipkl_cq_hndl), &(cq_info_p->cq_num));
  if (ret != VIP_OK) {
	  MTL_ERROR1(MT_FLFMT("VIPKL_create_cq() failed."));
	  goto create_cq_fail;
  }
  
  /* user level call */
  hh_ret = HHUL_create_cq_done(hobul_p->hhul_hndl, cq_info_p->hhul_cq_hndl, cq_info_p->cq_num, 
    cq_ul_resources_p);
  if (hh_ret != HH_OK) {
	  MTL_ERROR1(MT_FLFMT("HHUL_create_cq_done() failed."));
    ret= (VIP_ret_t) hh_ret;
    goto create_cq_done_fail;
  }

  cq_info_p->priv_context= NULL;
  cq_info_p->cq_block_hndl= VIPKL_CQBLK_INVAL_HNDL;
  /* return values */
  ret = add_to_cq_rev_info_db(hobul_p,cq_info_p);
  if ( ret != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("add_to_cq_rev_info_db failed: %s"), VAPI_strerror_sym(ret));
      ret = VAPI_EAGAIN;
      goto add_to_cq_rev_info_db_fail;
  }

  local_cq_hndl = add_to_cq_info_db(hobul_p,cq_info_p);
  if (local_cq_hndl == (VAPI_cq_hndl_t) VAPI_INVAL_HNDL) {
      MTL_ERROR1(MT_FLFMT("local_cq_hndl==VAPI_INVAL_HNDL"));
      ret = VAPI_EAGAIN;
      goto create_cq_done_fail;
  }
  *cq_hndl_p  = local_cq_hndl;
  
  FREE(cq_ul_resources_p);
  return VIP_OK;
  
  create_cq_done_fail:
    remove_from_cq_rev_info_db(hobul_p, (CQM_cq_hndl_t)*cq_hndl_p);
  add_to_cq_rev_info_db_fail:
    VIPKL_destroy_cq(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,cq_info_p->vipkl_cq_hndl,FALSE);
  create_cq_fail:
    HHUL_destroy_cq_done(hobul_p->hhul_hndl,cq_info_p->hhul_cq_hndl);
  create_cq_prep_fail:
    FREE(cq_ul_resources_p);
  alloc_ul_res_p_fail:
    FREE(cq_info_p);
  malloc_fail:
  num_ent_fail:
    HOBUL_DEC_REF_CNT(hobul_p);
    return ret;
}



VIP_ret_t HOBUL_destroy_cq(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_cq_hndl_t cq_hndl)
{
  VIP_ret_t ret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p = remove_from_cq_info_db(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  
  remove_from_cq_rev_info_db(hobul_p, cq_info_p->vipkl_cq_hndl);

  /* kernel level call */
  ret = VIPKL_destroy_cq(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl ,cq_info_p->vipkl_cq_hndl,FALSE);
  if ( ret != VIP_OK ) {
    /* rollback */
    add_to_cq_rev_info_db(hobul_p,cq_info_p);
    add_to_cq_info_db(hobul_p,cq_info_p);
    return ret;  /* resource still valid. Do not free UL resources */
  }

  if (HHUL_destroy_cq_done(hobul_p->hhul_hndl, (HHUL_cq_hndl_t)cq_hndl) != HH_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Unexpected error: Failed HHUL_destroy_cq_done for cqn=%d"),__func__,
               cq_info_p->cq_num);
    /* Continue anyway - VIPKL_destroy was successfull */
  }
  FREE(cq_info_p);
  HOBUL_DEC_REF_CNT(hobul_p);
  return ret;
}


VIP_ret_t HOBUL_resize_cq(
    /*IN*/  HOBUL_hndl_t    hobul_hndl,
    /*IN*/  VAPI_cq_hndl_t  cq_hndl,
    /*IN*/ VAPI_cqe_num_t min_num_o_entries,
    /*OUT*/ VAPI_cqe_num_t *num_o_entries_p
)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  void              *cq_ul_resources_p;
  HH_ret_t ret;
  VIP_ret_t vret;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  
  cq_ul_resources_p = (void*)MALLOC(hobul_p->cq_ul_resources_sz);
  if (cq_ul_resources_p == NULL)  {
    MTL_ERROR1(MT_FLFMT("%s: Failed memory allocation for cq_ul_resources (%ld bytes)"),__func__,
      (unsigned long) hobul_p->cq_ul_resources_sz);
    return HH_EAGAIN;
  }
  
  
  ret= HHUL_resize_cq_prep(hobul_p->hhul_hndl,cq_info_p->hhul_cq_hndl,min_num_o_entries,
                           num_o_entries_p,cq_ul_resources_p);
  if (ret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed HHUL_resize_cq_prep for CQ 0x%X (%s)"),__func__,
               cq_info_p->cq_num,HH_strerror_sym(ret));
    FREE(cq_ul_resources_p);
    return (VIP_ret_t)ret;
  }

  vret= VIPKL_resize_cq(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,cq_info_p->vipkl_cq_hndl,
                        hobul_p->cq_ul_resources_sz,cq_ul_resources_p);
  if (vret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed VIPKL_resize_cq for CQ 0x%X (%s)"),__func__,
               cq_info_p->cq_num,VAPI_strerror_sym(vret));
    HHUL_resize_cq_done(hobul_p->hhul_hndl,cq_info_p->hhul_cq_hndl,NULL); /* undo resize */
    FREE(cq_ul_resources_p);
    return vret;
  }

  ret= HHUL_resize_cq_done(hobul_p->hhul_hndl,cq_info_p->hhul_cq_hndl,cq_ul_resources_p); 
  if (ret != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: unexpected error: Failed HHUL_resize_cq_done for CQ 0x%X (%s)"),
               __func__,cq_info_p->cq_num,HH_strerror_sym(ret));
    /* no recovery anyway (this should not happen !) */
  }
  cq_info_p->num_o_cqes= *num_o_entries_p; /* Update for get_cq_props */

  FREE(cq_ul_resources_p);
  return (VIP_ret_t)ret;
}

VIP_ret_t HOBUL_get_cq_props(  
    /*IN*/  HOBUL_hndl_t    hobul_hndl,
    /*IN*/  VAPI_cq_hndl_t  cq_hndl,
    /*OUT*/ VAPI_cqe_num_t  *num_of_entries_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= get_cq_info(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  MTL_TRACE1("%s calling VIPKL_get_cq_props()...\n", __func__);
  *num_of_entries_p= cq_info_p->num_o_cqes;
  return VIP_OK;
}


VIP_ret_t HOBUL_poll_cq(/*IN*/ HOBUL_hndl_t   hobul_hndl,
                        /*IN*/ VAPI_cq_hndl_t  cq_hndl,
                       /*OUT*/ VAPI_wc_desc_t *comp_desc_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  HH_ret_t ret;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  /* call user level only */
  MTPERF_TIME_START(HHUL_poll4cqe);
  ret= HHUL_poll4cqe(hobul_p->hhul_hndl, (HHUL_cq_hndl_t) cq_hndl, comp_desc_p);
  MTPERF_TIME_END(HHUL_poll4cqe);
  return (VIP_ret_t)ret;
}

VIP_ret_t HOBUL_poll_cq_block( 
                                /*IN*/ HOBUL_hndl_t    hobul_hndl,
                              /*IN*/ VAPI_cq_hndl_t  cq_hndl,
                              /*IN*/  MT_size_t      timeout_usec,
                              /*OUT*/ VAPI_wc_desc_t *comp_desc_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  HH_ret_t ret;
  VIP_ret_t vret;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  if (cq_info_p->cq_block_hndl == VIPKL_CQBLK_INVAL_HNDL) {
    MTL_ERROR4(MT_FLFMT(
      "Blocking poll for completion with no blocking context (EVAPI_set_comp_eventh not invoked)"));
    return VIP_EINVAL_PARAM;
  }
  ret= HHUL_poll4cqe(hobul_p->hhul_hndl, (HHUL_cq_hndl_t) cq_hndl, comp_desc_p);
  if (ret != HH_CQ_EMPTY)  return (VIP_ret_t)ret;
  /* Arm CQ event for next completion */
  ret= HHUL_req_comp_notif(hobul_p->hhul_hndl , (HHUL_cq_hndl_t) cq_hndl , VAPI_NEXT_COMP);
  if (ret != HH_OK)  return (VIP_ret_t)ret;
  /* Poll again - avoid race with notification request */
  ret= HHUL_poll4cqe(hobul_p->hhul_hndl, (HHUL_cq_hndl_t) cq_hndl, comp_desc_p);
  if (ret != HH_CQ_EMPTY)  return (VIP_ret_t)ret;
  /* Now we go to sleep until completion event is received */
  vret=VIPKL_cqblk_wait(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,cq_info_p->cq_block_hndl,timeout_usec);
  if (vret != VIP_OK) {
    if ((vret == VIP_ETIMEDOUT) || (vret == VIP_EINTR)) {
        MTL_DEBUG4(MT_FLFMT("%s: VIPKL_cqblk_wait timed out, or interrupted (%s)"),__func__,VAPI_strerror_sym(vret));
    } else {
        MTL_ERROR4(MT_FLFMT("%s: Failed VIPKL_cqblk_wait (%s)"),__func__,VAPI_strerror_sym(vret));
    }
    return vret;
  }
  ret= HHUL_poll4cqe(hobul_p->hhul_hndl, (HHUL_cq_hndl_t) cq_hndl, comp_desc_p);
  return (VIP_ret_t)ret;
}

VIP_ret_t HOBUL_poll_cq_unblock(
                       /*IN*/  HOBUL_hndl_t      hobul_hndl,
                       /*IN*/  VAPI_cq_hndl_t    cq_hndl
                       )
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  if (cq_info_p->cq_block_hndl == VIPKL_CQBLK_INVAL_HNDL) {
    MTL_ERROR4(MT_FLFMT(
      "EVAPI_poll_cq_unblock with no blocking context (EVAPI_set_comp_eventh not invoked)"));
    return VIP_EINVAL_PARAM;
  }
  return VIPKL_cqblk_signal(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,cq_info_p->cq_block_hndl);
}

VIP_ret_t HOBUL_peek_cq(
  /*IN*/  HOBUL_hndl_t      hobul_hndl,
  /*IN*/  VAPI_cq_hndl_t       cq_hndl,
  /*IN*/  VAPI_cqe_num_t       cqe_index
)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  return (VIP_ret_t)HHUL_peek_cq(hobul_p->hhul_hndl,(HHUL_cq_hndl_t) cq_hndl,cqe_index);
}

VIP_ret_t HOBUL_req_comp_notif(/*IN*/ HOBUL_hndl_t             hobul_hndl,
                                /*IN*/ VAPI_cq_hndl_t           cq_hndl,
                                /*IN*/ VAPI_cq_notif_type_t     notif_type)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;

  return HHUL_req_comp_notif(hobul_p->hhul_hndl , (HHUL_cq_hndl_t) cq_hndl ,notif_type);
}

VIP_ret_t HOBUL_req_ncomp_notif(
    /*IN*/  HOBUL_hndl_t       hobul_hndl,
    /*IN*/  VAPI_cq_hndl_t    cq_hndl,
    /*IN*/  VAPI_cqe_num_t    cqe_num
)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;

  return HHUL_req_ncomp_notif(hobul_p->hhul_hndl , (HHUL_cq_hndl_t) cq_hndl ,cqe_num);
}

VIP_ret_t HOBUL_create_srq(
                         IN      HOBUL_hndl_t     hobul_hndl,
                         IN      VAPI_srq_attr_t  *srq_props_p,
                         OUT     VAPI_srq_hndl_t  *srq_hndl_p,
                         OUT     VAPI_srq_attr_t  *actual_srq_props_p
                         )
{
  VIP_ret_t         ret;
  HH_ret_t          hh_ret;
  HOBUL_t           *hobul_p= (HOBUL_t *)hobul_hndl;
  void              *srq_ul_resources_p;
  srq_info_t        *srq_info_p;
  pd_info_t         *pd_info_p;
  VIP_array_handle_t local_srq_hndl;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  HOBUL_INC_REF_CNT(hobul_p);

  pd_info_p= get_pd_info(hobul_p,srq_props_p->pd_hndl);
  if (pd_info_p == NULL)  {HOBUL_DEC_REF_CNT(hobul_p); return VIP_EINVAL_PD_HNDL;}

  srq_info_p= TMALLOC(srq_info_t);
  if (srq_info_p == NULL)  {
    MTL_ERROR1(MT_FLFMT("Failed memory allocation for srq_info_p"));
    ret= VIP_EAGAIN;
    goto malloc_fail;
  }

  /* allocate SRQ resources */
  /* first, allocate the ul resources buffer */
  srq_ul_resources_p = (void*)MALLOC(hobul_p->srq_ul_resources_sz);
  if (srq_ul_resources_p == NULL)  {
    MTL_ERROR1(MT_FLFMT("Failed memory allocation for srq_ul_resources ("SIZE_T_FMT" bytes)"),
      hobul_p->srq_ul_resources_sz);
    ret= VIP_EAGAIN; 
	goto alloc_ul_res_p_fail;
  }

  hh_ret = HHUL_create_srq_prep(hobul_p->hhul_hndl, pd_info_p->hhul_pd_hndl, 
                                srq_props_p->max_outs_wr, srq_props_p->max_sentries,
                                &(srq_info_p->hhul_srq_hndl), 
                                &(srq_info_p->max_outs_wr), &(srq_info_p->max_sentries),
                                srq_ul_resources_p);
  if (hh_ret != HH_OK)  {
	  MTL_ERROR1(MT_FLFMT("HHUL_create_srq_prep() failed.(%s)"), HH_strerror_sym(hh_ret));
    ret= (VIP_ret_t) hh_ret;
    goto create_srq_prep_fail;
  }
  srq_info_p->pd_hndl= srq_props_p->pd_hndl; /* Save for query_srq */

  ret= VIP_array_insert(hobul_p->srq_info_db, (VIP_array_obj_t)srq_info_p, &local_srq_hndl);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array_insert (err=%d)"), __func__, ret);
    goto vip_array_fail;
  }
  *srq_hndl_p= (VAPI_srq_hndl_t)local_srq_hndl;

  /* kernel level call */
  ret = VIPKL_create_srq(VIP_RSCT_NULL_USR_CTX, hobul_p->vipkl_hndl, 
                        *srq_hndl_p, pd_info_p->vipkl_pd_hndl ,
                        hobul_p->async_hndl_ctx, hobul_p->srq_ul_resources_sz, srq_ul_resources_p, 
                        &(srq_info_p->vipkl_srq_hndl));
  if (ret != VIP_OK) {
	  MTL_ERROR1(MT_FLFMT("VIPKL_create_srq() failed."));
	  goto create_srq_fail;
  }
  srq_info_p->hh_srq= srq_info_p->vipkl_srq_hndl; /*1-to-1 mapping from vipkl's to hh's*/
  
  /* user level call */
  hh_ret = HHUL_create_srq_done(hobul_p->hhul_hndl, srq_info_p->hhul_srq_hndl,
                                srq_info_p->hh_srq, srq_ul_resources_p);
  if (hh_ret != HH_OK) {
	  MTL_ERROR1(MT_FLFMT("HHUL_create_srq_done() failed.(%s)"), HH_strerror_sym(hh_ret));
    ret= (VIP_ret_t) hh_ret;
    goto create_srq_done_fail;
  }

  FREE(srq_ul_resources_p);
  
  actual_srq_props_p->max_outs_wr= srq_info_p->max_outs_wr;
  actual_srq_props_p->max_sentries= srq_info_p->max_sentries;
  actual_srq_props_p->pd_hndl= srq_info_p->pd_hndl;
  
  return VIP_OK;
  
  create_srq_done_fail:
    VIPKL_destroy_srq(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,srq_info_p->vipkl_srq_hndl);
  create_srq_fail:
    VIP_array_erase(hobul_p->srq_info_db, local_srq_hndl, NULL);
  vip_array_fail:
    HHUL_destroy_cq_done(hobul_p->hhul_hndl,srq_info_p->hhul_srq_hndl);
  create_srq_prep_fail:
    FREE(srq_ul_resources_p);
  alloc_ul_res_p_fail:
    FREE(srq_info_p);
  malloc_fail:
    HOBUL_DEC_REF_CNT(hobul_p);
    return ret;
}
                         

VIP_ret_t HOBUL_query_srq(
                         IN      HOBUL_hndl_t     hobul_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl,
                         OUT     VAPI_srq_attr_t   *srq_attr_p
                         )
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  VIP_array_obj_t vobj;
  srq_info_t *srq_info_p;
  VIP_ret_t rc;

  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  rc= VIP_array_find_hold(hobul_p->srq_info_db, srq_hndl, &vobj);
  if (rc != VIP_OK)  return VIP_EINVAL_SRQ_HNDL;
  srq_info_p= (srq_info_t *)vobj;

  rc= VIPKL_query_srq(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl, srq_info_p->vipkl_srq_hndl, 
                      &(srq_attr_p->srq_limit));
  
  srq_attr_p->max_outs_wr= srq_info_p->max_outs_wr;
  srq_attr_p->max_sentries= srq_info_p->max_sentries;
  srq_attr_p->pd_hndl= srq_info_p->pd_hndl;

  VIP_array_find_release(hobul_p->srq_info_db, srq_hndl);

  return rc;
}
			 


VIP_ret_t HOBUL_destroy_srq(
                         IN      HOBUL_hndl_t     hobul_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl
                         )
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  VIP_array_obj_t vobj;
  srq_info_t *srq_info_p;
  VIP_ret_t rc;
  HH_ret_t hh_rc;

  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  rc= VIP_array_erase_prepare(hobul_p->srq_info_db, srq_hndl, &vobj);
  if (rc != VIP_OK)  {
    MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array_erase_prepare (%d=%s)"), __func__, 
               rc, VAPI_strerror_sym(rc));
    return (rc == VIP_EBUSY) ? VIP_EBUSY : VIP_EINVAL_SRQ_HNDL;
  }
  srq_info_p= (srq_info_t *)vobj;

  rc= VIPKL_destroy_srq(VIP_RSCT_NULL_USR_CTX, hobul_p->vipkl_hndl, srq_info_p->vipkl_srq_hndl);
  if (rc != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed VIPKL_destroy_srq (%s) for srqn=0x%X vipkl_srq_hndl=0x%X"), __func__,
               VAPI_strerror_sym(rc), srq_info_p->hh_srq, srq_info_p->vipkl_srq_hndl);
    VIP_array_erase_undo(hobul_p->srq_info_db, srq_hndl);
    return rc;
  }

  hh_rc= HHUL_destroy_srq_done(hobul_p->hhul_hndl, srq_info_p->hhul_srq_hndl);
  if (hh_rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed HHUL_destroy_srq_done (%s) for srqn=0x%X"), __func__,
               HH_strerror_sym(hh_rc), srq_info_p->hh_srq);
    /* Nothing we can do - resource is already gone... */
  }

  VIP_array_erase_done(hobul_p->srq_info_db, srq_hndl, NULL);

  HOBUL_DEC_REF_CNT(hobul_p);

  FREE(srq_info_p);
  
  return VIP_OK;
}


VIP_ret_t HOBUL_alloc_qp( 
                          /*IN*/  HOBUL_hndl_t        hobul_hndl,
                          /*IN*/  VAPI_special_qp_t   qp_type,
                          /*IN*/  IB_port_t           port,
                          /*IN*/  VAPI_qp_init_attr_t *qp_init_attr_p,
                          /*IN*/  VAPI_qp_init_attr_ext_t *qp_ext_attr_p,
                          /*OUT*/ VAPI_qp_hndl_t      *qp_hndl_p,
                          /*OUT*/ VAPI_qp_num_t       *qpn_p,
                          /*OUT*/ VAPI_qp_cap_t       *qp_cap_p)
{
  VIP_ret_t             ret;
  HH_ret_t              hh_ret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  QPM_qp_init_attr_t    qpm_qp_init_attr;
  void                  *qp_ul_resources_p;
  HHUL_qp_init_attr_t   qp_init_attr;
  pd_info_t             *pd_info_p;
  cq_info_t             *rq_cq_info_p,*sq_cq_info_p;
  srq_info_t            *srq_info_p= NULL;
  qp_info_t             *qp_info_p;
  VIP_array_obj_t       vobj;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  HOBUL_INC_REF_CNT(hobul_p);
  pd_info_p= get_pd_info(hobul_p,qp_init_attr_p->pd_hndl);
  if (pd_info_p == NULL)  {HOBUL_DEC_REF_CNT(hobul_p); return VIP_EINVAL_PD_HNDL;}
  rq_cq_info_p= get_cq_info(hobul_p,qp_init_attr_p->rq_cq_hndl);
  if (rq_cq_info_p == NULL)  {HOBUL_DEC_REF_CNT(hobul_p); return VIP_EINVAL_CQ_HNDL;}
  sq_cq_info_p= get_cq_info(hobul_p,qp_init_attr_p->sq_cq_hndl);
  if (sq_cq_info_p == NULL)  {HOBUL_DEC_REF_CNT(hobul_p); return VIP_EINVAL_CQ_HNDL;}

  if (qp_ext_attr_p != NULL) {
    if (qp_ext_attr_p->srq_hndl != VAPI_INVAL_SRQ_HNDL) {
      ret= VIP_array_find_hold(hobul_p->srq_info_db, qp_ext_attr_p->srq_hndl, &vobj);
      if (ret != VIP_OK) {
        MTL_ERROR1(MT_FLFMT("%s: Invalid SRQ handle ("MT_ULONG_PTR_FMT")"), __func__, qp_ext_attr_p->srq_hndl);
        HOBUL_DEC_REF_CNT(hobul_p);
        return VIP_EINVAL_SRQ_HNDL;
      }
      srq_info_p= (srq_info_t*)vobj;
    }
  }
  /* RDD ? */

  qp_info_p= TMALLOC(qp_info_t);
  if (qp_info_p == NULL) {
    MTL_ERROR1(MT_FLFMT("Failed allocating memory for qp_info.\n"));
    ret= VIP_EAGAIN;
    goto malloc_fail;
  }
  
  /* allocate ul resources buffer */
  qp_ul_resources_p = (void*)MALLOC(hobul_p->qp_ul_resources_sz);
  if (qp_ul_resources_p == NULL)  {
    MTL_ERROR1(MT_FLFMT("Failed allocating memory for qp_ul_resources_p."));
    ret= VIP_EAGAIN;
    goto qp_ul_res_malloc_fail;
  }
  MTL_DEBUG4("%s: qp_ul_resources_sz=%lu\n", __func__,
             (unsigned long) hobul_p->qp_ul_resources_sz);

  /* Init. HHUL's QP attributes struct. */
  memset(&qp_init_attr, 0, sizeof(HHUL_qp_init_attr_t));
  memcpy(&(qp_init_attr.qp_cap), &(qp_init_attr_p->cap), sizeof(VAPI_qp_cap_t));
  qp_init_attr.rq_cq =  rq_cq_info_p->hhul_cq_hndl; 
  qp_init_attr.sq_cq =  sq_cq_info_p->hhul_cq_hndl; 
  qp_init_attr.sq_sig_type = qp_init_attr_p->sq_sig_type;  
  qp_init_attr.rq_sig_type = qp_init_attr_p->rq_sig_type;
  qp_init_attr.srq = 
    (srq_info_p != NULL) ? srq_info_p->hhul_srq_hndl: HHUL_INVAL_SRQ_HNDL; 
  qp_init_attr.pd = pd_info_p->hhul_pd_hndl;      
  qp_init_attr.ts_type = qp_init_attr_p->ts_type;      

  if (qp_type == VAPI_REGULAR_QP) {
    qp_init_attr.ts_type = qp_init_attr_p->ts_type;      
    hh_ret= HHUL_create_qp_prep(hobul_p->hhul_hndl, &qp_init_attr, 
                             &(qp_info_p->hhul_qp_hndl), &(qp_info_p->qp_cap), qp_ul_resources_p);    
  } else { /* special QP */
    /* ignore user's ts type, and set UD */
    qp_init_attr.ts_type = IB_TS_UD;
    hh_ret= HHUL_special_qp_prep(hobul_p->hhul_hndl, qp_type, port,&qp_init_attr,
                             &(qp_info_p->hhul_qp_hndl), &(qp_info_p->qp_cap), qp_ul_resources_p);    
  }
  if (hh_ret != HH_OK) {
    ret= (VIP_ret_t) hh_ret;
    goto create_qp_prep_fail;
  }

  /* Init. VIPKL's QP attributes struct. */
  qpm_qp_init_attr.srq_hndl = 
    (srq_info_p != NULL) ? srq_info_p->vipkl_srq_hndl: SRQM_INVAL_SRQ_HNDL; 
  qpm_qp_init_attr.rq_cq_hndl = rq_cq_info_p->vipkl_cq_hndl; 
  qpm_qp_init_attr.sq_cq_hndl = sq_cq_info_p->vipkl_cq_hndl; 
  qpm_qp_init_attr.sq_sig_type = qp_init_attr_p->sq_sig_type;  
  qpm_qp_init_attr.rq_sig_type = qp_init_attr_p->rq_sig_type;
  qpm_qp_init_attr.pd_hndl = pd_info_p->vipkl_pd_hndl;      
  qpm_qp_init_attr.ts_type = qp_init_attr_p->ts_type;      
  /* copy over the actual capabilities into the QPM-level initial attributes structure*/
  memcpy(&(qpm_qp_init_attr.qp_cap), &(qp_info_p->qp_cap), sizeof(VAPI_qp_cap_t));
  
  /* kernel level call */
  if (qp_type == VAPI_REGULAR_QP) {
    qpm_qp_init_attr.ts_type = qp_init_attr_p->ts_type;      
    ret = VIPKL_create_qp(VIP_RSCT_NULL_USR_CTX,
                          hobul_p->vipkl_hndl,
                          (VAPI_qp_hndl_t)(qp_info_p->hhul_qp_hndl),
                          hobul_p->async_hndl_ctx,
                          hobul_p->qp_ul_resources_sz,
                          qp_ul_resources_p, 
                          &qpm_qp_init_attr, 
                          &(qp_info_p->vipkl_qp_hndl), 
                          &(qp_info_p->qp_num)); 
  } else { /* special QP */
    /* ignore user's ts type, and set UD */
    qpm_qp_init_attr.ts_type = IB_TS_UD;      
    ret = VIPKL_get_special_qp(VIP_RSCT_NULL_USR_CTX,
                               hobul_p->vipkl_hndl, 
                               (VAPI_qp_hndl_t)(qp_info_p->hhul_qp_hndl),
                               hobul_p->async_hndl_ctx,
                               hobul_p->qp_ul_resources_sz,
                               qp_ul_resources_p, port,
                               qp_type,
                               &qpm_qp_init_attr, 
                               &(qp_info_p->vipkl_qp_hndl), 
                               &(qp_info_p->qp_num)); 
  }
  if (ret != VIP_OK) {
    /*if (ret == VIP_E2SMALL_BUF) ret = VIP_EAGAIN; ???*/
    goto create_qp_fail;
  }

  /* user level call */
  ret = HHUL_create_qp_done(hobul_p->hhul_hndl, qp_info_p->hhul_qp_hndl, (IB_wqpn_t) qp_info_p->qp_num, qp_ul_resources_p);
  if (ret != HH_OK) {
    goto create_qp_done_fail;
  }
  FREE(qp_ul_resources_p); 
  
  /* Init. what's left to init in qp_info */
  MOSAL_mutex_init(&(qp_info_p->modify_qp_mutex));
  qp_info_p->qp_type = qp_type;
  qp_info_p->priv_context= NULL;
  qp_info_p->ts = qp_init_attr_p->ts_type;
  qp_info_p->associated_srq= (srq_info_p != NULL) ? qp_ext_attr_p->srq_hndl: VAPI_INVAL_SRQ_HNDL;

  *qp_hndl_p= add_to_qp_info_db(hobul_p,qp_info_p);
  *qpn_p= qp_info_p->qp_num;
  memcpy(qp_cap_p, &(qp_info_p->qp_cap), sizeof(VAPI_qp_cap_t));

  return VIP_OK;

  create_qp_done_fail:
    VIPKL_destroy_qp(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl, qp_info_p->vipkl_qp_hndl,FALSE);
  create_qp_fail:
    HHUL_destroy_qp_done(hobul_p->hhul_hndl, qp_info_p->hhul_qp_hndl);
  create_qp_prep_fail:
    FREE(qp_ul_resources_p);
  qp_ul_res_malloc_fail:
    FREE(qp_info_p);
  malloc_fail:
    if (srq_info_p != NULL) VIP_array_find_release(hobul_p->srq_info_db, qp_ext_attr_p->srq_hndl);
    HOBUL_DEC_REF_CNT(hobul_p); 
    return ret;
}


VIP_ret_t HOBUL_destroy_qp( 
                            /*IN*/ HOBUL_hndl_t  hobul_hndl,
                           /*IN*/ VAPI_qp_hndl_t qp_hndl)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  HH_ret_t hhret;
  VIP_ret_t ret;
  call_result_t mt_rc;

  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;

  qp_info_p = remove_from_qp_info_db(hobul_p,qp_hndl);
  if ( qp_info_p == NULL )  return VIP_EINVAL_QP_HNDL;

  /* kernel level call */
  ret = VIPKL_destroy_qp(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl ,qp_info_p->vipkl_qp_hndl,FALSE);
  if (ret != VIP_OK) {
    /* rollback */
    add_to_qp_info_db(hobul_p, qp_info_p);
    return ret;
  }
  /* user resources cleanup */
  hhret = HHUL_destroy_qp_done(hobul_p->hhul_hndl, qp_info_p->hhul_qp_hndl);
  if (hhret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("Failed freeing HHUL's resources for qpn=0x%X"),qp_info_p->qp_num);
     /* Continueing anyway, since QP does not exist due to success of VIPKL_destroy_qp */
    ret= (VIP_ret_t)hhret;
  }

  mt_rc = MOSAL_mutex_free(&(qp_info_p->modify_qp_mutex));
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed MOSAL_mutex_free (%s)"),mtl_strerror_sym(mt_rc));
  }
  if (qp_info_p->associated_srq != VAPI_INVAL_SRQ_HNDL) {
    ret= VIP_array_find_release(hobul_p->srq_info_db, qp_info_p->associated_srq);
    if (ret != VIP_OK) {
      MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array_find_release for VAPI_srq_hndl="MT_ULONG_PTR_FMT), __func__,
                 qp_info_p->associated_srq);
    }
  }
  FREE(qp_info_p);
  HOBUL_DEC_REF_CNT(hobul_p); 
  return VIP_OK; /* Don't care if we failed anything beyond VIPKL_destroy_qp - the QP is gone */
}


VIP_ret_t HOBUL_modify_qp(
                HOBUL_hndl_t  hobul_hndl,
    /*IN*/      VAPI_qp_hndl_t        qp_hndl,
    /*IN*/      VAPI_qp_attr_t       *qp_attr_p,
    /*IN*/      VAPI_qp_attr_mask_t  *qp_attr_mask_p,
    /*OUT*/     VAPI_qp_cap_t        *qp_cap_p
)
{
  VIP_ret_t rc;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
    
  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= get_qp_info(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;

   /* Avoid modifying the same QP by 2 threads at once */
  if (MOSAL_mutex_acq(&(qp_info_p->modify_qp_mutex),TRUE) != MT_OK)  return VIP_EINTR; 

  rc= VIPKL_modify_qp(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,qp_info_p->vipkl_qp_hndl,qp_attr_mask_p,qp_attr_p);
  if (rc == VIP_OK) {
      if (*qp_attr_mask_p & QP_ATTR_QP_STATE ) {
            rc = HHUL_modify_qp_done(hobul_p->hhul_hndl,qp_info_p->hhul_qp_hndl, qp_attr_p->qp_state);
            if (rc != VIP_OK) {
                MTL_ERROR1(" HHUL_modify_qp_done failed \n");
            }
      }
  }
  
  MOSAL_mutex_rel(&(qp_info_p->modify_qp_mutex)); 

  /* TBD: currently we do not support modification of QP capabilities, 
   *      so we can take info from QP creation                         */
  memcpy(qp_cap_p,&(qp_info_p->qp_cap),sizeof(VAPI_qp_cap_t));
  return rc;
}


VIP_ret_t HOBUL_query_qp(
    /*IN*/      HOBUL_hndl_t          hobul_hndl,
    /*IN*/      VAPI_qp_hndl_t        qp_hndl,
    /*OUT*/     VAPI_qp_attr_t       *qp_attr_p,
    /*OUT*/     VAPI_qp_attr_mask_t  *qp_attr_mask_p,
    /*OUT*/     VAPI_qp_init_attr_t  *qp_init_attr_p,
    /*OUT*/     VAPI_qp_init_attr_ext_t *qp_init_attr_ext_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  QPM_qp_query_attr_t qpm_attr;
  VIP_ret_t rc;

  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= get_qp_info(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  if ( !qp_init_attr_p ) return VIP_EINVAL_PARAM;
  
  rc= VIPKL_query_qp(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,qp_info_p->vipkl_qp_hndl,&qpm_attr,qp_attr_mask_p);
  if (rc == VIP_OK) {
    memcpy(qp_attr_p,&(qpm_attr.qp_mod_attr),sizeof(VAPI_qp_attr_t));
    qp_init_attr_p->pd_hndl= vipkl2vapi_pd(hobul_p,qpm_attr.pd_hndl);
    qp_init_attr_p->ts_type= qpm_attr.ts_type;
    qp_init_attr_p->rq_cq_hndl= vipkl2vapi_cq(hobul_p,qpm_attr.rq_cq_hndl);
    qp_init_attr_p->rq_sig_type= qpm_attr.rq_sig_type;
    qp_init_attr_p->sq_cq_hndl= vipkl2vapi_cq(hobul_p,qpm_attr.sq_cq_hndl);
    qp_init_attr_p->sq_sig_type= qpm_attr.sq_sig_type;
    qp_init_attr_p->rdd_hndl = VAPI_INVAL_HNDL; /* TBD: RD not implemented */
    memcpy(&(qp_init_attr_p->cap),&(qpm_attr.qp_mod_attr.cap),sizeof(VAPI_qp_cap_t));

    if (qp_init_attr_ext_p != NULL) {
      qp_init_attr_ext_p->srq_hndl= qp_info_p->associated_srq;
    }
  }
  return rc;
}

VIP_ret_t HOBUL_k_sync_qp_state(
    /*IN*/      HOBUL_hndl_t  		hobul_hndl,
    /*IN*/      VAPI_qp_hndl_t      qp_hndl,
    /*IN*/      VAPI_qp_state_t     curr_state
)
{
  VIP_ret_t rc;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  
  MTL_TRACE1("inside HOBUL_k_sync_qp_state()\n");
    
  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= get_qp_info(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;

   /* Avoid modifying the same QP by 2 threads at once */
  if (MOSAL_mutex_acq(&(qp_info_p->modify_qp_mutex),TRUE) != MT_OK)  return VIP_EINTR; 

  rc = HHUL_modify_qp_done(hobul_p->hhul_hndl,qp_info_p->hhul_qp_hndl,curr_state);
  MTL_TRACE1("HOBUL_k_sync_qp_state: HHUL_modify_qp_done() returned %s.\n",VAPI_strerror_sym(rc));  
  
  MOSAL_mutex_rel(&(qp_info_p->modify_qp_mutex)); 

  return rc;
}



static inline MT_bool valid_sq_opcode(VAPI_ts_type_t ts_type,  VAPI_wr_opcode_t opcode)
{
  /* the following condition's correctness depneds on the fact that
     VAPI_NUM_OPCODES and VAPI_NUM_TS_TYPES have the highest numerical
     values amongst all values in the enums where they're defined */
  if ( (opcode>=VAPI_NUM_OPCODES) || (ts_type>=VAPI_NUM_TS_TYPES) ) {
    return FALSE;
  }

  return snd_matrix[ts_type][opcode];
}

VIP_ret_t HOBUL_post_sendq(/*IN*/ HOBUL_hndl_t   hobul_hndl,
                           /*IN*/ VAPI_qp_hndl_t  qp_hndl,
                           /*IN*/ VAPI_sr_desc_t *sr_desc_p)
{
  HH_ret_t hhret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= GET_QP_INFO(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  
  if (!valid_sq_opcode(qp_info_p->ts,sr_desc_p->opcode)) {
    return VAPI_EINVAL_OP;
  }
   
  MTPERF_TIME_START(HHUL_post_send_req);
  hhret = HHUL_post_send_req(hobul_p->hhul_hndl, (HHUL_qp_hndl_t)qp_hndl, sr_desc_p);
  MTPERF_TIME_END(HHUL_post_send_req);

  if (hhret != HH_OK) return (VIP_ret_t)hhret;
  return VIP_OK;
}

VIP_ret_t HOBUL_post_inline_sendq(/*IN*/ HOBUL_hndl_t   hobul_hndl,
                                  /*IN*/ VAPI_qp_hndl_t  qp_hndl,
                                  /*IN*/ VAPI_sr_desc_t *sr_desc_p)
{
  HH_ret_t hhret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= GET_QP_INFO(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  
  switch (sr_desc_p->opcode) {
    case VAPI_SEND:
    case VAPI_SEND_WITH_IMM: break;   /* accepted opcode */
    case VAPI_RDMA_WRITE:
    case VAPI_RDMA_WRITE_WITH_IMM: if (qp_info_p->ts != VAPI_TS_RAW && qp_info_p->ts != VAPI_TS_UD) 
                                        break;
    default: return VAPI_EINVAL_OP;
  }

  MTPERF_TIME_START(HHUL_post_inline_send_req);
  hhret = HHUL_post_inline_send_req(hobul_p->hhul_hndl, (HHUL_qp_hndl_t)qp_hndl, sr_desc_p);
  MTPERF_TIME_END(HHUL_post_inline_send_req);

  if (hhret != HH_OK) return (VIP_ret_t)hhret;
  return VIP_OK;
}


VIP_ret_t HOBUL_post_gsi_sendq(/*IN*/ HOBUL_hndl_t   hobul_hndl,
                               /*IN*/ VAPI_qp_hndl_t  qp_hndl,
                               /*IN*/ VAPI_sr_desc_t *sr_desc_p,
                               /*IN*/ VAPI_pkey_ix_t pkey_index)
{
  HH_ret_t hhret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= GET_QP_INFO(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  
  if (sr_desc_p->opcode != VAPI_SEND) {
    return VAPI_EINVAL_OP;
  }
  
  hhret = HHUL_post_gsi_send_req(hobul_p->hhul_hndl, (HHUL_qp_hndl_t)qp_hndl, sr_desc_p, 
                                 pkey_index);

  if (hhret != HH_OK) return (VIP_ret_t)hhret;
  return VIP_OK;
}

VIP_ret_t HOBUL_post_list_sendq(/*IN*/ HOBUL_hndl_t hobul_hndl,
                                /*IN*/ VAPI_qp_hndl_t qp_hndl,
                                /*IN*/ u_int32_t num_of_requests,
                                /*IN*/ VAPI_sr_desc_t *sr_desc_array)
{
  HH_ret_t hhret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  u_int32_t i;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= GET_QP_INFO(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  if (num_of_requests < 1) {
    MTL_ERROR2(MT_FLFMT("HOBUL_post_list_sendq invoked with 0 send requests"));
    return VIP_EINVAL_PARAM;
  }
  
  for (i= 0; i < num_of_requests; i++) {
    if (!valid_sq_opcode(qp_info_p->ts, sr_desc_array[i].opcode)) {
      return VAPI_EINVAL_OP;
    }
  }
   
  hhret = HHUL_post_send_reqs(hobul_p->hhul_hndl, (HHUL_qp_hndl_t)qp_hndl, num_of_requests,sr_desc_array);

  if (hhret != HH_OK) return (VIP_ret_t)hhret;
  return VIP_OK;
}

VIP_ret_t HOBUL_post_recieveq(/*IN*/ HOBUL_hndl_t   hobul_hndl,
                              /*IN*/ VAPI_qp_hndl_t  qp_hndl,
                              /*IN*/ VAPI_rr_desc_t *rr_desc_p)
{
  HH_ret_t hhret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= GET_QP_INFO(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;

  if (rr_desc_p->opcode != VAPI_RECEIVE) {
    MTL_ERROR2(MT_FLFMT("receive req. for qpn=0x%X with opcode != VAPI_RECEIVE"),qp_info_p->qp_num);
    return VAPI_EINVAL_OP;
  }
  
  hhret = HHUL_post_recv_req(hobul_p->hhul_hndl, 
    (HHUL_qp_hndl_t)qp_hndl, rr_desc_p); 
  
  if (hhret != HH_OK) return (VIP_ret_t)hhret;
  return VIP_OK;
}


VIP_ret_t HOBUL_post_list_recieveq(/*IN*/ HOBUL_hndl_t hobul_hndl,
                                   /*IN*/ VAPI_qp_hndl_t qp_hndl,
                                   /*IN*/ u_int32_t num_of_requests,
                                   /*IN*/ VAPI_rr_desc_t *rr_desc_array)
{
  HH_ret_t hhret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
  u_int32_t i;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= GET_QP_INFO(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;

  for (i= 0; i < num_of_requests; i++) {
    if (rr_desc_array[i].opcode != VAPI_RECEIVE) {
      MTL_ERROR2(MT_FLFMT("receive req. for qpn=0x%X with opcode != VAPI_RECEIVE"),qp_info_p->qp_num);
      return VAPI_EINVAL_OP;
    }
  }
  
  hhret = HHUL_post_recv_reqs(hobul_p->hhul_hndl,(HHUL_qp_hndl_t)qp_hndl, 
                              num_of_requests,rr_desc_array); 
  
  if (hhret != HH_OK) return (VIP_ret_t)hhret;
  return VIP_OK;
}


VIP_ret_t HOBUL_post_srq(
                       IN HOBUL_hndl_t          hobul_hndl,
                       IN VAPI_srq_hndl_t       srq_hndl,
                       IN u_int32_t             rwqe_num,
                       IN VAPI_rr_desc_t       *rwqe_array,
                       OUT u_int32_t           *rwqe_posted_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  VIP_array_obj_t vobj;
  srq_info_t *srq_info_p;
  VIP_ret_t rc;
  HH_ret_t hh_rc;
  u_int32_t rwqe_posted_tmp;

  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  if ((rwqe_array == NULL) && (rwqe_num != 0))  return VIP_EINVAL_PARAM;
  if (rwqe_posted_p == NULL)  rwqe_posted_p= &rwqe_posted_tmp; /* allow ignoring output */

  rc= VIP_array_find_hold(hobul_p->srq_info_db, srq_hndl, &vobj);
  if (rc != VIP_OK)  {
    MTL_ERROR1(MT_FLFMT("%s: Failed finding given SRQ handle "MT_ULONG_PTR_FMT), __func__, srq_hndl);
    return VIP_EINVAL_SRQ_HNDL;
  }
  srq_info_p= (srq_info_t *)vobj;

  hh_rc= HHUL_post_srq(hobul_p->hhul_hndl, srq_info_p->hhul_srq_hndl, rwqe_num, rwqe_array, 
                       rwqe_posted_p); 
  
  VIP_array_find_release(hobul_p->srq_info_db, srq_hndl);

  return (VIP_ret_t)hh_rc;
}


VIP_ret_t  HOBUL_create_av(/*IN*/ HOBUL_hndl_t        hobul_hndl,
                           /*IN*/ VAPI_pd_hndl_t      pd_hndl,
                           /*IN*/ VAPI_ud_av_t        *av_data_p, 
                           /*OUT*/ HHUL_ud_av_hndl_t  *av_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  pd_info_t *pd_info_p;
  VIP_ret_t ret;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  HOBUL_INC_REF_CNT(hobul_p);
  pd_info_p= get_pd_info(hobul_p,pd_hndl);
  if (pd_info_p == NULL)  {HOBUL_DEC_REF_CNT(hobul_p); return VIP_EINVAL_PD_HNDL;}

  ret= HHUL_create_ud_av(hobul_p->hhul_hndl, 
                           (HHUL_pd_hndl_t)pd_hndl, 
                           av_data_p,av_p);
  if (ret != HH_OK)  {
    MTL_ERROR1(MT_FLFMT("%s: HHUL_create_ud_av returned %s (%d)"),__func__,HH_strerror_sym(ret),ret);
    HOBUL_DEC_REF_CNT(hobul_p);
  }
  return ret;
}


VIP_ret_t  HOBUL_destroy_av (/*IN*/ HOBUL_hndl_t      hobul_hndl, 
                             /*IN*/ HHUL_ud_av_hndl_t av)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  VIP_ret_t ret;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  
  ret= HHUL_destroy_ud_av(hobul_p->hhul_hndl,av);
  if (ret == VIP_OK)  {HOBUL_DEC_REF_CNT(hobul_p);} 
  return ret;
}


VIP_ret_t  HOBUL_get_av_data (/*IN*/  HOBUL_hndl_t      hobul_hndl,
                              /*IN*/  HHUL_ud_av_hndl_t av,
                              /*OUT*/ VAPI_ud_av_t      *av_data_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  
  return HHUL_query_ud_av(hobul_p->hhul_hndl,av,av_data_p);
}

VIP_ret_t  HOBUL_set_av_data (/*IN*/ HOBUL_hndl_t      hobul_hndl,
                              /*IN*/ HHUL_ud_av_hndl_t av, 
                              /*IN*/ VAPI_ud_av_t      *av_data_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  
  return HHUL_modify_ud_av (hobul_p->hhul_hndl,av,av_data_p);
}

VIP_ret_t HOBUL_alloc_pd (  /*IN*/ HOBUL_hndl_t hobul_hndl,
                          /*IN*/ u_int32_t  max_num_avs,
                          /*IN*/ MT_bool for_sqp,   /* TRUE if PD is for sqp */
                          /*OUT*/ VAPI_pd_hndl_t *pd_hndl_p)
{
  VIP_ret_t         ret;
  HH_ret_t          hh_ret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  pd_info_t *pd_info_p;
  HH_pd_hndl_t       hh_pd;
  VAPI_pd_hndl_t     local_pd_hndl;
  void              *pd_ul_resources_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  if ((max_num_avs != EVAPI_DEFAULT_AVS_PER_PD) && (max_num_avs > hobul_p->hca_caps.max_ah_num)) {
      MTL_ERROR1(MT_FLFMT("%s: number of AVs requested (0x%x) is greater than number supported by HCA (0x%x)"),
                 __func__, max_num_avs, hobul_p->hca_caps.max_ah_num);
      return VIP_EINVAL_PARAM;
  }
  HOBUL_INC_REF_CNT(hobul_p);
  
  pd_info_p= TMALLOC(pd_info_t);
  if (pd_info_p == NULL) {
    MTL_ERROR1(MT_FLFMT("Failed memory allocation for pd_info_p"));
    ret= VIP_EAGAIN;
    goto malloc_fail;
  }

  /* allocate user-level resources */
  pd_ul_resources_p = (void*)MALLOC(hobul_p->pd_ul_resources_sz);
  if (pd_ul_resources_p == NULL) {
    ret= VIP_EAGAIN;
    goto malloc_ul_res_buf_fail;
  }
  MTL_DEBUG4("%s: allocated user resources buffer. size=%ld, addr=%p, max_avs=0x%x\n", __func__, 
             (long)hobul_p->pd_ul_resources_sz, pd_ul_resources_p, max_num_avs);

  hh_ret = HHUL_alloc_pd_avs_prep(hobul_p->hhul_hndl, max_num_avs, (for_sqp ? PD_FOR_SQP : PD_NO_FLAGS),
                                 &(pd_info_p->hhul_pd_hndl), pd_ul_resources_p);
  if (hh_ret != HH_OK) {
    ret= (VIP_ret_t) hh_ret;
    goto alloc_pd_prep_fail;
  }

  /* kernel level call.  The protection context obtained here will be used only in the case that */
  /* we are running entirely in kernel mode, with no kernel wrapper for VIPKL. */
  ret = VIPKL_create_pd(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl, MOSAL_get_kernel_prot_ctx(), 
          hobul_p->pd_ul_resources_sz, pd_ul_resources_p,&(pd_info_p->vipkl_pd_hndl),&hh_pd);
  if (ret != VIP_OK)  {
      MTL_ERROR1(MT_FLFMT("%s: VIPKL_create_pd failed: %s (%d)"),
                 __func__, VAPI_strerror_sym(ret), ret);
      goto create_pd_fail;
  }
  
  hh_ret = HHUL_alloc_pd_done(hobul_p->hhul_hndl,pd_info_p->hhul_pd_hndl ,hh_pd, pd_ul_resources_p);

  if (hh_ret != HH_OK) {
    ret= (VIP_ret_t) hh_ret;
    goto create_pd_done_fail;
  }

  /* return values */
  ret = add_to_pd_rev_info_db(hobul_p,pd_info_p);
  if ( ret != VIP_OK ) {
      ret = VAPI_EAGAIN;
      goto add_to_pd_rev_info_db_fail;
  }

  local_pd_hndl = add_to_pd_info_db(hobul_p,pd_info_p);
  if (local_pd_hndl == (VAPI_pd_hndl_t) VAPI_INVAL_HNDL) {
      ret = VAPI_EAGAIN;
      goto add_to_pd_info_db_fail;
  }
   *pd_hndl_p  = local_pd_hndl;
  
  FREE(pd_ul_resources_p);
  return VIP_OK;
  
  add_to_pd_info_db_fail:
    remove_from_pd_rev_info_db(hobul_p, (PDM_pd_hndl_t)*pd_hndl_p);
  add_to_pd_rev_info_db_fail:
    HHUL_free_pd_prep (hobul_p->hhul_hndl,pd_info_p->hhul_pd_hndl,FALSE);
  create_pd_done_fail:
    VIPKL_destroy_pd(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl ,pd_info_p->vipkl_pd_hndl);
  create_pd_fail:
    HHUL_free_pd_done(hobul_p->hhul_hndl,pd_info_p->hhul_pd_hndl);
  alloc_pd_prep_fail:
    FREE(pd_ul_resources_p);
  malloc_ul_res_buf_fail:
    FREE(pd_info_p);
  malloc_fail:
    HOBUL_DEC_REF_CNT(hobul_p);
    return ret;
}


VIP_ret_t HOBUL_destroy_pd(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_pd_hndl_t pd_hndl)
{
  VIP_ret_t ret;
  HH_ret_t  hhret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  pd_info_t *pd_info_p;
  
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;

  pd_info_p = remove_from_pd_info_db(hobul_p,pd_hndl);
  if ( pd_info_p == NULL ) return VIP_EINVAL_PD_HNDL;
  if ( remove_from_pd_rev_info_db(hobul_p,pd_info_p->vipkl_pd_hndl) == (VAPI_pd_hndl_t)VAPI_INVAL_HNDL ) {
    MTL_ERROR1(MT_FLFMT("%s: inconsistent pd_info_t db. pd_hndl="MT_ULONG_PTR_FMT), __func__, pd_hndl);
  }

  hhret = HHUL_free_pd_prep(hobul_p->hhul_hndl, pd_info_p->hhul_pd_hndl, 0);
  if (hhret != HH_OK) {
      if (hhret != HH_EBUSY) {MTL_ERROR1(MT_FLFMT("HHUL_free_pd_prep failed (%s)."),HH_strerror_sym(hhret));}
      /* rollback */
      add_to_pd_rev_info_db(hobul_p, pd_info_p);
      add_to_pd_info_db(hobul_p, pd_info_p);
      switch(hhret) {
      case  HH_EBUSY:
          ret = VIP_EBUSY;
          break;
      case HH_EINVAL:
          ret = VIP_EINVAL_PARAM;
          break;
      case HH_EINVAL_PD_HNDL:
          ret = VIP_EINVAL_PD_HNDL;
          break;
      default:
          ret = VIP_EGEN;
          break;
      }
      return ret;
  }
  ret = VIPKL_destroy_pd(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl, pd_info_p->vipkl_pd_hndl);
  if (ret != VIP_OK) {
    /* rollback */
    if ((hhret=HHUL_free_pd_prep(hobul_p->hhul_hndl, pd_info_p->hhul_pd_hndl, TRUE)) != HH_OK) {
        MTL_ERROR1(MT_FLFMT("HHUL_free_pd_prep UNDO failed (%s)."),HH_strerror_sym(hhret));
        return VIP_EGEN;
    }

    add_to_pd_rev_info_db(hobul_p, pd_info_p);
    add_to_pd_info_db(hobul_p, pd_info_p);
    return ret;
  }
  
  hhret = HHUL_free_pd_done(hobul_p->hhul_hndl, pd_info_p->hhul_pd_hndl);
  if (hhret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("HHUL_free_pd_done failed (%s)."),HH_strerror_sym(hhret));
    /* Continue anyway (PD was freed already by the privileged call) */
  }

  FREE(pd_info_p);
  HOBUL_DEC_REF_CNT(hobul_p);
  return VIP_OK;
} /* HOBUL_destroy_pd */



/*************************************************************************/
VIP_ret_t  HOBUL_alloc_mw(
  /* IN  */ HOBUL_hndl_t     hobul_hndl,
  /* IN  */ VAPI_pd_hndl_t   pd,
  /* OUT */ VAPI_mw_hndl_t*  mw_hndl_p,
  /* OUT */ VAPI_rkey_t*     rkey_p
)
{
  VIP_ret_t         ret;
  HH_ret_t          hh_ret;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  pd_info_t *pd_info_p;
  mw_info_t         *mw_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  HOBUL_INC_REF_CNT(hobul_p);
  pd_info_p= get_pd_info(hobul_p,pd);
  if (pd_info_p == NULL)  {HOBUL_DEC_REF_CNT(hobul_p); return VIP_EINVAL_PD_HNDL;}
  
  mw_info_p= TMALLOC(mw_info_t);
  if (mw_info_p == NULL) {
    MTL_ERROR1(MT_FLFMT("Failed memory allocation for mw_info_p"));
    ret= VIP_EAGAIN;
    goto malloc_fail;
  }

  ret= VIPKL_create_mw(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,pd_info_p->vipkl_pd_hndl,&(mw_info_p->initial_rkey));
  if (ret != VIP_OK)  goto create_mw_fail;
  
  hh_ret= HHUL_alloc_mw(hobul_p->hhul_hndl,mw_info_p->initial_rkey,&(mw_info_p->hhul_mw_hndl));
  if (hh_ret != HH_OK) {
    ret= (VIP_ret_t) hh_ret;
    goto hhul_alloc_mw_fail;
  }

  mw_info_p->pd= pd;

  /* return values */
  *rkey_p = mw_info_p->initial_rkey;
  *mw_hndl_p = add_to_mw_info_db(hobul_p,mw_info_p);
  return VIP_OK;

  hhul_alloc_mw_fail:  
    VIPKL_destroy_mw(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,mw_info_p->initial_rkey);
  create_mw_fail:   
    FREE(mw_info_p);
  malloc_fail:
    HOBUL_DEC_REF_CNT(hobul_p);
    return ret;
} /* HOBUL_alloc_mw */


/*************************************************************************/
VIP_ret_t HOBUL_query_mw(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_mw_hndl_t    mw_hndl,
  /* OUT */ VAPI_rkey_t*      rkey_p,
  /* OUT */ VAPI_pd_hndl_t*   pd_p
)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  mw_info_t         *mw_info_p;
  VIP_ret_t         ret;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  mw_info_p= get_mw_info(hobul_p,mw_hndl);
  if (mw_info_p == NULL)  return VIP_EINVAL_MW_HNDL;

  MTL_TRACE1("%s: -UL- called for index 0x%x", __func__,mw_info_p->initial_rkey);
  ret= VIPKL_query_mw(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,mw_info_p->initial_rkey,rkey_p);
  *pd_p= mw_info_p->pd; /* This info. is static so we can obtain it from the init. data */
  return ret;
} /* HOBUL_query_mw */


/*************************************************************************/
VIP_ret_t HOBUL_bind_mw(
  /* IN  */ HOBUL_hndl_t            hobul_hndl,
  /* IN  */ VAPI_mw_hndl_t          mw_hndl,
  /* IN  */ const VAPI_mw_bind_t*   bind_prop_p,
  /* IN  */ VAPI_qp_hndl_t          qp_hndl, 
  /* IN  */ VAPI_wr_id_t            id,
  /* IN  */ VAPI_comp_type_t        comp_type,
  /* OUT */ VAPI_rkey_t*            new_rkey_p
)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  mw_info_t         *mw_info_p;
  qp_info_t         *qp_info_p;
  HHUL_mw_bind_t  prop;
  HH_ret_t        hhul_rc;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  mw_info_p= GET_MW_INFO(hobul_p,mw_hndl);
  if (mw_info_p == NULL)  return VIP_EINVAL_MW_HNDL;
  qp_info_p= GET_QP_INFO(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  
  /* set HHUL's bind struct. */
  prop.mr_lkey   = bind_prop_p->mr_lkey;
  prop.start     = bind_prop_p->start;
  prop.size      = bind_prop_p->size;
  prop.acl       = bind_prop_p->acl;
  prop.qp        = qp_info_p->hhul_qp_hndl;
  prop.id        = id;
  prop.comp_type = comp_type;

  hhul_rc = HHUL_bind_mw(hobul_p->hhul_hndl, mw_info_p->hhul_mw_hndl, &prop, new_rkey_p);
  if (hhul_rc != HH_OK)
  {
    MTL_ERROR1(MT_FLFMT("HOBUL_bind_mw: rc=%d=%s"), hhul_rc, HH_strerror_sym(hhul_rc));
    return (hhul_rc < VAPI_ERROR_MAX ? hhul_rc : VAPI_EGEN);
  }

  return VIP_OK;
} /* HOBUL_bind_mw */


/*************************************************************************/
VIP_ret_t HOBUL_dealloc_mw(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_mw_hndl_t    mw_hndl
)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  mw_info_t         *mw_info_p;
  HH_ret_t        hhret;
  VIP_ret_t         ret;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  mw_info_p = remove_from_mw_info_db(hobul_p,mw_hndl);
  if (mw_info_p == NULL)  return VIP_EINVAL_MW_HNDL;

  ret = VIPKL_destroy_mw(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl, mw_info_p->initial_rkey);
  if ( ret != VIP_OK ) {
    add_to_mw_info_db(hobul_p, mw_info_p);
    return ret;
  }
  hhret = HHUL_free_mw(hobul_p->hhul_hndl, mw_info_p->hhul_mw_hndl);
  if (hhret != HH_OK)
  {
    MTL_ERROR1(MT_FLFMT("HOBUL_dealloc_mw: rc=%d=%s"), hhret, HH_strerror_sym(hhret));
    ret= (hhret < VAPI_ERROR_MAX ? (VIP_ret_t)hhret : VIP_ERROR);
    /* Continue anyway - VIPKL_destroy was successfull */
  }

  FREE(mw_info_p);
  HOBUL_DEC_REF_CNT(hobul_p);
  return VIP_OK;
} /* HOBUL_dealloc_mw */


/*************************************************************************/
/*************************************************************************/
VIP_ret_t HOBUL_set_priv_context4qp(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_qp_hndl_t       qp,
  /* IN  */   void *               priv_context)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t         *qp_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= get_qp_info(hobul_p,qp);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  qp_info_p->priv_context= priv_context;
  return VIP_OK;
}

/*************************************************************************/
VIP_ret_t HOBUL_get_priv_context4qp(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_qp_hndl_t       qp,
  /* OUT */   void **             priv_context_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t         *qp_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= get_qp_info(hobul_p,qp);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;
  *priv_context_p= qp_info_p->priv_context;
  return VIP_OK;
}


/*************************************************************************/
VIP_ret_t HOBUL_set_priv_context4cq(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_cq_hndl_t       cq,
  /* IN  */   void *               priv_context)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t         *cq_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= get_cq_info(hobul_p,cq);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  cq_info_p->priv_context= priv_context;
  return VIP_OK;
}


/*************************************************************************/
VIP_ret_t HOBUL_get_priv_context4cq(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_cq_hndl_t       cq,
  /* OUT */   void **             priv_context_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t         *cq_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= get_cq_info(hobul_p,cq);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  *priv_context_p= cq_info_p->priv_context;
  return VIP_OK;
}



VIP_ret_t HOBUL_get_vendor_info(/*IN*/ HOBUL_hndl_t hobul_hndl,
                                 /*OUT*/ VAPI_hca_vendor_t *vendor_info_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;

  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  if (vendor_info_p == NULL) return VIP_EINVAL_PARAM;

  vendor_info_p->vendor_id= hobul_p->hhul_hndl->vendor_id;
  vendor_info_p->vendor_part_id= hobul_p->hhul_hndl->dev_id;
  vendor_info_p->hw_ver= hobul_p->hhul_hndl->hw_ver;
  vendor_info_p->fw_ver= hobul_p->hhul_hndl->fw_ver;

  return VIP_OK;
}

VIP_ret_t HOBUL_vapi2vipkl_pd(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ VAPI_pd_hndl_t vapi_pd,
                              /*OUT*/ PDM_pd_hndl_t *vipkl_pd_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  pd_info_t         *pd_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  pd_info_p= get_pd_info(hobul_p,vapi_pd);
  if (pd_info_p == NULL)  return VIP_EINVAL_PD_HNDL;

  *vipkl_pd_p= pd_info_p->vipkl_pd_hndl;
  return VIP_OK;
}


VIP_ret_t HOBUL_vipkl2vapi_pd(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ PDM_pd_hndl_t vipkl_pd,
                              /*OUT*/ VAPI_pd_hndl_t *vapi_pd_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  *vapi_pd_p= vipkl2vapi_pd(hobul_p,vipkl_pd);
  return (*vapi_pd_p == VAPI_INVAL_HNDL) ? VIP_EINVAL_PD_HNDL : (VIP_ret_t)VIP_OK;
}


VIP_ret_t HOBUL_vapi2vipkl_cq(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ VAPI_cq_hndl_t vapi_cq,
                              /*OUT*/ CQM_cq_hndl_t *vipkl_cq_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t         *cq_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= get_cq_info(hobul_p,vapi_cq);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;

  *vipkl_cq_p= cq_info_p->vipkl_cq_hndl;
  return VIP_OK;
}


VIP_ret_t HOBUL_vapi2vipkl_qp(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ VAPI_qp_hndl_t vapi_qp,
                              /*OUT*/ QPM_qp_hndl_t *vipkl_qp_p)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t         *qp_info_p;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= get_qp_info(hobul_p,vapi_qp);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;

  *vipkl_qp_p= qp_info_p->vipkl_qp_hndl;
  return VIP_OK;
}

/************* VIPKL_EQ functions ***************/
VIP_ret_t HOBUL_evapi_set_comp_eventh(
  /*IN*/HOBUL_hndl_t                     hobul_hndl,
  /*IN*/VAPI_cq_hndl_t                   cq_hndl,
  /*IN*/VAPI_completion_event_handler_t  completion_handler,
  /*IN*/void *                           private_data)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  VIP_ret_t ret;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  if (completion_handler == NULL) return VIP_EINVAL_PARAM;
  
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  
  
  if (completion_handler == EVAPI_POLL_CQ_UNBLOCK_HANDLER)  {/* handler for poll CQ unblocking */
  
    ret= VIPKL_cqblk_alloc_ctx(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,cq_info_p->vipkl_cq_hndl,
                               &(cq_info_p->cq_block_hndl));
  
    if (ret != VIP_OK)  cq_info_p->cq_block_hndl= VIPKL_CQBLK_INVAL_HNDL;
    return ret;
  }
  /* Else: regular handler */
#ifdef MT_KERNEL
  return VIPKL_bind_evapi_completion_event_handler(hobul_p->vipkl_hndl, cq_info_p->vipkl_cq_hndl, 
                                                   completion_handler, private_data);
#else
  return VIPKL_EQ_evapi_set_comp_eventh(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,
                                        hobul_p->pollt_ctx[VIPKL_EQ_COMP_EVENTH].vipkl_eq,
                                        cq_info_p->vipkl_cq_hndl,
                                        completion_handler,private_data);
#endif
}

VIP_ret_t HOBUL_evapi_clear_comp_eventh(
  /*IN*/HOBUL_hndl_t                     hobul_hndl,
  /*IN*/VAPI_cq_hndl_t                   cq_hndl)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
  VIP_ret_t ret;

  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= GET_CQ_INFO(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  
  if (cq_info_p->cq_block_hndl != VIPKL_CQBLK_INVAL_HNDL) {
    ret= VIPKL_cqblk_free_ctx(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,cq_info_p->cq_block_hndl);
    if (ret == VIP_OK)  cq_info_p->cq_block_hndl= VIPKL_CQBLK_INVAL_HNDL;
    return ret;
  }
  
  
  /* Else: regular handler */
#ifdef MT_KERNEL
  return VIPKL_bind_evapi_completion_event_handler(hobul_p->vipkl_hndl,cq_info_p->vipkl_cq_hndl,
                                                   NULL, NULL);
#else
  return VIPKL_EQ_evapi_clear_comp_eventh(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,
                                          hobul_p->pollt_ctx[VIPKL_EQ_COMP_EVENTH].vipkl_eq,
                                          cq_info_p->vipkl_cq_hndl);
#endif
}


VIP_ret_t HOBUL_set_async_event_handler(
                                  /*IN*/  HOBUL_hndl_t                 hobul_hndl,
                                  /*IN*/  VAPI_async_event_handler_t   handler,
                                  /*IN*/  void*                        private_data,
                                  /*OUT*/ EVAPI_async_handler_hndl_t  *async_handler_hndl_p)
{
  const HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;

#ifdef MT_KERNEL
  return VIPKL_set_async_event_handler(hobul_p->vipkl_hndl, hobul_p->async_hndl_ctx,
                                       handler, private_data, async_handler_hndl_p);
#else
  return VIPKL_EQ_set_async_event_handler(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,
                                          hobul_p->pollt_ctx[VIPKL_EQ_ASYNC_EVENTH].vipkl_eq,
                                          handler,private_data,async_handler_hndl_p);
#endif
}


VIP_ret_t HOBUL_clear_async_event_handler(
                               /*IN*/ HOBUL_hndl_t                hobul_hndl, 
                               /*IN*/ EVAPI_async_handler_hndl_t async_handler_hndl)
{
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;

#ifdef MT_KERNEL
  return VIPKL_clear_async_event_handler(hobul_p->vipkl_hndl, hobul_p->async_hndl_ctx,
                                         async_handler_hndl);
#else
  return VIPKL_EQ_clear_async_event_handler(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,
                                            hobul_p->pollt_ctx[VIPKL_EQ_ASYNC_EVENTH].vipkl_eq,
                                            async_handler_hndl);
#endif
}

/****************************************************************************************
 *             Private functions  (DBs access)
 ****************************************************************************************/

/* "Template" for getting info struct. via the db/list (i.e. validate handle) */
/* This is very slow for this implementation and should not be used for the data path functions */
#define GET_INFO(hobul_p,vapi_hndl,hndl_type) {                       \
  hndl_type##_info_t *cur_info_p;                                     \
  VIP_ret_t          vret;                                            \
  vret=VIP_hashp2p_find(hobul_p->hndl_type##_info_db,               \
                       (VIP_hashp2p_key_t) vapi_hndl,               \
                       (VIP_hashp2p_value_t *) &cur_info_p);        \
  if (vret != VIP_OK) {                                             \
      cur_info_p = NULL;                                            \
  }                                                                 \
  return cur_info_p;                                                  \
}

/* Slow lookup in order to validate given handle */
static inline pd_info_t* get_pd_info(HOBUL_t *hobul_p, VAPI_pd_hndl_t pd) 
  GET_INFO(hobul_p,pd,pd)
static inline cq_info_t* get_cq_info(HOBUL_t *hobul_p, VAPI_cq_hndl_t cq) 
  GET_INFO(hobul_p,cq,cq)
static inline qp_info_t* get_qp_info(HOBUL_t *hobul_p, VAPI_qp_hndl_t qp) 
  GET_INFO(hobul_p,qp,qp)
static inline mw_info_t* get_mw_info(HOBUL_t *hobul_p, VAPI_mw_hndl_t mw) 
  GET_INFO(hobul_p,mw,mw)


/* "Template" for adding an object to info_db */
#define ADD_TO_INFO_DB(hobul_p,info_obj_p,obj_type) {       \
  VIP_ret_t          vret;                                            \
  vret = VIP_hashp2p_insert(hobul_p->obj_type##_info_db,           \
                (VIP_hashp2p_key_t)(MT_ulong_ptr_t) (((obj_type##_info_t *)info_obj_p)->hhul_##obj_type##_hndl), \
                (VIP_hashp2p_value_t)(MT_ulong_ptr_t) info_obj_p);                    \
  if (vret != VIP_OK) {                                             \
        return (VAPI_##obj_type##_hndl_t)VAPI_INVAL_HNDL;                                                  \
  }                                                                 \
  return (VAPI_##obj_type##_hndl_t)(((obj_type##_info_t *)info_obj_p)->hhul_##obj_type##_hndl);/* The handle is the info pointer for this imp. */\
}

static inline VAPI_pd_hndl_t add_to_pd_info_db(HOBUL_t *hobul_p, pd_info_t *pd_info_p)
  ADD_TO_INFO_DB(hobul_p,pd_info_p,pd)
static inline VAPI_cq_hndl_t add_to_cq_info_db(HOBUL_t *hobul_p, cq_info_t *cq_info_p)
  ADD_TO_INFO_DB(hobul_p,cq_info_p,cq)
static inline VAPI_qp_hndl_t add_to_qp_info_db(HOBUL_t *hobul_p, qp_info_t *qp_info_p)
  ADD_TO_INFO_DB(hobul_p,qp_info_p,qp)
static inline VAPI_mw_hndl_t add_to_mw_info_db(HOBUL_t *hobul_p, mw_info_t *mw_info_p)
  ADD_TO_INFO_DB(hobul_p,mw_info_p,mw)


/* "Template" for removing of an object info_db */
#define REMOVE_FROM_INFO_DB(hobul_p,obj_vapi_hndl,obj_type) {                   \
  obj_type##_info_t *cur_p;                                                     \
  VIP_ret_t rc;                                                                 \
                                                                                \
  rc = VIP_hashp2p_erase(hobul_p->obj_type##_info_db,                           \
                    (VIP_hashp2p_key_t) obj_vapi_hndl,                          \
                    (VIP_hashp2p_value_t *) &cur_p);                            \
  if ( rc != VIP_OK ) return NULL;                                              \
  return cur_p;                                                                 \
}

static inline pd_info_t * remove_from_pd_info_db(HOBUL_t *hobul_p, VAPI_pd_hndl_t pd_hndl)
  REMOVE_FROM_INFO_DB(hobul_p,pd_hndl,pd)
static inline cq_info_t * remove_from_cq_info_db(HOBUL_t *hobul_p, VAPI_cq_hndl_t cq_hndl)
  REMOVE_FROM_INFO_DB(hobul_p,cq_hndl,cq)
static inline qp_info_t * remove_from_qp_info_db(HOBUL_t *hobul_p, VAPI_qp_hndl_t qp_hndl)
  REMOVE_FROM_INFO_DB(hobul_p,qp_hndl,qp)
static inline mw_info_t * remove_from_mw_info_db(HOBUL_t *hobul_p, VAPI_mw_hndl_t mw_hndl)
  REMOVE_FROM_INFO_DB(hobul_p,mw_hndl,mw)
/******************************************************************************/
/* reverse lookup db maintenance                                              */
/******************************************************************************/
#define ADD_TO_REV_INFO_DB(hobul_p,info_obj_p,obj_type) {             \
  return VIP_hashp_insert(hobul_p->obj_type##_rev_info_db,            \
              (VIP_hashp_key_t)(MT_ulong_ptr_t) (((obj_type##_info_t *)info_obj_p)->vipkl_##obj_type##_hndl),  \
              (VIP_hashp_value_t)(MT_ulong_ptr_t) (((obj_type##_info_t *)info_obj_p)->hhul_##obj_type##_hndl));\
}

static inline VIP_ret_t add_to_pd_rev_info_db(HOBUL_t *hobul_p, pd_info_t *pd_info_p)
  ADD_TO_REV_INFO_DB(hobul_p,pd_info_p,pd)
static inline VIP_ret_t add_to_cq_rev_info_db(HOBUL_t *hobul_p, cq_info_t *cq_info_p)
  ADD_TO_REV_INFO_DB(hobul_p,cq_info_p,cq)

/* "Template" for removing of an object info_db */
#define REMOVE_FROM_REV_INFO_DB(hobul_p,obj_vipkl_hndl,obj_type) {        \
  VAPI_##obj_type##_hndl_t obj_hndl;                                      \
  VIP_common_ret_t  vret;                                                 \
                                                                          \
  vret = VIP_hashp_erase(hobul_p->obj_type##_rev_info_db,                 \
                    (VIP_hashp_key_t) obj_vipkl_hndl,                     \
                    (VIP_hashp_value_t *) &obj_hndl);                     \
  if (vret != VIP_OK) {                                                   \
    obj_hndl = VAPI_INVAL_HNDL;                                           \
  }                                                                       \
  return obj_hndl;                                                        \
}
static inline VAPI_pd_hndl_t remove_from_pd_rev_info_db(HOBUL_t *hobul_p, PDM_pd_hndl_t pd_hndl)
  REMOVE_FROM_REV_INFO_DB(hobul_p,pd_hndl,pd)
static inline VAPI_cq_hndl_t remove_from_cq_rev_info_db(HOBUL_t *hobul_p, CQM_cq_hndl_t cq_hndl)
  REMOVE_FROM_REV_INFO_DB(hobul_p,cq_hndl,cq)



/* Template for reverse mapping from VIPKL's handle to VAPI's */
#define VIPKL2VAPI(hobul_p,vipkl_hndl,obj_type) {                         \
  /* Non-efficient reverse lookup VAPI handle based on VIPKL's handle */  \
  VAPI_##obj_type##_hndl_t obj_hndl;                                      \
  VIP_ret_t          vret;                                                \
                                                                          \
  vret=VIP_hashp_find(hobul_p->obj_type##_rev_info_db,                    \
                         (VIP_hashp_key_t) vipkl_hndl,                    \
                         (VIP_hashp_value_t *) &obj_hndl);                \
  if (vret != VIP_OK) {                                                   \
      obj_hndl = VAPI_INVAL_HNDL;                                         \
  }                                                                       \
                                                                          \
  return obj_hndl;                                                        \
}


static VAPI_pd_hndl_t vipkl2vapi_pd(HOBUL_t *hobul_p, PDM_pd_hndl_t vipkl_pd_hndl)
  VIPKL2VAPI(hobul_p,vipkl_pd_hndl,pd)
static VAPI_cq_hndl_t vipkl2vapi_cq(HOBUL_t *hobul_p, CQM_cq_hndl_t vipkl_cq_hndl)
  VIPKL2VAPI(hobul_p,vipkl_cq_hndl,cq)

#if defined(MT_SUSPEND_QP)
VIP_ret_t HOBUL_suspend_qp(
                /*IN*/ HOBUL_hndl_t   hobul_hndl,
                /*IN*/ VAPI_qp_hndl_t qp_hndl,
                /*IN*/ MT_bool        suspend_flag)
{
  VIP_ret_t rc;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  qp_info_t *qp_info_p;
    
  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  qp_info_p= get_qp_info(hobul_p,qp_hndl);
  if (qp_info_p == NULL)  return VIP_EINVAL_QP_HNDL;

   /* Avoid modifying the same QP by 2 threads at once */
  if (MOSAL_mutex_acq(&(qp_info_p->modify_qp_mutex),TRUE) != MT_OK)  return VIP_EINTR; 

  rc= VIPKL_suspend_qp(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,
                           qp_info_p->vipkl_qp_hndl, suspend_flag);
  
  MOSAL_mutex_rel(&(qp_info_p->modify_qp_mutex)); 

  return rc;
}

VIP_ret_t HOBUL_suspend_cq(
                /*IN*/ HOBUL_hndl_t   hobul_hndl,
                /*IN*/ VAPI_qp_hndl_t cq_hndl,
                /*IN*/ MT_bool        do_suspend)
{
  VIP_ret_t rc;
  HOBUL_t *hobul_p= (HOBUL_t *)hobul_hndl;
  cq_info_t *cq_info_p;
    
  /* check arguments */
  if (hobul_p == NULL) return VIP_EINVAL_HCA_HNDL;
  cq_info_p= get_cq_info(hobul_p,cq_hndl);
  if (cq_info_p == NULL)  return VIP_EINVAL_CQ_HNDL;
  
  rc= VIPKL_suspend_cq(VIP_RSCT_NULL_USR_CTX,hobul_p->vipkl_hndl,
                           cq_info_p->vipkl_cq_hndl, do_suspend);
  
  return rc;
}
#endif

