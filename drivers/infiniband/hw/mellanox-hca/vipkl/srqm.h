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

#ifndef H_SRQM_H
#define H_SRQM_H
#include <vapi_common.h>
#include <vip.h>
#include <hh.h>


/******************************************************************************
 *  Function: SRQM_new
 *
 *  Description: Create a SRQM object associated with given HCA object.
 *
 *  Parameters:
 *    hca_hndl(IN) = HCA handle
 *    srqm_hndl_p(OUT)  = Queue Pair Manager for the HCA Object
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_HCA_HNDL : invalid HCA handle
 *    VIP_EAGAIN : out of resources
 *
 *****************************************************************************/
VIP_ret_t SRQM_new(HOBKL_hndl_t hca_hndl, SRQM_hndl_t *srqm_hndl_p);

/******************************************************************************
 *  Function: SRQM_delete
 *
 *  Description: Destroy a SRQM instance from the VIP
 *
 *  Parameters:
 *    srqm_hndl(IN)  = the SRQM handle
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_SRQM_HNDL : invalid SRQM handle
 *
 *****************************************************************************/
VIP_ret_t SRQM_delete(SRQM_hndl_t srqm_hndl);

/******************************************************************************
 *  Function: SRQM_create_srq
 *
 *  Description: Create a SRQ
 *
 *  Parameters:
 *    usr_ctx(IN)    = RSCT context of caller
 *    srqm_hndl(IN)  = the SRQM handle
 *    vapi_srq_hndl(IN) = VAPI handle to be returned on events (SRQM_get_vapi_hndl)
 *    pd_hndl(IN)   = PD to associate SRQ with
 *    async_hndl_ctx(IN) = handle for asynch error handler context
 *    srq_ul_resources_p(IN/OUT) = UL resources to pass to HH
 *    srq_hndl_p(OUT) = Allocated SRQ handle
 *    hh_srq_p(OU)    = Associated HH SRQ handle (SRQ index - helps on debug)
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_SRQM_HNDL : invalid SRQM handle
 *    VIP_EAGAIN
 *    VIP_EINVAL_PD_HNDL
 *
 *****************************************************************************/
VIP_ret_t SRQM_create_srq(VIP_RSCT_t        usr_ctx,
                          SRQM_hndl_t       srqm_hndl, 
                          VAPI_srq_hndl_t   vapi_srq_hndl, 
                          PDM_pd_hndl_t     pd_hndl,
                          EM_async_ctx_hndl_t async_hndl_ctx,
                          void                *srq_ul_resources_p,
                          SRQM_srq_hndl_t     *srq_hndl_p);


/******************************************************************************
 *  Function: SRQM_destroy_srq
 *
 *  Description: Destroy a SRQ
 *
 *  Parameters:
 *    usr_ctx(IN)    = RSCT context of caller
 *    srqm_hndl(IN)  = the SRQM handle
 *    srq_hndl(IN)   = SRQ to destroy
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_SRQM_HNDL 
 *    VIP_EINVAL_SRQ_HNDL
 *
 *****************************************************************************/
VIP_ret_t SRQM_destroy_srq(VIP_RSCT_t usr_ctx, SRQM_hndl_t srqm_hndl, 
                           SRQM_srq_hndl_t srq_hndl);


/******************************************************************************
 *  Function: SRQM_query_srq
 *
 *  Description: Query a SRQ
 *
 *  Parameters:
 *    usr_ctx(IN)    = RSCT context of caller
 *    srqm_hndl(IN)  = the SRQM handle
 *    srq_hndl(IN)   = SRQ to destroy
 *    limit_p(OUT)   = Current "limit" value of the SRQ (0 if limit event is not armed)
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_SRQM_HNDL 
 *    VIP_EINVAL_SRQ_HNDL
 *    VIP_ESRQ            : SRQ in error state
 *
 *****************************************************************************/
VIP_ret_t SRQM_query_srq(VIP_RSCT_t usr_ctx,SRQM_hndl_t srqm_hndl, SRQM_srq_hndl_t srq_hndl, 
                         u_int32_t *limit_p);


/******************************************************************************
 *  Function: SRQM_bind_qp_to_srq
 *
 *  Description: Increment SRQ reference count and return its HH handle
 *
 *  Parameters:
 *    usr_ctx (IN)   = User context of the QP creator (to validate against SRQ's)
 *    srqm_hndl(IN)  = SRQM handle
 *    srq_hndl(IN)   = SRQ handle
 *    hh_srq_p(OUT)  = HH SRQ handle (for QP put in HH_create_qp)
 *
 *  Returns: VIP_OK
 *    VIP_EPERM : SRQ is of different user context
 *    VIP_EINVAL_SRQM_HNDL 
 *    VIP_EINVAL_SRQ_HNDL
 *
 *****************************************************************************/
VIP_ret_t SRQM_bind_qp_to_srq(VIP_RSCT_t usr_ctx, SRQM_hndl_t srqm_hndl,
                              SRQM_srq_hndl_t srq_hndl, HH_srq_hndl_t *hh_srq_p);

/******************************************************************************
 *  Function: SRQM_unbind_qp_from_srq
 *
 *  Description: Decrement SRQ reference count
 *
 *  Parameters:
 *    srqm_hndl(IN)  = SRQM handle
 *    srq_hndl(IN)   = SRQ handle
 *
 *  Returns: VIP_OK
 *    VIP_EPERM : SRQ is of different user context
 *    VIP_EINVAL_SRQM_HNDL 
 *    VIP_EINVAL_SRQ_HNDL
 *
 *****************************************************************************/
VIP_ret_t SRQM_unbind_qp_from_srq(SRQM_hndl_t srqm_hndl, 
                                  SRQM_srq_hndl_t srq_hndl);


/******************************************************************************
 *  Function: SRQM_get_vapi_hndl
 *
 *  Description: Get VAPI handle of a SRQ from its HH handle
 *
 *  Parameters:
 *    srqm_hndl(IN)  = the SRQM handle
 *    hh_srq(IN)     = HH SRQ handle
 *    vapi_srq_p(OUT) = VAPI SRQ handle
 *    async_hndl_ctx(OUT) = Async. event handler context
 *
 *  Returns: VIP_OK
 *    VIP_EINVAL_SRQM_HNDL 
 *    VIP_EINVAL_SRQ_HNDL
 *
 *****************************************************************************/
VIP_ret_t SRQM_get_vapi_hndl(SRQM_hndl_t srqm_hndl, HH_srq_hndl_t hh_srq, 
                             VAPI_srq_hndl_t *vapi_srq_p, EM_async_ctx_hndl_t *async_hndl_ctx);

#endif
