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
#ifndef H_EVAPI_H
#define H_EVAPI_H

#include <mtl_common.h>
#include <vapi.h>


/**********************************************************
 * 
 * Function: EVAPI_get_hca_hndl
 *
 * Arguments:
 *  hca_id : HCA ID to get handle for
 *  hca_hndl_p : Returned handle
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_ID : No such opened HCA.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (open device file or ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  Get handle of an already opened HCA.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_get_hca_hndl(
  IN      VAPI_hca_id_t          hca_id,
  OUT     VAPI_hca_hndl_t       *hca_hndl_p
);

/**********************************************************
 * 
 * Function: EVAPI_release_hca_hndl
 *
 * Arguments:
 *  hca_hndl : HCA handle to for which to release process resources
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL : No such opened HCA.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  Release all resources used by this process for an opened HCA.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_release_hca_hndl(
  IN      VAPI_hca_hndl_t     hca_hndl
);



/**********************************************************
 * 
 * Function: EVAPI_k_get_cq_hndl
 *
 * Arguments:
 *  hca_hndl : HCA handle
 *  cq_hndl  : VAPI cq handle
 *  k_cq_hndl_p: pointer to kernel level handle for the cq
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL : No such opened HCA.
 *  VIP_EINVAL_CQ_HNDL : No such CQ.
 *
 * Description:
 *    Get the vipkl cq handle for the cq. This may be used by a user level process
 *    to pass this handle to another kernel driver which may then use
 *    EVAPI_k_set/clear_comp_eventh() to attach/detach kernel handlers
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_k_get_cq_hndl( IN  VAPI_hca_hndl_t hca_hndl,
                                IN  VAPI_cq_hndl_t  cq_hndl,
                                OUT VAPI_k_cq_hndl_t *k_cq_hndl_p);


#ifdef __KERNEL__
/**********************************************************
 * 
 * Function: EVAPI_k_set_comp_eventh (kernel space only)
 *
 * Arguments:
 *  k_hca_hndl : HCA handle
 *  k_cq_hndl : cq handle
 *  completion_handler : handler to call for completions on
 *                       Completion Queue cq_hndl
 *  private_data       : pointer to data for completion handler
 *  completion_handler_hndl:  returned handle to use for clearing this
 *                            completion handler
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL : No such opened HCA.
 *  VIP_EINVAL_CQ_HNDL : No such CQ.
 *
 * Description:
 *  Registers a specific completion handler to handle completions 
 *  for a specific completion queue.  The private data give here
 *  is provided to the completion callback when a completion occurs
 *  on the given CQ.  If the private data is a pointer, it should point
 *  to static or "malloc'ed" data; The private data must be available 
 *  until this completion handler instance is cleared (with 
 *  EVAPI_k_clear_comp_eventh).
 *
 * Note:
 *  This function is exposed to kernel modules only.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_k_set_comp_eventh(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_cq_hndl_t                 k_cq_hndl,
  IN   VAPI_completion_event_handler_t  completion_handler,
  IN   void *                           private_data,
  OUT  EVAPI_compl_handler_hndl_t       *completion_handler_hndl );


/**********************************************************
 * 
 * Function: EVAPI_k_clear_comp_eventh
 *
 * Arguments:
 *  k_hca_hndl : HCA handle
 *  completion_handler_hndl:  handle to use for clearing this
 *                            completion handler
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL : No such opened HCA.
 *  VIP_EINVAL_CQ_HNDL  : No such CQ.
 *
 * Description:
 *  Clears a completion handler which was registered 
 *  to handle completions for a specific completion queue.
 *  If a handler was not registered, returns OK anyway.
 *
 * Note:
 *  This function is exposed to kernel modules only.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_k_clear_comp_eventh(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   EVAPI_compl_handler_hndl_t       completion_handler_hndl);


/**********************************************************
 * 
 * Function: EVAPI_k_set_destroy_cq_cbk
 *
 * Arguments:
 *  k_hca_hndl : HCA handle
 *  k_cq_hndl: Kernel level CQ handle as known from EVAPI_k_get_cq_hndl()
 *  cbk_func: Callback function to invoke when the CQ is destroyed
 *  private_data: Caller's context to be used when invoking the callback
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL : No such opened HCA.
 *  VIP_EINVAL_CQ_HNDL  : No such CQ.
 *  VAPI_EBUSY: A destroy_cq callback is already set for this CQ
 *  
 *
 * Description:
 *   Set a callback function that notifies the caller (a kernel module that
 *   uses EVAPI_k_set_comp_eventh ) when a CQ is destroyed.
 *   The function is meant to be used in order to clean the kernel module's
 *   context for that resource, and not in order to clear the completion handler.
 *   The callback is invoked after the CQ handle is already invalid
 *   so EVAPI_k_clear_comp_eventh is not suppose to be called for the 
 *   obsolete CQ (the completion_handler_hndl is already invalid).
 *   This callback is implicitly cleared after it is called.
 *
 * Note: Only a single context in kernel may invoke this function per CQ.
 *       Simultanous invocation by more than one kernel context,
 *       for the same CQ, will result in unexpected behavior.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_k_set_destroy_cq_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_cq_hndl_t                 k_cq_hndl,
  IN   EVAPI_destroy_cq_cbk_t           cbk_func,
  IN   void*                            private_data
);
 
/**********************************************************
 * 
 * Function: EVAPI_k_clear_destroy_cq_cbk
 *
 * Arguments:
 *  k_hca_hndl : HCA handle
 *  k_cq_hndl: Kernel level CQ handle as known from EVAPI_k_get_cq_hndl()
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL : No such opened HCA.
 *  VIP_EINVAL_CQ_HNDL  : No such CQ.
 *
 * Description:
 *  Clear the callback function set in EVAPI_k_set_destroy_cq_cbk().
 *  Use this function when the kernel module stops using the given k_cq_hndl.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_k_clear_destroy_cq_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_cq_hndl_t                k_cq_hndl
);
 
 
#endif /* __KERNEL__ */


/**********************************************************
 * 
 * Function: EVAPI_set_comp_eventh
 *
 * Arguments:
 *  hca_hndl : HCA handle
 *  cq_hndl  : cq handle
 *  completion_handler : handler to call for completions on
 *                       Completion Queue cq_hndl
 *  private_data       : pointer to data for completion handler
 *  completion_handler_hndl:  returned handle to use for clearing this
 *                            completion handler
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL : No such opened HCA.
 *  VAPI_EINVAL_CQ_HNDL : No such CQ.
 *  VAPI_EINVAL_PARAM: Event handler is NULL
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  Registers a specific completion handler to handle completions 
 *  for a specific completion queue.  The private data give here
 *  is provided to the completion callback when a completion occurs
 *  on the given CQ.  If the private data is a pointer, it should point
 *  to static or "malloc'ed" data; The private data must be available 
 *  until this completion handler instance is cleared (with 
 *  EVAPI_clear_comp_eventh).
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_set_comp_eventh(
  IN   VAPI_hca_hndl_t                  hca_hndl,
  IN   VAPI_cq_hndl_t                   cq_hndl,
  IN   VAPI_completion_event_handler_t  completion_handler,
  IN   void *                           private_data,
  OUT  EVAPI_compl_handler_hndl_t       *completion_handler_hndl );


/**********************************************************
 * 
 * Function: EVAPI_clear_comp_eventh
 *
 * Arguments:
 *  hca_hndl : HCA handle
 *  completion_handler_hndl:  handle to use for clearing this
 *                            completion handler
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL : No such opened HCA.
 *  VAPI_EINVAL_CQ_HNDL  : No such CQ.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  Clears a completion handler which was registered 
 *  to handle completions for a specific completion queue.
 *  If a handler was not registered, returns OK anyway.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_clear_comp_eventh(
  IN   VAPI_hca_hndl_t                  hca_hndl,
  IN   EVAPI_compl_handler_hndl_t       completion_handler_hndl ); 

#define EVAPI_POLL_CQ_UNBLOCK_HANDLER ((VAPI_completion_event_handler_t)(-2))
/**********************************************************
 * 
 * Function: EVAPI_poll_cq_block
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq_hndl: CQ Handle.
 *  timeout_usec: Timeout of blocking in micro-seconds (0 = infinite timeout)
 *  comp_desc_p: Pointer to work completion descriptor structure.
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle
 *  VAPI_EINVAL_PARAM: Event handler not initialized with EVAPI_POLL_CQ_UNBLOCK_HANDLER
 *  VAPI_ETIMEOUT: Blocking timed out (and got no completion event).
 *  VAPI_CQ_EMPTY: Blocking interrupted due to EVAPI_poll_cq_unblock() call OR
 *                  got a completion event due to a previous call to this function or
 *                  another request for completion notification.
 *  VAPI_EINTR: Operation interrupted (OS signal)
 *
 * Description:
 *   Poll given CQ and if empty, request completion notification event and
 *   then sleep until event received, then poll again and return result
 *   (even if still empty - to allow cancelling of blocking, e.g. on signals).
 *
 * Notes:
 *  1) This function will block only if EVAPI_set_comp_eventh was invoked for this
 *     CQ with completion_handler=EVAPI_POLL_CQ_UNBLOCK_HANDLER.
 *     (EVAPI_clear_comp_eventh should be invoked for cleanup, as for regular callback)
 *  2) One cannot set another completion event handler for this CQ 
 *     (handler is bounded to CQ unblocking handler).
 *  3) VAPI_req_comp_notif should not be invoked explicitly for a CQ using this facility.
 *  4) One may still use (non-blocking) VAPI_poll_cq() for this CQ.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_poll_cq_block(
                       IN  VAPI_hca_hndl_t      hca_hndl,
                       IN  VAPI_cq_hndl_t       cq_hndl,
                       IN  MT_size_t            timeout_usec,
                       OUT VAPI_wc_desc_t      *comp_desc_p
                       );

/**********************************************************
 * 
 * Function: EVAPI_poll_cq_unblock
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq_hndl: CQ Handle.
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle
 *
 * Description:
 *   Signal a thread blocked with EVAPI_poll_cq_block() to "wake-up".
 *   ("waked-up" thread will poll the CQ again anyway and return result/completion)
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_poll_cq_unblock(
                       IN  VAPI_hca_hndl_t      hca_hndl,
                       IN  VAPI_cq_hndl_t       cq_hndl
                       );


/**********************************************************
 * 
 * Function: EVAPI_peek_cq
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq_hndl: CQ Handle.
 *  cqe_num: Number of CQE to peek to (next CQE is #1)
 *
 * Returns:
 *  VAPI_OK: At least cqe_num CQEs outstanding in given CQ
 *  VAPI_CQ_EMPTY: Less than cqe_num CQEs are outstanding in given CQ
 *  VAPI_E2BIG_CQ_NUM: cqe_index is beyond CQ size (or 0)
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *
 * Description:
 *  Check if there are at least cqe_num CQEs outstanding in the CQ.
 *  (i.e., peek into the cqe_num CQE in the given CQ). 
 *  No CQE is consumed from the CQ.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_peek_cq(
  IN  VAPI_hca_hndl_t      hca_hndl,
  IN  VAPI_cq_hndl_t       cq_hndl,
  IN  VAPI_cqe_num_t       cqe_num
);

/*************************************************************************
 * Function: EVAPI_req_ncomp_notif
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq_hndl: CQ Handle.
 *  cqe_num: Number of outstanding CQEs which trigger this notification 
 *           This may be 1 up to CQ size, limited by HCA capability (0x7FFF for InfiniHost)
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL: invalid CQ handle
 *  VAPI_E2BIG_CQ_NUM: cqe_index is beyond CQ size or beyond HCA notification capability (or 0)
 *                     For InfiniHost cqe_num is limited to 0x7FFF.
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *   Request notification when CQ holds at least N (non-polled) CQEs
 *  
 *
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_req_ncomp_notif(
                              IN  VAPI_hca_hndl_t         hca_hndl,
                              IN  VAPI_cq_hndl_t          cq_hndl,
                              IN  VAPI_cqe_num_t          cqe_num
                              );


/*****************************************************************************
 * Function: EVAPI_list_hcas
 *
 * Arguments:
 *            hca_id_buf_sz(IN)    : Number of entries in supplied array 'hca_id_buf_p',
 *            num_of_hcas_p(OUT)   : Actual number of currently available HCAs
 *            hca_id_buf_p(OUT)    : points to an\n array allocated by the caller of 
 *                                   'VAPI_hca_id_t' items, able to hold 'hca_id_buf_sz' 
 *                                    entries of that item.

 *
 * Returns:   VAPI_OK     : operation successful.
 *            VAPI_EINVAL_PARAM : Invalid parameter.
 *            VAPI_EAGAIN : hca_id_buf_sz is smaller than num_of_hcas.  In this case, NO hca_ids
 *                         are returned in the provided array.
 *            VAPI_ESYSCALL: A procedure call to the underlying O/S (open device file, or ioctl) 
 *                 has returned an error.
 *
 * Description:
 *   Used to get a list of the device IDs of the available devices.
 *   These names can then be used in VAPI_open_hca to open each
 *   device in turn.
 *
 *   If the size of the supplied buffer is too small, the number of available devices
 *   is still returned in the num_of_hcas parameter, but the return code is set to
 *   HH_EAGAIN.  In this case, NO device IDs are returned; the user must simply supply
 *   a larger array and call this procedure again. (The user may call this function
 *   with hca_id_buf_sz = 0 and hca_id_buf_p = NULL to get the number of hcas currently
 *   available).
 *****************************************************************************/

VAPI_ret_t MT_API EVAPI_list_hcas(/* IN*/ u_int32_t         hca_id_buf_sz,
                            /*OUT*/ u_int32_t*       num_of_hcas_p,
                            /*OUT*/ VAPI_hca_id_t*   hca_id_buf_p);

/**********************************************************
 * Function: EVAPI_process_local_mad
 *
 * Arguments:
 *  hca_hndl : HCA handle
 *  port : port which received the MAD packet
 *  slid : Source LID of incoming MAD. Required Mkey violation trap genenration.
 *         (this parameter is ignored if EVAPI_MAD_IGNORE_MKEY flag is set)
 *  proc_mad_opts: Modifiers to MAD processing.
 *         currently, only modifier is : EVAPI_MAD_IGNORE_MKEY
 *  mad_in_p:  pointer to MAD packet received
 *  mad_out_p: pointer to response MAD packet, if any
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL : No such opened HCA.
 *  VAPI_EINVAL_PORT  : No such port.
 *  VAPI_EINVAL_PARAM : invalid parameter (error in mad_in packet)
 *  VAPI_EGEN
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  submits a MAD packet to the local HCA for processing.
 *  Obtains the response MAD.  mad_out_p must be a buffer of
 *  size 256 (IB_MAD_LEN) allocated by caller.
 *
 *  for the proc_mad_opts argument, if EVAPI_MAD_IGNORE_MKEY is given, MKEY
 *  will be ignored when processing the MAD.  If zero is given for this argument
 *  MKEY validation will be performed (this is the default), and the given slid
 *  may be used to generate a trap in case of Mkey violation, as defined in IB-spec.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_process_local_mad(
  IN   VAPI_hca_hndl_t       hca_hndl,
  IN   IB_port_t             port,
  IN   IB_lid_t              slid, /* ignored on EVAPI_MAD_IGNORE_MKEY */
  IN   EVAPI_proc_mad_opt_t  proc_mad_opts,
  IN   const void *          mad_in_p,
  OUT  void *                mad_out_p);                           


/**********************************************************
 * Function: EVAPI_set/get_priv_context4qp/cq
 *
 * Arguments:
 *  hca_hndl : HCA handle
 *  qp/cq : QP/CQ for which the private context refers
 *  priv_context : Set/Returned private context associated with given QP/CQ
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL : No such opened HCA.
 *  VAPI_EINVAL_QP_HNDL/VAPI_EINVAL_CQ_HNDL : Unknown QP/CQ within current context
 *
 * Description:
 *    Set/Get private context for QP/CQ.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_set_priv_context4qp(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   VAPI_qp_hndl_t       qp,
  IN   void *               priv_context);

VAPI_ret_t MT_API EVAPI_get_priv_context4qp(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   VAPI_qp_hndl_t       qp,
  OUT   void **             priv_context_p);

VAPI_ret_t MT_API EVAPI_set_priv_context4cq(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   VAPI_cq_hndl_t       cq,
  IN   void *               priv_context);

VAPI_ret_t MT_API EVAPI_get_priv_context4cq(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   VAPI_cq_hndl_t       cq,
  OUT   void **             priv_context_p);
  

#ifdef __KERNEL__
/* FMRs are not allowed in user-space */

/*************************************************************************
 * Function: EVAPI_alloc_fmr
 *
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
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 *  
 *
 * Description:
 *   Allocate a fast memory region resource, to be used with EVAPI_map_fmr/EVAPI_unmap_fmr
 *  
 **********************************************************/
VAPI_ret_t MT_API EVAPI_alloc_fmr(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   EVAPI_fmr_t          *fmr_props_p,
  OUT  EVAPI_fmr_hndl_t     *fmr_hndl_p
);

/*************************************************************************
 * Function: EVAPI_map_fmr
 *
 * Arguments:
 *  hca_hndl :	HCA Handle.
 *  fmr_hndl: The fast memory region handle.
 *  map_p: Properties of mapping request
 *  l_key_p: Allocated L-Key for the new mapping 
 *          (may be different than prev. mapping of the same FMR)
 *  r_key_p: Allocated R-Key for the new mapping
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources (invoke EVAPI_unmap_fmr for this region and then retry)
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_MR_HNDL: invalid memory region handle (e.g. not a FMR region)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 * Description:
 *   Map given memory block to this fast memory region resource.
 *   Upon a return from this function, the new L-key/R-key may be used in regard to CI operations
 *   over given memory block.
 *  
 **********************************************************/
VAPI_ret_t MT_API EVAPI_map_fmr(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   EVAPI_fmr_hndl_t     fmr_hndl,
  IN   EVAPI_fmr_map_t      *map_p,
  OUT  VAPI_lkey_t          *l_key_p,
  OUT  VAPI_rkey_t          *r_key_p
);


/*************************************************************************
 * Function: EVAPI_unmap_fmr
 *
 * Arguments:
 *  hca_hndl :	HCA Handle.
 *  num_of_fmrs_to_unmap: Number of memory regions handles in given array
 *  fmr_hndls_array: Array of num_of_fmrs_to_unmap FMR handles to unmap.(!max limit: 2000 handles)
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_MR_HNDL: invalid memory region handle (e.g. not a FMR region)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 * Description:
 *   Unmap given FMRs.
 *   In case of a failure other than VAPI_EINVAL_HCA_HNDL or VAPI_ESYSCALL,
 *   the state of the FMRs is undefined (some may still be mapped while others umapped).
 *  
 **********************************************************/
VAPI_ret_t MT_API EVAPI_unmap_fmr(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   MT_size_t            num_of_fmrs_to_unmap,
  IN   EVAPI_fmr_hndl_t     *fmr_hndls_array
);

/*************************************************************************
 * Function: EVAPI_free_fmr
 *
 * Arguments:
 *  hca_hndl :	HCA Handle.
 *  fmr_hndl: The fast memory region handle.
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EAGAIN: out of resources
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_MR_HNDL: invalid memory region handle (e.g. not a FMR region, or an u)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  
 * Description:
 *   Free given FMR resource.
 *  
 **********************************************************/
VAPI_ret_t MT_API EVAPI_free_fmr(
  IN   VAPI_hca_hndl_t      hca_hndl,
  IN   EVAPI_fmr_hndl_t       mr_hndl
);

#endif /* FMRs in kernel only */

/* *************************************************************************
 * Function: EVAPI_post_inline_sr
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_hndl: QP Handle.
 *  sr_desc_p: Pointer to the send request descriptor attributes structure.
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_QP_HNDL: invalid QP handle
 *  VAPI_E2BIG_WR_NUM: Too many posted work requests.
 *  VAPI_EINVAL_OP: invalid operation
 *  VAPI_EINVAL_QP_STATE: invalid QP state
 *  VAPI_EINVAL_NOTIF_TYPE: invalid completion notification type
 *  VAPI_EINVAL_SG_FMT: invalid scatter/gather list format 
 *  VAPI_EINVAL_SG_NUM: invalid scatter/gather list length
 *                    (too much data for inline send with this QP) 
 *  VAPI_EINVAL_AH: invalid address handle
 *  VAPI_EPERM: not enough permissions.
 *
 * Description:
 *   Post data in given gather list as inline data in a send WQE.
 *  (Only for Sends and RDMA-writes, with optional immediate)
 *
 * Note:
 *  1) No L-key checks are done. Data is copied to WQE from given virtual address in 
 *     this process memory space.
 *  2) Maximum data is limited by maximum WQE size for this QP's 
 *     send queue. Information on this limitation may be queried via VAPI_query_qp
 *     (property max_inline_data_sq in QP capabilities).
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_post_inline_sr(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_qp_hndl_t        qp_hndl,
                       IN VAPI_sr_desc_t       *sr_desc_p
);

/* *************************************************************************
 * Function: EVAPI_post_sr_list
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_hndl: QP Handle.
 *  num_of_requests: Number of send requests in the given array
 *  sr_desc_array: Pointer to an array of num_of_requests send requests
 *  
 * Returns:
 *  VAPI_OK
 *	VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *	VAPI_EINVAL_QP_HNDL: invalid QP handle
 *  VAPI_E2BIG_WR_NUM: Too many posted work requests.
 *  VAPI_EINVAL_OP: invalid operation
 *  VAPI_EINVAL_QP_STATE: invlaid QP state
 *  VAPI_EINVAL_NOTIF_TYPE: invalid completion notification  
 *       type
 *  VAPI_EINVAL_SG_FMT: invalid scatter/gather list format
 *  VAPI_EINVAL_SG_NUM: invalid scatter/gather list length
 *  VAPI_EINVAL_AH: invalid address handle
 *  VAPI_EAGAIN: not enough resources to complete operation (not enough WQEs)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_EINVAL_PARAM: num_of_requests is 0 or sr_desc_array is NULL
 *  
 * Description:
 *  The verb posts num_of_requests send queue work requests, as given in the sr_desc_array
 *  In case of a failure none of the requests is posted ("all or nothing").
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_post_sr_list(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_qp_hndl_t        qp_hndl,
                       IN u_int32_t             num_of_requests,
                       IN VAPI_sr_desc_t       *sr_desc_array
                       );

/* *************************************************************************
 * Function: EVAPI_post_gsi_sr
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_hndl   : QP Handle.
 *  sr_desc_p : Pointer to the send request descriptor attributes structure.
 *  pkey_index: P-Key index in Pkey table of the port of the QP to put in BTH of sent GMP
 *  
 * Returns:
 *  VAPI_OK
 *	VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *	VAPI_EINVAL_QP_HNDL: invalid QP handle (or not a GSI QP)
 *  VAPI_E2BIG_WR_NUM: Too many posted work requests.
 *  VAPI_EINVAL_OP: invalid operation
 *  VAPI_EINVAL_QP_STATE: invlaid QP state
 *  VAPI_EINVAL_SG_NUM: invalid scatter/gather list length
 *  VAPI_EINVAL_AH: invalid address handle
 *  VAPI_EPERM: not enough permissions.
 *  
 * Description:
 *  The verb posts a send queue work request to the given GSI QP, with given P-key index
 *  used in the GMP's BTH, instead of the QP's P-key.
 *  This function has identical sematics to VAPI_post_sr, but for the overriden P-Key index.
 *  Using this function allows one to change P-key used by the given GSI QP without having
 *  to modify to SQD state first.
 *
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_post_gsi_sr(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_qp_hndl_t        qp_hndl,
                       IN VAPI_sr_desc_t       *sr_desc_p,
                       IN VAPI_pkey_ix_t        pkey_index
                       );

/************************************************************************
 * Function: EVAPI_post_rr_list
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_hndl: QP Handle.
 *  num_of_requests: Number of receive requests in the given array
 *  rr_desc_array: Pointer to an array of num_of_requests receive requests
 *  
 * returns: 
 *  VAPI_OK
 * 	VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 * 	VAPI_EINVAL_QP_HNDL: invalid QP handle
 *  VAPI_EINVAL_SRQ_HNDL: QP handle used for a QP associted with a SRQ (use VAPI_post_srq)
 *  VAPI_E2BIG_WR_NUM: Too many posted work requests.
 *  VAPI_EINVAL_OP: invalid operation
 *  VAPI_EINVAL_QP_STATE: invlaid QP state
 *  VAPI_EINVAL_SG_NUM: invalid scatter/gather list length
 *  VAPI_EAGAIN: Not enough resources to complete operation (not enough WQEs)
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_EINVAL_PARAM: num_of_requests is 0 or rr_desc_array is NULL
 *  
 * Description:
 *  The verb posts all the given receive requests to the receive queue. 
 *  Given QP must have num_of_requests available WQEs in its receive queue.
 *  In case of a failure none of the requests is posted ("all or nothing").
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_post_rr_list(
                       IN VAPI_hca_hndl_t       hca_hndl,
                       IN VAPI_qp_hndl_t        qp_hndl,
                       IN u_int32_t             num_of_requests,
                       IN VAPI_rr_desc_t       *rr_desc_array
                       );


/* *************************************************************************
 * Function: EVAPI_k_get_qp_hndl
 *
 * Arguments:
 *  1) hca_hndl	: HCA Handle.
 *  2) qp_ul_hndl: user level QP Handle.
 *  3) qp_kl_hndl_p: Pointer to the kernel level handle of the QP requested in argument 2.
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_QP_HNDL: invalid QP handle
 *
 * Description:
 * Retrieve the kernel level handle of a QP in user level. Should be invoked in user level
 * to get a handle to be used by a kernel level code. This handle is valid only for special
 * verbs as described below. 
 *
 * Note:
 * The kernel QP handle is passed to the kernel module by the application. It should use
 * some IOCTL path to this kernel module.
 *
 *************************************************************************/ 

VAPI_ret_t MT_API EVAPI_k_get_qp_hndl(
					/*IN*/	VAPI_hca_hndl_t 	hca_hndl,
					/*IN*/	VAPI_qp_hndl_t 	qp_ul_hndl,
					/*OUT*/	VAPI_k_qp_hndl_t *qp_kl_hndl);


#ifdef __KERNEL__

/* *************************************************************************
 * Function: EVAPI_k_modify_qp
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_kl_hndl: QP kernel level handle.
 *  qp_attr_p: Pointer to QP attributes to be modified.
 *  qp_attr_mask_p: Pointer to the attributes mask to be modified.
 *   
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_QP_HNDL: invalid QP handle
 *  VAPI_EAGAIN: out of resources.
 *  VAPI_EINVAL_QP_HNDL: invalid QP handle.
 *  VAPI_ENOSYS_ATTR: QP attribute is not supported.
 *  VAPI_EINVAL_ATTR: can not change QP attribute.
 *  VAPI_EINVAL_PKEY_IX: PKey index out of range.
 *  VAPI_EINVAL_PKEY_TBL_ENTRY: Pkey index points to an invalid entry in pkey table. 
 *  VAPI_EINVAL_QP_STATE: invalid QP state.
 *  VAPI_EINVAL_RDD_HNDL: invalid RDD domain handle.
 *  VAPI_EINVAL_MIG_STATE: invalid path migration state.
 *  VAPI_E2BIG_MTU: MTU exceeds HCA port capabilities
 *  VAPI_EINVAL_PORT: invalid port
 *  VAPI_EINVAL_SERVICE_TYPE: invalid service type
 *  VAPI_E2BIG_WR_NUM: maximum number of WR requested exceeds HCA capabilities
 *  VAPI_EINVAL_RNR_NAK_TIMER: invalid RNR NAK timer value
 *  VAPI_EPERM: not enough permissions.
 *
 * Description:
 *  Modify QP state of the user level QP by using the kernel level QP handle.
 *
 * Note:
 *  Supported only in kernel modules.
 *
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_k_modify_qp(
					/*IN*/ 	VAPI_hca_hndl_t 	hca_hndl,
					/*IN*/  VAPI_k_qp_hndl_t	qp_kl_hndl,
					/*IN*/  VAPI_qp_attr_t		*qp_attr_p,
                    /*IN*/  VAPI_qp_attr_mask_t *qp_attr_mask_p
);

/**********************************************************
 * 
 * Function: EVAPI_k_set_destroy_qp_cbk
 *
 * Arguments:
 *  k_hca_hndl : HCA handle
 *  k_qp_hndl: Kernel level QP handle as known from EVAPI_k_get_qp_hndl()
 *  cbk_func: Callback function to invoke when the QP is destroyed
 *  private_data: Caller's context to be used when invoking the callback
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL : No such opened HCA.
 *  VIP_EINVAL_QP_HNDL  : No such QP
 *  VAPI_EBUSY: A destroy_cq callback is already set for this QP
 *  
 *
 * Description:
 *   Set a callback function that notifies the caller (a kernel module that
 *   uses EVAPI_k_set_comp_eventh ) when a QP is destroyed.
 *   The function is meant to be used in order to clean the kernel module's
 *   context for that resource.
 *   This callback is implicitly cleared after it is called.
 *
 * Note: Only a single context in kernel may invoke this function per QP.
 *       Simultanous invocation by more than one kernel context,
 *       for the same QP, will result in unexpected behavior.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_k_set_destroy_qp_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_qp_hndl_t                k_qp_hndl,
  IN   EVAPI_destroy_qp_cbk_t           cbk_func,
  IN   void*                            private_data
);
 
/**********************************************************
 * 
 * Function: EVAPI_k_clear_destroy_qp_cbk
 *
 * Arguments:
 *  k_hca_hndl : HCA handle
 *  k_qp_hndl: Kernel level QP handle as known from EVAPI_k_get_qp_hndl()
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL : No such opened HCA.
 *  VIP_EINVAL_QP_HNDL  : No such QP.
 *
 * Description:
 *  Clear the callback function set in EVAPI_k_set_destroy_qp_cbk().
 *  Use this function when the kernel module stops using the given k_qp_hndl.
 *
 **********************************************************/
VAPI_ret_t MT_API EVAPI_k_clear_destroy_qp_cbk(
  IN   VAPI_hca_hndl_t                  k_hca_hndl,
  IN   VAPI_k_qp_hndl_t                k_qp_hndl
);
 
#endif /*__KERNEL__ */

/**************************************************************************
 * Function: EVAPI_k_sync_qp_state
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  qp_ul_hndl: user level QP Handle.
 *  curr_state: The state that should be synch to
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_QP_HNDL: invalid QP handle
 *
 * Description:
 *  This function synchronized the user level QP with a QP state which was modified 
 *  by kernel level agent (as returned from the kernel agent via it's IOCTL). 
 *
 * Note:
 *  Failing to synch the QP state correctly may result in unexpected behavior for 
 *  this QP as well as to other QPs which use the same CQ. There is no need 
 *  to synch upon each QP state change, but application must synch when going back to RESET, 
 *  and for any other state when user level application is going to use that QP in regard 
 *  to that new state (e.g., user level will not allow posting requests to the send 
 *  queue if it was not synch with a transition to RTS state).
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_k_sync_qp_state(
					/*IN*/ 	VAPI_hca_hndl_t 	hca_hndl,
					/*IN*/ 	VAPI_qp_hndl_t 		qp_ul_hndl,
					/*IN*/  VAPI_qp_state_t		curr_state
);

/**************************************************************************
 * Function: EVAPI_alloc_map_devmem
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  mem_type: Type of attached device memory 
 *  bsize: Size in bytes of required memory buffer
 *  align_shift: log2 of alignment requirement
 *            note: in DDR: chunk should be aligned to its size
 *  buf_p: Returned physical address of allocated buffer
 *  virt_addr_p: pointer to virt. adrs mapped to phys adrs (if not NULL, io_remap is done).
 *  dm_hndl: device memory handle
 
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_ENOSYS: Given memory type is not supported in given HCA device
 *  VAPI_EAGAIN: Not enough resources (memory) to satisfy request
 *  VAPI_EINVAL_PARAM: Invalid memory type or invalid alignment requirement
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  Allocate (and map) device attached memory resources (e.g. in InfiniHost's: attached DDR-SDRAM)
 *
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_alloc_map_devmem(
					        VAPI_hca_hndl_t 	hca_hndl,
                            EVAPI_devmem_type_t mem_type,
                            VAPI_size_t           bsize,
                            u_int8_t            align_shift, 
                            VAPI_phy_addr_t*    buf_p,
                            void**              virt_addr_p,
                            VAPI_devmem_hndl_t*  dm_hndl

);

/**************************************************************************
 * Function: EVAPI_query_devmem
 *
 * Arguments:
 *  hca_hndl	: HCA Handle.
 *  mem_type: Type of attached device memory 
 *  align shift
 *  devmem_info_p: pointer to info structure
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_ENOSYS: Given memory type is not supported in given HCA device
 *  VAPI_EINVAL_PARAM: Invalid memory type or invalid alignment requirement
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  query device attached memory resources (e.g. in InfiniHost's attached DDR-SDRAM)
 *
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_query_devmem(
	/* IN  */   VAPI_hca_hndl_t      hca_hndl, 	   
	/* IN  */   EVAPI_devmem_type_t  mem_type, 	   
                u_int8_t             align_shift,
	/* OUT */   EVAPI_devmem_info_t  *devmem_info_p);  


/**************************************************************************
 * Function: EVAPI_free_unmap_devmem
 *
 * Arguments:
 *  hca_hndl	 HCA Handle.
 *  dm_hndl: device memory handle
 *
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_ENOSYS: Given memory type is not supported in given HCA device
 *  VAPI_EINVAL_PARAM: Invalid memory type or invalid alignment requirement
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  Free device attached memory buffer allocated with EVAPI_alloc_devmem.
 *
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_free_unmap_devmem(
					        VAPI_hca_hndl_t 	hca_hndl,
                            VAPI_devmem_hndl_t  dm_hndl
);


/*************************************************************************
 * Function: EVAPI_alloc_pd
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  max_num_avs:  max number of AVs which can be allocated for this PD
 *	pd_hndl_p: Pointer to Handle to Protection Domain object.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EAGAIN: not enough resources.
 *  VAPI_EINVAL_PARAM : invalid parameter
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  					
 *  
 *  
 *
 * Description:
 *  
 *  This call is identical to VAPI_alloc_pd, except that the caller may specify the max 
 *  number of AVs which will be allocatable for this PD.  If the system default value is
 *  desired, the caller can specify EVAPI_DEFAULT_AVS_PER_PD for the requested_num_avs.
 *
 *  Note that max_num_avs may not be zero.  Furthermore, the minimum number of AVs allocated is
 *  2, so if you ask for only 1 AV as the max, the actual maximum will be 2.  For all other values
 *  (up to the maximum supported by the channel adapter) the maximum avs requested will be the
 *  maximum avs obtained.
 *  
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_alloc_pd(
                        /*IN*/      VAPI_hca_hndl_t      hca_hndl,
                        /*IN*/      u_int32_t            max_num_avs, 
                        /*OUT*/     VAPI_pd_hndl_t       *pd_hndl_p
                        );

/*************************************************************************
 * Function: EVAPI_alloc_pd_sqp
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  max_num_avs:  max number of AVs which can be allocated for this PD
 *	pd_hndl_p: Pointer to Handle to Protection Domain object.
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle.
 *  VAPI_EAGAIN: not enough resources.
 *  VAPI_EINVAL_PARAM : invalid parameter
 *  VAPI_EPERM: not enough permissions.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *  					
 * Description:
 *  
 *  This call is identical to EVAPI_alloc_pd, except that it should be used.
 *  when allocating a protection domain for a special QP.  If the caller wishes
 *  that the default number of AVs be used for this PD, use EVAPI_DEFAULT_AVS_PER_PD
 *  for the max_num_avs parameter. Using this function is highly recommended in order
 *  to prevent reads from DDR and to enhance performance of special QPs
 *
 *  Note that max_num_avs may not be zero.
 *  
 *************************************************************************/ 
VAPI_ret_t MT_API EVAPI_alloc_pd_sqp(
                        /*IN*/      VAPI_hca_hndl_t      hca_hndl,
                        /*IN*/      u_int32_t            max_num_avs, 
                        /*OUT*/     VAPI_pd_hndl_t       *pd_hndl_p
                        );
/**********************************************************
 * 
 * Function: EVAPI_open_hca
 *
 * Arguments:
 *  hca_id : HCA ID to open
 *  profile_p : pointer to desired profile
 *  sugg_profile_p : pointer to returned actual values, or suggested values when
 *                a failure is due to parameter values that are too large.
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EBUSY : This HCA has already been opened.
 *  VAPI_EINVAL_PARAM :    profile does not pass sanity checks
 *  VAPI_EINVAL_HCA_ID : No such HCA.
 *  VAPI_EAGAIN : Max number of supported HCAs on this host already open.
 *  VAPI_ENOMEM: some of the parameter values provided in the profile are too large
 *               in the case where the 'require' flag in the profile is set to TRUE.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (open device file, or ioctl) 
 *                 has returned an error.
 *
 * Description:
 *     Opens a registered HCA using the given profile. If profile is NULL, the default
 *     (internal, compiled)  profile is used.  If the sugg_profile_p is NULL, no profile data
 *     is returned.
 * 
 *     If sugg_profile_p is non-NULL:
 *         If the open succeeds, the profile data with which the HCA was opened is returned.  
 *         If a profile structure was provided, and the require flag was false,  these values 
 *         may be smaller than the ones provided in the profile.
 *
 *         If  the open fails with VAPI_ENOMEM, a suggested set of values is returned sugg_profile_p.
 *         Otherwise the values given in   profile_p (which may not be valid) are returned.
 *
 *         In all cases, the returned value of the require flag is the value that was passed in 
 *         profile_p.
 *
 *     'require' flag in the EVAPI_hca_profile_t structure:
 *         if this flag is set to FALSE, and the profile passes sanity checks, and the given
 *         parameter values use too many Tavor resources, the driver will attempt to reduce the 
 *         given values until a set is found which does meet Tavor resource requirements.  The HCA
 *         will be opened using thes reduced set of values.  The reduced set of values is returned 
 *         in the sugg_profile_p structure, if it is non-NULL.
 *
 *         If the 'require' flag is set to TRUE, and the profile passes sanity checks, and the
 *         given set of parameter values use too many Tavor resources, this function will return
 *         VAPI_ENOMEM.  If sugg_profile_p is non-NULL, a set of values will be returned in that
 *         structure which can be used to successfully open the HCA.
 *
 **********************************************************/
VAPI_ret_t MT_API  EVAPI_open_hca(/*IN*/  VAPI_hca_id_t          hca_id,
                           /*IN*/  EVAPI_hca_profile_t    *profile_p,
                           /*OUT*/ EVAPI_hca_profile_t    *sugg_profile_p
                         );

/**********************************************************
 * 
 * Function: EVAPI_close_hca
 *
 * Arguments:
 *  hca_id : HCA ID to close
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_ID : No such HCA.
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  closes an open HCA. This procedure is meant for administrative use only.
 *  User Level Apps should use EVAPI_release_hca_handle.
 *
 **********************************************************/
VAPI_ret_t MT_API  EVAPI_close_hca(
                         /*IN*/      VAPI_hca_id_t          hca_id
                         );


/***********************************************************************
 *  Asynchronous events functions
 ***********************************************************************/

/*************************************************************************
 * Function: EVAPI_set_async_event_handler
 *
 * Arguments:
 *  hca_hndl: HCA Handle 
 *  handler: Async Event Handler function address.
 *  private_data: Pointer to handler context (handler specific).
 *  async_handler_hndl: The handle to the registered handler function. Used in the clear function.
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_PARAM: handler given is NULL
 *  VAPI_EAGAIN: No enough system resources (e.g. memory)
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  
 *  Registers an asynch event handler to get all asynch events for this process. (affiliated and non-affiliated)
 *  Notes: 
 *   1. All non affiliated events are being "broadcast" to all registered handlers. 
 *   2. Affiliated events will be send only to the callback of the process that the resources (e.g. QP) are belong to.  
 *       However, all kernel modules are treated as one process thus all of them will get all affiliated events of all 
 *       kernel modules.
 *   3. Multiple registration of handlers is allowed.
 *
 *  The event handler function prototype is as follows:
 *  
 *  void
 *  VAPI_async_event_handler
 *  (
 *    IN	VAPI_hca_hndl_t       hca_hndl,
 *    IN	VAPI_event_record_t  *event_record_p,
 *    IN	void 		        *private_data
 *  )
 *  
 *  
 *  
 *
 *************************************************************************/ 
                                                                            
VAPI_ret_t MT_API EVAPI_set_async_event_handler(
                                       IN  VAPI_hca_hndl_t                 hca_hndl,
                                       IN  VAPI_async_event_handler_t      handler,
                                       IN  void*                           private_data,
                                       OUT EVAPI_async_handler_hndl_t     *async_handler_hndl_p
                                       );


/**********************************************************
 * 
 * Function: EVAPI_clear_async_event_handler
 *
 * Arguments:
 *  hca_hndl: HCA Handle 
 *  async_handler_hndl: The handle to the registered handler function.
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL: invalid HCA handle
 *  VAPI_EINVAL_PARAM: invalid async_handler_hndl
 *  VAPI_ESYSCALL: A procedure call to the underlying O/S (ioctl) 
 *                 has returned an error.
 *
 * Description:
 *  Function which clears a previously registered by EVAPI_set_async_event_handler. 
 *  This function must be called before calling to EVAPI_release_hca_hndl
 *
 ***********************************************************/


VAPI_ret_t MT_API EVAPI_clear_async_event_handler(
                     /*IN*/ VAPI_hca_hndl_t                  hca_hndl, 
                     /*IN*/ EVAPI_async_handler_hndl_t async_handler_hndl);




#if defined(MT_SUSPEND_QP)
/* *************************************************************************
 * Function: EVAPI_suspend_qp
 *
 * Arguments:
 *  1) hca_hndl	:  HCA Handle.
 *  2) qp_ul_hndl: user level QP Handle.
 *  3) suspend_flag: TRUE--suspend, FALSE--unsuspend
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL:  invalid HCA handle
 *  VAPI_EINVAL_QP_HNDL:   invalid QP handle
 *  VAPI_EINVAL_QP_STATE:  QP is in RESET or INIT state for suspend, 
 *                            not in SUSPEND state for unsuspend 
 *  VAPI_EAGAIN:           QP is currently executing another command (busy)
 *  VAPI_ENOSYS:           Operation not supported on channel adapter
 *  VAPI_EINTR             Could not grab qp-modification mutex
 *  VAPI_EGEN:             operation failed (internal error)
 *
 * Description:
 *  suspend_flag = TRUE:
 *     Suspends operation of the given QP (i.e., places it in the SUSPENDED state).
 *     The operation is valid only if the QP is currently NOT in RESET state,
 *       and NOT in INIT state.
 *
 *  suspend_flag = FALSE:
 *      Transitions the given QP from the SUSPENDED to the state it was in 
 *      when suspended.
 *
 *************************************************************************/ 

VAPI_ret_t EVAPI_suspend_qp(
					/*IN*/	VAPI_hca_hndl_t  hca_hndl,
					/*IN*/	VAPI_qp_hndl_t 	 qp_ul_hndl,
                    /*IN*/  MT_bool          suspend_flag);

/* *************************************************************************
 * Function: EVAPI_suspend_CQ
 *
 * Arguments:
 *  1) hca_hndl	:  HCA Handle.
 *  2) qp_ul_hndl: user level CQ Handle.
 *  3) do_suspend: TRUE--suspend, FALSE--unsuspend
 *  
 *
 * Returns:
 *  VAPI_OK
 *  VAPI_EINVAL_HCA_HNDL:  invalid HCA handle
 *  VAPI_EINVAL_CQ_HNDL:   invalid QP handle
 *  VAPI_EAGAIN:           QP is currently executing another command (busy)
 *  VAPI_EGEN:             operation failed (internal error)
 *
 * Description:
 *  do_suspend = TRUE:
 *     releases locking of CQ cookies region.
 *
 *  do_suspend = FALSE:
 *     Locks the CQE internal mr again.
 *
 *  NOTE: NO safety checks are performed.  If there is an unsuspended QP which is
 *        currently using the cq, the results are not predictable (but will NOT be
 *        good).
 *
 *************************************************************************/ 

VAPI_ret_t EVAPI_suspend_cq(
					/*IN*/	VAPI_hca_hndl_t  hca_hndl,
					/*IN*/	VAPI_cq_hndl_t 	 cq_ul_hndl,
                    /*IN*/  MT_bool          do_suspend);
#endif

#endif
