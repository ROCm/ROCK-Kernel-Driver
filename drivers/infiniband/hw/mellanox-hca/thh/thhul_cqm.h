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

#ifndef H_THHUL_CQM_H
#define H_THHUL_CQM_H

#include <mtl_common.h>
#include <hhul.h>
#include <thhul.h>


HH_ret_t THHUL_cqm_create( 
  /*IN*/ THHUL_hob_t  hob, 
  /*OUT*/ THHUL_cqm_t *cqm_p 
);

HH_ret_t THHUL_cqm_destroy (
  /*IN*/ THHUL_cqm_t cqm
);


HH_ret_t THHUL_cqm_create_cq_prep(
  /*IN*/  HHUL_hca_hndl_t hca, 
  /*IN*/  VAPI_cqe_num_t  num_o_cqes, 
  /*OUT*/ HHUL_cq_hndl_t  *hhul_cq_p, 
  /*OUT*/ VAPI_cqe_num_t  *num_o_cqes_p, 
  /*OUT*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
);


HH_ret_t THHUL_cqm_create_cq_done(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_cq_hndl_t hhul_cq, 
  /*IN*/ HH_cq_hndl_t hh_cq, 
  /*IN*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
);


HH_ret_t THHUL_cqm_destroy_cq_done(
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq 
);


HH_ret_t THHUL_cqm_resize_cq_prep(
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cqe_num_t num_o_cqes, 
  /*OUT*/ VAPI_cqe_num_t *num_o_cqes_p, 
  /*OUT*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
);


HH_ret_t THHUL_cqm_resize_cq_done( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ void/*THH_cq_ul_resources_t*/ *cq_ul_resources_p 
);


HH_ret_t THHUL_cqm_cq_cleanup( 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ IB_wqpn_t qp,
  /*IN*/ THHUL_srqm_t srqm,
  /*IN*/ HHUL_srq_hndl_t srq
);


HH_ret_t THHUL_cqm_poll4cqe( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*OUT*/ VAPI_wc_desc_t *vapi_cqe_p 
);

HH_ret_t THHUL_cqm_peek_cq( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cqe_num_t cqe_num
);

HH_ret_t THHUL_cqm_req_comp_notif( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cq_notif_type_t notif_type 
);

HH_ret_t THHUL_cqm_req_ncomp_notif( 
  /*IN*/ HHUL_hca_hndl_t hca_hndl, 
  /*IN*/ HHUL_cq_hndl_t cq, 
  /*IN*/ VAPI_cqe_num_t cqe_num 
) ;
#endif /* H_THHUL_CQM_H */
