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
 
#include <vip_hash.h>
#include <vip_array.h>
#include <vapi_common.h>
#include <hobkl.h>
#include "pdm.h"

struct PDM_t {
  u_int32_t ref_count[VIP_MAX_OBJECT_TYPE]; /* number of objects referencing objects managed by this pd */
  VIP_array_p_t array;
  HOBKL_hndl_t hob;
  HH_hca_hndl_t hh;
};

struct PDM_pd_t {
  HH_pd_hndl_t pd_id;
  VIP_RSCT_rscinfo_t rsc_ctx;
  MOSAL_prot_ctx_t prot_ctx;
};

static void VIP_free(void* p)
{
   FREE(p); 
   MTL_ERROR1(MT_FLFMT("PDM delete:found unreleased pd"));        
}
/*************************************************************************
 * Function: PDM_new
 *
 * Arguments:
 *  hca_hndl (IN) - HCA for which this PDM is created
 *  pdm_p (OUT) - Pointer to PDM_obj_handle_t to return new obj_hndl instance in
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Creates new PDM object for new HOB.
 *
 *************************************************************************/ 
VIP_ret_t PDM_new(HOBKL_hndl_t hca_hndl, PDM_hndl_t *pdm_p)
{
  VIP_ret_t ret=VIP_OK;
  VAPI_hca_cap_t hca_caps;
  PDM_hndl_t pdm;

  pdm= (PDM_hndl_t)MALLOC(sizeof(struct PDM_t));
  if (pdm == NULL) return VIP_EAGAIN;
  memset(pdm, 0, sizeof(struct PDM_t));
  
  /* get info about the HCA capabilities from hobkl */
  ret =  HOBKL_query_cap(hca_hndl, &hca_caps );
  if (ret != VIP_OK) {
    MTL_ERROR2("%s: failed HOBKL_query_cap (%s).\n", __func__,VAPI_strerror_sym(ret));
    return VIP_EINVAL_HOB_HNDL;
  }

  MTL_DEBUG3("%smaximum num PDs = %d\n", __func__, hca_caps.max_pd_num);
  /* sanity check - protect from buggy HCA drivers... */
  if (hca_caps.max_pd_num < 1)  {
    MTL_ERROR1("%s: invalid HCA caps (max_pd_num=%d).\n", __func__,
      hca_caps.max_pd_num);
    return VIP_EINVAL_HOB_HNDL;
  }

  ret=VIP_array_create_maxsize(hca_caps.max_pd_num>>CREATE_SHIFT, hca_caps.max_pd_num, &(pdm->array));
  if (ret != VIP_OK) {
    FREE(pdm);
    return ret;
  }
  
  pdm->hob=hca_hndl;
  pdm->hh=HOBKL_get_hh_hndl(pdm->hob);

  if (pdm_p) *pdm_p=pdm;

  return ret;
}


/*************************************************************************
 * Function: PDM_delete
 *
 * Arguments:
 *  pdm (IN) - PDM object to destroy
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EBUSY: PDs still in use (if not forced).
 *
 * Description:
 *   Cleanup resources of given PDM object.
 *   Note: This function will fail if any of its PDs are still in use
 *   and forced == FALSE
 *
 *************************************************************************/ 
VIP_ret_t PDM_delete(PDM_hndl_t pdm)
{
  //VIP_ret_t ret;
  //VIP_array_handle_t hdl;
//  struct PDM_pd_t *pd;

#ifdef MAX_ERROR
  if (pdm == NULL) {
    MTL_ERROR1("%s called with NULL parameter\n", __func__);
    return VIP_EINVAL_PDM_HNDL;
  }
#endif

  VIP_array_destroy(pdm->array,VIP_free);
  FREE(pdm);

  return VIP_OK;
}


/*************************************************************************
 * Function: PDM_create_pd
 *
 * Arguments:
 *  pdm (IN)
 *  prot_ctx (IN)
 *  pd_ul_resources_p (IN/OUT)
 *  pd_hndl_p (OUT) - returned pd handle
 *  num (OUT)       - returned HH pd handle
 *  
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EAGAIN: Not enough resources to do PD allocation.
 *  VIP_EINVAL_PDM_HNDL: Invalid HCA handle.
 *  VIP_ENOSYS: PDM doesn't support given Flag.
 *
 * Description:
 *   Create new Protection domain.
 *
 *************************************************************************/ 
VIP_ret_t PDM_create_pd(VIP_RSCT_t usr_ctx, PDM_hndl_t pdm_hndl, MOSAL_protection_ctx_t prot_ctx, 
                        void * pd_ul_resources_p, PDM_pd_hndl_t *pd_hndl_p, HH_pd_hndl_t *pd_num_p)
{

  VIP_ret_t ret=VIP_OK;
  HH_ret_t hh_ret=HH_OK;
  struct PDM_pd_t *pd;
  PDM_pd_hndl_t hdl;
  VIP_RSCT_rschndl_t r_h;
  
  MTL_DEBUG3("CALLED " "%s for hh: %p\n", __func__, (void *) pdm_hndl->hh);

  pd = TMALLOC(struct PDM_pd_t);
  if ( pd == NULL ) {
    ret= VIP_EAGAIN;
    goto failed_malloc;
  }

  hh_ret = HH_alloc_pd(pdm_hndl->hh, prot_ctx, pd_ul_resources_p, &(pd->pd_id));
  if (hh_ret != HH_OK) {
    ret= (hh_ret >= VIP_COMMON_ERROR_MIN) ? VIP_EGEN : (VIP_ret_t)hh_ret;
    goto failed_hh_alloc_pd;
  }

  pd->prot_ctx = prot_ctx;

  ret = VIP_array_insert(pdm_hndl->array, pd, &hdl);
  if (ret != VIP_OK)  goto failed_array_insert;

  r_h.rsc_pd_hndl = hdl;
  VIP_RSCT_register_rsc(usr_ctx,&pd->rsc_ctx,VIP_RSCT_PD,r_h);
  
  if (pd_hndl_p) *pd_hndl_p = hdl;
  if (pd_num_p)  *pd_num_p = pd->pd_id;
  
  return VIP_OK;

  failed_array_insert:
    HH_free_pd(pdm_hndl->hh, pd->pd_id);
  failed_hh_alloc_pd:
    FREE(pd);
  failed_malloc:
    return ret;
}


/*************************************************************************
 * Function: PDM_destroy_pd
 *
 * Arguments:
 *  pdm (IN)
 *  pd_hndl (IN) - returned pd handle
 *  flags   (IN) - PROT, NOPROT, NOTHREAD
 *  
 *
 * Returns:	
 *   VIP_OK: Protection Domain remove successfully.
 *   VIP_EINVAL_PDM_HNDL: Invalid PDM Handle.
 *   VIP_EINVAL_PD_HNDL: Invalid Protection Domain Handle.
 *   VIP_EAGAIN: Not enough resources.
 *   VIP_EBUSY: Protection Domain has still object attached to it.
 *   VIP_EPERM: Calling process is not allowed to destroy PD.
 *
 * Description:
 *   Destroy Protection domain (if not attached to any object).
 *   If protection domain has still elements linked to it or the calling process doesn't have 
 *   control over the Protection Domain an error will be reported.
 *
 *************************************************************************/ 
VIP_ret_t PDM_destroy_pd(VIP_RSCT_t usr_ctx,PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl)
{
  struct PDM_pd_t* pd;
  VIP_ret_t ret;

  FUNC_IN;

  ret = VIP_array_find_hold(pdm_hndl->array, pd_hndl, (VIP_array_obj_t *)&pd);
  if (ret != VIP_OK ) {
    return VIP_EINVAL_PD_HNDL;
  }
  ret = VIP_RSCT_check_usr_ctx(usr_ctx,&pd->rsc_ctx);
  if (ret != VIP_OK) {
    VIP_array_find_release(pdm_hndl->array, pd_hndl);
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. pd handle=0x%x (%s)"),__func__,pd_hndl,VAPI_strerror_sym(ret));
    return ret;
  }

  ret = VIP_array_find_release_erase(pdm_hndl->array, pd_hndl, (VIP_array_obj_t *)&pd);
  if (ret != VIP_OK) {
    if (ret == VIP_EINVAL_HNDL) return VIP_EINVAL_PD_HNDL;
    else return ret;
  }
  
  HH_free_pd(pdm_hndl->hh, pd->pd_id);
  VIP_RSCT_deregister_rsc(usr_ctx,&pd->rsc_ctx,VIP_RSCT_PD);
  FREE(pd);
  
  MT_RETURN(VIP_OK);
  
}


/*************************************************************************
 * Function: PDM_add_object_to_pd
 *
 * Arguments:
 *  pdm     (IN)
 *  pd_hndl (IN) - pd to add to
 *  prot_ctx_p(OUT) - protection context of this PD
 *  pd_id_p(OUT) - to return hh pd id
 *  
 *
 * Returns:
 *   VIP_OK
 *   VIP_EINVAL_PDM_HNDL: invalid PDM handle.
 *   VIP_EINVAL_PD_HNDL: invalid PD handle.
 *   VIP_EPERM: has no permission to control this PD.
 *   VIP_EAGAIN: not enough resources.
 *   VIP_BUSY: Object already has PD associated with it 
 *
 * Description:
 *   Add a VIP object to protection domain.
 *
 *************************************************************************/ 
VIP_ret_t PDM_add_object_to_pd(VIP_RSCT_t usr_ctx,PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl, MOSAL_prot_ctx_t *prot_ctx_p, HH_pd_hndl_t *pd_id_p) 
{
  struct PDM_pd_t *pd;
  VIP_ret_t rc;

  /* will be released in PDM_rm_object_from_pd */
  rc = VIP_array_find_hold(pdm_hndl->array, pd_hndl, (VIP_array_obj_t *)&pd);
  if ( rc != VIP_OK ) return rc;

  rc = VIP_RSCT_check_usr_ctx(usr_ctx, &pd->rsc_ctx);
  if ( rc != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. pd handle=0x%x (%s)"),__func__,pd_hndl,VAPI_strerror_sym(rc));
    if ( VIP_array_find_release(pdm_hndl->array, pd_hndl) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: data structs inconsistency"), __func__);
    }
  }
  else {
    if ( prot_ctx_p ) *prot_ctx_p = pd->prot_ctx;
    if ( pd_id_p ) *pd_id_p = pd->pd_id;
  }
  
  return rc;
}


/*************************************************************************
 * Function: PDM_rm_object_from_pd
 *
 * Arguments:
 *  pdm     (IN)
 *  pd_hndl (IN) - pd to rm 
 *  
 *
 * Returns:
 *   VIP_OK
 *   VIP_EINVAL_PDM_HNDL: invalid PDM handle.
 *   VIP_EINVAL_PD_HNDL: invalid PD handle.
 *   VIP_EINVAL_OBJ_HNDL: invalid object handle (not in this PD).
 *   VIP_EPERM: has no permission to control this PD.
 *   VIP_EAGAIN: not enough resources.
 *
 * Description:
 *   Remove a VIP object to protection domain.
 *
 *************************************************************************/ 
VIP_ret_t PDM_rm_object_from_pd(PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl)
{
  return VIP_array_find_release(pdm_hndl->array, pd_hndl);
}


/*************************************************************************
 * Function: PDM_get_pd_id
 *
 * Arguments:	
 *   pdm_hndl (IN) PDM Handle.
 *   pd_hndl (IN) Protection Domain handle.
 *   pd_id_p (OUT) Returned PD ID.
 * Arguments:
 *
 * Returns:
 *   VIP_OK
 *   VIP_EINVAL_PDM_HNDL: invalid PDM handle.
 *   VIP_EINVAL_PD_HNDL: invalid PD handle.
 *
 * Description:
 *   This function returns the PD ID allocated in hardware for 
 *   this PD when it was created.
 *
 *************************************************************************/ 
VIP_ret_t PDM_get_pd_id(PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl, HH_pd_hndl_t *pd_id_p)
{
  struct PDM_pd_t *pd;
  VIP_ret_t rc;

  rc = VIP_array_find_hold(pdm_hndl->array, pd_hndl, (VIP_array_obj_t *)&pd);
  if (rc != VIP_OK ) {
    return rc;
  }

  if ( pd_id_p ) *pd_id_p = pd->pd_id;
  VIP_array_find_release(pdm_hndl->array, pd_hndl);
  return VIP_OK;
}


/*************************************************************************
 * Function: PDM_get_prot_ctx
 *
 * Arguments:	
 *   pdm_hndl (IN) PDM Handle.
 *   pd_hndl (IN) Protection Domain handle.
 *   prot_ctx_p (OUT) Returned protection context
 * Arguments:
 *
 * Returns:
 *   VIP_OK
 *
 *************************************************************************/ 
VIP_ret_t PDM_get_prot_ctx(PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl, MOSAL_prot_ctx_t *prot_ctx_p)
{
  struct PDM_pd_t *pd;
  VIP_ret_t rc;

  rc = VIP_array_find_hold(pdm_hndl->array, pd_hndl, (VIP_array_obj_t *)&pd);
  if (rc != VIP_OK ) {
    return rc;
  }

  *prot_ctx_p = pd->prot_ctx;
  VIP_array_find_release(pdm_hndl->array, pd_hndl);
  return VIP_OK;
}


/*************************************************************************
 * Function: PDM_set_acl_level_in_pd
 *
 * Arguments:
 *  pdm_hndl (IN) PDM Handle
 *  pd_hndl (IN)  Protection Domain handle.
 *  proc_id (IN)  processing entity id (process/thread)
 *  acl_level (IN)  ACL_FULL, ACL_PART, ACL_NONE
 *
 * Returns:
 *   VIP_OK
 *   VIP_EINVAL_PDM_HNDL: invalid PDM handle.
 *   VIP_EINVAL_PD_HNDL: invalid PD handle.
 *   VIP_EAGAIN: not enough resources
 *   VIP_ENOSYS: ACL not supported for this PD
 *
 * Description:
 *   Set ACL level the given process/thread to protection domain (use ACL_NONE to remove).
 *   This operation may be called by a FULL authority process/thread only.
 *   And only if NOPROT flag is NOT given on PD creation.
 *
 *************************************************************************/ 
VIP_ret_t PDM_set_acl_level_of_pd(PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl,
                                  MOSAL_proc_id_t proc_id, PDM_acl_level_t acl_level)
{
  return VIP_ENOSYS;
}




/*************************************************************************
 * Function: PDM_get_acl_level_in_pd
 *
 * Arguments:
 *  pdm     (IN)
 *  pd_hndl (IN) - pd to add to
 *  proc_id (IN) processing entity id (process/thread)
 *  acl_level (OUT)  -  ACL_NONE/ACL_PART/ACL_FULL
 *
 * Returns:
 *   VIP_OK
 *   VIP_EINVAL_PDM_HNDL: invalid PDM handle.
 *   VIP_EINVAL_PD_HNDL: invalid PD handle.
 *   VIP_ENOSYS: ACL not supported for this PD
 *
 * Description:
 *   Get ACL level of the given process/thread in given protection domain.
 *
 *************************************************************************/ 
VIP_ret_t PDM_get_acl_level_of_pd(PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl, 
    MOSAL_proc_id_t proc_id, PDM_acl_level_t *acl_level)
{
  return VIP_ENOSYS;
}

/*************************************************************************
 * Function: PDM_get_num_pds
 *
 * Arguments:
 *  pdm (IN) - PDM object 
 *  num_pds (OUT) - returns current number of pds
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_PDM_HNDL
 *  VIP_EINVAL_PARAM -- NULL pointer given for num_pds
 *
 * Description:
 *   returns number of created pds
 *************************************************************************/ 
VIP_ret_t PDM_get_num_pds(PDM_hndl_t pdm, u_int32_t * num_pds)
{
  if (pdm == NULL) {
    MTL_ERROR1("%s called with NULL parameter for pdm\n", __func__);
    return VIP_EINVAL_PDM_HNDL;
  }

  if (num_pds == NULL) {
      MTL_ERROR1("%s called with NULL parameter for num pds\n", __func__);
      return VIP_EINVAL_PARAM;
  }

  *num_pds = VIP_array_get_num_of_objects(pdm->array);
  return VIP_OK;
}

