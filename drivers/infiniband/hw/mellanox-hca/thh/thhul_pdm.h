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

#ifndef H_THHUL_PDM_H
#define H_THHUL_PDM_H

#include <mtl_common.h>
#include <hh.h>
#include <hhul.h>
#include <thhul.h>

#define  THHUL_PDM_MAX_UL_UDAV_PER_PD   256

HH_ret_t THHUL_pdm_create (
  /*IN*/ THHUL_hob_t hob, 
  /*IN*/ MT_bool priv_ud_av, 
  /*OUT*/ THHUL_pdm_t *pdm_p
);

HH_ret_t THHUL_pdm_destroy (/*IN*/ THHUL_pdm_t pdm);

HH_ret_t THHUL_pdm_alloc_pd_prep (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ HHUL_pd_hndl_t *pd_p, 
  /*OUT*/ void/*THH_pd_ul_resources_t*/ *pd_ul_resources_p 
);


HH_ret_t THHUL_pdm_alloc_pd_done (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_pd_hndl_t hhul_pd, 
  /*IN*/ HH_pd_hndl_t hh_pd, 
  /*IN*/ void/*THH_pd_ul_resources_t*/ *pd_ul_resources_p 
);

HH_ret_t THHUL_pdm_free_pd_prep (
  /*IN*/HHUL_hca_hndl_t hca, 
  /*IN*/HHUL_pd_hndl_t hhul_pd,
  /*IN*/MT_bool        undo_flag 
);

HH_ret_t THHUL_pdm_free_pd_done (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_pd_hndl_t hhul_pd 
);


HH_ret_t THHUL_pdm_create_ud_av ( 
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_pd_hndl_t hhul_pd, 
  /*IN*/ VAPI_ud_av_t *av_p, 
  /*OUT*/ HHUL_ud_av_hndl_t *ah_p
);


HH_ret_t THHUL_pdm_modify_ud_av (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_ud_av_hndl_t ah, 
  /*IN*/ VAPI_ud_av_t *av_p 
);


HH_ret_t THHUL_pdm_query_ud_av (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_ud_av_hndl_t ah, 
  /*OUT*/ VAPI_ud_av_t *av_p 
);


HH_ret_t THHUL_pdm_destroy_ud_av (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ HHUL_ud_av_hndl_t ah 
);


HH_ret_t THHUL_pdm_get_hh_pd(
  /*IN*/ THHUL_pdm_t  pdm,
  /*IN*/ HHUL_pd_hndl_t hhul_pd,
  /*OUT*/ HH_pd_hndl_t  *hh_pd_p
);


HH_ret_t THHUL_pdm_get_ud_av_memkey_sqp_ok(
  /*IN*/ THHUL_pdm_t  pdm,
  /*IN*/ HHUL_pd_hndl_t hhul_pd,
  /*OUT*/MT_bool *ok_for_sqp,
  /*OUT*/ VAPI_lkey_t *ud_av_memkey_p
);

HH_ret_t THHUL_pdm_alloc_pd_avs_prep (
   /*IN*/ HHUL_hca_hndl_t hca, 
   /*IN*/ u_int32_t max_num_avs,
   /*IN*/ HH_pdm_pd_flags_t pd_flags,
   /*IN*/ HHUL_pd_hndl_t *pd_p, 
   /*OUT*/ void *pd_ul_resources_p 
);


#endif /* H_THHUL_PDM_H */
