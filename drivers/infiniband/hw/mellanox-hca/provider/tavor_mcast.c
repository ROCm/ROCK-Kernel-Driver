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

  $Id: tavor_mcast.c,v 1.4 2004/03/04 02:10:04 roland Exp $
*/

#include "tavor_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>

int tsIbTavorMulticastAttach(
                             tTS_IB_QP  qp,
                             tTS_IB_LID lid,
                             tTS_IB_GID gid
                             ) {
  tTS_IB_TAVOR_QP      priv_qp = qp->private;
  tTS_IB_TAVOR_PRIVATE priv    = qp->device->private;
  VAPI_ret_t           ret;

  ret = VAPI_attach_to_multicast(priv->vapi_handle,
                                 gid,
                                 priv_qp->qp_handle.kernel_handle,
                                 lid);

  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_attach_to_multicast failed, return code = %d (%s)",
                   qp->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}

int tsIbTavorMulticastDetach(
                             tTS_IB_QP  qp,
                             tTS_IB_LID lid,
                             tTS_IB_GID gid
                             ) {
  tTS_IB_TAVOR_QP      priv_qp = qp->private;
  tTS_IB_TAVOR_PRIVATE priv    = qp->device->private;
  VAPI_ret_t           ret;

  ret = VAPI_detach_from_multicast(priv->vapi_handle,
                                   gid,
                                   priv_qp->qp_handle.kernel_handle,
                                   lid);

  if (ret != VAPI_OK) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "%s: VAPI_detach_from_multicast failed, return code = %d (%s)",
                   qp->device->name, ret, VAPI_strerror(ret));
    return -EINVAL;
  }

  return 0;
}
