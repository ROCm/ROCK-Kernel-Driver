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

#define C_THHUL_SRQM_C

#include <mosal.h>
#include <ib_defs.h>
#include <vapi.h>
#include <tlog2.h>
#include <MT23108.h>
#include <thh.h>
#include <thhul.h>
#include <uar.h>
#include <thhul_hob.h>
#include <thhul_pdm.h>
#include <vapi_common.h>

#include "thhul_srqm.h"

/* Always support fork (SRQ is usually large enough to allow the memory overhead) */
#define MT_FORK_SUPPORT

#ifndef MT_KERNEL
/* instead of "ifdef"ing all over the code we define an empty macro */
#define MOSAL_pci_phys_free_consistent(addr,sz)  do {} while(0);
#endif


/* Limit kmalloc to 4 pages  */
#define WQ_KMALLOC_LIMIT (4*MOSAL_SYS_PAGE_SIZE)
#define SMALL_VMALLOC_AREA (1<<28)  /* VMALLOC area of 256MB or less is considered a scarce resource */

#define WQE_ALIGN_SHIFT 6        /* WQE address should be aligned to 64 Byte */
#define WQE_SZ_MULTIPLE_SHIFT 4           /* WQE size must be 16 bytes multiple */
/* WQE segments sizes */
#define WQE_SEG_SZ_NEXT (sizeof(struct wqe_segment_next_st)/8)            /* NEXT segment */
#define WQE_SEG_SZ_CTRL (sizeof(struct wqe_segment_ctrl_send_st)/8)       /* CTRL segment */
#define WQE_SEG_SZ_SG_ENTRY (sizeof(struct wqe_segment_data_ptr_st)/8)/* Scatter/Gather entry(ptr)*/
#define WQE_SEG_SZ_SG_ENTRY_DW (sizeof(struct wqe_segment_data_ptr_st)/32)/* (same in DWORDs) */
#define MAX_WQE_SZ 1008

#define MAX_ALLOC_RETRY 3  /* Maximum retries to get WQEs buffer which does not cross 4GB boundry */

#define SRQ_EMPTY_SENTRY_LKEY 1

struct THHUL_srq_st { /* SRQ context */
  MT_virt_addr_t wqe_buf;  /* The buffer for this queue WQEs - aligned to WQE size */ 
  VAPI_wr_id_t *wqe_id; /* Array of max_outs entries for holding each WQE ID (WQE index based) */
  u_int32_t srqn;     /* SRQ number/index */
  u_int32_t max_outs; /* Max. outstanding (number of WQEs in buffer) */
  u_int32_t cur_outs; /* Currently outstanding */
  u_int32_t max_sentries;  /* Max. Scatter list size */
  u_int8_t log2_max_wqe_sz; /* WQE size is a power of 2 (software implementation requirement) */
  u_int32_t* free_wqes_list;  /* "next" of each WQE is put on the WQE beginning */
  u_int32_t *wqe_draft;
  volatile  u_int32_t *last_posted_p;
  MOSAL_spinlock_t q_lock;   /* Protect concurrent usage of the queue */
  HHUL_pd_hndl_t pd;
  THH_uar_t uar;  /* UAR to use for this QP */
  void* wqe_buf_orig;   /* Pointer returned by malloc_within_4GB() for WQE buffer */
  MT_bool used_virt_alloc;     /* Used "MOSAL_pci_virt_alloc_consistent" for buffer allocation */
  MT_size_t wqe_buf_orig_size; /* size in bytes of wqe_buf_orig */
  struct THHUL_srq_st *next; /* SRQs list */
};
typedef struct THHUL_srq_st *THHUL_srq_t;

struct THHUL_srqm_st { /* THHUL_srqm_t is a pointer to this */
  struct THHUL_srq_st* srqs_list;
  MOSAL_mutex_t srqm_lock;
};

/**********************************************************************************************
 *                    Private functions protoypes declarations
 **********************************************************************************************/
static HH_ret_t init_srq(
  HHUL_hca_hndl_t hca,
  HHUL_pd_hndl_t  pd,
  u_int32_t max_outs,
  u_int32_t max_sentries,
  THHUL_srq_t new_srq
);

static HH_ret_t alloc_wqe_buf(
  /*IN*/ MT_bool in_ddr_mem,   /* Allocation of WQEs buffer is requested in attached DDR mem. */
  /*IN*/ u_int32_t max_outs,
  /*IN*/ u_int32_t max_sentries,
  /*IN/OUT*/ THHUL_srq_t new_srq,
  /*OUT*/    THH_srq_ul_resources_t *qp_ul_resources_p
);

static HH_ret_t alloc_aux_data_buf(
  /*IN/OUT*/ THHUL_srq_t new_srq
);


/**********************************************************************************************
 *                    Private inline functions 
 **********************************************************************************************/

/*********** WQE building functions ***********/

/* Init a not-connected (invalid) "next" segment (i.e. NDS=0) */
inline static u_int32_t WQE_init_next(u_int32_t *wqe_buf)
{
  memset(wqe_buf,0,WQE_SEG_SZ_NEXT);
  return WQE_SEG_SZ_NEXT;
}

inline static u_int32_t WQE_pack_recv_next(u_int32_t *segment_p,u_int32_t next_wqe_32lsb)
{
  memset(segment_p,0,WQE_SEG_SZ_NEXT);  /* Clear all "RESERVED" */
  segment_p[MT_BYTE_OFFSET(wqe_segment_next_st,nda_31_6)>>2]= ( next_wqe_32lsb & (~MASK32(6)) ) 
    | 1 ;  /* LS-bit is set to work around bug #16159/16160/16161 */;
  MT_INSERT_ARRAY32(segment_p,1, /* DBD always '1 for RQ */
    MT_BIT_OFFSET(wqe_segment_next_st,dbd),MT_BIT_SIZE(wqe_segment_next_st,dbd));
  MT_INSERT_ARRAY32(segment_p,0, /* NDS always 0 for SRQs */
    MT_BIT_OFFSET(wqe_segment_next_st,nds),MT_BIT_SIZE(wqe_segment_next_st,nds));
  return WQE_SEG_SZ_NEXT;
}

/* Build the scatter list (pointer segments) */
inline static u_int32_t WQE_pack_slist(u_int32_t *segment_p,
  u_int32_t sg_lst_len,VAPI_sg_lst_entry_t *sg_lst_p, u_int32_t desc_sentries)
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
   
   for (;i < desc_sentries; i++ , cur_loc_p+= WQE_SEG_SZ_SG_ENTRY_DW) {
     cur_loc_p[MT_BYTE_OFFSET(wqe_segment_data_ptr_st,byte_count)>>2]= 0;
     cur_loc_p[MT_BYTE_OFFSET(wqe_segment_data_ptr_st,l_key)>>2]= SRQ_EMPTY_SENTRY_LKEY;
   }
   return (u_int32_t)(((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)segment_p));
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
  THHUL_srq_t srq,
  VAPI_rr_desc_t *recv_req_p,
  u_int32_t *wqe_buf
)
{
  u_int8_t *cur_loc_p= (u_int8_t*)wqe_buf; /* Current location in the WQE */

  cur_loc_p+= WQE_init_next((u_int32_t*)cur_loc_p); /* Make "unlinked" "next" segment */
  cur_loc_p+= WQE_pack_ctrl_recv((u_int32_t*)cur_loc_p,
    recv_req_p->comp_type, 0/*event bit*/);
  /* Pack scatter list segments */
  cur_loc_p+= WQE_pack_slist((u_int32_t*)cur_loc_p,recv_req_p->sg_lst_len,recv_req_p->sg_lst_p,
                             srq->max_sentries);
  
  return (u_int32_t)(((MT_virt_addr_t)cur_loc_p) - ((MT_virt_addr_t)wqe_buf));
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

/**********************************************************************************************
 *                    Public API Functions (defined in thhul_hob.h)
 **********************************************************************************************/

HH_ret_t THHUL_srqm_create( 
  THHUL_hob_t hob, 
  THHUL_srqm_t *srqm_p 
) 
{ 
  *srqm_p= (THHUL_srqm_t) MALLOC(sizeof(struct THHUL_srqm_st));
  if (*srqm_p == NULL) {
    MTL_ERROR1("%s: Failed allocating THHUL_srqm_t.\n", __func__);
    return HH_EAGAIN;
  }

  (*srqm_p)->srqs_list= NULL;
  MOSAL_mutex_init(&((*srqm_p)->srqm_lock));

  return HH_OK;
}


HH_ret_t THHUL_srqm_destroy( 
   THHUL_srqm_t srqm 
) 
{ 
  
  THHUL_srq_t srq;

  while (srqm->srqs_list) {
    srq = srqm->srqs_list;
    srqm->srqs_list= srq->next;
    MTL_ERROR4(MT_FLFMT("%s: Releasing resource left-overs for SRQ 0x%X"), __func__, srq->srqn);
    /* Free QP resources: Auxilary buffer + WQEs buffer */
    THH_SMART_FREE(srq->wqe_id, srq->max_outs * sizeof(VAPI_wr_id_t));
    if (srq->wqe_buf_orig != NULL) {
      /* If allocated here (not in device mem.) than should be freed */
      if (srq->used_virt_alloc)
        MOSAL_pci_virt_free_consistent(srq->wqe_buf_orig, srq->wqe_buf_orig_size);
      else
        MOSAL_pci_phys_free_consistent(srq->wqe_buf_orig, srq->wqe_buf_orig_size);    
    }
    FREE(srq->wqe_draft);
    FREE(srq);
  }

  MOSAL_mutex_free(&(srqm->srqm_lock));
  FREE(srqm);
  return HH_OK; 
}


HH_ret_t THHUL_srqm_create_srq_prep( 
  /*IN*/
  HHUL_hca_hndl_t hca, 
  HHUL_pd_hndl_t  pd,
  u_int32_t max_outs,
  u_int32_t max_sentries,
  /*OUT*/
  HHUL_srq_hndl_t *srq_hndl_p,
  u_int32_t *actual_max_outs_p,
  u_int32_t *actual_max_sentries_p,
  void /*THH_srq_ul_resources_t*/ *srq_ul_resources_p
) 
{ 
  THHUL_srqm_t srqm;
  THH_hca_ul_resources_t hca_ul_res;
  THH_srq_ul_resources_t *ul_res_p= (THH_srq_ul_resources_t*)srq_ul_resources_p;
  THHUL_srq_t new_srq;
  HH_ret_t rc;
  THHUL_pdm_t pdm;
  
  rc= THHUL_hob_get_srqm(hca,&srqm);
  if (rc != HH_OK) {
    MTL_ERROR4(MT_FLFMT("%s: Invalid HCA handle (%p)."), __func__, hca);
    return HH_EINVAL_HCA_HNDL;
  }
  rc= THHUL_hob_get_hca_ul_res(hca,&hca_ul_res);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THHUL_hob_get_hca_ul_res (%d=%s).\n"), __func__,
               rc,HH_strerror_sym(rc));
    return rc;
  }
  
  rc= THHUL_hob_get_pdm(hca,&pdm);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed THHUL_hob_get_pdm (%d=%s).\n"), __func__, 
               rc,HH_strerror_sym(rc));
    return rc;
  }
  
  (new_srq)= (THHUL_srq_t)MALLOC(sizeof(struct THHUL_srq_st));
  if (new_srq == NULL) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocating THHUL_srq_t."), __func__);
    return HH_EAGAIN;
  }

  rc= init_srq(hca,pd,max_outs,max_sentries,new_srq);
  if (rc != HH_OK) {
    goto failed_init_srq;
  }

  rc= alloc_wqe_buf(FALSE/*not in DDR*/,hca_ul_res.max_srq_ous_wr,hca_ul_res.max_num_sg_ent_srq,
                    new_srq,srq_ul_resources_p);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocating WQEs buffers."), __func__);
    goto failed_alloc_wqe;
  }

  rc= alloc_aux_data_buf(new_srq);
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocating auxilary buffers."), __func__);
    goto failed_alloc_aux;
  }
  
  /* Set output modifiers */
  *srq_hndl_p= new_srq;
  *actual_max_outs_p= new_srq->max_outs;
  *actual_max_sentries_p= new_srq->max_sentries;
  rc= THH_uar_get_index(new_srq->uar,&(ul_res_p->uar_index)); 
  if (rc != HH_OK) {
    MTL_ERROR1(MT_FLFMT(": Failed getting UAR index.\n"));
    goto failed_uar_index;
  }
  /* wqe_buf data in srq_ul_resources_p is already set in alloc_wqe_buf */
  
  /* update SRQs list */
  MOSAL_mutex_acq_ui(&(srqm->srqm_lock));
  new_srq->next= srqm->srqs_list;
  srqm->srqs_list= new_srq;
  MOSAL_mutex_rel(&(srqm->srqm_lock));

  return HH_OK;

  /* Error cleanup */
  failed_uar_index:
    THH_SMART_FREE(new_srq->wqe_id, new_srq->max_outs * sizeof(VAPI_wr_id_t)); 
  failed_alloc_aux:
    if (new_srq->wqe_buf_orig != NULL) {
      /* WQEs buffer were allocated in process mem. or by the THH_srqm ? */ 
      /* If allocated here than should be freed */
      if (new_srq->used_virt_alloc) 
        MOSAL_pci_virt_free_consistent(new_srq->wqe_buf_orig, new_srq->wqe_buf_orig_size);
      else
        MOSAL_pci_phys_free_consistent(new_srq->wqe_buf_orig, new_srq->wqe_buf_orig_size);    
    }
  failed_alloc_wqe:
  failed_init_srq:
    FREE(new_srq);
  return rc;
}


HH_ret_t THHUL_srqm_create_srq_done( 
  HHUL_hca_hndl_t hca, 
  HHUL_srq_hndl_t hhul_srq, 
  HH_srq_hndl_t hh_srq, 
  void/*THH_srq_ul_resources_t*/ *srq_ul_resources_p
) 
{ 
  THHUL_srqm_t srqm;
  THHUL_srq_t srq= (THHUL_srq_t)hhul_srq;
  THH_srq_ul_resources_t *ul_res_p= (THH_srq_ul_resources_t*)srq_ul_resources_p;
  u_int32_t i;
  u_int32_t* cur_wqe;
  u_int32_t wqe_sz_dwords;
  HH_ret_t rc;
  
  rc= THHUL_hob_get_srqm(hca,&srqm);
  if (rc != HH_OK) {
    MTL_ERROR4(MT_FLFMT("%s: Invalid HCA handle (%p)."), __func__, hca);
    return HH_EINVAL_HCA_HNDL;
  }
  if (srq == NULL) {
    MTL_ERROR4(MT_FLFMT("%s: NULL hhul_qp handle."), __func__);
    return HH_EINVAL;
  }
  
  if (srq->wqe_buf_orig == NULL) { /* WQEs buffer allocated in DDR mem. by THH_qpm */
    if (ul_res_p->wqes_buf == 0) {
      MTL_ERROR1(MT_FLFMT("%s: Got NULL WQEs buffer from qp_ul_res for new srqn=0x%X."), __func__, 
                 hh_srq);
      return HH_EINVAL;
    }
    /* Set the per queue resources */
    srq->wqe_buf= MT_UP_ALIGNX_VIRT(ul_res_p->wqes_buf,srq->log2_max_wqe_sz);
    if (srq->wqe_buf != ul_res_p->wqes_buf) {
      MTL_ERROR1(
        "THHUL_srqm_create_qp_done: Buffer allocated by THH_qpm ("VIRT_ADDR_FMT") "
        "is not aligned to RQ WQE size (%d bytes).\n",
        ul_res_p->wqes_buf,1<<srq->log2_max_wqe_sz);
      return HH_EINVAL;
    }
  }

  /* Create free WQEs list of wqe_buf */
  wqe_sz_dwords= (1 << (srq->log2_max_wqe_sz - 2));
  for (i= 0 , cur_wqe= (u_int32_t*)srq->wqe_buf; i < srq->max_outs; i++) {
    *((u_int32_t**)cur_wqe)= srq->free_wqes_list; /* Link WQE to first in free list */
    srq->free_wqes_list= cur_wqe;     /* Put as first in free list      */
    MTL_DEBUG5(MT_FLFMT("%s: Added srq->free_wqes_list a WQE at %p"),__func__,srq->free_wqes_list);
    cur_wqe+= wqe_sz_dwords; /* u_int32_t* pointer arithmatic */
  }
  
  srq->srqn= hh_srq;

  MTL_DEBUG4(MT_FLFMT("%s: srqn=0x%X  buf_p="VIRT_ADDR_FMT"  sz=0x%X"), __func__,
             srq->srqn, srq->wqe_buf, (1 << srq->log2_max_wqe_sz) * srq->max_outs);

  return HH_OK;
}


HH_ret_t THHUL_srqm_destroy_srq_done( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_srq 
) 
{ 
  THHUL_srqm_t srqm;
  THHUL_srq_t srq= (THHUL_srq_t)hhul_srq;
  THHUL_srq_t cur_srq,prev_srq;
  HH_ret_t rc;
  
  rc= THHUL_hob_get_srqm(hca,&srqm);
  if (rc != HH_OK) {
    MTL_ERROR4("%s: Invalid HCA handle (%p).", __func__, hca);
    return HH_EINVAL_HCA_HNDL;
  }
  
  /* update SRQs list */
  MOSAL_mutex_acq_ui(&(srqm->srqm_lock));
  /* find SRQ in SRQs list */
  for (prev_srq= NULL, cur_srq= srqm->srqs_list; 
       (cur_srq != NULL) && (cur_srq != srq); 
       prev_srq= cur_srq , cur_srq= cur_srq->next);
  if (cur_srq == NULL) {
    MOSAL_mutex_rel(&(srqm->srqm_lock));
    MTL_ERROR2(MT_FLFMT("%s: Could not find given SRQ (hndl=0x%p , srqn=0x%X)"), __func__, 
               srq, srq->srqn);
    return HH_EINVAL_SRQ_HNDL;
  }
  /* remove SRQ from list */
  if (prev_srq != NULL) prev_srq->next= srq->next;
  else                 srqm->srqs_list= srq->next;
  MOSAL_mutex_rel(&(srqm->srqm_lock));

  /* Free SRQ resources: Auxilary buffer + WQEs buffer + WQE draft + SRQ object */
  MTL_DEBUG4(MT_FLFMT("Freeing user level WQE-IDs auxilary buffers"));
  THH_SMART_FREE(srq->wqe_id, srq->max_outs * sizeof(VAPI_wr_id_t)); 
  if (srq->wqe_buf_orig != NULL) {/* WQEs buffer were allocated in process mem. */ 
    if (srq->used_virt_alloc) 
      MOSAL_pci_virt_free_consistent(srq->wqe_buf_orig, srq->wqe_buf_orig_size);
    else
      MOSAL_pci_phys_free_consistent(srq->wqe_buf_orig, srq->wqe_buf_orig_size);    
  }
  FREE(srq->wqe_draft);
  FREE(srq);
  
  return HH_OK;  
}



HH_ret_t THHUL_srqm_post_recv_reqs(
                                 /*IN*/ HHUL_hca_hndl_t hca, 
                                 /*IN*/ HHUL_srq_hndl_t hhul_srq, 
                                 /*IN*/ u_int32_t num_of_requests,
                                 /*IN*/ VAPI_rr_desc_t *recv_req_array,
                                 /*OUT*/ u_int32_t *posted_requests_p
                                 )
{
  THHUL_srq_t srq= (THHUL_srq_t)hhul_srq;
  u_int32_t* wqe_draft= srq->wqe_draft;
  u_int32_t next_draft[WQE_SEG_SZ_NEXT>>2]; /* Build "next" segment here */
  volatile u_int32_t* next_wqe= NULL; /* Actual WQE pointer */
  volatile u_int32_t* prev_wqe_p= srq->last_posted_p; 
  u_int32_t wqe_sz_dwords= 0;
  u_int32_t i,reqi,next2post_index;
  THH_uar_recvq_dbell_t rq_dbell;
  HH_ret_t ret= HH_OK;

  *posted_requests_p= 0;
  if (num_of_requests == 0)  return HH_OK; /* nothing to do */
  
  /* Init. invariant RQ doorbell fields */
  rq_dbell.qpn= srq->srqn;
  rq_dbell.next_size= 0; /* For SRQs, NDS comes from SRQC */
  rq_dbell.credits= 0;   /* For 256 WQEs quantums */
  
  MOSAL_spinlock_irq_lock(&(srq->q_lock)); /* protect wqe_draft as well as WQE allocation/link */
  
  rq_dbell.next_addr_32lsb= (u_int32_t)(MT_virt_addr_t)srq->free_wqes_list; /* For first chain */

  /* Build and link all WQEs */
  for (reqi= 0; (reqi < num_of_requests) ; reqi++) {
    
    if (srq->free_wqes_list == NULL) {
      MTL_ERROR2(MT_FLFMT(
        "%s: Posting only %u requests out of %u"), __func__, *posted_requests_p, num_of_requests);
      ret= HH_EAGAIN;
      break;
    }
    
    if (srq->max_sentries < recv_req_array[reqi].sg_lst_len) {
      MTL_ERROR2(MT_FLFMT(
        "%s: Scatter list of req. #%u is too large (%u entries > max_sg_sz=%u)"), __func__,
                reqi,recv_req_array[reqi].sg_lst_len,srq->max_sentries);
      ret= HH_EINVAL_SG_NUM;
      break;
    }

    if (recv_req_array[reqi].opcode != VAPI_RECEIVE) {
      MTL_ERROR2(MT_FLFMT(
        "%s: Invalid opcode (%d=%s)in request #%d"), __func__,
         recv_req_array[reqi].opcode, VAPI_wr_opcode_sym(recv_req_array[reqi].opcode), reqi);
      ret= HH_EINVAL_OPCODE;
      break;
    }

    /* Build WQE */
    wqe_sz_dwords= (WQE_build_recv(srq,recv_req_array+reqi,wqe_draft) >> 2);
  #ifdef MAX_DEBUG
    if ((wqe_sz_dwords<<2) > (1U << srq->log2_max_wqe_sz)) {
      MTL_ERROR1(MT_FLFMT("%s: SRQ 0x%X: WQE too large (%d > max=%d)"), __func__,
                 srq->srqn,(wqe_sz_dwords<<2),(1U << srq->log2_max_wqe_sz));
    	}
  #endif
    
    /* Allocate next WQE */
    next_wqe= srq->free_wqes_list ;
    srq->free_wqes_list= *((u_int32_t**)next_wqe);/* next WQE is in the WQE (when free) */
    /* Save WQE ID */
    next2post_index= (u_int32_t)(((u_int8_t*)next_wqe - (u_int8_t*)srq->wqe_buf) >> srq->log2_max_wqe_sz);
    MTL_DEBUG6(MT_FLFMT("%s: SRQ 0x%X posting WQE at index %u (addr=%p)"), __func__, 
               srq->srqn, next2post_index, next_wqe); //DEBUG
    srq->wqe_id[next2post_index]= recv_req_array[reqi].id;  /* Save WQE ID */

    /* copy (while swapping,if needed) the wqe_draft to the actual WQE */
    /* TBD: for big-endian machines we can optimize here and use memcpy */
    for (i= 0; i < wqe_sz_dwords; i++) {
      next_wqe[i]= MOSAL_cpu_to_be32(wqe_draft[i]);
    }
    
    if (prev_wqe_p != NULL) { 
      /* Update "next" segment of previous WQE */
      /* Build linking "next" segment in last posted WQE */
      WQE_pack_recv_next(next_draft, (u_int32_t)(MT_ulong_ptr_t)next_wqe);
      for (i= 0;i < (WQE_SEG_SZ_NEXT>>2) ;i++) {
        /* This copy assures big-endian as well as that DBD/NDS is written last */
        prev_wqe_p[i]= MOSAL_cpu_to_be32(next_draft[i]);
      }
    }
    prev_wqe_p= next_wqe;

    (*posted_requests_p)++;

    if (((*posted_requests_p) & 0xFF) == 0) { /* ring RQ doorbell every 256 WQEs */
      THH_uar_recvq_dbell(srq->uar,&rq_dbell);
      rq_dbell.next_addr_32lsb= (u_int32_t)(MT_virt_addr_t)srq->free_wqes_list; /* For next chain */
    }
  }
  
  if (((*posted_requests_p) & 0xFF) != 0) { /* left-overs (less than 256 WQEs) */
    rq_dbell.credits= (*posted_requests_p) & 0xFF;
    THH_uar_recvq_dbell(srq->uar,&rq_dbell);
  }
  
  srq->last_posted_p= prev_wqe_p;
  srq->cur_outs+= *posted_requests_p; /* redundant info - for debug ? */
  
  MOSAL_spinlock_unlock(&(srq->q_lock));
  return ret;

}



/* Release this WQE only and return its WQE ID */
HH_ret_t THHUL_srqm_comp( 
  THHUL_srqm_t srqm, 
  HHUL_srq_hndl_t hhul_srq,
  u_int32_t wqe_addr_32lsb, 
  VAPI_wr_id_t *wqe_id_p
) 
{ 
  THHUL_srq_t srq= (THHUL_srq_t)hhul_srq;
  u_int32_t wqes_base_32lsb= (u_int32_t)(srq->wqe_buf & 0xFFFFFFFF) ;
  u_int32_t freed_wqe_index;
  MT_virt_addr_t wqe_buf_h= sizeof(u_int32_t*) > 4 ? (srq->wqe_buf >> 32) << 32 : 0;
  u_int32_t *wqe_p= (u_int32_t*)(wqe_buf_h | wqe_addr_32lsb);

  if (wqe_addr_32lsb < wqes_base_32lsb) {
    MTL_ERROR1(MT_FLFMT("%s: Got wqe_addr_32lsb=0x%X < wqes_base_32lsb=0x%X"), __func__,
               wqe_addr_32lsb, wqes_base_32lsb);
    return HH_EINVAL;
  }
  if (wqe_addr_32lsb & MASK32(srq->log2_max_wqe_sz)) {
    MTL_ERROR1(MT_FLFMT(
      "%s: Got wqe_addr_32lsb=0x%X which is not aligned to WQE size/stride 2^%u"),
       __func__, wqe_addr_32lsb, srq->log2_max_wqe_sz);
    return HH_EINVAL;
  }

  freed_wqe_index= (wqe_addr_32lsb - wqes_base_32lsb) >> srq->log2_max_wqe_sz;
  if (freed_wqe_index > srq->max_outs) {
    MTL_ERROR1(MT_FLFMT("%s: Got wqe_addr_32lsb=0x%X which is WQE index 0x%X "
                        "(max_outs=0x%X , wqes_base_32lsb=0x%X , log2_max_wqe_sz=0x%X)"), 
               __func__, wqe_addr_32lsb, freed_wqe_index, 
               srq->max_outs, wqes_base_32lsb, srq->log2_max_wqe_sz);
    return HH_EINVAL;
  }

  /* Get WQE ID from auxilary buffer */
  *wqe_id_p= srq->wqe_id[freed_wqe_index]; 

  /* Return WQE to free list */
  MOSAL_spinlock_irq_lock(&(srq->q_lock));
  *((u_int32_t**)wqe_p)= srq->free_wqes_list; /* Link WQE to first in free list */
  srq->free_wqes_list= wqe_p;                 /* Put as first in free list      */
  srq->cur_outs --;                                    /* (for debug purpose)            */
  if (wqe_p == srq->last_posted_p) {
    /* After WQE put in the free list, we should not link it to next WQE */
    srq->last_posted_p= NULL;
  }
  MOSAL_spinlock_unlock(&(srq->q_lock));

  return HH_OK;
}



/**********************************************************************************************
 *                    Private Functions
 **********************************************************************************************/


/* Allocate THHUL_qp_t object and initialize it */
static HH_ret_t init_srq(
  HHUL_hca_hndl_t hca,
  HHUL_pd_hndl_t  pd,
  u_int32_t max_outs,
  u_int32_t max_sentries,
  THHUL_srq_t new_srq
)
{
  HH_ret_t rc;
  THHUL_pdm_t pdm;

  memset(new_srq,0,sizeof(struct THHUL_srq_st));
  
  rc= THHUL_hob_get_uar(hca,&(new_srq->uar));
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed getting THHUL_hob's UAR (%d=%s)."),
               __func__,rc,HH_strerror_sym(rc));
    return rc;
  }
  rc= THHUL_hob_get_pdm(hca,&pdm);
  if (rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed getting THHUL_hob_get_pdm's UAR (%d=%s)."),
               __func__,rc,HH_strerror_sym(rc));
    return rc;
  }
  
  new_srq->srqn= 0xFFFFFFFF;  /* Init to invalid SRQ num. until create_qp_done is invoked */
  new_srq->pd= pd;
  new_srq->max_outs= max_outs;
  new_srq->max_sentries= max_sentries;
  MOSAL_spinlock_init(&(new_srq->q_lock));

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
  /*IN*/ MT_bool in_ddr_mem,   /* Allocation of WQEs buffer is requested in attached DDR mem. */
  /*IN*/ u_int32_t max_outs_wqes, /* HCA cap. */
  /*IN*/ u_int32_t max_sentries,  /* HCA cap. of max.scatter entries for SRQs */
  /*IN/OUT*/ THHUL_srq_t new_srq,
  /*OUT*/    THH_srq_ul_resources_t *srq_ul_resources_p
)
{
  u_int32_t wqe_sz,buf_sz,wqe_base_sz;
  u_int8_t log2_wqe_sz;

  /* Check requested capabilities */
  if ((new_srq->max_outs == 0) || (new_srq->max_outs > max_outs_wqes)) {
    MTL_ERROR3(MT_FLFMT("%s: Got a request for a SRQ with %u WQEs - rejecting !"), __func__,
               new_srq->max_outs);
    return HH_E2BIG_WR_NUM;
  }
  
  if (new_srq->max_sentries > max_sentries) {
    MTL_ERROR2(MT_FLFMT(
      "%s: Got request for %u scatter entries (HCA cap. for SRQ is %u scatter entries)"),
      __func__, new_srq->max_sentries, max_sentries);
    return HH_E2BIG_SG_NUM;
  }

  /* Compute RQ WQE requirements */
  wqe_base_sz= WQE_SEG_SZ_NEXT + WQE_SEG_SZ_CTRL; 
  wqe_sz= wqe_base_sz + (new_srq->max_sentries * WQE_SEG_SZ_SG_ENTRY);
  if (wqe_sz > MAX_WQE_SZ) {
    MTL_ERROR2(
      MT_FLFMT("required SRQ capabilities (max_sentries=%d) require a too large WQE (%u bytes)"),
        new_srq->max_sentries, wqe_sz);
    return HH_E2BIG_SG_NUM;
  }
  log2_wqe_sz= ceil_log2(wqe_sz);  /* Align to next power of 2 */
  /* A WQE must be aligned to 64B (WQE_ALIGN_SHIFT) so we take at least this size */
  if (log2_wqe_sz < WQE_ALIGN_SHIFT)  log2_wqe_sz= WQE_ALIGN_SHIFT;
  wqe_sz= (1<<log2_wqe_sz);
  MTL_DEBUG4(MT_FLFMT("%s: Allocating SRQ WQE of size %d."), __func__, wqe_sz);
  /* Compute real number of s/g entries based on rounded up WQE size */
  new_srq->max_sentries= (wqe_sz - wqe_base_sz) / WQE_SEG_SZ_SG_ENTRY;  
  /* Make sure we do not exceed reported HCA cap. */
  new_srq->max_sentries= (new_srq->max_sentries > max_sentries) ? 
    max_sentries : new_srq->max_sentries;
  new_srq->wqe_draft= (u_int32_t *)MALLOC(wqe_sz);
  if (new_srq->wqe_draft == NULL) {
    MTL_ERROR2(MT_FLFMT("%s: Failed allocating %u bytes for SRQ's wqe draft"), __func__, wqe_sz);
    return HH_EAGAIN;
  }
  
  buf_sz= new_srq->max_outs * wqe_sz;
  
  if (in_ddr_mem) { /* Allocate WQEs buffer by THH_srqm in the attached DDR memory */
    new_srq->wqe_buf_orig= NULL;
    srq_ul_resources_p->wqes_buf= 0;   /* Allocate in attached DDR memory */
  } else { /* Allocate WQEs buffer in main memory */
#if !defined(MT_KERNEL) && defined(MT_FORK_SUPPORT)
    /* Assure the buffer covers whole pages (no sharing of locked memory with other date) */
    new_srq->wqe_buf_orig_size = 
      (MOSAL_SYS_PAGE_SIZE-1)/* For alignment */+MT_UP_ALIGNX_U32(buf_sz, MOSAL_SYS_PAGE_SHIFT);
    /* Prevent other data reside in the last page of the buffer... */
    /* cover last page (last WQE can be at last page begin and its size is 64B min.)*/
#else
    /* Allocate one more for each queue in order to make each aligned to its WQE size */
    /* (Assures no WQE crosses a page boundry, since we make WQE size a power of 2)   */ 
    new_srq->wqe_buf_orig_size = buf_sz+(wqe_sz-1);
#endif

    new_srq->wqe_buf_orig= malloc_within_4GB(new_srq->wqe_buf_orig_size,&new_srq->used_virt_alloc);
    if (new_srq->wqe_buf_orig == NULL) {
      MTL_ERROR2(MT_FLFMT("%s: Failed allocation of WQEs buffer of "SIZE_T_FMT" bytes within "
        "4GB boundries."), __func__, new_srq->wqe_buf_orig_size);
      goto failed_wqe_buf;
    }
  
    /* Set the per queue resources */
#if !defined(MT_KERNEL) && defined(MT_FORK_SUPPORT)
    new_srq->wqe_buf= MT_UP_ALIGNX_VIRT((MT_virt_addr_t)(new_srq->wqe_buf_orig),
                                        MOSAL_SYS_PAGE_SHIFT);
#else  
    new_srq->wqe_buf= MT_UP_ALIGNX_VIRT((MT_virt_addr_t)(new_srq->wqe_buf_orig),log2_wqe_sz);
#endif
    
    srq_ul_resources_p->wqes_buf= new_srq->wqe_buf;
  }

  new_srq->log2_max_wqe_sz= log2_wqe_sz;
  srq_ul_resources_p->wqes_buf_sz= buf_sz;
  srq_ul_resources_p->wqe_sz= wqe_sz;

  return HH_OK;

  failed_wqe_buf:
    FREE(new_srq->wqe_draft);
    return HH_EAGAIN;
}


/* Allocate the auxilary WQEs data 
 * (a software context of a WQE which does not have to be in the registered WQEs buffer) */
static HH_ret_t alloc_aux_data_buf(
  /*IN/OUT*/ THHUL_srq_t new_srq
)
{
  /* RQ auxilary buffer: WQE ID per WQE */ 
  new_srq->wqe_id= (VAPI_wr_id_t*)
    THH_SMART_MALLOC(new_srq->max_outs * sizeof(VAPI_wr_id_t)); 
  if (new_srq->wqe_id == NULL) {
    MTL_ERROR1(MT_FLFMT("%s: Failed allocating SRQ auxilary buffer (for WQE ID)."), __func__);
    return HH_EAGAIN;
  }
  
  return HH_OK;
}


