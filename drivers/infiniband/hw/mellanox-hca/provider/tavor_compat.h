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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: tavor_compat.h,v 1.2 2004/03/04 02:10:04 roland Exp $
*/

#ifndef _TAVOR_COMPAT_H
#define _TAVOR_COMPAT_H

#if !defined(EVAPI_FEATURE_ALLOC_PD_AV_SQP)
static inline VAPI_ret_t MT_API EVAPI_alloc_pd_sqp(
                                                   VAPI_hca_hndl_t      hca_hndl,
                                                   u_int32_t            max_num_avs,
                                                   VAPI_pd_hndl_t       *pd_hndl_p
                                                   ) {
  return VAPI_alloc_pd(hca_hndl, pd_hndl_p);
}
#endif /* EVAPI_FEATURE_ALLOC_PD_AV_SQP */

#if defined(EVAPI_FEATURE_LOCAL_MAD_SLID)
#  define EVAPI_LOCAL_MAD_SLID(slid) slid,
#else
#  define EVAPI_LOCAL_MAD_SLID(slid)
#endif

#if defined(EVAPI_FEATURE_PROC_MAD_OPTS)
#  define EVAPI_PROC_MAD_OPTS(opts) opts,
#else
#  define EVAPI_PROC_MAD_OPTS(opts)
#endif

#endif /* _TAVOR_PRIV_H */
