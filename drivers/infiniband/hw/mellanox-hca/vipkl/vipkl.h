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

#ifndef H_VIP_KL_H
#define H_VIP_KL_H

#include <vip.h>
#include <qpm.h>
#include <mmu.h>
#include <vipkl_eq.h>
#include <vipkl_cqblk.h>
#include <hh.h>




/*************************************************************************
 * Function: VIPKL_init_layer
 *
 * Arguments:
 *
 * Returns:
 *    VIP_OK - success
 *
 * Description:
 *   performs initialization required prior to using layer's functions
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_init_layer(void);

/*************************************************************************
 * Function: VIPKL_open_hca
 *
 * Arguments:
 *  hca_id: The system specific HCA ID (device name)
 *  profile_p: pointer to user's profile.  may be NULL
 *  sugg_profile_p: pointer to profile to return to caller.  may be NULL
 *  hca_hndl_p: VIP's handle to map into HOBKL object
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_ID: Unknown device or device is not HCA
 *  VIP_ENOSYS: Given device is not supported
 *  VIP_EBUSY: This device already open
 *  VIP_EAGAIN  
 *  
 *
 * Description:
 *  
 *  This function creates an HOBKL object and allocates a handle to map to it from user 
 *  space. An HCA-driver appropriate to the given device should be initilized succesfully 
 *  before creating HOBKL, the HOBKL is assoicated with specific HCA driver (when 
 *  more than one driver is supported, HH function pointers record is associated) and device 
 *  handle. Then HOBKL may be initialized with HCA capabilities for future referece 
 *  (assuming HCA cap. are static).
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_open_hca(/*IN*/ VAPI_hca_id_t hca_id,
                         /*IN*/ EVAPI_hca_profile_t  *profile_p,
                         /*OUT*/ EVAPI_hca_profile_t  *sugg_profile_p,
                         /*OUT*/ VIP_hca_hndl_t *hca_hndl_p);

/*************************************************************************
 * Function: VIPKL_close_hca
 *
 * Arguments:
 *  hca_hndl: HCA to close.
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EBUSY: HCA is still used by processes (?)
 *  
 *  
 *
 * Description:
 *  
 *  This function destroy the HOBKL object associated with given handle and close the 
 *  device in the associated HCA driver.
 *  This function prevent closing HCA which is still in use (see process resource tracking - 
 *  PRT). Only VIP module cleanup may force destroying of HOBKL which are still in use 
 *  (resource  tracking - PRT - enables freeing of all resources, though still in use).
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_close_hca(/*IN*/ VIP_hca_hndl_t hca_hndl);


/*************************************************************************
 * Function: VIPKL_get_hca_hndl
 *
 * Arguments:
 *  hca_id: The system specific HCA ID (device name)
 *  hca_hndl_p: VIP's handle to map into HOBKL object
 *  hh_hndl_p: HH handle to map into HOBKL object
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_ID: Given device is not opened.
 *  
 *  
 *
 * Description:
 *  
 *  When VIP in user space performs HCA_open_hca_ul it does not open an HCA but only 
 *  initilizes user space resources for working with given HCA. This function enables it to 
 *  get the HCA handle associated with given HCA ID.
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hca_hndl(/*IN*/ VAPI_hca_id_t hca_id,
    /*OUT*/ VIP_hca_hndl_t *hca_hndl_p,
    /*OUT*/ HH_hca_hndl_t *hh_hndl_p
    );

/*****************************************************************************
 * Function: VIPKL_list_hcas
 *
 * Arguments:
 *            hca_id_buf_sz(IN)    : Number of entries in supplied array 'hca_id_buf_p',
 *            num_of_hcas_p(OUT)   : Actual number of currently available HCAs
 *            hca_id_buf_p(OUT)    : points to an array allocated by the caller of 
 *                                   'VAPI_hca_id_t' items, able to hold 'hca_id_buf_sz' 
 *                                    entries of that item.

 *
 * Returns:   VIP_OK     : operation successful.
 *            VIP_EINVAL_PARAM : Invalid params.
 *            VIP_EAGAIN : hca_id_buf_sz is smaller than num_of_hcas.  In this case, NO hca_ids
 *                         are returned in the provided array.
 *
 * Description:
 *   Used to get a list of the device IDs of the available devices.
 *   These names can then be used in VAPI_open_hca to open each
 *   device in turn.
 *
 *   If the size of the supplied buffer is too small, the number of available devices
 *   is still returned in the num_of_hcas parameter, but the return code is set to
 *   VIP_EAGAIN.  In this case, the user's buffer is filled to capacity with 
 *   'hca_id_buf_sz' device IDs; to get more of the available device IDs, the the user may
 *   allocate a larger array and call this procedure again. (The user may call this function
 *   with hca_id_buf_sz = 0 and hca_id_buf_p = NULL to get the number of hcas currently
 *   available).
 *****************************************************************************/

VIP_ret_t VIPKL_list_hcas(/* IN*/ u_int32_t          hca_id_buf_sz,
                            /*OUT*/ u_int32_t*       num_of_hcas_p,
                            /*OUT*/ VAPI_hca_id_t*   hca_id_buf_p);

/*************************************************************************
 * Function: VIPKL_cleanup
 *
 * Arguments:
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EBUSY: HCAs still used by processes (?)
 *  
 *  
 *
 * Description:
 *  
 *  This function performs module cleanup
 *  By destroying all HOBKL structures
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_cleanup(void);


/*************************************************************************
 * The following functions are the wrapper functions of VIP-KL.
 * These functions may be called directly in kernel space for kernel-modules which
 * use VIP-KL (i.e. their VIP-UL is running in kernel space),
 * or called via a system call wrapper from user space.
 *
 * All these functions are actually "shadows"
 * of functions of HOBKL and its contained objects (PDM,CQM,QPM,MMU,EM) but 
 * instead of the object handle a VIP_hca_hndl_t is provided.
 * These functions map from the HCA handle to the appropriate object and call
 * its function.
 * (so the documentation for these functions point to the called function).
 *************************************************************************/
 



/*************************************************************************
                        
                        HOBKL functions
 
 *************************************************************************/ 
/*************************************************************************
 * Function: VIPKL_alloc_ul_resources <==> HOBKL_alloc_ul_resources
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_alloc_ul_resources(VIP_hca_hndl_t hca_hndl, 
                                   MOSAL_protection_ctx_t usr_prot_ctx,
                                   MT_size_t hca_ul_resources_sz,
                                   void *hca_ul_resources_p,
                                   EM_async_ctx_hndl_t *async_hndl_ctx_p);

/*************************************************************************
 * Function: VIPKL_free_ul_resources <==> HOBKL_free_ul_resources
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_free_ul_resources(VIP_hca_hndl_t hca_hndl,
								  MT_size_t hca_ul_resources_sz,
								  void *hca_ul_resources_p,
                                  EM_async_ctx_hndl_t async_hndl_ctx);

/*************************************************************************
 * Function: VIPKL_query_hca_cap <==> HOBKL_query_hca_cap
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_hca_cap(/*IN*/ VIP_hca_hndl_t hca_hndl, /*OUT*/ VAPI_hca_cap_t *caps);

/*************************************************************************
 * Function: VIPKL_query_port_prop  <==> HOBKL_query_port_prop
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_port_prop(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  VAPI_hca_port_t *port_props_p);

/*************************************************************************
 * Function: VIPKL_query_port_gid_tbl  <==> HOBKL_query_port_gid_tbl
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_port_gid_tbl(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  u_int16_t tbl_len_in, u_int16_t *tbl_len_out_p,
  VAPI_gid_t *gid_tbl_p);

/*************************************************************************
 * Function: VIPKL_query_port_pkey_tbl  <==> HOBKL_query_port_pkey_tbl
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_port_pkey_tbl(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  u_int16_t tbl_len_in, u_int16_t *tbl_len_out_p,
  VAPI_pkey_t *pkey_tbl_p);

/*************************************************************************
 * Function: VIP_modify_hca_attr <==> HOBKL_modify_hca_attr
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_modify_hca_attr(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  VAPI_hca_attr_t *hca_attr_p, VAPI_hca_attr_mask_t *hca_attr_mask_p);


/*************************************************************************
 * Function: VIPKL_get_hh_hndl <==> HOBKL_get_hh_hndl
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hh_hndl(VIP_hca_hndl_t hca_hndl, HH_hca_hndl_t *hh_p);

/*************************************************************************
 * Function: VIPKL_get_hca_ul_info <==> HOBKL_get_hca_ul_info
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hca_ul_info(VIP_hca_hndl_t hca_hndl, HH_hca_dev_t *hca_ul_info_p);

/*************************************************************************
 * Function: VIPKL_get_hca_id <==> HOBKL_get_hca_id
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hca_id(VIP_hca_hndl_t hca_hndl, VAPI_hca_id_t *hca_id_p);


/*************************************************************************
                        PDM functions
 *************************************************************************/ 

/*************************************************************************
 * Function: VIP_create_pd <==> PDM_create_pd
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_create_pd(VIP_RSCT_t usr_ctx, VIP_hca_hndl_t hca_hndl, MOSAL_protection_ctx_t prot_ctx, 
						MT_size_t pd_ul_resources_sz,
                        void * pd_ul_resources_p, PDM_pd_hndl_t *pd_hndl_p, HH_pd_hndl_t *pd_num_p);


/*************************************************************************
 * Function: VIP_destroy_pd <==> PDM_destroy_pd
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_destroy_pd(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, PDM_pd_hndl_t pd_hndl);




/*************************************************************************
                        
                        QPM functions
 
 *************************************************************************/ 

/******************************************************************************
 *  Function: VIPKL_create_qp <==> QPM_create_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_create_qp (VIP_RSCT_t usr_ctx,
                 VIP_hca_hndl_t hca_hndl,
                 VAPI_qp_hndl_t vapi_qp_hndl,
                 EM_async_ctx_hndl_t async_hndl_ctx,
				 MT_size_t qp_ul_resources_sz,
                 void  *qp_ul_resources_p,
                 QPM_qp_init_attr_t *qp_init_attr_p,
                 QPM_qp_hndl_t *qp_hndl_p, 
                 VAPI_qp_num_t *qp_num_p);

/******************************************************************************
 *  Function: VIPKL_get_special_qp <==> QPM_get_special_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_get_special_qp (VIP_RSCT_t            usr_ctx,
                      VIP_hca_hndl_t        hca_hndl,
                      VAPI_qp_hndl_t        vapi_qp_hndl, /* VAPI handle to the QP */
                      EM_async_ctx_hndl_t   async_hndl_ctx,
          			  MT_size_t             qp_ul_resources_sz,
                      void                  *qp_ul_resources_p, 
                      IB_port_t             port,
                      VAPI_special_qp_t     qp_type,
                      QPM_qp_init_attr_t    *qp_init_attr_p, 
                      QPM_qp_hndl_t         *qp_hndl_p,
                      IB_wqpn_t             *sqp_hndl);

/******************************************************************************
 *  Function: VIPKL_destroy_qp <==> QPM_destroy_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_destroy_qp (VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, QPM_qp_hndl_t qp_hndl,MT_bool in_rsct_cleanup);

/******************************************************************************
 *  Function: VIPKL_modify_qp <==> QPM_modify_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_modify_qp (VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, QPM_qp_hndl_t qp_hndl, VAPI_qp_attr_mask_t *qp_mod_mask_p,
                VAPI_qp_attr_t *qp_mod_attr_p);

/******************************************************************************
 *  Function:  VIPKL_query_qp <==> QPM_query_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_query_qp (VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, QPM_qp_hndl_t qp_hndl, QPM_qp_query_attr_t *qp_query_prop_p,
              VAPI_qp_attr_mask_t *qp_mod_mask_p);



/*************************************************************************
                        
                        SRQM functions
 
 *************************************************************************/ 

/******************************************************************************
 *  Function: VIPKL_create_srq <==> SRQM_create_srq
 *****************************************************************************/
VIP_ret_t
VIPKL_create_srq (VIP_RSCT_t usr_ctx,
                 VIP_hca_hndl_t hca_hndl,
                  VAPI_srq_hndl_t   vapi_srq_hndl, 
                  PDM_pd_hndl_t     pd_hndl,
                  EM_async_ctx_hndl_t async_hndl_ctx,
                  MT_size_t           srq_ul_resources_sz,
                  void                *srq_ul_resources_p,
                  SRQM_srq_hndl_t     *srq_hndl_p );

/******************************************************************************
 *  Function: VIPKL_destroy_srq <==> SRQM_destroy_srq
 *****************************************************************************/
VIP_ret_t
VIPKL_destroy_srq(VIP_RSCT_t usr_ctx,
                  VIP_hca_hndl_t hca_hndl,
                  SRQM_srq_hndl_t srq_hndl);

/******************************************************************************
 *  Function: VIPKL_query_srq <==> SRQM_query_srq
 *****************************************************************************/
VIP_ret_t
VIPKL_query_srq(VIP_RSCT_t usr_ctx,
                VIP_hca_hndl_t hca_hndl,
                SRQM_srq_hndl_t srq_hndl,
                u_int32_t *limit_p);


/*************************************************************************
                        
                        CQM functions
 
 *************************************************************************/ 

/*************************************************************************
 * Function: VIPKL_query_cq_memory_size <==> CQM_query_memory_size
 *************************************************************************/ 
VIP_ret_t VIPKL_query_cq_memory_size(VIP_RSCT_t usr_ctx,/*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ MT_size_t num_o_entries,
    /*OUT*/ MT_size_t* cq_bytes
    );

/*************************************************************************
 * Function: VIPKL_create_cq <==> CQM_create_cq
 *************************************************************************/ 
VIP_ret_t VIPKL_create_cq(
    /*IN*/ VIP_RSCT_t usr_ctx,
    /*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ VAPI_cq_hndl_t vapi_cq_hndl,
    /*IN*/ MOSAL_protection_ctx_t usr_prot_ctx,
    /*IN*/ EM_async_ctx_hndl_t async_hndl_ctx,
    /*IN*/ MT_size_t cq_ul_resources_sz,
    /*IN*/ void  *cq_ul_resources_p,
    /*OUT*/ CQM_cq_hndl_t *cq_p,
    /*OUT*/ HH_cq_hndl_t* cq_id_p
    );


/*************************************************************************
 * Function: VIPKL_destroy_cq <==> CQM_destroy_cq
 *************************************************************************/ 
VIP_ret_t VIPKL_destroy_cq(VIP_RSCT_t usr_ctx,/*IN*/ VIP_hca_hndl_t hca_hndl,/*IN*/ CQM_cq_hndl_t cq,
                           /*IN*/MT_bool in_rsct_cleanup);

/*************************************************************************
 * Function: VIPKL_get_cq_props <==> CQM_get_cq_props
 *************************************************************************/ 
VIP_ret_t VIPKL_get_cq_props(VIP_RSCT_t usr_ctx,/*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ CQM_cq_hndl_t cq,
   /*OUT*/ HH_cq_hndl_t* cq_id_p,
    /*OUT*/ VAPI_cqe_num_t *num_o_entries_p
    );


/*************************************************************************
 * Function: VIPKL_resize_cq <==> CQM_resize_cq
 *************************************************************************/ 
VIP_ret_t VIPKL_resize_cq(VIP_RSCT_t usr_ctx,/*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ CQM_cq_hndl_t cq,
    /*IN*/ MT_size_t cq_ul_resources_sz,
    /*IN/OUT*/ void *cq_ul_resources_p
    );



/*************************************************************************
                        
                        MMU functions
 
 *************************************************************************/ 

/*******************************************************************
 * FUNCTION: VIPKL_create_mr <==> MM_create_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_create_mr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,VAPI_mrw_t* mr_req_p,PDM_pd_hndl_t pd_hndl,
                          MM_mrw_hndl_t *mrw_hndl_p,MM_VAPI_mro_t* mr_props_p);

/*******************************************************************
 * FUNCTION: VIPKL_create_smr <==> MM_create_smr
 *******************************************************************/ 
VIP_ret_t VIPKL_create_smr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,MM_mrw_hndl_t orig_mrw_hndl, VAPI_mrw_t* mr_req_p,
                          PDM_pd_hndl_t pd_hndl,MM_mrw_hndl_t *mrw_hndl_p,MM_VAPI_mro_t* mr_props_p);

/*******************************************************************
 * FUNCTION: VIPKL_reregister_mr <==> MM_reregister_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_reregister_mr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,VAPI_mr_hndl_t mr_hndl,VAPI_mr_change_t change_type,
                              VAPI_mrw_t* req_mrw_p,PDM_pd_hndl_t pd_hndl,MM_mrw_hndl_t *rep_mr_hndl_p,
                              MM_VAPI_mro_t* rep_mrw_p);

/*******************************************************************
 * FUNCTION: VIPKL_alloc_fmr <==> MM_alloc_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_alloc_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,EVAPI_fmr_t* fmr_p,PDM_pd_hndl_t pd_hndl,MM_mrw_hndl_t *mrw_hndl_p);

/*******************************************************************
 * FUNCTION: VIPKL_map_fmr <==> MM_map_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_map_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,EVAPI_fmr_hndl_t fmr_hndl,EVAPI_fmr_map_t* map_p,
                        VAPI_lkey_t *lkey_p,VAPI_rkey_t *rkey_p);

/*******************************************************************
 * FUNCTION: VIPKL_unmap_fmr <==> MM_unmap_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_unmap_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,MT_size_t size,VAPI_mr_hndl_t* mr_hndl_arr);

/*******************************************************************
 * FUNCTION: VIPKL_free_fmr <==> MM_free_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_free_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,MM_mrw_hndl_t mrw_hndl);






/*******************************************************************
 * FUNCTION:  VIPKL_create_mw <==>  MM_create_mw
 *******************************************************************/ 
VIP_ret_t VIPKL_create_mw(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, PDM_pd_hndl_t pd_hndl,
                          MM_key_t* r_key_p);

/*******************************************************************
 * FUNCTION:  VIPKL_destroy_mr <==>  MM_destroy_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_destroy_mr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, MM_mrw_hndl_t mrw_hndl);

/*******************************************************************
 * FUNCTION:  VIPKL_destroy_mw <==>        MM_destroy_mw
 *******************************************************************/ 
VIP_ret_t VIPKL_destroy_mw(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, MM_mrw_hndl_t mrw_hndl);

/*******************************************************************
 * FUNCTION:  VIPKL_query_mr <==>        MM_query_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_query_mr (VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, MM_mrw_hndl_t mrw_hndl,MM_VAPI_mro_t *mr_prop_p);

/*******************************************************************
 * FUNCTION:  VIPKL_query_mw <==>        MM_query_mw
 *******************************************************************/ 
VIP_ret_t VIPKL_query_mw(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, IB_rkey_t initial_rkey,IB_rkey_t *current_key);

/*******************************************************************
 * FUNCTION:   VIPKL_bind_async_error_handler <==> EM_bind_async_error_handler
 *******************************************************************/ 
VIP_ret_t VIPKL_bind_async_error_handler(/*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ VAPI_async_event_handler_t handler, /*IN*/ void* private_data);

/*************************************************************************
 * FUNCTION: VIPKL_bind_completion_event_handler <==> EM_bind_completion_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_bind_completion_event_handler(/*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ VAPI_completion_event_handler_t handler, /*IN*/ void* private_data);

/*************************************************************************
 * FUNCTION: VIPKL_bind_evapi_completion_event_handler <==> EM_bind_evapi_completion_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_bind_evapi_completion_event_handler(/*IN*/ VIP_hca_hndl_t hca_hndl,
    /* IN */CQM_cq_hndl_t cq_hndl, /*IN*/ VAPI_completion_event_handler_t handler, 
                                                           /*IN*/ void* private_data);

/*************************************************************************
 * FUNCTION: VIPKL_set_async_event_handler <==> EM_set_async_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_set_async_event_handler(
                                  /*IN*/  VIP_hca_hndl_t                  hca_hndl,
                                  /*IN*/  EM_async_ctx_hndl_t             hndl_ctx,
                                  /*IN*/  VAPI_async_event_handler_t      handler,
                                  /*IN*/  void*                           private_data,
                                  /*OUT*/ EVAPI_async_handler_hndl_t     *async_handler_hndl);

/*************************************************************************
 * FUNCTION: VIPKL_clear_async_event_handler <==> EM_clear_async_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_clear_async_event_handler(
                               /*IN*/ VIP_hca_hndl_t              hca_hndl, 
                               /*IN*/ EM_async_ctx_hndl_t         hndl_ctx,
                               /*IN*/ EVAPI_async_handler_hndl_t  async_handler_hndl);


VAPI_ret_t VIPKL_set_destroy_cq_cbk(
  /*IN*/ VIP_hca_hndl_t           hca_hndl,
  /*IN*/ CQM_cq_hndl_t            cq_hndl,
  /*IN*/ EVAPI_destroy_cq_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
);
 
VAPI_ret_t VIPKL_clear_destroy_cq_cbk(
  /*IN*/ VIP_hca_hndl_t           hca_hndl,
  /*IN*/ CQM_cq_hndl_t            cq_hndl
);

VAPI_ret_t VIPKL_set_destroy_qp_cbk(
  /*IN*/ VIP_hca_hndl_t           hca_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl,
  /*IN*/ EVAPI_destroy_qp_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
);
 
VAPI_ret_t VIPKL_clear_destroy_qp_cbk(
  /*IN*/ VIP_hca_hndl_t           hca_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl
);

/*******************************************************************
 * FUNCTION: VIPKL_detach_from_multicast <==> 
 *******************************************************************/ 
VIP_ret_t VIPKL_detach_from_multicast(VIP_RSCT_t usr_ctx,/* IN */     VAPI_hca_hndl_t     hca_hndl,
                                      /* IN */     IB_gid_t            mcg_dgid,
                                      /* IN */     VAPI_qp_hndl_t      qp_hndl);


/*******************************************************************
 * FUNCTION: VIPKL_attach_to_multicast <==> 
 *******************************************************************/ 
VIP_ret_t VIPKL_attach_to_multicast(VIP_RSCT_t usr_ctx,/* IN */     VAPI_hca_hndl_t     hca_hndl,
                                    /* IN */     IB_gid_t            mcg_dgid,
                                    /* IN */     VAPI_qp_hndl_t      qp_hndl);


/*************************************************************************
 * Function: VIPKL_process_local_mad  <==> HOBKL_process_local_mad
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_process_local_mad(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,IB_lid_t slid,
  EVAPI_proc_mad_opt_t proc_mad_opts, void * mad_in_p, void *mad_out_p);

VIP_ret_t VIPKL_alloc_map_devmem(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, EVAPI_devmem_type_t mem_type,
                             VAPI_size_t  bsize,u_int8_t align_shift,VAPI_phy_addr_t* buf_p,
                             void** virt_addr_p,DEVMM_dm_hndl_t* dm_hndl_p);
VIP_ret_t VIPKL_query_devmem(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, EVAPI_devmem_type_t mem_type,
                             u_int8_t align_shift, EVAPI_devmem_info_t*  devmem_info_p);
VIP_ret_t VIPKL_free_unmap_devmem(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,DEVMM_dm_hndl_t dm_hndl);


#if defined(MT_SUSPEND_QP)
/******************************************************************************
 *  Function: VIPKL_suspend_qp <==> QPM_suspend_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_suspend_qp (VIP_RSCT_t      usr_ctx,
                  VIP_hca_hndl_t  hca_hndl, 
                  QPM_qp_hndl_t   qp_hndl,
                  MT_bool         suspend_flag);

VIP_ret_t
VIPKL_suspend_cq (VIP_RSCT_t      usr_ctx,
                  VIP_hca_hndl_t  hca_hndl, 
                  CQM_cq_hndl_t   cq_hndl,
                  MT_bool         do_suspend);
#endif

#if defined(__KERNEL__)
VIP_ret_t VIPKL_get_debug_info(
	VIP_hca_hndl_t hca_hndl, 		    /*IN*/
	VIPKL_debug_info_t *debug_info_p	/*OUT*/
);



/*************************************************************************
 * Function: VIPKL_get_rsrc_info
 *
 * Arguments:
 *  hca_hndl (IN) - hca device
 *  rsrc_info_p (OUT) - where to return resources list
 *
 * Returns: 
 *
 * Description: This function allocates resoucres that must be released
 *              when no longer needed by calling VIPKL_rel_qp_rsrc_info
 *************************************************************************/ 
VIP_ret_t VIPKL_get_rsrc_info(VIP_hca_hndl_t hca_hndl, VIPKL_rsrc_info_t *rsrc_info_p);

/*************************************************************************
 * Function: VIPKL_get_qp_rsrc_str
 *
 * Arguments:
 *  qpd (IN) - contains fields of lists of data to be parsed
 *  str (OUT) - string where to put parsed data of one entry
 *
 * Returns: number of bytes written to string
 *
 * Description: 
 *************************************************************************/ 
int VIPKL_get_qp_rsrc_str(struct qp_data_st *qpd, char *str);



/*************************************************************************
 * Function: VIPKL_rel_qp_rsrc_info
 *
 * Arguments:
 *  qpd (IN) - list of resources to be frred
 *
 *************************************************************************/ 
void VIPKL_rel_qp_rsrc_info(struct qp_data_st *qpd);


#endif


#ifndef MT_KERNEL
VIP_ret_t FS_register_thread(void);
VIP_ret_t FS_deregister_thread(void);
#endif


#endif
