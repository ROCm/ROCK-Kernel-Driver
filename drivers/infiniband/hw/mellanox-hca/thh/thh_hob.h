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

#ifndef H_THH_HOB_H
#define H_THH_HOB_H

#include <vapi.h>
#include <vip_delay_unlock.h>
#include <mosal.h>
#include <hh.h>
#include <thh.h>

#define THH_RESERVED_PD        0

/* the memory key used for the UDAVM in THH (privileged mode) */
#define THH_UDAVM_PRIV_RESERVED_LKEY      0

#define THH_INVALID_HNDL       ((MT_ulong_ptr_t) (-1L))

typedef struct THH_intr_props_st {
    MOSAL_IRQ_ID_t    irq;
    u_int8_t    intr_pin;
} THH_intr_props_t;

typedef struct THH_hw_props_st {
    MOSAL_pci_dev_t   pci_dev;
    u_int8_t          bus;
    u_int8_t          dev_func;
    u_int16_t         device_id;
    u_int16_t         pci_vendor_id;
    u_int32_t         hw_ver;
    MT_phys_addr_t       cr_base;
    MT_phys_addr_t       uar_base;
    MT_phys_addr_t       ddr_base;
    THH_intr_props_t  interrupt_props;
} THH_hw_props_t;


/* Prototypes */

/******************************************************************************
 *  Function:     THH_hob_create 
 *
 *  Arguments:
 *        hw_props_p - pointer to HW properties.
 *        hca_seq_num  - a sequence number assigned to this HCA to differentiate it 
 *                        from other HCAs on this host
 *        mod_flags - ptr to struct containing flags passed during module initialization
 *                    (e.g. insmod)
 *        hh_hndl_p - Returned HH context handle.  HOB object is accessed via device field 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameter
 *
 *  Description:
 *    Allocate THH_hob object and register the device in HH.
 */
HH_ret_t    THH_hob_create(/*IN*/  THH_hw_props_t    *hw_props_p,
                           /*IN*/  u_int32_t         hca_seq_num,
                           /*IN*/  THH_module_flags_t *mod_flags,
                           /*OUT*/ HH_hca_hndl_t     *hh_hndl_p );

/******************************************************************************
 *  Function:     THH_hob_destroy 
 *
 *  Arguments:
 *        hca_hndl - The HCA handle allocated on device registration 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL_HCA_HNDL - The HCA either is not opened or is unknown
 *
 *  Description:
 *    Deregister given device from HH and free all associated resources.
 *    If the HCA is still open, perform THH_hob_close_hca() before freeing the THH_hob.
 */
HH_ret_t    THH_hob_destroy(HH_hca_hndl_t hca_hndl);

/******************************************************************************
 *  Function:     THH_hob_get_ver_info 
 *
 *  Arguments:
 *        hob
 *        version_p - Returned version information 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get version information of device associated with given hob.
 */
HH_ret_t THH_hob_get_ver_info ( /*IN*/ THH_hob_t        hob, 
                                /*OUT*/ THH_ver_info_t  *version_p );

/******************************************************************************
 *  Function:     THH_hob_get_cmd_if 
 *
 *  Arguments:
 *        hob
 *        cmd_if_p - Included command interface object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to associated command interface object.
 */
HH_ret_t THH_hob_get_cmd_if ( /*IN*/   THH_hob_t   hob, 
                              /*OUT*/ THH_cmd_t    *cmd_if_p );

/******************************************************************************
 *  Function:     THH_hob_get_uldm 
 *
 *  Arguments:
 *        hob
 *        uldm_p - Included THH_uldm object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to associated user-level domain management object.
 */
HH_ret_t THH_hob_get_uldm ( /*IN*/ THH_hob_t hob, 
                            /*OUT*/ THH_uldm_t *uldm_p );

/******************************************************************************
 *  Function:     THH_hob_get_mrwm 
 *
 *  Arguments:
 *        hob
 *        mrwm_p - Included THH_mrwm object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to associated memory regions/windows management object.
 */
HH_ret_t THH_hob_get_mrwm ( /*IN*/ THH_hob_t hob, 
                            /*OUT*/ THH_mrwm_t *mrwm_p );

/******************************************************************************
 *  Function:     THH_hob_get_udavm_info 
 *
 *  Arguments:
 *        hob
 *        udavm_p - Included THH_udavm object
 *        use_priv_udav - flag:  TRUE if using privileged UDAV mode
 *        av_on_board   - flag:  TRUE if should use DDR SDRAM on Tavor for UDAVs
 *        lkey          - lkey allocated for udav table in privileged UDAV mode 
 *        hide_ddr      - flag:  TRUE if should use host memory for UDAVs
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    return udavm information (needed by uldm object in PD allocation).
 */
HH_ret_t THH_hob_get_udavm_info ( /*IN*/ THH_hob_t hob, 
                                  /*OUT*/ THH_udavm_t *udavm_p,
                                  /*OUT*/ MT_bool *use_priv_udav,
                                  /*OUT*/ MT_bool *av_on_board,
                                  /*OUT*/ VAPI_lkey_t  *lkey,
                                  /*OUT*/ u_int32_t *max_ah_num,
                                  /*OUT*/ MT_bool *hide_ddr);

/******************************************************************************
 *  Function:     THH_hob_get_hca_hndl 
 *
 *  Arguments:
 *        hob
 *        hca_hndl_p - Included THH_mrwm object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to the HH hca object.
 */
HH_ret_t THH_hob_get_hca_hndl ( /*IN*/  THH_hob_t hob, 
                                /*OUT*/ HH_hca_hndl_t *hca_hndl_p );
/******************************************************************************
 *  Function:     THH_hob_get_ddrmm 
 *
 *  Arguments:
 *        hob
 *        uldm_p - Included THH_ddrmm object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to DDR memory management object.
 */
HH_ret_t THH_hob_get_ddrmm ( /*IN*/ THH_hob_t hob, 
                            /*OUT*/ THH_ddrmm_t *ddrmm_p );

/******************************************************************************
 *  Function:     THH_hob_get_qpm 
 *
 *  Arguments:
 *        hob
 *        eventp_p - Included THH_qpm object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to qp management object.
 */
HH_ret_t THH_hob_get_qpm ( /*IN*/ THH_hob_t hob, 
                          /*OUT*/ THH_qpm_t *qpm_p );

/******************************************************************************
 *  Function:     THH_hob_get_cqm 
 *
 *  Arguments:
 *        hob
 *        eventp_p - Included THH_cqm object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to cq management object.
 */
HH_ret_t THH_hob_get_cqm ( /*IN*/ THH_hob_t hob, 
                          /*OUT*/ THH_cqm_t *cqm_p );

/******************************************************************************
 *  Function:     THH_hob_get_eventp 
 *
 *  Arguments:
 *        hob
 *        eventp_p - Included THH_eventp object 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Get a handle to event management object.
 */
HH_ret_t THH_hob_get_eventp ( /*IN*/ THH_hob_t hob, 
                              /*OUT*/ THH_eventp_t *eventp_p );

/* Function for special QPs (provide info. for building MLX IB headers) */
HH_ret_t THH_hob_get_sgid(HH_hca_hndl_t hca_hndl,IB_port_t port,u_int8_t idx, IB_gid_t* gid_p);
HH_ret_t THH_hob_get_qp1_pkey(HH_hca_hndl_t hca_hndl,IB_port_t port,VAPI_pkey_t* pkey);
HH_ret_t THH_hob_get_pkey(HH_hca_hndl_t  hca_hndl,IB_port_t  port,VAPI_pkey_ix_t pkey_index,
                          VAPI_pkey_t* pkey_p/*OUT*/);
HH_ret_t THH_hob_get_gid_tbl(HH_hca_hndl_t hca_hndl,IB_port_t port,u_int16_t tbl_len_in,
                             u_int16_t* tbl_len_out,IB_gid_t* param_gid_p);
HH_ret_t THH_hob_init_gid_tbl(HH_hca_hndl_t hca_hndl,IB_port_t port,u_int16_t tbl_len_in,
                             u_int16_t* tbl_len_out,IB_gid_t* param_gid_p);
HH_ret_t THH_hob_get_pkey_tbl(HH_hca_hndl_t  hca_hndl,IB_port_t     port_num,
                              u_int16_t tbl_len_in,u_int16_t *tbl_len_out,IB_pkey_t *pkey_tbl_p);
HH_ret_t THH_hob_init_pkey_tbl(HH_hca_hndl_t  hca_hndl,IB_port_t     port_num,
                              u_int16_t tbl_len_in,u_int16_t *tbl_len_out,IB_pkey_t *pkey_tbl_p);
HH_ret_t THH_hob_get_legacy_mode(THH_hob_t thh_hob_p,MT_bool *p_mode);
HH_ret_t THH_hob_check_qp_init_attrs (THH_hob_t hob, HH_qp_init_attr_t * init_attr_p,
                                           MT_bool is_special_qp );


/******************************************************************************
 *  Function:     THH_hob_fatal_error 
 *
 *  Arguments:
 *        hob
 *        fatal_err_type - type of fatal error which occurred 
 *
 *  Returns:
 *    HH_OK
 *    HH_EINVAL - Invalid parameters
 *
 *  Description:
 *    Initiates centralized fatal error handling when a fatal error is detected
 */
HH_ret_t   THH_hob_fatal_error(/*IN*/ THH_hob_t hob,
                               /*IN*/ THH_fatal_err_t  fatal_err_type,
                               /*IN*/ VAPI_event_syndrome_t  syndrome);

VIP_delay_unlock_t THH_hob_get_delay_unlock(THH_hob_t hob);

HH_ret_t   THH_hob_get_init_params(/*IN*/ THH_hob_t  thh_hob_p, 
                   /*OUT*/  THH_hw_props_t     *hw_props_p,
                   /*OUT*/  u_int32_t          *hca_seq_num,
                   /*OUT*/  THH_module_flags_t *mod_flags);
HH_ret_t THH_hob_restart(HH_hca_hndl_t  hca_hndl);

HH_ret_t THH_hob_get_state(THH_hob_t thh_hob_p, THH_hob_state_t *fatal_state);
HH_ret_t THH_hob_get_fatal_syncobj(THH_hob_t thh_hob_p, MOSAL_syncobj_t *syncobj);
HH_ret_t THH_hob_wait_if_fatal(THH_hob_t thh_hob_p, MT_bool *had_fatal);

HH_ret_t THH_hob_query_port_prop(HH_hca_hndl_t  hca_hndl,IB_port_t port_num,VAPI_hca_port_t *hca_port_p );
HH_ret_t THH_hob_alloc_ul_res(HH_hca_hndl_t hca_hndl,MOSAL_protection_ctx_t prot_ctx,void *hca_ul_resources_p);
HH_ret_t THH_hob_free_ul_res(HH_hca_hndl_t hca_hndl,void *hca_ul_resources_p);
HH_ret_t THH_hob_alloc_pd(HH_hca_hndl_t hca_hndl, MOSAL_protection_ctx_t prot_ctx, void * pd_ul_resources_p,HH_pd_hndl_t *pd_num_p);
HH_ret_t THH_hob_free_pd(HH_hca_hndl_t hca_hndl, HH_pd_hndl_t pd_num);
HH_ret_t THH_hob_alloc_rdd(HH_hca_dev_t *hh_dev_p, HH_rdd_hndl_t *rdd_p);
HH_ret_t THH_hob_free_rdd(HH_hca_dev_t *hh_dev_p, HH_rdd_hndl_t rdd);
HH_ret_t THH_hob_create_ud_av(HH_hca_hndl_t hca_hndl,HH_pd_hndl_t pd,VAPI_ud_av_t *av_p, HH_ud_av_hndl_t *ah_p);
HH_ret_t THH_hob_modify_ud_av(HH_hca_hndl_t hca_hndl, HH_ud_av_hndl_t ah,VAPI_ud_av_t *av_p);
HH_ret_t THH_hob_query_ud_av(HH_hca_hndl_t hca_hndl, HH_ud_av_hndl_t ah,VAPI_ud_av_t *av_p);
HH_ret_t THH_hob_destroy_ud_av(HH_hca_hndl_t hca_hndl, HH_ud_av_hndl_t ah);
HH_ret_t THH_hob_register_mr(HH_hca_hndl_t hca_hndl,HH_mr_t *mr_props_p,VAPI_lkey_t *lkey_p,IB_rkey_t *rkey_p);
HH_ret_t THH_hob_reregister_mr(HH_hca_hndl_t hca_hndl,VAPI_lkey_t lkey, VAPI_mr_change_t change_mask, HH_mr_t *mr_props_p, 
                                     VAPI_lkey_t* lkey_p,IB_rkey_t *rkey_p);
HH_ret_t THH_hob_register_smr(HH_hca_hndl_t hca_hndl,HH_smr_t *mr_props_p,VAPI_lkey_t *lkey_p,IB_rkey_t *rkey_p);
HH_ret_t THH_hob_query_mr(HH_hca_hndl_t hca_hndl,VAPI_lkey_t lkey,HH_mr_info_t *mr_info_p);
HH_ret_t THH_hob_deregister_mr(HH_hca_hndl_t hca_hndl,VAPI_lkey_t lkey);
HH_ret_t THH_hob_alloc_mw(HH_hca_hndl_t hca_hndl,HH_pd_hndl_t pd,IB_rkey_t *initial_rkey_p);
HH_ret_t THH_hob_query_mw(HH_hca_hndl_t hca_hndl,IB_rkey_t initial_rkey,IB_rkey_t *current_rkey_p,HH_pd_hndl_t *pd_p);
HH_ret_t THH_hob_free_mw(HH_hca_hndl_t hca_hndl,IB_rkey_t initial_rkey);
HH_ret_t THH_hob_alloc_fmr(HH_hca_hndl_t hca_hndl, HH_pd_hndl_t pd,
                           VAPI_mrw_acl_t acl,MT_size_t max_pages,u_int8_t log2_page_sz,VAPI_lkey_t* last_lkey_p);
HH_ret_t  THH_hob_map_fmr(HH_hca_hndl_t hca_hndl,VAPI_lkey_t last_lkey,
                          EVAPI_fmr_map_t* map_p,VAPI_lkey_t* lkey_p,IB_rkey_t* rkey_p);
HH_ret_t  THH_hob_unmap_fmr(HH_hca_hndl_t hca_hndl,u_int32_t num_of_fmrs_to_unmap, VAPI_lkey_t*  last_lkeys_array);
HH_ret_t  THH_hob_free_fmr(HH_hca_hndl_t  hca_hndl,VAPI_lkey_t     last_lkey);
HH_ret_t THH_hob_create_cq(HH_hca_hndl_t hca_hndl,MOSAL_protection_ctx_t user_prot_context,
                                 void *cq_ul_resources_p,HH_cq_hndl_t *cq_p);
HH_ret_t THH_hob_resize_cq(HH_hca_hndl_t hca_hndl,HH_cq_hndl_t cq,void *cq_ul_resources_p);
HH_ret_t THH_hob_query_cq(HH_hca_hndl_t hca_hndl,HH_cq_hndl_t cq,VAPI_cqe_num_t *num_o_cqes_p);
HH_ret_t THH_hob_destroy_cq(HH_hca_hndl_t hca_hndl,HH_cq_hndl_t cq);
HH_ret_t THH_hob_create_qp(HH_hca_hndl_t hca_hndl,HH_qp_init_attr_t *init_attr_p, void  *qp_ul_resources_p,IB_wqpn_t *qpn_p);
HH_ret_t THH_hob_get_special_qp(HH_hca_hndl_t hca_hndl,VAPI_special_qp_t qp_type,IB_port_t port, 
                                      HH_qp_init_attr_t *init_attr_p,void *qp_ul_resources_p,IB_wqpn_t *sqp_hndl_p);
HH_ret_t THH_hob_modify_qp(HH_hca_hndl_t hca_hndl,IB_wqpn_t qpn,VAPI_qp_state_t cur_qp_state,
                                 VAPI_qp_attr_t *qp_attr_p,VAPI_qp_attr_mask_t *qp_attr_mask_p);
HH_ret_t THH_hob_query_qp(HH_hca_hndl_t hca_hndl,IB_wqpn_t qpn,VAPI_qp_attr_t *qp_attr_p);
HH_ret_t THH_hob_destroy_qp(HH_hca_hndl_t hca_hndl,IB_wqpn_t qpn);

HH_ret_t THH_hob_create_srq(HH_hca_hndl_t hca_hndl, HH_pd_hndl_t pd, void *srq_ul_resources_p, 
                            HH_srq_hndl_t     *srq_p);
HH_ret_t THH_hob_query_srq(HH_hca_hndl_t hca_hndl, HH_srq_hndl_t srq, u_int32_t *limit_p);
HH_ret_t THH_hob_destroy_srq(HH_hca_hndl_t hca_hndl, HH_srq_hndl_t srq);


HH_ret_t THH_hob_process_local_mad(HH_hca_hndl_t  hca_hndl,IB_port_t port_num, IB_lid_t slid,
                        EVAPI_proc_mad_opt_t proc_mad_opts, void *mad_in_p, void * mad_out_p );


HH_ret_t THH_hob_ddrmm_alloc(HH_hca_hndl_t  hca_hndl,VAPI_size_t size,u_int8_t align_shift,VAPI_phy_addr_t*  buf_p);
HH_ret_t THH_hob_ddrmm_query(HH_hca_hndl_t  hca_hndl,u_int8_t      align_shift,VAPI_size_t*    total_mem,    
                             VAPI_size_t*    free_mem,VAPI_size_t*    largest_chunk,  
                             VAPI_phy_addr_t*  largest_free_addr_p);

HH_ret_t THH_hob_ddrmm_free(HH_hca_hndl_t  hca_hndl,VAPI_phy_addr_t  buf, VAPI_size_t size);


HH_ret_t THH_hob_create_eec(HH_hca_hndl_t hca_hndl,HH_rdd_hndl_t rdd,IB_eecn_t *eecn_p);
HH_ret_t THH_hob_modify_eec(HH_hca_hndl_t hca_hndl,IB_eecn_t eecn,VAPI_qp_state_t cur_ee_state, 
                                  VAPI_qp_attr_t  *ee_attr_p,VAPI_qp_attr_mask_t *ee_attr_mask_p);
HH_ret_t THH_hob_query_eec(HH_hca_hndl_t hca_hndl,IB_eecn_t eecn,VAPI_qp_attr_t *ee_attr_p);
HH_ret_t THH_hob_destroy_eec(HH_hca_hndl_t hca_hndl,IB_eecn_t eecn);
HH_ret_t THH_hob_attach_to_multicast(HH_hca_hndl_t hca_hndl,IB_wqpn_t qpn,IB_gid_t dgid);
HH_ret_t THH_hob_detach_from_multicast(HH_hca_hndl_t hca_hndl,IB_wqpn_t qpn,IB_gid_t dgid);
HH_ret_t  THH_hob_get_num_ports( HH_hca_hndl_t  hca_hndl, IB_port_t *num_ports_p);

#ifdef IVAPI_THH

HH_ret_t THH_hob_set_comp_eventh(HH_hca_hndl_t      hca_hndl,
                                 HH_comp_eventh_t   event,
                                 void*              private_data);
HH_ret_t THH_hob_set_async_eventh(HH_hca_hndl_t      hca_hndl,
                                  HH_async_eventh_t  event,
                                  void*              private_data);
HH_ret_t THH_hob_open_hca(HH_hca_hndl_t  hca_hndl, 
                                 EVAPI_hca_profile_t  *prop_props_p,
                                 EVAPI_hca_profile_t  *sugg_profile_p);
HH_ret_t THH_hob_close_hca(HH_hca_hndl_t  hca_hndl);
HH_ret_t THH_hob_query(HH_hca_hndl_t  hca_hndl, VAPI_hca_cap_t *hca_cap_p);
HH_ret_t THH_hob_modify(HH_hca_hndl_t        hca_hndl,
                        IB_port_t            port_num,
                        VAPI_hca_attr_t      *hca_attr_p,
                        VAPI_hca_attr_mask_t *hca_attr_mask_p);

#endif /* IVAPI_THH */
#if defined(MT_SUSPEND_QP)
HH_ret_t THH_hob_suspend_qp(HH_hca_hndl_t  hca_hndl, 
                            IB_wqpn_t      qpn, 
                            MT_bool        suspend_flag);
HH_ret_t THH_hob_suspend_cq(HH_hca_hndl_t  hca_hndl, 
                            HH_cq_hndl_t   cq, 
                            MT_bool        do_suspend);
#endif

#endif  /* H_THH_H */
