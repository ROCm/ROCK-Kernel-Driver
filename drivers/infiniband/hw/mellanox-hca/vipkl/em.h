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

#ifndef H_VIP_EM_H
#define H_VIP_EM_H

#include <vipkl.h>

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
VIP_ret_t EM_new(/*IN*/ HOBKL_hndl_t hobkl_hndl,/*OUT*/ EM_hndl_t *em_p);


/*************************************************************************
 * Function: EM_set_vapi_hca_hndl
 *
 * Arguments:
 *  em: EM object 
 *  hca_hndl: VAPI's HCA handle to report in callbacks
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_EM_hndl
 *  
 * Description:
 *  
 *  Sets VAPI's HCA handle for event callbacks.
 *
 *************************************************************************/ 
VIP_ret_t  EM_set_vapi_hca_hndl(/*IN*/ EM_hndl_t em, /*IN*/VAPI_hca_hndl_t hca_hndl);


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
VIP_ret_t EM_delete(/*IN*/ EM_hndl_t em);

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
    /*IN*/ VAPI_async_event_handler_t handler, /*IN*/ void* private_data);

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
 *  Bind the given completion event handler to the completion event of the assoicated 
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
 *  IN	VAPI_cq_handle_t 	cq,
 *  void *private_data 
 *  )
 *
 *************************************************************************/ 
VIP_ret_t EM_bind_completion_event_handler(/*IN*/ EM_hndl_t em,
    /*IN*/ VAPI_completion_event_handler_t handler, /*IN*/ void* private_data);

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
VIP_ret_t EM_bind_evapi_completion_event_handler(/*IN*/ EM_hndl_t em, /*IN*/ CQM_cq_hndl_t cq_hndl,
    /*IN*/ VAPI_completion_event_handler_t handler, /*IN*/ void* private_data);



VIP_ret_t EM_set_async_event_handler(
                               /*IN*/ EM_hndl_t                       em,
                               /*IN*/ EM_async_ctx_hndl_t             hndl_ctx,
                               /*IN*/ VAPI_async_event_handler_t      handler,
                               /*IN*/ void*                           private_data,
                               /*OUT*/EVAPI_async_handler_hndl_t     *async_handler_hndl);

VIP_ret_t EM_clear_async_event_handler(
                               /*IN*/ EM_hndl_t                       em,
                               /*IN*/ EM_async_ctx_hndl_t             hndl_ctx,
                               /*IN*/ EVAPI_async_handler_hndl_t     async_handler_hndl);

VIP_ret_t EM_alloc_async_handler_ctx(
                               /*IN*/  EM_hndl_t                   em,
                               /*OUT*/ EM_async_ctx_hndl_t         *hndl_ctx);


VIP_ret_t EM_dealloc_async_handler_ctx(
                               /*IN*/ EM_hndl_t                   em,
                               /*IN*/ EM_async_ctx_hndl_t         hndl_ctx);

VIP_ret_t EM_get_num_hca_handles(EM_hndl_t em, u_int32_t *num_hca_hndls);

#endif
