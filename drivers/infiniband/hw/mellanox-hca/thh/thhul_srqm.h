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

#ifndef H_THHUL_SRQM_H
#define H_THHUL_SRQM_H

#include <thhul.h>


HH_ret_t THHUL_srqm_create( 
  THHUL_hob_t hob, 
  THHUL_srqm_t *srqm_p 
);


HH_ret_t THHUL_srqm_destroy( 
   THHUL_srqm_t srqm 
); 

HH_ret_t THHUL_srqm_create_srq_prep( 
  /*IN*/
  HHUL_hca_hndl_t hca, 
  HHUL_pd_hndl_t  pd,
  u_int32_t max_outs,
  u_int32_t max_sentries,
  /*OUT*/
  HHUL_srq_hndl_t *srq_hndl_p,
  u_int32_t *actual_max_outs_p,
  u_int32_t *actual_max_sentries_p,
  void /*THH_srq_ul_resources_t*/ *srq_ul_resources_p 
);

HH_ret_t THHUL_srqm_create_srq_done( 
  HHUL_hca_hndl_t hca, 
  HHUL_srq_hndl_t hhul_srq, 
  HH_srq_hndl_t hh_srq, 
  void/*THH_srq_ul_resources_t*/ *srq_ul_resources_p
);

HH_ret_t THHUL_srqm_destroy_srq_done( 
   HHUL_hca_hndl_t hca, 
   HHUL_qp_hndl_t hhul_srq 
);

HH_ret_t THHUL_srqm_post_recv_reqs(
                                 /*IN*/ HHUL_hca_hndl_t hca, 
                                 /*IN*/ HHUL_srq_hndl_t hhul_srq, 
                                 /*IN*/ u_int32_t num_of_requests,
                                 /*IN*/ VAPI_rr_desc_t *recv_req_array,
                                 /*OUT*/ u_int32_t *posted_requests_p
                                 );

/* Release this WQE only and return its WQE ID */
HH_ret_t THHUL_srqm_comp( 
  THHUL_srqm_t srqm, 
  HHUL_srq_hndl_t hhul_srq,
  u_int32_t wqe_addr_32lsb, 
  VAPI_wr_id_t *wqe_id_p
);

#endif
