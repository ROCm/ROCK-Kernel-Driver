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

  $Id: ts_ib_tavor_provider.h,v 1.3 2004/03/04 02:10:05 roland Exp $
*/

#ifndef _TS_IB_TAVOR_PROVIDER_H
#define _TS_IB_TAVOR_PROVIDER_H

#include "ts_ib_core_types.h"

typedef struct tTS_IB_TAVOR_PD_PARAM_STRUCT tTS_IB_TAVOR_PD_PARAM_STRUCT,
  *tTS_IB_TAVOR_PD_PARAM;
typedef struct tTS_IB_TAVOR_QP_CREATE_PARAM_STRUCT tTS_IB_TAVOR_QP_CREATE_PARAM_STRUCT,
  *tTS_IB_TAVOR_QP_CREATE_PARAM;

struct tTS_IB_TAVOR_PD_PARAM_STRUCT {
  int special_qp;               /* if non-zero, PD is for special QPs */
};

/* We make vapi_k_handle a void * so this file does not have to
   include vapi_types.h.  This saves any consumers from having to deal
   with include paths, etc. */
struct tTS_IB_TAVOR_QP_CREATE_PARAM_STRUCT {
  void            *vapi_k_handle; /* VAPI_k_qp_hndl_t for user QP */
  tTS_IB_QPN       qpn;           /* queue pair number being registered */
  tTS_IB_QP_STATE  qp_state;      /* state of user QP being registered */
};

#endif /* _TS_IB_TAVOR_PROVIDER_H */
