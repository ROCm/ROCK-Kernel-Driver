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


#ifndef H_HHUL_H
#define H_HHUL_H

#include <vapi.h>
#include <hh_common.h>

typedef struct HHUL_hca_dev_st*  HHUL_hca_hndl_t;

typedef void*                    HHUL_mw_hndl_t;
typedef void*                    HHUL_qp_hndl_t;
typedef void*                    HHUL_srq_hndl_t;
#define HHUL_INVAL_SRQ_HNDL ((void*)(MT_ulong_ptr_t)0xFFFFFFFF)
typedef VAPI_ud_av_hndl_t        HHUL_ud_av_hndl_t;
typedef void*                    HHUL_cq_hndl_t;
typedef unsigned long            HHUL_pd_hndl_t;

typedef struct HHUL_qp_init_attr_st {
    IB_ts_t         ts_type;
    HHUL_pd_hndl_t  pd;
    HHUL_cq_hndl_t  sq_cq;
    HHUL_cq_hndl_t  rq_cq;
    VAPI_sig_type_t sq_sig_type;
    VAPI_sig_type_t rq_sig_type;
    HHUL_srq_hndl_t srq;   /* HHUL_INVAL_SRQ_HNDL if not associated with a SRQ */
    VAPI_qp_cap_t   qp_cap;
} HHUL_qp_init_attr_t;


typedef struct HHUL_hca_dev_st   HHUL_hca_dev_t;
typedef struct HHUL_mw_bind_st   HHUL_mw_bind_t;



/************************************************************************
 *  Function:   HHUL_alloc_hca_hndl
 *
 *  Arguments:
 *     vendor_id - The Vendor ID for the device to get handle for
 *     device_id - The device ID for the device to get handle for
 *     hca_ul_resources_p - Resources allocated by the privileged driver
 *     hhul_hca_hndl_p - HCA handle provided for this HCA in user-level
 *                       (user-level resources context)
 *  Returns:   HH_OK,
 *             HH_ENODEV - Unknown device type (based on vendor and device id).
 *             HH_EAGAIN  - Failed to allocate user space resources
 *
 *  Description:
 *
 *             This function invokes the HHUL_init_user_level()
 *             which initilizes resources in user-level for given
 *             HCA. The specific HHUL_init_user_level() is
 *             selected based on device type (given in id
 *             parameters). The hca_ul_resources_p parameter is a
 *             copy of the resources context given by
 *             HH_alloc_ul_resources().
 *
 ************************************************************************/
extern HH_ret_t  HHUL_alloc_hca_hndl
(
  u_int32_t	    vendor_id,
  u_int32_t	    device_id,
  void*             hca_ul_resources_p,
  HHUL_hca_hndl_t*  hhul_hca_hndl_p
);


/************************************************************************
 *  Function: HHUL_init_user_level
 *
 *  This is merely a prototype function, that should have specific
 *  implementation for each type of device.
 *
 *  Arguments:
 *   hca_ul_resourcs_p (IN) -  The user-level resources context
 *                             allocated by
 *                             HH_alloc_ul_resources(). Its size is
 *                             defined by the specific instance of
 *                             this function (per device type).
 *    hhul_hndl_p (OUT) - The user level handle of the HCA
 *
 *  Returns:  HH_OK
 *            HH_EINVAL - Invalid parameters
 *                        (NULL ptr or invalid resources context)
 *  Description:
 *
 *  This function is invoked by the HHUL wrapper for each HCA one wishes
 *  to use in an application, when HHUL_alloc_hca_hndl is invoked. 
 *  The specificdevice function is selected based on the device type. 
 *  It should be called after allocating privileged resources using 
 *  HH_alloc_ul_resources().
 *
 *  This function should allocate all the user-level HCA resources
 *  required by given device in order to later manage the user-level HCA
 *  operations. This includes user-level mirroring of HCA resources data
 *  required while performing OS bypassing and other user-level context.
 *
 *  The returned hhul_hndl is actually a pointer to HHUL device context.
 *
 ************************************************************************/

#if 0
extern HH_ret_t  <deviceType>HHUL_init_user_level
(
  void*             hca_ul_resources_p,
  HHUL_hca_hndl_t*  hhul_hndl_p
);
#endif

typedef struct  HHUL_if_ops_st {
  
  HH_ret_t  (*HHULIF_cleanup_user_level)(HHUL_hca_hndl_t  hhul_hndl);
  
  HH_ret_t  (*HHULIF_alloc_pd_prep)(HHUL_hca_hndl_t  hca_hndl,
                                    HHUL_pd_hndl_t*  hhul_pd_p,
                                    void*            pd_ul_resources_p);
  
  HH_ret_t  (*HHULIF_alloc_pd_avs_prep)(HHUL_hca_hndl_t  hca_hndl,
                                    u_int32_t        max_num_avs,
                                    HH_pdm_pd_flags_t pd_flags,
                                    HHUL_pd_hndl_t*  hhul_pd_p,
                                    void*            pd_ul_resources_p);

  HH_ret_t  (*HHULIF_alloc_pd_done)(HHUL_hca_hndl_t  hca_hndl,
                                    HHUL_pd_hndl_t   hhul_pd,
                                    HH_pd_hndl_t     hh_pd,
                                    void*            pd_ul_resources_p);
  
  HH_ret_t  (*HHULIF_free_pd_prep)(HHUL_hca_hndl_t  hca_hndl,
                                   HHUL_pd_hndl_t   pd,
                                   MT_bool          undo_flag);

  HH_ret_t  (*HHULIF_free_pd_done)(HHUL_hca_hndl_t  hca_hndl,
                                   HHUL_pd_hndl_t   pd);

  HH_ret_t  (*HHULIF_alloc_mw)(HHUL_hca_hndl_t  hhul_hndl,
                               IB_rkey_t        initial_rkey,
                               HHUL_mw_hndl_t*  mw_p);

  HH_ret_t  (*HHULIF_bind_mw)(HHUL_hca_hndl_t   hhul_hndl,
                              HHUL_mw_hndl_t    mw,
                              HHUL_mw_bind_t*   bind_prop_p,
                              IB_rkey_t*        bind_rkey_p);

  HH_ret_t  (*HHULIF_free_mw)(HHUL_hca_hndl_t  hhul_hndl,
                              HHUL_mw_hndl_t   mw);

  HH_ret_t  (*HHULIF_create_ud_av)(HHUL_hca_hndl_t     hca_hndl,
                                   HHUL_pd_hndl_t      pd,
                                   VAPI_ud_av_t*       av_p,
                                   HHUL_ud_av_hndl_t*  ah_p);

  HH_ret_t  (*HHULIF_modify_ud_av)(HHUL_hca_hndl_t    hca_hndl,
                                   HHUL_ud_av_hndl_t  ah,
                                   VAPI_ud_av_t*      av_p);

  HH_ret_t  (*HHULIF_query_ud_av)(HHUL_hca_hndl_t    hca_hndl,
                                  HHUL_ud_av_hndl_t  ah,
                                  VAPI_ud_av_t*      av_p);

  HH_ret_t  (*HHULIF_destroy_ud_av)(HHUL_hca_hndl_t    hca_hndl,
                                    HHUL_ud_av_hndl_t  ah);

  HH_ret_t  (*HHULIF_create_cq_prep)(HHUL_hca_hndl_t  hca_hndl,
                                     VAPI_cqe_num_t   num_o_cqes,
                                     HHUL_cq_hndl_t*  hhul_cq_p,
                                     VAPI_cqe_num_t*  num_o_cqes_p,
                                     void*            cq_ul_resources_p);

  HH_ret_t  (*HHULIF_create_cq_done)(HHUL_hca_hndl_t  hca_hndl,
                                     HHUL_cq_hndl_t   hhul_cq,
                                     HH_cq_hndl_t     hh_cq,
                                     void*            cq_ul_resources_p);

  HH_ret_t  (*HHULIF_resize_cq_prep)(HHUL_hca_hndl_t  hca_hndl,
                                     HHUL_cq_hndl_t   cq,
                                     VAPI_cqe_num_t   num_o_cqes,
                                     VAPI_cqe_num_t*  num_o_cqes_p,
                                     void*            cq_ul_resources_p);
 
  HH_ret_t (*HHULIF_resize_cq_done)(HHUL_hca_hndl_t   hca_hndl,
                                    HHUL_cq_hndl_t    cq,
                                    void*             cq_ul_resources_p);

  HH_ret_t  (*HHULIF_poll4cqe)(HHUL_hca_hndl_t  hca_hndl,
                               HHUL_cq_hndl_t   cq,
                               VAPI_wc_desc_t*  cqe_p);
  
  HH_ret_t  (*HHULIF_peek_cq)(HHUL_hca_hndl_t  hca_hndl,
                              HHUL_cq_hndl_t   cq,
                              VAPI_cqe_num_t   cqe_num);


  HH_ret_t  (*HHULIF_req_comp_notif)(HHUL_hca_hndl_t       hca_hndl,
                                     HHUL_cq_hndl_t        cq,
                                     VAPI_cq_notif_type_t  notif_type);

  HH_ret_t  (*HHULIF_req_ncomp_notif)(HHUL_hca_hndl_t       hca_hndl,
                                      HHUL_cq_hndl_t        cq,
                                      VAPI_cqe_num_t        cqe_num);

  HH_ret_t  (*HHULIF_destroy_cq_done)(HHUL_hca_hndl_t  hca_hndl,
                                      HHUL_cq_hndl_t   cq);

  HH_ret_t  (*HHULIF_create_qp_prep)(HHUL_hca_hndl_t        hca_hndl,
                                     HHUL_qp_init_attr_t*   qp_init_attr_p,
                                     HHUL_qp_hndl_t*        qp_hndl_p,
                                     VAPI_qp_cap_t*         qp_cap_out_p,
                                     void*                  qp_ul_resources_p);

  HH_ret_t  (*HHULIF_special_qp_prep)(HHUL_hca_hndl_t       hca_hndl,
                                      VAPI_special_qp_t     special_qp_type,
                                      IB_port_t             port,
                                      HHUL_qp_init_attr_t*  qp_init_attr_p,
                                      HHUL_qp_hndl_t*       qp_hndl_p,
                                      VAPI_qp_cap_t*        qp_cap_out_p,
                                      void*                 qp_ul_resources_p);

  HH_ret_t  (*HHULIF_create_qp_done)(HHUL_hca_hndl_t  hca_hndl,
                                     HHUL_qp_hndl_t   hhul_qp,
                                     IB_wqpn_t        hh_qp,
                                     void*            qp_ul_resources_p);
  
  HH_ret_t  (*HHULIF_modify_qp_done)(HHUL_hca_hndl_t  hca_hndl,
                                     HHUL_qp_hndl_t   hhul_qp,
                                     VAPI_qp_state_t  cur_state);

  HH_ret_t  (*HHULIF_post_send_req)(HHUL_hca_hndl_t   hca_hndl,
                                    HHUL_qp_hndl_t    qp_hndl,
                                    VAPI_sr_desc_t*   send_req_p);

  HH_ret_t  (*HHULIF_post_inline_send_req)(HHUL_hca_hndl_t   hca_hndl,
                                           HHUL_qp_hndl_t    qp_hndl,
                                           VAPI_sr_desc_t*   send_req_p);
  
  HH_ret_t  (*HHULIF_post_send_reqs)(HHUL_hca_hndl_t   hca_hndl,
                                    HHUL_qp_hndl_t    qp_hndl,
                                    u_int32_t         num_of_requests,
                                    VAPI_sr_desc_t*   send_req_array);
  
  HH_ret_t  (*HHULIF_post_gsi_send_req)(HHUL_hca_hndl_t   hca_hndl,
                                        HHUL_qp_hndl_t    qp_hndl,
                                        VAPI_sr_desc_t*   send_req_p,
                                        VAPI_pkey_ix_t    pkey_index);
  
  HH_ret_t  (*HHULIF_post_recv_req)(HHUL_hca_hndl_t   hca_hndl,
                                    HHUL_qp_hndl_t    qp_hndl,
                                    VAPI_rr_desc_t*   recv_req_p);

  HH_ret_t  (*HHULIF_post_recv_reqs)(HHUL_hca_hndl_t   hca_hndl,
                                    HHUL_qp_hndl_t    qp_hndl,
                                    u_int32_t         num_of_requests,
                                    VAPI_rr_desc_t*   recv_req_array);
  
  HH_ret_t  (*HHULIF_destroy_qp_done)(HHUL_hca_hndl_t  hca_hndl,
                                      HHUL_qp_hndl_t   qp_hndl);
  
HH_ret_t (*HHULIF_create_srq_prep)( 
                                  /*IN*/
                                  HHUL_hca_hndl_t hca, 
                                  HHUL_pd_hndl_t  pd,
                                  u_int32_t max_outs,
                                  u_int32_t max_sentries,
                                  /*OUT*/
                                  HHUL_srq_hndl_t *srq_hndl_p,
                                  u_int32_t *actual_max_outs_p,
                                  u_int32_t *actual_max_sentries_p,
                                  void /*THH_srq_ul_resources_t*/ *srq_ul_resources_p);

HH_ret_t (*HHULIF_create_srq_done)( 
                                  HHUL_hca_hndl_t hca, 
                                  HHUL_srq_hndl_t hhul_srq, 
                                  HH_srq_hndl_t hh_srq, 
                                  void/*THH_srq_ul_resources_t*/ *srq_ul_resources_p
);

HH_ret_t (*HHULIF_destroy_srq_done)( 
                                   HHUL_hca_hndl_t hca, 
                                   HHUL_qp_hndl_t hhul_srq 
);

HH_ret_t (*HHULIF_post_srq)(
                     /*IN*/ HHUL_hca_hndl_t hca, 
                     /*IN*/ HHUL_srq_hndl_t hhul_srq, 
                     /*IN*/ u_int32_t num_of_requests,
                     /*IN*/ VAPI_rr_desc_t *recv_req_array,
                     /*OUT*/ u_int32_t *posted_requests_p
);
  
} HHUL_if_ops_t;


struct HHUL_hca_dev_st {
  HH_hca_hndl_t   hh_hndl;        /* kernel level HH handle of associated HCA */
  char*           dev_desc;            /* Device description (name, etc.) */
  u_int32_t       vendor_id;           /* IEEE's 24 bit Device Vendor ID */
  u_int32_t       dev_id;              /* Device/part ID */
  u_int32_t       hw_ver;              /* Hardware version (Stepping/Rev.) */
  u_int64_t       fw_ver;              /* Device's firmware version (device specific) */
  HHUL_if_ops_t*  if_ops;              /* Interface operations (Function map) */
  MT_size_t       hca_ul_resources_sz; /* #bytes user-level resr. HCA context */
  MT_size_t       pd_ul_resources_sz;  /* #bytes user-level resr. PD context */
  MT_size_t       cq_ul_resources_sz;  /* #bytes user-level resr. CQ context */
  MT_size_t       srq_ul_resources_sz; /* #bytes user-level resr. SRQ context */
  MT_size_t       qp_ul_resources_sz;  /* #bytes user-level resr. QP context */
  void*           device;              /* Pointer to device's private data */
  void*           hca_ul_resources_p;  /* Privileged User-Level resources
                                        * allocated for this HCA in process */
};


struct HHUL_mw_bind_st {
  VAPI_lkey_t     mr_lkey; /* L-Key of memory region to bind to */
  IB_virt_addr_t  start;   /* Memory window.
                            * start virtual address (byte addressing) */
  VAPI_size_t       size;    /* Size of memory window in bytes */
  VAPI_mrw_acl_t  acl;     /* Access Control (R/W permission - local/remote) */
  HHUL_qp_hndl_t  qp;      /* QP to use for posting this binding request */
  VAPI_wr_id_t    id;      /* Work request ID to be used in this binding request*/
  VAPI_comp_type_t comp_type; /* Create CQE or not (for QPs set to signaling per request) */
};



/************************************************************************
 * Function:   HHUL_cleanup_user_level
 *  
 *  
 *  Arguments:      hhul_hndl (IN) - The user level HCA handle
 *  
 *  Returns:        HH_OK
 *                  HH_EINVAL_HCA_HNDL
 *  Description:
 *  
 *  This function frees the device specific user level resources.
 *  
 *  After calling this function the device dependent layer should free the
 *  associated privileged resources by calling the privileged call
 *  HH_free_ul_resources(). The associated hca_ul_resources should be
 *  saved before the call to this function in order to be able to use it
 *  when calling HH_free_ul_resources().
 *  
 ************************************************************************/

#define HHUL_cleanup_user_level(hhul_hndl) \
  (hhul_hndl)->if_ops->HHULIF_cleanup_user_level(hhul_hndl)


/************************************************************************
 * Function:   HHUL_alloc_mw
 *  
 *  
 *  Arguments:  hhul_hndl (IN) - The user level HCA handle
 *              initial_rkey (IN) - The initial R-Key provided by kernel level
 *              mw_p (OUT) - Returned memory window handle for user level
 *  
 *  Returns:    HH_OK
 *              HH_EINVAL_HCA_HNDL
 *  
 *  Description:
 *  
 *  In order to efficiently deal with OS bypassing while performing memory
 *  window binding, this function should be called after a successful
 *  return from kernel level memory window allocation.  The initial R-Key
 *  returned from the kernel level call should be used when allocating
 *  user level memory window context.
 *  
 ************************************************************************/

#define HHUL_alloc_mw(hhul_hndl, initial_rkey, mw_p) \
  (hhul_hndl)->if_ops->HHULIF_alloc_mw(hhul_hndl, initial_rkey, mw_p)


/************************************************************************
 * Function:   HHUL_bind_mw
 *  
 *  
 *  Arguments:  hhul_hndl (IN) - The user level HCA handle
 *              mw (IN) - The memory window handle
 *              bind_prop_p (IN) - Binding properties
 *              bind_rkey_p (OUT) - The allocated R-Key for binding
 *  
 *  Returns:    HH_OK
 *              HH_EINVAL_HCA_HNDL
 *              HH_EINVAL - Invalid parameters (NULL ptrs)
 *              HH_EINVAL_MW_HNDL - Invalid memory window handle
 *              HH_EINVAL_QP_HNDL -
 *                 Given QP handle is unknown or not in the correct
 *                 state state or is not of the correct trasport
 *                 service type (RC,UC,RD).  HH_EAGAIN - No available
 *                 resources to perform given operation.
 *  
 *  Description:
 *  
 *  This function performs the posting of the memory binding WQE based on
 *  given binding properties.  Unbinding of a window may be done by
 *  providing a 0 sized window.
 *  
 ************************************************************************/

#define HHUL_bind_mw(\
    hhul_hndl, mw, bind_prop_p, bind_rkey_p) \
  (hhul_hndl)->if_ops->HHULIF_bind_mw(\
    hhul_hndl, mw, bind_prop_p, bind_rkey_p)


/************************************************************************
 * Function:   HHUL_free_mw
 *  
 *  
 *  Arguments:  hhul_hndl (IN) - The user level HCA handle
 *              mw (IN) - The memory window handle
 *  
 *  Returns:   HH_OK
 *             HH_EINVAL_HCA_HNDL
 *             HH_EINVAL_MW_HNDL - Invalid memory window handle
 *  
 *  Description:
 *  
 *  Free user-level context of given memory window. This function call
 *  should be followed by an invocation to the kernel function which frees
 *  the memory window resource.
 *  
 ************************************************************************/

#define HHUL_free_mw(hhul_hndl, mw) \
  (hhul_hndl)->if_ops->HHULIF_free_mw(hhul_hndl, mw)


/************************************************************************
 * Function:   HHUL_create_ud_av
 *  
 *  
 *  Arguments:      hca_hndl (IN) - User level handle of the HH device context
 *                  pd (IN)   - PD of created ud_av
 *                  av_p (IN) - Given address vector to create handle for
 *                  ah_p (OUT) - The returned address handle
 *  
 *  Returns:        HH_OK,
 *                  HH_ENODEV - Unknown device
 *                  HH_EINVAL - Invalid parameters (NULL pointer, etc.)
 *                  HH_EINVAL_HCA_HNDL - Given HCA is not in "HH_OPENED" state
 *                  HH_EAGAIN - No available UD AV resources
 *  
 *  Description:
 *  
 *  This function allocates the privileged resources and returns a handle
 *  which may be used when posting UD QP WQEs in a system that enforces
 *  privileged UD AVs.
 *  
 ************************************************************************/

#define HHUL_create_ud_av(hca_hndl, pd, av_p, ah_p) \
  (hca_hndl)->if_ops->HHULIF_create_ud_av(hca_hndl, pd, av_p, ah_p)


/************************************************************************
 * Function:   HHUL_modify_ud_av
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              ah (IN) - The address handle to modify
 *              av_p (IN) - Modified address vector
 *  
 *  Returns:    HH_OK,
 *              HH_ENODEV - Unknown device
 *              HH_EINVAL - Invalid parameters (NULL pointer, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is not in "HH_OPENED" state
 *              HH_EINVAL_AV_HNDL - Unknown UD AV handle
 *  
 *  Description:
 *  Modify properties of given UD address handle.
 *  
 ************************************************************************/

#define HHUL_modify_ud_av(hca_hndl, ah, av_p) \
  (hca_hndl)->if_ops->HHULIF_modify_ud_av(hca_hndl, ah, av_p)


/************************************************************************
 * Function:   HHUL_query_ud_av
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              ah (IN) - The address handle to modify
 *              av_p (IN) - Returned address vector
 *  
 *  Returns:    HH_OK,
 *              HH_ENODEV - Unknown device
 *              HH_EINVAL - Invalid parameters (NULL pointer, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is not in "HH_OPENED" state
 *              HH_EINVAL_AV_HNDL - Unknown UD AV handle
 *  
 *  Description:
 *  Get address vector data for given address handle.
 *  
 ************************************************************************/

#define HHUL_query_ud_av(hca_hndl, ah, av_p) \
  (hca_hndl)->if_ops->HHULIF_query_ud_av(hca_hndl, ah, av_p)


/************************************************************************
 * Function:   HHUL_destroy_ud_av
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              ah (IN) - The address handle to destroy
 *  
 *  Returns:    HH_OK,
 *              HH_ENODEV - Unknown device
 *              HH_EINVAL - Invalid parameters (NULL pointer, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is not in "HH_OPENED" state
 *              HH_EINVAL_AV_HNDL - Unknown UD AV handle
 *  
 *  Description:
 *  Free privileged UD address handle resources in HCA.
 *  
 ************************************************************************/

#define HHUL_destroy_ud_av(hca_hndl, ah) \
  (hca_hndl)->if_ops->HHULIF_destroy_ud_av(hca_hndl, ah)


/************************************************************************
 * Function:   HHUL_create_cq_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hh_cq - The CQ to modify
 *              num_o_cqes (IN) - Requested minimum number of CQEs in CQ
 *              hhul_cq_p (OUT) - Returned user-level handle for this CQ
 *              num_o_cqes_p (OUT) - Actual number of CQEs in updated CQ
 *              cq_ul_resources_p (OUT) - Pointer to allocated resources
 *                                  context (of size cq_ul_resources_sz)
 *  	    
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters
 *                           (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown or not
 *                                   in "HH_OPENED" state
 *              HH_EINVAL_CQ_HNDL - Unknown CQ
 *              HH_EAGAIN - No available resources to complete 
 *  	                this operation
 *  
 *  Description:
 *  
 *  Before creating the CQ in a privileged call user level resources must
 *  be allocated. This function deals with the allocation of the required
 *  user level resources based on given parameters.
 *  
 *  The resources context is returned in the cq_ul_resources_p and should
 *  be given to kernel level call. Freeing of these resources is done
 *  using the function HHUL_destroy_cq_done().
 *  
 ************************************************************************/

#define HHUL_create_cq_prep(\
    hca_hndl, num_o_cqes, hhul_cq_p, num_o_cqes_p, cq_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_create_cq_prep(\
    hca_hndl, num_o_cqes, hhul_cq_p, num_o_cqes_p, cq_ul_resources_p)


/************************************************************************
 * Function:   HHUL_create_cq_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hhul_cq (IN) - The user level CQ handle
 *              hh_cq (IN) - The CQ allocated by HH
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_CQ_HNDL - Unknown CQ handle
 *  
 *  Description:
 *  
 *  After creation of the CQ in the privileged call to HH_create_qp()
 *  through VIP's checkings, this function deals with binding of allocated
 *  CQ to pre-allocated user-level context.
 *  
 *  The CQ cannot be polled before calling this function.
 *  
 ************************************************************************/

#define HHUL_create_cq_done(hca_hndl, hhul_cq, hh_cq, cq_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_create_cq_done(hca_hndl, hhul_cq, hh_cq, cq_ul_resources_p)


/************************************************************************
 * Function:   HHUL_resize_cq_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              cq (IN) - The CQ to modify
 *              num_o_cqes (IN) - Requested minimum number of CQEs in CQ
 *              num_o_cqes_p (OUT) - Actual number of CQEs in updated CQ
 *              cq_ul_resources_p (OUT) - Pointer to updated allocated
 *                                  resources context for modified CQ
 *                                  (of size cq_ul_resources_sz)
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_CQ_HNDL - Unknown CQ
 *              HH_EAGAIN - No available resources to complete this operation
 *              HH_EBUSY -  Previous resize is still in progress
 *  
 *  Description:
 *  
 *  This function prepares user-level resources for CQ modification
 *  (e.g. alternate CQE buffer). It should be called before calling the
 *  kernel level HH_resize_cq() (which should be called with given updated
 *  resources context).
 *  Only one outstanding resize is allowed.
 *  
 ************************************************************************/
#define HHUL_resize_cq_prep(\
    hca_hndl, cq, num_o_cqes, num_o_cqes_p, cq_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_resize_cq_prep(\
    hca_hndl, cq, num_o_cqes, num_o_cqes_p, cq_ul_resources_p)

/************************************************************************
 * Function:   HHUL_resize_cq_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              cq (IN) - The CQ to modify
 *              cq_ul_resources_p (IN) - Pointer to updated allocated
 *                                  resources context for modified CQ
 *                                  (of size cq_ul_resources_sz)
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_CQ_HNDL - Unknown CQ or given CQ is not "resizing" 
 *                                  (i.e. HHUL_resize_cq_prep() was not invoked for it)
 *              HH_EAGAIN - No available resources to complete this operation
 *  
 *  Description:
 *  
 *  This function notifies the user-level that the CQ modify operation has
 *  completed. It should be called after calling the
 *  kernel level HH_resize_cq() (which should be called with given updated
 *  resources context).
 *  
 *  In case of a failure in HH_resize_cq(), HHUL_resize_cq_done()
 *  must be called with cq_ul_resources=NULL in order to cause cleanup of 
 *  any resources allocated for CQ modification on HHUL_resize_cq_prep()
 *  and to assure proper CQ polling.
 *  
 ************************************************************************/
#define HHUL_resize_cq_done(hca_hndl, cq, cq_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_resize_cq_done(hca_hndl, cq, cq_ul_resources_p)


/************************************************************************
 * Function:   HHUL_poll4cqe
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              cq (IN) - The CQ to poll
 *              cqe_p (OUT) - The returned CQE (on HH_OK)
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_CQ_HNDL - Unknown CQ
 *              HH_CQ_EMPTY - No CQE in given CQ
 *  
 *  Description:  Pop CQE in head of CQ.
 *  
 *  
 ************************************************************************/

#define HHUL_poll4cqe(hca_hndl, cq, cqe_p) \
  (hca_hndl)->if_ops->HHULIF_poll4cqe(hca_hndl, cq, cqe_p)

/**********************************************************
 * 
 * Function: HHUL_peek_cq
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq: CQ Handle.
 *  cqe_num: Number of CQE to peek to (next CQE is #1)
 *
 * Returns:
 *  HH_OK: At least cqe_num CQEs outstanding in given CQ
 *  HH_CQ_EMPTY: Less than cqe_num CQEs are outstanding in given CQ
 *  HH_E2BIG_CQ_NUM: cqe_index is beyond CQ size (or 0)
 *  HH_EINVAL_CQ_HNDL: invalid CQ handle
 *  HH_EINVAL_HCA_HNDL: invalid HCA handle
 *
 * Description:
 *  Check if there are at least cqe_num CQEs outstanding in the CQ.
 *  (i.e., peek into the cqe_num CQE in the given CQ). 
 *  No CQE is consumed from the CQ.
 *
 **********************************************************/
#define HHUL_peek_cq(hca_hndl,cq,cqe_num) \
  (hca_hndl)->if_ops->HHULIF_peek_cq(hca_hndl, cq, cqe_num)

/************************************************************************
 * Function:   HHUL_req_comp_notif
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              cq (IN) - The CQ to poll
 *              notif_type (IN) - Notification event type (Next or Solicited)
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_CQ_HNDL - Unknown CQ
 *  
 *  Description: Request completion notification for given CQ.
 *  
 ************************************************************************/

#define HHUL_req_comp_notif(hca_hndl, cq, notif_type) \
  (hca_hndl)->if_ops->HHULIF_req_comp_notif(hca_hndl, cq, notif_type)

/*************************************************************************
 * Function: HHUL_req_ncomp_notif
 *
 * Arguments:
 *  hca_hndl: Handle to HCA.
 *  cq: CQ Handle.
 *  cqe_num: Number of outstanding CQEs which trigger this notification 
 *           (This may be 1 up to CQ size, limited by HCA capability - 0x7FFF for InfiniHost)
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
#define HHUL_req_ncomp_notif(hca_hndl, cq, cqe_num) \
  (hca_hndl)->if_ops->HHULIF_req_ncomp_notif(hca_hndl, cq, cqe_num)


/************************************************************************
 * Function:   HHUL_destroy_cq_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              cq (IN) - The CQ to free user level resources for
 *  
 *  
 *  Arguments:  HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_CQ_HNDL - Unknown CQ
 *  
 *  Description:
 *  
 *  This function frees the user level resources allocated during
 *  HHUL_create_cq_prep(). It must be called after succesfully calling the
 *  kernel level HH_destroy_cq() (or when HH_create_cq() fails).
 *  
 ************************************************************************/

#define HHUL_destroy_cq_done(hca_hndl, cq) \
  (hca_hndl)->if_ops->HHULIF_destroy_cq_done(hca_hndl, cq)


/************************************************************************
 * Function:   HHUL_create_qp_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              qp_init_attr_p  - init attributes for the QP
 *              qp_hndl_p (OUT) - User level QP handle
 *              qp_cap_out_p (OUT) - Actual QP capabilities
 *              qp_ul_resources_p (OUT) -  The user-level resources context
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *              HH_EAGAIN - No available resources to complete this operation
 *  
 *  Description:
 *  
 *  This function allocates user level resources for a new QP. It is
 *  called before calling the kernel level HH_create_qp(). 
 *  The allocated resources context returned in qp_ul_resources_p should
 *  be passed to HH_create_qp() in order to
 *  synchronize the hardware context.
 *  
 *  Freeing of resources allocated here may be done using the function
 *  HHUL_destroy_qp_done().
 *  
 ************************************************************************/

#define HHUL_create_qp_prep(\
    hca_hndl, qp_init_attr_p,  qp_hndl_p, qp_cap_out_p, qp_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_create_qp_prep(\
    hca_hndl, qp_init_attr_p,  qp_hndl_p, qp_cap_out_p, qp_ul_resources_p)


/************************************************************************
 * Function:   HHUL_special_qp_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *  	        special_qp_type (IN) - Type of special QP to prepare for
 *              port - Port number for special QP.
 *              qp_init_attr_p  - init attributes for the QP
 *              qp_hndl_p (OUT) - User level QP handle
 *              qp_cap_out_p (OUT) - Actual QP capabilities
 *              qp_ul_resources_p (OUT) -  The user-level resources context
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *              HH_EAGAIN - No available resources to complete this operation
 *  
 ************************************************************************/

#define HHUL_special_qp_prep(\
    hca_hndl, qp_type, port, qp_init_attr_p, qp_hndl_p, qp_cap_out_p, qp_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_special_qp_prep(\
      hca_hndl, qp_type, port, qp_init_attr_p, qp_hndl_p, qp_cap_out_p, qp_ul_resources_p)


/************************************************************************
 * Function:   HHUL_create_qp_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hhul_qp (IN) - The user-level QP context
 *              hh_qp (IN) - The QP number (or handle for special QP) returned
 *                      by HH on QP creation.
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (for HHUL's)
 *  
 *  Description:
 *  
 *  On succesful call to HH's QP creation function this function should be
 *  called in order to enable the QP. This function performs the binding
 *  of the hardware QP resource allocated to the user-level QP context.
 *  
 *  
 ************************************************************************/

#define HHUL_create_qp_done(hca_hndl, hhul_qp, hh_qp, qp_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_create_qp_done(hca_hndl, hhul_qp, hh_qp, qp_ul_resources_p)

/************************************************************************
 * Function:   HHUL_modify_qp_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hhul_qp (IN) - The user-level QP context
 *              cur_state (IN) - state of QP after modify operation.
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (for HHUL's)
 *  
 *  Description:
 *  
 *  On succesful call to HH's QP modify function this function should be
 *  called in order to synchronize the user copy of the QP state with the
 *  actual QP state after the modify-qp operation.
 *  
 *  
 ************************************************************************/

#define HHUL_modify_qp_done(hca_hndl, hhul_qp, cur_state) \
  (hca_hndl)->if_ops->HHULIF_modify_qp_done(hca_hndl, hhul_qp, cur_state)
  
/************************************************************************
 * Function:   HHUL_post_send_req
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              qp_hndl (IN) - User level QP handle
 *              send_req_p (IN) - Send request
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *              HH_EAGAIN - No available resources to complete this operation
 *              HH_EINVAL_WQE - Invalid send request (send_req_p)
 *              HH_EINVAL_SG_NUM - Scatter/Gather list length error
 *              HH_EINVAL_QP_STATE - Invalid QP state (not RTS)
 *              HH_EINVAL_AV_HNDL - Invalid UD address handle (UD only)
 *              HH_EINVAL_OPCODE - Invalid opcode for given send-q
 *  
 *  Description:
 *  
 *  This function posts a send request to the send queue of the given
 *  QP. QP must be in RTS state.
 *  
 *  Every WQE successfully posted (HH_OK) must create a CQE in the CQ
 *  associated with this send queue (unless QP explicitly moved to RESET
 *  state).
 *  
 *  
 *  
 ************************************************************************/

#define HHUL_post_send_req(hca_hndl, qp_hndl, send_req_p) \
  (hca_hndl)->if_ops->HHULIF_post_send_req(hca_hndl, qp_hndl, send_req_p)
#define HHUL_post_send_reqs(hca_hndl, qp_hndl, num_of_requests, send_req_array) \
  (hca_hndl)->if_ops->HHULIF_post_send_reqs(hca_hndl, qp_hndl, num_of_requests,send_req_array)


/************************************************************************
 * Function:   HHUL_post_inline_send_req
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              qp_hndl (IN) - User level QP handle
 *              send_req_p (IN) - Send request with a signle gather entry
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *              HH_EAGAIN - No available resources to complete this operation
 *              HH_EINVAL_WQE - Invalid send request (send_req_p)
 *              HH_EINVAL_SG_NUM - Scatter/Gather list length error
 *              HH_EINVAL_QP_STATE - Invalid QP state (not RTS)
 *              HH_EINVAL_AV_HNDL - Invalid UD address handle (UD only)
 *              HH_EINVAL_OPCODE - Invalid opcode for given send-q
 *  
 *  Description:
 *  
 *  This function posts an inline send request (data copied into WQE)
 *  The request may be send, send w/immediate, RDMA-wrire , RDMA-write w/immediate.
 *  Gather list may be of one entry only, and data length is defined in QP capabilities
 *  (max_inline_data_sq) - to be defined in create_qp and queries via query_qp.
 *  
 ************************************************************************/
#define HHUL_post_inline_send_req(hca_hndl, qp_hndl, send_req_p) \
  (hca_hndl)->if_ops->HHULIF_post_inline_send_req(hca_hndl, qp_hndl, send_req_p)

/************************************************************************
 * Function:   HHUL_post_gsi_send_req
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              qp_hndl (IN) - User level QP handle
 *              send_req_p (IN) - Send request with a signle gather entry
 *              pkey_index (IN) - Pkey index to put in outgoing
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *              HH_EAGAIN - No available resources to complete this operation
 *              HH_EINVAL_WQE - Invalid send request (send_req_p)
 *              HH_EINVAL_SG_NUM - Scatter/Gather list length error
 *              HH_EINVAL_QP_STATE - Invalid QP state (not RTS)
 *              HH_EINVAL_AV_HNDL - Invalid UD address handle 
 *              HH_EINVAL_OPCODE - Invalid opcode for given send-q
 *  
 *  Description:
 *  
 *  This function posts an inline send request (data copied into WQE)
 *  The request may be send, send w/immediate, RDMA-wrire , RDMA-write w/immediate.
 *  Gather list may be of one entry only, and data length is defined in QP capabilities
 *  (max_inline_data_sq) - to be defined in create_qp and queries via query_qp.
 *  
 ************************************************************************/
#define HHUL_post_gsi_send_req(hca_hndl, qp_hndl, send_req_p, pkey_index) \
  (hca_hndl)->if_ops->HHULIF_post_gsi_send_req(hca_hndl, qp_hndl, send_req_p, pkey_index)


/************************************************************************
 * Function:   HHUL_post_recv_req
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              qp_hndl (IN) - User level QP handle
 *              recv_req_p (IN) - Receive request
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *              HH_EAGAIN - No available resources to complete this operation
 *              HH_EINVAL_WQE - Invalid send request (send_req_p)
 *              HH_EINVAL_SG_NUM - Scatter/Gather list length error
 *              HH_EINVAL_QP_STATE - Invalid QP state (RESET or ERROR)
 *  
 *  Description:
 *  
 *  This function posts a receive request to the receive queue of the
 *  given QP. QP must not be in RESET or ERROR state.
 *  
 *  Every WQE successfully posted (HH_OK) must create a CQE in the CQ
 *  associated with this send queue (unless QP explicitly moved to RESET
 *  state).
 *  
 ************************************************************************/

#define HHUL_post_recv_req(hca_hndl, qp_hndl, recv_req_p) \
  (hca_hndl)->if_ops->HHULIF_post_recv_req(hca_hndl, qp_hndl, recv_req_p)
#define HHUL_post_recv_reqs(hca_hndl, qp_hndl, num_of_requests, recv_req_array) \
  (hca_hndl)->if_ops->HHULIF_post_recv_reqs(hca_hndl, qp_hndl, num_of_requests,recv_req_array)


/************************************************************************
 * Function:   HHUL_destroy_qp_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              qp_hndl (IN) - User level handle of QP to destroy
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *  
 *  Description:
 *  
 *  This function frees the user level resources allocated during
 *  HHUL_create_qp_prep(). It must be called after succesfully calling the
 *  kernel level HH_destroy_qp() (or on failure of HH_create_qp() or
 *  HH_get_special_qp() ).
 *  
 ************************************************************************/

#define HHUL_destroy_qp_done(hca_hndl, qp_hndl) \
  (hca_hndl)->if_ops->HHULIF_destroy_qp_done(hca_hndl, qp_hndl)



/************************************************************************
 * Function:   HHUL_create_srq_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              max_outs (IN) - Reuested max. outstanding WQEs in the SRQ
 *              max_sentries (IN)- Requested max. scatter entries per WQE
 *              srq_hndl_p (OUT) - Returned (HHUL) SRQ handle
 *              max_outs_p (OUT) - Actual limit on number of outstanding WQEs
 *              max_sentries(OUT)- Actual limit on scatter entries per WQE
 *              srq_ul_resources_p(OUT)- SRQ user-level resources context (to pass down)
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, etc.)
 *              HH_E2BIG_WR_NUM - requested max. outstanding WQEs is higher than HCA capability
 *              HH_E@BIG_SG_NUM - requested sentries is higher than HCA capability
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EAGAIN - No available resources to complete this operation
 *  
 *  Description:
 *  This function allocates user level resources for a new SRQ. It is
 *  called before calling the kernel level HH_create_srq(). 
 *  The allocated resources context returned in srq_ul_resources_p should
 *  be passed to HH_create_srq() in order to synchronize the hardware context.
 *  
 *  Freeing of resources allocated here may be done using the function
 *  HHUL_destroy_srq_done().
 *  
 ************************************************************************/
#define HHUL_create_srq_prep(hca_hndl, pd, max_outs, max_sentries,                      \
                             srq_hndl_p, actual_max_outs_p, acutal_max_sentries,        \
                             srq_ul_resources_p)                                        \
  (hca_hndl)->if_ops->HHULIF_create_srq_prep(hca_hndl, pd, max_outs, max_sentries,      \
                             srq_hndl_p, actual_max_outs_p, acutal_max_sentries,        \
                             srq_ul_resources_p)

/************************************************************************
 * Function:   HHUL_create_srq_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hhul_srq (IN) - The user-level SRQ context
 *              hh_srq   (IN) - The SRQ handle returned by HH on SRQ creation.
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_SRQ_HNDL - Unknown SRQ (for HHUL's)
 *  
 *  Description:
 *  On succesful call to HH's SRQ creation function this function should be
 *  called in order to enable the SRQ. This function performs the binding
 *  of the hardware SRQ resource allocated to the user-level QP context.
 *  
 *  
 ************************************************************************/
#define HHUL_create_srq_done(hca_hndl, hhul_srq, hh_srq, srq_ul_resources_p)                \
  (hca_hndl)->if_ops->HHULIF_create_srq_done(hca_hndl, hhul_srq, hh_srq, srq_ul_resources_p)

/************************************************************************
 * Function:   HHUL_destroy_srq_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              srq_hndl (IN) - User level handle of SRQ to destroy
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_SRQ_HNDL - Unknown SRQ
 *  
 *  Description:
 *  This function frees the user level resources allocated during
 *  HHUL_create_srq_prep(). It must be called after succesfully calling the
 *  kernel level HH_destroy_srq() (or on failure of HH_create_srq()).
 *  
 ************************************************************************/
#define HHUL_destroy_srq_done(hca_hndl, hhul_srq)                                      \
  (hca_hndl)->if_ops->HHULIF_destroy_srq_done(hca_hndl, hhul_srq) 

/************************************************************************
 * Function:   HHUL_post_srq
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hhul_srq (IN) - User level SRQ handle
 *              num_of_requests (IN) - Number of requests given in recv_req_array
 *              recv_req_array  (IN) - Array of receive requests
 *              posted_requests_p (OUT) - Actually commited/posted requests (valid also in error).
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_QP_HNDL - Unknown QP (number)
 *              HH_EAGAIN - No available resources to complete this operation
 *              HH_EINVAL_WQE - Invalid send request (send_req_p)
 *              HH_EINVAL_SG_NUM - Scatter/Gather list length error
 *              HH_EINVAL_QP_STATE - Invalid QP state (RESET or ERROR)
 *  
 *  Description:
 *  This function posts receive requests to the given SRQ.
 *  Upon error return code, returned *posted_requests_p identify the number of requests
 *  successfully posted (or the index of the failing WQE). The error code refer to the
 *  request in index *posted_requests_p.
 *  
 ************************************************************************/
#define HHUL_post_srq(hca_hndl, hhul_srq, num_of_requests, recv_req_array, posted_requests_p) \
 (hca_hndl)->if_ops->HHULIF_post_srq(hca_hndl, hhul_srq, num_of_requests, recv_req_array,    \
                                      posted_requests_p)


/************************************************************************
 * Function:   HHUL_alloc_pd_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hhul_qp_p (OUT) - Returned user-level handle for this QP
 *              pd_ul_resources_p (OUT) - Pointer to allocated resources
 *                                  context (of size pd_ul_resources_sz)
 *  	    
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters
 *                           (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown or not
 *                                   in "HH_OPENED" state
 *              HH_EAGAIN - No available resources to complete 
 *  	                this operation
 *  
 *  Description:
 *  
 *  Before creating the PD in a privileged call user level resources must
 *  be allocated. This function deals with the allocation of the required
 *  user level resources based on given parameters.
 *  
 *  The resources context is returned in the pd_ul_resources_p and should
 *  be given to kernel level call. Freeing of these resources is done
 *  using the function HHUL_free_pd_done().
 *  
 ************************************************************************/

#define HHUL_alloc_pd_prep(\
    hca_hndl, hhul_pd_p,  pd_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_alloc_pd_prep(hca_hndl, hhul_pd_p, pd_ul_resources_p)


/************************************************************************
 * Function:   HHUL_alloc_pd_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              hhul_pd (IN) - The user level PD handle
 *              hh_pd (IN) - The PD allocated by HH
 *              pd_ul_resources_p (OUT) - Pointer to allocated resources
 *                                  context (of size pd_ul_resources_sz)
 *  
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_PD_HNDL - Unknown CQ handle
 *  
 *  Description:
 *  
 *  After creation of the PD in the privileged call to HH_alloc_pd()
 *  through VIP's checkings, this function deals with binding of allocated
 *  PD to pre-allocated user-level context.
 *  
 ************************************************************************/

#define HHUL_alloc_pd_done(hca_hndl, hhul_pd, hh_pd, pd_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_alloc_pd_done(hca_hndl, hhul_pd, hh_pd, pd_ul_resources_p)

/************************************************************************
 * Function:   HHUL_free_pd_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              pd (IN) - The PD to free user level resources for
 *              undo_flag (IN) - if TRUE, undo the previous free_pd_prep,
 *                               and restore the PD
 *  
 *  
 *  Arguments:  HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_PD_HNDL - Unknown PD
 *              HH_EBUSY  - when prepping (undo_flag = FALSE), indicates that PD
 *                          still has allocated udav's
 *  
 *  Description:
 *  
 *  This function prepares the user level resources allocated during
 *  HHUL_alloc_pd_prep() for freeing, checking if there are still UDAVs allocated to this PD.
 *  It must be called before calling the kernel level HH_free_pd().
 *  
 ************************************************************************/

#define HHUL_free_pd_prep(hca_hndl, pd, undo_flag) \
  (hca_hndl)->if_ops->HHULIF_free_pd_prep(hca_hndl, pd, undo_flag)


/************************************************************************
 * Function:   HHUL_free_pd_done
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              pd (IN) - The PD to free user level resources for
 *  
 *  
 *  Arguments:  HH_OK,
 *              HH_EINVAL - Invalid parameters (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown not in "HH_OPENED" state
 *              HH_EINVAL_PD_HNDL - Unknown PD
 *  
 *  Description:
 *  
 *  This function frees the user level resources allocated during
 *  HHUL_alloc_pd_prep(). It must be called after succesfully calling the
 *  kernel level HH_free_pd() (or when HH_alloc_pd() fails).
 *  
 ************************************************************************/

#define HHUL_free_pd_done(hca_hndl, pd) \
  (hca_hndl)->if_ops->HHULIF_free_pd_done(hca_hndl, pd)


/************************************************************************
 * Set the if_ops tbl with dummy functions returning HH_ENOSYS.
 * This is convenient for initializing tables prior
 * setting them with partial real implementation.
 *
 * This way, the general HH_if_ops_t table structure can be extended,
 * requiring just recompilation.
 ************************************************************************/
extern void HHUL_ifops_tbl_set_enosys(HHUL_if_ops_t* tbl);


/************************************************************************
 * Function:   HHUL_alloc_pd_avs_prep
 *  
 *  
 *  Arguments:  hca_hndl (IN) - User level handle of the HH device context
 *              max_num_avs - desired max number of AVs to be available for this PD
 *              pd_flags - currently, if is a PD for a SQP or not.
 *              hhul_qp_p (OUT) - Returned user-level handle for this QP
 *              pd_ul_resources_p (OUT) - Pointer to allocated resources
 *                                  context (of size pd_ul_resources_sz)
 *  	    
 *  Returns:    HH_OK,
 *              HH_EINVAL - Invalid parameters
 *                           (NULL pointer, invalid L-Key, etc.)
 *              HH_EINVAL_HCA_HNDL - Given HCA is unknown or not
 *                                   in "HH_OPENED" state
 *              HH_EAGAIN - No available resources to complete 
 *  	                this operation
 *  
 *  Description:
 *  
 *  Before creating the PD in a privileged call user level resources must
 *  be allocated. This function deals with the allocation of the required
 *  user level resources based on given parameters.
 *
 *  If the caller desires to use the default maximum number of AVs for this PD,
 *  max_num_avs should be set to EVAPI_DEFAULT_AVS_PER_PD.
 *  
 *  The resources context is returned in the pd_ul_resources_p and should
 *  be given to kernel level call. Freeing of these resources is done
 *  using the function HHUL_free_pd_done().
 *  
 ************************************************************************/

#define HHUL_alloc_pd_avs_prep(\
    hca_hndl,max_num_avs, pd_flags, hhul_pd_p,  pd_ul_resources_p) \
  (hca_hndl)->if_ops->HHULIF_alloc_pd_avs_prep(hca_hndl, max_num_avs, pd_flags,hhul_pd_p, pd_ul_resources_p)


/************************************************************************
 *  Some common struct's to be used by lower level drivers
 */


/* Send Request Descriptor */
struct  HHUL_sr_wqe_st {
  struct HHUL_sr_wqe_st   *next;   /* for WQEs list */
  struct HHUL_sr_wqe_st   *ul_wqe_p;   /* For passing from kernel to user */

  VAPI_wr_id_t                  id;

  VAPI_wr_opcode_t              opcode;
  VAPI_comp_type_t              comp_type;

  VAPI_imm_data_t               imm_data;
  MT_bool                          fence;
  MT_bool                          av_valid; /* TRUE is following AV is valid */
  VAPI_ud_av_t                  av;
  VAPI_qp_num_t                 remote_qp;
  VAPI_qkey_t                   remote_qkey;
  VAPI_ethertype_t              ethertype;

  MT_bool                          set_se;
  VAPI_virt_addr_t              remote_addr;


  VAPI_rkey_t                   r_key;
  u_int64_t                     operand1;    /* for atomic */
  u_int64_t                     operand2;    /* for atomic */

  u_int32_t                     sg_lst_len;

  /* TBD - Add AckReg - Data Seg - If Gent - */

  /* TBD - Add support for reliable datagram (RD) */
  u_int32_t                     sg_total_byte_len;

  VAPI_sg_lst_entry_t           sg_lst_p[1];

  /* here comes the data ...
   * if (sg_lst_len > 0)
   * MALLOC(sizeof(HH_sr_wqe_t)+[sg_lst_len-1]*sizeof(VAPI_sg_lst_entry_t))
   */

}; /* HHUL_sr_wqe_t */;


/* Receive Request WQE */       /* TBD - move to HH */
struct HHUL_rr_wqe_st {
  struct HHUL_rr_wqe_st*  next;           /* Next wqe in receive queue */
  struct HHUL_rr_wqe_st*  prev;           /* Previous wqe in receive queue */

  VAPI_wr_id_t          id;          /* ID provided by the user */

  VAPI_comp_type_t      comp_type;   /* Mellanox Specific
                                      * {VAPI_SIGNALED, VAPI_UNSIGNALED} */
  u_int32_t             total_len;   /* Current s/g list length -
                                      * need to calculate it     */
  u_int32_t             sg_lst_len;
  VAPI_sg_lst_entry_t   sg_lst_p[1]; /* TBD is it ok to define this way */

  /* here comes the data ...
   * if (sg_lst_len > 0)
   * MALLOC(sizeof(HHUL_rr_wqe_t)+[sg_lst_len-1]*sizeof(VAPI_sg_lst_entry_t))
   */
} /* HHUL_rr_wqe_t */;

#endif
