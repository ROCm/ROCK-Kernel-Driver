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

#ifndef H_VIP_HOBUL_H
#define H_VIP_HOBUL_H

#include <mtl_common.h>
#include <vapi.h>
#include <vip.h>
#include <hobkl.h>
#include <hhul.h>

typedef  struct HOBUL_st *  HOBUL_hndl_t;


/*************************************************************************
 * Function: HOBUL_new
 *
 * Arguments:
 *  vipkl_hndl: The handle for the HOBKL object to be associated with.
 *  hobul_hndl_p: The returned HOBUL.
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_ID: Given device is not opened.
 *  
 *
 * Description:
 *  This is the constructor of the HOBUL object. The created object is associated with the HCA 
 *  associated with given HOBKL module. The given hobkl_hndl is used to retrieve all the information 
 *  of its HCA required for its operation. This includes:
 *  ·	Associated HCA-HAL handle
 *  ·	CQ entry size.
 *  ·	RQ entry maximum size.
 *  ·	SQ entry maximum size.
 *  
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_new(/*IN*/ VIP_hca_hndl_t vipkl_hndl,/*OUT*/ HOBUL_hndl_t *hobul_hndl_p);

/*************************************************************************
 * Function: HOBUL_delete
 *
 * Arguments:
 *  hobul_hndl: The object to destroy.
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL: Invalid HOBUL handle.
 *  
 *
 * Description:
 *  This function frees all the resources associated with this HOBUL instance. This includes:
 *  ·	Closing all associated QPs
 *  ·	Destroying all CQs
 *  ·	Unbinding all memory windows and destroying them.
 *  ·	destroying all memory regions.
 *  ·	Freeing HOBUL data structure
 *  
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_delete(/*IN*/ HOBUL_hndl_t hobul_hndl);
void HOBUL_delete_force(/*IN*/ HOBUL_hndl_t hobul_hndl);

VIP_ret_t HOBUL_inc_ref_cnt(/*IN*/ HOBUL_hndl_t hobul_hndl);
VIP_ret_t HOBUL_dec_ref_cnt(/*IN*/ HOBUL_hndl_t hobul_hndl);


/*************************************************************************
 * Function: HOBUL_create_cq
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  min_num_o_entries: Minimum number of entries in CQ
 *  cq_hndl_p: Handle of allocated CQ 
 *  num_o_entries_p: Returned actual num. of CQEs
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EAGAIN: Not enough resources
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle
 *  
 *
 * Description:
 *  Creation of a CQ requires allocation of a buffer in memory for the CQ. 
 *  This function queries the HCA properties for the size of a CQ entry and allocates a memory 
 *  buffer of enough size to accommodate the minimum CQ size required. Then it passes the virtual 
 *  address and size of allocated buffer to CQM's Create CQ function. CQM will take care of 
 *  registering this memory buffer with MMU and allocating the CQ resources. If the operation fails, 
 *  it releases the memory.
 *  On success, this function records the CQ handle of VIP (to return) and the appropriate HH's CQ 
 *  handle. This is required for CQ polling in user-space. On failure the allocated buffers should 
 *  be decollated.
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_create_cq(  /*IN*/ HOBUL_hndl_t hobul_hndl,
                          /*IN*/ VAPI_cqe_num_t min_num_o_entries,
                          /*OUT*/ VAPI_cq_hndl_t *cq_hndl_p, 
                          /*OUT*/ VAPI_cqe_num_t *num_o_entries_p);

/*************************************************************************
 * Function: HOBUL_destroy_cq
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  cqm_cq_hndl: The CQ to destroy
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle
 *  VIP_EINVAL_CQ_HNDL
 *  
 *
 * Description:
 *  This function call the CQM of its HOBUL twin in order to free given CQ resources.
 *  If the CQM succeeds it will return the location of the CQ buffer and size to free. 
 *  This function will free the buffer and other CQ resources in HOBUL. Otherwise the failure error 
 *  is returned and the CQ is considered to be remained. *  
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_destroy_cq(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_cq_hndl_t cqm_cq_hndl);


/*************************************************************************
 * Function: HOBUL_resize_cq
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  cq_hndl: CQ to resize
 *  min_num_o_entries: Minimum number of entries in resized CQ
 *  num_o_entries_p: Returned actual num. of CQEs in resized CQ
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EAGAIN: Not enough resources
 *  VIP_E2BIG_CQ_NUM: Too many CQEs outstanding in the CQ to fit in new size
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle
 *  VIP_EINVAL_CQ_HNDL
 *
 * Description:
 *   Invoke HHUL_resize_cq_prep + VIPKL_resize_cq + HHUL_resize_cq_done
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_resize_cq(
    /*IN*/  HOBUL_hndl_t    hobul_hndl,
    /*IN*/  VAPI_cq_hndl_t  cq_hndl,
    /*IN*/ VAPI_cqe_num_t min_num_o_entries,
    /*OUT*/ VAPI_cqe_num_t *num_o_entries_p
);

/*************************************************************************
 * Function: HOBUL_get_cq_props
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  cq_hndl: The CQ to query
 *  num_of_entries_p: Returned max. number of CQEs in the CQ
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle
 *  VIP_EINVAL_CQ_HNDL
 *  
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_get_cq_props(
    /*IN*/  HOBUL_hndl_t    hobul_hndl,
    /*IN*/  VAPI_cq_hndl_t  cq_hndl,
    /*OUT*/ VAPI_cqe_num_t  *num_of_entries_p);


/*************************************************************************
 * Function: HOBUL_poll_cq
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  cqm_cq_hndl: The CQ handle
 *  comp_desc_p: Work completion description (if returned VIP_OK)
 *  
 *
 * Returns:
 *  VIP_OK: comp_desc_p holds valid data
 *  VIP_EAGAIN: Given CQ is empty
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle
 *  VIP_EINVAL_CQ_HNDL
 *  
 *
 * Description:
 *  This function uses its knowledge of HH's CQ handle matching given CQ in order to call HH's CQ 
 *  polling function in user space. If the CQ is not empty the first completion entry is "pop"ed 
 *  into the given data structure.
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_poll_cq(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_cq_hndl_t cqm_cq_hndl,
                        /*OUT*/ VAPI_wc_desc_t *comp_desc_p);

VIP_ret_t HOBUL_poll_cq_block( 
                                /*IN*/ HOBUL_hndl_t    hobul_hndl,
                              /*IN*/ VAPI_cq_hndl_t  cq_hndl,
                              /*IN*/  MT_size_t      timeout_usec,
                              /*OUT*/ VAPI_wc_desc_t *comp_desc_p);

VIP_ret_t HOBUL_poll_cq_unblock(
                       /*IN*/  HOBUL_hndl_t      hobul_hndl,
                       /*IN*/  VAPI_cq_hndl_t       cq_hndl
                       );

VIP_ret_t HOBUL_peek_cq(
  /*IN*/  HOBUL_hndl_t         hobul_hndl,
  /*IN*/  VAPI_cq_hndl_t       cq_hndl,
  /*IN*/  VAPI_cqe_num_t       cqe_num
);


VIP_ret_t HOBUL_create_srq(
                         IN      HOBUL_hndl_t     hobul_hndl,
                         IN      VAPI_srq_attr_t  *srq_props_p,
                         OUT     VAPI_srq_hndl_t  *srq_hndl_p,
                         OUT     VAPI_srq_attr_t  *actual_srq_props_p
                         );

VIP_ret_t HOBUL_query_srq(
                         IN      HOBUL_hndl_t     hobul_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl,
                         OUT     VAPI_srq_attr_t   *srq_attr_p
                         );

VIP_ret_t HOBUL_destroy_srq(
                         IN      HOBUL_hndl_t     hobul_hndl,
                         IN      VAPI_srq_hndl_t   srq_hndl
                         );


/*************************************************************************
 * Function: HOBUL_alloc_qp
 *
 * Arguments:
 *  hobul_hndl: The HOBUL handle
 *  qp_type: VAPI_REGULAR_QP for normal VAPI_create_qp, otherwise defines special QP type
 *  port: Port number (valid for special QPs only)
 *  qp_init_attr_p: (see in VAPI_create_qp)
 *  qp_ext_attr_p: Extended "Create QP" Verb parameters
 *  qp_hndl_p: The handle for created QP in QPM
 *  qp_prop_p: (see in VAPI_create_qp)
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL: invalid HOBUL
 *  VIP_EINVAL_PD_HNDL
 *  VIP_EAGAIN: Not enough resources to create the QP
 *  
 *
 * Description:
 *  This function perform the needed preparations in user space before calling the kernel space 
 *  function QPM_create_qp. This includes mainly the allocation of buffers for descriptors of SQ and
 *  RQ . The allocation is done based on the requested QP capabilities.
 *  On succesfull return from QPM_create_qp this function records the QP properties needed to 
 *  perform WQE posting in user space (QP handle of HH - probably the QP num.). On failure the 
 *  allocated buffers are deallocated.
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_alloc_qp(/*IN*/  HOBUL_hndl_t        hobul_hndl,
                          /*IN*/  VAPI_special_qp_t   qp_type,
                          /*IN*/  IB_port_t           port,
                          /*IN*/  VAPI_qp_init_attr_t *qp_init_attr_p,
                          /*IN*/  VAPI_qp_init_attr_ext_t *qp_ext_attr_p,
                          /*OUT*/ VAPI_qp_hndl_t      *qp_hndl_p,
                          /*OUT*/ VAPI_qp_num_t       *qpn_p,
                          /*OUT*/ VAPI_qp_cap_t       *qp_cap_p);


/*************************************************************************
 * Function: HOBUL_destroy_qp
 *
 * Arguments:
 *  hobul_hndl
 *  qp_hndl
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EINVAL_QP_HNDL
 *  
 *
 * Description:
 *  This function calls QPM_destroy_qp in kernel space and on success takes care of releasing user 
 *  space related resources (e.g. descriptors buffers).
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_destroy_qp(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_qp_hndl_t qp_hndl);


/*************************************************************************
 * Function: HOBUL_modify_qp
 *
 * Arguments:
 *  hobul_hndl
 *  qp_hndl
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EINVAL_QP_HNDL
 *  
 * Description:
 *   This function invokes VIPKL_modify_qp and then HHUL_modify_qp_done
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_modify_qp(          
    /*IN*/      HOBUL_hndl_t  hobul_hndl,
    /*IN*/      VAPI_qp_hndl_t        qp_hndl,
    /*IN*/      VAPI_qp_attr_t       *qp_attr_p,
    /*IN*/      VAPI_qp_attr_mask_t  *qp_attr_mask_p,
    /*OUT*/     VAPI_qp_cap_t        *qp_cap_p
);


/*************************************************************************
 * Function: HOBUL_query_qp
 *
 * Arguments:
 *  hobul_hndl
 *  qp_hndl
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EINVAL_QP_HNDL
 *  
 * Description:
 *   This function invokes VIPKL_query_qp and translates all VIPKL handles to VAPI's
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_query_qp(
    /*IN*/      HOBUL_hndl_t          hobul_hndl,
    /*IN*/      VAPI_qp_hndl_t        qp_hndl,
    /*OUT*/     VAPI_qp_attr_t       *qp_attr_p,
    /*OUT*/     VAPI_qp_attr_mask_t  *qp_attr_mask_p,
    /*OUT*/     VAPI_qp_init_attr_t  *qp_init_attr_p,
    /*OUT*/     VAPI_qp_init_attr_ext_t *qp_init_attr_ext_p);


/*************************************************************************
 * Function: HOBUL_k_sync_qp_state
 *
 * Arguments:
 *  1) hobul_hndl: hca object.
 *  2) qp_hndl: usel level handle of the QP.
 *  3) curr_state: updated QP state as reported by kernel level agent.
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EINVAL_QP_HNDL
 *  VIP_EAGAIN
 *  
 * Description:
 *  for user level processes who have become aware of a QP state
 *  transition by a kernel agent, This function will sync the user 
 *	level QP context & execute any task required by the transition.
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_k_sync_qp_state(
    /*IN*/      HOBUL_hndl_t  		hobul_hndl,
    /*IN*/      VAPI_qp_hndl_t      qp_hndl,
    /*IN*/      VAPI_qp_state_t     curr_state
);

/*************************************************************************
 * Function: HOBUL_post_sendq
 *
 * Arguments:
 *  hobul_hndl
 *  qp_hndl
 *  sr_desc_p : The send request descriptor
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EINVAL_QP_HNDL
 *  VIP_EAGAIN           : Not enough resources.
 *  VIP_EINVAL_QP_STATE  : Invalid QP State.
 *  VIP_EINVAL_CN_TYPE   : Invalid Completion Notification Type.
 *  VIP_EINVAL_SG_FMT    : Invalid Scatter/Gather format.
 *  VIP_EINVAL_SG_LEN    : Invalid Scatter/Gather Length.
 *  VIP_ATOMIC_NA        : Atomic operations not support.
 *  VIP_INVALID_AV       : Invalid Address Vector Handle.
 *  
 *
 * Description:
 *  Post a send work request 
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_post_sendq(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_qp_hndl_t qp_hndl,
                           /*IN*/ VAPI_sr_desc_t *sr_desc_p);
VIP_ret_t HOBUL_post_inline_sendq(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_qp_hndl_t qp_hndl,
                                  /*IN*/ VAPI_sr_desc_t *sr_desc_p);
VIP_ret_t HOBUL_post_gsi_sendq(/*IN*/ HOBUL_hndl_t   hobul_hndl,
                               /*IN*/ VAPI_qp_hndl_t  qp_hndl,
                               /*IN*/ VAPI_sr_desc_t *sr_desc_p,
                               /*IN*/ VAPI_pkey_ix_t pkey_index 
                                 /* use given Pkey index instead of QP's*/);
VIP_ret_t HOBUL_post_list_sendq(/*IN*/ HOBUL_hndl_t hobul_hndl,
                                /*IN*/ VAPI_qp_hndl_t qp_hndl,
                                /*IN*/ u_int32_t num_of_requests,
                                /*IN*/ VAPI_sr_desc_t *sr_desc_array);

/*************************************************************************
 * Function: HOBUL_post_receiveq
 *
 * Arguments:
 *  hobul_hndl
 *  qp_hndl
 *  rr_desc_p : The receive request descriptor
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EINVAL_QP_HNDL
 *  VIP_EAGAIN           : Not enough resources.
 *  VIP_EINVAL_QP_STATE  : Invalid QP State.
 *  VIP_EINVAL_CN_TYPE   : Invalid Completion Notification Type.
 *  VIP_EINVAL_SG_FMT    : Invalid Scatter/Gather format.
 *  VIP_EINVAL_SG_LEN    : Invalid Scatter/Gather Length.
 *  
 *
 * Description:
 *  Post a send work request 
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_post_recieveq(/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ VAPI_qp_hndl_t qp_hndl,
                              /*IN*/ VAPI_rr_desc_t *rr_desc_p);
VIP_ret_t HOBUL_post_list_recieveq(/*IN*/ HOBUL_hndl_t hobul_hndl,
                                   /*IN*/ VAPI_qp_hndl_t qp_hndl,
                                   /*IN*/ u_int32_t num_of_requests,
                                   /*IN*/ VAPI_rr_desc_t *rr_desc_array);


VIP_ret_t HOBUL_post_srq(
                       IN HOBUL_hndl_t          hobul_hndl,
                       IN VAPI_srq_hndl_t       srq_hndl,
                       IN u_int32_t             rwqe_num,
                       IN VAPI_rr_desc_t       *rwqe_array,
                       OUT u_int32_t           *rwqe_posted_p);


/*************************************************************************
 * Function: HOBUL_create_av
 *  
 *  Arguments:  
 *    hobul_hndl (IN) The HOBUL object handle
 *    pd_hndl - PD handle for this AV
 *    av_data_p (IN) Pointer to address vector data (as defined in IB-spec.)
 *    av_p (OUT) Pointer to return allocated address vector handle
 *  
 *  Returns:  
 *    VIP_OK
 *    VIP_EAGAIN: Not enough resources
 *    VIP_EINVAL_HOBUL_HANDLE
*  
 *  Description:  
 *  
 *  Allocate address vector entry in the AVDB and provide handle for future 
 *  reference.
 *  
 *
 *************************************************************************/

VIP_ret_t  HOBUL_create_av(/*IN*/ HOBUL_hndl_t hobul_hndl, /*IN*/ VAPI_pd_hndl_t pd_hndl,/*IN*/ VAPI_ud_av_t  *av_data_p, 
                           /*OUT*/ HHUL_ud_av_hndl_t  *av_p);

/*************************************************************************
 * Function: HOBUL_destroy_av
 *  
 *  Arguments:  
 *    hobul_hndl (IN)  The HOBUL object handle
 *    av (IN)  Address vector handle
 *  
 *  Returns:  
 *    VIP_OK
 *    VIP_EINVAL_HOBUL_HANDLE
 *    VIP_EINVAL_AV_HANDLE
 *  
 *  Description:  
 *  
 *  Remove address vector entry from the AVDB.
 *  
 *
 *************************************************************************/

VIP_ret_t  HOBUL_destroy_av (/*IN*/ HOBUL_hndl_t hobul_hndl, /*IN*/ HHUL_ud_av_hndl_t av);

/*************************************************************************
 * Function: HOBUL_get_av_data
 *  
 *  Arguments:  
 *  hobul_hndl (IN)  The HOBUL object handle
 *  av (IN)  Address vector handle
 *  av_data_p (OUT)  Pointer to return address vector data
 *  
 *  Returns:  
 *    VIP_OK
 *    VIP_EINVAL_HOBUL_HANDLE
 *    VIP_EINVAL_AV_HANDLE
 *  
 *  Description:  
 *  
 *  Get data in address vector entry.
 *  
 *
 *************************************************************************/

VIP_ret_t  HOBUL_get_av_data (/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ HHUL_ud_av_hndl_t av,
                             /*OUT*/ VAPI_ud_av_t *av_data_p);


/*************************************************************************
 * Function: HOBUL_set_av_data
 *  
 *  
 *  Arguments:  
 *  hobul_hndl (IN)  The HOBUL object handle
 *  av (IN)  Address vector handle
 *  av_data_p (IN)  Pointer to address vector data
 *  
 *  Returns:  VIP_OK
 *  VIP_EINVAL_HOBUL_HANDLE
 *  VIP_EINVAL_AV_HANDLE
 *  
 *  Description:  
 *  
 *  Modify data in address vector entry.
 *  
 *
 *************************************************************************/

VIP_ret_t  HOBUL_set_av_data (/*IN*/ HOBUL_hndl_t hobul_hndl,/*IN*/ HHUL_ud_av_hndl_t av, 
                             /*IN*/ VAPI_ud_av_t *av_data_p);


/*************************************************************************
 * Function: HOBUL_req_comp_notif
 *  
 *  
 *  Arguments:  
 *  hobul_hndl (IN)  The HOBUL object handle
 *  cq_hndl (IN)     handle of CQ to notify completion in
 *  notif_type (IN)  Notification type
 *  
 *  Returns:  VIP_OK
 *  VIP_EINVAL_HOBUL_HANDLE
 *  VIP_EINVAL_AV_HANDLE
 *  
 *  Description:  
 *  
 *  Request completion notification for next completion.
 *  
 *
 *************************************************************************/
VIP_ret_t HOBUL_req_comp_notif(
    /*IN*/ HOBUL_hndl_t hobul_hndl,
    /*IN*/  VAPI_cq_hndl_t          cq_hndl,
    /*IN*/  VAPI_cq_notif_type_t    notif_type
);

/*************************************************************************
 * Function: HOBUL_req_ncomp_notif
 *  
 *  
 *  Arguments:  
 *  hobul_hndl (IN)  The HOBUL object handle
 *  cq_hndl (IN)     handle of CQ to notify completion in
 *  cqe_num (IN)     Number of outstanding CQE which trigger this notification 
 *                  (This may be 1 up to CQ size, limited by HCA capability - 0x7FFF for InfiniHost)
 *  
 *  Returns:  VIP_OK
 *  VIP_EINVAL_HOBUL_HANDLE
 *  VIP_EINVAL_AV_HANDLE
 *  
 *  Description:  
 *  
 *  Request completion notification for next completion.
 *  
 *
 *************************************************************************/
VIP_ret_t HOBUL_req_ncomp_notif(
    /*IN*/  HOBUL_hndl_t       hobul_hndl,
    /*IN*/  VAPI_cq_hndl_t    cq_hndl,
    /*IN*/  VAPI_cqe_num_t    cqe_num
);


 /*************************************************************************
 * Function: HOBUL_attach_qp_mc_grp
 *
 * Arguments:
 *  hca_hndl:  HCA Handle.
 *  mcg_dlid:  DLID of multicast group
 *  ipv6_addr: IPV6 address of multicast group
 *  qp_hndl:   QP Handle
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  OUT_OF_RESOURCES
 *  INVALID_HCA_HNDL
 *  INVALID_HCA_PORT_NUMBER
 *  INVALID_MULTICAST_GROUP_DLID
 *  INVALID_MULTCAST_GROUP_IPV6
 *  INVALID_QP_HNDL	
 *  INVALID_QP_SERVICE_TYPE
 *  NUM_OF_MULT_GROUP_ATTACHED_QP_EX
 *  VAPI_EPERM: not enough permissions.
 *  
 *  
 *
 * Description:
 *  
 *  Attaches qp to multicast group..
 *************************************************************************/ 

VIP_ret_t HOBUL_attach_qp_mc_grp(
                                /*IN*/      HOBUL_hndl_t        hobul_hndl,
                                /*IN*/      IB_lid_t            mcg_ddid, 
                                /*IN*/      QPM_qp_hndl_t       qp_hndl);

/*************************************************************************
 * Function: HOBUL_attach_qp_mc_grp
 *
 * Arguments:
 *  hca_hndl: HCA Handle.
 *  port: port number
 *  mcg_dlid: DLID of multicast group
 *  ipv6_addr: IPV6 address of multicast group
 *  qp_hndl: QP Handle
 *  
 *  
 *
 * Returns:
 *  VAPI_OK
 *  OUT_OF_RESOURCES
 *  INVALID_HCA_hndl
 *  INVALID_HCA_PORT_NUMBER
 *  INVALID_MULTICAST_GROUP_DLID
 *  INVALID_MULTCAST_GROUP_IPV6
 *  INVALID_QP_hndl	
 *  INVALID_QP_SERVICE_TYPE
 *  NUM_OF_MULT_GROUP_ATTACHED_QP_EX
 *  VAPI_EPERM: not enough permissions.
 *  
 *
 * Description:
 *  
 *  Detaches qp from multicast group..
 *************************************************************************/ 

VIP_ret_t HOBUL_detach_qp_mc_grp(
                                /*IN*/      HOBUL_hndl_t        hobul_hndl,
                                /*IN*/      IB_lid_t            mcg_ddid, 
                                /*IN*/      QPM_qp_hndl_t       qp_hndl);

/*************************************************************************
 * Function: HOBUL_alloc_pd
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  max_num_avs:  max number of avs to be allocated for this PD
 *  for_sqp:      TRUE if PD is for sqp
 *  PDM_pd_hndl_p: Handle of allocated PD in VIP.
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EAGAIN: Not enough resources
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle
 *  
 *
 * Description:
 *  On success, this function records the PD handle of VIP (to return) and the appropriate HH's PD 
 *  handle. 
 *
 *  If the caller desires to use the default maximum number of AVs for this PD,
 *  max_num_avs should be set to EVAPI_DEFAULT_AVS_PER_PD.
 *  
 *************************************************************************/ 
VIP_ret_t HOBUL_alloc_pd (  /*IN*/ HOBUL_hndl_t hobul_hndl,
                          /*IN*/ u_int32_t  max_num_avs,
                          /*IN*/ MT_bool for_sqp, 
                          /*OUT*/ VAPI_pd_hndl_t *pd_hndl_p);

/*************************************************************************
 * Function: HOBUL_destroy_pd
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  pd_hndl_p: PD handle
 *  
 *
 * Returns:
 *  VIP_OK: comp_desc_p holds valid data
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle
 *  VIP_EINVAL_PD_HNDL
 *  
 *
 * Description:
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_destroy_pd (/*IN*/ HOBUL_hndl_t hobul_hndl,
                          /*IN*/ VAPI_pd_hndl_t pd_hndl_p);


/*************************************************************************/
VIP_ret_t  HOBUL_alloc_mw( 
  /* IN  */ HOBUL_hndl_t     hobul_hndl,
  /* IN  */ VAPI_pd_hndl_t   pd,
  /* OUT */ VAPI_mw_hndl_t*  mw_hndl_p,
  /* OUT */ VAPI_rkey_t*     rkey_p
);


/*************************************************************************/
VIP_ret_t HOBUL_query_mw( 
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_mw_hndl_t    mw_hndl,
  /* OUT */ VAPI_rkey_t*      rkey_p,
  /* OUT */ VAPI_pd_hndl_t*   pd_p
);

/*************************************************************************/
VIP_ret_t HOBUL_bind_mw(
  /* IN  */ HOBUL_hndl_t            hobul_hndl,
  /* IN  */ VAPI_mw_hndl_t          mw_hndl,
  /* IN  */ const VAPI_mw_bind_t*   bind_prop_p,
  /* IN  */ VAPI_qp_hndl_t          qp, 
  /* IN  */ VAPI_wr_id_t            id,
  /* IN  */ VAPI_comp_type_t        comp_type,
  /* OUT */ VAPI_rkey_t*            new_rkey
);

/*************************************************************************/
VIP_ret_t HOBUL_dealloc_mw( 
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_mw_hndl_t    mw_hndl
);

/*************************************************************************/
/*************************************************************************/
VIP_ret_t HOBUL_set_priv_context4qp(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_qp_hndl_t       qp,
  /* IN  */   void *               priv_context);

/*************************************************************************/
VIP_ret_t HOBUL_get_priv_context4qp(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_qp_hndl_t       qp,
  /* OUT */   void **             priv_context_p);

/*************************************************************************/
VIP_ret_t HOBUL_set_priv_context4cq(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_cq_hndl_t       cq,
  /* IN  */   void *               priv_context);

/*************************************************************************/
VIP_ret_t HOBUL_get_priv_context4cq(
  /* IN  */ HOBUL_hndl_t      hobul_hndl,
  /* IN  */ VAPI_cq_hndl_t       cq,
  /* OUT */   void **             priv_context_p);

/*************************************************************************
 * Function: HOBUL_get_vendor_info
 *
 * Arguments:
 *  hobul_hndl: The HCA handle 
 *  vendor_info_p: Returned vendor info
 *  
 *
 * Returns:
 *  VIP_OK: comp_desc_p holds valid data
 *  VIP_EINVAL_HCA_HNDL: invalid hobul handle (or NULL pointer given for output)
 *  
 *
 * Description:
 *
 *************************************************************************/ 
VIP_ret_t HOBUL_get_vendor_info(/*IN*/ HOBUL_hndl_t hobul_hndl,
                                 /*OUT*/ VAPI_hca_vendor_t *vendor_info_p);

/*************************************************************************
 * Function for mapping from VIPKL's handle to VAPI's (and back for PD)
 * (to be used by vipul.c for "direct" VIPKL access)
 *************************************************************************/
 

VIP_ret_t HOBUL_vapi2vipkl_pd(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ VAPI_pd_hndl_t vapi_pd,
                              /*OUT*/ PDM_pd_hndl_t *vipkl_pd_p);

VIP_ret_t HOBUL_vipkl2vapi_pd(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ PDM_pd_hndl_t vipkl_pd,
                              /*OUT*/ VAPI_pd_hndl_t *vapi_pd_p);

VIP_ret_t HOBUL_vapi2vipkl_cq(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ VAPI_cq_hndl_t vapi_cq,
                              /*OUT*/ CQM_cq_hndl_t *vipkl_cq_p);

VIP_ret_t HOBUL_vapi2vipkl_qp(/*IN*/ HOBUL_hndl_t hobul_hndl,
                              /*IN*/ VAPI_qp_hndl_t vapi_qp,
                              /*OUT*/ QPM_qp_hndl_t *vipkl_qp_p);

VIP_ret_t HOBUL_set_async_event_handler(
                                  /*IN*/  HOBUL_hndl_t                 hobul_hndl,
                                  /*IN*/  VAPI_async_event_handler_t   handler,
                                  /*IN*/  void*                        private_data,
                                  /*OUT*/ EVAPI_async_handler_hndl_t  *async_handler_hndl_p);


VIP_ret_t HOBUL_clear_async_event_handler(
                               /*IN*/ HOBUL_hndl_t                hobul_hndl, 
                               /*IN*/ EVAPI_async_handler_hndl_t async_handler_hndl);

VIP_ret_t HOBUL_evapi_set_comp_eventh( 
  /*IN*/HOBUL_hndl_t                     hobul_hndl,
  /*IN*/VAPI_cq_hndl_t                   cq_hndl,
  /*IN*/VAPI_completion_event_handler_t  completion_handler,
  /*IN*/void *                           private_data);

VIP_ret_t HOBUL_evapi_clear_comp_eventh(
  /*IN*/HOBUL_hndl_t                     hobul_hndl,
  /*IN*/VAPI_cq_hndl_t                   cq_hndl);

#if defined(MT_SUSPEND_QP)
VIP_ret_t HOBUL_suspend_qp(
                /*IN*/ HOBUL_hndl_t   hobul_hndl,
                /*IN*/ VAPI_qp_hndl_t qp_hndl,
                /*IN*/ MT_bool        suspend_flag);

VIP_ret_t HOBUL_suspend_cq(
                /*IN*/ HOBUL_hndl_t   hobul_hndl,
                /*IN*/ VAPI_cq_hndl_t cq_hndl,
                /*IN*/ MT_bool        do_suspend);
#endif

#endif

