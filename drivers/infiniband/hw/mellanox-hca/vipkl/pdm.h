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
 
#ifndef H_VIP_PDM_H
#define H_VIP_PDM_H


#include <mtl_common.h>
#include <vapi.h>
#include <vip.h>
#include <hh.h>
#include <VIP_rsct.h>

#define PDM_INVAL_PD_HNDL VAPI_INVAL_HNDL

typedef u_int32_t PDM_obj_handle_t;  /* Objects that can be added to a PD */
typedef enum{ACL_NONE,ACL_PART,ACL_FULL} PDM_acl_level_t;
typedef enum {PDM_PROT, PDM_NOPROT, PDM_NOTHREAD} PDM_flags_t;
 
typedef VIP_obj_type_t PDM_obj_type_t;  /* Object type */

/* TBD: Stub */
typedef u_int32_t MOSAL_proc_id_t;


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
VIP_ret_t PDM_new( HOBKL_hndl_t hca_hndl, PDM_hndl_t *pdm_p);


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
VIP_ret_t PDM_delete(PDM_hndl_t pdm);


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
VIP_ret_t PDM_create_pd(VIP_RSCT_t usr_ctx,PDM_hndl_t pdm_hndl, MOSAL_protection_ctx_t prot_ctx, 
                        void * pd_ul_resources_p, PDM_pd_hndl_t *pd_hndl_p, HH_pd_hndl_t *pd_num_p);


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
VIP_ret_t PDM_destroy_pd(VIP_RSCT_t usr_ctx,PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl);


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
VIP_ret_t PDM_add_object_to_pd(VIP_RSCT_t usr_ctx,PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl, MOSAL_prot_ctx_t *prot_ctx_p, HH_pd_hndl_t *pd_id_p);


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
VIP_ret_t PDM_rm_object_from_pd(PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl);


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
VIP_ret_t	PDM_get_pd_id (PDM_hndl_t	pdm_hndl, PDM_pd_hndl_t  pd_hndl,
                         HH_pd_hndl_t *pd_id_p);


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
VIP_ret_t PDM_get_prot_ctx(PDM_hndl_t pdm_hndl, PDM_pd_hndl_t pd_hndl, MOSAL_prot_ctx_t *prot_ctx_p);

/*************************************************************************
 * Function: PDM_get_pd_by_id
 *
 * Arguments:	
 *   pdm_hndl (IN) PDM Handle.
 *   pd_id (IN)  PD ID.
 *   pd_hndl (OUT) Returned Protection Domain handle.
 * Arguments:
 *
 * Returns:
 *   VIP_OK
 *   VIP_EINVAL_PDM_HNDL: invalid PDM handle.
 *   VIP_EINVAL_PD_HNDL: no PD handle bound to this ID
 *
 * Description:
 *   Reverse mapping: finds the handle mapped in HAL PD ID
 *
 *************************************************************************/ 
VIP_ret_t	PDM_get_pd_by_id (PDM_hndl_t	pdm_hndl, HH_pd_hndl_t pd_id,
  PDM_pd_hndl_t*  pd_hndl);

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
    MOSAL_proc_id_t proc_id, PDM_acl_level_t acl_level);




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
    MOSAL_proc_id_t proc_id, PDM_acl_level_t *acl_level);

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
 *   returns current number of pds
 *************************************************************************/ 
VIP_ret_t PDM_get_num_pds(PDM_hndl_t pdm, u_int32_t * num_pds);

#endif
