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



#ifndef __MMU_HEADER
#define __MMU_HEADER

#include <vip.h>
#include <vapi.h>
#include <evapi.h>
#include <hobkl.h>
#include <VIP_rsct.h>
                  
/* typedef VAPI_mrw_acl_t MM_mr_acl_t; */
typedef u_int8_t MM_mr_acl_t;

/* Memory Region type */
typedef VAPI_mrw_type_t MM_mr_type_t;
/* Remote and Local keys type */
typedef u_int32_t MM_key_t;

/* Memory Region properties type */

typedef struct  {
    MM_mr_type_t type;                         /* Memory Region Type */
    MM_key_t l_key, r_key;                     /* Local (Lkey) and Remote Protection Key (Rkey) */
    VAPI_virt_addr_t re_local_start, re_local_end;  /* Real local start and end addresses of the region */
    VAPI_virt_addr_t re_remote_start, re_remote_end;/* Real remote start and end addresses of the region */
    PDM_pd_hndl_t pd_hndl;                     /* Protection Domain handle */
    MM_mr_acl_t acl;                           /* Region access control list */
} MM_VAPI_mro_t;



/* Page mask */
#define MM_PAGE_MASK (~(PAGE_SIZE - 1))
/* Rounds down to page */
#define MM_DOWN_TO_PAGE( va) \
        ( MM_PAGE_MASK & (va) )
/* Rounds up to the page */
#define MM_UP_TO_PAGE(hndl, va) \
  	    ((va + ~MM_PAGE_MASK) & MM_PAGE_MASK)

#define MM_DOWN_ALIGNX_PHYS(value, mask)     ((VAPI_phy_addr_t)(value) & (~((VAPI_phy_addr_t)0) << (mask)))


/* Checks the correctness of Access Permissions flags */
#define CHECK_ACL(acl) \
        ( (!((acl) & VAPI_EN_LOCAL_WRITE)) && ( (acl) & (VAPI_EN_REMOTE_WRITE | VAPI_EN_REMOTE_ATOM) ) \
        ? VIP_EINVAL_ACL : (VIP_ret_t) VIP_OK )

#define MM_INVAL_MRW_HNDL VAPI_INVAL_HNDL

/*******************************************************************
 * FUNCTION:
 *         MM_new
 * DESCRIPTION:
 *         Creates a new Memory Managment Unit.
 * ARGUMENTS:
 *         hob_hndl(IN): HOB handle
 *         delay_unlocks(IN): delay_unlock object (pointer)
 *         mm_hdnl(OUT): Memory Managment handle.
 * RETURNS:
 *         VIP_OK
 *         VIP_EAGAIN: not enough resources
 *******************************************************************/
VIP_ret_t MM_new(HOBKL_hndl_t hob_hndl, VIP_delay_unlock_t delay_unlocks, MM_hndl_t *mm_hndl_p);

/*******************************************************************
 * FUNCTION:
 *         MM_delete
 * DESCRIPTION:
 *         Deletes Memory Managment Unit.
 * ARGUMENTS:
 *         mm_hdnl(IN): Memory Managment handle.
 * RETURNS:
 *         VIP_OK
 *         VIP_EINVAL_MM_HNDL: invalid MMU handle.
 *******************************************************************/
VIP_ret_t MM_delete(MM_hndl_t mm_hndl);

/*******************************************************************
 * FUNCTION:
 *         MM_create_mr
 * DESCRIPTION:
 *         Registers Memory Region
 * ARGUMENTS:
 *         usr_ctx(IN) resource tracking context
 *         mm_hndl(IN): Memory Manager handle
 *         mr_props_p(IN) properties of the memory region
 *         pd_hndl(IN): Protection domain handle for the new memory region
 *         mr_hndl_p(OUT): handle of new Memory Region.
           mr_prop_p(OUT): props of the new mr
 * RETURNS:
 *         VIP_OK
 *         VIP_EAGAIN: not enough resources.
 *         VIP_EINVAL_MMU_HNDL: invalid MMU handle.
 *         VIP_EINVAL_MR_TYPE: invalid memory region type.
 *         VIP_EINVAL_SIZE: invalid region size.
 *         VIP_EINVAL_ADDR: invalid region address.
 *         VIP_EINVAL_ACL: invalid ACL flags.
 *******************************************************************/
VIP_ret_t MM_create_mr(VIP_RSCT_t usr_ctx,
                       MM_hndl_t mm_hndl,VAPI_mrw_t* mr_props_p, PDM_pd_hndl_t pd_hndl, 
                       MM_mrw_hndl_t* mr_hndl_p, MM_VAPI_mro_t* mr_prop_p);

/*******************************************************************
 * FUNCTION:
 *         MM_create_mw.
 * DESCRIPTION:
 *         Allocates Memory window.
 * ARGUMENTS:
 *         mm_hndl(IN): Memory Managment handle.
 *         pdm_pd_h(IN): pd for new window. 
 *         r_key_p(OUT): r_key of new Memory Region.
 * RETURNS:
 *         VIP_OK
 *         VIP_EAGAIN: not enough resources.
 *         VIP_EINVAL_MMU_HNDL: invalid MMU handle.
 *******************************************************************/
VIP_ret_t MM_create_mw(VIP_RSCT_t usr_ctx,MM_hndl_t mm_h,PDM_pd_hndl_t pdm_pd_h,IB_rkey_t *r_key_p);

/*******************************************************************
 * FUNCTION:
 *         MM_create_smr
 * DESCRIPTION:
 *         Registers Memory Region
 * ARGUMENTS:
 *         mm_hndl(IN): Memory Managment handle
 *         mrw_orig_hndl(IN): handle of original Memory Region.
 *         mr_req_p(IN): mr strure with all properties(size, acl,..except pd). 
 *         pd_hndl(IN): Protection domain handler for the new memory region
 *         mrw_hndl_p(OUT): handle of new Shared Memory Region.
 *         mr_prop_p(OUT): props of the new mr
 * RETURNS:
 *         VIP_OK
 *         VIP_EAGAIN: not enough resources.
 *         VIP_EINVAL_MMU_HNDL: invalid MMU handle.
 *         VIP_EINVAL_ADDR: invalid region address.
 *         VIP_EINVAL_ACL: invalid ACL flags.
 *******************************************************************/
VIP_ret_t MM_create_smr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,MM_mrw_hndl_t mrw_orig_hndl,VAPI_mrw_t* mr_props_p, PDM_pd_hndl_t pd_hndl, 
                       MM_mrw_hndl_t* mr_hndl_p, MM_VAPI_mro_t* mr_prop_p);


VIP_ret_t MM_reregister_mr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,
                           VAPI_mr_hndl_t ori_mr_hndl,VAPI_mr_change_t change_type,
                           VAPI_mrw_t *mrw_prop_p,PDM_pd_hndl_t pd_hndl,
                           MM_mrw_hndl_t *mr_hndl_p, MM_VAPI_mro_t *mm_prop_p);

/*******************************************************************
 * FUNCTION:
 *         MM_destroy_mr
 * DESCRIPTION:
 *         Destroys registered Memory Region.
 * ARGUMENTS:
 *         mm_hdnl(IN): Memory Managment handle.
 *         mrw_hndl(IN):handle of Memory Region which is to be destroyed
 * RETURNS:
 *         VIP_OK
 *         VIP_EINVAL_MM_HNDL: invalid MMU handle.
 *         VIP_EINVAL_MRW_HNDL: invalid memory region handle.
 *         VIP_EPERM: permission denied.
 *         VIP_BUSY: windows still bounded to region.
 * REMARK:
 *         Only the process that created this region may destroy it.
 *******************************************************************/
VIP_ret_t MM_destroy_mr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl, MM_mrw_hndl_t mrw_hndl);
/*******************************************************************
 * FUNCTION:
 *         MM_destroy_mw.
 * DESCRIPTION:
 *         Destroys registered memory window.
 * ARGUMENTS:
 *         mm_hdnl(IN): Memory Managment handle.
 *         init_r_key(IN):initial r_key of Memory Region which is to be destroyed.
 * RETURNS:
 *         VIP_OK
 *         VIP_EINVAL_MM_HNDL: invalid MMU handle.
 *         VIP_EINVAL_MRW_HNDL: invalid memory region handle.
 * REMARK:
 *         Only the process that created this region may destroy it.
 *******************************************************************/
VIP_ret_t MM_destroy_mw(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl, IB_rkey_t init_r_key);


/*******************************************************************
 * FUNCTION:
 *         MM_query_mr
 * DESCRIPTION:
 *         The call will retrieve Memory Region properties table (see description of MM_VAPI_mro_t) .
 * ARGUMENTS:
 *         mm_hdnl(IN): Memory Managment handle.
 *         mrw_hndl(IN):handle of quering Memory Region
 *         mr_prop_p(OUT): properties table (pointer to the appropiate memory buffer)
 *
 * RETURNS:
 *         VIP_OK
 *         VIP_EINVAL_MM_HNDL: invalid MMU handle.
 *         VIP_EINVAL_MRW_HNDL: invalid region handle.
 *         VIP_EPERM: permission denied.
 * REMARKS:
 *        mr_prop_p must be a pointer to appropriate memory buffer as properties
 *        table will be copied to that buffer
 *******************************************************************/
VIP_ret_t MM_query_mr (VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl, MM_mrw_hndl_t mrw_hndl, MM_VAPI_mro_t *mr_prop_p);

/*******************************************************************
 * FUNCTION:
 *         MM_alloc_fmr
 * Arguments:
 *  hca_hndl :	HCA Handle.
 *  fmr_props_p: Pointer to the requested fast memory region properties.
 *  fmr_hndl_p: Pointer to the fast memory region handle.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_PD_HNDL: invalid PD handle
 *  VAPI_EINVAL_LEN: invalid length
 *  VAPI_EINVAL_ACL: invalid ACL specifier (e.g. VAPI_EN_MEMREG_BIND)
 *  VAPI_EPERM: not enough permissions.
 *    
 * *******************************************************************/
VIP_ret_t MM_alloc_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,EVAPI_fmr_t *fmr_prop_p, PDM_pd_hndl_t pd_hndl,
                       MM_mrw_hndl_t *mr_hndl_p);

 
 /*******************************************************************
 * FUNCTION:
 *         MM_map_fmr
 * DESCRIPTION:
 *           maps start adrs to the mr
 * ARGUMENTS:
 *         mm_hdnl(IN): Memory Managment handle.
 *         fmr_hndl(IN): The fast memory region handle.
 *         map_p:  properties
 *           l_key_p(OUT): Allocated L-Key for the new mapping 
 *          (may be different than prev. mapping of the same FMR)
 *         r_key_p(OUT): Allocated R-Key for the new mapping
 *
 * RETURNS:
 *  VIP_OK
 *  VIP_EAGAIN: out of resources
 *  VIP_EINVAL_MM_HNDL: invalid HCA handle
 *  VIP_EINVAL_MRW_HNDL: invalid memory region handle (e.g. not a FMR region, or an u)
 *  VIP_EPERM: not enough permissions.
 *
 *******************************************************************/
VIP_ret_t MM_map_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,EVAPI_fmr_hndl_t fmr_hndl,EVAPI_fmr_map_t* map_p,
                     VAPI_lkey_t *lkey_p,VAPI_rkey_t *rkey_p);

/*******************************************************************
 * FUNCTION:
 *         MM_unmap_fmr
 * DESCRIPTION:
 *           unmaps (unlocks) for each mr in the array, all the va's that it was mapped to.            
 * ARGUMENTS:
 *         mm_hdnl(IN): Memory Managment handle.
 *         size(IN):array sz
 *         mr_hndl_arr: array of mr handles
 *
 * RETURNS:
 *  VIP_OK
 *  VIP_EAGAIN: out of resources
 *  VIP_EINVAL_MM_HNDL: invalid HCA handle
 *  VIP_EINVAL_MRW_HNDL: invalid memory region handle (e.g. not a FMR region, or an u)
 *  VIP_EPERM: not enough permissions.

 * REMARKS:
 *        mr_prop_p must be a pointer to appropriate memory buffer as properties
 *        table will be copied to that buffer
 *******************************************************************/
VIP_ret_t MM_unmap_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,MT_size_t size,EVAPI_fmr_hndl_t* mr_hndl_arr);


/*******************************************************************
 * FUNCTION:
 *         MM_free_fmr
 * DESCRIPTION:
 *         
 * ARGUMENTS:
 *         mm_hdnl(IN): Memory Managment handle.
 *         fmr_hndl(IN):handle of fast mr
 *
 * RETURNS:
 *  VIP_OK
 *  VIP_EAGAIN: out of resources
 *  VIP_EINVAL_MM_HNDL: invalid HCA handle
 *  VIP_EINVAL_MRW_HNDL: invalid memory region handle (e.g. not a FMR region, or an u)
 *  VIP_EPERM: not enough permissions.

 * REMARKS:
 *        mr_prop_p must be a pointer to appropriate memory buffer as properties
 *        table will be copied to that buffer
 *******************************************************************/
VIP_ret_t MM_free_fmr(VIP_RSCT_t usr_ctx,MM_hndl_t mm_hndl,EVAPI_fmr_hndl_t fmr_hndl);

/*************************************************************************
 * Function: MMU_get_num_objs
 *
 * Arguments:
 *  mm_hndl (IN) - MMU object 
 *  num_mrs (OUT) - returns current number of mrs
 *  num_fmrs (OUT) - returns current number of fmrs
 *  num_mws (OUT) - returns current number of mws
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_PDM_HNDL
 *  VIP_EINVAL_PARAM -- NULL pointer given for all three out params
 *
 * Description:
 *   For those output params which are not NULL, returns their current values (e.g.,
 *   if num_mrs is not null, will return the current number of MRs).  At least one
 *   output parameter must be non-NULL.
 *************************************************************************/ 
VIP_ret_t MMU_get_num_objs(MM_hndl_t mm_hndl,u_int32_t *num_mrs, u_int32_t *num_fmrs,u_int32_t *num_mws );



#endif /*__MMU_HEADER */
