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


//#define MTPERF
#include <mtperf.h>

#include <hh.h>
#include <vapi.h>
#include <vapi_common.h>
#include "em.h"
#include <cqm.h>
#include <srqm.h>

#define CACHE_SHIFT 8
#define CACHE_SIZE (1<<CACHE_SHIFT)
#define CACHE_MASK (CACHE_SIZE-1)

typedef struct {
  HH_cq_hndl_t cq_num;
  VAPI_completion_event_handler_t completion_handler;
  void *private_data;
  VAPI_cq_hndl_t vapi_cq_hndl;
  MT_bool valid;
}
cache_item_t;

typedef struct em_async_event_st{
  VAPI_async_event_handler_t async_clbk_hndl;
  void* private_data;
  u_int32_t hndler_hndl_key;
  struct em_async_event_st *next; 
  struct em_async_event_st *priv;
} EM_async_event_t;

typedef struct em_async_hob_ctx_st{
  u_int16_t last_key;
  EM_async_event_t *async_list_head; 
  struct em_async_hob_ctx_st *next;
  struct em_async_hob_ctx_st *priv;
} EM_async_hob_ctx_t;


struct EM_t {
  HOBKL_hndl_t hob;
  VAPI_hca_hndl_t hca;
  MOSAL_spinlock_t async_spl;
  VAPI_async_event_handler_t async_h;
  void *async_data;
  MOSAL_spinlock_t compl_spl;
  VAPI_completion_event_handler_t compl_h;
  void *compl_data;
  MOSAL_spinlock_t evapi_compl_spl;
#ifdef MTPERF
  u_int32_t total_misses;
  u_int32_t total_hits;
#endif
  cache_item_t cq2handler_cache[CACHE_SIZE]; /* use this construct to cahnge th esize of the cache */
  VIP_array_p_t em_async_array;       /* the array which holds all the async handlers */
  EM_async_hob_ctx_t *hob_ctx_list_head;
  u_int32_t number_of_hob_ctx;
};

#define EM_MAX_HANDLERS 0xffff
#define EM_MAX_HOB_CTX 0xffff

static void VIP_free(void* p)
{
       MTL_ERROR1(MT_FLFMT("EM delete:found unreleased async object"));        
}

static inline void cache_insert(EM_hndl_t em, HH_cq_hndl_t cq_num,
                                VAPI_completion_event_handler_t completion_handler,
                                void *private_data, VAPI_cq_hndl_t vapi_cq_hndl)
{
  register u_int32_t idx = cq_num & CACHE_MASK;

  
  em->cq2handler_cache[idx].cq_num = cq_num;
  em->cq2handler_cache[idx].completion_handler = completion_handler;
  em->cq2handler_cache[idx].private_data = private_data;
  em->cq2handler_cache[idx].vapi_cq_hndl = vapi_cq_hndl;
  em->cq2handler_cache[idx].valid = TRUE;
  
}



static inline call_result_t cache_retrieve(EM_hndl_t em, HH_cq_hndl_t cq_num, cache_item_t *citem_p)
{
  register u_int32_t idx = cq_num & CACHE_MASK;

  
  if ( (em->cq2handler_cache[idx].valid==TRUE) && (em->cq2handler_cache[idx].cq_num==cq_num) ) {
    *citem_p = em->cq2handler_cache[idx];
    
    return MT_OK;
  }
  

  return MT_ENORSC;
}


static inline void cache_invalidate_entry(EM_hndl_t em, HH_cq_hndl_t cq_num)
{
  register u_int32_t idx = cq_num & CACHE_MASK;

  
  em->cq2handler_cache[idx].valid = FALSE;
  
}

/* this function must be called with spinlock around it */
static inline void EM_call_all_ctx_handelrs(EM_hndl_t em, 
                                     EM_async_hob_ctx_t *async_hndl_ctx_p, 
                                     VAPI_event_record_t rec)
{
  EM_async_event_t *async_list_head = async_hndl_ctx_p->async_list_head;
  MTL_DEBUG1("EM_call_all_ctx_handelrs: last_key=%d, next=%p, priv=%p\n",
         async_hndl_ctx_p->last_key, async_hndl_ctx_p->next, async_hndl_ctx_p->priv);

  while (async_list_head != NULL) {
    /* call the actual handler */
    async_list_head->async_clbk_hndl(em->hca, &rec, async_list_head->private_data);
    async_list_head = async_list_head->next;
  }
}


static inline void EM_call_all_unaff_hobs_hndlrs(EM_hndl_t em, 
                                          VAPI_event_record_t rec)
{
  EM_async_hob_ctx_t *async_hndl_ctx_p; 
  
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  async_hndl_ctx_p = em->hob_ctx_list_head;
  while (async_hndl_ctx_p != NULL) {
    EM_call_all_ctx_handelrs(em, async_hndl_ctx_p, rec);
    async_hndl_ctx_p = async_hndl_ctx_p->next;
  }
  MOSAL_spinlock_unlock(&em->async_spl);

}

static inline void EM_call_all_aff_hob_ctx_hndlrs(EM_hndl_t em, 
                                           EM_async_ctx_hndl_t async_hndl_ctx, 
                                           VAPI_event_record_t rec)
{
  EM_async_hob_ctx_t *async_hndl_ctx_p;
  VIP_ret_t ret=VIP_OK;
  
  FUNC_IN;
  ret = VIP_array_find_hold(em->em_async_array, async_hndl_ctx, (VIP_array_obj_t *)&async_hndl_ctx_p);
  MTL_DEBUG3(MT_FLFMT("Calling VIP_array_find_hold ret=%d"), ret);
  if ( ret!=VIP_OK ) {
    MTL_ERROR1("%s: Internal mismatch - async_hob_ctx (%d) is not in array\n", 
               __func__,async_hndl_ctx);
    FUNC_OUT;
  }
  
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  EM_call_all_ctx_handelrs(em, async_hndl_ctx_p, rec);
  MOSAL_spinlock_unlock(&em->async_spl);

  ret = VIP_array_find_release(em->em_async_array, async_hndl_ctx);
  MTL_DEBUG3(MT_FLFMT("Calling VIP_array_find_release ret=%d"), ret);
  if ( ret!=VIP_OK ) {
    MTL_ERROR1("%s: Internal mismatch - async_hob_ctx (%d) is not in array\n", 
               __func__,async_hndl_ctx);
    FUNC_OUT;
  }
  FUNC_OUT;
}

MTPERF_NEW_SEGMENT(vip_em,100000);
//MTPERF_NEW_SEGMENT(CQM_get_cq_by_id, 2000);
//MTPERF_EXTERN_SEGMENT(CQM_get_cq_by_id_hash_find);
//MTPERF_EXTERN_SEGMENT(CQM_get_cq_by_id_array_find);

//static HH_async_eventh_t EM_HH_async_event;
//static HH_comp_eventh_t EM_HH_comp_event;


/* Actual event handler called from HH.
 * Decode parameters and delegate to user-given handler
 * stored in EM 
 * In addition it calls all other callbacks */

static void EM_HH_async_event(HH_hca_hndl_t hndl, HH_event_record_t *event_rec, void *priv)

{
  EM_hndl_t em=(EM_hndl_t)priv;
  VIP_ret_t ret=VIP_OK;
  VAPI_event_record_t rec;
  QPM_hndl_t qpm=NULL; /* to stop compiler warnings */
  SRQM_hndl_t srqm;
  cq_params_t cqprms;
  EM_async_ctx_hndl_t async_hndl_ctx;

  FUNC_IN;
  
  /* take the syndrom in all cases */
  rec.syndrome = event_rec->syndrome;

  switch (rec.type=event_rec->etype) {
    /* QP affiliated events/errors */
    case VAPI_QP_PATH_MIGRATED:
    case VAPI_QP_COMM_ESTABLISHED:
    case VAPI_SEND_QUEUE_DRAINED:
    case VAPI_RECEIVE_QUEUE_DRAINED:
    case VAPI_LOCAL_WQ_CATASTROPHIC_ERROR:
    case VAPI_LOCAL_WQ_INV_REQUEST_ERROR:
    case VAPI_LOCAL_WQ_ACCESS_VIOL_ERROR:
    case VAPI_PATH_MIG_REQ_ERROR:
      MTL_DEBUG1("EM_HH_async_event: Got QP error event from type: %d, QP num: 0x%x\n", rec.type,  event_rec->event_modifier.qpn);
      qpm = HOBKL_get_qpm(em->hob);
      ret = QPM_get_vapiqp_by_qp_num(qpm, event_rec->event_modifier.qpn, 
                                     &(rec.modifier.qp_hndl), &async_hndl_ctx); 
      if (ret != VIP_OK) {
        MTL_ERROR1("EM_HH_async_event: Unknown QP num 0x%x received by the asynchronous event handler"
                   " for VIP event type %d(%s): discarding. (QPM_get_vapiqp_by_qp_num ret=%s)\n",
                   event_rec->event_modifier.qpn, rec.type,VAPI_event_record_sym(rec.type), VAPI_strerror_sym(ret));
        return;
      }
      
      /* call the async handler of this hob_ctx */
      EM_call_all_aff_hob_ctx_hndlrs(em,async_hndl_ctx,rec);

      break;

    /* SRQ affiliated events */
    case VAPI_SRQ_CATASTROPHIC_ERROR:
    case VAPI_SRQ_LIMIT_REACHED:
      srqm= HOBKL_get_srqm(em->hob);
      ret= SRQM_get_vapi_hndl(srqm, event_rec->event_modifier.srq, 
                              &(rec.modifier.srq_hndl), &async_hndl_ctx);
      if (ret != VIP_OK) {
        MTL_ERROR1("EM_HH_async_event: Unknown SRQ handle 0x%x received by the "
                   "asynchronous event handler"
                   " for VIP event type %d (%s): discarding. (SRQM_get_vapi_hndl ret=%s)\n",
                   event_rec->event_modifier.srq, rec.type, VAPI_event_record_sym(rec.type), VAPI_strerror_sym(ret));
        return;
      }
      
      /* call the async handler of this hob_ctx */
      EM_call_all_aff_hob_ctx_hndlrs(em,async_hndl_ctx,rec);
      break;

      /* TBD: EEC events not supported */
    case VAPI_EEC_PATH_MIGRATED:
      MTL_ERROR1("EM_HH_async_event: Unsupported asynchronious event type: VAPI_EEC_PATH_MIGRATED\n");
      return;

      /* TBD: EEC events not supported */
    case VAPI_EEC_COMM_ESTABLISHED:
      MTL_ERROR1("EM_HH_async_event: Unsupported asynchronious event type: VAPI_EEC_COMM_ESTABLISHED\n");
      return;

    case VAPI_PORT_ERROR:
    case VAPI_PORT_ACTIVE:
      rec.modifier.port_num = event_rec->event_modifier.port;
      /* call the async handler of this hob_ctx */
      EM_call_all_unaff_hobs_hndlrs(em, rec);
      break;

    case VAPI_CQ_ERROR:
      MTL_DEBUG1("EM_HH_async_event: Got CQ error event, CQ num: 0x%x\n", event_rec->event_modifier.cq);
      
      cqprms.cqm_hndl = HOBKL_get_cqm(em->hob);
      ret = CQM_get_cq_by_id(event_rec->event_modifier.cq, &cqprms);
      if (ret != VIP_OK) {
        MTL_ERROR1("EM_HH_async_event: Unknown CQ num %d received by the asynchronous event handler:"
                   " event type= VAPI_CQ_ERROR: discarding. (CQM_get_cq_by_id ret=%s)\n",
                   event_rec->event_modifier.cq, VAPI_strerror_sym(ret));
        return;
      }
      
      rec.modifier.cq_hndl = cqprms.vapi_cq_hndl;       
      /* call the async handler of this hob_ctx */
      EM_call_all_aff_hob_ctx_hndlrs(em, cqprms.async_hndl_ctx, rec);
      
      break;

    case VAPI_LOCAL_CATASTROPHIC_ERROR:
      /*No parameters here*/
      /* call the async handlers of all open hobs */
      MTL_DEBUG1("EM_HH_async_event: Got VAPI_LOCAL_CATASTROPHIC_ERROR error event\n");
      //return;
      
      EM_call_all_unaff_hobs_hndlrs(em, rec);
      
      break;

    default:
      MTL_ERROR1("EM_HH_async_event: Unsupported asynchronious event type reported: %d\n", event_rec->etype);
      return;
  }

  MOSAL_spinlock_dpc_lock(&em->async_spl);
  em->async_h(em->hca, &rec, em->async_data);
  MOSAL_spinlock_unlock(&em->async_spl);
}

/* Actual event handler called from HH.
 * Decode parameters and delegate to user-given handler
 * stored in EM */

static void EM_HH_comp_event(HH_hca_hndl_t hndl, HH_cq_hndl_t cq_num, void *priv)
{
  EM_hndl_t em=(EM_hndl_t)priv;
  VIP_ret_t ret;
  cq_params_t cqprms;
  call_result_t rc;
  cache_item_t citem;
  MT_bool cache_hit;

  //MTPERF_TIME_START(vip_em);
  
  MTL_TRACE4("%s(hca=%p,cqn=%d.priv=%p)\n", __func__,hndl,cq_num,priv);
  cqprms.cqm_hndl = HOBKL_get_cqm(em->hob);

  MOSAL_spinlock_dpc_lock(&em->evapi_compl_spl);
  rc = cache_retrieve(em, cq_num, &citem);
  if ( rc != MT_OK ) {
    cache_hit = FALSE;
    /* cache miss */
#ifdef MTPERF
  em->total_misses++;
#endif
    MTL_TRACE6(MT_FLFMT("cache miss: cq=%d"), cq_num);
    //MTPERF_TIME_START(CQM_get_cq_by_id);
    ret = CQM_get_cq_by_id(cq_num, &cqprms);
 //   MTPERF_TIME_END(CQM_get_cq_by_id);
    if (ret != VIP_OK) {
      MOSAL_spinlock_unlock(&em->evapi_compl_spl);
      MTL_ERROR1("Unknown CQ num %d received by the completion event handler: discarding. (CQM_get_cq_by_id ret=%s)\n",
                 cq_num, VAPI_strerror_sym(ret));
      return;
    }
    //MTPERF_TIME_END(vip_em);
    if ( cqprms.completion_handler ) {
      (*cqprms.completion_handler)(em->hca, cqprms.vapi_cq_hndl, cqprms.private_data);
    }
    cache_insert(em, cq_num, cqprms.completion_handler, cqprms.private_data, cqprms.vapi_cq_hndl);
  }
  else {
    /* cache hit */
    cache_hit = TRUE;
#ifdef MTPERF
  em->total_hits++;
#endif
    MTL_TRACE6(MT_FLFMT("cache hit: cq=%d"), cq_num);
    if ( citem.completion_handler ) {
      (*citem.completion_handler)(em->hca, citem.vapi_cq_hndl, citem.private_data);
    }
  }
  MOSAL_spinlock_unlock(&em->evapi_compl_spl);

  if ( cache_hit == FALSE ) {
    MOSAL_spinlock_dpc_lock(&em->compl_spl);
    em->compl_h(em->hca, cqprms.vapi_cq_hndl, em->compl_data);
    MOSAL_spinlock_unlock(&em->compl_spl);
  }
  else {
    MOSAL_spinlock_dpc_lock(&em->compl_spl);
    em->compl_h(em->hca, citem.vapi_cq_hndl, em->compl_data);
    MOSAL_spinlock_unlock(&em->compl_spl);
  }
   //MTPERF_TIME_END(vip_em);

}

/* Default event handlers: do nothing*/
static void MT_API EM_default_async_error_handler
(
/*IN*/  VAPI_hca_hndl_t   hca_hndl,
/*IN*/  VAPI_event_record_t   *event_record,
/*IN*/  void* private_data

)
{
  MTL_TRACE4("%s(hca=%d,event_p=%p,priv=%p)\n", __func__,
             hca_hndl,event_record,private_data);
}

/* Default event handlers: do nothing*/
static void MT_API EM_default_completion_event_handler
(
/*IN*/  VAPI_hca_hndl_t   hca_hndl,
/*IN*/  VAPI_cq_hndl_t  cq,
/*IN*/  void* private_data
)
{
  MTL_TRACE4("%s(hca=%d,cqh=0x%X,priv=%p)\n", __func__,hca_hndl,(u_int32_t)cq,private_data);
}

static inline VIP_ret_t EM_add_async_events_arr(EM_hndl_t em)
{
  VIP_ret_t ret = VIP_OK;

  /* create handlers array */
  ret = VIP_array_create_maxsize(1024, 1024,&(em->em_async_array));
  if ( ret != VIP_OK ) {
    MTL_ERROR1("%s: VIP_array_create_maxsize failed \n", __func__);
  }
  return ret;
}

static inline void EM_remove_hob_ctx(EM_hndl_t em,
                              EM_async_hob_ctx_t *hob_ctx)
{
   EM_async_event_t *list_head, *tmp_p;

   list_head = hob_ctx->async_list_head;
   /* No need to protect with the spinlock since before we remove this list we already  
      remove it from the contexts list */
   
   /* first free the list */
   while (list_head != NULL) {
     tmp_p = list_head->next;
     FREE(list_head);
     list_head = tmp_p;
   }
   /* now free the ctx itself */
   FREE(hob_ctx);
   
}


static void EM_remove_async_events_arr(EM_hndl_t em)
{
  VIP_ret_t ret = VIP_OK;
  EM_async_hob_ctx_t *tmp_ctx_p, *tmp_list_head;
  
  /* first cat the list */
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  tmp_list_head = em->hob_ctx_list_head;
  em->hob_ctx_list_head = NULL;
  MOSAL_spinlock_unlock(&em->async_spl);
      
  /* loop all the hob_ctx list and clean it */
  while (tmp_list_head != NULL) {
    tmp_ctx_p = tmp_list_head->next;
    EM_remove_hob_ctx(em, tmp_list_head);
    tmp_list_head = tmp_ctx_p;
  }
  
  /* destroy the array */
  ret = VIP_array_destroy(em->em_async_array,VIP_free);
  if (ret != VIP_OK) {
    MTL_ERROR1("%s: VIP_array_destroy failed \n", __func__);
  }

}

/*************************************************************************
 * Function: EM_new
 *
 * Arguments:
 *  hobkl_hndl: Associated HCA object
 *  hca_hndl: VAPI handle (used only to pass to the handlers)
 *  em_p: Newly allocated EM object
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *  
 *  Allocating the new EM and binding it to the appropriate event notification queue of 
 *  HCA-HAL.
 *
 *************************************************************************/ 
VIP_ret_t EM_new(/*IN*/ HOBKL_hndl_t hob_hndl,
                 /*OUT*/ EM_hndl_t *em_p)
{
  VIP_ret_t ret=VIP_OK;
  EM_hndl_t em=NULL;
  HH_hca_hndl_t hh;

  em = TMALLOC(struct EM_t);
  if ( !em ) {
    ret = VIP_EAGAIN;
    goto alloc_lbl;
  }
  memset(em, 0, sizeof(struct EM_t));
  em->hob = hob_hndl;

  MOSAL_spinlock_init(&em->async_spl);
  MOSAL_spinlock_init(&em->compl_spl);
  MOSAL_spinlock_init(&em->evapi_compl_spl);
  ret = EM_bind_completion_event_handler(em, NULL, NULL);
  if (ret != HH_OK) goto em_bind_compl;

  ret = EM_bind_async_error_handler(em, NULL, NULL);
  if (ret != HH_OK) goto em_bind_async;

  hh = HOBKL_get_hh_hndl(em->hob);

  ret = HH_set_comp_eventh(hh, EM_HH_comp_event, em);  /* for Tavor calls THH_hob_set_comp_eventh */
  if (ret != HH_OK) goto hh_comp_event;

  ret = HH_set_async_eventh(hh, EM_HH_async_event, em);
  if (ret != HH_OK) goto hh_async_event;

  ret = EM_add_async_events_arr(em);
  if (ret != HH_OK) goto EM_add_async;
  
  if (em_p) *em_p=em;

  return VIP_OK;

  EM_add_async:
  HH_set_async_eventh(hh, NULL, NULL);
  hh_async_event:
  HH_set_comp_eventh(hh, NULL, NULL);
  hh_comp_event:
  em_bind_async:
  em_bind_compl:
  FREE(em);
  alloc_lbl:
  return ret;
}

VIP_ret_t  EM_set_vapi_hca_hndl(/*IN*/ EM_hndl_t em, /*IN*/VAPI_hca_hndl_t hca_hndl)
{
  if (em == NULL)  return VIP_EINVAL_HNDL;
  em->hca = hca_hndl;
  return VIP_OK;
}

/*************************************************************************
 * Function: EM_delete
 *
 * Arguments:
 *  em: EM object handle to destroy.
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_EM_hndl
 *  
 * Description:
 *  
 *  Destroy the EM object.
 *
 *************************************************************************/ 
VIP_ret_t EM_delete(/*IN*/ EM_hndl_t em)
{
  HH_hca_hndl_t hh;
  VIP_ret_t ret=VIP_OK;

  MTL_DEBUG3("Inside " "%s(%p);\n", __func__, em);


  //MTPERF_REPORT_PRINTF(vip_em);
 //MTPERF_REPORT_PRINTF(CQM_get_cq_by_id);

#ifdef MTPERF
  //MTL_ERROR1("total misses=%d\n", em->total_misses);
  //MTL_ERROR1("total hits=%d\n", em->total_hits);
#endif
//  MTPERF_REPORT_PRINTF(CQM_get_cq_by_id_hash_find);
//  MTPERF_REPORT_PRINTF(CQM_get_cq_by_id_array_find);
  
//
  hh = HOBKL_get_hh_hndl(em->hob);

  ret = HH_set_comp_eventh(hh,NULL,NULL);  /* for Tavor calls THH_hob_set_comp_eventh */
  if (ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("HH_set_comp_event failed in EM_delete"));
  }


  ret = HH_set_async_eventh(hh,NULL,NULL);
  if (ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("HH_set_async_event failed in EM_delete"));
  }
  
  EM_remove_async_events_arr(em);
  
  FREE(em);

  MTL_DEBUG3("Inside " "%s Done removing EM\n", __func__);
  return VIP_OK;
}

/*************************************************************************
 * Function: EM_bind_async_error_handler
 *
 * Arguments:
 *  em: The EM object handle
 *  handler: The handler function pointer (as defined in the IB-spec.)
 *  private_data: Pointer to private data (will be passed to event on call)
 *  
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_EM_HNDL
 *  
 * Description:
 *  
 *  Bind the given Async Error Handler to the asynchronous error events of given EM.
 *  
 *  The handler is a pointer to a function as follows:
 *  
 *  void	
 *  
 *  VAPI_async_error_handler
 *  
 *  (
 *  
 *  IN	VAPI_hca_hndl_t 	hca_hndl,
 *  IN	VAPI_event_record_t 	event_record,
 *  void *private_data 
 *  )
 *
 *************************************************************************/ 
VIP_ret_t EM_bind_async_error_handler(/*IN*/ EM_hndl_t em,
                                      /*IN*/ VAPI_async_event_handler_t handler, /*IN*/ void* private_data)
{
  MTL_TRACE4("%s(em=%p,handler=%p,priv=%p)\n", __func__,em,handler,private_data);
  if (handler == NULL) handler=EM_default_async_error_handler;
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  em->async_h = handler;
  em->async_data = private_data;
  MOSAL_spinlock_unlock(&em->async_spl);
  return VIP_OK;
}

/*************************************************************************
 * Function: EM_bind_completion_event_handler
 *
 * Arguments:
 *  em: The EM object handle
 *  handler: The handler function pointer (as defined in the IB-spec.)
 *  private_data: Pointer to private data (will be passed to event on call)
 *
 * Returns:
 *  VIP_OK
 *  
 * Description:
 *  
 *  Bind the given completion event handler to the completion event of the associated 
 *  HCA.
 *  
 *  The handler is a pointer to a function as follows:
 *  
 *  void	
 *  
 *  VAPI_completion_event_handler
 *  
 *  (
 *  
 *  IN	VAPI_hca_hndl_t 	hca_hndl,
 *  IN	VAPI_cq_hndl_t 	cq,
 *  void *private_data 
 *  )
 *
 *************************************************************************/ 
VIP_ret_t EM_bind_completion_event_handler(/*IN*/ EM_hndl_t em, 
                                           /*IN*/ VAPI_completion_event_handler_t handler, /*IN*/void* private_data)
{
  MTL_TRACE4("%s(em=%p,handler=%p,priv=%p)\n", __func__,em,handler,private_data);
  if (handler == NULL) handler=EM_default_completion_event_handler;
  MOSAL_spinlock_dpc_lock(&em->compl_spl);
  em->compl_h=handler;
  em->compl_data=private_data;
  MOSAL_spinlock_unlock(&em->compl_spl);
  return VIP_OK;
}

/*************************************************************************
 * Function: EM_bind_evapi_completion_event_handler
 *
 * Arguments:
 *  em: The EM object handle
 *  cq_hndl:  the CQ handle
 *  handler: The handler function pointer (as defined in the IB-spec.)
 *  private_data: Pointer to private data (will be passed to event on call)
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQ_HNDL
 *  
 * Description:
 *  
 *  Bind the given completion event handler to the completion event of the associated 
 *  completion queue. If a NULL pointer is provided for the handler, the completion
 *  handler registration is CLEARED for the provided CQ.
 *  
 *  The handler is a pointer to a function as follows:
 *  
 *  void VAPI_completion_event_handler (
 *      IN	VAPI_hca_hndl_t  hca_hndl,
 *      IN	VAPI_cq_hndl_t 	 cq,
 *      IN  void *private_data 
 *  )
 *
 *************************************************************************/ 
VIP_ret_t EM_bind_evapi_completion_event_handler(/*IN*/ EM_hndl_t em,
                                                 /*IN*/ CQM_cq_hndl_t cq_hndl,
                                                 /*IN*/ VAPI_completion_event_handler_t handler, /*IN*/void* private_data)
{
  CQM_hndl_t cqm;
  VIP_ret_t rc;
  HH_cq_hndl_t cq_id;

  MTL_TRACE4("%s(em=%p,handler=%p,priv=%p)\n", __func__,em,handler,private_data);
  cqm = HOBKL_get_cqm(em->hob);
  MOSAL_spinlock_dpc_lock(&em->evapi_compl_spl);
  if (handler == NULL) {
    rc = CQM_get_cq_props(VIP_RSCT_IGNORE_CTX,cqm, cq_hndl, &cq_id, NULL);
    if ( rc != VIP_OK ) {
      MOSAL_spinlock_unlock(&em->evapi_compl_spl);
      MTL_ERROR4(MT_FLFMT("%s: Failed CQM_get_cq_props"),__func__);
      return rc;
    }
    rc = CQM_clear_cq_compl_hndlr_info(cqm, cq_hndl);
    cache_invalidate_entry(em, cq_id);
  }
  else {
    rc = CQM_set_cq_compl_hndlr_info(cqm, cq_hndl, handler, private_data);
  }
  MOSAL_spinlock_unlock(&em->evapi_compl_spl);
  return rc;
}


VIP_ret_t EM_set_async_event_handler(
                                  /*IN*/  EM_hndl_t                       em,
                                  /*IN*/  EM_async_ctx_hndl_t             hndl_ctx,
                                  /*IN*/  VAPI_async_event_handler_t      handler,
                                  /*IN*/  void*                           private_data,
                                  /*OUT*/ EVAPI_async_handler_hndl_t     *async_handler_hndl)
{
  VIP_ret_t rc=VIP_OK;
  EM_async_hob_ctx_t *async_hndl_ctx_p;
  EM_async_event_t* async_event_p;
  u_int16_t handler_key;
  

  FUNC_IN;
  if (handler == NULL) {
    MTL_ERROR1(MT_FLFMT("NULL handler function"));
    return VIP_EINVAL_PARAM;
  }

  if (async_handler_hndl == NULL) {
    MTL_ERROR1(MT_FLFMT("NULL async_handler_hndl"));
    return VIP_EINVAL_PARAM;
  }


  rc = VIP_array_find_hold(em->em_async_array, hndl_ctx, (VIP_array_obj_t *)&async_hndl_ctx_p);
  MTL_DEBUG3(MT_FLFMT("rc=%d"), rc);
  if ( rc!=VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: VIP_array_find_hold failed for hndl_ctx=%d "), __func__, hndl_ctx);
    return VIP_EINVAL_EM_HNDL;
  }
  /* build new handler element */
  /* first find the unique key */
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  if (async_hndl_ctx_p->last_key == EM_MAX_HANDLERS) {
    MOSAL_spinlock_unlock(&em->async_spl);
    VIP_array_find_release(em->em_async_array, hndl_ctx);
    MTL_ERROR1(MT_FLFMT("Got to max numbers of handlers for one hob (0xffff)"));
    return VIP_EBUSY;
  }
  handler_key = async_hndl_ctx_p->last_key++;
  MOSAL_spinlock_unlock(&em->async_spl);
  
  async_event_p = (EM_async_event_t*)MALLOC(sizeof(EM_async_event_t));
  if ( async_event_p == NULL) {
    VIP_array_find_release(em->em_async_array, hndl_ctx);
    return VIP_EAGAIN;
  }
  memset(async_event_p, 0, sizeof(EM_async_event_t));
  
  async_event_p->hndler_hndl_key = handler_key;
  async_event_p->async_clbk_hndl = handler;
  async_event_p->private_data = private_data;
  
  /* Add the new handler to the list this should be protected with spinlock */

  MOSAL_spinlock_dpc_lock(&em->async_spl);
  async_event_p->next = async_hndl_ctx_p->async_list_head;
  async_event_p->priv = NULL;
  if (async_hndl_ctx_p->async_list_head != NULL) {
    async_hndl_ctx_p->async_list_head->priv = async_event_p;
  }
  async_hndl_ctx_p->async_list_head = async_event_p;
  MOSAL_spinlock_unlock(&em->async_spl);
  
  /* The handler is build from: [key | array_index] */
  *async_handler_hndl = hndl_ctx | ((u_int32_t)handler_key) <<16;

  VIP_array_find_release(em->em_async_array, hndl_ctx);
  
  return VIP_OK;
}

VIP_ret_t EM_clear_async_event_handler(
                               /*IN*/ EM_hndl_t                   em,
                               /*IN*/ EM_async_ctx_hndl_t         hndl_ctx,
                               /*IN*/ EVAPI_async_handler_hndl_t async_handler_hndl)
{
  VIP_ret_t rc=VIP_OK;
  EM_async_hob_ctx_t *async_hndl_ctx_p;
  EM_async_event_t* async_event_p;
  u_int16_t handler_key;
  

  FUNC_IN;
  rc = VIP_array_find_hold(em->em_async_array, hndl_ctx, (VIP_array_obj_t *)&async_hndl_ctx_p);
  MTL_DEBUG3(MT_FLFMT("rc=%d"), rc);
  if ( rc!=VIP_OK ) {
    MTL_ERROR1("Inside " "%s: Unable to VIP_array_find_hold, hndl_ctx=%d, ret=%d\n", 
               __func__, hndl_ctx, rc);
    return VIP_EINVAL_EM_HNDL;
  }
  handler_key = (u_int16_t)(async_handler_hndl >> 16);
  /* first find the exact handle according to the unique key */
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  async_event_p = async_hndl_ctx_p->async_list_head;
  while (async_event_p !=NULL && (async_event_p->hndler_hndl_key != handler_key)){
    async_event_p = async_event_p->next;
  }
  if (async_event_p == NULL) {
    /* handler was not found */
    MOSAL_spinlock_unlock(&em->async_spl);
    VIP_array_find_release(em->em_async_array, hndl_ctx);
    return VIP_EINVAL_PARAM;
  }
  /* remove entry from the linked list */
  if (async_event_p == async_hndl_ctx_p->async_list_head) { /* first item in list */
    async_hndl_ctx_p->async_list_head = async_event_p->next;
    if (async_event_p->next != NULL) {
      async_event_p->next->priv = NULL;
    }
  }
  else {
    if (async_event_p->next != NULL) {
      async_event_p->next->priv = async_event_p->priv;
    }
    if (async_event_p->priv != NULL) {
      async_event_p->priv->next = async_event_p->next;
    }
  }
  /* Check if we can decreaze the last_key */
  if (handler_key == async_hndl_ctx_p->last_key) {
    async_hndl_ctx_p->last_key--;
  }
  MOSAL_spinlock_unlock(&em->async_spl);
  
  FREE(async_event_p);
  
  VIP_array_find_release(em->em_async_array, hndl_ctx);

  return VIP_OK;
}


VIP_ret_t EM_alloc_async_handler_ctx(
                               /*IN*/ EM_hndl_t                   em,
                               /*OUT*/ EM_async_ctx_hndl_t        *hndl_ctx_p)
{
  VIP_ret_t ret=VIP_OK;
  EM_async_hob_ctx_t *async_hndl_ctx_p;

  FUNC_IN;
  
  if (hndl_ctx_p == NULL) {
    MTL_ERROR1(MT_FLFMT("NULL hndl_ctx_p"));
    return VIP_EINVAL_PARAM;
  }

  /* check that we still have place for more context in the array */
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  if (em->number_of_hob_ctx == EM_MAX_HOB_CTX) {
    MOSAL_spinlock_unlock(&em->async_spl);
    MTL_ERROR1(MT_FLFMT("Achived max number of hob contexts: 0xffff"));
    return VIP_EAGAIN;
  }
  else {
    em->number_of_hob_ctx++;
    MOSAL_spinlock_unlock(&em->async_spl);
  }

  async_hndl_ctx_p = (EM_async_hob_ctx_t*)MALLOC(sizeof(EM_async_hob_ctx_t));
  if (async_hndl_ctx_p == NULL) {
    goto malloc_err;
    return VIP_EAGAIN;
  }
  memset(async_hndl_ctx_p, 0, sizeof(EM_async_hob_ctx_t));
  
  /* get handle to the new context */
  ret = VIP_array_insert(em->em_async_array, async_hndl_ctx_p, hndl_ctx_p);
  if (ret != VIP_OK) {
    MTL_ERROR1("Inside " "%s: Unable to insert handler context in array\n", __func__);
    goto insert_err;
  }
  
  MTL_DEBUG2("EM_alloc_async_handler_ctx: *hndl_ctx_p=%d, number_of_hob_ctx=%d\n", *hndl_ctx_p,
             em->number_of_hob_ctx);
  /* Add the new contex to the list of contexts */
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  async_hndl_ctx_p->next = em->hob_ctx_list_head;
  if (em->hob_ctx_list_head != NULL) {
    em->hob_ctx_list_head->priv = async_hndl_ctx_p;
  }

  em->hob_ctx_list_head = async_hndl_ctx_p;
  MOSAL_spinlock_unlock(&em->async_spl);

  return VIP_OK;

insert_err:  
  FREE(async_hndl_ctx_p);
malloc_err:
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  em->number_of_hob_ctx--;
  MOSAL_spinlock_unlock(&em->async_spl);
  
  return ret;

}

VIP_ret_t EM_dealloc_async_handler_ctx(
                               /*IN*/ EM_hndl_t                   em,
                               /*IN*/ EM_async_ctx_hndl_t         hndl_ctx)
{

  VIP_ret_t ret=VIP_OK;
  VIP_array_obj_t obj;
  EM_async_hob_ctx_t *async_hndl_ctx_p=NULL;

  FUNC_IN;
  /* release context from the array */
  ret = VIP_array_erase(em->em_async_array, hndl_ctx, &obj);

  MTL_DEBUG2("EM_dealloc_async_handler_ctx: hndl_ctx=%d, number_of_hob_ctx=%d\n", hndl_ctx,
             em->number_of_hob_ctx);
  if (ret != VIP_OK) {
    MTL_ERROR1("Inside " "%s: Unable to VIP_array_erase handler context; hndl_ctx=%d, ret = %d\n",
                __func__, hndl_ctx, ret);
    return ret;
  }

  async_hndl_ctx_p = (EM_async_hob_ctx_t*)obj;
  if (async_hndl_ctx_p == NULL) {
    MTL_ERROR1("Inside " "%s: got NULL async_hndl_ctx_p from the array\n", __func__);
    return VIP_ERROR;
  }
  
  /* Remove the new contex from the list of contexts */
  MOSAL_spinlock_dpc_lock(&em->async_spl);
  if (async_hndl_ctx_p == em->hob_ctx_list_head) { /* first item in list */
    em->hob_ctx_list_head = async_hndl_ctx_p->next;
    if (async_hndl_ctx_p->next != NULL) { /* might be the only intem in the list */
      async_hndl_ctx_p->next->priv = NULL;
    }
  }
  else {
    if (async_hndl_ctx_p->next != NULL) { /* might be the last item in the list */
      async_hndl_ctx_p->next->priv = async_hndl_ctx_p->priv;
    }
    async_hndl_ctx_p->priv->next = async_hndl_ctx_p->next;
  }
  /* decrese the number of contexts in the array */
  em->number_of_hob_ctx--;
  MOSAL_spinlock_unlock(&em->async_spl);
  
  EM_remove_hob_ctx(em, async_hndl_ctx_p);

  return VIP_OK;

}

VIP_ret_t EM_get_num_hca_handles(EM_hndl_t em, u_int32_t *num_hca_hndls)
{
  /* check attributes */
  if (em  == NULL) {
    return VIP_EINVAL_EM_HNDL;
  }
  
  if (num_hca_hndls == NULL) {
      return VIP_EINVAL_PARAM;
  }
  *num_hca_hndls = em->number_of_hob_ctx;
  return VIP_OK;
}

