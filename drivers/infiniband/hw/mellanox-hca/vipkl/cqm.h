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

#ifndef H_VIP_CQM_H
#define H_VIP_CQM_H

#include <mtl_common.h>
#include <vip.h>
#include <evapi.h>
#include <mmu.h>


/*struct CQM_t;
typedef struct CQM_t* CQM_hndl_t;*/

/*typedef u_int32_t CQM_cq_hndl_t;*/
#define CQM_INVAL_CQ_HNDL VAPI_INVAL_HNDL


/*************************************************************************
 * Function: CQM_new
 *
 * Arguments:
 *  hca_hndl (IN): Assoicated HCA object.
 *  cqm_p (OUT): Pointer to a CQM hndl var.
 *
 * Returns:
 *  VIP_OK
 *  VIP_EAGAIN: Not enough resources.
 *
 * Description:
 *  
 *  Create a CQM object associated with given HCA object.
 *
 *************************************************************************/ 
VIP_ret_t CQM_new(/*IN*/ HOBKL_hndl_t hca_hndl,/*OUT*/ CQM_hndl_t *cqm_p);

/*************************************************************************
 * Function: CQM_delete
 *
 * Arguments:
 *  cqm (IN): CQM hndl.
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *
 * Description:
 *  
 *  Destroy CQM object.
 *
 *************************************************************************/ 
VIP_ret_t CQM_delete(/*IN*/ CQM_hndl_t cqm);

/*************************************************************************
 * Function: CQM_set_vapi_hca_hndl
 *
 * Arguments:
 *  cqm (IN): CQM hndl.
 *  hca_hndl (IN): VAPI hca handle for this CQM.
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_HNDL
 *
 * Description:
 *  Set VAPI HCA handle associated with this CQM - for destroy event callbacks.
 *
 *************************************************************************/ 
VIP_ret_t  CQM_set_vapi_hca_hndl(/*IN*/ CQM_hndl_t cqm, /*IN*/VAPI_hca_hndl_t hca_hndl);

/*************************************************************************
 * Function: CQM_query_memory_size
 *
 * Arguments:
 *  cqm (IN): CQM hndl
 *  num_o_entries (IN): number of completion entries that we want to hold
 *  cq_bytes (OUT): Pointer to return size of memory needed in bytes
 *  
 * Returns:
 *  
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *
 
 * Description:
 *  
 *  Query the HCA HAL and return how much memory is needed for a
 *  CQ with the given amount of entries.
 *
 *************************************************************************/ 
VIP_ret_t CQM_query_memory_size(/*IN*/ CQM_hndl_t cqm,
    /*IN*/ MT_size_t num_o_entries,
    /*OUT*/ MT_size_t* cq_bytes
    );

/*************************************************************************
 * Function: CQM_create_cq
 *
 * Arguments:
 *  cqm (IN): CQM hndl
 *  usr_prot_ctx (IN): User protection context for this CQ (usually PID)
 *  cq_ul_resources_p (IN): pointer to user-level CQ resources structure used by driver
 *  min_num_o_entries (IN): Minimum num. of required CQ entries
 *  async_hndl_ctx (IN): handle for asynch error handler context
 *  cq_p (OUT): Pointer to return created CQ hndl
 *  cq_id_p(OUT): ID of this CQ in HW
 *  
 * Returns:
 *  
 *  VIP_OK
 *  VIP_EAGAIN: Not enough resources
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *
 
 * Description:
 *  
 *  Allocate CQ resources in given memory region.
 *  
 *************************************************************************/ 
VIP_ret_t CQM_create_cq(
    /*IN*/ VIP_RSCT_t usr_ctx,
    /*IN*/ CQM_hndl_t cqm,
    /*IN*/ VAPI_cq_hndl_t vapi_cq_hndl,
    /*IN*/ MOSAL_protection_ctx_t  usr_prot_ctx,
    /*IN*/ EM_async_ctx_hndl_t async_hndl_ctx,
    /*IN*/ void     * cq_ul_resources_p,
    /*OUT*/ CQM_cq_hndl_t *cq_hndl_p,
    /*OUT*/ HH_cq_hndl_t* cq_id_p
    );

/*************************************************************************
 * Function: CQM_destroy_cq
 *
 * Arguments:
 *  cqm (IN): CQM hndl
 *  cq (IN): CQ hndl
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *  VIP_EBUSY: CQ is still in use by QPs.
 *
 * Description:
 *  
 *  Release resources of given CQ.
 *  
 *  Releasing CQ involves freeing of CQ resources in hardware via HCA-HAL.
 *  
 *  The CQ cannot be released while QPs are associated with it. The CQ object holds a ref-
 *  erence count that counts associated QPs.
 *
 *************************************************************************/ 
VIP_ret_t CQM_destroy_cq(VIP_RSCT_t usr_ctx,/*IN*/ CQM_hndl_t cqm,/*IN*/ CQM_cq_hndl_t cq,
                         /*IN*/MT_bool in_rsct_cleanup);

/*************************************************************************
 * Function: CQM_get_cq_props
 *
 * Arguments:
 *  cqm(IN): CQM hndl
 *  cq(IN): CQ hndl
    cq_id_p(OUT): ID of this CQ in HW
 *  num_o_entries_p(OUT): Pointer to return number of entries in the CQ
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *
 * Description:
 *  
 *  Get properties of CQ object.
 *  
 *  Note that a NULL pointer in the return parameters denotes that given value is not 
 *  needed, e.g., if one only needs the number of entries in the CQ he can give NULL 
 *  pointer in the last 3 parameters.
 *
 *************************************************************************/ 
VIP_ret_t CQM_get_cq_props(VIP_RSCT_t usr_ctx,/*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*OUT*/ HH_cq_hndl_t* cq_id_p,
    /*OUT HHUL_cq_hndl_t* hhul_cq_hndl_p, */
    /*OUT*/ VAPI_cqe_num_t *num_o_entries_p);

/*************************************************************************
 * Function: CQM_get_cq_by_id
 *
 * Arguments:
 *  cqm(IN): CQM hndl
    cq_id(IN): ID of this CQ in HW
 *  cqprms_p(OUT): pointer to struct returning required params
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl - no CQ hndl bound to this ID in HW
 *
 * Description:
 *
 *  Get CQ bound in HAL to this CQ ID (id of the CQ at the HAL)
 *
 *************************************************************************/ 
VIP_ret_t CQM_get_cq_by_id(
    /*IN*/ HH_cq_hndl_t cq_id,
    /*OUT*/ cq_params_t *cqprms_p
    );


void CQM_get_handler_info(cq_params_t *cqprms_p);

/*************************************************************************
 * Function: CQM_modify_cq_props
 *
 * Arguments:
 *  cqm(IN): CQM hndl
 *  cq(IN): CQ hndl
 *  cq_ul_resources_p (IN): pointer to user-level CQ resources structure 
 *  
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *  VIP_ENOSYS
 *  VIP_EAGAIN
 *  
 * Description:
 *  Resize the CQ by provinding new CQ resources.
 *************************************************************************/ 
VIP_ret_t CQM_resize_cq(
   VIP_RSCT_t usr_ctx,
   /*IN*/ CQM_hndl_t cqm,
  /*IN*/ CQM_cq_hndl_t cq_hndl,
  /*IN/OUT*/ void     * cq_ul_resources_p
);

/*************************************************************************
 * Function: CQM_bind_qp_to_cq
 *
 * Arguments:
 *  cqm(IN): CQM hndl
 *  cq(IN): CQ hndl
 *  qp(IN): Bound QP.
 *  hh_cq_id(OUT): Pointer to return HCA-HAL's CQ ID.
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *  VIP_EBUSY: this QP bound to some CQ
 *  VIP_EAGAIN: not enouth resources
 *  
 *  
 *
 * Description:
 *  
 *  For every new QP this function should be called before the actual creation. This returns 
 *  the CQ ID of this CQ as for HCA-HAL. This is required for QPM to bind a created QP 
 *  to this CQ on creation via HCA-HAL.
 *  
 *  The call to this function is required not only in order to provide QPM with HCA-HAL's 
 *  CQ ID but also to update CQ reference count (there is no need to actually hold QP list). 
 *  The reverse effect is achieved by calling the following CQM_unbind_qp_from_cq.
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t CQM_bind_qp_to_cq(VIP_RSCT_t usr_ctx,/*IN*/ CQM_hndl_t cqm,
    /*IN*/ CQM_cq_hndl_t cq,
    /*IN*/ QPM_qp_hndl_t qp,
    /*OUT*/ HH_cq_hndl_t *hh_cq_id_p);

/*************************************************************************
 * Function: CQM_unbind_qp_from_cq
 *
 * Arguments:
 *  cqm(IN): CQM hndl
 *  cq(IN): CQ hndl
 *  qp(IN): Bound QP.
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *  VIP_EINVAL_QP_hndl  -  this QP hndl was not bound to this CQ
 *  
 *  
 *
 * Description:
 *  
 *  Unbind given QP from given CQ (VIP level only).
 *  
 *  This function must called upon QP destroyal by QPM in order to decrement reference 
 *  count of this CQ.
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t CQM_unbind_qp_from_cq(/*IN*/ CQM_hndl_t cqm,
    /*IN*/ CQM_cq_hndl_t cq,/*IN*/ QPM_qp_hndl_t qp);

/*************************************************************************
 * Function: CQM_get_cq_compl_hndlr_info
 *
 * Arguments:
 *  cqm(IN): CQM hndl
 *  cq(IN): CQ hndl
 *  completion_handler_p(OUT): pointer to address of completion handler for this CQ
 *  private_data_p(OUT): Pointer to private data pointer for the completion handler
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *
 * Description:
 *  
 *  Get special completion handler registered for this CQ.
 *  
 *************************************************************************/ 
VIP_ret_t CQM_get_cq_compl_hndlr_info(/*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*OUT*/ VAPI_completion_event_handler_t* completion_handler_p,
    /*OUT*/ void **private_data_p);

/*************************************************************************
 * Function: CQM_set_cq_compl_hndlr_info
 *
 * Arguments:
 *  cqm(IN): CQM hndl
 *  cq(IN): CQ hndl
 *  completion_handler(IN): address of completion handler for this CQ
 *  private_data(IN): Pointer to private data for the completion handler
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *
 * Description:
 *  
 *  Set a special completion handler registered for this CQ.
 *  
 *************************************************************************/ 
VIP_ret_t CQM_set_cq_compl_hndlr_info(/*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*IN*/ VAPI_completion_event_handler_t completion_handler,
    /*IN*/ void *private_data_p);

/*************************************************************************
 * Function: CQM_clear_cq_compl_hndlr_info
 *
 * Arguments:
 *  cqm(IN): CQM hndl
 *  cq(IN): CQ hndl
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_CQM_hndl
 *  VIP_EINVAL_CQ_hndl
 *
 * Description:
 *  
 *  clears special completion handler registered for this CQ.
 *  
 *************************************************************************/ 
VIP_ret_t CQM_clear_cq_compl_hndlr_info(/*IN*/ CQM_hndl_t cqm_hndl,
    /*IN*/ CQM_cq_hndl_t cq_hndl
    );

VIP_ret_t CQM_set_destroy_cq_cbk(
  /*IN*/ CQM_hndl_t               cqm_hndl,
  /*IN*/ CQM_cq_hndl_t            cq_hndl,
  /*IN*/ EVAPI_destroy_cq_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
);
 
VIP_ret_t CQM_clear_destroy_cq_cbk(
  /*IN*/ CQM_hndl_t               cqm_hndl,
  /*IN*/ CQM_cq_hndl_t            cq_hndl
);

/*************************************************************************
 * Function: CQM_get_num_cqs
 *
 * Arguments:
 *  cqm (IN) - CQM object 
 *  num_qps (OUT) - returns number of currently allocated cqs
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_CQM_HNDL
 *  VIP_EINVAL_PARAM -- NULL pointer given for num_cqs
 *
 * Description:
 *   returns number of currently allocated cqs
 *************************************************************************/ 
VIP_ret_t CQM_get_num_cqs(CQM_hndl_t cqm_hndl, u_int32_t *num_cqs);


/*************************************************************************
 * Function: CQM_get_hh_cq_num
 *
 * Arguments:
 *  cqm (IN) - CQM object 
 *  cq_id_p (OUT) - HH cq number
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_CQM_HNDL
 *  VIP_EINVAL_PARAM -- NULL pointer given for num_cqs
 *
 * Description:
 *************************************************************************/ 
VIP_ret_t CQM_get_hh_cq_num(CQM_hndl_t cqm_hndl, CQM_cq_hndl_t cq_hndl, HH_cq_hndl_t *cq_id_p);

#if defined(MT_SUSPEND_QP)
VIP_ret_t
CQM_suspend_cq (VIP_RSCT_t usr_ctx,CQM_hndl_t cqm_hndl, CQM_cq_hndl_t cq_hndl, MT_bool do_suspend);
#endif

#endif

