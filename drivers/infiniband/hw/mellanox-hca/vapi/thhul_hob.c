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

#define C_THHUL_HOB_C

#include <mosal.h>
#include <MT23108.h>
#include "thhul_hob.h"
#include <thhul_pdm.h>
#include <thhul_cqm.h>
#include <thhul_srqm.h>
#include <thhul_qpm.h>
#include <thhul_mwm.h>
#include <uar.h>

#define GET_HOB(hhul_dev) (hhul_dev ? ((THHUL_hob_t)(hhul_dev->device)) : NULL)

struct THHUL_hob_st { /* *THHUL_hob_t; */
  HHUL_hca_dev_t hhul_hca;        /* HHUL's device context */
  THH_hca_ul_resources_t ul_res;  /* Resources allocated by HH_alloc_ul_resources() */
  /* Included objects */
  THH_uar_t uar;
  THHUL_pdm_t pdm;
  THHUL_cqm_t cqm;
  THHUL_qpm_t qpm;
  THHUL_srqm_t srqm;
  THHUL_mwm_t mwm;

  /* global_resource_cnt:
   * A counter for resources set up for within this THHUL_hob context.
   * This counter enables avoiding object destruction in case there are still 
   * resources that were not freed (e.g. QPs).
   * Each THHUL object is responsible to update this counter using THHUL_hob_res_add/rem */
  u_int32_t global_resource_cnt;
  MOSAL_mutex_t cntr_lock;  /* A lock for assuring atomicity of the counter lock */
};

static HHUL_if_ops_t thhul_ops=
{ 
  THHUL_hob_destroy         /* HHULIF_cleanup_user_level*/,  
  THHUL_pdm_alloc_pd_prep   /* HHULIF_alloc_pd_prep     */,
  THHUL_pdm_alloc_pd_avs_prep   /* HHULIF_alloc_pd_avs_prep     */,
  THHUL_pdm_alloc_pd_done   /* HHULIF_alloc_pd_done     */,
  THHUL_pdm_free_pd_prep    /* HHULIF_free_pd_prep      */,
  THHUL_pdm_free_pd_done    /* HHULIF_free_pd_done      */,
  THHUL_mwm_alloc_mw        /* HHULIF_alloc_mw          */,
  THHUL_mwm_bind_mw         /* HHULIF_bind_mw           */,
  THHUL_mwm_free_mw         /* HHULIF_free_mw           */,
  THHUL_pdm_create_ud_av    /* HHULIF_create_ud_av      */,
  THHUL_pdm_modify_ud_av    /* HHULIF_modify_ud_av      */,
  THHUL_pdm_query_ud_av     /* HHULIF_query_ud_av       */,
  THHUL_pdm_destroy_ud_av   /* HHULIF_destroy_ud_av     */,
  THHUL_cqm_create_cq_prep  /* HHULIF_create_cq_prep    */,
  THHUL_cqm_create_cq_done  /* HHULIF_create_cq_done    */,
  THHUL_cqm_resize_cq_prep  /* HHULIF_resize_cq_prep    */,
  THHUL_cqm_resize_cq_done  /* HHULIF_resize_cq_done    */,
  THHUL_cqm_poll4cqe        /* HHULIF_poll4cqe          */,
  THHUL_cqm_peek_cq         /* HHULIF_peek_cq           */,
  THHUL_cqm_req_comp_notif  /* HHULIF_req_comp_notif    */,
  THHUL_cqm_req_ncomp_notif /* HHULIF_req_ncomp_notif   */,
  THHUL_cqm_destroy_cq_done /* HHULIF_destroy_cq_done   */,
  THHUL_qpm_create_qp_prep  /* HHULIF_create_qp_prep    */,
  THHUL_qpm_special_qp_prep /* HHULIF_special_qp_prep   */,
  THHUL_qpm_create_qp_done  /* HHULIF_create_qp_done    */,
  THHUL_qpm_modify_qp_done  /* HHULIF_modify_qp_done    */,
  THHUL_qpm_post_send_req   /* HHULIF_post_send_req     */,
  THHUL_qpm_post_inline_send_req /* HHULIF_post_inline_send_req */,
  THHUL_qpm_post_send_reqs  /* HHULIF_post_send_reqs    */,
  THHUL_qpm_post_gsi_send_req /* HHULIF_post_gsi_send_req */,
  THHUL_qpm_post_recv_req   /* HHULIF_post_recv_req     */,
  THHUL_qpm_post_recv_reqs  /* HHULIF_post_recv_reqs    */,
  THHUL_qpm_destroy_qp_done /* HHULIF_destroy_qp_done   */,
  THHUL_srqm_create_srq_prep/* HHULIF_create_srq_prep   */,
  THHUL_srqm_create_srq_done/* HHULIF_create_srq_done   */,
  THHUL_srqm_destroy_srq_done/* HHULIF_destroy_srq_done */,
  THHUL_srqm_post_recv_reqs  /* HHULIF_post_srq         */ 
};

/* Private functions prototypes */
static HH_ret_t alloc_hob_context(
  THHUL_hob_t *new_hob_p, 
  THH_hca_ul_resources_t *hca_ul_resources_p
);


/**********************************************************************************************
 *                    Public API Functions (defined in thhul_hob.h)
 **********************************************************************************************/

HH_ret_t THHUL_hob_create(
  /*IN*/ void/*THH_hca_ul_resources_t*/ *hca_ul_resources_p,
  /*IN*/ u_int32_t	    device_id,
  /*OUT*/ HHUL_hca_hndl_t *hca_p 
)
{
  THHUL_hob_t new_hob;
  HH_ret_t rc;

  if (hca_ul_resources_p == NULL) {
    MTL_ERROR1("THHUL_hob_create: NULL hca_ul_resources_p.\n");
    return HH_EINVAL;
  }

  /* Allocate associated memory resources and included objects */
  rc= alloc_hob_context(&new_hob,(THH_hca_ul_resources_t*)hca_ul_resources_p);
  if (rc != HH_OK)  return rc;

  /* Fill the HHUL_hca_dev_t structure */
  new_hob->hhul_hca.hh_hndl= ((THH_hca_ul_resources_t*)hca_ul_resources_p)->hh_hca_hndl;
  new_hob->hhul_hca.dev_desc= "InfiniHost(Tavor)";
  new_hob->hhul_hca.vendor_id= MT_MELLANOX_IEEE_VENDOR_ID;
  new_hob->hhul_hca.dev_id= device_id; 
  
  new_hob->hhul_hca.hw_ver= ((THH_hca_ul_resources_t*)hca_ul_resources_p)->version.hw_ver;
  new_hob->hhul_hca.fw_ver= 
    ((THH_hca_ul_resources_t*)hca_ul_resources_p)->version.fw_ver_major; 
  new_hob->hhul_hca.fw_ver= (new_hob->hhul_hca.fw_ver << 16) |
    ((THH_hca_ul_resources_t*)hca_ul_resources_p)->version.fw_ver_minor; 
  new_hob->hhul_hca.fw_ver= (new_hob->hhul_hca.fw_ver << 16) |
    ((THH_hca_ul_resources_t*)hca_ul_resources_p)->version.fw_ver_subminor; 
  new_hob->hhul_hca.if_ops= &thhul_ops;
  new_hob->hhul_hca.hca_ul_resources_sz= sizeof(THH_hca_ul_resources_t);
  new_hob->hhul_hca.pd_ul_resources_sz= sizeof(THH_pd_ul_resources_t);
  new_hob->hhul_hca.cq_ul_resources_sz= sizeof(THH_cq_ul_resources_t);
  new_hob->hhul_hca.srq_ul_resources_sz= sizeof(THH_srq_ul_resources_t);
  new_hob->hhul_hca.qp_ul_resources_sz= sizeof(THH_qp_ul_resources_t);
  /* Get a copy of allocated resources */
  memcpy(&(new_hob->ul_res),hca_ul_resources_p,sizeof(THH_hca_ul_resources_t));
  new_hob->hhul_hca.hca_ul_resources_p= &(new_hob->ul_res);
  
  new_hob->hhul_hca.device= new_hob;  /* Connect to new THHUL_hob */
  
  /* Return allocated HHUL device context */
  *hca_p= &(new_hob->hhul_hca);
    
  return HH_OK;
}


HH_ret_t THHUL_hob_destroy(/*IN*/ HHUL_hca_hndl_t hca)
{
  THHUL_hob_t hob= GET_HOB(hca);

  if (hob == NULL)  return HH_EINVAL; /* Invalid handle */

  THHUL_mwm_destroy(hob->mwm);
  THHUL_qpm_destroy(hob->qpm);
  THHUL_srqm_destroy(hob->srqm);
  THHUL_pdm_destroy(hob->pdm);
  THHUL_cqm_destroy(hob->cqm);
  THH_uar_destroy(hob->uar);
  FREE(hob);
  return HH_OK;
}


HH_ret_t THHUL_hob_query_version(
  /*IN*/ THHUL_hob_t hob,
  /*OUT*/ THH_ver_info_t *version_p 
)
{
  if ((hob == NULL) || (version_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  memcpy(version_p,&(hob->ul_res.version),sizeof(THH_ver_info_t));
  return HH_OK;
}

HH_ret_t THHUL_hob_get_hca_ul_handle
(
  /*IN*/ THHUL_hob_t 		hob,
  /*OUT*/ HHUL_hca_hndl_t 	*hca_ul_p
)
{
  if ((hob == NULL) || (hca_ul_p == NULL)){ 
	  return HH_EINVAL; /* Invalid handle/pointer */
  }
  
  *hca_ul_p = &hob->hhul_hca;
  return HH_OK;
}

HH_ret_t THHUL_hob_get_hca_ul_res(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THH_hca_ul_resources_t *hca_ul_res_p
)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);
  
  if ((hob == NULL) || (hca_ul_res_p == NULL)) {
	  MTL_ERROR1("%s Wrong parameters: hob = %p, hca_ul_res_p=%p\n", __func__, hob, hca_ul_res_p); 
	  return HH_EINVAL; /* Invalid handle/pointer */
  }
  memcpy(hca_ul_res_p,&(hob->ul_res),sizeof(THH_hca_ul_resources_t));
  return HH_OK;
}

HH_ret_t THHUL_hob_get_pdm(/*IN*/ HHUL_hca_hndl_t hca,
                           /*OUT*/ THHUL_pdm_t *pdm_p)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);

  if ((hob == NULL) || (pdm_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  *pdm_p= hob->pdm;
  return HH_OK;
}

HH_ret_t THHUL_hob_get_cqm (/*IN*/ HHUL_hca_hndl_t hca,
                            /*OUT*/ THHUL_cqm_t *cqm_p)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);
  
  if ((hob == NULL) || (cqm_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  *cqm_p= hob->cqm;
  return HH_OK;
}

HH_ret_t THHUL_hob_get_qpm (/*IN*/ HHUL_hca_hndl_t hca, 
                            /*OUT*/ THHUL_qpm_t *qpm_p)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);
  
  if ((hob == NULL) || (qpm_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  *qpm_p= hob->qpm;
  return HH_OK;
}

HH_ret_t THHUL_hob_get_srqm (/*IN*/ HHUL_hca_hndl_t hca, 
                             /*OUT*/ THHUL_srqm_t *srqm_p)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);
  
  if ((hob == NULL) || (srqm_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  *srqm_p= hob->srqm;
  return HH_OK;
}

HH_ret_t THHUL_hob_get_uar (/*IN*/ HHUL_hca_hndl_t hca, 
                            /*OUT*/ THH_uar_t *uar_p)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);
  
  if ((hob == NULL) || (uar_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  *uar_p= hob->uar;
  return HH_OK;
}


HH_ret_t THHUL_hob_get_mwm (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THHUL_mwm_t *mwm_p
)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);
  
  if ((hob == NULL) || (mwm_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  *mwm_p= hob->mwm;
  return HH_OK;
}


HH_ret_t THHUL_hob_is_priv_ud_av(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ MT_bool *is_priv_ud_av_p
)
{
  THHUL_hob_t hob= (THHUL_hob_t)(hca->device);
  
  if ((hob == NULL) || (is_priv_ud_av_p == NULL)) return HH_EINVAL; /* Invalid handle/pointer */
  *is_priv_ud_av_p= hob->ul_res.priv_ud_av;
  return HH_OK;
}


/**********************************************************************************************
 *                    Private Functions
 **********************************************************************************************/


/*******************************************************
 * Function: alloc_hob_context
 *
 * Description:  Allocate the THHUL_hob object memory and included objects
 *
 * Arguments: new_hob_p - Object to allocate for
 *            hca_ul_resources_p - As given to THHUL_hob_create()
 *
 * Returns: HH_OK
 *          HH_EAGAIN
 *******************************************************/
static HH_ret_t alloc_hob_context(
  THHUL_hob_t *new_hob_p, 
  THH_hca_ul_resources_t *hca_ul_resources_p
)
{
  HH_ret_t rc;

  /* Allocate THHUL_hob own context */
  *new_hob_p= (THHUL_hob_t)MALLOC(sizeof(struct THHUL_hob_st));
  if (*new_hob_p == NULL) return HH_EAGAIN;

  /* Create included objects */
  rc= THH_uar_create(
        &(hca_ul_resources_p->version),
        hca_ul_resources_p->uar_index,
        (void*)(hca_ul_resources_p->uar_map),
        &((*new_hob_p)->uar));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_hob_create: Failed creating THHUL_uar (err=%d).\n",rc);
    goto failed_uar;
  }

  rc= THHUL_pdm_create((*new_hob_p),hca_ul_resources_p->priv_ud_av,&((*new_hob_p)->pdm));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_hob_create: Failed creating THHUL_pdm (%d=%s).\n", rc, HH_strerror_sym(rc));
    goto failed_pdm;
  }

  rc= THHUL_cqm_create((*new_hob_p),&((*new_hob_p)->cqm));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_hob_create: Failed creating THHUL_cqm (%d=%s).\n", rc, HH_strerror_sym(rc));
    goto failed_cqm;
  }

  rc= THHUL_srqm_create((*new_hob_p),&((*new_hob_p)->srqm));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_hob_create: Failed creating THHUL_srqm (%d=%s).\n", rc, HH_strerror_sym(rc));
    goto failed_srqm;
  }
  
  rc= THHUL_qpm_create((*new_hob_p), (*new_hob_p)->srqm, &((*new_hob_p)->qpm));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_hob_create: Failed creating THHUL_qpm (%d=%s).\n", rc, HH_strerror_sym(rc));
    goto failed_qpm;
  }

  rc= THHUL_mwm_create((*new_hob_p),hca_ul_resources_p->log2_mpt_size,&((*new_hob_p)->mwm));
  if (rc != HH_OK) {
    MTL_ERROR1("THHUL_hob_create: Failed creating THHUL_mwm (%d=%s).\n", rc, HH_strerror_sym(rc));
    goto failed_mwm;
  }

  return HH_OK;

  /* Failure cleanup (error exit flow) */
  failed_mwm:
    THHUL_qpm_destroy((*new_hob_p)->qpm);
  failed_qpm:
    THHUL_srqm_destroy((*new_hob_p)->srqm);
  failed_srqm:
    THHUL_cqm_destroy((*new_hob_p)->cqm);
  failed_cqm:
    THHUL_pdm_destroy((*new_hob_p)->pdm);
  failed_pdm:
    THH_uar_destroy((*new_hob_p)->uar);
  failed_uar:
    FREE(*new_hob_p);
    return rc;
}

