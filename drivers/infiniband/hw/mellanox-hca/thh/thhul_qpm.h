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

#ifndef H_THHUL_QPM_H
#define H_THHUL_QPM_H

#include <mtl_common.h>
#include <hhul.h>
#include <thhul.h>

/* The value of completed WQEs 32 ls-bits we return from THHUL_qpm_comp_ok/error 
 * in case of end of WQEs chain - for synchronizing flush-error CQE recycling flow.
 * we use 1 since 0 is a valid value while 1 is not - it is not aligned to 64B */ 
#define THHUL_QPM_END_OF_WQE_CHAIN 1


HH_ret_t THHUL_qpm_create( 
  /*IN*/ THHUL_hob_t hob, 
  /*IN*/ THHUL_srqm_t srqm,
  /*OUT*/ THHUL_qpm_t *qpm_p 
);


HH_ret_t THHUL_qpm_destroy( 
  /*IN*/ THHUL_qpm_t qpm 
);


HH_ret_t THHUL_qpm_create_qp_prep( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_init_attr_t *qp_init_attr_p, 
  /*OUT*/ HHUL_qp_hndl_t *qp_hndl_p, 
  /*OUT*/ VAPI_qp_cap_t *qp_cap_out_p, 
  /*OUT*/ void/*THH_qp_ul_resources_t*/ *qp_ul_resources_p 
);

HH_ret_t THHUL_qpm_special_qp_prep( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ VAPI_special_qp_t qp_type, 
  /*IN*/ IB_port_t port, 
  /*IN*/ HHUL_qp_init_attr_t *qp_init_attr_p, 
  /*OUT*/ HHUL_qp_hndl_t *qp_hndl_p, 
  /*OUT*/ VAPI_qp_cap_t *qp_cap_out_p, 
  /*OUT*/ void/*THH_qp_ul_resources_t*/ *qp_ul_resources_p 
);


HH_ret_t THHUL_qpm_create_qp_done( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp, 
  /*IN*/ IB_wqpn_t hh_qp, 
  /*IN*/ void/*THH_qp_ul_resources_t*/ *qp_ul_resources_p
);


HH_ret_t THHUL_qpm_destroy_qp_done( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp 
);

HH_ret_t THHUL_qpm_modify_qp_done( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp, 
  /*IN*/ VAPI_qp_state_t cur_state 
);


HH_ret_t THHUL_qpm_post_send_req( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp, 
  /*IN*/ VAPI_sr_desc_t *send_req_p 
);

HH_ret_t THHUL_qpm_post_inline_send_req( 
   /*IN*/ HHUL_hca_hndl_t hca, 
   /*IN*/ HHUL_qp_hndl_t hhul_qp, 
   /*IN*/ VAPI_sr_desc_t *send_req_p 
);

HH_ret_t THHUL_qpm_post_send_reqs( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp, 
  /*IN*/ u_int32_t num_of_requests,
  /*IN*/ VAPI_sr_desc_t *send_req_array 
);

HH_ret_t THHUL_qpm_post_gsi_send_req( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_qp, 
   VAPI_sr_desc_t *send_req_p,
   VAPI_pkey_ix_t pkey_index
);

HH_ret_t THHUL_qpm_post_recv_req( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp, 
  /*IN*/ VAPI_rr_desc_t *recv_req_p 
);

HH_ret_t THHUL_qpm_post_recv_reqs( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_qp_hndl_t hhul_qp, 
  /*IN*/ u_int32_t num_of_requests,
  /*IN*/ VAPI_rr_desc_t *recv_req_array 
);

HH_ret_t THHUL_qpm_post_bind_req(
  /*IN*/ HHUL_mw_bind_t *bind_props_p,
  /*IN*/ IB_rkey_t new_rkey
);


HH_ret_t THHUL_qpm_comp_ok( 
  /*IN*/ THHUL_qpm_t qpm, 
  /*IN*/ IB_wqpn_t qpn, 
  /*IN*/ u_int32_t wqe_addr_32lsb,
  /*OUT*/ VAPI_special_qp_t *qp_type_p,
  /*OUT*/ IB_ts_t *qp_ts_type_p,
  /*OUT*/ VAPI_wr_id_t *wqe_id_p,
  /*OUT*/ u_int32_t *wqes_released_p
#ifdef IVAPI_THH
   , u_int32_t *reserved_p
#endif 
);


HH_ret_t THHUL_qpm_comp_err( 
  /*IN*/ THHUL_qpm_t qpm, 
  /*IN*/ IB_wqpn_t qpn, 
  /*IN*/ u_int32_t wqe_addr_32lsb, 
  /*OUT*/ VAPI_wr_id_t *wqe_id_p,
  /*OUT*/ u_int32_t *wqes_released_p, 
  /*OUT*/ u_int32_t *next_wqe_32lsb_p,
  /*OUT*/ u_int8_t  *dbd_bit_p
);

VAPI_cqe_num_t THHUL_qpm_wqe_cnt( 
  /*IN*/THHUL_qpm_t qpm, 
  /*IN*/IB_wqpn_t qpn, 
  /*IN*/u_int32_t wqe_addr_32lsb, 
  /*IN*/u_int16_t dbd_cnt
);
#endif /* H_THHUL_QPM_H */
