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

#ifndef H_THHUL_MWM_H
#define H_THHUL_MWM_H

#include <mtl_common.h>
#include <hhul.h>
#include <thhul.h>


HH_ret_t THHUL_mwm_create( 
  /*IN*/ THHUL_hob_t	hob,
  /*IN*/ u_int32_t		log2_mpt_size,
  /*OUT*/ THHUL_mwm_t	*mwm_p 
);


HH_ret_t THHUL_mwm_destroy( 
  /*IN*/ THHUL_mwm_t mwm 
);


HH_ret_t THHUL_mwm_alloc_mw(
  /*IN*/ HHUL_hca_hndl_t hca, 
  /*IN*/ IB_rkey_t initial_rkey,
  /*OUT*/ HHUL_mw_hndl_t*  mw_p
);


HH_ret_t THHUL_mwm_bind_mw(
  /*IN*/ HHUL_hca_hndl_t   hhul_hndl,
  /*IN*/ HHUL_mw_hndl_t    mw,
  /*IN*/ HHUL_mw_bind_t*   bind_prop_p,
  /*OUT*/ IB_rkey_t*        bind_rkey_p
);


HH_ret_t THHUL_mwm_free_mw(
  /*IN*/ HHUL_hca_hndl_t  hhul_hndl,
  /*IN*/ HHUL_mw_hndl_t   mw
);

#endif /* H_THHUL_QPM_H */
