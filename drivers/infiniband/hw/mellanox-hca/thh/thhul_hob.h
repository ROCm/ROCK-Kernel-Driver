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

#ifndef H_THHUL_HOB_H
#define H_THHUL_HOB_H

#include <mtl_common.h>
#include <hhul.h>
#include <thhul.h>


HH_ret_t THHUL_hob_create(
  /*IN*/ void/*THH_hca_ul_resources_t*/ *hca_ul_resources_p, 
  /*IN*/ u_int32_t	    device_id,
  /*OUT*/ HHUL_hca_hndl_t *hca_p 
);


HH_ret_t THHUL_hob_destroy(/*IN*/ HHUL_hca_hndl_t hca);


HH_ret_t THHUL_hob_query_version(
  /*IN*/ THHUL_hob_t hob, 
  /*OUT*/ THH_ver_info_t *version_p 
);

HH_ret_t THHUL_hob_get_hca_ul_handle(
  /*IN*/ THHUL_hob_t 		hob,
  /*OUT*/ HHUL_hca_hndl_t 	*hca_ul_p
);

HH_ret_t THHUL_hob_get_hca_ul_res(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THH_hca_ul_resources_t *hca_ul_res_p
);


HH_ret_t THHUL_hob_get_pdm(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THHUL_pdm_t *pdm_p
);


HH_ret_t THHUL_hob_get_cqm (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THHUL_cqm_t *cqm_p
);


HH_ret_t THHUL_hob_get_qpm (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THHUL_qpm_t *qpm_p
);

HH_ret_t THHUL_hob_get_srqm (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THHUL_srqm_t *srqm_p
);

HH_ret_t THHUL_hob_get_uar (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THH_uar_t *uar_p
);

HH_ret_t THHUL_hob_get_mwm (
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ THHUL_mwm_t *mwm_p
);

HH_ret_t THHUL_hob_is_priv_ud_av(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*OUT*/ MT_bool *is_priv_ud_av_p
);



#endif
