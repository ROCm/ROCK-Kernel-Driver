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

#ifndef H_HH_H
#define H_HH_H

#include <mtl_common.h>
#include <ib_defs.h>
#include <vapi_types.h>
#include <hh_common.h>
#include <mosal.h>

/*
 * Global defines
 *
 */


/*
 * Typedefs
 *
 */

typedef enum
{
  HH_HCA_STATUS_INVALID,  /* Invalid context entry */
  HH_HCA_STATUS_ZOMBIE,   /* HCA was removed but still has outstanding resources */
  HH_HCA_STATUS_CLOSED,
  HH_HCA_STATUS_OPENED
} HH_hca_status_t;


/*
 * Device information (public object/context)
 *
 */

typedef struct HH_hca_dev_st {
  char               *dev_desc;         /* Device description (name, etc.) */
  char               *user_lib;         /* User level library (dyn-link) */
  u_int32_t           vendor_id;        /* IEEE's 24 bit Device Vendor ID */
  u_int32_t           dev_id;           /* Device ID */
  u_int32_t           hw_ver;           /* Hardware version (step/rev)  */
  struct hh_if_ops*   if_ops;           /* Interface operations */

                                        /* Size (bytes) of user-level ...  */
  MT_size_t       hca_ul_resources_sz;  /* .. resources context for an HCA */
  MT_size_t       pd_ul_resources_sz;   /* .. resources context for a PD   */
  MT_size_t       cq_ul_resources_sz;   /* .. resources context for a CQ   */
  MT_size_t       srq_ul_resources_sz;  /* .. resources context for a SRQ  */
  MT_size_t       qp_ul_resources_sz;   /* .. resources context for a QP   */

  void*               device;           /* Device private data */
  HH_hca_status_t     status;           /* Device Status */
} HH_hca_dev_t;


typedef enum {
    HH_TPT_PAGE,
    HH_TPT_BUF,
    HH_TPT_IOBUF} HH_tpt_type_t;

typedef struct {

  HH_tpt_type_t     tpt_type;

  MT_size_t       num_entries; /* Num. of entries */
  union {
    struct {
      VAPI_phy_addr_t  *phys_page_lst; /* Array of physical addrs. per page */
      u_int8_t      page_shift;    /* log2 page size in this page list */
    } page_lst;
    struct {
      VAPI_phy_addr_t *phys_buf_lst;  /* Array of physical addrs. per buffer (sys page-sz aligned) */
      VAPI_size_t  *buf_sz_lst;    /* Size of each buffer (sys page-sz multiple) */
      VAPI_phy_addr_t  iova_offset;    /* Offset of start IOVA in first buffer  */
    } buf_lst;
    MOSAL_iobuf_t iobuf;  /* HH_TPT_IOBUF */
  } tpt;
} HH_tpt_t;


typedef struct HH_mr_st {     /* Memory region registration data */
  IB_virt_addr_t   start;     /* Start virtual address (byte addressing) */
  VAPI_size_t      size;      /* Size in bytes */
  HH_pd_hndl_t     pd;        /* PD handle for new memory region */
  VAPI_mrw_acl_t   acl;       /* Access control (R/W permission local/remote */
  HH_tpt_t         tpt;       /* TPT - physical addresses list */
} HH_mr_t;


typedef struct HH_smr_st {    /* Shared Memory region registration data */
  VAPI_lkey_t       lkey;     /* L-Key of the region to share with */
  IB_virt_addr_t    start;    /* This region start virtual addr */
  HH_pd_hndl_t      pd ;      /* PD handle for new memory region */
  VAPI_mrw_acl_t    acl;      /* Access control (R/W permission local/remote */
} HH_smr_t;


typedef struct HH_mr_info_st {
  VAPI_lkey_t       lkey;          /* Local region key */
  VAPI_rkey_t       rkey;          /* Remote region key */
  IB_virt_addr_t    local_start;   /* Actual enforce local access lower limit */
  VAPI_size_t       local_size;    /* Actual enforce size for local access */
  IB_virt_addr_t    remote_start;  /* Actual enforce remote access lower limit
                                    * (volid only if remote access allowed */
  VAPI_size_t       remote_size;   /* Actual enforce size for remote access
                                    * (volid only if remote access allowed */
  HH_pd_hndl_t      pd;            /* PD handle for new memory region */
  VAPI_mrw_acl_t    acl;           /* Access control (R/W  local/remote */
} HH_mr_info_t;



/*
 * Queue Pairs
 *
 */

/* Initial Attributes passed during creation */
typedef struct HH_qp_init_attr_st {
  VAPI_ts_type_t      ts_type;       /* Transport Type */
  HH_pd_hndl_t        pd;            /* Protection Domain for this QP */
  HH_rdd_hndl_t       rdd;           /* Reliable Datagram Domain (RD only) */
  HH_srq_hndl_t       srq;
                                     
  HH_cq_hndl_t        sq_cq;         /* Send Queue Completion Queue Number */
  HH_cq_hndl_t        rq_cq;         /* Receive Queue Completion Queue Number */
                                     
  VAPI_sig_type_t     sq_sig_type;   /* Signal Type for Send Queue */
  VAPI_sig_type_t     rq_sig_type;   /* Signal Type for Receive Queue */

  VAPI_qp_cap_t       qp_cap;        /* Capabilities (Max(outstand)+max(S/G)) */

} HH_qp_init_attr_t;


/* HH Event Records */
typedef struct {
    VAPI_event_record_type_t  etype;     /* event record type - see vapi.h  */
    VAPI_event_syndrome_t    syndrome;     /* syndrome value (for fatal error) */
    union {                              
        IB_wqpn_t             qpn;       /* Affiliated QP Number   */
        HH_srq_hndl_t         srq;       /* Affiliated SRQ handle  */
        IB_eecn_t             eecn;      /* Affiliated EEC Number  */
        HH_cq_hndl_t          cq;        /* Affiliated CQ handle   */
        IB_port_t             port;      /* Affiliated Port Number */
    } event_modifier;
} HH_event_record_t;


typedef void (*HH_async_eventh_t)(HH_hca_hndl_t, 
                                  HH_event_record_t *, 
                                  void*  private_context);
typedef void (*HH_comp_eventh_t)(HH_hca_hndl_t, 
                                 HH_cq_hndl_t, 
                                 void*  private_context);

/***************************/
/* HH API Function mapping */
/***************************/


typedef struct hh_if_ops {

   /* Global HCA resources */
   /************************/

  HH_ret_t  (*HHIF_open_hca)(HH_hca_hndl_t  hca_hndl,
                             EVAPI_hca_profile_t  *prop_props_p,
                             EVAPI_hca_profile_t  *sugg_props_p);

  HH_ret_t  (*HHIF_close_hca)(HH_hca_hndl_t  hca_hndl);

  HH_ret_t  (*HHIF_alloc_ul_resources)(HH_hca_hndl_t  hca_hndl,
                                       MOSAL_protection_ctx_t   user_protection_context,
                                       void*          hca_ul_resources_p);

  HH_ret_t  (*HHIF_free_ul_resources)(HH_hca_hndl_t  hca_hndl,
                                      void*          hca_ul_resources_p);

  HH_ret_t  (*HHIF_query_hca)(HH_hca_hndl_t    hca_hndl,
                              VAPI_hca_cap_t*  hca_cap_p);

  HH_ret_t  (*HHIF_modify_hca)(HH_hca_hndl_t          hca_hndl,
                               IB_port_t              port_num,
                               VAPI_hca_attr_t*       hca_attr_p,
                               VAPI_hca_attr_mask_t*  hca_attr_mask_p);

  HH_ret_t  (*HHIF_query_port_prop)(HH_hca_hndl_t     hca_hndl,
                                    IB_port_t         port_num,
                                    VAPI_hca_port_t*  hca_port_p);

  HH_ret_t  (*HHIF_get_pkey_tbl)(HH_hca_hndl_t  hca_hndl,
                                 IB_port_t      port_num,
                                 u_int16_t      tbl_len_in,
                                 u_int16_t*     tbl_len_out,
                                 IB_pkey_t*     pkey_tbl_p);

  HH_ret_t  (*HHIF_get_gid_tbl)(HH_hca_hndl_t  hca_hndl,
                                IB_port_t      port_num,
                                u_int16_t      tbl_len_in,
                                u_int16_t*     tbl_len_out,
                                IB_gid_t*      gid_tbl_p);

  HH_ret_t  (*HHIF_get_lid)(HH_hca_hndl_t  hca_hndl,
                            IB_port_t      port,
                            IB_lid_t*      lid_p,
                            u_int8_t*      lmc_p);


   /* Protection Domain */
   /*********************/

  HH_ret_t  (*HHIF_alloc_pd)(HH_hca_hndl_t  hca_hndl, 
                             MOSAL_protection_ctx_t prot_ctx, 
                             void * pd_ul_resources_p, 
                             HH_pd_hndl_t *pd_num_p);

  HH_ret_t  (*HHIF_free_pd)(HH_hca_hndl_t  hca_hndl,
                            HH_pd_hndl_t   pd);


   /* Reliable Datagram Domain */
   /****************************/

  HH_ret_t  (*HHIF_alloc_rdd)(HH_hca_hndl_t   hca_hndl,
                              HH_rdd_hndl_t*  rdd_p);

  HH_ret_t  (*HHIF_free_rdd)(HH_hca_hndl_t  hca_hndl,
                             HH_rdd_hndl_t  rdd);


   /* Privileged UD AV */
   /********************/

  HH_ret_t  (*HHIF_create_priv_ud_av)(HH_hca_hndl_t     hca_hndl,
                                      HH_pd_hndl_t      pd,
                                      VAPI_ud_av_t*     av_p,
                                      HH_ud_av_hndl_t*  ah_p);

  HH_ret_t  (*HHIF_modify_priv_ud_av)(HH_hca_hndl_t    hca_hndl,
                                      HH_ud_av_hndl_t  ah,
                                      VAPI_ud_av_t*    av_p);

  HH_ret_t  (*HHIF_query_priv_ud_av)(HH_hca_hndl_t    hca_hndl,
                                     HH_ud_av_hndl_t  ah,
                                     VAPI_ud_av_t*    av_p);

  HH_ret_t  (*HHIF_destroy_priv_ud_av)(HH_hca_hndl_t    hca_hndl,
                                       HH_ud_av_hndl_t  ah);


   /* Memory Regions/Windows */
   /**************************/

  HH_ret_t  (*HHIF_register_mr)(HH_hca_hndl_t  hca_hndl,
                                HH_mr_t*       mr_props_p,
                                VAPI_lkey_t*   lkey_p,
                                IB_rkey_t*   rkey_p);

  HH_ret_t  (*HHIF_reregister_mr)(HH_hca_hndl_t  hca_hndl,
                                  VAPI_lkey_t    lkey,
                                  VAPI_mr_change_t  change_mask,
                                  HH_mr_t*       mr_props_p,
                                  VAPI_lkey_t*  lkey_p,
                                  IB_rkey_t*   rkey_p);

  HH_ret_t  (*HHIF_register_smr)(HH_hca_hndl_t  hca_hndl,
                                 HH_smr_t*      smr_props_p,
                                 VAPI_lkey_t*   lkey_p,
                                 IB_rkey_t*   rkey_p);

  HH_ret_t  (*HHIF_deregister_mr)(HH_hca_hndl_t  hca_hndl,
                                  VAPI_lkey_t    lkey);

  HH_ret_t  (*HHIF_query_mr)(HH_hca_hndl_t  hca_hndl,
                             VAPI_lkey_t    lkey,
                             HH_mr_info_t*  mr_info_p);

  HH_ret_t  (*HHIF_alloc_mw)(HH_hca_hndl_t  hca_hndl,
                             HH_pd_hndl_t   pd,
                             IB_rkey_t*   initial_rkey_p);

  HH_ret_t  (*HHIF_query_mw)(HH_hca_hndl_t  hca_hndl,
                             IB_rkey_t      initial_rkey, 
                             IB_rkey_t*     current_rkey_p,
                             HH_pd_hndl_t   *pd);

  HH_ret_t  (*HHIF_free_mw)(HH_hca_hndl_t  hca_hndl,
                            IB_rkey_t    initial_rkey);

  /* Fast Memory Regions */
  /***********************/
  HH_ret_t  (*HHIF_alloc_fmr)(HH_hca_hndl_t  hca_hndl,
                              HH_pd_hndl_t   pd,
                              VAPI_mrw_acl_t acl, 
                              MT_size_t      max_pages,      /* Maximum number of pages that can be mapped using this region */
							                u_int8_t       log2_page_sz,	 /* Fixed page size for all maps on a given FMR */
                              VAPI_lkey_t*   last_lkey_p);   /* To be used as the initial FMR handle */

  HH_ret_t  (*HHIF_map_fmr)(HH_hca_hndl_t  hca_hndl,
                            VAPI_lkey_t   last_lkey,
                            EVAPI_fmr_map_t* map_p,
                            VAPI_lkey_t*   lkey_p,
                            IB_rkey_t*     rkey_p);
  
  HH_ret_t  (*HHIF_unmap_fmr)(HH_hca_hndl_t hca_hndl,
                              u_int32_t     num_of_fmrs_to_unmap,
                              VAPI_lkey_t*  last_lkeys_array);

  HH_ret_t  (*HHIF_free_fmr)(HH_hca_hndl_t  hca_hndl,
                             VAPI_lkey_t    last_lkey);   /* as returned on last successful mapping operation */


   /* Completion Queues */
   /*********************/

  HH_ret_t  (*HHIF_create_cq)(HH_hca_hndl_t  hca_hndl,
                              MOSAL_protection_ctx_t  user_protection_context,
                              void*          cq_ul_resources_p,
                              HH_cq_hndl_t*  cq);

  HH_ret_t  (*HHIF_resize_cq)(HH_hca_hndl_t  hca_hndl,
                              HH_cq_hndl_t   cq,
                              void*          cq_ul_resources_p);

  HH_ret_t  (*HHIF_query_cq)(HH_hca_hndl_t  hca_hndl,
                             HH_cq_hndl_t   cq,
                             VAPI_cqe_num_t*   num_o_cqes_p);

  HH_ret_t  (*HHIF_destroy_cq)(HH_hca_hndl_t  hca_hndl,
                               HH_cq_hndl_t   cq);


#if defined(MT_SUSPEND_QP)
  HH_ret_t  (*HHIF_suspend_cq)(HH_hca_hndl_t  hca_hndl,
                               HH_cq_hndl_t   cq,
                               MT_bool        do_suspend);
#endif

   /* Queue Pairs */
   /***************/

  HH_ret_t  (*HHIF_create_qp)(HH_hca_hndl_t       hca_hndl,
                              HH_qp_init_attr_t*  init_attr_p,
                              void*               qp_ul_resources_p,
                              IB_wqpn_t*          qpn_p);

  HH_ret_t  (*HHIF_get_special_qp)(HH_hca_hndl_t       hca_hndl,
                                   VAPI_special_qp_t   qp_type,
                                   IB_port_t           port,
                                   HH_qp_init_attr_t*  init_attr_p,
                                   void*               qp_ul_resources_p,
                                   IB_wqpn_t*          sqp_hndl_p);

  HH_ret_t  (*HHIF_modify_qp)(HH_hca_hndl_t         hca_hndl,
                              IB_wqpn_t             qpn,
                              VAPI_qp_state_t       cur_qp_state,
                              VAPI_qp_attr_t*       qp_attr_p,
                              VAPI_qp_attr_mask_t*  qp_attr_mask_p);

  HH_ret_t  (*HHIF_query_qp)(HH_hca_hndl_t    hca_hndl,
                             IB_wqpn_t        qp_num,
                             VAPI_qp_attr_t*  qp_attr_p);

  HH_ret_t  (*HHIF_destroy_qp)(HH_hca_hndl_t  hca_hndl,
                               IB_wqpn_t      qp_num);

#if defined(MT_SUSPEND_QP)
  HH_ret_t  (*HHIF_suspend_qp)(HH_hca_hndl_t  hca_hndl,
                             IB_wqpn_t        qp_num,
                             MT_bool          suspend_flag);
#endif

  
  /* Shared Receive Queue (SRQ) */
  /******************************/

  HH_ret_t  (*HHIF_create_srq)(HH_hca_hndl_t  hca_hndl,
                               HH_pd_hndl_t pd, 
                               void *srq_ul_resources_p, 
                               HH_srq_hndl_t     *srq_p);

  HH_ret_t  (*HHIF_query_srq)(HH_hca_hndl_t  hca_hndl,
                              HH_srq_hndl_t  srq,
                              u_int32_t      *limit_p);

  HH_ret_t  (*HHIF_destroy_srq)(HH_hca_hndl_t  hca_hndl,
                                HH_srq_hndl_t  srq);
   

   /* End to End Context */
   /**********************/

  HH_ret_t  (*HHIF_create_eec)(HH_hca_hndl_t  hca_hndl,
                               HH_rdd_hndl_t  rdd,
                               IB_eecn_t*     eecn_p);

  HH_ret_t  (*HHIF_modify_eec)(HH_hca_hndl_t         hca_hndl,
                               IB_eecn_t             eecn,
                               VAPI_qp_state_t       cur_ee_state,
                               VAPI_qp_attr_t*       ee_attr_p,
                               VAPI_qp_attr_mask_t*  ee_attr_mask_p);

  HH_ret_t  (*HHIF_query_eec)(HH_hca_hndl_t    hca_hndl,
                              IB_eecn_t        eecn,
                              VAPI_qp_attr_t*  ee_attr_p);

  HH_ret_t  (*HHIF_destroy_eec)(HH_hca_hndl_t  hca_hndl,
                                IB_eecn_t      eecn);


   /* Event Handler Calls */
   /***********************/

  HH_ret_t  (*HHIF_set_async_eventh)(HH_hca_hndl_t      hca_hndl,
                                     HH_async_eventh_t  handler,
                                     void*              private_context);

  HH_ret_t  (*HHIF_set_comp_eventh)(HH_hca_hndl_t     hca_hndl,
                                    HH_comp_eventh_t  handler,
                                    void*             private_context);


   /* Multicast Groups */
   /********************/

  HH_ret_t  (*HHIF_attach_to_multicast)(HH_hca_hndl_t  hca_hndl,
                                        IB_wqpn_t      qpn,
                                        IB_gid_t       dgid);

  HH_ret_t  (*HHIF_detach_from_multicast)(HH_hca_hndl_t  hca_hndl,
                                          IB_wqpn_t      qpn,
                                          IB_gid_t       dgid);


  /* Local MAD processing */
  HH_ret_t  (*HHIF_process_local_mad)(HH_hca_hndl_t     hca_hndl,
                                   IB_port_t            port,
                                   IB_lid_t             slid,
                                   EVAPI_proc_mad_opt_t proc_mad_opts,
                                   void*                mad_in_p,
                                   void*                mad_out_p);
  

  HH_ret_t (*HHIF_ddrmm_alloc)(HH_hca_hndl_t    hca_hndl,
                               VAPI_size_t     size, 
                               u_int8_t      align_shift,
                               VAPI_phy_addr_t*  buf_p);

  HH_ret_t (*HHIF_ddrmm_query)(HH_hca_hndl_t hca_hndl, 
                                u_int8_t      align_shift,
                                VAPI_size_t*    total_mem,        
                                VAPI_size_t*    free_mem,      
                                VAPI_size_t*    largest_chunk,    
                                VAPI_phy_addr_t*  largest_free_addr_p);


  HH_ret_t  (*HHIF_ddrmm_free)(HH_hca_hndl_t    hca_hndl,
                               VAPI_phy_addr_t  buf,  
                               VAPI_size_t    size);


} HH_if_ops_t;


/*
 * HH Functions mapping definition
 */


#ifdef MT_KERNEL

/* Global HCA resources */
/************************/
#define HH_open_hca(hca_hndl, prop_props_p, sugg_props_p) \
  (hca_hndl)->if_ops->HHIF_open_hca(hca_hndl, prop_props_p, sugg_props_p)

#define HH_close_hca(hca_hndl) \
  (hca_hndl)->if_ops->HHIF_close_hca(hca_hndl)

#define HH_alloc_ul_resources(hca_hndl, usr_prot_ctx, hca_ul_resources_p) \
  (hca_hndl)->if_ops->HHIF_alloc_ul_resources(hca_hndl, usr_prot_ctx, hca_ul_resources_p)

#define HH_free_ul_resources(hca_hndl, hca_ul_resources_p) \
  (hca_hndl)->if_ops->HHIF_free_ul_resources(hca_hndl, hca_ul_resources_p)

#define HH_query_hca(hca_hndl, hca_cap_p) \
  (hca_hndl)->if_ops->HHIF_query_hca(hca_hndl, hca_cap_p)

#define HH_modify_hca(\
    hca_hndl, port_num, hca_attr_p, hca_attr_mask_p) \
  (hca_hndl)->if_ops->HHIF_modify_hca(\
    hca_hndl, port_num, hca_attr_p, hca_attr_mask_p)

#define HH_query_port_prop(hca_hndl, port_num, hca_port_p) \
  (hca_hndl)->if_ops->HHIF_query_port_prop(hca_hndl, port_num, hca_port_p)

#define HH_get_pkey_tbl(\
    hca_hndl, port_num, tbl_len_in, tbl_len_out, pkey_tbl_p) \
  (hca_hndl)->if_ops->HHIF_get_pkey_tbl(\
    hca_hndl, port_num, tbl_len_in, tbl_len_out, pkey_tbl_p)

#define HH_get_gid_tbl(\
    hca_hndl, port_num, tbl_len_in, tbl_len_out, pkey_tbl_p) \
  (hca_hndl)->if_ops->HHIF_get_gid_tbl(\
    hca_hndl, port_num, tbl_len_in, tbl_len_out, pkey_tbl_p)

#define HH_get_lid(\
    hca_hndl, port, lid_p, lmc_p) \
  (hca_hndl)->if_ops->HHIF_get_lid(\
    hca_hndl, port, lid_p, lmc_p)



/* Protection Domain */
/*********************/
#define HH_alloc_pd(hca_hndl, prot_ctx, pd_ul_resources_p,  pd_num_p) \
  (hca_hndl)->if_ops->HHIF_alloc_pd(hca_hndl, prot_ctx, pd_ul_resources_p, pd_num_p)

#define HH_free_pd(hca_hndl, pd) \
  (hca_hndl)->if_ops->HHIF_free_pd(hca_hndl, pd)



/* Reliable Datagram Domain */
/****************************/
#define HH_alloc_rdd(hca_hndl, rdd_p) \
  (hca_hndl)->if_ops->HHIF_alloc_rdd(hca_hndl, rdd_p)

#define HH_free_rdd(hca_hndl, rdd) \
  (hca_hndl)->if_ops->HHIF_free_rdd(hca_hndl, rdd)



/* Privileged UD AV */
/********************/
#define HH_create_priv_ud_av(hca_hndl, pd, av_p, ah_p) \
  (hca_hndl)->if_ops->HHIF_create_priv_ud_av(hca_hndl, pd, av_p, ah_p)

#define HH_modify_priv_ud_av(hca_hndl, ah, av_p) \
  (hca_hndl)->if_ops->HHIF_modify_priv_ud_av(hca_hndl, ah, av_p)

#define HH_query_priv_ud_av(hca_hndl, ah, av_p) \
  (hca_hndl)->if_ops->HHIF_query_priv_ud_av(hca_hndl, ah, av_p)

#define HH_destroy_priv_ud_av(hca_hndl, ah) \
  (hca_hndl)->if_ops->HHIF_destroy_priv_ud_av(hca_hndl, ah)



/* Memory Regions/Windows */
/**************************/
#define HH_register_mr(\
    hca_hndl, mr_props_p, lkey_p, rkey_p) \
  (hca_hndl)->if_ops->HHIF_register_mr(\
    hca_hndl, mr_props_p, lkey_p, rkey_p)

#define HH_reregister_mr(\
    hca_hndl, lkey, change_mask, mr_props_p, lkey_p, rkey_p) \
  (hca_hndl)->if_ops->HHIF_reregister_mr(\
    hca_hndl, lkey, change_mask, mr_props_p, lkey_p, rkey_p)

#define HH_register_smr(\
    hca_hndl, smr_props_p, lkey_p, rkey_p) \
  (hca_hndl)->if_ops->HHIF_register_smr(\
    hca_hndl, smr_props_p, lkey_p, rkey_p)

#define HH_deregister_mr(hca_hndl, lkey) \
  (hca_hndl)->if_ops->HHIF_deregister_mr(hca_hndl, lkey)

#define HH_query_mr(hca_hndl, lkey, mr_info_p) \
  (hca_hndl)->if_ops->HHIF_query_mr(hca_hndl, lkey, mr_info_p)

#define HH_alloc_mw(hca_hndl, pd,  initial_rkey_p) \
  (hca_hndl)->if_ops->HHIF_alloc_mw(hca_hndl, pd, initial_rkey_p)

#define HH_query_mw(hca_hndl, initial_rkey, current_rkey_p, pd_p) \
  (hca_hndl)->if_ops->HHIF_query_mw(hca_hndl, initial_rkey, current_rkey_p, pd_p)

#define HH_free_mw(hca_hndl, initial_rkey) \
  (hca_hndl)->if_ops->HHIF_free_mw(hca_hndl, initial_rkey)


/* Fast Memory Regions */
/***********************/
#define HH_alloc_fmr(hca_hndl,pd,acl,max_pages,log2_page_sz,last_lkey_p) \
  (hca_hndl)->if_ops->HHIF_alloc_fmr(hca_hndl,pd,acl,max_pages,log2_page_sz,last_lkey_p)
  
#define HH_map_fmr(hca_hndl,last_lkey,map_p,lkey_p,rkey_p) \
  (hca_hndl)->if_ops->HHIF_map_fmr(hca_hndl,last_lkey,map_p,lkey_p,rkey_p)

#define HH_unmap_fmr(hca_hndl,num_of_fmrs_to_unmap,last_lkeys_array) \
  (hca_hndl)->if_ops->HHIF_unmap_fmr(hca_hndl,num_of_fmrs_to_unmap,last_lkeys_array)

#define HH_free_fmr(hca_hndl,last_lkey) \
  (hca_hndl)->if_ops->HHIF_free_fmr(hca_hndl,last_lkey)
  

/* Completion Queues */
/*********************/
#define HH_create_cq(hca_hndl, usr_prot_ctx, cq_ul_resources_p, cq) \
  (hca_hndl)->if_ops->HHIF_create_cq(hca_hndl, usr_prot_ctx, cq_ul_resources_p, cq)

#define HH_resize_cq(hca_hndl, cq, cq_ul_resources_p) \
  (hca_hndl)->if_ops->HHIF_resize_cq(hca_hndl, cq, cq_ul_resources_p)

#define HH_query_cq(hca_hndl, cq, num_o_cqes_p) \
  (hca_hndl)->if_ops->HHIF_query_cq(hca_hndl, cq, num_o_cqes_p)

#define HH_destroy_cq(hca_hndl, cq) \
  (hca_hndl)->if_ops->HHIF_destroy_cq(hca_hndl, cq)



/* Queue Pairs */
/***************/
#define HH_create_qp(\
    hca_hndl, init_attr_p, qp_ul_resources_p, qpn_p) \
  (hca_hndl)->if_ops->HHIF_create_qp(\
    hca_hndl, init_attr_p, qp_ul_resources_p, qpn_p)

#define HH_get_special_qp(\
    hca_hndl, qp_type, port, init_attr_p, qp_ul_resources_p, sqp_hndl_p) \
  (hca_hndl)->if_ops->HHIF_get_special_qp(\
    hca_hndl, qp_type, port, init_attr_p, qp_ul_resources_p, sqp_hndl_p)

#define HH_modify_qp(\
    hca_hndl, qp_num, cur_qp_state, qp_attr_p, qp_attr_mask_p) \
  (hca_hndl)->if_ops->HHIF_modify_qp(\
    hca_hndl, qp_num, cur_qp_state, qp_attr_p, qp_attr_mask_p)

#define HH_query_qp(hca_hndl, qp_num, qp_attr_p) \
  (hca_hndl)->if_ops->HHIF_query_qp(hca_hndl, qp_num, qp_attr_p)

#define HH_destroy_qp(hca_hndl, qp_num) \
  (hca_hndl)->if_ops->HHIF_destroy_qp(hca_hndl, qp_num)


  /* Shared Receive Queue (SRQ) */
  /******************************/

#define HH_create_srq(hca_hndl, pd, srq_ul_resources_p,srq_p) \
        (hca_hndl)->if_ops->HHIF_create_srq(hca_hndl, pd, srq_ul_resources_p,srq_p)

#define HH_query_srq(hca_hndl, srq, limit_p)                  \
        (hca_hndl)->if_ops->HHIF_query_srq(hca_hndl, srq, limit_p)

#define HH_destroy_srq(hca_hndl, srq)                         \
        (hca_hndl)->if_ops->HHIF_destroy_srq(hca_hndl, srq)


/* End to End Context */
/**********************/
#define HH_create_eec(hca_hndl, rdd, eecn_p) \
  (hca_hndl)->if_ops->HHIF_create_eec(hca_hndl, rdd, eecn_p)

#define HH_modify_eec(\
    hca_hndl, eecn, cur_ee_state, ee_attr_p, ee_attr_mask_p) \
  (hca_hndl)->if_ops->HHIF_modify_eec(\
    hca_hndl, eecn, cur_ee_state, ee_attr_p, ee_attr_mask_p)

#define HH_query_eec(hca_hndl, eecn, ee_attr_p) \
  (hca_hndl)->if_ops->HHIF_query_eec(hca_hndl, eecn, ee_attr_p)

#define HH_destroy_eec(hca_hndl, eecn) \
  (hca_hndl)->if_ops->HHIF_destroy_eec(hca_hndl, eecn)



/* Event Handler Calls */
/***********************/
#define HH_set_async_eventh(hca_hndl, handler, private_context) \
  (hca_hndl)->if_ops->HHIF_set_async_eventh(hca_hndl, handler, private_context)

#define HH_set_comp_eventh(hca_hndl, handler, private_context) \
  (hca_hndl)->if_ops->HHIF_set_comp_eventh(hca_hndl, handler, private_context)



/* Multicast Groups */
/********************/
#define HH_attach_to_multicast(hca_hndl, qpn, dgid) \
  (hca_hndl)->if_ops->HHIF_attach_to_multicast(hca_hndl, qpn, dgid)

#define HH_detach_from_multicast(hca_hndl, qpn, dgid) \
  (hca_hndl)->if_ops->HHIF_detach_from_multicast(hca_hndl, qpn, dgid)

/* Local MAD processing */
/************************/
#define HH_process_local_mad(\
    hca_hndl, port, slid, flags, mad_in_p, mad_out_p) \
  (hca_hndl)->if_ops->HHIF_process_local_mad(\
    hca_hndl, port, slid, flags, mad_in_p, mad_out_p)

#define HH_ddrmm_alloc(\
        hca_hndl, size, align_shift, buf_p) \
        (hca_hndl)->if_ops->HHIF_ddrmm_alloc(\
        hca_hndl, size, align_shift, buf_p)
        
#define HH_ddrmm_query(\
        hca_hndl, align_shift, total_mem, free_mem, largest_chunk,largest_free_addr_p) \
        (hca_hndl)->if_ops->HHIF_ddrmm_query(\
        hca_hndl, align_shift, total_mem, free_mem, largest_chunk,largest_free_addr_p)

#define HH_ddrmm_free(\
        hca_hndl, buf, size) \
        (hca_hndl)->if_ops->HHIF_ddrmm_free(\
        hca_hndl, buf, size)

#if defined(MT_SUSPEND_QP)
#define HH_suspend_qp(\
        hca_hndl, qp_num, suspend_flag) \
  (hca_hndl)->if_ops->HHIF_suspend_qp(\
        hca_hndl, qp_num, suspend_flag)
#define HH_suspend_cq(\
        hca_hndl, cq, do_suspend) \
  (hca_hndl)->if_ops->HHIF_suspend_cq(\
        hca_hndl, cq, do_suspend)
#endif

#else  

  HH_ret_t  HH_open_hca(HH_hca_hndl_t  hca_hndl,
                        EVAPI_hca_profile_t *prop_props_p,
                        EVAPI_hca_profile_t *sugg_props_p);

  HH_ret_t  HH_close_hca(HH_hca_hndl_t  hca_hndl);

  HH_ret_t  HH_alloc_ul_resources(HH_hca_hndl_t  hca_hndl,
                                       MOSAL_protection_ctx_t   user_protection_context,
                                       void*          hca_ul_resources_p);

  HH_ret_t  HH_free_ul_resources(HH_hca_hndl_t  hca_hndl,
                                      void*          hca_ul_resources_p);

  HH_ret_t  HH_query_hca(HH_hca_hndl_t    hca_hndl,
                              VAPI_hca_cap_t*  hca_cap_p);

  HH_ret_t  HH_modify_hca(HH_hca_hndl_t          hca_hndl,
                               IB_port_t              port_num,
                               VAPI_hca_attr_t*       hca_attr_p,
                               VAPI_hca_attr_mask_t*  hca_attr_mask_p);

  HH_ret_t  HH_query_port_prop(HH_hca_hndl_t     hca_hndl,
                                    IB_port_t         port_num,
                                    VAPI_hca_port_t*  hca_port_p);

  HH_ret_t  HH_get_pkey_tbl(HH_hca_hndl_t  hca_hndl,
                                 IB_port_t      port_num,
                                 u_int16_t      tbl_len_in,
                                 u_int16_t*     tbl_len_out,
                                 IB_pkey_t*     pkey_tbl_p);

  HH_ret_t  HH_get_gid_tbl(HH_hca_hndl_t  hca_hndl,
                                IB_port_t      port_num,
                                u_int16_t      tbl_len_in,
                                u_int16_t*     tbl_len_out,
                                IB_gid_t*      gid_tbl_p);

  HH_ret_t  HH_get_lid(HH_hca_hndl_t  hca_hndl,
                            IB_port_t      port,
                            IB_lid_t*      lid_p,
                            u_int8_t*      lmc_p);


   /* Protection Domain */
   /*********************/

  HH_ret_t  HH_alloc_pd(HH_hca_hndl_t  hca_hndl, 
                             MOSAL_protection_ctx_t prot_ctx, 
                             void * pd_ul_resources_p, 
                             HH_pd_hndl_t *pd_num_p);

  HH_ret_t  HH_free_pd(HH_hca_hndl_t  hca_hndl,
                            HH_pd_hndl_t   pd);


   /* Reliable Datagram Domain */
   /****************************/

  HH_ret_t  HH_alloc_rdd(HH_hca_hndl_t   hca_hndl,
                              HH_rdd_hndl_t*  rdd_p);

  HH_ret_t  HH_free_rdd(HH_hca_hndl_t  hca_hndl,
                             HH_rdd_hndl_t  rdd);


   /* Privileged UD AV */
   /********************/

  HH_ret_t  HH_create_priv_ud_av(HH_hca_hndl_t     hca_hndl,
                                      HH_pd_hndl_t      pd,
                                      VAPI_ud_av_t*     av_p,
                                      HH_ud_av_hndl_t*  ah_p);

  HH_ret_t  HH_modify_priv_ud_av(HH_hca_hndl_t    hca_hndl,
                                      HH_ud_av_hndl_t  ah,
                                      VAPI_ud_av_t*    av_p);

  HH_ret_t  HH_query_priv_ud_av(HH_hca_hndl_t    hca_hndl,
                                     HH_ud_av_hndl_t  ah,
                                     VAPI_ud_av_t*    av_p);

  HH_ret_t  HH_destroy_priv_ud_av(HH_hca_hndl_t    hca_hndl,
                                       HH_ud_av_hndl_t  ah);


   /* Memory Regions/Windows */
   /**************************/

  HH_ret_t  HH_register_mr(HH_hca_hndl_t  hca_hndl,
                                HH_mr_t*       mr_props_p,
                                VAPI_lkey_t*   lkey_p,
                                IB_rkey_t*   rkey_p);

  HH_ret_t  HH_reregister_mr(HH_hca_hndl_t  hca_hndl,
                                  VAPI_lkey_t    lkey,     
                                  VAPI_mr_change_t  change_mask,
                                  HH_mr_t*       mr_props_p,
                                  VAPI_lkey_t*    lkey_p,
                                  IB_rkey_t*   rkey_p);

  HH_ret_t  HH_register_smr(HH_hca_hndl_t  hca_hndl,
                                 HH_smr_t*      smr_props_p,
                                 VAPI_lkey_t*   lkey_p,
                                 IB_rkey_t*   rkey_p);

  HH_ret_t  HH_deregister_mr(HH_hca_hndl_t  hca_hndl,
                                  VAPI_lkey_t    lkey);

  HH_ret_t  HH_query_mr(HH_hca_hndl_t  hca_hndl,
                             VAPI_lkey_t    lkey,
                             HH_mr_info_t*  mr_info_p);

  HH_ret_t  HH_alloc_mw(HH_hca_hndl_t  hca_hndl,
                             HH_pd_hndl_t   pd,
                             IB_rkey_t*   initial_rkey_p);

  HH_ret_t  HH_query_mw(HH_hca_hndl_t  hca_hndl,
                             IB_rkey_t      initial_rkey, 
                             IB_rkey_t*     current_rkey_p,
                             HH_pd_hndl_t   *pd);

  HH_ret_t  HH_free_mw(HH_hca_hndl_t  hca_hndl,
                            IB_rkey_t    initial_rkey);


  /* Fast Memory Regions */
  /***********************/
  HH_ret_t  HH_alloc_fmr(HH_hca_hndl_t  hca_hndl,
                          HH_pd_hndl_t   pd,
                          VAPI_mrw_acl_t acl, 
                          MT_size_t      max_pages,      /* Maximum number of pages that can be mapped using this region */
                          u_int8_t       log2_page_sz,	 /* Fixed page size for all maps on a given FMR */
                          VAPI_lkey_t*   last_lkey_p);   /* To be used as the initial FMR handle */

  HH_ret_t  HH_map_fmr(HH_hca_hndl_t   hca_hndl,
                        VAPI_lkey_t    last_lkey,
                        EVAPI_fmr_map_t* map_p,
                        VAPI_lkey_t*   lkey_p,
                        IB_rkey_t*     rkey_p);
  
  HH_ret_t  HH_unmap_fmr(HH_hca_hndl_t hca_hndl,
                         u_int32_t    num_of_fmrs_to_unmap,
                         VAPI_lkey_t* last_lkeys_array);

  HH_ret_t  HH_free_fmr(HH_hca_hndl_t  hca_hndl,
                         VAPI_lkey_t   last_lkey);   /* as returned on last successful mapping operation */


   /* Completion Queues */
   /*********************/

  HH_ret_t  HH_create_cq(HH_hca_hndl_t  hca_hndl,
                              MOSAL_protection_ctx_t  user_protection_context,
                              void*          cq_ul_resources_p,
                              HH_cq_hndl_t*  cq);

  HH_ret_t  HH_resize_cq(HH_hca_hndl_t  hca_hndl,
                              HH_cq_hndl_t   cq,
                              void*          cq_ul_resources_p);

  HH_ret_t  HH_query_cq(HH_hca_hndl_t  hca_hndl,
                             HH_cq_hndl_t   cq,
                             VAPI_cqe_num_t*   num_o_cqes_p);

  HH_ret_t  HH_destroy_cq(HH_hca_hndl_t  hca_hndl,
                               HH_cq_hndl_t   cq);


   /* Queue Pairs */
   /***************/

  HH_ret_t  HH_create_qp(HH_hca_hndl_t       hca_hndl,
                              HH_qp_init_attr_t*  init_attr_p,
                              void*               qp_ul_resources_p,
                              IB_wqpn_t*          qpn_p);

  HH_ret_t  HH_get_special_qp(HH_hca_hndl_t       hca_hndl,
                                   VAPI_special_qp_t   qp_type,
                                   IB_port_t           port,
                                   HH_qp_init_attr_t*  init_attr_p,
                                   void*               qp_ul_resources_p,
                                   IB_wqpn_t*          sqp_hndl_p);

  HH_ret_t  HH_modify_qp(HH_hca_hndl_t         hca_hndl,
                              IB_wqpn_t             qp_num,
                              VAPI_qp_state_t       cur_qp_state,
                              VAPI_qp_attr_t*       qp_attr_p,
                              VAPI_qp_attr_mask_t*  qp_attr_mask_p);

  HH_ret_t  HH_query_qp(HH_hca_hndl_t    hca_hndl,
                             IB_wqpn_t        qp_num,
                             VAPI_qp_attr_t*  qp_attr_p);

  HH_ret_t  HH_destroy_qp(HH_hca_hndl_t  hca_hndl,
                               IB_wqpn_t      qp_num);


   /* End to End Context */
   /**********************/

  HH_ret_t  HH_create_eec(HH_hca_hndl_t  hca_hndl,
                               HH_rdd_hndl_t  rdd,
                               IB_eecn_t*     eecn_p);

  HH_ret_t  HH_modify_eec(HH_hca_hndl_t         hca_hndl,
                               IB_eecn_t             eecn,
                               VAPI_qp_state_t       cur_ee_state,
                               VAPI_qp_attr_t*       ee_attr_p,
                               VAPI_qp_attr_mask_t*  ee_attr_mask_p);

  HH_ret_t  HH_query_eec(HH_hca_hndl_t    hca_hndl,
                              IB_eecn_t        eecn,
                              VAPI_qp_attr_t*  ee_attr_p);

  HH_ret_t  HH_destroy_eec(HH_hca_hndl_t  hca_hndl,
                                IB_eecn_t      eecn);


   /* Event Handler Calls */
   /***********************/

  HH_ret_t  HH_set_async_eventh(HH_hca_hndl_t      hca_hndl,
                                     HH_async_eventh_t  handler,
                                     void*              private_context);

  HH_ret_t  HH_set_comp_eventh(HH_hca_hndl_t     hca_hndl,
                                    HH_comp_eventh_t  handler,
                                    void*             private_context);


   /* Multicast Groups */
   /********************/

  HH_ret_t  HH_attach_to_multicast(HH_hca_hndl_t  hca_hndl,
                                        IB_wqpn_t      qpn,
                                        IB_gid_t       dgid);

  HH_ret_t  HH_detach_from_multicast(HH_hca_hndl_t  hca_hndl,
                                          IB_wqpn_t      qpn,
                                          IB_gid_t       dgid);

  /* Local MAD processing */
  HH_ret_t  HH_process_local_mad(HH_hca_hndl_t        hca_hndl,
                                 IB_port_t            port,
                                 IB_lid_t             slid,
                                 EVAPI_proc_mad_opt_t proc_mad_opts,
                                 void*                mad_in_p,
                                 void*                mad_out_p);

  HH_ret_t  THH_ddrmm_alloc(HH_hca_hndl_t hca_hndl,
                                VAPI_size_t     size,
                                u_int8_t      align_shift,
                                VAPI_phy_addr_t*  buf_p);
  
  HH_ret_t  THH_ddrmm_query(HH_hca_hndl_t hca_hndl, 
                                u_int8_t      align_shift,
                                VAPI_size_t*    total_mem,        
                                VAPI_size_t*    free_mem,      
                                VAPI_size_t*    largest_chunk,    
                                VAPI_phy_addr_t*  largest_free_addr_p);

  
  HH_ret_t  THH_ddrmm_free(HH_hca_hndl_t hca_hndl,
                           VAPI_phy_addr_t  buf,
                           VAPI_size_t    size);

#if defined(MT_SUSPEND_QP)
  HH_ret_t HH_suspend_qp(HH_hca_hndl_t         hca_hndl,
                          IB_wqpn_t             qp_num,
                          MT_bool               suspend_flag);
  HH_ret_t HH_suspend_cq(HH_hca_hndl_t         hca_hndl,
                          HH_cq_hndl_t         cq,
                          MT_bool              do_suspend);
#endif
  
#endif  /*ifdef kernel */

extern HH_ret_t HHIF_dummy(void);


/****************************************************************************
 * Function: HH_add_hca_dev
 *
 * Arguments:
 *            dev_info (IN) : device data (with strings not copied).
 *            hca_hndl_p (OUT) : Allocated HCA object in HH
 *
 * Returns:   HH_OK     : operation successfull.
 *            HH_EAGAIN : not enough resources.
 *
 * Description:
 *   Add new HCA device to HH layer. This function should be called by an
 *   HCA device driver for each new device it finds and set up.
 * Important:
 *   Though given HCA handle is a pointer to HH_hca_dev_t, do NOT try to
 *   copy the HH_hca_dev_t and use a pointer to the copy. The original
 *   pointer/handle must be used !
 *
 ****************************************************************************/
extern HH_ret_t HH_add_hca_dev(HH_hca_dev_t* dev_info, HH_hca_hndl_t*  hca_hndl_p);


/****************************************************************************
 * Function: HH_rem_hca_dev
 *
 * Arguments:
 *            hca_hndl (IN) : HCA to remove
 *
 * Returns:   HH_OK     : operation successfull.
 *            HH_ENODEV : no such device.
 *
 * Description:
 *   Remove given HCA from HH. This function should be called by an
 *   HCA device driver for each of its devices upon cleanup (or for
 *   specific device which was "hot-removed".
 *
 ****************************************************************************/
extern  HH_ret_t  HH_rem_hca_dev(HH_hca_hndl_t hca_hndl);

#ifdef IVAPI_THH
/*****************************************************************************
 * Function: HH_lookup_hca
 *
 * Arguments:
 *           name         (IN)  : device name
 *           hca_handle_p (OUT) : HCA handle
 *
 * Returns:   HH_OK     : operation successfull.
 *            HH_ENODEV : no such device.
 *
 *****************************************************************************/
extern  HH_ret_t HH_lookup_hca(const char * name, HH_hca_hndl_t* hca_handle_p);
#endif


/*****************************************************************************
 * Function: HH_list_hcas
 *
 * Arguments:
 *            buf_entries(IN)     : Number of entries in given buffer
 *            num_of_hcas_p(OUT)  : Actual number of currently available HCAs
 *            hca_list_buf_p(OUT) : A buffer of buf_sz entries of HH_hca_hndl_t
 * Returns:   HH_OK     : operation successfull.
 *            HH_EINVAL : Invalid params (NULL ptrs).
 *            HH_EAGAIN : buf_entries is smaller then num_of_hcas
 * Description:
 *   Each device is refereced using HH_hca_hndl. In order to get list of
 *   available devices and get a handle to each of the HCA currently
 *   available HH provides in kernel HCA listing function (immitating usage
 *   of ls /dev/ in user space))..
 *
 *****************************************************************************/
extern HH_ret_t HH_list_hcas(u_int32_t       buf_entries,
                             u_int32_t*      num_of_hcas_p,
                             HH_hca_hndl_t*  hca_list_buf_p);

/************************************************************************
 * Set the if_ops tbl with dummy functions returning HH_ENOSYS.
 * This is convenient for initializing tables prior
 * setting them with partial real implementation.
 *
 * This way, the general HH_if_ops_t table structure can be extended,
 * requiring just recompilation.
 ************************************************************************/
extern void HH_ifops_tbl_set_enosys(HH_if_ops_t* tbl);

#endif /*_H_HH_H_*/
