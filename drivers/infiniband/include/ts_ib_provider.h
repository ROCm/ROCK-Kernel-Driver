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

  $Id: ts_ib_provider.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_PROVIDER_H
#define _TS_IB_PROVIDER_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../core,core_export.ver)
#endif

#include "ts_ib_provider_types.h"

#define TS_IB_DEVICE_IS_SWITCH(device) (NULL != (device)->switch_ops)

int ib_device_register  (struct ib_device *device);
int ib_device_deregister(struct ib_device *device);

void ib_completion_event_dispatch(struct ib_cq *cq);
void ib_async_event_dispatch(struct ib_async_event_record *event_record);

static inline tTS_IB_DEVICE_HANDLE ib_device_to_handle(struct ib_device *device)
{
  return device;
}

static inline tTS_IB_QP_HANDLE ib_qp_to_handle(struct ib_qp *qp)
{
  return qp;
}

static inline tTS_IB_CQ_HANDLE ib_cq_to_handle(struct ib_cq *cq)
{
  return cq;
}

static inline struct ib_pd *ib_pd_from_handle(tTS_IB_PD_HANDLE pd_handle)
{
  return pd_handle;
}

static inline struct ib_cq *ib_cq_from_handle(tTS_IB_CQ_HANDLE cq_handle)
{
  return cq_handle;
}

static inline struct ib_address *ib_address_from_handle(tTS_IB_ADDRESS_HANDLE address_handle)
{
  return address_handle;
}

/* Defines to support legacy code -- don't use the tsIb names in new code. */
#define tsIbDeviceRegister               ib_device_register
#define tsIbDeviceDeregister             ib_device_deregister
#define tsIbCompletionEventDispatch      ib_completion_event_dispatch
#define tsIbAsyncEventDispatch           ib_async_event_dispatch
#define tsIbDeviceToHandle               ib_device_to_handle
#define tsIbQpToHandle                   ib_qp_to_handle
#define tsIbCqToHandle                   ib_cq_to_handle
#define tsIbPdFromHandle                 ib_pd_from_handle
#define tsIbCqFromHandle                 ib_cq_from_handle
#define tsIbAddressFromHandle            ib_address_from_handle

#endif /* _TS_IB_PROVIDER_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
