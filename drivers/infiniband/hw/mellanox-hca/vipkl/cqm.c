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
 
 
// #define MTPERF
#include <mtperf.h>
MTPERF_NEW_SEGMENT(CQM_get_cq_by_id_hash_find,2000);
MTPERF_NEW_SEGMENT(CQM_get_cq_by_id_array_find, 2000);
 
#include <vip_hash.h>
#include <vip_array.h>
#include <vapi_common.h>
#include <hh.h>
#include <hobkl.h>
#include <pdm.h>
#include <em.h>
#include "cqm.h"

struct CQM_t {
  VIP_hash_p_t id2hdl;
  VIP_array_p_t array;
  MM_hndl_t mm_hndl;
  HH_hca_hndl_t hh;
  HOBKL_hndl_t hob;
  VAPI_hca_hndl_t vapi_hca;
};

struct CQM_cq_t {
  HH_cq_hndl_t cq_id;
  VAPI_cq_hndl_t vapi_cq_hndl;
  VAPI_cqe_num_t num_o_entries;
  VAPI_completion_event_handler_t   completion_handler;  /* private completion hndlr for this cq */
  void *private_data;   /* priv data for completion handler */
  EVAPI_destroy_cq_cbk_t destroy_cbk; /* To be invoked on CQ destruction */
  void *destroy_cbk_private_data;
  VIP_RSCT_rscinfo_t rsc_ctx;
  EM_async_ctx_hndl_t async_hndl_ctx; /* point to the asynch handler of this CQ */
};
/* Stub: cq_id */
HH_cq_hndl_t maxcqid=0;

static void VIP_free(void* p)
{
    MTL_ERROR1(MT_FLFMT("CQM delete:found unreleased cq"));        
    FREE(p); 
}


/*
 *  CQM_new
 */
VIP_ret_t CQM_new(/*IN*/ HOBKL_hndl_t hca_hndl,/*OUT*/ CQM_hndl_t *cqm_p)
{
  VIP_ret_t ret=VIP_OK;
  VAPI_hca_cap_t hca_caps;
  CQM_hndl_t cqm;
  
  MTL_DEBUG3("CALLED " "%s \n", __func__);

  /* get info about the HCA capabilities from hobkl */
  ret = HOBKL_query_cap(hca_hndl, &hca_caps);
  if ( ret != VIP_OK ) {
    MTL_ERROR2("%s: failed HOBKL_query_cap (%s).\n", __func__, VAPI_strerror_sym(ret));
    return VIP_EINVAL_HOB_HNDL;
  }

  cqm = (CQM_hndl_t)MALLOC(sizeof(struct CQM_t));
  if ( cqm == NULL ) return VIP_EAGAIN;

  MTL_DEBUG3("%s: maximum num CQs = %d\n", __func__, hca_caps.max_num_cq);
  
  /* sanity check - protect from buggy HCA drivers... */
  if ( hca_caps.max_num_cq < 1 )  {
    MTL_ERROR1("%s: invalid HCA caps (max_num_cq=%d).\n", __func__, hca_caps.max_num_cq);
    return VIP_EINVAL_HOB_HNDL;
  }

  /* this HASH maps between CQM cq handle to HH handle */
  ret = VIP_hash_create_maxsize(hca_caps.max_num_cq>>CREATE_SHIFT, hca_caps.max_num_cq,&cqm->id2hdl);
  if (ret != VIP_OK) {
    FREE(cqm);
    return ret;
  }

  ret = VIP_array_create_maxsize(hca_caps.max_num_cq>>CREATE_SHIFT,hca_caps.max_num_cq, &cqm->array);
  if ( ret != VIP_OK ) {
    VIP_hash_destroy(cqm->id2hdl);
    FREE(cqm);
    return ret;
  }
  MTL_DEBUG3("Inside " "%s initialized containers\n", __func__);

  cqm->hob = hca_hndl;
  cqm->mm_hndl = HOBKL_get_mm(hca_hndl);
  cqm->hh = HOBKL_get_hh_hndl(hca_hndl);

  MTL_DEBUG3("Inside " "%s initialized managers\n", __func__);

  if (cqm_p) *cqm_p = cqm;
  return ret;
}

VIP_ret_t CQM_delete(/*IN*/ CQM_hndl_t cqm)
{
  //VIP_array_handle_t cq_hndl;
  //VIP_common_ret_t ret;
  //struct CQM_cq_t *cq;

  if (cqm == NULL) {
    MTL_ERROR1("%s called with NULL parameter\n", __func__);
    return VIP_EINVAL_CQM_HNDL;
  }
  MTL_DEBUG3("CALLED " "%s(%p);\n", __func__, cqm);

  VIP_array_destroy(cqm->array,VIP_free);
  VIP_hash_destroy(cqm->id2hdl);
  FREE(cqm);

  return VIP_OK;
}

VIP_ret_t  CQM_set_vapi_hca_hndl(/*IN*/ CQM_hndl_t cqm, /*IN*/VAPI_hca_hndl_t hca_hndl)
{
  if (cqm == NULL)  return VIP_EINVAL_CQM_HNDL;
  cqm->vapi_hca = hca_hndl;
  return VIP_OK;
}


VIP_ret_t CQM_create_cq(VIP_RSCT_t usr_ctx,
                        /*IN*/ CQM_hndl_t cqm_hndl,
                        /*IN*/ VAPI_cq_hndl_t vapi_cq_hndl,
                        /*IN*/ MOSAL_protection_ctx_t  usr_prot_ctx,
                        /*IN*/ EM_async_ctx_hndl_t async_hndl_ctx,
                        /*IN*/ void     * cq_ul_resources_p,
                        /*OUT*/ CQM_cq_hndl_t *cq_hndl_p,
                        /*OUT*/ HH_cq_hndl_t  *cq_id_p)
{
  VIP_ret_t ret=VIP_OK;
  struct CQM_cq_t *cqm_cq;
  CQM_cq_hndl_t hdl;
  HH_ret_t hh_ret;
  VIP_RSCT_rschndl_t r_h;

  cqm_cq = (struct CQM_cq_t*)MALLOC(sizeof(struct CQM_cq_t));
  if ( cqm_cq == NULL ) {
    ret = VIP_EAGAIN;
    goto malloc_cq_lbl;
  }
  memset(cqm_cq, 0, sizeof(struct CQM_cq_t));
  cqm_cq->vapi_cq_hndl = vapi_cq_hndl;

  hh_ret = HH_create_cq(cqm_hndl->hh, usr_prot_ctx, cq_ul_resources_p, &cqm_cq->cq_id);
  if (hh_ret != HH_OK) {
    MTL_ERROR1("Inside " "%s: Unable to register HH CQ. hh_ret = %d\n", __func__, hh_ret);
    ret = (hh_ret >= VIP_COMMON_ERROR_MIN ? VIP_EGEN : hh_ret);
    goto hh_cq_lbl;
  }

  MTL_DEBUG3("Inside " "%s HH created CQ %u\n", __func__, cqm_cq->cq_id);
  
  hh_ret = HH_query_cq(cqm_hndl->hh, cqm_cq->cq_id, &(cqm_cq->num_o_entries));
  if (hh_ret != HH_OK) {
    MTL_ERROR1("Inside " "%s: Unable to retrieve HH CQ info. hh_ret = %d\n", __func__, hh_ret);
    ret= (hh_ret >= VIP_COMMON_ERROR_MIN ? VIP_EGEN : hh_ret);
    goto query_lbl;
  }

  /* get handle to the new cq */
  ret = VIP_array_insert(cqm_hndl->array, cqm_cq, &hdl);
  if (ret != VIP_OK) {
    MTL_ERROR1("Inside " "%s: Unable to insert CQ in array\n", __func__);
    goto array_lbl;
  }

  ret = VIP_hash_insert(cqm_hndl->id2hdl, cqm_cq->cq_id, hdl);
  if ( ret != VIP_OK ) {
    MTL_ERROR1("Inside " "%s: Unable to insert CQ by key %d\n", __func__, cqm_cq->cq_id);
    goto hash_lbl;
  }

  MTL_DEBUG3("Inside " "%s CQ handle %u associated with ID %u\n", __func__, hdl, cqm_cq->cq_id);

  cqm_cq->async_hndl_ctx = async_hndl_ctx;

  if ( cq_id_p ) *cq_id_p = cqm_cq->cq_id; 
  if ( cq_hndl_p ) *cq_hndl_p = hdl;

  MTL_DEBUG3("Inside " "%s CQ %u created successfully\n", __func__, hdl);
  r_h.rsc_cq_hndl = hdl;
  VIP_RSCT_register_rsc(usr_ctx,&cqm_cq->rsc_ctx,VIP_RSCT_CQ,r_h);
  return ret;


hash_lbl:
  VIP_array_erase(cqm_hndl->array, hdl, NULL);
array_lbl:
query_lbl:  
  HH_destroy_cq(cqm_hndl->hh,cqm_cq->cq_id);
hh_cq_lbl:
  FREE(cqm_cq);
malloc_cq_lbl:
  return ret;
}

VIP_ret_t CQM_destroy_cq(VIP_RSCT_t usr_ctx,/*IN*/ CQM_hndl_t cqm_hndl,/*IN*/ CQM_cq_hndl_t cq_hndl,
                         MT_bool in_rsct_cleanup)
{
  struct CQM_cq_t *cqm_cq;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(cqm_hndl->array,cq_hndl,(VIP_array_obj_t *)&cqm_cq);
  if (ret != VIP_OK ) {
    return VIP_EINVAL_CQ_HNDL;
  }
  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&cqm_cq->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(cqm_hndl->array,cq_hndl);
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. cq handle=0x%x (%s)"),__func__,cq_hndl,VAPI_strerror_sym(ret));
    return ret;
  } 
  if (in_rsct_cleanup) {
    /* If this call came from RSCT we want to unbind the completion event handler (if any) */
    EM_bind_evapi_completion_event_handler(cqm_hndl->hob->em,cq_hndl,NULL,NULL);
  }
  
  ret = VIP_array_find_release_erase_prepare(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cqm_cq);
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed VIP_array_erase_prep: got %s handle=%d"),VIP_common_strerror_sym(ret),cq_hndl);
    return ret;
  }
  if (cqm_cq->destroy_cbk) {
    /* Invoke "destroy callback" before this CQ handle is put back to free-pool by erase_done */
    cqm_cq->destroy_cbk(cqm_hndl->vapi_hca, cq_hndl, cqm_cq->destroy_cbk_private_data);
  }
  ret = VIP_array_erase_done(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cqm_cq);
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed VIP_array_erase_done: got %s handle=%d"),
               VIP_common_strerror_sym(ret),cq_hndl);
    return ret;
  }

  if ( VIP_hash_erase(cqm_hndl->id2hdl, cqm_cq->cq_id, NULL) != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed to erase hash entry: 0x%x"), cqm_cq->cq_id);
  }
 
  if ( HH_destroy_cq(cqm_hndl->hh,cqm_cq->cq_id) != HH_OK ) {
    MTL_ERROR1(MT_FLFMT("failed to destroy CQ: 0x%x"), cqm_cq->cq_id);
  }

  VIP_RSCT_deregister_rsc(usr_ctx,&cqm_cq->rsc_ctx,VIP_RSCT_CQ);
  FREE(cqm_cq);
  return VIP_OK;
}

VIP_ret_t CQM_get_cq_props(VIP_RSCT_t usr_ctx,/*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*OUT*/ HH_cq_hndl_t *cq_id_p,
    /*OUT*/ VAPI_cqe_num_t *num_o_entries_p
    )
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret;

  MTL_DEBUG1(MT_FLFMT("inside  CQM_get_cq_props \n"));
  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if ( ret != VIP_OK ) {
    MTL_ERROR5(MT_FLFMT("%s: Failed VIP_array_find_hold for cq_hndl=0x%X (%s)"),__func__,
               cq_hndl,VIP_common_strerror_sym(ret));
    return (ret == VIP_EINVAL_HNDL) ? VIP_EINVAL_CQ_HNDL : ret;
  }
  MTL_DEBUG1(MT_FLFMT("before check ctx \n"));
  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&cq->rsc_ctx);
  if (ret != VIP_OK) {
      VIP_array_find_release(cqm_hndl->array,cq_hndl);
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. cq handle=0x%x (%s)"),__func__,cq_hndl,VAPI_strerror_sym(ret));
    return ret;
  }

  MTL_DEBUG1(MT_FLFMT("after check ctx \n"));
  MTL_TRACE6(MT_FLFMT("VIP_array_find_hold success: handle = 0x%x"), cq_hndl);
  if ( cq_id_p ) *cq_id_p = cq->cq_id;
  if ( num_o_entries_p ) *num_o_entries_p = cq->num_o_entries - 1;
  VIP_array_find_release(cqm_hndl->array, cq_hndl);
  MTL_TRACE6(MT_FLFMT("calling VIP_array_find_release: handle = 0x%x"), cq_hndl);
  return VIP_OK;
}

VIP_ret_t CQM_get_cq_by_id(
                           /*IN*/ HH_cq_hndl_t cq_id,
                           /*OUT*/ cq_params_t *cqprms_p
    )
{
  VIP_ret_t ret;
  struct CQM_cq_t *cq_p;

  MTPERF_TIME_START(CQM_get_cq_by_id_hash_find);
  ret = VIP_hash_find(cqprms_p->cqm_hndl->id2hdl, cq_id, &cqprms_p->cqm_cq_hndl);
  MTPERF_TIME_END(CQM_get_cq_by_id_hash_find);
  if (ret != VIP_OK) {
    return VIP_EINVAL_CQ_HNDL;
  } 
  /* will be relased after handler is called */
  MTPERF_TIME_START(CQM_get_cq_by_id_array_find);
  ret = VIP_array_find_hold(cqprms_p->cqm_hndl->array, cqprms_p->cqm_cq_hndl, (VIP_array_obj_t *)&cq_p);
  MTPERF_TIME_END(CQM_get_cq_by_id_array_find);
  if ( ret != VIP_OK ) {
    return ret;
  }
  MTL_TRACE6(MT_FLFMT("VIP_array_find_hold success: handle = 0x%x"), cqprms_p->cqm_cq_hndl);
  cqprms_p->vapi_cq_hndl = cq_p->vapi_cq_hndl;
  cqprms_p->completion_handler = cq_p->completion_handler;
  cqprms_p->private_data = cq_p->private_data;
  cqprms_p->cq_p = cq_p;
  cqprms_p->async_hndl_ctx = cq_p->async_hndl_ctx;
  VIP_array_find_release(cqprms_p->cqm_hndl->array, cqprms_p->cqm_cq_hndl);
  MTL_TRACE6(MT_FLFMT("calling VIP_array_find_release: handle = 0x%x"), cqprms_p->cqm_cq_hndl);
  return ret;
}


void CQM_get_handler_info(cq_params_t *cqprms_p)
{
  cqprms_p->completion_handler = cqprms_p->cq_p->completion_handler;
  cqprms_p->vapi_cq_hndl = cqprms_p->cq_p->vapi_cq_hndl;
  cqprms_p->private_data = cqprms_p->cq_p->private_data;
}


VIP_ret_t CQM_resize_cq(
    VIP_RSCT_t usr_ctx,
    /*IN*/ CQM_hndl_t cqm,
  /*IN*/ CQM_cq_hndl_t cq_hndl,
  /*IN/OUT*/ void     * cq_ul_resources_p
)
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret;
  HH_ret_t hhret;

  ret = VIP_array_find_hold(cqm->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if ( ret != VIP_OK ) {
    return ret;
  }

  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&cq->rsc_ctx);
  if (ret != VIP_OK) {
     VIP_array_find_release(cqm->array,cq_hndl);
        MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. cq handle=0x%x (%s)"),__func__,cq_hndl,VAPI_strerror_sym(ret));
        return ret;
  } 

  hhret= HH_resize_cq(cqm->hh,cq->cq_id,cq_ul_resources_p);
  if (hhret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("Failed HH_resize_cq for HH cq 0x%X (%s)"),
               cq->cq_id,HH_strerror_sym(hhret));
    VIP_array_find_release(cqm->array,cq_hndl);
    return (VIP_ret_t)hhret;
  }
  
  VIP_array_find_release(cqm->array, cq_hndl);
  return VIP_OK;
}

VIP_ret_t CQM_bind_qp_to_cq( VIP_RSCT_t usr_ctx,
    /*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*IN*/ QPM_qp_hndl_t qp,
    /*OUT*/ HH_cq_hndl_t *hh_cq_id_p)
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret=VIP_OK;

  
  /* will be released in CQM_unbind_qp_from_cq */
  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if ( ret != VIP_OK ) {
    return ret;
  }
  MTL_TRACE6(MT_FLFMT("VIP_array_find_hold success: handle = 0x%x"), cq_hndl);
  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&cq->rsc_ctx);
  if (ret != VIP_OK) {
        VIP_array_find_release(cqm_hndl->array,cq_hndl);
        MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. cq handle=0x%x (%s)"),__func__,cq_hndl,VAPI_strerror_sym(ret));
        return ret;
  }

  MTL_DEBUG3("%s: BIND- after VIP_array_find cq_hndl=%d, cq=%p, cq_num=%d\n", __func__, cq_hndl, cq, cq->cq_id);

  if ( hh_cq_id_p ) *hh_cq_id_p = cq->cq_id;
  return ret;
}

VIP_ret_t CQM_unbind_qp_from_cq(
    /*IN*/ CQM_hndl_t cqm,
    /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*IN*/ QPM_qp_hndl_t qp)
{
  MTL_TRACE6(MT_FLFMT("calling VIP_array_find_release: handle = 0x%x"), cq_hndl);
  return VIP_array_find_release(cqm->array, cq_hndl);
}

VIP_ret_t CQM_get_cq_compl_hndlr_info(/*IN*/ CQM_hndl_t cqm_hndl,
                                      /*IN*/ CQM_cq_hndl_t cq_hndl,
                                      /*OUT*/ VAPI_completion_event_handler_t *completion_handler_p,
                                      /*OUT*/ void **private_data_p)
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret;
  VAPI_completion_event_handler_t completion_handler;  /* private completion hndlr for this cq */
  void *private_data;   /* priv data for completion handler */

  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if ( ret != VIP_OK ) {
    return ret;
  }
  MTL_TRACE6(MT_FLFMT("VIP_array_find_hold success: handle = 0x%x"), cq_hndl);
  completion_handler = cq->completion_handler;
  private_data = cq->private_data;
  MTL_TRACE6(MT_FLFMT("calling VIP_array_find_release: handle = 0x%x"), cq_hndl);
  VIP_array_find_release(cqm_hndl->array, cq_hndl);
  if ( completion_handler == NULL ) {
    return VIP_EINVAL_CQ_HNDL; /* no completion handler registered for this CQ */
  }
  if ( completion_handler_p ) *completion_handler_p = completion_handler;
  if ( private_data_p ) *private_data_p = private_data;
  return VIP_OK;
}

VIP_ret_t CQM_set_cq_compl_hndlr_info(/*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*IN*/ VAPI_completion_event_handler_t  completion_handler,
    /*IN*/ void * private_data
    )
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if (ret == VIP_EINVAL_HNDL) {
    return VIP_EINVAL_CQ_HNDL;
  }
  MTL_TRACE6(MT_FLFMT("VIP_array_find_hold success: handle = 0x%x"), cq_hndl);
  if (cq->completion_handler != NULL) {
    MTL_TRACE6(MT_FLFMT("calling VIP_array_find_release: handle = 0x%x"), cq_hndl);
    VIP_array_find_release(cqm_hndl->array, cq_hndl);
    return VIP_EBUSY;
  }
  cq->completion_handler = completion_handler;
  cq->private_data = private_data;
  
  /* CQ handle is released on CQM_clear_cq_compl_hndlr_info */ 
  return VIP_OK;
}

VIP_ret_t CQM_clear_cq_compl_hndlr_info(/*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl
    )
{
  struct CQM_cq_t *cq;
  MT_bool was_bound;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if (ret == VIP_EINVAL_HNDL) {
    return VIP_EINVAL_CQ_HNDL;
  }
  MTL_TRACE6(MT_FLFMT("VIP_array_find_hold success: handle = 0x%x"), cq_hndl);
  was_bound= (cq->completion_handler != NULL);
  /* "bound" is checked in order to decide whether CQ's reference count should be decremented */
  cq->completion_handler = NULL;
  cq->private_data = NULL;
  MTL_TRACE6(MT_FLFMT("calling VIP_array_find_release: handle = 0x%x"), cq_hndl);
  VIP_array_find_release(cqm_hndl->array, cq_hndl);
  if (was_bound) {
    MTL_TRACE6(MT_FLFMT("calling VIP_array_find_release: handle = 0x%x"), cq_hndl);
    VIP_array_find_release(cqm_hndl->array, cq_hndl); /* Releasing CQ */
  }
  return VIP_OK;
}

VIP_ret_t CQM_set_destroy_cq_cbk(
  /*IN*/ CQM_hndl_t               cqm_hndl,
  /*IN*/ CQM_cq_hndl_t            cq_hndl,
  /*IN*/ EVAPI_destroy_cq_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
)
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if (ret == VIP_EINVAL_HNDL) {
    return VIP_EINVAL_CQ_HNDL;
  }

  /* Under API assumptions - we do not protect against a race of simultaneous set for the same CQ */
  /* (It is a trusted kernel code that invokes this - at most it will harm itself) */
  if (cq->destroy_cbk != NULL) {
    VIP_array_find_release(cqm_hndl->array, cq_hndl);
    return VIP_EBUSY;
  }
  cq->destroy_cbk = cbk_func;
  cq->destroy_cbk_private_data = private_data;
  
  VIP_array_find_release(cqm_hndl->array, cq_hndl);
  return VIP_OK;
}
 
VIP_ret_t CQM_clear_destroy_cq_cbk(
  /*IN*/ CQM_hndl_t               cqm_hndl,
  /*IN*/ CQM_cq_hndl_t            cq_hndl
)
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if (ret == VIP_EINVAL_HNDL) {
    return VIP_EINVAL_CQ_HNDL;
  }
  
  cq->destroy_cbk = NULL;
  cq->destroy_cbk_private_data = NULL;
  
  VIP_array_find_release(cqm_hndl->array, cq_hndl);
  return VIP_OK;
}

VIP_ret_t CQM_get_num_cqs(CQM_hndl_t cqm_hndl, u_int32_t *num_cqs)
{
  /* check attributes */
  if ( cqm_hndl == NULL || cqm_hndl->array == NULL) {
    return VIP_EINVAL_QPM_HNDL;
  }
  
  if (num_cqs == NULL) {
      return VIP_EINVAL_PARAM;
  }
  *num_cqs = VIP_array_get_num_of_objects(cqm_hndl->array);
  return VIP_OK;
}



VIP_ret_t CQM_get_hh_cq_num(CQM_hndl_t cqm_hndl, CQM_cq_hndl_t cq_hndl, HH_cq_hndl_t *cq_id_p)
{
  struct CQM_cq_t *cq;
  VIP_ret_t ret;

  ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
  if ( ret != VIP_OK ) {
    return (ret == VIP_EINVAL_HNDL) ? VIP_EINVAL_CQ_HNDL : ret;
  }

  if ( cq_id_p ) *cq_id_p = cq->cq_id;
  VIP_array_find_release(cqm_hndl->array, cq_hndl);
  return VIP_OK;
}

#if defined(MT_SUSPEND_QP)
VIP_ret_t
CQM_suspend_cq (VIP_RSCT_t usr_ctx,CQM_hndl_t cqm_hndl, CQM_cq_hndl_t cq_hndl, MT_bool do_suspend)
{
    VIP_ret_t ret;
    struct CQM_cq_t *cq;

    if (cqm_hndl == NULL || cqm_hndl->array == NULL) {
      return VIP_EINVAL_CQ_HNDL;
    }

    ret = VIP_array_find_hold(cqm_hndl->array, cq_hndl, (VIP_array_obj_t *)&cq);
    if ( ret!=VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: invalid cq handle=0x%x (%s)"),__func__,cq_hndl,VIP_common_strerror_sym(ret));
      return VIP_EINVAL_CQ_HNDL;
    }

    ret = HH_suspend_cq (cqm_hndl->hh, cq->cq_id, do_suspend);
    if (ret != HH_OK) {
      VIP_array_find_release(cqm_hndl->array, cq_hndl);
      MTL_ERROR1(MT_FLFMT("%s: HH_suspend_cq failed. cq handle=0x%x (%d:%s)"),
                 __func__,cq_hndl,ret,HH_strerror_sym(ret));
      return (ret >= VIP_COMMON_ERROR_MIN ? VIP_EGEN : ret);
    }

    VIP_array_find_release(cqm_hndl->array, cq_hndl);
    return VIP_OK;
}
#endif
