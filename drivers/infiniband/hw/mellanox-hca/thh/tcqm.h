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

#if !defined(H_TCQM_H)
#define H_TCQM_H

#include <mtl_common.h>
#include <vapi_types.h>
#include <mosal.h>
#include <hh.h>
#include <thh.h>
#include <thh_hob.h>


/************************************************************************
 *  Function: THH_cqm_create
 *  
 *  Arguments:
 *    hob         - The THH_hob object in which this object will be included
 *    log2_max_cq - (log2) Max. number of CQs (CQC table size)
 *    cqm_p       - The allocated CQM object
 *  
 *  Returns: 
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available
 *  
 *  Description: 
 *    This function creates the THH_cqm object.
 */
extern HH_ret_t  THH_cqm_create(
  THH_hob_t   hob,          /* IN  */  
  u_int8_t    log2_max_cq,  /* IN  */
  u_int8_t    log2_rsvd_cqs,  /* IN  */
  THH_cqm_t*  cqm_p         /* OUT */
);

/************************************************************************
 *  Function: THH_cqm_destroy
 *  
 *  Arguments:
 *    cqm -         The object to destroy
 *    hca_failure - If TRUE object destruction is required 
 *                  due to HCA (hardware) failure (e.g.  surprise  removal)
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handle 
 *  
 *  Description: 
 *    Free all CQM related resources.
 */
extern HH_ret_t  THH_cqm_destroy(
  THH_cqm_t  cqm,         /* IN */ 
  MT_bool    hca_failure  /* IN */
);

/************************************************************************
 *  Function: THH_cqm_create_cq
 *  
 *  Arguments:
 *    cqm -                     CQM object context 
 *    user_protection_context - User context of given CQE buffer
 *    comp_eqn -                Completion Event Queue
 *    error_eqn -               Error Error Queue
 *    cq_ul_resources_p         buffers, requested and/or actually created.
 *    cq_p -                    The allocated CQ handle (probably CQ index).
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available to complete operation
 *  
 *  Description:
 *    Set up a CQ resource.
 */
extern HH_ret_t  THH_cqm_create_cq(
  THH_cqm_t               cqm,                     /* IN  */
  MOSAL_protection_ctx_t  user_protection_context, /* IN  */
  THH_eqn_t               comp_eqn,                /* IN  */
  THH_eqn_t               error_eqn,               /* IN  */
  THH_cq_ul_resources_t*  cq_ul_resources_p,       /* IO  */
  HH_cq_hndl_t*           cq_p                     /* OUT */
);

/************************************************************************
 *  Function: THH_cqm_modify_cq
 *  
 *  Arguments:
 *    cqm (IN) -        CQM object context
 *    cq  (IN) -        CQ to resize
 *    cq_ul_resources_p (IO)- CQ resources allocated/defined in user space 
 *                            and returned producer index
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *    HH_EAGAIN - Not enough resources available to complete operation
 *  
 *  Description:
 *    Modify CQ by replacing the CQEs buffer. 
 *    Replace CQEs buffer with new buffer given is cq_ul_resources_p (new cqe_buf + buf_sz) 
 *    Return in cq_ul_resources_p the next prodcuer index (to start with in new buffer)     
 */
HH_ret_t  THH_cqm_resize_cq(
  THH_cqm_t               cqm,                     /* IN */
  HH_cq_hndl_t            cq,                      /* IN */
  THH_cq_ul_resources_t*  cq_ul_resources_p        /* IO */
);

/************************************************************************
 *  Function: THH_cqm_destroy_cq
 *  Arguments:
 *    cqm - The THH_cqm object handle
 *    cq - The CQ to destroy
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handles 
 *  
 *  Description:
 *    Free CQ resources.
 */
extern HH_ret_t  THH_cqm_destroy_cq(
  THH_cqm_t     cqm /* IN */,
  HH_cq_hndl_t  cq  /* IN */
);

/************************************************************************
 *  Function: THH_cqm_query_cq
 *  
 *  Arguments:
 *    cqm -          The THH_cqm object handle
 *    cq -           The CQ to query
 *    num_o_cqes_p - Maximum outstanding CQEs for this CQ 
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handles
 *  
 *  Description:
 *    Query CQ for number of outstanding CQEs limit.
 */
extern HH_ret_t  THH_cqm_query_cq(
  THH_cqm_t        cqm,           /* IN  */
  HH_cq_hndl_t     cq,            /* IN  */
  VAPI_cqe_num_t*  num_o_cqes_p   /* IN  */
);


/************************************************************************
 *  Function: THH_cqm_get_num_cqs
 *  
 *  Arguments:
 *    cqm -       The THH_cqm object handle
 *    num_cqs_p - number of CQs currently allocated 
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handles
 *  
 *  Description:
 */
HH_ret_t  THH_cqm_get_num_cqs(
  THH_cqm_t  cqm,         /* IN */
  u_int32_t  *num_cqs_p   /* OUT*/
);

#if defined(MT_SUSPEND_QP)
/************************************************************************
 *  Function: THH_cqm_suspend_cq
 *  
 *  Arguments:
 *    cqm -   The THH_cqm object handle
 *    cq  -   CQ handle 
 *    do_suspend -- if TRUE, suspend (i.e., unpin the CQ's resources).
 *                  if FALSE, unsuspend (i.e., re-pin the CQs resources).
 *                           
 *  
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid handles
 *  
 *  Description:
 */
HH_ret_t  THH_cqm_suspend_cq(
  THH_cqm_t        cqm,           /* IN  */
  HH_cq_hndl_t     cq,            /* IN  */ 
  MT_bool          do_suspend     /* IN  */
);
#endif
#endif /* H_TCQM_H */
