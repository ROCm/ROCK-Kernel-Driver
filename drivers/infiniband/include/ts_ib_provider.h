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

  $Id: ts_ib_provider.h,v 1.5 2004/02/25 00:32:33 roland Exp $
*/

#ifndef _TS_IB_PROVIDER_H
#define _TS_IB_PROVIDER_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../core,core_export.ver)
#endif

#include "ts_ib_provider_types.h"

#define TS_IB_DEVICE_IS_SWITCH(device) (NULL != (device)->switch_ops)

int tsIbDeviceRegister(
                       tTS_IB_DEVICE device
                       );


int tsIbDeviceDeregister(
                         tTS_IB_DEVICE device
                         );

void tsIbCompletionEventDispatch(
                                 tTS_IB_CQ cq
                                 );

void tsIbAsyncEventDispatch(
                            tTS_IB_ASYNC_EVENT_RECORD event_record
                            );

static inline tTS_IB_DEVICE_HANDLE tsIbDeviceToHandle(
                                                      tTS_IB_DEVICE device
                                                      ) {
  return device;
}

static inline tTS_IB_QP_HANDLE tsIbQpToHandle(
                                              tTS_IB_QP qp
                                              ) {
  return qp;
}

static inline tTS_IB_CQ_HANDLE tsIbCqToHandle(
                                              tTS_IB_CQ cq
                                              ) {
  return cq;
}

static inline tTS_IB_PD tsIbPdFromHandle(
                                         tTS_IB_PD_HANDLE pd_handle
                                         ) {
  return pd_handle;
}

static inline tTS_IB_CQ tsIbCqFromHandle(
                                         tTS_IB_CQ_HANDLE cq_handle
                                         ) {
  return cq_handle;
}

static inline tTS_IB_ADDRESS tsIbAddressFromHandle(
                                                   tTS_IB_ADDRESS_HANDLE address_handle
                                                   ) {
  return address_handle;
}

#endif /* _TS_IB_PROVIDER_H */
