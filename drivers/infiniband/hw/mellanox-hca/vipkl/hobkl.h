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
 
#ifndef H_VIP_HOBKL_H
#define H_VIP_HOBKL_H


#include <mtl_common.h>
#include <vapi.h>
#include <vip.h>
#include <vip_delay_unlock.h>
#include <hh.h>

#ifdef QPX_PRF
#include <vapi_common.h>
#include "/mswg/work/matan/shared_files/prf_count.h"
#endif

typedef VAPI_hca_id_t VIP_hca_id_t;

struct HOBKL_t {
  VIP_hca_id_t id;
  DEVMM_hndl_t devmm;
  PDM_hndl_t pdm;  
  CQM_hndl_t cqm;
  MM_hndl_t mm;
  SRQM_hndl_t srqm;
  QPM_hndl_t qpm;
  EM_hndl_t em;
  HH_hca_hndl_t hh;
  u_int32_t ref_count;
  VIP_delay_unlock_t  delay_unlocks;
#ifdef QPX_PRF
  prf_counter_t	pc_qpm_mod_qp;
  prf_counter_t	pc_qpm_mod_qp_array[VAPI_ERR][VAPI_ERR];
#endif

};

/*************************************************************************
 * Function: HOBKL_create
 *
 * Arguments:
 *  hca_id (IN) - HW HCA id
 *  hca_hndl (IN) - VAPI handle (for EM)
 *  hob_hndl_p (OUT) - Pointer to HOB KL handle to return
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HCA_ID: invalid HCA ID
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Creates new PDM object for new HOB.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_create ( /*IN*/ VIP_hca_id_t hca_id,
                         /*IN*/ EVAPI_hca_profile_t  *profile_p,
                         /*OUT*/ EVAPI_hca_profile_t  *sugg_profile_p,
                         /*OUT*/ HOBKL_hndl_t *hob_hndl_p);


/*************************************************************************
 * Function: HOBKL_set_vapi_hca_hndl
 *
 * Arguments:
 *  hob_hndl (IN) - HOB object handle
 *  hca_hndl (IN) - VAPI handle (for EM)
 *
 * Returns:
 *  VIP_OK,
 *  VIP_EINVAL_HCA_HNDL
 *
 * Description:
 *   Sets VAPI's hca handle in EM, for reporting in events callbacks (used by EM).
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_set_vapi_hca_hndl(/*IN*/HOBKL_hndl_t hob_hndl, /*IN*/VAPI_hca_hndl_t hca_hndl);


/*************************************************************************
 * Function: HOBKL_destroy
 *
 * Arguments:
 *  hob_hndl (IN) - HOB KL object to destroy
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HOB_HNDL
 *  VIP_EPERM
 *
 * Description:
 *   Cleanup resources of given HOB KL object
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_destroy ( /*IN*/ HOBKL_hndl_t hob_hndl );


/*************************************************************************
 * Function: HOBKL_query_cap
 *
 * Arguments:
 *  hob_hndl (IN)
 *  caps (OUT) - fill HCA capabilities here
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *
 * Description:
 *   Query HCA associated with this HOB.
 *   Only number of ports is returned.
 *   Use additional APIs to query specific ports.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_cap(HOBKL_hndl_t hob_hndl, VAPI_hca_cap_t* caps);

/*************************************************************************
 * Function: HOBKL_query_port_prop
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) number of the port to query
 *  hobkl_port_p (OUT) - fill HCA capabilities here
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *
 * Description:
 *   Query HCA port associated with this HOB.
 *   Only number of GIDs/ PKs is returned.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_port_prop(
    /*IN*/      HOBKL_hndl_t      hobkl_hndl,
    /*IN*/      IB_port_t           port_num,
    /*OUT*/     VAPI_hca_port_t     *hobkl_port_p
);

/*************************************************************************
 * Function: HOBKL_query_port_pkey_tbl
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) - number of the port to query
 *  tbl_len_in (IN) - size of table allocated
 *  tbl_len_out_p (OUT) return actual number of entries filled here
 *  pkey_tbl_p (OUT) - fill this PK table
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *  VIP_EAGAIN:  tbl_len_out > tbl_len_in
 *
 * Description:
 *   Query HCA port associated with this HOB.
 *   Sufficient memory must be allocated by the user
 *   beforehand. It is possible to use HOBKL_query_port_prop
 *   to find out the amount of memory that is necessary.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_port_pkey_tbl(
    /*IN*/  HOBKL_hndl_t     hobkl_hndl,
    /*IN*/  IB_port_t        port_num,
    /*IN*/  u_int16_t        tbl_len_in,
    /*OUT*/ u_int16_t        *tbl_len_out_p,
    /*OUT*/ VAPI_pkey_t      *pkey_tbl_p
);

/*************************************************************************
 * Function: HOBKL_query_port_gid_tbl
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) - number of the port to query
 *  tbl_len_in (IN) - size of table allocated
 *  tbl_len_out_p (OUT) return actual number of entries filled here
 *  gid_tbl_p (OUT) - fill this PK table
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *  VIP_EAGAIN:  tbl_len_out > tbl_len_in
 *
 * Description:
 *   Query HCA port associated with this HOB.
 *   Sufficient memory must be allocated by the user
 *   beforehand. It is possible to use HOBKL_query_port_prop
 *   to find out the amount of memory that is necessary.
 *
 *************************************************************************/ 
VIP_ret_t HOBKL_query_port_gid_tbl(
    /*IN*/  HOBKL_hndl_t     hobkl_hndl,
    /*IN*/  IB_port_t        port_num,
    /*IN*/  u_int16_t        tbl_len_in,
    /*OUT*/ u_int16_t        *tbl_len_out_p,
    /*OUT*/ VAPI_gid_t      *gid_tbl_p
);

/*************************************************************************
 * Function: HOBKL_process_local_mad
 *
 * Arguments:
 *  hobkl_hndl (IN)
 *  port_num (IN) - number of the port to which to submit MAD packet
 *  mad_in_p (IN) - mad input packet
 *  mad_out_p (OUT)  - mad packet returned by adapter
 *
 * Returns:	
 *  VIP_OK: Operation completed successfully.
 *  VIP_EINVAL_HOB_HNDL: Invalid HOB KL handle.
 *  VIP_EINVAL_PORT_NUM: Invalid port number
 *  VIP_EAGAIN:  
 *
 * Description:
 *   Submits a MAD packet to the channel adapter for processing,
 *   and returns a mad packet for replying to source host when needed.
 *************************************************************************/ 
VIP_ret_t HOBKL_process_local_mad(
    /*IN*/  HOBKL_hndl_t     hobkl_hndl,
    /*IN*/  IB_port_t        port_num,
    /*IN*/  IB_lid_t         slid, /* ignored on EVAPI_MAD_IGNORE_MKEY */
    /*IN*/  EVAPI_proc_mad_opt_t proc_mad_opts,
    /*IN*/  void *           mad_in_p,
    /*OUT*/ void *           mad_out_p
);

VAPI_ret_t HOBKL_modify_hca_attr(
    /*IN*/  HOBKL_hndl_t            hobkl_hndl,
    /*IN*/  IB_port_t               port_num,
    /*IN*/  VAPI_hca_attr_t         *hca_attr_p,
    /*IN*/  VAPI_hca_attr_mask_t    *hca_attr_mask_p
);


/* Functions to get all VIP managers from the HOB KL */
static inline DEVMM_hndl_t HOBKL_get_devmm(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->devmm;
}

static inline PDM_hndl_t HOBKL_get_pdm(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->pdm;
}


static inline CQM_hndl_t HOBKL_get_cqm(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->cqm;
}


static inline MM_hndl_t HOBKL_get_mm(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->mm;
}

static inline SRQM_hndl_t HOBKL_get_srqm(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->srqm;
}

static inline QPM_hndl_t HOBKL_get_qpm(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->qpm;
}


static inline EM_hndl_t HOBKL_get_em(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->em;
}


static inline HH_hca_hndl_t HOBKL_get_hh_hndl(HOBKL_hndl_t hca_hndl)
{
  return hca_hndl->hh;
}





VIP_ret_t HOBKL_get_hca_ul_info(HOBKL_hndl_t hob, HH_hca_dev_t *hca_ul_info_p);
VIP_ret_t HOBKL_get_hca_id(HOBKL_hndl_t hca_hndl, VIP_hca_id_t* hca_id_p);

VIP_ret_t HOBKL_alloc_ul_resources(HOBKL_hndl_t hca_hndl, 
                                   MOSAL_protection_ctx_t usr_prot_ctx, 
                                   void * hca_ul_resources_p,
                                   EM_async_ctx_hndl_t *async_hndl_ctx_p);

VIP_ret_t HOBKL_free_ul_resources(HOBKL_hndl_t hca_hndl, 
                                  void * hca_ul_resources_p,
                                  EM_async_ctx_hndl_t async_hndl_ctx);


#endif
