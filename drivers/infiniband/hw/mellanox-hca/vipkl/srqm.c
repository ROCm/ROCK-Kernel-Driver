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

#define C_SRQM_C
#include <vapi_common.h>
#include <vip_array.h>
#include <VIP_rsct.h>
#include <vip.h>
#include <hobkl.h>
#include <pdm.h>
#include <hh.h>
#include "srqm.h"


typedef struct SRQM_t {
  HOBKL_hndl_t hca_hndl;
  HH_hca_hndl_t hh_hndl;
  u_int32_t  max_num_srq;         /* Maximum number of SRQs. Zero if SRQs are not supported. */
  PDM_hndl_t pdm_hndl;
  VIP_array_p_t srq_array;       /* the array which holds the maps the qp handles to qp structs */
} SRQM_t;

/* SRQ Attributes held by SRQM  */
typedef struct {
  HH_srq_hndl_t   hh_srq;
  PDM_pd_hndl_t   pd_hndl;         /* Protection domain handle */
  VAPI_srq_hndl_t  vapi_srq_hndl; /* For events reverse lookup */
    
  VIP_RSCT_rscinfo_t rsc_ctx;
  EM_async_ctx_hndl_t async_hndl_ctx; /* point to the asynch handler of this SRQ */
} SRQM_srq_t; 



VIP_ret_t SRQM_new(HOBKL_hndl_t hca_hndl, SRQM_hndl_t *srqm_hndl_p)
{
  SRQM_t 			*new_srqm;
  VIP_ret_t 		ret;
  VAPI_hca_cap_t 	hca_caps;

  /* get info about the HCA capabilities from hobkl */
  ret = HOBKL_query_cap(hca_hndl, &hca_caps);
  if ( ret != VIP_OK ) {
    MTL_ERROR2("%s: failed HOBKL_query_cap (%s).\n", __func__,VAPI_strerror_sym(ret));
    return VIP_EINVAL_HOB_HNDL;
  }

  new_srqm = TMALLOC(SRQM_t);
  if (new_srqm == NULL) {
    return HH_EAGAIN;
  }
  memset(new_srqm, 0, sizeof(SRQM_t));

  ret = VIP_array_create(hca_caps.max_num_srq>>CREATE_SHIFT, &(new_srqm->srq_array));
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array creation (%d=%s)"), __func__, 
               ret, VAPI_strerror_sym(ret));
    FREE(new_srqm);
    return ret;
  }

  new_srqm->hca_hndl = hca_hndl;
  new_srqm->hh_hndl = HOBKL_get_hh_hndl(hca_hndl);
  new_srqm->pdm_hndl = HOBKL_get_pdm(hca_hndl);
  new_srqm->max_num_srq= hca_caps.max_num_srq;

  *srqm_hndl_p = new_srqm;

  return VIP_OK;
}


static void srqc_free(void *srq_p)
{
  MTL_ERROR1(MT_FLFMT("%s: Releasing SRQ left-overs (HCA closed abnormally)"), __func__);
  FREE(srq_p);
}

VIP_ret_t SRQM_delete(SRQM_hndl_t srqm_hndl)
{
  VIP_ret_t ret;
  SRQM_t *srqm= (SRQM_t*)srqm_hndl;

  /* check attributes */
  if ( srqm == NULL || srqm->srq_array == NULL) {
    return VIP_EINVAL_SRQM_HNDL;
  }
  
  /* destroy the qp array */
  ret = VIP_array_destroy(srqm->srq_array, srqc_free);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed VIP_array_destroy (%d=%s)"), __func__, 
               ret, VAPI_strerror_sym(ret));
  }

  FREE(srqm);
  return VIP_OK;
}


VIP_ret_t SRQM_create_srq(VIP_RSCT_t        usr_ctx,
                          SRQM_hndl_t       srqm_hndl, 
                          VAPI_srq_hndl_t   vapi_srq_hndl, 
                          PDM_pd_hndl_t     pd_hndl,
                          EM_async_ctx_hndl_t async_hndl_ctx,
                          void                *srq_ul_resources_p,
                          SRQM_srq_hndl_t     *srq_hndl_p )
{
  VIP_ret_t rc;
  HH_ret_t hh_rc;
  HH_pd_hndl_t hh_pd;
  SRQM_srq_t *new_srq_p;
  VIP_RSCT_rschndl_t r_h;

  rc = PDM_add_object_to_pd(usr_ctx, srqm_hndl->pdm_hndl, pd_hndl, NULL, &hh_pd);
  if ( rc != VIP_OK ) return rc;

  new_srq_p= TMALLOC(SRQM_srq_t);
  if (new_srq_p == NULL) {
    rc= VIP_EAGAIN;
    goto failed_malloc;
  }

  hh_rc= HH_create_srq(srqm_hndl->hh_hndl, hh_pd, srq_ul_resources_p, &(new_srq_p->hh_srq));
  if (hh_rc != HH_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed HH_create_srq (%s)"), __func__, HH_strerror_sym(hh_rc));
    rc= (VIP_ret_t)hh_rc;
    goto failed_hh_create_srq;
  }

  new_srq_p->pd_hndl= pd_hndl;
  new_srq_p->vapi_srq_hndl= vapi_srq_hndl;
  new_srq_p->async_hndl_ctx= async_hndl_ctx;

  /* NOTE: assuming SRQ handles of HH are indices in an array, too... */
  rc= VIP_array_insert2hndl(srqm_hndl->srq_array, new_srq_p, new_srq_p->hh_srq);
  if (rc != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed VIP_array_insert2hndl (%d=%s)"), __func__,
               rc, VAPI_strerror_sym(rc));
    goto failed_vip_array;
  }
  
  *srq_hndl_p= new_srq_p->hh_srq;

  r_h.rsc_srq_hndl = *srq_hndl_p;
  VIP_RSCT_register_rsc(usr_ctx, &(new_srq_p->rsc_ctx), VIP_RSCT_SRQ, r_h);
  
  return VIP_OK;

  failed_vip_array:
    HH_destroy_srq(srqm_hndl->hh_hndl, new_srq_p->hh_srq);
  failed_hh_create_srq:
    FREE(new_srq_p);
  failed_malloc:
    PDM_rm_object_from_pd(srqm_hndl->pdm_hndl,pd_hndl);
    return rc;
}


VIP_ret_t SRQM_destroy_srq(VIP_RSCT_t usr_ctx, SRQM_hndl_t srqm_hndl, 
                           SRQM_srq_hndl_t srq_hndl)
{
  VIP_ret_t ret;
  HH_ret_t hh_ret;
  SRQM_t *srqm=srqm_hndl;
  SRQM_srq_t *srq;
  VIP_array_obj_t vobj;

  /* check arguments & get the qp structure */
  if (srqm_hndl == NULL || srqm_hndl->srq_array == NULL) {
    return VIP_EINVAL_SRQM_HNDL;
  }

  ret = VIP_array_find_hold(srqm->srq_array, srq_hndl, &vobj);
  if (ret != VIP_OK ) {
    MTL_ERROR2(MT_FLFMT("%s: Got invalid SRQ handle (0x%X)"), __func__, srq_hndl);
    return VIP_EINVAL_SRQ_HNDL;
  }
  srq= (SRQM_srq_t*)vobj;

  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&srq->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(srqm->srq_array,srq_hndl);
    MTL_ERROR2(MT_FLFMT("%s: invalid usr_ctx. srq handle=0x%X (%s)"),__func__,
               srq_hndl,VAPI_strerror_sym(ret));
    return ret;
  } 

  ret = VIP_array_find_release_erase(srqm->srq_array, srq_hndl, NULL);
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed VIP_array_erase_prep: got %s(%d) handle=%d"),
               VAPI_strerror_sym(ret), ret, srq_hndl);
    return ret;
  }
  
  hh_ret = HH_destroy_srq(srqm->hh_hndl, srq->hh_srq);
  if (hh_ret != HH_OK) {
    MTL_ERROR1(MT_FLFMT("%s: HH_destroy_srq returned %s for SRQn=0x%06X"), __func__,
               HH_strerror_sym(hh_ret), srq->hh_srq);
  }
    
  ret = PDM_rm_object_from_pd (srqm->pdm_hndl, srq->pd_hndl);
  if (ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: PDM_rm_object_from_pd failed pd_hndl=%d - %s"), __func__, 
               srq->pd_hndl, VAPI_strerror_sym(ret));
    /* Unexpected error... */
  }
  
  VIP_RSCT_deregister_rsc(usr_ctx,&srq->rsc_ctx,VIP_RSCT_SRQ);
  FREE(srq);

  return VIP_OK;
}



VIP_ret_t SRQM_query_srq (VIP_RSCT_t usr_ctx,SRQM_hndl_t srqm_hndl, SRQM_srq_hndl_t srq_hndl, 
                          u_int32_t *limit_p)
{
  VIP_ret_t ret;
  HH_ret_t hh_ret;
  SRQM_t *srqm=srqm_hndl;
  SRQM_srq_t *srq;
  VIP_array_obj_t vobj;

  /* check arguments & get the qp structure */
  if (srqm_hndl == NULL || srqm->srq_array == NULL) {
    return VIP_EINVAL_SRQM_HNDL;
  }

  ret = VIP_array_find_hold(srqm->srq_array, srq_hndl, &vobj);
  if (ret != VIP_OK ) {
    MTL_ERROR2(MT_FLFMT("%s: Got invalid SRQ handle (0x%X)"), __func__, srq_hndl);
    return VIP_EINVAL_SRQ_HNDL;
  }
  srq= (SRQM_srq_t*)vobj;

  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&srq->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(srqm->srq_array,srq_hndl);
    MTL_ERROR2(MT_FLFMT("%s: invalid usr_ctx. srq handle=0x%X (%s)"),__func__,
               srq_hndl,VAPI_strerror_sym(ret));
    return ret;
  } 

  hh_ret= HH_query_srq(srqm_hndl->hh_hndl, srq->hh_srq, limit_p);

  ret = VIP_array_find_release(srqm->srq_array, srq_hndl);
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed VIP_array_find_release: got %s handle=%d"),
               VAPI_strerror_sym(ret),srq_hndl);
    return ret;
  }

  return (VIP_ret_t)hh_ret;
}


VIP_ret_t SRQM_bind_qp_to_srq(VIP_RSCT_t usr_ctx, SRQM_hndl_t srqm_hndl,
                              SRQM_srq_hndl_t srq_hndl, HH_srq_hndl_t *hh_srq_p)
{
  VIP_ret_t ret;
  SRQM_t *srqm=srqm_hndl;
  SRQM_srq_t *srq;
  VIP_array_obj_t vobj;

  /* check arguments & get the qp structure */
  if (srqm_hndl == NULL || srqm->srq_array == NULL) {
    MTL_ERROR2(MT_FLFMT("%s: Got invalid SRQM handle (0x%p)"), __func__, srqm_hndl);
    return VIP_EINVAL_SRQM_HNDL;
  }

  ret = VIP_array_find_hold(srqm->srq_array, srq_hndl, &vobj);
  if (ret != VIP_OK ) {
    MTL_ERROR2(MT_FLFMT("%s: Got invalid SRQ handle (0x%X)"), __func__, srq_hndl);
    return VIP_EINVAL_SRQ_HNDL;
  }
  srq= (SRQM_srq_t*)vobj;

  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&srq->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(srqm->srq_array,srq_hndl);
    MTL_ERROR2(MT_FLFMT("%s: invalid usr_ctx. srq handle=0x%X (%s)"),__func__,
               srq_hndl,VAPI_strerror_sym(ret));
    return ret;
  } 

  *hh_srq_p= srq->hh_srq;
  /* no "VIP_array_find_release" - this is what holds the SRQ */
  return VIP_OK;
}

VIP_ret_t SRQM_unbind_qp_from_srq(SRQM_hndl_t srqm_hndl, 
                                  SRQM_srq_hndl_t srq_hndl)
{
  VIP_ret_t ret;
  SRQM_t *srqm=srqm_hndl;

  /* check arguments & get the qp structure */
  if (srqm_hndl == NULL || srqm->srq_array == NULL) {
    MTL_ERROR2(MT_FLFMT("%s: Got invalid SRQM handle (0x%p)"), __func__, srqm_hndl);
    return VIP_EINVAL_SRQM_HNDL;
  }

  ret = VIP_array_find_release(srqm->srq_array, srq_hndl);
  if (ret != VIP_OK ) {
    MTL_ERROR2(MT_FLFMT("%s: Got invalid SRQ handle (0x%X)"), __func__, srq_hndl);
    return VIP_EINVAL_SRQ_HNDL;
  }
  
  return VIP_OK;
}


VIP_ret_t SRQM_get_vapi_hndl(SRQM_hndl_t srqm_hndl, HH_srq_hndl_t hh_srq, 
                             VAPI_srq_hndl_t *vapi_srq_p, EM_async_ctx_hndl_t *async_hndl_ctx)
{
  VIP_ret_t ret;
  SRQM_t *srqm=srqm_hndl;
  SRQM_srq_hndl_t srq_hndl= (SRQM_srq_hndl_t)hh_srq; /* 1-to-1 mapping */
  SRQM_srq_t *srq;
  VIP_array_obj_t vobj;

  /* check arguments & get the qp structure */
  if (srqm_hndl == NULL || srqm->srq_array == NULL) {
    return VIP_EINVAL_SRQM_HNDL;
  }

  ret = VIP_array_find_hold(srqm->srq_array, srq_hndl, &vobj);
  if (ret != VIP_OK ) {
    MTL_ERROR2(MT_FLFMT("%s: Got invalid SRQ handle (0x%X)"), __func__, srq_hndl);
    return VIP_EINVAL_SRQ_HNDL;
  }
  srq= (SRQM_srq_t*)vobj;

  *vapi_srq_p= srq->vapi_srq_hndl;
  *async_hndl_ctx= srq->async_hndl_ctx;

  ret = VIP_array_find_release(srqm->srq_array, srq_hndl);
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("failed VIP_array_find_release: got %s handle=%d"),
               VAPI_strerror_sym(ret),srq_hndl);
    return ret;
  }

  return VIP_OK;
}
